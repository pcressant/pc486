# Installation neuve depuis des supports locaux

`scripts/installer` crée un PC sous **MS-DOS 6.22 français** (clavier français,
page de code 850) avec **Épaudos**, lecteur d'écran DOS du projet, puis **Windows pour
Workgroups 3.11 français** avec **Épaulard** et la plage braille Baum sur COM3.
Selon les supports fournis, il ajoute les **outils du disque supplémentaire** de DOS,
**Word 6.0c**, **Excel 5**, **Access 2.0**, **Ami Pro 3.01**, **Word 6.0 pour DOS**
(mode Texte, lu par Épaudos), **Netscape Navigator 3.01**, **Communicator 4.08**,
**Eudora Light 3.06**, le lecteur de CD-ROM et le réseau isolé (options ci-dessous).
Le disque obtenu ne dépend que des sources du dépôt et des supports d'installation :
aucune image existante n'est lue.

Le code publié ne contient ni image Microsoft ou d'un autre éditeur, ni archive de
ces logiciels. L'installateur n'utilise pas le réseau et ne télécharge rien.

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

Préparer les supports que l'on est autorisé à utiliser. Chaque option accepte un
ZIP ou un dossier ; les images sont lues dans l'archive sans l'extraire, et un ZIP
suffit (inutile de garder ses images à côté).

- DOS : `DISK1` à `DISK3` (`.IMG` ou `.IMA`, zéros et casse indifférents).
- Windows : `DISK1` à `DISK9`.
- Facultatif, `--suppl` : le disque supplémentaire de MS-DOS 6.22 (édition anglaise
  « Supplemental Disk », la seule publiée), en image seule ou dans un ZIP ou dossier,
  par exemple le ZIP de DOS où il s'appelle `Dsuppl.IMA` : il est reconnu par son
  contenu (`SETUP.BAT` et outils), pas par son nom.
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
scripts/installer --dos media/msdos622/msdos622fr.zip --windows media/wfw311/wfw_3-11_fr.zip \
  --suppl media/msdos622/msdos622fr.zip --check
```

Cette commande vérifie les prérequis, le nombre et la taille des disquettes,
leur lecture par mtools et les fichiers caractéristiques du profil. Elle ne crée
aucun disque et ne démarre pas QEMU. Des doublons ambigus ou une disquette absente
font échouer la vérification.

## Installer la machine de référence

Machine complète, avec tous les logiciels pris en charge, le CD-ROM et le réseau
isolé (supports locaux rangés dans `media/`, en ZIP) :

```sh
scripts/installer \
  --dos media/msdos622/msdos622fr.zip \
  --windows media/wfw311/wfw_3-11_fr.zip \
  --suppl media/msdos622/msdos622fr.zip \
  --word media/word6-win/msword6cfr.zip \
  --excel media/excel5c-fr/msexcel50c_525hd_win31x_fr.zip \
  --access media/access2-fr/msaccess20_525hd_win31x_fr.zip \
  --access-an2000 media/access2-fr/acc2date.zip \
  --amipro media/amipro-fr/lotusamifr.zip \
  --netscape media/netscape3-fr/netscape301_win31x_fr.zip \
  --communicator media/communicator408-fr/netscc16fr408.zip \
  --eudora media/eudora306/eul306.zip \
  --word-dos media/word6-dos-fr/msword60_525hd_dos_fr.zip \
  --cdrom media/udvd2/udvd2.zip \
  --reseau media/tcpip32/tcp32b.zip \
  --name 'Utilisateur' \
  --output vm/win311.img
