"""Vrai moteur C d'Épaudos : navigation, veille et transports sans DOS."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WATCOM = Path(os.environ.get('WATCOM', ROOT / 'tools/watcom')).resolve()


@unittest.skipUnless((WATCOM / 'binl64/wcl386').exists(), 'Open Watcom absent')
class EpaudosTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(['bash', str(ROOT / 'epaudos/build.sh')], check=True,
                       capture_output=True)
        cls.tmp = tempfile.TemporaryDirectory(prefix='epaudos-native-')
        tmp = Path(cls.tmp.name)
        for name in ('core.c', 'core.h', 'br850.h', 'dialog.c', 'dialog.h'):
            (tmp / name).write_bytes((ROOT / 'epaudos/build' / name).read_bytes())
        (tmp / 'test.c').write_bytes((ROOT / 'tests/dos/core-test.c').read_text().encode('cp850'))
        cls.exe = tmp / 'bench'
        env = dict(os.environ, WATCOM=str(WATCOM), INCLUDE=str(WATCOM / 'lh'),
                   PATH=f"{WATCOM / 'binl64'}:{WATCOM / 'binl'}:{os.environ.get('PATH', '')}")
        result = subprocess.run(['wcl386', '-q', '-bt=linux', '-l=linux', '-za99',
                                 '-wx', '-we', '-wcd=202', '-wcd=303', 'test.c',
                                 f'-fe={cls.exe}'], cwd=tmp, env=env, capture_output=True)
        if result.returncode:
            raise AssertionError(result.stdout.decode('latin1')+result.stderr.decode('latin1'))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_com_file_covers_the_resident(self):
        # LH dans un bloc supérieur plus grand que le fichier mais plus petit que le
        # résident : la remise à zéro du BSS écrasait le bloc suivant (QEMM sous 86Box).
        # Fichier complété : DOS ne le place que dans un bloc assez grand.
        import re
        build = ROOT / 'epaudos/build'
        end = int(re.search(r'0000:([0-9a-f]+)\s+_resident_end',
                            (build / 'EPAUDOS.MAP').read_text()).group(1), 16)
        stack = int(re.search(r'LOAD_STACK equ (\d+)', (build / 'tsr.asm').read_text()).group(1))
        size = (build / 'EPAUDOS.COM').stat().st_size
        self.assertGreaterEqual(size + 0x100, end + stack)
        self.assertLess(end + stack, 0xfffe)

    def test_scenarios(self):
        for scenario in ('review', 'menu', 'pitch', 'sleep', 'guest', 'echo', 'baum', 'frame', 'format', 'keys',
                         'preemption', 'bounded', 'scroll', 'typing', 'screen', 'word', 'wordstart',
                         'dialog', 'editdialog'):
            with self.subTest(scenario=scenario):
                result = subprocess.run([str(self.exe), scenario], capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0,
                                 result.stdout.decode('latin1')+result.stderr.decode('latin1'))


if __name__ == '__main__':
    unittest.main()
