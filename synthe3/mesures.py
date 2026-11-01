#!/usr/bin/env python3
"""Lire les rapports PIT de S3BASE et S3BENCH sans confondre CPU et temps écoulé.

Outil de développement hôte uniquement ; aucune dépendance Python dans les EXE.
Usage : python3 synthe3/mesures.py BASE.TXT BENCH.TXT [--machine DX4/100]
"""
import argparse
import json
from pathlib import Path
import re
import statistics


def report(path):
    raw = dict(re.findall(r'(\w+)=([^\s]+)', path.read_text()))
    hz = int(raw['hz'])
    if hz <= 0 or raw.get('clock_fault') != '0':
        raise ValueError(f'horloge non qualifiée : {path}')
    if raw['status'] != '0' or raw['underruns'] != '0':
        raise ValueError(f'essai interrompu ou sous-alimenté : {path}')
    ticks = lambda key: int(raw[key])
    ms = lambda key: round(ticks(key)*1000/hz, 3)
    elapsed = ticks('total')
    if not elapsed or ticks('idle') > elapsed:
        raise ValueError(f'compteurs incohérents : {path}')
    return {
        'fichier': str(path), 'brut': raw,
        'mesures_ms': {
            'initialisation': ms('init'), 'phonetisation': ms('phon'),
            'generation_pcm_seule': ms('pcm'),
            'phonetisation_et_pcm': round((ticks('phon')+ticks('pcm'))*1000/hz, 3),
            'lecture_totale': ms('total'), 'producteur_pendant_lecture': ms('source'),
            'attente_et_reveil': ms('idle'), 'dsp_vers_fin_observee': ms('audio'),
            'premier_bloc_disponible': ms('first_buffer'), 'soumission_dsp': ms('submit'),
            'paire_horodatages_min': ms('pair_min'),
        },
        'activite_hors_attente_estimee_pct': round((elapsed-ticks('idle'))*100/elapsed, 2),
        'latence_audible_debut': None, 'latence_audible_arret': None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('optimise', type=Path)
    parser.add_argument('--machine', default='non précisée')
    parser.add_argument('--reference-repeat', type=Path, action='append', default=[])
    parser.add_argument('--optimise-repeat', type=Path, action='append', default=[])
    args = parser.parse_args()
    old, new = report(args.reference), report(args.optimise)
    old_runs = [old] + [report(p) for p in args.reference_repeat]
    new_runs = [new] + [report(p) for p in args.optimise_repeat]
    for key in ('samples', 'hash', 'produced', 'played'):
        if any(old['brut'][key] != run['brut'][key] for run in old_runs+new_runs):
            raise ValueError(f'résultats non comparables : {key}')
    before = old['mesures_ms']['phonetisation_et_pcm']
    after = new['mesures_ms']['phonetisation_et_pcm']
    result = {
        'version': 1, 'machine': args.machine,
        'methode': 'Temps écoulés PIT ; PCM sans fichier ni hash dans la section mesurée. '
                   'Activité estimée = (total - attente STI/HLT)/total, IRQ et mesure non isolées. '
                   'Chaque rapport décrit un essai ; les répétitions sont résumées séparément, '
                   'sans étude statistique générale ni mesure acoustique.',
        'reference': old, 'optimise': new,
        'gain_generation_pct': round((before-after)*100/before, 2),
        'empreintes_pcm_egales': True,
        'preuve_pcm': 'La même empreinte ne remplace pas les comparaisons byte à byte des bancs natifs.',
    }
    if len(old_runs) > 1 or len(new_runs) > 1:
        def summarize(runs):
            times = [r['mesures_ms']['phonetisation_et_pcm'] for r in runs]
            return {'nombre': len(runs), 'generation_ms_mediane': statistics.median(times),
                    'generation_ms_min': min(times), 'generation_ms_max': max(times),
                    'activite_pct_mediane': statistics.median(
                        r['activite_hors_attente_estimee_pct'] for r in runs)}
        result['repetitions'] = {'reference': summarize(old_runs), 'optimise': summarize(new_runs),
                                 'reference_essais': old_runs, 'optimise_essais': new_runs}
        a = result['repetitions']['reference']['generation_ms_mediane']
        b = result['repetitions']['optimise']['generation_ms_mediane']
        result['gain_generation_median_pct'] = round((a-b)*100/a, 2)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
