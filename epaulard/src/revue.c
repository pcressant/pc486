/* EPAULARD.EXE — souris, revue au pavé et suivi braille. */
#include "ep.h"

/* ------------------------------------------------------------------ souris */

/* MOUSE_EVENT de USER, point d'entrée du pilote de souris : paramètres dans les
   registres (AX drapeaux, BX et CX position, DX nombre de boutons, SI:DI information
   supplémentaire). Les clics passent ainsi par la file système comme un vrai clic.
   Sans drapeau de déplacement, ils ont lieu à la position courante du pointeur. */

FARPROC mouse_fn;
int mouse_locked;        /* boutons verrouillés : 1 gauche, 2 droit */
POINT mouse_back;        /* position à rétablir après un clic de routage */

void mouse_raw(WORD flags)
{
    if (!mouse_fn)
        mouse_fn = GetProcAddress(GetModuleHandle("USER"), "MOUSE_EVENT");
    if (!mouse_fn)
        return;
#ifndef EP_NATIVE_TEST                  /* banc natif (tests/win16) : pas de pilote */
    _asm {
        push si
        push di
        mov ax, flags
        xor bx, bx
        xor cx, cx
        mov dx, 2
        xor si, si
        xor di, di
        pushf
        cli
        call dword ptr mouse_fn
        popf
        pop di
        pop si
    }
#endif
}

void mouse_button(BOOL right, BOOL down)
{
    mouse_raw(right ? (down ? ME_RIGHTDOWN : ME_RIGHTUP) : (down ? ME_LEFTDOWN : ME_LEFTUP));
}

void mouse_click(BOOL right)
{
    int bit = right ? 2 : 1;
    if (mouse_locked & bit) {           /* bouton verrouillé : le relâcher d'abord */
        mouse_button(right, FALSE);
        mouse_locked &= ~bit;
    }
    mouse_button(right, TRUE);
    mouse_button(right, FALSE);
}

/* Clic au point pt (écran) ; restore : remettre le pointeur où il était */
void click_at(POINT pt, BOOL right, BOOL restore)
{
    if (restore) {
        GetCursorPos(&mouse_back);
        SetTimer(hwnd_main, TIMER_MS, 300, NULL);
    }
    SetCursorPos(pt.x, pt.y);
    mouse_click(right);
}

void on_mouse(int action)
{
    BOOL right = (action & 3) == 2;
    int bit = right ? 2 : 1;

    if (action & 4) {                   /* Maj : verrouiller ou relâcher */
        mouse_locked ^= bit;
        say((mouse_locked & bit) ?
            (right ? "bouton droit verrouillé" : "bouton gauche verrouillé") :
            (right ? "bouton droit relâché" : "bouton gauche relâché"), TRUE);
        mouse_button(right, (mouse_locked & bit) != 0);
        return;
    }
    say(right ? "clic droit" : "clic gauche", TRUE);
    mouse_click(right);
}

/* --------------------------------------------------------- revue au pavé */

void review_anchor(HWND h, POINT *pt)
{
    RECT rc;
    if (EpOsmCaret(h, pt))
        return;
    if (is_class(h, "EDIT")) {
        caret_screen(h, pt);
        return;
    }
    GetWindowRect(h, &rc);
    pt->x = (rc.left + rc.right) / 2;
    pt->y = (rc.top + rc.bottom) / 2;
}

BOOL review_edit_line(REVIEW *r, int row)
{
    int n, count = (int) SendMessage(r->scope, EM_GETLINECOUNT, 0, 0L);
    if (row < 0 || row >= count)
        return FALSE;
    *(WORD *) r->text = sizeof(r->text) - 1;
    n = (int) SendMessage(r->scope, EM_GETLINE, row, (LPARAM) (LPSTR) r->text);
    r->text[n] = 0;
    r->row = row;
    return TRUE;
}

