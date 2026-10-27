#!/usr/bin/env bash
# Compilation seule : aucune installation et aucune écriture sur une image.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
export WATCOM="$(readlink -f "${WATCOM:-$ROOT/tools/watcom}")"
export PATH="$WATCOM/binl64:$WATCOM/binl:$PATH"
export INCLUDE="$WATCOM/h"
B="$HERE/build"
mkdir -p "$B"
for f in "$HERE"/src/*.[ch]; do
    iconv -f UTF-8 -t CP850 "$f" > "$B/$(basename "$f")"
done
cp "$HERE"/src/*.asm "$B/"
# Même table TBFR2007 qu'Épaulard, conversion OEM -> ANSI calculée à la compilation.
python3 - "$ROOT" "$B" <<'PY'
from pathlib import Path
import re, sys
root, build = map(Path, sys.argv[1:])
source = (root / 'epaulard/src/brfr.h').read_text()
table = [int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', source)]
assert len(table) == 256
values = []
for b in range(256):
    try:
        cp = bytes([b]).decode('cp850').encode('cp1252')[0]
        values.append(table[cp])
    except UnicodeError:
        values.append(0xff)
header = source[:source.index('static const')]
(build / 'br850.h').write_text(header + '\n/* Index CP850 ; caractères sans équivalent CP1252 : cellule pleine. */\n'
    + 'static const BYTE br850[256] = {\n'
    + '\n'.join(','.join('0x%02x' % x for x in values[i:i+16]) + ',' for i in range(0,256,16))
    + '\n};\n', encoding='utf-8')
PY
cd "$B"
wasm -q -0 tsr.asm
wasm -q -0 tail.asm
wcc -q -0 -ms -ecc -os -s -zl -wx -we core.c
wcc -q -0 -ms -ecc -os -s -zl -wx -we dialog.c
wcc -q -0 -ms -ecc -os -s -zl -wx -we dos.c
wlink op q format dos com name EPAUDOS.COM file tsr.o,core.o,dialog.o,dos.o,tail.o op map=EPAUDOS.MAP
ls -l EPAUDOS.COM
python3 - <<'PY'
from pathlib import Path
import re
mapping = Path('EPAUDOS.MAP').read_text()
end = int(re.search(r'0000:([0-9a-f]+)\s+_resident_end', mapping).group(1), 16)
assert end < 0xff00, 'Le TSR doit tenir dans un segment avec sa pile.'
print('Mémoire résidente (PSP, données et pile compris) : %d octets.' % ((end+15)//16*16))
PY
