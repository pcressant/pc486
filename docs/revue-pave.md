# Revue au pavé numérique

Ce guide concerne Épaulard sous Windows. Le [manuel d’utilisation](manuel-lecteurs.md)
présente aussi les commandes d’Épaudos sous DOS et les différences entre les lecteurs.

Éteindre Verr Num. La touche Inser peut être celle du clavier principal ou le 0
du pavé. Le signe moins utilisé ci-dessous est celui du pavé.

| Unité | Précédent | Courant | Suivant |
|---|---|---|---|
| Ligne | 7 | 8 | 9 |
| Mot | 4 | 5 | 6 |
| Caractère | 1 | 2 | 3 |

- **Inser + 7** ou **Inser + 1** du pavé : changer de mode (revue à plat ou
  normale), annoncé à voix haute ; ce sont les modes de revue de NVDA.
- **Inser + moins** ou **Inser + 5** du pavé : recentrer et relire la ligne, sans
  changer de mode.
- **Plus du pavé** : dire tout, comme Inser + Bas.
- **Ctrl seul** : couper la parole, comme auparavant.
- **Inser + H** : aide des commandes, y compris le pavé.

## Revue normale

Mode initial. Chaque commande part du curseur de saisie ou du contrôle qui a le
focus. Elle lit autour de cette position sans déplacer le curseur ni modifier
la sélection. Deux appuis sur 9 relisent donc la même ligne suivante ; avancer
dans l'application avec ses flèches habituelles, ou passer en revue à plat pour
parcourir indépendamment le texte.

Les contrôles EDIT, comme le Bloc-notes, fournissent directement leur texte et
leur position. Les boutons et autres contrôles standard fournissent leur
description. Write utilise le modèle du texte dessiné.

## Revue à plat

Le curseur de lecture parcourt le texte visible de la fenêtre active. Des appuis
successifs sur 9 avancent de ligne en ligne, sur 6 de mot en mot, sur 3 de
caractère en caractère. La lecture peut passer à la ligne voisine à ses limites.
Une commande de ligne précédente/suivante remet la colonne au début de la ligne.

Le curseur de saisie, le focus et la sélection restent en place. Les flèches de
l'application peuvent continuer à les déplacer ; Inser+moins permet ensuite de
recentrer la revue. Une nouvelle fenêtre réinitialise le point de lecture. Le
texte est relu dans le modèle à chaque commande, et non conservé dans une copie
figée du document.

Sur la plage braille, la ligne affichée est celle du point de revue. Les touches
ligne précédente/suivante de la plage déplacent aussi ce point. Un curseur
routine y place la colonne de revue et dit le caractère ; deux appuis sur le même
curseur routine en moins d'une seconde font un clic gauche sur ce caractère.

## Souris

Comme le clavier de bureau de NVDA, Verr Num éteint :

| Touche | Action |
|---|---|
| / du pavé | clic gauche à la position du pointeur |
| * du pavé | clic droit |
| Maj + / ou Maj + * | verrouiller le bouton gauche ou droit ; même touche pour le relâcher |
| Inser + / | amener le pointeur au point de revue |
| Inser + * | lire la rangée sous le pointeur ; en revue à plat, y placer la revue |
| Inser + M, Inser + Maj + M | suivi de la souris, sons de la souris (ci-dessous) |

Le point de revue est le caractère courant de la revue à plat ; en revue normale,
le curseur de saisie, l'élément d'une boîte de Word ou le milieu du contrôle qui a
le focus. Pour cliquer sur un élément affiché, passer en revue à plat (Inser +
7 du pavé), s'y rendre au pavé, puis Inser + / et / (ou * pour le menu contextuel).
Un bouton verrouillé permet de glisser : verrouiller, déplacer le pointeur par la
revue, puis relâcher.

Les clics passent par le point d'entrée `MOUSE_EVENT` de USER, comme ceux du
pilote de souris : Windows les traite comme un vrai clic (activation, capture,
menus). Le pointeur est placé par `SetCursorPos`.

### Suivi de la souris

Comme NVDA, Épaulard suit le pointeur quand l'utilisateur déplace la souris
(`epaulard/src/souris.c`) :

| Touche | Action |
| --- | --- |
| Inser + M | suivi de la souris activé ou désactivé (comme NVDA+M) |
| Inser + Maj + M | sons de la souris activés ou désactivés |

- **Lecture.** Sous le pointeur, un bouton, une case, une barre de défilement, une
  liste déroulante ou une zone de saisie d'une ligne est dit en entier (libellé,
  rôle, état, « grisé » compris), une seule fois tant que le pointeur y reste. Ailleurs
  (documents, listes, textes dessinés), la rangée de texte de la fenêtre sous le
  pointeur, relevée par le modèle hors écran, est dite quand elle change ; avec le
  réglage « mot », seulement le mot sous le pointeur. Un blanc ne dit rien. Chaque
  annonce coupe la précédente. Rien n'est lu dans un menu ouvert (Windows annonce
  l'élément survolé), pendant un dire tout ni dans une invite MS-DOS relayée.
