/* Modèle hors écran d'Épaulard (dans EPAUHOOK.DLL).
 *
 * Les programmes qui dessinent eux-mêmes (icônes du Gestionnaire de programmes, documents
 * de Write...) n'offrent aucun texte à lire. Comme les lecteurs d'écran de l'époque, on
 * intercepte l'affichage du texte : les points d'entrée TextOut et ExtTextOut de GDI sont
 * modifiés en mémoire (saut vers nos fonctions), et chaque texte affiché est noté avec sa
 * fenêtre, sa position à l'écran et ses couleurs. Un texte affiché en couleurs de
 * sélection signale l'élément courant (« surbrillance »), qu'EPAULARD.EXE annonce.
 *
 * Pour appeler la vraie fonction, on remet ses octets d'origine, on l'appelle, puis on
 * repose le saut. Le multitâche coopératif ne supprime pas les callbacks imbriqués :
 * chaque interception compte ses appels actifs et diffère sa libération au retour.
 * Pendant que le point d’entrée est restauré, les appels imbriqués à cette même
 * fonction passent directement par GDI et ne sont pas observés.
 *
 * Win16 n'a pas de « finally » : si la tâche meurt pendant l'appel d'origine (faute
 * dans GDI ou dans un rappel de BeginPaint), notre retour n'a jamais lieu. Chaque
 * appel actif note donc sa tâche ; osm_reap, appelé par les crochets, repose le saut
 * (ou achève le retrait) quand cette tâche n'existe plus.
 */
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include "epaulard.h"


#define OSM_N    320
#define OSM_TXT  160            /* une ligne entière de Word en petite police */

typedef struct {
    HWND hwnd;                  /* fenêtre où le texte est affiché (0 = entrée libre) */
    int x, y, w, h;             /* rectangle à l'écran */
    BYTE hi;                    /* affiché en couleurs de sélection */
    BYTE font;                  /* police dans fonts[] + 1 (0 : inconnue) */
    DWORD seq;                  /* ordre d'affichage (pour remplacer la plus ancienne) */
    char t[OSM_TXT];
} OSMENT;

/* Tableaux alloués à l'installation : trop grands pour le segment de données */
static OSMENT FAR *osm;
static BYTE FAR *wid;           /* largeur de chaque caractère, OSM_TXT par entrée */
static DWORD seq;

/* Texte écrit dans un contexte en mémoire (bitmap), en attendant d'être copié à
   l'écran par BitBlt : Word prépare ainsi chaque ligne de son document hors écran. */
#define MEM_N    96

typedef struct {
    HDC hdc;                    /* contexte en mémoire (0 = entrée libre) */
    int x, y, w, h;             /* rectangle, en unités de périphérique du contexte */
    BYTE hi;
    BYTE font;
    DWORD seq;
    char t[OSM_TXT];
    BYTE cw[OSM_TXT];           /* largeur de chaque caractère */
} MEMENT;

static MEMENT FAR *mem;

/* Polices et couleurs des textes affichés (Inser+F). Une entrée recyclée change la
   description des textes anciens qui la désignaient : assez rare avec FONT_N. */
#define FONT_N 48
typedef struct {
    char face[LF_FACESIZE];
    int pts;                    /* corps en points, d'après la hauteur à l'écran */
    BYTE bold, italic, under, strike;
    COLORREF color;
    DWORD seq;
} FONTENT;
static FONTENT fonts[FONT_N];

/* Zones inversées (DSTINVERT) : sélection de Write et de Word. Deux inversions se
   compensent : on tient le OU exclusif des rectangles, en les découpant. Un
   dessin opaque, un remplissage ou une copie remet la zone à l'endroit. */
#define INV_N 64
static struct { HWND hwnd; RECT r; } inv[INV_N];

typedef struct {
    FARPROC target;             /* point d'entrée dans GDI */
    HGLOBAL fixed;              /* fixation possédée, libérée avec le sélecteur */
    UINT depth;                 /* appels originaux encore actifs */
    HTASK task;                 /* tâche de l'appel actif (0 : aucun) */
    BOOL removing;
    UINT alias;                 /* sélecteur de données sur son segment de code */
    BYTE orig[5], jmp[5];
} PATCH;

static PATCH p_eto, p_to, p_bp, p_gdc, p_gwdc, p_gdcex, p_pat;
static PATCH p_sw, p_sdc, p_blt, p_ddc;
static UINT pending;            /* interceptions dont un appel d'origine est actif */
static HTASK scrolling;         /* ScrollWindow peut appeler ScrollDC (tâche ou 0) */

/* Curseur texte dessiné par le programme lui-même (Write) : mince rectangle inversé */
static HWND fc_hwnd;
static int fc_x, fc_y, fc_h;

/* Rectangles de focus pointillés, tracés par quatre traits fins en mode XOR (boîtes
   de dialogue de Word) : tracer deux fois le même rectangle l'efface. Suivre cette
   parité est fragile (un tracé manqué l'inverse) : on retient le dernier tracé. Quand
   le focus quitte la boîte pour une zone de saisie, le rectangle effacé reste le
   dernier, mais il n'est consulté que si la boîte elle-même a le focus. */
#define FR_N 8
static struct { HWND hwnd; RECT r; DWORD seq; BYTE mem; } frect[FR_N];
static struct { HWND hwnd; int x, y, w; } fr_top;     /* trait horizontal en attente */

/* Rectangle de focus tracé dans un bitmap, reporté à l'écran quand le bitmap y est
   copié : onglets des boîtes de Word (« Police, style et attributs », « Espacement »).
   Coordonnées de périphérique du bitmap. Un remplissage qui le couvre l'efface. */
static struct { HDC hdc; RECT r; BOOL done, top; int x, y, w; } mfr;

/* Petites images copiées d'un bitmap sans texte : case à cocher, flèche de liste
   déroulante (Word). La position dans l'image source donne l'état de la case. */
#define GL_N 32
static struct { HWND hwnd; RECT r; int xs, ys; DWORD seq; } glyph[GL_N];
static void note_glyph(HDC dst, int x, int y, int w, int h, int xs, int ys);

/* Association contexte de dessin -> fenêtre, notée quand USER le donne (BeginPaint,
   GetDC...) : WindowFromDC n'existe pas sous Windows 3.1. Les contextes sont recyclés ;
   la dernière association d'un même contexte remplace l'ancienne. */
#define DCMAP_N 48
static struct { HDC hdc; HWND hwnd; } dcmap[DCMAP_N];
static int dcmap_next;

static void dc_note(HDC hdc, HWND hwnd)
{
    int i;
    if (!hdc)
        return;
    for (i = 0; i < DCMAP_N; i++)
        if (dcmap[i].hdc == hdc) {
            dcmap[i].hwnd = hwnd;
            return;
        }
    dcmap[dcmap_next].hdc = hdc;
    dcmap[dcmap_next].hwnd = hwnd;
    dcmap_next = (dcmap_next + 1) % DCMAP_N;
}

/* Fenêtre du contexte, si l'association est encore valable : USER recycle ses
   contextes en interne sans repasser par nos fonctions ; on vérifie que l'origine du
   contexte est bien celle de la zone client (ou de la fenêtre entière) notée. */
static HWND dc_window(HDC hdc)
{
    int i;
    DWORD org;
    POINT pt;
    RECT r;
    HWND w;

    for (i = 0; i < DCMAP_N; i++)
        if (dcmap[i].hdc == hdc)
            break;
    if (i == DCMAP_N)
        return (HWND) 0;
    w = dcmap[i].hwnd;
    if (!IsWindow(w))
        return (HWND) 0;
    org = GetDCOrg(hdc);
    pt.x = 0;
    pt.y = 0;
    ClientToScreen(w, &pt);
    if (LOWORD(org) == (WORD) pt.x && HIWORD(org) == (WORD) pt.y)
        return w;
    GetWindowRect(w, &r);
    if (LOWORD(org) == (WORD) r.left && HIWORD(org) == (WORD) r.top)
        return w;
    return (HWND) 0;
}
static HTASK busy;              /* évite de noter deux fois (TextOut appelle ExtTextOut) */
extern HWND ep_hwnd_main;       /* fenêtre d'EPAULARD.EXE (hook.c) */
extern BOOL ep_in_menu;         /* un menu est ouvert (hook.c) */

/* ------------------------------------------------------------- patch de GDI */

static void poke(PATCH *p, const BYTE *b)
{
    BYTE FAR *d = (BYTE FAR *) MAKELP(p->alias, OFFSETOF(p->target));
    int i;
    for (i = 0; i < 5; i++)
        d[i] = b[i];
}

static BOOL patch_install(PATCH *p, const char *module, const char *name, FARPROC hook)
{
    BYTE FAR *src;
    int i;

    if (p->alias) {
        /* Retrait différé, appel d'origine encore actif dans cette même instance de
           la DLL : garder sélecteur et octets d'origine, le retour reposera le saut. */
        if (!p->removing)
            return FALSE;
        p->removing = FALSE;
        return TRUE;
    }
    p->target = GetProcAddress(GetModuleHandle(module), name);
    if (!p->target)
        return FALSE;
    /* segment de code de GDI : le fixer en mémoire, et en faire un alias inscriptible */
    p->fixed = (HGLOBAL) LOWORD(GlobalHandle(SELECTOROF(p->target)));
    if (!p->fixed)
        return FALSE;
    GlobalFix(p->fixed);
    p->alias = AllocSelector(SELECTOROF(p->target));
    if (!p->alias) {
        GlobalUnfix(p->fixed);
        p->fixed = 0;
        return FALSE;
    }
    if (!PrestoChangoSelector(SELECTOROF(p->target), p->alias)) {
        FreeSelector(p->alias);
        p->alias = 0;
        GlobalUnfix(p->fixed);
        p->fixed = 0;
        return FALSE;
    }
    p->removing = FALSE;
    src = (BYTE FAR *) p->target;
    for (i = 0; i < 5; i++)
        p->orig[i] = src[i];
    p->jmp[0] = 0xEA;                   /* JMP FAR seg:ofs */
    p->jmp[1] = LOBYTE(OFFSETOF(hook));
    p->jmp[2] = HIBYTE(OFFSETOF(hook));
    p->jmp[3] = LOBYTE(SELECTOROF(hook));
    p->jmp[4] = HIBYTE(SELECTOROF(hook));
    poke(p, p->jmp);
    return TRUE;
}

