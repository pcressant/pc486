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


def collect_media(source, count, target):
    """Importe uniquement DISK1..N ; les noms internes d'un ZIP ne sont jamais extraits."""
    source = source.resolve()
    found = {}
    target.mkdir()

    def put(number, data):
        if number in found:
            raise InstallError(f'support ambigu : plusieurs disquettes {number} dans {source}')
        if len(data) != FLOPPY_SIZE:
            raise InstallError(f'disquette {number} : image brute 1,44 Mo attendue')
        p = target / f'disk{number}.img'
        p.write_bytes(data)
        found[number] = p

    if source.is_dir():
        for path in sorted(source.rglob('*')):
            n = media_number(path.name)
            if path.is_file() and n is not None and 1 <= n <= count:
                if path.stat().st_size != FLOPPY_SIZE:
                    raise InstallError(f'image 1,44 Mo attendue : {path}')
                put(n, path.read_bytes())
    elif source.is_file() and zipfile.is_zipfile(source):
        with zipfile.ZipFile(source) as z:
            for entry in z.infolist():
                n = media_number(entry.filename)
                if not entry.is_dir() and n is not None and 1 <= n <= count:
                    if entry.file_size != FLOPPY_SIZE:
                        raise InstallError(f'image 1,44 Mo attendue dans {source} : {entry.filename}')
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


def validate_word(media):
    stf = run('mtype', '-i', media[0], '::/INSTALL.STF')
    if b'WINWORD.EXE' not in stf or b'6.0.0.3015(1036)' not in stf:
        raise InstallError('supports de Word 6.0c français attendus (INSTALL.STF)')
    run('mtype', '-i', media[0], '::/INSTALL.EXE')


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


def collect_word_dos(source, target):
    """Disquettes de Word 6.0 pour DOS, reconnues par leur nom de volume.
    Dossier ou ZIP local ; seules les images de disquette sont lues."""
    source = source.resolve()
    target.mkdir()
    if source.is_dir():
        candidates = [p.read_bytes for p in sorted(source.rglob('*'))
                      if p.is_file() and p.suffix.lower() in ('.ima', '.img')]
        return keep_word_dos(source, target, candidates)
    if source.is_file() and zipfile.is_zipfile(source):
        # Toutes les lectures ont lieu dans keep_word_dos, archive encore ouverte.
        with zipfile.ZipFile(source) as z:
            candidates = [lambda e=e: z.read(e) for e in z.infolist()
                          if not e.is_dir() and Path(e.filename).suffix.lower() in ('.ima', '.img')]
            return keep_word_dos(source, target, candidates)
    raise InstallError(f'dossier ou archive ZIP locale introuvable : {source}')


def keep_word_dos(source, target, candidates):
    """Copie de travail des disquettes reconnues, dans l'ordre de WORD_DOS_VOLUMES."""
    found = {}
    for i, read in enumerate(candidates):
        data = read()
        if len(data) not in WORD_DOS_FLOPPY:
            continue
        p = target / f'image{i}.img'
        p.write_bytes(data)
        label = volume_label(p)
        if label not in WORD_DOS_VOLUMES:
            p.unlink()
            continue
        if label in found:
            raise InstallError(f'Word pour DOS : plusieurs disquettes {label} dans {source}')
        found[label] = p
    missing = [v for v in WORD_DOS_VOLUMES if v not in found]
    if missing:
        raise InstallError(f'Word pour DOS : disquettes manquantes dans {source} : {", ".join(missing)}')
    return [found[v] for v in WORD_DOS_VOLUMES]


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


