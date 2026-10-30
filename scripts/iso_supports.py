# SPDX-License-Identifier: GPL-3.0-or-later
"""ISO locale des supports d'installation, décompressés, pour le lecteur E: de la VM.

Chaque support (ZIP ou dossier, mêmes options que scripts/installer) est lu par les
fonctions de l'installateur, puis le contenu de ses disquettes est extrait avec mtools
et rangé comme l'installateur le range dans C:\\INSTALL :

  DOS622\\    images DISK1..3.IMG (SETUP démarre sur la disquette 1), SUPPL.IMG et
              SUPPL\\ (contenu du disque supplémentaire)
  WFW311\\    toutes les disquettes de Windows dans un seul dossier (INSTALL.EXE)
  WORD6\\, EXCEL\\, ACCESS\\   un dossier DISKn par disquette (ACME) ; ACCESS\\AN2000\\
  AMIPRO\\, WORDDOS\\         toutes les disquettes dans un seul dossier ;
              WORDDOS\\GRAMMAT\\ : Grammatik, installateur séparé (INSTALLE.EXE)
  NETSCAPE\\, COMMUNIC\\, EUDORA\\   dossier d'installation de l'archive
  TCPIP32\\   fichiers de TCP/IP-32 3.11b (disquette « fournie par le constructeur »)
  AUTRES\\    --autres : toute autre archive (.zip, .7z, .rar) du dossier, décompressée,
              images de disquettes et ZIP imbriqués compris (noms ramenés en 8.3)

L'ISO contient des logiciels tiers : elle reste locale (jamais dans /nix/store ni
dans le dépôt). Usage : `pc486 start --cdrom ISO`, sur un disque installé avec --cdrom.
"""
import argparse
import itertools
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import installation as inst  # noqa: E402
import options_vm  # noqa: E402

ARCHIVES = ('.zip', '.7z', '.rar')
# Tailles des disquettes FAT : 360 Ko à 2,88 Mo, DMF 1,68 Mo compris.
FLOPPY_SIZES = (368640, 737280, 1228800, 1474560, 1720320, 2949120)
SCRATCH = itertools.count()   # dossiers de travail uniques
VOLUME = 'SUPPORTS486'   # MSCDEX n'affiche que 11 caractères


def extract(floppy, dest):
    """Contenu d'une image de disquette, sous-dossiers compris ; erreur de lecture : arrêt."""
    dest.mkdir(parents=True, exist_ok=True)
    inst.run('mcopy', '-s', '-n', '-m', '-i', floppy, '::/*', dest)
    if not any(dest.iterdir()):
        raise inst.InstallError(f'disquette vide ou illisible : {floppy}')


def merge(sources, dest, overwrite=False):
    """Dossiers réunis en un seul. Un même nom doit avoir le même contenu, sauf
    overwrite (Lotus : la dernière disquette l'emporte, comme dans l'installateur)."""
    dest.mkdir(parents=True, exist_ok=True)
    for src in sources:
        for p in sorted(src.rglob('*')):
            if not p.is_file():
                continue
            out = dest / p.relative_to(src)
            if out.exists() and not overwrite and inst.digest(out) != inst.digest(p):
                raise inst.InstallError(f'fichiers homonymes différents : {p.relative_to(src)}')
            out.parent.mkdir(parents=True, exist_ok=True)
            if overwrite or not out.exists():
                shutil.copyfile(p, out)


def per_disk(media, dest, work):
    for i, floppy in enumerate(media, 1):
        extract(floppy, dest / f'DISK{i}')


def merged(media, dest, work, overwrite=False):
    parts = []
    for i, floppy in enumerate(media, 1):
        part = work / f'{dest.name}-{i}'
        extract(floppy, part)
        parts.append(part)
    merge(parts, dest, overwrite)


