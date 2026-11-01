"""PC486_S3_TESTS=1 : Épaudos (Synthese=Synthe3 dans EPAUDOS.INI) parlant par S3RES (Synthé3 résident, Sound Blaster).

Copie privée du disque arrêté sous work/vmtest/s3dos/, QEMU muet sauf la Sound
Blaster, capturée dans un WAV. Aucune parole n'est attendue sur COM1 : Apolo-emu
(simulé) sert de témoin. La « parole » vérifiée est la trace des propositions
confiées au moteur (RES_INFO, lue en mémoire), les compteurs du pilote et le WAV.

PC486_S3_TESTS=1 python3 -m unittest tests.test_synthe3_dos_vm -v
"""
import array
from pathlib import Path
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
import unittest
import wave

from tests.test_vm_scenarios import FakeBaum, main_vm_busy

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from synthe3_etat import linear_memory, resident  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'work/vmtest/s3dos'
ENV = dict(os.environ, PC486_DEV_DISK=str(BASE / 'dos.img'),
           PC486_DEV_RUN=str(BASE / 'run'), PC486_DEV_LOGS=str(BASE / 'logs'),
           PC486_AUDIO=f'wav,path={BASE / "sound-blaster.wav"}')
CONFIG = (b'DEVICE=C:\\WINDOWS\\HIMEM.SYS\r\nDEVICE=C:\\DOS\\EMM386.EXE NOEMS\r\n'
          b'BUFFERS=20,0\r\nFILES=40\r\nDOS=HIGH,UMB\r\nSTACKS=9,256\r\n'
          b'COUNTRY=033,850,C:\\DOS\\COUNTRY.SYS\r\n'
          b'DEVICEHIGH=C:\\DOS\\DISPLAY.SYS CON=(EGA,,1)\r\nDEVICEHIGH=C:\\DOS\\POWER.EXE\r\n'
          b'DEVICE=C:\\WINDOWS\\IFSHLP.SYS\r\n')
AUTOEXEC = (b'@ECHO OFF\r\nPROMPT $P$G\r\nPATH C:\\DOS;C:\\WINDOWS;C:\\\r\n'
            b'SET TEMP=C:\\TEMP\r\n'
            b'MODE CON CODEPAGE PREPARE=((850) C:\\DOS\\EGA.CPI)\r\nMODE CON CODEPAGE SELECT=850\r\n'
            b'KEYB FR,,C:\\DOS\\KEYBOARD.SYS\r\nSET BLASTER=A220 I5 D1 H5 T6\r\n'
            b'C:\\EPAUDOS.COM\r\nLH C:\\S3RES.EXE\r\nMEM /C > C:\\MEM.TXT\r\n')
# Choix de Synthé3, lu par Épaudos (chargé d'abord : il attend S3RES) et par S3RES.
EPAUDOS_INI = b'Synthese=Synthe3\r\nVolume=5\r\n'


class Baum(FakeBaum):
    def listen(self, s):
        self.socket = s
        super().listen(s)


