#!/usr/bin/env bash
# Utilitaires DOS du projet (RESEAU.EXE) : compilation seule, aucune écriture sur une image.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
export WATCOM="$(readlink -f "${WATCOM:-$ROOT/tools/watcom}")"
export PATH="$WATCOM/binl64:$WATCOM/binl:$PATH"
export INCLUDE="$WATCOM/h"
B="$HERE/build"
rm -rf "$B"
mkdir -p "$B"
for f in "$HERE"/src/*.[ch]; do
    iconv -f UTF-8 -t CP850 "$f" > "$B/$(basename "$f")"
done
cd "$B"
wcl -q -bt=dos -l=dos -0 -ml -os -wx -we -fe=RESEAU.EXE reseau.c
ls -l RESEAU.EXE
