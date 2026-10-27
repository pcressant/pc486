"""Essai opt-in DOS réel : copie privée, aucun Sonolect, aucune plage physique.

PC486_DOS_TESTS=1 python3 -m unittest tests.test_epaudos_vm -v
"""
from pathlib import Path
import importlib.util
import os
import shutil
import subprocess
import time
import unittest

from tests.test_vm_scenarios import FakeBaum, main_vm_busy

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'work/vmtest/epaudos'
ENV = dict(os.environ, PC486_DEV_DISK=str(BASE / 'dos.img'),
           PC486_DEV_RUN=str(BASE / 'run'), PC486_DEV_LOGS=str(BASE / 'logs'),
           PC486_AUDIO='none')


class Baum(FakeBaum):
    def listen(self, s):
        self.socket = s
        super().listen(s)


@unittest.skipUnless(os.environ.get('PC486_DOS_TESTS') == '1', 'PC486_DOS_TESTS=1')
class EpaudosVMTests(unittest.TestCase):
    def test_dos(self):
        if main_vm_busy():
            self.skipTest('image principale utilisée : copie refusée')
        subprocess.run(['bash', 'epaudos/build.sh'], cwd=ROOT, check=True, capture_output=True)
        mapping = (ROOT / 'epaudos/build/EPAUDOS.MAP').read_text()
        BASE.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vm/win311.img', BASE / 'dos.img')
        autoexec = BASE / 'AUTOEXEC.BAT'
        autoexec.write_bytes(
            b'@ECHO OFF\r\nPROMPT $P$G\r\nPATH C:\\DOS;C:\\WINDOWS;C:\\\r\n'
            b'MODE CON CODEPAGE PREPARE=((850) C:\\DOS\\EGA.CPI)\r\n'
            b'MODE CON CODEPAGE SELECT=850\r\n'
            b'KEYB FR,,C:\\DOS\\KEYBOARD.SYS\r\nC:\\EPAUDOS.COM\r\n')
        img = f'{BASE / "dos.img"}@@32256'
        watcom = Path(os.environ.get('WATCOM', ROOT / 'tools/watcom')).resolve()
        env_build = dict(os.environ, WATCOM=str(watcom),
                         PATH=f"{watcom / 'binl64'}:{watcom / 'binl'}:{os.environ.get('PATH', '')}")
        subprocess.run(['wasm', '-q', '-0', f'-fo={BASE / "modes.o"}',
                        str(ROOT / 'tests/dos/modes.asm')], check=True, env=env_build)
        subprocess.run(['wlink', 'op', 'q', 'format', 'dos', 'com', 'name',
                        str(BASE / 'MODES.COM'), 'file', str(BASE / 'modes.o')],
                       check=True, env=env_build)
        for src, dst in ((ROOT / 'epaudos/build/EPAUDOS.COM', 'EPAUDOS.COM'),
                         (autoexec, 'AUTOEXEC.BAT'), (BASE / 'MODES.COM', 'MODES.COM')):
            subprocess.run(['mcopy', '-o', '-i', img, str(src), f'::/{dst}'], check=True)
        spec = importlib.util.spec_from_file_location('epaudos_vmctl', ROOT / 'scripts/vmctl.py')
        vmctl = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(vmctl)
        self.q = None
        self.baum = None
        self.speech_mark = len(self.speech())
        try:
            self.dev('pc486', 'start', '--qemu', '--headless', '--braille')
            self.baum = Baum(BASE / 'run/com3.sock')
            self.baum.start()
            self.q = vmctl.Qmp(str(BASE / 'run/qmp.sock'))
            self.wait_screen('Épaudos résident', 90)
            self.segment = int.from_bytes(self.q.memory(0x2f*4+2,2), 'little')
            import re
            self.debug_address = self.segment*16+int(re.search(
                r'0000:([0-9a-f]+)[+*]?\s+_ed_debug', mapping).group(1),16)
            self.windows_address = self.segment*16 + int(re.search(
                r'0000:([0-9a-f]+)\s+_ed_windows', mapping).group(1), 16)
            end_offset = int(re.search(r'0000:([0-9a-f]+)\s+_resident_end', mapping).group(1),16)
            paragraphs = int.from_bytes(self.q.memory((self.segment-1)*16+3,2), 'little')
            self.assertEqual(paragraphs, (end_offset+15)//16, 'bloc DOS non réduit')
            time.sleep(2)
            print('TSR chargé', flush=True)
            self.key('insert-h')
            self.wait_speech('Pavé', 15)
            print('Aide clavier prononcée', flush=True)
            self.key('ctrl')
            self.key('insert-e')
            self.wait_screen('Épaudos : Débit', 10)
            self.key('right')
            self.key('esc')
            self.wait_speech('Réglages enregistrés', 15)
            print('Configuration enregistrée depuis INT 28h', flush=True)
            # La revue doit laisser le texte de la commande intact.
            self.type('echo premiere ligne\n', vmctl)
            self.wait_screen('premiere ligne', 10)
            self.wait_speech('premiere ligne', 10)
            print('Écho écran : sortie DOS lue sans commande', flush=True)
            self.key('insert-kp_add')
            self.wait_speech('désactivé', 10)
            self.key('insert-kp_add')
            self.wait_speech('activé', 10)
            lines, (cy, _), _ = self.q.screen()
            target = next(i for i, line in enumerate(lines) if line.strip() == 'premiere ligne')
            self.key('insert-kp_subtract')
            self.wait_speech('Revue à plat', 10)
            for _ in range(cy-target):
                self.key('kp_7')
            self.wait_speech('premiere ligne', 10)
            self.assertTrue(self.baum.frames, 'aucune trame Baum')
            self.speech_mark = len(self.speech())
            self.baum.socket.sendall(b'\x1b\x27\x01')
            self.wait_speech("'p ", 10)
            trace = BASE / 'logs/apollo-trace.log'
            mark = trace.stat().st_size
            self.key('insert-down')
            self.wait_speech('premiere ligne', 10)
            end = time.monotonic()+12
            while time.monotonic() < end:
                acknowledgements = re.findall(r' TX [0-9a-f]{2}', trace.read_text()[mark:])
                if len(acknowledgements) >= 2:
                    break
                time.sleep(.2)
            self.assertGreaterEqual(len(acknowledgements),2, 'le dire tout ne reçoit pas les échos COM1')
            self.key('ctrl')
            print('Revue et braille Baum reçus', flush=True)
            self.key('insert-kp_subtract')
            self.type('edit\n', vmctl)
            self.wait_screen('Fichier', 25)
            self.key('esc')
            self.type('bonjour epaudos', vmctl)
            self.wait_screen('bonjour epaudos', 10)
            self.key('insert-up')
            self.wait_speech('bonjour epaudos', 10)
            print('EDIT : saisie et lecture de ligne', flush=True)
            self.type('\n', vmctl)
            for i in range(1, 25):
                self.type(f'l{i}\n', vmctl)
            time.sleep(1)
            self.key('pgup')
            time.sleep(1.5)
            expected = self.cursor_text()
            self.assertTrue(expected.startswith('l'), expected)
            self.wait_speech(expected, 10)
            print('EDIT : défilement par page lu depuis la rangée du curseur', flush=True)
            self.key('down')
            self.wait_speech(self.cursor_text(), 10)
            self.key('ctrl-end')
            time.sleep(1.5)
            self.key('up')
            self.wait_speech(self.cursor_text(), 10)
            print('EDIT : Bas et Haut lus après défilement', flush=True)
            self.key('alt-f')
            self.key('up')
            self.key('ret')
            self.wait_screen('Enregistrer maintenant', 10)
            self.key('tab')
            self.key('ret')
            self.wait_screen('C:\\>', 20)
            self.type('qbasic\n', vmctl)
            self.wait_screen('Fichier', 25)
            self.key('esc')
            self.type('PRINT "bonjour"', vmctl)
            self.key('insert-up')
            self.wait_speech('bonjour', 10)
            print('QBasic : édition et lecture', flush=True)
            self.key('alt-f')
            self.key('up')
            self.key('ret')
            self.wait_screen('Enregistrer maintenant', 10)
            self.key('tab')
            self.key('ret')
            self.wait_screen('C:\\>', 20)
            self.type('dosshell /t\n', vmctl)
            self.wait_screen('Fichier', 25)
            end = time.monotonic()+30
            while 'Lecture des informations' in self.screen() and time.monotonic() < end:
                time.sleep(.5)
            time.sleep(1)
            self.key('alt-f')
            self.key('down')
            self.wait_speech('Exécuter', 10)
            print('DOSSHELL : déplacement dans le menu annoncé', flush=True)
            self.key('esc')
            self.key('alt-f4')
            self.wait_screen('C:\\>', 15)
            self.type('modes\n', vmctl)
            self.assertEqual(self.q.screen()[2], 0x13)
            time.sleep(1)
            mark = trace.stat().st_size
            time.sleep(2)
            self.assertEqual(trace.stat().st_size, mark, 'parole pendant le mode graphique')
            self.key('ret')
            self.wait_screen('RETOUR TEXTE', 10)
            self.key('insert-h')
            self.wait_speech('Pavé', 10)
            print('Mode graphique : veille puis reprise du texte', flush=True)
            self.type('win\n', vmctl)
            end = time.monotonic()+90
            while not self.q.memory(self.windows_address, 1)[0] and time.monotonic() < end:
                time.sleep(.5)
            self.assertEqual(self.q.memory(self.windows_address, 1), b'\x01')
            time.sleep(12)
            self.assertEqual(self.q.memory(self.windows_address, 1), b'\x01')
            print('Windows réel : Épaudos en veille', flush=True)
            for _ in range(5):
                self.key('alt-f4')
                self.key('ret')
                time.sleep(3)
                if not self.q.memory(self.windows_address, 1)[0]:
                    break
            self.assertEqual(self.q.memory(self.windows_address, 1), b'\x00')
            self.wait_screen('C:\\>', 20)
            self.key('insert-h')
            self.wait_speech('Pavé', 15)
            print('Sortie de Windows : reprise d’Épaudos', flush=True)
            self.assertEqual(self.q.memory(self.segment*16+end_offset-2048,64), b'\0'*64,
                             'pile privée épuisée pendant le scénario')
        finally:
            if self.q is not None:
                self.q.f.close()
                self.q.s.close()
            if self.baum is not None:
                self.baum.stop = True
            self.dev('pc486', 'stop', check=False)

    @staticmethod
    def dev(*args, check=True):
        return subprocess.run([str(ROOT / 'scripts/pc486-dev'), *args], env=ENV,
                              cwd=ROOT, capture_output=True, text=True, check=check, timeout=120)

    def screen(self):
        return '\n'.join(self.q.screen()[0])

    def cursor_text(self):
        """Texte de la rangée du curseur, sans le cadre d'EDIT."""
        lines, (cy, _), _ = self.q.screen()
        return lines[cy].strip().strip('│║░▒▓ ')

    def wait_screen(self, text, timeout):
        end = time.monotonic()+timeout
        while time.monotonic() < end:
            s = self.screen()
            if text in s:
                return s
            time.sleep(.5)
        self.fail(f'écran attendu {text!r} :\n{s}')

    def wait_speech(self, text, timeout):
        end = time.monotonic()+timeout
        while time.monotonic() < end:
            s = self.speech()[self.speech_mark:]
            if text in s:
                return
            time.sleep(.3)
        import struct
        debug = struct.unpack('<12H', self.q.memory(self.debug_address, 24))
        self.fail(f'parole attendue {text!r} :\n{s[-2500:]}\nÉtat TSR : {debug}\nÉcran :\n{self.screen()}')

    def key(self, keys):
        self.speech_mark = len(self.speech())
        self.q.keys(keys.split('-'))
        time.sleep(.3)

    def type(self, text, vmctl):
        self.speech_mark = len(self.speech())
        vmctl.type_text(self.q, text, fr=True, delay=.12)

    @staticmethod
    def speech():
        p = BASE / 'logs/apolo-emu.log'
        return p.read_text(errors='replace') if p.exists() else ''


if __name__ == '__main__':
    unittest.main()
