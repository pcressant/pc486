# CD-ROM et réseau isolé (facultatifs)

Ces options ne changent rien tant qu'elles ne sont pas demandées : sans `--cdrom` ni
`--reseau`, le disque installé, la configuration générée pour 86Box et la ligne de
commande de QEMU restent ceux d'avant.

## Vue d'ensemble

```
Windows 3.11 (TCP/IP-32, NE2000 0x300 IRQ10, 10.0.2.15, passerelle 10.0.2.2)
  │ proxy HTTP 10.0.2.2:8080, POP3 10.0.2.2:110, SMTP 10.0.2.2:25 — rien d'autre
  ▼
QEMU : -netdev user,restrict=on + guestfwd   │   86Box : slirp dans un espace de noms
  │ (socat vers les sockets Unix)            │   réseau vide + relais (netns.py)
  ▼                                          ▼
run/web.sock, run/pop3.sock, run/smtp.sock : python3 -m reseau (scripts/reseau/)
  ├─ proxy : pages d'aujourd'hui (HTTPS) converties en HTML 3.2 ; Chromium facultatif
  └─ boîte aux lettres locale de test (Maildir), aucun vrai compte
```

## Préparer le disque

Installation neuve (voir [installation-automatique.md](installation-automatique.md)) :

```sh
scripts/installer … --cdrom media/udvd2/udvd2.zip --reseau media/tcpip32/tcp32b.zip
```

Disque déjà installé (Word, Excel… conservés) : `scripts/options-vm` copie le disque
arrêté sous le verrou des émulateurs, ajoute les options, vérifie Windows sans carte,
Windows avec carte, puis le DOS avec Épaudos (et E: avec `--cdrom`), et publie une
**nouvelle** image ; la source n'est jamais modifiée.

```sh
scripts/options-vm --source vm/win311.img --sortie work/win311-reseau.img \
    --cdrom media/udvd2/udvd2.zip --reseau media/tcpip32/tcp32b.zip
```

Remplacer ensuite `vm/win311.img` par la nouvelle image (VM arrêtée, ancienne image
gardée dans `work/sauvegardes/`) reste une décision de l'utilisateur.

Supports : `udvd2.zip` est le paquet FreeDOS de UDVD2 (Jack R. Ellis, « Free w/sources »),
tiré de `FD14LITE.img` (`packages/drivers/udvd2.zip`) ; `tcp32b.zip` est TCP/IP-32 3.11b
de Microsoft. Les fichiers réseau de WfW eux-mêmes viennent de `C:\WININST`.

## Lancer

```sh
scripts/pc486 start --reseau                 # 86Box, réseau isolé
scripts/pc486 start --cdrom media/x.iso      # lecteur E: avec une image ISO
scripts/pc486 start --cdrom                  # lecteur vide
scripts/pc486 start --qemu --reseau --cdrom media/x.iso
```

`--reseau` démarre d'abord les services de l'hôte (`logs/reseau.log`, une ligne par
page ou message) et les arrête avec la VM ; `pc486 status` les signale. Données du
courrier : `vm/reseau/courrier` (`PC486_RESEAU_DATA`) ; `scripts/pc486-dev` utilise
`work/dev/reseau`.

## Ce que voit la VM

| Destination | Résultat |
| --- | --- |
| 10.0.2.2:8080 | proxy web |
| 10.0.2.2:110, 10.0.2.2:25 | POP3 et SMTP de la boîte locale |
| tout autre port de 10.0.2.2, 10.0.2.254, DNS 10.0.2.3, Internet, ports de l'hôte | refusé |

- **QEMU** : `-netdev user,restrict=on` coupe tout trafic sortant ; seuls les
  `guestfwd=tcp:10.0.2.2:PORT-cmd:socat - UNIX-CONNECT:run/SERVICE.sock` répondent.
  libslirp refuse un `guestfwd` sur l'adresse de l'hôte virtuel : celui-ci est déplacé
  en 10.0.2.254 pour libérer 10.0.2.2. `hostfwd` (sens hôte → invité) n'est pas utilisé.
- **86Box** : aucun correctif. `scripts/reseau/netns.py` crée un espace de noms
  utilisateur + réseau (sans privilège, même uid), y active seulement la boucle locale,
  lance un relais `127.0.0.1:PORT → run/SERVICE.sock` pour chaque service puis exécute
  86Box. Son slirp traduit 10.0.2.2 en 127.0.0.1 de l'espace de noms : il n'y a ni route
  vers l'extérieur ni accès aux ports de l'hôte. Les sockets Unix nommées, les pty, X11,
  Wayland et PipeWire restent accessibles. Si le noyau refuse les espaces de noms
  utilisateur, 86Box ne démarre pas avec `--reseau` (jamais sans isolement).

Vérifié depuis Windows le 30 octobre 2026 par `tests/win16/sonde.c` (SONDE.EXE, Winsock),
sous QEMU **et** sous 86Box : les trois services répondent, les six autres destinations
échouent. Côté hôte : `tests/test_reseau.py` (`NetnsTests`) et
`tests/test_pc486_lifecycle.py` (86Box simulé dans son espace de noms).

