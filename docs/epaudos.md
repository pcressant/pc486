# Épaudos — lecteur d'écran résident pour DOS

Épaudos est un TSR 8086 pour MS-DOS 3.3 et ultérieur, prévu ici pour le DOS
6.22 français du PC 486. Il reprend la navigation d'Épaulard, sans souris :
parole Apollo II sur COM1, braille Baum sur COM3. Les passerelles hôte restent
Apolo-emu et `scripts/braille/`. Il remplace Sonolect, qui n'est plus installé.

Cette première version lit les écrans texte OEM **CP850**, de 40 à 132 colonnes
et jusqu'à 50 lignes, y compris lorsque l'application écrit directement dans
la mémoire vidéo. Elle observe aussi la page vidéo active et les attributs.
Elle reconnaît des cadres simples ou doubles, même titrés, y compris les cadres
ouverts d'EDIT et QBasic (barre de défilement à droite, ligne d'état en bas), et
cherche les surbrillances qui changent après une touche. Ces indices ne constituent pas
encore une description sémantique de tous les contrôles DOS.

## Compiler et charger

```sh
epaudos/build.sh
```

Open Watcom est le même que pour Épaulard (`tools/watcom`, ou `WATCOM`). Le
résultat est `epaudos/build/EPAUDOS.COM`, sans bibliothèque C embarquée. Le
fichier MAP indique la borne `_resident_end` ; arrondie au paragraphe supérieur,
elle donne la mémoire conservée, PSP et pile privée compris. Le petit fichier
COM sur disque n'inclut pas les tampons non initialisés résidents.

Copier le COM dans un répertoire du disque DOS **arrêté**, puis le lancer à
l'invite DOS. Cette compilation n'installe rien et ne modifie aucun fichier
de démarrage. Le modèle `dos/AUTOEXEC.BAT` lance `C:\EPAUDOS.COM` dans la branche
DOS du menu, après KEYB FR et la page 850 ; `scripts/installer` copie le TSR. Pour
mettre à jour la VM existante, compiler, arrêter la VM puis :

```sh
mcopy -o -i vm/win311.img@@32256 epaudos/build/EPAUDOS.COM ::/EPAUDOS.COM
```

`LH EPAUDOS` peut être essayé si les UMB sont configurés ;
le chargement en mémoire haute n'a pas encore été validé ici.

Un second lancement détecte l'instance déjà résidente. Il n'y a pas encore de
désinstallation à chaud ; redémarrer DOS pour retirer le TSR. L'installation
depuis une session Windows est refusée en mode amélioré : toujours charger
Épaudos avant Windows, jamais dans une fenêtre DOS de Windows.

## Commandes

Verr Num éteint ; les flèches dédiées restent celles de l'application.

| Unité | Précédent | Courant | Suivant |
| --- | --- | --- | --- |
| Ligne | pavé 7 | pavé 8 | pavé 9 |
| Mot | pavé 4 | pavé 5 | pavé 6 |
| Caractère | pavé 1 | pavé 2 | pavé 3 |

En revue normale, chaque commande repart du curseur ou de la surbrillance
reconnue. En revue à plat, les commandes font avancer un point indépendant,
sans modifier le curseur de l'application. Le passage de ligne reste dans les
limites du cadre reconnu ; faute de cadre, il porte sur tout l'écran.

| Commande | Action |
| --- | --- |
| Inser + moins du pavé | basculer revue normale / à plat |
| Inser + plus du pavé | activer / désactiver l'écho écran |
| Inser + 5 du pavé, Inser + Haut | recentrer et lire la ligne active ; dans une boîte, la boîte et son contrôle actif |
| Inser + Bas | lire de la ligne courante au bas du cadre ou de l'écran |
| Inser + B ou O | lire le cadre ou l'écran depuis le haut |
| Inser + T | lire la première ligne intérieure du cadre ; dans une boîte, son titre |
| Inser + PgSuiv | lire la dernière ligne du cadre |
| Inser + D | donner ligne, colonne et attribut du curseur |
| Inser + H | aide |
| Inser + E | réglages |
| Ctrl | couper la parole et le dire tout |

### Suivi du curseur et écho écran

