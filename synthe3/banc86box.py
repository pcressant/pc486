#!/usr/bin/env python3
"""Banc 86Box de Synthé3 : exécuter des variantes S3BENCH-like sur une copie privée.

Outil hôte : Xvfb et D-Bus privés, sortie audio nulle, aucun lecteur d'écran,
aucune fenêtre dans la session utilisateur. Seuls une copie du disque arrêté et
une copie de la CMOS sont modifiées. Le signal de fin vient de S3END.COM sur le
COM1 privé de la machine d'essai.

Usage :
  python3 synthe3/banc86box.py --cpu dx4-100 [--passes 3] [--texte TEXTE] PROG.EXE...
  python3 synthe3/banc86box.py --cpu sx-25 S3INT.EXE

Les programmes sont pris dans work/dev/synthe3/build. Chaque passage lance les
variantes dans un ordre alterné (A B C, C B A, A B C...) pour ne pas favoriser
une place fixe. Les rapports TXT sont copiés dans le dossier d'essai, et
rapports.json contient leurs champs bruts par variante et par passage.
"""
import argparse
import json
import os
from pathlib import Path
import re
import select
import shutil
import signal
import socket
import statistics
import subprocess
import tempfile
import time
import tty

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'work/dev/synthe3/build'
CPUS = {
    # 86Box : famille, horloge, multiplicateur, coprocesseur.
    'dx4-100': dict(cpu_family='idx4', cpu_speed='100000000', cpu_multi='3', fpu_type='internal'),
    'sx-25': dict(cpu_family='i486sx', cpu_speed='25000000', cpu_multi='1', fpu_type='none'),
    'dx-33': dict(cpu_family='i486dx', cpu_speed='33333333', cpu_multi='1', fpu_type='internal'),
}
TEXTE = 'Bonjour Michel. Un élève prend un café.'