- **Sons.** À chaque déplacement, un bip de 40 ms dont la hauteur suit l'ordonnée du
  pointeur : 880 Hz en haut de l'écran, 220 Hz en bas (comme les « coordonnées
  audio » de NVDA). Le pilote Sound Blaster 1.5 de Windows 3.1 étant mono, la
  position horizontale ne s'entend pas.
- Les déplacements faits par Épaulard (Inser + /, clic de routage braille) ne sont
  ni lus ni sonnés.

La scrutation compare la position du pointeur toutes les 100 ms : Windows 3.1 ne
signale pas les déplacements à un autre programme. Les bips passent par
`EPAUS3.DLL` (`S3Tone`), seule à tenir la Sound Blaster pendant la parole de
Synthé3 : elle mêle le bip au tampon suivant, ou le joue seul dans un tampon de
2048 échantillons complété de silence (SNDBLST2.DRV perd le début des tampons plus
courts), au plus un derrière celui qui joue ; un bip resté plus de 300 ms en attente
est abandonné. Avec l'Apollo II, Épaulard charge la DLL pour ses seuls bips, sans
lui confier la parole ; DLL absente ou sans pilote audio, la souris reste muette.

## Navigation par objet

Comme dans NVDA, l'objet navigateur se déplace avec Inser et le pavé numérique
(Verr Num éteint), sans toucher au focus :

| Touches | Action |
| --- | --- |
| Inser+5 | dire l'objet courant |
| Inser+8 | objet parent |
| Inser+2 | premier objet enfant |
| Inser+4, Inser+6 | objet précédent, suivant |
| Inser+9, Inser+3 | précédent, suivant dans l'ordre de lecture (vue à plat) |
| Inser+Entrée du pavé | activer l'objet (clic au centre) |
| Inser+moins | objet du focus (la revue est aussi recentrée) |
| Inser+Maj+moins | donner le focus à l'objet |
| Inser+Suppr du pavé | position et taille de l'objet, en points |
| Inser+/ | souris sur l'objet, si la dernière commande était une commande d'objet |
| Inser+* | objet sous la souris |

Windows 3.1 n'ayant pas d'arbre d'accessibilité, l'arbre des objets est celui des
fenêtres : au sommet le bureau, puis les fenêtres principales, puis leurs fenêtres
filles visibles dans l'ordre de Windows (celui de la tabulation dans une boîte).
Après les fenêtres filles viennent les textes que la fenêtre a dessinés elle-même,
relevés par le modèle hors écran, de haut en bas et de gauche à droite : c'est ce
qui permet de parcourir une application qui dessine tout dans une seule fenêtre
(icônes d'un groupe du Gestionnaire de programmes, par exemple). Un contrôle
inconnu est annoncé « volet » suivi de sa classe. La plage braille montre l'objet
jusqu'au prochain changement de focus ou commande de revue.

Limites : pas de rôles ni d'états pour les textes dessinés (ce ne sont que des
textes) ; au plus 40 textes par fenêtre ; une fenêtre qui ne dessine qu'en
images n'a pas d'enfants lisibles. L'activation clique au centre de l'objet, ce
qui n'est pas toujours la bonne action (NVDA appelle l'action par défaut).

## Limites de la revue

La revue à plat décrit des rangées à l'écran : deux contrôles côte à côte peuvent
être lus sur la même ligne. Elle ne fait pas défiler le document. Dans Write,
la revue normale est également limitée au texte visible enregistré par le modèle.
Les positions de caractères dans le texte dessiné restent estimées à partir des
largeurs, et les mots sont séparés par les espaces et tabulations.

Les flèches dédiées, Verr Num activé et les combinaisons du pavé avec Maj, Ctrl
ou Alt conservent leur comportement dans l'application ; seuls Inser+Maj+moins,
/ et * du pavé gardent un sens avec Maj (verrouillage) et ne tapent plus rien
Verr Num éteint.

Dire tout dans Write ou Word lancé moins d'une seconde ou deux après une autre
commande Inser : Ctrl ramène parfois le curseur trop haut (d'une ou deux lignes,
voire au début du document) au lieu de la ligne en cours de lecture. Défaut
constaté sous QEMU le 31 octobre 2026, antérieur aux touches de NVDA (même
résultat avec Inser+T), non corrigé.
Les clics se font où le modèle situe le caractère : sur du texte dessiné, la
position peut être décalée d'un ou deux points. Dans une session DOS sous Windows,
ce sont les commandes d’Épaudos qui s’appliquent ; parole et braille sont
[relayés par Épaulard](invite-dos.md). Les boîtes modales système qui bloquent
Épaulard sont lues par sa DLL, en parole et en braille, mais la revue et les
commandes de souris décrites ici n’y sont pas disponibles ; voir
[le braille dans ces boîtes](braille.md).
