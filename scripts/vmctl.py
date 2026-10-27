#!/usr/bin/env python3
"""Pilotage de la VM pc486 par QMP, sans captures d'écran.

  vmctl.py screen              écran texte VGA (mémoire 0xB8000) en UTF-8
  vmctl.py type 'texte\\n'      tape du texte (clavier QWERTY, avant KEYB FR)
  vmctl.py type --fr 'texte\\n' idem, une fois KEYB FR chargé (AZERTY)
  vmctl.py key ctrl-alt-delete  envoie une combinaison (noms QEMU « qcode »)
  vmctl.py wait 'C:\\\\>' 60      attend qu'une expression régulière apparaisse à l'écran
  vmctl.py floppy FICHIER.img  change la disquette A: (« eject » pour l'éjecter)
  vmctl.py shot FICHIER.png    capture de l'écran (aussi en mode graphique)
  vmctl.py hmp 'info status'   commande du moniteur QEMU
  vmctl.py quit                arrêt immédiat de QEMU
"""

import json
import os
import re
import socket
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QMP = os.environ.get("PC486_QMP", os.path.join(os.environ.get("PC486_RUN", os.path.join(ROOT, "run")), "qmp.sock"))

# caractère -> (qcode, shift) pour un clavier US (DOS sans KEYB)
US = {}
for c in "abcdefghijklmnopqrstuvwxyz":
    US[c] = (c, False)
    US[c.upper()] = (c, True)
for c, k in zip("0123456789", "0123456789"):
    US[c] = (k, False)
for c, k in zip(")!@#$%^&*(", "0123456789"):
    US[c] = (k, True)
US.update({
    " ": ("spc", False), "\n": ("ret", False), "\t": ("tab", False),
    "-": ("minus", False), "_": ("minus", True), "=": ("equal", False), "+": ("equal", True),
    "[": ("bracket_left", False), "{": ("bracket_left", True),
    "]": ("bracket_right", False), "}": ("bracket_right", True),
    ";": ("semicolon", False), ":": ("semicolon", True),
    "'": ("apostrophe", False), '"': ("apostrophe", True),
    "`": ("grave_accent", False), "~": ("grave_accent", True),
    "\\": ("backslash", False), "|": ("backslash", True),
    ",": ("comma", False), "<": ("comma", True), ".": ("dot", False), ">": ("dot", True),
    "/": ("slash", False), "?": ("slash", True),
})

# caractère -> liste de qcodes pour AZERTY français (KEYB FR sous DOS)
FR = {}
for c in "abcdefghijklmnopqrstuvwxyz":
    k = {"a": "q", "q": "a", "z": "w", "w": "z", "m": "semicolon"}.get(c, c)
    FR[c] = [k]
    FR[c.upper()] = ["shift", k]
for c, k in zip("0123456789", "0123456789"):
    FR[c] = ["shift", k]
FR.update({
    " ": ["spc"], "\n": ["ret"], "\t": ["tab"],
    "&": ["1"], "é": ["2"], '"': ["3"], "'": ["4"], "(": ["5"], "-": ["6"], "è": ["7"],
    "_": ["8"], "ç": ["9"], "à": ["0"], ")": ["minus"], "=": ["equal"], "+": ["shift", "equal"],
    ",": ["m"], "?": ["shift", "m"], ";": ["comma"], ".": ["shift", "comma"],
    ":": ["dot"], "/": ["shift", "dot"], "!": ["slash"], "*": ["backslash"],
    "<": ["less"], ">": ["shift", "less"], "$": ["bracket_right"], "%": ["shift", "apostrophe"],
    "\\": ["alt_r", "8"], "@": ["alt_r", "0"], "#": ["alt_r", "3"], "~": ["alt_r", "2"],
    "{": ["alt_r", "4"], "}": ["alt_r", "equal"], "[": ["alt_r", "5"], "]": ["alt_r", "minus"],
    "|": ["alt_r", "6"],
})