def build_tree(args, root, work):
    """Arborescence de l'ISO dans root ; renvoie les lignes de LISEZMOI.TXT."""
    notes = []
    if args.dos:
        dos = inst.collect_media(args.dos, 3, work / 'dos')
        for p in dos:
            inst.run('mdir', '-i', p, '::/')
        if b'6.22' not in inst.run('mtype', '-i', dos[0], '::/COMMAND.COM'):
            raise inst.InstallError('la disquette DOS 1 ne contient pas MS-DOS 6.22')
        (root / 'DOS622').mkdir()
        for i, p in enumerate(dos, 1):
            shutil.copyfile(p, root / f'DOS622/DISK{i}.IMG')
        notes.append('DOS622     MS-DOS 6.22 : images DISK1..3.IMG (amorcer sur DISK1)')
    if args.suppl:
        suppl = inst.collect_suppl(args.suppl, work / 'suppl.img')
        (root / 'DOS622').mkdir(exist_ok=True)
        shutil.copyfile(suppl, root / 'DOS622/SUPPL.IMG')
        extract(suppl, root / 'DOS622/SUPPL')
        notes.append('DOS622\\SUPPL  disque supplémentaire de MS-DOS 6.22 (SETUP.BAT)')
    if args.windows:
        windows = inst.collect_media(args.windows, 9, work / 'windows')
        if b'Windows for Workgroups 3.11' not in inst.run('mtype', '-i', windows[0], '::/SETUP.INF'):
            raise inst.InstallError('supports Windows pour Workgroups 3.11 attendus')
        merged(windows, root / 'WFW311', work)
        notes.append('WFW311     Windows pour Workgroups 3.11 : INSTALL.EXE')
    if args.word:
        word = inst.collect_media(args.word, 11, work / 'word')
        inst.validate_word(word)
        per_disk(word, root / 'WORD6', work)
        notes.append('WORD6      Word 6.0c : DISK1\\' + inst.ACME['word'].setup)
    acme = (('excel', ('excel', 'excel50')), ('access', ('access',)), ('amipro', ('amipro',)),
            ('netscape', ('netscape',)), ('communicator', ('communicator',)), ('eudora', ('eudora',)))
    for option, keys in acme:
        source = getattr(args, option)
        if not source:
            continue
        product, media = inst.collect_acme(source, work / option, keys)
        dest = root / product.folder
        if product.archive:
            merge(media, dest)
            start = product.setup
        elif product.merge:
            merged(media, dest, work, overwrite=True)
            start = product.setup
        else:
            per_disk(media, dest, work)
            start = 'DISK1\\' + product.setup
        notes.append(f'{product.folder:<10} {product.title} : {start}')
    if args.access_an2000:
        dlls = inst.collect_access_y2k(args.access_an2000, work / 'an2000')
        (root / 'ACCESS/AN2000').mkdir(parents=True)
        for p in dlls:
            shutil.copyfile(p, root / 'ACCESS/AN2000' / p.name)
        notes.append('ACCESS\\AN2000  an 2000 : MSABC200.DLL -> WINDOWS\\SYSTEM, MSAJU200.DLL -> ACCESS')
    if args.word_dos:
        media = inst.collect_word_dos(args.word_dos, work / 'word-dos')
        inst.validate_word_dos(media)
        merged(media, root / 'WORDDOS', work)
        notes.append('WORDDOS    Word 6.0 pour DOS : INIT.EXE')
        # Grammatik : installateur séparé, ignoré par scripts/installer ; facultatif.
        try:
            grammar = inst.collect_word_dos(args.word_dos, work / 'grammar', ('GRAMMAR_60',))
        except inst.InstallError:
            grammar = []
        if grammar:
            extract(grammar[0], root / 'WORDDOS/GRAMMAT')
            notes.append('WORDDOS\\GRAMMAT  Grammatik (correcteur) : INSTALLE.EXE')
    if args.reseau:
        try:
            files = options_vm.tcpip32(args.reseau)
        except ValueError as e:
            raise inst.InstallError(str(e)) from None
        (root / 'TCPIP32').mkdir()
        for name, data in files.items():
            (root / 'TCPIP32' / name).write_bytes(data)
        notes.append('TCPIP32    TCP/IP-32 3.11b (OEMSETUP.INF)')
    if args.autres:
        notes += others(args, root / 'AUTRES', work / 'autres')
    if not notes:
        raise inst.InstallError('aucun support demandé')
    return notes


def dos_name(name, taken, folder=False):
    """Nom DOS 8.3 unique dans taken (mis à jour). Les caractères permis par DOS sont
    gardés (« ! » des fichiers compressés de Norton, « $ » de Word pour DOS) : xorriso
    les conserve, et un installateur cherche ses fichiers sous leur nom exact."""
    stem, dot, ext = name.upper().rpartition('.')
    if folder or not dot or not stem:
        stem, ext = name.upper(), ''

    def clean(t):
        return re.sub(r'[^A-Z0-9_$~!#%&-]', '_', re.sub(r'[. +]', '', t))
    stem, ext = clean(stem) or '_', clean(ext)[:3]
    for n in range(100):
        base = stem[:8] if n == 0 else stem[:7 - len(str(n))] + '_' + str(n)
        out = base + ('.' + ext if ext else '')
        if out not in taken:
            taken.add(out)
            return out
    raise inst.InstallError(f'trop de noms semblables : {name}')


def unpack(archive, dest):
    """Archive décompressée dans dest ; les outils refusent les chemins sortants."""
    dest.mkdir(parents=True)
    kind = archive.suffix.lower()
    if kind == '.zip':
        with zipfile.ZipFile(archive) as z:
            z.extractall(dest)   # zipfile retire « .. » et les chemins absolus
        return
    tool = {'.7z': '7zz', '.rar': 'unar'}[kind]
    if not shutil.which(tool):
        raise inst.InstallError(f'{tool} introuvable pour {archive.name} (nix run .#iso le fournit)')
    if kind == '.7z':
        inst.run('7zz', 'x', '-y', '-bd', f'-o{dest}', archive)
    else:
        inst.run('unar', '-q', '-f', '-D', '-o', dest, archive)