```

Chaque logiciel est facultatif : retirer son option suffit. Seuls `--dos` et
`--windows` sont obligatoires ; `--access-an2000` suppose `--access`. Une seule
version d'Excel s'installe : 5.0c (ci-dessus) ou 5.0 (`media/excel5-fr`). Pour
Ami Pro, seul `lotusamifr.zip` est pris en charge, pas l'archive « bonus ».
Ajouter `--check` vérifie d'abord tous les supports sans créer de disque. Sans
`--reseau`, Netscape, Communicator et Eudora s'installent mais n'ont aucun réseau.
Avec `--reseau`, démarrer par `scripts/pc486 start --reseau` (voir [reseau.md](reseau.md)).

Le disque de sortie doit porter l'extension `.img` et **doit être absent** : un
fichier existant, un dossier ou un lien symbolique, même cassé, est refusé.
L'installateur ne propose pas d'option pour écraser une installation. Sur une
machine déjà équipée, choisir une sortie sous `work/installations/` ; cela laisse
`vm/win311.img` intact.

Options :

| Option | Effet |
| --- | --- |
| `--name`, `--company` | Utilisateur et organisation de Windows, 30 caractères au plus. |
| `--suppl IMAGE` | Image, ZIP ou dossier (voir plus haut). Lance le `SETUP.BAT` d'origine du disque supplémentaire vers `C:\DOS` : tous les composants (AccessDOS, utilitaires clavier, DRVBOOT, outils de MS-DOS 5, fichiers réseau, MS-DOS Shell en VGA). |
| `--word DOSSIER` | Installe Word 6.0c dans `C:\WINWORD` avec les options par défaut (WordArt, Graph, éditeur d'équations, convertisseurs et filtres, aide), sans clipart. Les supports restent dans `C:\INSTALL\DISK1` à `DISK11`, comme dans la VM de référence. |
| `--excel DOSSIER` | Installe Excel dans `C:\EXCEL` avec les options par défaut. Deux supports sont reconnus, d'après leurs images : Excel 5.0c (11 disquettes 1,2 Mo, cabinets `EXCEL*.CAB`) ou Excel 5.0 (10 disquettes 1,44 Mo). Sources dans `C:\INSTALL\EXCEL\DISKn`. |
| `--access DOSSIER` | Installe Access 2.0 dans `C:\ACCESS` (10 disquettes de 1,2 ou 1,44 Mo), sources dans `C:\INSTALL\ACCESS\DISKn`. |
| `--access-an2000 DOSSIER` | Avec `--access` : correctif ACC2DATE de Microsoft. `MSABC200.DLL` va dans `C:\WINDOWS\SYSTEM`, `MSAJU200.DLL` dans `C:\ACCESS` ; les deux DLL sont lues dans le dossier, le ZIP ou l'`ACC2DATE.EXE` qu'il contient (ZIP autoextractible, jamais exécuté). |
| `--amipro DOSSIER` | Installe Ami Pro 3.01 (installation complète, monoposte, Windows) dans `C:\AMIPRO`. Les 8 disquettes sont regroupées dans `C:\INSTALL\AMIPRO` : l'installateur Lotus, sans mode silencieux, ne demande aucune disquette ; ses boîtes reçoivent les réponses d'`INSTAUTO.EXE` (voir plus bas). |
| `--netscape ZIP` | Installe Netscape Navigator 3.01 français 16 bits (dossier `N16FR31` du ZIP) dans `C:\NETSCAPE`, réglé pour `pc486 start --reseau` : proxy 10.0.2.2:8080, page d'accueil `http://pc486/`, courrier 10.0.2.2, signets pc486. Réponses d'`INSTAUTO.EXE`. |
| `--communicator ZIP` | Installe Netscape Communicator 4.08 français 16 bits (`netscc16fr408.zip`, auto-extracteur InstallShield) dans `C:\NETSCAPE\COMM`. Les mêmes réglages sont écrits par défaut pour tout profil dans `NETSCAPE.CFG` (voir plus bas) ; le profil lui-même est créé par l'assistant de Communicator au premier lancement (lu par Épaulard). |
| `--eudora ZIP` | Installe Eudora Light 3.06 (anglais, `eul306.zip`) dans `C:\EUDORA` : compte `pc486@10.0.2.2`, SMTP 10.0.2.2 (boîte locale de `--reseau`, mot de passe quelconque). Au premier lancement, Eudora ouvre ses Options, puis signale une table des matières de la boîte In à recréer (« Create new ») : fichiers livrés ainsi. |
| `--word-dos DOSSIER` | Installe Word 6.0 pour DOS dans `C:\WORD` en mode Texte, sans gestionnaire d'imprimante ni de souris, avec le gestionnaire de clavier « pour résidents » ; ajoute `C:\WORD` au PATH. Les fichiers des disquettes restent dans `C:\INSTALL\WORDDOS` pour relancer `INIT` (imprimante, par exemple). |
| `--cdrom FICHIER` | `UDVD2.SYS` ou le paquet `udvd2.zip` de FreeDOS (`media/udvd2/`) : pilote ATAPI dans `C:\DOS`, `MSCDEX` en E:. Sans lecteur (lancement sans `--cdrom`), UDVD2 ne se charge pas et MSCDEX est sauté. Voir [reseau.md](reseau.md). |
| `--reseau FICHIER` | `tcp32b.zip` (TCP/IP-32 3.11b) : carte NE2000 (0x300, IRQ10) et TCP/IP de WfW, adresse 10.0.2.15. `RESEAU.EXE`, avant `WIN`, retire le réseau de SYSTEM.INI quand la carte manque. Vérifié en démarrant Windows sans puis avec carte. Voir [reseau.md](reseau.md). |
| `--sans-braille` | Ne configure pas la plage. Par défaut, Épaulard utilise une plage Baum sur COM3, base 03E8, IRQ9 (`BraillePort=3`, ports de SYSTEM.INI), comme la VM de référence ; la plage n'est pas contactée pendant l'installation. `--braille`, ancienne option, reste accepté. |
| `--binaries DOSSIER` | Utilise des binaires déjà compilés : `EPAULARD.EXE`, `EPAUHOOK.DLL`, `EPAUDOS.COM` (et `INSTAUTO.EXE` avec `--word`, `--excel`, `--access`, `--amipro`, `--netscape`, `--communicator` ou `--eudora` ; `RESEAU.EXE` avec `--reseau`). Sinon `epaulard/build.sh` et `epaudos/build.sh` sont lancés. |
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
   Avec `--excel`, `--access` : même méthode, sources dans `C:\INSTALL\EXCEL` ou
   `C:\INSTALL\ACCESS`, journal `excel-instauto.log`, `access-instauto.log`.
   Avec `--amipro` : l'installateur Lotus pose ses questions. `INSTAUTO.EXE` les
   lit dans `[pc486]` de WIN.INI, `reponse1=Début du titre|touches`, `reponse2=…`,
   et joue chacune quand une fenêtre de ce titre est restée active une seconde :
   texte, `{TAB}`, `{ENTREE}`, `{ECHAP}`, `{ESPACE}`, `{BAS}`, une touche toutes les
   250 ms à la fenêtre qui a le focus. Les réponses (accueil, société, nom, monoposte,
   installation complète, Windows, `C:\AMIPRO`, fin, Quitter) sont dans
   `ACME['amipro']` de `scripts/installation.py`. Une boîte inattendue arrête la
   suite des réponses : l'étape échoue alors au délai, capture d'écran et journal
   conservés. Netscape, Communicator et Eudora (InstallShield) suivent la même
   méthode ; deux boîtes de même titre (« Question ») reçoivent chacune leur
   réponse, dans l'ordre. Leurs réglages (INI, signets) sont écrits après
   l'installation (`configure_product`).
   Communicator : `NETSCAPE.CFG`, lu au démarrage, contient des
   `PrefConfig.pref(…)` (valeurs par défaut, modifiables ; `pref` seul n'y est pas
   défini). Le fichier est décalé de 7 octets et sa première ligne porte le MD5 d'une
   clé fixe suivie du reste du fichier (`netscape_cfg`) : un fichier modifié sans
   recalcul est refusé (« netscape.cfg n'est pas valide »). Le générateur reproduit
   à l'octet près le fichier vide livré avec Communicator (test).
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
  `KEYB FR`, `BLASTER=A220 I5 D1 H5 T6`, puis `C:\EPAUDOS.COM`, suivi de `WIN`
  pour le choix Windows : Épaudos dort sous Windows et lit l'invite MS-DOS avec
  Épaulard ([invite MS-DOS](invite-dos.md)).
- Braille : COM3 Baum (base 03E8, IRQ 9) configuré dans WIN.INI et SYSTEM.INI.
  Côté hôte, `nix run .#prepare` construit `tools/braille` et `scripts/pc486` lance
  alors la passerelle BrlAPI sans option ; voir [le guide braille](braille.md).
- Windows : sources dans `C:\WININST`, Épaulard par `load=` dans WIN.INI,
  Sound Blaster 1.5 et AdLib (A220 I5), souris PS/2 ou série.
- Word 6.0c dans `C:\WINWORD`, groupe Microsoft Office.
- Avec `--excel`, `--access`, `--amipro` : Excel dans `C:\EXCEL`, Access dans
  `C:\ACCESS` (groupe Microsoft Office), Ami Pro dans `C:\AMIPRO` (groupe
  Applications Lotus).
- Avec `--netscape`, `--communicator`, `--eudora` : groupes « Netscape Navigator
  3.0 », « Netscape Communicator », « Eudora Light » ; à utiliser avec `--reseau` et
  `pc486 start --reseau`.
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

## ISO des supports décompressés

Pour installer ou réinstaller un logiciel à la main, depuis Windows ou DOS, les
mêmes supports peuvent être réunis dans une ISO locale, lue en E: par une VM dont
le CD-ROM est installé (`--cdrom`, ou `scripts/options-vm --cdrom`) :

```sh
nix run .#iso -- --sortie work/supports.iso \
  --dos media/msdos622/msdos622fr.zip --suppl media/msdos622/msdos622fr.zip \
  --windows media/wfw311/wfw_3-11_fr.zip --word media/word6-win/msword6cfr.zip \
  --word-dos media/word6-dos-fr/msword60_525hd_dos_fr.zip --reseau media/tcpip32/tcp32b.zip \
  --autres media
scripts/pc486 start --cdrom "$PWD/work/supports.iso"
```

Les options sont celles de l'installateur (`--excel`, `--access`, `--access-an2000`,
`--amipro`, `--netscape`, `--communicator`, `--eudora`…), toutes facultatives ; les
supports sont reconnus et vérifiés de la même façon. `scripts/iso-supports` fait la
même chose hors de Nix (mtools et xorriso nécessaires, 7zz et unar pour `.7z` et `.rar`). Le contenu des disquettes est
extrait, fichiers cachés compris, et rangé comme l'installateur le range dans
`C:\INSTALL` :

