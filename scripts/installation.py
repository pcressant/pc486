#!/usr/bin/env python3
"""Installation neuve : MS-DOS 6.22 FR avec Épaudos, WfW 3.11 FR avec Épaulard et,
si fournis, le disque supplémentaire de MS-DOS 6.22, Word 6.0c pour Windows et
Word 6.0 pour DOS.

Les logiciels tiers sont exclusivement des entrées locales de l'utilisateur.
Aucun téléchargement, aucun disque existant en sortie, aucun D: commun attaché.
"""
import argparse
from contextlib import contextmanager, suppress
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unicodedata
import zipfile

from vmctl import FR, Qmp, type_text
import options_vm

ROOT = Path(__file__).resolve().parents[1]
FLOPPY_SIZE = 1474560
WIN_MARKER = 'PC486_WINDOWS_SETUP_OK'
SUPPL_MARKER = 'PC486_SUPPL_OK'
WORD_MARKER = 'PC486_WORD_OK'
WORD_DOS_MARKER = 'PC486_WORD_DOS_OK'
WORD_DOS_SOURCES = 'INSTALL/WORDDOS'   # gardé sur C: pour relancer INIT (imprimante…)
# Word 6.0 pour DOS français, disquettes 5,25" 1,2 Mo : nom de volume -> rôle.
# GRAMMAR_60 (Grammatik, installateur séparé) est ignoré.
WORD_DOS_VOLUMES = ('SETUP_60', 'PROG_60', 'UTIL_60', 'FONT_60', 'PRINT_60', 'PRINT2_60')
WORD_DOS_FLOPPY = (1228800, FLOPPY_SIZE)
SOUND_DRIVERS = ('SNDBLST2.DRV', 'MSADLIB.DRV')     # Sound Blaster 1.5 et AdLib
SOURCES = 'WININST'      # sources Windows sur C:, même dossier que la VM de référence
# Outils copiés par SETUP.BAT du disque supplémentaire (tous les composants, VGA).
SUPPL_FILES = ('DOSSHELL.EXE', 'DOSSHELL.INI', 'EDLIN.EXE', 'ADOS.COM', 'KBDBUF.SYS',
               'DRVBOOT.BAT', 'MIRROR.COM', 'NETBEUI.DOS')


class InstallError(Exception):
    pass


def run(*args):
    p = subprocess.run([str(a) for a in args], capture_output=True, timeout=120)
    if p.returncode:
        raise InstallError(f'{args[0]} : {p.stderr.decode("utf-8", "replace").strip()}')
    return p.stdout


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def media_number(name):
    m = re.fullmatch(r'disk0*([1-9][0-9]*)\.(?:img|ima)', Path(name).name, re.I)
    return int(m[1]) if m else None


def size_names(sizes):
    return ' ou '.join({FLOPPY_SIZE: '1,44 Mo', 1228800: '1,2 Mo'}.get(n, f'{n} octets') for n in sizes)


def collect_media(source, count, target, sizes=(FLOPPY_SIZE,)):
    """Importe uniquement DISK1..N ; les noms internes d'un ZIP ne sont jamais extraits.
    sizes : tailles d'image admises (1,44 Mo, ou 1,2 Mo pour des disquettes 5,25")."""
    source = source.resolve()
    found = {}
    target.mkdir()

    def put(number, data):
        if number in found:
            raise InstallError(f'support ambigu : plusieurs disquettes {number} dans {source}')
        if len(data) not in sizes:
            raise InstallError(f'disquette {number} : image brute {size_names(sizes)} attendue')
        p = target / f'disk{number}.img'
        p.write_bytes(data)
        found[number] = p

    if source.is_dir():
        for path in sorted(source.rglob('*')):
            n = media_number(path.name)
            if path.is_file() and n is not None and 1 <= n <= count:
                if path.stat().st_size not in sizes:
                    raise InstallError(f'image {size_names(sizes)} attendue : {path}')
                put(n, path.read_bytes())
    elif source.is_file() and zipfile.is_zipfile(source):
        with zipfile.ZipFile(source) as z:
            for entry in z.infolist():
                n = media_number(entry.filename)
                if not entry.is_dir() and n is not None and 1 <= n <= count:
                    if entry.file_size not in sizes:
                        raise InstallError(f'image {size_names(sizes)} attendue dans {source} : {entry.filename}')
                    put(n, z.read(entry))
    else:
        raise InstallError(f'dossier ou archive ZIP locale introuvable : {source}')
    missing = sorted(set(range(1, count + 1)) - found.keys())
    if missing:
        raise InstallError(f'disquettes manquantes dans {source} : {missing}')
    return [found[n] for n in range(1, count + 1)]


def validate_media(dos, windows):
    for path in dos + windows:
        run('mdir', '-i', path, '::/')
    command = run('mtype', '-i', dos[0], '::/COMMAND.COM')
    if b'6.22' not in command:
        raise InstallError('la disquette DOS 1 ne contient pas MS-DOS 6.22')
    inf = run('mtype', '-i', windows[0], '::/SETUP.INF')
    if b'Windows for Workgroups 3.11' not in inf or b'fredll' not in inf:
        raise InstallError('supports Windows pour Workgroups 3.11 français attendus')
    # Le mode batch est documenté sur le support, pas téléchargé ni livré ici.
    run('mtype', '-i', windows[0], '::/SETUP.SHH')
    run('mtype', '-i', windows[0], '::/INSTALL.EXE')


def validate_suppl(path):
    if path.stat().st_size != FLOPPY_SIZE:
        raise InstallError(f'disque supplémentaire : image brute 1,44 Mo attendue : {path}')
    setup = run('mtype', '-i', path, '::/SETUP.BAT')
    if b'Supplemental Utilities' not in setup or b'MS-DOS 6.22' not in setup:
        raise InstallError(f'disque supplémentaire de MS-DOS 6.22 attendu : {path}')
    for name in ('720ID01.BAT', 'SD6COPY.BAT', 'CHOICE.COM', 'EXPAND.EXE', 'GET_FUNC.COM'):
        run('mtype', '-i', path, '::/' + name)


def collect_suppl(source, target):
    """Disque supplémentaire : image seule, ou dossier ou ZIP local (celui de DOS 6.22, où
    il s'appelle par exemple Dsuppl.IMA). Reconnu par son contenu, pas par son nom."""
    source = source.resolve()
    if source.is_file() and not zipfile.is_zipfile(source):
        shutil.copyfile(source, target)
        validate_suppl(target)
        return target
    if source.is_dir():
        candidates = [p.read_bytes for p in sorted(source.rglob('*'))
                      if p.is_file() and p.suffix.lower() in ('.ima', '.img')
                      and p.stat().st_size == FLOPPY_SIZE]
    elif source.is_file():
        z = zipfile.ZipFile(source)
        candidates = [lambda e=e: z.read(e) for e in z.infolist()
                      if not e.is_dir() and Path(e.filename).suffix.lower() in ('.ima', '.img')
                      and e.file_size == FLOPPY_SIZE]
    else:
        raise InstallError(f'disque supplémentaire introuvable : {source}')
    found = 0
    for read in candidates:
        target.write_bytes(read())
        try:
            validate_suppl(target)
        except (InstallError, subprocess.SubprocessError):
            continue
        found += 1
        kept = target.read_bytes()
    if found != 1:
        target.unlink(missing_ok=True)
        raise InstallError(f'disque supplémentaire de MS-DOS 6.22 : {"plusieurs" if found else "aucun"} '
                           f'dans {source}')
    target.write_bytes(kept)
    return target


def disk_partition(path):
    with path.open('rb') as f:
        mbr = f.read(512)
    if mbr[510:512] != b'\x55\xaa':
        raise InstallError('MBR manquant après installation DOS')
    start, length = struct.unpack_from('<II', mbr, 446 + 8)
    if mbr[446] != 0x80 or mbr[450] not in (4, 6, 14) or start < 1 or length < 1:
        raise InstallError('partition DOS active FAT16 introuvable')
    if (start + length) * 512 > path.stat().st_size:
        raise InstallError('partition hors du disque')
    return f'{path}@@{start * 512}'


def volume_label(path):
    """Nom de volume d'une image de disquette, lu par mtools (secteur d'amorce)."""
    out = run('mlabel', '-s', '-i', path, '::').decode('cp850', 'replace')
    m = re.search(r'Volume label is (\S+)', out)
    return m[1].upper() if m else ''


def collect_word_dos(source, target, volumes=WORD_DOS_VOLUMES):
    """Disquettes de Word 6.0 pour DOS, reconnues par leur nom de volume (volumes,
    dans cet ordre). Dossier ou ZIP local ; seules les images de disquette sont lues."""
    source = source.resolve()
    target.mkdir()
    if source.is_dir():
        candidates = [p.read_bytes for p in sorted(source.rglob('*'))
                      if p.is_file() and p.suffix.lower() in ('.ima', '.img')]
        return keep_word_dos(source, target, candidates, volumes)
    if source.is_file() and zipfile.is_zipfile(source):
        # Toutes les lectures ont lieu dans keep_word_dos, archive encore ouverte.
        with zipfile.ZipFile(source) as z:
            candidates = [lambda e=e: z.read(e) for e in z.infolist()
                          if not e.is_dir() and Path(e.filename).suffix.lower() in ('.ima', '.img')]
            return keep_word_dos(source, target, candidates, volumes)
    raise InstallError(f'dossier ou archive ZIP locale introuvable : {source}')


def keep_word_dos(source, target, candidates, volumes):
    """Copie de travail des disquettes reconnues, dans l'ordre de volumes."""
    found = {}
    for i, read in enumerate(candidates):
        data = read()
        if len(data) not in WORD_DOS_FLOPPY:
            continue
        p = target / f'image{i}.img'
        p.write_bytes(data)
        label = volume_label(p)
        if label not in volumes:
            p.unlink()
            continue
        if label in found:
            raise InstallError(f'Word pour DOS : plusieurs disquettes {label} dans {source}')
        found[label] = p
    missing = [v for v in volumes if v not in found]
    if missing:
        raise InstallError(f'Word pour DOS : disquettes manquantes dans {source} : {", ".join(missing)}')
    return [found[v] for v in volumes]


# Fichiers sans lesquels INIT ne démarre pas ou n'installe pas WORD.EXE ; INIT1.EXE et
# WORD60.INF (textes et écrans d'INIT) accompagnent INIT.EXE sur la disquette d'origine.
WORD_DOS_REQUIRED = ((0, ('INIT.EXE', 'INIT1.EXE', 'WORD60.INF')), (1, ('WORD.EX$',)))


def validate_word_dos(media):
    """Fichiers essentiels sur SETUP_60 et PROG_60 ; chaque disquette lisible et non vide."""
    for image in media:
        listing = subprocess.run(['mdir', '-/', '-b', '-i', image, '::'], capture_output=True)
        if listing.returncode or not listing.stdout.strip():
            raise InstallError(f'Word pour DOS : disquette illisible ou vide : {volume_label(image)}')
    for index, names in WORD_DOS_REQUIRED:
        for name in names:
            if subprocess.run(['mdir', '-i', media[index], '::/' + name], capture_output=True).returncode:
                raise InstallError(f'Word pour DOS : {name} absent de la disquette {volume_label(media[index])}')


def write_dos_file(stage, image, name, text, encoding='cp850'):
    p = stage / name.replace('/', '_')
    data = text if isinstance(text, bytes) else \
        text.replace('\r\n', '\n').replace('\n', '\r\n').encode(encoding)
    p.write_bytes(data)
    run('mcopy', '-o', '-i', image, p, '::/' + name)


