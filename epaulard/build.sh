#!/usr/bin/env bash
# Compile Épaulard (Windows 3.1x, 16 bits) avec Open Watcom (tools/watcom).
#   epaulard/build.sh            compile dans epaulard/build/
#   epaulard/build.sh install    compile puis copie dans C:\EPAULARD de vm/win311.img
#   epaulard/build.sh install --braille active aussi COM3 Baum (IRQ9)
#                                (VM arrêtée) et le lance au démarrage de Windows
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
ROOT="$(dirname "$HERE")"
export WATCOM="$(readlink -f "${WATCOM:-$ROOT/tools/watcom}")"
export PATH="$WATCOM/binl64:$WATCOM/binl:$PATH"
export INCLUDE="$WATCOM/h:$WATCOM/h/win"
# Le verrou est acquis avant compilation et conservé jusqu’à la dernière écriture.
if [[ "${1:-}" == install && "${PC486_INSTALL_LOCKED:-}" != 1 ]]; then
    exec python3 "$ROOT/scripts/with-disk-lock.py" "$ROOT/vm/win311.img" -- \
        env PC486_INSTALL_LOCKED=1 bash "$HERE/build.sh" "$@"
fi
B="$HERE/build"
rm -rf "$B"; mkdir -p "$B"

# Sources en UTF-8 -> CP1252 (jeu de caractères ANSI de Windows 3.1 en français)
for f in "$HERE"/src/*.[ch]; do iconv -f utf-8 -t cp1252 "$f" >"$B/$(basename "$f")"; done
cd "$B"

CFLAGS=(-q -bt=windows -ml -zw -zu -os -s -wx -we -DWINVER=0x030a -DSTRICT)
wcc "${CFLAGS[@]}" -bd hook.c
wcc "${CFLAGS[@]}" -bd osm.c
wcc "${CFLAGS[@]}" -bd braille.c
wlink op q system windows_dll name EPAUHOOK.DLL file hook.o,osm.o,braille.o \
    op implib=EPAUHOOK.LIB op map=EPAUHOOK.MAP \
    segment type code fixed
wcc "${CFLAGS[@]}" epaulard.c
wlink op q system windows name EPAULARD.EXE file epaulard.o library EPAUHOOK.LIB \
    op stack=8k op heapsize=4k op map=EPAULARD.MAP
# Aide de scripts/installer (--word) : jamais copiée par « install » ci-dessous.
wcc "${CFLAGS[@]}" instauto.c
wlink op q system windows name INSTAUTO.EXE file instauto.o library toolhelp.lib \
    op stack=8k op heapsize=1k
ls -l EPAULARD.EXE EPAUHOOK.DLL INSTAUTO.EXE

[[ "${1:-}" == install ]] || exit 0
DISK="$ROOT/vm/win311.img"
IMG="$DISK@@32256"
# Conserver l'image arrêtée avant toute écriture (y compris WIN.INI).
mkdir -p "$ROOT/work/sauvegardes"
BACKUP=$(mktemp "$ROOT/work/sauvegardes/win311-avant-install-$(date +%Y%m%d-%H%M%S).XXXXXX.img")
cp --reflink=auto --sparse=always "$DISK" "$BACKUP"
mmd -i "$IMG" ::/EPAULARD 2>/dev/null || true
mcopy -o -i "$IMG" EPAULARD.EXE EPAUHOOK.DLL ::/EPAULARD/
# WIN.INI : load=C:\EPAULARD\EPAULARD.EXE (section [windows])
mcopy -o -i "$IMG" ::/WINDOWS/WIN.INI WIN.INI
python3 -I "$ROOT/scripts/configure-epaulard.py" WIN.INI

if [[ "${2:-}" == --braille ]]; then
    mcopy -o -i "$IMG" ::/WINDOWS/SYSTEM.INI SYSTEM.INI
    python3 -I "$ROOT/scripts/configure-braille.py" WIN.INI SYSTEM.INI
    mcopy -o -i "$IMG" SYSTEM.INI ::/WINDOWS/SYSTEM.INI
fi
mcopy -o -i "$IMG" WIN.INI ::/WINDOWS/WIN.INI
echo "installé dans C:\\EPAULARD"
