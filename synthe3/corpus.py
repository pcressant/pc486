#!/usr/bin/env python3
"""Corpus de comparaison des variantes du moteur : WAV comparables et mesures PCM.

Outil hôte, bibliothèque standard seulement. Les variantes sont compilées en Linux
32 bits avec l'Open Watcom du projet (mêmes sources que les EXE DOS ; les noyaux
entiers donnent le même PCM qu'en 16 bits, vérifié par les empreintes des bancs).

  python3 synthe3/corpus.py [--dossier work/dev/synthe3/corpus] [--rapide]

Pour chaque cas du corpus et chaque variante, un WAV mono 16 bits à 10 kHz,
même texte, même débit/hauteur, même gain fixe (volume moteur 10). Les mesures
sont numériques : longueur, égalité octet à octet, rapport signal/écart, écart
maximal, nombre d'échantillons écrêtés, crête. Elles ne mesurent pas
l'intelligibilité ; l'écoute des WAV reste nécessaire pour la juger.
"""
import argparse
import array
import json
import math
import os
from pathlib import Path
import random
import subprocess
import wave

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'synthe3'
WATCOM = Path(os.environ.get('WATCOM', ROOT / 'tools/watcom')).resolve()
VARIANTS = {
    # nom : (options du moteur, fichiers)
    'reference': ([], ['src/engine.c', 'src/kernels.c', 'src/weights.c', 'src/main.c']),
    'scalaire': (['-DS3_SCALAR_WINDOW'], ['src/engine.c', 'src/kernels.c', 'src/weights.c', 'src/main.c']),
    'bloc': (['-DS3_BLOCK'], ['src/engine.c', 'src/kernels.c', 'src/weights.c', 'src/block.c', 'src/main.c']),
    'entier': (['-DS3_BLOCK', '-DS3_INTEGER_PERIOD'],
               ['src/engine.c', 'src/kernels.c', 'src/weights.c', 'src/block.c', 'src/main.c']),
    'cache16': (['-DS3_BLOCK', '-DS3_INTEGER_PERIOD', '-DS3_WINDOW_CACHE=16'],
                ['src/engine.c', 'src/kernels.c', 'src/weights.c', 'src/block.c', 'src/main.c']),
}
PHRASES = [
    'Bonjour Michel. Un élève prend un café.',
    'Le prix est de 12,50 francs.',
    'café, élève, fête, où, çà et là.',
    '12,50 francs; C:\\DOS\\CONFIG.SYS !',
    'Bonjour, comment allez-vous ?',
    'Le lecteur d\'écran lit la fenêtre active, les menus et les boîtes de dialogue.',
    'En 1994, Windows pour Workgroups 3.11 tourne sur un 486 à 100 mégahertz.',
    'Appuyez sur Entrée pour continuer, ou sur Échap pour annuler l\'opération.',
    'Les voyelles longues : pâte, fête, côte ; les nasales : an, on, un, in.',
    'Il y a 1 234 567 habitants dans cette région, soit 33,3 pour cent.',
    'AAAAAAAAAAAA', 'zzzzzzzz vvvvvvv jjjjjjj',
]


def cases(quick=False):
    rng = random.Random(486)
    result = [(t, 3, 5) for t in PHRASES]
    result += [('Bonjour, comment allez-vous ?', rate, pitch)
               for rate in (0, 3, 9, 15) for pitch in (0, 5, 8, 15)]
    result += [(PHRASES[0], rate, pitch) for rate, pitch in ((0, 15), (15, 0), (7, 11), (11, 2))]
    result += [('a' * 240, 3, 5)]
    if not quick:
        words = ['bonjour', 'élève', 'Michel', '123', 'café', '.', '?', 'fenêtre', 'tableau', ',']
        result += [(' '.join(rng.choice(words) for _ in range(20)), rng.randrange(16), rng.randrange(16))
                   for _ in range(12)]
    return result


def build(folder, names=None):
    folder.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, WATCOM=str(WATCOM), INCLUDE=str(WATCOM / 'lh'),
               PATH=f'{WATCOM / "binl64"}:{os.environ.get("PATH", "")}')
    for name in names or VARIANTS:
        options, files = VARIANTS[name]
        objects = folder / f'obj-{name}'
        objects.mkdir(exist_ok=True)
        command = ['wcl386', '-q', '-bt=linux', '-ecc', '-j', '-za99', '-wx', '-we', '-wcd=202',
                   '-wcd=303', f'-i={SOURCE / "src"}', f'-fe={folder / name}', f'-fo={objects}/'] + options
        result = subprocess.run(command + [str(SOURCE / f) for f in files], cwd=objects, env=env,
                                capture_output=True)
        if result.returncode:
            raise RuntimeError(result.stdout.decode('latin1') + result.stderr.decode('latin1'))


