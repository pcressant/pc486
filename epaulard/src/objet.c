/* EPAULARD.EXE — navigation par objet, comme NVDA (Inser + pavé numérique).

   Windows 3.1 n'a pas d'arbre d'accessibilité : l'arbre des objets est celui des
   fenêtres. Une fenêtre a pour enfants ses fenêtres filles visibles (ordre de
   Windows, celui de la tabulation dans une boîte), puis les textes qu'elle a
   dessinés elle-même, relevés par le modèle hors écran (de haut en bas, de gauche à
   droite) : c'est ce qui permet de découvrir une application qui dessine tout dans
   une seule fenêtre. Au-dessus des fenêtres principales : le bureau.

   Inser+8 parent, Inser+2 premier enfant, Inser+4 et Inser+6 précédent et suivant,
   Inser+9 et Inser+3 précédent et suivant dans l'ordre de lecture (vue à plat),
   Inser+5 objet courant, Inser+Entrée du pavé activer (clic au centre), Inser+moins
   objet du focus, Inser+Maj+moins focus sur l'objet, Inser+Suppr du pavé position. */
#include "ep.h"

#define OBJ_SLOTS 320           /* entrées du modèle hors écran (OSM_N d'osm.c) */
#define PIECES    40

static HWND obj;                /* objet navigateur : une fenêtre… */
static int piece = -1;          /* …ou un de ses textes dessinés (indice dans pc) */
static struct { RECT r; char t[64]; } pc[PIECES];
static int pc_n;
static HWND pc_of;              /* fenêtre dont pc décrit les textes */
static char obj_text[TXT];
static HWND obj_focus;          /* focus quand l'objet a été choisi (braille) */
static BOOL obj_braille, obj_dirty;
BOOL obj_recent;                /* Inser+/ : amener la souris à l'objet */

/* Textes dessinés par h lui-même, dans l'ordre de lecture */
static int load_pieces(HWND h)
{
    HWND w;
    RECT r, client;
    POINT pt;
    char t[TXT];
    int i, j, k, n;
    pc_n = 0;
    pc_of = h;
    GetClientRect(h, &client);
    pt.x = pt.y = 0;
    ClientToScreen(h, &pt);
    OffsetRect(&client, pt.x, pt.y);
    for (i = 0; i < OBJ_SLOTS && pc_n < PIECES; i++) {
        if (!EpOsmEntry(i, &w, &r, t, sizeof(t)) || w != h)
            continue;
        pt.x = (r.left + r.right) / 2;  /* barre de titre, menus : hors zone client */
        pt.y = (r.top + r.bottom) / 2;
        if (!PtInRect(&client, pt))
            continue;
        for (n = 0; t[n]; n++)          /* police ajoutée par EpOsmEntry */
            if (t[n] == ' ' && !_fstrncmp(t + n, " (police ", 9)) {
                t[n] = 0;
                break;
            }
        for (k = 0; t[k] == ' '; k++) {}
        if (!t[k])
            continue;
        for (n = lstrlen(t); n > k && t[n - 1] == ' '; n--)
            t[n - 1] = 0;
        for (j = pc_n; j > 0 && (pc[j - 1].r.top > r.top ||
                                 (pc[j - 1].r.top == r.top && pc[j - 1].r.left > r.left)); j--)
            pc[j] = pc[j - 1];
        pc[j].r = r;
        lstrcpyn(pc[j].t, t + k, sizeof(pc[j].t));
        pc_n++;
    }
    /* Étiquette d'icône coupée en deux lignes : textes étroits l'un sous l'autre,
       centrés sur le même axe (Progman : même case pour les deux lignes) */
    for (i = 0; i < pc_n; i++)
        for (j = i + 1; j < pc_n; j++) {
            RECT *a = &pc[i].r, *b = &pc[j].r;
            int ca = (a->left + a->right) / 2, cb = (b->left + b->right) / 2;
            if (b->top < a->bottom - 1 || b->top > a->bottom + 4 || ca - cb > 3 ||
                cb - ca > 3 || a->right - a->left >= 100 || b->right - b->left >= 100)
                continue;
            if (lstrlen(pc[i].t) + lstrlen(pc[j].t) + 2 <= sizeof(pc[i].t)) {
                lstrcat(pc[i].t, " ");
                lstrcat(pc[i].t, pc[j].t);
            }
            UnionRect(a, a, b);
            for (k = j; k + 1 < pc_n; k++)
                pc[k] = pc[k + 1];
            pc_n--;
            j--;
        }
    return pc_n;
}

static int pieces_of(HWND h)
{
    return h == GetDesktopWindow() ? 0 : load_pieces(h);
}

