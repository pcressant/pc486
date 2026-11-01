"""Port Synthé3 autonome : provenance, génération par blocs et fidélité au moteur copié.

Aucun accès à une VM ou une image disque. Les bancs sont compilés dans /tmp.
"""
import array
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'synthe3'
sys.path.insert(0, str(SOURCE))
import corpus  # noqa: E402  outil hôte synthe3/corpus.py : mesures PCM
WATCOM = Path(os.environ.get('WATCOM', ROOT / 'tools/watcom')).resolve()


class ProvenanceTests(unittest.TestCase):
    def test_vendor_files_are_unchanged(self):
        manifest = json.loads((SOURCE / 'vendor/provenance.json').read_text())
        for source, expected in manifest['files'].items():
            with self.subTest(source=source):
                self.assertEqual(hashlib.sha256(
                    (SOURCE / 'vendor' / Path(source).name).read_bytes()).hexdigest(), expected)

    def test_generated_windows_match(self):
        import re
        c = (SOURCE / 'src/window.h').read_text().split('{', 1)[1].split('}', 1)[0]
        asm = (SOURCE / 'src/window.inc').read_text().split('window_q15 label word', 1)[1]
        values = [round((1-math.cos(math.pi*i/256))*16384) for i in range(257)]
        self.assertEqual(list(map(int, re.findall(r'\d+', c))), values)
        self.assertEqual(list(map(int, re.findall(r'\d+', asm))), values)

    def test_cp850_input_table(self):
        import re
        values = list(map(int, re.findall(r'\d+', (SOURCE / 'src/cp850.inc').read_text())))
        expected = []
        for byte in range(128, 256):
            try:
                expected.append(bytes([byte]).decode('cp850').encode('latin1')[0])
            except UnicodeError:
                expected.append(32)
        self.assertEqual(values, expected)

    def test_fixed_engine_volume_is_unity(self):
        # Le noyau ASM remplace le gain flottant fixe du moteur au volume 10.
        import re
        data = (SOURCE / 'vendor/voice_data.h').read_text()
        body = re.search(r'voice_tables\[\] = \{(.*?)\};', data, re.S).group(1)
        table = bytes(map(int, re.findall(r'\d+', body)))
        offset = int.from_bytes(table[26:28], 'little')
        self.assertEqual(int.from_bytes(table[offset+20:offset+22], 'little'), 1000)