def load_wav(path):
    """Échantillons mono (premier canal, échelle 16 bits) et fréquence d'un WAV de
    86Box (OpenAL Soft, pilote wave : flottants 32 bits) ou 16 bits entiers."""
    import array
    import struct
    data = Path(path).read_bytes()
    i, fmt, pcm = 12, None, b''
    while i + 8 <= len(data):
        cid, n = data[i:i+4], struct.unpack('<I', data[i+4:i+8])[0]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', data[i+8:i+24])
        if cid == b'data':
            pcm = data[i+8:i+8+n] if n else data[i+8:]   # taille nulle : fichier non refermé
            break
        i += 8 + n
    channels, rate, bits = fmt[1], fmt[2], fmt[5]
    if bits == 32:
        a = array.array('f')
        a.frombytes(pcm[:len(pcm) // 4 * 4])
        return [x * 32768 for x in a[::channels]], rate
    a = array.array('h')
    a.frombytes(pcm[:len(pcm) // 2 * 2])
    return list(a[::channels]), rate


def envelope(samples, rate, ms=1):
    step = max(1, rate * ms // 1000)
    return [max(abs(x) for x in samples[i:i+step]) for i in range(0, len(samples) - step + 1, step)]


def utterances(samples, rate, threshold=300, gap_ms=150):
    """Débuts et fins (ms) des énoncés : son au-dessus du seuil, séparés de plus de gap_ms."""
    env = envelope(samples, rate)
    spans, start, last = [], None, None
    for t, e in enumerate(env):
        if e > threshold:
            if start is None:
                start = t
            elif t - last > gap_ms:
                spans.append((start, last))
                start = t
            last = t
    if start is not None:
        spans.append((start, last))
    return spans


def spoken_ms(samples, rate):
    """Durée parlée : du premier au dernier instant au-dessus de 3 % de la crête."""
    env = envelope(samples, rate)
    threshold = max(env) * .03
    on = [i for i, v in enumerate(env) if v > threshold]
    return on[-1] - on[0] if on else 0


def report(path):
    return dict(re.findall(r'(\w+)=([^\s]+)', path.read_text(errors='replace')))


def run_batch(cpu, programs, passes=3, texte=TEXTE, source=None, timeout=900, extra_files=(),
              extra_autoexec=b'', base=None, checks=(), config=None, sound=False, outputs_extra=()):
    """Exécute le lot et renvoie (dossier d'essai, rapports par nom de fichier).

    config : CONFIG.SYS (octets) au lieu du minimum DOS, par exemple pour Windows.
    sound : sortie audio de 86Box enregistrée dans son.wav (OpenAL Soft, pilote wave)
    au lieu d'être jetée. outputs_extra : fichiers de C:\\ relus en fin d'essai."""
    box = ROOT / 'tools/86box/bin/86Box'
    for tool in ('Xvfb', 'dbus-run-session', 'mcopy'):
        if not shutil.which(tool):
            raise RuntimeError(f'{tool} absent')
    if not box.exists():
        raise RuntimeError('86Box absent')
    source = Path(source or os.environ.get('PC486_S3_SOURCE', ROOT / 'vm/win311.img')).resolve()
    base = Path(base or ROOT / 'work/vmtest/synthe3')
    base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=f'banc-{cpu}-', dir=base))
    disk = work / 'dos.img'
    subprocess.run(['python3', str(ROOT / 'scripts/with-disk-lock.py'), str(source), '--',
                    'cp', '--reflink=auto', '--sparse=always', str(source), str(disk)], check=True)
    image = f'{disk}@@32256'
    lines = [b'@ECHO OFF', b'SET BLASTER=A220 I5 D1 H5 T6', b'C:\\S3CLOCK.EXE > C:\\CLOCK.TXT']
    # Vérifications (programme et arguments), une fois chacune, sortie dans NOMVn.TXT.
    check_files = [f'{Path(c.split()[0]).stem[:6]}V{i+1}.TXT' for i, c in enumerate(checks)]
    for check, out in zip(checks, check_files):
        lines.append(f'C:\\{check} > C:\\{out}'.encode())
    outputs = []
    for p in range(passes):
        order = list(enumerate(programs))
        if p % 2:
            order.reverse()
        for i, name in order:
            out = f'V{i+1}P{p+1}.TXT'  # noms uniques même pour des programmes de préfixe commun
            outputs.append((name, p+1, out))
            lines.append(f'C:\\{name} C:\\TEXTE.TXT > C:\\{out}'.encode())
    lines += [l for l in extra_autoexec.split(b'\r\n') if l]
    lines.append(b'C:\\S3END.COM')
    (work / 'AUTOEXEC.BAT').write_bytes(b'\r\n'.join(lines) + b'\r\n')
    (work / 'CONFIG.SYS').write_bytes(config or b'FILES=20\r\nBUFFERS=10\r\n')
    (work / 'TEXTE.TXT').write_bytes(texte.encode('cp850'))
    files = [work / 'AUTOEXEC.BAT', work / 'CONFIG.SYS', work / 'TEXTE.TXT',
             BUILD / 'S3END.COM', BUILD / 'S3CLOCK.EXE'] + [BUILD / n for n in list(programs)+[c.split()[0] for c in checks]]
    files += [Path(f) for f in extra_files]
    for f in files:
        subprocess.run(['mcopy', '-o', '-i', image, str(f), f'::/{f.name}'], check=True)
    cfg = (ROOT / '86box/86box.cfg').read_text()
    cfg = re.sub(r'^hdd_02_.*\n', '', cfg, flags=re.M)
    cfg = re.sub(r'^hdd_01_fn = .*$', f'hdd_01_fn = {disk}', cfg, flags=re.M)
    cfg = cfg.replace('serial3_enabled = 1', 'serial3_enabled = 0')
    cfg = cfg.replace('serial3_passthrough_enabled = 1', 'serial3_passthrough_enabled = 0')
    for key, value in CPUS[cpu].items():
        cfg, n = re.subn(rf'^{key} = .*$', f'{key} = {value}', cfg, flags=re.M)
        if n != 1:
            raise RuntimeError(f'clé {key} absente du modèle 86box.cfg')
    (work / '86box.cfg').write_text(cfg)
    (work / 'nvr').mkdir()
    shutil.copyfile(ROOT / '86box/nvr/hot433a_v451pg.nvr.ok', work / 'nvr/hot433a_v451pg.nvr')
    env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_ACCESSIBILITY='0', NO_AT_BRIDGE='1',
               ALSOFT_DRIVERS='null', SDL_AUDIODRIVER='dummy',
               **{'86BOX_MANAGER_SOCKET': str(work / 'manager.sock')})
    if sound:
        (work / 'alsoft.conf').write_text(f'[wave]\nfile = {work / "son.wav"}\n')
        env.update(ALSOFT_DRIVERS='wave', ALSOFT_CONF=str(work / 'alsoft.conf'))
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
        xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(wfd), '-screen', '0', '1024x768x24',
                                 '-nolisten', 'tcp'], pass_fds=[wfd], stdout=xlog, stderr=xlog,
                                start_new_session=True)
        os.close(wfd)
        try:
            if not select.select([rfd], [], [], 10)[0]:
                raise RuntimeError('Xvfb non prêt')
            env['DISPLAY'] = ':' + os.read(rfd, 32).decode().strip()
            command = ['python3', str(ROOT / 'scripts/with-disk-lock.py'), str(disk), '--',
                       'dbus-run-session', '--', str(box), '-N', '-P', str(work), str(work / '86box.cfg')]
            process = subprocess.Popen(command, env=env, stdout=log, stderr=log, start_new_session=True)
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                if process.poll() is not None:
                    raise RuntimeError((work / '86box.log').read_text(errors='replace'))
                if connection is None:
                    try:
                        connection, _ = manager.accept()
                    except socket.timeout:
                        pass
                if serial is None:
                    match = re.search(r'Slave side is (/dev/pts/\d+)',
                                      (work / '86box.log').read_text(errors='replace'))
                    if match:
                        serial = os.open(match[1], os.O_RDONLY | os.O_NONBLOCK | os.O_NOCTTY)
                        tty.setraw(serial)
                elif select.select([serial], [], [], .1)[0]:
                    transcript.extend(os.read(serial, 4096))
                if b'S3_BOX_FIN' in transcript:
                    break
                time.sleep(.1)
            if b'S3_BOX_FIN' not in transcript:
                raise RuntimeError(f'Pas de fin DOS avant le délai : {work}')
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
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
            if connection:
                connection.close()
            manager.close()
            xvfb.terminate()
            xvfb.wait(timeout=5)
            (work / 'com1.txt').write_bytes(transcript)
    reports = {}
    for name in ['CLOCK.TXT'] + check_files + [out for _, _, out in outputs] + list(outputs_extra):
        subprocess.run(['mcopy', '-o', '-i', image, f'::/{name}', str(work / name)], check=True)
        reports[name] = report(work / name)
    cfg_after = (work / '86box.cfg').read_text()
    # 86Box réécrit sa configuration et omet les clés à leur valeur par défaut (fpu_type = none).
    effective = {k: (m.group(1) if (m := re.search(rf'^{k} = (.*)$', cfg_after, re.M)) else 'absent (défaut)')
                 for k in CPUS[cpu]}
    result = {'cpu': cpu, 'cpu_86box': effective, 'texte': texte, 'dossier': str(work),
              'passages': [{'programme': n, 'passage': p, 'fichier': out, 'brut': reports[out]}
                           for n, p, out in outputs],
              'horloge': (work / 'CLOCK.TXT').read_text(errors='replace').strip(),
              'verifications': {f'{out} : {check}': (work / out).read_bytes().decode('cp850', 'replace').strip()
                                for check, out in zip(checks, check_files)}}
    (work / 'rapports.json').write_text(json.dumps(result, ensure_ascii=False, indent=1))
    return work, result


