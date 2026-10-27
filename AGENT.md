# Guide de reprise — pc486

Ce fichier est la référence commune pour les agents qui modifient ce dépôt.
`AGENTS.md` (Codex) et `CLAUDE.md` (Claude Code) sont des liens vers ce fichier :
modifier uniquement `AGENT.md` pour conserver une seule version des consignes.
Communiquer et rédiger la documentation en français.
Le code et la documentation originaux sont sous GPL-3.0-or-later ; conserver les
licences et attributions des éléments tiers recensés dans `LICENSES/README.md`.

## Reprendre rapidement

1. Lire ce guide, puis `git status --short` et `git log -6 --oneline` ; préserver les
   modifications déjà présentes et ne pas les attribuer à la session courante.
2. Identifier le composant dans la carte ci-dessous ; lire seulement ses sources
   et sa documentation utile, sans parcourir les images disque ou les archives.
3. Avant toute manipulation de VM, regarder `scripts/pc486 status` et les processus
   QEMU/86Box actifs, y compris ceux qui utilisent un autre répertoire `run-*`.
4. Faire une correction ciblée, exécuter les vérifications adaptées et indiquer
   clairement ce qui a été compilé, testé automatiquement ou essayé dans la VM.
5. Mettre à jour la documentation concernée si le comportement change. Ajouter ici
   les nouveaux pièges durables, pas un journal exhaustif de chaque session.

## But et état de référence

Reconstituer un PC 486 accessible : MS-DOS 6.22 français avec **Épaudos**,
Windows pour Workgroups 3.11 français avec **Épaulard**, lecteur d'écran du projet,
et Word 6.0c français. L'hôte utilise Linux/Nix et une synthèse vocale moderne.

Chaîne vocale : Épaudos **ou** Épaulard → COM1 → socket Unix →
Apolo-emu (Apollo II virtuel, `github:pcressant/apolo-emu`, entrée du flake) → Speech Dispatcher (SSIP)
→ eSpeak NG.
86Box utilise un pseudo-terminal relié à la socket par socat ; QEMU fournit la socket.

État documenté au 24 octobre 2026 : Windows et Word sont installés sur
`vm/win311.img` ; Épaulard suit le texte, le curseur, les menus et plusieurs boîtes
de Word, ainsi que le défilement de Write et la revue au pavé. Le dire tout utilise
des accusés de fin de parole sur COM1. Ce n'est pas encore un lecteur d'écran complet.

Le 25 octobre 2026, la synthèse est passée à Apolo-emu : Épaulard (dont le dire tout
par échos `@E`) a été essayé par l'utilisateur sous 86Box et fonctionne.

Le 27 octobre 2026, Sonolect a été retiré à la demande de l'utilisateur : le choix DOS
du menu charge Épaudos (`C:\EPAUDOS.COM`), installé par `scripts/installer` ; il n'y a
plus d'option `--sonolect` ni `pc486 start --test`. Les fichiers Sonolect restent sur
`vm/win311.img` mais ne sont plus lancés. `docs/protocole-apollo.md` garde l'analyse
du protocole de Sonolect.

Le 25 octobre 2026, l'utilisateur a aussi confirmé le braille d'Épaulard sur sa
plage physique sous 86Box, via COM3/Baum et BrlAPI. Voir `docs/braille.md` pour
l'installation, les commandes et les limites de cette première version.

La fin de `docs/installation-word6.md` décrit un état ancien de la lecture de Word. Pour la lecture
de Word, consulter `docs/word6-accessibilite.md` et le code actuel. Ne pas réinstaller
Windows ou Word sur la base de ces passages. Les images locales ne sont pas versionnées :
vérifier leur présence et leur contenu avant de supposer leur état.

## Carte du code

