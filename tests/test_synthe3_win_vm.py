"""PC486_S3_TESTS=1 : Épaulard parlant par EPAUS3.DLL (Synthé3, waveOut de Windows).

Copie privée du disque arrêté sous work/vmtest/s3win/ : AUTOEXEC.BAT
des modèles de dos/, EPAULARD.INI sans clé Synthese (Auto). Parcours de l'utilisateur :
DOS avec l'Apollo II, Inser+E d'Épaudos : Synthèse, Synthé3 (EPAUDOS.INI), redémarrage,
Windows : S3RES (LH) et Épaudos avant WIN, Épaulard sur EPAUS3.DLL (configuration
« DOS et Windows »). QEMU sans bridage particulier, Sound Blaster
capturée dans un WAV, Apolo-emu simulé pour témoin (rien ne doit lui parvenir).
La parole vérifiée est la trace des propositions confiées au moteur, lue dans le
bloc de diagnostic de la DLL (signature S3WININF) en mémoire physique.

PC486_S3_TESTS=1 python3 -m unittest tests.test_synthe3_win_vm -v
"""
import array
from pathlib import Path
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
import unittest
import wave

from tests.test_vm_scenarios import FakeBaum, main_vm_busy

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from synthe3_etat import FIELDS, linear_memory, resident, win_info  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'work/vmtest/s3win'
ENV = dict(os.environ, PC486_DEV_DISK=str(BASE / 'win.img'),
           PC486_DEV_RUN=str(BASE / 'run'), PC486_DEV_LOGS=str(BASE / 'logs'),
           PC486_AUDIO=f'wav,path={BASE / "sound-blaster.wav"}')
