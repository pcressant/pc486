# Calculatrice Windows 3.1 et Épaulard

Lancer **Calculatrice** dans le groupe Accessoires, ou exécuter `calc` depuis le
Gestionnaire de programmes. Épaulard annonce la valeur affichée à l'ouverture,
puis chaque changement : saisie, résultat, effacement, collage et erreur. Elle
annonce aussi les changements de mémoire et de réglages scientifiques.
La lecture utilise la valeur de Windows ; Épaulard ne refait pas les calculs.

## Utilisation au clavier

- Saisir les nombres et les opérations avec les touches habituelles de la
  calculatrice, notamment `+`, `-`, `*`, `/` et `=` ou Entrée.
- Les opérations sont annoncées en français. Après `12+3=`, le résultat lu est
  « 15 ». Une frappe rapide peut regrouper les valeurs intermédiaires.
- **Inser+Haut** relit la valeur ou le message d'erreur affiché.
- **Échap** efface le calcul ; la valeur est relue, même si elle était déjà zéro.
- Les menus restent accessibles avec Alt et les flèches. Le menu **Affichage**
  permet de passer du mode Standard au mode Scientifique.

## Boutons dessinés et états

Les commandes suivantes utilisent Inser, sans déplacer le focus de Windows :

| Commande | Action |
| --- | --- |
| Inser+Gauche / Droite | Bouton précédent / suivant, avec son nom complet |
| Inser+Début / Fin | Premier / dernier bouton disponible |
| Inser+Entrée | Activer le bouton choisi |
| Inser+Tab | Relire le bouton choisi |
| Inser+Haut | Revenir au résultat et le relire |
| Inser+Page suivante | Lire le mode, la mémoire et les réglages scientifiques |
| Inser+H | Aide des commandes propres à la calculatrice |

Le parcours seul ne fait aucun calcul. **Inser+Entrée** est nécessaire pour
activer le bouton ; sans choix préalable, Épaulard invite à en choisir un.
Les touches ordinaires quittent ce parcours et rendent le suivi au résultat.
Les menus Windows restent utilisables normalement. Ce parcours n'a pas de
coordonnée de souris : Inser+/ annonce « aucune position » ; activer avec
Inser+Entrée.

Les premiers boutons sont Effacer tout, Effacer l'entrée, Retour arrière,
Mémoriser le résultat, Rappeler la mémoire, Effacer la mémoire, Ajouter à la
mémoire, puis les chiffres et les opérations. En mode scientifique, le parcours
propose aussi plusieurs fonctions, les bases, l'unité ou la taille du mot,
et les options Inv/Hyp. Il revient au début après le dernier bouton.

Les changements de mémoire sont annoncés par « mémoire occupée » ou « mémoire
vide ». Le bilan Inser+Page suivante permet de relire cet état, même sans changement.
En mode scientifique, les bases sont nommées en entier. Les radios
Deg/Rad/Grad deviennent **double mot 32 bits / mot 16 bits / octet 8 bits** dans
les bases non décimales ; Épaulard adapte leurs noms. Les cases Inv et Hyp
sont décrites comme fonctions inverses et hyperboliques, avec leur état.

Le signe négatif et la partie décimale sont prononcés même si la ponctuation
est désactivée dans les réglages d'Épaulard. La virgule finale que Windows affiche
sur les entiers n'est pas prononcée. Le résultat est relu après `=` ou Entrée,
même si sa valeur est identique à la précédente.

En revue normale, la plage braille et la revue au pavé suivent le résultat,
le bouton choisi ou le bilan, selon la dernière commande. Un véritable contrôle
atteint par Tab conserve sa description et son rôle. Le résultat est conservé tel
qu'affiché, avec son signe et sa virgule. La revue à plat conserve son
fonctionnement habituel sur le texte dessiné.

## Implémentation et limites

La fenêtre `SciCalc` garde généralement le focus ; les touches numériques et la
plupart des fonctions sont dessinées sans fenêtres BUTTON. L'EDIT 495 est caché
et ne représente pas le résultat. Épaulard suit les STATIC visibles 414 (mode
standard) ou 413 (mode scientifique), toutes les 100 ms. Seule la calculatrice
active est suivie. Les menus et les boîtes de dialogue gardent le suivi normal.

Les témoins de mémoire sont les STATIC 402 (standard) et 400 (scientifique).
Les radios 121 à 124 donnent la base ; 127 à 129 donnent l'unité ou la taille du mot,
selon la base. Le parcours transmet les identifiants WM_COMMAND observés dans
CALC.EXE français 3.10. Les calculs restent exécutés par Windows.

Le parcours ignore les chiffres incompatibles avec la base, la virgule hors de
la base décimale et les fonctions décimales dans les autres bases. Les noms des
fonctions trigonométriques suivent Inv/Hyp. Avec Inv activé, les puissances et
logarithmes sont retirés du parcours pour éviter des libellés trompeurs ; leurs
raccourcis Windows restent utilisables. Les fonctions statistiques, les opérations
logiques et certaines fonctions avancées ne figurent pas encore dans ce parcours.

La mémoire est suivie par son témoin, sans accès à sa valeur interne. Remplacer une
mémoire déjà occupée ne change donc pas ce témoin ; l'activation du bouton
« mémoriser le résultat » est néanmoins annoncée. Les états sont donnés en braille
par le bilan Inser+Page suivante.

En dehors de la calculatrice, les descriptions des contrôles Windows standard
indiquent désormais « grisé » quand le contrôle est désactivé, notamment pendant
la lecture de la fenêtre par Inser+B.
