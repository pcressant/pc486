"""Scénarios de bout en bout dans Windows 3.11 sous QEMU (lents : 6 à 10 minutes).

Lancés seulement sur demande, VM principale arrêtée :
    PC486_VM_TESTS=1 python3 -m unittest tests.test_vm_scenarios -v

Le disque est une copie jetable de vm/win311.img (work/vmtest/win311.img), sur laquelle
on installe EPAULARD.EXE et EPAUHOOK.DLL fraîchement compilés. QEMU tourne sans
fenêtre par scripts/pc486-dev, avec ses propres work/vmtest/run/ et work/vmtest/logs/, la
synthèse simulée d'Apolo-emu (15 caractères par seconde) et une fausse plage Baum de
40 cellules branchée sur com3.sock. Rien n'est écrit sur vm/win311.img.

Vérifié : icônes du Gestionnaire de programmes sans « vide » ; dire tout de
LISEZMOI.WRI sans pause ni arrêt prématuré, retour du curseur après Ctrl, arrêt en fin
de document ; sélection de Write en points 7-8 ; en-tête de Word suivi par la parole
et la plage ; calculatrice (résultats, erreur, relecture et braille).
"""
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import threading
import tempfile
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
DISK = ROOT / 'work/vmtest/win311.img'
RUN = ROOT / 'work/vmtest/run'
LOGS = ROOT / 'work/vmtest/logs'
ENV = dict(os.environ, PC486_DEV_DISK=str(DISK), PC486_DEV_RUN=str(RUN),
           PC486_DEV_LOGS=str(LOGS))
ENABLED = os.environ.get('PC486_VM_TESTS') == '1'


def dev(*args, check=True, timeout=120):
    return subprocess.run([str(ROOT / 'scripts/pc486-dev'), *args], env=ENV, cwd=ROOT,
                          capture_output=True, text=True, check=check, timeout=timeout)