| Besoin | Point d'entrée |
| --- | --- |
| Environnement Nix, préparation et lancement | `flake.nix`, `flake.lock` ; `docs/nix.md` |
| Installation neuve (DOS, outils suppl., Épaudos, Windows, Word, Épaulard, braille) | `scripts/installer`, `scripts/installation.py` ; `docs/installation-automatique.md` |
| Démarrage, arrêt, relais série, supervision, options VM | `scripts/pc486` (Bash) |
| Essai QEMU muet isolé (copie de C:, work/dev/run, work/dev/logs) | `scripts/pc486-dev` |
| Écran texte, touches, saisie, captures, disquettes QEMU | `scripts/vmctl.py` (QMP/HMP, Python) |
| Échanges de fichiers via D: | `scripts/transfert` (mtools) |
| Synthèse Apollo II (protocole, file vocale, SSIP, échos) | Apolo-emu (GitHub, `tools/apolo-emu` après `nix run .#prepare`), lancé par `gateway_start` dans `scripts/pc486` |
| Annonces Windows, description des contrôles, revue, dire tout | `epaulard/src/epaulard.c` : `MainProc`, `describe`, `poll`, `on_review`, fonctions `sa_*` |
| Calculatrice Windows : afficheurs standard/scientifique, résultats et erreurs | `epaulard.c` : `calc_*` ; `docs/calculatrice.md` |
| Réglages Inser+E, `EPAULARD.INI`, ponctuation, écho par mots | `epaulard.c` : `cfg_*`, `voice_apply`, `punct_words`, `say_char`, `echo_char` ; `docs/reglages.md` |
| Crochets globaux clavier/messages/focus et parole en boîte système modale | `epaulard/src/hook.c` |
| Modèle hors écran, détournements GDI/USER, texte et curseur dessinés | `epaulard/src/osm.c` |
| Contrat DLL/EXE et messages `EPM_*` | `epaulard/src/epaulard.h` |
| Braille Baum Win16, traduction française et cellules | `epaulard/src/braille.c`, `brfr.h`, suivi dans `epaulard.c` |
| Épaudos, lecteur DOS TSR : revue, attributs, Apollo/Baum et veille Windows | `epaudos/src/core.c`, `dos.c`, `tsr.asm` ; `docs/epaudos.md` |
| Épaudos : boîtes de dialogue CUA en mode texte (Word DOS, EDIT, QBasic…) | `epaudos/src/dialog.c` (analyse pure), `dialog()` dans `core.c` |
| Passerelle Baum/BrlAPI, focus et transports | `scripts/braille/`, `tools/braille.nix`, `docs/braille.md` |
| Compilation et installation dans C: | `epaulard/build.sh` |
| Réglages matériels et BIOS 486 | modèle `86box/86box.cfg` (copie générée `run/86box.cfg`), CMOS de référence `86box/nvr/hot433a_v451pg.nvr.ok` (Award, disques Auto) ; Setup du BIOS : `scripts/bios-86box`, `docs/bios-86box.md` |
| Correctifs capture clavier Wayland/86Box | `tools/86box-pc486.patch`, `tools/86box.nix` |
| Menu DOS et lancement Épaudos/Windows | `dos/CONFIG.SYS`, `dos/AUTOEXEC.BAT` |

## Commandes usuelles

Depuis la racine du dépôt :

```sh
scripts/pc486 status
scripts/pc486 start                 # 86Box, usage interactif et minutage fidèle
scripts/pc486 start --qemu          # automatisation via QMP
scripts/pc486 start --qemu --headless
scripts/pc486 screen                # écran texte QEMU seulement
scripts/vmctl.py key ret
scripts/vmctl.py type --fr 'dir\n'  # clavier français chargé dans DOS
scripts/vmctl.py shot /tmp/pc486.png
scripts/pc486 gateway restart
scripts/pc486 stop                  # après fermeture de Windows et retour au DOS
```

Menu DOS non vocalisé : **1** = DOS + Épaudos (défaut après 10 s) ; **2**, puis
Entrée = Windows + Épaulard. Épaudos se met en veille pendant Windows et lui laisse
COM1 et COM3 ; ne jamais le charger depuis une session DOS de Windows. Épaulard démarre par `load=` dans `C:\WINDOWS\WIN.INI`.

