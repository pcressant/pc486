# Revue au pavé numérique

Éteindre Verr Num. La touche Inser peut être celle du clavier principal ou le 0
du pavé. Le signe moins utilisé ci-dessous est celui du pavé.

| Unité | Précédent | Courant | Suivant |
|---|---|---|---|
| Ligne | 7 | 8 | 9 |
| Mot | 4 | 5 | 6 |
| Caractère | 1 | 2 | 3 |

- **Inser + moins** : basculer le mode, annoncé à voix haute.
- **Inser + 5** : recentrer et relire la ligne, sans changer de mode.
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
l'application peuvent continuer à les déplacer ; Inser+5 permet ensuite de
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

Le point de revue est le caractère courant de la revue à plat ; en revue normale,
le curseur de saisie, l'élément d'une boîte de Word ou le milieu du contrôle qui a
le focus. Pour cliquer sur un élément affiché, passer en revue à plat (Inser +
moins), s'y rendre au pavé, puis Inser + / et / (ou * pour le menu contextuel).
Un bouton verrouillé permet de glisser : verrouiller, déplacer le pointeur par la
revue, puis relâcher.

Les clics passent par le point d'entrée `MOUSE_EVENT` de USER, comme ceux du
pilote de souris : Windows les traite comme un vrai clic (activation, capture,
menus). Le pointeur est placé par `SetCursorPos`.

## Limites du POC

La revue à plat décrit des rangées à l'écran : deux contrôles côte à côte peuvent
être lus sur la même ligne. Elle ne fait pas défiler le document. Dans Write,
la revue normale est également limitée au texte visible enregistré par le modèle.
Les positions de caractères dans le texte dessiné restent estimées à partir des
largeurs, et les mots sont séparés par les espaces et tabulations.

Les flèches dédiées, Verr Num activé et les combinaisons du pavé avec Maj, Ctrl
ou Alt conservent leur comportement dans l'application ; seuls / et * du pavé
gardent un sens avec Maj (verrouillage) et ne tapent plus rien Verr Num éteint.
Les clics se font où le modèle situe le caractère : sur du texte dessiné, la
position peut être décalée d'un ou deux points. Les sessions DOS et les
boîtes modales système qui bloquent Épaulard ne sont pas rendues accessibles par
ces nouvelles commandes.
