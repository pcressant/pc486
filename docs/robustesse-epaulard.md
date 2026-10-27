# Robustesse des interceptions et de l’installation d’Épaulard

## Interceptions GDI et USER

`osm_install` ne réussit qu’après les onze interceptions. Un échec de résolution,
allocation de sélecteur ou conversion en alias retire les interceptions déjà posées.
Un second appel pendant une installation existante est refusé : il ne doit pas
sauvegarder un saut d’interception à la place des instructions système originales.
Chaque `GlobalFix` possède son `GlobalUnfix`, y compris sur les chemins d’échec.

Chaque interception compte les appels à la fonction originale. Le retrait restaure
immédiatement les instructions, mais conserve le sélecteur et la fixation jusqu’au
dernier retour actif. Ce retour ne repose pas le saut si un retrait a été demandé.
Les tests natifs injectent un échec à chacun des onze sélecteurs, un échec de
conversion, deux appels imbriqués et un retrait avant leurs retours. Un callback
GDI simulé appelle une autre interception, retire le modèle, puis fait retourner
une erreur à la fonction d’origine ; les instructions restent restaurées.

Cela reste une interception de code Win16, sans trampoline : pendant l’appel à
l’original, un nouvel appel à la même fonction échappe au modèle hors écran. Le
multitâche coopératif ne garantit pas l’absence de callbacks.

### Appel d’origine qui ne revient pas

Win16 n’a ni exceptions ni « finally ». Si une application meurt pendant l’appel
d’origine (faute dans son `WM_ERASEBKGND` sous le vrai `BeginPaint`, pointeur de
chaîne invalide passé à `TextOut`…), Windows termine la tâche et abandonne sa pile :
`patch_leave` n’est jamais exécuté. Les octets d’origine restaient alors en place
pour tout le système ; `busy` (texte) ou `scrolling` restaient pris et le modèle
hors écran cessait de noter le texte. L’ancien code, qui reposait le saut après
chaque appel, avait exactement le même défaut : le saut n’était reposé qu’au retour.

Chaque appel actif note sa tâche (`GetCurrentTask`) ; `busy` et `scrolling` aussi.
`osm_reap` repose le saut, ou achève un retrait différé, quand cette tâche n’existe
plus (`IsTask`). Il est appelé par le crochet WH_GETMESSAGE (n’importe quelle tâche,
un test de compteur dans le cas courant), par `osm_install` et par `osm_remove`.
Une tâche vivante qui attend une autre (SendMessage dans un rappel) garde son appel.

Si Épaulard se ferme pendant un appel encore vivant (boîte de message ouverte dans
un `WM_ERASEBKGND`, par exemple), `osm_remove` le signale et `EpRemove` garde la
DLL chargée par `LoadLibrary` jusqu’à la fin de Windows : le retour de l’appel ne
saute pas dans un segment libéré, et libère le sélecteur. Une nouvelle installation
dans la même instance de la DLL réutilise sélecteur et octets d’origine ; le saut
est reposé au retour de l’appel.

Limites : un `Throw` (Catch/Throw Win16) d’une application qui saute par-dessus
l’appel laisse une tâche vivante ; il n’est pas détecté. Un identifiant de tâche
réattribué à un nouveau programme retarde la récupération jusqu’à la fin de
celui-ci. Les tests natifs simulent la mort de la tâche par `longjmp` et un `IsTask`
factice ; ils n’établissent pas le comportement du noyau de Windows 3.11, à
vérifier sous Windows (application qui fait une faute dans son `WM_ERASEBKGND`).

## Tailles des chaînes dans epaulard.c

Les limites ci-dessous sont en octets ANSI (CP1252), après conversion à la compilation.
`TXT` vaut 256 ; les descriptions composées et `REVIEW.text` ont 512 octets.

