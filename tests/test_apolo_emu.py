"""Synthèse Apollo II virtuelle (Apolo-emu) lancée par scripts/pc486.

Run: python3 -m unittest discover -s tests -v
Apolo-emu tourne en mode muet (--backend simulated) ; QEMU est remplacé par une
socket Unix serveur, comme le chardev « server=on » de la VM. Le test est ignoré si
Apolo-emu n'est ni installé, ni préparé par Nix (tools/apolo-emu, paquet
github:pcressant/apolo-emu du flake), ni présent dans ../apolo-emu.
"""
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
APOLO_DIR = Path(os.environ.get('PC486_APOLO_EMU_DIR', ROOT.parent / 'apolo-emu'))
TOOL = ROOT / 'tools/apolo-emu'      # lien créé par « nix run .#prepare »
AVAILABLE = (shutil.which('apolo-emu') or (TOOL / 'bin/apolo-emu').is_file()
             or (APOLO_DIR / 'apolo_emu/__main__.py').is_file())


@unittest.skipUnless(AVAILABLE, 'Apolo-emu introuvable')
class ApoloEmuTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pc486-apolo-')
        self.root = Path(self.temp.name)
        for name in ('scripts', 'run', 'logs'):
            (self.root / name).mkdir()
        shutil.copy2(ROOT / 'scripts/pc486', self.root / 'scripts/pc486')
        if (TOOL / 'bin/apolo-emu').is_file():
            (self.root / 'tools').mkdir()
            (self.root / 'tools/apolo-emu').symlink_to(TOOL.resolve())
        self.env = dict(os.environ, PC486_RUN=str(self.root / 'run'),
                        PC486_APOLO_EMU_DIR=str(APOLO_DIR.resolve()),
                        PC486_GATEWAY_OPTS='--backend simulated --cps 200')
        # QEMU : socket COM1 en écoute (-chardev socket,server=on)
        self.qemu = socket.socket(socket.AF_UNIX)
        self.qemu.bind(str(self.root / 'run/com1.sock'))
        self.qemu.listen(1)
        self.qemu.settimeout(10)

    def tearDown(self):
        self.pc486('gateway', 'stop')
        self.qemu.close()
        self.temp.cleanup()

    def pc486(self, *args):
        return subprocess.run([str(self.root / 'scripts/pc486'), *args], cwd=self.root,
                              env=self.env, capture_output=True, text=True, timeout=20)

    def connect(self):
        p = self.pc486('gateway', 'start')
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        conn, _ = self.qemu.accept()
        conn.settimeout(10)
        self.addCleanup(conn.close)
        return conn

    def receive(self, conn, n):
        got = b''
        while len(got) < n:
            chunk = conn.recv(16)
            if not chunk:
                break
            got += chunk
        return got

    def test_epaulard_echo_after_each_line(self):
        """Dire tout d'Épaulard : texte, CR, puis écho @E dans sa propre proposition."""
        conn = self.connect()
        conn.sendall(b'\x18Bonjour.\r@EA\r')
        time.sleep(0.1)         # ligne suivante envoyée d'avance, pendant la lecture
        conn.sendall(b'le monde\r@EB\r')
        self.assertEqual(self.receive(conn, 2), b'AB')
        # Ctrl-X : un écho encore en file est perdu, comme sur l'Apollo II.
        conn.sendall(b'une ligne bien plus longue que les autres\r@EC\r\x18')
        conn.sendall(b'x\r@ED\r')
        self.assertEqual(self.receive(conn, 1), b'D')

    def test_index_query(self):
        conn = self.connect()
        conn.sendall(b'@I?')
        self.assertEqual(self.receive(conn, 4), b'I00M')

    def test_log_status_and_stop(self):
        conn = self.connect()
        conn.sendall(b'bonjour\r@EA\r')
        self.assertEqual(self.receive(conn, 1), b'A')
        self.assertIn('synthèse : en marche', self.pc486('status').stdout)
        self.pc486('gateway', 'stop')
        self.assertFalse((self.root / 'run/gateway.pid').exists())
        self.assertEqual(self.receive(conn, 1), b'')   # connexion fermée
        self.assertIn(': bonjour', (self.root / 'logs/apolo-emu.log').read_text())
        self.assertIn('RX 62 6f 6e', (self.root / 'logs/apollo-trace.log').read_text())


if __name__ == '__main__':
    unittest.main()