Le suivi ne devine pas d'après le seul curseur : la dernière touche laissée à
l'application est classée (Haut/Bas/Page, Gauche/Droite/Début/Fin, frappe,
Retour arrière, Suppr, Entrée, autre) et le changement d'écran est interprété
selon cette classe.

- Haut, Bas, PgPréc, PgSuiv, Ctrl+Début, Ctrl+Fin : la ligne du curseur est lue,
  y compris lorsque l'application fait défiler le texte sans déplacer le curseur
  (bas de fenêtre d'EDIT, page suivante) ou lorsque la ligne change sous un
  curseur qui saute en fin (rappel d'historique DOSKEY).
- Gauche, Droite, Début, Fin : le caractère atteint ; Ctrl+Gauche/Droite : le mot.
  Une ligne d'état qui change ailleurs (colonne dans EDIT) n'empêche plus la lecture.
- Frappe : selon le réglage d'écho clavier, le ou les caractères apparus entre
  l'ancienne et la nouvelle position du curseur (plusieurs frappes dans le même
  passage sont lues ensemble), ou le mot terminé par un blanc.
- Retour arrière et Suppr : le caractère effacé, pris dans la copie de la rangée
  du passage précédent.
- Entrée, autre touche ou aucune touche : écho écran s'il est activé ; sinon la
  ligne du curseur s'il a changé de rangée, ou le caractère atteint.

L'écho écran, activé par défaut, lit les rangées dont le texte a changé sans
qu'un déplacement ou une frappe l'explique : sortie d'une commande, message d'un
programme, invite. Un défilement vertical est reconnu en comparant les empreintes
des rangées décalées : seules les rangées nouvelles sont lues, dans la limite
du cadre reconnu. Une rangée insérée ou supprimée au milieu d'un éditeur n'est
pas relue non plus. L'écho attend que l'écran n'ait plus changé pendant un passage
du timer (55 ms) et compare alors au dernier écran stable : un éditeur lent
qui redessine sa fenêtre sur plusieurs passages n'est pas lu par morceaux ; une
sortie continue est vidée de force tous les six passages. Les changements survenant dans les
trois passages qui suivent une frappe ou un déplacement restent attribués à
cette touche (fin de redessin d'un éditeur lent). La rangée du curseur,
souvent en cours d'écriture, n'est lue qu'une fois stable ou quittée. Quand un
cadre d'application apparaît (lancement de Word, d'EDIT, de QBasic), le premier écho
lit aussi les rangées hors du cadre (barre de menus, ligne d'état, titre) sur toute la
largeur, sans les filets ; ensuite, l'écho reste limité au cadre. Sur une machine
rapide, Word dessine toute sa fenêtre en un passage : sans cette règle, seul son
document vide était comparé et rien n'était lu au lancement. Les lignes
sont ajoutées à la file vocale sans couper la parole en cours, chacune suivie
d'un écho `@E` qui neutralise l'extension `burst-interrupt` d'Apolo-emu ;
Ctrl vide la file. Les changements d'attributs seuls (surbrillance, clignotement)
ne sont pas des échos. Le texte qui défile entièrement entre deux passages du
timer n'est pas vu : pour une longue sortie, utiliser `| MORE`. Dans un éditeur,
les messages dessinés dans la zone de texte sont lus ; désactiver l'écho avec
Inser + plus si cela gêne.

Une surbrillance n'est annoncée que si la disposition des attributs de la rangée
a changé après une touche autre qu'une frappe : un compteur qui change de texte
dans une zone colorée fixe (barre d'état) n'est pas une surbrillance, et un mot
coloré par QBasic après sa saisie non plus.

Une barre remplie de ▒ (barre de titre d'une boîte de Word qui apparaît avant
que le curseur n'y entre) n'est pas une surbrillance. Dans Word, le cadre du
document (bord haut « ■═══ Document1 ═══ » sans coin) est reconnu : les lignes
sont lues sans le bord « │ », et la ligne d'état (« Pg1 Co12 ») et la ligne de
message, hors du cadre, ne sont plus relues pendant la frappe ni après Entrée.
La marque de fin de document (◆, 04h) et les autres glyphes sous 20h ne
comptent pas comme du texte nouveau.

### Boîtes de dialogue

Les applications bâties sur la bibliothèque « Character Windows » de Microsoft
(Word 6.0 pour DOS, EDIT, QBasic, DOSSHELL, Works…) placent le curseur matériel
dans le contrôle actif et dessinent les mêmes motifs. `epaudos/src/dialog.c`
les reconnaît autour du curseur, sans état ni appel DOS ; il ne contient rien
de propre à Word et peut servir à toute application du même genre.

| Motif sous le curseur | Annonce |
| --- | --- |
| `Libellé: [valeur·····]` (Word) ; zone encadrée de 3 rangées, libellé à gauche ou au-dessus (EDIT) | libellé, « champ », valeur ou « vide » |
| `[X] Libellé`, `[ ] Libellé` | libellé, « case à cocher », « cochée » / « non cochée » |
| `(•) Libellé`, `( ) Libellé` | libellé, « bouton radio », « sélectionné » / « non sélectionné » |
| cadre de 3 rangées avec un texte (Word, double trait s'il est actif) ; `< OK >` (EDIT) | libellé, « bouton » |
| cadre plus haut, libellé dans le bord ou au-dessus | libellé, « liste », élément sous le curseur |

La boîte est la zone de même fond que le contrôle. Son titre est dans le bord
supérieur (EDIT) ou dans une barre d'une autre couleur juste au-dessus (Word).
Une zone qui touche un bord de l'écran est refusée : c'est une fenêtre de
document, dont le texte peut contenir des crochets ou des parenthèses.

- À l'ouverture : titre, « boîte de dialogue », puis le contrôle actif. Si le
  focus est sur un bouton (boîte d'alerte), le texte libre de la boîte est lu
  entre les deux (« Enregistrer les changements sous Document1? »).
- Tab, Maj+Tab, raccourcis : le nouveau contrôle ; le titre d'un groupe
  (« Position groupe ») n'est dit qu'en y entrant.
- Espace sur une case, flèche dans une liste : seul le nouvel état ou le nouvel
  élément. Une flèche sans effet dans une liste redit l'élément.
- Frappe, Retour arrière, Suppr, Gauche/Droite dans un champ : écho clavier
  ordinaire.
- Fermeture : la ligne du curseur, une fois l'écran restauré, sans relire le
  texte que la boîte cachait.
- Inser + Haut ou Inser + 5 : la boîte et le contrôle actif ; Inser + T : le
  titre ; la revue et Inser + B se limitent à la boîte.

Dans une boîte reconnue, l'écho écran et la détection de surbrillance sont
suspendus : un contrôle en remplit souvent un autre (EDIT recopie le répertoire
choisi dans le nom du fichier). Word déplace un instant le curseur pour
dessiner : un contrôle n'est annoncé qu'après avoir été vu deux passages de
suite sur un écran stable, et la boîte n'est tenue pour fermée qu'après trois
passages sans contrôle (environ 0,1 et 0,17 s). Le braille affiche le champ ou
l'élément de liste avec son curseur, sinon le texte de l'annonce.

Les ponctuations ne sont pas ajoutées : chaque morceau est une proposition
Apollo séparée (CR), pour ne pas être nommé en mode « toutes ».

Le dire tout utilise les vrais échos Apollo `@E`, y compris pour les lignes
vides. Il attend l'accusé avant de passer à la ligne suivante ; toute touche
d'application l'interrompt. Il ne fait pas défiler l'application et ne connaît
que le texte actuellement visible. `@` dans le texte n'est jamais transmis
comme début de commande Apollo.

## Réglages

Dans Inser+E, Haut/Bas choisit le réglage et Gauche/Droite sa valeur. Entrée ou
Échap ferme le menu et demande la sauvegarde dans `EPAUDOS.INI`, à côté du COM.
Le menu rend automatiquement le clavier après une minute sans commande.

La sauvegarde attend un appel DOS d'inactivité (INT 28h) et annonce sa réussite
ou son échec. Si l'application ne passe jamais par cet appel, les valeurs restent
actives en mémoire ; revenir à l'invite ou lancer `EPAUDOS /SAVE` pour enregistrer.
Aucun accès aux fichiers DOS n'est effectué depuis le timer ou le clavier.

```ini
Debit=3
Hauteur=8
Voix=1
Ponctuation=1
Echo=0
Majuscules=0
EchoEcran=1
```

Débit : 1–9 ; hauteur : 0–15 ; voix : 1–6. Ponctuation : 0 aucune, 1 naturelle,
2 noms des signes. Écho : 0 caractères, 1 mots, 2 caractères et mots, 3 aucun.
Majuscules : 0 non, 1 oui. Écho écran : 0 non, 1 oui ; Inser + plus du pavé le
bascule sans enregistrer. Les noms des signes couvrent notamment ceux utilisés
dans QBasic ; la table n'est pas exhaustive.

## Braille

Baum à 19200 bauds, 8N1, **COM3 03E8 / IRQ9**, comme Épaulard. COM1 utilise
03F8 / IRQ4 à 9600 bauds. COM2 reste disponible pour le matériel existant.
La traduction française TBFR2007 reste dans le TSR ; la table d'Épaulard est
convertie de CP1252 vers CP850 à la compilation, avec ses attributions liblouis.

- FWINLT / FWINRT : défilement horizontal.
- LNUP / LNDN : revue à la ligne précédente / suivante ; dans les réglages,
  choix du réglage précédent / suivant.
- HOME : revenir au suivi ; dans les réglages, revenir au début de la ligne.
- ROUTE : placer le point de revue sur le caractère et le dire. Cette commande
  ne place pas le curseur de l'application et n'effectue aucun clic.

Une seule ligne de 1 à 80 cellules ; curseur en points 7–8. Les messages et le
menu de réglages sont aussi affichés sur la plage. Pas de saisie braille,
de braille abrégé ni de routage des messages.

## Veille et architecture

Le timer lit le mode vidéo BIOS : hors des modes texte 0–3 et 7, il arrête la
lecture et libère les touches. Les notifications Windows INT 2Fh 1605h/1606h
suspendent le lecteur pendant la session Windows ; COM1 et COM3 sont alors
laissés à Épaulard. À la sortie de Windows, les UART et l'état du lecteur sont
réinitialisés au premier passage du timer.

Exception, le **mode invité** : quand Épaulard relaie une fenêtre DOS active
(plein écran ou fenêtre), Épaudos lit cette machine virtuelle comme sous DOS,
sans toucher aux ports. Sa parole passe par une boîte aux lettres (INT 2Fh
AX=ED02h → ES:BX, structure `EDBOX` de `core.h`) qu'Épaulard vide vers COM1 ; les
échos `@E` lui reviennent par la même boîte. Le braille aussi (boîte version 2) :
ses trames Baum restent dans la file du port 1, Épaulard en tire les cellules et lui
rend les touches de la plage. La parole et la trame braille préparées pendant un
passage partent à la fin de ce passage (`send` dans `core.c`) : sous Windows, une
machine DOS inactive n'a parfois plus de passage pendant des secondes. La
machine suivie est celle qui reçoit les touches (INT 2Fh 1683h). Sans signe de vie
d'Épaulard pendant 2 s, Épaudos se rendort. Voir [l'invite MS-DOS](invite-dos.md).
C'est pourquoi le choix Windows du menu charge aussi Épaudos avant `WIN`.

`tsr.asm` installe les interruptions et fournit une pile privée de 2 Kio pour
le moteur C et une pile de 512 octets pour le clavier. Le moteur s'exécute après
le BIOS INT 8h, PIC acquitté, avec les interruptions permises : la réception
série et le clavier peuvent continuer pendant l'analyse. La file des commandes
est protégée par de très courtes sections critiques. Le clavier passe par
l'interception BIOS INT 15h/4Fh ; Épaudos
n'acquitte pas lui-même le contrôleur clavier. Les émissions UART utilisent
deux petites files et les interruptions THRE, pour ne pas être limitées à
un caractère par tick BIOS. Aucun chemin résident n'attend la disponibilité
du port série. La réception utilise aussi les interruptions UART et deux files,
pour ne pas perdre les réponses reçues entre deux passages du timer, même sur
un 8250 sans FIFO. Chaque ISR traite toutes les conditions signalées par l'IIR
avant l'IRET : un THRE resté derrière une réception (écho `@E` arrivant pendant
l'émission) ne produirait pas de nouveau front sur le 8259 et figerait la file ;
`hw_tx` relance en outre l'émission à la main si la file est pleine et l'UART libre. Le récepteur Baum est incrémental et accepte les ESC bruts des
réponses ; les ESC des cellules émises sont doublés.

Les sources sont en UTF-8, converties en CP850 dans `epaudos/build/`. Le moteur
`core.c` est aussi compilé pour Linux dans les bancs, sans réimplémentation de
ses algorithmes.

## Essais et limites

```sh
python3 -m unittest tests.test_epaudos -v
PC486_DOS_TESTS=1 python3 -m unittest tests.test_epaudos_vm -v
```

Le banc natif couvre la revue, les réglages, la veille, les échos de fin de
parole, le protocole Baum, les cadres, les surbrillances et une sortie série
indisponible, ainsi qu'une touche reçue pendant le traitement de l'écran. Il
couvre aussi le défilement sous curseur fixe avec ligne d'état, Droite malgré
une ligne d'état qui change, le rappel d'historique, la frappe (simple, groupée,
par mots, colorée), Retour arrière, Suppr, et l'écho écran : sortie sans touche,
défilement, rangée du curseur attendue stable, Entrée à l'invite et dans un
éditeur, bascule et file pleine. Les scénarios `word`, `dialog` et `editdialog`
reproduisent les écrans relevés sous QEMU : ligne d'état de Word jamais lue,
marque de fin à Entrée, boîtes Caractère et d'alerte de Word (champ, cases,
groupe de boutons radio, boutons, curseur déplacé pendant le dessin, barre de
titre prise pour une surbrillance), boîtes Ouvrir et Chemin de l'aide d'EDIT.
Le scénario QEMU emploie une copie privée sous
`work/vmtest/epaudos/`, Apolo-emu muet et une fausse plage Baum ; il ne touche
pas au disque principal. Il refuse de copier celui-ci pendant son utilisation.

Les essais QEMU valident le chargement, la réduction réelle du bloc mémoire DOS,
l'aide, la sauvegarde des réglages, la revue, les accusés COM1 du dire tout,
les trames et un curseur routine Baum, l'écho écran d'une commande et sa bascule,
la saisie et la lecture dans EDIT (dont PgPréc, Bas et Haut après défilement) et
QBasic, un déplacement de menu dans DOSSHELL, puis la veille/reprise en VGA graphique et pendant une vraie session
Windows. Ils vérifient aussi que la pile privée n'est pas épuisée. La parole
et la plage y sont simulées ; ce n'est pas un essai avec le matériel physique.

Les heuristiques de fenêtres et de couleurs doivent encore être affinées sur
les applications et les thèmes. Seules les boîtes de type Character Windows
nomment les contrôles par rôle ; les listes déroulantes de Word sont annoncées
comme des champs, la position dans une liste (« 3 sur 12 ») et les raccourcis
soulignés ne sont pas dits. Il n'y a pas d'historique des lignes sorties
de l'écran, de revue au-delà de l'écran ni de lecture garantie de toute la sortie
d'une commande rapide : l'écho écran compare l'écran 18 fois par seconde et ne
voit pas ce qui défile entièrement entre deux passages. Un crochet INT 10h/0Eh ou
INT 29h, qui capterait la sortie DOS caractère par caractère comme Sonolect,
reste à écrire. Le curseur vient de la zone BIOS ; un programme qui programme
le CRTC directement sans passer par INT 10h n'est pas suivi. Les programmes qui remplacent INT 9h en contournant
l'interception BIOS peuvent également contourner les commandes d'Épaudos.
Word 6.0 pour DOS, installé par `scripts/installer --word-dos` en mode Texte avec
le clavier « pour résidents », a été essayé sous QEMU : Inser+H, l'écho de la frappe
et Inser+Haut y fonctionnent, ainsi que le suivi des boîtes Ouvrir, Caractère et
d'enregistrement à la fermeture (28 octobre 2026, QEMU muet). Au lancement de Word,
l'écho écran lit encore l'écran d'accueil et une fois la ligne d'état. La plage
physique et 86Box restent à valider pour ce nouveau TSR.