static void patch_remove(PATCH *p)
{
    if (p->alias) {
        poke(p, p->orig);
        p->removing = TRUE;
        if (!p->depth) {
            FreeSelector(p->alias);
            p->alias = 0;
            GlobalUnfix(p->fixed);
            p->fixed = 0;
        }
    }
}

static void patch_enter(PATCH *p)
{
    if (!p->depth++) {
        p->task = GetCurrentTask();
        pending++;
    }
    if (p->alias)
        poke(p, p->orig);
}

static void patch_leave(PATCH *p)
{
    if (p->depth && !--p->depth) {
        p->task = 0;
        pending--;
    }
    if (p->removing)
        patch_remove(p);
    else if (p->alias && !p->depth)
        poke(p, p->jmp);
}

/* Appel d'origine dont la tâche a disparu : Windows a abandonné sa pile, il ne
   reviendra jamais. Une tâche vivante, même en attente d'une autre (SendMessage
   dans un rappel), garde son appel. */
static void patch_reap(PATCH *p)
{
    if (!p->depth || !p->task || IsTask(p->task))
        return;
    p->depth = 0;
    p->task = 0;
    pending--;
    if (p->removing)
        patch_remove(p);
    else if (p->alias)
        poke(p, p->jmp);
}

/* Appelé par les crochets de hook.c, dans n'importe quelle tâche : presque toujours
   un simple test de compteurs. */
void osm_reap(void)
{
    if (busy && !IsTask(busy))
        busy = 0;
    if (scrolling && !IsTask(scrolling))
        scrolling = 0;
    if (!pending)
        return;
    patch_reap(&p_eto);
    patch_reap(&p_to);
    patch_reap(&p_bp);
    patch_reap(&p_gdc);
    patch_reap(&p_gwdc);
    patch_reap(&p_gdcex);
    patch_reap(&p_pat);
    patch_reap(&p_sw);
    patch_reap(&p_sdc);
    patch_reap(&p_blt);
    patch_reap(&p_ddc);
}

/* Fenêtre détruite (crochet CBT) : oublier ce qu'elle affichait. Windows redonne
   aussitôt son identifiant à une nouvelle fenêtre, qui hériterait sinon de ces textes
   (« Ligne de commande: » de la boîte Exécuter lu dans le glossaire de l'Aide). */
/* (les contrôles d'une boîte détruite ne passent pas tous par le crochet : on oublie
   aussi les fenêtres filles, qui existent encore à cet instant) */
static BOOL gone(HWND h, HWND destroyed)
{
    return h && (h == destroyed || IsChild(destroyed, h));
}

void osm_destroyed(HWND hwnd)
{
    int i;
    if (!osm || !hwnd)
        return;
    for (i = 0; i < OSM_N; i++)
        if (gone(osm[i].hwnd, hwnd))
            osm[i].hwnd = 0;
    for (i = 0; i < INV_N; i++)
        if (gone(inv[i].hwnd, hwnd))
            inv[i].hwnd = 0;
    for (i = 0; i < FR_N; i++)
        if (gone(frect[i].hwnd, hwnd))
            frect[i].hwnd = 0;
    for (i = 0; i < GL_N; i++)
        if (gone(glyph[i].hwnd, hwnd))
            glyph[i].hwnd = 0;
    for (i = 0; i < DCMAP_N; i++)
        if (gone(dcmap[i].hwnd, hwnd))
            dcmap[i].hdc = 0, dcmap[i].hwnd = 0;
}

/* ------------------------------------------------------------ enregistrement */

static void forget_rect(HWND hwnd, int x, int y, int w, int h)
{
    int i;
    for (i = 0; i < OSM_N; i++) {
        OSMENT *e = &osm[i];
        if (e->hwnd == hwnd && e->x < x + w && x < e->x + e->w &&
            e->y < y + h && y < e->y + e->h)
            e->hwnd = 0;
    }
}

/* Fond repeint (ExtTextOut opaque sans texte) : n'efface que les textes dont il
   couvre le centre. Word trace ainsi les traits fins (soulignement des raccourcis,
   bords des boutons) par-dessus des libellés qui restent lisibles. */
static void forget_center(HWND hwnd, const RECT *r)
{
    int i;
    for (i = 0; i < OSM_N; i++) {
        OSMENT *e = &osm[i];
        int cx = e->x + e->w / 2, cy = e->y + e->h / 2;
        if (e->hwnd == hwnd && cx >= r->left && cx < r->right &&
            cy >= r->top && cy < r->bottom)
            e->hwnd = 0;
    }
}

/* Ce qu'une fenêtre affiche recouvre aussi ce que ses fenêtres parentes avaient écrit
   au même endroit (Word dessine ses boîtes de dialogue dans sa fenêtre principale :
   leur texte doit disparaître quand le document est redessiné dessous). */
static void forget_covered(HWND hwnd, int x, int y, int w, int h)
{
    for (; hwnd; hwnd = GetParent(hwnd)) {
        forget_rect(hwnd, x, y, w, h);
        if (!(GetWindowLong(hwnd, GWL_STYLE) & WS_CHILD))
            break;
    }
}

/* ------------------------------------------------------- zones inversées */

/* r moins s : jusqu'à quatre morceaux dans out */
static int rect_minus(const RECT *r, const RECT *s, RECT *out)
{
    RECT i;
    int n = 0;
    if (!IntersectRect(&i, r, s)) {
        out[0] = *r;
        return 1;
    }
    if (r->top < i.top) SetRect(&out[n++], r->left, r->top, r->right, i.top);
    if (i.bottom < r->bottom) SetRect(&out[n++], r->left, i.bottom, r->right, r->bottom);
    if (r->left < i.left) SetRect(&out[n++], r->left, i.top, i.left, i.bottom);
    if (i.right < r->right) SetRect(&out[n++], i.right, i.top, r->right, i.bottom);
    return n;
}

static void inv_reset(HWND hwnd)
{
    int i;
    for (i = 0; i < INV_N; i++)
        if (inv[i].hwnd == hwnd)
            inv[i].hwnd = 0;
}

static BOOL inv_add(HWND hwnd, const RECT *r)
{
    int i;
    if (IsRectEmpty(r))
        return TRUE;
    for (i = 0; i < INV_N; i++)
        if (!inv[i].hwnd || !IsWindow(inv[i].hwnd)) {
            inv[i].hwnd = hwnd;
            inv[i].r = *r;
            return TRUE;
        }
    return FALSE;
}

/* Nouvelle inversion : OU exclusif avec celles déjà notées */
static void inv_toggle(HWND hwnd, const RECT *r)
{
    static RECT work[16];
    RECT piece[4], p, e, x;
    int nw = 1, i, k, n;
    BOOL split;

    work[0] = *r;
    while (nw > 0) {
        p = work[--nw];
        split = FALSE;
        for (i = 0; i < INV_N && !split; i++) {
            if (inv[i].hwnd != hwnd || !IntersectRect(&x, &inv[i].r, &p))
                continue;
            e = inv[i].r;
            inv[i].hwnd = 0;
            n = rect_minus(&e, &p, piece);
            for (k = 0; k < n; k++)
                if (!inv_add(hwnd, &piece[k]))
                    goto full;
            n = rect_minus(&p, &e, piece);
            for (k = 0; k < n; k++) {
                if (nw >= 16)
                    goto full;
                work[nw++] = piece[k];
            }
            split = TRUE;
        }
        if (!split && !inv_add(hwnd, &p))
            goto full;
    }
    return;
full:
    inv_reset(hwnd);            /* trop morcelé : ne plus rien croire de cette fenêtre */
}

/* Zone redessinée à l'endroit */
static void inv_clear(HWND hwnd, const RECT *area)
{
    RECT piece[4], e, x;
    int i, k, n;
    for (i = 0; i < INV_N; i++) {
        if (inv[i].hwnd != hwnd || !IntersectRect(&x, &inv[i].r, area))
            continue;
        e = inv[i].r;
        inv[i].hwnd = 0;
        n = rect_minus(&e, area, piece);
        for (k = 0; k < n; k++)
            if (!inv_add(hwnd, &piece[k])) {
                inv_reset(hwnd);
                return;
            }
    }
}

static void inv_scroll(HWND hwnd, int dx, int dy, const RECT *area)
{
    static RECT moved[INV_N];
    RECT piece[4], e, x;
    int i, k, n, nm = 0;
    for (i = 0; i < INV_N; i++) {
        if (inv[i].hwnd != hwnd || !IntersectRect(&x, &inv[i].r, area))
            continue;
        e = inv[i].r;
        inv[i].hwnd = 0;
        n = rect_minus(&e, area, piece);
        for (k = 0; k < n; k++)
            if (!inv_add(hwnd, &piece[k])) {
                inv_reset(hwnd);
                return;
            }
        OffsetRect(&x, dx, dy);
        if (IntersectRect(&x, &x, area))
            moved[nm++] = x;
    }
    for (i = 0; i < nm; i++)
        if (!inv_add(hwnd, &moved[i])) {
            inv_reset(hwnd);
            return;
        }
}

/* Point (écran) dans un nombre impair de zones inversées de la fenêtre */
static BOOL inverted(HWND hwnd, int x, int y)
{
    POINT pt;
    int i;
    BOOL odd = FALSE;
    pt.x = x;
    pt.y = y;
    for (i = 0; i < INV_N; i++)
        if (inv[i].hwnd == hwnd && PtInRect(&inv[i].r, pt))
            odd = !odd;
    return odd;
}

/* Le défilement déplace des pixels sans repasser par TextOut : déplacer aussi
   les textes conservés. Oublier les fragments coupés ou sortis de la zone ;
   les nouvelles lignes seront enregistrées lors du prochain dessin. */
static void scroll_text(HWND hwnd, int dx, int dy, const RECT *area)
{
    int i;
    for (i = 0; i < OSM_N; i++) {
        OSMENT *e = &osm[i];
        if (e->hwnd != hwnd || e->x >= area->right || e->x + e->w <= area->left ||
            e->y >= area->bottom || e->y + e->h <= area->top)
            continue;
        if (e->x < area->left || e->x + e->w > area->right ||
            e->y < area->top || e->y + e->h > area->bottom) {
            e->hwnd = 0;
            continue;
        }
        e->x += dx;
        e->y += dy;
        if (e->x < area->left || e->x + e->w > area->right ||
            e->y < area->top || e->y + e->h > area->bottom)
            e->hwnd = 0;
    }
    inv_scroll(hwnd, dx, dy, area);
}

/* Texte en couleurs de sélection : fond opaque de la couleur de sélection, ou texte de
   la couleur « texte sélectionné » sur un fond peint à part (Gestionnaire de
   programmes) — mais pas dans la barre de titre, écrite de la même couleur. */
