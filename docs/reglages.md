# Réglages d'Épaulard (Inser+E)

Inser+E ouvre un menu parlé. Tant qu'il est ouvert, toutes les touches vont à
Épaulard et aucune n'atteint l'application :

| Touche | Effet |
| --- | --- |
| Haut, Bas (ou Tab) | réglage précédent, suivant |
| Gauche, Droite | changer la valeur ; débit, hauteur, voix et ponctuation s'entendent aussitôt |
| Entrée (ou Inser+E) | enregistrer et fermer |
| Échap | rétablir les valeurs d'avant et fermer |
| Ctrl | silence |
| H | rappel des touches |

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

Les défauts du débit et de la hauteur sont ceux de l'Apollo II : sous Windows,
Épaulard parlait déjà ainsi avant l'existence des réglages. Le volume n'est pas
proposé : Apolo-emu l'ignore (`--no-volume` dans `scripts/pc486`).

Épaulard envoie `@W…@F…@V…@P…` suivi d'un CR au démarrage et à chaque changement.
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
`Majuscules` (0, 1). Une valeur absente ou hors limites garde le défaut. Le fichier
n'est écrit qu'à l'enregistrement (Entrée) ; `build.sh install` n'y touche pas.