| Chemins examinés | Borne et traitement |
| --- | --- |
| Caches de focus, sélection, menus, boîtes SDM, calculatrice et revue | Copies avec `lstrcpyn` et `sizeof` de la destination. |
| `describe_value`, EDIT, LISTBOX, COMBOBOX | Destination d’au moins `TXT`. Les lectures de listes exigent une longueur positive ou nulle et inférieure à `TXT`. « sélectionné » est ajouté par `append_keep(..., TXT)` : l’élément est raccourci (242 octets au plus) plutôt que l’état perdu. |
| `describe` | Tampon de 512. `fit_before` raccourcit le libellé pour garder le rôle, la valeur et « grisé ». |
| `append`, descriptions, menus et réglages | Contrat : destination de 512 octets. Contrôle de la somme des longueurs avant chaque concaténation. |
| `label_drawn`, `label_left` | Limite explicite transmise aux copies ; somme contrôlée avant concaténation. |
| `punct_words` | Limite explicite ; vérification du nom de signe, des espaces et du zéro avant copie. |
| `calc_say` | Tampon de 512 ; réserve de 16 avant chaque caractère, substitutions constantes de moins de 16 octets. |
| `calc_states`, noms et états de boutons | Chaînes constantes ; chaque état de 64 octets contient au plus 37 caractères. Les sorties de noms ont au moins `TXT` octets. |
| `cfg_load` | Chemin de 144 octets ; lecture du module limitée à 130, puis ajout de `EPAULARD.INI` (12 caractères et zéro). |
| `wsprintf` de nombres et commandes vocales | Entiers Win16 bornés ; `v[8]`, commande vocale `[32]`, valeurs de configuration contrôlées. |
| `wsprintf` de sélection et diagnostic | Sélection : 552 octets pour le compte et une constante courte ; diagnostic : 256 octets, classe limitée à 31 et champs numériques Win16. |
| Messages courts et réglages | Constantes ou tables fermées, indices de configuration validés ; sorties de taille au moins `TXT`. |
| `sa_caret_row` | Déplacement du suffixe par `memmove`, zéro compris : chevauchement explicitement permis. |

Les tests de listes (3, 242, 243 et 255 octets, sélectionnées ou non) gardent des
octets sentinelles après la destination et vérifient que « sélectionné » et
« grisé » restent annoncés. Les contrôles Win16 doivent respecter le contrat de leurs
messages de lecture : les messages historiques `LB_GETTEXT` et `CB_GETLBTEXT`
ne prennent pas de taille de destination et ne protègent pas contre un contrôle
personnalisé qui mentirait sur sa longueur.

## Braille modal

L’EXE laisse la plage à la DLL tant qu’une fenêtre système modale existe. Le
compteur d’époque invalide le cache à la prise de main et à la fermeture, même
sans changement de focus. Le banc vérifie le maintien du texte modal puis le
retour au même document, y compris après un défilement du texte modal. La minuterie réelle de la DLL et une plage physique
nécessitent toujours un essai sous Windows.

## Écriture des images et WIN.INI

`with-disk-lock.py` est commun au démarrage QEMU, au lancement 86Box et à
`epaulard/build.sh install`. Il verrouille un fichier compagnon de chaque image
pendant la commande ; le descripteur est hérité par l’émulateur. QEMU verrouille
C: et D: ; 86Box utilise les chemins `hdd_*_fn` de sa configuration. Les fichiers
`.pc486.lock` persistent : ne pas les supprimer pour débloquer une session.

Le contrôle complémentaire parcourt les descripteurs ouverts dans `/proc`, sans
se fier au nom de processus QEMU tronqué. Un autre émulateur sur une autre image
inspectable n’empêche pas l’installation. Ce verrou est consultatif : un outil
extérieur qui ne l’utilise pas peut encore ouvrir le disque après le contrôle.
Les précautions du guide restent applicables à mtools et aux lancements manuels.
Les scripts de création d’images neuves ne sont pas modifiés par ce changement.

`configure-epaulard.py` modifie uniquement `load` dans `[windows]`, conserve les
autres programmes, fusionne les entrées répétées et évite les doublons du chemin
complet d’Épaulard, sans distinction de casse. Il crée la section ou la clé si
nécessaire et conserve les fins de ligne CRLF. C’est la seule écriture de `load=` :
`epaulard/build.sh install` l’exécute, l’installateur importe `configure` et
`edit_load` (qui retire aussi `C:\INSTAUTO.EXE` après l’installation de Word).

Le scénario QEMU Word crée son document RTF dans la copie jetable : il ne dépend
plus du fichier privé `C:\ESSAIFR.DOC` de certaines anciennes images.
