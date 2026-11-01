# Synthèse autonome : analyse et étapes vérifiables

Analyse des sources présentes le 31 octobre 2026. Ce document distingue les
comportements vérifiés dans le code, les essais exécutés et l'architecture proposée.
Le premier lot ne modifie ni les lecteurs, ni leur configuration, ni le moteur
original `../synthe3-ng`, ni le disque de référence.

## État vérifié

### Moteur

`src/engine.c` conserve la phonétisation du moteur copié dans `vendor/`. Il produit
des périodes de 0 à 255 échantillons dans un tampon fixe, puis `s3_read` en livre
des blocs choisis par l'appelant. Les données vocales et les tampons ne sont pas
alloués pendant la synthèse. Les entrées sont limitées à 240 octets Latin-1 ; EDh
et F8h servent aux marques internes. Une proposition Apollo peut être plus longue :
son découpage devra conserver l'ordre et les marques, sans tronquer à 240 octets.

Le x87 reste utilisé pour cinq grandeurs : hauteur et débit issus des tables,
position de lecture, allongement et moyenne pondérée des périodes. Cela comprend
deux divisions initiales par 1000, des multiplications et accumulations par période,
et le calcul/arrondi de la période résultante. Il n'y a plus de trigonométrie ni
d'opération flottante dans la boucle par échantillon. La compilation `-fpi87`
interdit actuellement le fonctionnement sur un SX sans coprocesseur.

Le noyau `s3_window` refait, pour chacune des quatre contributions et pour chaque
échantillon, la division donnant sa phase Q16 et l'interpolation d'une même fenêtre.
Le dénominateur est le minimum des périodes source et résultante : il reste fixe
pendant une période, et plusieurs contributions partagent souvent ce dénominateur.
C'est la première redondance à mesurer et à supprimer. Le gain, le filtre et la
saturation sont déjà en entier, avec un produit 64 bits pour le filtre.

La virgule fixe par période est une étape suivante : échelles signées, précision
des accumulations, arrondi de la période et dérive temporelle doivent être comparés
sur un corpus étendu. Supprimer le x87 en changeant simplement les types est exclu.

*Mise à jour, deuxième étape* : les coefficients étant des millièmes entiers, la
période et les positions sont tenues en entiers exacts (`S3_INTEGER_PERIOD`), et
les noyaux par période (`S3_BLOCK`) ont été mesurés ; voir [MESURES.md](MESURES.md).
La compilation de référence conserve le x87.

### Audio DOS

`src/sb.c`, `sbirq.asm` et `kernels.asm` utilisent un anneau DMA 8 bits de 1024
octets, partagé en deux moitiés de 512, à 10 kHz. Le DSP auto-initialisé interrompt
le CPU tous les 51,2 ms. Le producteur remplit la moitié libérée ; sa publication
est atomique. L'ISR ne synthétise pas et n'appelle pas DOS. Elle compte les blocs
terminés, arrête le DMA à la fin ou si la moitié suivante manque. Les statistiques
ne comptent pas le silence ajouté à la dernière moitié.

`sb_run` est un pilote de premier plan, qui alloue sa mémoire DOS et possède
temporairement le vecteur IRQ. Il ne constitue pas un TSR et ne doit pas être appelé
depuis une interruption. Il refuse Windows et attend par STI/HLT entre les blocs.
Sa détection de Ctrl est faite entre deux productions : ce n'est pas une garantie
de latence acoustique. Le compteur joué est quantifié à 512 échantillons.

### Mesures antérieures

Les 933 ms pour 33380 échantillons sous 86Box DX4/100 sont un **temps écoulé DOS
pour phonétisation, calcul, conversion de format et écriture du WAV**, mesuré avec
`clock()`. Ils ne constituent ni du temps CPU pur, ni une mesure de première parole.
Le banc QEMU est non bridé : il vérifie le fonctionnement et les régressions,
sans représenter la vitesse d'un 486. Les lectures directes réussies sous 86Box
établissent l'absence de sous-alimentation sur le seul texte essayé, pas sur tout
document ni toute machine physique.

## Protocole constaté

Sources consultées : `epaulard/src/parole.c`, `hook.c`, `epaudos/src/core.c` et le
paquet Apolo-emu réellement installé (`protocol.py`, `device.py`, `mapping.py`).
Apolo-emu reste en lecture seule ; son implémentation Python n'est pas embarquée.

