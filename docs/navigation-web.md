# Mode navigation des pages Web (Épaulard)

Épaulard lit les pages de **Netscape Navigator 3.01** et de **Communicator 4.08**
comme NVDA en mode navigation. Un curseur virtuel parcourt une représentation
structurée de la page : texte, titres, liens, listes, tableaux et champs. Les flèches
le déplacent sans toucher au focus de Netscape. Des lettres sautent d'un élément au
suivant. La parole et la plage braille suivent ce curseur. Le **mode formulaire** rend
les touches à Netscape pour saisir.

Le mode navigation suppose le réseau isolé et son proxy (`pc486 start --reseau`, voir
[reseau.md](reseau.md)). Sans proxy, ou pour une page qu'il n'a pas servie, Épaulard
lit la page comme avant : titre, lecture d'office, Haut/Bas sur les rangées dessinées.

## Utilisation

Au chargement d'une page, Épaulard dit son titre. Il demande ensuite la structure au
proxy et lit la page depuis le haut ; une touche interrompt la lecture. Le curseur
virtuel est alors actif.

### Déplacements

| Touche | Effet |
| --- | --- |
| Gauche, Droite | caractère |
| Ctrl+Gauche, Ctrl+Droite | mot |
| Haut, Bas | ligne (lignes de 100 caractères au plus, coupées entre deux mots) |
| Ctrl+Haut, Ctrl+Bas | paragraphe |
| Début, Fin | début, fin de la ligne |
| Ctrl+Début, Ctrl+Fin | haut, bas du document |
| Page précédente, Page suivante | 20 lignes |
| Inser+Bas | dire tout depuis le curseur ; Ctrl ou une touche l'interrompt, et le curseur reste sur la ligne qui était lue |
| Inser+Haut | relire la ligne |

En entrant dans un conteneur, Épaulard l'annonce : « liste de 3 éléments »,
« tableau de 6 lignes et 3 colonnes », « citation », « région navigation »,
« formulaire », « cadre », « objet ». Il annonce aussi la sortie : « fin de liste »,
etc. Une cellule atteinte aux flèches est annoncée par sa ligne et sa colonne.

### Navigation rapide

La lettre seule va à l'élément suivant ; avec Maj, au précédent. S'il n'y en a pas,
Épaulard le dit (« pas de titre suivant ») et le curseur ne bouge pas.

| Touche | Élément | Touche | Élément |
| --- | --- | --- | --- |
| H | titre | 1 à 6 | titre de ce niveau |
| K | lien | U, V | lien non visité, visité |
| L | liste | I | élément de liste |
| T | tableau de données | F | champ de formulaire |
| E | zone d'édition | B | bouton |
| X | case à cocher | R | bouton radio |
| C | liste déroulante ou liste | N | texte hors lien (après un groupe de liens) |
| Q | citation | S | séparateur (`<HR>`) |
| G | image (texte alternatif) | D | région (bannière, navigation, principal…) |
| M | cadre (`<IFRAME>`) | O | objet incorporé (vidéo, son) |
| P | paragraphe | virgule | fin du conteneur (Maj+virgule : son début) |