def brfr():
    src = (ROOT / 'epaulard/src/brfr.h').read_text(encoding='utf-8').split('{', 1)[1]
    return [int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', src)]


class FakeBaum(threading.Thread):
    """Plage Baum 40 cellules : répond à l'activation, décode chaque affichage en
    (texte, masque) ; masque « # » : points 7 et 8."""

    def __init__(self, path):
        super().__init__(daemon=True)
        table = brfr()
        self.exact = {table[c]: chr(c) for c in range(126, 31, -1)}
        self.marked = {}
        for c in range(ord('z'), ord('a') - 1, -1):
            self.marked[table[c] | 0xc0] = chr(c)
            self.marked[table[c - 32] | 0xc0] = chr(c)
        self.path = path
        self.frames = []
        self.stop = False

    def decode(self, cells):
        text = ''.join(self.marked.get(c, '?') if c & 0xc0 == 0xc0 else
                       self.exact.get(c, ' ' if c == 0 else '?') for c in cells)
        mask = ''.join('#' if c & 0xc0 == 0xc0 else ' ' for c in cells)
        return text, mask

    def run(self):
        while not self.stop:
            try:
                s = socket.socket(socket.AF_UNIX)
                s.connect(str(self.path))
                break
            except OSError:
                time.sleep(0.5)
        with s:
            self.listen(s)

    def listen(self, s):
        s.settimeout(0.5)
        buf = b''
        while not self.stop:
            try:
                d = s.recv(4096)
            except socket.timeout:
                continue
            if not d:
                return
            buf += d
            while True:
                i = buf.find(b'\x1b')
                if i < 0:
                    buf = b''
                    break
                buf = buf[i:]
                if len(buf) < 2:
                    break
                if buf[1] == 0x15:
                    s.send(b'\x1b\x01\x28')
                    buf = buf[3:]
                elif buf[1] == 0x01:
                    cells, j = [], 2
                    while len(cells) < 40 and j < len(buf):
                        if buf[j] == 0x1b:
                            if j + 1 >= len(buf):
                                break
                            j += 1
                        cells.append(buf[j])
                        j += 1
                    if len(cells) < 40:
                        break
                    buf = buf[j:]
                    self.frames.append(self.decode(cells))
                else:
                    buf = buf[2:]

    def last(self):
        return self.frames[-1] if self.frames else ('', '')


def main_vm_busy():
    out = subprocess.run(['ps', '-eo', 'args'], capture_output=True, text=True).stdout
    return any(('86Box' in l or 'qemu-system' in l) and 'win311.img' in l and
               'vmtest' not in l and '/work/dev/' not in l and 'win311-dev' not in l for l in out.splitlines())


@unittest.skipUnless(ENABLED, 'scénarios QEMU : PC486_VM_TESTS=1')
class WindowsScenarios(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if main_vm_busy():
            raise unittest.SkipTest('vm/win311.img est utilisé par une VM : copie refusée')
        if DISK.exists():
            # Une reprise ne doit pas recopier une image dont l’ancienne VM vit encore.
            subprocess.run([sys.executable, str(ROOT / 'scripts/with-disk-lock.py'),
                            str(DISK), '--', 'true'], check=True)
        subprocess.run([str(ROOT / 'epaulard/build.sh')], check=True, capture_output=True)
        DISK.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vm/win311.img', DISK)
        img = f'{DISK}@@32256'
        build = ROOT / 'epaulard/build'
        subprocess.run(['mcopy', '-o', '-i', img, str(build / 'EPAULARD.EXE'),
                        str(build / 'EPAUHOOK.DLL'), '::/EPAULARD/'], check=True)
        # Word doit ouvrir un vrai document, sans dépendre des fichiers privés de C:.
        with tempfile.TemporaryDirectory(prefix='pc486-word-') as tmp:
            document = Path(tmp) / 'EPTEST.RTF'
            document.write_bytes(b'{\\rtf1\\ansi Document de test\\par}')
            subprocess.run(['mcopy', '-o', '-i', img, str(document), '::/EPTEST.RTF'], check=True)
        shutil.rmtree(LOGS, ignore_errors=True)
        dev('pc486', 'start', '--qemu', '--headless', '--braille')
        cls.baum = FakeBaum(RUN / 'com3.sock')
        cls.baum.start()
        deadline = time.time() + 60
        while 'Entrez un choix' not in dev('pc486', 'screen', check=False).stdout:
            if time.time() > deadline:
                raise AssertionError('menu DOS absent')
            time.sleep(1)
        cls.key('2')
        cls.key('ret')
        cls.wait_speech(r'paulard pr', 0, 180)
        time.sleep(8)
        # une application ouverte au démarrage (Presse-livres) : revenir au Gestionnaire
        for _ in range(3):
            n = cls.mark()
            cls.key('insert-t')
            title = cls.wait_speech(r'.', n, 10)
            if 'Gestionnaire de programmes' in title:
                break
            cls.key('alt-f4')
            time.sleep(3)

    @classmethod
    def tearDownClass(cls):
        cls.baum.stop = True
        dev('pc486', 'stop', check=False)
        subprocess.run(['pkill', '-f', f'{RUN}/'], check=False)

    # ------------------------------------------------------------ outils
    @staticmethod
    def key(*keys):
        dev('vmctl', 'key', *keys)

    @staticmethod
    def type(text):
        dev('vmctl', 'type', '--fr', text.replace('\\', '\\\\'))

    @staticmethod
    def speech():
        p = LOGS / 'apolo-emu.log'
        out = []
        for line in p.read_text(encoding='utf-8', errors='replace').splitlines():
            m = re.search(r"(?:PAROLE \(simulée\)|SPEECH \(simulated\)) (['\"])(.*)\1$", line)
            if m:
                out.append(m.group(2).strip())
        return out

    @classmethod
    def mark(cls):
        return len(cls.speech())

    @classmethod
    def wait_speech(cls, pattern, since, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for s in cls.speech()[since:]:
                if re.search(pattern, s):
                    return s
            time.sleep(0.5)
        raise AssertionError(f'parole « {pattern} » attendue, reçu : {cls.speech()[since:]}')

    @staticmethod
    def trace():
        """(t, 'S', marque) pour chaque ligne marquée envoyée, (t, 'R', marque) pour
        chaque écho renvoyé, (t, 'X', None) pour chaque Ctrl-X, et le texte envoyé."""
        ev = []
        for l in (LOGS / 'apollo-trace.log').read_text().splitlines():
            m = re.match(r'\s*([\d.]+) (RX|TX) (.*)', l)
            if not m:
                continue
            t, b = float(m[1]), bytes(int(x, 16) for x in m[3].split())
            if m[2] == 'RX':
                if b'\x18' in b:
                    ev.append((t, 'X', None, ''))
                # texte avant ses échos : une ligne peut arriver en deux morceaux
                ev.append((t, 'T', None, b.decode('cp850', 'replace')))
                for k in re.finditer(rb'@E(.)', b):
                    ev.append((t, 'S', k[1], ''))
            else:
                ev.extend((t, 'R', bytes([c]), '') for c in b)
        return ev

    @staticmethod
    def gaps(ev, since):
        """Silences entre deux lignes d'un dire tout : file vide puis nouvelle ligne."""
        pend, empty, out = [], None, []
        for t, k, c, _ in ev:
            if t < since:
                continue
            if k == 'X':
                pend, empty = [], None
            elif k == 'S':
                if empty is not None:
                    out.append(t - empty)
                    empty = None
                pend.append(c)
            elif k == 'R' and c in pend:
                pend = pend[pend.index(c) + 1:]
                if not pend:
                    empty = t
        return out

    def wait_braille(self, prefix, timeout=10):
        # COM3 est asynchrone : la parole peut précéder la fin de la trame Baum.
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.baum.last()[0].startswith(prefix):
                return
            time.sleep(0.1)
        self.fail(f'braille {prefix!r} attendu, reçu : {self.baum.last()}')

    def run_program(self, command):
        self.key('alt')
        time.sleep(1)
        self.key('ret')
        time.sleep(1.5)
        self.key('up')
        time.sleep(1)
        self.key('up')
        time.sleep(1)
        self.key('ret')
        time.sleep(2)
        self.type(command)
        self.key('ret')

    # ------------------------------------------------------------ scénarios
    def test_0_calculator(self):
        try:
            n = self.mark()
            self.run_program('calc')
            self.wait_speech('Calculatrice', n, 30)
            time.sleep(3)
            self.key('esc')
            time.sleep(1)
            n = self.mark()
            self.type('12+3=')
            self.wait_speech(r'^15$', n, 15)
            self.wait_braille('15')
            n = self.mark()
            self.key('insert-up')
            self.wait_speech(r'^15$', n, 10)
            # Mémorisation et rappel par les boutons dessinés, sans clic souris.
            self.key('insert-home')
            for _ in range(3):
                self.key('insert-right'); time.sleep(0.3)
            n = self.mark()
            self.key('insert-kp_divide')
            self.wait_speech('aucune position', n, 10)
            n = self.mark()
            self.key('insert-ret')
            self.wait_speech('mémoire occupée', n, 15)
            self.key('esc'); time.sleep(1)
            self.key('insert-home')
            for _ in range(4):
                self.key('insert-right'); time.sleep(0.3)
            n = self.mark()
            self.key('insert-ret')
            self.wait_speech(r'^15$', n, 15)
            self.wait_braille('15')
            self.key('insert-right'); time.sleep(0.5)
            n = self.mark()
            self.key('insert-ret')
            self.wait_speech('mémoire vide', n, 15)
            self.key('insert-pgdn')
            self.wait_braille('mode standard')
            self.key('insert-up')
            self.wait_braille('15')
            self.key('esc'); time.sleep(1)
            n = self.mark()
            self.type('1/0=')
            self.wait_speech(r'Division par z', n, 15)
            self.key('esc'); time.sleep(1)
            n = self.mark()
            self.type('7*8=')
            self.wait_speech(r'^56$', n, 15)
            # Affichage scientifique : son afficheur STATIC a un autre identifiant.
            n = self.mark()
            for k in ('alt', 'right', 'down'):
                self.key(k)
                time.sleep(1)
            self.wait_speech('Scientifique', n, 10)
            self.key('ret'); time.sleep(2)
            self.key('esc'); time.sleep(1)
            n = self.mark()
            self.type('9*9=')
            self.wait_speech(r'^81$', n, 15)
            self.wait_braille('81')
            # Hexadécimal, taille du mot, retour décimal, options Inv/Hyp.
            self.key('insert-end')
            for _ in range(8):
                self.key('insert-left'); time.sleep(0.3)
            n = self.mark()
            self.key('insert-ret')
            self.wait_speech('base hexadécimale', n, 15)
            self.wait_braille('51')
            self.key('insert-end'); time.sleep(0.3)
            self.key('insert-left'); time.sleep(0.3)
            n = self.mark()
            self.key('insert-ret')
            self.wait_speech('mot 16 bits', n, 15)
            for _ in range(4):
                self.key('insert-left'); time.sleep(0.3)
            n = self.mark()
            self.key('insert-ret')
            self.wait_speech('base décimale', n, 15)
            self.key('insert-end'); time.sleep(0.3)
            n = self.mark()
            self.key('insert-ret')
            self.wait_speech('fonctions hyperboliques activées', n, 15)
            self.key('insert-left'); time.sleep(0.3)
            n = self.mark()
            self.key('insert-ret')
            self.wait_speech('fonctions inverses activées', n, 15)
            self.key('insert-pgdn')
            self.wait_braille('mode scientifique')
            self.key('insert-up')
            self.wait_braille('81')
            for k in ('alt', 'right', 'down', 'down', 'ret'):
                self.key(k)
                time.sleep(1)
        finally:
            self.key('esc')
            self.key('alt-f4')
            time.sleep(3)

    def test_1_progman_icons_never_say_empty(self):
        n = self.mark()
        for k in ('right', 'right', 'up', 'down', 'down', 'left', 'left', 'left'):
            self.key(k)
            time.sleep(1.5)
        said = self.speech()[n:]
        self.assertTrue(said, 'aucune icône annoncée')
        self.assertNotIn('vide', said)

    def test_2_write_say_all(self):
        n = self.mark()
        self.run_program(r'write c:\windows\lisezmoi.wri')
        self.wait_speech(r'LISEZMOI', n, 30)
        time.sleep(4)
        t0 = time.time()
        start = self.trace()[-1][0] if self.trace() else 0
        n = self.mark()
        self.key('insert-down')
        time.sleep(70)
        ev = self.trace()
        lines = [e for e in ev if e[1] == 'S' and e[0] > start]
        # 15 car./s : une quinzaine de lignes en 70 s, envoyées jusqu'au bout
        self.assertGreater(len(lines), 12, 'le dire tout s\'est arrêté trop tôt')
        self.assertGreater(lines[-1][0], ev[-1][0] - 15, 'plus rien envoyé à la fin')
        self.assertEqual(self.gaps(ev, start), [], 'pause dans le dire tout')
        # Ctrl : le curseur revient sur la ligne en cours de lecture
        self.key('ctrl')
        time.sleep(2)
        ev = self.trace()
        sent, done = [], set()
        for t, k, c, txt in ev:
            if t > start and k == 'S':
                sent.append(c)
            if t > start and k == 'R':
                done.add(c)
        # flux envoyé recomposé (les morceaux reçus coupent n'importe où), puis
        # découpé en propositions : chaque écho @E<c> suit sa ligne
        text_by_mark, prev = {}, ''
        stream = ''.join(txt for t, k, c, txt in ev if t > start and k == 'T')
        for tok in stream.replace('\x18', '').split('\r'):
            if len(tok) == 3 and tok.startswith('@E'):
                text_by_mark[tok[2].encode('cp850')] = prev
            elif tok.strip() and not tok.startswith('@'):
                prev = tok.strip()
        head = next(c for c in sent if c not in done)
        n = self.mark()
        self.key('insert-up')
        said = self.wait_speech(r'.', n, 10)
        # l'Apollo II découpe aux virgules : la première proposition suffit
        said, line = said.replace(' ', ''), text_by_mark[head].replace(' ', '')
        self.assertTrue(len(said) > 4 and line.startswith(said), f'{said!r} / {line!r}')
        self.assertLess(time.time() - t0, 200)
        # fin du document : arrêt propre sur le dernier paragraphe
        self.key('ctrl-end')
        time.sleep(3)
        for _ in range(6):
            self.key('up')
            time.sleep(0.8)
        time.sleep(2)
        n = self.mark()
        self.key('insert-down')
        self.wait_speech(r"l'Association r", n, 60)
        time.sleep(8)
        self.assertIn("groupes d'utilisateurs de PC)", ' '.join(self.speech()[n:]))
        n = self.mark()
        self.key('up')
        self.wait_speech(r'.', n, 10)   # navigation rendue : la lecture est arrêtée

    def test_3_write_selection_in_braille(self):
        self.key('ctrl-home')
        time.sleep(3)
        self.key('ctrl-shift-right')
        time.sleep(3)
        text, mask = self.baum.last()
        self.assertTrue(text.startswith('complement') or text.startswith('compl'), text)
        self.assertEqual(mask[:11], '#' * 11, f'{text!r} {mask!r}')
        self.assertEqual(mask[11:20], ' ' * 9)
        self.key('right')
        time.sleep(3)
        text, mask = self.baum.last()
        self.assertEqual(mask.count('#'), 1, f'{text!r} {mask!r}')

    def test_4_word_header_followed(self):
        n = self.mark()
        self.key('insert-t')
        title = self.wait_speech(r'.', n, 10)
        if 'Gestionnaire de programmes' not in title:
            self.key('alt-f4')
            time.sleep(3)
        n = self.mark()
        # Document créé dans la copie par setUpClass, sans dépendance à l’image locale.
        self.run_program(r'c:\winword\winword.exe c:\eptest.rtf')
        title = self.wait_speech(r'EPTEST|Astuce du jour', n, 60)
        if 'Astuce du jour' in title:
            self.key('esc')
            self.wait_speech(r'EPTEST', n, 30)
        time.sleep(6)
        n = self.mark()
        self.key('alt')
        time.sleep(1)
        for k in ('right', 'right', 'right', 'down'):
            self.key(k)
            time.sleep(1)
        for _ in range(7):
            self.key('down')
            time.sleep(0.6)
        self.wait_speech(r'En-tête et pied de page', n, 10)
        self.key('ret')
        time.sleep(6)
        self.type('Abc')
        time.sleep(3)
        self.key('left')
        time.sleep(2)
        text, mask = self.baum.last()
        self.assertTrue(text.lower().startswith('abc'), text)   # A : cellule majuscule
        self.assertEqual(mask[:4], '  # ', f'{text!r} {mask!r}')
        n = self.mark()
        self.key('insert-up')
        self.assertEqual(self.wait_speech(r'.', n, 10), 'Abc')


if __name__ == '__main__':
    unittest.main()
