# Installation neuve depuis des supports locaux

`scripts/installer` crée un PC sous **MS-DOS 6.22 français** (clavier français,
page de code 850) avec **Épaudos**, lecteur d'écran DOS du projet, et si on les
fournit les **outils du disque supplémentaire**, puis **Windows pour
Workgroups 3.11 français**, **Word 6.0c français** et **Épaulard**, et enfin
**Word 6.0 pour DOS français** en mode Texte, lu par Épaudos. Avec `--suppl` et
`--word`, le disque obtenu reproduit la VM de référence `vm/win311.img` à partir
des seules sources du dépôt et des supports d'installation : aucune image
existante n'est lue. Word pour DOS s'y ajoute avec `--word-dos`.

Le code publié ne contient ni image Microsoft, ni Word, ni archive de ces
logiciels, ni URL de téléchargement. L'installateur n'utilise pas le réseau.

## Prérequis

Avec Nix, `nix run .#prepare -- --dos … --windows … --output vm/win311.img`
fournit les outils et lance cet installateur. Voir [le guide Nix](nix.md).

Hôte Linux avec Python 3, Bash, QEMU (`qemu-system-i386`) et mtools (`mdir`, `mtype`,
`mcopy`, `mmd`). Pour compiler Épaulard, construire Open Watcom dans `tools/watcom`
comme indiqué dans le README. On peut aussi fournir les binaires d'Épaulard
avec `--binaries /chemin/vers/les/binaires` : `EPAULARD.EXE` et `EPAUHOOK.DLL`,
plus `INSTAUTO.EXE` avec `--word`.
Ces binaires sont ceux du lecteur du projet, pas des composants Microsoft.

Pour utiliser ensuite la synthèse vocale, installer séparément **Apolo-emu**, Speech
Dispatcher et sa voix. Les dépendances de la passerelle braille et de 86Box sont
décrites dans le README et [le guide braille](braille.md). L'installation elle-même
ne requiert ni serveur vocal, ni plage physique, ni ROM de 86Box.

Préparer les supports que l'on est autorisé à utiliser :

- DOS : `DISK1.IMG`, `DISK2.IMG`, `DISK3.IMG`.
- Windows : `DISK1.IMA` à `DISK9.IMA`.
- Facultatif, `--suppl` : l'image du disque supplémentaire de MS-DOS 6.22
  (`SUPPL.IMG`, édition anglaise « Supplemental Disk », la seule publiée).
- Facultatif, `--word` : Word 6.0c pour Windows, édition française, `Disk01.ima`
  à `Disk11.ima` (dossier ou ZIP), étiquettes `WWRD60C_F01` à `F11`.
- Facultatif, `--word-dos` : Word 6.0 pour MS-DOS, édition française, disquettes
  5,25" **1,2 Mo** (dossier ou ZIP). Elles sont reconnues par leur nom de volume,
  quel que soit le nom du fichier : `SETUP_60`, `PROG_60`, `UTIL_60`, `FONT_60`,
  `PRINT_60`, `PRINT2_60`. `GRAMMAR_60` (Grammatik, installateur séparé) est ignorée.
- Images brutes de disquettes **1,44 Mo** pour DOS, Windows et Word pour Windows, non modifiées. Les variantes 720 Ko,
  les éditions anglaises de DOS ou de Windows, les mises à jour et Windows 3.1 ne
  constituent pas le profil pris en charge.

Les noms des disquettes sont insensibles à la casse ; `Disk01.ima` convient
également. Un dossier ou un ZIP local peut être fourni pour DOS et pour Windows.
Dans un ZIP, seules les images DISK1..N sont importées, vers des noms fixes dans le
dossier de travail ; les chemins internes de l'archive ne sont jamais extraits.

## Vérifier les entrées

Depuis la racine du projet :

```sh
scripts/installer --dos media/msdos622 --windows media/wfw311 \
  --suppl media/msdos622/SUPPL.IMG --check
```

Cette commande vérifie les prérequis, le nombre et la taille des disquettes,
leur lecture par mtools et les fichiers caractéristiques du profil. Elle ne crée
aucun disque et ne démarre pas QEMU. Des doublons ambigus ou une disquette absente
font échouer la vérification.

## Installer une machine identique à la VM de référence

```sh
scripts/installer \
  --dos media/msdos622 \
  --windows media/wfw311 \
  --suppl media/msdos622/SUPPL.IMG \
  --word media/word6-win \
  --word-dos media/word6-dos-fr \
  --name 'Utilisateur' \
  --output vm/win311.img
```