## Dans l'invité

- `C:\WINDOWS\SYSTEM.INI` et `PROTOCOL.INI` reproduisent l'installation réseau faite
  avec le programme d'installation de WfW (NE2000 compatible, TCP/IP-32 seul, adresse
  fixe 10.0.2.15, masque 255.0.0.0, passerelle 10.0.2.2), plus : nom PC486, pas de
  partage, pas de boîte d'ouverture de session. `NET START` n'est pas nécessaire.
- Sans carte, WfW bloque sur « adaptateur MS$NE2CLONE ». `C:\WINDOWS\RESEAU.EXE`
  (`dos/src/reseau.c`), lancé dans la branche Windows d'AUTOEXEC.BAT avant `WIN`,
  cherche la NE2000 en 0x300 et bascule les seules lignes qui chargent le réseau,
  d'après la section `[pc486.reseau]` (`section/clé=avec carte|sans carte`, `-` = ligne
  absente). Il n'écrit que si le contenu change. `RESEAU OUI|NON` force un état.
- CD-ROM : `DEVICEHIGH=C:\DOS\UDVD2.SYS /D:PC486CD` (CONFIG.SYS) et
  `IF EXIST PC486CD LH C:\DOS\MSCDEX.EXE /D:PC486CD /L:E` (AUTOEXEC.BAT). Lecteur ATAPI
  maître du canal IDE secondaire (QEMU : présent par défaut ; 86Box : contrôleur
  intégré de la HOT-433A, `cdrom_01_parameters = 1, atapi`, canal `1:0`, IRQ15).
- Interruptions : COM1 IRQ4 (parole), COM2 IRQ3 (souris), Sound Blaster IRQ5, COM3
  IRQ9 (Baum), NE2000 IRQ10, IDE IRQ14/15.

**Limite** : sous QEMU à pleine vitesse (`PC486_ICOUNT=off`), Windows se bloque au
chargement des pilotes réseau de Microsoft ; au rythme normal (`PC486_ICOUNT=7`,
défaut) et sous 86Box, il démarre. `pc486` prévient ; l'installateur vérifie le
démarrage avec carte au rythme normal.

## Proxy web

Dans le navigateur : proxy HTTP `10.0.2.2`, port `8080`, pour HTTP (et FTP s'il le
demande) ; page d'accueil `http://pc486/` (recherche DuckDuckGo et quelques adresses).

- Les adresses s'écrivent en `http://` : le proxy essaie HTTPS puis HTTP. `CONNECT`
  (HTTPS direct) est refusé avec une explication.
- Les pages sont converties en HTML 3.2 (`scripts/reseau/html32.py`) : titres, liens,
  listes, tableaux de données (avec en-têtes), formulaires ; scripts, styles, éléments
  cachés et tableaux de mise en page retirés ; images remplacées par leur texte
  alternatif ; lien « Aller au contenu » vers `<main>`. Codage windows-1252 avec
  translittération (émojis supprimés).
- Pages longues coupées (`PC486_PAGE`, 60 000 caractères par défaut) aux limites de
  bloc sûres : « Page suivante » en bas de page (`http://pc486/suite`).
- Les adresses et formulaires saisis en windows-1252 sont réencodés en UTF-8 ; les
  liens de redirection des moteurs de recherche sont remplacés par leur cible.
- Fichiers (ZIP, EXE, images) transmis tels quels, jusqu'à 64 Mo.
- **Liens numérotés** : chaque lien porte son numéro (« Wikipédia [1] ») et le haut de
  chaque page un champ « Aller au lien n° » : taper le numéro puis Entrée suit le lien
  (`http://pc486/lien`, pages gardées en mémoire : 100). `PC486_LIENS=0` les retire.
  Un lien dont le texte n'existe pas en windows-1252 (langues de Wikipédia en
  arabe, japonais…) prend son titre, sinon son code de langue.
- **Structure des pages** : le proxy garde les pages envoyées et sert leur structure
  (`http://pc486/tampon?url=…`, `scripts/reseau/tampon.py`) au mode navigation
  d'Épaulard ; voir [navigation-web.md](navigation-web.md). Les liens suivis sont
  notés dans `visites.txt` (dossier de données) pour « lien visité ».
- **Chromium (facultatif)** : s'il est trouvé (`PC486_CHROMIUM` ou `PATH`), chaque page
  propose en bas « Version JavaScript », rendue par `chromium --headless --dump-dom`
  avant conversion. Sans Chromium, ce lien n'apparaît pas et rien d'autre ne change.

## Courrier de test

SMTP `10.0.2.2`, POP3 `10.0.2.2`, nom et mot de passe quelconques. Tout message envoyé,
quel que soit le destinataire, arrive dans la boîte unique ; rien ne quitte l'hôte. Un
message de bienvenue est déposé une fois. Pas de DNS : saisir l'adresse IP comme nom de
serveur.

## Tests

```sh
python3 -m unittest tests.test_reseau tests.test_pc486_lifecycle -v
PC486_NET_VM_TESTS=1 python3 -m unittest tests.test_reseau_vm -v   # VM principale arrêtée
```

