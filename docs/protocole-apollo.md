# Sonolect 7.11 : pilotes de synthèse et protocole Apollo

Depuis le 27 octobre 2026, Sonolect n'est plus installé ni lancé par le projet :
Épaudos l'a remplacé sous DOS. Cette analyse reste la référence du protocole
Apollo tel que Sonolect l'emploie, utile pour Apolo-emu.

Analyse par désassemblage de `SONOLECT.COM` (41 967 octets, identique dans
`snl711.rar` et en téléchargement séparé ; sha256 a237d668…a3c2).
Désassemblage complet : `work/archives/sonolect.asm` (ndisasm, origine 0x100).

## Configuration (SONOLECT.INI)

Écrit par SNLINST. Une entrée par synthèse : `*` + 5 caractères.
`*a2sb1` = pilote `a`, variante `2` (Apollo 2), `s` = série, port `1` (COM1).
Valeur d'origine dans l'archive : `*s+pca*s-pca` (Synthé 3+ parallèle).
En mémoire, emplacement de 9 octets à l'adresse 0x15C : code, variante,
3 octets, port LPT (mot), port COM (mot, 0x3F8 = COM1).

La configuration utilisée était `*a2sb1` (Apollo 2, série, COM1) ; le fichier
`dos/sonolect.ini.apollo2-com1` qui la contenait a été retiré du dépôt.

## Table des pilotes (adresse 0x178, 11 octets par entrée)

Code + 5 routines : initialisation, arrêt/réglages, envoi filtré, envoi brut, fin de message.

| code | liaison  | synthèse (déduite)                  |
|------|----------|-------------------------------------|
| a    | série    | Dolphin Apollo 1 / Apollo 2 (variante `2`) |
| v    | série    | 4800 bauds, commandes ESC (Keynote Gold ou Synthé 3+N série) |
| x    | série    | 9600 bauds, commandes ESC + 0x03/0x04 |
| k    | série    | routines à 0x967A                   |
| t    | carte    | Télévox / PSOLA                     |
| s, y, z | parallèle / carte | famille Synthé 3 d'Electrel |

Liaison série : UART 8250 programmé directement (pas d'INT 14h), 8N1,
diviseur 12 (9600 bauds) ou 24 (4800 bauds), émission par scrutation de LSR,
sans contrôle de flux. Aucune lecture : la synthèse n'a rien à répondre.

## Protocole Apollo émis par Sonolect (pilote `a`)

| octets            | sens                                        |
|-------------------|---------------------------------------------|
| texte … `0D`      | message à prononcer                          |
| `18` (Ctrl-X)     | arrêt immédiat de la parole                 |
| `@W` + `1`…`8`    | débit (défaut 4)                            |
| `@A` + `0 1 2 3 5 7 A F` | volume, Apollo 2 seulement (défaut 3) |
| `@F` + `6`…`D`    | hauteur (défaut 9)                          |
| `@=` + chiffre    | langue                                      |
| `.`               | termine une série de réglages               |

Le texte est en page de codes 850 ; Sonolect l'écrit déjà « phonétiquement »
pour la synthèse (« poin », « bacslache », « croché goche »…).

Séquence au démarrage observée :
`18 40 57 34 40 41 33 40 46 39 2E` puis `  sonolect 7 poin onze b 0D`.

## Synthèse côté hôte : Apolo-emu

Depuis le 25 octobre 2026, la synthèse est l'Apollo II virtuel
[Apolo-emu](https://github.com/pcressant/apolo-emu), fourni par le flake, qui remplace l'ancienne passerelle `gateway/apollo2spd.py` (voir
l'historique git). Il suit le protocole documenté par Dolphin et la ROM 2.01 ; détails
dans [protocole-fr.md](https://github.com/pcressant/apolo-emu/blob/main/docs/protocole-fr.md). Points qui concernent ce projet :

- Une proposition se termine par CR **ou** par `. , : ; ! ?` : c'est pourquoi Sonolect
  clôt ses réglages par `.`. Le texte sans CR ni ponctuation attend la suite, comme sur
  l'appareil (l'ancienne passerelle le prononçait après 0,3 s de silence).
- Ctrl-X coupe la parole et efface tout ce qui est en file, échos compris.
- Débit : W3 = débit par défaut du moteur, W4 un peu plus rapide ; hauteur : F8 = défaut.
  Le volume `@A` est ignoré (`--no-volume` dans `scripts/pc486`).
- `burst-interrupt` (extension Apolo-emu activée par `scripts/pc486`) : un message qui
  arrive après plus de 40 ms de silence coupe la parole en cours. Ajoutée pour l'écho
  de la frappe de Sonolect, elle reste active, Épaulard ayant été validé avec elle.
  Jamais tant qu'un écho `@E` est attendu : l'écho écran d'Épaudos en profite.

## Dire tout d'Épaulard : écho `@E`

Épaulard envoie chaque ligne suivie d'un écho dans sa propre proposition :

    texte de la ligne 0D 40 45 <c> 0D

`@E<c>` (APOLLO.TXT, « echo character ») est traité quand la lecture atteint sa
proposition, donc après la fin de la ligne : l'Apollo II renvoie alors l'octet `<c>`
(`A` à `Z`). Épaulard envoie la ligne suivante d'avance et ne fait défiler le
document qu'au retour de l'écho. Un Ctrl-X efface l'écho avec la ligne. Sans retour
(synthèse débranchée), une durée estimée prend le relais.

L'ancienne extension `@I<c>.` n'existe pas sur l'Apollo II (un vrai appareil dirait
« I A ») ; elle n'est plus émise.