def place(src, dest, work, renames, rel=''):
    """Copie de src dans dest en noms 8.3 ; images de disquettes et ZIP décompressés."""
    dest.mkdir(parents=True, exist_ok=True)
    taken = {p.name for p in dest.iterdir()}
    for p in sorted(src.iterdir(), key=lambda p: p.name.upper()):
        inner = None
        if p.is_file() and p.suffix.lower() in ('.ima', '.img', '.vfd', '.dsk') \
                and p.stat().st_size in FLOPPY_SIZES:
            inner = work / f'f{next(SCRATCH)}'
            try:
                extract(p, inner)
            except inst.InstallError:
                inner = None       # pas une disquette FAT lisible : gardée telle quelle
        elif p.is_file() and p.suffix.lower() == '.zip' and zipfile.is_zipfile(p):
            inner = work / f'z{next(SCRATCH)}'
            unpack(p, inner)
        if p.is_dir() or inner:
            name = dos_name(p.stem if inner else p.name, taken, folder=True)
        else:
            name = dos_name(p.name, taken)
        original = rel + p.name
        if name != p.name.upper() or inner:
            renames.append((original, (rel + name) if not inner else (rel + name + '\\')))
        if p.is_dir() or inner:
            place(inner or p, dest / name, work, renames, rel + name + '\\')
        else:
            shutil.copyfile(p, dest / name)


NOISE = {'win3x', 'win31x', 'w31', 'floppy35hd', '525hd', 'zip'}
LANGS = {'fr', 'en', 'de', 'it', 'es', 'nl'}


def short_label(text):
    """Nom de dossier parlant en 8 caractères : début du nom, puis version et langue
    gardées (communicator405-en -> COM405EN, qemm-7.5-fr-win3x -> QEMM75FR)."""
    alpha, digits, lang = '', '', ''
    for token in re.split(r'[-_. ]+', text.lower()):
        if token in NOISE:
            continue
        if token in LANGS:
            lang = lang or token
            continue
        for part in re.findall(r'[a-z]+|[0-9]+', token):
            if part.isdigit():
                digits += part
            else:
                alpha += part
    tail = (digits + lang)[:5]
    return (alpha[:8 - len(tail)] + tail).upper() or 'ARCHIVE'


def others(args, dest, work):
    """--autres : archives du dossier non prises par une option de produit."""
    folder = args.autres.resolve()
    if not folder.is_dir():
        raise inst.InstallError(f'dossier introuvable : {args.autres}')
    used = {getattr(args, o.replace('-', '_')).resolve() for o in PRODUCTS
            if getattr(args, o.replace('-', '_'))}
    archives = sorted(p for p in folder.rglob('*')
                      if p.is_file() and p.suffix.lower() in ARCHIVES and p.resolve() not in used)
    notes, taken = [], set()
    for i, archive in enumerate(archives):
        raw = work / f'a{i}'
        unpack(archive, raw)
        # Dossier de media/ à une seule archive : son nom est le plus parlant.
        alone = archive.parent != folder and sum(
            q.suffix.lower() in ARCHIVES for q in archive.parent.iterdir()) == 1
        name = dos_name(short_label(archive.parent.name if alone else archive.stem), taken, folder=True)
        renames = []
        place(raw, dest / name, work / f't{i}', renames)
        if renames:
            text = ''.join(f'{new:<40} {old}\r\n' for old, new in renames)
            index = dest / name / dos_name('NOMS.TXT', {q.name for q in (dest / name).iterdir()})
            index.write_bytes(
                ('Noms d\'origine (images et ZIP decompresses dans un dossier) :\r\n'
                 + text).encode('cp850', 'replace'))
        notes.append(f'AUTRES\\{name:<9} {archive.relative_to(folder)}')
    return notes


def check_names(root):
    """Noms DOS 8.3 et profondeur ISO 9660 niveau 1, sinon xorriso les changerait."""
    for p in root.rglob('*'):
        rel = p.relative_to(root)
        if len(rel.parts) > 8:
            raise inst.InstallError(f'chemin trop profond pour ISO 9660 : {rel}')
        if not inst.SHORT_NAME.fullmatch(p.name.upper()) or (p.is_dir() and '.' in p.name):
            raise inst.InstallError(f'nom non DOS 8.3 : {rel}')