class Qmp:
    def __init__(self, path=QMP):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(path)
        self.f = self.s.makefile("rwb")
        self._read()                       # greeting
        self.cmd("qmp_capabilities")

    def _read(self):
        while True:
            line = self.f.readline()
            if not line:
                raise ConnectionError("QMP fermé")
            msg = json.loads(line)
            if "event" not in msg:
                return msg

    def cmd(self, name, **args):
        req = {"execute": name}
        if args:
            req["arguments"] = args
        self.f.write(json.dumps(req).encode() + b"\n")
        self.f.flush()
        rep = self._read()
        if "error" in rep:
            raise RuntimeError(rep["error"].get("desc"))
        return rep.get("return")

    def hmp(self, line):
        return self.cmd("human-monitor-command", **{"command-line": line})

    def keys(self, qcodes, hold=60):
        self.cmd("send-key", keys=[{"type": "qcode", "data": k} for k in qcodes], **{"hold-time": hold})

    def memory(self, addr, size):
        fd, path = tempfile.mkstemp(prefix="pc486-", suffix=".mem")
        os.close(fd)
        try:
            self.cmd("pmemsave", val=addr, size=size, filename=path)
            with open(path, "rb") as fh:
                return fh.read()
        finally:
            os.unlink(path)

    def screen(self):
        bda = self.memory(0x449, 0x40)
        mode = bda[0]
        cols = bda[0x4A - 0x49] or 80
        rows = (bda[0x84 - 0x49] or 24) + 1
        if mode == 7:
            base = 0xB0000
        else:
            base = 0xB8000
        page = self.memory(base, cols * rows * 2)
        curpos = bda[0x50 - 0x49:0x52 - 0x49]
        lines = []
        for r in range(rows):
            row = page[r * cols * 2:(r + 1) * cols * 2:2]
            lines.append(row.decode("cp850", "replace").replace("\x00", " ").rstrip())
        return lines, (curpos[1], curpos[0]), mode


def type_text(q, text, fr=False, delay=0.05):
    for c in text:
        if fr:
            seq = FR.get(c)
            if seq is None:
                raise SystemExit(f"caractère non géré en AZERTY : {c!r}")
            q.keys(seq)
        else:
            if c not in US:
                raise SystemExit(f"caractère non géré en QWERTY : {c!r}")
            k, sh = US[c]
            q.keys(["shift", k] if sh else [k])
        time.sleep(0.8 if c == "\n" else delay)


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0
    cmd, rest = argv[1], argv[2:]
    q = Qmp()
    if cmd == "screen":
        lines, (r, c), mode = q.screen()
        while lines and not lines[-1]:
            lines.pop()
        print("\n".join(lines))
        print(f"--- mode vidéo {mode}, curseur ligne {r + 1} colonne {c + 1}")
    elif cmd == "type":
        fr = False
        if rest and rest[0] == "--fr":
            fr, rest = True, rest[1:]
        text = " ".join(rest).encode().decode("unicode_escape").encode("latin-1").decode("utf-8")
        type_text(q, text, fr)
    elif cmd == "key":
        for combo in rest:
            q.keys(combo.split("-"))
            time.sleep(0.1)
    elif cmd == "wait":
        pat = re.compile(rest[0])
        timeout = float(rest[1]) if len(rest) > 1 else 60
        t0 = time.time()
        while time.time() - t0 < timeout:
            lines, _, _ = q.screen()
            if pat.search("\n".join(lines)):
                print("\n".join(l for l in lines if l))
                return 0
            time.sleep(0.5)
        print("\n".join(l for l in q.screen()[0] if l))
        print(f"--- délai dépassé ({timeout} s) en attendant {rest[0]!r}", file=sys.stderr)
        return 1
    elif cmd == "floppy":
        if rest[0] == "eject":
            q.hmp("eject -f floppy0")
        else:
            q.hmp(f"change floppy0 {os.path.abspath(rest[0])} raw")
    elif cmd == "shot":
        q.cmd("screendump", filename=os.path.abspath(rest[0]), format="png")
    elif cmd == "hmp":
        print(q.hmp(" ".join(rest)))
    elif cmd == "quit":
        q.cmd("quit")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
