# Synthé3 embarqué : début du port DOS et Win16

Ce répertoire contient une **copie locale** du moteur Synthé3-ng et un premier
port autonome pour DOS. La phrase est transformée en son dans la VM, pendant que
sa Sound Blaster joue les blocs déjà calculés. Aucun calcul de voix n'est délégué à Linux, Apolo-emu,
Speech Dispatcher ou un lecteur d'écran moderne.

Le port ne modifie ni Épaulard, ni Épaudos, ni leurs installations. Il n'est pas
encore un synthétiseur résident, une DLL Win16 ou un remplacement de COM1.
Le dépôt original `../synthe3-ng` reste en lecture seule : il n'est nécessaire
ni pour compiler ce port, ni pour exécuter ses tests.

L'[analyse d'architecture](ARCHITECTURE.md) prépare l'intégration Apollo/DOS/Win16.
La [méthode de mesure et l'optimisation exacte des fenêtres](MESURES.md) distinguent
maintenant génération PCM sans fichier, lecture et activité CPU estimée. Les
anciens temps de création de WAV ci-dessous sont conservés comme tels.
La première comparaison sur trois passages DX4/100 donne **756,7 → 602,7 ms**
pour phonétisation et génération PCM pure, sans différence PCM dans les 32 cas
de non-régression. L'activité estimée hors attente passe de **25,0 à 17,9 %**
sur le texte mesuré ; cela ne constitue pas une mesure CPU matérielle.
La deuxième étape (profil, noyaux par période, arithmétique entière) ramène la
génération à **231 ms sur DX4/100** avec un PCM identique, et à **902 ms sur
486SX/25 sans coprocesseur**, où la lecture directe tient le temps réel.

## Intégration aux lecteurs d'écran

Épaudos et Épaulard parlent par Synthé3 sur la Sound Blaster, sans Apollo, sans
liaison série vers l'hôte ni composant moderne dans la machine. C'est la synthèse
par défaut ; l'Apollo II sur COM1 se choisit sans réinstaller.

| Fichier dans la VM | Rôle |
| --- | --- |
| `C:\S3RES.EXE` | résident DOS : protocole Apollo local, moteur, Sound Blaster (DMA direct) |
| `C:\EPAULARD\EPAUS3.DLL` | même moteur pour Épaulard, sortie `waveOut` du pilote de Windows |
| `C:\EPAUDOS.INI` | réglages d'Épaudos ; `Synthese=Synthe3` (défaut, aussi sans clé) ou `Synthese=Apollo` |

Le choix se fait dans Épaudos : Inser+E, réglage « Synthèse », puis Entrée
(enregistré dans `EPAUDOS.INI`), et prend effet au redémarrage. `AUTOEXEC.BAT`
(modèle `dos/AUTOEXEC.BAT`) lance `LH C:\S3RES.EXE` avant SMARTDRV, puis
`LH C:\EPAUDOS.COM` dans les deux branches du menu ; S3RES lit le même fichier et ne
reste pas résident avec `Synthese=Apollo` (`S3RES /F` : toujours). Sans clé et sans
S3RES résident (pas de Sound Blaster), Épaudos parle à l'Apollo II. Épaulard a son
propre réglage « Synthèse » (comme Épaudos par défaut, Synthé3 ou Apollo), appliqué
aussitôt ; voir `docs/reglages.md`.

Mémoire (VM de référence, 117 Kio de mémoire supérieure avec `NOEMS`, mesuré par
`MEM /C` sous QEMU) : S3RES occupe 95 Kio, dont 62 Kio de données de voix. Chargé
après SMARTDRV, il ne trouvait que 74 Kio libres en haut et retombait en entier en
mémoire conventionnelle (465 Kio libres). Chargé en premier : 2 Kio en bas (l'anneau
DMA), SMARTDRV et Épaudos en bas, 528 Kio libres. Avec l'Apollo, S3RES n'est pas
résident : SMARTDRV et Épaudos tiennent en haut, 603 Kio libres. Placer les données
de voix en XMS (environ 50 Kio gagnés en haut) reste possible mais demanderait des
copies XMS depuis l'interruption d'horloge, à protéger contre les appels XMS en cours
(SMARTDRV) : non fait. Sous Windows, Épaulard trouve S3RES
(`[Parole] Synthese=Auto`) et charge EPAUS3.DLL. Retrait : Synthèse « Apollo »,
redémarrer, puis effacer au besoin `S3RES.EXE` et `EPAUS3.DLL` ; aucun autre fichier
n'est modifié.

Débit : `@W1..9` des lecteurs → débit Synthé3 3, 5, 6, 8, 9, 11, 12, 14, 15
(`rates` de `src/ap.c` ; facteur 2^(1/6) par pas du moteur) : W1 au débit naturel,
W3 (défaut) 1,4 fois plus vite, W5 deux fois, W9 quatre fois. L'ancienne
correspondance (débit = W) plafonnait à deux fois le débit naturel.

Volume : réglage « Volume » d'Épaudos et d'Épaulard (1 à 8, `@A` de l'Apollo II).
Synthé3 applique 3 dB par niveau autour du niveau d'origine (4) ; au-delà de 6, les
crêtes sont écrêtées (la voix plafonne vers −10 dB de la pleine échelle).

S3RES lit `BLASTER` (`A`, `I`, `D`) ; des arguments `A220 I5 D1` les remplacent.
Il refuse une adresse, une IRQ (3, 5, 7, 9, 10) ou un DMA 8 bits (0, 1, 3) hors des
valeurs connues, un DSP absent ou de version < 2, et le chargement sous Windows.
`S3RES /U` le retire si aucun lecteur n'y est attaché. Sous Windows, la carte est
celle du pilote de Windows (`[sndblst.drv]` de SYSTEM.INI), indépendamment de S3RES.
Processeur : 386 ou mieux (noyaux 386), **sans coprocesseur**.

Architecture, choix de la variante et contraintes : [ARCHITECTURE.md](ARCHITECTURE.md),
« Intégration aux lecteurs ». Mesures : [MESURES.md](MESURES.md), « Troisième étape ».

Essais :

```sh
python3 -m unittest tests.test_synthe3 -v      # dont le protocole Apollo local (ap-test.c)
python3 -m unittest tests.test_epaulard -v     # dont Épaulard sur une fausse EPAUS3.DLL
PC486_S3_TESTS=1 python3 -m unittest tests.test_synthe3_dos_vm -v   # QEMU, ~1 min
PC486_S3_TESTS=1 python3 -m unittest tests.test_synthe3_win_vm -v   # QEMU, ~2 min
PC486_S3_86BOX=1 python3 -m unittest tests.test_synthe3_86box.Synthe3WindowsSoundTests -v
                                  # 86Box DX4/100 et SX/25, son enregistré, ~12 min
```

## Ce qui fonctionne

- Phonétisation française et données de la voix Michel reprises du moteur libre.
- Production de PCM **mono signé 16 bits à 10 000 Hz**, par blocs de taille choisie
  par l'appelant, sans allocation dynamique ni accumulation d'une phrase audio.
- Fenêtres de recouvrement des diphones en Q15 interpolé, gain, filtre historique
  approché en Q16 et saturation PCM, **écrits en assembleur 386, exécutés en mode
  réel 16 bits**. Aucun calcul flottant dans la boucle par échantillon.
- Conversion PCM 16 bits signé vers 8 bits non signé et programmation du DMA
  8237 également en assembleur ; sortie Sound Blaster 8 bits à 10 kHz.
- Lecture directe `S3DOS -s`, sans WAV intermédiaire, avec DMA auto-initialisé et
  deux demi-tampons de 512 échantillons. L'ISR, la publication des tampons et
  l'attente sans boucle active (`STI`/`HLT`) sont aussi en assembleur.
- Arrêt par Ctrl seul ou une touche ; détection d'un producteur trop lent,
  arrêt du DMA, puis reprise possible sur une nouvelle phrase.
- Référence C des noyaux pour vérifier les résultats de l'assembleur.
- Abandon des blocs en attente, reprise sur un nouveau texte et indices de mots
  en échantillons 32 bits. Ces indices indiquent ce qui est **produit**, pas encore
  ce qui a été joué par la carte.

La phonétisation et l'ordonnancement des périodes restent en C. Dans la
compilation de référence (`S3DOS.EXE`), les calculs de hauteur, de débit et de
période utilisent encore le coprocesseur x87 : elle exige un **386 avec 387 ou
un 486DX**. La variante expérimentale sans x87 (`S3DOSX.EXE`, options
`S3_BLOCK` et `S3_INTEGER_PERIOD`) fonctionne sur **486SX** et vise le 386 ;
voir [MESURES.md](MESURES.md) pour ses mesures et ses différences de PCM.

### Variantes sélectionnables à la compilation

| Option du moteur | Effet | PCM |
| --- | --- | --- |
| (aucune) | Référence : fenêtres préparées, appels par échantillon, x87 par période. | référence |
| `S3_SCALAR_WINDOW` | Ancien chemin scalaire (`s3_window`), conservé comme oracle. | identique |
| `S3_BLOCK` | Noyaux par période `s3_mix`/`s3_output_block` (`block.c` en C, `block.asm` en 386). | identique |
| `S3_INTEGER_PERIOD` | Période, positions et allongement en millièmes entiers, sans x87. | identique sauf égalités exactes d'arrondi (2 cas sur 45) |
| `S3_WINDOW_CACHE=n` | Cache LRU de n fenêtres (4 à 64, 512 octets chacune) au lieu des 4 historiques. | identique |
| `S3_PROFILE` | Compteurs et temps PIT par période pour `S3PROF`/`S3PROFI` (banc DOS). | identique |

`build.sh` compile toutes les combinaisons mesurées ; la référence n'est pas
modifiée par ces options.

## Compiler

Depuis la racine de pc486, avec l'Open Watcom déjà fourni par le projet :

```sh
bash synthe3/build.sh
```

Les sources UTF-8 sont converties dans le répertoire de travail. Les résultats
restent dans `work/dev/synthe3/build/` :

| Fichier | Fonction |
| --- | --- |
| `S3DOS.EXE` | Parler directement avec `-s`, ou écrire un WAV mono 16 bits. |
| `S3PLAY.EXE` | Jouer ce WAV avec le même pilote à double tampon. |
| `CHECKASM.EXE` | Vecteurs de calcul utilisant les noyaux assembleur. |
| `CHECKC.EXE` | Mêmes vecteurs utilisant la référence C. |
| `S3DOS.MAP` | Carte mémoire et symboles du programme DOS. |
| `S3FAULT.EXE` | Banc matériel : retard volontaire du producteur, arrêt et reprise. |
| `S3END.COM` | Signal de fin du banc 86Box sur son COM1 privé. |
| `S3CLOCK.EXE` | Qualification de l'horloge PIT, sans changer sa fréquence. |
| `S3BENCH.EXE` / `S3BASE.EXE` | Mesures sans WAV et A/B optimisé/scalaire. |
| `S3WEIGH.EXE` | Comparaison exacte des fenêtres préparées et scalaires sous DOS. |
| `S3PROF.EXE` / `S3PROFI.EXE` | Même banc avec profil par période (référence, puis variante rapide). |
| `S3BLKC.EXE` / `S3BLKA.EXE` | Banc des noyaux par période, C puis assembleur, x87 conservé. |
| `S3INTW.EXE` / `S3INTA.EXE` / `S3INTAC.EXE` | Banc sans x87 : entier seul, entier + noyaux par période, + cache LRU 16. |
| `S3INTW2.EXE` | Sonde de bruit de disposition : source de `S3INTW`, options de compilation de la référence. |
| `S3DOSX.EXE` | Programme autonome sans x87 (`-s`, WAV), pour 486SX et 386. |
| `CHKBLKC.EXE` / `CHKBLKA.EXE` | Noyaux par période contre les noyaux scalaires, C puis assembleur. |

La compilation n'accède à aucun disque de VM. Elle utilise le modèle large,
les caractères signés (`-j`, indispensable aux sentinelles phonétiques) et place
les gros tableaux en mémoire far (`-zt=1024`) pour respecter la limite de DGROUP.
Ne pas modifier les copies de sources dans `work/`.

Les tables Q15 et CP850 sont versionnées. Pour les régénérer :

```sh
python3 synthe3/gen_tables.py
```

## Essai dans DOS

Les exécutables doivent être placés sur un disque d'essai arrêté, sans écriture
sur un disque utilisé par une VM. Le test automatique ci-dessous prépare sa propre
copie et ne demande aucune installation dans le disque principal.

Depuis l'invite de la machine d'essai :

```dos
SET BLASTER=A220 I5 D1 H5 T6
S3DOS -s Bonjour Michel.
```

Pour les accents et les phrases qui dépassent la ligne de commande DOS :

```dos
S3DOS -s -f TEXTE.TXT -r 3 -p 5
```

Pour conserver le son dans un fichier et le rejouer :

```dos
S3DOS -f TEXTE.TXT -o MICHEL.WAV
S3PLAY MICHEL.WAV
```

Le texte est en **CP850 par défaut**, converti en Latin-1 pour le moteur.
L'option `-l` accepte directement le Latin-1. Le fichier contient au plus
240 octets, sans octet nul. Les octets Latin-1 EDh et F8h sont réservés aux
marques internes et refusés ; les caractères CP850 sans équivalent Latin-1
deviennent des espaces. Aucun décodage UTF-8 n'est inclus pour l'instant.

Le débit `-r` et la hauteur `-p` acceptent 0 à 15. Les défauts sont 3 et 5,
comme le moteur actuel de Synthé3-ng. Une touche interrompt la génération d'un
WAV ; la lecture directe ou depuis WAV reconnaît aussi Ctrl seul. Le code de
sortie est 2 après interruption, 0 en cas de succès, 1 en cas d'erreur et 3 en
cas de sous-alimentation audio. `-s` et `-o` sont incompatibles.

`S3PLAY` accepte le WAV de `S3DOS`, avec en-tête PCM de 44 octets, 10 kHz,
mono signé 16 bits. Le pilote autonome prend les paramètres `A`, `I`, `D` dans
`BLASTER` : base 210h à 280h par pas de 10h, IRQ 5 ou 7, DMA 1 ou 3.
Le réglage pc486 habituel est A220/I5/D1. Le tampon DMA ne franchit pas 64 Kio.
Le programme restaure le vecteur d'interruption et le masque du PIC après lecture.
Le mode audio **refuse de s'exécuter sous Windows**, où un pilote différent est
requis ; la simple génération d'un WAV n'accède pas à la carte.

La commande `S3DOS` mesure le temps de phonétisation, calcul et écriture du WAV,
en ticks avec leur fréquence ; elle ne mesure pas la latence acoustique.
En lecture directe, elle affiche les nombres d'échantillons produits et joués,
le nombre de sous-alimentations et l'état final.

### Double tampon et limites de l'arrêt

Le DSP doit être de version 2.0 ou ultérieure. Le DMA boucle sur 1 024 octets et
interrompt le processeur tous les 512 octets, soit **51,2 ms**. Le premier plan
calcule le demi-tampon libéré pendant que l'autre est joué. L'ISR ne fait aucun
calcul de voix, appel DOS ou accès au x87. Une publication atomique rend un bloc
disponible seulement après sa conversion complète. Si le suivant manque à son
échéance, l'ISR masque le canal DMA et signale l'erreur plutôt que de continuer
à boucler sur les anciennes données.

Le dernier demi-tampon est complété de silence, au plus 51,2 ms ; ce silence
n'entre pas dans les statistiques. Le compteur joué est validé à chaque IRQ :
après interruption, il ne compte pas la partie déjà consommée du bloc courant.
Il ne constitue pas encore un système de marques de parole à l'échantillon près.
Le test clavier est fait entre les productions de blocs : sa réactivité dépend
aussi de leur temps de calcul et des buffers audio de l'émulateur.

Ce pilote travaille exclusivement au premier plan. Pendant l'attente audio, il
endort le processeur jusqu'à une interruption, sans occuper une boucle active.
Il ne faut pas appeler `sb_run` depuis une ISR ou avec les interruptions masquées.

## Vérifications

```sh
python3 -m unittest tests.test_synthe3 -v
PC486_S3_TESTS=1 python3 -m unittest tests.test_synthe3_vm -v
PC486_S3_86BOX=1 python3 -m unittest tests.test_synthe3_86box -v
python3 synthe3/corpus.py          # WAV comparables et mesures PCM des variantes
python3 synthe3/banc86box.py --cpu dx4-100 S3BENCH.EXE S3BLKA.EXE S3INTA.EXE
python3 synthe3/banc86box.py --cpu sx-25 --verif 'S3DOSX.EXE -s -f C:\TEXTE.TXT' S3INTA.EXE
```

`corpus.py` compile les variantes nativement et écrit, pour 45 cas, un WAV par
variante dans `work/dev/synthe3/corpus/wav/` (même texte, même débit/hauteur,
même gain) avec `comparaison.json` : longueur, égalité, rapport signal/écart,
écart maximal, écrêtage, crête. Ces mesures sont numériques ; elles ne jugent
pas l'intelligibilité, qui demande l'écoute des fichiers. `banc86box.py` lance un
lot de variantes sur une copie privée sous 86Box (DX4/100 ou 486SX/25) et
résume les médianes ; l'empreinte du PCM du banc DOS (`cd5146b4`) est celle des
WAV natifs du même texte.

Les tests natifs compilent les vraies sources en Linux 32 bits avec Open Watcom,
dans un répertoire temporaire. Ils vérifient les empreintes de la copie amont,
les tables, la conversion CP850, les limites d'entrée, l'annulation et la reprise,
et l'absence de changement du PCM selon le découpage en blocs de 1, 127 ou
512 échantillons. Le PCM est comparé à celui du moteur original copié, pour
six textes/réglages : durée identique, rapport signal/erreur supérieur à 45 dB,
écart maximal inférieur à 100 unités sur 32768. Il s'agit de la sortie native
10 kHz, sans le DSP moderne de Synthé3-ng.
Les noyaux vérifient aussi les 65 536 valeurs d'entrée possibles du gain et de
la saturation, face à l'arrondi flottant antérieur. Le produit du filtre utilise
64 bits dans l'assembleur pour éviter le débordement sur les fortes transitions.

Le scénario QEMU prend le verrou du disque source avant copie et refuse une source
ouverte par une VM. `PC486_S3_SOURCE=/chemin/image.img` permet de choisir une autre
image DOS installée, arrêtée. Il travaille exclusivement dans un nouveau dossier
`work/vmtest/synthe3/essai-*/`, sans D:, réseau, lecteur d'écran ni périphérique
audio physique. L'audio Sound Blaster est capturé en WAV par QEMU. Les résultats
et l'image privée restent dans ce dossier pour inspection.
Il vérifie aussi la lecture directe sans création de WAV, l'arrêt par Ctrl et
la reprise, les compteurs produit/joué et un retard de 250 ms du producteur.
Ce dernier doit produire une erreur de sous-alimentation, puis une lecture
suivante doit réussir ; le masque PIC et le vecteur IRQ doivent être restaurés.

Le banc 86Box utilise un **DX4/100, 16 Mio et SB16**, selon le modèle du dépôt,
sur une copie privée sous `work/vmtest/synthe3/86box-*/`. La CMOS aussi est copiée.
Son affichage Xvfb, son D-Bus et son COM1 sont privés ; sa sortie OpenAL est
nulle. Il n'ouvre aucune fenêtre dans la session utilisateur, ni plage braille,
ni synthèse hôte. Deux lectures directes doivent finir sans sous-alimentation.
Un second essai (`Synthe3SxTests`) configure la même machine en **486SX/25 sans
coprocesseur** et exige que `S3DOSX -s` joue deux fois la phrase sans
sous-alimentation, avec le même PCM.

### Résultat du premier essai, 31 octobre 2026

- Compilation DOS réussie, avertissements traités comme erreurs ; `S3DOS.EXE`
  fait environ 94 Ko et `S3PLAY.EXE` 28 Ko.
- Sous QEMU, CPU déclaré 486, les vecteurs des noyaux C et assembleur donnent
  le même résultat `2c68596f`.
- « Bonjour Michel. Un élève prend un café. » produit **33 380 échantillons**,
  soit **3,338 secondes**, en **274 ms**, écriture du WAV comprise.
- Le WAV est ensuite joué par la Sound Blaster émulée ; la capture audio contient
  un signal, et les programmes reviennent au DOS sans erreur.
- Le scénario final vérifie aussi le refus d'un canal DMA réservé, l'interruption
  de la lecture par Échap et une nouvelle lecture réussie après l'arrêt.
- Sur quatre textes de référence au débit 3/hauteur 5, le rapport signal/erreur
  numérique du moteur adapté est d'environ 55 à 59 dB.

QEMU n'est pas bridé à une fréquence de 486 : **ces temps ne prouvent pas le
temps réel sur un vrai 486DX/100 ou sous 86Box à sa vitesse de référence**.
La capture n'est pas une validation d'écoute par l'utilisateur.

### Lecture continue sur DX4/100 émulé

Après ajout du double tampon, le même texte a été essayé sous **86Box DX4/100** :
génération et écriture du WAV en **933 ms** pour 3,338 s de voix, puis deux lectures
directes de 33 380 échantillons sans sous-alimentation. La boucle audio n'attend
plus la génération de toute la phrase. Ce sont des mesures dans l'invité émulé,
pas sur un PC physique. Le banc ne constitue pas une validation d'écoute.

Le banc des noyaux conserve la signature `2c68596f` pour le fenêtrage/filtre et
ajoute `5eb7612d` pour le gain et la saturation sur toutes les entrées 16 bits.

## Travail restant

1. Écoute comparative et réglage de la correspondance hauteur Apollo → Synthé3
   (provisoire : hauteur = F − 3) ; débit : table `rates`, à confirmer à l'écoute.
2. Mesurer la latence acoustique réelle (capture synchronisée à la demande) ;
   aucun essai sur 386 ; EPAUS3.DLL avec Épaulard réel (pas seulement la sonde)
   sous 86Box.
3. Expliquer la sous-alimentation isolée vue une fois sous QEMU (DOS) et celles
   observées sous Windows pendant une fenêtre DOS plein écran.
4. Paquet pour une machine réelle (hors installateur) : copie des trois fichiers
   et du modèle `AUTOEXEC.BAT` documentée, pas encore d'archive publiée.

## Provenance et licences

La copie provient du dépôt local Synthé3-ng, révision
`b20e178444106d4a4526f2cf09eae642af1e186e`. Les fichiers de référence dans
`vendor/` sont copiés **sans modification** ; leur origine et leurs empreintes
SHA-256 figurent dans [provenance.json](vendor/provenance.json).

Le moteur historique est de **Michel Morel**, copyright 1985–2015, CRISCO,
université de Caen, maintenu par **Shérab (Sébastien Hinderer)**. Son portage C
est repris de Synthé3-ng. La source historique est
[acceslibreinfo/synthe3](https://github.com/acceslibreinfo/synthe3), révision
`8e601dc1895d71c0150c39ee1d10f2e04a291fe8` indiquée par Synthé3-ng.

Le moteur garde ses mentions **LGPL-2.1-or-later** ; les adaptations et nouveaux
fichiers pc486 sont sous **GPL-3.0-or-later**. Les données vocales gardent les
attributions et la licence de la distribution amont, qui ne leur attribue pas de
licence distincte. Les notices [de provenance](vendor/UPSTREAM.md),
[de licence](vendor/LICENSE.md) et les textes [GPL](vendor/LICENSE-GPL) et
[LGPL](vendor/LICENSE-LGPL) sont conservés. Voir aussi le
[registre des éléments tiers](../LICENSES/README.md).

La programmation DSP/DMA suit le
[guide matériel Sound Blaster de Creative](https://www.ardent-tool.com/sound/Sound_Blaster_HW_Programming_Guide_1st.pdf).
Le code du pilote et des noyaux assembleur est propre à ce port ; aucun pilote
Creative binaire n'est embarqué.
