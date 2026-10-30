"""Integration tests for the launcher with real socat and a fake 86Box process.

Run: python3 -m unittest discover -s tests -v
No DOS disk, graphical session or speech service is used.
"""
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
APOLO_DIR = Path(os.environ.get('PC486_APOLO_EMU_DIR', ROOT.parent / 'apolo-emu'))
APOLO_TOOL = ROOT / 'tools/apolo-emu'  # lien créé par « nix run .#prepare »
FAKE_BOX = r'''
import os, pty, socket, sys, time
from pathlib import Path
if Path('fail-start').exists():
    sys.exit(1)
# Comme 86Box 5.3 : configuration en dernier argument, réécrite avec les valeurs de l'hôte
cfg = Path(sys.argv[-1])
Path('box-args').write_text('\n'.join(sys.argv[1:]))
Path('box-cfg').write_text(cfg.read_text())
with cfg.open('a') as f:
    f.write('host_cpu = simulé\nuuid = 00000000-0000-5000-8000-000000000000\n')
if Path('net-probe').exists():
    # --reseau : espace de noms de 86Box, relais du proxy, port de l'hôte inaccessible
    def reach(port):
        try:
            s = socket.create_connection(('127.0.0.1', port), timeout=3)
        except OSError as e:
            return 'ERR ' + type(e).__name__
        if port == 8080:
            s.sendall(b'GET http://pc486/ HTTP/1.0\r\n\r\n')
            data = b''
            while True:
                chunk = s.recv(65536)
                if not chunk:
                    break
                data += chunk
            return 'OK ' + data.split(b'\r\n')[0].decode()
        return 'OK'
    host_port = int(Path('net-probe').read_text())
    Path('box-net').write_text('\n'.join([os.readlink('/proc/self/ns/net'), reach(8080),
                                          reach(host_port)]))
master, slave = pty.openpty()
print('serial_passthrough: Slave side is ' + os.ttyname(slave), flush=True)
if os.environ.get('PC486_BRAILLE') == '1':
    master3, slave3 = pty.openpty()
    print('serial_passthrough: Slave side is ' + os.ttyname(slave3), flush=True)
manager = socket.socket(socket.AF_UNIX)
manager.connect(os.environ['86BOX_MANAGER_SOCKET'])
while True:
    data = manager.recv(4096)
    if not data or b'shutdownnoprompt' in data:
        break
'''


class LifecycleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pc486-lifecycle-')
        self.root = Path(self.temp.name)
        for name in ('scripts', 'tools', 'run', 'logs', '86box'):
            (self.root / name).mkdir()
        for script in ('pc486', 'with-disk-lock.py'):
            shutil.copy2(ROOT / 'scripts' / script, self.root / 'scripts' / script)
        shutil.copytree(ROOT / 'scripts/reseau', self.root / 'scripts/reseau',
                        ignore=shutil.ignore_patterns('__pycache__'))
        (self.root / 'disk.img').touch()
        (self.root / '86box/86box.cfg').write_text('[Hard disks]\nhdd_01_fn = ../disk.img\n')
        (self.root / 'tools/socat').symlink_to((ROOT / 'tools/socat').resolve())
        box = self.root / 'fake-box'
        box.write_text('#!' + sys.executable + '\n' + FAKE_BOX)
        box.chmod(0o755)
        # Ne jamais hériter de la plage, des journaux ou des disques de la session hôte.
        self.env = {key: value for key, value in os.environ.items()
                    if not key.startswith('PC486_')}
        self.env.update(PC486_86BOX=str(box), PC486_RUN=str(self.root / 'run'),
                        PC486_SOCAT=shutil.which('socat') or str(ROOT / 'tools/socat/bin/socat'))
        self.pids = set()

    def tearDown(self):
        for p in (self.root / 'run').glob('*.pid'):
            try:
                self.pids.add(int(p.read_text()))
            except (OSError, ValueError):
                pass
        for pid in self.pids:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        self.temp.cleanup()

    def launch(self, *args, success=True):
        p = subprocess.run([str(self.root / 'scripts/pc486'), *args], cwd=self.root,
                           env=self.env, capture_output=True, text=True, timeout=25)
        self.assertEqual(p.returncode == 0, success, p.stdout + p.stderr)
        for f in (self.root / 'run').glob('*.pid'):
            self.pids.add(int(f.read_text()))
        return p

    def wait_clean(self):
        # run/nvr garde la CMOS de 86Box d'un lancement à l'autre.
        def left():
            return [p for p in (self.root / 'run').iterdir() if p.name != 'nvr']
        until = time.monotonic() + 5
        while time.monotonic() < until:
            if not left():
                return
            time.sleep(.05)
        self.fail('Remaining runtime files: ' + str(left()))

    def test_gateway_uses_prepared_tool_without_neighbour_checkout(self):
        tool = self.root / 'tools/apolo-emu/bin/apolo-emu'
        tool.parent.mkdir(parents=True)
        tool.write_text('#!' + sys.executable + '\n' +
                        'import pathlib, signal\n' +
                        'pathlib.Path("gateway-ready").touch()\n' +
                        'signal.pause()\n')
        tool.chmod(0o755)
        self.env['PC486_APOLO_EMU_DIR'] = str(self.root / 'absent')
        # Masquer une éventuelle commande globale sans perdre les outils système.
        bindir = self.root / 'bin'
        bindir.mkdir()
        for name in ('bash', 'dirname', 'readlink', 'mkdir', 'nohup', 'setsid', 'cat', 'rm'):
            (bindir / name).symlink_to(shutil.which(name))
        self.env['PATH'] = str(bindir)
        self.launch('gateway', 'start')
        until = time.monotonic() + 3
        while time.monotonic() < until and not (self.root / 'gateway-ready').exists():
            time.sleep(.05)
        self.assertTrue((self.root / 'gateway-ready').exists())
        self.launch('gateway', 'stop')

    def test_missing_cmos_comes_from_reference_without_replacing_existing(self):
        # 86Box lit nvr/ à côté de la configuration générée (run/), pas sous -P.
        ref = self.root / '86box/nvr'
        ref.mkdir()
        nvr = self.root / 'run/nvr'
        nvr.mkdir()
        (ref / 'neuve.nvr.ok').write_bytes(b'reference')
        (ref / 'vecue.nvr.ok').write_bytes(b'reference')
        (nvr / 'vecue.nvr').write_bytes(b'reglee dans le Setup')
        self.launch('start', '--no-gateway')
        self.launch('stop')
        self.wait_clean()
        self.assertEqual((nvr / 'neuve.nvr').read_bytes(), b'reference')
        self.assertEqual((nvr / 'vecue.nvr').read_bytes(), b'reglee dans le Setup')
        self.assertFalse((ref / 'neuve.nvr').exists())

    def test_window_close_cleans_relays_and_allows_restart(self):
        self.launch('start', '--no-gateway')
        # A real serial client creates the socat child which must also be stopped.
        client = socket.socket(socket.AF_UNIX)
        self.addCleanup(client.close)
        client.connect(str(self.root / 'run/com1.sock'))
        time.sleep(.1)
        pid = int((self.root / 'run/86box.pid').read_text())
        os.kill(pid, signal.SIGTERM)
        self.wait_clean()
        client.settimeout(2)
        self.assertEqual(client.recv(1), b'')
        self.launch('start', '--no-gateway')
        self.launch('stop')
        self.wait_clean()

    @unittest.skipUnless(shutil.which('apolo-emu') or (APOLO_TOOL / 'bin/apolo-emu').is_file()
                         or (APOLO_DIR / 'apolo_emu/__main__.py').is_file(),
                         'Apolo-emu introuvable')
    def test_window_close_stops_speech(self):
        if (APOLO_TOOL / 'bin/apolo-emu').is_file():
            (self.root / 'tools/apolo-emu').symlink_to(APOLO_TOOL.resolve())
        self.env.update(PC486_APOLO_EMU_DIR=str(APOLO_DIR.resolve()),
                        PC486_GATEWAY_OPTS='--backend simulated')
        self.launch('start')
        gateway = int((self.root / 'run/gateway.pid').read_text())
        pid = int((self.root / 'run/86box.pid').read_text())
        os.kill(pid, signal.SIGTERM)
        self.wait_clean()
        until = time.monotonic() + 5
        while time.monotonic() < until and Path(f'/proc/{gateway}').exists():
            time.sleep(.05)
        self.assertFalse(Path(f'/proc/{gateway}').exists(), 'Apolo-emu tourne encore')

    def test_braille_uses_second_pty_and_stops_with_window(self):
        helper = self.root / 'fake-braille'
        helper.write_text('#!' + sys.executable + '\n' +
                          'import pathlib, signal, sys\n' +
                          'pathlib.Path("braille-args").write_text(" ".join(sys.argv[1:]))\n' +
                          'signal.pause()\n')
        helper.chmod(0o755)
        self.env.update(PC486_BRAILLE_CMD=str(helper), PC486_BRAILLE_FOCUS='always')
        self.launch('start', '--no-gateway')
        until = time.monotonic() + 3
        while not (self.root / 'braille-args').exists() and time.monotonic() < until:
            time.sleep(.05)
        args = (self.root / 'braille-args').read_text()
        self.assertIn('--device /dev/pts/', args)
        self.assertIn('--focus always', args)
        braille = int((self.root / 'run/braille.pid').read_text())
        os.kill(int((self.root / 'run/86box.pid').read_text()), signal.SIGTERM)
        self.wait_clean()
        # Le PID doit disparaître : aucune passerelle ne conserve la plage.
        until = time.monotonic() + 3
        while Path(f'/proc/{braille}').exists() and time.monotonic() < until:
            time.sleep(.05)
        self.assertFalse(Path(f'/proc/{braille}').exists())

    def test_start_failure_cleans_manager(self):
        (self.root / 'fail-start').touch()
        self.launch('start', '--no-gateway', success=False)
        self.wait_clean()

    TEMPLATE = ('[General]\nhost_cpu = autre hôte\nuuid = 11111111-2222-5333-8444-555555555555\n'
                'language = fr-FR\n\n[Hard disks]\nhdd_01_fn = ../disk.img\n'
                'hdd_01_ide_channel = 0:0\nhdd_02_fn = ../vm/transfert.img\n'
                'hdd_02_ide_channel = 0:1\nhdd_02_parameters = 63, 16, 260, 0, ide\n\n'
                '[Ports (COM & LPT)]\nserial1_passthrough_enabled = 1\n')

    def generated_config(self):
        until = time.monotonic() + 3
        while not (self.root / 'box-cfg').exists() and time.monotonic() < until:
            time.sleep(.05)
        return (self.root / 'box-cfg').read_text()

    def test_86box_config_is_generated_without_host_values(self):
        template = self.root / '86box/86box.cfg'
        template.write_text(self.TEMPLATE)
        self.launch('start', '--no-gateway')
        cfg = self.generated_config()
        args = (self.root / 'box-args').read_text().splitlines()
        self.assertEqual(args[-1], str(self.root / 'run/86box.cfg'))
        self.assertEqual(args[args.index('-P') + 1], str(self.root / '86box'))
        self.assertNotIn('host_cpu', cfg)
        self.assertNotIn('uuid', cfg)
        self.assertIn(f'hdd_01_fn = {self.root}/86box/../disk.img\n', cfg)
        # vm/transfert.img absent : aucun D: annoncé à 86Box
        self.assertNotIn('hdd_02', cfg)
        self.assertIn('[Ports (COM & LPT)]\nserial1_passthrough_enabled = 1', cfg)
        self.launch('stop')
        self.wait_clean()
        # 86Box a écrit ses valeurs dans la copie, jamais dans le modèle versionné
        self.assertEqual(template.read_text(), self.TEMPLATE)

    def test_86box_attaches_existing_transfer_disk(self):
        (self.root / '86box/86box.cfg').write_text(self.TEMPLATE)
        xfer = self.root / 'vm/transfert.img'
        xfer.parent.mkdir()
        xfer.touch()
        self.launch('start', '--no-gateway')
        cfg = self.generated_config()
        self.assertIn(f'hdd_02_fn = {xfer}\nhdd_02_ide_channel = 0:1\n', cfg)
        # D: est verrouillé comme C: : une autre VM ne peut pas l'ouvrir
        result = subprocess.run([sys.executable, str(self.root / 'scripts/with-disk-lock.py'),
                                 str(xfer), '--', 'true'], capture_output=True, text=True)
        self.assertIn('verrouillée', result.stderr)
        self.launch('stop')
        self.wait_clean()
        (self.root / 'box-cfg').unlink()
        self.env['PC486_TRANSFERT'] = ''
        self.launch('start', '--no-gateway')
        self.assertNotIn('hdd_02', self.generated_config())
        self.launch('stop')
        self.wait_clean()

    CD_TEMPLATE = ('[Hard disks]\nhdd_01_fn = ../disk.img\n\n[Floppy and CD-ROM drives]\n'
                   'fdd_01_type = 35_2hd\nfdd_02_type = none\n\n[Ports (COM & LPT)]\n'
                   'serial1_passthrough_enabled = 1\n')

    def test_86box_without_options_has_no_cdrom_nor_network(self):
        (self.root / '86box/86box.cfg').write_text(self.CD_TEMPLATE)
        self.env['PC486_RESEAU_DATA'] = str(self.root / 'reseau')
        self.launch('start', '--no-gateway')
        cfg = self.generated_config()
        self.assertEqual(cfg, self.CD_TEMPLATE.replace('../disk.img', f'{self.root}/86box/../disk.img'))
        self.assertFalse((self.root / 'run/reseau.pid').exists())
        self.assertFalse((self.root / 'run/web.sock').exists())
        self.launch('stop')
        self.wait_clean()

    def test_86box_network_is_isolated_and_cdrom_attached(self):
        (self.root / '86box/86box.cfg').write_text(self.CD_TEMPLATE)
        self.env['PC486_RESEAU_DATA'] = str(self.root / 'reseau')
        iso = self.root / 'cd.iso'
        iso.write_bytes(b'\0' * 2048)
        outside = socket.socket()
        outside.bind(('127.0.0.1', 0))
        outside.listen()
        self.addCleanup(outside.close)
        (self.root / 'net-probe').write_text(str(outside.getsockname()[1]))
        p = self.launch('start', '--no-gateway', '--reseau', '--cdrom', str(iso))
        self.assertIn('réseau : proxy web et courrier local prêts', p.stdout)
        cfg = self.generated_config()
        self.assertIn('[Floppy and CD-ROM drives]\ncdrom_01_parameters = 1, atapi\n'
                      f'cdrom_01_ide_channel = 1:0\ncdrom_01_image_path = {iso}\nfdd_01_type', cfg)
        self.assertIn('[Network]\nnet_01_card = ne2k\nnet_01_net_type = slirp\n\n'
                      '[NE2000 Compatible #1]\nbase = 0300\nirq = 10\nmac = 48:60:01\n', cfg)
        until = time.monotonic() + 10
        while not (self.root / 'box-net').exists() and time.monotonic() < until:
            time.sleep(.05)
        ns, proxy, host = (self.root / 'box-net').read_text().splitlines()
        self.assertNotEqual(ns, os.readlink('/proc/self/ns/net'), '86Box hors espace de noms isolé')
        self.assertEqual(proxy, 'OK HTTP/1.0 200 OK')
        self.assertEqual(host, 'ERR ConnectionRefusedError', 'port de l’hôte joignable depuis 86Box')
        for name in ('web', 'pop3', 'smtp'):
            self.assertTrue((self.root / f'run/{name}.sock').is_socket())
        self.assertTrue((self.root / 'reseau/courrier/new').is_dir())
        services = int((self.root / 'run/reseau.pid').read_text())
        self.launch('stop')
        self.wait_clean()
        with self.assertRaises(ProcessLookupError, msg='services réseau encore en marche'):
            os.kill(services, 0)

    def test_second_start_preserves_running_vm(self):
        self.launch('start', '--no-gateway')
        pid = (self.root / 'run/86box.pid').read_text()
        result = subprocess.run([sys.executable, str(self.root / 'scripts/with-disk-lock.py'),
                                 str(self.root / 'disk.img'), '--', 'true'],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('verrouillée', result.stderr)
        self.launch('start', '--no-gateway', success=False)
        self.assertEqual((self.root / 'run/86box.pid').read_text(), pid)
        self.launch('stop')
        self.wait_clean()


if __name__ == '__main__':
    unittest.main()
