#!/usr/bin/env python3
"""Verrouille les images pendant une commande ; les verrous survivent à exec."""
import argparse
import configparser
import fcntl
import os
from pathlib import Path
import sys


def check_open(disk):
    """Complément pour les émulateurs lancés sans notre verrou (nom tronqué inclus)."""
    for proc in Path('/proc').iterdir():
        if not proc.name.isdecimal() or int(proc.name) == os.getpid():
            continue
        try:
            for fd in (proc / 'fd').iterdir():
                try:
                    if fd.samefile(disk):
                        raise RuntimeError(f'image ouverte par le processus {proc.name} : {disk}')
                except FileNotFoundError:
                    pass
        except (FileNotFoundError, ProcessLookupError):
            continue
        except PermissionError:
            # Les processus d’un autre utilisateur ne sont pas tous inspectables.
            # Refuser si leur nom indique un émulateur, même tronqué à 15 caractères.
            try:
                name = (proc / 'comm').read_text().strip().lower()
            except (FileNotFoundError, PermissionError):
                continue
            if name.startswith(('qemu-system-', '86box')):
                raise RuntimeError(f'émulateur non inspectable : processus {proc.name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--86box-dir', dest='box_dir', type=Path)
    # Configuration lue par 86Box, si ce n’est pas 86box.cfg dans --86box-dir ;
    # ses chemins relatifs restent résolus depuis --86box-dir (option -P de 86Box).
    parser.add_argument('--86box-cfg', dest='box_cfg', type=Path)
    parser.add_argument('args', nargs=argparse.REMAINDER)
    opts = parser.parse_args()
    split = opts.args.index('--')
    disks = [Path(p).resolve(strict=True) for p in opts.args[:split]]
    if opts.box_dir:
        directory = opts.box_dir.resolve()
        cfg = configparser.ConfigParser(interpolation=None, strict=False)
        path = opts.box_cfg or directory / '86box.cfg'
        if not cfg.read(path):
            raise RuntimeError(f'configuration 86Box illisible : {path}')
        for section in cfg.sections():
            for key, value in cfg.items(section):
                if key.startswith('hdd_') and key.endswith('_fn') and value.strip():
                    disks.append((directory / value.strip()).resolve(strict=True))
    # Ordre stable ; jamais de délai d’attente ni de suppression d’un fichier verrou.
    handles = []
    for disk in sorted(set(disks)):
        handle = open(str(disk) + '.pc486.lock', 'a+b')
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError(f'image déjà verrouillée : {disk}') from None
        handles.append(handle)
        check_open(disk)
        os.set_inheritable(handle.fileno(), True)
    command = opts.args[split + 1:]
    os.execvp(command[0], command)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, ValueError, IndexError) as exc:
        sys.exit(f'Protection des disques : {exc}')
