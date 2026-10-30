/* EPAULARD.EXE — lecture continue (dire tout). */
#include "ep.h"

/* -------------------------------------------------- lecture continue (dire tout) */

/* Comme dans NVDA : lecture de la ligne du curseur jusqu'à la fin du document, ligne
   après ligne ; le curseur de l'application suit la lecture, ce qui fait défiler le
   document. Plusieurs lignes sont envoyées d'avance (SA_AHEAD) pour que la parole
   s'enchaîne sans pause, même quand il faut faire défiler l'écran ou traverser des
   lignes vides. Chaque ligne est suivie d'un écho @E : l'Apollo II le renvoie quand
   elle a été lue ; on envoie alors la suivante.
     EDIT (Bloc-notes) : lignes lues par EM_GETLINE, sans bouger le curseur ; celui-ci
     est posé par EM_SETSEL sur la ligne en cours de lecture.
     Texte dessiné (Word, Write) : on ne connaît que les rangées affichées. Le curseur
     est donc envoyé en avant, par les touches Bas puis Début, sur la dernière ligne
     envoyée : le programme fait défiler son texte pendant que la parole lit les
     lignes précédentes. Une interruption le ramène sur la ligne en cours de lecture. */

SA_STATE sa;

BOOL sa_edit_text(int line, char *out, int max)
{
    int n;
    if (line < 0 || line >= (int) SendMessage(sa.hwnd, EM_GETLINECOUNT, 0, 0L))
        return FALSE;
    *(WORD *) out = (WORD) (max - 1);
    n = (int) SendMessage(sa.hwnd, EM_GETLINE, line, (LPARAM) (LPSTR) out);
    out[n < max ? n : max - 1] = 0;
    return TRUE;
}

int sa_edit_caret_line(int *col)
{
    int pos = LOWORD(SendMessage(sa.hwnd, EM_GETSEL, 0, 0L));
    int line = (int) SendMessage(sa.hwnd, EM_LINEFROMCHAR, pos, 0L);
    if (col)
        *col = pos - (int) SendMessage(sa.hwnd, EM_LINEINDEX, line, 0L);
    return line;
}

BOOL sa_blank(const char *s)
{
    for (; *s; s++)
        if (*s != ' ' && *s != '\t')
            return FALSE;
    return TRUE;
}

/* Ligne du curseur ; from_caret : seulement à partir de la colonne du curseur */
void sa_caret_row(char *out, BOOL from_caret)
{
    int col = 0;
    out[0] = 0;
    if (sa.kind == 1)
        sa_edit_text(sa_edit_caret_line(&col), out, TXT * 2);
    else {
        POINT pt;
        caret_screen(sa.hwnd, &pt);
        pt.y += 2;
        if (!EpOsmReviewLine(sa.hwnd, &pt, 0, out, TXT * 2, &col))
            out[0] = 0;
    }
    if (from_caret && col > 0 && col <= lstrlen(out))
        memmove(out, out + col, lstrlen(out + col) + 1);
}

void sa_edit_caret(int line)
{
    int pos = (int) SendMessage(sa.hwnd, EM_LINEINDEX, line, 0L);
    if (pos >= 0)
        SendMessage(sa.hwnd, EM_SETSEL, 0, MAKELPARAM(pos, pos));
}

void sa_push(const char *text, int moves, int line)
{
    SALINE *l = &sa.q[sa.n];
    sa.seq = (char) (sa.seq >= 'Z' || sa.seq < 'A' ? 'A' : sa.seq + 1);
    say_mark(text, sa.seq);
    l->mark = sa.seq;
    l->len = lstrlen(text);
    l->moves = moves;
    l->line = line;
    if (sa.n++ == 0) {
        sa.head_tick = GetTickCount();
        if (sa.kind == 4)
            nav_sa_at(line);
        if (sa.kind == 1 && line != sa_edit_caret_line(NULL))
            sa_edit_caret(line);        /* lignes vides sautées : le curseur suit */
    }
}

void sa_key(UINT vk, int scan)
{
    /* touche étendue (flèches dédiées), une répétition, marquée EPK_OWN : jamais
       prise pour une frappe de l'utilisateur. Ne pas compter ces touches pour les
       écarter au retour : le crochet ne les voit pas, et un compteur avalait les
       vraies flèches pendant la lecture. */
    PostMessage(sa.hwnd, WM_KEYDOWN, vk, MAKELPARAM(1, scan | 0x100) | EPK_OWN);
    PostMessage(sa.hwnd, WM_KEYUP, vk, MAKELPARAM(1, scan | 0x100 | 0xC000) | EPK_OWN);
}

void sa_stop(BOOL cut)
{
    if (!sa.on)
        return;
    sa.on = FALSE;
    KillTimer(hwnd_main, TIMER_SA);
    if (cut)
        silence();
    if (sa.kind == 3 && is_help(sa.hwnd))    /* page défilée par la lecture : pas une */
        help_row(sa.hwnd, FALSE, status_said, sizeof(status_said), NULL);  /* autre rubrique */
    /* le curseur a bougé : ne pas relire la ligne où la lecture s'est arrêtée */
    line_key = FALSE;
    if (last_focus && IsWindow(last_focus)) {
        caret_screen(last_focus, &last_caret);
        remember_value(last_focus);
    }
}

