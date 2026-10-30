# SPDX-License-Identifier: GPL-3.0-or-later
"""python3 -m reseau --run DOSSIER --donnees DOSSIER : proxy web et courrier local.

Sockets Unix DOSSIER/web.sock, pop3.sock et smtp.sock, reliées à la VM par QEMU
(guestfwd) ou par l'espace de noms de 86Box (netns.py). Journal sur la sortie
d'erreur : une ligne par page ou message.
"""
import argparse
import os
import signal
import sys
import threading

from . import courrier, debit, proxy


def main(argv=None):
    p = argparse.ArgumentParser(prog='reseau', description=__doc__)
    p.add_argument('--run', required=True, help='dossier des sockets (PC486_RUN)')
    p.add_argument('--donnees', required=True, help='dossier de la boîte aux lettres locale')
    args = p.parse_args(argv)
    os.makedirs(args.run, exist_ok=True)
    servers = [proxy.Server(os.path.join(args.run, 'web.sock'), proxy.Proxy(data=args.donnees)),
               courrier.Server(os.path.join(args.run, 'pop3.sock'), courrier.Pop3,
                               os.path.join(args.donnees, 'courrier'), proxy.log),
               courrier.Server(os.path.join(args.run, 'smtp.sock'), courrier.Smtp,
                               os.path.join(args.donnees, 'courrier'), proxy.log)]
    for s in servers:
        threading.Thread(target=s.serve_forever, daemon=True).start()
    rate = debit.BUCKET.rate // 1024
    proxy.log(f'services prêts : web, pop3, smtp dans {args.run} ; débit vers la VM '
              + (f'{rate} Ko/s' if rate else 'illimité')
              + (' ; Chromium disponible' if proxy.chromium() else ''))
    stop = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    stop.wait()
    for s in servers:
        s.shutdown()
        s.server_close()
        try:
            os.unlink(s.server_address)
        except OSError:
            pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