BOOL review_load(REVIEW *r)
{
    HWND h = GetFocus(), top = GetActiveWindow();
    int col;
    if (!top || !IsWindow(top))
        return FALSE;
    if (!h) h = top;
    memset(r, 0, sizeof(*r));
    r->scope = h;
    if (!flat_review && calc_value(r->text)) {
        r->scope = top;
        if (calc_view == 1 && calc_button >= 0 && calc_available(calc_button)) {
            lstrcpyn(r->text, calc_name(calc_button), sizeof(r->text));
            append(r->text, "bouton");
        } else if (calc_view == 2) calc_summary(r->text);
        else if (h != top && is_standard(h) && IsWindowVisible(h)) describe(h, r->text);
        return TRUE;
    }
    if (flat_review) {
        r->kind = 2;
        r->scope = top;
        if (review_window == top) {
            r->pt = review_point;
            if (!EpOsmReviewLine(top, &r->pt, 2, r->text, sizeof(r->text), &col))
                return FALSE;
            r->col = review_column;
        } else {
            review_anchor(h, &r->pt);
            if (!EpOsmReviewLine(top, &r->pt, 2, r->text, sizeof(r->text), &r->col))
                return FALSE;
        }
        if (r->col > lstrlen(r->text)) r->col = lstrlen(r->text);
        review_window = top;
        review_point = r->pt;
        review_column = r->col;
    } else if (is_class(h, "EDIT")) {
        if (GetWindowLong(h, GWL_STYLE) & ES_PASSWORD) {
            lstrcpyn(r->text, "mot de passe", sizeof(r->text));
            return TRUE;
        }
        r->kind = 1;
        col = LOWORD(SendMessage(h, EM_GETSEL, 0, 0L));
        r->row = (int) SendMessage(h, EM_LINEFROMCHAR, col, 0L);
        r->col = col - (int) SendMessage(h, EM_LINEINDEX, r->row, 0L);
        if (!review_edit_line(r, r->row))
            return FALSE;
        if (r->col > lstrlen(r->text)) r->col = lstrlen(r->text);
    } else if (is_standard(h)) {
        describe(h, r->text);
    } else if (is_sdm(h) && h == sdm_hwnd && sdm_text[0]) {
        lstrcpyn(r->text, sdm_text, sizeof(r->text));     /* boîte de Word : élément du rectangle de focus */
    } else if (EpOsmCaret(h, &r->pt)) {
        r->kind = 2;
        r->pt.x += 1;                   /* caractère qui suit le curseur */
        r->pt.y += 2;
        EpOsmReviewLine(h, &r->pt, 0, r->text, sizeof(r->text), &r->col);
    } else if (!EpOsmHilite(h, r->text, sizeof(r->text))) {
        r->kind = 2;
        review_anchor(h, &r->pt);
        if (!EpOsmReviewLine(h, &r->pt, 2, r->text, sizeof(r->text), &r->col))
            return FALSE;
    }
    return TRUE;
}

BOOL review_line(REVIEW *r, int direction)
{
    if (r->kind == 1)
        return review_edit_line(r, r->row + direction);
    if (r->kind == 2)
        return EpOsmReviewLine(r->scope, &r->pt, direction, r->text,
                              sizeof(r->text), &r->col) != 0;
    return direction == 0;
}

/* Position (écran) du point de revue : caractère de la revue à plat, sinon curseur de
   saisie, élément d'une boîte de Word ou milieu du contrôle qui a le focus.
   *scope : fenêtre dont le texte est concerné. */
BOOL review_screen_point(POINT *pt, HWND *scope)
{
    REVIEW r;
    RECT rc;
    HWND h = GetFocus();
    char buf[TXT * 2];
    int col;

    if (flat_review) {
        if (!review_load(&r))
            return FALSE;
        *pt = r.pt;
        *scope = r.scope;
        return EpOsmReviewPoint(r.scope, pt, r.col);
    }
    /* Un bouton virtuel/bilan n'a pas de point écran : ne pas amener la souris
       au milieu de SciCalc, ce qui ferait viser un autre bouton. */
    if (calc_view && is_class(GetActiveWindow(), "SCICALC")) return FALSE;
    if (!h)
        h = GetActiveWindow();
    if (!h)
        return FALSE;
    *scope = h;
    if (is_sdm(h) && EpOsmFocusRect(h, &rc)) {
        pt->x = (rc.left + rc.right) / 2;
        pt->y = (rc.top + rc.bottom) / 2;
    } else if (EpOsmCaret(h, pt) || is_class(h, "EDIT")) {
        POINT q;
        caret_screen(h, pt);
        q.x = pt->x + 1;
        q.y = pt->y + 2;
        if (EpOsmReviewLine(h, &q, 0, buf, sizeof(buf), &col))
            pt->y = q.y;                /* milieu de la ligne */
        else
            pt->y += 2;
        pt->x++;
    } else {
        GetWindowRect(h, &rc);
        pt->x = (rc.left + rc.right) / 2;
        pt->y = (rc.top + rc.bottom) / 2;
    }
    return TRUE;
}