| Élément | Sémantique à conserver |
| --- | --- |
| Texte | CP850 ; Épaulard convertit son ANSI en OEM avant émission. |
| Fins | CR ou `. , : ; ! ?`. L'accumulateur Apollo coupe aussi près de 250 octets, avec préférence pour un espace à partir de 220. Les paramètres doivent pouvoir arriver fragmentés. |
| Ctrl-X, 18h | Annuler immédiatement le son et purger propositions, commandes différées et échos ; conserver les réglages déjà appliqués. Une génération identifie les anciens buffers pour rejeter les retours tardifs. |
| `@W1`…`@W9` | Débit ; W3 est le défaut Apollo. Table `rates` de `ap.c` : W1 → 3 (débit naturel), W5 → 9 (×2), W9 → 15 (×4). |
| `@F0`…`@FF` | Hauteur sur un seul caractère hexadécimal ; F8 est le défaut Apollo. Les signes + et - existent dans Apolo-emu mais ne sont pas émis par les lecteurs actuels. |
| `@P0`, `@P1` | Ponctuation naturelle ou nommée. Épaulard développe déjà certains signes ; Épaudos les nomme lui-même. Épaudos n'émet actuellement pas `@P`. |
| `@V1`…`@V6` | Les lecteurs émettent ces choix. Le moteur local possède seulement Michel : accepter les numéros sans inventer d'autres voix, et documenter leur équivalence. `@V0`, silencieux dans Apolo-emu, ne doit pas perdre les échos. |
| `@E<c>` | Commande différée au début de sa proposition dans Apolo-emu sans extension word-echo. Les lecteurs l'envoient dans une proposition séparée **après le texte** : son effet vient donc après la restitution des propositions précédentes. Ce n'est pas une notification de fin du calcul PCM. |

L'extension hôte `burst-interrupt` est activée par le lanceur. Elle n'appartient
pas au protocole Apollo matériel ; sa politique d'interruption devra être étudiée
séparément. L'interruption explicite Ctrl-X reste prioritaire et indispensable.

Un défaut d'émission a été constaté : `voice()` d'Épaudos écrit la hauteur en
décimal, donc `@F10` au lieu de `@FA`. Le parseur Apollo consomme F1, puis voit le
zéro restant comme du texte. Le parseur local ne doit pas introduire une commande
ambiguë pour masquer ce défaut : sa correction ciblée nécessitera son propre test
dans le lecteur, lors de l'intégration. Ce premier lot ne le modifie pas.

## Architecture retenue pour la suite

1. **Protocole C borné**, indépendant du matériel : accumulateur, paramètres,
   file de propositions et marqueurs. Entrées CP850, sorties de texte vers le
   moteur et événements vers le transport. Ctrl-X invalide une génération entière.
2. **Transports séparés** : le transport série actuel reste disponible. Pour DOS,
   une interface résidente INT 2Fh ne fait que déposer/prendre des octets et signaler
   l'annulation, sans attendre le port ni calculer le son dans l'appel du lecteur.
   Pour Win16, une interface FAR explicite à un composant local évite d'utiliser
   une adresse DOS brute comme pointeur protégé.
3. **Moteur linguistique et PCM commun**, mono-instance par environnement, calcul
   borné en petits blocs. Les règles et données sont partagées entre les builds.
4. **Restitution** : Sound Blaster directe sous DOS réel, `waveOut` sous Windows.
   Les échos sont libérés par les événements de consommation, avec vérification de
   génération ; un reset ne doit jamais libérer une ancienne marque.

### DOS, TSR et Windows : contraintes établies

Épaudos est un COM à segment unique, avec ses piles d'interruption. Le moteur
Synthé3, ses données et son état far n'y entrent pas directement. Un résident
distinct devra posséder ses segments, sa pile et ses files. Le calcul ne doit pas
ouvrir de fichier ni allouer pendant une interruption ; la politique de pompage,
la réentrance et la sauvegarde x87 tant qu'il reste du flottant sont à valider.
Le rythme BIOS de 55 ms ne suffit pas à lui seul à garantir le remplissage de
demi-tampons de 51,2 ms : on ne réutilisera pas aveuglément la boucle actuelle.

Épaudos reçoit déjà INT 2Fh 1605h/1606h pour se mettre en veille pendant Windows.
Le résident de synthèse devra arrêter/libérer le DSP et le DMA avant que le pilote
Windows prenne la carte, et ne pas les rouvrir depuis une session DOS sous Windows.

