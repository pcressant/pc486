# Braille Baum dans Épaulard

Première version du 25 octobre 2026 : sortie en braille informatique français
huit points (TBFR2007), suivi de la ligne et du curseur, défilement et revue par
les touches de la plage. Le braille est optionnel ; la parole conserve COM1.

## Installation

Construire les outils et installer Épaulard, **VM arrêtée**, depuis la racine :

```sh
nix build --impure -f tools/braille.nix -o tools/braille
nix build --impure -f tools/86box.nix -o tools/86box
epaulard/build.sh install --braille
scripts/pc486 start --braille
# ou : scripts/pc486 start --qemu --braille
```

L'installation sauvegarde d'abord le disque dans `work/sauvegardes/win311-avant-install-*.img`,
puis copie EXE/DLL et configure les INI. Elle refuse l'installation si un
processus QEMU ou 86Box est actif. Elle vise toujours `vm/win311.img`.
Sélectionner **2**, Windows, dans le menu DOS. Sous DOS (choix 1), Épaudos utilise
la même plage sur COM3 ; voir `docs/epaudos.md`.

Le port braille est **COM3, base 03E8, IRQ9**, à 19200 bauds, 8N1. COM1 reste
réservé à Apollo et COM2 à la souris série de 86Box. Notre correctif 86Box remplace
l'IRQ4 habituelle de COM3 par IRQ9 : utiliser impérativement la version reconstruite.
Le lanceur QEMU affecte également IRQ9 à sa troisième UART.

Dans `WIN.INI`, `[epaulard] BraillePort=3` active le pilote ; `BraillePort=0`
le désactive. `[ports] COM3:=19200,n,8,1` complète le réglage. Dans `SYSTEM.INI`,
`[386Enh]` reçoit `COM3Base=03E8`, `COM3Irq=9`, `COM3Buffer=1024`.
Ces réglages sont destinés au mode 386 étendu de Windows pour Workgroups 3.11.
La compilation seule (`epaulard/build.sh`) ne modifie aucun disque.

## Utilisation

La ligne courante provient du contrôle EDIT ou du modèle hors écran existant ;
les autres contrôles utilisent leur description. Les menus ont un affichage
propre, distinct de la parole. Le curseur de saisie est indiqué par les points
7 et 8 fixes, ajoutés à sa cellule. Le texte sélectionné porte aussi les points
7 et 8, sous chacun de ses caractères : sélection d'une zone d'édition (EM_GETSEL),
zones inversées de Write et Word, texte en couleurs de sélection. Une tabulation occupe une cellule vide pour
conserver une correspondance directe caractère/cellule. Fin de ligne incluse,
le déplacement du curseur fait suivre la fenêtre braille horizontalement.

Les touches sont celles configurées dans BRLTTY pour la plage physique :

| Commande BRLTTY | Action dans Épaulard | Touche Baum virtuelle |
| --- | --- | --- |
| FWINLT / FWINRT | reculer / avancer d'une largeur de plage | d2 / d5 |
| LNUP / LNDN | ligne précédente / suivante en revue | d1 / d3 |
| HOME | revenir au suivi du curseur courant | d4 |
| ROUTE | placer le curseur ou le point de revue (voir ci-dessous) | curseur routine |

La revue braille ne déplace pas le curseur du document. Une modification du texte,
du focus ou du curseur ramène au suivi. Les lignes de revue n'affichent pas de
curseur de saisie. Les champs de mot de passe affichent seulement « mot de passe »
et ne sont jamais routés.

Curseur routine, selon ce qu'affiche la plage :