def final_boot_files(word_dos=False):
    """CONFIG.SYS et AUTOEXEC.BAT de la VM de référence (modèles de dos/) :
    1 = DOS avec Épaudos (défaut après 10 s), 2 = Windows avec Épaulard.
    Avec Word pour DOS, C:\\WORD s'ajoute au PATH, seul changement demandé par INIT."""
    config, autoexec = ((ROOT / 'dos' / name).read_bytes().decode('cp850').replace('\r\n', '\n')
                        for name in ('CONFIG.SYS', 'AUTOEXEC.BAT'))
    path = 'PATH C:\\WINDOWS;C:\\DOS\n'
    if 'C:\\EPAUDOS.COM\n' not in autoexec or 'MENUDEFAULT=DOS,10' not in config or path not in autoexec:
        raise InstallError('modèles dos/CONFIG.SYS ou dos/AUTOEXEC.BAT inattendus')
    if word_dos:
        autoexec = autoexec.replace(path, path[:-1] + ';C:\\WORD\n')
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
    def __init__(self, stage, disk, phase, floppy=None, voice=False, boot=None):
        self.stage, self.phase = stage, phase
        self.runtime = tempfile.TemporaryDirectory(prefix='pc486-install-')
        self.socket = Path(self.runtime.name) / 'qmp.sock'
        self.voice = stage / 'verification-com1.log'
        self.log = (stage / f'{phase}-qemu.log').open('wb')
        filearg = str(disk).replace(',', ',,')
        cmd = ['qemu-system-i386', '-machine', 'pc,accel=tcg,hpet=off', '-rtc', 'base=localtime', '-cpu', '486', '-m', '16',
               *([] if ICOUNT == 'off' else ['-icount', f'shift={ICOUNT},sleep=on,align=on']),
               '-drive', f'file={filearg},format=raw,if=ide,index=0',
               '-boot', 'order=' + (boot or ('a' if floppy else 'c')), '-vga', 'cirrus',
               '-display', 'none', '-nic', 'none', '-audiodev', 'none,id=snd0',
               '-device', 'sb16,audiodev=snd0,iobase=0x220,irq=5,dma=1,dma16=5',
               '-device', 'adlib,audiodev=snd0',
               # COM3 de la plage Baum, comme scripts/pc486, sans plage branchée.
               '-chardev', 'null,id=com3', '-device', 'isa-serial,chardev=com3,index=2,iobase=0x3e8,irq=9',
               '-serial', f'file:{self.voice}' if voice else 'null',
               '-qmp', f'unix:{self.socket},server=on,wait=off']
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


def stage_word(stage, image, media, name, company, binaries):
    """Supports dans C:\\INSTALL\\DISKn, comme l'installation manuelle de référence."""
    run('mmd', '-i', image, '::/INSTALL')
    for i, floppy in enumerate(media, 1):
        dest = stage / f'word-disk{i}'
        dest.mkdir()
        run('mcopy', '-i', floppy, '::/*', dest)
        run('mmd', '-i', image, f'::/INSTALL/DISK{i}')
        run('mcopy', '-o', '-i', image, *sorted(dest.iterdir()), f'::/INSTALL/DISK{i}/')
    run('mcopy', '-o', '-i', image, binaries / 'INSTAUTO.EXE', '::/INSTAUTO.EXE')
    if not mexists(image, 'TEMP/'):
        run('mmd', '-i', image, '::/TEMP')
    ini = load_ini_editor()
    win = stage / 'WIN.INI'
    text = run('mtype', '-i', image, '::/WINDOWS/WIN.INI').decode('cp1252')
    # Mode sans dialogue d'ACME : installation par défaut, nom et société fournis.
    command = f'C:\\INSTALL\\DISK1\\INSTALL.EXE /Q1 /N "{name}"' + (f' /O "{company}"' if company else '')
    text = ini.setting(text, 'windows', 'load', r'C:\INSTAUTO.EXE')
    text = ini.setting(text, 'pc486', 'commande', command)
    text = ini.setting(text, 'pc486', 'etat', '')
    win.write_bytes(text.encode('cp1252'))
    run('mcopy', '-o', '-i', image, win, '::/WINDOWS/WIN.INI')
    write_dos_file(stage, image, 'CONFIG.SYS', SETUP_CONFIG)
    write_dos_file(stage, image, 'AUTOEXEC.BAT',
                   '@ECHO OFF\nPATH C:\\WINDOWS;C:\\DOS\nSET TEMP=C:\\TEMP\n' + SETUP_CODEPAGE +
                   f'C:\nCD \\\nWIN\nECHO {WORD_MARKER}\n')


def install_word(stage, disk, image, timeout, name, company):
    with machine(stage, disk, 'word') as m:
        start = time.monotonic()
        end, report = start + timeout, start + 60
        while time.monotonic() < end:
            if time.monotonic() >= report:
                print(f'  Word : installation en cours ({int(time.monotonic() - start)} s)', flush=True)
                report += 60
            if WORD_MARKER in m.text():
                break
            time.sleep(2)
        else:
            raise InstallError('délai dépassé pendant l’installation de Word ; capture conservée')
    journal = subprocess.run(['mtype', '-i', image, '::/INSTAUTO.LOG'], capture_output=True).stdout
    (stage / 'word-instauto.log').write_bytes(journal)
    text = run('mtype', '-i', image, '::/WINDOWS/WIN.INI').decode('cp1252')
    if 'etat=fini' not in text.replace(' ', '').lower():
        raise InstallError('INSTAUTO n’a pas terminé ; voir word-instauto.log')
    if not run('mtype', '-i', image, '::/WINWORD/WINWORD.EXE') or 'Word.Document.6' not in text:
        raise InstallError('Word 6.0c non installé ; voir word-instauto.log et word-screen.png')
    # Nom proposé par Word (Outils, Options), écrit seulement en mode interactif.
    ini = load_ini_editor()
    text = ini.setting(ini.setting(remove_section(text, 'pc486'), 'MS User Info', 'DefName', name),
                       'MS User Info', 'DefCompany', company)
    # INSTAUTO.EXE est supprimé : le retirer de load= sans toucher aux autres programmes.
    text = epaulard_ini().edit_load(text, remove=(r'C:\INSTAUTO.EXE',))
    win = stage / 'WIN.INI'
    win.write_bytes(text.encode('cp1252'))
    run('mcopy', '-o', '-i', image, win, '::/WINDOWS/WIN.INI')
    run('mdel', '-i', image, '::/INSTAUTO.EXE', '::/INSTAUTO.LOG')


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