/* Interruption par l'utilisateur : couper la parole et, pour le texte dessiné, ramener
   le curseur (envoyé en avant) sur la ligne qui était en cours de lecture. quiet : ne
   pas annoncer cette position (Ctrl, commande d'Épaulard). */
void sa_interrupt(BOOL quiet)
{
    int i, up;
    if (!sa.on)
        return;
    if (sa.kind == 2 && sa.n && sa.hwnd == GetFocus()) {
        up = sa.moves + (sa.moving ? 1 : 0);
        for (i = 1; i < sa.n; i++)
            up += sa.q[i].moves;
        if (up) {
            while (up-- > 0)
                sa_key(VK_UP, 0x48);
            sa_key(VK_HOME, 0x47);
            caret_settle = GetTickCount();
            caret_quiet = quiet;
        }
    }
    sa_stop(TRUE);
}

/* Descendre le curseur d'une ligne (texte dessiné) */
void sa_move(void)
{
    sa.moving = TRUE;
    sa.move_tick = GetTickCount();
    caret_screen(sa.hwnd, &sa.caret);
    sa.sig = EpOsmSignature(sa.hwnd);
    sa_key(VK_DOWN, 0x50);
    sa_key(VK_HOME, 0x47);
}

/* Aide, page : rangée suivant sa.y dans la partie visible ; FALSE en bas de la fenêtre */
BOOL sa_help_next(char *text)
{
    RECT r;
    POINT pt;
    int col;
    if (!doc_area(sa.hwnd, FALSE, &r))
        return FALSE;
    pt.x = r.left + 2;
    pt.y = sa.y;
    if (!EpOsmReviewLine(sa.hwnd, &pt, 1, text, TXT * 2, &col) || pt.y >= r.bottom)
        return FALSE;
    sa.y = pt.y;
    return TRUE;
}

/* Même rangée, au signe de tête près : une puce (« · » de Netscape) peut se rattacher
   à une rangée voisine après le défilement */
BOOL same_row(const char *a, const char *b)
{
    while (*a && !IsCharAlphaNumeric(*a))
        a++;
    while (*b && !IsCharAlphaNumeric(*b))
        b++;
    return lstrcmp(a, b) == 0;
}

/* Aide : page suivante affichée ; reprendre après la dernière rangée lue si elle est
   restée visible (recouvrement), sinon au haut de la rubrique */
void sa_help_resume(void)
{
    char text[TXT * 2];
    RECT r;
    int top;
    if (!doc_area(sa.hwnd, FALSE, &r)) {
        sa.eof = TRUE;
        return;
    }
    top = sa.y = r.top - 1;
    if (sa.last[0])
        while (sa_help_next(text))
            if (same_row(text, sa.last))
                return;                 /* sa.y : cette rangée */
    sa.y = top;
}

/* Envoyer d'avance les lignes suivantes, jusqu'à SA_AHEAD */
void sa_fill(void)
{
    char text[TXT * 2];
    if (!sa.on)
        return;
    if (sa.kind == 4) {
        while (sa.n < SA_AHEAD && !sa.eof) {
            int start = sa.line;
            if (!nav_sa_text(&sa.line, text, sizeof(text)))
                sa.eof = TRUE;
            else if (!sa_blank(text))
                sa_push(text, 0, start);
        }
    } else if (sa.kind == 3) {
        while (sa.n < SA_AHEAD && !sa.eof && !sa.moving) {
            if (sa_help_next(text)) {
                lstrcpyn(sa.last, text, sizeof(sa.last));
                if (!sa_blank(text))
                    sa_push(text, 0, 0);
            } else {
                /* bas de la fenêtre : page suivante */
                sa.moving = TRUE;
                sa.move_tick = GetTickCount();
                sa.sig = EpOsmSignature(sa.hwnd);
                sa_key(VK_NEXT, 0x51);
            }
        }
    } else if (sa.kind == 1) {
        while (sa.n < SA_AHEAD && !sa.eof) {
            if (!sa_edit_text(sa.line, text, sizeof(text))) {
                sa.eof = TRUE;
                break;
            }
            if (!sa_blank(text))
                sa_push(text, 0, sa.line);
            sa.line++;
        }
    } else if (!sa.moving && !sa.eof && sa.n < SA_AHEAD)
        sa_move();
    if (sa.eof && !sa.n && !sa.moving)
        sa_stop(FALSE);                 /* fin du document : tout a été lu */
}

void sa_end(void)
{
    sa.moving = FALSE;
    sa.eof = TRUE;
    sa_fill();
}