def summarize(result):
    """Médianes par programme ; refuse les rapports invalides."""
    rows = {}
    for run in result['passages']:
        raw = run['brut']
        ok = raw.get('clock_fault') == '0' and raw.get('status') == '0' and raw.get('underruns') == '0'
        rows.setdefault(run['programme'], []).append((ok, raw))
    table = {}
    for name, runs in rows.items():
        valid = [r for ok, r in runs if ok]
        hz = int(valid[0]['hz']) if valid else 1
        ms = lambda r, k: int(r[k])*1000/hz
        gen = [ms(r, 'phon')+ms(r, 'pcm') for r in valid]
        table[name] = {
            'essais_valides': len(valid), 'essais': len(runs),
            'variant': valid[0].get('variant') if valid else None,
            'generation_ms_mediane': round(statistics.median(gen), 3) if gen else None,
            'generation_ms_min': round(min(gen), 3) if gen else None,
            'generation_ms_max': round(max(gen), 3) if gen else None,
            'activite_pct_mediane': round(statistics.median(
                (int(r['total'])-int(r['idle']))*100/int(r['total']) for r in valid), 2) if valid else None,
            'hash': sorted({r['hash'] for r in valid}), 'samples': sorted({r['samples'] for r in valid}),
            'cpu_class': sorted({r.get('cpu_class') for r in valid}), 'fpu': sorted({r.get('fpu') for r in valid}),
        }
    return table


