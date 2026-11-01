# Première étape : fenêtres préparées et mesures DOS

Cette étape améliore le moteur autonome et sa mesure. Elle ne constitue pas encore
l'intégration d'Épaudos, d'Épaulard ou du protocole Apollo local. L'architecture
et les contraintes de ces intégrations sont dans [ARCHITECTURE.md](ARCHITECTURE.md).

## Ce qui est mesuré

`S3BENCH.EXE TEXTE.TXT` lit son entrée CP850 avant les sections chronométrées,
initialise le moteur, puis effectue une génération complète **sans WAV**. Le temps
de `s3_start` est séparé de la somme des temps de `s3_read`. Le calcul de
l'empreinte PCM est fait **en dehors** de ces sections. Il n'y a donc ni écriture
disque, ni calcul d'empreinte inclus dans « phon » ou « pcm ». Ce sont toutefois
des **temps écoulés**, incluant les interruptions survenues pendant les appels.

Le banc mesure ensuite une restitution complète, avec le producteur et le pilote
instrumentés séparément. Il remet le cache des fenêtres à zéro avant ce nouvel
essai. Le moteur est à sa hauteur 5 et son débit 3 ; les autres réglages sont
couverts par les tests numériques, sans être tous profilés sur la machine.

| Champ brut | Définition |
| --- | --- |
| `hz` | Fréquence de conversion : 1 193 182 ticks PIT par seconde. |
| `pair_min` | Plus petit écart observé entre deux appels successifs à l'horloge, sur 100 essais ; les frais de mesure ne sont pas soustraits. |
| `init` | Initialisation des tables et du cache, séparée de la requête vocale. |
| `phon` | Temps écoulé dans `s3_start`, préparation linguistique et initialisation des périodes comprises. |
| `pcm` | Somme des temps écoulés dans `s3_read`, sans fichier ni empreinte. |
| `samples`, `hash` | Nombre d'échantillons et empreinte FNV du PCM signé 16 bits, calculés hors chronométrage. |
| `total` | Appel complet au pilote mesuré : préparation matérielle, production, lecture, attente et remise en état. |
| `source` | Somme des temps dans le producteur pendant la lecture, phonétisation incluse au premier appel. |
| `idle` | Temps entourant STI/HLT : comprend son coût d'appel et l'interruption qui réveille le CPU. |
| `busy_est` | `total-idle`, **estimation d'activité hors attente**, pas un compteur CPU. |
| `audio` | Soumission de la commande DSP jusqu'à la fin observée au premier plan ou à la pause DSP ; comprend le silence terminal et le délai de réveil. |
| `first_buffer` | Requête au pilote jusqu'au premier bloc PCM disponible, pas jusqu'au premier son audible. |
| `submit` | Requête au pilote jusqu'à la soumission au DSP, préremplissage compris. |
| `stop_command` | Détection de l'arrêt jusqu'à l'acceptation de la pause DSP. Zéro si aucune interruption, ou intervalle non résolu à l'horloge. |
| `produced`, `played` | Échantillons produits et validés aux IRQ de fin de demi-tampon. |
| `underruns` | Sous-alimentations constatées ; un essai de performance normal doit en avoir zéro. |
| `clock_fault` | Signal sticky d'horloge non qualifiée : un rapport qui le contient à 1 est refusé. |

L'activité estimée inclut le travail du pilote, des instructions de mesure et des
interruptions arrivant hors attente. Elle retranche l'interruption de réveil avec
l'attente, donc n'attribue pas exactement tout le CPU. Aucun compteur de cycles
CPU ni d'occupation propre au processus n'est annoncé. Un banc de synthèse au
premier plan, sans autre résident, est nécessaire pour interpréter cette estimation.

### Latences audibles

`acoustic_start=ND` et `acoustic_stop=ND` sont intentionnels : **non déterminées**.
Le premier bloc prêt n'est pas nécessairement audible, et une pause DSP ne mesure
ni un FIFO matériel ni les buffers de sortie de l'émulateur. Une mesure acoustique
nécessitera une capture synchronisée ou une boucle de retour avec une référence
de commande commune. Les chiffres internes ne remplacent pas cette expérience.