`vmctl.py screen` et `wait` lisent la mémoire texte, pas le contenu graphique de
Windows : utiliser `shot` et les journaux vocaux pour Windows. `key` prend des qcodes
QEMU et des combinaisons séparées par des tirets ; voir son aide/source pour les touches.

## Protéger les disques et les sessions

- `scripts/installer` ne remplace aucun disque existant. Il travaille dans un
  dossier privé `.pc486-install-*` et ne publie sa sortie qu'après démarrage du
  lecteur et retour au DOS. Sous WfW FR /H, les groupes Réseau et Démarrage ont
  besoin d'alias de fichiers ASCII : un nom ANSI face à un nom FAT OEM peut bloquer
  le Gestionnaire de programmes avant le lancement du lecteur.
  Ses copies de supports et images restent locales ;
  ne pas les ajouter à une publication. Ses VM n'attachent aucun D: commun.
  `dos/CONFIG.SYS` et `dos/AUTOEXEC.BAT` sont les fichiers de démarrage qu'il écrit
  (identiques à la VM de référence) : les modifier change les nouvelles
  installations. Les pilotes de Windows sont compressés en KWAJ, pas en SZDD : il
  les décompresse dans l'invité avec `C:\WINDOWS\EXPAND.EXE`. SNDBLST2.DRV lit la
  section `[sndblst.drv]` de SYSTEM.INI (pas seulement `[sndblst2.drv]`) : sans
  elle, boîte d'erreur Sound Blaster au démarrage et Épaulard bloqué.
  Word 6.0c : ACME accepte `/Q1 /N "nom"` (installation par défaut sans dialogue) ;
  `epaulard/src/instauto.c` (INSTAUTO.EXE, jamais copié par `build.sh install`)
  le lance par `load=`, survit au redémarrage de Windows et quitte Windows à la fin.
  Le braille COM3 est configuré par défaut (`--sans-braille` pour l'éviter).
  Word 6.0 pour DOS (`--word-dos`) : INIT est piloté par sa table d'écrans
  `WORD_DOS_SCREENS` ; les choix sont lus en mémoire vidéo : barre unique d'une
  couleur différente du cadre, jamais un simple attribut 0Fh (`menu_rows`,
  `MenuWalk`). Les flèches de défilement (18h/19h) suivent certains libellés. Les
  vrais messages d'INIT sont dans `WORD60.INF` (demande : « Insérez la … dans
  l'unité … »). Toutes les disquettes regroupées dans `C:\INSTALL\WORDDOS` :
  aucune demande de disquette 5,25". `load=` de WIN.INI : uniquement par
  `configure-epaulard.py` (`configure`, `edit_load`), qui garde les autres programmes.
  Mode Texte obligatoire (graphique = Épaudos en veille).
- **Jamais deux émulateurs sur la même image inscriptible**, y compris D:
  `vm/transfert.img`. Un autre `PC486_RUN` isole les sockets/PID, pas les disques.
- `scripts/pc486` et `epaulard/build.sh install` partagent le verrou de
  `scripts/with-disk-lock.py`, conservé par l’émulateur. Ne jamais supprimer un
  fichier compagnon `.pc486.lock` pour contourner un refus. Les outils externes
  restent soumis aux précautions ci-dessous ; voir `docs/robustesse-epaulard.md`.
- Ne jamais utiliser mtools, `scripts/transfert` ou `build.sh install` sur un disque
  utilisé par une VM. Les garde-fous des scripts ne couvrent pas tous les lancements
  personnalisés : vérifier aussi les processus et leurs arguments.
- `pc486 stop`, `reset`, `cad` et QMP `quit` peuvent perdre le travail en cours.
  Enregistrer, quitter les applications puis Windows et revenir à l'invite DOS avant
  l'arrêt. Ne pas arrêter une session utilisateur pour une simple compilation.
- Avant installation ou expérimentation risquée, faire une copie du disque arrêté.
  `PC486_DISK=/chemin/copie.img` sélectionne le disque C: de QEMU ; 86Box utilise
  ses chemins de configuration. QEMU attache encore le D: commun s'il existe : pour
  un essai simultané, isoler aussi D: dans un lancement adapté.
- `build.sh install` vise toujours `vm/win311.img`, indépendamment de `PC486_DISK`.
- Les artefacts de développement restent sous `work/dev/` et `work/vmtest/`
  (disque, sockets et journaux ensemble) ; les sauvegardes sont dans
  `work/sauvegardes/`. Éviter de recréer des dossiers `run-*` ou `logs-*` à la racine.
- `vm/`, `media/`, `work/`, `logs/`, `run*`, les sorties de compilation et les liens
  d'outils sont locaux/ignorés par Git. Ne pas les ajouter de force ni les supprimer
  sans nécessité. Les logiciels tiers ne font pas partie du code à versionner.

## Compiler et vérifier

Prérequis locaux : Python 3 (bibliothèque standard), Bash, QEMU, mtools, socat,
86Box et ses ROM, Open Watcom pour Épaulard ; Speech Dispatcher/eSpeak NG pour parler.
Les liens `tools/86box`, `tools/socat`, `tools/watcom` pointent vers le magasin Nix.
Les commandes de reconstruction des outils sont dans le README et `tools/86box.nix`.

```sh
python3 -m unittest discover -s tests -v
epaulard/build.sh                   # compilation seule, aucune écriture dans la VM
epaudos/build.sh                    # TSR DOS, compilation seule
epaulard/build.sh install           # compilation + copie EXE/DLL + WIN.INI ; VM arrêtée
```

- Synthèse/cycle de vie : les tests lancent Apolo-emu en mode muet derrière une fausse
  socket QEMU et un faux 86Box avec le vrai socat ; ils ne nécessitent ni DOS ni parole
  réelle. Ils prennent Apolo-emu dans le `PATH`, dans `tools/apolo-emu` (paquet GitHub
  du flake, créé par `nix run .#prepare`) ou dans `../apolo-emu`, et sont ignorés sinon.
  Le protocole Apollo II lui-même est testé dans le dépôt Apolo-emu.
- Bash : `bash -n scripts/pc486 scripts/pc486-dev scripts/bios-86box scripts/transfert epaulard/build.sh` si ces scripts changent.
- Épaulard : compiler avec Open Watcom ; les avertissements sont traités comme erreurs.
- Bancs natifs (`tests/test_epaulard.py`, `tests/test_braille.py`) : les vrais `osm.c`,
  `epaulard.c` et `braille.c` sont compilés pour Linux par le Watcom du projet
  (`tests/native.py`, macro `EP_NATIVE_TEST`) sur la simulation Win16
  `tests/win16/win16sim.h`, face à une fausse Apollo II (15 car./s, échos `@E`), une
  fausse plage Baum et de faux Write, Word, Bloc-notes et Gestionnaire de programmes.
  Temps simulé : quelques secondes. Ils couvrent le dire tout (lignes vides,
  défilement, aucune pause, fin, Ctrl, Bas), les icônes sans « vide », l'en-tête de
  Word en mode Page, la sélection en points 7-8. Tout nouveau défaut corrigé y ajoute
  son scénario ; vérifier qu'il échoue sans le correctif. Un comportement réel de
  Windows qui contredit le banc signifie que la simulation est à corriger aussi.
- Scénarios QEMU (`tests/test_vm_scenarios.py`, 6 à 10 min, VM principale arrêtée) :
  `PC486_VM_TESTS=1 python3 -m unittest tests.test_vm_scenarios -v`. Copie jetable
  `work/vmtest/win311.img`, `work/vmtest/run/`, `work/vmtest/logs/`, EXE/DLL fraîchement
  compilés ; rien n'est écrit sur `vm/win311.img`. Après changement de hooks,
  curseur ou lecture, les lancer en plus des bancs.
- Régression utile : Bloc-notes (EDIT standard), Write (défilement dans les deux sens),
  Word (document, menus, boîtes SDM), revue au pavé, Inser+Bas et interruption par Ctrl.
  Les essais précis et leurs limites sont dans `docs/essais-write.md`,
  `docs/revue-pave.md` et `docs/word6-accessibilite.md`.

Pour un essai muet QEMU sur une copie, utiliser `scripts/pc486-dev`. Il fixe
`PC486_RUN=work/dev/run`, `PC486_LOGS=work/dev/logs`, `PC486_DISK=work/dev/win311.img`,
la synthèse simulée, aucun D: (`PC486_TRANSFERT=`) et un `PC486_BRAILLE_CMD` qui ne
fait rien. Il refuse `vm/win311.img` et peut donc tourner à côté de 86Box :

```sh
mkdir -p work/dev
cp vm/win311.img work/dev/win311.img       # seulement disque arrêté
scripts/pc486-dev pc486 start --qemu --headless --braille
scripts/pc486-dev vmctl key ret
scripts/pc486-dev pc486 stop
eval "$(scripts/pc486-dev env)"            # mêmes variables pour un autre outil
```

Les variables `PC486_DEV_DISK`, `PC486_DEV_RUN`, `PC486_DEV_LOGS`,
`PC486_DEV_GATEWAY_OPTS` et `PC486_DEV_BRAILLE_CMD` changent ces valeurs.
Apolo-emu se connecte à `work/dev/run/com1.sock`. `--backend simulated` ne parle pas
mais simule la durée de lecture (`--cps`, défaut 15), nécessaire aux échos du dire tout ;
le texte « prononcé » est dans `work/dev/logs/apolo-emu.log`. Avec `--braille`,
`com3.sock` reste libre pour un faux Baum (voir `docs/braille.md`). Si 86Box tourne,
partir d'une copie d'une image arrêtée (`work/sauvegardes/win311-avant-install-*.img`), jamais
de `vm/win311.img` en cours d'usage.

`.claude/settings.json` autorise sans confirmation la lecture git, `pgrep`/`ps`,
la compilation sans installation, les tests, `bash -n`, `pc486 status`/`screen`,
`scripts/pc486-dev`, `mdir`/`mtype`, les copies vers `work/`, toutes les commandes
`scripts/pc486` (start/stop/reset/cad compris), `vmctl.py` et `kill`/`pkill` : l'utilisateur
ne veut pas de confirmation pour relancer, arrêter ou déboguer les VM. Ces commandes
pouvant viser la VM principale, appliquer soi-même les précautions de « Protéger les
disques et les sessions » (session utilisateur en cours, travail non enregistré). Restent
soumis à confirmation, car ils écrivent sur les images : `build.sh install`,
`transfert`, `mcopy`/`mdel`.
Préférer `scripts/pc486-dev` aux préfixes de variables, que les règles ne couvrent pas.

## Contraintes techniques à conserver

- Épaudos est un COM 8086 sans bibliothèque C, un seul segment ; la borne
  `_resident_end` de `tail.asm` doit suivre toutes les données et les piles.
  Le moteur tourne après le BIOS INT 8h, PIC acquitté et interruptions permises,
  pour recevoir les UART sans FIFO pendant l'analyse de l'écran. Le clavier
  INT 15h/4Fh a sa propre pile de 512 octets ; le moteur et INT 28h partagent
  2 Kio avec garde de réentrance. Ne pas remettre le traitement sous INT 1Ch
  avec les interruptions masquées : pertes de réponses Baum sur un 8250.
  Les fichiers DOS ne sont ouverts que depuis INT 28h (PSP résident et gestion
  temporaire des erreurs critiques) ou le programme de premier plan. Le scénario
  `PC486_DOS_TESTS=1 python3 -m unittest tests.test_epaudos_vm -v` utilise une copie
  privée sous `work/vmtest/epaudos/`, jamais le disque principal.

- Boîtes de dialogue d'Épaudos : `dialog.c` reconnaît le contrôle sous le curseur
  matériel d'après les motifs Character Windows (`[··]`, `[X]`, `( )`, cadres,
  `< OK >`) et la boîte d'après le fond du contrôle ; une boîte qui touche un bord
  de l'écran est refusée (fenêtre de document). Word déplace un instant le curseur
  en dessinant : un contrôle n'est annoncé que vu deux passages de suite, la boîte
  n'est fermée qu'après trois passages sans contrôle. Pas d'écho écran ni de
  détection de surbrillance dans une boîte reconnue.