@unittest.skipUnless(os.environ.get('PC486_S3_TESTS') == '1', 'PC486_S3_TESTS=1')
class Synthe3EpaudosVMTests(unittest.TestCase):
    def test_epaudos_speaks_with_local_synthe3(self):
        if main_vm_busy():
            self.skipTest('image principale utilisée : copie refusée')
        source = ROOT / 'vm/win311.img'
        if not source.is_file():
            self.skipTest('disque DOS source absent')
        for script in ('synthe3/build.sh', 'epaudos/build.sh'):
            subprocess.run(['bash', script], cwd=ROOT, check=True, capture_output=True)
        if BASE.exists():
            shutil.rmtree(BASE)
        BASE.mkdir(parents=True)
        subprocess.run(['python3', str(ROOT / 'scripts/with-disk-lock.py'), str(source), '--',
                        'cp', '--reflink=auto', '--sparse=always', str(source), str(BASE / 'dos.img')],
                       check=True)
        img = f'{BASE / "dos.img"}@@32256'
        (BASE / 'CONFIG.SYS').write_bytes(CONFIG)
        (BASE / 'AUTOEXEC.BAT').write_bytes(AUTOEXEC)
        (BASE / 'EPAUDOS.INI').write_bytes(EPAUDOS_INI)
        for src in (BASE / 'CONFIG.SYS', BASE / 'AUTOEXEC.BAT', BASE / 'EPAUDOS.INI',
                    ROOT / 'epaudos/build/EPAUDOS.COM',
                    ROOT / 'work/dev/synthe3/build/S3RES.EXE'):
            subprocess.run(['mcopy', '-o', '-i', img, str(src), f'::/{src.name}'], check=True)
        spec = importlib.util.spec_from_file_location('s3dos_vmctl', ROOT / 'scripts/vmctl.py')
        vmctl = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(vmctl)
        self.q = None
        baum = None
        report = {}
        try:
            self.dev('pc486', 'start', '--qemu', '--headless', '--braille')
            baum = Baum(BASE / 'run/com3.sock')
            baum.start()
            self.q = vmctl.Qmp(str(BASE / 'run/qmp.sock'))
            self.wait_screen('Synthé3 résident', 90)
            self.wait_screen('C:\\>', 30)
            apollo_mark = self.apollo()
            self.trace_mark = 0
            self.wait_speech('Épaudos prêt', 15)
            first = self.info()
            self.assertEqual(first['clients'], 1, 'Épaudos non attaché à S3RES')
            self.assertEqual((first['base'], first['irq'], first['dma']), (0x220, 5, 1))
            print(f'S3RES et Épaudos (Synthé3) chargés, « Épaudos prêt » prononcé [sous-alimentations : {self.info()["underruns"]}]', flush=True)

            self.type('echo premiere ligne\n', vmctl)
            self.wait_screen('premiere ligne', 10)
            self.wait_speech('premiere ligne', 15)
            trace = self.info()['trace']
            # écho par caractère ; deux frappes lues dans le même passage se regroupent
            letters = [t for t in trace.split('|') if 1 <= len(t) <= 2 and t.isalpha()]
            self.assertGreaterEqual(len(letters), 8, f'écho clavier par caractère : {trace}')
            print(f'Écho clavier et écho écran [sous-alimentations : {self.info()["underruns"]}]', flush=True)

            self.key('insert-up')
            self.wait_speech('supérieur', 10)
            print(f'Revue : ligne du curseur [sous-alimentations : {self.info()["underruns"]}]', flush=True)

            # Lecture longue (dire tout de l'écran), coupée par Ctrl pendant la parole.
            before = self.info()
            self.key('insert-b')
            self.wait_until(lambda i: i['flags'] & 1 and i['played'] > 2048, 15, 'lecture longue')
            self.key('ctrl')
            t0 = time.monotonic()
            self.wait_until(lambda i: not i['flags'] & 1, 2, 'DSP arrêté après Ctrl')
            stopped = time.monotonic() - t0
            cut = self.info()
            self.assertGreater(cut['cut_stops'], before['cut_stops'])
            self.key('kp_8')
            self.wait_until(lambda i: i['starts'] > cut['starts'], 5, 'reprise après Ctrl')
            self.wait_speech('supérieur', 5)
            report['arret_constate_s'] = round(stopped, 3)
            print(f'Interruption ({stopped:.2f} s pour la constater) et reprise immédiate [sous-alimentations : {self.info()["underruns"]}]', flush=True)

            # Dire tout jusqu'au bout : les échos @E ne reviennent qu'après restitution.
            self.type('cls\n', vmctl)
            time.sleep(1)
            for line in ('echo un', 'echo deux', 'echo trois'):
                self.type(line + '\n', vmctl)
            time.sleep(2)
            self.key('ctrl')
            marks = self.info()['marks']
            self.key('insert-b')
            self.wait_speech('trois', 40)
            done = self.info()
            self.assertGreaterEqual(done['marks'] - marks, 3, 'échos du dire tout')
            print(f'Dire tout : lignes enchaînées par les échos joués [sous-alimentations : {self.info()["underruns"]}]', flush=True)

            self.type('edit\n', vmctl)
            self.wait_screen('Fichier', 25)
            self.key('esc')
            self.type('bonjour synthese', vmctl)
            self.wait_screen('bonjour synthese', 10)
            self.key('insert-up')
            self.wait_speech('bonjour synthese', 10)
            self.type('\nseconde ligne', vmctl)
            self.key('up')
            self.wait_speech('bonjour synthese', 10)
            self.key('down')
            self.wait_speech('seconde ligne', 10)
            self.key('alt-f')
            self.key('up')
            self.key('ret')
            self.wait_screen('Enregistrer maintenant', 10)
            self.key('tab')
            self.key('ret')
            self.wait_screen('C:\\>', 20)
            print(f'EDIT : saisie, Haut et Bas lus [sous-alimentations : {self.info()["underruns"]}]', flush=True)

            self.type('qbasic\n', vmctl)
            self.wait_screen('Fichier', 25)
            self.key('esc')
            self.type('PRINT "salut"', vmctl)
            self.key('insert-up')
            self.wait_speech('salut', 10)
            self.key('alt-f')
            self.key('up')
            self.key('ret')
            self.wait_screen('Enregistrer maintenant', 10)
            self.key('tab')
            self.key('ret')
            self.wait_screen('C:\\>', 20)
            print(f'QBasic : édition et lecture [sous-alimentations : {self.info()["underruns"]}]', flush=True)

            self.assertTrue(baum.frames, 'aucune trame braille pendant la parole')
            print(f'Braille Baum reçu en même temps [sous-alimentations : {self.info()["underruns"]}]', flush=True)

            # Épaudos /S3 n'a rien envoyé sur COM1 (sous Windows, Épaulard l'ouvre).
            self.assertEqual(self.apollo(), apollo_mark, 'parole envoyée sur COM1 en mode /S3')
            # Windows prend la carte (INT 2Fh 1605h) puis la rend (1606h).
            self.type('win\n', vmctl)
            self.wait_until(lambda i: i['windows'] == 1, 90, 'S3RES en veille sous Windows')
            self.assertFalse(self.info()['flags'], 'DMA de S3RES actif sous Windows')
            time.sleep(15)
            for _ in range(6):
                self.key('alt-f4')
                self.key('ret')
                time.sleep(3)
                if not self.info()['windows']:
                    break
            self.wait_until(lambda i: i['windows'] == 0, 20, 'retour de Windows')
            self.wait_screen('C:\\>', 20)
            self.type('echo retour\n', vmctl)
            self.wait_speech('retour|', 15)     # ligne affichée après la commande
            self.key('insert-h')
            self.wait_speech('Pav', 15)
            print(f'Windows : S3RES se tait, puis reparle au retour [sous-alimentations : {self.info()["underruns"]}]', flush=True)

            final = self.info()
            report.update({k: final[k] for k in (
                'pumps', 'busy', 'busy_max', 'starts', 'idle_stops', 'cut_stops', 'skipped',
                'stack_low', 'first_buffer', 'submit', 'first_buffer_max', 'submit_max',
                'wait_max', 'requests', 'underruns', 'propositions', 'samples', 'cuts',
                'marks', 'lost_marks', 'refused', 'clock_fault', 'segment')})
            self.assertEqual(final['refused'], 0)
            self.assertEqual(final['lost_marks'], 0)
            self.assertEqual(final['clock_fault'], 0)
            self.assertFalse(final['state'], 'S3RES : carte indisponible ou DSP en erreur')
            self.assertGreater(final['stack_low'], 256, 'pile de la pompe presque pleine')
            self.assertEqual(final['underruns'], 0, 'sous-alimentation de la Sound Blaster')
        finally:
            if self.q is not None:
                self.q.f.close()
                self.q.s.close()
            if baum is not None:
                baum.stop = True
            self.dev('pc486', 'stop', check=False)
        subprocess.run(['mcopy', '-o', '-i', img, '::/MEM.TXT', str(BASE / 'MEM.TXT')], check=True)
        mem = (BASE / 'MEM.TXT').read_bytes().decode('cp850')
        for name in ('S3RES', 'EPAUDOS'):
            row = re.search(rf'{name}\s+([\d\xa0 ]+)\s+\(\s*\d+K\)\s+([\d\xa0 ]+)\s+\(\s*\d+K\)'
                            rf'\s+([\d\xa0 ]+)\s+\(', mem)
            self.assertIsNotNone(row, mem)
            total, low, high = (int(re.sub(r'\D', '', g)) for g in row.groups())
            report[f'{name.lower()}_octets'] = {'total': total, 'conventionnelle': low, 'superieure': high}
        with wave.open(str(BASE / 'sound-blaster.wav')) as w:
            rate, channels = w.getframerate(), w.getnchannels()
            pcm = array.array('h', w.readframes(w.getnframes()))
        window = rate // 10 * channels
        active = sum(1 for i in range(0, len(pcm) - window, window)
                     if max(pcm[i:i + window]) - min(pcm[i:i + window]) > 2000)
        report['capture_s'] = round(len(pcm) / channels / rate, 1)
        report['capture_active_s'] = active / 10
        self.assertGreater(active, 50, 'capture Sound Blaster presque silencieuse')
        (BASE / 'mesures.json').write_text(json.dumps(report, indent=1, ensure_ascii=False))
        print(json.dumps(report, ensure_ascii=False), flush=True)

    # ------------------------------------------------------------------ outils
    @staticmethod
    def dev(*args, check=True):
        return subprocess.run([str(ROOT / 'scripts/pc486-dev'), *args], env=ENV, cwd=ROOT,
                              capture_output=True, text=True, check=check, timeout=120)

    def info(self):
        """État de S3RES. Première lecture sous DOS : mémoire linéaire (memsave ; LH
        place S3RES dans un UMB paginé par EMM386), puis traduction de ses pages en
        adresses physiques, qui restent valables quand Windows change la pagination."""
        if not getattr(self, 'pages', None):
            info = resident(linear_memory(self.q))
            self.assertIsNotNone(info, 'S3RES introuvable en mémoire')
            self.linear = info['dgroup'] * 16
            size = max(info['off_sb'] + 64, info['off_stats'] + 64, info['off_trace'] + 520, info['off_link'] + 64,
                       info['info_off'] + 128)
            self.span = size
            self.pages = {}
            for page in range(self.linear & ~0xfff, self.linear + size, 0x1000):
                out = self.q.cmd('human-monitor-command', **{'command-line': f'gva2gpa {page:#x}'})
                m = re.search(r'gpa: (0x[0-9a-f]+)', out)
                self.assertIsNotNone(m, out)
                self.pages[page] = int(m.group(1), 16)
            return info
        data = bytearray()
        for page in sorted(self.pages):
            data += self.q.memory(self.pages[page], 0x1000)
        start = self.linear - (self.linear & ~0xfff)
        image = bytes(self.linear) + bytes(data[start:start + self.span])
        info = resident(image)
        self.assertIsNotNone(info, 'S3RES introuvable')
        return info

    def mark(self):
        self.trace_mark = self.info()['propositions']

    def screen(self):
        return '\n'.join(self.q.screen()[0])

    def wait_screen(self, text, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            s = self.screen()
            if text in s:
                return s
            time.sleep(.5)
        self.fail(f'écran attendu {text!r} :\n{s}')

    def wait_until(self, test, timeout, what):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            i = self.info()
            if test(i):
                return i
            time.sleep(.1)
        self.fail(f'{what} : {dict((k, v) for k, v in i.items() if k != "trace")}\n{i["trace"]}')

    def wait_speech(self, text, timeout):
        """Texte confié au moteur depuis la dernière touche : les propositions
        ajoutées depuis, à la fin de la trace chronologique."""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            i = self.info()
            new = i['propositions'] - self.trace_mark
            recent = '|'.join(i['trace'].split('|')[-new - 1:]) if new > 0 else ''
            if text in recent:
                return
            time.sleep(.3)
        self.fail(f'parole attendue {text!r} :\n{i}\nÉcran :\n{self.screen()}')

    def key(self, keys):
        self.mark()
        self.q.keys(keys.split('-'))
        time.sleep(.3)

    def type(self, text, vmctl):
        self.mark()
        vmctl.type_text(self.q, text, fr=True, delay=.12)

    @staticmethod
    def apollo():
        p = BASE / 'logs/apolo-emu.log'
        text = p.read_text(errors='replace') if p.exists() else ''
        return len([l for l in text.splitlines() if 'apolo_emu.device' in l])


if __name__ == '__main__':
    unittest.main()
