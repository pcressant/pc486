/* Pages de Netscape, dans EPAUHOOK.DLL : reconnaissance de la vue qui affiche la
   page et circulation au clavier entre elle et ses champs de formulaire. Utilisé
   par le crochet clavier (hook.c) et par EPAULARD.EXE (lecture, dire tout). */
#include <windows.h>
#include "epaulard.h"

/* Vue principale d'un programme MFC, sans curseur : page de Netscape (texte dessiné
   qui défile ; ses champs de formulaire sont des contrôles fils). */
#define AFX_IDW_PANE_FIRST 0xE900
BOOL FAR PASCAL __export EpPageView(HWND h)
{
    char cls[8];
    POINT pt;
    if (!h || !IsWindow(h) || !(GetWindowLong(h, GWL_STYLE) & WS_CHILD) ||
        (WORD) GetWindowWord(h, GWW_ID) != AFX_IDW_PANE_FIRST)
        return FALSE;
    GetClassName(h, cls, sizeof(cls));
    return lstrlen(cls) >= 3 && (cls[0] | 0x20) == 'a' && (cls[1] | 0x20) == 'f' &&
           (cls[2] | 0x20) == 'x' && !EpOsmCaret(h, &pt);
}

/* Champ de formulaire d'une page : contrôle fils visible et actif */
static BOOL page_field(HWND c)
{
    char cls[16];
    long st = GetWindowLong(c, GWL_STYLE);
    if (!(st & WS_VISIBLE) || (st & WS_DISABLED))
        return FALSE;
    GetClassName(c, cls, sizeof(cls));
    return lstrcmpi(cls, "Edit") == 0 || lstrcmpi(cls, "Button") == 0 ||
           lstrcmpi(cls, "ComboBox") == 0 || lstrcmpi(cls, "ListBox") == 0;
}

/* Rang de lecture d'un champ : de haut en bas, puis de gauche à droite */
static BOOL page_before(HWND a, HWND b)
{
    RECT ra, rb;
    GetWindowRect(a, &ra);
    GetWindowRect(b, &rb);
    if (ra.top != rb.top)
        return ra.top < rb.top;
    return ra.left < rb.left;
}

/* Faire défiler la page jusqu'au champ c : il est dessiné avec son libellé (lu par
   Épaulard, vu par un voyant). Arrêt si la page ne bouge plus. */
static void page_show(HWND view, HWND c)
{
    RECT v, r;
    int i, last = -32000;
    for (i = 0; i < 60; i++) {
        GetClientRect(view, &v);
        ClientToScreen(view, (POINT FAR *) &v.left);
        ClientToScreen(view, (POINT FAR *) &v.right);
        GetWindowRect(c, &r);
        if (r.top == last)
            break;
        last = r.top;
        if (r.top < v.top)
            SendMessage(view, WM_VSCROLL, SB_PAGEUP, 0L);
        else if (r.bottom > v.bottom)
            SendMessage(view, WM_VSCROLL, SB_PAGEDOWN, 0L);
        else
            break;
    }
    UpdateWindow(view);
}

static BOOL page_shown(HWND c)
{
    return (GetWindowLong(c, GWL_STYLE) & WS_VISIBLE) != 0;
}

/* Déplacer la page de quelques écrans dans un sens tant que le champ reste caché ;
   FALSE si elle ne bouge plus */
static BOOL page_seek(HWND view, HWND c, int code, int steps)
{
    int i, pos;
    for (i = 0; i < steps && !page_shown(c); i++) {
        pos = GetScrollPos(view, SB_VERT);
        SendMessage(view, WM_VSCROLL, code, 0L);
        UpdateWindow(view);
        if (GetScrollPos(view, SB_VERT) == pos)
            return page_shown(c);
    }
    return page_shown(c);
}

/* Champ hors de la partie affichée : Netscape le cache (sans WS_VISIBLE) et range sa
   fenêtre à une position sans rapport avec la page. Aller à la position estimée,
   chercher autour, puis depuis le haut ; enfin l'amener entièrement à l'écran. */
BOOL FAR PASCAL __export EpPageReveal(HWND view, HWND c, int permille)
{
    int lo, hi, start;
    if (!IsWindow(view) || !IsWindow(c))
        return FALSE;
    if (!page_shown(c)) {
        GetScrollRange(view, SB_VERT, &lo, &hi);
        if (hi > lo) {
            if (permille < 0) permille = 0;
            if (permille > 1000) permille = 1000;
            start = lo + (int) ((long) (hi - lo) * permille / 1000);
            SendMessage(view, WM_VSCROLL, SB_THUMBPOSITION, MAKELPARAM(start, 0));
            UpdateWindow(view);
        }
        start = GetScrollPos(view, SB_VERT);
        if (!page_seek(view, c, SB_PAGEDOWN, 3)) {
            SendMessage(view, WM_VSCROLL, SB_THUMBPOSITION, MAKELPARAM(start, 0));
            UpdateWindow(view);
            if (!page_seek(view, c, SB_PAGEUP, 3)) {
                SendMessage(view, WM_VSCROLL, SB_TOP, 0L);
                UpdateWindow(view);
                page_seek(view, c, SB_PAGEDOWN, 200);
            }
        }
    }
    if (page_shown(c))
        page_show(view, c);
    return page_shown(c);
}

/* Tab, Maj+Tab dans une page (crochet clavier) : Netscape 3 n'entre pas au clavier
   dans ses champs. Le focus circule de la page à ses champs dans l'ordre de lecture,
   puis revient à la page. FALSE : pas une page, la touche va au programme. */
BOOL FAR PASCAL __export EpPageTab(BOOL back)
{
    HWND f = GetFocus(), view, c, best = NULL;
    if (EpPageView(f))
        view = f;
    else if (f && EpPageView(GetParent(f)))
        view = GetParent(f);
    else
        return FALSE;
    for (c = GetWindow(view, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (c == f || !page_field(c))
            continue;
        if (back ? (f == view || page_before(c, f)) && (!best || page_before(best, c))
                 : (f == view || page_before(f, c)) && (!best || page_before(c, best)))
            best = c;
    }
    if (best)
        page_show(view, best);
    SetFocus(best ? best : view);
    return TRUE;
}
