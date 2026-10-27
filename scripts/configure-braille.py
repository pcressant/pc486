#!/usr/bin/env python3
"""Configure des copies de WIN.INI/SYSTEM.INI pour Baum sur COM3 (IRQ9)."""
import re
import sys
from pathlib import Path


def setting(text, section, key, value):
    newline = '\r\n' if '\r\n' in text else '\n'
    lines = text.splitlines()
    start = next((i for i, line in enumerate(lines)
                  if line.strip().casefold() == f'[{section}]'.casefold()), None)
    if start is None:
        lines.extend(('', f'[{section}]', f'{key}={value}'))
    else:
        end = next((i for i in range(start + 1, len(lines))
                    if lines[i].lstrip().startswith('[')), len(lines))
        matches = [i for i in range(start + 1, end)
                   if re.match(r'\s*' + re.escape(key) + r'\s*=', lines[i], re.I)]
        if matches:
            lines[matches[0]] = f'{key}={value}'
            for i in reversed(matches[1:]):
                del lines[i]
        else:
            lines.insert(end, f'{key}={value}')
    return newline.join(lines) + newline


def configure(win, system):
    for path, entries in ((win, [('epaulard', 'BraillePort', '3'),
                                 ('ports', 'COM3:', '19200,n,8,1')]),
                          (system, [('386Enh', 'COM3Base', '03E8'),
                                    ('386Enh', 'COM3Irq', '9'),
                                    ('386Enh', 'COM3Buffer', '1024')])):
        text = path.read_bytes().decode('cp1252')
        for section, key, value in entries:
            text = setting(text, section, key, value)
        path.write_bytes(text.encode('cp1252'))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit('usage : configure-braille.py WIN.INI SYSTEM.INI (copies hors VM)')
    configure(Path(sys.argv[1]), Path(sys.argv[2]))
