# SPDX-License-Identifier: GPL-3.0-or-later
"""Tampon de la page d'essai du mode navigation (http://pc486/essai), tel que le proxy
le donne à Épaulard, en en-tête C pour le banc natif (tests/win16/navweb-essai.h).

    python3 tests/navweb_essai.py        régénère l'en-tête après un changement du
                                         proxy, de html32.py, de tampon.py ou d'essai.html

tests/test_navigation.py vérifie que l'en-tête suit le proxy. La page d'accueil
(http://pc486/) y est un lien visité, http://pc486/jamais-visite ne l'est pas.
"""
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'tests/win16/navweb-essai.h'


def tampon_bytes():
    sys.path.insert(0, str(ROOT / 'scripts'))
    os.environ.setdefault('PC486_CHROMIUM', '/nonexistent')
    from reseau import proxy, tampon
    p = proxy.Proxy()
    p.handle('GET', 'http://pc486/essai', None, None)
    p.documents.visit('http://pc486/')
    return p.documents.tampon('http://pc486/essai')


def header():
    data = tampon_bytes()
    out = ['/* Généré par tests/navweb_essai.py : tampon de http://pc486/essai (proxy). */',
           'static const char nav_essai[] =']
    line = ''
    for b in data:
        c = chr(b)
        if c == '\\' or c == '"':
            piece = '\\' + c
        elif 32 <= b < 127:
            piece = c
        else:
            piece = f'\\{b:03o}'
        line += piece
        if c == '\n' or len(line) > 70:
            out.append(f'    "{line}"')
            line = ''
    if line:
        out.append(f'    "{line}"')
    out[-1] += ';'
    out.append(f'#define NAV_ESSAI_LEN {len(data)}')
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    HEADER.write_text(header(), encoding='utf-8')
    print(f'{HEADER.relative_to(ROOT)} écrit')