/* Inser + / : pointeur de la souris au point de revue */
void mouse_to_review(void)
{
    POINT pt;
    HWND scope;
    if (!review_screen_point(&pt, &scope)) {
        say("aucune position", TRUE);
        return;
    }
    SetCursorPos(pt.x, pt.y);
    say("souris déplacée", TRUE);
}

/* Inser + * : lire la rangée sous le pointeur ; en revue à plat, y placer la revue */
void review_to_mouse(void)
{
    POINT pt, q;
    HWND top;
    char text[TXT * 2];
    int col;

    GetCursorPos(&pt);
    top = top_level(WindowFromPoint(pt));
    q = pt;
    if (!top || !EpOsmReviewLine(top, &q, 2, text, sizeof(text), &col)) {
        say("rien sous la souris", TRUE);
        return;
    }
    if (flat_review && top == GetActiveWindow()) {
        review_window = top;
        review_point = q;
        review_point.x = pt.x;
        review_column = col;
    }
    say(text[0] ? text : "vide", TRUE);
}

/* Inser + F : police et attributs au point de revue */
void say_format(void)
{
    POINT pt;
    HWND scope;
    char out[TXT];
    if (!review_screen_point(&pt, &scope) || !EpOsmFormat(scope, pt.x, pt.y, out, sizeof(out)))
        say("mise en forme inconnue", TRUE);
    else
        say(out, TRUE);
}

/* Inser + Maj + Haut : texte sélectionné */
void say_selected(void)
{
    HWND h = GetFocus();
    static char text[TXT * 2];
    DWORD sel;

    text[0] = 0;
    if (h && is_class(h, "EDIT")) {
        sel = SendMessage(h, EM_GETSEL, 0, 0L);
        if (GetWindowLong(h, GWL_STYLE) & ES_PASSWORD)
            sel = 0;
        if (LOWORD(sel) != HIWORD(sel)) {
            edit_range(h, LOWORD(sel), HIWORD(sel), text, sizeof(text));
            if (HIWORD(sel) - LOWORD(sel) > 400) {
                wsprintf(text, "%d caractères sélectionnés", HIWORD(sel) - LOWORD(sel));
                say(text, TRUE);
                return;
            }
        }
    } else if (h && !is_standard(h))
        EpOsmSelection(h, text, sizeof(text));
    say(text[0] ? text : "aucune sélection", TRUE);
}

/* Le braille conserve sa propre ligne de revue ; un changement du focus,
   du texte ou du curseur ramène automatiquement au suivi du document. */
REVIEW braille_live, braille_view;
BOOL braille_valid;
int braille_epoch;       /* la DLL a écrit seule (boîte modale système) */
int route_key;
DWORD route_tick;

BOOL braille_same(REVIEW *a, REVIEW *b)
{
    return a->scope == b->scope && a->kind == b->kind && a->row == b->row &&
           a->col == b->col && a->pt.y == b->pt.y && a->sel_from == b->sel_from &&
           a->sel_to == b->sel_to && lstrcmp(a->text, b->text) == 0;
}

