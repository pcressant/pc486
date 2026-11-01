# Manuel d’utilisation d’Épaulard et d’Épaudos

Épaulard est le lecteur d’écran de Windows 3.1 et de Windows pour Workgroups
3.11. Épaudos est le lecteur d’écran résident de MS-DOS. Tous deux proposent la
parole et le braille, avec des commandes de lecture proches. Ce manuel décrit
leur utilisation dans pc486 et la couverture connue des applications ; il ne
suppose pas de savoir compiler le projet.

Les fonctions décrites correspondent aux sources du dépôt. Les essais cités sont
ceux consignés dans les guides du projet au 31 octobre 2026 : une fonction testée
avec une synthèse ou une plage simulée n’est pas nécessairement validée avec le
matériel physique. La présence d’une application sur votre disque dépend de son
installation ; ce manuel n’est pas un inventaire du disque en cours d’utilisation.

## Sommaire

- [Démarrer et se repérer](#démarrer-et-se-repérer)
- [Principes et revue au pavé](#principes-et-revue-au-pavé)
- [Épaulard : fonctions et commandes](#épaulard--fonctions-et-commandes)
- [Épaulard : couverture des applications](#épaulard--couverture-des-applications)
- [Épaudos : fonctions et commandes](#épaudos--fonctions-et-commandes)
- [Épaudos : couverture des applications](#épaudos--couverture-des-applications)
- [Régler la parole](#régler-la-parole)
- [Utiliser la plage braille](#utiliser-la-plage-braille)
- [Dépannage et limites](#dépannage-et-limites)
- [Guides complémentaires](#guides-complémentaires)

## Démarrer et se repérer

Dans une installation pc486 configurée avec les lecteurs :

1. Démarrer la machine avec `scripts/pc486 start` depuis Linux, ou avec le
   lanceur habituel. 86Box est l’émulateur par défaut.
2. Le menu de démarrage DOS n’est pas vocalisé : aucun lecteur n’est encore
   chargé. Choisir **1** pour DOS avec Épaudos, ou **2**, puis **Entrée**, pour
   Windows avec Épaulard. Sans intervention, DOS démarre après dix secondes.
3. Sous DOS, Épaudos se charge automatiquement. Le choix Windows charge aussi
   Épaudos avant de lancer Windows ; Épaulard démarre ensuite automatiquement.
4. Appuyer sur **Inser+H** pour entendre l’aide du lecteur actif, puis sur
   **Ctrl seul** pour couper la parole si nécessaire.

Sous Windows, utiliser **Tab** et **Maj+Tab** pour parcourir les contrôles,
**Alt** pour les menus, les flèches pour y circuler, **Entrée** pour valider et
**Échap** pour revenir. **Alt+Tab** change d’application ; Épaulard annonce le
programme choisi pendant la bascule rapide de Windows. **Inser+T** relit le titre,
**Inser+Tab** le contrôle actif. Ces deux dernières commandes ont un sens différent
sous DOS, précisé plus loin.

Pour ouvrir un programme Windows, choisir son icône dans le Gestionnaire de
programmes et appuyer sur Entrée. On peut aussi utiliser son menu **Fichier,
Exécuter** et saisir, par exemple, `notepad`, `write` ou `calc`.

Pour arrêter la machine, enregistrer les documents et quitter les applications,
puis quitter Windows depuis le Gestionnaire de programmes avec Alt+F4. Une fois
revenu à l’invite DOS, l’arrêt par `scripts/pc486 stop` est possible. Fermer
seulement la fenêtre de l’émulateur ne constitue pas une fermeture de Windows.

## Principes et revue au pavé

Le **focus** est le contrôle qui reçoit les touches. Le **curseur de saisie**
indique où l’application insère du texte. Le **point de revue** sert à lire sans
déplacer ce curseur ni changer le focus.

**Inser** s’utilise comme un modificateur : la maintenir pendant l’appui sur
l’autre touche. Elle est réservée au lecteur. On peut utiliser la touche Inser
dédiée ou le **0 du pavé avec Verr Num éteint**. « Maj » signifie Majuscule,
et « Début » est aussi appelé Origine. Les signes moins, plus, / et * mentionnés
pour la revue sont ceux du pavé numérique.

Éteindre **Verr Num** pour utiliser les commandes suivantes, communes aux deux
lecteurs. Les chiffres s’appuient seuls, sans maintenir Inser.

| Unité lue | Précédente | Courante | Suivante |
| --- | --- | --- | --- |
| Ligne | pavé 7 | pavé 8 | pavé 9 |
| Mot | pavé 4 | pavé 5 | pavé 6 |
| Caractère | pavé 1 | pavé 2 | pavé 3 |

- **Revue normale**, mode initial : chaque commande repart du curseur ou de
  l’élément actif reconnu. Deux appuis sur 9 relisent donc la même ligne suivante.
- **Inser+7 du pavé** (mode de revue suivant) ou **Inser+1 du pavé** (précédent)
  passe en **revue à plat** : les commandes déplacent un point de lecture
  indépendant. Deux appuis sur 9 avancent de deux lignes. La même combinaison
  revient en revue normale.
- **Inser+moins du pavé** recentre la revue sur le curseur ou l’élément actif et
  le relit, sans changer le mode. Dans Épaudos, **Inser+5 du pavé** fait de même ;
  dans Épaulard, il dit l’objet navigateur (voir « Navigation par objet »).
- **Plus du pavé** lance le dire tout, comme Inser+Bas.

Les deux lecteurs reprennent les touches de NVDA (disposition « ordinateur de
bureau », Inser joue le rôle de la touche NVDA) : voir « Touches communes avec
NVDA » plus bas. Les anciennes touches restent valables quand NVDA ne les emploie
pas (Inser+H, Inser+E, Inser+Page suivante, Inser+D sous DOS).

Les flèches dédiées restent utilisables pour agir dans l’application. La revue à
plat reste limitée au texte visible ; elle ne fait pas défiler le document. Sous
DOS, elle reste dans le cadre reconnu ou, faute de cadre, dans l’écran entier.
Sous Windows, des contrôles côte à côte peuvent être lus sur une même rangée.

Pour explorer un écran sans agir dessus : passer en revue à plat, parcourir les
lignes avec 7 et 9, les mots avec 4 et 6, puis les caractères avec 1 et 3.
Recentrer avec Inser+moins du pavé avant de reprendre la saisie.

## Épaulard : fonctions et commandes

### Lecture automatique

Épaulard annonce les fenêtres activées, les menus et les contrôles atteints au
clavier : libellé, rôle, valeur et états disponibles, comme « cochée »,
« sélectionné » ou « grisé ». Il suit les boutons, cases à cocher, boutons radio,
zones d’édition, listes, listes déroulantes et certains curseurs de réglage.
Les boîtes de message sont lues à leur ouverture. Les jauges reconnues sont
annoncées en pourcentage.

Dans les zones d’édition prises en charge, les déplacements verticaux lisent la
ligne, Gauche/Droite le caractère et Ctrl+Gauche/Droite le mot. L’écho clavier
peut lire les caractères, les mots ou les deux. Maj+déplacement annonce le texte
ajouté à la sélection ou retiré de celle-ci. Pour Write, Word et Ami Pro,
la lecture s’appuie sur le texte dessiné à l’écran ; elle dépend donc de ce que
l’application affiche et de la reconnaissance de son curseur.

### Commandes générales

| Commande | Utilisation |
| --- | --- |
| Ctrl seul | Couper la parole et interrompre le dire tout. |
| Inser+H | Entendre l’aide ; dans la Calculatrice, l’aide propre à celle-ci. |
| Inser+T | Lire le titre de la fenêtre active. |
| Inser+Tab | Relire le contrôle actif ; dans Excel, classeur, feuille et cellule. |
| Inser+Haut | Relire la ligne ou la valeur courante, selon l’application. |
| Inser+Bas | Lancer le dire tout dans un document pris en charge. |
| Inser+B | Lire le contenu de la fenêtre ou de la boîte de dialogue. |
| Inser+O | Lire le texte affiché enregistré dans le modèle hors écran. |
| Inser+Maj+Haut | Lire la sélection connue ; dans le texte dessiné, sa partie visible. |
| Inser+F | Lire la police et la mise en forme disponibles au curseur ou au point de revue. |
| Inser+Fin ou Inser+Page suivante | Lire la barre d’état reconnue ; dans la Calculatrice, Inser+Page suivante donne le bilan des états et Inser+Fin va au dernier bouton. |
| Inser+N ou Inser+E | Ouvrir les réglages de parole. |
| Inser+1 | Entendre l’aide (comme Inser+H). |
| Inser+2, Inser+3 | Écho des caractères, des mots : activer ou désactiver (non enregistré). |
| Inser+P | Niveau de ponctuation suivant (non enregistré). |
| Inser+F12 | Heure ; deux fois de suite, date. |
| Inser+7 ou Inser+1 du pavé | Changer de mode de revue. |
| Inser+moins du pavé | Recentrer et relire la revue ; l’objet navigateur revient au focus. |
| Plus du pavé | Dire tout. |
| Inser+5, 8, 2, 4, 6, 9, 3 du pavé | Navigation par objet (voir ci-dessous). |
| Inser+F4 | Quitter Épaulard. Windows et les applications restent ouverts. |

Inser+B et Inser+O servent à explorer une fenêtre ; ils ne remplacent pas le
dire tout d’un document. Inser+O peut contenir du texte de boîtes masquées.
Inser+F décrit les attributs que le lecteur connaît, sans fournir tous les
styles et réglages de paragraphe de l’application.

Pour un diagnostic, **Inser+D** décrit le focus et ses parents,
**Inser+Maj+D** l’arbre des fenêtres et **Inser+Maj+O** les textes et tracés notés.
Ces informations passent dans la parole et dans le journal
`logs/apolo-emu.log` avec le lancement habituel.

### Navigation par objet

Comme dans NVDA, pour découvrir une application qui n’est pas lue
automatiquement. Éteindre Verr Num.

| Commande | Action |
| --- | --- |
| Inser+5 du pavé | Dire l’objet courant. |
| Inser+8 du pavé | Objet parent (au sommet : le bureau). |
| Inser+2 du pavé | Premier objet enfant. |
| Inser+4 ou Inser+6 du pavé | Objet précédent ou suivant au même niveau. |
| Inser+9 ou Inser+3 du pavé | Précédent ou suivant dans l’ordre de lecture de la fenêtre. |
| Inser+Entrée du pavé | Activer l’objet (clic en son centre). |
| Inser+moins du pavé | Revenir à l’objet qui a le focus. |
| Inser+Maj+moins du pavé | Donner le focus à l’objet. |
| Inser+Suppr du pavé | Position et taille de l’objet. |

Les objets sont les fenêtres et contrôles, puis, à l’intérieur d’une fenêtre qui
dessine elle-même son contenu, les textes qu’elle affiche, de haut en bas. Détails
et limites : [revue au pavé](revue-pave.md#navigation-par-objet).

### Lire un document en continu

Dans le Bloc-notes, Write, Word ou Ami Pro, placer le curseur au point de départ
puis appuyer sur **Inser+Bas**. Le lecteur poursuit au-delà de l’écran en faisant
défiler le document. Dans l’Aide Windows, il tourne les pages de la rubrique.
Dans une page Web structurée, il part du curseur virtuel.

**Ctrl** arrête la lecture. Une autre touche l’interrompt aussi. Dans le texte
dessiné, le curseur peut précéder la parole de deux lignes ; Ctrl, Haut/Bas ou
une commande Inser le ramènent à la ligne lue. Pour reprendre une correction,
interrompre de préférence avec Ctrl avant de saisir : les autres touches ne
garantissent pas ce retour. Le dire tout déplace le curseur de l’application.

### Utiliser la souris depuis la revue

Ces commandes sont propres à Épaulard. Éteindre Verr Num.

| Commande | Action |
| --- | --- |
| / du pavé | Clic gauche à la position du pointeur. |
| * du pavé | Clic droit. |
| Maj+/ ou Maj+* | Maintenir le bouton correspondant ; même combinaison pour le relâcher. |
| Inser+/ | Amener le pointeur au point de revue, ou à l’objet navigateur juste après une commande d’objet. |
| Inser+* | Lire sous le pointeur ; en revue à plat, y placer le point de revue. L’objet navigateur devient l’objet sous le pointeur. |

Exemple : pour cliquer sur un texte affiché, passer en revue à plat, l’atteindre
au pavé, appuyer sur Inser+/ puis sur /. Pour glisser, maintenir le bouton avec
Maj+/, déplacer le pointeur par la revue, puis relâcher avec Maj+/.
Les positions estimées dans du texte dessiné peuvent être légèrement décalées.
Dans la Calculatrice, activer les boutons avec Inser+Entrée : le parcours des
boutons ne fournit pas de position pour Inser+/.

## Épaulard : couverture des applications

« Essayé sous QEMU » désigne les essais consignés avec synthèse simulée ; cela
ne garantit pas chaque fonction du programme. Le fonctionnement général de la
parole, du dire tout et du braille physique sous 86Box a été confirmé par
l’utilisateur. Les guides liés précisent les essais et les limites par fonction.

| Application | Fonctions utilisables | Limites ou état des essais |
| --- | --- | --- |
| Gestionnaire de programmes et Gestionnaire de fichiers | Icônes ou éléments sélectionnés, menus, boîtes, listes et titres des fenêtres de répertoire. | Lecture du texte et des sélections reconnus ; pas de description générale des images. |
| Bloc-notes et éditeurs à contrôles standard | Saisie, lignes, mots, caractères, sélection, revue et dire tout. | Les éditeurs qui dessinent eux-mêmes leur contenu demandent une prise en charge adaptée. |
| Write | Texte, déplacements, défilement, sélection, mise en forme et dire tout. | Revue et sélection du texte dessiné limitées à la partie visible. |
| Word 6.0c pour Windows | Document, menus, boîtes dessinées, champs, listes, cases, radios, onglets, sélection, police, braille et dire tout ; suivi en mode Page, dont en-têtes et pieds de page. | Sélection visible seulement ; taille de police estimée, exacte au zoom 100 % ; certains titres de groupe manquent. Voir [Word](word6-accessibilite.md). |
| Ami Pro 3.01 | Lignes, mots, caractères, sélection et dire tout ; menus et boîtes standard. | Barre d’icônes non lue. Essais QEMU consignés dans [Programmes Windows](programmes-windows.md). |
| Excel 5.0 / 5.0c | Cellule, valeur, formule, feuille, plage sélectionnée, police et alignement ; édition F2 ; boîtes de dialogue. | Étendue de sélection visible seulement ; pas de lecture du contenu de toute la plage ni d’association de titres de lignes et de colonnes aux données. Voir [Tableurs](tableur.md). |
| Access 2.0 | Fenêtre de base de données, boîtes, boutons à image identifiés, grilles de création, propriétés et feuilles de données ; étiquette, colonne et valeur de cellule. | Conseiller non lu, numéro d’enregistrement non annoncé, formulaires non audités. Voir [Grilles](tableur.md#grilles-à-éditeur-access). |
| Calculatrice | Saisie et résultats, erreurs, mémoire, modes Standard et Scientifique ; parcours et activation des boutons. | Prise en charge de CALC.EXE français 3.10. Voir les commandes ci-dessous et [Calculatrice](calculatrice.md). |
| Aide Windows et glossaire | Liens à Tab, ouverture à Entrée, lecture aux flèches et par pages, dire tout de la rubrique, définitions surgissantes. | Lecture du texte dessiné ; ne décrit pas les illustrations. |
| Fiches, Table de caractères, Horloge | Fiche active et sa ligne ; caractère choisi et code Alt ; heure à l’activation et par Inser+Haut. | Couverture ciblée des informations textuelles. |
| Panneau de configuration | Modules essayés au clavier, libellés, groupes, valeurs, curseurs, unités, listes. | Certaines flèches de réglage de Bureau et Date/Heure ne répondent qu’à la souris. |
| Sysedit, Regedit, PIF, Liste des tâches, Winver, Gestionnaire d’impression, Presse-livres, Enregistreur | Contrôles standard, listes, titres des documents. | Ne garantit pas l’accessibilité des contenus graphiques du Presse-livres. |
| Enregistreur de sons | Boutons, position en secondes et changements d’état. | Le Diffuseur de médias a des boutons nommés, mais sa lecture n’a pas été essayée faute de périphérique MCI. |
| Terminal | Menus, réglages du port, lecture des nouvelles lignes lorsque l’affichage se stabilise. | Réception sur une vraie liaison non essayée. |
| Microsoft Mail | Messages de boîtes, champs, corps, dossiers, liste des messages et menus ; création d’un bureau de poste local, identification et composition essayées. | Barre d’outils sans focus ; liste vide parfois annoncée comme zone de texte. |
| Didacticiel Windows | Texte principal des écrans à leur apparition. | Certaines initiales manquent ; une boîte peut mêler son texte à celui du dessous. |
| Outils Windows de MS-DOS 6 : Anti-Virus, Undelete, Backup, MSInfo, SmartDrive Monitor | Lecture des contrôles standard. | Backup signale VFINTD.386 absent dans la configuration essayée. |
| Netscape Navigator 3.01 et Communicator 4.08 | Pages, champs et mode navigation structuré décrit ci-dessous. | Mode structuré réservé aux pages servies par le proxy ; essais QEMU, braille physique et 86Box non validés pour ce mode. |
| Eudora | L’installation et le courrier local sont décrits dans [Réseau](reseau.md). | Aucun bilan d’accessibilité détaillé comparable à celui de Microsoft Mail : couverture à vérifier. |
| Invite MS-DOS et programmes DOS sous Windows | Parole et braille d’Épaudos relayés par Épaulard, en fenêtre ou en plein écran. | Épaudos doit être chargé avant Windows. Une seule session DOS suivie à la fois. |
| Schedule+ | — | Le programme refuse l’année 2026 : plage de dates 1920–2019. |
| Paintbrush, images de télécopie, Solitaire, Démineur, Dame de Pique | Menus ou boîtes standard éventuellement lisibles. | Dessins, cartes et cases sans description : ces activités ne sont pas accessibles. |
| Autres applications, dont Norton Desktop | Les mécanismes génériques peuvent lire certains contrôles. | Aucune couverture complète établie ; installation possible ne signifie pas accessibilité validée. |

### Excel et Access en pratique

Dans **Excel**, se déplacer avec les flèches, Tab ou Entrée : Épaulard annonce
la référence, la valeur et la formule si elle existe. **Inser+Haut** relit la
cellule, **Inser+Tab** le classeur, la feuille et la cellule, **Inser+F** la
mise en forme. **Ctrl+Page suivante/précédente** change de feuille ;
**Maj+flèches** étend la sélection et fait annoncer la plage visible.
**F2** ouvre l’édition ; les flèches lisent alors le caractère et Ctrl+flèches
le mot de la formule.

Dans les **grilles d’Access**, les déplacements font annoncer l’étiquette de
ligne, l’en-tête de colonne lorsqu’il est reconnu, puis la valeur. Les parties
inchangées ne sont pas répétées ; **Inser+Haut** relit l’ensemble.

### Calculatrice en pratique

Saisir les nombres et opérations au clavier ; le résultat est annoncé après
**=** ou **Entrée**, et **Inser+Haut** le relit. **Échap** efface le calcul.
Les menus permettent de choisir Standard ou Scientifique.

Pour explorer les boutons dessinés : **Inser+Gauche/Droite** choisit le bouton
précédent/suivant, **Inser+Début/Fin** le premier/dernier et **Inser+Tab** relit
le choix. **Inser+Entrée** l’active. Le parcours seul ne déclenche aucun calcul.
**Inser+Page suivante** annonce le mode, la mémoire et les réglages scientifiques,
dont la base, l’unité ou la taille du mot et les options inverses/hyperboliques.

### Naviguer sur le Web

Le disque doit être préparé pour le réseau et la machine lancée avec
`scripts/pc486 start --reseau`. Le proxy fournit une version HTML 3.2 des pages
et leur structure au lecteur. Une première page d’essai est
`http://pc486/essai`, à saisir dans Netscape.

Après chargement de la structure, le **curseur virtuel** parcourt la page sans
déplacer le focus du navigateur. Haut/Bas lit la ligne, Gauche/Droite le
caractère, Ctrl+Gauche/Droite le mot, Ctrl+Haut/Bas le paragraphe,
Ctrl+Début/Fin les extrémités du document, et Page précédente/suivante vingt
lignes. **Inser+Bas** lance le dire tout.

| Lettre seule | Élément suivant ; avec Maj, élément précédent |
| --- | --- |
| H ; 1 à 6 | Titre ; titre du niveau indiqué. |
| K ; U ; V | Lien ; lien non visité ; lien visité par le proxy. |
| L ; I | Liste ; élément de liste. |
| T | Tableau de données. |
| F ; E ; B | Champ ; zone d’édition ; bouton. |
| X ; R ; C | Case à cocher ; bouton radio ; liste déroulante ou liste. |
| P ; N | Paragraphe ; texte hors lien après un groupe de liens. |
| Q ; S | Citation ; séparateur. |
| G ; D ; M ; O | Image avec texte alternatif ; région ; cadre ; objet. |
| Virgule | Fin du conteneur ; Maj+virgule : début. |

Sur AZERTY, les niveaux 1 à 6 utilisent les touches de la rangée supérieure
**sans Maj**, donc & é " ' ( -. **Ctrl+Alt+flèches** parcourt les cellules d’un
tableau avec leurs en-têtes reconnus. Ctrl+Alt+Début/Fin atteint la première/
dernière cellule de la ligne ; Ctrl+Alt+Page précédente/suivante la première/
dernière ligne du tableau.

**Entrée** ouvre un lien. Entrée ou Espace active une case, un bouton radio ou
un bouton. Entrée sur un champ d’édition ou une liste passe en **mode formulaire** :
les touches vont au navigateur pour saisir ou choisir. Tab vers un champ peut
aussi déclencher ce mode. **Échap** revient au mode navigation ; une liste
déroulante ouverte peut d’abord utiliser Échap pour se fermer.
**Inser+Espace** bascule le mode manuellement. **Inser+F5** redemande la structure.

Sans structure, notamment pour un fichier local ou une page hors proxy, la
lecture se limite au texte dessiné et aux champs reconnus. La structure est
limitée à 30 000 caractères et 1 500 éléments. Il n’y a pas de liste des éléments
par Inser+F7 ni de recherche propre au curseur virtuel. Les sites qui exigent
des fonctions absentes de HTML 3.2 ne sont pas entièrement représentés.
Voir [Navigation Web](navigation-web.md) pour la synchronisation et ses limites.

## Épaudos : fonctions et commandes

Épaudos lit les écrans texte CP850, de 40 à 132 colonnes et jusqu’à 50 lignes,
y compris ceux écrits directement en mémoire vidéo. Il suit le curseur, les
cadres reconnus et certaines surbrillances. Il se met en veille en mode graphique.

La revue au pavé est celle décrite au début du manuel. Épaudos n’a pas les
commandes de souris d’Épaulard.

| Commande | Utilisation sous DOS |
| --- | --- |
| Ctrl | Couper la parole et arrêter le dire tout. |
| Inser+H ou Inser+1 | Entendre l’aide. |
| Inser+Haut, Inser+5 ou Inser+moins du pavé | Recentrer et relire la ligne active ; dans une boîte reconnue, la boîte et le contrôle actif. |
| Inser+Bas | Lire de la ligne courante au bas du cadre ou de l’écran ; en revue à plat, partir de la ligne de revue. |
| Inser+B ou Inser+O | Lire le cadre ou l’écran depuis le haut. |
| Inser+T | Lire la première ligne intérieure du cadre ; dans une boîte, son titre. |
| Inser+Fin ou Inser+Page suivante | Lire la dernière ligne du cadre (souvent la ligne d’état). |
| Inser+7 ou Inser+1 du pavé | Basculer revue normale / à plat. |
| Plus du pavé | Dire tout, comme Inser+Bas. |
| Inser+5 (rangée des chiffres) | Activer ou désactiver l’écho écran. |
| Inser+N ou Inser+E | Ouvrir les réglages. |
| Inser+2, Inser+3, Inser+P | Écho des caractères, des mots, niveau de ponctuation (non enregistrés). |
| Inser+F | Couleurs du caractère (« blanc sur bleu »). |
| Inser+Suppr du pavé | Ligne et colonne du curseur ou du point de revue. |
| Inser+F12 | Heure ; deux fois de suite, date. |
| Inser+D | Donner la ligne, la colonne et l’attribut du curseur. |

**Différence essentielle : le dire tout DOS ne fait pas défiler l’application.**
Il lit seulement les lignes présentes dans le cadre ou l’écran. Toute touche
d’application l’interrompt. Il n’existe pas d’historique des lignes disparues.

### Suivi et échos

- Haut/Bas, Page précédente/suivante et Ctrl+Début/Fin font lire la ligne atteinte,
  y compris quand le texte défile sous un curseur qui reste en bas de fenêtre.
- Gauche/Droite et Début/Fin font lire le caractère ; Ctrl+Gauche/Droite le mot.
- L’écho clavier lit les caractères saisis, les mots terminés ou les deux selon
  le réglage. Retour arrière et Suppr font entendre le caractère effacé.
- L’**écho écran**, actif par défaut, lit les nouvelles lignes de sortie ou les
  messages qui ne proviennent pas d’un déplacement ou de la frappe. Inser+5
  permet de le couper si un éditeur devient trop bavard.

Une sortie trop rapide peut défiler entre deux observations et ne pas être lue.
Pour une longue liste de fichiers, saisir par exemple `DIR | MORE` et avancer
page par page. Le lecteur ne peut pas retrouver la sortie déjà sortie de l’écran.

### Boîtes de dialogue texte

Dans les boîtes reconnues de type Character Windows, Épaudos annonce le titre,
le rôle, le libellé et la valeur du contrôle : champ, case à cocher, bouton radio,
bouton ou liste. À Tab/Maj+Tab, il suit le nouveau contrôle ; Espace sur une case
fait annoncer son état. Les messages d’alerte sont lus à l’ouverture. À la
fermeture, il relit la ligne du curseur.

Inser+Haut relit la boîte et le contrôle ; Inser+T relit son titre. La revue et
Inser+B restent dans la boîte reconnue. La détection dépend du dessin, du fond
et du curseur matériel ; elle ne couvre pas toutes les interfaces DOS.
Les listes déroulantes de Word sont décrites comme des champs, sans position
« n sur total » ni annonce des raccourcis soulignés.

## Épaudos : couverture des applications

| Application ou contexte | Couverture connue | État et limites |
| --- | --- | --- |
| Invite DOS, commandes et DOSKEY | Frappe, lecture de ligne, rappel d’historique, sortie par écho écran, revue et dire tout de l’écran. | Bancs natifs et QEMU ; sorties rapides partiellement perdues, utiliser MORE. |
| EDIT de MS-DOS | Saisie, déplacements, défilement, lecture et revue ; boîtes Ouvrir et Chemin de l’aide reconnues. | Essais QEMU et scénarios natifs à partir des écrans relevés. |
| QBasic | Saisie et déplacements dans l’éditeur, lecture de l’écran et signes usuels. | Essais QEMU ; ne constitue pas un audit de tous les menus, outils et dialogues. |
| DOSSHELL | Lecture de l’écran et de surbrillances ; déplacement de menu essayé. | Couverture partielle, pas d’audit de toutes les opérations. |
| Word 6.0 pour DOS | En mode Texte : frappe, ligne, revue ; boîtes Ouvrir, Caractère et enregistrement à la fermeture. | Essai QEMU muet du 28 octobre 2026 et scénarios natifs. Choisir le clavier « pour résidents ». Mode graphique non lu ; plage physique et 86Box restent à valider pour le TSR. |
| Autres programmes à interface Character Windows, dont Works | Reconnaissance générique possible des champs, cases, radios, boutons et listes. | Ce mécanisme ne vaut pas validation de chaque application ; pas d’essai complet de Works consigné. |
| Invite MS-DOS sous Windows | Même lecture et mêmes commandes qu’au DOS ; parole et braille relayés par Épaulard, en fenêtre ou en plein écran. | Relais essayé sous QEMU, braille de l’invite confirmé par l’utilisateur sous 86Box. |
| Programmes graphiques, jeux et interfaces sans curseur BIOS suivi | Pas de couverture générale. | Le lecteur se met en veille hors des modes texte reconnus ; certains programmes peuvent contourner aussi son interception clavier. |

Pour Word DOS, le mode Texte est indispensable. Word DOS et Word pour Windows
sont deux programmes distincts : leurs fonctions de lecture et leurs réglages
de lecteur ne sont pas interchangeables.

### Passer du DOS à Windows et revenir

Épaudos reste résident mais se met en veille pendant Windows et laisse les ports
à Épaulard. Quand une session DOS est active, il reprend la lecture de cette
session par le relais d’Épaulard : **ce sont alors les commandes d’Épaudos** qui
s’appliquent. **Alt+Entrée** bascule fenêtre/plein écran ; **Alt+Tab** revient à
une application Windows, et `EXIT` ferme l’invite DOS.

Ne pas charger une nouvelle instance d’Épaudos dans une session DOS de Windows.
Il doit avoir été chargé avant `WIN`, comme le fait le menu pc486. Après la
fermeture de Windows, il reprend automatiquement la lecture DOS. Pour retirer
le résident, redémarrer DOS : il n’existe pas de désinstallation à chaud.

## Régler la parole

**Inser+E** ouvre les réglages du lecteur actif. **Haut/Bas** choisit le réglage
et **Gauche/Droite** change sa valeur. Le menu prend temporairement le clavier ;
le fermer avant de reprendre le travail dans l’application.

| Réglage | Valeurs | Défaut |
| --- | --- | --- |
| Débit | 1 à 9 | 3 |
| Hauteur | 0 à 15 | 8 |
| Voix | 1 à 6 | 1 |
| Ponctuation | Aucune, certaine/naturelle, toute | Certaine/naturelle |
| Écho clavier | Caractères, mots, caractères et mots, aucun | Caractères |
| Annonce des majuscules | Non, oui | Non |
| Écho écran, Épaudos seulement | Non, oui | Oui |

Un caractère lu seul est nommé, même si la lecture des phrases utilise une
ponctuation réduite. Le détail des signes nommés diffère entre les lecteurs.
Le volume n’est pas proposé par ces menus ; il dépend de la sortie vocale hôte.

**Attention à la différence d’enregistrement :**

| Action | Épaulard | Épaudos |
| --- | --- | --- |
| Entrée | Enregistrer et fermer. | Fermer et demander l’enregistrement. |
| Échap | Annuler les modifications du menu et fermer. | Fermer et demander l’enregistrement, comme Entrée. |
| Sans commande pendant une minute | Fermer en annulant. | Rendre automatiquement le clavier. |
| Fichier | `EPAULARD.INI` près de l’EXE, normalement `C:\EPAULARD`, section `[Parole]`. | `EPAUDOS.INI` près du COM, normalement à la racine de C:. |

Épaudos attend une période d’inactivité DOS pour écrire ses réglages et annonce
le résultat. Si l’application ne lui en donne pas l’occasion, revenir à l’invite
et lancer `EPAUDOS /SAVE`. Les valeurs restent actives en mémoire dans l’attente.
La bascule Inser+5 change l’écho écran sans l’enregistrer immédiatement.
Les deux lecteurs conservent des réglages séparés ; Épaulard rétablit les siens
en reprenant la main après une session DOS.

## Utiliser la plage braille

Les deux lecteurs affichent du **braille informatique français à huit points**,
sur une seule ligne de 1 à 80 cellules. Il n’y a ni braille abrégé ni saisie au
clavier braille. Le curseur utilise les points 7–8 ; sous Windows, la sélection
les utilise aussi, sans signe distinct.

La passerelle Baum/BrlAPI doit être disponible sur l’hôte ; elle démarre
automatiquement lorsque l’outil braille est construit. Le lancement
`scripts/pc486 start --braille` l’impose. La plage suit normalement le focus de
la fenêtre de la VM et revient au lecteur de l’hôte lorsque celui-ci reprend le
focus. Le guide [Braille](braille.md) détaille la préparation.

| Commande de plage | Action |
| --- | --- |
| FWINLT / FWINRT | Reculer / avancer horizontalement d’une largeur de plage. |
| LNUP / LNDN | Ligne précédente / suivante en revue. |
| HOME | Revenir au suivi du curseur ou du contrôle actif. |
| ROUTE, curseur routine | Action selon le contexte, ci-dessous. |

Les noms physiques dépendent de la plage. La correspondance de la Baum virtuelle
est d2/d5 pour le défilement, d1/d3 pour les lignes et d4 pour HOME.

- **Épaulard, zone d’édition standard** : le routage place le curseur de saisie.
- **Épaulard, document dessiné de Write ou Word** : le routage clique sur le
  caractère estimé pour y placer le curseur, puis restaure le pointeur.
- **Épaulard, revue à plat** : le premier routage place le point de revue et lit
  le caractère ; un second sur la même cellule dans la seconde effectue un clic.
- **Épaulard, Web structuré** : le routage place le curseur virtuel ; un second
  appui active l’élément, comme Entrée.
- **Épaudos**, y compris sous Windows : le routage place seulement la revue et
  dit le caractère. Il ne déplace pas le curseur de saisie et ne clique pas.

Dans les boîtes système qui bloquent temporairement Épaulard, comme « Quitter
Windows », titre, texte et bouton actif restent lus et affichés. Seuls le
défilement horizontal et HOME de la plage sont disponibles ; pas de routage.
Les champs de mot de passe reconnus par Épaulard affichent « mot de passe »
et ne sont pas routés.

## Dépannage et limites

| Situation | Vérification ou action |
| --- | --- |
| Aucun son au menu de démarrage | C’est attendu : choisir DOS ou Windows pour charger le lecteur. |
| Le pavé déplace le curseur au lieu de lire | Éteindre Verr Num ; utiliser les chiffres du pavé seuls pour la revue. |
| La lecture répète toujours la même ligne suivante | La revue normale repart du curseur ; passer en revue à plat avec Inser+7 du pavé. |
| Les touches ne vont plus à l’application | Fermer les réglages ; sur le Web, passer en mode formulaire pour saisir. |
| La lecture est trop bavarde sous DOS | Couper avec Ctrl ; basculer l’écho écran par Inser+5 et adapter l’écho clavier. |
| Une sortie DOS est incomplète | Utiliser une commande paginée, par exemple `DIR | MORE`. Pas de récupération des lignes déjà sorties de l’écran. |
| Une fenêtre DOS sous Windows ne dit que son titre | Vérifier qu’Épaudos a été chargé avant Windows ; ne pas le charger depuis cette fenêtre. |
| Le Web ne répond pas à H ou K | Vérifier le mode navigation, le lancement avec réseau et le passage de la page par le proxy. Inser+Espace annonce l’absence de mode si la page n’a pas de structure. |
| La structure Web semble ancienne | Inser+F5 ; une alerte de divergence signifie que le texte structuré peut différer de l’affichage. |
| Le braille reste sur l’hôte ou disparaît | Remettre le focus sur la VM ; consulter l’état de la passerelle et `logs/braille.log`. |
| Plus de parole après un démarrage réussi | Depuis Linux, `scripts/pc486 status` et `logs/apolo-emu.log` permettent de distinguer le lecteur de la passerelle vocale. `scripts/pc486 gateway restart` relance la passerelle et interrompt la parole en cours. |

Les lecteurs ne font pas de reconnaissance d’images. Une fenêtre ouverte ou une
application installée n’est donc pas forcément utilisable : dessins, graphiques,
barres d’icônes sans libellés et interfaces particulières peuvent rester muets.
La revue du texte dessiné décrit l’affichage disponible ; elle ne fournit pas
un accès général au document complet ni à sa structure. Épaudos dépend en outre
du curseur BIOS et des conventions de dessin de l’application.

## Guides complémentaires

- [Épaudos : architecture, compilation et essais](epaudos.md).
- [Revue au pavé et souris d’Épaulard](revue-pave.md).
- [Réglages d’Épaulard et fichier INI](reglages.md).
- [Word 6.0c pour Windows](word6-accessibilite.md).
- [Excel 5 et grilles d’Access 2.0](tableur.md).
- [Calculatrice](calculatrice.md).
- [Programmes Windows : détail des essais et limites](programmes-windows.md).
- [Navigation Web](navigation-web.md) et [réseau et courrier locaux](reseau.md).
- [Invite DOS sous Windows](invite-dos.md).
- [Braille : installation et diagnostic](braille.md).
- [Installation de la machine et des applications](installation-automatique.md).

Ce manuel concerne l’utilisation. Les procédures d’installation et de maintenance
du disque restent dans leurs guides ; elles doivent être coordonnées avec toute
installation ou session de VM en cours.
