# Programmes Windows 16 bits et Épaulard

État au 28 octobre 2026 de la lecture, par Épaulard, des exécutables Windows 16 bits
présents sur `vm/win311.img` (Paintbrush, programme de dessin, est volontairement
laissé de côté). Les programmes DOS (`DOS\*.EXE`, `WORD_DCA.EXE`…) relèvent
d'Épaudos ; les programmes d'installation (`INSTALL.EXE`, `WINSETUP.EXE` et
`WINWORD\INSTALLE\INSTALL.EXE`) n'ont pas été relancés. Chaque programme a été ouvert
dans une copie de C: sous QEMU (`scripts/pc486-dev`), synthèse simulée, avec Épaulard
fraîchement compilé. Les comportements génériques sont couverts par les bancs natifs
(`tests/win16/epaulard-test.c`, `osm-test.c`).

## Corrections génériques (profitent à plusieurs programmes)

- **Liste vide** : « liste, vide » au lieu de « élément 1 » (Gestionnaire
  d'impression, Presse-livres local).
- **Liste dessinée pas encore peinte** au moment du focus : « élément 1 », puis
  l'élément réel 400 ms plus tard (arbre du Gestionnaire de fichiers).
- **Libellé par l'ordre des fenêtres** : le texte statique qui précède le contrôle et
  porte son raccourci (`&`), un « : » ou un « ? », en remontant les textes
  intermédiaires (« Longue », « Courte » d'un curseur du Clavier) ; zone de saisie
  réglée par le bouton radio ou la case qui la précède (386 étendu, « Inactivité
  (secs) »). Les séparateurs sans lettre (« / », « : » de Date/Heure) ne sont jamais
  des libellés.
- **Texte des boîtes d'autres classes que `#32770`** (boîte `NBMSGCLASS` de Microsoft
  Backup, fenêtre du Gestionnaire de télécopie), sauf formulaires (zones de saisie,
  listes, curseurs).
- **Titre du document MDI** quand Ctrl+F6 passe à une autre fenêtre de document non
  agrandie (Sysedit : AUTOEXEC.BAT, CONFIG.SYS, WIN.INI…).
- **Cadre de groupe** dit en y entrant (les quatre boutons « Modifier » d'International
  sont distingués par « Format de la date », « Format horaire »…).
- **Curseurs** (barres de défilement contrôles) : nommés par leur libellé ou leur
  cadre, valeur en pour cent de leur course.
- **Zones de saisie** : unité écrite après elles (« 2 Minutes », « 75 Pixels ») ; texte
  lu dans le modèle hors écran quand la zone ne le rend pas ; signe seul nommé
  (« point-virgule ») ; nouvelle valeur dite après Haut/Bas/Pages ; un champ
  `ES_MULTILINE` d'une seule ligne de haut est traité comme une zone d'édition.
- **Liste déroulante dessinée** : ce qu'elle affiche, et non le texte de l'élément
  qui porte des données (« Standard Microsoft=C0C0C0,… » de Couleurs).
- Flèches de décor « >> » et « << » retirées des noms (« Palette de couleurs »).
- Boutons dessinés d'autres classes contenant « BUTTON » (`MSW_WN_GRBUTTON` de
  Microsoft Undelete) : rôle « bouton ».
- Rangée d'une liste dessinée signalée par un rectangle de focus (dossiers et messages
  de Mail) : l'élément seul, sans « bouton ».
- **Modèle hors écran** : les textes d'une fenêtre détruite (et de ses filles) sont
  oubliés au crochet CBT `HCBT_DESTROYWND`. Windows réutilise aussitôt les identifiants
  de fenêtre : sans cela, le glossaire de l'Aide « contenait » « Ligne de commande: »
  de la boîte Exécuter fermée. Un sous-titre en couleurs de sélection dont le texte est
  celui de sa fenêtre n'est plus pris pour une barre de titre si la fenêtre n'en a pas
  (`WS_CAPTION`).
- **Lecture des boîtes dessinées** (`EpOsmMainText`) : ordre de lecture tolérant aux
  lettres en gras décalées de quelques points.

## Par programme

