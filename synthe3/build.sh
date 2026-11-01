#!/usr/bin/env bash
# Compilation autonome, sans installer ni accéder à une image disque.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
export WATCOM="$(readlink -f "${WATCOM:-$ROOT/tools/watcom}")"
export PATH="$WATCOM/binl64:$WATCOM/binl:$PATH"
export INCLUDE="$WATCOM/h"
B="$ROOT/work/dev/synthe3/build"
mkdir -p "$B/src" "$B/vendor"
for f in "$HERE"/src/*.[ch]; do
    iconv -f UTF-8 -t CP850 "$f" > "$B/src/$(basename "$f")"
done
cp "$HERE"/src/*.asm "$HERE"/src/*.inc "$B/src/"
cp "$HERE"/vendor/voice_data.h "$B/vendor/"
cd "$B/src"
FLAGS=(-q -bt=dos -ml -4 -fp3 -fpi87 -os -za99 -ecc -j -zt=1024 -wx -we)
wasm -q kernels.asm
wasm -q block.asm
wasm -q sbirq.asm
wasm -q s3time.asm
wasm -q "$HERE/tests/end.asm" -fo=end.o
wlink op q format dos com name ../S3END.COM file end.o
for m in engine main play kernels sb weights; do wcc "${FLAGS[@]}" -DS3_ASM_WEIGHTED "$m.c" -fo="$m-c.o"; done
wcc "${FLAGS[@]}" -DS3_SCALAR_WINDOW engine.c -fo=engine-scalar.o
wcc "${FLAGS[@]}" -DS3_ASM_WEIGHTED -DS3_PROFILE engine.c -fo=engine-prof.o
wcc "${FLAGS[@]}" -DS3_ASM_WEIGHTED -DS3_BLOCK engine.c -fo=engine-blk.o
wcc "${FLAGS[@]}" block.c -fo=block-c.o
# Variantes sans x87 : arithmétique par période entière, compilées sans -fpi87.
INTFLAGS=(-q -bt=dos -ml -4 -fpc -os -za99 -ecc -j -zt=1024 -wx -we)
wcc "${INTFLAGS[@]}" -DS3_ASM_WEIGHTED -DS3_BLOCK -DS3_INTEGER_PERIOD engine.c -fo=engine-int.o
wcc "${INTFLAGS[@]}" -DS3_ASM_WEIGHTED -DS3_INTEGER_PERIOD engine.c -fo=engine-intw.o
wcc "${INTFLAGS[@]}" -DS3_ASM_WEIGHTED -DS3_BLOCK -DS3_INTEGER_PERIOD -DS3_WINDOW_CACHE=16 engine.c -fo=engine-intc.o
# Sonde de bruit de disposition : même source entière, options flottantes de la référence.
wcc "${FLAGS[@]}" -DS3_ASM_WEIGHTED -DS3_INTEGER_PERIOD engine.c -fo=engine-intw2.o
wcc "${INTFLAGS[@]}" -DS3_ASM_WEIGHTED -DS3_BLOCK -DS3_INTEGER_PERIOD -DS3_PROFILE engine.c -fo=engine-profi.o
for o in engine-int.o engine-intw.o engine-intc.o engine-profi.o; do
    if wdis "$o" | grep -qP '\t(f[a-z0-9]+)\t'; then
        echo "$o : instruction x87 trouvée" >&2; exit 1
    fi
done
wcc "${FLAGS[@]}" "$HERE/tests/block-check.c" -i=. -fo=block-check.o
wcc "${FLAGS[@]}" "$HERE/tests/check.c" -i=. -fo=check.o
wcc "${FLAGS[@]}" "$HERE/tests/starve.c" -i=. -fo=starve.o
wcc "${FLAGS[@]}" "$HERE/tests/clock.c" -i=. -fo=clock.o
wcc "${FLAGS[@]}" "$HERE/tests/bench.c" -i=. -fo=bench.o
wcc "${FLAGS[@]}" "$HERE/tests/weights-check.c" -i=. -fo=weights-check.o
wcl -q -bt=dos -ml -4 -fpi87 -ecc -fe=../S3DOS.EXE -k8192 engine-c.o main-c.o sb-c.o kernels.o sbirq.o s3time.o weights-c.o -fm=../S3DOS.MAP
wcl -q -bt=dos -ml -4 -ecc -fe=../S3PLAY.EXE play-c.o sb-c.o kernels.o sbirq.o s3time.o
wcl -q -bt=dos -ml -4 -ecc -fe=../CHECKASM.EXE check.o kernels.o
wcl -q -bt=dos -ml -4 -ecc -fe=../CHECKC.EXE check.o kernels-c.o
wcl -q -bt=dos -ml -4 -ecc -fe=../S3FAULT.EXE starve.o sb-c.o kernels.o sbirq.o s3time.o
wcl -q -bt=dos -ml -4 -ecc -fe=../S3CLOCK.EXE clock.o s3time.o
wcl -q -bt=dos -ml -4 -ecc -fe=../S3WEIGH.EXE weights-check.o kernels.o weights-c.o
wcl -q -bt=dos -ml -4 -fpi87 -ecc -k8192 -fe=../S3BENCH.EXE engine-c.o bench.o sb-c.o kernels.o sbirq.o s3time.o weights-c.o
wcl -q -bt=dos -ml -4 -fpi87 -ecc -k8192 -fe=../S3BASE.EXE engine-scalar.o bench.o sb-c.o kernels.o sbirq.o s3time.o weights-c.o
wcc "${FLAGS[@]}" -DS3_PROFILE "$HERE/tests/bench.c" -i=. -fo=bench-prof.o
wcl -q -bt=dos -ml -4 -fpi87 -ecc -k8192 -fe=../S3PROF.EXE engine-prof.o bench-prof.o sb-c.o kernels.o sbirq.o s3time.o weights-c.o
# Noyaux par période : même banc, C portable (S3BLKC) puis assembleur (S3BLKA).
wcl -q -bt=dos -ml -4 -fpi87 -ecc -k8192 -fe=../S3BLKC.EXE engine-blk.o bench.o sb-c.o kernels.o block-c.o sbirq.o s3time.o weights-c.o
wcl -q -bt=dos -ml -4 -fpi87 -ecc -k8192 -fe=../S3BLKA.EXE engine-blk.o bench.o sb-c.o kernels.o block.o sbirq.o s3time.o weights-c.o
wcl -q -bt=dos -ml -4 -ecc -fe=../CHKBLKC.EXE block-check.o kernels.o block-c.o weights-c.o
wcl -q -bt=dos -ml -4 -ecc -fe=../CHKBLKA.EXE block-check.o kernels.o block.o weights-c.o
# Sans x87 : S3INTA (bloc assembleur + période entière), S3INTW (période entière seule),
# S3DOSX (programme autonome équivalent à S3DOS, exécutable sur 486SX/386).
for m in main bench; do
    src="$m.c"; [ "$m" = bench ] && src="$HERE/tests/bench.c"
    wcc "${INTFLAGS[@]}" "$src" -i=. -fo="$m-nofp.o"
done
wcl -q -bt=dos -ml -4 -fpc -ecc -k8192 -fe=../S3INTA.EXE engine-int.o bench-nofp.o sb-c.o kernels.o block.o sbirq.o s3time.o weights-c.o -fm=../S3INTA.MAP
wcl -q -bt=dos -ml -4 -fpc -ecc -k8192 -fe=../S3INTW.EXE engine-intw.o bench-nofp.o sb-c.o kernels.o sbirq.o s3time.o weights-c.o
wcl -q -bt=dos -ml -4 -fpi87 -ecc -k8192 -fe=../S3INTW2.EXE engine-intw2.o bench.o sb-c.o kernels.o sbirq.o s3time.o weights-c.o
wcl -q -bt=dos -ml -4 -fpc -ecc -k8192 -fe=../S3INTAC.EXE engine-intc.o bench-nofp.o sb-c.o kernels.o block.o sbirq.o s3time.o weights-c.o
wcc "${INTFLAGS[@]}" -DS3_PROFILE "$HERE/tests/bench.c" -i=. -fo=bench-profi.o
wcl -q -bt=dos -ml -4 -fpc -ecc -k8192 -fe=../S3PROFI.EXE engine-profi.o bench-profi.o sb-c.o kernels.o block.o sbirq.o s3time.o weights-c.o
wcl -q -bt=dos -ml -4 -fpc -ecc -k8192 -fe=../S3DOSX.EXE engine-int.o main-nofp.o sb-c.o kernels.o block.o sbirq.o s3time.o weights-c.o -fm=../S3DOSX.MAP
for map in ../S3INTA.MAP ../S3DOSX.MAP; do
    if grep -qiE '__fltused|emu387|__init_387|_8087' "$map"; then echo "$map : bibliothèque flottante liée" >&2; exit 1; fi
done
# Production pour les lecteurs d'écran (README, « Variante retenue ») : noyaux par période
# en assembleur, période entière sans x87, quatre fenêtres (pas de cache LRU), sans
# contrôle de pile (-s) : la pompe résidente tourne sur sa propre pile.
RESFLAGS=(-q -bt=dos -ml -4 -fpc -os -s -za99 -ecc -j -wx -we)
wasm -q s3res.asm -fo=s3resa.o
wcc "${RESFLAGS[@]}" -zt=1024 -DS3_ASM_WEIGHTED -DS3_BLOCK -DS3_INTEGER_PERIOD engine.c -fo=engine-res.o
wcc "${RESFLAGS[@]}" -DS3_ASM_WEIGHTED weights.c -fo=weights-res.o
wcc "${RESFLAGS[@]}" ap.c -fo=ap-res.o
wcc "${RESFLAGS[@]}" s3res.c -fo=s3res.o
for o in engine-res.o weights-res.o ap-res.o s3res.o; do
    if wdis "$o" | grep -qP '\t(f[a-z0-9]+)\t'; then
        echo "$o : instruction x87 trouvée" >&2; exit 1
    fi
done
wlink op q system dos name ../S3RES.EXE op stack=1024 op map=../S3RES.MAP \
    file s3res.o,ap-res.o,engine-res.o,weights-res.o,kernels.o,block.o,s3time.o,s3resa.o
if grep -qiE '__fltused|emu387|__init_387|_8087' ../S3RES.MAP; then echo "S3RES.MAP : bibliothèque flottante liée" >&2; exit 1; fi
# S3PROBE.EXE : mesures du résident intégré (client de son API, comme Épaudos).
wcc "${RESFLAGS[@]}" "$HERE/tests/s3probe.c" -fo=s3probe.o
wlink op q system dos name ../S3PROBE.EXE file s3probe.o,s3time.o
# EPAUS3.DLL : même moteur et même Apollo local pour Épaulard, sortie waveOut (Win16,
# 386 requis par les noyaux). DS de la DLL distinct de la pile de l'appelant (-zu).
WINFLAGS=(-q -bt=windows -ml -4 -fpc -os -s -za99 -ecc -j -zu -bd -wx -we -DWINVER=0x030a)
WINC=("${WINFLAGS[@]}" -i="$WATCOM/h/win")
wcc "${WINC[@]}" -zt=1024 -DS3_ASM_WEIGHTED -DS3_BLOCK -DS3_INTEGER_PERIOD engine.c -fo=engine-win.o
wcc "${WINC[@]}" -DS3_ASM_WEIGHTED weights.c -fo=weights-win.o
wcc "${WINC[@]}" ap.c -fo=ap-win.o
wcc "${WINC[@]}" -zw s3win.c -fo=s3win.o
for o in engine-win.o weights-win.o ap-win.o s3win.o; do
    if wdis "$o" | grep -qP '\t(f[a-z0-9]+)\t'; then
        echo "$o : instruction x87 trouvée" >&2; exit 1
    fi
done
wlink op q system windows_dll name ../EPAUS3.DLL op map=../EPAUS3.MAP \
    file s3win.o,ap-win.o,engine-win.o,weights-win.o,kernels.o,block.o library mmsystem.lib
if grep -qiE '__fltused|emu387|__init_387|_8087' ../EPAUS3.MAP; then echo "EPAUS3.MAP : bibliothèque flottante liée" >&2; exit 1; fi
# S3WPROBE.EXE : EPAUS3.DLL pompée comme par Épaulard, pour les essais sous 86Box.
wcc -q -bt=windows -ml -zw -zu -os -s -wx -we -DWINVER=0x030a -i="$WATCOM/h/win" \
    "$HERE/tests/s3wprobe.c" -fo=s3wprobe.o
wlink op q system windows name ../S3WPROBE.EXE file s3wprobe.o
ls -l "$B/S3DOS.EXE" "$B/S3PLAY.EXE" "$B/S3RES.EXE" "$B/S3PROBE.EXE" "$B/EPAUS3.DLL"