static BOOL is_highlight(HDC hdc, HWND hwnd, int sx, int sy, LPCSTR s, int n)
{
    COLORREF hl = GetSysColor(COLOR_HIGHLIGHT), hlt = GetSysColor(COLOR_HIGHLIGHTTEXT);
    COLORREF tc = GetTextColor(hdc);
    RECT rc;
    POINT pt;
    HWND w;
    char title[OSM_TXT];

    pt.x = sx;
    pt.y = sy;
    if (!(GetBkMode(hdc) == OPAQUE && GetBkColor(hdc) == hl)) {
        if (tc != hlt || tc == GetSysColor(COLOR_WINDOWTEXT))
            return FALSE;
        GetClientRect(hwnd, &rc);
        ScreenToClient(hwnd, &pt);
        if (!PtInRect(&rc, pt))
            return FALSE;
        pt.x = sx;
        pt.y = sy;
    }
    /* barre de titre d'une fenêtre enfant (groupe du Gestionnaire de programmes) :
       même couleurs, mais c'est le titre de la fenêtre qui se trouve là */
    w = WindowFromPoint(pt);
    if (w && n < OSM_TXT && (GetWindowLong(w, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) {
        /* (pas un sous-titre sans barre : « --Contenu-- » du Gestionnaire de liaisons) */
        GetWindowText(w, title, sizeof(title));
        if (lstrlen(title) == n && _fstrncmp(title, s, n) == 0)
            return FALSE;
    }
    return TRUE;
}

/* Texte affiché à l'écran dans hwnd : remplace ce qui était dessous */
static void store(HWND hwnd, int x, int y, int w, int h, BOOL hi, LPCSTR s, int n,
                  const BYTE FAR *cw, BYTE font)
{
    int i, slot;
    OSMENT *e;

    forget_covered(hwnd, x, y, w, h);
    slot = 0;
    for (i = 0; i < OSM_N; i++) {       /* place libre, sinon la plus ancienne */
        if (!osm[i].hwnd || !IsWindow(osm[i].hwnd)) {
            slot = i;
            break;
        }
        if (osm[i].seq < osm[slot].seq)
            slot = i;
    }
    e = &osm[slot];
    e->hwnd = hwnd;
    e->x = x;
    e->y = y;
    e->w = w;
    e->h = h;
    e->hi = (BYTE) hi;
    e->font = font;
    e->seq = ++seq;
    if (n >= OSM_TXT)
        n = OSM_TXT - 1;
    for (i = 0; i < n; i++) {
        e->t[i] = s[i] ? s[i] : ' ';
        wid[slot * OSM_TXT + i] = cw[i];
    }
    e->t[n] = 0;
    if (e->hi && ep_hwnd_main)
        PostMessage(ep_hwnd_main, EPM_HILITE, slot, MAKELPARAM(hwnd, 0));
}

/* ------------------------------------------------- contextes en mémoire */

static void mem_forget(HDC hdc, int x, int y, int w, int h)
{
    int i;
    for (i = 0; i < MEM_N; i++) {
        MEMENT *m = &mem[i];
        if (m->hdc == hdc && m->x < x + w && x < m->x + m->w &&
            m->y < y + h && y < m->y + m->h)
            m->hdc = 0;
    }
}

static void mem_store(HDC hdc, int x, int y, int w, int h, BOOL hi, LPCSTR s, int n,
                      const BYTE FAR *cw, BYTE font)
{
    int i, slot = 0;
    MEMENT *m;

    mem_forget(hdc, x, y, w, h);
    for (i = 0; i < MEM_N; i++) {
        if (!mem[i].hdc) {
            slot = i;
            break;
        }
        if (mem[i].seq < mem[slot].seq)
            slot = i;
    }
    m = &mem[slot];
    m->hdc = hdc;
    m->x = x;
    m->y = y;
    m->w = w;
    m->h = h;
    m->hi = (BYTE) hi;
    m->font = font;
    m->seq = ++seq;
    if (n >= OSM_TXT)
        n = OSM_TXT - 1;
    for (i = 0; i < n; i++) {
        m->t[i] = s[i] ? s[i] : ' ';
        m->cw[i] = cw[i];
    }
    m->t[n] = 0;
}

static void mem_forget_center(HDC hdc, const RECT *r)
{
    int i;
    for (i = 0; i < MEM_N; i++) {
        MEMENT *m = &mem[i];
        int cx = m->x + m->w / 2, cy = m->y + m->h / 2;
        if (m->hdc == hdc && cx >= r->left && cx < r->right &&
            cy >= r->top && cy < r->bottom)
            m->hdc = 0;
    }
}

static BOOL mem_has(HDC hdc)
{
    int i;
    for (i = 0; i < MEM_N; i++)
        if (mem[i].hdc == hdc)
            return TRUE;
    return FALSE;
}

/* Rectangle logique -> unités de périphérique (sans l'origine du contexte) */
static void rect_dp(HDC hdc, int l, int t, int r, int b, RECT *out)
{
    POINT p[2];
    p[0].x = l; p[0].y = t;
    p[1].x = r; p[1].y = b;
    LPtoDP(hdc, p, 2);
    SetRect(out, p[0].x, p[0].y, p[1].x, p[1].y);
}

/* Contexte sans fenêtre connue et d'origine 0,0 : bitmap en mémoire. (Un contexte
   d'écran ainsi placé, sans fenêtre notée, était déjà ignoré.) */
static BOOL is_memdc(HWND hwnd, DWORD org)
{
    return !hwnd && org == 0;
}

/* Copie d'un bitmap en mémoire dans un autre : en mode Page, Word prépare l'en-tête
   et le pied de page dans un bitmap, recopié dans celui de la page avant l'écran.
   Le texte suit la copie (décalé si le bitmap est copié sur lui-même). */
static void mem_copy(HDC dst, const RECT *d, HDC src, const RECT *sr)
{
    int i, dx = d->left - sr->left, dy = d->top - sr->top;
    DWORD start = seq;

    if (dst != src)
        mem_forget(dst, d->left, d->top, d->right - d->left, d->bottom - d->top);
    for (i = 0; i < MEM_N; i++) {
        MEMENT *m = &mem[i];
        int cy = m->y + m->h / 2;
        if (m->hdc != src || m->seq > start || cy < sr->top || cy >= sr->bottom ||
            m->x >= sr->right || m->x + m->w <= sr->left)
            continue;
        if (dst == src) {
            m->x += dx;
            m->y += dy;
        } else
            mem_store(dst, m->x + dx, m->y + dy, m->w, m->h, m->hi, m->t,
                      lstrlen(m->t), m->cw, m->font);
    }
}

/* Copie d'un bitmap en mémoire vers l'écran : le texte qu'on y avait noté apparaît */
static void blit_text(HDC dst, int x, int y, int w, int h, HDC src, int xs, int ys)
{
    DWORD org = GetDCOrg(dst);
    HWND hwnd = dc_window(dst);
    RECT d, sr;
    int i;

    rect_dp(dst, x, y, x + w, y + h, &d);
    rect_dp(src, xs, ys, xs + w, ys + h, &sr);
    if (is_memdc(hwnd, org)) {
        mem_copy(dst, &d, src, &sr);    /* d'un bitmap à l'autre (Word, en-tête) */
        return;
    }
    OffsetRect(&d, (int) LOWORD(org), (int) HIWORD(org));
    if (!hwnd) {
        POINT c;
        if (ep_in_menu)
            return;
        c.x = (d.left + d.right) / 2;
        c.y = (d.top + d.bottom) / 2;
        hwnd = WindowFromPoint(c);
    }
    if (!hwnd)
        return;
    forget_covered(hwnd, d.left, d.top, d.right - d.left, d.bottom - d.top);
    inv_clear(hwnd, &d);
    for (i = 0; i < MEM_N; i++) {
        MEMENT *m = &mem[i];
        int cy = m->y + m->h / 2;
        if (m->hdc != src || cy < sr.top || cy >= sr.bottom ||
            m->x >= sr.right || m->x + m->w <= sr.left)
            continue;
        store(hwnd, d.left + m->x - sr.left, d.top + m->y - sr.top, m->w, m->h,
              m->hi, m->t, lstrlen(m->t), m->cw, m->font);
    }
}

/* Largeur à l'écran de chaque caractère : espacement imposé par le programme (Word),
   sinon celle de la police ; ramenée à la largeur totale w du texte affiché. */
static void char_widths(HDC hdc, LPCSTR s, int n, const int FAR *dx, int w, BYTE *cw)
{
    static int cwbuf[256];
    UINT lo = 255, hi = 0;
    long total = 0;
    int i, v;

    if (n > OSM_TXT - 1)
        n = OSM_TXT - 1;
    if (!dx) {
        for (i = 0; i < n; i++) {
            if ((BYTE) s[i] < lo) lo = (BYTE) s[i];
            if ((BYTE) s[i] > hi) hi = (BYTE) s[i];
        }
        if (!GetCharWidth(hdc, lo, hi, cwbuf))
            memset(cwbuf, 0, sizeof(cwbuf));
    }
    for (i = 0; i < n; i++)
        total += dx ? dx[i] : cwbuf[(BYTE) s[i] - lo];
    for (i = 0; i < n; i++) {
        v = dx ? dx[i] : cwbuf[(BYTE) s[i] - lo];
        if (total > 0)
            v = (int) ((long) v * w / total);
        else
            v = w / n;
        cw[i] = (BYTE) (v < 0 ? 0 : v > 255 ? 255 : v);
    }
}

static BOOL same_font(const FONTENT *a, const FONTENT *b)
{
    return a->pts == b->pts && a->bold == b->bold && a->italic == b->italic &&
           a->under == b->under && a->strike == b->strike && a->color == b->color &&
           lstrcmp(a->face, b->face) == 0;
}

/* Police et couleur courantes du contexte : numéro dans fonts[] + 1 */
static BYTE font_note(HDC hdc)
{
    TEXTMETRIC tm;
    FONTENT f;
    RECT r;
    int i, slot = 0, h, dpi;

    memset(&f, 0, sizeof(f));
    GetTextFace(hdc, sizeof(f.face), f.face);
    if (!GetTextMetrics(hdc, &tm))
        return 0;
    h = tm.tmHeight - tm.tmInternalLeading;
    rect_dp(hdc, 0, 0, 0, h, &r);
    h = abs(r.bottom - r.top);
    dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    f.pts = dpi > 0 ? (int) (((long) h * 72 + dpi / 2) / dpi) : h;
    f.bold = (BYTE) (tm.tmWeight >= FW_SEMIBOLD);
    f.italic = (BYTE) (tm.tmItalic != 0);
    f.under = (BYTE) (tm.tmUnderlined != 0);
    f.strike = (BYTE) (tm.tmStruckOut != 0);
    f.color = GetTextColor(hdc);
    for (i = 0; i < FONT_N; i++) {
        if (fonts[i].seq && same_font(&fonts[i], &f)) {
            fonts[i].seq = ++seq;
            return (BYTE) (i + 1);
        }
        if (fonts[i].seq < fonts[slot].seq)
            slot = i;
    }
    fonts[slot] = f;
    fonts[slot].seq = ++seq;
    return (BYTE) (slot + 1);
}

static void record(HDC hdc, int x, int y, UINT opts, const RECT FAR *rc,
                   LPCSTR s, int n, const int FAR *dx, DWORD cp)
{
    static BYTE cw[OSM_TXT];
    HWND hwnd;
    DWORD org, ext;
    POINT pt;
    UINT align;
    int i, w, h;
    BOOL memdc;
    BYTE font;

    /* Fenêtre du contexte de dessin ; sinon (contexte obtenu par USER pour lui-même),
       celle qui se trouve à l'écran sous le texte. Un contexte en mémoire (bitmap) a
       son origine en 0,0 : son texte est mis de côté jusqu'à sa copie à l'écran. */
    org = GetDCOrg(hdc);
    hwnd = dc_window(hdc);
    memdc = is_memdc(hwnd, org);

    if (rc && (opts & ETO_OPAQUE)) {    /* fond repeint : ce qui était dessous disparaît */
        RECT r;
        rect_dp(hdc, rc->left, rc->top, rc->right, rc->bottom, &r);
        if (memdc)
            mem_forget_center(hdc, &r);
        else {
            POINT c;
            OffsetRect(&r, (int) LOWORD(org), (int) HIWORD(org));
            c.x = r.left;
            c.y = r.top;
            if (!hwnd)
                hwnd = WindowFromPoint(c);
            if (hwnd) {
                forget_center(hwnd, &r);
                inv_clear(hwnd, &r);
            }
        }
    }
    if (n <= 0 || !s)
        return;

    align = GetTextAlign(hdc);
    if (align & TA_UPDATECP) {
        x = LOWORD(cp);
        y = HIWORD(cp);
    }
    ext = GetTextExtent(hdc, s, n);
    w = LOWORD(ext);
    h = HIWORD(ext);
    if (dx) {                           /* espacement imposé par le programme */
        long sum = 0;
        for (i = 0; i < n; i++)
            sum += dx[i];
        if (sum > 0 && sum < 32000)
            w = (int) sum;
    }
    {   /* largeur et hauteur en unités de périphérique */
        RECT r;
        rect_dp(hdc, 0, 0, w, h, &r);
        w = r.right - r.left;
        h = r.bottom - r.top;
        if (h < 0) h = -h;
    }
    pt.x = x;
    pt.y = y;
    LPtoDP(hdc, &pt, 1);
    if ((align & (TA_CENTER | TA_RIGHT)) == TA_CENTER)
        pt.x -= w / 2;
    else if (align & TA_RIGHT)
        pt.x -= w;
    if (align & TA_BOTTOM)
        pt.y -= h;
    else if (align & TA_BASELINE) {
        TEXTMETRIC tm;
        GetTextMetrics(hdc, &tm);
        pt.y -= tm.tmAscent;
    }
    if (w <= 0)
        w = 1;
    char_widths(hdc, s, n, dx, w, cw);
    font = font_note(hdc);
    if (memdc) {
        mem_store(hdc, pt.x, pt.y, w, h, GetBkMode(hdc) == OPAQUE &&
                  GetBkColor(hdc) == GetSysColor(COLOR_HIGHLIGHT), s, n, cw, font);
        return;
    }
    pt.x += LOWORD(org);
    pt.y += HIWORD(org);
    if (!hwnd) {
        POINT c;
        /* menus déroulants : WindowFromPoint ne les voit pas et donnerait la fenêtre
           en dessous ; leur texte est lu par ailleurs (WM_MENUSELECT) */
        if (ep_in_menu)
            return;
        c.x = pt.x + (w > 2 ? 1 : 0);
        c.y = pt.y + h / 2;
        hwnd = WindowFromPoint(c);
    }
    if (!hwnd)
        return;
    if (GetBkMode(hdc) == OPAQUE) {     /* fond du texte repeint : plus d'inversion */
        RECT r;
        SetRect(&r, pt.x, pt.y, pt.x + w, pt.y + h);
        inv_clear(hwnd, &r);
    }
    store(hwnd, pt.x, pt.y, w, h,
          is_highlight(hdc, hwnd, pt.x + w / 2, pt.y + h / 2, s, n), s, n, cw, font);
}

/* ----------------------------------------------------- fonctions de remplacement */

BOOL FAR PASCAL __export __loadds EpExtTextOut(HDC hdc, int x, int y, UINT opts,
                                               const RECT FAR *rc, LPCSTR s, UINT n,
                                               int FAR *dx)
{
    BOOL ret, rec = !busy;
    DWORD cp = 0;

    if (rec) {
        busy = GetCurrentTask();
        if (GetTextAlign(hdc) & TA_UPDATECP)
            cp = GetCurrentPosition(hdc);
    }
    patch_enter(&p_eto);
    ret = ExtTextOut(hdc, x, y, opts, rc, s, n, dx);
    patch_leave(&p_eto);
    if (rec) {
        record(hdc, x, y, opts, rc, s, n, dx, cp);
        busy = 0;
    }
    return ret;
}

BOOL FAR PASCAL __export __loadds EpTextOut(HDC hdc, int x, int y, LPCSTR s, int n)
{
    BOOL ret, rec = !busy;
    DWORD cp = 0;

    if (rec) {
        busy = GetCurrentTask();
        if (GetTextAlign(hdc) & TA_UPDATECP)
            cp = GetCurrentPosition(hdc);
    }
    patch_enter(&p_to);
    ret = TextOut(hdc, x, y, s, n);
    patch_leave(&p_to);
    if (rec) {
        record(hdc, x, y, 0, NULL, s, n, NULL, cp);
        busy = 0;
    }
    return ret;
}

/* Mince rectangle inversé à l'écran : curseur texte dessiné par le programme
   (Write par PatBlt, Word par BitBlt). Le clignotement le redessine au même endroit. */
static void note_caret(HDC hdc, int x, int y, int w, int h)
{
    DWORD org;
    POINT pt;
    HWND hwnd;

    if (w <= 0 || w > 4 || h < 6)
        return;
    org = GetDCOrg(hdc);
    hwnd = dc_window(hdc);
    if (is_memdc(hwnd, org))
        return;
    pt.x = x;
    pt.y = y;
    LPtoDP(hdc, &pt, 1);
    pt.x += LOWORD(org);
    pt.y += HIWORD(org);
    if (!hwnd)
        hwnd = WindowFromPoint(pt);
    if (hwnd) {
        fc_hwnd = hwnd;
        fc_x = pt.x;
        fc_y = pt.y;
        fc_h = h;
    }
}

/* Rectangle inversé plus large qu'un curseur : sélection */
static void note_invert(HDC hdc, int x, int y, int w, int h)
{
    DWORD org;
    HWND hwnd;
    RECT r;

    if (h < 3)
        return;
    org = GetDCOrg(hdc);
    hwnd = dc_window(hdc);
    if (is_memdc(hwnd, org))
        return;
    rect_dp(hdc, x, y, x + w, y + h, &r);
    OffsetRect(&r, (int) LOWORD(org), (int) HIWORD(org));
    if (r.left > r.right) { int t = r.left; r.left = r.right; r.right = t; }
    if (r.top > r.bottom) { int t = r.top; r.top = r.bottom; r.bottom = t; }
    if (!hwnd) {
        POINT c;
        c.x = (r.left + r.right) / 2;
        c.y = (r.top + r.bottom) / 2;
        hwnd = WindowFromPoint(c);
    }
    if (hwnd)
        inv_toggle(hwnd, &r);
}

static void focus_rect_note(HWND hwnd, const RECT *r, BYTE from_mem)
{
    int i, slot = 0;
    for (i = 0; i < FR_N; i++)
        if (frect[i].hwnd == hwnd && abs(frect[i].r.left - r->left) <= 1 &&
            abs(frect[i].r.top - r->top) <= 1 && abs(frect[i].r.right - r->right) <= 1 &&
            abs(frect[i].r.bottom - r->bottom) <= 1) {
            frect[i].seq = ++seq;       /* retracé (effacé ou redessiné) */
            frect[i].mem = from_mem;
            if (ep_hwnd_main)
                PostMessage(ep_hwnd_main, EPM_FOCUSRECT, (WPARAM) hwnd, 0L);
            return;
        }
    for (i = 0; i < FR_N; i++) {
        if (!frect[i].hwnd || !IsWindow(frect[i].hwnd)) {
            slot = i;
            break;
        }
        if (frect[i].seq < frect[slot].seq)
            slot = i;
    }
    frect[slot].hwnd = hwnd;
    frect[slot].r = *r;
    frect[slot].seq = ++seq;
    frect[slot].mem = from_mem;
    if (ep_hwnd_main)
        PostMessage(ep_hwnd_main, EPM_FOCUSRECT, (WPARAM) hwnd, 0L);
}

/* Trait fin inversé : deux traits horizontaux alignés forment le haut et le bas
   d'un rectangle de focus (les traits verticaux suivent, inutiles ici). */
static void focus_line(HDC hdc, int x, int y, int w, int h)
{
    DWORD org;
    HWND hwnd;
    RECT r;

    if (h != 1 || w < 4)
        return;
    org = GetDCOrg(hdc);
    hwnd = dc_window(hdc);
    rect_dp(hdc, x, y, x + w, y + h, &r);
    if (is_memdc(hwnd, org)) {
        if (mfr.top && mfr.hdc == hdc && abs(mfr.x - r.left) <= 1 && r.top > mfr.y &&
            abs(mfr.w - (r.right - r.left)) <= 2) {
            RECT f;
            SetRect(&f, mfr.x, mfr.y, mfr.x + max(mfr.w, r.right - r.left), r.bottom);
            /* le même rectangle retracé sur le bitmap l'efface (XOR) */
            mfr.done = !(mfr.done && EqualRect(&f, &mfr.r));
            mfr.r = f;
            mfr.top = FALSE;
            return;
        }
        if (mfr.hdc != hdc)
            mfr.done = FALSE;
        mfr.hdc = hdc;
        mfr.top = TRUE;
        mfr.x = r.left;
        mfr.y = r.top;
        mfr.w = r.right - r.left;
        return;
    }
    OffsetRect(&r, (int) LOWORD(org), (int) HIWORD(org));
    if (!hwnd) {
        POINT c;
        c.x = r.left;
        c.y = r.top;
        hwnd = WindowFromPoint(c);
    }
    if (!hwnd)
        return;
    if (fr_top.hwnd == hwnd && abs(fr_top.x - r.left) <= 1 && r.top > fr_top.y &&
        abs(fr_top.w - (r.right - r.left)) <= 2) {
        RECT f;
        SetRect(&f, fr_top.x, fr_top.y, fr_top.x + max(fr_top.w, r.right - r.left), r.bottom);
        fr_top.hwnd = 0;
        focus_rect_note(hwnd, &f, 0);
        return;
    }
    fr_top.hwnd = hwnd;
    fr_top.x = r.left;
    fr_top.y = r.top;
    fr_top.w = r.right - r.left;
}

BOOL FAR PASCAL __export __loadds EpPatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop)
{
    BOOL ret;
    patch_enter(&p_pat);
    ret = PatBlt(hdc, x, y, w, h, rop);
    patch_leave(&p_pat);
    /* fond repeint : le texte dont il couvre le centre disparaît */
    if (rop == PATCOPY || rop == WHITENESS || rop == BLACKNESS) {
        DWORD org = GetDCOrg(hdc);
        HWND hwnd = dc_window(hdc);
        RECT r;
        rect_dp(hdc, x, y, x + w, y + h, &r);
        if (is_memdc(hwnd, org)) {
            if (mem_has(hdc))
                mem_forget_center(hdc, &r);
            if (mfr.done && mfr.hdc == hdc && r.left <= mfr.r.left && r.top <= mfr.r.top &&
                r.right >= mfr.r.right && r.bottom >= mfr.r.bottom)
                mfr.done = FALSE;
        } else {
            OffsetRect(&r, (int) LOWORD(org), (int) HIWORD(org));
            if (!hwnd) {
                POINT c;
                c.x = r.left;
                c.y = r.top;
                hwnd = WindowFromPoint(c);
            }
            if (hwnd) {
                forget_center(hwnd, &r);
                inv_clear(hwnd, &r);
            }
        }
    }
    if (rop == DSTINVERT && abs(w) > 2)  /* curseur : un ou deux points de large */
        note_invert(hdc, x, y, w, h);
    else if (rop == DSTINVERT)
        note_caret(hdc, x, y, w, h);
    else if (rop == 0x00A50065L || rop == PATINVERT)    /* DPxn : pointillés de Word */
        focus_line(hdc, x, y, w, h);
    return ret;
}

/* Copie d'un bitmap où un rectangle de focus a été tracé : le reporter à l'écran */
static void mfr_blit(HDC dst, int x, int y, int w, int h, HDC src, int xs, int ys)
{
    DWORD org = GetDCOrg(dst);
    HWND hwnd = dc_window(dst);
    RECT d, sr, f;
    int cx = (mfr.r.left + mfr.r.right) / 2, cy = (mfr.r.top + mfr.r.bottom) / 2;

    rect_dp(dst, x, y, x + w, y + h, &d);
    rect_dp(src, xs, ys, xs + w, ys + h, &sr);
    if (cx < sr.left || cx >= sr.right || cy < sr.top || cy >= sr.bottom)
        return;
    f = mfr.r;
    OffsetRect(&f, d.left - sr.left, d.top - sr.top);
    if (is_memdc(hwnd, org)) {          /* d'un bitmap à l'autre */
        mfr.hdc = dst;
        mfr.r = f;
        return;
    }
    OffsetRect(&f, (int) LOWORD(org), (int) HIWORD(org));
    if (!hwnd) {
        POINT c;
        c.x = (f.left + f.right) / 2;
        c.y = (f.top + f.bottom) / 2;
        hwnd = WindowFromPoint(c);
    }
    if (hwnd)
        focus_rect_note(hwnd, &f, 1);
}

/* Dernier rectangle de focus tracé dans hwnd (écran) : 1, ou 2 s'il vient d'un
   bitmap (onglet d'une boîte de Word) ; 0 si aucun */
BOOL FAR PASCAL __export EpOsmFocusRect(HWND hwnd, RECT FAR *r)
{
    int i, best = -1;
    for (i = 0; i < FR_N; i++)
        if (frect[i].hwnd == hwnd && (best < 0 || frect[i].seq > frect[best].seq))
            best = i;
    if (best < 0)
        return FALSE;
    *r = frect[best].r;
    return frect[best].mem ? 2 : 1;
}

/* Premier texte en gras (le plus haut, puis le plus à gauche) affiché dans hwnd et
   dont le centre est dans le rectangle écran (l, t, r, b) : onglet actif d'une boîte
   de Word, au-dessus des libellés de la page (gras eux aussi) */
int FAR PASCAL __export EpOsmBoldIn(HWND hwnd, int l, int t, int r, int b, LPSTR out, int max)
{
    int i, best = -1;

    out[0] = 0;
    if (!osm)
        return 0;
    for (i = 0; i < OSM_N; i++) {
        OSMENT *e = &osm[i];
        int cx = e->x + e->w / 2, cy = e->y + e->h / 2;
        if (e->hwnd != hwnd || !e->t[0] || !e->font || !fonts[e->font - 1].bold ||
            cx < l || cx >= r || cy < t || cy >= b)
            continue;
        if (best < 0 || cy < osm[best].y + osm[best].h / 2 - 2 ||
            (cy <= osm[best].y + osm[best].h / 2 + 2 && e->x < osm[best].x))
            best = i;
    }
    if (best < 0)
        return 0;
    lstrcpyn(out, osm[best].t, max);
    return lstrlen(out);
}

BOOL FAR PASCAL __export __loadds EpBitBlt(HDC dst, int x, int y, int w, int h,
                                          HDC src, int xs, int ys, DWORD rop)
{
    BOOL ret;
    patch_enter(&p_blt);
    ret = BitBlt(dst, x, y, w, h, src, xs, ys, rop);
    patch_leave(&p_blt);
    if (src && ret && mfr.done && mfr.hdc == src)
        mfr_blit(dst, x, y, w, h, src, xs, ys);
    if (src && rop != SRCCOPY && abs(w) <= 4)
        /* Curseur de Word (2 px, rop 0x690145). En mode Page (en-tête, pied de
           page), son bitmap source a aussi reçu du texte : ce n'est pas une copie de
           texte, qui effacerait du modèle la ligne sous le curseur. */
        note_caret(dst, x, y, w, h);
    else if (src && ret && mem_has(src))
        blit_text(dst, x, y, w, h, src, xs, ys);
    else if (src && rop == SRCCOPY)
        note_glyph(dst, x, y, w, h, xs, ys);
    else if (rop == DSTINVERT && abs(w) > 2)
        note_invert(dst, x, y, w, h);
    else
        note_caret(dst, x, y, w, h);    /* Word : curseur inversé par BitBlt */
    return ret;
}

static void note_glyph(HDC dst, int x, int y, int w, int h, int xs, int ys)
{
    DWORD org;
    HWND hwnd;
    RECT r;
    int i, slot = -1;

    if (w < 6 || w > 16 || h < 6 || h > 16)
        return;
    org = GetDCOrg(dst);
    hwnd = dc_window(dst);
    if (!hwnd || is_memdc(hwnd, org))
        return;
    rect_dp(dst, x, y, x + w, y + h, &r);
    OffsetRect(&r, (int) LOWORD(org), (int) HIWORD(org));
    for (i = 0; i < GL_N; i++)
        if (glyph[i].hwnd == hwnd && EqualRect(&glyph[i].r, &r)) {
            slot = i;
            break;
        }
    if (slot >= 0) {
        if (glyph[slot].xs != xs || glyph[slot].ys != ys) {
            glyph[slot].xs = xs;
            glyph[slot].ys = ys;
            if (ep_hwnd_main)           /* état changé (case cochée...) */
                PostMessage(ep_hwnd_main, EPM_GLYPH, (WPARAM) hwnd, 0L);
        }
        return;
    }
    slot = 0;
    for (i = 0; i < GL_N; i++) {
        if (!glyph[i].hwnd || !IsWindow(glyph[i].hwnd)) {
            slot = i;
            break;
        }
        if (glyph[i].seq < glyph[slot].seq)
            slot = i;
    }
    glyph[slot].hwnd = hwnd;
    glyph[slot].r = r;
    glyph[slot].xs = xs;
    glyph[slot].ys = ys;
    glyph[slot].seq = ++seq;
}

/* Petite image de hwnd qui touche le rectangle (écran) : taille et position dans
   l'image source ; FALSE si aucune */
BOOL FAR PASCAL __export EpOsmGlyph(HWND hwnd, int l, int t, int r, int b,
                                    int FAR *w, int FAR *h, int FAR *xs, int FAR *ys)
{
    int i;
    for (i = 0; i < GL_N; i++)
        if (glyph[i].hwnd == hwnd && glyph[i].r.left < r && l < glyph[i].r.right &&
            glyph[i].r.top < b && t < glyph[i].r.bottom) {
            *w = glyph[i].r.right - glyph[i].r.left;
            *h = glyph[i].r.bottom - glyph[i].r.top;
            *xs = glyph[i].xs;
            *ys = glyph[i].ys;
            return TRUE;
        }
    return FALSE;
}

BOOL FAR PASCAL __export __loadds EpDeleteDC(HDC hdc)
{
    BOOL ret;
    int i;
    for (i = 0; i < MEM_N; i++)
        if (mem[i].hdc == hdc)
            mem[i].hdc = 0;
    patch_enter(&p_ddc);
    ret = DeleteDC(hdc);
    patch_leave(&p_ddc);
    return ret;
}

void FAR PASCAL __export __loadds EpScrollWindow(HWND hwnd, int dx, int dy,
                                               const RECT FAR *rc, const RECT FAR *clip)
{
    RECT area;
    POINT org;
    BOOL outer = !scrolling;

    if (outer) {
        scrolling = GetCurrentTask();
        GetClientRect(hwnd, &area);
        if (rc)
            IntersectRect(&area, &area, rc);
        if (clip)
            IntersectRect(&area, &area, clip);
        org.x = org.y = 0;
        ClientToScreen(hwnd, &org);
        OffsetRect(&area, org.x, org.y);
        scroll_text(hwnd, dx, dy, &area);
    }
    patch_enter(&p_sw);
    ScrollWindow(hwnd, dx, dy, rc, clip);
    patch_leave(&p_sw);
    if (outer)
        scrolling = 0;
}

BOOL FAR PASCAL __export __loadds EpScrollDC(HDC hdc, int dx, int dy,
    const RECT FAR *rc, const RECT FAR *clip, HRGN update, RECT FAR *update_rc)
{
    BOOL ret, outer = !scrolling;
    HWND hwnd;
    RECT area;
    POINT pt[2];
    DWORD org;

    if (outer) {
        scrolling = GetCurrentTask();
        hwnd = dc_window(hdc);
        if (hwnd) {
            GetClipBox(hdc, &area);
            if (rc)
                IntersectRect(&area, &area, rc);
            if (clip)
                IntersectRect(&area, &area, clip);
            pt[0].x = area.left; pt[0].y = area.top;
            pt[1].x = area.right; pt[1].y = area.bottom;
            LPtoDP(hdc, pt, 2);
            org = GetDCOrg(hdc);
            SetRect(&area, pt[0].x + (int) LOWORD(org), pt[0].y + (int) HIWORD(org),
                    pt[1].x + (int) LOWORD(org), pt[1].y + (int) HIWORD(org));
            /* ScrollDC exprime ses déplacements en unités de périphérique. */
            scroll_text(hwnd, dx, dy, &area);
        }
    }
    patch_enter(&p_sdc);
    ret = ScrollDC(hdc, dx, dy, rc, clip, update, update_rc);
    patch_leave(&p_sdc);
    if (outer)
        scrolling = 0;
    return ret;
}

/* Curseur dessiné par le programme dans hwnd : position (écran) ; FALSE si aucun */
BOOL FAR PASCAL __export EpOsmCaret(HWND hwnd, POINT FAR *pt)
{
    if (!fc_hwnd || fc_hwnd != hwnd)
        return FALSE;
    pt->x = fc_x;
    pt->y = fc_y;
    return TRUE;
}

HDC FAR PASCAL __export __loadds EpBeginPaint(HWND hwnd, PAINTSTRUCT FAR *ps)
{
    HDC hdc;
    POINT a, b;
    patch_enter(&p_bp);
    hdc = BeginPaint(hwnd, ps);
    patch_leave(&p_bp);
    dc_note(hdc, hwnd);
    /* La zone à repeindre va être redessinée : oublier ce qui y était écrit. Sauf
       dans les boîtes de dialogue de Word (classe bosa_sdm…, et sa_sdm… pour les
       pages d'une boîte à onglets comme Options) : elles ne redessinent
       qu'une partie de la zone (le rectangle de focus d'un bouton, pas son libellé)
       et peignent leur fond elles-mêmes ; leurs remplissages effacent alors ce
       qu'ils recouvrent. */
    {
        char cls[10];
        GetClassName(hwnd, cls, sizeof(cls));
        if (_fstrncmp(cls, "bosa_sdm", 8) == 0 || _fstrncmp(cls, "sa_sdm", 6) == 0)
            return hdc;
    }
    a.x = ps->rcPaint.left;
    a.y = ps->rcPaint.top;
    b.x = ps->rcPaint.right;
    b.y = ps->rcPaint.bottom;
    ClientToScreen(hwnd, &a);
    ClientToScreen(hwnd, &b);
    forget_rect(hwnd, a.x, a.y, b.x - a.x, b.y - a.y);
    {
        RECT r;
        SetRect(&r, a.x, a.y, b.x, b.y);
        inv_clear(hwnd, &r);
    }
    return hdc;
}

HDC FAR PASCAL __export __loadds EpGetDC(HWND hwnd)
{
    HDC hdc;
    patch_enter(&p_gdc);
    hdc = GetDC(hwnd);
    patch_leave(&p_gdc);
    dc_note(hdc, hwnd);
    return hdc;
}

HDC FAR PASCAL __export __loadds EpGetWindowDC(HWND hwnd)
{
    HDC hdc;
    patch_enter(&p_gwdc);
    hdc = GetWindowDC(hwnd);
    patch_leave(&p_gwdc);
    dc_note(hdc, hwnd);
    return hdc;
}

HDC FAR PASCAL __export __loadds EpGetDCEx(HWND hwnd, HRGN rgn, DWORD flags)
{
    HDC hdc;
    patch_enter(&p_gdcex);
    hdc = GetDCEx(hwnd, rgn, flags);
    patch_leave(&p_gdcex);
    dc_note(hdc, hwnd);
    return hdc;
}

#define LIVE(p) ((p).alias && !(p).removing)

BOOL osm_install(void)
{
    BOOL ok;
    osm_reap();
    /* Ne pas remplacer les octets sauvegardés lors d’un second appel. */
    if (LIVE(p_eto) || LIVE(p_to) || LIVE(p_bp) || LIVE(p_gdc) ||
        LIVE(p_gwdc) || LIVE(p_gdcex) || LIVE(p_pat) || LIVE(p_sw) ||
        LIVE(p_sdc) || LIVE(p_blt) || LIVE(p_ddc))
        return FALSE;
    if (!osm)
        osm = (OSMENT FAR *) GlobalLock(GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT | GMEM_SHARE,
                                                    (DWORD) sizeof(OSMENT) * OSM_N));
    if (!mem)
        mem = (MEMENT FAR *) GlobalLock(GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT | GMEM_SHARE,
                                                    (DWORD) sizeof(MEMENT) * MEM_N));
    if (!wid)
        wid = (BYTE FAR *) GlobalLock(GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT | GMEM_SHARE,
                                                  (DWORD) OSM_TXT * OSM_N));
    if (!osm || !mem || !wid)
        return FALSE;
    _fmemset(osm, 0, sizeof(OSMENT) * OSM_N);
    memset(frect, 0, sizeof(frect));
    memset(glyph, 0, sizeof(glyph));
    memset(fonts, 0, sizeof(fonts));
    memset(inv, 0, sizeof(inv));
    memset(dcmap, 0, sizeof(dcmap));
    fc_hwnd = 0;
    fr_top.hwnd = 0;
    _fmemset(mem, 0, sizeof(MEMENT) * MEM_N);
    ok = patch_install(&p_eto, "GDI", "EXTTEXTOUT", (FARPROC) EpExtTextOut) &&
           patch_install(&p_to, "GDI", "TEXTOUT", (FARPROC) EpTextOut) &&
           patch_install(&p_bp, "USER", "BEGINPAINT", (FARPROC) EpBeginPaint) &&
           patch_install(&p_gdc, "USER", "GETDC", (FARPROC) EpGetDC) &&
           patch_install(&p_gwdc, "USER", "GETWINDOWDC", (FARPROC) EpGetWindowDC) &&
           patch_install(&p_gdcex, "USER", "GETDCEX", (FARPROC) EpGetDCEx) &&
           patch_install(&p_pat, "GDI", "PATBLT", (FARPROC) EpPatBlt) &&
           patch_install(&p_sw, "USER", "SCROLLWINDOW", (FARPROC) EpScrollWindow) &&
           patch_install(&p_sdc, "USER", "SCROLLDC", (FARPROC) EpScrollDC) &&
           patch_install(&p_blt, "GDI", "BITBLT", (FARPROC) EpBitBlt) &&
           patch_install(&p_ddc, "GDI", "DELETEDC", (FARPROC) EpDeleteDC);
    if (!ok)
        osm_remove();
    return ok;
}

