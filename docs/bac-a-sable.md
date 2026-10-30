# Bac à sable : installer et auditer des logiciels Windows

`scripts/bac-a-sable` installe un logiciel dans une **copie** du disque, avec
Épaulard chargé : ce qu'il dit pendant l'installation est le premier audit
d'accessibilité. Il ne touche jamais `vm/win311.img`.

```sh
scripts/bac-a-sable preparer [--depuis IMG] [--remplacer]   # copie (VM arrêtée)
scripts/bac-a-sable produits
scripts/bac-a-sable installer excel media/excel5c-fr/…zip   # 5.0c, 1,2 Mo
scripts/bac-a-sable installer excel50 media/excel5-fr/…zip  # 5.0, 1,44 Mo
scripts/bac-a-sable installer access media/access2-fr/…zip
scripts/bac-a-sable installer amipro media/amipro-fr/lotusamifr.zip --timeout 900
scripts/bac-a-sable maj        # recompile Épaulard/Épaudos, les copie avec AUTOEXEC.BAT
```

Une installation déjà faite est refusée (ACME ne réinstalle pas en mode `/Q`) ; après
un échec, WIN.INI est rendu tel qu'avant et INSTAUTO retiré.

Image : `work/bac/win311.img`, ou `PC486_BAC_DISK` (un second bac à sable, par
exemple `work/bac50/win311.img`). Verrou partagé avec `scripts/pc486`.
`installer` reprend les fonctions de `scripts/installation.py` (ACME `/Q1`,
`INSTAUTO.EXE`, réponses aux installateurs à questions), garde Épaulard dans
`load=` et rend CONFIG.SYS et AUTOEXEC.BAT d'origine. Journaux dans
`work/bac/journaux/<produit>-…/` : `verification-com1.log` (octets envoyés à
l'Apollo II : tout ce qui a été dit), `<produit>-instauto.log` (titres des
fenêtres), captures en cas d'échec. Pendant l'installation, le second moniteur
QMP `observe.sock` du dossier `/tmp/pc486-install-*` permet `vmctl.py shot` et
`key` (`PC486_QMP=…`) sans gêner l'installateur.

Audit dans Windows ensuite :

```sh
PC486_DEV_DISK=$PWD/work/bac/win311.img PC486_DEV_RUN=$PWD/work/bac/run \
PC486_DEV_LOGS=$PWD/work/bac/logs scripts/pc486-dev pc486 start --qemu --headless --no-braille
```

Texte dit : `work/bac/logs/apolo-emu.log`. Commandes de diagnostic : Inser+Maj+D
(arbre des fenêtres), Inser+Maj+O (textes du modèle hors écran, avec fenêtre,
position et police).

## Audit du 29 octobre 2026

| Logiciel | Installation | Constats, corrections |
| --- | --- | --- |
| Excel 5.0c FR (ACME, cabinets) | sans intervention, 1 min | « Annuler, bouton » répété à chaque fichier → corrigé (focus rendu sans touche : pas redit). Jauge sans texte : pas de pourcentage. |
| Excel 5.0 FR (ACME) | sans intervention, 1 min | idem. |
| Access 2.0 FR (ACME) | sans intervention | « Annuler » dit une fois. |
| Ami Pro 3.01 FR (Lotus) | réponses d'INSTAUTO, sans intervention | Boutons à icône du menu dits « bouton » → corrigé (libellé à droite). Paragraphes de l'accueil au-delà de 512 caractères abandonnés → corrigé (texte des boîtes jusqu'à 1 000 caractères, 640 par paragraphe). |
| Excel 5.0c, utilisation | — | Grille illisible (« A B C D… ») → [module tableur](tableur.md). Format de cellule lu en entier à l'ouverture → titre seul. Texte des boutons OK/Annuler perdu au changement d'onglet (effacement sous une région à trou) → corrigé (`PtVisible`). Tab vers Orientation redisait « Bas » (cadre effacé en XOR) → corrigé. Plage sélectionnée muette → `InvertRgn` détourné. Présentation rapide du premier lancement non lue (image). |
| Ami Pro 3.01, utilisation | — | Document muet (« vide ») : curseur tracé en twips par `PatBlt PATINVERT`, sélection par `InvertRect` à hauteur négative → tailles jugées en points. Sélection décalée d'un mot (largeurs arrondies une à une) → arrondi cumulé. Lignes, mots, caractères, sélection (Inser+Maj+Haut), dire tout, menus et boîte Enregistrer sous lus. |
| Access 2.0, utilisation | — | Conseiller (fenêtre d'Aide toujours au premier plan, jamais active) : non lu, se ferme à la souris (case « Ne plus afficher »). Boutons à image (`CIRRUSSBUTTON`, texte « #bmpBlnkTbl ») → libellé voisin. Grilles (création, propriétés, feuille de données) muettes → [grilles à éditeur](tableur.md). Messages, Enregistrer sous, fenêtre de base de données lus. |
| Invite MS-DOS | — | Muette → [relais Épaudos](invite-dos.md). |
| Netscape Navigator 3.01 FR (InstallShield) | réponses d'INSTAUTO (`{ESPACE}` ajouté) | Boutons « Suivant > », « < Précédent » → chevrons retirés. Licence (zone en lecture seule) non lue → lue avec la boîte. |
| Netscape 3, utilisation | — | Page muette → titre puis lecture d'office, rangées sous Haut/Bas ; ligne sautée après Pg.Suiv. (puce haute de 46 px, défilement mal suivi) → corrigé. Lecture en boucle → corrigé. Champs de formulaire inaccessibles au clavier → Tab intercepté ; champs des Préférences vides (WM_GETTEXT vide) → modèle hors écran. Voir [reseau.md](reseau.md). |
| Communicator 4.08 FR (auto-extracteur InstallShield) | réponses d'INSTAUTO (`{BAS}` ajouté), 10 boîtes | Licence commençant par une ligne vide : « vide ». Réglages par `NETSCAPE.CFG` (empreinte recalculée). |
| Communicator 4.08, utilisation | — | Assistant de profil : titre et bouton seuls (EDIT cachés des pages suivantes) → texte lu ; pages suivantes muettes → lues au changement. Pages comme Netscape 3. |
| Eudora Light 3.06 (anglais) | réponses d'INSTAUTO, 6 boîtes | « DlgcacWinName » annoncé entre deux boîtes (fenêtre interne d'InstallShield). |
| Eudora Light, utilisation | — | Corps du message nommé « Attachments: » (libellé du champ au-dessus) → corrigé. Liste des messages, lecture, envoi, relève lus. |

À faire : formulaires et états d'Access, Conseiller d'Access, barres d'outils d'Ami Pro
(icônes sans texte), Présentation rapide d'Excel, Orientation du Format de cellule.
