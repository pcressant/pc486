# Invite MS-DOS sous Windows

Sous Windows en mode amélioré, une session MS-DOS (icône « Invite MS-DOS »,
`COMMAND.COM`, ou tout programme DOS) tourne dans sa propre machine virtuelle.
Son écran texte ne passe ni par USER ni par GDI : Épaulard ne peut pas le lire.
Ses touches non plus : en fenêtre comme en plein écran, Windows les envoie
directement à la machine DOS, sans passer par le crochet clavier d'Épaulard.

C'est donc **Épaudos** qui lit l'invite, comme sous DOS, avec ses propres
commandes (Inser+flèches, pavé, Inser+E…, voir [Épaudos](epaudos.md)). Épaulard ne
fait que transmettre sa parole à l'Apollo II et son braille à la plage Baum : la
plage montre la ligne d'Épaudos (curseur en points 7-8), ses touches de défilement,
de ligne et de routage vont à Épaudos, comme sous DOS.

## Utilisation

- Le choix Windows du menu de démarrage charge Épaudos avant `WIN`
  (`dos/AUTOEXEC.BAT`). Sans lui, la fenêtre DOS n'est annoncée que par son titre.
- En arrivant sur une fenêtre DOS (Alt+Tab, lancement, Alt+Entrée), Épaulard dit
  son titre, puis Épaudos annonce « Épaudos prêt » et lit l'écran.
- Alt+Tab vers un programme Windows rend la parole et le clavier à Épaulard ;
  `EXIT` ferme la session.
- Fonctionne en plein écran et en fenêtre (Alt+Entrée), parole et braille. Juste
  après le passage en fenêtre, l'écran virtuel peut être haché (un caractère sur
  deux, dans la fenêtre aussi) jusqu'à ce que le programme le redessine : Épaudos
  lit et la plage affiche alors ce qui est réellement affiché.
- Fluidité mesurée sous QEMU (`test_5_dos_prompt_braille`) : une frappe apparaît
  sur la plage en 0,13 à 0,27 s, lancement de la touche compris, en plein écran
  comme en fenêtre ; la parole part au même moment.

## Fonctionnement

| Côté | Rôle |
| --- | --- |
| `epaudos/src/dos.c` | `ed_box` (structure `EDBOX`, `core.h`) : signature `EPBX`, drapeau `relay` posé par Épaulard, compteur `alive`, machine suivie `vm`, et les adresses des files `uart_buf`/`uart_rxbuf` du port 0. `hw_guest()` décide : dormir, lire cette machine, ou passer (autre machine). |
| `epaudos/src/tsr.asm` | INT 2Fh AX=ED02h → ES:BX de la boîte ; `cur_vm` (INT 2Fh 1683h). |
| `epaudos/src/core.c` | Sous Windows, `ed_tick` et `ed_scan` consultent `hw_guest` ; en mode invité, aucun accès aux ports : la parole reste dans la file. |
| `epaulard/src/dosbox.c` | Fenêtre active de classe `TTY` : trouve la boîte (DPMI 0300h, INT 2Fh simulée en mode réel ; 0002h pour le sélecteur), pose `relay`, incrémente `alive` toutes les 100 ms, envoie la file à COM1 et rend les octets reçus de l'Apollo II (échos `@E`) à Épaudos. Épaulard se tait (`poll`) tant que dure le relais. Braille : voir ci-dessous. |
| `epaulard/src/braille.c` | Mode relais (`br_relay`) : affiche les cellules données par `br_cells`, rend touches (`ESC 24`), routage (`ESC 27`) et largeur (`ESC 01`) en trames pour Épaudos (`br_relay_read`). |

### Braille

Boîte version 2 : la file du port 1 d'Épaudos (indices `head+1`, `tail+1`, données
`buf+256` ; réception `rxhead+1`, `rxtail+1`, `rxbuf+256`) porte ses trames Baum.
Épaulard ne les recopie pas octet par octet : une trame coupée à l'entrée ou à la
sortie de la fenêtre DOS mélangerait deux émetteurs sur COM3. Il découpe les trames
(`ESC`, commande, données, `ESC` doublé ; recalage sur un `ESC` seul), donne les
cellules de `ESC 01` à son pilote, qui les envoie lui-même, et répond à la sonde
`ESC 15` par la largeur de la plage. En quittant la fenêtre, l'affichage d'Épaulard
revient.

### Fluidité

- Épaudos envoie la parole et la trame à la fin du passage qui les prépare (sinon
  la cellule attendait le passage suivant, parfois des secondes plus tard).
- Le pilote braille d'Épaulard écrit chaque trame dans l'appel qui la prépare.
- En plein écran, la machine système est en arrière-plan : Épaulard, qui attend un
  message, la laisse passer pour inactive, et elle n'est reprise que toutes les
  0,5 à 1 s (`WinTimeSlice` n'y change rien). Pendant 4 s après une frappe dans la
  machine DOS (compteur `keys` de la boîte) ou une sortie d'Épaudos, la boucle de
  messages d'Épaulard ne s'endort pas (`dos_spin` : `PeekMessage` puis relais) ;
  ensuite `GetMessage`, aucun cœur occupé par une invite qui attend. La première
  frappe après un silence peut encore attendre jusqu'à une seconde.
- Épaudos cède le reste de sa tranche de temps (INT 2Fh 1680h, depuis INT 28h)
  quand une file vers Épaulard n'est pas vide.

La boîte est en mémoire DOS chargée avant Windows, donc commune à toutes les
machines virtuelles. Chaque file a un seul producteur et un seul consommateur,
qui n'écrit que son indice. En quittant la fenêtre DOS, Épaulard coupe la parole
et rétablit ses réglages de voix (Épaudos a pu les changer).

## Limites

- Une seule machine DOS est suivie à la fois : celle qui reçoit les touches.
- Les commandes Inser d'Épaulard n'atteignent pas une fenêtre DOS (touches
  réservées à la machine virtuelle) ; celles d'Épaudos les remplacent.

## Vérifications

- Banc natif : `test_dos_box` dans `tests/win16/epaulard-test.c` (relais, échos,
  touches, sortie ; braille : sonde, cellules, touches et routage rendus, trame
  coupée, affichage d'Épaulard rendu ; boucle éveillée après une frappe), scénario
  `guest` de `tests/dos/core-test.c` (trames Baum en mode invité).
- Scénario QEMU `test_5_dos_prompt_braille` (`tests/test_vm_scenarios.py`) : fausse
  plage, plein écran puis fenêtre, délai de chaque frappe, routage, `EXIT`.
- Essai réel (bac à sable, 29 octobre 2026) : `COMMAND` en plein écran lu
  (bannière, `ver`, Inser+Haut), en fenêtre (`echo bonjour`), Alt+Tab aller-retour,
  `EXIT` puis reprise d'Épaulard.