Les chiffres sont ceux de la rangée du haut, **sans Maj** (& é " ' ( - sur AZERTY),
comme avec NVDA en français. Maj+chiffre cherche en arrière. Ctrl et Alt seuls ne sont
pas pris : Ctrl+R, Alt+Gauche, Ctrl+O et les menus vont à Netscape.

T ne s'arrête que sur les tableaux de données (bordure, en-têtes ou légende), comme
NVDA. Les tableaux de mise en page restent lisibles aux flèches et par Ctrl+Alt.

### Tableaux

| Touche | Effet |
| --- | --- |
| Ctrl+Alt+flèches | cellule voisine |
| Ctrl+Alt+Début, Ctrl+Alt+Fin | première, dernière cellule de la ligne |
| Ctrl+Alt+Page précédente, Ctrl+Alt+Page suivante | première, dernière ligne |

Hors cellule, sur la légende juste après T, Ctrl+Alt+flèche entre dans la première
cellule. Quand le curseur change de ligne, Épaulard dit l'en-tête de la ligne puis
« ligne n » ; quand il change de colonne, l'en-tête de la colonne puis « colonne n ».
Viennent ensuite « sur 2 colonnes » ou « sur 2 lignes » pour une cellule fusionnée,
puis le contenu ; une cellule vide se dit « vide ». Au bord : « bord du tableau ».

### Champs, liens et modes

- **Entrée** sur un lien : Netscape l'ouvre ; un lien « dans la page » (`#ancre`)
  amène le curseur à son ancre.
- **Entrée ou Espace** sur une case ou un bouton radio : il est coché par Netscape,
  puis son nouvel état est dit une seule fois. Sur un bouton, il est activé ; un
  envoi de formulaire recharge la structure en gardant la position.
- **Entrée** sur une zone d'édition, une liste déroulante ou une liste : le focus y va,
  et Épaulard passe en **mode formulaire automatique** (« mode formulaire, Nom :, zone
  d'édition Dupont »). Les touches vont alors à Netscape. **Échap** revient au mode
  navigation, à la position du champ. Tab vers un champ fait de même.
- **Inser+Espace** bascule le mode à la main. Le focus va au champ sous le curseur
  virtuel s'il y en a un, sinon il reste dans la page.
- **Inser+F5** redemande la structure (page modifiée sans changer de titre).

Les champs disent leur libellé (`<LABEL>`, texte qui précède) et leur état réel, lu
dans les fenêtres de Netscape : case cochée ou non, choix d'une liste, texte saisi.

### Braille

La plage montre la ligne du curseur virtuel avec des abréviations : `t1` titre de
niveau 1, `ln` lien, `lnv` lien visité, `ed` zone d'édition, `btn` bouton, `cbo` liste
déroulante, `img` image, `---` séparateur. Les touches de défilement de la plage
changent de ligne. Une touche de routage place le curseur ; un second appui sur la
même cellule active l'élément, comme Entrée.

### Réglage

`EPAULARD.INI`, section `[Navigation]`, clé `Proxy=10.0.2.2:8080` (valeur par défaut).

## Fonctionnement

Le choix vient d'un essai des possibilités de Netscape par une sonde
(`tests/win16/sondenav.c`) :

- La page est **dessinée** dans une vue MFC (`AFX:…`, identifiant `0xE900`). Netscape
  n'offre ni modèle de document ni API d'accessibilité. Le modèle hors écran
  d'Épaulard ne voit que la partie affichée, en rangées de pixels.
- Le **DDE** de Netscape (`WWW_GetWindowInfo`) donne l'adresse et le titre de la page
  affichée ; `WWW_OpenURL` ouvre une adresse.
- Les **champs** de formulaire sont de vraies fenêtres filles de la vue (EDIT, BUTTON,
  COMBOBOX, LISTBOX). Leurs identifiants croissent dans l'ordre du document, y compris
  pour les champs hors de la partie affichée (cachés). Leur état se lit par messages
  (`BM_GETCHECK`, `CB_GETCURSEL`, `WM_GETTEXTLENGTH`). Le texte d'une zone d'édition,
  lui, n'est pas rendu par `WM_GETTEXT`.

D'où l'architecture :

1. Le **proxy** garde chaque page HTML 3.2 qu'il envoie à Netscape (100 adresses) et
   l'historique des liens suivis (`visites.txt` dans son dossier de données). Il
   sert `http://pc486/tampon?url=…`, la structure de la page au format
   `PC486-TAMPON 1` (`scripts/reseau/tampon.py`). Ce format contient le texte en
   windows-1252 puis la liste des éléments : titres et niveaux, paragraphes, liens et
   destinations, listes, cellules (ligne, colonne, fusion, en-têtes), champs (rang de
   fenêtre, libellé, état initial), régions, images, cadres et objets. La conversion
   (`html32.py`) marque les régions, images, cadres et objets par des commentaires
   `<!--pc486 …-->` ; les numéros de liens en sont exclus.
2. **Épaulard** (`navweb.c`, réseau dans `navnet.c`) demande, par DDE, l'adresse de la
   page affichée dès que son dessin est stable. Il obtient la structure par Winsock,
   de façon asynchrone (WINSOCK.DLL déjà chargée par Netscape, sinon rien), sans
   bloquer Windows.
3. **Synchronisation** : la structure n'est acceptée que si son titre est celui de
   la fenêtre. Netscape 3 écrit « Netscape - [titre] », Communicator « titre -
   Netscape ». Si deux des quatre premières rangées affichées n'ont aucun mot de
   cinq lettres ou plus présent dans la structure, Épaulard dit « Attention : la
   structure lue peut différer de la page affichée » et ne lit pas la page d'office.
   Un texte saisi dans un champ (remis par un retour arrière) ne déclenche pas
   l'alerte. Les champs ne sont associés que si leur nombre et leurs classes
   concordent (Communicator : boutons `BS_OWNERDRAW`, listes sans textes) ; sinon
   Épaulard répond « champ introuvable dans Netscape ».
   Un nouveau titre, un retour arrière, Ctrl+R, F5 ou un envoi de formulaire
   provoquent une nouvelle demande.
4. **Clavier** : en mode navigation, le crochet d'EPAUHOOK.DLL (`EpNavMode`) prend
   lettres, chiffres, virgule, Entrée, Espace, Tab et touches de déplacement destinés
   à la vue, et les envoie à l'EXE (`EPM_NAV`). Ctrl+touche et Alt+touche restent à
   Netscape, sauf Ctrl+Alt+flèches. En mode formulaire automatique, seul Échap est
   pris (sauf liste déroulante ouverte). Hors de Netscape, rien ne change.
5. **Activation** : un lien est ouvert par DDE. Pour un champ, la page défile jusqu'à
   lui (`EpPageReveal`), puis il reçoit le focus et Espace (case, radio, bouton). Une
   zone d'édition ou une liste reçoit seulement le focus.

## Essais

- **Banc natif** (`tests/test_epaulard.py`, `test_navweb` dans
  `tests/win16/epaulard-test.c`) : faux DDE, faux Winsock, faux champs de Netscape
  et structure réelle de la page d'essai. L'en-tête `tests/win16/navweb-essai.h` est
  généré par `python3 tests/navweb_essai.py` ; à refaire après tout changement de
  `html32.py`, `tampon.py`, `proxy.py` ou `essai.html` (un test le vérifie). Le
  scénario couvre chaque touche rapide dans les deux sens, les tableaux (vide,
  fusions, bords), les champs, les modes, Entrée sur un lien, le dire tout et son
  interruption, le braille, le changement de page, une page hors proxy et un titre
  différent.
- **Proxy** : `tests/test_navigation.py` (structure, en-têtes de tableau, rangées
  vides, champs, limites, point `/tampon`, historique).
- **VM (QEMU, copies `work/bac-net5` et `work/bac-net3`, 30 octobre 2026)** :
  - Netscape 3 et Communicator 4.08, page d'essai `http://pc486/essai` et page
    réelle `fr.wikipedia.org/wiki/Intel_80486` (62 Ko de structure) ;
  - H, Maj+H, 2, K, U, T, X, R, C, E, B, Ctrl+Alt+flèches ;
  - Entrée sur une case (état dit une fois), sur un radio, sur un bouton (envoi) et
    sur un lien ;
  - zone d'édition, saisie, Échap, Inser+Espace ;
  - dire tout et interruption ; Alt+Gauche, Ctrl+R.
  - Délai entre la touche et le premier octet envoyé à l'Apollo, sous QEMU bridé : 0
    à 125 ms de plus que l'envoi de la touche.

## Limites

- **Pages du proxy seulement.** Une page locale (`file:`), d'un autre proxy ou en
  HTTPS direct n'a pas de structure : lecture comme avant, et Inser+Espace répond
  « pas de mode navigation pour cette page ».
- **Synchronisation par le titre.** Deux versions d'une page de même titre (contenu
  changé par le site entre deux chargements) ne se distinguent que par l'alerte des
  mots manquants. Inser+F5 redemande la structure.
- **Liens visités** : ceux suivis par le proxy (historique conservé entre les
  sessions), pas l'historique de Netscape.
- **Texte saisi.** Il est lu dans le modèle hors écran quand le champ est affiché.
  Le champ est repeint si le modèle n'a pas la bonne longueur. Pour un champ hors de
  la partie affichée, Épaulard reprend la dernière valeur lue ou le texte initial si
  la longueur concorde, sinon il dit « 19 caractères ». Une liste dessinée par
  Communicator dont le choix a changé, hors de la partie affichée, est dite
  « élément 2 sur 3 ».
- **Taille** : 30 000 caractères et 1 500 éléments par page (au-delà, la structure
  est tronquée). Les pages plus longues sont déjà coupées par le proxy (« Page
  suivante »).
- **Non disponibles** : liste des éléments (Inser+F7 de NVDA), recherche dans la
  page, éléments que HTML 3.2 ne représente pas (repères ARIA hors régions, onglets,
  arbres, etc.). Netscape n'affiche pas les cadres (`<IFRAME>`) : M les signale par
  leur titre sans pouvoir y entrer.
- **Non essayés en VM** : la virgule (vérifiée au banc avec la touche Windows
  `VK_OEM_COMMA`), la plage braille physique, 86Box.