Un relais DOS/Win16 existe **déjà pour Épaudos** : ED02h découvre une boîte en
mémoire conventionnelle ; `dosbox.c` utilise DPMI 0300h, convertit le segment avec
0002h, suit la VM clavier et échange deux files à producteurs uniques. Ses tests
ne prouvent pas que de nouvelles allocations, une autre machine DOS, des callbacks
réels ou des pointeurs far arbitraires se partageront de la même manière. Toute
extension à une nouvelle boîte nécessitera des sondes de visibilité, d'identité
de VM, de durée de vie et de remise à zéro, en mode 386 étendu.

L'architecture Windows privilégiée est donc un composant Win16 propre, utilisant
`waveOutPrepareHeader`/`waveOutWrite`, des buffers stables et des retours de
consommation `MM_WOM_DONE` ou `WHDR_DONE`. Les prototypes Win16 sont présents dans
l'Open Watcom du projet. Cela confirme la disponibilité de l'API, **pas encore
le fonctionnement d'un pilote particulier**. Un retour audio n'effectuera aucun
calcul lourd ; il rendra un buffer au producteur.

Les boîtes modales système bloquent parfois l'EXE d'Épaulard : la DLL actuelle y
parle déjà et pilote le braille. Une simple application de synthèse séparée ne
résout donc pas tout. Le pompage du composant vocal devra aussi être essayé dans
ces boîtes avant de choisir définitivement DLL seule ou service avec relais.

## Première étape implémentable sans toucher aux lecteurs

- Instrumenter le DOS autonome : génération PCM sans écriture de fichier, temps
  d'attente audio et temps total séparés, résolution et frais de mesure documentés.
- Profiler le noyau de fenêtres, puis déplacer la phase/interpolation redondante
  vers une préparation par période ; garder un chemin scalaire de comparaison.
- Exiger un PCM identique au port précédent pour cette optimisation exacte,
  en plus de la comparaison déjà disponible avec le moteur historique.
- Mesurer sous DX4/100 avant/après. Garder le x87 tant que son remplacement n'a
  pas de preuve numérique et linguistique suffisante. Le SX/25 reste expérimental.

Les horodatages internes de disponibilité PCM, soumission DSP et arrêt DSP ne
seront pas présentés comme des latences **audibles**. Une mesure acoustique exige
une capture synchronisée ou une boucle de retour, avec une référence temporelle
commune à la commande. Ni `WHDR_DONE` ni l'IRQ DMA ne mesurent seuls le haut-parleur.

## Intégration aux lecteurs (troisième étape, réalisée)

### Avant / après

Avant (seule chaîne possible) :

```
Épaudos (TSR DOS) ──┐
                    ├─ octets Apollo II (CP850, Ctrl-X, @W @F @V @P @E) ─ COM1 ─▶ Apollo II
Épaulard (Win16) ───┘   (en fenêtre DOS : Épaudos → boîte ED02h → Épaulard → COM1)   ou Apolo-emu
                                                                      échos @E ◀─ COM1
```

