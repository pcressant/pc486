# SPDX-License-Identifier: GPL-3.0-or-later
"""Boîte aux lettres locale de test : SMTP et POP3 pour Eudora ou Netscape.

Aucun vrai compte : tout message envoyé (à n'importe quelle adresse) est déposé dans
la boîte unique, rangée en Maildir sous le dossier de données de l'hôte ; POP3 la
sert à n'importe quel nom d'utilisateur et mot de passe. Rien ne sort de l'hôte.
"""
import mailbox
import os
import socketserver
import threading
import time
from email.utils import formatdate, make_msgid

from . import debit

LOCK = threading.Lock()

WELCOME = ('From: pc486 <pc486@pc486>\r\nTo: utilisateur@pc486\r\n'
           'Subject: Bienvenue dans la boîte de test\r\n'
           'MIME-Version: 1.0\r\nContent-Type: text/plain; charset=iso-8859-1\r\n'
           'Content-Transfer-Encoding: 8bit\r\n'
           f'Date: {formatdate(localtime=True)}\r\nMessage-ID: <bienvenue@pc486>\r\n\r\n'
           'Bonjour,\r\n\r\nCette boîte aux lettres est locale : les messages envoyés depuis la VM\r\n'
           '(serveur SMTP 10.0.2.2) arrivent ici, quel que soit le destinataire, et se\r\n'
           'relèvent par POP3 (serveur 10.0.2.2, nom et mot de passe quelconques).\r\n'
           'Aucun message ne quitte l\'ordinateur.\r\n')


def box(root):
    # Maildir ne crée cur/new/tmp que s'il crée aussi le dossier lui-même
    os.makedirs(os.path.dirname(os.path.abspath(root)), exist_ok=True)
    md = mailbox.Maildir(root, create=True)
    for sub in ('cur', 'new', 'tmp'):
        os.makedirs(os.path.join(root, sub), exist_ok=True)
    if not os.path.exists(os.path.join(root, '.pc486-bienvenue')):
        with LOCK:
            md.add(WELCOME.encode('latin-1'))
        open(os.path.join(root, '.pc486-bienvenue'), 'w').close()
    return md


class Line(socketserver.StreamRequestHandler):
    timeout = 600

    def setup(self):
        super().setup()
        self.wfile = debit.Limited(self.wfile)

    def send(self, line):
        self.wfile.write(line.encode('latin-1') + b'\r\n')

    def readline(self):
        raw = self.rfile.readline(65536)
        if not raw:
            raise EOFError
        return raw.rstrip(b'\r\n').decode('latin-1')


class Smtp(Line):
    def handle(self):
        self.send('220 pc486 SMTP boîte locale de test')
        sender, rcpts = None, []
        try:
            while True:
                line = self.readline()
                verb = line[:4].upper()
                if verb in ('HELO', 'EHLO'):
                    self.send('250 pc486')
                elif verb == 'MAIL':
                    sender, rcpts = line[10:].strip(), []
                    self.send('250 OK')
                elif verb == 'RCPT':
                    if sender is None:
                        self.send('503 MAIL d\'abord')
                    else:
                        rcpts.append(line[8:].strip())
                        self.send('250 OK')
                elif verb == 'DATA':
                    if not rcpts:
                        self.send('503 RCPT d\'abord')
                        continue
                    self.send('354 Fin par une ligne contenant un point')
                    lines = []
                    while True:
                        raw = self.rfile.readline(1 << 20)
                        if not raw:
                            raise EOFError
                        if raw.rstrip(b'\r\n') == b'.':
                            break
                        if raw.startswith(b'..'):
                            raw = raw[1:]
                        lines.append(raw.rstrip(b'\r\n'))
                    msg = b'\r\n'.join(lines) + b'\r\n'
                    if b'\r\nmessage-id:' not in (b'\r\n' + msg.split(b'\r\n\r\n', 1)[0]).lower():
                        msg = f'Message-ID: {make_msgid("pc486")}\r\n'.encode() + msg
                    with LOCK:
                        self.server.box.add(msg)
                    self.server.log(f'SMTP : message de {sender} pour {", ".join(rcpts)} '
                                    f'({len(msg)} octets) déposé')
                    sender, rcpts = None, []
                    self.send('250 Message déposé dans la boîte locale')
                elif verb == 'RSET':
                    sender, rcpts = None, []
                    self.send('250 OK')
                elif verb == 'NOOP':
                    self.send('250 OK')
                elif verb == 'QUIT':
                    self.send('221 Au revoir')
                    return
                else:
                    self.send('502 Commande non prise en charge')
        except (EOFError, OSError, TimeoutError):
            return