- Le banc DOS compile avec le `strstr` du Watcom, qui ne trouve pas les octets
  ≥ 80h ici : vérifier un caractère CP850 avec `nobyte()`, pas `absent("\xb3")`.
- Épaudos suit le curseur d'après la **classe de la dernière touche** transmise à
  l'application (`classify`, `track` dans `core.c`) et des empreintes par rangée
  (texte, attributs). Ne pas revenir à un test sur « la dernière rangée modifiée » :
  une ligne d'état qui change (EDIT) masquait tout déplacement. L'écho écran
  ajoute ses lignes avec `say_more` et un écho `@E`, sans Ctrl-X : sans l'écho,
  `burst-interrupt` couperait chaque ligne 55 ms après la précédente. Les ISR
  UART de `tsr.asm` bouclent sur l'IIR jusqu'à « rien en attente » : un seul
  traitement par interruption perdait le THRE derrière une réception (8259 à
  front) et figeait COM1 une fois la file pleine.
- Braille automatique si `tools/braille` est construit (`--no-braille` le désactive) : COM3 base 03E8 **IRQ9**, et non IRQ4,
  réservée à COM1. Reconstruire `tools/86box` après le correctif série et installer
  avec `epaulard/build.sh install --braille` pour régler SYSTEM.INI. COM2 reste la
  souris. Ne pas lancer `PC486_BRAILLE_FOCUS=always` contre le BRLTTY utilisateur :
  ce mode sert aux serveurs de test privés ; le mode normal suit le focus AT-SPI.
  Le paquet nécessite BrlAPI avec Polkit, GI et les typelibs AT-SPI/DBus/GLib ;
  `tools/braille/bin/pc486-braille --check` les vérifie sans ouvrir la plage.
  Polkit peut refuser un lancement SSH hors de la session graphique active.
