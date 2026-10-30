# SPDX-License-Identifier: GPL-3.0-or-later
"""Options de l'invité, jamais actives par défaut : lecteur de CD-ROM et réseau.

CD-ROM : UDVD2.SYS (pilote ATAPI libre de FreeDOS, Jack Ellis) et MSCDEX.EXE de
MS-DOS 6.22, lettre E:. Sans lecteur, UDVD2 ne se charge pas et MSCDEX est sauté.

Réseau : WfW 3.11 avec carte NE2000 compatible (0x300, IRQ10) et TCP/IP-32 3.11b,
adresse fixe 10.0.2.15, passerelle 10.0.2.2, exactement comme l'installation
réseau faite à la main dans Windows (SYSTEM.INI et PROTOCOL.INI relevés le
30 octobre 2026). Sans carte, WfW bloquerait sur « adaptateur MS$NE2CLONE » :
RESEAU.EXE, lancé avant WIN, cherche la carte et bascule les seules lignes qui
chargent le réseau d'après la section [pc486.reseau] de SYSTEM.INI.

Les fichiers réseau de WfW sont compressés (KWAJ) dans C:\\WININST : ils sont
décompressés dans l'invité par EXPAND.EXE, comme les pilotes son.
"""
from pathlib import Path
import re
import zipfile

CD_DEVICE = 'PC486CD'
CD_CONFIG = f'DEVICEHIGH=C:\\DOS\\UDVD2.SYS /D:{CD_DEVICE}\n'
CD_AUTOEXEC = f'IF EXIST {CD_DEVICE} LH C:\\DOS\\MSCDEX.EXE /D:{CD_DEVICE} /L:E\n'
RESEAU_AUTOEXEC = 'C:\\WINDOWS\\RESEAU.EXE AUTO\n'

# Fichiers ajoutés par l'installation réseau de WfW (relevé du 30 octobre 2026),
# depuis C:\WININST (nom terminé par « _ ») : (dossier, nom).
WFW_FILES = tuple(('WINDOWS', n) for n in (
    'LOGONOFF.EXE', 'NDDENB.DLL', 'NDISHLP.SYS', 'NE2000.DOS', 'NETDDE.EXE', 'NET.EXE',
    'NETH.MSG', 'NET.MSG', 'NETWATCH.EXE', 'NETWATCH.HLP', 'PROTMAN.DOS', 'PROTMAN.EXE',
    'WFWNET.HLP', 'WINCHAT.EXE', 'WINCHAT.HLP', 'WINMETER.EXE', 'WINPOPUP.EXE',
    'WINPOPUP.HLP')) + tuple(('WINDOWS/SYSTEM', n) for n in (
    'LMSCRIPT.EXE', 'LMSCRIPT.PIF', 'NDIS2SUP.386', 'NDIS.386', 'NE2000.386', 'NETAPI.DLL',
    'PMSPL.DLL', 'VNETSUP.386', 'VREDIR.386', 'VSERVER.386', 'WFWAUDIT.DLL', 'WFWNETCP.CPX',
    'WFWNET.DRV'))

# TCP/IP-32 3.11b, non compressé : placement de son OEMSETUP.INF (copié en OEM0.INF).
TCP_FILES = tuple(('WINDOWS', n) for n in (
    'TCP32UI.DLL', 'ARP.EXE', 'PING.EXE', 'ROUTE.EXE', 'TRACERT.EXE', 'HOSTS.SAM',
    'LMHOSTS.SAM', 'SERVICES', 'PROTOCOL', 'NETWORKS', 'IPCONFIG.EXE', 'FTP.EXE',
    'TELNET.EXE', 'NETSTAT.EXE', 'NBTSTAT.EXE', 'README.TXT', 'MTCPIP32.HLP', 'WINSOCK.DLL',
    'TELNET.HLP', 'MSTCP32.DEF')) + tuple(('WINDOWS/SYSTEM', n) for n in (
    'WSASRV.EXE', 'VIP.386', 'VDHCP.386', 'VTDI.386', 'VTCP.386', 'VNBT.386', 'WSOCK.386',
    'WSTCP.386'))

