"""PC486_S3_86BOX=1 : mesure DX4/100 puis 486SX/25, Xvfb et D-Bus privés, audio nul.

Seuls une copie du disque arrêté et une copie de la CMOS sont modifiées.
Le signal de fin vient d'un petit programme DOS sur COM1, hors de tout lecteur.
Le second essai utilise synthe3/banc86box.py sur une machine 486SX/25 sans
coprocesseur : seules les variantes sans x87 y sont lancées.
"""
import os
from pathlib import Path
import re
import sys
import select
import signal
import shutil
import socket
import subprocess
import tempfile
import time
import tty
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'synthe3'))
import banc86box  # noqa: E402


@unittest.skipUnless(os.environ.get('PC486_S3_86BOX') == '1', 'PC486_S3_86BOX=1')
class Synthe3BoxTests(unittest.TestCase):
    def test_dx4_stream(self):
        box = ROOT / 'tools/86box/bin/86Box'
        if not box.exists() or not shutil.which('Xvfb') or not shutil.which('dbus-run-session'):
            self.skipTest('86Box, Xvfb ou D-Bus absent')
        subprocess.run(['bash', 'synthe3/build.sh'], cwd=ROOT, check=True, capture_output=True)
        source = Path(os.environ.get('PC486_S3_SOURCE', ROOT / 'vm/win311.img')).resolve()
        base = ROOT / 'work/vmtest/synthe3'
        base.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix='86box-', dir=base))
        disk = work / 'dos.img'
        subprocess.run(['python3', str(ROOT / 'scripts/with-disk-lock.py'), str(source), '--',
                        'cp', '--reflink=auto', '--sparse=always', str(source), str(disk)], check=True)
        image = f'{disk}@@32256'
        autoexec = work / 'AUTOEXEC.BAT'
        autoexec.write_bytes(
            b'@ECHO OFF\r\nSET BLASTER=A220 I5 D1 H5 T6\r\n'
            b'C:\\S3DOS.EXE -f C:\\TEXTE.TXT -o C:\\MICHEL.WAV > C:\\S3.TXT\r\n'
            b'C:\\S3DOS.EXE -s -f C:\\TEXTE.TXT > C:\\DIRECT.TXT\r\n'
            b'C:\\S3DOS.EXE -s -f C:\\TEXTE.TXT > C:\\DIRECT2.TXT\r\n'
            b'C:\\S3CLOCK.EXE > C:\\CLOCK.TXT\r\n'
            b'C:\\S3BENCH.EXE C:\\TEXTE.TXT > C:\\BENCH.TXT\r\n'
            b'C:\\S3BASE.EXE C:\\TEXTE.TXT > C:\\BASE.TXT\r\n'
            b'C:\\S3BASE.EXE C:\\TEXTE.TXT > C:\\BASE2.TXT\r\n'
            b'C:\\S3BENCH.EXE C:\\TEXTE.TXT > C:\\BENCH2.TXT\r\n'
            b'C:\\S3BENCH.EXE C:\\TEXTE.TXT > C:\\BENCH3.TXT\r\n'
            b'C:\\S3BASE.EXE C:\\TEXTE.TXT > C:\\BASE3.TXT\r\n'
            b'C:\\CHKBLKC.EXE > C:\\CHKBLKC.TXT\r\n'
            b'C:\\CHKBLKA.EXE > C:\\CHKBLKA.TXT\r\n'
            b'C:\\S3BLKA.EXE C:\\TEXTE.TXT > C:\\BLKA.TXT\r\n'
            b'C:\\S3INTA.EXE C:\\TEXTE.TXT > C:\\INTA.TXT\r\n'
            b'C:\\S3END.COM\r\n')
        config = work / 'CONFIG.SYS'
        config.write_bytes(b'FILES=20\r\nBUFFERS=10\r\n')
        text = work / 'TEXTE.TXT'
        text.write_bytes('Bonjour Michel. Un élève prend un café.'.encode('cp850'))
        build = ROOT / 'work/dev/synthe3/build'
        for f in (autoexec, config, text, build / 'S3DOS.EXE', build / 'S3END.COM',
                  build / 'S3CLOCK.EXE', build / 'S3BENCH.EXE', build / 'S3BASE.EXE',
                  build / 'CHKBLKC.EXE', build / 'CHKBLKA.EXE', build / 'S3BLKA.EXE', build / 'S3INTA.EXE'):
            subprocess.run(['mcopy', '-o', '-i', image, str(f), f'::/{f.name}'], check=True)
        cfg = (ROOT / '86box/86box.cfg').read_text()
        cfg = re.sub(r'^hdd_02_.*\n', '', cfg, flags=re.M)
        cfg = re.sub(r'^hdd_01_fn = .*$', f'hdd_01_fn = {disk}', cfg, flags=re.M)
        cfg = cfg.replace('serial3_enabled = 1', 'serial3_enabled = 0')
        cfg = cfg.replace('serial3_passthrough_enabled = 1', 'serial3_passthrough_enabled = 0')
        (work / '86box.cfg').write_text(cfg)
        (work / 'nvr').mkdir()
        shutil.copyfile(ROOT / '86box/nvr/hot433a_v451pg.nvr.ok',
                        work / 'nvr/hot433a_v451pg.nvr')
        env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_ACCESSIBILITY='0', NO_AT_BRIDGE='1',
                   ALSOFT_DRIVERS='null', SDL_AUDIODRIVER='dummy',
                   **{'86BOX_MANAGER_SOCKET': str(work / 'manager.sock')})
        for key in ('WAYLAND_DISPLAY', 'XDG_SESSION_TYPE', 'AT_SPI_BUS_ADDRESS', 'DBUS_SESSION_BUS_ADDRESS'):
            env.pop(key, None)
        manager = socket.socket(socket.AF_UNIX)
        manager.bind(str(work / 'manager.sock'))
        manager.listen(1)
        manager.settimeout(.1)
        rfd, wfd = os.pipe()
        process = connection = serial = None
        transcript = bytearray()
        with (work / 'xvfb.log').open('wb') as xlog, (work / '86box.log').open('wb') as log:
            xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(wfd), '-screen', '0',
                                     '1024x768x24', '-nolisten', 'tcp'],
                                    pass_fds=[wfd], stdout=xlog, stderr=xlog, start_new_session=True)
            os.close(wfd)
            try:
                self.assertTrue(select.select([rfd], [], [], 10)[0], 'Xvfb non prêt')
                env['DISPLAY'] = ':' + os.read(rfd, 32).decode().strip()
                (work / 'display.txt').write_text(env['DISPLAY'])
                command = ['python3', str(ROOT / 'scripts/with-disk-lock.py'), str(disk), '--',
                           'dbus-run-session', '--', str(box), '-N', '-P', str(work),
                           str(work / '86box.cfg')]
                process = subprocess.Popen(command, env=env, stdout=log, stderr=log, start_new_session=True)
                end = time.monotonic() + 120
                while time.monotonic() < end:
                    if process.poll() is not None:
                        self.fail((work / '86box.log').read_text(errors='replace'))
                    if connection is None:
                        try:
                            connection, _ = manager.accept()
                        except socket.timeout:
                            pass
                    if serial is None:
                        output = (work / '86box.log').read_text(errors='replace')
                        match = re.search(r'Slave side is (/dev/pts/\d+)', output)
                        if match:
                            serial = os.open(match[1], os.O_RDONLY | os.O_NONBLOCK | os.O_NOCTTY)
                            tty.setraw(serial)
                    elif select.select([serial], [], [], .1)[0]:
                        transcript.extend(os.read(serial, 4096))
                    if b'S3_BOX_FIN' in transcript:
                        break
                    time.sleep(.1)
                self.assertIn(b'S3_BOX_FIN', transcript, f'Pas de fin DOS : {work}')
            finally:
                os.close(rfd)
                if serial is not None:
                    os.close(serial)
                if process:
                    if connection:
                        connection.sendall(b'shutdownnoprompt\n')
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        # Échec d'essai sur sa copie privée, aucune session utilisateur.
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=5)
                if connection:
                    connection.close()
                manager.close()
                xvfb.terminate()
                xvfb.wait(timeout=5)
                (work / 'com1.txt').write_bytes(transcript)
        for name in ('S3.TXT', 'DIRECT.TXT', 'DIRECT2.TXT', 'MICHEL.WAV', 'CLOCK.TXT',
                     'BENCH.TXT', 'BASE.TXT', 'BENCH2.TXT', 'BASE2.TXT', 'BENCH3.TXT', 'BASE3.TXT',
                     'CHKBLKC.TXT', 'CHKBLKA.TXT', 'BLKA.TXT', 'INTA.TXT'):
            subprocess.run(['mcopy', '-o', '-i', image, f'::/{name}', str(work / name)], check=True)
        for name in ('DIRECT.TXT', 'DIRECT2.TXT'):
            self.assertIn('33380 produits, 33380 joués, 0 sous-alimentations, état 0',
                          (work / name).read_bytes().decode('cp850'))
        self.assertIn('PIT OK', (work / 'CLOCK.TXT').read_text())
        bench = (work / 'BENCH.TXT').read_text()
        baseline = (work / 'BASE.TXT').read_text()
        self.assertIn('status=0 produced=33380 played=33380 underruns=0', bench)
        self.assertIn('status=0 produced=33380 played=33380 underruns=0', baseline)
        self.assertIn('clock_fault=0', bench)
        self.assertIn('clock_fault=0', baseline)
        values = dict(re.findall(r'(\w+)=(\d+)', bench))
        self.assertGreater(int(values['pcm']), 0)
        self.assertGreater(int(values['idle']), 0)
        self.assertLess(int(values['source']), int(values['total']))
        self.assertEqual(re.search(r'hash=(\w+)', bench).group(1),
                         re.search(r'hash=(\w+)', baseline).group(1))
        for name in ('BENCH2.TXT', 'BASE2.TXT', 'BENCH3.TXT', 'BASE3.TXT'):
            result = (work / name).read_text()
            self.assertIn('clock_fault=0', result)
            self.assertIn('status=0 produced=33380 played=33380 underruns=0', result)
            self.assertIn('hash=cd5146b4', result)
        self.assertIn('block c OK', (work / 'CHKBLKC.TXT').read_text())
        self.assertIn('block asm OK', (work / 'CHKBLKA.TXT').read_text())
        for name, variant in (('BLKA.TXT', 'fenetres+bloc-asm'), ('INTA.TXT', 'fenetres+bloc-asm+entier')):
            result = (work / name).read_text()
            self.assertIn('clock_fault=0', result)
            self.assertIn('status=0 produced=33380 played=33380 underruns=0', result)
            self.assertIn('hash=cd5146b4', result)
            self.assertIn(f'variant={variant}', result)
            # Noyaux par période : génération plus courte que la référence du même démarrage.
            self.assertLess(int(re.search(r'pcm=(\d+)', result).group(1)), int(values['pcm']))
        print(f'\nSynthé3 86Box DX4/100 : {work}', flush=True)
        print((work / 'S3.TXT').read_bytes().decode('cp850').strip(), flush=True)
        print(bench.strip(), flush=True)
        print('SCALAIRE\n'+baseline.strip(), flush=True)


