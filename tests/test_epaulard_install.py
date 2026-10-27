"""Protection de l’installation d’Épaulard, sur fichiers temporaires uniquement."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GUARD = ROOT / 'scripts/with-disk-lock.py'
spec = importlib.util.spec_from_file_location('configure_epaulard', ROOT / 'scripts/configure-epaulard.py')
ini = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ini)


class IniTests(unittest.TestCase):
    def test_preserves_programs_sections_and_crlf(self):
        text = '[other]\r\nload=other.exe\r\n[WINDOWS]\r\n Load = clock.exe,calc.exe\r\nload="C:\\EPAULARD\\EPAULARD.EXE" C:\\EPAULARD\\EPAULARD.EXE\r\n[next]\r\nload=next.exe\r\n'
        result = ini.configure(text)
        self.assertIn('load=other.exe\r\n', result)
        self.assertIn('load=next.exe\r\n', result)
        self.assertIn('load=clock.exe calc.exe "C:\\EPAULARD\\EPAULARD.EXE"\r\n', result)
        self.assertEqual(result, ini.configure(result))

    def test_missing_key_or_section(self):
        for text in ('', '[windows]\nrun=clock.exe\n[next]\nx=y\n', '[other]\nload=x.exe\n'):
            result = ini.configure(text)
            self.assertIn('load=' + ini.PROGRAM, result)
            self.assertEqual(result, ini.configure(result))


class LockTests(unittest.TestCase):
    def test_concurrent_commands_and_release(self):
        with tempfile.TemporaryDirectory() as tmp:
            disk = Path(tmp) / 'disk.img'
            disk.touch()
            cmd = [sys.executable, str(GUARD), str(disk), '--', sys.executable, '-c']
            proc = subprocess.Popen(cmd + ['import sys; print("ready", flush=True); sys.stdin.read()'],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            try:
                self.assertEqual(proc.stdout.readline().strip(), 'ready')
                result = subprocess.run(cmd + ['pass'], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('verrouillée', result.stderr)
            finally:
                proc.communicate('')
            subprocess.run(cmd + ['pass'], check=True)

    def test_open_image_without_shared_lock(self):
        with tempfile.TemporaryDirectory() as tmp:
            disk = Path(tmp) / 'disk.img'
            disk.touch()
            with disk.open('rb'):
                result = subprocess.run([sys.executable, str(GUARD), str(disk), '--', 'true'],
                                        capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('image ouverte', result.stderr)

    def test_other_image_can_run_concurrently(self):
        with tempfile.TemporaryDirectory() as tmp:
            first, second = (Path(tmp) / name for name in ('first.img', 'second.img'))
            first.touch(); second.touch()
            with first.open('rb'):
                subprocess.run([sys.executable, str(GUARD), str(second), '--', 'true'], check=True)

    def test_86box_relative_disk(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            disk = directory / 'disk.img'
            disk.touch()
            (directory / '86box.cfg').write_text('[Hard disks]\nhdd_01_fn = disk.img\n')
            with disk.open('rb'):
                result = subprocess.run([sys.executable, str(GUARD), '--86box-dir', tmp,
                                         '--', 'true'], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('image ouverte', result.stderr)
