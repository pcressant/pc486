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

    def test_scenarios(self):
        for scenario in ('review', 'menu', 'sleep', 'guest', 'echo', 'baum', 'frame', 'format', 'keys',
                         'preemption', 'bounded', 'scroll', 'typing', 'screen', 'word', 'wordstart',
                         'dialog', 'editdialog'):
            with self.subTest(scenario=scenario):
                result = subprocess.run([str(self.exe), scenario], capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0,
                                 result.stdout.decode('latin1')+result.stderr.decode('latin1'))


if __name__ == '__main__':
    unittest.main()