def write_readme(root, notes):
    text = ('Supports d\'installation pc486, décompressés (lecteur E:).\r\n'
            'Copie locale de logiciels tiers : ne pas diffuser.\r\n\r\n'
            + ''.join(n + '\r\n' for n in notes))
    (root / 'LISEZMOI.TXT').write_bytes(text.encode('cp850'))


def make_iso(root, output):
    tool = shutil.which('xorriso')
    if not tool:
        raise inst.InstallError('xorriso introuvable (nix run .#iso le fournit)')
    for p in root.rglob('*'):
        if p.is_file():
            p.rename(p.with_name(p.name.upper()))
    for p in sorted(root.rglob('*'), key=lambda p: -len(p.parts)):
        if p.is_dir() and p.name != p.name.upper():
            p.rename(p.with_name(p.name.upper()))
    check_names(root)
    # -relaxed-filenames : sans lui, xorriso change « $ », « ! », « ~ », « - »… en « _ »
    # dans les noms ISO 9660 lus par MSCDEX (WORD.EX$ de Word pour DOS introuvable).
    r = subprocess.run([tool, '-as', 'mkisofs', '-quiet', '-iso-level', '1', '-relaxed-filenames',
                        '-V', VOLUME, '-o', str(output), str(root)], capture_output=True, text=True)
    if r.returncode:
        raise inst.InstallError('xorriso : ' + (r.stderr.strip() or 'échec'))
    expected = {'/' + str(p.relative_to(root)) for p in root.rglob('*')}
    written = set(iso_names(output))
    if written != expected:
        diff = sorted(expected ^ written)[:5]
        raise inst.InstallError('noms changés dans l\'ISO : ' + ', '.join(diff))


def iso_names(path):
    """Chemins ISO 9660 tels que DOS les voit (sans « ;1 »), lus dans les
    enregistrements de répertoire, sans Rock Ridge ni Joliet."""
    with open(path, 'rb') as f:
        def read(sector, size):
            f.seek(sector * 2048)
            return f.read(size)
        pvd = read(16, 2048)
        if pvd[:6] != b'\x01CD001':
            raise inst.InstallError(f'pas une ISO 9660 : {path}')
        stack = [('', int.from_bytes(pvd[158:162], 'little'), int.from_bytes(pvd[166:170], 'little'))]
        while stack:
            prefix, sector, size = stack.pop()
            data, i = read(sector, size), 0
            while i < len(data):
                n = data[i]
                if n == 0:                          # fin du secteur
                    i = (i // 2048 + 1) * 2048
                    continue
                record = data[i:i + n]
                i += n
                name = record[33:33 + record[32]]
                if name in (b'\0', b'\1'):
                    continue
                name = name.decode('ascii').split(';')[0].rstrip('.')
                yield prefix + '/' + name
                if record[25] & 2:
                    stack.append((prefix + '/' + name, int.from_bytes(record[2:6], 'little'),
                                  int.from_bytes(record[10:14], 'little')))


PRODUCTS = ('dos', 'windows', 'suppl', 'word', 'excel', 'access', 'access-an2000', 'amipro',
            'netscape', 'communicator', 'eudora', 'word-dos', 'reseau')


def parser():
    p = argparse.ArgumentParser(
        prog='nix run .#iso --',
        description="ISO locale des supports, décompressés (un dossier par logiciel). Mêmes "
                    "options que scripts/installer, toutes facultatives ; aucun support "
                    "n'est modifié et rien n'entre dans /nix/store.")
    p.add_argument('--sortie', type=Path, required=True, help='image ISO à créer (refus si elle existe)')
    for name in PRODUCTS:
        p.add_argument('--' + name, type=Path, help='comme scripts/installer')
    p.add_argument('--autres', type=Path, metavar='DOSSIER',
                   help='ajoute dans AUTRES\\ toute archive (.zip, .7z, .rar) de ce dossier '
                        'qui n\'est pas déjà donnée par une option ci-dessus')
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    output = Path(os.path.abspath(args.sortie))
    try:
        if output.suffix.lower() != '.iso':
            raise inst.InstallError('la sortie doit se terminer par .iso')
        if os.path.lexists(output):
            raise inst.InstallError(f'sortie déjà existante, aucune écriture : {output}')
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.pc486-iso-', dir=output.parent) as tmp:
            tmp = Path(tmp)
            root, work = tmp / 'iso', tmp / 'travail'
            root.mkdir()
            work.mkdir()
            notes = build_tree(args, root, work)
            write_readme(root, notes)
            partial = tmp / 'supports.iso'
            make_iso(root, partial)
            os.link(partial, output)
    except (inst.InstallError, OSError, subprocess.SubprocessError) as e:
        print(f'ISO non créée : {e}', file=sys.stderr)
        return 1
    print(f'ISO créée : {output}')
    for n in notes:
        print('  ' + n)
    print(f'Utilisation : scripts/pc486 start --cdrom {output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
