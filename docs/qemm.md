# QEMM 8.0 : plus de mémoire conventionnelle

`scripts/installer --qemm media/archives-utilitaires-fr/qemm-8.0-fr-win3x.zip`
installe QEMM 8.0 français à la place de HIMEM et EMM386. Les supports locaux ne
contiennent pas de QEMM 8.5 : la version 8.0 (décembre 1995, 3 disquettes) est la
plus récente disponible ; la 7.5 est aussi présente, non utilisée.

## Résultat

Mémoire conventionnelle libre sous DOS, avec Synthé3 (S3RES), Épaudos, SMARTDRV et
KEYB, mesurée par `MEM /C` (« Taille maximale du programme exécutable ») :

| Émulateur | HIMEM + EMM386 NOEMS | QEMM 8.0 |
| --- | --- | --- |
| 86Box (HOT-433A, Award, GD5428 ISA) | 509 Ko | 600 Ko |
| QEMU (SeaBIOS, Cirrus PCI, lecteur de CD) | 500 Ko | 526 Ko |

Sous 86Box, QEMM trouve 189 Ko de mémoire supérieure (B000-B7FF et C800-EFFF)
contre 117 Ko pour EMM386, que S3RES (95 Ko d'un seul bloc) ne pouvait d'ailleurs
pas utiliser : il se chargeait en mémoire basse. Avec QEMM, S3RES, KEYB, Épaudos
et une partie de SMARTDRV sont en mémoire supérieure. Sous QEMU, la ROM vidéo PCI
et le BIOS laissent moins de place ; Épaudos reste en mémoire basse.

## Configuration écrite

```
DEVICE=C:\QEMM\QEMM386.SYS RAM FRAME=NONE NOPE
```

- `RAM` : mémoire supérieure. Aucune région n'est fixée (ni `RAM=`, `X=`, `I=`,
  ni `/R:` de LOADHI) : QEMM l'analyse à chaque démarrage, et le même disque
  fonctionne sous QEMU et sous 86Box.
- `FRAME=NONE` : pas de cadre de page EMS (comme `NOEMS` d'EMM386). Avec cadre,
  ses 64 Ko coupent la mémoire supérieure en blocs trop petits pour S3RES
  (455 Ko libres sous QEMU). L'EMS reste disponible sans cadre pour les
  programmes qui l'acceptent.
- Pas de Stealth ROM : il exige un cadre de page (« Désactive StealthROM:M car il
  n'y a pas de page frame »), et `ST:M` sous QEMU rend le disque illisible dès la
  ligne suivante de CONFIG.SYS.
- `NOPE` : pas de pause « Appuyez sur une touche » sur erreur de QEMM, écran que
  rien ne lit.
- `DEVICEHIGH` et `LH` du DOS restent : `LOADHI.COM` de QEMM masque la mémoire
  basse pendant le chargement, et S3RES n'y trouve plus son tampon DMA (« S3RES :
  mémoire basse insuffisante pour le DMA », Synthé3 muet).
- Ordre d'AUTOEXEC.BAT (seulement avec `--qemm`) : S3RES, `LH KEYB`, puis dans
  chaque branche Épaudos **avant** SMARTDRV. SMARTDRV (28 Ko) passe en mémoire
  basse, en tout ou en partie, plutôt qu'Épaudos (45 Ko).

L'installateur de QEMM ajoute aussi MagnaRAM (`MAGNA31.VXD`, compression de la
mémoire, `PAGEOVERCOMMIT=8`), FreeMeg et le Gestionnaire de ressources
(`FREEMEG.DLL`, `RSRCMGR.DLL` dans `drivers=`, chargés avant USER et GDI, que
détourne Épaulard) et son logo (`run=`). `configure_qemm` les retire : Épaulard
n'a pas été validé avec eux. QDPMI et les VxD de QEMM pour Windows restent.

## Installation sans intervention

INSTALL.EXE de QEMM 8.0 est un programme Windows. Il est lancé par `INSTAUTO.EXE`
comme Norton Desktop ; ses pages sont des boîtes `#32770` **sans titre** d'un même
cadre, reconnues par leur en-tête (règle `#En-tête|touches`, voir
`epaulard/src/instauto.c`) :

1. Bienvenue : Suivant.
2. Personnalisez votre produit : nom, société, Ville (`Paris`) et Pays (`FRANCE`)
   obligatoires, numéro de série lu dans `SERIAL.TXT` de la disquette 1.
3. Répertoire : `C:\QEMM`, groupe « Quarterdeck QEMM ».
4. Type d'installation : Installation rapide.
5. QEMM Setup : « Retirez HIMEM.SYS ? » Oui ; il enregistre alors seul sa
   configuration, aussitôt remplacée par celle du projet.
6. Boîte de fin : Quitter, **sans OPTIMIZE**. Windows redémarre ; INSTAUTO, déjà
   « fini », le quitte au démarrage suivant.

La vérification finale démarre le DOS avec Synthé3, relève `MEM /C` (journal
`qemm-mem.txt` du dossier de travail) et exige QEMM386, Épaudos, et S3RES en
mémoire supérieure.

## Pourquoi pas OPTIMIZE

- Ses régions dépendent du BIOS : l'installateur tourne sous QEMU, l'usage sous
  86Box. Un OPTIMIZE sous QEMU donnerait une configuration fausse pour 86Box.
- Il gère mal le CONFIG.SYS à menu et `GOTO %CONFIG%` (README de QEMM : un passage
  par branche, `WIN` à retirer d'AUTOEXEC.BAT) ; ses écrans entre les redémarrages
  ne sont pas vocalisés.
- La configuration ci-dessus, sans adresse, prend déjà toute la mémoire supérieure
  que QEMM détecte.

Pour essayer OPTIMIZE soi-même sous 86Box, sur une **copie** du disque : retirer le
menu de CONFIG.SYS (garder `[COMMON]` et la branche voulue) et la ligne `WIN`
d'AUTOEXEC.BAT, puis la procédure sûre du README : `OPTIMIZE /REMOVEALL`, ligne
`QEMM386.SYS` réduite à `RAM FRAME=NONE NOPE`, `OPTIMIZE /ST`. LOADHI ne doit pas
charger S3RES (garder `LH`). Restaurer ensuite le menu.

## Défaut d'Épaudos révélé

Sous 86Box, `LH C:\EPAULARD\EPAUDOS.COM` figeait la machine ou corrompait la chaîne
mémoire du DOS (« MSDOS 471 056 » en mémoire supérieure). Le fichier (23 Ko) tenait
dans le bloc supérieur libre (37 Ko), pas le résident (45 Ko avec données et piles) :
la remise à zéro du démarrage écrasait le bloc suivant. `epaudos/build.sh` complète
désormais le fichier jusqu'au résident plus 1 Ko de pile de chargement : DOS ne le
met que dans un bloc assez grand, sinon en mémoire basse. `start` (`tsr.asm`)
vérifie en plus la place et refuse de se charger plutôt que d'écraser la mémoire.
Le même défaut pouvait toucher n'importe quel gestionnaire de mémoire.

## Limites

- Automatisé sous QEMU par l'installateur (Windows, Épaulard, Synthé3) et sous
  86Box par `tests/test_qemm_86box.py` : `MEM /C`, démarrage de Windows avec
  Épaulard (Apollo II sur COM1), parole de Synthé3 sous Windows (son enregistré,
  comparable au témoin EMM386). Pas encore d'usage prolongé ni d'essai des
  applications (Word, Excel, Netscape) avec QEMM.
- MagnaRAM, FreeMeg et le Gestionnaire de ressources ne sont pas essayés.
