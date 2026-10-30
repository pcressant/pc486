# Sessions DOS sous Windows 3.11 : diagnostic

Une session « Invite MS-DOS » de Windows est lue par **Épaudos**, dont Épaulard relaie
la parole et le braille (voir [l'invite MS-DOS](invite-dos.md)). Épaulard lui-même
ne peut pas la lire : son modèle hors écran suit les appels de dessin de Windows,
pas la mémoire vidéo de la machine DOS virtuelle, et les touches de cette machine
ne passent pas par son crochet clavier.

## Session silencieuse

1. **Épaudos chargé avant `WIN` ?** Seul le choix Windows du menu de démarrage le
   charge (`dos/AUTOEXEC.BAT`). Sans lui, la fenêtre DOS n'est annoncée que par son
   titre. Ne jamais lancer `EPAUDOS.COM` depuis une session DOS de Windows.
2. **Fenêtre active ?** Seule la machine DOS qui reçoit les touches est lue. Alt+Tab
   vers une autre fenêtre rend la parole à Épaulard.
3. **Plein écran juste après Alt+Entrée ?** L'écran virtuel peut être haché jusqu'à
   ce que le programme le redessine : Épaudos lit ce qui est réellement affiché.
4. **Journaux** : `logs/apolo-emu.log` montre le texte reçu par la synthèse ; une
   absence totale de texte avec Épaudos chargé désigne le relais (`dosbox.c`).

Une absence de parole ne signifie pas que Windows est bloqué :

- Alt+Tab revient à une application Windows ;
- Alt+Entrée bascule entre fenêtre et plein écran ;
- `EXIT` ferme la session DOS.

Sous QEMU, le texte d'une session DOS en fenêtre peut être déformé. Ce défaut a
également été observé sans Épaulard ; sa cause reste à isoler.