- contrôle EDIT (Bloc-notes, zones d'édition) : le curseur y est placé (EM_SETSEL) ;
- texte dessiné qui a le focus (Write, document Word) : un clic de souris simulé
  sur le caractère y pose le curseur ; le pointeur revient ensuite à sa place ;
- revue à plat (Inser + moins du pavé) : le point de revue va sur ce caractère,
  qui est prononcé ; un second appui sur le même curseur routine dans la seconde
  clique sur ce caractère (menu, bouton, icône). En revue à plat, les touches ligne
  précédente/suivante de la plage déplacent aussi le point de revue de la parole.

Dans une boîte de Word (SDM), la plage affiche l'élément du rectangle de focus,
avec son libellé, son rôle et sa valeur, comme la parole.

Les boîtes modales système (« Quitter Windows », certains messages d'erreur)
bloquent EPAULARD.EXE. Le pilote braille est donc dans EPAUHOOK.DLL : la DLL,
qui les annonce déjà à la parole, affiche aussi leur titre, leur texte et le
bouton qui a le focus, puis le bouton suivant à chaque Tab. Une minuterie de la
tâche de la boîte lit les touches de défilement de la plage (FWINLT/FWINRT,
HOME) jusqu'à sa fermeture ; l'EXE réaffiche ensuite sa propre ligne. Le routage
n'est pas disponible dans ces boîtes.

La passerelle utilise AT-SPI pour suivre la fenêtre de l'application QEMU nommée
`pc486` ou l'application 86Box. Elle vérifie aussi l'état toutes les secondes pour
rattraper les événements manquants. Un terminal titré « pc486 » ne prend pas la
plage. Priorités : priorité BrlAPI nulle hors focus, 50 avec le focus. Elle conserve les
dernières cellules et les restaure à la reprise. Un silence de six secondes sur
la liaison invité rend aussi la plage à l'hôte ; Épaulard réannonce le protocole
toutes les trois secondes pour permettre la reconnexion. La fermeture de 86Box
arrête la passerelle avec les autres relais.

`logs/braille.log` indique les connexions et leurs erreurs ; `scripts/pc486 status`
affiche l'état du processus. La passerelle est lancée automatiquement si `tools/braille` est construit.
`--braille` ou `PC486_BRAILLE=1` l'impose ; `--no-braille` ou `PC486_BRAILLE=0`
la désactive explicitement. Si AT-SPI est indisponible,
le mode automatique ne doit pas être remplacé par une prise de contrôle permanente
pour un usage normal.

## Architecture et référence NVDA

```text
Épaulard : texte CP1252 → points TBFR2007 → Baum sur COM3
  QEMU : socket com3.sock ─────┐
  86Box : PTY du troisième COM ├→ passerelle Python → BrlAPI → BRLTTY → plage
                             └← touches Baum ← commandes BRLTTY
```

`scripts/braille/baum.py` implémente le sous-ensemble série ; `gateway.py` gère
BrlAPI, les transports, le focus et la reconnexion. `epaulard/src/braille.c` gère
le port Win16, le parseur incrémental, les écritures partielles, les cellules et
le défilement. Il est lié dans EPAUHOOK.DLL et ses fonctions sont exportées : l'EXE
et la DLL (boîtes modales système) partagent ainsi un seul état et une seule trame
en cours, sans mélange d'octets sur COM3. `EpBrailleEpoch` signale à l'EXE que la
DLL a remplacé l'affichage. `brfr.h` contient la table issue des données liblouis 3.33.0
(`fr-bfu-comp8.utb` et `latinLetterDef8Dots.uti`, attribution et licence dans le
fichier). La traduction reste dans l'invité : Baum transporte des points.

La référence est le [pilote Baum de NVDA](https://github.com/nvaccess/nvda/blob/master/source/brailleDisplayDrivers/baum.py)
et l'[émulation QEMU](https://github.com/qemu/qemu/blob/master/chardev/baum.c).
Les requêtes commencent par ESC et la commande ; les ESC des arguments sont
doublés. Les réponses ont une longueur connue et des arguments bruts, y compris
la valeur ESC. L'activation `15 01` renvoie largeur `01`, identité `84` (16 octets)
et numéro de série `8A` (8 octets). L'affichage `01` transmet une ligne entière ;
les touches utilisent `24` et les curseurs routine individuels `27` (index à
partir de 1). Le pilote attend le premier relâchement des touches de navigation.

La passerelle assure le dialogue Baum pour QEMU et 86Box. Elle choisit le VT
de la session active si `CONTROLVT` n'est pas déjà défini.

## Limites et diagnostic

Limites de cette version : une seule ligne, 1 à 80 cellules, texte limité à
511 caractères ; pas de braille abrégé, de saisie au clavier braille, de joystick
ni de distinction entre curseur et sélection (tous deux en points 7-8). Le routage dans Write et Word
dépend de la position estimée des caractères. Dans une boîte modale système, seuls
le défilement et HOME sont lus. Le fonctionnement sur la plage physique sous 86Box
a été confirmé par l'utilisateur.

Le paquet Nix inclut GI et les typelibs AT-SPI, GLib et DBus. Il utilise le même
BrlAPI avec Polkit que le BRLTTY de l'hôte. Lancer depuis la session graphique
active : une session SSH inactive peut être refusée par Polkit même avec le
même utilisateur. `tools/braille/bin/pc486-braille --check` vérifie les imports
sans connexion à la plage. `--trace` sur la passerelle journalise RX/TX Baum ;
les messages « focus VM » et « accès plage » permettent de distinguer la liaison
série de l'arbitrage avec Orca. Ne pas utiliser l'état de `pc486 status` obtenu
dans un espace de PID isolé pour conclure que les disques sont libres : contrôler
les processus de l'hôte.
