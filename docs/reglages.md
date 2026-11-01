# Réglages d'Épaulard (Inser+E)

Inser+N (comme le menu de NVDA) ou Inser+E ouvre un menu parlé. Tant qu'il est ouvert, toutes les touches vont à
Épaulard et aucune n'atteint l'application :

| Touche | Effet |
| --- | --- |
| Haut, Bas (ou Tab) | réglage précédent, suivant |
| Gauche, Droite | changer la valeur ; débit, hauteur, voix et ponctuation s'entendent aussitôt |
| Entrée (ou Inser+E) | enregistrer et fermer |
| Échap | rétablir les valeurs d'avant et fermer |
| Ctrl | silence |
| H | rappel des touches |

Hors du menu, Inser+2 et Inser+3 basculent l'écho des caractères et des mots,
Inser+P passe au niveau de ponctuation suivant, comme NVDA ; ces changements ne
sont enregistrés qu'à la prochaine fermeture du menu par Entrée.

Sans touche pendant une minute, le menu se ferme comme par Échap. La DLL rend aussi
le clavier d'elle-même au bout de 70 s, ou si EPAULARD.EXE a disparu.

## Réglages

| Réglage | Valeurs | Défaut | Envoi à l'Apollo II |
| --- | --- | --- | --- |
| Débit | 1 à 9 | 3 | `@W` |
| Hauteur | 0 à 15 | 8 | `@F` (hexadécimal) |
| Voix | 1 à 6 | 1 | `@V` |
| Ponctuation | aucune, certaine, toute | certaine | `@P0`, `@P0`, `@P1` |
| Écho du clavier | caractères, mots, caractères et mots, aucun | caractères | — |
| Majuscules | ne rien dire, dire majuscule | ne rien dire | — |
| Volume | 1 à 8 | 4 | `@A0 1 2 3 5 7 A F` |

Les défauts du débit et de la hauteur sont ceux de l'Apollo II : sous Windows,
Épaulard parlait déjà ainsi avant l'existence des réglages. Le volume agit sur
l'Apollo II et sur Synthé3 ; Apolo-emu l'ignore (`--no-volume` dans `scripts/pc486`).

Épaulard envoie `@W…@F…@V…@P…@A…` suivi d'un CR au démarrage et à chaque changement.
Le CR clôt une proposition vide ; le `.` de Sonolect serait prononcé avec `@P1`.

- **Ponctuation.** « Aucune » : le texte tel quel. « Certaine » : Épaulard nomme
  `@ # $ % & * + / \ = < > | ~ _ ^` (chemins, adresses, raccourcis comme « Ctrl plus
  N »), mais ni la ponctuation des phrases ni les crochets. « Toute » : l'Apollo II
  dit toute la ponctuation (`@P1`) ; « @ », qui ouvrirait une commande, est dit
  « arobase » par Épaulard. Quel que soit le réglage, un caractère dit seul (flèches,
  revue, écho) est nommé : « point », « virgule », « tiret »… Quatre signes
  identiques ou plus (`______`, `*****`) forment un trait de séparation, jamais dit.
- **Écho par mots.** Le mot est dit quand un espace, un signe ou Entrée le termine ;
  Retour arrière le raccourcit ; un déplacement ou un changement de focus l'oublie.
  Avec « mots » seuls, ni les lettres ni les espaces ne sont dits.
- **Majuscules.** « Majuscule B » pour une lettre capitale dite seule.

## Fichier

`EPAULARD.INI`, dans le répertoire d'`EPAULARD.EXE` (`C:\EPAULARD`), section
`[Parole]` : `Debit`, `Hauteur`, `Voix`, `Ponctuation` (0, 1, 2), `Echo` (0 à 3),
`Majuscules` (0, 1), `Volume` (1 à 8, défaut 4 : `@A0` à `@AF` de l'Apollo II,
appliqués aussi par Synthé3, 3 dB par niveau). Une valeur absente ou hors limites
garde le défaut.
`Synthese` (lu au démarrage d'Épaulard, absent du menu) : `Auto` par défaut, Synthé3
local (`EPAUS3.DLL`, pilote audio de Windows) si `S3RES` a été chargé avant
Windows, c'est-à-dire si `EPAUDOS.INI` contient `Synthese=Synthe3` (choix fait dans
Épaudos), sinon Apollo II sur COM1 ; `Apollo` ou `Synthe3` forcent le choix. Si
Synthé3 est demandé mais indisponible, Épaulard parle sur COM1 et annonce « Synthé3
indisponible, erreur N » (2 : DLL absente ; 100 : DLL d'une autre version ; 1000 +
erreur MMSYSTEM, par exemple 1006 sans pilote audio ; 2000 + n : mémoire). Le fichier
n'est écrit qu'à l'enregistrement (Entrée) ; `build.sh install` n'y touche pas.