| Programme | État |
| --- | --- |
| Gestionnaire de programmes, de fichiers, Bloc-notes, Write, Calculatrice, Word 6 | Déjà lus ; voir les documents existants. Gestionnaire de fichiers : premier élément de l'arbre corrigé, titres des fenêtres de répertoire au Ctrl+F6 |
| Aide (`WINHELP`), glossaire | Voir ci-dessous |
| Fiches (`CARDFILE`) | Titre de la fiche de devant dit au focus et à chaque changement de fiche (Page suivante, Rechercher), avec sa ligne courante ; mode Liste déjà lisible |
| Table de caractères (`CHARMAP`) | Grille : caractère choisi, nommé (« point d'exclamation », « é, Alt+0233 »), d'après la barre d'état ; Inser+Haut le relit |
| Horloge (`CLOCK`) | Heure dite à l'activation et par Inser+Haut (heure du PC) |
| Enregistreur de sons (`SOUNDREC`) | Boutons Retour rapide, Avance rapide, Lecture, Arrêt, Enregistrement ; curseur « Position » en secondes ; état (Arrêté, Lecture…) dit quand il change |
| Diffuseur de médias (`MPLAYER`) | Boutons nommés (Lecture, Pause, Arrêt, Éjecter), « curseur de position » sans valeur. Aucun périphérique MCI n'est installé : non essayé en lecture |
| Gestionnaire de liaisons (`PACKAGER`) | Volet actif (Apparence, Contenu) et son contenu, à Tab et Inser+Haut |
| Panneau de configuration et ses modules | Couleurs, Polices, Ports, Souris, Bureau, Clavier, Imprimantes, International, Date/Heure (jour, mois, année selon le format court de WIN.INI ; heures, minutes, secondes), MIDI, 386 étendu, Gestionnaires, Fax, Son : essayés au clavier |
| Gestionnaire d'impression, Presse-livres (`CLIPBRD`), Sysedit, Regedit, PIF, Liste des tâches, Winver, Enregistreur (`RECORDER`) | Contrôles standard ; listes vides et titres MDI corrigés |
| Terminal | Boîte de port et menus lus ; nouvelles lignes de la zone de terminal (`DC_TERM`) dites quand l'affichage se stabilise, sauf l'écho de la frappe. **Non essayé avec des données reçues** (pas de modem dans la VM) |
| Didacticiel (`WINTUTOR`) | Chaque écran (colonne principale de texte) lu quand il apparaît. Leçon « Souris » sans intérêt au clavier |
| Microsoft Mail | Boîtes (`Sdi`) : message lu à l'ouverture ; champs nommés par leur libellé dessiné (« Mot de passe: », « A: », « Objet: »), corps du message en « zone de texte » ; dossiers et messages ; menus. Essayé : création d'un bureau de poste local, identification, composition |
| Schedule+ | Refuse de démarrer : il n'accepte que les dates de 1920 à 2019, et l'horloge de la VM est en 2026. Limite du programme |
| Télécopie (`FAXMGR`, `FAXVIEW`), `KEYVIEW`, `SIGVIEW` | Messages et fenêtre principale lus ; `FAXVIEW` affiche des images (comme Paintbrush) ; `KEYVIEW`/`SIGVIEW` exigent Mail avec sécurité |
| SmartDrive Monitor, MSInfo, Anti-Virus, Undelete, Backup (outils de MS-DOS 6) | Contrôles standard. Backup signale que VFINTD.386 n'est pas installé (configuration, pas Épaulard) |
| `CLIPSRV`, `MSREMIND`, `RASSTART`, `MAILSPL`, `MWAVTSR` | Programmes de fond sans fenêtre à lire |
| Excel 5.0 / 5.0c | Grille, formules, feuilles, plage sélectionnée, mise en forme : voir [Tableurs](tableur.md). Boîtes SDM comme Word |
| Ami Pro 3.01 | Document (dessiné en twips) : lignes, mots, caractères, sélection, dire tout ; menus, boîtes standard (`AMIDIALOG`). Barre d'icônes non lue |
| Access 2.0 | Boîtes, boutons à image nommés par leur libellé, fenêtre de base de données, grilles (création, propriétés, feuille de données) : voir [Grilles](tableur.md#grilles-à-éditeur-access). Conseiller non lu |
| Invite MS-DOS | Lue par Épaudos, parole relayée : voir [Invite MS-DOS](invite-dos.md) |
| Solitaire, Démineur, Dame de Pique | **Non accessibles** : cartes et cases sont des images (voir Limites) |

## Aide de Windows

WinHelp n'a pas de curseur : le focus reste sur la fenêtre `MS_WINDOC` (ou
`MS_WINTOPIC_SECONDARY` pour le glossaire), le texte est dessiné dans `MS_WINTOPIC`
sous la zone fixe `MS_WINNSR`.

- **Tab / Maj+Tab** : le lien choisi (texte inversé) est dit, suivi de « lien ».
- **Entrée** sur un lien, Index, Précédent… : la première ligne de la nouvelle
  rubrique est dite.
- **Haut / Bas, Page précédente / suivante** : la ligne apparue (en bas avec Bas, en haut
  sinon).
- **Inser+Bas** : dire tout de la rubrique, page par page (Page suivante envoyée par
  Épaulard, la rangée de recouvrement n'est pas répétée) ; fin quand la page ne change
  plus. Ctrl ou une autre touche l'arrête.
- Définitions du glossaire (fenêtre `MS_WINNOTE`) : lues à leur apparition.

## Commandes de diagnostic

- **Inser+D** : focus, classe, curseur, puis la chaîne des parents du focus (classe,
  style).
- **Inser+Maj+D** : toutes les fenêtres de la fenêtre de premier niveau du focus
  (profondeur, identifiant, classe, numéro, style, texte), dans la parole et donc dans
  `apolo-emu.log`. Sert à reconnaître la structure d'un programme inconnu.
- **Inser+Maj+O** : le modèle hors écran de la fenêtre active (fenêtre, position,
  taille, texte, police), les petites images notées (cases, boutons radio) et les huit
  dernières opérations raster inhabituelles (`P` PatBlt, `B` BitBlt, `R` InvertRgn,
  `I` InvertRect, avec le code et la taille en unités logiques).

## Limites

- **Jeux** : Solitaire dessine ses cartes avec ses propres bitmaps (il n'utilise pas
  `CARDS.DLL`), la Dame de Pique charge `CARDS.DLL` (`cdtDraw`, `cdtDrawExt`), le
  Démineur copie des cases depuis une image. Rien de tout cela n'est du texte : il
  faudrait reconnaître les images (détourner `cdtDraw`, ou noter les bitmaps chargés et
  leurs copies). Non fait.
- Didacticiel : la lettrine du premier mot de certains écrans est perdue (« e
  Gestionnaire »), sa position étant mal notée dans le modèle ; une boîte posée sur le
  texte peut mêler ses lignes au texte dessous.
- Mail : une liste de messages vide sans élément choisi peut être annoncée « zone de
  texte » ; les boutons de la barre d'outils ne reçoivent pas le focus.
- Bureau, Date/Heure : les flèches de réglage (`CPARROW`) ne répondent qu'à la souris ;
  la nouvelle valeur est dite si elle change après Haut/Bas.
- Terminal non essayé avec une vraie liaison ; Diffuseur de médias sans périphérique.