PROTOCOL_INI = '''[network.setup]
version=0x3110
netcard=ms$ne2clone,1,MS$NE2CLONE,3
transport=ms$ndishlp,MS$NDISHLP
transport=tcpip-32r,MSTCP32
lana0=ms$ne2clone,1,tcpip-32r
lana1=ms$ne2clone,1,ms$ndishlp


[protman]
DriverName=PROTMAN$
PRIORITY=MS$NDISHLP

[MS$NE2CLONE]
DriverName=MS2000$
INTERRUPT=10
IOBASE=0x300

[NE2000]
Adapters=MS$NE2CLONE

[MS$NDISHLP]
DriverName=ndishlp$
BINDINGS=MS$NE2CLONE

[MSTCP32]
BINDINGS=MS$NE2CLONE
LANABASE=0
'''

# Lignes qui chargent le réseau : basculées par RESEAU.EXE (avec carte | sans carte).
# « R\x82seau » : Setup écrit cette description en page de code 850.
NET_SWITCHED = (
    ('boot', 'network.drv', 'wfwnet.drv'),
    ('boot.description', 'network.drv', 'R\x82seau Microsoft Windows (version 3.11)'),
    ('386Enh', 'network', '*vnetbios,*vwc,vnetsup.386,vredir.386,vserver.386'),
    ('386Enh', 'netheapsize', '20'),
    ('386Enh', 'netmisc', 'ndis.386,ndis2sup.386,wsock.386,wstcp.386'),
    ('386Enh', 'netcard', 'ne2000.386'),
    ('386Enh', 'transport', 'vip.386,vdhcp.386,vtdi.386,vtcp.386,vnbt.386'),
    ('386Enh', 'InDOSPolling', 'FALSE'),
    ('Network', 'winnet', 'wfwnet/00025100'),
)

# Réglages permanents, lus seulement par les composants réseau.
NET_FIXED = (
    ('Network', 'LogonDisconnected', 'yes'),
    ('Network', 'EnableSharing', 'no'),
    ('Network', 'UserName', 'PC486'),
    ('Network', 'Workgroup', 'WORKGROUP'),
    ('Network', 'ComputerName', 'PC486'),
    ('Network', 'Comment', 'pc486'),
    # Pas de boîte « Ouverture de session » ni de liste de mots de passe au démarrage.
    ('Network', 'AutoLogon', 'No'),
    ('Network', 'LMLogon', '0'),
    ('Network', 'PasswordCaching', 'no'),
    ('network drivers', 'netcard', 'ne2000.dos'),
    ('network drivers', 'transport', 'ndishlp.sys'),
    ('ms$ne2clone0', 'DefaultGateway', '10.0.2.2'),
    ('ms$ne2clone0', 'IPMask', '255.0.0.0'),
    ('ms$ne2clone0', 'IPAddress', '10.0.2.15'),
    ('ms$ne2clone0', 'Description', 'NE2000 Compatible'),
    ('ms$ne2clone0', 'Binding', 'ms$ne2clone'),
    ('MSTCP', 'EnableRouting', '0'),
    ('MSTCP', 'Interfaces', 'ms$ne2clone0'),
    ('MSTCP', 'deadgwdetect', '1'),
    ('MSTCP', 'pmtudiscovery', '1'),
    ('DNS', 'DomainName', ''),
    ('DNS', 'DNSDomains', ''),
    ('NBT', 'LANABASE', '0'),
    ('NBT', 'EnableProxy', '0'),
    ('NBT', 'EnableDNS', '0'),
)

SWITCH_SECTION = 'pc486.reseau'


def get(text, section, key):
    """Valeur de la première clé de la section, ou None."""
    inside = False
    for line in text.splitlines():
        s = line.strip()
        if s.startswith('['):
            inside = s.casefold() == f'[{section}]'.casefold()
        elif inside and '=' in s:
            k, _, v = s.partition('=')
            if k.strip().casefold() == key.casefold():
                return v.strip()
    return None


def remove_section(text, section):
    newline = '\r\n' if '\r\n' in text else '\n'
    out, skip = [], False
    for line in text.splitlines():
        if line.lstrip().startswith('['):
            skip = line.strip().casefold() == f'[{section}]'.casefold()
        if not skip:
            out.append(line)
    while out and not out[-1].strip():
        out.pop()
    return newline.join(out) + newline


