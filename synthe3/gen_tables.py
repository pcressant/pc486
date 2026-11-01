#!/usr/bin/env python3
"""Régénère les petites tables numériques du port, sans lire le dépôt original."""
from pathlib import Path
import math

root = Path(__file__).resolve().parent / 'src'
values = [round((1-math.cos(math.pi*i/256))*16384) for i in range(257)]
(root / 'window.h').write_text(
    '/* SPDX-License-Identifier: GPL-3.0-or-later\n'
    ' * Fenêtre cosinus montante Q15, 257 points, générée par gen_tables.py. */\n'
    'static const unsigned short window_q15[257] = {\n' +
    ''.join('    '+','.join(map(str, values[i:i+12]))+',\n' for i in range(0, 257, 12)) + '};\n')
(root / 'window.inc').write_text(
    '; SPDX-License-Identifier: GPL-3.0-or-later\n'
    '; Même table que window.h, dans le segment de code.\nwindow_q15 label word\n' +
    ''.join('    dw '+','.join(map(str, values[i:i+12]))+'\n' for i in range(0, 257, 12)))
cp850 = []
for byte in range(128, 256):
    try:
        cp850.append(bytes([byte]).decode('cp850').encode('latin1')[0])
    except UnicodeError:
        cp850.append(32)
(root / 'cp850.inc').write_text(''.join(
    '            '+','.join(map(str, cp850[i:i+16]))+',\n' for i in range(0, 128, 16)))