def render(folder, name, text, rate, pitch, output):
    input_file = output.with_suffix('.txt')
    input_file.write_bytes(text.encode('latin1'))
    subprocess.run([str(folder / name), '-l', '-f', str(input_file), '-o', str(output), '-r', str(rate),
                    '-p', str(pitch)], check=True, capture_output=True, timeout=60)


def samples(path):
    with wave.open(str(path)) as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 10000)
        return array.array('h', wav.readframes(wav.getnframes()))


def stats(pcm):
    peak = max((abs(x) for x in pcm), default=0)
    return {'echantillons': len(pcm), 'duree_ms': round(len(pcm) / 10, 1), 'crete': peak,
            'ecretes': sum(1 for x in pcm if abs(x) >= 32766),
            'rms': round(math.sqrt(sum(x * x for x in pcm) / len(pcm)), 1) if pcm else 0}


def compare(reference, other):
    """Mesures numériques entre deux PCM ; alignées sur la longueur commune."""
    n = min(len(reference), len(other))
    noise = sum((a - b) ** 2 for a, b in zip(reference, other))
    signal = sum(b * b for b in reference[:n])
    result = {
        'identique': reference == other,
        'ecart_echantillons': len(other) - len(reference),
        'ecart_duree_ms': round((len(other) - len(reference)) / 10, 1),
        'ecart_max': max((abs(a - b) for a, b in zip(reference, other)), default=0),
        'snr_db': round(10 * math.log10(signal / noise), 2) if noise and signal else None,
        'premier_ecart': next((i for i, (a, b) in enumerate(zip(reference, other)) if a != b), None),
    }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--dossier', type=Path, default=ROOT / 'work/dev/synthe3/corpus')
    parser.add_argument('--rapide', action='store_true')
    parser.add_argument('--variantes', nargs='+', default=['reference', 'bloc', 'entier'])
    args = parser.parse_args()
    folder = args.dossier
    build(folder / 'natif', args.variantes)
    report = {'variantes': {}, 'cas': []}
    for name in args.variantes:
        report['variantes'][name] = {'options': VARIANTS[name][0]}
    for index, (text, rate, pitch) in enumerate(cases(args.rapide)):
        entry = {'numero': index, 'texte': text, 'debit': rate, 'hauteur': pitch, 'variantes': {}}
        pcm = {}
        for name in args.variantes:
            out = folder / 'wav' / name / f'{index:02d}-r{rate}-p{pitch}.wav'
            out.parent.mkdir(parents=True, exist_ok=True)
            render(folder / 'natif', name, text, rate, pitch, out)
            pcm[name] = samples(out)
            entry['variantes'][name] = {'wav': str(out), **stats(pcm[name])}
        ref = args.variantes[0]
        for name in args.variantes[1:]:
            entry['variantes'][name]['contre_' + ref] = compare(pcm[ref], pcm[name])
        report['cas'].append(entry)
    ref = args.variantes[0]
    summary = {}
    for name in args.variantes[1:]:
        comps = [c['variantes'][name]['contre_' + ref] for c in report['cas']]
        snrs = [c['snr_db'] for c in comps if c['snr_db'] is not None]
        summary[name] = {
            'cas': len(comps), 'identiques': sum(c['identique'] for c in comps),
            'duree_differente': sum(1 for c in comps if c['ecart_echantillons']),
            'ecart_duree_max_ms': max((abs(c['ecart_duree_ms']) for c in comps), default=0),
            'snr_min_db': min(snrs) if snrs else None,
            'ecart_max': max(c['ecart_max'] for c in comps),
            'ecretes_total': sum(c['variantes'][name]['ecretes'] for c in report['cas']),
            'ecretes_total_' + ref: sum(c['variantes'][ref]['ecretes'] for c in report['cas']),
        }
    report['resume'] = summary
    (folder / 'comparaison.json').write_text(json.dumps(report, ensure_ascii=False, indent=1))
    print(json.dumps({'dossier': str(folder), 'resume': summary}, ensure_ascii=False, indent=1))


if __name__ == '__main__':
    main()