/* Partie sélectionnée de la ligne affichée en braille */
void braille_selection(REVIEW *r)
{
    r->sel_from = r->sel_to = -1;
    if (r->kind == 1) {
        DWORD sel = SendMessage(r->scope, EM_GETSEL, 0, 0L);
        int base = is_multiline(r->scope) ?
            (int) SendMessage(r->scope, EM_LINEINDEX, r->row, 0L) : 0;
        int a = (int) LOWORD(sel) - base, b = (int) HIWORD(sel) - base;
        int n = lstrlen(r->text);
        if (a < 0) a = 0;
        if (b > n) b = n;
        if (a < b) {
            r->sel_from = a;
            r->sel_to = b;
        }
    } else if (r->kind == 2)
        EpOsmRowSelection(r->scope, r->pt.y, &r->sel_from, &r->sel_to);
}

void say_column(const char *text, int col)
{
    int n = lstrlen(text);
    if (!n)
        say("vide", TRUE);
    else if (col >= n)
        say("fin de ligne", TRUE);
    else if (text[col] == ' ' || text[col] == '\t')
        say("espace", TRUE);
    else
        say_char(text[col], TRUE);
}

/* Curseur routine : zone d'édition, déplacer le curseur ; revue à plat, déplacer
   le point de revue (deux appuis : clic gauche à cet endroit) ; texte dessiné
   (Write, Word), cliquer sur le caractère pour y poser le curseur. */
void braille_route(int key)
{
    REVIEW *v = &braille_view;
    HWND h = v->scope;
    POINT pt;
    int pos, base;
    BOOL again = key == route_key && GetTickCount() - route_tick < 1000;

    route_key = key;
    route_tick = GetTickCount();
    pos = br_offset() + key - BR_ROUTE;
    if (!IsWindow(h))
        return;
    if (v->kind == 1) {
        /* Ne jamais déplacer le curseur d'un ancien contrôle. */
        if (h != GetFocus() || (GetWindowLong(h, GWL_STYLE) & ES_PASSWORD) ||
            pos > lstrlen(v->text))
            return;
        base = (int) SendMessage(h, EM_LINEINDEX, v->row, 0L);
        if (base < 0)
            return;
        SendMessage(h, EM_SETSEL, 0, MAKELONG(base + pos, base + pos));
    } else if (v->kind == 2 && flat_review) {
        if (h != GetActiveWindow())
            return;
        if (pos > lstrlen(v->text))
            pos = lstrlen(v->text);
        review_window = h;
        review_point = v->pt;
        review_column = pos;
        pt = v->pt;
        if (again && EpOsmReviewPoint(h, &pt, pos)) {
            say("clic gauche", TRUE);
            click_at(pt, FALSE, FALSE);
            route_key = 0;
        } else
            say_column(v->text, pos);
    } else if (v->kind == 2 && h == GetFocus()) {
        pt = v->pt;
        if (pos > lstrlen(v->text) || !EpOsmReviewPoint(h, &pt, pos))
            return;
        click_at(pt, FALSE, TRUE);
    } else
        return;
    braille_valid = FALSE;
}

void braille_poll(void)
{
    REVIEW r;
    int key, epoch = EpBrailleEpoch();
    if (epoch != braille_epoch) {       /* affichage remplacé par la DLL */
        braille_epoch = epoch;
        braille_valid = FALSE;
    }
    if (GetSysModalWindow())
        return;                 /* la DLL possède la plage pendant la boîte */
    if (dos_relay) {
        braille_valid = FALSE;  /* invite MS-DOS : la plage est à Épaudos (dosbox.c) */
        return;
    }
    if (in_menu) {
        br_text(braille_menu, -1, FALSE);
        br_select(-1, -1);
        braille_valid = FALSE;
    } else if (nav_br_show()) {
        braille_valid = FALSE;  /* page Web en mode navigation : curseur virtuel */
    } else if (review_load(&r)) {
        braille_selection(&r);
        if (!braille_valid || !braille_same(&r, &braille_live)) {
            braille_live = r;
            braille_view = r;
            braille_valid = TRUE;
            br_text(r.text, r.kind ? r.col : -1, TRUE);
            br_select(r.sel_from, r.sel_to);
        }
    } else {
        br_text("", -1, FALSE);
        br_select(-1, -1);
        braille_valid = FALSE;
    }
    br_tick();
    key = br_key();
    if (!in_menu && nav_br_key(key))
        ;
    else if (key == BR_BACK || key == BR_FORWARD) br_pan(key == BR_BACK ? -1 : 1);
    else if (key == BR_HOME) {
        braille_valid = FALSE;
        br_pan(0);
    } else if (!in_menu && braille_valid && (key == BR_UP || key == BR_DOWN)) {
        if (review_line(&braille_view, key == BR_UP ? -1 : 1)) {
            braille_view.col = 0;
            if (flat_review && braille_view.kind == 2) {
                /* revue à plat : la parole et la souris suivent la ligne affichée */
                review_window = braille_view.scope;
                review_point = braille_view.pt;
                review_column = 0;
            }
            /* Une ligne de revue n'affiche pas le curseur de saisie. */
            braille_selection(&braille_view);
            br_text(braille_view.text, -1, FALSE);
            br_select(braille_view.sel_from, braille_view.sel_to);
        }
    } else if (!in_menu && braille_valid && key >= BR_ROUTE)
        braille_route(key);
}

