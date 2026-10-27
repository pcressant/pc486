"""Essai réel BrlAPI avec un BRLTTY privé et son pilote de plage virtuelle tt."""
import os
from pathlib import Path
import pty
import select
import shutil
import socket
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
GATEWAY = ROOT / 'tools/braille/bin/pc486-braille'
BRLTTY = os.environ.get('PC486_TEST_BRLTTY', shutil.which('brltty'))


@unittest.skipUnless(GATEWAY.is_file(), 'construire tools/braille')
class RuntimeTests(unittest.TestCase):
    def test_focus_dependencies_available_without_opening_display(self):
        result = subprocess.run([str(GATEWAY), '--check'], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('BrlAPI et AT-SPI disponibles', result.stdout)


@unittest.skipUnless(GATEWAY.is_file() and BRLTTY, 'construire tools/braille et installer brltty')
class BrlApiIntegrationTests(unittest.TestCase):
    def test_cells_reach_private_virtual_display(self):
        # Choisir un port libre. BrlAPI encode le port comme un décalage depuis 4101.
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        with tempfile.TemporaryDirectory(prefix='pc486-brlapi-') as tmp:
            work = Path(tmp)
            master, slave = pty.openpty()
            self.addCleanup(os.close, master)
            self.addCleanup(os.close, slave)
            with open(work / 'brltty.log', 'wb') as log, open(work / 'gateway.log', 'wb') as gwlog:
                daemon = subprocess.Popen([BRLTTY, '-n', '-e', '-q', '-b', 'tt',
                    '-d', 'serial:' + os.ttyname(slave), '-x', 'no', '-s', 'no',
                    '-f', '/dev/null', '-W', tmp, '-P', str(work / 'pid'),
                    '-A', f'host=127.0.0.1:{port - 4101},auth=none'], stdout=log, stderr=log)
                gateway = None
                try:
                    until = time.monotonic() + 5
                    while time.monotonic() < until:
                        try:
                            with socket.create_connection(('127.0.0.1', port), timeout=.1):
                                break
                        except OSError:
                            time.sleep(.05)
                    else:
                        self.fail((work / 'brltty.log').read_text())
                    with socket.socket(socket.AF_UNIX) as server:
                        server.bind(str(work / 'com3.sock'))
                        server.listen(1)
                        server.settimeout(5)
                        gateway = subprocess.Popen([str(GATEWAY), '--connect', str(work / 'com3.sock'),
                            '--focus', 'always'], env=dict(os.environ, BRLAPI_HOST=f'127.0.0.1:{port - 4101}'),
                            stdout=gwlog, stderr=gwlog)
                        with server.accept()[0] as conn:
                            conn.settimeout(5)
                            conn.sendall(b'\x1b\x15\1')
                            reply = bytearray()
                            while len(reply) < 31:
                                part = conn.recv(31 - len(reply))
                                self.assertTrue(part, 'passerelle déconnectée')
                                reply.extend(part)
                            width = reply[2]
                            self.assertEqual(reply[:2], b'\x1b\1')
                            self.assertTrue(3 <= width <= 80)
                            # Inclure un ESC dans les points : le quatrième caractère doit arriver intact.
                            cells = (bytes((1, 3, 9, 0x1b)) + bytes(width))[:width]
                            conn.sendall(b'\x1b\1' + cells.replace(b'\x1b', b'\x1b\x1b'))
                            capture = bytearray()
                            expected = ''.join(chr(0x2800 + c) for c in cells[:4]).encode()
                            until = time.monotonic() + 4
                            while time.monotonic() < until:
                                readable, _, _ = select.select([master], [], [], .1)
                                if readable:
                                    capture.extend(os.read(master, 4096))
                                if expected in capture:
                                    break
                            self.assertIn(expected, capture, (work / 'gateway.log').read_text())
                            # TT active le mode curseur application (ESC O A), comme un terminal.
                            os.write(master, b'\x1bOA')
                            key = bytearray()
                            while len(key) < 6:
                                part = conn.recv(6 - len(key))
                                self.assertTrue(part)
                                key.extend(part)
                            self.assertEqual(key, b'\x1b\x24\x01\x1b\x24\0')
                finally:
                    if gateway:
                        gateway.terminate()
                        gateway.wait(timeout=5)
                    daemon.terminate()
                    daemon.wait(timeout=5)