def configure_system(text, setting):
    """SYSTEM.INI (texte latin-1, octets conservés) : réglages permanents et section
    de bascule. Les valeurs « sans carte » sont celles du fichier avant le réseau ;
    une installation déjà faite n'est pas relue comme référence."""
    old = {}
    previous = get_switch(text)
    for section, key, on in NET_SWITCHED:
        if (section, key.casefold()) in previous:
            old[section, key] = previous[section, key.casefold()][1]
        else:
            value = get(text, section, key)
            old[section, key] = '-' if value is None else value
    text = remove_section(text, SWITCH_SECTION)
    for section, key, value in NET_FIXED:
        text = setting(text, section, key, value)
    newline = '\r\n' if '\r\n' in text else '\n'
    lines = [f'[{SWITCH_SECTION}]']
    for section, key, on in NET_SWITCHED:
        off = old[section, key]
        if '|' in off or '|' in on:
            raise ValueError(f'valeur avec « | » dans [{section}] {key}')
        lines.append(f'{section}/{key}={on}|{off}')
    return text.rstrip('\r\n') + newline + newline + newline.join(lines) + newline


def get_switch(text):
    """Entrées de [pc486.reseau] : {(section, clé en minuscules): (avec, sans)}."""
    out, inside = {}, False
    for line in text.splitlines():
        s = line.strip()
        if s.startswith('['):
            inside = s.casefold() == f'[{SWITCH_SECTION}]'
        elif inside and '=' in s and '/' in s.partition('=')[0]:
            k, _, v = s.partition('=')
            section, _, key = k.partition('/')
            on, _, off = v.partition('|')
            out[section, key.casefold()] = (on, off)
    return out


def boot_files(config, autoexec, cdrom=False, reseau=False):
    """Lignes ajoutées aux fichiers de démarrage (texte, fins de ligne \\n)."""
    if cdrom and CD_CONFIG not in config:
        anchor = 'DEVICE=C:\\WINDOWS\\IFSHLP.SYS\n'
        if anchor not in config:
            raise ValueError('CONFIG.SYS sans IFSHLP.SYS : emplacement du pilote CD-ROM inconnu')
        config = config.replace(anchor, anchor + CD_CONFIG, 1)
    if cdrom and CD_AUTOEXEC not in autoexec:
        anchor = 'GOTO %CONFIG%\n'
        if anchor not in autoexec:
            raise ValueError('AUTOEXEC.BAT sans GOTO %CONFIG%')
        autoexec = autoexec.replace(anchor, CD_AUTOEXEC + anchor, 1)
    if reseau and RESEAU_AUTOEXEC not in autoexec:
        m = re.search(r'^:WIN\n(?:.*\n)*?(WIN\n)', autoexec, re.M)
        if not m:
            raise ValueError('AUTOEXEC.BAT sans branche :WIN terminée par WIN')
        autoexec = (autoexec[:m.start(1)] + 'REM Carte reseau absente : RESEAU retire le reseau de SYSTEM.INI\n'
                    + RESEAU_AUTOEXEC + autoexec[m.start(1):])
    return config, autoexec


def udvd2(source):
    """UDVD2.SYS : le fichier lui-même ou le paquet udvd2.zip de FreeDOS."""
    source = Path(source)
    if zipfile.is_zipfile(source):
        with zipfile.ZipFile(source) as z:
            names = [n for n in z.namelist() if n.upper().endswith('/UDVD2.SYS') or n.upper() == 'UDVD2.SYS']
            if not names:
                raise ValueError(f'UDVD2.SYS absent de {source}')
            data = z.read(names[0])
    else:
        data = source.read_bytes()
    if b'UDVD2' not in data[:4096] or len(data) > 65536:
        raise ValueError(f'{source} : UDVD2.SYS attendu')
    return data


def tcpip32(source):
    """Fichiers de TCP/IP-32 3.11b (tcp32b.zip ou dossier) : {nom: octets}."""
    source = Path(source)
    wanted = {n for _, n in TCP_FILES} | {'OEMSETUP.INF'}
    files = {}
    if zipfile.is_zipfile(source):
        with zipfile.ZipFile(source) as z:
            for n in z.namelist():
                if n.rsplit('/', 1)[-1].upper() in wanted:
                    files[n.rsplit('/', 1)[-1].upper()] = z.read(n)
    else:
        for p in source.iterdir():
            if p.name.upper() in wanted:
                files[p.name.upper()] = p.read_bytes()
    missing = sorted(wanted - set(files))
    if missing:
        raise ValueError(f'{source} : fichiers de TCP/IP-32 absents : {", ".join(missing)}')
    if b'tcpip-32r' not in files['OEMSETUP.INF']:
        raise ValueError(f'{source} : OEMSETUP.INF n’est pas celui de TCP/IP-32 3.11b')
    return files


