"""Réseau dans Windows 3.11 sous QEMU : isolement et latence sous charge (lents).

Lancés seulement sur demande, VM principale arrêtée :
    PC486_NET_VM_TESTS=1 python3 -m unittest tests.test_reseau_vm -v

Disque : work/vmtest/reseau.img, fabriqué une fois par scripts/options-vm (CD-ROM et
réseau ajoutés à une copie de vm/win311.img, environ 10 min), puis copié pour chaque
essai ; EPAULARD, EPAUDOS et SONDE.EXE (tests/win16/sonde.c) fraîchement compilés.
QEMU au rythme de l'usage (PC486_ICOUNT=7), synthèse simulée, fausse plage Baum.

Mesure (demande de l'utilisateur : ne pas supposer qu'IRQ9 passe avant IRQ10) :
délai entre une flèche Bas dans le Bloc-notes et la parole sur COM1, puis la ligne
sur la plage (COM3), sans charge puis pendant un téléchargement POP3 continu de
SONDE.EXE ; dire tout complet dans les deux cas. QEMU retient les octets série
quand l'UART de l'invité est plein : une perte d'octets n'est pas observable ici,
seulement le retard ; ce banc ne remplace donc pas un essai sous 86Box.
"""
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_vm_scenarios import FakeBaum, main_vm_busy  # noqa: E402
from vmctl import Qmp, type_text  # noqa: E402

BASE = ROOT / 'work/vmtest/reseau.img'
DISK = ROOT / 'work/vmtest/reseau-essai.img'
RUN = ROOT / 'work/vmtest/reseau-run'
LOGS = ROOT / 'work/vmtest/reseau-logs'
DATA = ROOT / 'work/vmtest/reseau-donnees'
WATCOM = Path(os.environ.get('WATCOM', ROOT / 'tools/watcom')).resolve()
ENV = dict(os.environ, PC486_DEV_DISK=str(DISK), PC486_DEV_RUN=str(RUN),
           PC486_DEV_LOGS=str(LOGS), PC486_DEV_RESEAU_DATA=str(DATA), PC486_ICOUNT='7')
ENABLED = os.environ.get('PC486_NET_VM_TESTS') == '1'
LINES = 15
LOAD_SECONDS = 420


def dev(*args, check=True, timeout=120):
    return subprocess.run([str(ROOT / 'scripts/pc486-dev'), *args], env=ENV, cwd=ROOT,
                          capture_output=True, text=True, check=check, timeout=timeout)


def build_sonde(out):
    with tempfile.TemporaryDirectory(prefix='sonde-') as d:
        src = (ROOT / 'tests/win16/sonde.c').read_text(encoding='utf-8')
        (Path(d) / 'sonde.c').write_bytes(src.encode('cp1252'))
        env = dict(os.environ, WATCOM=str(WATCOM), INCLUDE=f'{WATCOM}/h:{WATCOM}/h/win',
                   PATH=f"{WATCOM / 'binl64'}:{WATCOM / 'binl'}:{os.environ['PATH']}")
        for cmd in (['wcc', '-q', '-bt=windows', '-ml', '-zw', '-zu', '-os', '-s', '-wx', '-we',
                     '-DWINVER=0x030a', '-DSTRICT', 'sonde.c'],
                    ['wlink', 'op', 'q', 'system', 'windows', 'name', 'SONDE.EXE', 'file', 'sonde.o',
                     'op', 'stack=8k', 'op', 'heapsize=1k']):
            subprocess.run(cmd, cwd=d, env=env, check=True, capture_output=True)
        shutil.copyfile(Path(d) / 'SONDE.EXE', out)


class TimedBaum(FakeBaum):
    """Plage Baum dont chaque trame est horodatée à son arrivée."""

    def __init__(self, path):
        super().__init__(path)
        self.times = []

    def decode(self, cells):
        self.times.append(time.monotonic())
        return super().decode(cells)


class Trace:
    """Phrases reçues par Apolo-emu (« speech N: … » de apolo-emu.log), horodatées à
    leur apparition dans le journal (sondage 5 ms)."""

    def __init__(self, path):
        self.path, self.pos, self.events = path, 0, []

    def poll(self):
        try:
            with self.path.open('rb') as f:
                f.seek(self.pos)
                data = f.read()
        except FileNotFoundError:
            return
        end = data.rfind(b'\n') + 1
        self.pos += end
        now = time.monotonic()
        for line in data[:end].decode('utf-8', 'replace').splitlines():
            m = re.search(r'apolo_emu\.device: speech \d+: (.*)$', line)
            if m:
                self.events.append((now, m[1].strip()))