- Épaulard est du **C Win16**, Open Watcom, modèle mémoire large (`-ml`), pas du Win32.
  Respecter `FAR`, `PASCAL`, `CALLBACK`, `__export`, `__loadds` et les limites de pile,
  segments et tampons. Suivre le style C existant compatible avec ce compilateur.
- Modifier les sources UTF-8 de `epaulard/src/` ; `build.sh` les convertit en CP1252
  dans `epaulard/build/`, qu'il recrée intégralement. Ne pas modifier les copies générées.
- Calculatrice `SciCalc` : les témoins mémoire sont les STATIC 402 (standard) et
  400 (scientifique). Les radios 127..129 signifient Deg/Rad/Grad en décimal,
  mais Dmot/Mot/Octet dans les autres bases : ne pas leur donner un nom fixe.
  Les boutons dessinés sont activés par les WM_COMMAND de CALC.EXE 3.10, pas par
  des fenêtres BUTTON. Le parcours Inser+flèches ne doit jamais activer un bouton seul.
- Windows 3.1 n'a pas d'API d'accessibilité moderne. Les contrôles standard passent
  par USER ; Write/Word nécessitent le modèle hors écran. Préserver les protections
  contre réentrance et double comptage lors des détournements GDI/USER.
- Détournements d'`osm.c` : pendant l'appel d'origine, les octets d'origine sont en
  place (`patch_enter`/`patch_leave`, compteur `depth`) ; ne pas reposer le saut
  après chaque appel. Win16 n'a pas de « finally » : un appel dont la tâche meurt
  ne revient jamais. Chaque appel actif, `busy` et `scrolling` notent leur tâche ;
  `osm_reap` (crochet WH_GETMESSAGE, installation, retrait) les libère quand
  `IsTask` est faux. Retrait pendant un appel vivant : `EpRemove` garde la DLL
  chargée (`LoadLibrary`). Un `Throw` d'application qui saute l'appel n'est pas
  détecté (tâche vivante).