def expand_batch(sources):
    """Commandes DOS de décompression des fichiers réseau de WfW."""
    lines = []
    for folder, name in WFW_FILES:
        packed = name[:-1] + '_'
        lines.append(f'C:\\WINDOWS\\EXPAND C:\\{sources}\\{packed} C:\\{folder.replace("/", chr(92))}\\{name}\n')
    return ''.join(lines)


def main(argv=None):
    """scripts/options-vm : options appliquées à la copie d'un disque arrêté."""
    import argparse
    import os
    import shutil
    import subprocess
    import sys
    import tempfile
    import installation as inst

    p = argparse.ArgumentParser(prog='scripts/options-vm', description=
                                'Ajoute le CD-ROM et/ou le réseau à une COPIE d’un disque installé, '
                                'vérifie le démarrage puis publie une nouvelle image (jamais de remplacement).')
    p.add_argument('--source', type=Path, required=True, help='disque C: arrêté (lu sous verrou, jamais modifié)')
    p.add_argument('--sortie', type=Path, required=True, help='nouvelle image (refus si elle existe)')
    p.add_argument('--cdrom', type=Path, help='UDVD2.SYS ou udvd2.zip de FreeDOS')
    p.add_argument('--reseau', type=Path, help='tcp32b.zip (TCP/IP-32 3.11b)')
    p.add_argument('--timeout', type=int, default=600)
    args = p.parse_args(argv)
    try:
        if not (args.cdrom or args.reseau):
            raise inst.InstallError('rien à faire : --cdrom et/ou --reseau')
        output = Path(os.path.abspath(args.sortie))
        if os.path.lexists(output) or output.suffix.lower() != '.img':
            raise inst.InstallError(f'sortie existante ou sans .img : {output}')
        try:
            driver = udvd2(args.cdrom) if args.cdrom else None
            tcp = tcpip32(args.reseau) if args.reseau else None
        except ValueError as e:
            raise inst.InstallError(str(e)) from None
        reseau_exe = inst.ROOT / 'dos/build/RESEAU.EXE'
        if tcp:
            inst.run(inst.ROOT / 'dos/build.sh')
        stage = Path(tempfile.mkdtemp(prefix='.pc486-options-', dir=output.parent))
        disk = stage / 'options.img'
        # Copie sous le verrou des émulateurs : refus si une VM utilise la source.
        inst.run(sys.executable, inst.ROOT / 'scripts/with-disk-lock.py', args.source.resolve(), '--',
                 'cp', '--reflink=auto', args.source.resolve(), disk)
        image = inst.disk_partition(disk)
        print('Copie faite ; options en cours', flush=True)
        if driver:
            inst.install_cdrom(stage, image, driver)
        if tcp:
            inst.install_reseau(stage, disk, image, tcp, reseau_exe, args.timeout)
        config, autoexec = (inst.run('mtype', '-i', image, '::/' + n).decode('cp850').replace('\r\n', '\n')
                            for n in ('CONFIG.SYS', 'AUTOEXEC.BAT'))
        try:
            config, autoexec = boot_files(config, autoexec, bool(driver), bool(tcp))
        except ValueError as e:
            raise inst.InstallError(str(e)) from None
        inst.write_dos_file(stage, image, 'CONFIG.SYS', config)
        inst.write_dos_file(stage, image, 'AUTOEXEC.BAT', autoexec)
        print('Vérification : Windows sans carte', flush=True)
        inst.verify_boot(stage, disk, args.timeout)
        if tcp:
            inst.check_reseau(image, False)
            print('Vérification : Windows avec la carte réseau', flush=True)
            inst.verify_boot(stage, disk, args.timeout, nic=True, phase='verification-reseau')
            inst.check_reseau(image, True)
        print('Vérification : DOS avec Épaudos', flush=True)
        inst.verify_epaudos(stage, disk, args.timeout, cdrom=bool(driver))
        inst.publish(disk, output)
        print(f'Image publiée : {output}\nJournaux : {stage}', flush=True)
        return 0
    except (inst.InstallError, OSError, subprocess.SubprocessError, RuntimeError, ConnectionError,
            KeyboardInterrupt) as exc:
        print(f'Options interrompues : {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    import sys
    sys.exit(main())
