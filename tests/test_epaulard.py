"""Non-régression d'Épaulard hors de Windows : les vraies sources C (osm.c, epaulard.c,
braille.c) compilées pour Linux avec la simulation Win16 de tests/win16/win16sim.h.

osm-test.c       modèle hors écran : Write, Word (Normal et Page/en-tête), bitmaps
                 chaînés, surbrillance, boîtes de Word, défilement, sélection.
epaulard-test.c  lecteur complet face à une fausse Apollo II et une fausse plage Baum :
                 dire tout de Write (lignes vides, défilement, aucune pause, fin),
                 interruption par Ctrl et Bas, Bloc-notes, icônes du Gestionnaire de
                 programmes (jamais « vide »), liste du Gestionnaire de fichiers
                 (chaque élément dit une fois), menus (Gauche/Droite sans ancien titre),
                 boîte Caractères de Word (onglets, Ctrl+Tab, libellés), boîte
                 Options (page sa_sdm, boutons radio, groupes), réglages
                 Inser+E et EPAULARD.INI, écho par mots et noms des signes, barre
                 d'état, sélection en points 7-8. Calculatrice : valeurs, signes,
                 résultat inchangé, erreurs, afficheur scientifique et braille,
                 mémoire, bases, unités, Inv/Hyp, parcours/activation des boutons,
                 bilan braille et contrôles désactivés.
Run: python3 -m unittest tests.test_epaulard -v
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native


@unittest.skipUnless(native.available(), 'ni Open Watcom (tools/watcom) ni compilateur C')
class EpaulardBenchTests(unittest.TestCase):
    def test_offscreen_model(self):
        self.assertIn('tous les scénarios réussis', native.build_and_run('osm-test.c'))

    def test_screen_reader_scenarios(self):
        self.assertIn('tous les scénarios réussis',
                      native.build_and_run('epaulard-test.c', timeout=300))


if __name__ == '__main__':
    unittest.main()
