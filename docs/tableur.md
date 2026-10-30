# Tableurs (Excel 5) et grilles (Access 2.0)

`epaulard/src/tableur.c` lit les grilles des tableurs, qui sont entièrement
dessinées (aucun contrôle standard, aucun curseur système). La méthode est
générale ; un profil ne fait que nommer les classes de fenêtres.

## Ce qui est dit

| Action | Annonce |
| --- | --- |
| Déplacement (flèches, Entrée, Tab, Ctrl+Origine, Pg.suiv., Atteindre) | `B3, 36, formule égal B2 étoile 3` : référence, valeur affichée, formule s'il y en a une ; `B4, vide`. Un texte saisi différent de l'affichage est dit « saisi ». |
| Ctrl+Pg.suiv./Pg.préc. | La feuille : `Feuil2, A1, vide`. |
| Maj+flèches, Maj+Fin… | La plage : `sélection A1 à B3` ; revenue à la cellule active : `A1`. |
| Suppr, Ctrl+Z, collage | Le nouveau contenu de la cellule. |
| Inser+Haut | Relit la cellule. |
| Inser+F | Police, taille, gras/italique/souligné/barré, couleur, alignement (gauche, droite, centré). |
| Inser+Tab | Classeur, feuille, cellule. |
| F2, puis flèches, Origine, Fin | Le caractère sous le curseur ; Ctrl+flèches, le mot : `SOMME`, `B1`, `B3`. |

Les boîtes d'Excel (Format de cellule, vérification orthographique…) sont des
boîtes SDM comme celles de Word et sont lues par le même code (`boites.c`) :
cases, boutons radio avec leur groupe, listes, boutons. Une boîte SDM qui contient
une liste (`SCROLLBAR`) ou une zone `EDTBX` est un formulaire : à l'ouverture, seul
son titre est dit, pas tout son texte. Le choix d'Orientation (images de texte
tourné) n'est pas lu.

## Méthode

1. La barre de formule (Excel : fenêtre `EXCEL6`) donne le contenu ; la
   référence est le texte à sa gauche sur la même rangée (zone Nom), sinon son
   premier mot.
2. La cellule est située par ses en-têtes : « B » en haut de la grille, « 3 » à
   gauche ; ses bornes sont à mi-chemin des en-têtes voisins.
3. La valeur est le texte qui **commence** dans la cellule (pas celui d'une
   voisine qui déborde) ; sa police et sa position donnent la mise en forme.
4. La feuille active est l'onglet en gras, sur la rangée de la barre de
   défilement horizontale de la grille.
5. La plage sélectionnée est inversée, sauf la cellule active. Excel l'inverse par
   `InvertRgn` (la plage moins la cellule active, puis chaque ajout) : le modèle hors
   écran garde, par fenêtre, une vraie région combinée en OU exclusif
   (`rg_xor`, `EpOsmInvBox`). Depuis la cellule active, la plage s'étend aux
   colonnes et lignes dont le centre est dans l'enveloppe de cette région.

Deux corrections du modèle hors écran en découlent :

- Excel repeint toute sa grille sous une région limitée à deux cellules (Entrée) :
  les effacements (`PatBlt`, `ExtTextOut` opaque, `BitBlt`) sont désormais bornés à
  la zone de découpage du contexte (`clip_dp` dans `osm.c`) ; sinon les en-têtes
  disparaissaient du modèle.
- Ctrl+G change l'attribut sans redessiner le texte : Inser+F fait repeindre la
  cellule (`InvalidateRect`, `UpdateWindow`) avant de lire sa police.

Édition (F2) : la formule est dans la fenêtre `EXCEL7`, lue comme tout texte
dessiné avec un curseur. Excel n'y redessine que les trois caractères autour du
curseur : le modèle hors écran garde le début et la fin d'une ligne quand un
fragment identique est retracé dessus (`cut_text` dans `osm.c`).

## Autres tableurs : profils

Dans `EPAULARD.INI` :

```ini
[Tableurs]
; nom=classe de l'application,classe de la grille (focus),classe de la barre de formule[,classe des onglets]
autre=CLASSE_APPLICATION,CLASSE_GRILLE,CLASSE_BARRE_FORMULE
```

Le profil intégré d'Excel 5 est `XLMAIN,EXCEL9,EXCEL6`. Pour un tableur inconnu :
Inser+Maj+D donne l'arbre des fenêtres (classes), Inser+Maj+O les textes du modèle
hors écran avec leur fenêtre, leur position et leur police.

## Grilles à éditeur (Access)

Les feuilles de données, la grille de création et le volet des propriétés
d'Access 2.0 sont des grilles dessinées (`OGRID`) sur lesquelles une fenêtre
d'édition (`OKTTBX`) se place à la cellule active ; Access la recrée souvent.
Quand elle change de place, `grid_poll` dit :

- l'étiquette de la ligne : le texte à gauche de la cellule sur sa rangée (« Taille
  du champ », nom du champ, ou la première colonne d'un enregistrement), si la ligne
  a changé ;
- l'en-tête de colonne, si la colonne a changé et que la grille a des sélecteurs de
  ligne (petites images à gauche) : sinon, sa rangée du haut est une donnée ;
- la valeur, ou « vide ». Un contenu changé sans frappe (Suppr, liste) est dit.

Inser+Haut relit les trois. Exemple : « Dupont, Ville, Paris », puis Bas :
« Martin, Lyon ». Profils dans `EPAULARD.INI` :

```ini
[Grilles]
; nom=classe de l'éditeur,classe de la grille
autre=CLASSE_EDITEUR,CLASSE_GRILLE
```

La ligne active est dessinée par l'éditeur : ses textes sont notés dans sa fenêtre,
où l'étiquette est aussi cherchée.

## Limites

- Plage sélectionnée : seule sa partie affichée est mesurée ; son contenu n'est pas lu.
- Cellule hors de l'écran : son contenu est lu dans la barre, pas sa valeur affichée.
- Les titres de lignes et de colonnes ne sont pas annoncés.
- La saisie dans une cellule est lue par l'écho du clavier, pas par la grille.
- Access : numéro d'enregistrement (« Enr: 1 sur 2 ») non annoncé ; formulaires non
  audités.
