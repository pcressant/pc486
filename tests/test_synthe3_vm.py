"""PC486_S3_TESTS=1 : port DOS et vrai assembleur, sur copie privée, audio vers WAV.

Pas d'Épaulard, d'Épaudos, de D: partagé, de parole hôte ni de plage physique.
La source n'est copiée qu'arrêtée, sous le verrou du projet. Aucun remplacement.
"""
import array
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.environ.get('PC486_S3_TESTS') == '1', 'PC486_S3_TESTS=1')
class Synthe3VMTests(unittest.TestCase):
    def test_dos_assembly_and_sound_blaster(self):
        source = Path(os.environ.get('PC486_S3_SOURCE', ROOT / 'vm/win311.img')).resolve()
        if not source.is_file():
            self.skipTest('disque DOS source absent')
        subprocess.run(['bash', 'synthe3/build.sh'], cwd=ROOT, check=True, capture_output=True)
        base = ROOT / 'work/vmtest/synthe3'
        base.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix='essai-', dir=base))
        disk = work / 'dos.img'
        # Le verrou refuse aussi un émulateur ouvert hors des lanceurs du projet.
        subprocess.run(['python3', str(ROOT / 'scripts/with-disk-lock.py'), str(source), '--',
                        'cp', '--reflink=auto', '--sparse=always', str(source), str(disk)], check=True)
        image = f'{disk}@@32256'
        autoexec = work / 'AUTOEXEC.BAT'
        autoexec.write_bytes(
            b'@ECHO OFF\r\nSET BLASTER=A220 I5 D1 H5 T6\r\n'
            b'C:\\CHECKASM.EXE > C:\\ASM.TXT\r\nIF ERRORLEVEL 1 GOTO ERREUR\r\n'
            b'C:\\CHECKC.EXE > C:\\C.TXT\r\nIF ERRORLEVEL 1 GOTO ERREUR\r\n'
            b'C:\\S3WEIGH.EXE > C:\\WEIGHTS.TXT\r\nIF ERRORLEVEL 1 GOTO ERREUR\r\n'
            b'C:\\S3CLOCK.EXE > C:\\CLOCK.TXT\r\nIF ERRORLEVEL 1 GOTO ERREUR\r\n'
            b'C:\\S3DOS.EXE -f C:\\TEXTE.TXT -o C:\\MICHEL.WAV > C:\\S3.TXT\r\n'
            b'IF ERRORLEVEL 1 GOTO ERREUR\r\n'
            b'SET BLASTER=A220 I5 D2 H5 T6\r\n'
            b'C:\\S3PLAY.EXE C:\\MICHEL.WAV > C:\\INVAL.TXT\r\n'
            b'IF NOT ERRORLEVEL 1 GOTO ERREUR\r\nSET BLASTER=A220 I5 D1 H5 T6\r\n'
            b'C:\\S3PLAY.EXE C:\\MICHEL.WAV > C:\\PLAY.TXT\r\n'
            b'IF ERRORLEVEL 1 GOTO ERREUR\r\nECHO S3_INTERROMPRE\r\n'
            b'C:\\S3PLAY.EXE C:\\MICHEL.WAV > C:\\STOP.TXT\r\n'
            b'IF NOT ERRORLEVEL 2 GOTO ERREUR\r\nIF ERRORLEVEL 3 GOTO ERREUR\r\n'
            b'C:\\S3PLAY.EXE C:\\MICHEL.WAV > C:\\REPLAY.TXT\r\n'
            b'IF ERRORLEVEL 1 GOTO ERREUR\r\nMD C:\\DIRECT\r\nCD C:\\DIRECT\r\n'
            b'C:\\S3DOS.EXE -s -f C:\\TEXTE.TXT > C:\\DIRECT.TXT\r\n'
            b'IF ERRORLEVEL 1 GOTO ERREUR\r\nIF EXIST MICHEL.WAV GOTO ERREUR\r\n'
            b'ECHO S3_CTRL\r\nC:\\S3DOS.EXE -s -f C:\\TEXTE.TXT > C:\\CTRL.TXT\r\n'
            b'IF NOT ERRORLEVEL 2 GOTO ERREUR\r\nIF ERRORLEVEL 3 GOTO ERREUR\r\n'
            b'ECHO S3_REPRENDRE\r\nPAUSE > NUL\r\n'
            b'C:\\S3DOS.EXE -s -f C:\\TEXTE.TXT > C:\\DIRECT2.TXT\r\n'
            b'IF ERRORLEVEL 1 GOTO ERREUR\r\n'
            b'C:\\S3FAULT.EXE > C:\\FAULT.TXT\r\nIF ERRORLEVEL 1 GOTO ERREUR\r\n'
            b'C:\\S3BENCH.EXE -s C:\\TEXTE.TXT > C:\\BCANCEL.TXT\r\n'
            b'IF NOT ERRORLEVEL 2 GOTO ERREUR\r\nIF ERRORLEVEL 3 GOTO ERREUR\r\n'
            b'ECHO S3_FIN\r\nGOTO FIN\r\n'
            b':ERREUR\r\nECHO S3_ERREUR\r\n:FIN\r\n')
        config = work / 'CONFIG.SYS'
        config.write_bytes(b'FILES=20\r\nBUFFERS=10\r\n')
        text = work / 'TEXTE.TXT'
        text.write_bytes('Bonjour Michel. Un élève prend un café.'.encode('cp850'))
        build = ROOT / 'work/dev/synthe3/build'
        for f in [autoexec, config, text, *[build / n for n in (
                'CHECKASM.EXE', 'CHECKC.EXE', 'S3DOS.EXE', 'S3PLAY.EXE', 'S3FAULT.EXE',
                'S3WEIGH.EXE', 'S3CLOCK.EXE', 'S3BENCH.EXE')]]:
            subprocess.run(['mcopy', '-o', '-i', image, str(f), f'::/{f.name}'], check=True)
        spec = importlib.util.spec_from_file_location('s3_vmctl', ROOT / 'scripts/vmctl.py')
        vmctl = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(vmctl)
        audio = work / 'sound-blaster.wav'
        qmp_path = work / 'qmp.sock'
        command = ['qemu-system-i386', '-machine', 'pc,accel=tcg', '-cpu', '486', '-m', '16',
                   '-display', 'none', '-monitor', 'none', '-serial', 'none', '-parallel', 'none',
                   '-net', 'none', '-no-reboot', '-qmp', f'unix:{qmp_path},server=on,wait=off',
                   '-drive', f'file={disk},format=raw,if=ide',
                   '-audiodev', f'wav,id=s3,path={audio}', '-device', 'sb16,audiodev=s3']
        q = None
        with (work / 'qemu.log').open('wb') as log:
            # Le verrou reste dans QEMU ; son seul disque est la copie privée.
            process = subprocess.Popen(['python3', str(ROOT / 'scripts/with-disk-lock.py'),
                                        str(disk), '--', *command], stdout=log, stderr=log)
            try:
                end = time.monotonic() + 120
                while not qmp_path.exists() and time.monotonic() < end:
                    if process.poll() is not None:
                        self.fail((work / 'qemu.log').read_text())
                    time.sleep(.1)
                q = vmctl.Qmp(str(qmp_path))
                screen = ''
                interrupted = False
                ctrl_sent = False
                resumed = False
                bench_interrupted = False
                while time.monotonic() < end:
                    lines, _, _ = q.screen()
                    screen = '\n'.join(lines)
                    if 'S3_FIN' in screen or 'S3_ERREUR' in screen:
                        break
                    if 'S3_INTERROMPRE' in screen and not interrupted:
                        q.keys(['esc'])
                        interrupted = True
                    if 'S3_CTRL' in screen and not ctrl_sent:
                        q.keys(['ctrl'], hold=120)
                        ctrl_sent = True
                    if 'S3_REPRENDRE' in screen and ctrl_sent and not resumed:
                        q.keys(['ret'])
                        resumed = True
                    if 'S3_BENCH_AUDIO' in screen and not bench_interrupted:
                        q.keys(['ctrl'], hold=120)
                        bench_interrupted = True
                    time.sleep(.25)
                # Le batch a fini, aucune application n'a de travail à enregistrer.
                self.assertIn('S3_FIN', screen, screen)
            finally:
                if q:
                    try:
                        q.cmd('quit')
                    except (ConnectionError, OSError):
                        pass
                if process.poll() is None:
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.terminate()
                        process.wait(timeout=5)
                if q:
                    q.f.close()
                    q.s.close()
        for name in ('ASM.TXT', 'C.TXT', 'S3.TXT', 'PLAY.TXT', 'STOP.TXT',
                     'REPLAY.TXT', 'INVAL.TXT', 'MICHEL.WAV', 'DIRECT.TXT',
                     'CTRL.TXT', 'DIRECT2.TXT', 'FAULT.TXT', 'WEIGHTS.TXT', 'CLOCK.TXT', 'BCANCEL.TXT'):
            subprocess.run(['mcopy', '-o', '-i', image, f'::/{name}', str(work / name)], check=True)
        self.assertEqual((work / 'ASM.TXT').read_text().strip(), '2c68596f 5eb7612d')
        self.assertEqual((work / 'ASM.TXT').read_bytes(), (work / 'C.TXT').read_bytes())
        self.assertIn('état 0', (work / 'S3.TXT').read_bytes().decode('cp850'))
        self.assertIn('état 0', (work / 'PLAY.TXT').read_bytes().decode('cp850'))
        self.assertIn('état 2', (work / 'STOP.TXT').read_bytes().decode('cp850'))
        self.assertIn('état 0', (work / 'REPLAY.TXT').read_bytes().decode('cp850'))
        self.assertIn('BLASTER', (work / 'INVAL.TXT').read_bytes().decode('cp850'))
        for name in ('PLAY.TXT', 'REPLAY.TXT', 'DIRECT.TXT', 'DIRECT2.TXT'):
            self.assertIn('33380 produits, 33380 joués, 0 sous-alimentations, état 0',
                          (work / name).read_bytes().decode('cp850'))
        self.assertIn('état 2', (work / 'CTRL.TXT').read_bytes().decode('cp850'))
        self.assertIn('reprise : OK', (work / 'FAULT.TXT').read_text())
        self.assertIn('weights OK', (work / 'WEIGHTS.TXT').read_text())
        self.assertIn('PIT OK', (work / 'CLOCK.TXT').read_text())
        import re
        cancel = (work / 'BCANCEL.TXT').read_text()
        values = dict(re.findall(r'(\w+)=(\d+)', cancel))
        self.assertEqual(values['status'], '2')
        self.assertGreater(int(values['submit']), 0)
        self.assertGreater(int(values['played']), 0)
        self.assertLess(int(values['stop_command']), int(values['hz']) // 10)
        self.assertEqual(values['clock_fault'], '0')
        self.assertLess(int(values['played']), 33380)
        with wave.open(str(work / 'MICHEL.WAV')) as f:
            self.assertEqual(f.getframerate(), 10000)
            self.assertGreater(f.getnframes(), 10000)
            pcm = array.array('h', f.readframes(f.getnframes()))
            self.assertGreater(max(pcm)-min(pcm), 1000)
        with wave.open(str(audio)) as f:
            raw = f.readframes(f.getnframes())
            self.assertGreater(len(set(raw)), 16, 'capture Sound Blaster silencieuse')
        print(f'\nSynthé3 DOS/ASM/Sound Blaster : {work}', flush=True)
