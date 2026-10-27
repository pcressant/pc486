"""Sous-ensemble série Baum utilisé par Épaulard (19200, 8N1).

Requêtes : ESC, commande, arguments avec ESC doublés.
Réponses : ESC, commande, arguments bruts (comme QEMU et le pilote NVDA).
"""
ESC = 0x1B


def packet(command, data):
    return bytes((ESC, command)) + bytes(data)


class Baum:
    def __init__(self, width, display):
        if not 1 <= width <= 80:
            raise ValueError('largeur Baum attendue : 1..80')
        self.width = width
        self.display = display
        self.active = False
        self.command = None
        self.data = bytearray()
        self.escape = False
        self.started = False

    def reset(self):
        self.active = False
        self.command = None
        self.data.clear()
        self.escape = self.started = False

    def feed(self, data):
        reply = bytearray()
        for value in data:
            if not self.started:
                self.started = value == ESC
                continue
            if self.command is None:
                if value not in (0x01, 0x02, 0x15):
                    self.started = value == ESC
                    continue
                self.command = value
                self.escape = False
                self.data.clear()
                continue
            if self.escape:
                self.escape = False
                if value != ESC:
                    # ESC + commande resynchronise après une requête tronquée.
                    self.command = None
                    self.started = True
                    reply.extend(self.feed(bytes((value,))))
                    continue
            elif value == ESC:
                self.escape = True
                continue
            self.data.append(value)
            size = self.width if self.command == 0x01 else 1
            if len(self.data) != size:
                continue
            if self.command == 0x01:
                if self.active:
                    self.display(bytes(self.data))
            else:
                if self.command == 0x15:
                    self.active = bool(self.data[0])
                if self.command == 0x02 or self.active:
                    reply.extend(packet(0x01, (self.width,)))
                    reply.extend(packet(0x84, b'Baum Epaulard'.ljust(16, b' ')))
                    reply.extend(packet(0x8A, b'00000001'))
            self.command = None
            self.started = False
        return bytes(reply)

    def key(self, name, argument=0):
        if not self.active:
            return b''
        mask = {'LNUP': 1, 'FWINLT': 2, 'LNDN': 4, 'FWINRT': 16,
                'HOME': 8}.get(name)
        if mask is not None:
            return packet(0x24, (mask,)) + packet(0x24, (0,))
        if name == 'ROUTE' and 0 <= argument < self.width:
            return packet(0x27, (argument + 1,)) + packet(0x27, (0,))
        return b''
