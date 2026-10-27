"""Compilation et exécution des bancs C natifs (tests/win16/*.c).

Les bancs incluent les vraies sources d'Épaulard avec une simulation de l'API Win16.
Compilateur : Open Watcom du projet (tools/watcom), cible Linux 32 bits, le même que
pour Épaulard, donc toujours disponible ; sinon $CC ou cc. Les sources sont d'abord
converties en CP1252, comme par epaulard/build.sh.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def _watcom():
    w = Path(os.environ.get('WATCOM', ROOT / 'tools/watcom'))
    if not w.exists():
        return None
    w = w.resolve()
    if not (w / 'binl64/wcl386').exists() or not (w / 'lib386/linux').exists():
        return None
    return w


def available():
    return _watcom() is not None or shutil.which(os.environ.get('CC', 'cc')) is not None


def build_and_run(bench, timeout=60):
    """Compile tests/win16/<bench> et l'exécute ; renvoie la sortie (échec : exception)."""
    with tempfile.TemporaryDirectory(prefix='pc486-native-') as tmp:
        tmp = Path(tmp)
        # Arborescence en CP1252 : tests/win16 et epaulard/src, chemins relatifs conservés
        for sub in ('tests/win16', 'epaulard/src'):
            dst = tmp / sub
            dst.mkdir(parents=True)
            for f in (ROOT / sub).iterdir():
                if f.suffix in ('.c', '.h'):
                    dst.joinpath(f.name).write_bytes(
                        f.read_text(encoding='utf-8').encode('cp1252'))
        src = tmp / 'tests/win16' / bench
        exe = tmp / 'bench'
        w = _watcom()
        if w:
            env = dict(os.environ, WATCOM=str(w), INCLUDE=str(w / 'lh'),
                       PATH=f"{w / 'binl64'}:{w / 'binl'}:{os.environ.get('PATH', '')}")
            cmd = ['wcl386', '-q', '-bt=linux', '-l=linux', '-wx', '-we', '-wcd=202', '-wcd=303', '-za99',
                   '-DEP_NATIVE_TEST', f'-i={tmp / "tests/win16"}', str(src),
                   f'-fe={exe}', f'-fo={tmp}/']
        else:
            env = os.environ
            cmd = [os.environ.get('CC', 'cc'), '-std=gnu99', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-function', '-DEP_NATIVE_TEST',
                   '-I', str(tmp / 'tests/win16'), str(src), '-o', str(exe)]
        r = subprocess.run(cmd, cwd=tmp, env=env, capture_output=True, text=True,
                           encoding='latin-1')
        if r.returncode:
            raise AssertionError(f'compilation de {bench} :\n{r.stdout}{r.stderr}')
        r = subprocess.run([str(exe)], cwd=tmp, capture_output=True, text=True,
                           encoding='latin-1', timeout=timeout)
        if r.returncode:
            raise AssertionError(f'{bench} a échoué ({r.returncode}) :\n{r.stdout}{r.stderr}')
        return r.stdout