/* TRUE si un appel d'origine reviendra encore dans la DLL : hook.c la garde chargée */
BOOL osm_remove(void)
{
    osm_reap();
    patch_remove(&p_eto);
    patch_remove(&p_to);
    patch_remove(&p_bp);
    patch_remove(&p_gdc);
    patch_remove(&p_gwdc);
    patch_remove(&p_gdcex);
    patch_remove(&p_pat);
    patch_remove(&p_sw);
    patch_remove(&p_sdc);
    patch_remove(&p_blt);
    patch_remove(&p_ddc);
    return pending != 0;
}

/* ------------------------------------------------------- lecture (pour l'EXE) */

/* Caractère de l'entrée i sous l'abscisse sx (écran), d'après les largeurs notées */
static int column_at(int i, int sx)
{
    int k, len = lstrlen(osm[i].t), pos = osm[i].x;
    for (k = 0; k < len; k++) {
        pos += wid[i * OSM_TXT + k];
        if (sx < pos)
            return k;
    }
    return len;
}

/* Texte d'une entrée (après EPM_HILITE) ; 0 si elle n'existe plus */
int FAR PASCAL __export EpOsmGet(int slot, LPSTR out, int max)
{
    if (!osm)
        return 0;
    if (slot < 0 || slot >= OSM_N || !osm[slot].hwnd)
        return 0;
    lstrcpyn(out, osm[slot].t, max);
    return lstrlen(out);
}