def reread(work):
    """Reconstruit rapports.json d'un dossier d'essai déjà exécuté (AUTOEXEC.BAT et TXT présents)."""
    work = Path(work)
    autoexec = (work / 'AUTOEXEC.BAT').read_bytes().decode('cp850').splitlines()
    runs = re.findall(r'^C:\\(\S+) C:\\TEXTE\.TXT > C:\\(\w+\.TXT)$', '\n'.join(autoexec), re.M)
    checks = re.findall(r'^C:\\(.+?) > C:\\(\w+V\d+\.TXT)$', '\n'.join(autoexec), re.M)
    cfg_after = (work / '86box.cfg').read_text()
    cpu = re.search(r'^cpu_family = (.*)$', cfg_after, re.M).group(1)
    name = next((k for k, v in CPUS.items() if v['cpu_family'] == cpu), cpu)
    keys = CPUS.get(name, {'cpu_family': cpu})
    effective = {k: (m.group(1) if (m := re.search(rf'^{k} = (.*)$', cfg_after, re.M)) else 'absent (défaut)')
                 for k in keys}
    counts = {}
    passages = []
    for prog, out in runs:
        counts[prog] = counts.get(prog, 0) + 1
        passages.append({'programme': prog, 'passage': counts[prog], 'fichier': out, 'brut': report(work / out)})
    result = {'cpu': name, 'cpu_86box': effective, 'texte': (work / 'TEXTE.TXT').read_bytes().decode('cp850'),
              'dossier': str(work), 'passages': passages,
              'horloge': (work / 'CLOCK.TXT').read_text(errors='replace').strip(),
              'verifications': {f'{out} : {check}': (work / out).read_bytes().decode('cp850', 'replace').strip()
                                for check, out in checks}}
    result['resume'] = summarize(result)
    (work / 'rapports.json').write_text(json.dumps(result, ensure_ascii=False, indent=1))
    return work, result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--relire', metavar='DOSSIER', help='relire un essai existant sans rien exécuter')
    parser.add_argument('--cpu', choices=sorted(CPUS), default='dx4-100')
    parser.add_argument('--passes', type=int, default=3)
    parser.add_argument('--texte', default=TEXTE)
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--verif', action='append', default=[],
                        help='programme (et arguments) exécuté une fois, sortie conservée')
    parser.add_argument('programmes', nargs='*')
    args = parser.parse_args()
    if args.relire:
        work, result = reread(args.relire)
    else:
        if not args.programmes:
            parser.error('au moins un programme')
        work, result = run_batch(args.cpu, args.programmes, args.passes, args.texte, timeout=args.timeout,
                                 checks=args.verif)
        result['resume'] = summarize(result)
    (work / 'rapports.json').write_text(json.dumps(result, ensure_ascii=False, indent=1))
    print(json.dumps({'dossier': str(work), 'cpu_86box': result['cpu_86box'],
                      'verifications': result['verifications'], 'resume': result['resume']},
                     ensure_ascii=False, indent=1))


if __name__ == '__main__':
    main()
