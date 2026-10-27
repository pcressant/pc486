# Intervenir dans le BIOS de la VM 86Box

La VM 86Box est une Shuttle HOT-433A avec le BIOS Award 4.51PG (`86box/86box.cfg`).
Sa CMOS de travail, `run/nvr/hot433a_v451pg.nvr` (86Box la cherche à côté de sa
configuration générée, pas sous `-P`), est copiée par `scripts/pc486`
depuis la référence versionnée `86box/nvr/hot433a_v451pg.nvr.ok` quand elle manque.
L'écran du BIOS n'est pas vocalisé : on ne règle donc pas la CMOS dans la VM de
l'utilisateur, mais sur une machine d'essai, avec `scripts/bios-86box`, puis on met à
jour la référence.

## Procédure rapide

```sh
scripts/bios-86box prepare          # work/dev/bios : disques vierges, sonde, CMOS de référence
scripts/bios-86box start --setup    # 86Box isolé ; Suppr pendant le POST
scripts/bios-86box shot             # capture work/dev/bios/ecran.png, à lire avec Read
scripts/bios-86box key Return       # touches, une par une ou en série
#   … réglages, puis au menu principal : SAVE & EXIT SETUP, y, Return
scripts/bios-86box stop             # arrêt propre : 86Box écrit la CMOS d'essai
scripts/bios-86box start            # sans --setup : démarrage à froid sur la sonde
sleep 20; scripts/bios-86box com1   # attendu : N02 03F6/0F/3F 0103/0F/3FZ
scripts/bios-86box stop
scripts/bios-86box save             # copie la CMOS d'essai vers la référence .nvr.ok
```

Pour que la VM de l'utilisateur prenne la nouvelle référence : VM arrêtée, supprimer
`run/nvr/hot433a_v451pg.nvr` ; le prochain `pc486 start` la recrée. Ne jamais le faire
pendant que 86Box tourne : il réécrit la CMOS à l'arrêt.

Options : `prepare --vierge` (CMOS absente, pour voir le comportement d'origine),
`prepare --sans-d` (C: seul), `start --floppy IMAGE` (disquette dans A:, amorcée avant
C:). `PC486_BIOS_DIR` et `PC486_BIOS_DISPLAY` (défaut `:77`) changent le dossier et
l'affichage. Les outils (Xvfb, Openbox, xdotool, ImageMagick, nasm) viennent du nixpkgs
verrouillé du flake ; le premier lancement peut les télécharger.

## Isolation : ne pas déranger Orca

`bios-86box` lance 86Box dans `dbus-run-session`, avec `QT_ACCESSIBILITY=0` et
`NO_AT_BRIDGE=1`, sur un Xvfb privé. C'est indispensable : avec le D-Bus de la session,
chaque prise de focus dans Xvfb est annoncée par Orca à l'utilisateur comme une fenêtre
86Box au premier plan, et la passerelle braille suit aussi ce focus. Openbox est
nécessaire : sans gestionnaire de fenêtres, 86Box n'a pas le focus clavier.

## Clavier

Les touches sont envoyées par xdotool : appui de 80 ms, puis 350 ms de pause (plus vite,
l'Award en perd). Passent : `Delete`, `Up`, `Down`, `Left`, `Right`, `Prior` (Pg.Préc),
`Next` (Pg.Suiv), `Return`, `Escape`, les lettres. **Les touches de fonction (F1, F10…)
n'arrivent pas** : enregistrer par le menu (SAVE & EXIT SETUP, `y`, `Return`), et
entrer dans le Setup par `Delete` (`start --setup`) plutôt que par F1 sur un message
d'erreur. Le premier `Down` après l'ouverture d'un écran est parfois perdu : vérifier par
une capture avant de modifier une valeur.

## Carte du Setup (Award 4.51PG, 2A4X5H21)

Menu principal, deux colonnes ; `Right` passe à la seconde colonne sur la même ligne :

| colonne gauche | colonne droite |
| --- | --- |
| STANDARD CMOS SETUP | PASSWORD SETTING |
| BIOS FEATURES SETUP | IDE HDD AUTO DETECTION |
| CHIPSET FEATURES SETUP | SAVE & EXIT SETUP |
| POWER MANAGEMENT SETUP | EXIT WITHOUT SAVING |
| PNP/PCI CONFIGURATION | |
| LOAD BIOS DEFAULTS | |
| LOAD SETUP DEFAULTS | |

Dans les écrans de réglage, `Prior`/`Next` changent la valeur du champ en surbrillance,
`Escape` revient au menu principal. Depuis BIOS FEATURES SETUP, `Escape`, `Right`,
`Down` mène sur SAVE & EXIT SETUP.

- **STANDARD CMOS SETUP** : date, heure, puis les quatre disques. Sur la ligne d'un
  disque, `Right` va au champ MODE. Valeurs de la référence : TYPE **Auto** pour les
  quatre disques, MODE **NORMAL** pour le maître et l'esclave primaires, Drive A 1.44M,
  Drive B None, Video EGA/VGA, Halt On **No Errors**. `Up` depuis la date mène à Halt On.
- **BIOS FEATURES SETUP** : la référence ne change que Boot Up Floppy Seek
  (**Disabled**, 7ᵉ ligne). Boot Sequence A,C,SCSI, caches activés, NumLock On par défaut.

## Ce que dit la sonde

C: porte `tools/bios/sonde.asm` : elle écrit sur COM1 le nombre de disques vus par le BIOS
puis, pour 80h et 81h, « cylindre max/tête max/secteurs » rendus par INT 13h AH=08h, en
hexadécimal (`E` si le disque n'existe pas). Pour le modèle (C: 1015/16/63, D: 260/16/63) :

- `N02 03F6/0F/3F 0103/0F/3FZ` : correct, 16 têtes, avec D: ;
- `N01 03F6/0F/3F EZ` : correct, sans D: ;
- `…/1F/3F` (32 têtes) : un disque est en MODE AUTO ou LBA. Le DOS installé sous QEMU
  utilise 16 têtes : à corriger avant d'enregistrer la référence ;
- rien sur COM1 : le BIOS s'est arrêté, faire `shot`. Une CMOS vierge affiche « CMOS
  checksum error - Defaults loaded » et attend F1 ; recalculer la somme 0x10-0x2D en
  0x2E/0x2F n'y suffit pas, il faut enregistrer depuis le Setup.

## Pièges déjà rencontrés

- 86Box réécrit sa configuration et supprime les clés qu'il ne connaît pas (par exemple
  `vid_renderer = vnc`) : `bios-86box start` repart toujours d'une copie du modèle.
- Le choix du BIOS est la section `[Shuttle HOT-433A]`, clé `bios = hot433a_v451pg` ;
  sans elle, c'est l'AMI de la même carte. Le Setup d'Award se reconnaît au titre
  « ROM PCI/ISA BIOS (2A4X5H21) ».
- `fdc` et `hdc_1` doivent rester `internal` : la carte a son UM8663AF (disquette, IDE,
  ports). Un `fdc_at` en plus affiche « Floppy disk(s) fail (40) » et A: n'amorce plus.
- 86Box écrit aussi `nvr/hot433a_v451pg.bin`, copie de la mémoire flash du BIOS :
  fichier ignoré par Git, inutile à la référence.
