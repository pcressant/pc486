"""Passerelle série Baum ↔ BrlAPI. Aucune traduction de texte sur l'hôte."""
import argparse
import logging
import os
from pathlib import Path
import select
import signal
import socket
import termios
import threading
import time
import tty

from .baum import Baum

LOG = logging.getLogger('braille')


class Focus:
    """AT-SPI : désactiver BrlAPI lorsque la fenêtre de la VM perd le focus."""
    def __init__(self):
        import pyatspi
        self.api = pyatspi
        self.active = False
        self.api.Registry.registerEventListener(self.event, 'window:activate', 'window:deactivate')
        # État initial, si la fenêtre existait avant la passerelle.
        self.refresh()
        self.thread = threading.Thread(target=self.api.Registry.start, daemon=True)
        self.thread.start()

    @staticmethod
    def matches(window):
        try:
            # Le titre pc486 distingue nos fenêtres des autres VM QEMU.
            application = (window.getApplication().name or '').casefold()
            return application == '86box' or (
                'qemu' in application and 'pc486' in (window.name or '').casefold())
        except Exception:
            return False

    def refresh(self):
        # Les événements Qt peuvent être absents au démarrage ou perdus lors
        # d'une reconnexion AT-SPI. Vérifier aussi l'état réel, sans voler le focus.
        active = False
        try:
            for app in self.api.Registry.getDesktop(0):
                for window in app:
                    if window.getState().contains(self.api.STATE_ACTIVE) and self.matches(window):
                        active = True
        except Exception as error:
            LOG.debug('état du focus indisponible : %s', error)
        if self.active != active:
            LOG.info('focus VM : %s', 'actif' if active else 'inactif')
        self.active = active

    def event(self, event):
        LOG.debug('événement focus %s, fenêtre VM=%s', event.type, self.matches(event.source))
        if event.type.endswith(':activate'):
            self.active = self.matches(event.source)
        elif self.matches(event.source):
            self.active = False

    def close(self):
        self.api.Registry.deregisterEventListener(self.event, 'window:activate', 'window:deactivate')
        self.api.Registry.stop()
        self.thread.join(timeout=1)


class Output:
    def __init__(self):
        import brlapi
        self.api = brlapi
        self.connection = brlapi.Connection()
        try:
            self.connection.enterTtyModeWithPath()
            self.connection.setParameter(brlapi.PARAM_CLIENT_PRIORITY, 0, False, 0)
            self.size = self.connection.displaySize[0] * self.connection.displaySize[1]
            if not 1 <= self.size <= 80:
                raise ValueError('cette version accepte 1 à 80 cellules')
            self.connection.ignoreKeys(brlapi.rangeType_all, (0,))
            names = ('FWINLT', 'FWINRT', 'LNUP', 'LNDN', 'HOME', 'ROUTE')
            self.commands = {getattr(brlapi, 'KEY_CMD_' + n): n for n in names}
            self.connection.acceptKeys(brlapi.rangeType_command, tuple(brlapi.KEY_TYPE_CMD | cmd for cmd in self.commands))
        except Exception:
            self.connection.closeConnection()
            raise
        self.active = False
        self.cells = bytes(self.size)

    def activate(self, active):
        if active != self.active:
            self.connection.setParameter(self.api.PARAM_CLIENT_PRIORITY, 0, False, 50 if active else 0)
            self.active = active
            LOG.info('accès plage : %s', 'actif' if active else 'rendu à l’hôte')
            if active:
                self.connection.writeDots(self.cells)

    def display(self, cells):
        self.cells = cells
        if self.active:
            self.connection.writeDots(cells)

    def keys(self):
        for _ in range(32):
            code = self.connection.readKey(False)
            if not code:
                break
            key = self.api.expandKeyCode(code)
            if self.active and key['type'] == self.api.KEY_TYPE_CMD:
                yield self.commands.get(key['command']), key['argument']

    def close(self):
        try:
            self.activate(False)
        finally:
            self.connection.closeConnection()


def connect(args):
    if args.device:
        fd = os.open(args.device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            tty.setraw(fd)
            attrs = termios.tcgetattr(fd)
            attrs[4] = attrs[5] = termios.B19200
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
        except Exception:
            os.close(fd)
            raise
        return fd, lambda: os.close(fd)
    sock = socket.socket(socket.AF_UNIX)
    try:
        sock.connect(args.connect)
        sock.setblocking(False)
    except Exception:
        sock.close()
        raise
    return sock.fileno(), sock.close


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument('--connect', help='socket COM3 QEMU')
    transport.add_argument('--device', help='PTY COM3 86Box')
    parser.add_argument('--focus', choices=('auto', 'always'), default='auto',
                        help='always : essais privés sans interface graphique')
    parser.add_argument('--check', action='store_true', help='vérifier BrlAPI et AT-SPI sans ouvrir la plage')
    parser.add_argument('--trace', action='store_true', help='journaliser les octets Baum RX/TX')
    args = parser.parse_args()
    if args.check:
        import brlapi
        import pyatspi
        print('BrlAPI et AT-SPI disponibles.')
        return
    if not args.connect and not args.device:
        parser.error('préciser --connect ou --device')
    if not os.environ.get('CONTROLVT'):
        for session in Path('/run/systemd/sessions').glob('*'):
            try:
                values = dict(line.split('=', 1) for line in session.read_text().splitlines()
                              if '=' in line)
            except OSError:
                continue
            if values.get('UID') == str(os.getuid()) and values.get('ACTIVE') == '1' and values.get('VTNR'):
                os.environ['CONTROLVT'] = values['VTNR']
                break
    logging.basicConfig(level=logging.DEBUG if args.trace else logging.INFO, format='%(asctime)s %(message)s')
    running = True

    def stop(*_):
        nonlocal running
        running = False
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    # Pas de prise de contrôle implicite si AT-SPI est indisponible.
    focus = Focus() if args.focus == 'auto' else None
    output = None
    close = None
    try:
        while running:
            try:
                output = Output()
                fd, close = connect(args)
                protocol = Baum(output.size, output.display)
                pending = bytearray()
                last_guest = time.monotonic()
                next_focus_check = 0
                LOG.info('Baum connecté, %d cellules', output.size)
                while running:
                    if focus is not None and time.monotonic() >= next_focus_check:
                        focus.refresh()
                        next_focus_check = time.monotonic() + 1
                    output.activate(protocol.active and time.monotonic() - last_guest < 6
                                    and (focus is None or focus.active))
                    readable, writable, _ = select.select([fd], [fd] if pending else [], [], .05)
                    if readable:
                        data = os.read(fd, 4096)
                        if not data:
                            raise ConnectionError('liaison série fermée')
                        LOG.debug("RX Baum %s", data.hex(" "))
                        last_guest = time.monotonic()
                        pending.extend(protocol.feed(data))
                    for name, argument in output.keys():
                        pending.extend(protocol.key(name, argument))
                    if len(pending) > 8192:
                        raise ConnectionError('file série saturée')
                    if writable:
                        try:
                            sent = os.write(fd, pending)
                            LOG.debug("TX Baum %s", pending[:sent].hex(" "))
                            del pending[:sent]
                        except BlockingIOError:
                            pass
            except Exception as error:
                LOG.warning('connexion braille : %s', error)
            finally:
                if close:
                    close()
                    close = None
                if output:
                    try:
                        output.close()
                    except Exception:
                        LOG.debug('fermeture BrlAPI', exc_info=True)
                    output = None
            if running:
                time.sleep(1)
    finally:
        if focus:
            focus.close()


if __name__ == '__main__':
    main()
