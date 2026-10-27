# Word 6.0c et Épaulard

Word 6 dessine presque tout lui-même : texte du document, règle, boîtes de dialogue
(classe `bosa_sdm_Microsoft Word 6.0`). Seules les zones de saisie (`OpusFedt`) et les
barres de défilement sont de vraies fenêtres. Épaulard s'appuie donc sur son modèle
hors écran (`epaulard/src/osm.c`).

## Ce qu'il faut savoir de Word

- Clavier : Word lit les touches par `PeekMessage(PM_NOREMOVE)`. Une touche absorbée par
  le crochet clavier pendant ce coup d'œil (`HC_NOREMOVE`) ne revient jamais en
  `HC_ACTION` : les commandes Inser+touche sont traitées au premier appel.
- Texte du document et règle : écrits dans un DC mémoire (origine 0,0), puis copiés à
  l'écran par BitBlt. Le modèle note le texte du DC mémoire et le reporte à l'écran au
  BitBlt ; l'espacement de chaque caractère (`dx` d'ExtTextOut) est conservé, ce qui
  donne le bon caractère sous le curseur.
- Curseur : BitBlt 2×15 px, rop 0x690145, sur la fenêtre `OpusWwd` ; le curseur système
  n'est pas tenu à jour.
- Mode Page (en-tête et pied de page) : le texte est écrit dans un DC mémoire à
  origine de viewport décalée, puis copié à l'écran. Le bitmap source du curseur y
  contient aussi du texte ; une copie fine non SRCCOPY reste un curseur.
- Sélection : chaque extension (Maj+flèche) inverse à l'écran le seul morceau ajouté ou
  retiré, en DSTINVERT, comme Write. Un caractère étroit (« t », « i ») fait 3 à 4 px :
  seule une inversion de 1 à 2 px est prise pour un curseur.
- Boîtes SDM : rectangle de focus en quatre traits PatBlt XOR (rop 0xA50065) ; cases à
  cocher et boutons radio par copie d'une image 14×13 de la bande des cases (abscisse
  source 0 = vide, 14 = marquée ; ordonnée 0 = case, 13 = bouton radio, 26 = case à
  trois états) ;
  flèche de liste déroulante 7×9 à droite du texte ; listes déroulantes ouvertes dans
  une fenêtre surgissante `_sdm_…` sans propriétaire. Les soulignés des raccourcis sont
  des ExtTextOut opaques vides, qui ne doivent pas effacer le texte voisin.
- Onglets (boîte Caractères) : la rangée d'onglets est dessinée dans un bitmap, avec
  son rectangle de focus, puis copiée à l'écran. Le modèle note le rectangle tracé
  dans le bitmap et le reporte à la copie (`EpOsmFocusRect` renvoie alors 2). La page
  affichée est l'onglet écrit en gras. Ctrl+Tab redessine la page en plus de 300 ms
  sous QEMU.
- Boîte Options (trois rangées d'onglets) : les onglets sont dessinés dans la boîte
  `bosa_sdm_…`, les contrôles dans une page enfant `sa_sdm_…`, qui a le focus. Ses
  BeginPaint ne doivent pas effacer les libellés (comme pour `bosa_sdm_…`). L'onglet
  actif est le texte en gras le plus haut des 80 premiers points ; les titres de
  groupe de la page sont en gras eux aussi. Ctrl+Tab n'y change pas de page : on
  passe d'un onglet à l'autre par Gauche/Droite quand l'onglet a le focus.

## Ce qu'Épaulard annonce

- Document : ligne, caractère et mot sous le curseur ; dire tout (Inser+Bas) avec
  défilement, jusqu'à la fin du document.
- Menus, y compris le menu système du document agrandi.
- Boîtes : texte du message ou de l'astuce à l'ouverture, puis l'élément qui a le focus
  avec son libellé dessiné : zone d'édition, liste, liste déroulante (et ses éléments
  quand elle est ouverte), case à cocher (état, et changement à l'Espace), bouton
  radio (« Saut de colonne, bouton radio, sélectionné »), titre du groupe à l'entrée
  dans un autre cadre (« groupe Insérer, Saut de page… »), bouton
  (sans libellé voisin), onglet (« Espacement, onglet »). Ctrl+Tab annonce la page
  affichée, puis l'élément qui a le focus. Le libellé pris à gauche est le texte en
  « : » le plus proche (« De: », pas « Normal De: »).
  La plage braille affiche le même élément, avec le libellé de la liste.
- Sélection : Maj+flèches, Maj+Origine… annoncent le texte ajouté (« te
  sélectionné ») ou retiré (« désélectionné ») ; Inser+Maj+Haut lit la sélection
  visible. Inser+F donne la police, le corps, gras, italique, souligné, barré et la
  couleur du texte sous le curseur.
- Braille : un curseur routine clique sur le caractère, ce qui y place le curseur.

## Limites connues

- Le titre de groupe est le texte qui commence juste à gauche des cases, au-dessus :
  la colonne de droite d'un groupe n'en a pas. En revenant de l'autre groupe vers
  elle, le groupe n'est donc pas redit.
- Ctrl+Tab : si la page ne change pas (boîte Options), la page actuelle est redite
  après 1,5 s d'attente ; sinon dès que l'onglet en gras change.
- La sélection n'est connue que pour la partie
  visible, d'après les inversions vues depuis le lancement d'Épaulard ; une sélection
  qui sort de la fenêtre n'est lue qu'en partie. Une sélection faite à la souris
  (glisser par Maj+/) n'est pas annoncée d'elle-même : Inser+Maj+Haut la lit.
- Inser+F ne connaît que ce qui est affiché : le corps est calculé d'après la hauteur à
  l'écran (exact au zoom 100 %), sans style ni mise en forme de paragraphe.
- Le texte de boîtes masquées peut rester dans Inser+O.
- Pendant le dire tout, le curseur précède la parole de deux lignes au plus (il fait
  défiler le document à l'avance) ; il revient sur la ligne lue à l'interruption par
  Ctrl, Haut, Bas ou une commande Inser, mais pas pour les autres touches.