# Démarrage pendant les étapes d'installation : page de code 850 et clavier
# français actifs, pour que Windows choisisse les polices et tables 850.
SETUP_CONFIG = ('DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=60\nBUFFERS=20\nSTACKS=9,256\n'
                'COUNTRY=033,850,C:\\DOS\\COUNTRY.SYS\n'
                'DEVICE=C:\\DOS\\DISPLAY.SYS CON=(EGA,,1)\n')
SETUP_CODEPAGE = ('MODE CON CODEPAGE PREPARE=((850) C:\\DOS\\EGA.CPI)\n'
                  'MODE CON CODEPAGE SELECT=850\nKEYB FR,,C:\\DOS\\KEYBOARD.SYS\n')


def final_boot_files(word_dos=False, cdrom=False, reseau=False):
    """CONFIG.SYS et AUTOEXEC.BAT de la VM de référence (modèles de dos/) :
    1 = DOS avec Épaudos (défaut après 10 s), 2 = Windows avec Épaulard.
    Avec Word pour DOS, C:\\WORD s'ajoute au PATH, seul changement demandé par INIT.
    Options --cdrom et --reseau : lignes de options_vm.boot_files, sinon aucune."""
    config, autoexec = ((ROOT / 'dos' / name).read_bytes().decode('cp850').replace('\r\n', '\n')
                        for name in ('CONFIG.SYS', 'AUTOEXEC.BAT'))
    path = 'PATH C:\\WINDOWS;C:\\DOS\n'
    if 'C:\\EPAUDOS.COM\n' not in autoexec or 'MENUDEFAULT=DOS,10' not in config or path not in autoexec:
        raise InstallError('modèles dos/CONFIG.SYS ou dos/AUTOEXEC.BAT inattendus')
    if word_dos:
        autoexec = autoexec.replace(path, path[:-1] + ';C:\\WORD\n')
    config, autoexec = options_vm.boot_files(config, autoexec, cdrom, reseau)
    return tuple(t.replace('\n', '\r\n').encode('cp850') for t in (config, autoexec))



def answer_file(name, company):
    # Fichier propre au projet ; les sections sont celles documentées par SETUP.SHH.
    return ('[sysinfo]\nshowsysinfo=no\n[configuration]\nmachine=ibm_compatible\n'
            'display=vga\nmouse=ps2mouse\nnetwork=nonet\nkeyboard=t4s0enha\n'
            'language=fra\nkblayout=fredll\n[windir]\nc:\\windows\n[userinfo]\n'
            f'"{name}"\n"{company}"\n[endinstall]\nconfigfiles=save\nendopt=exit\n')


def dos_action(screen):
    """Décisions bornées aux écrans observés de l'installateur français 6.22."""
    if 'Retirez les disquettes de tous vos lecteurs' in screen:
        return ('done', None)
    if 'Insérez la disquette dans le lecteur' in screen:
        m = re.search(r'Disquette\s+([123])\b', screen)
        if m:
            return ('floppy', int(m[1]))
    for text in ('Bienvenue à Install.', "Configurer l'espace disque non alloué",
                 'Install va redémarrer votre ordinateur.', 'Les paramètres sont corrects.',
                 'Install placera vos fichiers MS-DOS'):
        if text in screen:
            return ('enter', text)
    return None


# Bridage du processeur de QEMU : 2^N ns par instruction (7 : ~7,8 MIPS, comme
# scripts/pc486), ou 'off' : pleine vitesse de TCG, horloge de l'invité sur l'hôte.
# Aucun programme de l'installation ne dépend de la vitesse : pleine vitesse par
# défaut (installation complète en ~5 min au lieu de plus de 20, le 28 octobre 2026).
ICOUNT = 'off'


def icount_option(value):
    if value == 'off' or (value.isdigit() and 0 <= int(value) <= 10):
        return value
    raise argparse.ArgumentTypeError('valeur attendue : off ou un entier de 0 à 10')


class Machine:
    """Une seule image privée ; le processus QEMU reste enfant de l'installateur."""
    def __init__(self, stage, disk, phase, floppy=None, voice=False, boot=None, nic=False, icount=None):
        self.stage, self.phase = stage, phase
        self.runtime = tempfile.TemporaryDirectory(prefix='pc486-install-')
        self.socket = Path(self.runtime.name) / 'qmp.sock'
        self.voice = stage / 'verification-com1.log'
        self.log = (stage / f'{phase}-qemu.log').open('wb')
        filearg = str(disk).replace(',', ',,')
        cmd = ['qemu-system-i386', '-machine', 'pc,accel=tcg,hpet=off', '-rtc', 'base=localtime', '-cpu', '486', '-m', '16',
               *([] if (icount or ICOUNT) == 'off' else ['-icount', f'shift={icount or ICOUNT},sleep=on,align=on']),
               '-drive', f'file={filearg},format=raw,if=ide,index=0',
               '-boot', 'order=' + (boot or ('a' if floppy else 'c')), '-vga', 'cirrus',
               '-display', 'none', '-audiodev', 'none,id=snd0',
               # Carte NE2000 de --reseau, sans aucune destination joignable (restrict=on).
               *(['-netdev', 'user,id=lan,restrict=on,ipv6=off,net=10.0.2.0/24,host=10.0.2.254',
                  '-device', 'ne2k_isa,netdev=lan,iobase=0x300,irq=10'] if nic else ['-nic', 'none']),
               '-device', 'sb16,audiodev=snd0,iobase=0x220,irq=5,dma=1,dma16=5',
               '-device', 'adlib,audiodev=snd0',
               # COM3 de la plage Baum, comme scripts/pc486, sans plage branchée.
               '-chardev', 'null,id=com3', '-device', 'isa-serial,chardev=com3,index=2,iobase=0x3e8,irq=9',
               '-serial', f'file:{self.voice}' if voice else 'null',
               '-qmp', f'unix:{self.socket},server=on,wait=off',
               # Second moniteur pour observer sans gêner l'installateur (captures).
               '-qmp', f'unix:{Path(self.runtime.name) / "observe.sock"},server=on,wait=off']
        if floppy:
            cmd += ['-drive', f'file={str(floppy).replace(",", ",,")},format=raw,if=floppy,index=0']
        self.proc = None
        self.q = None
        try:
            self.proc = subprocess.Popen(cmd, stdout=self.log, stderr=self.log, start_new_session=True)
            end = time.monotonic() + 15
            while not self.socket.exists():
                if self.proc.poll() is not None or time.monotonic() > end:
                    raise InstallError(f'échec QEMU : {stage / (phase + "-qemu.log")}')
                time.sleep(.1)
            self.q = Qmp(str(self.socket))
            self.q.s.settimeout(10)
        except BaseException:
            self.close()
            raise

    def screen(self):
        lines, _, mode = self.q.screen()
        text = '\n'.join(lines).strip()
        (self.stage / f'{self.phase}-screen.txt').write_text(text, encoding='utf-8')
        return text, mode

    def key(self, combo):
        self.q.keys(combo.split('-'))
        time.sleep(.3)

    def text(self):
        """Écran texte seulement (mode 2, 3 ou 7), sinon chaîne vide."""
        screen, mode = self.screen()
        return screen if mode in (2, 3, 7) else ''

    def floppy(self, path):
        self.q.hmp('eject -f floppy0')
        self.q.hmp('change floppy0 ' + json.dumps(str(path), ensure_ascii=False) + ' raw')
        time.sleep(.5)
        self.key('ret')

    def diagnostic(self):
        try:
            self.q.cmd('screendump', filename=str(self.stage / f'{self.phase}-screen.png'), format='png')
        except (OSError, RuntimeError):
            pass

    def close(self):
        if self.proc is not None:
            if self.q is not None and self.proc.poll() is None:
                try:
                    self.q.cmd('quit')
                except (OSError, RuntimeError, ConnectionError):
                    pass
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait()
        if self.q is not None:
            with suppress(OSError):
                self.q.f.close()
            with suppress(OSError):
                self.q.s.close()
        self.log.close()
        self.runtime.cleanup()


@contextmanager
def machine(*args, **kwargs):
    m = Machine(*args, **kwargs)
    try:
        yield m
    except BaseException:
        m.diagnostic()
        raise
    finally:
        m.close()


def install_dos(stage, disk, media, timeout):
    with machine(stage, disk, 'dos', floppy=media[0]) as m:
        end, last = time.monotonic() + timeout, None
        while time.monotonic() < end:
            screen, _ = m.screen()
            action = dos_action(screen)
            if action and action != last:
                last = action
                kind, arg = action
                if kind == 'done':
                    # Install a fermé ses fichiers et demande le retrait des supports.
                    return
                if kind == 'floppy':
                    print(f'  DOS : disquette {arg}/3', flush=True)
                    m.floppy(media[arg - 1])
                else:
                    print('  DOS : ' + arg, flush=True)
                    m.key('ret')
            time.sleep(.5)
        raise InstallError('délai dépassé pendant l’installation DOS ; voir les écrans conservés')


def suppl_action(screen):
    """Réponses de SETUP.BAT (anglais) : tous les composants, MS-DOS Shell en VGA."""
    if SUPPL_MARKER in screen:
        return ('done', None)
    tail = screen.rstrip()
    if tail.endswith('All, Selected, Exit [A,S,X]?'):
        return ('key', 'a')
    if tail.endswith('Do you want to overwrite it?  Yes, No [Y,N]?'):
        return ('key', 'n')     # conserver le fichier de MS-DOS 6.22
    if tail.endswith('You selected "VGA". Is this correct?\nYes, No [Y,N]?'):
        return ('key', 'y')
    if 'Enter your display type by pressing' in screen and tail.endswith('F10 -  Quit'):
        return ('key', 'f5')
    return None


def install_suppl(stage, disk, image, floppy, timeout):
    # Clavier américain pendant cette étape : les touches envoyées sont QWERTY.
    write_dos_file(stage, image, 'CONFIG.SYS', 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=40\n'
                   'SHELL=C:\\COMMAND.COM C:\\ /E:1024 /P\n')
    write_dos_file(stage, image, 'AUTOEXEC.BAT',
                   f'@ECHO OFF\nPATH C:\\DOS\nA:\nCALL SETUP C:\\DOS\nC:\nECHO {SUPPL_MARKER}\n')
    with machine(stage, disk, 'supplementaire', floppy=floppy, boot='c') as m:
        end, last = time.monotonic() + timeout, None
        while time.monotonic() < end:
            action = suppl_action(m.text())
            if action and action != last:
                last = action
                kind, key = action
                if kind == 'done':
                    break
                print(f'  Disque supplémentaire : touche {key.upper()}', flush=True)
                m.key(key)
            elif not action:
                last = None
            time.sleep(.5)
        else:
            raise InstallError('délai dépassé pendant le disque supplémentaire ; écrans conservés')
    for name in SUPPL_FILES:
        if not run('mtype', '-i', image, '::/DOS/' + name):
            raise InstallError(f'outil supplémentaire absent : C:\\DOS\\{name}')


def stage_windows(stage, image, media, name, company):
    merged = stage / 'windows-source'
    merged.mkdir()
    for i, floppy in enumerate(media, 1):
        dest = stage / f'windows-disk{i}'
        dest.mkdir()
        run('mcopy', '-i', floppy, '::/*', dest)
        for p in dest.iterdir():
            out = merged / p.name.upper()
            if out.exists() and digest(out) != digest(p):
                raise InstallError(f'fichiers Windows homonymes différents : {p.name}')
            if not out.exists():
                shutil.copyfile(p, out)
    run('mmd', '-i', image, '::/' + SOURCES)
    run('mcopy', '-o', '-i', image, *sorted(merged.iterdir()), f'::/{SOURCES}/')
    write_dos_file(stage, image, 'PC486.SHH', answer_file(name, company), 'cp1252')
    write_dos_file(stage, image, 'CONFIG.SYS', SETUP_CONFIG)
    write_dos_file(stage, image, 'AUTOEXEC.BAT',
                   '@ECHO OFF\nPATH C:\\DOS\n' + SETUP_CODEPAGE + f'C:\nCD \\{SOURCES}\n'
                   'INSTALL /H:C:\\PC486.SHH\nIF ERRORLEVEL 1 GOTO ECHEC\n'
                   # Pilotes son (format KWAJ) : EXPAND.EXE de Windows, comme à la main.
                   + ''.join(f'C:\\WINDOWS\\EXPAND C:\\{SOURCES}\\{name[:-1]}_ C:\\WINDOWS\\SYSTEM\\{name}\n'
                             for name in SOUND_DRIVERS)
                   + f'ECHO {WIN_MARKER}\nGOTO FIN\n:ECHEC\nECHO PC486_SETUP_FAILED\n:FIN\n')


