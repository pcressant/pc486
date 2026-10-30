# pc486 : DOS et Windows 3.11 accessibles avec Épaudos et Épaulard

Chaîne vocale : Épaudos ou Épaulard (protocole Apollo 2) → COM1 → socket Unix
`run/com1.sock` → [Apolo-emu](https://github.com/pcressant/apolo-emu) (Apollo II virtuel) → Speech Dispatcher
→ eSpeak NG.

Émulateur : **86Box** par défaut (vrai BIOS de 486, minutage fidèle), QEMU en option
(`--qemu`) pour le pilotage automatique (`vmctl.py`). Les deux utilisent le même disque
`vm/win311.img` : ne jamais les lancer en même temps.

Le [témoignage à l’origine du projet](docs/temoignage.md) présente son histoire
et sa motivation.

**Épaudos**, le lecteur d'écran TSR DOS du projet, est en cours de développement :
navigation au pavé inspirée d'Épaulard, écho écran, parole Apollo II et braille Baum.
Il est chargé par le choix DOS du menu de démarrage.
Voir [la compilation, les commandes et les limites](docs/epaudos.md).

## Avec Nix

Le flake fournit les dépendances sur Linux x86-64, dont Apolo-emu depuis GitHub :

```sh
nix develop                 # outils de développement
nix run .#prepare           # liens locaux et compilation, sans modifier les disques
nix run .#run               # démarrage 86Box ; ajouter -- --qemu pour QEMU
nix run .#run -- status
```

Pour une installation neuve : `nix run .#prepare -- --dos media/msdos622/msdos622fr.zip
--windows media/wfw311/wfw_3-11_fr.zip --output vm/win311.img` (sur une seule ligne).
Les supports restent locaux. Voir [le guide Nix](docs/nix.md) pour les commandes,
les services hôte et le démarrage d'une installation neuve avec QEMU.

## Installation neuve

L'installateur utilise les supports **locaux** de l'utilisateur : DOS 6.22 français
(trois disquettes) et Windows pour Workgroups 3.11 français (neuf disquettes), sous
forme de ZIP (ou de dossiers d'images IMG/IMA). Facultativement : le disque supplémentaire de
DOS 6.22, Word 6.0c, Excel 5, Access 2.0, Ami Pro 3.01, Word 6.0 pour DOS,
Netscape Navigator 3.01, Communicator 4.08, Eudora Light 3.06, le pilote de CD-ROM et
TCP/IP-32. Ces logiciels ne sont ni inclus dans les sources ni téléchargés par le
script.

```sh
scripts/installer --dos media/msdos622/msdos622fr.zip --windows media/wfw311/wfw_3-11_fr.zip \
  --suppl media/msdos622/msdos622fr.zip --check
scripts/installer --dos media/msdos622/msdos622fr.zip --windows media/wfw311/wfw_3-11_fr.zip \
  --suppl media/msdos622/msdos622fr.zip \
  --word media/word6-win/msword6cfr.zip --word-dos media/word6-dos-fr/msword60_525hd_dos_fr.zip --name 'Utilisateur' \
  --output vm/win311.img          # machine minimale : DOS, Windows, Word
```

La sortie doit être absente. Le script crée un nouveau disque, installe DOS
(clavier français, page 850), les outils supplémentaires, Épaudos, Windows,
son pilote Sound Blaster, les logiciels fournis, Épaulard et le braille Baum sur
COM3, avec le menu et les fichiers de démarrage du dépôt. Il vérifie ensuite le
démarrage de chaque lecteur. Les options `--excel`, `--access`, `--amipro`,
`--netscape`, `--communicator`, `--eudora`, `--cdrom` et `--reseau` sont décrites
avec les autres ; la commande d'une machine complète, réseau compris, est dans le
guide ci-dessous. `nix run .#iso` réunit les mêmes supports, décompressés, dans une
ISO locale lue en E: pour installer un logiciel à la main.
Voir [les prérequis, options et diagnostics](docs/installation-automatique.md).

## Commandes

    cd ~/pc486
    scripts/pc486 start            # VM 86Box + synthèse Apollo II (Apolo-emu)
    scripts/pc486 start --floppy DISK1.IMG   # avec une image de disquette dans A:
    scripts/pc486 start --qemu     # même disque sous QEMU
    scripts/pc486 stop             # arrête VM et synthèse (attendre l'invite C:\> avant)
    scripts/pc486 status
    scripts/pc486 gateway restart  # relancer seulement la synthèse
    scripts/pc486 screen           # écran texte QEMU, lisible au lecteur d'écran
    scripts/transfert put FICHIER -d APPS   # copier vers D:\APPS (VM arrêtée)
    scripts/transfert ls

Ajouter `--headless` pour démarrer QEMU sans fenêtre. Dans la fenêtre QEMU, Ctrl+Alt+G
libère le clavier et la souris.

La synthèse est **[Apolo-emu](https://github.com/pcressant/apolo-emu)**, fournie
par le flake ou par la commande `apolo-emu` installée. Le dépôt voisin
`../apolo-emu` reste accepté (autre emplacement : `PC486_APOLO_EMU_DIR`).
`scripts/pc486` la lance avec `--language fr --codepage cp850 --extension
burst-interrupt --no-volume`. Options supplémentaires (variable `PC486_GATEWAY_OPTS`) :
`--module pico`, `--voice …`, `--rate-offset -15`, `--backend simulated` (muet), etc. ;
liste complète : `apolo-emu --help` dans le shell Nix.
Journaux : `logs/apolo-emu.log` (texte prononcé, mutes, commandes inconnues) et
`logs/apollo-trace.log` (octets reçus et renvoyés).

## Machine virtuelle 86Box (par défaut)

| élément  | réglage                                                    |
|----------|------------------------------------------------------------|
| carte    | Shuttle HOT-433A (UMC 8881), BIOS Award 4.51PG              |
| CPU      | Intel 486DX4/100, mémoire 16 Mo                             |
| vidéo    | Cirrus Logic GD5428 ISA                                     |
| son      | Sound Blaster 16 (A220 I5 D1 H5) + haut-parleur PC          |
| disques  | IDE de la carte : C: `vm/win311.img` (1015/16/63), D: `vm/transfert.img` (260/16/63) s'il existe |
| souris   | série Microsoft sur COM2 (pour Windows)                     |
| COM1     | pseudo-terminal 86Box, relié à `run/com1.sock` par socat    |

- Configuration : modèle versionné `86box/86box.cfg`. À chaque lancement, `pc486` en
  écrit une copie `run/86box.cfg` (dans `PC486_RUN`), passée à 86Box et supprimée à
  l'arrêt : chemins de disques absolus, D: retiré si son image manque
  (`PC486_TRANSFERT=` : jamais de D:). 86Box y ajoute ses valeurs propres à l'hôte
  (`host_cpu`, `uuid`) ; le modèle n'est jamais réécrit. Un réglage changé dans
  l'interface de 86Box est perdu à l'arrêt : le reporter dans le modèle.
- CMOS : `run/nvr/hot433a_v451pg.nvr` (86Box la lit à côté de sa configuration générée), créée par `pc486` depuis la référence
  versionnée `hot433a_v451pg.nvr.ok` si elle manque (jamais remplacée ensuite). La
  référence a été enregistrée par le Setup de l'Award : disques en type **Auto**, détectés
  à chaque démarrage (avec ou sans D:), mode **NORMAL** (le mode AUTO présente C: en
  32 têtes au lieu des 16 de QEMU et de l'installation), « Halt On : No Errors » et
  « Boot Up Floppy Seek : Disabled ». Une CMOS vierge arrête le BIOS sur « CMOS checksum
  error », en attente de F1, écran que rien ne vocalise. Pour revenir à la référence :
  supprimer `run/nvr/hot433a_v451pg.nvr`, VM arrêtée. Changer un réglage du BIOS :
  `scripts/bios-86box`, sur une machine d'essai isolée (`docs/bios-86box.md`).
- Outils fixés dans `tools/` (liens nix, protègent du ramasse-miettes) : `86box`
  (`_86box-with-roms`, ROM non libres : `NIXPKGS_ALLOW_UNFREE=1 nix build --impure`) et `socat`.
- 86Box ignore SIGTERM : `pc486 stop` passe par sa socket de gestion (`run/86box-mgr.sock`,
  commande `shutdownnoprompt` écrite dans `run/86box.ctl`).
- Un superviseur attend la fin de 86Box et ferme ses relais, même quand on quitte
  l'émulateur par sa fenêtre. Cela évite qu'un ancien relais lise le port série
  d'une nouvelle VM après réattribution du pseudo-terminal. Son journal est
  `logs/86box-supervisor.log`.
- 86Box tourne en Wayland natif (`QT_QPA_PLATFORM=wayland`, `PC486_QPA=xcb` pour revenir à
  XWayland) : il demande à GNOME de lui laisser tous les raccourcis (Alt+F6, Alt+F7…) tant
  que sa fenêtre a le focus. **Super+Échap** rend les raccourcis à GNOME.
  86Box est recompilé avec `tools/86box-pc486.patch` (`nix build --impure -f tools/86box.nix
  -o tools/86box`) : sans lui, la demande à GNOME part trop tôt et n'aboutit pas.
  **Ctrl+Alt+G** bascule la capture des raccourcis, comme sous QEMU.
- En fenêtre, 86Box envoie toutes les touches à la machine et ignore ses propres raccourcis
  (Ctrl+F12, Ctrl+Alt+F12…). Ils ne marchent qu'en plein écran, et les combinaisons par
  défaut (Ctrl+F10/F11/F12) gêneraient les applications DOS. À la place : `pc486 cad`, `pc486 reset`,
  `pc486 pause`.

## Braille dans Épaulard

Le braille optionnel utilise un afficheur Baum virtuel sur COM3, relié à BrlAPI
et au BRLTTY de l'hôte. Il suit la ligne et le curseur, permet le défilement et la
revue ; le routage fonctionne dans les zones EDIT et, par estimation de la
position des caractères, dans Write et Word. Construire la passerelle
et le 86Box corrigé, puis installer avec `epaulard/build.sh install --braille`
(VM arrêtée) et démarrer avec `scripts/pc486 start` : le braille est alors automatique.
`--no-braille` permet de le désactiver.
Voir [l'installation, les commandes et les limites](docs/braille.md).

## Machine virtuelle QEMU (`--qemu`)

| élément  | réglage                                                    |
|----------|------------------------------------------------------------|
| CPU      | 486, émulation pure (TCG, pas de KVM), `-icount shift=7,align=on` ≈ 7,8 MIPS, calé sur le temps réel |
| mémoire  | 16 Mo (`PC486_RAM=…` pour changer)                         |
| C:       | IDE maître, `vm/win311.img`, 500 Mo                         |
| D:       | IDE esclave, `vm/transfert.img`, 128 Mo FAT16 (échanges avec Linux), attaché s'il existe |
| A:       | disquette 1,44 Mo, changeable à chaud (`scripts/vmctl.py floppy …`) |
| vidéo    | Cirrus Logic (VGA, pilotes Windows 3.11 existants)          |
| son      | Sound Blaster 16 (A220 I5 D1 H5) + AdLib + haut-parleur PC |
| COM1     | socket Unix `run/com1.sock` (synthèse Apolo-emu)           |
| LPT1     | fichier `logs/lpt1.out` (sert d'imprimante)                |
| BIOS     | SeaBIOS classique                                          |

Le bridage `-icount` reste le défaut de `pc486 start --qemu` : des programmes Turbo
Pascal plantent avec « Runtime error 200 » sur un processeur trop rapide.
`PC486_ICOUNT=off` le retire (pleine vitesse de TCG, horloge de l'invité sur celle de
l'hôte), par exemple pour un essai automatique. L'installateur tourne sans bridage par
défaut (`--icount 7` pour le remettre) : aucun programme de l'installation n'en dépend.
Ne pas descendre à `shift=6` (15 MIPS) : en mode icount QEMU émule sur un seul fil et
ne suit plus ; la VM tourne alors à ~55 % du temps réel en occupant un cœur entier.

Consommation : DOS attend le clavier en boucle active, ce qui fait tourner l'émulateur
à 100 %. `POWER.EXE`, chargé par `dos/CONFIG.SYS` sous MS-DOS 6.22,
permet de réduire cette consommation au repos.

## Contenu publié

Le dépôt contient les sources, tests, scripts de compilation et d’installation,
configurations nécessaires et documentation technique, avec leurs licences.
Les supports propriétaires, disques,
compilations, journaux, archives personnelles et réglages locaux des assistants
restent exclus par `.gitignore`.

Pour publier, utiliser les fichiers sélectionnés par Git plutôt qu’une archive
du répertoire de travail : celui-ci peut contenir des supports et données privés.
Après le premier commit, `git archive --format=tar.gz --output=/tmp/pc486-sources.tar.gz HEAD`
produit une archive des seuls fichiers versionnés.

## Arborescence

| Dossier | Contenu |
| --- | --- |
| `epaulard/` | Sources Win16 du lecteur et compilation locale dans `build/` |
| `epaudos/` | Sources du lecteur DOS (TSR) et compilation locale dans `build/` |
| `scripts/` | Lancement, installation, pilotage, passerelle braille, services réseau (`reseau/`) |
| `tests/` | Bancs natifs, intégrations et scénarios Windows |
| `docs/` | Guides d'utilisation, protocoles et diagnostics |
| `dos/`, `86box/` | Fichiers de démarrage et outil RESEAU.EXE de l'invité, configuration de l'émulateur |
| `tools/` | Expressions Nix, correctifs et liens locaux vers les outils |
| `vm/` | Disques locaux C: et D: (facultatif), exclus de Git |
| `media/` | Supports locaux, exclus de la publication |
| `run/`, `logs/` | État et journaux de la session principale |
| `work/` | Données de développement et archives locales, exclues de Git |

## Limites

- Écho écran d'Épaudos : il compare la mémoire vidéo 18 fois par seconde ; un texte qui
  défile entièrement entre deux comparaisons n'est pas lu (voir `docs/epaudos.md`).
- L'extension `burst-interrupt` d'Apolo-emu coupe la parole en cours quand un message
  arrive après une pause de plus de 40 ms, sauf si un écho `@E` est attendu. Ajoutée
  pour Sonolect, elle reste active : Épaulard a été validé avec elle, et Épaudos fait
  suivre chaque ligne de son écho écran d'un `@E` pour ne pas être coupé.
- La synthèse parle en priorité SSIP « message » : en « text », chaque message annulait
  le précédent (on n'entendait que le prompt après un dir).

- Minutage : QEMU exécute d'une traite les boucles d'attente (un bip de 0,1 s sur un
  vrai 486 : environ 2 000 × 256 instructions `loop`) puis met la VM en pause pour
  recaler l'horloge. Les bips ne durent que 0,4 à 2 ms en temps réel (mesuré avec
  `PC486_TRACE=pcspk_io_write`). Tout ce qui est minuté par boucle est faussé.

- Touches : pour diagnostiquer : `PC486_TRACE_KEYS=1 scripts/pc486 start …` enregistre dans
  `logs/touches.log` les touches que QEMU reçoit de GTK.

- Une session « Invite MS-DOS » de Windows est lue par Épaudos, dont Épaulard relaie
  la parole et le braille ([l'invite MS-DOS](docs/invite-dos.md)) ; il faut pour cela
  avoir démarré Windows par le menu, qui charge Épaudos avant `WIN`. Une session
  silencieuse ne signifie pas que Windows est figé : voir
  [le diagnostic des sessions DOS](docs/diagnostic-dos.md).
- Apolo-emu reproduit le protocole de l'Apollo II (texte, arrêt, réglages, échos,
  interrogations), pas sa voix. Le volume demandé par `@A` est ignoré
  (`--no-volume`) : celui de l'hôte s'applique. Limites : [limitations-fr.md](https://github.com/pcressant/apolo-emu/blob/main/docs/limitations-fr.md).
- `scripts/pc486 stop` coupe la machine comme un interrupteur : quitter les applications
  et revenir à C:\> avant.

## Windows pour Workgroups 3.11 et Épaulard

Avec `--word`, Word 6.0c pour Windows (édition française) est installé dans
`C:\WINWORD`. Lancement : groupe **Microsoft Office**, icône **Microsoft Word**. Voir
[prise en charge et limites de lecture](docs/word6-accessibilite.md).
Pour installer Word, suivre [l’installation automatique](docs/installation-automatique.md).

L’installation utilise MS-DOS 6.22 et Windows pour Workgroups 3.11 français,
en VGA 16 couleurs pour fonctionner sous QEMU comme sous 86Box.

    scripts/pc486 start            86Box
    scripts/pc486 start --qemu     QEMU

Menu de démarrage (non lu à voix haute, aucun lecteur n'est encore chargé) :
**1** = DOS avec Épaudos, **2** puis Entrée = Windows avec Épaulard ; DOS au bout de 10 s.
L'installateur neuf reproduit ce menu. SMARTDRV est en
lecture seule. Enregistrer les documents, quitter Windows (Alt+F4 dans le
Gestionnaire de programmes) et revenir au DOS avant `pc486 stop`.

**Épaulard** (`epaulard/`) est un lecteur d'écran écrit pour ce projet, Windows 3.1
n'ayant aucune interface d'accessibilité. Il parle en protocole Apollo II sur COM1, donc
par Apolo-emu, comme Épaudos. Le dire tout attend l'écho `@E` renvoyé après chaque ligne. Lancé par `load=` dans WIN.INI.

- `EPAUHOOK.DLL` : crochets globaux (clavier, caractères tapés, menus, focus) ;
  parle et affiche en braille elle-même quand une fenêtre modale système bloque les
  autres programmes (« Quitter Windows »). Elle contient le pilote braille Baum.
- `EPAULARD.EXE` : lit la fenêtre active, les boîtes de message, le contrôle qui a le
  focus (bouton, case à cocher, bouton radio, zone d'édition, liste, liste déroulante)
  avec son libellé (texte statique au-dessus ou à gauche), les menus, l'écho des
  caractères, la ligne et le caractère sous le curseur dans une zone de texte.
  Les contrôles désactivés sont décrits comme « grisés ».
- Calculatrice : annonce des valeurs, des résultats et des erreurs en modes
  Standard et Scientifique ; **Inser+Haut** relit le résultat. **Inser+Gauche/Droite**
  parcourt les boutons, **Inser+Entrée** les active et **Inser+Page suivante** donne
  la mémoire et les réglages scientifiques, également consultables en braille.
  Voir [la calculatrice](docs/calculatrice.md).
- Modèle hors écran (`osm.c`) : TextOut, ExtTextOut et PatBlt de GDI, BeginPaint,
  GetDC, GetWindowDC et GetDCEx de USER sont détournés en mémoire (saut vers la DLL) ;
  chaque texte affiché est noté avec sa fenêtre, sa position et ses couleurs. Il sert
  aux éléments en surbrillance (icônes du Gestionnaire de programmes), aux listes
  dessinées par le programme (Gestionnaire de fichiers) et au curseur que Write dessine
  lui-même (mince rectangle inversé par PatBlt).
  ScrollWindow et ScrollDC déplacent aussi le texte mémorisé lors du défilement.
  La lecture suit les déplacements verticaux même quand le curseur reste au bord
  de la fenêtre, notamment dans les grands documents Write.
  BitBlt et DeleteDC sont aussi détournés : le texte dessiné hors écran puis copié
  (lignes de Word) est suivi, ainsi que le curseur, les rectangles de focus et les
  cases à cocher que Word dessine lui-même. Voir [Word 6](docs/word6-accessibilite.md).
  Le modèle retient aussi la police et la couleur de chaque texte (Inser+F) et les
  zones inversées en DSTINVERT, qui forment la sélection de Write et de Word.
- Commandes : Inser+T titre, Inser+Tab focus, Inser+Haut ligne courante, Inser+B lire
  la fenêtre, Inser+O tout le texte affiché, **Inser+Bas dire tout** (lecture continue
  qui fait défiler le document, trois lignes envoyées d'avance pour une parole sans
  pause ; Ctrl ou toute autre touche l'arrête, Ctrl et les flèches verticales
  repartent de la ligne qui était lue), Inser+F
  police et mise en forme, Inser+Maj+Haut lire la sélection, Inser+PgSuiv barre
  d'état, **Inser+E réglages** (débit, hauteur, voix, ponctuation, écho du clavier,
  majuscules ; voir [les réglages](docs/reglages.md)), Inser+H aide,
  Inser+F4 quitter, Inser+D débogage (Inser+Maj+D : arbre des fenêtres), Ctrl seul = silence. Ctrl+Gauche/Droite lit le
  mot ; Maj+flèches annonce le texte sélectionné ou désélectionné (zones d'édition,
  Write, Word). La touche Inser est réservée à Épaulard.
- Alt+Tab : tant qu'Alt est tenu, chaque Tab coupe la parole et dit le titre du
  programme que la boîte de bascule de Windows affiche (Maj+Alt+Tab remonte, Échap
  annule et relit la fenêtre active) ; au relâchement, la fenêtre activée est lue
  comme d'habitude. Seulement avec la bascule rapide de Windows (`CoolSwitch`, actif
  par défaut) ; sans elle, chaque fenêtre est activée et lue à son tour.
- Autres programmes Windows (Aide, Fiches, Table de caractères, Panneau de
  configuration, Mail, Terminal, Didacticiel, Ami Pro…) : état et limites par
  programme dans [les programmes Windows](docs/programmes-windows.md).
- Excel 5 et Access 2.0 : cellule, valeur, formule, feuille, plage sélectionnée et
  mise en forme ; voir [les tableurs](docs/tableur.md).
- Invite MS-DOS : lue par Épaudos, parole et braille relayés par Épaulard ; voir
  [l'invite MS-DOS](docs/invite-dos.md).
- Pages de Netscape 3 et de Communicator 4 : mode navigation comme NVDA (curseur
  virtuel, H titres, K liens, T tableaux, X cases à cocher…, Inser+Espace pour le
  mode formulaire) ; voir [la navigation Web](docs/navigation-web.md). Il suppose le
  réseau isolé ci-dessous.
- Souris, comme NVDA, Verr Num éteint : / du pavé clic gauche, * clic droit, Maj+/ ou
  Maj+* verrouille le bouton, Inser+/ amène le pointeur au point de revue, Inser+*
  lit sous le pointeur. Voir [la revue et la souris](docs/revue-pave.md).
- Revue au pavé, **Verr Num éteint** : 7/8/9 ligne précédente/courante/suivante,
  4/5/6 mot précédent/courant/suivant, 1/2/3 caractère précédent/courant/suivant.
  **Inser+moins du pavé** bascule entre revue normale et revue à plat ;
  **Inser+5 du pavé** recentre la revue sur le focus ou le curseur de saisie.
  Les flèches dédiées et les combinaisons avec Maj, Ctrl ou Alt restent disponibles.
  Voir [les modes de revue et leurs limites](docs/revue-pave.md).

## CD-ROM et réseau isolé (facultatifs)

`scripts/pc486 start --cdrom [ISO]` ajoute un lecteur E: ; `scripts/pc486 start
--reseau` branche une carte NE2000 qui ne peut joindre que les services locaux de
l'hôte : un proxy qui traduit les pages modernes en HTML 3.2, et une boîte aux
lettres POP3/SMTP de test. Sans ces options, rien ne change. Voir
[le CD-ROM et le réseau](docs/reseau.md).

Pour essayer un logiciel sur une copie du disque et entendre ce qu'Épaulard en dit
pendant son installation : [le bac à sable](docs/bac-a-sable.md).

## Compilation et développement

Compilation (Open Watcom, `tools/watcom`, construit par
`NIXPKGS_ALLOW_UNFREE=1 nix build --impure nixpkgs#open-watcom-v2 -o tools/watcom`) :

    epaulard/build.sh            # dans epaulard/build/
    epaulard/build.sh install    # copie dans C:\EPAULARD de vm/win311.img (VM arrêtée)

Développement sans fenêtre, sur une copie isolée :

```sh
mkdir -p work/dev
cp vm/win311.img work/dev/win311.img  # VM principale arrêtée
scripts/pc486-dev pc486 start --qemu --headless
scripts/pc486-dev vmctl shot work/dev/ecran.png
scripts/pc486-dev pc486 stop
```

La synthèse est simulée, D: est débranché et les journaux sont dans
`work/dev/logs/`. Le disque principal reste indépendant.

Tests automatiques : installation, bancs Win16, synthèse simulée, braille et
cycle de vie des processus. Les scénarios Windows sous QEMU s’activent séparément ;
voir [le guide de développement Nix](docs/nix.md#construire-vérifier-et-actualiser).

    python3 -m unittest discover -s tests -v

## Licence

Le code et la documentation originaux de pc486 / Épaulard sont sous
**GNU GPL version 3 ou ultérieure** (`GPL-3.0-or-later`). Voir [LICENSE](LICENSE).

Les éléments tiers conservent leurs licences : la table braille issue de liblouis
est sous LGPL 2.1 ou ultérieure et le correctif dérivé de 86Box sous GPL 2 ou
ultérieure. Les provenances et licences
sont précisées dans [LICENSES/README.md](LICENSES/README.md).
Les supports Microsoft et les images disque restent extérieurs à cette licence.