Après (choix explicite, l'Apollo II reste disponible) :

```
                       ┌──── EPAUDOS.INI Synthese=Apollo : chaîne d'origine, COM1 ───────────┐
DOS seul               │                                                                     │
  Épaudos (Synthe3) ─ API far (INT 2Fh D300h) ─▶ S3RES.EXE (résident, UMB si LH)                   │
                 dépose 1 octet / lit 1 écho │ file AP_LINK 1 Kio, Ctrl-X jamais refusé      │
                                             ├─ pompe sur INT 8h (pile privée, IF=1) :        │
                                             │    ap.c (Apollo local) → moteur Synthé3        │
                                             │    (bloc asm + période entière, sans x87)      │
                                             └─ IRQ SB : anneau DMA 4 × 512 o, 8 bits, 10 kHz │
                                                échos @E libérés quand l'IRQ a JOUÉ la marque │
Windows 3.1x                                                                                 │
  S3RES dort (INT 2Fh 1605h : DSP arrêté, vecteur IRQ rendu ; 1606h : repris)                │
  Épaulard ─ com_write/com_read ─▶ EPAUS3.DLL (même moteur, même ap.c, sortie waveOut)       │
    ▲  fenêtre DOS : Épaudos (mode invité) → ED02h → Épaulard → EPAUS3.DLL                   │
    └─ EPAUHOOK.DLL (boîte modale système) : EpSpeech → S3Write/S3Pump à sa minuterie        │
                       └─────────────────────────────────────────────────────────────────────┘
```

Les deux lecteurs ne sont pas réécrits : ils produisent toujours le même flux
Apollo II. Seul le transport change (`hw_tx`/`hw_rx` d'Épaudos, `com_write`/
`com_read` d'Épaulard). Le noyau commun est au niveau **source** (`engine.c`,
`block.asm`, `kernels.asm`, `weights.c`, `ap.c`) ; les sorties sont distinctes
(DMA direct sous DOS, `waveOut` sous Windows). Aucune mémoire n'est partagée
entre le résident DOS et la DLL.

### Protocole Apollo local (`src/ap.c`)

Sous-ensemble réellement émis par les deux lecteurs : texte CP850, propositions
terminées par CR ou `. , : ; ! ?` (coupure à un espace au-delà de 200 caractères),
Ctrl-X, `@W1..9` débit, `@F0..F` hauteur, `@V` voix (mémorisée), `@P0/@P1`
ponctuation (signes nommés), `@E<c>` écho. Le reste est ignoré sans erreur.

- **Accusé de fin** : une marque `@E` est rattachée à la position *produite* ;
  l'écho n'est rendu que lorsque la position *jouée* (IRQ de fin de bloc DMA sous
  DOS, `WHDR_DONE` sous Windows) l'a atteinte. Générer ou mettre en file ne libère
  rien. Sans place pour une marque, la plus ancienne est rendue d'avance
  (`lost_marks`, toujours 0 dans les essais).
- **Ctrl-X** : l'écrivain note seulement la position de la file (`cut_at`) et un
  compteur ; il n'est jamais refusé, même file pleine, et n'attend jamais derrière
  le texte qu'il annule. Sous DOS, l'API masque aussi le canal DMA sur-le-champ.
  La pompe abandonne ensuite texte, marques et audio en file.
- Débit : W1..9 → 3, 5, 6, 8, 9, 11, 12, 14, 15 (`rates`), demandé à l'écoute
  (l'ancien débit = W était trop lent à W9) ; hauteur = F − 3, bornée, pas réglée à
  l'oreille face à un Apollo II réel.

### Variante de production

| Critère (ordre de la mission) | Retenu |
| --- | --- |
| fiabilité, interruptions | moteur par blocs de 512 échantillons, un bloc par pas ; Ctrl-X immédiat |
| sans coprocesseur | `S3_INTEGER_PERIOD` : aucune instruction x87 (vérifié par `wdis` et la carte) |
| réactivité, charge | `S3_BLOCK` en assembleur : −58 % de calcul sur DX4/100 |
| mémoire résidente | quatre fenêtres, **pas de cache LRU** ; `-s` (aucun contrôle de pile) |
| maintenance | mêmes sources et mêmes options pour S3RES et EPAUS3.DLL |

Le cache LRU 16 (`S3_WINDOW_CACHE=16`) est **rejeté pour la production** : 3,4 %
de calcul en moins sur DX4/100 (7,3 % d'activité au lieu de 8,7 %) contre 6 Kio de
résident en plus, alors que S3RES occupe déjà 92 Kio et que la configuration de
référence n'offre que 102 Kio d'UMB : il risquerait de repousser S3RES en mémoire
conventionnelle. La charge mesurée du résident (7,2 % sur DX4/100, 28,8 % sur
SX/25) ne justifie pas ce coût. L'option reste compilable et testée.

Conservés pour les essais seulement : le moteur scalaire x87 (`S3BASE`, référence
octet à octet du corpus), les variantes C des noyaux et `S3_SCALAR_WINDOW`.

### Résident DOS (`S3RES.EXE`, `src/s3res.c`, `src/s3res.asm`)

- **Aucun calcul dans un appel du lecteur** : l'API (appel far) dépose un octet
  ou lit un écho, registres 16 bits, sans DOS ni BIOS. Épaudos l'appelle depuis
  ses interruptions comme il écrivait dans sa file série.
- **Pompe sur INT 8h** après le BIOS (PIC acquitté), interruptions permises, pile
  privée de 1,5 Kio (242 octets utilisés au plus), registres 32 bits, FS et GS
  sauvegardés, garde de réentrance. Au plus trois blocs calculés par passage ;
  passage le plus long mesuré : 12 ms (DX4/100), 48 ms (SX/25).
- **IRQ de la carte** : ne fait que tourner l'anneau DMA (auto-init, 4 × 512 octets
  en mémoire basse, sans franchir 64 Kio) et compter le joué ; bloc absent →
  silence écrit avant que le DMA ne l'atteigne, compté comme sous-alimentation si
  une proposition était en calcul ; arrêt du DMA après six blocs de silence.
- **Aucun fichier, aucune allocation** après l'installation ; pas d'appel DOS en
  interruption.
- **Carte indisponible** (vecteur de l'IRQ repris par un autre programme) : la
  parole est calculée au rythme réel sans être jouée, les échos reviennent, le
  lecteur ne se bloque pas (`state = 1`, `skipped`). Le DSP muet : `state = 2`.
- **Windows** : 1605h arrête DSP et DMA et **rend le vecteur de l'IRQ** ; 1606h le
  reprend. Sans cela, l'interruption de la carte était renvoyée à la machine DOS
  active (S3RES endormi) et la restitution `waveOut` se figeait dès qu'une fenêtre
  DOS plein écran était active (constaté sous QEMU, voir `tests/test_synthe3_win_vm.py`).
  Il **remasque aussi l'IRQ au PIC** si elle l'était avant lui (`pic_was_masked`),
  et la démasque à 1606h : VPICD tient pour globale une IRQ démasquée au démarrage
  de Windows et la renvoie à la machine qui tourne. Avec une fenêtre DOS active, la
  fin de bloc n'arrivait plus à SNDBLST2.DRV, dont le DMA rebouclait sur son tampon :
  première phrase répétée sans fin, même après la fermeture de la fenêtre.
- Retrait : `S3RES /U`, refusé si un lecteur est attaché ou si un vecteur a changé.

### Windows (`EPAUS3.DLL`, `src/s3win.c`)

- Chargée par EPAULARD.EXE (`LoadLibrary`) selon `[Parole] Synthese` :
  `Auto` (défaut : Synthé3 si S3RES répond à INT 2Fh D300h, donc choisi au
  démarrage), `Synthe3` ou `Apollo` ; réglage « Synthèse » d'Inser+E, appliqué
  aussitôt (`speech_restart` : DLL libérée ou COM1 fermé, l'autre ouvert, `EpComm`
  et `EpSpeech` pour la parole directe de la DLL). Échec (DLL absente, pas de
  pilote) : Apollo sur COM1, et Épaulard annonce « Synthé3 indisponible ».
- `waveOut` 8 bits mono 10 kHz (accepté par SNDBLST2.DRV), 11 025 Hz par
  interpolation si le pilote refuse ; quatre tampons de 2048 échantillons (0,8 s) ;
  aucun rappel (`CALLBACK_NULL`) : la pompe relève `WHDR_DONE`. **Pas de tampon
  plus petit que 2048 octets** : SNDBLST2.DRV perd alors environ 2048 octets au
  début de chaque lecture (86Box DX4/100, enregistrement comparé au calcul natif :
  200 ms manquants par phrase avec 512 ou 1024, « fchier », « ption » ; aucune
  perte avec 2048 ou 4096). Les compteurs de la DLL ne le voient pas : la file
  n'était jamais vide.
- **Pompe dans la boucle de messages** d'Épaulard, jamais dans un crochet : un
  tampon par message `EPM_S3` (trois au plus d'affilée après une écriture), puis
  minuterie de 55 ms qui calcule jusqu'à quatre tampons (16 ms de calcul par tampon
  sur DX4/100). Une chaîne de messages
  postés sans borne affamait `WM_TIMER` (produit seulement file vide) : la
  scrutation s'arrêtait, Épaudos en fenêtre DOS se croyait abandonné après 2 s.
  Pendant une boîte modale système, la minuterie de la DLL de crochets pompe.
- **Périphérique tenu seulement pendant la parole** : ouvert à la demande, fermé
  après 1 s de silence. Pris par un autre programme (son de démarrage de Windows,
  constaté) : nouvel essai toutes les 100 ms ; au-delà de 3 s, restitution « à
  vide » au rythme réel pour que les échos reviennent (`skipped`).
- Bloc de diagnostic `S3WININF` (compteurs, mesures, trace) recopié à chaque pas,
  dans une seule page, avec somme de contrôle : les essais le lisent en mémoire.

## Distribution

Le moteur copié conserve ses notices LGPL-2.1-or-later ; les fichiers nouveaux
pc486 sont GPL-3.0-or-later. La distribution Synthé3-ng se déclare GPLv2 et ses
données Michel n'ont pas de licence distincte dans les notices consultées.
Les notices et empreintes sont conservées ; la compatibilité du périmètre exact
de l'archive, dont les données, reste à clarifier avant publication. Aucun binaire
Microsoft, ROM, pilote Creative propriétaire ou support d'installation ne sera
inclus. Aucune archive publique n'est produite dans cette première étape.

Le lot destiné à win3x.org reste conditionné à un essai sur une copie neuve de
DOS/Windows, sans service vocal hôte, avec sélection du backend local et retrait
propre. Le backend Apollo série, le braille et les fichiers système existants
restent inchangés pendant le travail préparatoire.