@unittest.skipUnless(os.environ.get('PC486_S3_TESTS') == '1', 'PC486_S3_TESTS=1')
class Synthe3EpaulardVMTests(unittest.TestCase):
    def test_epaulard_speaks_with_local_synthe3(self):
        if main_vm_busy():
            self.skipTest('image principale utilisée : copie refusée')
        source = ROOT / 'vm/win311.img'
        if not source.is_file():
            self.skipTest('disque Windows source absent')
        for script in ('synthe3/build.sh', 'epaudos/build.sh', 'epaulard/build.sh'):
            subprocess.run(['bash', script], cwd=ROOT, check=True, capture_output=True)
        if BASE.exists():
            shutil.rmtree(BASE)
        BASE.mkdir(parents=True)
        disk = BASE / 'win.img'
        subprocess.run(['python3', str(ROOT / 'scripts/with-disk-lock.py'), str(source), '--',
                        'cp', '--reflink=auto', '--sparse=always', str(source), str(disk)], check=True)
        img = f'{disk}@@32256'
        self.configure(img)
        spec = importlib.util.spec_from_file_location('s3win_vmctl', ROOT / 'scripts/vmctl.py')
        self.vmctl = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.vmctl)
        self.q = None
        baum = None
        report = {}
        try:
            self.dev('pc486', 'start', '--qemu', '--headless', '--braille')
            baum = FakeBaum(BASE / 'run/com3.sock')
            baum.start()
            self.q = self.vmctl.Qmp(str(BASE / 'run/qmp.sock'))
            end = time.monotonic() + 60
            while 'Entrez un choix' not in self.screen():
                self.assertLess(time.monotonic(), end, 'menu DOS absent')
                time.sleep(1)
            # DOS avec l'Apollo II (Synthese=Apollo), puis le choix de l'utilisateur :
            # Inser+E d'Épaudos, Haut (dernier réglage, Synthèse), Droite, Entrée ; la
            # sauvegarde à l'invite écrit Synthese=Synthe3. Redémarrage : Synthé3 partout.
            self.key('1')
            self.key('ret')
            self.wait_screen('C:\\>', 60)
            time.sleep(3)
            self.key('insert-e')
            time.sleep(1)
            self.key('up')                  # Braille (dernier réglage), puis Synthèse
            time.sleep(1)
            self.key('up')
            time.sleep(1)
            self.key('right')
            time.sleep(1)
            self.key('ret')
            end = time.monotonic() + 20
            while not re.search(r'Synthèse Synthé3,.*au redémarrage.*Réglages enregistrés',
                                self.apollo_text(), re.S):
                self.assertLess(time.monotonic(), end, 'réglage Synthèse non enregistré par Épaudos')
                time.sleep(.5)
            self.q.cmd('system_reset')
            end = time.monotonic() + 60
            while 'Entrez un choix' not in self.screen():
                self.assertLess(time.monotonic(), end, 'menu DOS absent après le choix de Synthé3')
                time.sleep(1)
            apollo_mark = self.apollo()
            self.key('2')
            self.key('ret')
            self.n = 0
            self.wait_speech('Gestionnaire|Presse-livres|paulard pr', 240)
            first = self.info()
            self.assertGreaterEqual(first['opens'], 1, 'waveOut jamais ouvert')
            self.assertEqual(first['buffers'], 4)
            report['frequence_wave'] = first['rate']
            print(f"Épaulard prêt par Synthé3 (waveOut {first['rate']} Hz)", flush=True)
            time.sleep(8)
            for _ in range(3):          # application ouverte au démarrage : la fermer
                self.mark()
                self.key('insert-t')
                time.sleep(3)
                if 'Gestionnaire de programmes' in self.said():
                    break
                self.key('alt-f4')
                time.sleep(3)

            self.mark()
            self.run_program('notepad')
            self.wait_speech('Bloc-notes', 30)
            print('Bloc-notes ouvert, titre lu', flush=True)
            self.mark()
            self.type('bonjour ')
            self.wait_speech(r'\|j\|.*bonjour', 15)  # écho des caractères puis du mot
            self.type('\npremiere ligne du texte\nseconde ligne du texte\n'
                      'troisieme ligne du texte\nquatrieme ligne du texte')
            time.sleep(3)
            self.mark()
            self.key('up')
            self.wait_speech('troisieme ligne', 10)
            print('Saisie et flèches lues', flush=True)

            self.mark()
            self.key('alt')
            self.wait_speech('Fichier', 10)
            self.key('right')
            self.wait_speech('Edition|Édition', 10)
            self.key('esc')
            self.key('esc')
            print('Menus lus', flush=True)

            # Souris (souris.c) : pointeur au coin supérieur gauche, puis descente lente
            # (pas de 3 points, sous le seuil d'accélération de Windows) : rangées du
            # Bloc-notes lues, bips de hauteur mêlés par EPAUS3.DLL (cherchés dans le WAV).
            wav = BASE / 'sound-blaster.wav'
            wav_from = wav.stat().st_size
            self.mouse(-40, -40, 30)
            time.sleep(2)
            self.mark()
            try:
                self.mouse(1, 3, 110, .06)
                self.wait_speech('ligne du texte', 15)
            except AssertionError:
                self.q.cmd('screendump', filename=str(BASE / 'souris.ppm'))
                raise
            time.sleep(2)
            wav_span = (wav_from, wav.stat().st_size)
            report['souris'] = self.said()
            print('Souris : rangée sous le pointeur lue', flush=True)

            # Dire tout depuis le début : avance par les échos @E rendus par la DLL
            self.key('ctrl-home')
            time.sleep(2)
            self.mark()
            before = self.info()
            self.key('insert-down')
            self.wait_speech('quatrieme ligne', 60)
            done = self.info()
            self.assertGreater(done['marks'], before['marks'], 'aucun écho rendu')
            print('Dire tout jusqu’à la dernière ligne', flush=True)

            # Interruption : Ctrl pendant une longue lecture, puis reprise immédiate
            self.key('ctrl-home')
            time.sleep(2)
            self.key('insert-down')
            self.wait_until(lambda i: i['queued'] > 0, 10, 'audio en file')
            cut = self.info()
            self.key('ctrl')
            self.wait_until(lambda i: i['cuts'] > cut['cuts'] and i['resets'] > cut['resets'],
                            5, 'Ctrl-X appliqué')
            self.mark()
            self.key('down')
            self.wait_speech('premiere ligne|seconde ligne', 10)
            print('Interruption par Ctrl et reprise', flush=True)

            # Boîte : fermeture sans enregistrer
            self.mark()
            self.key('alt-f4')
            self.wait_speech('Enregistrer|modifi', 15)
            self.key('n')
            time.sleep(3)
            print('Boîte « Enregistrer les modifications » lue', flush=True)

            # Invite MS-DOS : Épaudos (mode invité) parle par Épaulard, donc par la DLL
            self.mark()
            self.run_program('command')
            self.wait_speech('paudos pr', 60)
            self.mark()
            for word in ('salut', 'encore', 'fin'):
                self.type(f'echo {word}\n')
                self.wait_speech(word, 20)
            # Tampons rendus par le pilote (WHDR_DONE), puis périphérique fermé : sans
            # cela, l'IRQ de la Sound Blaster partait à la machine DOS, le DMA du
            # pilote rebouclait sur son tampon (première phrase répétée sans fin).
            self.wait_until(lambda i: i['queued'] == 0 and not i['opened'], 20,
                            'waveOut rendu pendant la fenêtre DOS')
            self.type('exit\n')
            time.sleep(6)
            self.mark()
            self.key('insert-t')
            self.wait_speech('Gestionnaire', 15)
            self.wait_until(lambda i: i['queued'] == 0 and not i['opened'], 20,
                            'waveOut rendu après la fenêtre DOS')
            print('Invite MS-DOS relayée par Épaulard vers Synthé3', flush=True)

            self.assertTrue(baum.frames, 'aucune trame braille')
            self.assertEqual(self.apollo(), apollo_mark, 'parole envoyée sur COM1')
            final = self.info()
            report.update({k: final[k] for k in FIELDS[5:]})
            self.assertEqual(final['refused'], 0)
            self.assertEqual(final['lost_marks'], 0)

            # Quitter Windows : S3RES reprend la Sound Blaster pour Épaudos
            for _ in range(4):
                self.key('alt-f4')
                time.sleep(2)
                self.key('ret')
                time.sleep(8)
                if 'C:\\' in self.screen():
                    break
            self.wait_screen('C:\\>', 30)
            res = self.resident()
            self.assertEqual(res['windows'], 0)
            self.type('echo retour\n')
            mark = res['propositions']
            end = time.monotonic() + 15
            while time.monotonic() < end:
                res = self.resident()
                if res['propositions'] > mark and '|retour|' in res['trace']:
                    break
                time.sleep(.5)
            self.assertIn('|retour|', res['trace'], f'Épaudos muet après Windows : {res}\n{self.screen()}')
            report['s3res_apres_windows'] = {k: res[k] for k in ('underruns', 'refused', 'clock_fault')}
            print('Retour au DOS : Épaudos reparle par S3RES', flush=True)
        finally:
            if self.q is not None:
                self.q.f.close()
                self.q.s.close()
            if baum is not None:
                baum.stop = True
            self.dev('pc486', 'stop', check=False)
        with wave.open(str(BASE / 'sound-blaster.wav')) as w:
            rate, channels = w.getframerate(), w.getnchannels()
            pcm = array.array('h', w.readframes(w.getnframes()))
        window = rate // 10 * channels
        active = sum(1 for i in range(0, len(pcm) - window, window)
                     if max(pcm[i:i + window]) - min(pcm[i:i + window]) > 2000)
        report['capture_s'] = round(len(pcm) / channels / rate, 1)
        report['capture_active_s'] = active / 10
        self.assertGreater(active, 50, 'capture Sound Blaster presque silencieuse')
        first, last = ((b - 44) // (2 * channels) * channels for b in wav_span)
        tones = self.tone_frames(pcm[first:last], rate, channels)
        report['bips_souris'] = len(tones)
        report['bips_hz'] = sorted(set(tones))[:12]
        self.assertGreaterEqual(len(tones), 5, 'aucun bip de souris dans la capture')
        (BASE / 'mesures.json').write_text(json.dumps(report, indent=1, ensure_ascii=False))
        print(json.dumps(report, ensure_ascii=False), flush=True)

    # ------------------------------------------------------------------ outils
    @staticmethod
    def configure(img):
        def get(path):
            return subprocess.run(['mtype', '-i', img, path], check=True,
                                  capture_output=True).stdout
        def put(name, data):
            tmp = BASE / name.split('/')[-1]
            tmp.write_bytes(data)
            subprocess.run(['mcopy', '-o', '-i', img, str(tmp), f'::/{name}'], check=True)
        ini = get('::/EPAULARD/EPAULARD.INI')
        ini = re.sub(rb'(?im)^(Synthese|Echo)=.*\r?\n', b'', ini)     # Synthese=Auto (défaut)
        ini = re.sub(rb'(?i)\[Parole\]\r?\n', b'[Parole]\r\nEcho=2\r\n', ini, count=1)
        put('EPAULARD/EPAULARD.INI', ini)
        # Démarrage du modèle de l'installateur (dos/) : le choix se fait dans C:\\EPAULARD\\EPAUDOS.INI.
        bat = get('::/AUTOEXEC.BAT')
        template = (ROOT / 'dos/AUTOEXEC.BAT').read_bytes().replace(b'\n', b'\r\n')
        if b'MSCDEX' in bat:            # disque installé avec --cdrom : garder E:
            line = re.search(rb'(?m)^IF EXIST PC486CD.*\r\n', bat)
            template = template.replace(b'GOTO %CONFIG%', (line[0] if line else b'') + b'GOTO %CONFIG%')
        put('AUTOEXEC.BAT', template)
        # Rangement d'avant C:\\EPAULARD : copies de la racine retirées, le modèle ne les charge plus.
        for name in ('SYNTHE3.ON', 'SYNTHESE.BAT', 'EPAUDOS.COM', 'EPAUDOS.INI', 'S3RES.EXE'):
            subprocess.run(['mdel', '-i', img, '::/' + name], capture_output=True)
        put('EPAULARD/EPAUDOS.INI', b'Synthese=Apollo\r\n')    # Apollo II au départ (Synthé3 par défaut)
        build = ROOT / 'epaulard/build'
        synth = ROOT / 'work/dev/synthe3/build'
        subprocess.run(['mcopy', '-o', '-i', img, str(build / 'EPAULARD.EXE'), str(build / 'EPAUHOOK.DLL'),
                        str(synth / 'EPAUS3.DLL'), '::/EPAULARD/'], check=True)
        subprocess.run(['mcopy', '-o', '-i', img, str(ROOT / 'epaudos/build/EPAUDOS.COM'),
                        str(synth / 'S3RES.EXE'), '::/EPAULARD/'], check=True)

    def mouse(self, dx, dy, steps, pause=.03):
        for _ in range(steps):
            self.q.cmd('input-send-event', events=[
                {'type': 'rel', 'data': {'axis': 'x', 'value': dx}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': dy}}])
            time.sleep(pause)

    @staticmethod
    def tone_frames(pcm, rate, channels):
        """Fenêtres de 20 ms presque pures entre 200 et 900 Hz (bips de la souris) :
        une seule raie (Goertzel, pas de 10 Hz) porte plus de 70 % de l'énergie. La
        parole de Synthé3 n'y arrive pas. Renvoie leurs fréquences."""
        import math
        step = max(1, rate // 11025)           # sortie du pilote : 11 kHz au plus
        mono = pcm[::channels * step]
        rate //= step
        n = rate // 50
        coeffs = [(f, 2 * math.cos(2 * math.pi * f / rate)) for f in range(200, 901, 10)]
        found = []
        for start in range(0, len(mono) - n, n):
            frame = mono[start:start + n]
            mean = sum(frame) / n
            x = [v - mean for v in frame]
            total = sum(v * v for v in x)
            if total < n * 1500 ** 2:           # silence ou parole faible
                continue
            best, best_f = 0, 0
            for f, c in coeffs:
                s1 = s2 = 0.0
                for v in x:
                    s1, s2 = v + c * s1 - s2, s1
                power = (s1 * s1 + s2 * s2 - c * s1 * s2) * 2 / n
                if power > best:
                    best, best_f = power, f
            if best > .7 * total:
                found.append(best_f)
        return found

    @staticmethod
    def dev(*args, check=True):
        return subprocess.run([str(ROOT / 'scripts/pc486-dev'), *args], env=ENV, cwd=ROOT,
                              capture_output=True, text=True, check=check, timeout=120)

    def ram(self):
        mb = int(os.environ.get('PC486_RAM', '16'))
        return self.q.memory(0, mb << 20)

    def info(self):
        for _ in range(10):             # lu pendant sa mise à jour : somme fausse
            info = win_info(self.ram())
            if info:
                break
            time.sleep(.1)
        self.assertIsNotNone(info, 'bloc S3WININF introuvable : EPAUS3.DLL non chargée')
        return info

    def resident(self):
        """S3RES après Windows (LH : UMB d'EMM386, mémoire linéaire)."""
        info = resident(linear_memory(self.q))
        self.assertIsNotNone(info, 'S3RES introuvable')
        return info

    def mark(self):
        self.n = self.info()['propositions']

    def said(self):
        i = win_info(self.ram())
        if i is None:                   # DLL pas encore chargée
            return ''
        new = i['propositions'] - self.n
        return '|'.join(i['trace'].split('|')[-new - 1:]) if new > 0 else ''

    def wait_speech(self, pattern, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if re.search(pattern, self.said()):
                return
            time.sleep(.5)
        i = win_info(self.ram())
        self.fail(f'parole attendue {pattern!r} ; diagnostic : {i}')

    def wait_until(self, test, timeout, what):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            i = self.info()
            if test(i):
                return i
            time.sleep(.1)
        self.fail(f'{what} : {i}')

    def screen(self):
        return '\n'.join(self.q.screen()[0])

    def wait_screen(self, text, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if text in self.screen():
                return
            time.sleep(.5)
        self.fail(f'écran attendu {text!r} :\n{self.screen()}')

    def key(self, keys):
        self.q.keys(keys.split('-'))
        time.sleep(.4)

    def type(self, text):
        self.vmctl.type_text(self.q, text, fr=True, delay=.12)

    def run_program(self, command):
        for k, pause in (('alt', 1), ('ret', 1.5), ('up', 1), ('up', 1), ('ret', 2)):
            self.key(k)
            time.sleep(pause)
        self.type(command)
        self.key('ret')

    @staticmethod
    def apollo():
        p = BASE / 'logs/apolo-emu.log'
        text = p.read_text(errors='replace') if p.exists() else ''
        return len([l for l in text.splitlines() if 'apolo_emu.device' in l])

    @staticmethod
    def apollo_text():
        p = BASE / 'logs/apolo-emu.log'
        return p.read_text(errors='replace') if p.exists() else ''


if __name__ == '__main__':
    unittest.main()
