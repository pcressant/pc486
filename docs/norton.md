# Norton Desktop 3.0 pour Windows et Épaulard

État au 31 octobre 2026 de la lecture, par Épaulard, de Norton Desktop 3.0 français,
installé par `scripts/installer --norton` (voir
[Installation automatique](installation-automatique.md)). Chaque programme a été
ouvert dans une copie de l'image d'installation sous QEMU (`scripts/pc486-dev`,
synthèse simulée), avec Épaulard fraîchement compilé. Les comportements corrigés sont
couverts par le scénario `test_norton` du banc natif (`tests/win16/epaulard-test.c`).
**Pas encore essayé sous 86Box avec la parole réelle.**

## Ce qui est lu

| Programme | État |
| --- | --- |
| Bureau Norton (`NDW.EXE`) | Menus, groupes et icônes. Fenêtre de disque (F10, Fenêtre, Ouvrir fenêtre disque) : arbre des répertoires avec son niveau (« ndw, niveau 2 », niveau dit en entrant dans l'arbre et quand il change), liste des fichiers (« dossier parent » pour `..`, dessiné en image), barre de boutons nommée |
| Opérations sur les fichiers | Boîtes Copie (F8), Déplacer (F7), Effacement (Suppr) : champs nommés, « Vers : » en liste déroulante modifiable ; confirmation « Attention » (fichier existant) lue avec son message |
| Éditeur Norton (DeskEdit), ScriptMaker | Lignes, mots, sélection, menus ; boîte « … a été modifié. Enregistrer avant de fermer ? » lue avec son message |
| Chercheur SuperFind | Champs nommés (listes déroulantes modifiables) ; état de la recherche et nombre de fichiers trouvés dits quand ils changent |
| Informations système (SIW) | Page dessinée lue entière à l'ouverture et à chaque changement de page (menu Résumé…) |
| Calculatrices scientifique, financière, imprimante | Valeur de l'afficheur dite à chaque changement ; les touches dessinées ne sont plus annoncées |
| AntiVirus (`NAVW`) | Liste « Disques : » : disque et « sélectionné / non sélectionné » à chaque Espace ; progression et rapport d'analyse lus |
| Docteur Disque (`NDDW`), Speed Disk, SmartErase | Boutons, jauge de progression (lue même quand les menus sont grisés), arbre et liste de SmartErase comme dans la fenêtre de disque |
| Backup | Boîtes de configuration, champs de la fenêtre principale (modes Sauvegarde, Restauration, Configuration par Alt+lettre) |
| Programmateur (Scheduler) | Liste des actions programmées et boutons |
| KeyFinder (table de caractères) | Champs nommés : chaque libellé est placé *après* son champ dans l'ordre des fenêtres, la position à l'écran décide |
| Formatage de disquette, Panneau de contrôle (économiseur d'écran) | Contrôles standard |

## Limites

- **Organiseur (`DAYPLAN`)** : fenêtres MFC dessinées (`AfxFrameOrView`) sans
  contrôles standard ; Tab passe d'une fenêtre à l'autre et seul leur titre est dit.
  Agenda, mémos et calendrier ne sont pas lus. Non fait.
- Docteur Disque : les coches des étapes et le texte d'aide affiché à l'ouverture ne
  sont pas lus.
- Boîte Copie : le répertoire et le fichier à copier (« Répertoire : c:\ndw ») ne sont
  dits qu'en allant sur leur champ, comme tout libellé de formulaire.
- Copie de disquette, Visionneur, Sleeper, Éditeur d'icônes et de boîte : non essayés
  (le Visionneur et la copie de disquette ont été interrompus par un blocage de la VM
  d'essai, voir ci-dessous).
- Barre de gros boutons de Backup (Sauvegarde, Comparaison…) : non accessible au
  clavier ; utiliser Alt+lettre.
- Dans la VM d'essai QEMU (`pc486-dev`, synthèse simulée), Windows se fige parfois à la
  fermeture d'une boîte du Gestionnaire de programmes (« Quitter Windows », « Erreur
  d'exécution ») : boucle en anneau 3. Constaté aussi avec les binaires d'origine et sur
  une copie de `vm/win311.img` sans Norton ; l'installateur, lui, revient au DOS. Cause
  non trouvée, sans lien établi avec Norton.

## Repères techniques

- `NLBCLASSV1` est une liste `LISTBOX` dessinée sans `LBS_HASSTRINGS` : `class_of`
  la traite comme `LISTBOX` (`class_alias`, `outils.c`), `raw_class` garde le nom réel
  pour le diagnostic. Arbre : `NLBCLASSV1` dans `NTREECLASSV1`, 20 points par niveau
  (`tree_level`, `controles.c`).
- Les boîtes de Norton se réactivent à chaque Tab sans être recréées : le crochet CBT
  signale une réactivation de la même fenêtre non détruite (lParam d'`EPM_ACTIVATE`),
  dont le titre n'est pas redit.
- Message des boîtes « Attention » et « Enregistrer ? » : bouton `BS_OWNERDRAW`
  désactivé, sans `&` (`message_button`, `epaulard.c`).
- Les groupes du bureau dessinent leurs icônes avec un contexte inconnu d'Épaulard :
  un tel texte n'est attribué à la fenêtre visible au même endroit que si son centre
  est visible dans le contexte (`vis_pt`, `osm.c`), sinon « Jeux » se mêlait à la liste
  des fichiers.
- Calculatrices (`NSCI_CALC`, `NFIN_CALC`, `NTAPE_CALC`) : `ncalc_*` (`calc.c`).
  SuperFind : `status_poll` ; SIW (`SIOUTPUTCHILD`) : `auto_poll` (`epaulard.c`).
