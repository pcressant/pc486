# SPDX-License-Identifier: GPL-3.0-or-later
"""Lance une commande (86Box) dans un espace de noms réseau vide, avec relais.

    python3 -m reseau.netns --run DOSSIER --service web:8080 ... -- commande args…

Nouvel espace de noms utilisateur et réseau (sans privilège) : seule la boucle
locale existe, sans route vers l'extérieur ni vers les ports de l'hôte. Un relais
écoute 127.0.0.1:PORT pour chaque service et le relie à DOSSIER/NOM.sock, socket
Unix des services de l'hôte (les sockets nommées ne dépendent pas de l'espace de
noms réseau). Le réseau virtuel (slirp) de 86Box traduit l'hôte 10.0.2.2 par
127.0.0.1 : la VM n'atteint que ces relais. L'utilisateur reste le même (uid et
gid identiques), les fichiers et sockets de la session restent accessibles.
Le relais est un processus fils sans exec : il garde le droit d'ouvrir les ports
inférieurs à 1024 dans cet espace de noms ; il s'arrête avec la commande.
"""
import argparse
import ctypes
import fcntl
import os
import select
import signal
import socket
import struct
import sys

CLONE_NEWUSER = 0x10000000
CLONE_NEWNET = 0x40000000
SIOCGIFFLAGS, SIOCSIFFLAGS, IFF_UP = 0x8913, 0x8914, 0x1
PR_SET_PDEATHSIG = 1


def write(path, text):
    with open(path, 'w') as f:
        f.write(text)


def enter():
    """Espace de noms utilisateur + réseau, même uid/gid ; boucle locale active."""
    uid, gid = os.getuid(), os.getgid()
    os.unshare(CLONE_NEWUSER | CLONE_NEWNET)
    write('/proc/self/setgroups', 'deny')
    write('/proc/self/uid_map', f'{uid} {uid} 1\n')
    write('/proc/self/gid_map', f'{gid} {gid} 1\n')
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        ifr = struct.pack('16sH14x', b'lo', 0)
        flags = struct.unpack('16sH14x', fcntl.ioctl(s, SIOCGIFFLAGS, ifr))[1]
        fcntl.ioctl(s, SIOCSIFFLAGS, struct.pack('16sH14x', b'lo', flags | IFF_UP))
    finally:
        s.close()


def relay(run, services):
    """Fils : relais 127.0.0.1:port -> run/nom.sock, jusqu'à la mort du parent."""
    listeners = {}
    for name, port in services:
        ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        ls.bind(('127.0.0.1', port))
        ls.listen(16)
        listeners[ls] = os.path.join(run, f'{name}.sock')
    r, w = os.pipe()
    pid = os.fork()
    if pid:
        for ls in listeners:
            ls.close()
        os.close(r)
        return w                    # gardé ouvert par le parent (puis par la commande)
    os.close(w)
    libc = ctypes.CDLL(None, use_errno=True)
    libc.prctl(PR_SET_PDEATHSIG, signal.SIGTERM)
    os.setsid()
    pairs = {}
    while True:
        rd = list(listeners) + list(pairs) + [r]
        ready, _, _ = select.select(rd, [], [])
        for s in ready:
            if s == r:
                os._exit(0)         # tube fermé : la commande est finie
            if s in listeners:
                conn, _ = s.accept()
                up = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                try:
                    up.connect(listeners[s])
                except OSError:
                    conn.close()
                    up.close()
                    continue
                pairs[conn], pairs[up] = up, conn
                continue
            try:
                data = s.recv(65536)
            except OSError:
                data = b''
            peer = pairs.get(s)
            if data and peer is not None:
                try:
                    peer.sendall(data)
                    continue
                except OSError:
                    pass
            for x in (s, peer):
                if x is not None:
                    pairs.pop(x, None)
                    x.close()


def main(argv=None):
    p = argparse.ArgumentParser(prog='reseau.netns', description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--run', required=True)
    p.add_argument('--service', action='append', default=[], metavar='NOM:PORT')
    p.add_argument('commande', nargs=argparse.REMAINDER)
    args = p.parse_args(argv)
    cmd = args.commande[1:] if args.commande[:1] == ['--'] else args.commande
    if not cmd:
        p.error('commande manquante')
    services = []
    for item in args.service:
        name, _, port = item.partition(':')
        services.append((name, int(port)))
    try:
        enter()
    except OSError as e:
        print(f'reseau.netns : espace de noms réseau impossible ({e}) ; '
              'la VM ne démarre pas sans isolement', file=sys.stderr)
        return 1
    keep = relay(args.run, services)
    os.set_inheritable(keep, True)  # tube hérité : le relais suit la commande
    os.execvp(cmd[0], cmd)


if __name__ == '__main__':
    sys.exit(main())