def configure(stage, image, binaries, braille, epaudos, word_dos=False):
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
    config, autoexec = final_boot_files(word_dos)
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


def verify_boot(stage, disk, timeout):
    with machine(stage, disk, 'verification', voice=True) as m:
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


def verify_epaudos(stage, disk, timeout, word_dos=False):
    with machine(stage, disk, 'epaudos', voice=True) as m:
        end = time.monotonic() + timeout
        boot_menu(m, end, '1')
        if not wait_prompt(m, end):
            raise InstallError('invite DOS absente après le chargement d’Épaudos')
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
    if len(value) > 30 or any(c in value for c in '\r\n";') or any(ord(c) < 32 for c in value):
        raise argparse.ArgumentTypeError('30 caractères au plus, sans guillemet, point-virgule ni retour à la ligne')
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
    p.add_argument('--suppl', type=Path, help='image du disque supplémentaire de MS-DOS 6.22 (SUPPL.IMG)')
    p.add_argument('--word', type=Path, help='dossier ou ZIP local : Word 6.0c français, Disk01 à Disk11')
    p.add_argument('--word-dos', type=Path, help='dossier ou ZIP local : Word 6.0 pour DOS français, '
                   'disquettes SETUP_60, PROG_60, UTIL_60, FONT_60, PRINT_60, PRINT2_60')
    p.add_argument('--sans-braille', dest='braille', action='store_false',
                   help='ne pas configurer la plage Baum (par défaut : COM3, base 03E8, IRQ9)')
    p.add_argument('--braille', dest='braille', action='store_true', help=argparse.SUPPRESS)
    p.add_argument('--binaries', type=Path, help='EPAULARD.EXE, EPAUHOOK.DLL, EPAUDOS.COM (et INSTAUTO.EXE pour --word) déjà compilés ; sinon compilation')
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
    if args.binaries:
        for name in ('EPAULARD.EXE', 'EPAUHOOK.DLL', 'EPAUDOS.COM') + (('INSTAUTO.EXE',) if args.word else ()):
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
        if args.word_dos:
            steps.append('Installation de Word 6.0 pour DOS (mode Texte)')
        steps += ['Épaulard, Épaudos, son et démarrage DOS',
                  'Démarrage Windows/Épaulard et retour au DOS',
                  'Démarrage DOS avec Épaudos']
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
        word_dos = None
        if args.word_dos:
            word_dos = collect_word_dos(args.word_dos, stage / 'word-dos')
            validate_word_dos(word_dos)
            manifest['media']['word-dos'] = [dict(name=volume_label(p), sha256=digest(p)) for p in word_dos]
        suppl = None
        if args.suppl:
            suppl = stage / 'suppl.img'
            shutil.copyfile(args.suppl, suppl)
            validate_suppl(suppl)
            manifest['media']['suppl'] = [dict(name=suppl.name, sha256=digest(suppl))]
        (stage / 'supports.json').write_text(json.dumps(manifest, indent=2) + '\n')
        if args.check:
            print('Supports et prérequis valides. Aucun disque créé.', flush=True)
            shutil.rmtree(stage)
            return 0
        binaries = args.binaries.resolve() if args.binaries else ROOT / 'epaulard/build'
        epaudos = binaries / 'EPAUDOS.COM' if args.binaries else ROOT / 'epaudos/build/EPAUDOS.COM'
        if not args.binaries:
            run(ROOT / 'epaulard/build.sh')
            run(ROOT / 'epaudos/build.sh')
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
        if word_dos:
            step()
            stage_word_dos(stage, image, word_dos)
            install_word_dos(stage, disk, image, args.timeout, args.name)
        step()
        configure(stage, image, binaries, args.braille, epaudos, bool(word_dos))
        step()
        verify_boot(stage, disk, args.timeout)
        verified = ['dos-setup', 'windows-setup', 'epaulard-com1', 'exit-to-dos']
        if suppl:
            verified.insert(1, 'suppl-tools')
        if word:
            verified.insert(-2, 'word-setup')
        step()
        verify_epaudos(stage, disk, args.timeout, bool(word_dos))
        verified.append('epaudos-com1')
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