- Annonces composées (`describe`, `describe_value`) : rôle, valeur et états
  (« sélectionné », « grisé ») passent avant la fin d'un libellé trop long
  (`fit_before`, `append_keep`). Ne pas revenir à un `append` qui les abandonne.
- `86box/86box.cfg` est un modèle : `pc486` génère `$PC486_RUN/86box.cfg` (chemins
  absolus, D: seulement si l'image existe) et le passe en dernier argument à 86Box,
  qui y écrit `host_cpu`/`uuid`. Ne pas relancer 86Box directement sur le modèle.
- 86Box : Shuttle HOT-433A, BIOS Award 4.51PG (`bios = hot433a_v451pg`), DX4/100,
  clavier PS/2. `fdc` et `hdc_1` restent `internal` : la carte a son UM8663AF ;
  un `fdc_at` en plus donne « Floppy disk(s) fail (40) » et A: n'amorce plus.
  La CMOS de travail est `$PC486_RUN/nvr/hot433a_v451pg.nvr` : 86Box la lit à côté
  du fichier de configuration passé en argument, **pas sous `-P`**. Elle vient de la
  référence `86box/nvr/hot433a_v451pg.nvr.ok` (copiée par `box_nvr` si absente). Disques en type Auto mais mode **NORMAL** : le mode AUTO
  traduit C: en 32 têtes, incompatible avec un disque installé sous QEMU (16 têtes).
  Une CMOS vierge bloque sur F1, écran non vocalisé. Changer un réglage du BIOS :
  **`scripts/bios-86box`** et `docs/bios-86box.md` (machine d'essai isolée, carte du
  Setup, touches utilisables, sonde de géométrie). Ne jamais lancer 86Box d'essai avec le
  D-Bus de la session : Orca annoncerait sa fenêtre à l'utilisateur.
- Word dessine en DC mémoire puis BitBlt ; son curseur n'est pas le curseur système.
  Son clavier utilise `PeekMessage(PM_NOREMOVE)` : les commandes Inser sont prises
  dès `HC_NOREMOVE`. Lire `docs/word6-accessibilite.md` avant de toucher à ces chemins.
- En mode Page (en-tête, pied de page), le bitmap d'où Word copie son curseur
  (2 px, rop 0x690145) contient aussi du texte noté : `EpBitBlt` traite donc toute
  copie de 4 px au plus, hors SRCCOPY, comme un curseur, avant `blit_text`. Sinon le
  curseur efface la ligne du modèle et la lecture se fige.
- Dire tout du texte dessiné : le curseur est envoyé en avant (Bas, Début) sur la
  dernière des `SA_AHEAD` lignes envoyées ; une interruption (Ctrl, Haut/Bas, commande)
  le ramène par Haut. La fin du document se voit à `EpOsmSignature` inchangée, pas à
  la seule ligne du curseur (plusieurs lignes vides défilent sous un curseur fixe).
- Ne pas lire Ctrl ou Maj par `GetKeyState` dans l'EXE pour une touche reçue par
  `EPM_KEY` : elle est traitée après coup, touche souvent relâchée (Ctrl+Tab n'était
  jamais vu). Le crochet transmet leur état dans le lParam (`EPK_CTRL`, `EPK_SHIFT`).
- Gauche/Droite dans un menu ouvert : Windows envoie d'un trait WM_MENUSELECT pour
  l'ancien titre, le nouveau et son premier élément. Les titres (MF_POPUP) attendent
  60 ms (`TIMER_MENU`) ; seul le dernier est dit.
- Boîtes de Word : `on_focusrect` ne s'applique pas aux contrôles standard (liste du
  Gestionnaire de fichiers lue deux fois, avec « bouton »). Les onglets sont dessinés
  dans un bitmap : le rectangle de focus y est noté puis reporté au BitBlt (`mfr`).
  Boîte Options : contrôles dans une page enfant `sa_sdm_…` (focus), traitée comme
  `bosa_sdm_…` par `is_sdm` et par `EpBeginPaint`, sinon ses libellés s'effacent.
