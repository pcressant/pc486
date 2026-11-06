# Réglages d'Épaulard (Inser+E)

Inser+N (comme le menu de NVDA) ou Inser+E ouvre un menu parlé. Tant qu'il est ouvert, toutes les touches vont à
Épaulard et aucune n'atteint l'application :

| Touche | Effet |
| --- | --- |
| Haut, Bas (ou Tab) | réglage précédent, suivant |
| Gauche, Droite | changer la valeur ; débit, hauteur, voix, ponctuation, volume et synthèse s'entendent aussitôt |
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
| Suivi de la souris | désactivé, ligne, mot | ligne | — |
| Sons de la souris | désactivés, activés | activés | — |
| Synthèse | comme Épaudos, Synthé3, Apollo | comme Épaudos | — |

Les défauts du débit et de la hauteur sont ceux de l'Apollo II, et les mêmes
qu'Épaudos : un même débit donne la même vitesse sous DOS et sous Windows (avec
Synthé3, les deux lecteurs passent par la même table, `rates` de `synthe3/src/ap.c` :
débit 1 = débit naturel de Synthé3, 5 = deux fois plus vite, 9 = quatre fois).

**Synthèse.** « Comme Épaudos » (défaut) : Synthé3 si S3RES a été chargé avant
Windows, c'est-à-dire sauf `Synthese=Apollo` dans `EPAUDOS.INI`, sinon l'Apollo II.
« Synthé3 » : Synthé3 local (`EPAUS3.DLL`, pilote audio de Windows), même sans S3RES.
« Apollo » : Apollo II sur COM1. Le changement est immédiat : la valeur est dite par
la nouvelle synthèse (« comme Épaudos, Synthé3 »), Échap revient à l'ancienne. Si
Synthé3 ne peut pas s'ouvrir, Épaulard reste sur COM1 et dit « Synthé3 indisponible,
Apollo ». La parole de l'invite MS-DOS (Épaudos relayé) suit la synthèse d'Épaulard. Le volume agit sur
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
- **Souris.** Texte sous le pointeur quand la souris bouge (ligne ou mot) et bip
  dont la hauteur suit le pointeur ; Inser+M et Inser+Maj+M les basculent hors du
  menu. Voir [la revue et la souris](revue-pave.md#suivi-de-la-souris).

## Fichier

`EPAULARD.INI`, dans le répertoire d'`EPAULARD.EXE` (`C:\EPAULARD`), section
`[Parole]` : `Debit`, `Hauteur`, `Voix`, `Ponctuation` (0, 1, 2), `Echo` (0 à 3),
`Majuscules` (0, 1), `Volume` (1 à 8, défaut 4 : `@A0` à `@AF` de l'Apollo II,
appliqués aussi par Synthé3, 3 dB par niveau). Une valeur absente ou hors limites
garde le défaut.
`Synthese` (`Auto`, `Synthe3`, `Apollo` : réglage Synthèse ci-dessus ; `Auto` si
absent). Si Synthé3 est demandé au démarrage mais indisponible, Épaulard parle sur
COM1 et annonce « Synthé3 indisponible, erreur N » (2 : DLL absente ; 100 : DLL d'une autre version ; 1000 +
erreur MMSYSTEM, par exemple 1006 sans pilote audio ; 2000 + n : mémoire). Le fichier
n'est écrit qu'à l'enregistrement (Entrée) ; `build.sh install` n'y touche pas.
Section `[Souris]` : `Suivi` (0 désactivé, 1 ligne, 2 mot ; défaut 1) et `Sons`
(0, 1 ; défaut 1).