static HWND parent_of(HWND h)
{
    if (h == GetDesktopWindow())
        return NULL;
    if (GetWindowLong(h, GWL_STYLE) & WS_CHILD)
        return GetParent(h);
    return GetDesktopWindow();
}

static HWND visible(HWND h, UINT dir)
{
    while (h && !IsWindowVisible(h))
        h = GetWindow(h, dir);
    return h;
}
static HWND first_child(HWND h) { return visible(GetWindow(h, GW_CHILD), GW_HWNDNEXT); }
static HWND next_of(HWND h) { return visible(GetWindow(h, GW_HWNDNEXT), GW_HWNDNEXT); }
static HWND prev_of(HWND h) { return visible(GetWindow(h, GW_HWNDPREV), GW_HWNDPREV); }
static HWND last_child(HWND h)
{
    HWND c = first_child(h), n;
    while (c && (n = next_of(c)) != NULL)
        c = n;
    return c;
}

static void obj_check(void)
{
    if (!obj || !IsWindow(obj) || (obj != GetDesktopWindow() && !IsWindowVisible(obj))) {
        obj = GetFocus();
        if (!obj)
            obj = GetActiveWindow();
        piece = -1;
    }
    if (piece >= 0 && (pc_of != obj || piece >= pieces_of(obj)))
        piece = -1;
}

static void obj_rect(RECT *r)
{
    if (piece >= 0)
        *r = pc[piece].r;
    else
        GetWindowRect(obj, r);
}

static void obj_describe(char *out)
{
    char cls[32];
    out[0] = 0;
    if (piece >= 0) {
        lstrcpy(out, pc[piece].t);
        return;
    }
    if (obj == GetDesktopWindow()) {
        lstrcpy(out, "Bureau");
        return;
    }
    if (!(GetWindowLong(obj, GWL_STYLE) & WS_CHILD)) {
        GetWindowText(obj, out, TXT - 16);
        if (!out[0])
            lstrcpy(out, "sans titre");
        append(out, "fenêtre");
    } else
        describe(obj, out);
    if (!out[0]) {                      /* contrôle inconnu : sa classe */
        raw_class(obj, cls, sizeof(cls));
        wsprintf(out, "volet %s", (LPSTR) cls);
    }
    if (!IsWindowEnabled(obj))
        append(out, "indisponible");
}

/* Dire l'objet et l'afficher en braille ; edge : message de bord à dire avant */
static void obj_say(const char *edge)
{
    obj_describe(obj_text);
    if (edge)
        say(edge, TRUE);
    say(obj_text[0] ? obj_text : "vide", edge == NULL);
    obj_focus = GetFocus();
    obj_braille = obj_dirty = TRUE;
    obj_recent = TRUE;
}

/* Déplacements, sans parole ; FALSE au bord */
static BOOL to_parent(void)
{
    HWND p;
    if (piece >= 0) {
        piece = -1;
        return TRUE;
    }
    if ((p = parent_of(obj)) == NULL)
        return FALSE;
    obj = p;
    return TRUE;
}

static BOOL to_child(void)
{
    HWND c;
    if (piece >= 0)
        return FALSE;
    if ((c = first_child(obj)) != NULL) {
        obj = c;
        return TRUE;
    }
    if (pieces_of(obj)) {
        piece = 0;
        return TRUE;
    }
    return FALSE;
}

/* Suivant parmi les frères : fenêtres filles, puis textes du parent */
static BOOL to_next(void)
{
    HWND n, p;
    if (obj == GetDesktopWindow())
        return FALSE;
    if (piece >= 0) {
        if (piece + 1 >= pc_n)
            return FALSE;
        piece++;
        return TRUE;
    }
    if ((n = next_of(obj)) != NULL) {
        obj = n;
        return TRUE;
    }
    p = parent_of(obj);
    if (p && pieces_of(p)) {
        obj = p;
        piece = 0;
        return TRUE;
    }
    return FALSE;
}

static BOOL to_prev(void)
{
    HWND p;
    if (obj == GetDesktopWindow())
        return FALSE;
    if (piece > 0) {
        piece--;
        return TRUE;
    }
    if (piece == 0) {
        if ((p = last_child(obj)) == NULL)
            return FALSE;
        obj = p;
        piece = -1;
        return TRUE;
    }
    if ((p = prev_of(obj)) != NULL) {
        obj = p;
        return TRUE;
    }
    return FALSE;
}