Le disque de sortie doit porter l'extension `.img` et **doit être absent** : un
fichier existant, un dossier ou un lien symbolique, même cassé, est refusé.
L'installateur ne propose pas d'option pour écraser une installation. Sur une
machine déjà équipée, choisir une sortie sous `work/installations/` ; cela laisse
`vm/win311.img` intact.

Options :

| Option | Effet |
| --- | --- |
| `--name`, `--company` | Utilisateur et organisation de Windows, 30 caractères au plus. |
| `--suppl IMAGE` | Lance le `SETUP.BAT` d'origine du disque supplémentaire vers `C:\DOS` : tous les composants (AccessDOS, utilitaires clavier, DRVBOOT, outils de MS-DOS 5, fichiers réseau, MS-DOS Shell en VGA). |
| `--word DOSSIER` | Installe Word 6.0c dans `C:\WINWORD` avec les options par défaut (WordArt, Graph, éditeur d'équations, convertisseurs et filtres, aide), sans clipart. Les supports restent dans `C:\INSTALL\DISK1` à `DISK11`, comme dans la VM de référence. |
| `--word-dos DOSSIER` | Installe Word 6.0 pour DOS dans `C:\WORD` en mode Texte, sans gestionnaire d'imprimante ni de souris, avec le gestionnaire de clavier « pour résidents » ; ajoute `C:\WORD` au PATH. Les fichiers des disquettes restent dans `C:\INSTALL\WORDDOS` pour relancer `INIT` (imprimante, par exemple). |
| `--sans-braille` | Ne configure pas la plage. Par défaut, Épaulard utilise une plage Baum sur COM3, base 03E8, IRQ9 (`BraillePort=3`, ports de SYSTEM.INI), comme la VM de référence ; la plage n'est pas contactée pendant l'installation. `--braille`, ancienne option, reste accepté. |
| `--binaries DOSSIER` | Utilise des binaires déjà compilés : `EPAULARD.EXE`, `EPAUHOOK.DLL`, `EPAUDOS.COM` (et `INSTAUTO.EXE` avec `--word`). Sinon `epaulard/build.sh` et `epaudos/build.sh` sont lancés. |
| `--icount N` | Bridage du processeur QEMU : `off` (défaut, pleine vitesse, installation complète en 5 min environ) ou `N`, 2^N ns par instruction (`7`, environ 7,8 MIPS, comme `pc486 start --qemu`, plus de 20 min). |
| `--timeout S` | Délai maximal par étape VM (défaut 1200 s). |

L'ancienne option `--sonolect` n'existe plus : Épaudos, compilé depuis les
sources, remplace Sonolect dans le choix DOS du menu.

Réglages versionnés, repris de la VM de référence :

- `dos/EPAULARD.INI` : réglages vocaux d'Épaulard (Inser+E), débit 5, sans écho
  du clavier. Le modifier change les nouvelles installations.
- Épaudos démarre avec ses réglages par défaut ; `C:\EPAUDOS.INI` est créé à la
  première sauvegarde (Inser+E).

## Étapes

Les étapes s'affichent dans le terminal, numérotées selon les options :

1. Vérification et copie privée des supports, calcul des empreintes SHA-256.
2. Installation DOS d'origine, partitionnement, formatage et changement automatique
   des trois disquettes.
3. Avec `--suppl` : démarrage de C: avec le disque supplémentaire dans A:, puis
   `A:\SETUP C:\DOS` ; réponses « All », F5 (VGA) et « Yes ». Si un fichier
   existe déjà, la réponse est « No » : le fichier de MS-DOS 6.22 est conservé.
4. Regroupement des neuf disquettes Windows dans `C:\WININST`, puis INSTALL.EXE
   avec un fichier de réponses `/H` : VGA, clavier français, souris « Microsoft ou
   IBM PS/2 » (pilote qui gère aussi la souris série de 86Box sur COM2), sans
   réseau, imprimante ni didacticiel. Le DOS charge alors la page de code 850 et
   `KEYB FR`, pour que Windows installe les polices et tables 850. L'`EXPAND.EXE`
   de Windows décompresse ensuite les pilotes Sound Blaster 1.5 et AdLib.
   Avec `--word` : copie des onze disquettes dans `C:\INSTALL\DISKn`, puis Windows
   démarre avec `load=C:\INSTAUTO.EXE` au lieu d'Épaulard. Ce petit programme du
   projet (`epaulard/src/instauto.c`) lance `INSTALL.EXE /Q1 /N "nom"`, le mode
   sans dialogue de l'installateur ACME, attend qu'aucune autre tâche ne tourne
   depuis 10 s (y compris après un redémarrage de Windows demandé par
   l'installateur), puis quitte Windows. Il note les titres des fenêtres vues dans
   `word-instauto.log` du dossier de travail. `INSTAUTO.EXE`, son journal, son
   entrée de `load=` et la section `[pc486]` de WIN.INI sont ensuite retirés.
   `load=` n'est modifié que par `scripts/configure-epaulard.py`, commun à
   l'installateur et à `epaulard/build.sh install` : les autres programmes sont
   conservés, sans doublon, et un nouveau passage ne change rien.
5. Avec `--word-dos` : fichiers des six disquettes regroupés dans
   `C:\INSTALL\WORDDOS` (copie récursive, homonymes différents refusés, relecture
   de C: comparée aux disquettes), si bien qu'INIT ne demande aucun changement de
   disquette. Les disquettes doivent porter `INIT.EXE`, `INIT1.EXE`, `WORD60.INF`
   (SETUP_60) et `WORD.EX$` (PROG_60).
   DOS démarre en page 850 avec `KEYB FR` et lance `INIT`. Chaque écran est
   reconnu par son texte, chaque choix par son libellé en surbrillance dans la
   mémoire vidéo (table `WORD_DOS_SCREENS` de `scripts/installation.py`). La
   surbrillance est une barre d'une seule couleur, différente de celle du cadre,
   qui déborde du libellé, dans un cadre `┌…└` : une couleur seule ne suffit pas.
   Elle doit être unique ; chaque flèche doit amener le libellé voisin, et leur
   nombre est borné par la taille de la liste :
   installation sur disque dur dans `C:\WORD`, pays et clavier France, clavier
   « pour résidents » (lu par le BIOS, donc par Épaudos), VGA, mode **Texte**
   (en mode graphique, Épaudos se met en veille), sans test des caractères
   spéciaux, orthographe, synonymes, conversions, aide, didacticiel et polices
   TrueType, papier A4. Le nom est saisi sans accents (INIT le met en majuscules).
   INIT ne modifie ni `CONFIG.SYS` ni `AUTOEXEC.BAT` et n'écrit pas de `.W60`.
   Une demande de disquette (« Insérez la … dans l'unité …, puis appuyez sur
   ENTREE », texte de `WORD60.INF`), un message d'erreur d'INIT, un choix absent ou
   une surbrillance incohérente arrêtent l'installation ; une simple mention d'une
   unité ou du verbe insérer dans un écran d'information ne l'arrête pas.
6. Copie d'Épaulard et de ses réglages, réglage de WIN.INI, de SYSTEM.INI (son
   A220 I5 dans `[sndblst.drv]` et `[sndblst2.drv]`, ports), des alias ASCII des groupes Réseau et Démarrage, copie de `C:\EPAUDOS.COM`. Écriture de `CONFIG.SYS` et `AUTOEXEC.BAT` depuis les modèles `dos/`.
7. Démarrage réel de Windows par le choix 2 du menu, réception de l'annonce de
   disponibilité d'Épaulard sur COM1, fermeture de Windows et retour à `C:\>`.
8. Démarrage du choix 1, chargement d'Épaudos, annonce « Épaudos prêt » reçue
   sur COM1 et invite `C:\>`. Avec `--word-dos`, `WORD` est lancé : l'écran de
   Word en mode Texte doit être lu par Épaudos sur COM1, puis Alt+F4 ramène à `C:\>`.

Groupes Réseau et Démarrage : WfW FR `/H` écrit leurs noms accentués en ANSI
dans PROGMAN.INI. En page 850, les fichiers sur la FAT s'appellent déjà
`RESEAU.GRP` et `DEMARRAG.GRP` ; l'installateur aligne PROGMAN.INI sur ces noms
ASCII (et crée l'alias si le fichier accentué existe). Sans cela, le Gestionnaire
de programmes ouvre une boîte d'erreur avant le lancement du lecteur.

## Résultat

Le disque final est une image brute de **500 Mo**, compatible avec la géométrie
486 du projet. Ses fichiers de démarrage sont ceux de la VM de référence :

- Menu non vocalisé : **1** = MS-DOS avec Épaudos (défaut après 10 s),
  **2** puis Entrée = Windows pour Workgroups 3.11 avec Épaulard.
- `CONFIG.SYS` : HIMEM de Windows, EMM386 NOEMS, `DOS=HIGH,UMB`, pays 033 et
  DISPLAY.SYS en page 850, POWER.EXE (processeur au repos), IFSHLP.SYS.
  Pas d'ANSI.SYS.
- `AUTOEXEC.BAT` : SMARTDRV en lecture seule sur C:, page de code 850,
  `KEYB FR`, `BLASTER=A220 I5 D1 H5 T6`, puis `C:\EPAUDOS.COM` ou `WIN` selon le menu.
- Braille : COM3 Baum (base 03E8, IRQ 9) configuré dans WIN.INI et SYSTEM.INI.
  Côté hôte, `nix run .#prepare` construit `tools/braille` et `scripts/pc486` lance
  alors la passerelle BrlAPI sans option ; voir [le guide braille](braille.md).
- Windows : sources dans `C:\WININST`, Épaulard par `load=` dans WIN.INI,
  Sound Blaster 1.5 et AdLib (A220 I5), souris PS/2 ou série.
- Word 6.0c dans `C:\WINWORD`, groupe Microsoft Office.
- Avec `--word-dos` : Word 6.0 pour DOS dans `C:\WORD`, lancé par `WORD` à l'invite
  du choix 1 ; `PATH C:\WINDOWS;C:\DOS;C:\WORD`.

Différences connues avec `vm/win311.img` : `NORMAL.DOT`, `WINWORD.OPT` et
`PERSO.DIC`, que Word crée à son premier lancement ; les fichiers de sauvegarde
(`CONFIG.OLD`, `CONFIG.UMB`, `MEMMAKER.STS`…) et les documents de l'utilisateur ne
sont pas recréés ; la taille du fichier d'échange dépend de la place libre. La VM
de référence n'a pas les outils du disque supplémentaire : `--suppl` les ajoute.

Aucun disque D: partagé n'est attaché pendant l'installation. Les processus et les
sockets de l'installateur sont privés ; il ne lance ni n'arrête les VM gérées par
`scripts/pc486`. Les supports d'origine restent intacts.

## Lancer la nouvelle installation

Pour l'essayer sans toucher à `vm/win311.img`, l'utiliser comme disque d'essai :

```sh
PC486_DEV_DISK="$PWD/work/installations/nouveau-pc.img" \
  scripts/pc486-dev pc486 start --qemu --headless
```

Pour qu'elle devienne la VM principale, arrêter la VM, mettre de côté l'ancien
disque puis déplacer le nouveau :

```sh
scripts/pc486 status                      # VM arrêtée
mv vm/win311.img work/sauvegardes/win311-avant-remplacement.img
mv work/installations/nouveau-pc.img vm/win311.img
scripts/pc486 start
```

Le profil 86Box utilise les chemins du modèle `86box/86box.cfg` et ses ROM
installées séparément. D: `vm/transfert.img` n'est attaché, sous 86Box comme sous
QEMU, que si cette image existe ; l'installateur ne la crée pas. La CMOS de
référence `86box/nvr/hot433a_v451pg.nvr.ok` laisse le BIOS détecter les disques : elle
convient avec ou sans D: (voir le README). Le script n'installe pas les paquets de
l'hôte et ne télécharge pas de ROM.

## Fin, erreurs et diagnostics

Le disque est travaillé dans un dossier privé `.pc486-install-*` à côté de la
sortie. Il n'est rendu disponible sous le nom demandé qu'après les vérifications.
Cette opération refuse également un fichier créé entre-temps par un autre processus.

En cas d'échec ou de Ctrl+C, QEMU est fermé et le disque partiel est conservé dans
ce dossier, avec les derniers écrans texte (`dos-screen.txt`,
`supplementaire-screen.txt`, `windows-screen.txt`…), une capture en cas d'échec,
les journaux QEMU, le journal COM1 (`verification-com1.log`) et les empreintes
des supports (`supports.json`). Aucun résultat incomplet n'est publié sous le nom
final. Il n'y a pas de reprise d'une installation interrompue : corriger la cause
et relancer vers une sortie absente.

Le délai maximal est de 20 minutes **par étape VM** ; `--timeout 1800` l'augmente.
Un écran inattendu reste en attente jusqu'au délai : le script ne valide pas des
questions qu'il ne reconnaît pas. La validation COM1 prouve le chargement des
lecteurs, pas la qualité d'une voix ni le fonctionnement d'une plage physique.
Ces essais restent à faire dans la session utilisateur.

Les dossiers temporaires, le disque final, les copies des supports et les journaux
contiennent des données locales. Ils doivent rester hors de la publication des
sources ; `.gitignore` les exclut. Les empreintes permettent de rapprocher un essai
de ses supports, elles ne constituent pas un catalogue de téléchargements.