BOOL review_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void review_store(REVIEW *r)
{
    if (flat_review) {
        review_point = r->pt;
        review_column = r->col;
    }
}

void on_review(int key)
{
    REVIEW r;
    int direction, n, start, end;

    if (key == 10) {
        flat_review = !flat_review;
        review_window = NULL;
        say(flat_review ? "revue à plat" : "revue normale", TRUE);
        return;
    }
    if (key == 11)
        review_window = NULL;
    if (!review_load(&r)) {
        say("aucun texte disponible", TRUE);
        return;
    }
    if (key == 11) {
        say("revue recentrée", TRUE);
        say(r.text[0] ? r.text : "vide", FALSE);
        return;
    }
    direction = key == 7 || key == 4 || key == 1 ? -1 :
                key == 9 || key == 6 || key == 3 ? 1 : 0;
    if (key >= 7) {
        if (direction && !review_line(&r, direction))
            goto boundary;
        if (direction) r.col = 0;
        review_store(&r);
        say(r.text[0] ? r.text : "vide", TRUE);
        return;
    }
    n = lstrlen(r.text);
    if (key <= 3) {
        if (direction < 0) {
            if (r.col > 0)
                r.col--;
            else {
                if (!review_line(&r, -1)) goto boundary;
                n = lstrlen(r.text);
                r.col = n ? n - 1 : 0;
            }
        } else if (direction > 0) {
            if (r.col < n)
                r.col++;
            else {
                if (!review_line(&r, 1)) goto boundary;
                r.col = 0;
            }
        }
        review_store(&r);
        n = lstrlen(r.text);
        if (!n) say("vide", TRUE);
        else if (r.col >= n) say("fin de ligne", TRUE);
        else if (review_space(r.text[r.col])) say("espace", TRUE);
        else say_char(r.text[r.col], TRUE);
        return;
    }
    if (direction < 0) {
        if (!r.col) {
            if (!review_line(&r, -1)) goto boundary;
            r.col = lstrlen(r.text);
        }
        while (r.col > 0 && review_space(r.text[r.col - 1])) r.col--;
        if (r.col > 0) r.col--;
        while (r.col > 0 && !review_space(r.text[r.col - 1])) r.col--;
    } else if (direction > 0) {
        while (r.col < n && !review_space(r.text[r.col])) r.col++;
        while (r.col < n && review_space(r.text[r.col])) r.col++;
        if (r.col >= n) {
            if (!review_line(&r, 1)) goto boundary;
            r.col = 0;
            while (review_space(r.text[r.col])) r.col++;
        }
    }
    review_store(&r);
    n = lstrlen(r.text);
    if (!n) say("vide", TRUE);
    else if (r.col >= n) say("fin de ligne", TRUE);
    else if (review_space(r.text[r.col])) say("espace", TRUE);
    else {
        start = r.col;
        while (start > 0 && !review_space(r.text[start - 1])) start--;
        end = r.col;
        while (r.text[end] && !review_space(r.text[end])) end++;
        r.text[end] = 0;
        say(r.text + start, TRUE);
    }
    return;
boundary:
    say(direction < 0 ? "début de la zone" : "fin de la zone", TRUE);
}