@unittest.skipUnless((WATCOM / 'binl64/wcl386').exists(), 'Open Watcom absent')
class EngineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='pc486-synthe3-')
        cls.build = Path(cls.temporary.name)
        env = dict(os.environ, WATCOM=str(WATCOM), INCLUDE=str(WATCOM / 'lh'),
                   PATH=f'{WATCOM / "binl64"}:{os.environ.get("PATH", "")}')
        programs = {
            'port': ['src/engine.c', 'src/kernels.c', 'src/weights.c', 'src/main.c'],
            'baseline': ['src/engine.c', 'src/kernels.c', 'src/weights.c', 'src/main.c'],
            'reference': ['vendor/engine.c', 'tests/reference.c'],
            'stream': ['src/engine.c', 'src/kernels.c', 'src/weights.c', 'tests/stream.c'],
            'check': ['src/kernels.c', 'tests/check.c'],
            'weights': ['src/kernels.c', 'src/weights.c', 'tests/weights-check.c'],
            'block': ['src/engine.c', 'src/kernels.c', 'src/weights.c', 'src/block.c', 'src/main.c'],
            'block-check': ['src/kernels.c', 'src/weights.c', 'src/block.c', 'tests/block-check.c'],
            'integer': ['src/engine.c', 'src/kernels.c', 'src/weights.c', 'src/block.c', 'src/main.c'],
            'cache': ['src/engine.c', 'src/kernels.c', 'src/weights.c', 'src/block.c', 'src/main.c'],
            # Variante de production des lecteurs (S3RES.EXE, EPAUS3.DLL) et son Apollo local.
            'apollo': ['src/engine.c', 'src/kernels.c', 'src/weights.c', 'src/block.c',
                       'src/ap.c', 'tests/ap-test.c'],
        }
        try:
            for name, files in programs.items():
                command = ['wcl386', '-q', '-bt=linux', '-ecc', '-j', '-za99', '-wx', '-we',
                           '-wcd=202', '-wcd=303', f'-i={SOURCE / "src"}',
                           f'-fe={cls.build / name}', f'-fo={cls.build}/']
                if name == 'baseline':
                    command.append('-DS3_SCALAR_WINDOW')
                if name == 'block':
                    command.append('-DS3_BLOCK')
                if name in ('integer', 'apollo'):
                    command += ['-DS3_BLOCK', '-DS3_INTEGER_PERIOD']
                if name == 'cache':
                    command += ['-DS3_BLOCK', '-DS3_INTEGER_PERIOD', '-DS3_WINDOW_CACHE=16']
                result = subprocess.run(command + [str(SOURCE / f) for f in files],
                                        cwd=cls.build, env=env, capture_output=True)
                if result.returncode:
                    raise AssertionError(result.stdout.decode('latin1') + result.stderr.decode('latin1'))
        except BaseException:
            cls.temporary.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_program(self, name, *arguments):
        result = subprocess.run([str(self.build / name), *map(str, arguments)],
                                cwd=self.build, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_stream_boundaries_cancellation_and_invalid_input(self):
        self.assertIn(b'stream OK', self.run_program('stream'))

    def test_local_apollo_protocol(self):
        # Ctrl-X, marques @E libérées à la position jouée, fragmentation, découpage.
        self.assertIn(b'apollo OK', self.run_program('apollo'))

    def test_integer_kernel_vectors(self):
        # Également produit par CHECKASM.EXE et CHECKC.EXE dans la vraie VM DOS.
        self.assertEqual(self.run_program('check').strip(), b'2c68596f 5eb7612d')

    def test_prepared_windows_are_exact(self):
        self.assertIn(b'weights OK', self.run_program('weights'))

    def test_block_kernels_match_scalar_kernels(self):
        # Également produit par CHKBLKC.EXE (C) et CHKBLKA.EXE (assembleur) sous DOS.
        self.assertIn(b'block c OK', self.run_program('block-check'))

    def test_pcm_against_original_engine(self):
        for text, rate, pitch in [('Bonjour Michel.', 3, 5),
                                  ('café, élève, fête.', 3, 5),
                                  ('Le prix est de 12,50 francs.', 3, 5),
                                  ('a' * 240, 3, 5),
                                  ('Bonjour.', 0, 0), ('Bonjour.', 15, 15)]:
            input_file = self.build / 'input.txt'
            reference = self.build / 'reference.raw'
            input_file.write_bytes(text.encode('latin1'))
            self.run_program('reference', input_file, reference, rate, pitch)
            expected = array.array('h', reference.read_bytes())
            for name in ('port', 'integer'):
                with self.subTest(text=text[:40], rate=rate, pitch=pitch, variant=name):
                    output = self.build / f'{name}.wav'
                    self.run_program(name, '-l', '-f', input_file, '-o', output,
                                     '-r', rate, '-p', pitch)
                    with wave.open(str(output)) as wav:
                        self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()),
                                         (1, 2, 10000))
                        actual = array.array('h', wav.readframes(wav.getnframes()))
                    self.assertGreater(len(actual), 0)
                    if name == 'integer' and len(actual) != len(expected):
                        # Égalité exacte tranchée autrement (voir test_integer_period_differs_only_at_ties).
                        self.assertLessEqual(abs(len(actual) - len(expected)), 255)
                        continue
                    self.assertEqual(len(actual), len(expected))
                    noise = sum((a-b)**2 for a, b in zip(actual, expected))
                    signal = sum(b*b for b in expected)
                    snr = 10*math.log10(signal/max(noise, 1))
                    self.assertGreater(snr, 45, f'SNR={snr:.2f} dB')
                    self.assertLess(max(abs(a-b) for a, b in zip(actual, expected)), 100)

    def test_dos_codepage_conversion(self):
        input_file = self.build / 'input.txt'
        output = self.build / 'port.wav'
        input_file.write_bytes('élève et café'.encode('cp850'))
        self.run_program('port', '-f', input_file, '-o', output)
        oem = output.read_bytes()
        input_file.write_bytes('élève et café'.encode('latin1'))
        self.run_program('port', '-l', '-f', input_file, '-o', output)
        self.assertEqual(oem, output.read_bytes())

    def exact_cases(self):
        import random
        rng = random.Random(486)
        cases = [('Bonjour Michel. Un élève prend un café.', 3, 5),
                 ('12,50 francs; C:\\DOS\\CONFIG.SYS !', 3, 5),
                 ('é è ê ë à ù ç', 3, 5), ('a' * 240, 3, 5)]
        cases += [('Bonjour, comment allez-vous ?', rate, pitch)
                  for rate in (0, 3, 9, 15) for pitch in (0, 5, 8, 15)]
        cases += [(' '.join(rng.choice(['bonjour', 'élève', 'Michel', '123', 'café', '.', '?'])
                            for _ in range(20)), rng.randrange(16), rng.randrange(16)) for _ in range(12)]
        return cases

    def render(self, name, text, rate, pitch):
        input_file, output = self.build / 'input.txt', self.build / f'{name}.wav'
        input_file.write_bytes(text.encode('latin1'))
        self.run_program(name, '-l', '-f', input_file, '-o', output, '-r', rate, '-p', pitch)
        return output.read_bytes()

    def test_pcm_is_bit_identical_to_scalar_port(self):
        # Fenêtres préparées (port) et noyaux par période (block) contre le scalaire.
        for text, rate, pitch in self.exact_cases():
            with self.subTest(text=text[:40], rate=rate, pitch=pitch):
                expected = self.render('baseline', text, rate, pitch)
                self.assertEqual(self.render('port', text, rate, pitch), expected)
                self.assertEqual(self.render('block', text, rate, pitch), expected)

    def test_integer_period_differs_only_at_ties(self):
        # Sans x87 : même PCM sauf aux égalités exactes d'arrondi (demi-période,
        # passage par zéro de la position), où le rationnel exact tranche autrement
        # que le flottant. Mesuré : au plus une période de décalage, aucun écrêtage ajouté.
        identical = 0
        cases = self.exact_cases() + [(t, 3, 5) for t in corpus.PHRASES]
        for text, rate, pitch in cases:
            with self.subTest(text=text[:40], rate=rate, pitch=pitch):
                reference = self.render('port', text, rate, pitch)
                integer = self.render('integer', text, rate, pitch)
                self.assertEqual(self.render('cache', text, rate, pitch), integer)
                a = array.array('h', reference[44:])
                b = array.array('h', integer[44:])
                result = corpus.compare(a, b)
                identical += result['identique']
                self.assertLessEqual(abs(result['ecart_echantillons']), 255)
                self.assertEqual(corpus.stats(b)['ecretes'], corpus.stats(a)['ecretes'])
                if result['ecart_echantillons'] == 0 and not result['identique']:
                    self.assertGreater(result['snr_db'], 30)
        self.assertGreaterEqual(identical, len(cases) * 9 // 10, identical)