/* Ligne de texte de la fenêtre hwnd qui contient l'ordonnée sy (écran), de gauche à
   droite. Renvoie le nombre de morceaux trouvés. */
int FAR PASCAL __export EpOsmLine(HWND hwnd, int sy, LPSTR out, int max)
{
    int used[OSM_N / 8 + 1];
    int i, best, n = 0, len = 0, last_right = -32000;

    if (!osm)
        return 0;

    memset(used, 0, sizeof(used));
    out[0] = 0;
    for (;;) {
        best = -1;
        for (i = 0; i < OSM_N; i++) {
            OSMENT *e = &osm[i];
            if (e->hwnd != hwnd || (used[i / 16] & (1 << (i % 16))) ||
                sy < e->y || sy >= e->y + e->h)
                continue;
            if (best < 0 || e->x < osm[best].x)
                best = i;
        }
        if (best < 0)
            break;
        used[best / 16] |= 1 << (best % 16);
        if (len && osm[best].x > last_right + 2 && len < max - 1) {
            out[len++] = ' ';
            out[len] = 0;
        }
        lstrcpyn(out + len, osm[best].t, max - len);
        len = lstrlen(out);
        last_right = osm[best].x + osm[best].w;
        n++;
    }
    return n;
}

static BOOL child_of(HWND h, HWND top)
{
    for (; h; h = GetParent(h))
        if (h == top)
            return TRUE;
    return FALSE;
}