/* Texte dessiné : le curseur a atteint la ligne suivante (ou n'a pas pu bouger) */
void sa_arrived(void)
{
    char text[TXT * 2];
    POINT pt;
    BOOL moved;
    DWORD dt = GetTickCount() - sa.move_tick;

    if (sa.kind == 3) {
        /* Aide : attendre que la page soit redessinée ; inchangée, c'est la fin */
        if (EpOsmSignature(sa.hwnd) == sa.sig) {
            if (dt >= 700)
                sa_end();
            return;
        }
        if (dt < 300)
            return;
        sa.moving = FALSE;
        if (page_view(sa.hwnd)) {
            /* Netscape fait défiler sa page sans que le modèle suive toutes ses
               copies : la faire repeindre entière avant d'en lire les rangées */
            InvalidateRect(sa.hwnd, NULL, TRUE);
            UpdateWindow(sa.hwnd);
        }
        sa_help_resume();
        sa_fill();
        return;
    }
    caret_screen(sa.hwnd, &pt);
    moved = pt.x != sa.caret.x || pt.y != sa.caret.y;
    /* Laisser le programme traiter les touches. Si le curseur reste en place et que
       rien n'a changé à l'écran, c'est la fin du document ; si le texte a changé,
       il a défilé sous le curseur (bas de la fenêtre). */
    if (!moved && EpOsmSignature(sa.hwnd) == sa.sig) {
        if (dt >= 500)
            sa_end();
        return;
    }
    sa_caret_row(text, FALSE);
    if (sa_blank(text)) {
        /* ligne vide, ou pas encore dessinée après un défilement */
        if (dt < (DWORD) (moved ? 100 : 300))
            return;
        sa.moves++;
        if (++sa.blanks > 100)
            sa_end();
        else
            sa_move();
        return;
    }
    sa.moves++;
    sa.moving = FALSE;
    sa.blanks = 0;
    sa_push(text, sa.moves, 0);
    sa.moves = 0;
    sa_fill();
}

/* Fin de lecture de q[0] : passer à la ligne suivante */
void sa_pop(void)
{
    int i;
    if (!sa.n)
        return;
    sa.n--;
    for (i = 0; i < sa.n; i++)
        sa.q[i] = sa.q[i + 1];
    sa.head_tick = GetTickCount();
    if (sa.kind == 1 && sa.n)
        sa_edit_caret(sa.q[0].line);
    if (sa.kind == 4 && sa.n)
        nav_sa_at(sa.q[0].line);        /* le curseur virtuel suit la lecture */
    sa_fill();
}

/* Écho renvoyé par l'Apollo II : une ligne a fini d'être lue (et celles d'avant) */
void sa_mark(char c)
{
    int i, k;
    if (!sa.on || !c)
        return;
    for (i = 0; i < sa.n; i++)
        if (sa.q[i].mark == c) {
            for (k = 0; k <= i && sa.on; k++)
                sa_pop();
            return;
        }
}

void sa_tick(void)
{
    if (!sa.on)
        return;
    if (!IsWindow(sa.hwnd) || GetFocus() != sa.hwnd) {
        sa_stop(TRUE);
        return;
    }
    com_read();
    if (sa.on && sa.moving)
        sa_arrived();
    if (sa.on && sa.n &&
        GetTickCount() - sa.head_tick > 3000 + 150 * (DWORD) sa.q[0].len)
        sa_pop();       /* pas d'écho en retour (synthèse muette ou débranchée) */
}

BOOL sa_active(void)
{
    return sa.on;
}

void sa_start(void)
{
    HWND h = GetFocus();
    char text[TXT * 2];
    int line;

    sa_stop(FALSE);
    memset(&sa, 0, sizeof(sa));
    if (!h)
        return;
    if (nav_sa_start(&line)) {
        /* page Web en mode navigation : lignes du tampon, depuis le curseur virtuel */
        sa.kind = 4;
        sa.hwnd = h;
        sa.on = TRUE;
        silence();
        sa.line = line;
        sa_fill();
        if (sa.on)
            SetTimer(hwnd_main, TIMER_SA, SA_MS, NULL);
        return;
    }
    if (is_help(h) || page_view(h)) {
        /* Aide, page de Netscape : de la première rangée affichée, puis page par page */
        RECT r;
        if (!help_row(h, FALSE, text, sizeof(text), NULL) || !doc_area(h, TRUE, &r))
            return;
        sa.kind = 3;
        sa.hwnd = h;
        sa.on = TRUE;
        silence();
        sa.y = r.top - 1;               /* au-dessus de la première rangée */
        sa_fill();
        if (sa.on)
            SetTimer(hwnd_main, TIMER_SA, SA_MS, NULL);
        return;
    }
    if (is_class(h, "EDIT") && !(GetWindowLong(h, GWL_STYLE) & ES_PASSWORD))
        sa.kind = 1;
    else if (!is_standard(h))
        sa.kind = 2;
    else {
        read_window();                  /* boîte de dialogue : tout son contenu */
        return;
    }
    sa.hwnd = h;
    sa.on = TRUE;
    silence();
    sa_caret_row(text, TRUE);
    line = sa.kind == 1 ? sa_edit_caret_line(NULL) : 0;
    if (!sa_blank(text))
        sa_push(text, 0, line);
    sa.line = line + 1;
    sa_fill();
    if (sa.on)
        SetTimer(hwnd_main, TIMER_SA, SA_MS, NULL);
}