| Dossier de E: | Contenu | Programme à lancer |
| --- | --- | --- |
| `DOS622` | images `DISK1..3.IMG` (amorçage sur disquette), `SUPPL.IMG` et `SUPPL\` | `SUPPL\SETUP.BAT` |
| `WFW311` | les 9 disquettes réunies | `INSTALL.EXE` |
| `WORD6`, `EXCEL`, `ACCESS` | un dossier `DISKn` par disquette (ACME) | `DISK1\INSTALL.EXE` |
| `ACCESS\AN2000` | DLL du correctif ACC2DATE | à copier à la main |
| `AMIPRO`, `WORDDOS` | toutes les disquettes réunies | `INSTALL.EXE`, `INIT.EXE` |
| `WORDDOS\GRAMMAT` | Grammatik, installateur séparé (ignoré par l'installateur) | `INSTALLE.EXE` |
| `NETSCAPE`, `COMMUNIC`, `EUDORA` | dossier d'installation de l'archive | `SETUP.EXE`, `CC16F408.EXE`, `EUL306.EXE` |
| `TCPIP32` | TCP/IP-32 3.11b | « disquette du constructeur » |
| `AUTRES\…` | `--autres DOSSIER` : toute autre archive du dossier | selon le logiciel |

`--autres` ramasse, sous-dossiers compris, les `.zip`, `.7z` et `.rar` qui ne sont pas
déjà donnés par une option de produit (versions anglaises, utilitaires, FreeDOS…), pour
les essayer. Chaque archive devient un dossier de `AUTRES` nommé d'après son dossier
de `media/` (ou l'archive elle-même s'il en contient plusieurs), version et langue
gardées : `COM405EN`, `QEMM75FR`, `EXCEL5FR`… Les images de disquettes FAT et les ZIP
imbriqués sont décompressés dans un dossier de même nom ; les EXE autoextractibles et
les images de disque dur restent tels quels. Les noms longs sont ramenés en 8.3 ; les
caractères permis par DOS sont gardés (`NBWIN.EX!` de Norton, `WORD.EX$`), car un
installateur cherche ses fichiers sous leur nom exact. `NOMS.TXT`, dans le dossier de
l'archive, donne le nom d'origine de tout ce qui a été renommé ou décompressé.

`LISEZMOI.TXT`, à la racine, rappelle ce tableau. ACME lit l'emplacement de
chaque fichier dans sa liste : il veut un dossier par disquette ; SETUP de Windows,
Lotus et INIT trouvent tout dans un seul dossier. L'ISO contient des logiciels
tiers : elle est écrite dans la copie de travail, jamais dans `/nix/store`, et ne doit
pas être diffusée. La sortie existante n'est jamais remplacée.

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