static BOOL review_visible(OSMENT *e, HWND scope)
{
    RECT rc, win;
    HWND h;
    int cy = e->y + e->h / 2;
    if (!e->hwnd || !IsWindowVisible(e->hwnd) || !child_of(e->hwnd, scope))
        return FALSE;
    SetRect(&rc, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    for (h = e->hwnd; h; h = GetParent(h)) {
        GetWindowRect(h, &win);
        IntersectRect(&rc, &rc, &win);
        if (h == scope) break;
    }
    return cy >= rc.top && cy < rc.bottom && e->x < rc.right && e->x + e->w > rc.left;
}

/* Une rangée de texte visible, même si ses morceaux appartiennent à plusieurs
   contrôles. Ne renvoyer que des copies : les entrées OSM sont recyclées. */
int FAR PASCAL __export EpOsmReviewLine(HWND scope, POINT FAR *pt, int direction,
                                       LPSTR out, int max, int FAR *column)
{
    int i, best = -1, distance = 32000, d, sy = pt->y;
    int top = sy, bottom = sy + 1, len = 0, right = -32000, count = 0;
    int used[OSM_N / 16 + 1];

    if (!osm)
        return 0;

    out[0] = 0;
    *column = 0;
    /* Limites de la rangée courante : ne pas prendre un autre fragment de la
       même ligne pour la ligne suivante (polices de hauteurs différentes). */
    for (i = 0; i < OSM_N; i++) {
        OSMENT *e = &osm[i];
        if (review_visible(e, scope) &&
            sy >= e->y && sy < e->y + e->h) {
            if (e->y < top) top = e->y;
            if (e->y + e->h > bottom) bottom = e->y + e->h;
        }
    }
    for (i = 0; i < OSM_N; i++) {
        OSMENT *e = &osm[i];
        if (!review_visible(e, scope))
            continue;
        if (direction == -1) {
            if (e->y + e->h > top) continue;
            d = top - e->y - e->h;
        } else if (direction == 1) {
            if (e->y < bottom) continue;
            d = e->y - bottom;
        } else {
            d = sy < e->y ? e->y - sy : sy >= e->y + e->h ? sy - e->y - e->h + 1 : 0;
            if (direction == 0 && d) continue;
        }
        if (best < 0 || d < distance) {
            best = i;
            distance = d;
        }
    }
    if (best < 0)
        return 0;
    sy = osm[best].y + osm[best].h / 2;
    pt->y = sy;
    memset(used, 0, sizeof(used));
    for (;;) {
        best = -1;
        for (i = 0; i < OSM_N; i++) {
            OSMENT *e = &osm[i];
            if ((used[i / 16] & (1 << (i % 16))) || !review_visible(e, scope) ||
                sy < e->y || sy >= e->y + e->h)
                continue;
            if (best < 0 || e->x < osm[best].x)
                best = i;
        }
        if (best < 0 || len >= max - 2)
            break;
        used[best / 16] |= 1 << (best % 16);
        if (len && osm[best].x > right + 2)
            out[len++] = ' ';
        if (pt->x >= osm[best].x)
            *column = len + column_at(best, pt->x);
        lstrcpyn(out + len, osm[best].t, max - len);
        len = lstrlen(out);
        right = osm[best].x + osm[best].w;
        count++;
    }
    if (*column > len) *column = len;
    return count;
}

/* Tout le texte connu des fenêtres contenues dans top, de haut en bas puis de gauche à
   droite, une ligne par rangée de texte (séparateur « / »). Renvoie le nombre de
   morceaux. */
int FAR PASCAL __export EpOsmWindow(HWND top, LPSTR out, int max)
{
    int used[OSM_N / 16 + 1];
    int i, best, n = 0, len = 0, row = -32000, last_right = -32000;

    if (!osm)
        return 0;

    memset(used, 0, sizeof(used));
    out[0] = 0;
    for (;;) {
        best = -1;
        for (i = 0; i < OSM_N; i++) {
            OSMENT *e = &osm[i], *b;
            if (!e->hwnd || (used[i / 16] & (1 << (i % 16))) || !child_of(e->hwnd, top))
                continue;
            b = best >= 0 ? &osm[best] : NULL;
            /* même rangée si les hauteurs se chevauchent de plus de la moitié */
            if (!b || e->y + e->h / 2 < b->y ||
                (e->y < b->y + b->h / 2 && e->x < b->x))
                best = i;
        }
        if (best < 0 || len >= max - 4)
            break;
        used[best / 16] |= 1 << (best % 16);
        if (len) {
            if (osm[best].y >= row + osm[best].h / 2)
                lstrcpy(out + len, " / ");
            else if (osm[best].x > last_right + 2)
                lstrcpy(out + len, " ");
            len = lstrlen(out);
        }
        lstrcpyn(out + len, osm[best].t, max - len);
        len = lstrlen(out);
        row = osm[best].y;
        last_right = osm[best].x + osm[best].w;
        n++;
    }
    return n;
}

/* Texte principal d'une boîte de dialogue dessinée (message, astuce) : les textes de
   la zone client de hwnd sont regroupés en colonnes (morceaux qui se chevauchent
   horizontalement) ; on garde la colonne la plus fournie, de haut en bas. La colonne
   des boutons, à côté, est laissée de côté. Renvoie le nombre de morceaux. */
/* Largeur d'un morceau pour le regroupement en colonnes : jamais nulle (lettrine du
   Didacticiel, dans une police dont les largeurs ne sont pas connues) */
static int main_w(int i)
{
    return osm[i].w > 0 ? osm[i].w : osm[i].h / 2 + 1;
}

/* Ordre de lecture : a avant b s'il est franchement au-dessus, ou sur la même rangée
   (centres proches : lettre en gras décalée de quelques points) et à gauche */
static BOOL main_before(int a, int b)
{
    int ca = osm[a].y + osm[a].h / 2, cb = osm[b].y + osm[b].h / 2;
    int tol = min(osm[a].h, osm[b].h) / 2;
    if (ca < cb - tol)
        return TRUE;
    if (ca > cb + tol)
        return FALSE;
    return osm[a].x < osm[b].x;
}

int FAR PASCAL __export EpOsmMainText(HWND hwnd, LPSTR out, int max)
{
    static BYTE col[OSM_N];
    static int total[OSM_N];
    int i, j, best = -1, n = 0, len = 0, changed, prev = -1;
    POINT org;
    RECT rc;

    out[0] = 0;
    if (!osm)
        return 0;
    org.x = org.y = 0;
    ClientToScreen(hwnd, &org);
    GetClientRect(hwnd, &rc);
    OffsetRect(&rc, org.x, org.y);
    for (i = 0; i < OSM_N; i++) {
        OSMENT *e = &osm[i];
        col[i] = (BYTE) (e->hwnd == hwnd && e->y + e->h / 2 >= rc.top &&
                         e->y + e->h / 2 < rc.bottom && e->t[0]) ? 1 : 0;
    }
    /* colonnes : propager un numéro commun aux morceaux qui se chevauchent */
    for (i = 0; i < OSM_N; i++)
        total[i] = col[i] ? i : -1;
    do {
        changed = 0;
        for (i = 0; i < OSM_N; i++) {
            if (total[i] < 0)
                continue;
            for (j = 0; j < OSM_N; j++)
                if (total[j] >= 0 && total[j] != total[i] &&
                    osm[i].x < osm[j].x + main_w(j) && osm[j].x < osm[i].x + main_w(i)) {
                    int m = min(total[i], total[j]);
                    changed |= total[i] != m || total[j] != m;
                    total[i] = total[j] = m;
                }
        }
    } while (changed);
    {   /* colonne la plus fournie */
        static int chars[OSM_N];
        memset(chars, 0, sizeof(chars));
        for (i = 0; i < OSM_N; i++)
            if (total[i] >= 0) {
                chars[total[i]] += lstrlen(osm[i].t);
                if (best < 0 || chars[total[i]] > chars[best])
                    best = total[i];
            }
    }
    if (best < 0)
        return 0;
    for (;;) {                          /* de haut en bas, puis de gauche à droite */
        int k = -1;
        for (i = 0; i < OSM_N; i++)
            if (total[i] == best && (k < 0 || main_before(i, k)))
                k = i;
        if (k < 0 || len >= max - 2)
            break;
        total[k] = -1;
        /* lettrine (Didacticiel) : grande initiale suivie du reste du mot */
        if (len && !(prev >= 0 && osm[prev].t[1] == 0 && osm[prev].h > osm[k].h * 3 / 2))
            out[len++] = ' ';
        prev = k;
        lstrcpyn(out + len, osm[k].t, max - len);
        len = lstrlen(out);
        n++;
    }
    return n;
}

/* Texte en surbrillance de la fenêtre hwnd (de haut en bas), morceaux séparés d'une
   espace : « Gestionnaire » + « de fichiers » sur deux lignes sous une icône. */
int FAR PASCAL __export EpOsmHilite(HWND hwnd, LPSTR out, int max)
{
    int used[OSM_N / 16 + 1];
    int i, best, n = 0, len = 0;

    if (!osm)
        return 0;

    memset(used, 0, sizeof(used));
    out[0] = 0;
    for (;;) {
        best = -1;
        for (i = 0; i < OSM_N; i++) {
            OSMENT *e = &osm[i];
            if (e->hwnd != hwnd || !e->hi || (used[i / 16] & (1 << (i % 16))))
                continue;
            if (best < 0 || e->y < osm[best].y ||
                (e->y == osm[best].y && e->x < osm[best].x))
                best = i;
        }
        if (best < 0 || len >= max - 2)
            break;
        used[best / 16] |= 1 << (best % 16);
        if (len)
            out[len++] = ' ';
        lstrcpyn(out + len, osm[best].t, max - len);
        len = lstrlen(out);
        n++;
    }
    return n;
}

/* Texte affiché dans le rectangle (écran) de la fenêtre hwnd, de gauche à droite :
   élément d'une liste dessinée par le programme (Gestionnaire de fichiers). */
int FAR PASCAL __export EpOsmRect(HWND hwnd, int l, int t, int r, int b, LPSTR out, int max)
{
    int used[OSM_N / 16 + 1];
    int i, best, n = 0, len = 0;

    if (!osm)
        return 0;

    memset(used, 0, sizeof(used));
    out[0] = 0;
    for (;;) {
        best = -1;
        for (i = 0; i < OSM_N; i++) {
            OSMENT *e = &osm[i];
            int cy = e->y + e->h / 2;
            if (e->hwnd != hwnd || (used[i / 16] & (1 << (i % 16))) ||
                cy < t || cy >= b || e->x >= r || e->x + e->w <= l)
                continue;
            if (best < 0 || e->x < osm[best].x)
                best = i;
        }
        if (best < 0 || len >= max - 2)
            break;
        used[best / 16] |= 1 << (best % 16);
        if (len)
            out[len++] = ' ';
        lstrcpyn(out + len, osm[best].t, max - len);
        len = lstrlen(out);
        n++;
    }
    return n;
}

/* Caractère de la fenêtre hwnd sous le point (sx, sy) de l'écran ; 0 si rien. */
int FAR PASCAL __export EpOsmCharAt(HWND hwnd, int sx, int sy)
{
    int i, len, k;

    if (!osm)
        return 0;
    for (i = 0; i < OSM_N; i++) {
        OSMENT *e = &osm[i];
        if (e->hwnd != hwnd || sy < e->y || sy >= e->y + e->h ||
            sx < e->x || sx >= e->x + e->w)
            continue;
        len = lstrlen(e->t);
        k = column_at(i, sx);
        return (k >= 0 && k < len) ? (BYTE) e->t[k] : 0;
    }
    return 0;
}

/* Abscisse (écran) dans le caractère k de l'entrée i, au premier tiers : un clic y
   pose le curseur avant lui, malgré l'imprécision des largeurs estimées */
static int char_x(int i, int k)
{
    int j, x = osm[i].x;
    for (j = 0; j < k; j++)
        x += wid[i * OSM_TXT + j];
    return x + wid[i * OSM_TXT + k] / 3;
}

/* Rangée d'ordonnée pt->y composée comme dans EpOsmReviewLine : abscisse du
   caractère column (fin de rangée : juste après le dernier caractère). */
BOOL FAR PASCAL __export EpOsmReviewPoint(HWND scope, POINT FAR *pt, int column)
{
    int used[OSM_N / 16 + 1];
    int i, best, len = 0, n, right = -32000, sy = pt->y;

    if (!osm)
        return FALSE;
    memset(used, 0, sizeof(used));
    for (;;) {
        best = -1;
        for (i = 0; i < OSM_N; i++) {
            OSMENT *e = &osm[i];
            if ((used[i / 16] & (1 << (i % 16))) || !review_visible(e, scope) ||
                sy < e->y || sy >= e->y + e->h)
                continue;
            if (best < 0 || e->x < osm[best].x)
                best = i;
        }
        if (best < 0)
            break;
        used[best / 16] |= 1 << (best % 16);
        if (len && osm[best].x > right + 2) {
            if (column == len) {        /* espace ajoutée entre deux morceaux */
                pt->x = (right + osm[best].x) / 2;
                return TRUE;
            }
            len++;
        }
        n = lstrlen(osm[best].t);
        if (column < len + n) {
            pt->x = char_x(best, column - len < 0 ? 0 : column - len);
            return TRUE;
        }
        len += n;
        right = osm[best].x + osm[best].w;
    }
    if (right == -32000)
        return FALSE;
    pt->x = right + 1;
    return TRUE;
}

static void add_word(LPSTR out, int max, const char *s)
{
    int n = lstrlen(out);
    if (n && n < max - 3) {
        lstrcat(out, ", ");
        n += 2;
    }
    lstrcpyn(out + n, s, max - n);
}

static const char *color_name(COLORREF c)
{
    static const struct { COLORREF c; const char *name; } names[] = {
        { RGB(0, 0, 0), "noir" }, { RGB(255, 255, 255), "blanc" },
        { RGB(255, 0, 0), "rouge" }, { RGB(0, 255, 0), "vert vif" },
        { RGB(0, 0, 255), "bleu" }, { RGB(255, 255, 0), "jaune" },
        { RGB(0, 255, 255), "turquoise" }, { RGB(255, 0, 255), "rose" },
        { RGB(128, 0, 0), "rouge foncé" }, { RGB(0, 128, 0), "vert" },
        { RGB(0, 0, 128), "bleu foncé" }, { RGB(128, 128, 0), "jaune foncé" },
        { RGB(0, 128, 128), "bleu-vert" }, { RGB(128, 0, 128), "violet" },
        { RGB(128, 128, 128), "gris" }, { RGB(192, 192, 192), "gris clair" },
    };
    int i;
    for (i = 0; i < (int) (sizeof(names) / sizeof(names[0])); i++)
        if (names[i].c == (c & 0xFFFFFFL))
            return names[i].name;
    return NULL;
}

int FAR PASCAL __export EpOsmFormat(HWND scope, int sx, int sy, LPSTR out, int max)
{
    int i, best = -1, d, best_d = 32000;
    FONTENT *f;
    char buf[48];
    const char *c;

    out[0] = 0;
    if (!osm)
        return 0;
    for (i = 0; i < OSM_N; i++) {
        OSMENT *e = &osm[i];
        if (!review_visible(e, scope) || sy < e->y || sy >= e->y + e->h || !e->t[0])
            continue;
        /* texte sous le point, sinon le plus proche à sa gauche, puis à sa droite */
        d = sx < e->x ? e->x - sx + 1000 : sx >= e->x + e->w ? sx - e->x - e->w + 1 : 0;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (best < 0 || !osm[best].font)
        return 0;
    f = &fonts[osm[best].font - 1];
    lstrcpyn(out, f->face, max);
    wsprintf(buf, "%d points", f->pts);
    add_word(out, max, buf);
    if (f->bold) add_word(out, max, "gras");
    if (f->italic) add_word(out, max, "italique");
    if (f->under) add_word(out, max, "souligné");
    if (f->strike) add_word(out, max, "barré");
    if (osm[best].hi)                   /* couleur de la sélection, pas du texte */
        add_word(out, max, "en surbrillance");
    else if ((f->color & 0xFFFFFFL) != 0 &&
             (f->color & 0xFFFFFFL) != (GetSysColor(COLOR_WINDOWTEXT) & 0xFFFFFFL)) {
        c = color_name(f->color);
        if (!c) {
            wsprintf(buf, "couleur %d %d %d", GetRValue(f->color), GetGValue(f->color),
                     GetBValue(f->color));
            c = buf;
        }
        add_word(out, max, c);
    }
    return lstrlen(out);
}

/* Caractère k de l'entrée i sélectionné : couleurs de sélection ou zone inversée */
static BOOL char_selected(int i, int k)
{
    return osm[i].hi || inverted(osm[i].hwnd, char_x(i, k), osm[i].y + osm[i].h / 2);
}

int FAR PASCAL __export EpOsmSelection(HWND scope, LPSTR out, int max)
{
    int used[OSM_N / 16 + 1];
    int i, k, n, best, len = 0, row = -32000, right = -32000;
    BOOL prev = FALSE;

    out[0] = 0;
    if (!osm)
        return 0;
    memset(used, 0, sizeof(used));
    for (;;) {                          /* même ordre que EpOsmWindow */
        best = -1;
        for (i = 0; i < OSM_N; i++) {
            OSMENT *e = &osm[i], *b;
            if ((used[i / 16] & (1 << (i % 16))) || !review_visible(e, scope))
                continue;
            b = best >= 0 ? &osm[best] : NULL;
            if (!b || e->y + e->h / 2 < b->y ||
                (e->y < b->y + b->h / 2 && e->x < b->x))
                best = i;
        }
        if (best < 0 || len >= max - 2)
            break;
        used[best / 16] |= 1 << (best % 16);
        if (osm[best].y >= row + osm[best].h / 2 || osm[best].x > right + 2)
            prev = FALSE;               /* autre rangée ou intervalle : séparer */
        n = lstrlen(osm[best].t);
        for (k = 0; k < n && len < max - 2; k++) {
            if (!char_selected(best, k)) {
                prev = FALSE;
                continue;
            }
            if (!prev && len && out[len - 1] != ' ')
                out[len++] = ' ';
            out[len++] = osm[best].t[k];
            prev = TRUE;
        }
        out[len] = 0;
        row = osm[best].y;
        right = osm[best].x + osm[best].w;
    }
    while (len > 0 && out[len - 1] == ' ')
        out[--len] = 0;
    return len;
}

BOOL FAR PASCAL __export EpOsmRowSelection(HWND scope, int sy, int FAR *from, int FAR *to)
{
    int used[OSM_N / 16 + 1];
    int i, k, n, best, len = 0, right = -32000;
    BOOL any = FALSE;

    *from = *to = -1;
    if (!osm)
        return FALSE;
    /* le plus souvent rien n'est sélectionné sur la rangée : ne pas la composer */
    for (i = 0; i < INV_N && !any; i++)
        any = inv[i].hwnd && child_of(inv[i].hwnd, scope) &&
              sy >= inv[i].r.top && sy < inv[i].r.bottom;
    for (i = 0; i < OSM_N && !any; i++)
        any = osm[i].hi && review_visible(&osm[i], scope) &&
              sy >= osm[i].y && sy < osm[i].y + osm[i].h;
    if (!any)
        return FALSE;
    memset(used, 0, sizeof(used));
    for (;;) {                          /* même composition que EpOsmReviewPoint */
        best = -1;
        for (i = 0; i < OSM_N; i++) {
            OSMENT *e = &osm[i];
            if ((used[i / 16] & (1 << (i % 16))) || !review_visible(e, scope) ||
                sy < e->y || sy >= e->y + e->h)
                continue;
            if (best < 0 || e->x < osm[best].x)
                best = i;
        }
        if (best < 0)
            break;
        used[best / 16] |= 1 << (best % 16);
        if (len && osm[best].x > right + 2)
            len++;                      /* espace ajoutée entre deux morceaux */
        n = lstrlen(osm[best].t);
        for (k = 0; k < n; k++)
            if (char_selected(best, k)) {
                if (*from < 0)
                    *from = len + k;
                *to = len + k + 1;
            }
        len += n;
        right = osm[best].x + osm[best].w;
    }
    return *from >= 0;
}

DWORD FAR PASCAL __export EpOsmSignature(HWND scope)
{
    DWORD sum = 0, h;
    int i, k;

    if (!osm)
        return 0;
    for (i = 0; i < OSM_N; i++) {
        OSMENT *e = &osm[i];
        if (!e->hwnd || !child_of(e->hwnd, scope))
            continue;
        /* somme : indépendante de la place des entrées dans le tableau */
        h = (DWORD) e->y * 7919L + e->x;
        for (k = 0; e->t[k]; k++)
            h = h * 31 + (BYTE) e->t[k];
        sum += h;
    }
    return sum;
}