@unittest.skipUnless(os.environ.get('PC486_S3_86BOX') == '1', 'PC486_S3_86BOX=1')
class Synthe3SxTests(unittest.TestCase):
    def test_sx25_without_fpu_plays_in_real_time(self):
        box = ROOT / 'tools/86box/bin/86Box'
        if not box.exists() or not shutil.which('Xvfb') or not shutil.which('dbus-run-session'):
            self.skipTest('86Box, Xvfb ou D-Bus absent')
        subprocess.run(['bash', 'synthe3/build.sh'], cwd=ROOT, check=True, capture_output=True)
        work, result = banc86box.run_batch(
            'sx-25', ['S3INTA.EXE'], passes=2, timeout=1500,
            checks=['S3DOSX.EXE -s -f C:\\TEXTE.TXT', 'S3DOSX.EXE -s -f C:\\TEXTE.TXT', 'CHKBLKA.EXE'])
        result['resume'] = banc86box.summarize(result)
        (work / 'rapports.json').write_text(__import__('json').dumps(result, ensure_ascii=False, indent=1))
        self.assertEqual(result['cpu_86box']['cpu_family'], 'i486sx')
        self.assertIn('PIT OK', result['horloge'])
        summary = result['resume']['S3INTA.EXE']
        self.assertEqual(summary['essais_valides'], 2)
        self.assertEqual((summary['fpu'], summary['cpu_class']), (['0'], ['4']))
        self.assertEqual((summary['hash'], summary['samples']), (['cd5146b4'], ['33380']))
        # Génération sous la durée de parole (3,338 s) : le producteur suit la carte.
        self.assertLess(summary['generation_ms_mediane'], 3338 * 0.6)
        for key, text in result['verifications'].items():
            if 'S3DOSX' in key:
                self.assertIn('33380 produits, 33380 joués, 0 sous-alimentations, état 0', text, key)
            else:
                self.assertIn('block asm OK', text, key)
        print(f'\nSynthé3 86Box 486SX/25 : {work}', flush=True)
        print(summary, flush=True)