`S3BENCH.EXE -s TEXTE.TXT` omet les micro-bancs et la génération isolée. Il permet
au scénario QEMU d'interrompre une restitution mesurée. Son repère de test est
affiché au troisième appel du producteur : **le DMA a déjà commencé**, et non
pendant son préremplissage. L'interruption se fait ensuite par une vraie touche
Ctrl injectée dans la machine d'essai. Le scénario exige qu'une partie du PCM
ait été jouée, puis vérifie le retour d'interruption et ses compteurs.

## Horloge et qualification

L'horloge assembleur associe les ticks BIOS au compteur PIT 8254, en modes 2 ou 3,
avec la période BIOS habituelle de 65536. Elle ne change ni fréquence du canal 0,
ni masque PIC. Le mode 3 exige de tenir compte du bit OUT et de la décrémentation
de deux par cycle. L'IRR du PIC identifie un IRQ0 en attente. Les timestamps 32 bits
sont modulaires sur environ une heure ; les essais sont courts et ne doivent pas
traverser la remise à zéro journalière du compteur BIOS.

Le banc `S3CLOCK.EXE` vérifie 20 000 lectures successives monotones, un délai DOS
d'une seconde à 10 % près, et l'absence de signal d'erreur. Cette marge du délai
sert à qualifier l'horloge, pas à annoncer une précision de 10 % des mesures.
La résolution nominale PIT est d'environ 0,838 µs ; la lecture coûte elle-même
plusieurs microsecondes et ne doit pas être confondue avec cette résolution.

