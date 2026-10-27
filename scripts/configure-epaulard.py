#!/usr/bin/env python3
"""Ajoute Épaulard au load de [windows], sans remplacer les autres programmes."""
from pathlib import Path
import re
import sys

PROGRAM = r'C:\EPAULARD\EPAULARD.EXE'


def same_program(token, program):
    return token.strip('"').replace('/', '\\').casefold() == program.casefold()


def edit_load(text, add=None, remove=()):
    """load= de [windows] : retire les programmes de remove, ajoute add une seule fois,
    conserve les autres programmes, leur ordre et le reste du fichier. Les clés load
    répétées sont fusionnées. Sans rien à ajouter, une section ou clé absente le reste."""
    newline = '\r\n' if '\r\n' in text else '\n'
    lines = text.splitlines()
    start = next((i for i, line in enumerate(lines)
                  if line.strip().lower() == '[windows]'), None)
    if start is None:
        if not add:
            return text
        return newline.join(lines + ['', '[windows]', 'load=' + add]) + newline
    end = next((i for i in range(start + 1, len(lines))
                if lines[i].lstrip().startswith('[')), len(lines))
    matches = [i for i in range(start + 1, end)
               if re.match(r'\s*load\s*=', lines[i], re.I)]
    if not matches and not add:
        return text
    programs = []
    found = False
    for i in matches:
        value = lines[i].split('=', 1)[1].strip()
        for token in re.findall(r'"[^"]*"|[^\s,]+', value):
            if any(same_program(token, r) for r in remove):
                continue
            if add and same_program(token, add):
                if found:
                    continue
                found = True
            programs.append(token)
    if add and not found:
        programs.append(add)
    line = 'load=' + ' '.join(programs)
    if matches:
        lines[matches[0]] = line
        for i in reversed(matches[1:]):
            del lines[i]
    else:
        lines.insert(end, line)
    return newline.join(lines) + newline


def configure(text):
    """Épaulard dans load=, sans remplacer les autres programmes."""
    return edit_load(text, add=PROGRAM)


if __name__ == '__main__':
    path = Path(sys.argv[1])
    path.write_bytes(configure(path.read_bytes().decode('cp1252')).encode('cp1252'))