# Mots isolés du scénario par défaut de S3WPROBE.EXE, dans l'ordre.
WIN_WORDS = ('Fichier', 'Option', 'Options', 'Édition', 'Affichage', 'Fenêtre')
WIN_CONFIG = (b'DEVICE=C:\\WINDOWS\\HIMEM.SYS\r\nDOS=HIGH\r\nFILES=40\r\nBUFFERS=20\r\n'
              b'STACKS=9,256\r\nDEVICE=C:\\WINDOWS\\IFSHLP.SYS\r\n')


@unittest.skipUnless(os.environ.get('PC486_S3_86BOX') == '1', 'PC486_S3_86BOX=1')
class Synthe3WindowsSoundTests(unittest.TestCase):
    """EPAUS3.DLL sous Windows au rythme d'un 486 : la parole enregistrée à la sortie
    de 86Box doit durer autant que le PCM du moteur. Avec des tampons waveOut de 512
    octets, SNDBLST2.DRV perdait 200 ms par phrase (« fchier », « ption »)."""

    def test_dll_speech_is_complete_on_dx4_and_sx25(self):
        box = ROOT / 'tools/86box/bin/86Box'
        watcom = ROOT / 'tools/watcom'
        if not box.exists() or not shutil.which('Xvfb') or not shutil.which('dbus-run-session'):
            self.skipTest('86Box, Xvfb ou D-Bus absent')
        if not (watcom / 'binl64/wcl386').exists():
            self.skipTest('Open Watcom absent')
        source = Path(os.environ.get('PC486_S3_SOURCE', ROOT / 'vm/win311.img')).resolve()
        if subprocess.run(['mdir', '-i', f'{source}@@32256', '::/WINDOWS/WIN.COM'],
                          capture_output=True).returncode:
            self.skipTest('disque source sans Windows')
        subprocess.run(['bash', 'synthe3/build.sh'], cwd=ROOT, check=True, capture_output=True)
        build = banc86box.BUILD
        with tempfile.TemporaryDirectory(prefix='pc486-render-') as tmp:
            tmp = Path(tmp)
            env = dict(os.environ, WATCOM=str(watcom), INCLUDE=str(watcom / 'lh'),
                       PATH=f'{watcom / "binl64"}:{os.environ.get("PATH", "")}')
            src = ROOT / 'synthe3/src'
            subprocess.run(['wcl386', '-q', '-bt=linux', '-ecc', '-j', '-za99', '-DS3_BLOCK',
                            '-DS3_INTEGER_PERIOD', f'-i={src}', f'-fe={tmp / "render"}', f'-fo={tmp}/',
                            str(ROOT / 'synthe3/tests/render.c'),
                            *(str(src / f) for f in ('engine.c', 'kernels.c', 'weights.c', 'block.c', 'ap.c'))],
                           cwd=tmp, env=env, check=True, capture_output=True)
            native = {}
            for word in WIN_WORDS:
                subprocess.run([str(tmp / 'render'), word.encode('cp850'), str(tmp / 'w.raw')], check=True)
                pcm = __import__('array').array('h', (tmp / 'w.raw').read_bytes())
                native[word] = banc86box.spoken_ms([(x >> 8) << 8 for x in pcm], 10000)  # sortie 8 bits
        for cpu in ('dx4-100', 'sx-25'):
            with self.subTest(cpu=cpu):
                work, _ = banc86box.run_batch(
                    cpu, [], passes=0, source=source, config=WIN_CONFIG, sound=True,
                    extra_autoexec=b'PATH C:\\WINDOWS;C:\\DOS\r\nC:\\WINDOWS\\WIN.COM C:\\S3WPROBE.EXE\r\n',
                    extra_files=[build / 'S3WPROBE.EXE', build / 'EPAUS3.DLL'],
                    outputs_extra=['S3WPROB.TXT'], timeout=420)
                probe = (work / 'S3WPROB.TXT').read_bytes().decode('cp850')
                self.assertIn('S3WPROBE=fin', probe)
                values = dict(re.findall(r'(\w+)=(\d+)', probe))
                self.assertEqual((values['rate'], values['open_error'], values['underruns'],
                                  values['lost_marks'], values['refused']), ('10000', '0', '0', '0', '0'))
                self.assertNotIn('sans_echo', probe)
                samples, rate = banc86box.load_wav(work / 'son.wav')
                spans = banc86box.utterances(samples, rate)
                # Mots isolés : après le son de démarrage de Windows (~850 ms).
                start = [i for i, (a, b) in enumerate(spans) if 820 < b - a < 900][-1]
                words = [s for s in spans[start + 1:] if s[1] - s[0] > 150][:len(WIN_WORDS)]
                self.assertEqual(len(words), len(WIN_WORDS), spans)
                report = []
                for word, (a, b) in zip(WIN_WORDS, words):
                    heard = banc86box.spoken_ms(samples[(a - 50) * rate // 1000:(b + 50) * rate // 1000], rate)
                    report.append(f'{word} {native[word]}/{heard} ms')
                    self.assertLess(abs(native[word] - heard), 30, f'{cpu} : parole tronquée {report}')
                print(f'\nEPAUS3.DLL 86Box {cpu} : {work}\n  ' + ', '.join(report), flush=True)