@unittest.skipUnless(ENABLED, 'scénarios réseau QEMU : PC486_NET_VM_TESTS=1')
class NetworkScenarios(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.prepare()
        except BaseException:
            cls.tearDownClass()
            raise

    @classmethod
    def prepare(cls):
        cls.baum = None
        if main_vm_busy():
            raise unittest.SkipTest('vm/win311.img est utilisé par une VM : copie refusée')
        if not BASE.exists():
            subprocess.run([str(ROOT / 'scripts/options-vm'), '--source', str(ROOT / 'vm/win311.img'),
                            '--sortie', str(BASE), '--cdrom', str(ROOT / 'media/udvd2/udvd2.zip'),
                            '--reseau', str(ROOT / 'media/tcpip32/tcp32b.zip')], check=True, timeout=3000)
        if DISK.exists():
            subprocess.run([sys.executable, str(ROOT / 'scripts/with-disk-lock.py'), str(DISK), '--', 'true'],
                           check=True)
        shutil.copyfile(BASE, DISK)
        img = f'{DISK}@@32256'
        subprocess.run([str(ROOT / 'epaulard/build.sh')], check=True, capture_output=True)
        subprocess.run([str(ROOT / 'epaudos/build.sh')], check=True, capture_output=True)
        build = ROOT / 'epaulard/build'
        subprocess.run(['mcopy', '-o', '-i', img, str(build / 'EPAULARD.EXE'), str(build / 'EPAUHOOK.DLL'),
                        '::/EPAULARD/'], check=True)
        subprocess.run(['mcopy', '-o', '-i', img, str(ROOT / 'epaudos/build/EPAUDOS.COM'), '::/EPAUDOS.COM'],
                       check=True)
        with tempfile.TemporaryDirectory(prefix='pc486-reseau-') as tmp:
            sonde = Path(tmp) / 'SONDE.EXE'
            build_sonde(sonde)
            note = Path(tmp) / 'NOTE.TXT'
            note.write_bytes(''.join(f'Ligne {i}\r\n' for i in range(1, LINES + 1)).encode('cp1252'))
            subprocess.run(['mcopy', '-o', '-i', img, str(sonde), str(note), '::/'], check=True)
        # Boîte POP3 : un seul message de 1 Mo, relu en boucle par SONDE.EXE CHARGE.
        shutil.rmtree(DATA, ignore_errors=True)
        box = DATA / 'courrier'
        for sub in ('cur', 'new', 'tmp'):
            (box / sub).mkdir(parents=True)
        (box / '.pc486-bienvenue').touch()
        body = ''.join(f'{i:06d} ' + 'x' * 70 + '\r\n' for i in range(13000))
        (box / 'new/1.gros').write_bytes(('Subject: charge\r\n\r\n' + body).encode())
        shutil.rmtree(LOGS, ignore_errors=True)
        dev('pc486', 'start', '--qemu', '--headless', '--braille', '--reseau')
        cls.q = Qmp(str(RUN / 'qmp.sock'))
        cls.baum = TimedBaum(RUN / 'com3.sock')
        cls.baum.start()
        cls.trace = Trace(LOGS / 'apolo-emu.log')
        # Une seule connexion QMP (QEMU n'en accepte qu'une) : écran et clavier par cls.q.
        deadline = time.time() + 60
        while 'Entrez un choix' not in '\n'.join(cls.q.screen()[0]):
            if time.time() > deadline:
                raise AssertionError('menu DOS absent')
            time.sleep(.5)
        cls.key('2')
        cls.key('ret')
        cls.wait_text('paulard pr', 300)
        time.sleep(8)
        for _ in range(3):
            n = len(cls.trace.events)
            cls.key('insert-t')
            title = cls.wait_text('.', 10, n)
            if 'Gestionnaire de programmes' in title:
                break
            cls.key('alt-f4')
            time.sleep(3)

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, 'baum', None):
            cls.baum.stop = True
        cls.close_qmp()
        dev('pc486', 'stop', check=False)
        subprocess.run(['pkill', '-f', f'{RUN}/'], check=False)

    @classmethod
    def close_qmp(cls):
        q, cls.q = getattr(cls, 'q', None), None
        if q:
            for f in (q.f, q.s):
                try:
                    f.close()
                except OSError:
                    pass

    @classmethod
    def key(cls, combo):
        cls.q.keys(combo.split('-'))
        time.sleep(.15)

    @classmethod
    def type(cls, text):
        type_text(cls.q, text, fr=True, delay=.1)

    @classmethod
    def wait_text(cls, pattern, timeout, since=None):
        since = len(cls.trace.events) if since is None else since
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            cls.trace.poll()
            for _, text in cls.trace.events[since:]:
                if re.search(pattern, text):
                    return text
            time.sleep(.05)
        raise AssertionError(f'parole « {pattern} » attendue, reçu : '
                             f'{[t for _, t in cls.trace.events[since:]][-10:]}')

    def run_program(self, command):
        for k, pause in (('alt', 1), ('ret', 1.5), ('up', 1), ('up', 1), ('ret', 2)):
            self.key(k)
            time.sleep(pause)
        self.type(command)
        self.key('ret')

    def open_note(self):
        n = len(self.trace.events)
        self.run_program('notepad c:\\note.txt')
        self.wait_text('Bloc-notes', 30, n)
        time.sleep(3)
        self.key('ctrl-home')
        time.sleep(1.5)

    def close_note(self):
        self.key('alt-f4')
        time.sleep(3)

    def arrows(self):
        """(délai parole, délai plage) pour chaque flèche Bas, en secondes."""
        out = []
        for i in range(2, LINES + 1):
            self.trace.poll()
            n, f = len(self.trace.events), len(self.baum.frames)
            t0 = time.monotonic()
            self.q.keys(['down'])
            speech = braille = None
            want = f'Ligne {i}'
            while time.monotonic() - t0 < 5 and (speech is None or braille is None):
                self.trace.poll()
                if speech is None:
                    speech = next((t for t, x in self.trace.events[n:] if want in x), None)
                if braille is None:
                    braille = next((t for t, (x, _) in zip(self.baum.times[f:], self.baum.frames[f:])
                                    if x.lower().startswith(want.lower())), None)
                time.sleep(.005)
            self.assertIsNotNone(speech, f'{want} non dit')
            self.assertIsNotNone(braille, f'{want} absent de la plage : {self.baum.last()}')
            out.append((speech - t0, braille - t0))
            time.sleep(.7)
        return out

    def say_all(self):
        """Dire tout depuis le début : lignes dites (ordre) et durée."""
        self.key('ctrl-home')
        time.sleep(1.5)
        self.trace.poll()
        n = len(self.trace.events)
        t0 = time.monotonic()
        self.key('insert-down')
        self.wait_text(f'Ligne {LINES}\\b', 60, n)
        took = time.monotonic() - t0
        spoken = [int(m) for _, x in self.trace.events[n:] for m in re.findall(r'Ligne (\d+)', x)]
        time.sleep(2)
        return spoken, took

    @staticmethod
    def summary(label, values):
        v = sorted(values)
        return (f'{label} : médiane {statistics.median(v) * 1000:.0f} ms, '
                f'90 % {v[int(len(v) * .9) - 1] * 1000:.0f} ms, max {v[-1] * 1000:.0f} ms')

    def test_latency_under_network_load(self):
        self.open_note()
        calm = self.arrows()
        calm_all, calm_took = self.say_all()
        self.close_note()
        # Charge : SONDE.EXE sans fenêtre, le Gestionnaire de programmes garde le focus.
        self.run_program(f'c:\\sonde.exe charge {LOAD_SECONDS}')
        start = time.monotonic()
        time.sleep(5)
        self.open_note()
        busy = self.arrows()
        busy_all, busy_took = self.say_all()
        self.close_note()
        remaining = LOAD_SECONDS - (time.monotonic() - start)
        self.assertGreater(remaining, 0, 'mesures sous charge plus longues que la charge')
        time.sleep(remaining + 10)
        report = [self.summary('sans charge, parole', [s for s, _ in calm]),
                  self.summary('sans charge, plage', [b for _, b in calm]),
                  self.summary('sous charge, parole', [s for s, _ in busy]),
                  self.summary('sous charge, plage', [b for _, b in busy]),
                  f'dire tout : {calm_took:.1f} s sans charge, {busy_took:.1f} s sous charge']
        self.close_qmp()
        dev('pc486', 'stop', check=False)
        charge = subprocess.run(['mtype', '-i', f'{DISK}@@32256', '::/CHARGE.TXT'],
                                capture_output=True, text=True).stdout.strip()
        log = (LOGS / 'reseau.log').read_text(errors='replace')
        report.append(f'SONDE : {charge}')
        print('\n' + '\n'.join(report), file=sys.stderr)
        (LOGS / 'latence.txt').write_text('\n'.join(report) + '\n')
        m = re.search(r'CHARGE (\d+) octets en (\d+) ms', charge)
        self.assertTrue(m, f'charge réseau non exécutée : {charge!r}')
        self.assertGreater(int(m[1]), 2_000_000, 'charge réseau trop faible pour conclure')
        self.assertIn('POP3 :', log)
        self.assertEqual(calm_all, list(range(1, LINES + 1)))
        self.assertEqual(busy_all, list(range(1, LINES + 1)), 'dire tout incomplet sous charge')
        calm_max = max(max(s, b) for s, b in calm)
        busy_med = statistics.median([max(s, b) for s, b in busy])
        busy_max = max(max(s, b) for s, b in busy)
        self.assertLess(busy_max, calm_max + 0.3, 'retard sous charge réseau')
        self.assertLess(busy_med, 0.6, 'médiane de réaction sous charge')
        self.assertLess(busy_took, calm_took * 1.5 + 2, 'dire tout ralenti sous charge')


if __name__ == '__main__':
    unittest.main()