def install_windows(stage, disk, timeout):
    with machine(stage, disk, 'windows') as m:
        start = time.monotonic()
        end, report = start + timeout, start + 30
        while time.monotonic() < end:
            if time.monotonic() >= report:
                print(f'  Windows : installation en cours ({int(time.monotonic() - start)} s)', flush=True)
                report += 30
            screen = m.text()
            if WIN_MARKER in screen:
                return
            if 'PC486_SETUP_FAILED' in screen:
                raise InstallError('INSTALL.EXE a signalé un échec')
            time.sleep(1)
        raise InstallError('délai dépassé pendant l’installation Windows ; capture conservée')



def remove_section(text, section):
    newline = '\r\n' if '\r\n' in text else '\n'
    out, skip = [], False
    for line in text.splitlines():
        if line.lstrip().startswith('['):
            skip = line.strip().casefold() == f'[{section}]'.casefold()
        if not skip:
            out.append(line)
    return newline.join(out) + newline


class Acme:
    """Produit Microsoft installé par ACME sans dialogue (/Q1 /N), par INSTAUTO.EXE.
    key : option de scripts/installer ; folder : sources sous C:\\INSTALL (vide : à la
    racine, comme la VM de référence de Word) ; disks, sizes : disquettes attendues ;
    signature : (fichier de la disquette 1, octets qu'il doit contenir) ;
    program : exécutable installé, vérifié à la fin. quiet : options sans dialogue
    d'ACME ; sinon l'installateur pose ses questions (Lotus). merge : toutes les
    disquettes dans un même dossier (Lotus : fichiers témoins DISK1..N, aucune demande
    de disquette). answers : réponses jouées par INSTAUTO aux boîtes de l'installateur,
    (début du titre, touches) dans l'ordre ; {NOM} et {SOCIETE} sont remplacés.
    archive : dossier d'installation dans un ZIP (Netscape) au lieu de disquettes ;
    setup : programme lancé dans le dossier de la disquette 1. settings : réglages
    écrits après l'installation, (fichier INI de C:, section, clé, valeur) ; {NOM}
    remplacé. files : fichiers texte créés ensuite, (chemin sur C:, texte)."""
    def __init__(self, key, title, folder, disks, sizes, signature, program, marker,
                 quiet=True, merge=False, answers=(), archive=None, setup='INSTALL.EXE',
                 settings=(), files=()):
        self.key, self.title, self.folder, self.disks = key, title, folder, disks
        self.sizes, self.signature, self.program, self.marker = sizes, signature, program, marker
        self.quiet, self.merge, self.answers = quiet, merge, answers
        self.archive, self.setup, self.settings, self.files = archive, setup, settings, files

    def source(self, n):
        if self.merge:
            return '/'.join(p for p in ('INSTALL', self.folder) if p)
        return '/'.join(p for p in ('INSTALL', self.folder, f'DISK{n}') if p)


# Netscape 4 (Communicator) lit au démarrage NETSCAPE.CFG, à côté de NETSCAPE.EXE ;
# pref() n'y est défini que sur l'objet PrefConfig (essayé dans 4.08 16 bits). Fichier :
# octets décalés de 7, première ligne « // » suivie du MD5 (clé fixe, NUL compris, puis
# le reste du fichier) ; un fichier dont l'empreinte ne correspond pas est refusé. Ce
# schéma est celui de pref_VerifyLockFile (prefapi.c, sources de Mozilla de 1998).
NETSCAPE_CFG_KEY = b'VonGloda5652TX75235ISBN\0'


def netscape_cfg(lines):
    """NETSCAPE.CFG valide contenant ces lignes JavaScript (pref…)."""
    rest = ('\r' + ''.join(line + '\r\n' for line in lines)).encode('cp1252')
    digest = hashlib.md5(NETSCAPE_CFG_KEY + rest).digest()
    head = ('// ' + ' '.join(f'{b:02x}' for b in digest) + '\n').encode('ascii')
    assert len(head) == 51
    return bytes((b + 7) & 255 for b in head + rest)