- Menu des réglages : la DLL donne toutes les touches à l'EXE (`EpCfgMode`) ; elle
  rend le clavier seule après 70 s ou si l'EXE disparaît. Préserver ce garde-fou.
- Write et Word sélectionnent en inversant (DSTINVERT) chaque morceau ajouté ou retiré ;
  leur curseur fait 1 à 2 px de large, un caractère étroit 3 à 4 px. Ne pas relever le
  seuil curseur/sélection de `osm.c` sans réessayer Maj+flèches sur « texte ».
- Le pilote braille (`braille.c`) est dans EPAUHOOK.DLL, fonctions exportées
  (`BRAPI`) : la DLL l'utilise seule pendant une boîte modale système. Ne pas le
  recompiler dans l'EXE (deux états, trames mélangées sur COM3).
- `MOUSE_EVENT` (USER.299) prend ses paramètres dans les registres (AX drapeaux, DX
  nombre de boutons) : appel par `_asm` dans `mouse_raw`, pas par un prototype C.
- Apollo II : CP850, propositions terminées par CR **ou par `. , : ; ! ?`** ; Ctrl-X
  coupe la parole et efface la file. Le dire tout envoie `ligne CR @E<c> CR` : l'Apollo II
  renvoie `<c>` quand la lecture atteint l'écho, donc après la ligne, jamais après Ctrl-X.
  Commande authentique : ne plus utiliser l'ancienne extension `@I<c>`. Voir
  `docs/protocole-apollo.md` et `docs/protocole-fr.md` du dépôt Apolo-emu.