Une première version a échoué sous QEMU au passage `...FFFF` vers `...0000` : le
PIT avait rebouclé, mais le tick BIOS et l'état IRQ n'étaient pas encore alignés.
Le [code PIT de QEMU](https://github.com/qemu/qemu/blob/master/hw/timer/i8254.c)
effectue aussi des lectures distinctes du compte et du statut OUT. Le lecteur
d'horloge vérifie désormais une paire cohérente et, très près de la frontière,
laisse IRQ0 s'exécuter avant de reprendre une mesure au premier plan. Les reprises
sont bornées ; un échec marque le rapport invalide, sans imposer artificiellement
une valeur monotone ni assouplir le test. L'horloge n'est pas appelée dans l'ISR
audio. Les essais en présence d'un résident qui accélère le PIT ne sont pas qualifiés.

## Optimisation exacte

Le noyau scalaire calculait `floor(x*65536/longueur)` et interpolait la fenêtre
Q15 pour chacune des quatre contributions à chaque échantillon. Pour une longueur
donnée, ces valeurs sont constantes. Elles sont maintenant préparées avec un
quotient et un reste incrémental : aucune division par échantillon dans ce chemin.
Le cache contient quatre longueurs, soit 2048 octets de tables plus les index.
Les tables déjà référencées dans une période restent protégées jusqu'à sa fin.

Les bornes entières ont été vérifiées : phase sur 32 bits, erreur de quotient
inférieure à deux longueurs, produit d'interpolation inférieur à 65536, produit
fenêtre × échantillon × amplitude dans les 32 bits signés. Les divisions signées
conservent l'arrondi vers zéro, y compris pour les échantillons négatifs.

La première version entièrement C de la fenêtre préparée a pris **232790 ticks**
contre **183213** pour le scalaire ASM dans le micro-banc de 20200 appels : elle
était plus lente. Ce résultat est conservé, et a motivé uniquement un noyau
assembleur de lecture de la fenêtre préparée. Le moteur linguistique n'a pas été
réécrit. La préparation reste en C ; son amortissement et la réutilisation du cache
doivent être mesurés sur la phrase complète.

Les micro-bancs affichent donc séparément `scalar`, `prepared` (préparation à
chaque période) et `prepared_warm` (table réutilisée). Ils incluent leur boucle et
une empreinte ; ils ne sont pas présentés comme un profil instruction par instruction.
Le cas froid peut rester plus lent alors que le cache accélère le moteur réel.

### Qualité et non-régression

- Toutes les phases de longueurs 1 à 255 sont comparées à la formule de division
  exacte. Les noyaux préparés sont aussi comparés aux noyaux scalaires en C et
  dans le véritable code 16 bits/ASM sous DOS.
- **32 cas** (textes, accents, nombres, ponctuation, 240 octets et combinaisons de
  débit/hauteur) comparent les WAV du port optimisé au chemin scalaire : même
  longueur et mêmes octets, pas seulement une empreinte.
- La comparaison au moteur historique copié reste distincte : l'ancien passage
  au Q15/Q16 introduisait déjà de faibles écarts, dont les seuils sont conservés.
- Cette nouvelle optimisation introduit **zéro écart PCM observé** dans les cas
  comparés. Il n'y a donc pas de différence audible attendue liée au cache sur
  ces cas ; cela ne remplace pas une validation d'écoute ni un corpus exhaustif.

Le chemin scalaire reste compilable avec `S3_SCALAR_WINDOW` ; `S3BASE.EXE` utilise
ce chemin pour le même banc. Le x87 reste requis pour les calculs par période.
Le SX/25 ne sera qualifié qu'après leur remplacement et leurs propres tests.

### Arrêt déjà demandé pendant le calcul

Le contrôle d'attente vérifie maintenant atomiquement les drapeaux Ctrl et le
tampon clavier BIOS avant STI/HLT. Sinon une touche arrivée pendant la génération
pouvait avoir déjà provoqué son IRQ, puis le programme se rendormait jusqu'à
l'IRQ audio suivante. Le banc matériel reproduit un Ctrl déjà en attente et un
tampon clavier non vide : vingt appels ne doivent pas dormir. L'état BIOS est
restauré avant les autres scénarios. Le scénario complet vérifie aussi une vraie
interruption Ctrl pendant la lecture et une reprise sans sous-alimentation.

## Résultat de la première qualification

Les [rapports bruts et convertis](mesures/dx4-100-2026-10-31.json) conservent les
trois passages de chaque variante, le contexte, les empreintes des sources et les
limites de la méthode. Texte : « Bonjour Michel. Un élève prend un café. »,
débit 3, hauteur 5, 86Box DX4/100, 16 Mio, Sound Blaster 16, DOS 6.22.

| Mesure | Scalaire conservé | Fenêtres préparées |
| --- | --- | --- |
| Phonétisation + génération PCM pure, médiane | 756,712 ms | 602,711 ms |
| Minimum–maximum des trois passages | 756,698–756,761 ms | 602,692–602,761 ms |
| Activité estimée hors attente, médiane | 24,99 % | 17,90 % |
| PCM de la phrase | 33380 échantillons, 3,338 s | mêmes échantillons |
| Sous-alimentations pendant ces passages | 0 | 0 |

La réduction du temps de génération est de **20,35 %** sur ce banc. La comparaison
byte à byte porte sur les 32 cas natifs ; l'empreinte et le nombre d'échantillons
sont aussi identiques dans les mesures DOS. Le minimum des écarts entre lectures
PIT vaut environ 12 µs sous ce 86Box, sans correction soustraite aux chiffres.
La restitution comprend jusqu'à une demi-trame terminale de silence : elle finit
ici vers 3,379 s après soumission DSP, ce qui n'allonge pas le PCM produit.

La mesure fonctionnelle d'interruption sous QEMU rapporte un arrêt pendant la
lecture, avec 1536 échantillons validés comme joués et aucune sous-alimentation.
Son délai détection→commande est consigné dans les données brutes ; **il ne sert
pas à prédire une latence sur DX4 ni au haut-parleur**. Les deux latences audibles
restent non déterminées. La parole physique et la cohabitation avec les applications
réelles restent à qualifier lors de l'intégration.

## Reproduire

```sh
bash synthe3/build.sh
python3 -m unittest tests.test_synthe3 -v
PC486_S3_TESTS=1 python3 -m unittest tests.test_synthe3_vm -v
PC486_S3_86BOX=1 python3 -m unittest tests.test_synthe3_86box -v
```

Le banc DX4/100 fait trois passages par variante, dans l'ordre optimisé/scalaire,
scalaire/optimisé, optimisé/scalaire. Les fichiers `BENCH[2|3].TXT` et
`BASE[2|3].TXT` restent dans le dossier privé annoncé par le test. Le disque et la
CMOS sont des copies ; l'audio OpenAL est nul. Ces mesures ne certifient donc pas
la qualité acoustique de la sortie et ne proviennent pas d'un PC physique.

Pour convertir les ticks et calculer les médianes :

```sh
python3 synthe3/mesures.py DOSSIER/BASE.TXT DOSSIER/BENCH.TXT --machine DX4/100 \
  --reference-repeat DOSSIER/BASE2.TXT --reference-repeat DOSSIER/BASE3.TXT \
  --optimise-repeat DOSSIER/BENCH2.TXT --optimise-repeat DOSSIER/BENCH3.TXT
```

Le script refuse les horloges invalides, interruptions, sous-alimentations et
empreintes ou nombres d'échantillons différents. Il conserve les rapports bruts.
Les médianes de trois passages sur un texte constituent une première mesure,
pas une étude statistique générale. Aucune extrapolation linéaire vers le SX/25
n'est utilisée comme résultat.

# Deuxième étape : profil, noyaux par période et moteur sans x87

Même méthode, même texte et même machine d'essai que la première étape ; les
rapports bruts sont dans `mesures/*-2026-10-31-*.json`, produits par
`synthe3/banc86box.py` (voir « Reproduire »). Chaque lot alterne l'ordre des
variantes sur trois passages ; la dispersion intra-lot reste inférieure à 0,1 ms.

## Profil avant optimisation (S3PROF, DX4/100)

`S3PROF.EXE` est la variante à fenêtres préparées avec des bornes PIT dans le
moteur (`S3_PROFILE`). Les compteurs cumulent la génération isolée et la
génération pendant la lecture ; les valeurs ci-dessous sont ramenées à une
génération. Les bornes coûtent elles-mêmes environ 70 ms sur 616 ms mesurés :
les parts sont indicatives, pas des cycles comptés.

| Section | Temps ramené à une génération | Part |
| --- | --- | --- |
| Boucle par échantillon : 4 × `s3_weighted` + `s3_output`, appels far | ≈ 517 ms | ≈ 85–90 % |
| Contrôle par période (`perioSuiv`, flottant x87, chaînage) | ≈ 52 ms, dont ≈ 16 ms de bornes | ≈ 6 % |
| Préparation des fenêtres (116 préparations pour 391 périodes) | ≈ 24 ms | ≈ 4 % |
| Phonétisation (`s3_start`) | 2,4 ms | 0,4 % |

Micro-bancs sur 20 200 appels, boucle et empreinte comprises : `s3_window`
7,2 µs, `s3_weighted` 6,1 µs, `s3_output` 5,5 µs par appel. Le texte compte
391 périodes, 33 380 échantillons, 3 contributions actives par période en
moyenne : environ 167 000 appels far pour une phrase de 3,3 s.

Conclusion du profil : **le flottant n'est pas le premier goulet**. Il pèse
moins de 6 % sur DX4 ; le coût vient de la structure par appel (sauvegarde de
sept registres, chargement des arguments, bornes et indices recalculés pour
chaque échantillon et chaque contribution). Réduire la précision des
multiplications n'apporterait rien non plus : sur 486, `imul` 16 et 32 bits
coûtent le même ordre de cycles, et 86Box leur applique un coût fixe.

## Optimisation 1 : noyaux par période (`S3_BLOCK`)

`s3_mix` ajoute dans un tampon toute la contribution d'un sous-diphone pour une
période (un appel au lieu d'un par échantillon), puis `s3_output_block` applique
gain, filtre et écrêtage à la période entière. Les bornes, l'indice modulo
dans le segment, la division signée vers zéro et le produit 64 bits du filtre
sont strictement ceux des noyaux scalaires. Référence C portable dans
`src/block.c`, assembleur 386 dans `src/block.asm`.

| Variante | Génération (médiane de 3) | Activité estimée | PCM |
| --- | --- | --- | --- |
| `S3BENCH` fenêtres préparées (référence de cette étape) | 545,5 ms | 18,3 % | `cd5146b4`, 33 380 |
| `S3BLKC` noyaux par période en C | 429,8 ms (−21 %) | 13,3 % | identique |
| `S3BLKA` noyaux par période en assembleur | **231,3 ms (−58 %)** | 8,7 % | identique |

La version C mesurée d'abord justifie l'assembleur : le compilateur 16 bits
garde les variables 32 bits en mémoire et recharge les pointeurs far à chaque
tour. Le noyau assembleur tient fenêtre, segment et tampon dans DS, ES et FS, et
coûte deux `imul` 32 bits, une lecture et une écriture par échantillon et par
contribution. Le partage d'un noyau entre deux contributions de même fenêtre a
été évalué sur le corpus et abandonné : 8 % des périodes seulement.

Vérifications : `CHKBLKC.EXE` et `CHKBLKA.EXE` comparent `s3_mix` à
`s3_weighted` et `s3_output_block` à `s3_output` sur 17 552 combinaisons de
périodes, sens et amplitudes, sous DOS et nativement ; les 45 cas du corpus et
les 32 cas de non-régression donnent des WAV **identiques octet à octet**.

## Optimisation 2 : arithmétique par période entière (`S3_INTEGER_PERIOD`)

Les tables `tabHau` et `tabVit` sont des millièmes entiers (707 à 4000) :
`mulHauteur` et `mulDebit` ne sont que ces entiers divisés par 1000. Les
positions `xEcrEchelleDeLec` et `allonge` sont donc tenues en **millièmes
d'échantillon sur 32 bits** (pas |Δ| ≤ 255 × 4000 ; les accumulateurs oscillent
autour de zéro, l'allongement ajoute 250 000 par `:`). La période résultante est
l'arrondi de `Σ perio·amp × 1000 / (Σ amp × tabHau)` en rationnel exact
(numérateur ≤ 31 620 000, dénominateur ≤ 208 568, arrondi `(2n + d) / 2d`).
Aucun format Q n'est nécessaire : toutes les grandeurs sont des entiers exacts,
sans erreur de quantification. La seule différence possible avec le flottant est
le **sens d'une égalité exacte** : demi-entier à arrondir, ou passage par zéro de
la position, que le `float` 24 bits tranche selon ses propres arrondis.

Mesuré sur 45 cas (187 s de parole, débits et hauteurs 0 à 15) : 43 WAV
identiques ; deux cas diffèrent exactement à une égalité. Dans « Le prix est de
12,50 francs. », la période 316 vaut 82,6 × 1000 / 944 = 87,5 : le rationnel
donne 88, le flottant 87 ; un échantillon de plus, rapport signal/écart 36 dB
dû au décalage d'un échantillon. Dans la phrase « Il y a 1 234 567 habitants… »,
une position passe exactement par zéro : une période est lue un tour plus tard,
14 échantillons (1,4 ms) sur 89 871, et les deux sorties se resynchronisent
quelques périodes après. Aucun cas n'ajoute d'écrêtage (crête maximale 15 405
sur 32 767 dans les deux variantes). Ces mesures sont numériques ; elles ne
démontrent pas l'absence de différence audible, mais le changement est borné à
une décision de période, que le moteur historique prenait déjà arbitrairement.

Les six textes comparés au moteur copié gardent leur rapport signal/erreur
(> 45 dB) ; la compilation sans `-fpi87` est vérifiée par `build.sh` : aucune
instruction x87 dans l'objet (`wdis`), aucune bibliothèque flottante dans la
carte de l'exécutable.

| Variante (DX4/100, même lot) | Génération | Activité | PCM |
| --- | --- | --- | --- |
| `S3BENCH` fenêtres préparées, x87 | 545,5 ms | 18,2 % | `cd5146b4` |
| `S3INTW` entier sans noyaux par période | 608,1 ms | 17,8 % | identique |
| `S3BLKA` noyaux par période, x87 | 231,2 ms | 8,7 % | identique |
| `S3INTA` noyaux par période, entier | 225,9 ms | 8,7 % | identique |

Sur DX4, l'entier n'apporte que 2 % derrière les noyaux par période, conformément
au profil ; et `S3INTW` est plus lent que `S3BENCH` alors que son code par
échantillon est le même. Voir « Bruit de disposition » ci-dessous : l'intérêt de
cette optimisation est la **compatibilité 486SX et 386**, pas la vitesse sur DX4.

## 486SX/25 sans coprocesseur

`banc86box.py --cpu sx-25` configure 86Box en i486SX à 25 MHz, `fpu_type = none`,
même carte HOT-433A, même disque. Les rapports confirment `fpu=0` (mot
d'équipement BIOS) et `cpu_class=4`. Seules les variantes sans x87 sont lancées.

| Variante (486SX/25) | Génération de 3,338 s de parole | Activité estimée | PCM |
| --- | --- | --- | --- |
| `S3INTW` entier, fenêtres préparées, appels par échantillon | 2 432 ms (73 % du temps réel) | 65 % | `cd5146b4` |
| `S3INTA` entier, noyaux par période | **902 ms (27 % du temps réel)** | 29 % | identique |

`S3DOSX.EXE -s` (programme autonome équivalent à `S3DOS`, sans x87) a joué deux
fois la phrase en direct : 33 380 produits, 33 380 joués, **0 sous-alimentation**.
Le code n'utilise que des instructions 386 : un 386 avec Sound Blaster est
visé par la même compilation, mais la carte d'essai n'accepte pas de 386 et
aucune mesure n'existe encore.

## Optimisation 3 : cache LRU des fenêtres (`S3_WINDOW_CACHE=16`)

Le profil de la variante rapide (`S3PROFI`, bloc assembleur + entier) ramené à
une génération : boucle par échantillon ≈ 179 ms, contrôle par période ≈ 52 ms
(dont ≈ 16 ms de bornes), préparation des fenêtres ≈ 24 ms pour 116 préparations
sur 391 périodes. La politique historique (quatre emplacements, premier libre)
manque 116 fois ; une simulation sur les longueurs demandées du corpus (68
longueurs distinctes) donne 6,6 % de ratés pour 4 entrées LRU, 2,9 % pour 16,
0,8 % pour 32. Avec 16 entrées (8 Kio de tables, protection des quatre fenêtres
de la période courante, remplacement de la plus ancienne), mesuré dans un même lot :

| Variante (DX4/100) | Génération | Activité | PCM |
| --- | --- | --- | --- |
| `S3BENCH` référence | 545,7 ms | 18,3 % | `cd5146b4` |
| `S3BLKA` noyaux par période | 231,4 ms | 8,7 % | identique |
| `S3INTA` + entier | 226,1 ms | 8,7 % | identique |
| `S3INTAC` + cache LRU 16 | **218,3 ms** | 7,3 % | identique |

Gain de 3,4 % sur `S3INTA`, conservé comme option : il ne change pas le PCM
(vérifié sur les 45 cas et les 32 cas de non-régression) et coûte 6 Kio de plus.
Une préparation en assembleur ou un cache de 32 entrées gagneraient au plus les
≈ 8 ms restants ; non réalisé. Le prochain poste est le contrôle par période
(≈ 16 % : `perioSuiv` reçoit vingt pointeurs far par appel), qui touche au
moteur linguistique et n'a pas été modifié.

## Bruit de disposition dans 86Box

Deux lots donnent 545,5 ms pour `S3BENCH` ; la première étape avait mesuré
602,7 ms pour la même source avant l'ajout d'un micro-banc au programme. De même
`S3INTW` est plus lent que `S3BENCH` de 63 ms alors que les boucles par
échantillon sont identiques, et que les profils `S3PROF` et `S3PROFI` mesurent le
même contrôle par période en flottant et en entier (51,6 et 51,8 ms, bornes
comprises) : l'écart n'est pas dans le code modifié. `S3INTW2`, même source que
`S3INTW` compilée avec les options flottantes de la référence, donne 608,4 ms
contre 608,4 : les options de compilation n'y sont pour rien non plus. 86Box
modélise la file de préchargement et le cache du 486 : la disposition du code
(alignement des cibles de saut, tailles des routines) change le compte de cycles
d'un ordre de **10 %** sur les chemins riches en appels far. Conséquences :
comparer les variantes **dans un même lot**, ne pas interpréter un écart
inférieur à 10 % entre deux compilations, et ne pas convertir ces millisecondes
en cycles d'un vrai processeur. Les gains de 58 % (noyaux par période) et le
facteur 2,7 sur SX sont très au-dessus de ce bruit.

## Reproduire la deuxième étape

```sh
bash synthe3/build.sh
python3 -m unittest tests.test_synthe3 -v
python3 synthe3/corpus.py
python3 synthe3/banc86box.py --cpu dx4-100 --verif CHKBLKC.EXE --verif CHKBLKA.EXE \
    S3BENCH.EXE S3BLKC.EXE S3BLKA.EXE S3INTA.EXE S3INTAC.EXE S3PROF.EXE S3PROFI.EXE
python3 synthe3/banc86box.py --cpu sx-25 --verif 'S3DOSX.EXE -s -f C:\TEXTE.TXT' S3INTA.EXE S3INTW.EXE
PC486_S3_86BOX=1 python3 -m unittest tests.test_synthe3_86box -v
```

Les dossiers d'essai `work/vmtest/synthe3/banc-*/` gardent les rapports TXT et
`rapports.json` ; `banc86box.py --relire DOSSIER` en refait le résumé. Les WAV du
corpus sont dans `work/dev/synthe3/corpus/wav/<variante>/` ; leur écoute n'a pas
été faite dans cette étape et reste nécessaire avant de changer la compilation
par défaut.

# Troisième étape : mesures du résident intégré aux lecteurs

Ces mesures portent sur **S3RES tel qu'Épaudos l'utilise** (même API, mêmes octets
Apollo), pas sur un banc isolé. `S3PROBE.EXE` (`tests/s3probe.c`) est un client de
l'API : il envoie une phrase depuis le silence, attend l'écho `@E` de fin de
lecture, coupe par Ctrl-X une parole longue, compte une boucle de premier plan au
repos puis pendant la parole, et écrit sur le disque par DOS pendant la parole.

Unités : microsecondes d'après le PIT, sauf mention. « DMA lancé » est l'instant où
la pompe a démarré la carte sur le premier bloc : ce n'est **pas** la latence
acoustique (la carte convertit ensuite ce bloc ; aucun son n'a été capté). La
comparaison écho/durée de parole donne le retard de l'accusé de fin sur le temps
réel. Texte : « Bonjour Michel. Un élève prend un café. », 3,329 s de parole.

## 86Box, deux passages par processeur

| Mesure | DX4/100 | 486SX/25 sans x87 |
| --- | --- | --- |
| demande → DMA lancé | 30,1 ; 44,0 ms | 83,7 ; 79,3 ms |
| dans la pompe : données vues → 1er bloc calculé | 4,1 ; 3,9 ms | 16,3 ; 15,5 ms |
| dans la pompe : données vues → bloc soumis au DSP | 12,1 ; 11,9 ms | 48,2 ; 47,4 ms |
| demande → écho de fin (parole de 3 329 ms) | 3 424 ; 3 438 ms | 3 496 ; 3 493 ms |
| part du processeur prise pendant la parole | 7,2 % | 28,8 % |
| Ctrl-X → canal DMA masqué (dans l'appel) | 13 ; 14 µs | 31 ; 31 µs |
| Ctrl-X → DSP arrêté par la pompe | 20,3 ; 20,2 ms | 39,4 ; 39,4 ms |
| nouvelle demande après Ctrl-X → DMA lancé | 65,1 ; 65,1 ms | 95,7 ; 95,7 ms |
| sous-alimentations (phrase ; 6 s d'écritures disque de 4 Kio) | 0 ; 0 | 0 ; 0 |
| plus long passage de la pompe (INT 8h, IF=1) | 12,1 ms | 48,2 ms |
| marge de pile minimale (sur 1 536 octets) | 1 314 octets | 1 314 octets |
| propositions refusées, marques perdues, octets refusés | 0 | 0 |

Mémoire : bloc résident de 94 112 octets plus l'anneau DMA de 2 048 octets en
mémoire basse. Sous QEMU avec la configuration de référence (`LH`), `MEM /C` donne
S3RES 96 096 octets dont 94 032 en mémoire supérieure et 2 064 en conventionnelle
(l'anneau), Épaudos 44 160 octets (inchangé).

La demande attend d'abord le passage suivant de la pompe (INT 8h, 55 ms au plus),
puis deux blocs sont calculés avant le départ du DMA. La reprise après Ctrl-X est
plus lente que la première demande : la pompe arrête d'abord le DSP (remise à zéro)
au passage qui suit la coupure. Le DX4/100 garde 92,8 % du processeur au premier
plan pendant la parole ; le SX/25 71,2 %.

Reproduire (Xvfb privé, copie du disque, aucune fenêtre dans la session) :

```sh
synthe3/build.sh
python3 -c "import sys; sys.path.insert(0, 'synthe3'); import banc86box; \
  print(banc86box.run_batch('sx-25', ['S3PROBE.EXE'], passes=2, checks=['S3RES.EXE /F'])[0])"
```

Les rapports `V1P1.TXT`, `V1P2.TXT` et `rapports.json` sont dans le dossier imprimé
(`work/vmtest/synthe3/banc-*`).

## QEMU (fonctionnel, minutage non fidèle)

Scénarios `tests/test_synthe3_dos_vm.py` et `tests/test_synthe3_win_vm.py`. QEMU
n'émule pas le rythme d'un 486 : seuls les compteurs et l'ordre des événements
comptent ici.

- DOS : une seule sous-alimentation, dans un passage sur neuf passages complets,
  non reproduite ensuite et non expliquée (aucune sur 86Box).
- Windows, EPAUS3.DLL : `waveOut` à 10 000 Hz accepté par SNDBLST2.DRV ; demande →
  premier tampon soumis 13 à 17 ms (temps de l'hôte) ; au démarrage de Windows, le
  périphérique est pris par le son d'ouverture : première parole après 830 à 940 ms.
  Sous-alimentations : 0 dans un passage complet, 2 dans un autre (moment non
  relevé ; dans un essai antérieur à la correction de la pompe, elles étaient
  apparues pendant la fenêtre DOS plein écran). Un pas de pompe a duré jusqu'à
  430 ms de temps réel, ce qui inclut les tranches de temps données à la machine
  DOS (pas seulement le calcul) : à qualifier sur 86Box.

## Windows sous 86Box : EPAUS3.DLL et SNDBLST2.DRV

`S3WPROBE.EXE` (`tests/s3wprobe.c`) charge EPAUS3.DLL et la pompe comme Épaulard
(messages postés bornés, minuterie de 55 ms), sous WfW 3.11 avec le pilote Sound
Blaster de Windows (SNDBLST2.DRV, carte SB16 de 86Box). La sortie audio de 86Box est
enregistrée (OpenAL Soft, pilote `wave`) et la durée parlée de chaque mot isolé est
comparée à celle du PCM du moteur calculé sur l'hôte (`tests/render.c`, ramené en
8 bits). Précision de la comparaison : environ ±10 ms.

| Tampons waveOut | Machine | Parole perdue par mot (6 mots) | Sous-alimentations | Pompe max |
| --- | --- | --- | --- | --- |
| 6 × 512 octets (première version) | DX4/100 | **195 à 204 ms** | 0 | 6 ms |
| 8 × 1024 octets | DX4/100 | **195 à 203 ms** | 0 | 9 ms |
| 4 × 2048 octets (retenu) | DX4/100 | aucune (−9 à −1 ms) | 0 | 16 ms |
| 3 × 4096 octets | DX4/100 | aucune (−9 à −2 ms) | 0 | 32 ms |
| 4 × 2048 octets (retenu) | 486SX/25 | aucune (−9 à −2 ms) | 0 | 70 ms |

Avec des tampons de moins de 2048 octets, le pilote perd environ 2048 octets
(205 ms à 10 kHz) au début de chaque phrase, quelle que soit sa longueur, alors que
la file de la DLL n'est jamais vide : ses compteurs ne voient rien. C'est le défaut
entendu (« fchier », « ption », parole hachée). Le début du mot est joué, puis un
morceau manque. Une file plus profonde de petits tampons (8 × 1024) ne corrige rien ;
des tampons de 2048 octets suffisent. Un énoncé plus court qu'un tampon (lettre
« a », 1 866 échantillons) n'est pas tronqué. La phrase longue (6,6 s) passe sans
trou ni sous-alimentation sur les deux machines. Le calcul d'un tampon de 2048
échantillons occupe la boucle de messages d'Épaulard 16 ms sur DX4/100 et 70 ms sur
486SX/25 : la première parole part après ce calcul.

Reproduire : `PC486_S3_86BOX=1 python3 -m unittest
tests.test_synthe3_86box.Synthe3WindowsSoundTests -v` (copie du disque arrêté, Xvfb et
D-Bus privés ; le WAV est conservé sous `work/vmtest/synthe3/banc-*`). Sous DOS, le
même essai avec S3RES (`S3PROBE.EXE`) n'a montré aucun trou : S3RES programme lui-même
un DMA auto-initialisé sur son anneau de 4 × 512 octets.

## Apollo II externe, pour comparaison

Rien n'a été mesuré sur un Apollo II physique ; Apolo-emu n'en reproduit pas le
minutage. Ce qui est établi : avec l'Apollo, le PC ne calcule rien (une interruption
série par octet, 8250 sans FIFO) et la transmission à 9 600 bauds coûte 1,04 ms par
octet (42 ms pour la phrase de mesure) avant que l'appareil ait toute la
proposition ; sa propre latence de synthèse est inconnue. Avec Synthé3, le PC
fournit le calcul (7,2 % d'un DX4/100, 28,8 % d'un SX/25) et la Sound Blaster.

## Non mesuré

- 386 : la carte HOT-433A n'accepte pas de 386 ; le code n'utilise que des
  instructions 386, mais **aucun essai n'a été fait sur 386**.
- Écoute comparative : les WAV capturés sous QEMU ne sont pas une validation
  acoustique ; la correspondance débit/hauteur Apollo → Synthé3 n'a pas été réglée
  à l'oreille.