class Pop3(Line):
    def handle(self):
        self.send('+OK pc486 POP3 boîte locale de test')
        with LOCK:
            keys = sorted(self.server.box.keys())     # clés Maildir : horodatage en tête
        msgs = []
        for k in keys:
            with LOCK:
                data = self.server.box.get_bytes(k)
            data = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
            msgs.append((k, data))
        deleted = set()
        self.sent = 0
        try:
            while True:
                line = self.readline()
                parts = line.split()
                verb = parts[0].upper() if parts else ''
                arg = parts[1:] if len(parts) > 1 else []

                def number():
                    try:
                        n = int(arg[0])
                    except (IndexError, ValueError):
                        return None
                    return n if 1 <= n <= len(msgs) and n not in deleted else None

                if verb in ('USER', 'PASS', 'APOP'):
                    self.send('+OK')
                elif verb == 'CAPA':
                    self.send('+OK')
                    for c in ('USER', 'UIDL', 'TOP', '.'):
                        self.send(c)
                elif verb == 'STAT':
                    live = [d for i, (_, d) in enumerate(msgs, 1) if i not in deleted]
                    self.send(f'+OK {len(live)} {sum(len(d) for d in live)}')
                elif verb in ('LIST', 'UIDL'):
                    if arg:
                        n = number()
                        if n is None:
                            self.send('-ERR message inconnu')
                        else:
                            info = len(msgs[n - 1][1]) if verb == 'LIST' else msgs[n - 1][0]
                            self.send(f'+OK {n} {info}')
                        continue
                    self.send('+OK')
                    for i, (k, d) in enumerate(msgs, 1):
                        if i not in deleted:
                            self.send(f'{i} {len(d) if verb == "LIST" else k}')
                    self.send('.')
                elif verb in ('RETR', 'TOP'):
                    n = number()
                    if n is None:
                        self.send('-ERR message inconnu')
                        continue
                    data = msgs[n - 1][1]
                    if verb == 'TOP':
                        try:
                            count = int(arg[1])
                        except (IndexError, ValueError):
                            self.send('-ERR TOP n lignes')
                            continue
                        head, _, body = data.partition(b'\r\n\r\n')
                        data = head + b'\r\n\r\n' + b'\r\n'.join(body.split(b'\r\n')[:count])
                    self.send('+OK')
                    for raw in data.split(b'\r\n'):
                        self.wfile.write((b'.' + raw if raw.startswith(b'.') else raw) + b'\r\n')
                    self.send('.')
                    self.sent += len(data)
                elif verb == 'DELE':
                    n = number()
                    if n is None:
                        self.send('-ERR message inconnu')
                    else:
                        deleted.add(n)
                        self.send('+OK supprimé')
                elif verb == 'RSET':
                    deleted.clear()
                    self.send('+OK')
                elif verb == 'NOOP':
                    self.send('+OK')
                elif verb == 'QUIT':
                    with LOCK:
                        for n in deleted:
                            try:
                                self.server.box.remove(msgs[n - 1][0])
                            except KeyError:
                                pass
                    if deleted:
                        self.server.log(f'POP3 : {len(deleted)} message(s) supprimé(s)')
                    self.send('+OK Au revoir')
                    return
                else:
                    self.send('-ERR commande non prise en charge')
        except (EOFError, OSError, TimeoutError):
            return
        finally:
            if self.sent:
                self.server.log(f'POP3 : {self.sent} octets de messages envoyés')


class Server(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
    daemon_threads = True

    def __init__(self, path, handler, maildir, log=None):
        if os.path.exists(path):
            os.unlink(path)
        super().__init__(path, handler)
        self.box = box(maildir)
        self.log = log or (lambda msg: print(time.strftime('%H:%M:%S'), msg, flush=True))