def js_string(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


# Réglages communs à tous les profils de Communicator, créés ensuite par l'utilisateur
# (assistant du premier lancement) : valeurs par défaut (pref), modifiables, jamais
# imposées (lockPref).
COMMUNICATOR_PREFS = (
    ('network.proxy.type', 1), ('network.proxy.http', '10.0.2.2'),
    ('network.proxy.http_port', 8080), ('network.proxy.ftp', '10.0.2.2'),
    ('network.proxy.ftp_port', 8080), ('browser.startup.homepage', 'http://pc486/'),
    ('browser.startup.page', 1), ('network.hosts.pop_server', '10.0.2.2'),
    ('network.hosts.smtp_server', '10.0.2.2'), ('mail.pop_name', 'pc486'),
    ('mail.server_type', 0), ('security.warn_submit_insecure', False),
    ('security.warn_entering_secure', False), ('security.warn_leaving_secure', False),
    ('general.always_load_images', False),
)


def communicator_cfg():
    lines = []
    for name, value in COMMUNICATOR_PREFS:
        v = ('true' if value else 'false') if isinstance(value, bool) else \
            str(value) if isinstance(value, int) else js_string(value)
        lines.append(f'PrefConfig.pref({js_string(name)}, {v});')
    return netscape_cfg(lines)


NETSCAPE_BOOKMARKS = '''<!DOCTYPE NETSCAPE-Bookmark-file-1>
<TITLE>Signets</TITLE>
<H1>Signets</H1>
<DL><p>
    <DT><A HREF="http://pc486/">Accueil pc486</A>
    <DT><A HREF="http://lite.duckduckgo.com/lite/">DuckDuckGo (version légère)</A>
    <DT><A HREF="http://fr.wikipedia.org/">Wikipédia en français</A>
</DL><p>
'''

SIZES_35 = (FLOPPY_SIZE,)
SIZES_525 = (1228800,)
ACME = {
    'word': Acme('word', 'Word 6.0c', '', 11, SIZES_35,
                 ('INSTALL.STF', (b'WINWORD.EXE', b'6.0.0.3015(1036)')), 'WINWORD/WINWORD.EXE', WORD_MARKER),
    # Excel 5.0c français (CD Select, disquettes 5,25") : ACME avec cabinets EXCEL*.CAB.
    'excel': Acme('excel', 'Excel 5.0c', 'EXCEL', 11, SIZES_525,
                  ('INSTALL.LST', (b'Installation de Microsoft Excel', b'Excel1.cab')), 'EXCEL/EXCEL.EXE',
                  'PC486_EXCEL_OK'),
    # Excel 5.0 français, disquettes 3,5", fichiers compressés _ (même ACME que Word).
    'excel50': Acme('excel50', 'Excel 5.0', 'EXCEL', 10, SIZES_35,
                    ('INSTALL.LST', (b'Installation de Microsoft Excel 5.0',)), 'EXCEL/EXCEL.EXE',
                    'PC486_EXCEL_OK'),
    'access': Acme('access', 'Access 2.0', 'ACCESS', 10, SIZES_525 + SIZES_35,
                   ('INSTALL.LST', (b'Installation de Microsoft Access 2.0',)), 'ACCESS/MSACCESS.EXE',
                   'PC486_ACCESS_OK'),
    # Ami Pro 3.01 français : installateur Lotus (INSTALL.INS), questions à l'écran.
    'amipro': Acme('amipro', 'Ami Pro 3.01', 'AMIPRO', 8, SIZES_35,
                   ('LOTUS.INS', (b'AMISPELL.EXE',)), 'AMIPRO/AMIPRO.EXE', 'PC486_AMIPRO_OK',
                   quiet=False, merge=True, answers=(
                       ('Bienvenue dans le programme Install', '{ENTREE}'),
                       ('Nom de la soci', '{SOCIETE}{ENTREE}'),     # société obligatoire
                       ('Nom et initiales', '{NOM}{ENTREE}'),       # initiales déduites
                       ('Menu principal', '{ENTREE}'),              # ordinateur monoposte
                       ("Type d'installation", '{ENTREE}'),         # installation complète
                       ('Installation sous Windows', '{ENTREE}'),   # Windows, pas NewWave
                       ("Répertoire d'installation", '{ENTREE}'),   # C:\AMIPRO
                       ("Installation d'Ami Pro", '{ENTREE}'),      # « terminée »
                       ('Menu principal', '{TAB}{TAB}{TAB}{ENTREE}'),  # Quitter
                   )),
    # Netscape Navigator 3.01 français (16 bits) : InstallShield 3, dossier N16FR31 du ZIP.
    # Réglages : services de l'hôte de « pc486 start --reseau » (docs/reseau.md).
    'netscape': Acme('netscape', 'Netscape Navigator 3.01', 'NETSCAPE', 1, (),
                     ('SETUP.INI', (b'nsfrfr16.dll',)), 'NETSCAPE/NETSCAPE.EXE',
                     'PC486_NETSCAPE_OK', quiet=False, merge=True, archive='N16FR31',
                     setup='SETUP.EXE', answers=(
                         ('Bienvenue', '{ENTREE}'),
                         ('Netscape Navigator Chemin de destination', '{ENTREE}'),  # C:\NETSCAPE
                         ('Sélectionner un dossier', '{ENTREE}'),     # groupe Netscape Navigator 3.0
                         ('Lisez-moi', '{ESPACE}{ENTREE}'),          # sans ouvrir LISEZMOI
                         ('Question', '{TAB}{ENTREE}'),              # site Setup de Netscape : Non
                     ), settings=tuple(('NETSCAPE/NETSCAPE.INI',) + s for s in (
                         ('Main', 'Home Page', 'http://pc486/'),
                         ('Proxy Information', 'Proxy Type', '1'),       # manuel
                         ('Proxy Information', 'HTTP_Proxy', '10.0.2.2'),
                         ('Proxy Information', 'Http_ProxyPort', '8080'),
                         ('Services', 'SMTP_Server', '10.0.2.2'),
                         ('Services', 'POP_Server', '10.0.2.2'),
                         ('Mail', 'POP Name', 'pc486'),
                         ('User', 'User_Name', '{NOM}'),
                         ('User', 'User_Addr', 'utilisateur@pc486'),
                         # Tout passe en HTTP par le proxy : sinon une alerte à chaque formulaire
                         ('Security', 'Warn Insecure Forms', 'no'),
                     )), files=(
                         # Sans fichier de signets, alerte au premier lancement
                         ('NETSCAPE/BOOKMARK.HTM', NETSCAPE_BOOKMARKS),
                     )),
    # Netscape Communicator 4.08 français (16 bits) : auto-extracteur InstallShield.
    'communicator': Acme('communicator', 'Netscape Communicator 4.08', 'COMMUNIC', 1, (),
                         ('CC16F408.EXE', (b"EXE d'auto-extraction d'InstallShield",)),
                         'NETSCAPE/COMM/PROGRAM/NETSCAPE.EXE', 'PC486_COMMUNICATOR_OK', quiet=False,
                         merge=True, archive='cc16f408', setup='CC16F408.EXE', answers=(
                             # dans l'ordre des boîtes : créer C:\Netscape\Comm (Oui),
                             # Vidéo pour Windows absent (OK), LISEZMOI (Non), fin (OK),
                             # redémarrage plus tard (INSTAUTO quitte Windows)
                             ("Programme d'installation de Netscape Communicator", '{ENTREE}'),
                             ('Contrat de licence', '{ENTREE}'),
                             ("Type d'installation", '{ENTREE}'), ('Question', '{ENTREE}'),
                             ('Informations', '{ENTREE}'),
                             ('Sélectionner le dossier de programme', '{ENTREE}'),
                             ('Commencer à copier', '{ENTREE}'), ('Question', '{TAB}{ENTREE}'),
                             ('Informations', '{ENTREE}'),
                             ('Redémarrer Windows', '{BAS}{ENTREE}')),
                         files=(('NETSCAPE/COMM/PROGRAM/NETSCAPE.CFG', communicator_cfg()),
                                # signets copiés dans chaque nouveau profil
                                ('NETSCAPE/COMM/PROGRAM/DEFAULTS/BOOKMARK.HTM', NETSCAPE_BOOKMARKS))),
    # Eudora Light 3.06 (anglais, Win16 et Win32) : auto-extracteur InstallShield à la
    # racine du ZIP.
    'eudora': Acme('eudora', 'Eudora Light 3.06', 'EUDORA', 1, (),
                   ('EUL306.EXE', (b'Eudora.ini', b'win16.z')), 'EUDORA/EUDORA.EXE',
                   'PC486_EUDORA_OK', quiet=False, merge=True, archive='/', setup='EUL306.EXE',
                   answers=(('InstallShield Self-extracting EXE', '{ENTREE}'), ('Welcome', '{ENTREE}'),
                            ('Software License Agreement', '{ENTREE}'),
                            ('Choose Destination Location', '{ENTREE}'),
                            ('Start Copying Files', '{ENTREE}'),
                            ('Question', '{TAB}{ENTREE}')),          # README : Non
                   # boîte locale de « --reseau » ; pas de serveur ACAP
                   settings=tuple(('EUDORA/EUDORA.INI', 'Settings', k, v) for k, v in (
                       ('UseACAPServer', '0'), ('POPAccount', 'pc486@10.0.2.2'),
                       ('SMTPServer', '10.0.2.2'), ('RealName', '{NOM}'),
                       ('ReturnAddress', 'utilisateur@pc486')))),
}


def validate_acme(product, media):
    name, needles = product.signature
    if product.archive:
        folder = media[0]
        data = (folder / name).read_bytes() if (folder / name).is_file() else b''
        setup = (folder / product.setup).read_bytes()[:2] if (folder / product.setup).is_file() else b''
    else:
        data = run('mtype', '-i', media[0], '::/' + name)
        setup = run('mtype', '-i', media[0], '::/' + product.setup)[:2]
    if not all(n in data for n in needles):
        raise InstallError(f'supports de {product.title} français attendus ({name})')
    if setup != b'MZ':
        raise InstallError(f'{product.setup} de {product.title} absent')


SHORT_NAME = re.compile(r'[A-Z0-9_$~!#%&\-]{1,8}(\.[A-Z0-9_$~!#%&\-]{1,3})?')


def collect_archive(source, target, folder):
    """Dossier d'installation d'un ZIP (ou d'un dossier local) : seuls les fichiers placés
    directement dans `folder` (« / » : la racine), aux noms DOS 8.3, sont copiés dans
    target ; aucun autre chemin du ZIP n'est utilisé."""
    source = source.resolve()
    folder = folder.strip('/')
    target.mkdir(parents=True)
    files = {}
    if source.is_dir():
        base = source / folder if folder and (source / folder).is_dir() else source
        entries = [(p.name, p.read_bytes) for p in base.iterdir() if p.is_file()]
    elif source.is_file() and zipfile.is_zipfile(source):
        z = zipfile.ZipFile(source)
        entries = []
        for e in z.infolist():
            parent, _, name = e.filename.replace('\\', '/').rpartition('/')
            if not e.is_dir() and parent.casefold() == folder.casefold():
                entries.append((name, lambda e=e: z.read(e)))
    else:
        raise InstallError(f'dossier ou archive ZIP locale introuvable : {source}')
    for name, read in entries:
        upper = name.upper()
        if SHORT_NAME.fullmatch(upper) and upper not in files:
            files[upper] = read()
    if not files:
        raise InstallError(f'dossier {folder} absent de {source}')
    for name, data in files.items():
        (target / name).write_bytes(data)
    return [target]


def validate_word(media):
    validate_acme(ACME['word'], media)


def collect_acme(source, target, keys):
    """Supports d'un produit ACME, sous l'une de ses formes (Excel : 5.0c en 1,2 Mo
    avec cabinets, ou 5.0 en 1,44 Mo). Renvoie (produit, disquettes)."""
    errors = []
    target.mkdir(exist_ok=True)
    for key in keys:
        product = ACME[key]
        folder = target / product.key
        try:
            media = collect_product(product, source, folder)
            validate_acme(product, media)
            return product, media
        except InstallError as exc:
            errors.append(f'{product.title} : {exc}')
            shutil.rmtree(folder, ignore_errors=True)
    raise InstallError('supports non reconnus — ' + ' ; '.join(errors))


def collect_product(product, source, target):
    if product.archive:
        return collect_archive(source, target, product.archive)
    return collect_media(source, product.disks, target, product.sizes)


ACCESS_Y2K = {'MSABC200.DLL': 'WINDOWS/SYSTEM', 'MSAJU200.DLL': 'ACCESS'}


def collect_access_y2k(source, target):
    """Correctif de l'an 2000 d'Access 2.0 (ACC2DATE) : deux DLL, d'un dossier ou d'un
    ZIP, éventuellement dans ACC2DATE.EXE (ZIP autoextractible, jamais exécuté). Seuls
    ces deux noms sont lus."""
    source = source.resolve()
    target.mkdir()
    found = {}

    def scan(z, depth=0):
        for entry in z.infolist():
            name = entry.filename.replace('\\', '/').rsplit('/', 1)[-1].upper()
            if entry.is_dir():
                continue
            if name in ACCESS_Y2K:
                found[name] = z.read(entry)
            elif name.endswith('.EXE') and depth == 0 and entry.file_size < 4 * 1024 * 1024:
                data = io.BytesIO(z.read(entry))
                if zipfile.is_zipfile(data):
                    with zipfile.ZipFile(data) as inner:
                        scan(inner, 1)

    if source.is_dir():
        for path in source.rglob('*'):
            if path.is_file() and path.name.upper() in ACCESS_Y2K:
                found[path.name.upper()] = path.read_bytes()
            elif path.is_file() and path.suffix.upper() == '.EXE' and zipfile.is_zipfile(path):
                with zipfile.ZipFile(path) as z:
                    scan(z, 1)
    elif source.is_file() and zipfile.is_zipfile(source):
        with zipfile.ZipFile(source) as z:
            scan(z)
    else:
        raise InstallError(f'dossier ou archive ZIP locale introuvable : {source}')
    missing = sorted(set(ACCESS_Y2K) - found.keys())
    if missing:
        raise InstallError(f'correctif de l’an 2000 incomplet, absent : {", ".join(missing)}')
    out = []
    for name, data in sorted(found.items()):
        if data[:2] != b'MZ':
            raise InstallError(f'{name} n’est pas une DLL')
        (target / name).write_bytes(data)
        out.append(target / name)
    return out


def stage_acme(stage, image, product, media, name, company, binaries, extra_load=True):
    """Supports dans C:\\INSTALL[\\dossier]\\DISKn ; INSTAUTO.EXE lance ACME au démarrage
    de Windows puis quitte Windows. extra_load : garder les autres programmes de load=
    (Épaulard dans un bac à sable), sinon load= ne contient qu'INSTAUTO."""
    if not mexists(image, 'INSTALL/'):
        run('mmd', '-i', image, '::/INSTALL')
    if product.folder and not mexists(image, f'INSTALL/{product.folder}/'):
        run('mmd', '-i', image, f'::/INSTALL/{product.folder}')
    for i, floppy in enumerate(media, 1):
        dest = stage / f'{product.key}-disk{i}'
        if product.archive:
            shutil.copytree(floppy, dest)
        else:
            dest.mkdir()
            run('mcopy', '-i', floppy, '::/*', dest)
        if not mexists(image, product.source(i) + '/'):
            run('mmd', '-i', image, '::/' + product.source(i))
        run('mcopy', '-o', '-i', image, *sorted(dest.iterdir()), f'::/{product.source(i)}/')
    run('mcopy', '-o', '-i', image, binaries / 'INSTAUTO.EXE', '::/INSTAUTO.EXE')
    if not mexists(image, 'TEMP/'):
        run('mmd', '-i', image, '::/TEMP')
    ini = load_ini_editor()
    win = stage / 'WIN.INI'
    text = run('mtype', '-i', image, '::/WINDOWS/WIN.INI').decode('cp1252')
    # Mode sans dialogue d'ACME : installation par défaut, nom et société fournis.
    setup = '\\'.join(['C:'] + product.source(1).split('/') + [product.setup])
    command = setup
    if product.quiet:
        command += f' /Q1 /N "{name}"' + (f' /O "{company}"' if company else '')
    if extra_load:
        text = epaulard_ini().edit_load(text, add=r'C:\INSTAUTO.EXE')
    else:
        text = ini.setting(text, 'windows', 'load', r'C:\INSTAUTO.EXE')
    text = ini.setting(text, 'pc486', 'commande', command)
    text = ini.setting(text, 'pc486', 'etat', '')
    for i, (title, keys) in enumerate(product.answers, 1):
        keys = keys.replace('{NOM}', name).replace('{SOCIETE}', company or name)
        text = ini.setting(text, 'pc486', f'reponse{i}', f'{title}|{keys}')
    win.write_bytes(text.encode('cp1252'))
    run('mcopy', '-o', '-i', image, win, '::/WINDOWS/WIN.INI')
    write_dos_file(stage, image, 'CONFIG.SYS', SETUP_CONFIG)
    write_dos_file(stage, image, 'AUTOEXEC.BAT',
                   '@ECHO OFF\nPATH C:\\WINDOWS;C:\\DOS\nSET TEMP=C:\\TEMP\n' + SETUP_CODEPAGE +
                   f'C:\nCD \\\n{RESEAU_AUTO}WIN\nECHO {product.marker}\n')


# Disque déjà configuré pour le réseau (bac à sable) : sans carte, WfW se bloquerait.
RESEAU_AUTO = 'IF EXIST C:\\WINDOWS\\RESEAU.EXE C:\\WINDOWS\\RESEAU.EXE AUTO\n'


def stage_word(stage, image, media, name, company, binaries):
    stage_acme(stage, image, ACME['word'], media, name, company, binaries, extra_load=False)


def install_acme(stage, disk, image, product, timeout, voice=False):
    """Démarre Windows jusqu'au retour au DOS ; INSTAUTO a quitté Windows après ACME.
    Retourne le texte de WIN.INI, débarrassé de [pc486] et d'INSTAUTO."""
    with machine(stage, disk, product.key, voice=voice) as m:
        start = time.monotonic()
        end, report = start + timeout, start + 60
        while time.monotonic() < end:
            if time.monotonic() >= report:
                print(f'  {product.title} : installation en cours ({int(time.monotonic() - start)} s)',
                      flush=True)
                report += 60
            if product.marker in m.text():
                break
            time.sleep(2)
        else:
            raise InstallError(f'délai dépassé pendant l’installation de {product.title} ; capture conservée')
    journal = subprocess.run(['mtype', '-i', image, '::/INSTAUTO.LOG'], capture_output=True).stdout
    (stage / f'{product.key}-instauto.log').write_bytes(journal)
    text = run('mtype', '-i', image, '::/WINDOWS/WIN.INI').decode('cp1252')
    if 'etat=fini' not in text.replace(' ', '').lower():
        raise InstallError(f'INSTAUTO n’a pas terminé ; voir {product.key}-instauto.log')
    exe = subprocess.run(['mtype', '-i', image, '::/' + product.program], capture_output=True).stdout
    if exe[:2] != b'MZ':
        raise InstallError(f'{product.title} non installé ; voir {product.key}-instauto.log '
                           f'et {product.key}-screen.png')
    text = epaulard_ini().edit_load(remove_section(text, 'pc486'), remove=(r'C:\INSTAUTO.EXE',))
    run('mdel', '-i', image, '::/INSTAUTO.EXE', '::/INSTAUTO.LOG')
    return text


def configure_product(stage, image, product, name):
    """Réglages d'un produit après son installation (fichiers INI de C:)."""
    ini = load_ini_editor()
    files = {}
    for path, section, key, value in product.settings:
        if path not in files:
            files[path] = run('mtype', '-i', image, '::/' + path).decode('cp1252')
        files[path] = ini.setting(files[path], section, key, value.replace('{NOM}', name))
    files.update((path, text if isinstance(text, bytes) else text.replace('\n', '\r\n'))
                 for path, text in product.files)
    for path, text in files.items():
        local = stage / f'{product.key}-{Path(path).name}'
        local.write_bytes(text if isinstance(text, bytes) else text.encode('cp1252'))
        run('mcopy', '-o', '-i', image, local, '::/' + path)


def write_win_ini(stage, image, text):
    win = stage / 'WIN.INI'
    win.write_bytes(text.encode('cp1252'))
    run('mcopy', '-o', '-i', image, win, '::/WINDOWS/WIN.INI')


def install_office(stage, disk, image, product, media, timeout, name, company, binaries):
    """Excel, Access (ACME par défaut), Ami Pro (réponses d'INSTAUTO), puis WIN.INI
    rendu sans INSTAUTO."""
    stage_acme(stage, image, product, media, name, company, binaries, extra_load=False)
    write_win_ini(stage, image, install_acme(stage, disk, image, product, timeout))
    configure_product(stage, image, product, name)


def install_access_y2k(image, dlls):
    for dll in dlls:
        run('mcopy', '-o', '-i', image, dll, f'::/{ACCESS_Y2K[dll.name]}/{dll.name}')


def install_word(stage, disk, image, timeout, name, company):
    text = install_acme(stage, disk, image, ACME['word'], timeout)
    if 'Word.Document.6' not in text:
        raise InstallError('Word 6.0c non installé ; voir word-instauto.log et word-screen.png')
    # Nom proposé par Word (Outils, Options), écrit seulement en mode interactif.
    ini = load_ini_editor()
    text = ini.setting(ini.setting(text, 'MS User Info', 'DefName', name),
                       'MS User Info', 'DefCompany', company)
    write_win_ini(stage, image, text)


# Écrans d'INIT (Word 6.0 pour DOS, français), relevés le 27 octobre 2026.
# Texte caractéristique -> libellé à choisir dans la liste, ou action.
# Mode Texte imposé : en mode graphique, Épaudos se met en veille.
WORD_DOS_SCREENS = (
    ('BIENVENUE AU PROGRAMME INIT', 'ret'),
    ('Pour installer Word vous devez connaître', 'ret'),
    ('Installer sur un réseau', 'Installer sur disque dur'),
    ('Modifier une version existante de Word', 'Installer une nouvelle version de Word'),
    ('INIT propose le répertoire suivant', 'directory'),
    ('Veuillez sélectionner votre pays', 'France'),
    ('sélectionnez votre clavier dans le menu suivant', 'France'),
    # Clavier par le BIOS : les commandes d'Épaudos (INT 15h/4Fh) restent vues.
    ("type d'ordinateur que vous utilisez", 'Gestionnaire de clavier pour résidents et émulation 3270'),
    ("type d'adaptateur d'affichage", 'IBM PS/2 Modèle 50/60/70/80 VGA ou compatible'),
    ("Ne pas installer les gestionnaires d'imprimantes", "Ne pas installer les gestionnaires d'imprimantes"),
    ("vérificateur d'orthographe", "Installer le vérificateur d'orthographe"),
    ('dictionnaire des synonymes', 'Installer le dictionnaire des synonymes'),
    ('utilitaires de conversion', 'Installer tous les utilitaires de conversion'),
    ("Installer les fichiers d'Aide", "Installer les fichiers d'Aide"),
    ('Installer le Didacticiel', 'Installer le Didacticiel'),
    ('gestionnaire de souris Microsoft', 'Ne pas mettre à jour le gestionnaire de souris Microsoft'),
    ('Personnaliser les paramètres Word', 'Personnaliser les paramètres Word'),
    ('Exécuter Word en mode Texte', 'Exécuter Word en mode Texte'),
    ("Ne pas tester l'affichage du caractère", "Ne pas tester l'affichage du caractère"),
    ('Glisser-Déplacer pour modifier le texte', 'Utiliser Glisser-Déplacer pour modifier le texte'),
    ('touches de fonction Word 6.0', 'Utiliser les touches de fonction Word 6.0'),
    ('INS est la touche de refrappe', 'INS est la touche de refrappe'),
    ('Décrivez ce que vous voyez', '2 nuances de bleu et 2 nuances de rouge (4 barres)'),
    ('A4 (International)', 'A4 (International)'),
    ('Voulez-vous changer le nom', 'Ne pas changer le nom'),
    ('Tapez votre nom ou le nom de la société', 'name'),
    # Les fichiers de démarrage sont ensuite ceux des modèles dos/ (PATH C:\WORD).
    ('Ne pas mettre à jour les fichiers système', 'Ne pas mettre à jour les fichiers système'),
    ('Ne pas enregistrer les fichiers système', 'Ne pas enregistrer les fichiers système'),
    ('Installer les polices TrueType', 'Installer les polices TrueType'),
    ('a été installé avec succès', 'ret'),
)


# Messages d'INIT, relevés dans WORD60.INF de la disquette SETUP_60. Une demande de
# disquette réunit l'impératif, le nom de l'unité et l'invite ENTREE dans cet ordre :
# « vous insérez un caractère spécial » ou « démarrez … à partir de l'unité de C: »
# sont des écrans d'information.
WORD_DOS_MEDIA = re.compile(r"Insérez la .{1,60}? dans l'unité \S{0,3} ?, puis appuyez sur ENTR[EÉ]E")
WORD_DOS_ERRORS = (
    "INIT a des problèmes pour lire",            # disquette illisible
    "INIT s'est interrompu",
    "INIT n'a pas pu",
    "a reçu une erreur",                         # … de FUSDI6.EXE
    "Erreur d'accès au fichier source",
    "Impossible de créer le fichier destination",
    "Pas assez de mémoire pour décompresser",
    "Pas assez d'espace disque pour décompresser",
    "Le fichier d'initialisation manque ou est endommagé",
)


def screen_words(screen):
    """Texte de l'écran sans cadres ni retours à la ligne : une phrase peut être coupée."""
    return ' '.join(re.sub(r'[│║┌┐└┘╔╗╚╝─═■░▒▓█\x18\x19]', ' ', screen).split())


def word_dos_action(screen, labels=()):
    """Action pour un écran d'INIT ; None pendant la copie ou un écran inconnu.
    labels : libellés de la liste affichée (menu_rows). Un écran peut citer une autre
    question (« Si vous avez choisi de mettre à jour le gestionnaire de souris… » au-dessus
    du choix des fichiers système) : le choix présent dans la liste l'emporte alors.
    Une demande de disquette ou une erreur d'INIT arrête l'installation."""
    words = screen_words(screen)
    if WORD_DOS_MEDIA.search(words):
        raise InstallError('INIT de Word pour DOS demande une disquette ; écran conservé')
    for error in WORD_DOS_ERRORS:
        if error in words:
            raise InstallError(f'INIT de Word pour DOS signale une erreur (« {error} ») ; écran conservé')
    found = [action for marker, action in WORD_DOS_SCREENS if marker in screen or marker in words]
    if not found:
        return None
    return next((action for action in found if action in labels), found[0])


FRAME_TOP, FRAME_SIDE, FRAME_BOTTOM = '┌╔', '│║', '└╚'


def menu_rows(video, cols=80, rows=25):
    """Éléments des listes encadrées d'INIT : (texte, en surbrillance), d'après la
    mémoire vidéo. Seules les rangées d'un cadre ┌…└ comptent : un titre au-dessus
    n'est pas un élément. La surbrillance est une barre : libellé d'une seule couleur,
    prolongée sur la case voisine, et différente de celle du cadre. Une couleur seule
    (texte blanc brillant, par exemple) ne suffit pas."""
    text = [bytes(video[r * cols * 2:(r + 1) * cols * 2][0::2]).decode('cp850', 'replace')
            for r in range(rows)]
    attr = [video[r * cols * 2:(r + 1) * cols * 2][1::2] for r in range(rows)]
    items = []
    r = 0
    while r < rows:
        col = next((c for c, ch in enumerate(text[r]) if ch in FRAME_TOP), None)
        if col is None:
            r += 1
            continue
        normal = attr[r][col]
        r += 1
        while r < rows and text[r][col] in FRAME_SIDE:
            inner = text[r][col + 1:]
            # Les flèches de défilement (18h, 19h) et les blocs suivent le libellé.
            label = re.split(r'\s{2,}|[\x00-\x1f│║░█▒]', inner.lstrip(' ■'))[0].strip()
            if label:
                first = col + 1 + len(inner) - len(inner.lstrip(' ■'))
                last = first + len(label)
                cells = set(attr[r][first:last])

                def continues(c):
                    return col < c < cols and text[r][c] in ' ■' and attr[r][c] in cells
                bar = (len(cells) == 1 and normal not in cells and
                       (continues(last) or continues(first - 1)))
                items.append((label, bar))
            r += 1
        if r < rows and text[r][col] in FRAME_BOTTOM:
            r += 1
    return items


def menu_current(items):
    """Indice de l'unique élément en surbrillance ; None si aucun ou plusieurs."""
    lit = [i for i, (_, on) in enumerate(items) if on]
    return lit[0] if len(lit) == 1 else None


def menu_key(items, label):
    """Touche qui rapproche la surbrillance du libellé voulu ; 'ret' s'il est choisi.
    None si la liste n'est pas encore dessinée, si le libellé n'y figure pas ou si la
    surbrillance est absente ou ambiguë."""
    names = [t for t, _ in items]
    if label not in names:
        return None
    current = menu_current(items)
    if current is None:
        return None
    target = names.index(label)
    return 'ret' if current == target else ('down' if target > current else 'up')


class MenuWalk:
    """Contrôle les déplacements dans une liste : chaque flèche doit amener en
    surbrillance le libellé voisin dans le bon sens (comparé par libellé : une liste
    qui défile garde la barre au même rang), et le nombre de flèches reste borné par
    la taille de la liste. Sinon, échec explicite plutôt qu'une alternance jusqu'au
    délai de l'étape."""
    SETTLE = 3.0

    def __init__(self):
        self.action = None

    def ready(self, action, items, key, now):
        """True si la touche peut partir ; False s'il faut attendre le réaffichage."""
        names = [t for t, _ in items]
        current = names[menu_current(items)]
        if action != self.action:
            self.action, self.moves, self.sent = action, 0, None
        if self.sent:
            before, expected, at = self.sent
            if current == before:
                if now - at < self.SETTLE:
                    return False
                raise InstallError(f'INIT de Word pour DOS : la surbrillance ne suit pas les flèches '
                                   f'(« {action} ») ; écran conservé')
            if expected is not None and current != expected:
                raise InstallError(f'INIT de Word pour DOS : surbrillance sur « {current} » au lieu de '
                                   f'« {expected} » ; écran conservé')
            self.sent = None
        if key == 'ret':
            self.action = None              # choix validé : l'écran suivant repart de zéro
        else:
            self.moves += 1
            if self.moves > len(items):
                raise InstallError(f'INIT de Word pour DOS : « {action} » jamais atteint ; écran conservé')
            step = names.index(current) + (1 if key == 'down' else -1)
            self.sent = (current, names[step] if 0 <= step < len(names) else None, now)
        return True


def ascii_name(name):
    """INIT met le nom en majuscules : saisie sans accents, caractères AZERTY connus."""
    text = unicodedata.normalize('NFKD', name).encode('ascii', 'ignore').decode()
    return ''.join(c for c in text if c in FR)[:30] or 'Utilisateur'


def tree_files(root):
    """Fichiers sous root : chemin relatif en majuscules (noms FAT) -> fichier."""
    return {p.relative_to(root).as_posix().upper(): p for p in sorted(root.rglob('*')) if p.is_file()}


def stage_word_dos(stage, image, media):
    # Les disquettes d'origine n'ont que des fichiers à la racine (relevé du 28 octobre
    # 2026) ; un sous-répertoire serait néanmoins copié avec son arborescence.
    merged = stage / 'word-dos-source'
    merged.mkdir()
    for i, floppy in enumerate(media):
        dest = stage / f'word-dos-disk{i}'
        dest.mkdir()
        run('mcopy', '-s', '-n', '-i', floppy, '::/*', dest)   # erreur de lecture : arrêt
        files = tree_files(dest)
        if not files:
            raise InstallError(f'Word pour DOS : disquette vide ou illisible : {volume_label(floppy)}')
        for rel, p in files.items():
            out = merged / rel
            if out.exists() and digest(out) != digest(p):
                raise InstallError(f'fichiers Word pour DOS homonymes différents : {rel}')
            if not out.exists():
                out.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(p, out)
    if not mexists(image, 'INSTALL/'):
        run('mmd', '-i', image, '::/INSTALL')
    run('mmd', '-i', image, '::/' + WORD_DOS_SOURCES)
    run('mcopy', '-s', '-o', '-i', image, *sorted(merged.iterdir()), f'::/{WORD_DOS_SOURCES}/')
    # Relecture de C: : chaque fichier des disquettes doit y être, identique.
    check = stage / 'word-dos-check'
    check.mkdir()
    run('mcopy', '-s', '-n', '-i', image, f'::/{WORD_DOS_SOURCES}/*', check)
    expected, copied = tree_files(merged), tree_files(check)
    missing = sorted(set(expected) - set(copied))
    if missing:
        raise InstallError(f'Word pour DOS : fichiers absents de C:\\{WORD_DOS_SOURCES}'
                           f' : {", ".join(missing[:5])}')
    for rel, p in expected.items():
        if digest(copied[rel]) != digest(p):
            raise InstallError(f'Word pour DOS : copie différente sur C: : {rel}')
    # Toutes les disquettes dans un seul dossier : INIT ne demande aucun changement.
    write_dos_file(stage, image, 'CONFIG.SYS', SETUP_CONFIG)
    write_dos_file(stage, image, 'AUTOEXEC.BAT',
                   '@ECHO OFF\nPATH C:\\DOS\n' + SETUP_CODEPAGE +
                   f'C:\nCD \\{WORD_DOS_SOURCES.replace("/", chr(92))}\nINIT\nC:\nCD \\\nECHO {WORD_DOS_MARKER}\n')


def install_word_dos(stage, disk, image, timeout, name):
    walk = MenuWalk()
    with machine(stage, disk, 'word-dos') as m:
        end, last, waited = time.monotonic() + timeout, None, 0
        while time.monotonic() < end:
            screen = m.text()
            if WORD_DOS_MARKER in screen:
                break
            listed = [t for t, _ in menu_rows(m.q.memory(0xB8000, 4000))] if screen else []
            action = word_dos_action(screen, listed)
            if action is None:
                time.sleep(1)
                continue
            if action in ('ret', 'name', 'directory'):
                key = 'ret'
            else:
                items = menu_rows(m.q.memory(0xB8000, 4000))
                key = menu_key(items, action)
                if key is None:
                    # Liste en cours de dessin ; au-delà, choix réellement absent.
                    waited += 1
                    if waited > 15:
                        raise InstallError(f'INIT de Word pour DOS : choix « {action} » absent ; écran conservé')
                    time.sleep(1)
                    continue
                waited = 0
                if not walk.ready(action, items, key, time.monotonic()):
                    time.sleep(.3)
                    continue
            if key == 'ret':
                # Écran pas encore remplacé ; le nom saisi change le texte de son écran.
                if (screen, action) == last or (action == 'name' and last and last[1] == 'name'):
                    time.sleep(1)
                    continue
                last = (screen, action)
                if action == 'directory' and 'C:\\WORD' not in screen:
                    raise InstallError('INIT propose un autre répertoire que C:\\WORD ; écran conservé')
                if action == 'name':
                    print('  Word pour DOS : nom du propriétaire', flush=True)
                    type_text(m.q, ascii_name(name), fr=True, delay=.1)
                elif action not in ('ret', 'directory'):
                    print(f'  Word pour DOS : {action}', flush=True)
            m.key(key)
            time.sleep(1.5 if key == 'ret' else .5)
        else:
            raise InstallError('délai dépassé pendant l’installation de Word pour DOS ; écran conservé')
    for installed in ('WORD/WORD.EXE', 'WORD/MW.INI', 'WORD/SCREEN.VID'):
        if not run('mtype', '-i', image, '::/' + installed):
            raise InstallError(f'Word pour DOS : fichier installé vide : C:\\{installed}')
    if run('mtype', '-i', image, '::/WORD/WORD.EXE')[:2] != b'MZ':
        raise InstallError('Word pour DOS : C:\\WORD\\WORD.EXE non décompressé')


def configure_groups(stage, image):
    # WfW FR /H peut écrire les noms de groupes accentués en ANSI alors que les
    # noms FAT sont en OEM. Des alias ASCII évitent la boîte modale au premier boot.
    progman = run('mtype', '-i', image, '::/WINDOWS/PROGMAN.INI').decode('cp1252')
    # En page 850, les fichiers portent déjà le nom ASCII : seul PROGMAN.INI change.
    for original, alias in (('RÉSEAU.GRP', 'RESEAU.GRP'), ('DÉMARRAG.GRP', 'DEMARRAG.GRP')):
        if original not in progman:
            continue
        if not mexists(image, 'WINDOWS/' + alias):
            local = stage / alias
            local.write_bytes(run('mtype', '-i', image, '::/WINDOWS/' + original))
            run('mcopy', '-o', '-i', image, local, '::/WINDOWS/' + alias)
        progman = progman.replace(original, alias)
    local = stage / 'PROGMAN.INI'
    local.write_bytes(progman.encode('cp1252'))
    run('mcopy', '-o', '-i', image, local, '::/WINDOWS/PROGMAN.INI')

def mexists(image, path):
    return subprocess.run(['mdir', '-i', image, '::/' + path], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode == 0


def configure_sound(system_text):
    """Sound Blaster 1.5 (sndblst2) et AdLib, A220 I5, comme la VM de référence."""
    ini = load_ini_editor()
    for section, key, value in (('drivers', 'wave', 'sndblst2.drv'), ('drivers', 'midi', 'msadlib.drv'),
                                ('sndblst2.drv', 'port', '220'), ('sndblst2.drv', 'int', '5'),
                                # Section lue par SNDBLST2.DRV ; sans elle, IRQ 7 et boîte d'erreur.
                                ('sndblst.drv', 'port', '220'), ('sndblst.drv', 'int', '5')):
        system_text = ini.setting(system_text, section, key, value)
    return system_text


def load_script(module, filename):
    spec = importlib.util.spec_from_file_location(module, ROOT / 'scripts' / filename)
    script = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(script)
    return script


def load_ini_editor():
    # Import du même éditeur INI que le réglage COM3 existant.
    return load_script('configure_braille', 'configure-braille.py')


def epaulard_ini():
    """Seule implémentation de load= pour Épaulard, partagée avec epaulard/build.sh."""
    return load_script('configure_epaulard', 'configure-epaulard.py')


def configure(stage, image, binaries, braille, epaudos, word_dos=False, cdrom=False, reseau=False):
    ini = load_ini_editor()
    for name in ('WIN.INI', 'SYSTEM.INI'):
        p = stage / name
        p.write_bytes(run('mtype', '-i', image, '::/WINDOWS/' + name))
    win, system = stage / 'WIN.INI', stage / 'SYSTEM.INI'
    text = win.read_bytes().decode('cp1252')
    win.write_bytes(epaulard_ini().configure(text).encode('cp1252'))
    system.write_bytes(configure_sound(system.read_bytes().decode('cp1252')).encode('cp1252'))
    if braille:
        ini.configure(win, system)
    for directory in ('EPAULARD', 'TEMP'):
        if not mexists(image, directory + '/'):
            run('mmd', '-i', image, '::/' + directory)
    for name in ('EPAULARD.EXE', 'EPAUHOOK.DLL'):
        run('mcopy', '-o', '-i', image, binaries / name, '::/EPAULARD/' + name)
    for name in ('WIN.INI', 'SYSTEM.INI'):
        run('mcopy', '-o', '-i', image, stage / name, '::/WINDOWS/' + name)
    configure_groups(stage, image)
    # Lecteur DOS du projet, chargé par la branche DOS d'AUTOEXEC.BAT.
    run('mcopy', '-o', '-i', image, epaudos, '::/EPAUDOS.COM')
    # Réglages vocaux d'Épaulard de la VM de référence (débit 5, sans écho clavier).
    write_dos_file(stage, image, 'EPAULARD/EPAULARD.INI', (ROOT / 'dos/EPAULARD.INI').read_text(), 'cp1252')
    run('mdel', '-i', image, '::/PC486.SHH')     # réponses de Setup, plus utiles
    config, autoexec = final_boot_files(word_dos, cdrom, reseau)
    write_dos_file(stage, image, 'CONFIG.SYS', config)
    write_dos_file(stage, image, 'AUTOEXEC.BAT', autoexec)
    required = ['IO.SYS', 'MSDOS.SYS', 'DOS/POWER.EXE', 'DOS/EMM386.EXE', 'WINDOWS/HIMEM.SYS',
                'WINDOWS/IFSHLP.SYS', 'WINDOWS/SMARTDRV.EXE', 'WINDOWS/WIN.COM',
                'WINDOWS/PROGMAN.EXE', 'WINDOWS/SYSTEM.INI']
    required += ['WINDOWS/SYSTEM/' + name for name in SOUND_DRIVERS]
    for name in required:
        if not run('mtype', '-i', image, '::/' + name):
            raise InstallError(f'fichier installé vide : {name}')
    for name in SOUND_DRIVERS:
        if run('mtype', '-i', image, '::/WINDOWS/SYSTEM/' + name)[:2] != b'MZ':
            raise InstallError(f'pilote son non décompressé : {name}')
    for name in ('EPAULARD.EXE', 'EPAUHOOK.DLL'):
        if run('mtype', '-i', image, '::/EPAULARD/' + name) != (binaries / name).read_bytes():
            raise InstallError(f'copie incorrecte : {name}')
    if run('mtype', '-i', image, '::/EPAUDOS.COM') != epaudos.read_bytes():
        raise InstallError('copie incorrecte : EPAUDOS.COM')
    installed = run('mtype', '-i', image, '::/WINDOWS/SYSTEM.INI').decode('cp1252')
    if 'oemansi.bin=xlat850.bin' not in installed.lower():
        raise InstallError('Windows n’a pas été installé en page de code 850')


def install_cdrom(stage, image, driver):
    """UDVD2.SYS dans C:\\DOS ; les lignes de démarrage viennent de final_boot_files."""
    local = stage / 'UDVD2.SYS'
    local.write_bytes(driver)
    run('mcopy', '-o', '-i', image, local, '::/DOS/UDVD2.SYS')
    if not run('mtype', '-i', image, '::/DOS/MSCDEX.EXE'):
        raise InstallError('MSCDEX.EXE absent de C:\\DOS')


RESEAU_MARKER = 'PC486_RESEAU_OK'


def install_reseau(stage, disk, image, tcp, reseau_exe, timeout):
    """Réseau WfW : fichiers TCP/IP-32 copiés, fichiers WfW décompressés dans
    l'invité, SYSTEM.INI et PROTOCOL.INI de l'installation réseau relevée."""
    for folder, name in options_vm.TCP_FILES:
        local = stage / ('tcp-' + name)
        local.write_bytes(tcp[name])
        run('mcopy', '-o', '-i', image, local, f'::/{folder}/{name}')
    local = stage / 'tcp-OEM0.INF'
    local.write_bytes(tcp['OEMSETUP.INF'])
    run('mcopy', '-o', '-i', image, local, '::/WINDOWS/SYSTEM/OEM0.INF')
    run('mcopy', '-o', '-i', image, reseau_exe, '::/WINDOWS/RESEAU.EXE')
    for folder, name in options_vm.WFW_FILES:
        if not mexists(image, f'{SOURCES}/{name[:-1]}_'):
            raise InstallError(f'fichier réseau absent des sources Windows : {name[:-1]}_')
    saved = {name: run('mtype', '-i', image, '::/' + name) for name in ('CONFIG.SYS', 'AUTOEXEC.BAT')}
    write_dos_file(stage, image, 'CONFIG.SYS', SETUP_CONFIG)
    write_dos_file(stage, image, 'AUTOEXEC.BAT', '@ECHO OFF\nPATH C:\\WINDOWS;C:\\DOS\n'
                   + options_vm.expand_batch(SOURCES) + f'ECHO {RESEAU_MARKER}\n')
    with machine(stage, disk, 'reseau') as m:
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if RESEAU_MARKER in m.text():
                break
            time.sleep(1)
        else:
            raise InstallError('décompression des fichiers réseau non terminée ; capture conservée')
    for name, data in saved.items():
        write_dos_file(stage, image, name, data)
    for folder, name in options_vm.WFW_FILES:
        if run('mtype', '-i', image, f'::/{folder}/{name}')[:4] == b'KWAJ' or \
                not run('mtype', '-i', image, f'::/{folder}/{name}'):
            raise InstallError(f'fichier réseau non décompressé : {name}')
    ini = load_ini_editor()
    system = stage / 'SYSTEM-reseau.INI'
    text = run('mtype', '-i', image, '::/WINDOWS/SYSTEM.INI').decode('latin-1')
    system.write_bytes(options_vm.configure_system(text, ini.setting).encode('latin-1'))
    run('mcopy', '-o', '-i', image, system, '::/WINDOWS/SYSTEM.INI')
    write_dos_file(stage, image, 'WINDOWS/PROTOCOL.INI', options_vm.PROTOCOL_INI)


def check_reseau(image, on):
    """Après démarrage : RESEAU.EXE a basculé SYSTEM.INI selon la carte présente."""
    text = run('mtype', '-i', image, '::/WINDOWS/SYSTEM.INI').decode('latin-1')
    winnet = options_vm.get(text, 'Network', 'winnet')
    if (winnet == 'wfwnet/00025100') != on:
        raise InstallError(f'RESEAU.EXE : réseau {"absent" if on else "laissé"} dans SYSTEM.INI '
                           f'{"avec" if on else "sans"} carte (winnet={winnet})')


def wait_prompt(m, end):
    """Invite C:\\> en dernière ligne de l'écran texte."""
    while time.monotonic() < end:
        lines = [line for line in m.text().splitlines() if line.strip()]
        if lines and lines[-1].strip() == 'C:\\>':
            return True
        time.sleep(1)
    return False


def boot_menu(m, end, choice):
    """Menu de CONFIG.SYS : 1 = DOS, 2 = Windows, puis Entrée."""
    while time.monotonic() < end:
        if 'Windows pour Workgroups 3.11' in m.text():
            m.key(choice)
            m.key('ret')
            return
        time.sleep(.5)
    raise InstallError('menu de démarrage non affiché')


# Les pilotes réseau de WfW (NE2000, TCP/IP-32) bloquent le démarrage de Windows
# quand QEMU tourne à pleine vitesse (essai du 30 octobre 2026) ; au rythme de
# l'usage (scripts/pc486, PC486_ICOUNT=7) et sous 86Box, ils démarrent.
NIC_ICOUNT = '7'


def verify_boot(stage, disk, timeout, nic=False, phase='verification'):
    with machine(stage, disk, phase, voice=True, nic=nic, icount=NIC_ICOUNT if nic else None) as m:
        end = time.monotonic() + timeout
        boot_menu(m, end, '2')
        while time.monotonic() < end:
            m.screen()
            if m.voice.exists() and b'paulard pr' in m.voice.read_bytes():
                break
            time.sleep(1)
        else:
            raise InstallError('Windows/Épaulard ne démarre pas ; journal COM1 et capture conservés')
        # Installation neuve : aucune application utilisateur ni document à fermer.
        time.sleep(5)
        m.key('alt-f4')
        time.sleep(2)
        m.key('ret')
        if not wait_prompt(m, end):
            raise InstallError('retour au DOS non confirmé après fermeture de Windows')


def verify_epaudos(stage, disk, timeout, word_dos=False, cdrom=False):
    with machine(stage, disk, 'epaudos', voice=True) as m:
        end = time.monotonic() + timeout
        boot_menu(m, end, '1')
        if not wait_prompt(m, end):
            raise InstallError('invite DOS absente après le chargement d’Épaudos')
        # Lecteur de CD-ROM IDE vide de QEMU (maître secondaire) : MSCDEX lui donne E:.
        if cdrom and 'Lecteur E: = gestionnaire PC486CD' not in m.text():
            raise InstallError('MSCDEX n’a pas installé le lecteur E: ; écran conservé')
        while time.monotonic() < end:
            # « Épaudos prêt » en CP850 ; le É (0x90) n'est pas comparé.
            if m.voice.exists() and b'paudos pr' in m.voice.read_bytes():
                break
            time.sleep(1)
        else:
            raise InstallError('Épaudos n’a rien annoncé sur COM1 ; journal conservé')
        if word_dos:
            # Word pour DOS en mode Texte, lu par Épaudos, puis quitté sans document.
            mark = m.voice.stat().st_size
            type_text(m.q, 'word\n', fr=True, delay=.1)
            while time.monotonic() < end:
                if 'appuyez sur ALT pour choisir les commandes' in m.text():
                    break
                time.sleep(1)
            else:
                raise InstallError('Word pour DOS ne démarre pas en mode Texte ; écran conservé')
            time.sleep(3)
            if b'Fichier' not in m.voice.read_bytes()[mark:]:
                raise InstallError('Épaudos ne lit pas l’écran de Word pour DOS ; journal conservé')
            m.key('alt-f4')
            if not wait_prompt(m, end):
                raise InstallError('retour au DOS non confirmé après Word pour DOS')
        # SMARTDRV ne retarde aucune écriture sur C: : arrêt sûr à l'invite DOS.


def user_text(value):
    if len(value) > 30 or any(c in value for c in '\r\n";{}|') or any(ord(c) < 32 for c in value):
        raise argparse.ArgumentTypeError('30 caractères au plus, sans guillemet, point-virgule, accolade, '
                                         'barre verticale ni retour à la ligne')
    try:
        value.encode('cp1252')
    except UnicodeEncodeError:
        raise argparse.ArgumentTypeError('nom compatible avec Windows français (CP1252) attendu') from None
    return value


def parser():
    p = argparse.ArgumentParser(prog='scripts/installer', description=__doc__, add_help=False)
    p.add_argument('-h', '--help', action='help', help='afficher cette aide et quitter')
    p.add_argument('--dos', type=Path, required=True, help='dossier ou ZIP local : DISK1 à DISK3, IMG/IMA')
    p.add_argument('--windows', type=Path, required=True, help='dossier ou ZIP local : DISK1 à DISK9, IMG/IMA')
    p.add_argument('--output', type=Path, help='nouvelle image brute 500 Mo (refus si elle existe)')
    p.add_argument('--name', type=user_text, default='Utilisateur', help='nom Windows (30 caractères)')
    p.add_argument('--company', type=user_text, default='', help='organisation, facultative')
    p.add_argument('--suppl', type=Path, help='disque supplémentaire de MS-DOS 6.22 : image (SUPPL.IMG), ou '
                   'dossier ou ZIP local qui la contient (ZIP de DOS 6.22)')
    p.add_argument('--word', type=Path, help='dossier ou ZIP local : Word 6.0c français, Disk01 à Disk11')
    p.add_argument('--excel', type=Path, help='dossier ou ZIP local : Excel 5.0c français (11 disquettes '
                   '1,2 Mo) ou Excel 5.0 français (10 disquettes 1,44 Mo)')
    p.add_argument('--access', type=Path, help='dossier ou ZIP local : Access 2.0 français, 10 disquettes')
    p.add_argument('--access-an2000', type=Path, help='dossier ou ZIP du correctif ACC2DATE '
                   '(MSABC200.DLL, MSAJU200.DLL), appliqué après --access')
    p.add_argument('--amipro', type=Path, help='dossier ou ZIP local : Ami Pro 3.01 français, 8 disquettes '
                   '(installateur Lotus, réponses jouées par INSTAUTO)')
    p.add_argument('--netscape', type=Path, help='ZIP local de Netscape Navigator 3.01 français 16 bits '
                   '(dossier N16FR31) : réglé sur le proxy de « pc486 start --reseau »')
    p.add_argument('--communicator', type=Path, help='ZIP local de Netscape Communicator 4.08 '
                   'français 16 bits (netscc16fr408.zip) : proxy et courrier de « --reseau » '
                   'par défaut ; profil créé par son assistant au premier lancement')
    p.add_argument('--eudora', type=Path, help='ZIP local d’Eudora Light 3.06 (eul306.zip, anglais) : '
                   'courrier local de « --reseau » (POP3 et SMTP 10.0.2.2)')
    p.add_argument('--word-dos', type=Path, help='dossier ou ZIP local : Word 6.0 pour DOS français, '
                   'disquettes SETUP_60, PROG_60, UTIL_60, FONT_60, PRINT_60, PRINT2_60')
    p.add_argument('--cdrom', type=Path, help='UDVD2.SYS ou udvd2.zip (paquet FreeDOS) : lecteur de CD-ROM '
                   'E: par MSCDEX, utilisé avec « pc486 start --cdrom »')
    p.add_argument('--reseau', type=Path, help='tcp32b.zip (TCP/IP-32 3.11b) : carte NE2000 et TCP/IP de WfW, '
                   'utilisés avec « pc486 start --reseau » ; sans carte, réseau retiré au démarrage')
    p.add_argument('--sans-braille', dest='braille', action='store_false',
                   help='ne pas configurer la plage Baum (par défaut : COM3, base 03E8, IRQ9)')
    p.add_argument('--braille', dest='braille', action='store_true', help=argparse.SUPPRESS)
    p.add_argument('--binaries', type=Path, help='EPAULARD.EXE, EPAUHOOK.DLL, EPAUDOS.COM (et INSTAUTO.EXE pour '
                   '--word, --excel, --access, --amipro, --netscape, --communicator, --eudora, '
                   'RESEAU.EXE pour '
                   '--reseau) déjà compilés ; '
                   'sinon compilation')
    p.add_argument('--check', action='store_true', help='vérifier les supports et prérequis, sans créer de disque')
    p.add_argument('--icount', type=icount_option, default=ICOUNT,
                   help="bridage du processeur QEMU : off (pleine vitesse) ou N, 2^N ns par instruction "
                        f"(défaut {ICOUNT})")
    p.add_argument('--timeout', type=int, default=1200, help='délai maximal par étape VM, en secondes (défaut 1200)')
    return p


def prerequisites(args):
    commands = ['mdir', 'mtype', 'mcopy', 'mmd', 'mdel', 'qemu-system-i386']
    missing = [c for c in commands if not shutil.which(c)]
    if missing:
        raise InstallError('outils manquants : ' + ', '.join(missing))
    if args.timeout < 30:
        raise InstallError('--timeout doit être au moins égal à 30 secondes')
    if not args.name.strip():
        raise InstallError('nom Windows vide')
    if args.access_an2000 and not args.access:
        raise InstallError('--access-an2000 demande --access')
    if args.binaries:
        acme = (args.word or args.excel or args.access or args.amipro or args.netscape or
                args.communicator or args.eudora)
        for name in ('EPAULARD.EXE', 'EPAUHOOK.DLL', 'EPAUDOS.COM') + (('INSTAUTO.EXE',) if acme else ()) \
                + (('RESEAU.EXE',) if args.reseau else ()):
            if not (args.binaries / name).is_file():
                raise InstallError(f'binaire absent : {args.binaries / name}')
    elif not Path(os.environ.get('WATCOM', ROOT / 'tools/watcom')).exists():
        raise InstallError('Open Watcom absent : construire tools/watcom ou fournir --binaries')


def publish(disk, output):
    """Lien atomique sans remplacement : même une création concurrente est protégée."""
    os.link(disk, output)
    disk.unlink()


def main(argv=None):
    global ICOUNT
    args = parser().parse_args(argv)
    ICOUNT = args.icount
    stage = None
    try:
        prerequisites(args)
        if not args.check and args.output is None:
            raise InstallError('--output est obligatoire pour installer')
        # Ne pas résoudre le dernier composant : un lien, même cassé, est une sortie existante.
        output = Path(os.path.abspath(args.output)) if args.output else None
        if output and output.suffix.lower() != '.img':
            raise InstallError('la sortie doit se terminer par .img (image brute, pas QCOW2)')
        if output and os.path.lexists(output):
            raise InstallError(f'sortie déjà existante, aucune écriture : {output}')
        parent = output.parent if output else ROOT / 'work'
        parent.mkdir(parents=True, exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix='.pc486-install-', dir=parent))
        steps = ['Vérification des supports locaux', 'Installation de MS-DOS 6.22']
        if args.suppl:
            steps.append('Outils du disque supplémentaire de MS-DOS 6.22')
        steps.append('Installation de Windows pour Workgroups 3.11')
        if args.word:
            steps.append('Installation de Word 6.0c (options par défaut)')
        if args.excel:
            steps.append('Installation d’Excel (options par défaut)')
        if args.access:
            steps.append('Installation d’Access 2.0 (options par défaut)')
        if args.amipro:
            steps.append('Installation d’Ami Pro 3.01 (installation complète)')
        if args.netscape:
            steps.append('Installation de Netscape Navigator 3.01 (proxy pc486)')
        if args.communicator:
            steps.append('Installation de Netscape Communicator 4.08 (proxy pc486)')
        if args.eudora:
            steps.append('Installation d’Eudora Light 3.06 (courrier local)')
        if args.word_dos:
            steps.append('Installation de Word 6.0 pour DOS (mode Texte)')
        if args.cdrom:
            steps.append('Lecteur de CD-ROM (UDVD2, MSCDEX)')
        if args.reseau:
            steps.append('Réseau de WfW : NE2000 et TCP/IP-32')
        steps += ['Épaulard, Épaudos, son et démarrage DOS',
                  'Démarrage Windows/Épaulard et retour au DOS']
        if args.reseau:
            steps.append('Démarrage Windows/Épaulard avec la carte réseau')
        steps.append('Démarrage DOS avec Épaudos')
        numbered = iter(enumerate(steps, 1))

        def step():
            n, title = next(numbered)
            print(f'{n}/{len(steps)} — {title}', flush=True)

        step()
        dos = collect_media(args.dos, 3, stage / 'dos')
        windows = collect_media(args.windows, 9, stage / 'windows')
        validate_media(dos, windows)
        manifest = {'profile': 'msdos622-fr+wfw311-fr', 'media': {
            group: [dict(name=p.name, sha256=digest(p)) for p in paths]
            for group, paths in (('dos', dos), ('windows', windows))}}
        word = None
        if args.word:
            word = collect_media(args.word, 11, stage / 'word')
            validate_word(word)
            manifest['media']['word'] = [dict(name=p.name, sha256=digest(p)) for p in word]
        excel = access = amipro = netscape = communicator = eudora = (None, None)
        y2k = None
        if args.excel:
            excel = collect_acme(args.excel, stage / 'excel', ('excel', 'excel50'))
            manifest['media'][excel[0].key] = [dict(name=p.name, sha256=digest(p)) for p in excel[1]]
        if args.access:
            access = collect_acme(args.access, stage / 'access', ('access',))
            manifest['media']['access'] = [dict(name=p.name, sha256=digest(p)) for p in access[1]]
        if args.amipro:
            amipro = collect_acme(args.amipro, stage / 'amipro', ('amipro',))
            manifest['media']['amipro'] = [dict(name=p.name, sha256=digest(p)) for p in amipro[1]]
        if args.netscape:
            netscape = collect_acme(args.netscape, stage / 'netscape', ('netscape',))
            manifest['media']['netscape'] = [dict(name=args.netscape.name, sha256=digest(args.netscape))]
        if args.communicator:
            communicator = collect_acme(args.communicator, stage / 'communicator', ('communicator',))
            manifest['media']['communicator'] = [dict(name=args.communicator.name,
                                                       sha256=digest(args.communicator))]
        if args.eudora:
            eudora = collect_acme(args.eudora, stage / 'eudora', ('eudora',))
            manifest['media']['eudora'] = [dict(name=args.eudora.name, sha256=digest(args.eudora))]
        if args.access_an2000:
            y2k = collect_access_y2k(args.access_an2000, stage / 'access-an2000')
            manifest['media']['access-an2000'] = [dict(name=p.name, sha256=digest(p)) for p in y2k]
        word_dos = None
        if args.word_dos:
            word_dos = collect_word_dos(args.word_dos, stage / 'word-dos')
            validate_word_dos(word_dos)
            manifest['media']['word-dos'] = [dict(name=volume_label(p), sha256=digest(p)) for p in word_dos]
        cd_driver = tcp = None
        try:
            if args.cdrom:
                cd_driver = options_vm.udvd2(args.cdrom)
                manifest['media']['cdrom'] = [dict(name=args.cdrom.name, sha256=digest(args.cdrom))]
            if args.reseau:
                tcp = options_vm.tcpip32(args.reseau)
                manifest['media']['reseau'] = [dict(name=args.reseau.name, sha256=digest(args.reseau))]
        except ValueError as e:
            raise InstallError(str(e)) from None
        suppl = None
        if args.suppl:
            suppl = collect_suppl(args.suppl, stage / 'suppl.img')
            manifest['media']['suppl'] = [dict(name=suppl.name, sha256=digest(suppl))]
        (stage / 'supports.json').write_text(json.dumps(manifest, indent=2) + '\n')
        if args.check:
            print('Supports et prérequis valides. Aucun disque créé.', flush=True)
            shutil.rmtree(stage)
            return 0
        binaries = args.binaries.resolve() if args.binaries else ROOT / 'epaulard/build'
        epaudos = binaries / 'EPAUDOS.COM' if args.binaries else ROOT / 'epaudos/build/EPAUDOS.COM'
        reseau_exe = binaries / 'RESEAU.EXE' if args.binaries else ROOT / 'dos/build/RESEAU.EXE'
        if not args.binaries:
            run(ROOT / 'epaulard/build.sh')
            run(ROOT / 'epaudos/build.sh')
            if args.reseau:
                run(ROOT / 'dos/build.sh')
        disk = stage / 'installation.img'
        with disk.open('xb') as f:
            f.truncate(500 * 1024 * 1024)
        step()
        install_dos(stage, disk, dos, args.timeout)
        image = disk_partition(disk)
        if suppl:
            step()
            install_suppl(stage, disk, image, suppl, args.timeout)
        step()
        stage_windows(stage, image, windows, args.name, args.company)
        install_windows(stage, disk, args.timeout)
        if word:
            step()
            stage_word(stage, image, word, args.name, args.company, binaries)
            install_word(stage, disk, image, args.timeout, args.name, args.company)
        for product, media in (excel, access, amipro, netscape, communicator, eudora):
            if product is None:
                continue
            step()
            install_office(stage, disk, image, product, media, args.timeout, args.name, args.company,
                           binaries)
            if product.key == 'access' and y2k:
                install_access_y2k(image, y2k)
        if word_dos:
            step()
            stage_word_dos(stage, image, word_dos)
            install_word_dos(stage, disk, image, args.timeout, args.name)
        if cd_driver:
            step()
            install_cdrom(stage, image, cd_driver)
        if tcp:
            step()
            install_reseau(stage, disk, image, tcp, reseau_exe, args.timeout)
        step()
        configure(stage, image, binaries, args.braille, epaudos, bool(word_dos), bool(cd_driver), bool(tcp))
        step()
        verify_boot(stage, disk, args.timeout)
        if tcp:
            check_reseau(image, False)
            step()
            verify_boot(stage, disk, args.timeout, nic=True, phase='verification-reseau')
            check_reseau(image, True)
        verified = ['dos-setup', 'windows-setup', 'epaulard-com1', 'exit-to-dos']
        if tcp:
            verified.append('reseau-avec-et-sans-carte')
        if suppl:
            verified.insert(1, 'suppl-tools')
        if word:
            verified.insert(-2, 'word-setup')
        for product, _ in (excel, access, amipro):
            if product:
                verified.insert(-2, f'{product.key}-setup')
        step()
        verify_epaudos(stage, disk, args.timeout, bool(word_dos), bool(cd_driver))
        verified.append('epaudos-com1')
        if cd_driver:
            verified.append('cdrom-mscdex')
        if word_dos:
            verified.insert(-3, 'word-dos-setup')
            verified.append('word-dos-epaudos')
        manifest['epaulard'] = {p.name: digest(p) for p in (binaries / 'EPAULARD.EXE', binaries / 'EPAUHOOK.DLL',
                                                            epaudos)}
        manifest['verified'] = verified
        (stage / 'supports.json').write_text(json.dumps(manifest, indent=2) + '\n')
        publish(disk, output)
        print(f'Installation terminée : {output}\nJournaux et empreintes : {stage}', flush=True)
        return 0
    except (InstallError, OSError, subprocess.SubprocessError, zipfile.BadZipFile,
            RuntimeError, ConnectionError, KeyboardInterrupt, UnicodeDecodeError) as exc:
        reason = 'annulation demandée' if isinstance(exc, KeyboardInterrupt) else str(exc)
        print(f'Installation interrompue : {reason}', file=sys.stderr)
        if stage:
            print(f'Diagnostics et disque partiel conservés : {stage}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