- L'extension Apolo-emu `burst-interrupt` (activée par `scripts/pc486`) coupe la
  parole après 40 ms de silence, sauf si un écho `@E` est attendu. Ajoutée pour
  Sonolect, elle reste active : Épaulard a été validé avec elle.
- Garder le bridage QEMU (`PC486_ICOUNT=7` par défaut) pour l'usage interactif : trop
  rapide, un programme Turbo Pascal échoue avec Runtime error 200 ; les boucles de bips
  restent imparfaites. `PC486_ICOUNT=off` le retire. L'installateur tourne sans
  bridage par défaut (`--icount off`, ~5 min) : une vérification qui ne passe qu'en
  bridé révèle un défaut lié au rythme (Épaudos ne lisait rien au lancement de Word
  pour DOS, dessiné en un passage), à corriger plutôt qu'à masquer en rebridant.
- Conserver POWER.EXE sous DOS pour éviter la boucle active qui sature un cœur.
- Les sessions DOS dans Windows ne sont lues ni par Épaulard ni par Épaudos (en
  veille) : silence ne signifie pas blocage.

## Diagnostic ciblé

Consulter `logs/apolo-emu.log` (texte prononcé, mutes, commandes inconnues),
`logs/apollo-trace.log` (octets reçus RX et renvoyés TX), `logs/86box-supervisor.log`
(cycle de vie), les fichiers de `run/` ou du `PC486_RUN` choisi. Sous QEMU :
`PC486_TRACE_KEYS=1` active `logs/touches.log`, `PC486_TRACE=evenement` les traces QEMU.
Inser+D donne le débogage d'Épaulard ; Inser+H annonce ses commandes.
Pour une session DOS Windows silencieuse, lire `docs/diagnostic-dos.md`.

En fin d'intervention, rapporter les fichiers modifiés, les validations effectuées
et les limites restantes ; distinguer une compilation réussie d'un essai fonctionnel.