/* Vue à plat : ordre de lecture en profondeur, dans la fenêtre principale */
static BOOL to_flat_next(void)
{
    HWND top = top_level(obj), keep = obj;
    int keep_piece = piece;
    if (obj == GetDesktopWindow())
        return FALSE;
    if (to_child())
        return TRUE;
    while (piece >= 0 || obj != top) {  /* remonter jusqu'à un suivant */
        if (to_next())
            return TRUE;
        if (!to_parent())
            break;
    }
    obj = keep;
    piece = keep_piece;
    if (piece >= 0)
        pieces_of(obj);
    return FALSE;
}

static BOOL to_flat_prev(void)
{
    HWND top = top_level(obj);
    if (obj == GetDesktopWindow() || (obj == top && piece < 0))
        return FALSE;
    if (!to_prev())
        return to_parent();
    for (;;) {                          /* descendre au dernier descendant */
        HWND c;
        if (piece >= 0)
            break;
        if (pieces_of(obj)) {
            piece = pc_n - 1;
            break;
        }
        if ((c = last_child(obj)) == NULL)
            break;
        obj = c;
    }
    return TRUE;
}

/* Inser+moins : objet du focus, sans parole (la revue recentrée parle) */
void obj_to_focus(void)
{
    obj = GetFocus();
    if (!obj)
        obj = GetActiveWindow();
    piece = -1;
    obj_braille = FALSE;
}

/* Inser+* : objet sous le pointeur, sans parole (la rangée est lue par la revue) */
void obj_at_mouse(void)
{
    POINT pt;
    int i;
    GetCursorPos(&pt);
    obj = WindowFromPoint(pt);
    piece = -1;
    if (obj && !first_child(obj))
        for (i = pieces_of(obj) - 1; i >= 0; i--)
            if (PtInRect(&pc[i].r, pt)) {
                piece = i;
                break;
            }
    obj_recent = TRUE;
}

/* Inser+/ : pointeur au centre de l'objet */
void obj_mouse(void)
{
    RECT r;
    obj_check();
    obj_rect(&r);
    SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    say("souris déplacée", TRUE);
}

void obj_review_used(void)
{
    obj_braille = FALSE;
    obj_recent = FALSE;
}

/* Le braille montre l'objet jusqu'au prochain changement de focus ou de revue */
BOOL obj_br_show(void)
{
    if (!obj_braille)
        return FALSE;
    if (GetFocus() != obj_focus || cfg_on) {
        obj_braille = FALSE;
        return FALSE;
    }
    if (obj_dirty) {
        br_text(obj_text, -1, FALSE);
        br_select(-1, -1);
        obj_dirty = FALSE;
    }
    return TRUE;
}

/* Actions EPM_REVIEW 15 à 24 (crochet clavier, hook.c) */
void obj_command(int action)
{
    RECT r;
    POINT pt;
    char buf[64];
    BOOL moved;

    obj_check();
    switch (action) {
    case 15:                            /* Inser+5 : objet courant */
        obj_say(NULL);
        return;
    case 16:
        moved = to_parent();
        obj_say(moved ? NULL : "pas de parent");
        return;
    case 17:
        moved = to_child();
        obj_say(moved ? NULL : "pas d'enfant");
        return;
    case 18:
        moved = to_prev();
        obj_say(moved ? NULL : "pas d'objet précédent");
        return;
    case 19:
        moved = to_next();
        obj_say(moved ? NULL : "pas d'objet suivant");
        return;
    case 22:
        moved = to_flat_prev();
        obj_say(moved ? NULL : "début");
        return;
    case 23:
        moved = to_flat_next();
        obj_say(moved ? NULL : "fin");
        return;
    case 20:                            /* Inser+Entrée du pavé : clic au centre */
        if (piece < 0 && obj != GetDesktopWindow() && !IsWindowEnabled(obj)) {
            say("indisponible", TRUE);
            return;
        }
        obj_rect(&r);
        pt.x = (r.left + r.right) / 2;
        pt.y = (r.top + r.bottom) / 2;
        say("activer", TRUE);
        click_at(pt, FALSE, TRUE);
        return;
    case 21:                            /* Inser+Maj+moins : focus sur l'objet */
        if (obj == GetDesktopWindow() || !IsWindowEnabled(obj)) {
            say("impossible", TRUE);
            return;
        }
        if (GetWindowLong(obj, GWL_STYLE) & WS_CHILD) {
            SetActiveWindow(top_level(obj));
            SetFocus(obj);
        } else {
            BringWindowToTop(obj);
            SetActiveWindow(obj);
        }
        return;
    case 24:                            /* Inser+Suppr du pavé : position */
        obj_rect(&r);
        wsprintf(buf, "%d, %d, largeur %d, hauteur %d", r.left, r.top,
                 r.right - r.left, r.bottom - r.top);
        say(buf, TRUE);
        return;
    }
}