`tests/test_reseau.py` : conversion HTML 3.2, proxy face à un faux site local
(réencodage, pagination, fichiers, repli HTTP, Chromium simulé), POP3/SMTP, module
complet, espace de noms et banc natif de RESEAU.EXE (`tests/dos/reseau-test.c`).
`tests/test_reseau_vm.py` : latence de la parole et de la plage sous charge réseau.

## Latence sous charge réseau

Demande : ne pas supposer qu'IRQ9 (plage) passe avant IRQ10 (carte), mesurer. Banc
`tests/test_reseau_vm.py`, QEMU au rythme normal (`PC486_ICOUNT=7`, ~8 MIPS), Bloc-notes
de 15 lignes : délai entre une flèche Bas et la ligne dite (COM1), puis affichée sur la
fausse plage Baum (COM3) ; 14 flèches et un dire tout, sans charge puis pendant un
téléchargement POP3 continu de SONDE.EXE (client Win16 piloté par messages, comme un
navigateur). Mesures du 30 octobre 2026 (médiane / 90 % / maximum, parole) :

| Débit vers la VM | Sans charge | Sous charge | Dire tout (15 lignes) |
| --- | --- | --- | --- |
| illimité | — | Windows ne lit plus le clavier | — |
| 64 Ko/s | 59 / 88 / 175 ms | 224 / 236 / 244 ms | 6,7 s → 6,8 s |
| **32 Ko/s (défaut)** | 93 / 123 / 171 ms | 62 / 97 / 137 ms | 6,7 s → 6,7 s |

La plage suit la parole à quelques millisecondes près dans tous les cas ; aucune ligne
perdue ni déplacée dans le dire tout (échos `@E` reçus sur COM1 sous charge).

Sans limite, le réseau virtuel livre les données aussi vite que l'invité les prend : le
486 émulé passe tout son temps dans la NE2000 et TCP/IP-32, et Windows entier cesse de
répondre au clavier. Les services de l'hôte bornent donc le débit **total** envoyé à la
VM (`scripts/reseau/debit.py`, seau à jetons commun au proxy et à POP3) :
`PC486_DEBIT` en Ko/s, 32 par défaut (une page de 60 Ko en 2 s), `0` sans limite.
Sous 86Box (DX4/100), le processeur émulé est plus rapide : un débit plus élevé y est
probablement supportable, à mesurer avant de le changer.

Limite du banc : QEMU retient les octets série tant que l'UART de l'invité est plein,
une perte d'octets Baum ou Apollo n'y est donc pas observable, seulement le retard.
Sous 86Box, l'isolement a été vérifié dans Windows ; la latence sous charge reste à
mesurer avec la vraie plage.

## Navigateurs et courrier

Installés par `scripts/installer --netscape`, `--communicator`, `--eudora` (voir
[installation-automatique.md](installation-automatique.md)), déjà réglés sur ces
services. Essayés sous QEMU (`--reseau`) le 30 octobre 2026.

- **Netscape Navigator 3.01** et **Communicator 4.08** (français) : au chargement d'une
  page servie par le proxy, Épaulard dit son titre puis la lit d'office en **mode
  navigation** comme NVDA (curseur virtuel, H, K, T, X…, Inser+Espace pour le mode
  formulaire) : voir [navigation-web.md](navigation-web.md). Pour une autre page, il
  la lit d'office dès qu'elle est dessinée (aucune touche entre-temps, focus dans la
  page) ; Haut/Bas lisent alors la rangée qui apparaît, Inser+Bas relit tout. Tab et Maj+Tab, interceptés par Épaulard dans la page, vont
  aux champs de formulaire dans l'ordre de lecture (Netscape 3 n'y va pas au
  clavier), la page défilant jusqu'au champ, puis reviennent à la page ; un champ est
  nommé par le texte dessiné à sa gauche (« Aller au lien n° »). Après un lien suivi
  depuis un champ, le focus est rendu à la nouvelle page. Menus, préférences et
  boîtes sont lus comme ailleurs.
- **Communicator** : au premier lancement, l'assistant de profil (une seule boîte dont
  les pages s'affichent tour à tour) est lu page par page ; nom, adresse et serveurs
  sont déjà proposés (pc486, 10.0.2.2).
- **Eudora Light 3.06** (anglais) : liste des messages (expéditeur, date, taille,
  sujet), message ouvert lu par lignes ou en entier (Inser+Bas), rédaction (To,
  Subject, Cc, Bcc, corps), envoi (Ctrl+E) et relève (Ctrl+M, mot de passe
  quelconque) vers la boîte locale.

Limites : les onglets des Préférences de Netscape sont annoncés « bouton » ; une
puce « · » isolée peut être lue ; les textes d'exemple d'un assistant (« (ex. Jean
Dupond) ») sont lus avec l'explication de la page ; la licence de l'installateur de
Communicator commence par une ligne vide, annoncée « vide » (flèches pour la lire) ;
les boîtes InstallShield annoncent parfois « DlgcacWinName » (fenêtre interne).
