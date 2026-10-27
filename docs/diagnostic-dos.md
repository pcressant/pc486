# Sessions DOS sous Windows 3.11

Épaulard ne lit pas le contenu des sessions « Commandes MS-DOS » de Windows.
Son modèle hors écran suit les appels Windows de dessin de texte, pas la mémoire
vidéo de la machine DOS virtuelle. Une absence de parole ne signifie donc pas
que Windows est bloqué.

- Alt+Tab revient à une application Windows.
- Alt+Entrée bascule entre la fenêtre DOS et le plein écran.
- `EXIT` ferme la session DOS.

Pour utiliser DOS avec un lecteur d'écran, quitter Windows et choisir DOS avec
Épaudos dans le menu de démarrage. Épaudos reste en veille pendant toute la
session Windows, y compris dans ses sessions DOS : COM1 appartient à Épaulard.

Sous QEMU, le texte d'une session DOS en fenêtre peut être déformé. Ce défaut a
également été observé sans Épaulard ; sa cause reste à isoler.
