/* EPAULARD.EXE — suivi de la souris, comme NVDA : texte sous le pointeur quand il
   bouge (Inser+M) et bip dont la hauteur suit l'ordonnée du pointeur (Inser+Maj+M).

   Windows 3.1 ne signale pas les déplacements à un autre programme : la scrutation
   (100 ms) compare la position du pointeur. Les positions données par Épaulard
   lui-même (Inser+/, clic de routage) sont notées par mouse_placed(), ni lues ni
   sonnées. La Sound Blaster 1.5 de Windows 3.1 (SNDBLST2.DRV) est mono : seule la
   hauteur du pointeur s'entend, pas sa position horizontale. */
#include "ep.h"

#define MS_LOW   220            /* bas de l'écran (Hz), comme NVDA */
#define MS_HIGH  880            /* haut de l'écran */
#define MS_BEEP  40             /* durée d'un bip (ms) */

static POINT ms_last;           /* dernière position vue */
static BOOL ms_known;
static HWND ms_hwnd;            /* fenêtre sous le pointeur à la dernière annonce */
static int ms_row;              /* rangée de texte de cette annonce */
static char ms_said[TXT * 2];   /* texte de cette annonce */

/* Pointeur placé par Épaulard : position retenue, rien à dire ni à sonner */
void mouse_placed(void)
{
    GetCursorPos(&ms_last);
    ms_known = TRUE;
    ms_hwnd = NULL;
}

/* Inser+M : suivi (ligne, mot ou désactivé) ; Inser+Maj+M : bips. Comme Inser+2 et
   Inser+3, non enregistré (Inser+E pour enregistrer). */
void mouse_toggle(BOOL sounds)
{
    static int unit = 1;                /* ligne ou mot, rétabli à la réactivation */
    if (sounds) {
        cfg[CFG_MSOUND] = !cfg[CFG_MSOUND];
        say(cfg[CFG_MSOUND] ? "sons de la souris activés" : "sons de la souris désactivés", TRUE);
        return;
    }
    if (cfg[CFG_MOUSE]) {
        unit = cfg[CFG_MOUSE];
        cfg[CFG_MOUSE] = 0;
    } else
        cfg[CFG_MOUSE] = unit;
    ms_hwnd = NULL;
    say(cfg[CFG_MOUSE] ? "suivi de la souris activé" : "suivi de la souris désactivé", TRUE);
}

/* Fenêtre visible la plus profonde sous pt, désactivée comprise (WindowFromPoint
   de Windows 3.1 s'arrête à la fenêtre parente d'un contrôle grisé). Les cadres de
   groupe ne couvrent pas leurs contrôles (HTTRANSPARENT pour Windows). */
static HWND deepest(HWND h, POINT pt)
{
    HWND c, frame;
    RECT r;
    for (;;) {
        frame = NULL;
        for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            if (!IsWindowVisible(c))
                continue;
            GetWindowRect(c, &r);
            if (!PtInRect(&r, pt))
                continue;
            if (is_class(c, "BUTTON") &&
                (GetWindowLong(c, GWL_STYLE) & 0x0F) == BS_GROUPBOX) {
                if (!frame)
                    frame = c;
                continue;
            }
            break;
        }
        if (!c)
            c = frame;
        if (!c)
            return h;
        h = c;
    }
}

/* Contrôle dit en entier (rôle, libellé, état) plutôt que la rangée de texte :
   boutons, cases, barres de défilement, listes déroulantes, zones d'une ligne. */
static BOOL whole_object(HWND h)
{
    if (is_class(h, "BUTTON") || is_class(h, "SCROLLBAR") || is_class(h, "COMBOBOX"))
        return TRUE;
    if (is_class(h, "EDIT"))
        return !is_multiline(h) || is_class(GetParent(h), "COMBOBOX");
    return FALSE;
}

/* Texte sous le pointeur, dit s'il a changé depuis la dernière annonce */
static void mouse_say(POINT pt)
{
    static char text[TXT * 2];
    HWND top = WindowFromPoint(pt), h;
    POINT q;
    int col, row = 0;

    if (!top || top == GetDesktopWindow() || top == hwnd_main)
        return;
    h = deepest(top_level(top), pt);
    if (whole_object(h)) {
        if (h == ms_hwnd)
            return;
        describe(h, text);
    } else {
        q = pt;
        if (!EpOsmReviewLine(h, &q, 0, text, sizeof(text), &col)) {
            text[0] = 0;
            if (is_class(h, "STATIC"))
                GetWindowText(h, text, sizeof(text));
            col = -1;
            q.y = 0;
        } else if (cfg[CFG_MOUSE] == 2) {
            char *w = word_cut(text, col);  /* (pointeur sur une espace : rien) */
            if (w)
                lstrcpy(text, w);
            else
                text[0] = 0;
        }
        if (!text[0] || (cfg[CFG_MOUSE] == 1 && no_word(text)))
            return;                     /* rien de lisible : rien de nouveau */
        row = q.y;
        if (h == ms_hwnd && row == ms_row && !lstrcmp(text, ms_said))
            return;
    }
    ms_hwnd = h;
    ms_row = row;
    lstrcpyn(ms_said, text, sizeof(ms_said));
    say(text[0] ? text : "vide", TRUE);
}

/* Scrutation : pointeur déplacé par l'utilisateur */
void mouse_poll(void)
{
    POINT pt;
    int height;

    GetCursorPos(&pt);
    if (!ms_known) {
        mouse_placed();
        return;
    }
    if (pt.x == ms_last.x && pt.y == ms_last.y)
        return;
    ms_last = pt;
    if (cfg[CFG_MSOUND]) {
        /* avant la parole : avec Synthé3, le bip est mêlé à la phrase qui suit */
        height = GetSystemMetrics(SM_CYSCREEN);
        if (pt.y < 0) pt.y = 0;
        if (pt.y >= height) pt.y = height - 1;
        s3_tone(MS_LOW + (int) ((long) (MS_HIGH - MS_LOW) * (height - 1 - pt.y) /
                                (height > 1 ? height - 1 : 1)), MS_BEEP);
    }
    /* menus : Windows annonce l'élément survolé (WM_MENUSELECT) ; dire tout et
       invite MS-DOS : la lecture en cours garde la parole */
    if (cfg[CFG_MOUSE] && !in_menu && !sa_active() && !dos_relay)
        mouse_say(pt);
}
