/* Simulation de l'API Win16 pour exécuter les vraies sources d'Épaulard (osm.c,
   epaulard.c, braille.c) dans un banc natif. Le « matériel » est réduit à ce que lisent
   ces sources : fenêtres (arbre, rectangles, styles, classes, textes), contextes de
   dessin (fenêtre, origine, viewport, couleurs), file de messages postés, horloge,
   ports série. Les fonctions GDI de dessin ne dessinent rien : le banc appelle les
   remplaçants d'Épaulard (EpTextOut...) comme le ferait GDI une fois détourné.
   Polices : 8 px par caractère, 16 px de haut. */
#ifndef WIN16SIM_H
#define WIN16SIM_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <stdint.h>

#define FAR
#define NEAR
#define PASCAL
#define CALLBACK
#define WINAPI
#define __export
#define __loadds
#define TRUE 1
#define FALSE 0
/* poignées entières, comme sans STRICT : NULL vaut 0 */
#undef NULL
#define NULL 0

typedef unsigned char BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int BOOL;
typedef unsigned int UINT;
typedef int32_t LONG;
typedef UINT WPARAM;
typedef LONG LPARAM;
typedef LONG LRESULT;
typedef DWORD COLORREF;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef unsigned int HANDLE;
typedef HANDLE HWND, HDC, HMENU, HINSTANCE, HGLOBAL, HRGN, HMODULE, HICON, HCURSOR,
               HBRUSH, HTASK;
typedef void (*FARPROC)(void);
typedef void (*TIMERPROC)(HWND, UINT, UINT, DWORD);
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct { int x, y; } POINT;
typedef struct { int left, top, right, bottom; } RECT;
typedef RECT *LPRECT;
typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; } PAINTSTRUCT;
typedef struct { int tmHeight, tmAscent, tmDescent, tmInternalLeading, tmWeight;
                 BYTE tmItalic, tmUnderlined, tmStruckOut; } TEXTMETRIC;
typedef struct { UINT style; WNDPROC lpfnWndProc; HINSTANCE hInstance; LPCSTR lpszClassName; } WNDCLASS;
typedef struct { int fOutxCtsFlow, fOutxDsrFlow, fInX, fOutX, fRtsDisable, fDtrDisable; } DCB;
typedef struct { int cbOutQue; } COMSTAT;

#define LOWORD(l) ((WORD) ((DWORD) (l) & 0xFFFF))
#define HIWORD(l) ((WORD) (((DWORD) (l) >> 16) & 0xFFFF))
#define LOBYTE(w) ((BYTE) ((w) & 0xFF))
#define HIBYTE(w) ((BYTE) (((WORD) (w) >> 8) & 0xFF))
#define MAKELONG(a, b) ((LONG) (((WORD) (a)) | ((DWORD) ((WORD) (b))) << 16))
#define MAKELPARAM(a, b) MAKELONG(a, b)
#define RGB(r, g, b) ((COLORREF) ((BYTE) (r) | ((WORD) (BYTE) (g) << 8) | ((DWORD) (BYTE) (b) << 16)))
#define GetRValue(c) ((BYTE) (c))
#define GetGValue(c) ((BYTE) ((c) >> 8))
#define GetBValue(c) ((BYTE) ((c) >> 16))
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
/* Pointeurs 16:16 : le patch de GDI écrit dans une zone sans conséquence */
static BYTE sim_patch_area[16];
#define MAKELP(sel, off) ((void *) sim_patch_area)
#define OFFSETOF(p) ((WORD) 0)
#define SELECTOROF(p) ((WORD) 0)

/* ------------------------------------------------------------- constantes */
#define WS_OVERLAPPED 0x00000000L
#define WS_CHILD      0x40000000L
#define WS_CAPTION    0x00C00000L
#define WS_DISABLED   0x08000000L
#define WS_VISIBLE    0x10000000L
#define GWW_ID (-12)
#define GWL_STYLE (-16)
#define GW_HWNDNEXT 2
#define GW_HWNDPREV 3
#define GW_OWNER 4
#define GW_CHILD 5
#define ES_MULTILINE 0x0004L
#define ES_PASSWORD 0x0020L
#define SS_RIGHT 2
#define BS_CHECKBOX 2
#define BS_AUTOCHECKBOX 3
#define BS_RADIOBUTTON 4
#define BS_3STATE 5
#define BS_AUTO3STATE 6
#define BS_GROUPBOX 7
#define BS_AUTORADIOBUTTON 9
#define LBS_MULTIPLESEL 0x0008L
#define CBS_OWNERDRAWFIXED 0x0010L
#define CBS_OWNERDRAWVARIABLE 0x0020L
#define LBS_OWNERDRAWFIXED 0x0010L
#define LBS_OWNERDRAWVARIABLE 0x0020L
#define LBS_HASSTRINGS 0x0040L
#define LBS_EXTENDEDSEL 0x0800L
#define LB_ERR (-1)
#define WM_COMMAND 0x0111
#define WM_USER 0x0400
#define WM_DESTROY 0x0002
#define WM_TIMER 0x0113
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_MENUSELECT 0x011F
#define EM_GETSEL (WM_USER + 0)
#define EM_SETSEL (WM_USER + 1)
#define EM_GETLINECOUNT (WM_USER + 10)
#define EM_LINEINDEX (WM_USER + 11)
#define EM_GETLINE (WM_USER + 20)
#define EM_LINEFROMCHAR (WM_USER + 25)
#define LB_GETSEL (WM_USER + 8)
#define LB_GETCURSEL (WM_USER + 9)
#define LB_GETTEXT (WM_USER + 10)
#define LB_GETTEXTLEN (WM_USER + 11)
#define LB_GETITEMRECT (WM_USER + 25)
#define LB_GETCOUNT (WM_USER + 12)
#define LB_GETCARETINDEX (WM_USER + 32)
#define CB_GETLBTEXT (WM_USER + 8)
#define CB_GETLBTEXTLEN (WM_USER + 9)
#define CB_GETCURSEL (WM_USER + 7)
#define BM_GETCHECK (WM_USER + 0)
#define SB_CTL 2
#define MF_BYCOMMAND 0x0000
#define MF_GRAYED 0x0001
#define MF_DISABLED 0x0002
#define MF_BITMAP 0x0004
#define MF_CHECKED 0x0008
#define MF_POPUP 0x0010
#define MF_SEPARATOR 0x0800
#define MF_BYPOSITION 0x0400
#define MF_SYSMENU 0x2000
#define MB_OK 0
#define MB_ICONSTOP 0x10
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_ESCAPE 0x1B
#define VK_DELETE 0x2E
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_MULTIPLY 0x6A
#define VK_DIVIDE 0x6F
#define VK_F4 0x73
#define COLOR_HIGHLIGHT 13
#define COLOR_HIGHLIGHTTEXT 14
#define COLOR_WINDOWTEXT 8
#define OPAQUE 2
#define TRANSPARENT 1
#define ETO_OPAQUE 0x0002
#define TA_UPDATECP 1
#define TA_RIGHT 2
#define TA_CENTER 6
#define TA_BOTTOM 8
#define TA_BASELINE 24
#define FW_SEMIBOLD 600
#define LF_FACESIZE 32
#define LOGPIXELSY 90
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SRCCOPY 0x00CC0020L
#define PATCOPY 0x00F00021L
#define PATINVERT 0x005A0049L
#define DSTINVERT 0x00550009L
#define BLACKNESS 0x00000042L
#define WHITENESS 0x00FF0062L
#define GMEM_FIXED 0
#define GMEM_ZEROINIT 0x40
#define GMEM_SHARE 0x2000

/* ------------------------------------------------------------- horloge */
static DWORD sim_now = 1000;
static DWORD GetTickCount(void) { return sim_now; }

/* ------------------------------------------------------------- chaînes */
static int lstrlen(LPCSTR s) { return s ? (int) strlen(s) : 0; }
static LPSTR lstrcpy(LPSTR d, LPCSTR s) { memmove(d, s, strlen(s) + 1); return d; }
static LPSTR lstrcat(LPSTR d, LPCSTR s) { memmove(d + strlen(d), s, strlen(s) + 1); return d; }
static int lstrcmp(LPCSTR a, LPCSTR b) { return strcmp(a, b); }
static int lstrcmpi(LPCSTR a, LPCSTR b) { return strcasecmp(a, b); }
static LPSTR lstrcpyn(LPSTR d, LPCSTR s, int n)
{
    int k = 0;
    if (n <= 0) return d;
    while (k < n - 1 && s[k]) k++;
    memmove(d, s, k);
    d[k] = 0;
    return d;
}
static LPSTR AnsiUpper(LPSTR s) { char *p; for (p = s; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32; return s; }
static void AnsiToOem(LPCSTR a, LPSTR o) { if (a != o) strcpy(o, a); }
static BOOL IsCharUpper(char c) { return c >= 'A' && c <= 'Z'; }
static BOOL IsCharAlphaNumeric(char c)
{ return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (BYTE) c >= 0xC0; }

/* ------------------------------------------------------------- fichiers INI */
/* Un seul fichier, en mémoire ; sim_ini_writes compte les écritures */
#define SIM_INI_N 32
static struct { char sec[32], key[32], val[64]; } sim_ini[SIM_INI_N];
static int sim_ini_n, sim_ini_writes;
static char sim_ini_file[160];
static int GetModuleFileName(HINSTANCE h, LPSTR s, int n) { (void) h; lstrcpyn(s, "C:\\EPAULARD\\EPAULARD.EXE", n); return lstrlen(s); }
static int GetPrivateProfileInt(LPCSTR sec, LPCSTR key, int def, LPCSTR file)
{
    int i;
    (void) file;
    for (i = 0; i < sim_ini_n; i++)
        if (!strcmp(sim_ini[i].sec, sec) && !strcmp(sim_ini[i].key, key)) return atoi(sim_ini[i].val);
    return def;
}
static BOOL WritePrivateProfileString(LPCSTR sec, LPCSTR key, LPCSTR val, LPCSTR file)
{
    int i;
    lstrcpyn(sim_ini_file, file, sizeof(sim_ini_file));
    sim_ini_writes++;
    for (i = 0; i < sim_ini_n; i++)
        if (!strcmp(sim_ini[i].sec, sec) && !strcmp(sim_ini[i].key, key)) break;
    if (i == sim_ini_n) {
        if (sim_ini_n >= SIM_INI_N) abort();
        sim_ini_n++;
    }
    lstrcpyn(sim_ini[i].sec, sec, 32); lstrcpyn(sim_ini[i].key, key, 32); lstrcpyn(sim_ini[i].val, val, 64);
    return TRUE;
}
#define wsprintf sprintf
#define _fstrchr strchr
#define _fstrstr strstr
#define _fstrncmp strncmp
#define _fmemset memset
#define _fmemcpy memcpy
#define _fmemmove memmove

/* ------------------------------------------------------------- fenêtres */
#define SIM_WN 64
typedef struct {
    BOOL used;
    HWND parent, owner;
    LONG style;
    WORD id;
    char cls[32], text[256];       /* titre : jusqu'à TXT - 1 octets */
    RECT rc;                    /* écran ; zone client identique */
    LRESULT (*proc)(HWND, UINT, WPARAM, LPARAM);
} SIMWIN;
static SIMWIN sim_win[SIM_WN];
static HWND sim_focus, sim_active, sim_sysmodal;

static HWND sim_create(HWND parent, const char *cls, const char *text, LONG style,
                       int l, int t, int r, int b)
{
    int i;
    for (i = 1; i < SIM_WN; i++)
        if (!sim_win[i].used) {
            memset(&sim_win[i], 0, sizeof(sim_win[i]));
            sim_win[i].used = TRUE;
            sim_win[i].parent = parent;
            sim_win[i].style = style | WS_VISIBLE | (parent ? WS_CHILD : 0);
            strcpy(sim_win[i].cls, cls);
            strcpy(sim_win[i].text, text);
            sim_win[i].rc.left = l; sim_win[i].rc.top = t;
            sim_win[i].rc.right = r; sim_win[i].rc.bottom = b;
            return (HWND) i;
        }
    abort();
    return 0;
}
static void sim_destroy(HWND h) { sim_win[h].used = FALSE; }
static BOOL IsWindow(HWND h) { return h > 0 && h < SIM_WN && sim_win[h].used; }
static HWND GetParent(HWND h) { return IsWindow(h) ? sim_win[h].parent : 0; }
static WORD GetWindowWord(HWND h, int i) { (void) i; return sim_win[h].id; }
static LONG GetWindowLong(HWND h, int i) { (void) i; return IsWindow(h) ? sim_win[h].style : 0; }
static BOOL IsWindowEnabled(HWND h) { return IsWindow(h) && !(sim_win[h].style & WS_DISABLED); }
static BOOL IsWindowVisible(HWND h)
{
    for (; h; h = sim_win[h].parent)
        if (!IsWindow(h) || !(sim_win[h].style & WS_VISIBLE)) return FALSE;
    return TRUE;
}
static HWND GetWindow(HWND h, UINT cmd)
{
    int i;
    if (cmd == GW_OWNER) return IsWindow(h) ? sim_win[h].owner : 0;
    if (cmd == GW_CHILD) {
        for (i = 1; i < SIM_WN; i++)
            if (sim_win[i].used && sim_win[i].parent == h) return (HWND) i;
        return 0;
    }
    if (cmd == GW_HWNDPREV) {
        for (i = h - 1; i > 0; i--)
            if (sim_win[i].used && sim_win[i].parent == sim_win[h].parent) return (HWND) i;
        return 0;
    }
    for (i = h + 1; i < SIM_WN; i++)
        if (sim_win[i].used && sim_win[i].parent == sim_win[h].parent) return (HWND) i;
    return 0;
}
static BOOL IsZoomed(HWND h) { (void) h; return FALSE; }
static BOOL IsChild(HWND p, HWND h)
{
    for (h = IsWindow(h) ? sim_win[h].parent : 0; h; h = sim_win[h].parent)
        if (h == p) return TRUE;
    return FALSE;
}
static HWND GetDlgItem(HWND h, int id)
{
    HWND c;
    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (sim_win[c].id == (WORD) id) return c;
    return 0;
}
static BOOL GetWindowRect(HWND h, RECT *r) { *r = sim_win[h].rc; return TRUE; }
static BOOL GetClientRect(HWND h, RECT *r)
{
    r->left = r->top = 0;
    r->right = sim_win[h].rc.right - sim_win[h].rc.left;
    r->bottom = sim_win[h].rc.bottom - sim_win[h].rc.top;
    return TRUE;
}
static void ClientToScreen(HWND h, POINT *p) { p->x += sim_win[h].rc.left; p->y += sim_win[h].rc.top; }
static void ScreenToClient(HWND h, POINT *p) { p->x -= sim_win[h].rc.left; p->y -= sim_win[h].rc.top; }
static int GetWindowText(HWND h, LPSTR s, int n) { lstrcpyn(s, IsWindow(h) ? sim_win[h].text : "", n); return lstrlen(s); }
static int GetWindowTextLength(HWND h) { return IsWindow(h) ? lstrlen(sim_win[h].text) : 0; }
static int GetClassName(HWND h, LPSTR s, int n) { lstrcpyn(s, IsWindow(h) ? sim_win[h].cls : "", n); return lstrlen(s); }
static HWND GetFocus(void) { return sim_focus; }
static HWND GetActiveWindow(void) { return sim_active; }
static HWND GetSysModalWindow(void) { return sim_sysmodal; }
/* fenêtre visible la plus profonde, la dernière créée l'emportant */
static HWND WindowFromPoint(POINT p)
{
    HWND best = 0;
    int i, depth, bestdepth = -1;
    HWND h;
    for (i = 1; i < SIM_WN; i++) {
        RECT r = sim_win[i].rc;
        if (!sim_win[i].used || !IsWindowVisible((HWND) i) ||
            p.x < r.left || p.x >= r.right || p.y < r.top || p.y >= r.bottom)
            continue;
        for (depth = 0, h = (HWND) i; sim_win[h].parent; h = sim_win[h].parent) depth++;
        if (depth >= bestdepth) { best = (HWND) i; bestdepth = depth; }
    }
    return best;
}
static int GetSystemMetrics(int i) { return i == SM_CXSCREEN ? 640 : 480; }

/* ------------------------------------------------------------- rectangles */
static void SetRect(RECT *r, int l, int t, int rr, int b) { r->left = l; r->top = t; r->right = rr; r->bottom = b; }
static void SetRectEmpty(RECT *r) { SetRect(r, 0, 0, 0, 0); }
static void OffsetRect(RECT *r, int dx, int dy) { r->left += dx; r->right += dx; r->top += dy; r->bottom += dy; }
static BOOL IsRectEmpty(const RECT *r) { return r->right <= r->left || r->bottom <= r->top; }
static BOOL PtInRect(const RECT *r, POINT p) { return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom; }
static BOOL EqualRect(const RECT *a, const RECT *b) { return !memcmp(a, b, sizeof(RECT)); }
static BOOL IntersectRect(RECT *d, const RECT *a, const RECT *b)
{
    RECT r;
    r.left = max(a->left, b->left); r.top = max(a->top, b->top);
    r.right = min(a->right, b->right); r.bottom = min(a->bottom, b->bottom);
    if (IsRectEmpty(&r)) { SetRectEmpty(d); return FALSE; }
    *d = r;
    return TRUE;
}

/* ------------------------------------------------------------- contextes */
#define SIM_DN 64
typedef struct {
    BOOL used;
    HWND hwnd;                  /* 0 : bitmap en mémoire */
    BOOL whole;                 /* GetWindowDC : origine au coin de la fenêtre */
    int vx, vy;                 /* origine du viewport */
    COLORREF text, bk;
    int bkmode;
    UINT align;
    BOOL bold;                  /* police grasse (onglet actif de Word) */
} SIMDC;
static SIMDC sim_dc[SIM_DN];
static HDC sim_dc_new(HWND hwnd)
{
    int i;
    for (i = 1; i < SIM_DN; i++)
        if (!sim_dc[i].used) {
            memset(&sim_dc[i], 0, sizeof(sim_dc[i]));
            sim_dc[i].used = TRUE;
            sim_dc[i].hwnd = hwnd;
            sim_dc[i].bkmode = OPAQUE;
            sim_dc[i].bk = RGB(255, 255, 255);
            return (HDC) i;
        }
    abort();
    return 0;
}
static HDC sim_memdc(void) { return sim_dc_new(0); }
static HDC GetDC(HWND h) { return sim_dc_new(h); }
static HDC GetWindowDC(HWND h) { HDC d = sim_dc_new(h); sim_dc[d].whole = TRUE; return d; }
static HDC GetDCEx(HWND h, HRGN r, DWORD f) { (void) r; (void) f; return sim_dc_new(h); }
static RECT sim_paint;          /* zone à repeindre pour le prochain BeginPaint */
static void (*sim_paint_callback)(void);  /* WM_NCPAINT/WM_ERASEBKGND du programme */
static HDC BeginPaint(HWND h, PAINTSTRUCT *ps) {
    ps->hdc = sim_dc_new(h); ps->rcPaint = sim_paint;
    if (sim_paint_callback) sim_paint_callback();
    return ps->hdc;
}
static BOOL DeleteDC(HDC d) { sim_dc[d].used = FALSE; return TRUE; }
static DWORD GetDCOrg(HDC d)
{
    HWND h = sim_dc[d].hwnd;
    if (!h) return 0;
    return MAKELONG(sim_win[h].rc.left, sim_win[h].rc.top);
}
static BOOL LPtoDP(HDC d, POINT *p, int n)
{
    int i;
    for (i = 0; i < n; i++) { p[i].x += sim_dc[d].vx; p[i].y += sim_dc[d].vy; }
    return TRUE;
}
static DWORD GetViewportOrg(HDC d) { return MAKELONG(sim_dc[d].vx, sim_dc[d].vy); }
static DWORD GetWindowOrg(HDC d) { (void) d; return 0; }
static int GetClipBox(HDC d, RECT *r)
{
    HWND h = sim_dc[d].hwnd;
    GetClientRect(h, r);
    OffsetRect(r, -sim_dc[d].vx, -sim_dc[d].vy);
    return 1;
}
static COLORREF GetTextColor(HDC d) { return sim_dc[d].text; }
static COLORREF GetBkColor(HDC d) { return sim_dc[d].bk; }
static int GetBkMode(HDC d) { return sim_dc[d].bkmode; }
static UINT GetTextAlign(HDC d) { return sim_dc[d].align; }
static DWORD GetCurrentPosition(HDC d) { (void) d; return 0; }
static COLORREF GetSysColor(int i)
{
    return i == COLOR_HIGHLIGHT ? RGB(0, 0, 128) : i == COLOR_HIGHLIGHTTEXT ?
           RGB(255, 255, 255) : RGB(0, 0, 0);
}
static DWORD GetTextExtent(HDC d, LPCSTR s, int n) { (void) d; (void) s; return MAKELONG(8 * n, 16); }
static BOOL GetCharWidth(HDC d, UINT a, UINT b, int *w) { UINT c; (void) d; for (c = a; c <= b; c++) w[c - a] = 8; return TRUE; }
static int GetTextFace(HDC d, int n, LPSTR s) { (void) d; lstrcpyn(s, "Sim", n); return 3; }
static BOOL GetTextMetrics(HDC d, TEXTMETRIC *tm)
{
    memset(tm, 0, sizeof(*tm));
    tm->tmHeight = 16; tm->tmAscent = 13; tm->tmDescent = 3;
    tm->tmWeight = sim_dc[d].bold ? 700 : 400;
    return TRUE;
}
static int GetDeviceCaps(HDC d, int i) { (void) d; (void) i; return 96; }

/* fonctions de dessin d'origine (appelées par les remplaçants d'osm.c) : sans effet */
static BOOL ExtTextOut(HDC d, int x, int y, UINT o, const RECT *r, LPCSTR s, UINT n, int *dx)
{ (void) d; (void) x; (void) y; (void) o; (void) r; (void) s; (void) n; (void) dx; return TRUE; }
static void (*sim_gdi_callback)(void);
static BOOL sim_textout_result = TRUE;
static BOOL TextOut(HDC d, int x, int y, LPCSTR s, int n) {
    (void) d; (void) x; (void) y; (void) s; (void) n;
    if (sim_gdi_callback) sim_gdi_callback();
    return sim_textout_result;
}
static BOOL PatBlt(HDC d, int x, int y, int w, int h, DWORD r) { (void) d; (void) x; (void) y; (void) w; (void) h; (void) r; return TRUE; }
static BOOL BitBlt(HDC d, int x, int y, int w, int h, HDC s, int xs, int ys, DWORD r)
{ (void) d; (void) x; (void) y; (void) w; (void) h; (void) s; (void) xs; (void) ys; (void) r; return TRUE; }
static void ScrollWindow(HWND h, int dx, int dy, const RECT *r, const RECT *c) { (void) h; (void) dx; (void) dy; (void) r; (void) c; }
static BOOL ScrollDC(HDC d, int dx, int dy, const RECT *r, const RECT *c, HRGN u, RECT *ur)
{ (void) d; (void) dx; (void) dy; (void) r; (void) c; (void) u; (void) ur; return TRUE; }

/* ------------------------------------------------------------- mémoire, modules */
static HGLOBAL GlobalAlloc(UINT f, DWORD n) { (void) f; return (HGLOBAL) (uintptr_t) calloc(1, n); }
static void *GlobalLock(HGLOBAL h) { return (void *) (uintptr_t) h; }
static int sim_fixed;
static void GlobalFix(HGLOBAL h) { (void) h; sim_fixed++; }
static void GlobalUnfix(HGLOBAL h) { (void) h; sim_fixed--; }
static DWORD GlobalHandle(UINT s) { (void) s; return 1; }
static int sim_alloc_fail, sim_alloc_count, sim_aliases, sim_presto_fail;
static UINT AllocSelector(UINT s) {
    (void) s;
    if (++sim_alloc_count == sim_alloc_fail) return 0;
    sim_aliases++;
    return 1;
}
static UINT PrestoChangoSelector(UINT a, UINT b) { (void) a; return sim_presto_fail ? 0 : b; }
static UINT FreeSelector(UINT s) { (void) s; sim_aliases--; return 0; }
/* tâches : la courante, et une tâche terminée (IsTask faux, pile abandonnée) */
static HTASK sim_task = 1, sim_dead_task;
static HTASK GetCurrentTask(void) { return sim_task; }
static BOOL IsTask(HTASK t) { return t && t != sim_dead_task; }
static HMODULE GetModuleHandle(LPCSTR n) { (void) n; return 1; }
static void sim_proc(void) {}
static FARPROC GetProcAddress(HMODULE m, LPCSTR n) { (void) m; return strcmp(n, "MOUSE_EVENT") ? sim_proc : 0; }

/* ------------------------------------------------------------- messages */
#define SIM_QN 4096
static MSG sim_q[SIM_QN];
static int sim_qn;
static BOOL PostMessage(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (sim_qn >= SIM_QN) abort();
    sim_q[sim_qn].hwnd = h; sim_q[sim_qn].message = m;
    sim_q[sim_qn].wParam = w; sim_q[sim_qn].lParam = l;
    sim_qn++;
    return TRUE;
}
/* retire le premier message posté à h (0 : aucun) */
static BOOL sim_take(HWND h, MSG *m)
{
    int i;
    for (i = 0; i < sim_qn; i++)
        if (sim_q[i].hwnd == h) {
            *m = sim_q[i];
            memmove(sim_q + i, sim_q + i + 1, (sim_qn - i - 1) * sizeof(MSG));
            sim_qn--;
            return TRUE;
        }
    return FALSE;
}
static LRESULT SendMessage(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return IsWindow(h) && sim_win[h].proc ? sim_win[h].proc(h, m, w, l) : 0;
}
static BYTE sim_keys[256];
static int GetKeyState(int vk) { return sim_keys[vk & 255] ? -128 : 0; }
static UINT sim_timer[16];      /* période (0 : arrêtée) */
static DWORD sim_timer_due[16];
static UINT SetTimer(HWND h, UINT id, UINT ms, TIMERPROC p)
{ (void) h; (void) p; sim_timer[id & 15] = ms; sim_timer_due[id & 15] = sim_now + ms; return id; }
static BOOL KillTimer(HWND h, UINT id) { (void) h; sim_timer[id & 15] = 0; return TRUE; }
static POINT sim_cursor, sim_caret;
static BOOL GetCursorPos(POINT *p) { *p = sim_cursor; return TRUE; }
static BOOL SetCursorPos(int x, int y) { sim_cursor.x = x; sim_cursor.y = y; return TRUE; }
static void GetCaretPos(POINT *p) { *p = sim_caret; }
static int sim_scroll_pos, sim_scroll_max;
static int GetScrollPos(HWND h, int b) { (void) h; (void) b; return sim_scroll_pos; }
static void GetScrollRange(HWND h, int b, int *lo, int *hi) { (void) h; (void) b; *lo = 0; *hi = sim_scroll_max; }
/* Menus : sim_menu[m] liste les éléments du menu m (1 à SIM_MN - 1) */
#define SIM_MN 16
static struct { const char *name; HMENU sub; UINT id; } sim_menu[SIM_MN][8];
static int sim_menu_n[SIM_MN];
static int GetMenuItemCount(HMENU m) { return m < SIM_MN ? sim_menu_n[m] : 0; }
static HMENU GetSubMenu(HMENU m, int i) { return m < SIM_MN && i < sim_menu_n[m] ? sim_menu[m][i].sub : 0; }
static int GetMenuString(HMENU m, UINT i, LPSTR s, int n, UINT f)
{
    int k, j;
    lstrcpyn(s, "", n);
    if (f & MF_BYPOSITION) {
        if (m < SIM_MN && (int) i < sim_menu_n[m]) lstrcpyn(s, sim_menu[m][i].name, n);
    } else
        for (k = 0; k < SIM_MN; k++)
            for (j = 0; j < sim_menu_n[k]; j++)
                if (!sim_menu[k][j].sub && sim_menu[k][j].id == i) lstrcpyn(s, sim_menu[k][j].name, n);
    return lstrlen(s);
}
static int MessageBox(HWND h, LPCSTR t, LPCSTR c, UINT f) { (void) h; (void) t; (void) c; (void) f; return 0; }
static BOOL DestroyWindow(HWND h) { (void) h; return TRUE; }
static void PostQuitMessage(int c) { (void) c; }
static LRESULT DefWindowProc(HWND h, UINT m, WPARAM w, LPARAM l) { (void) h; (void) m; (void) w; (void) l; return 0; }
static BOOL RegisterClass(const WNDCLASS *c) { (void) c; return TRUE; }
static HWND CreateWindow(LPCSTR c, LPCSTR t, LONG s, int x, int y, int w, int h, HWND p, HMENU m, HINSTANCE i, void *v)
{ (void) c; (void) t; (void) s; (void) x; (void) y; (void) w; (void) h; (void) p; (void) m; (void) i; (void) v; return 0; }
static BOOL GetMessage(MSG *m, HWND h, UINT a, UINT b) { (void) m; (void) h; (void) a; (void) b; return FALSE; }
static BOOL TranslateMessage(const MSG *m) { (void) m; return TRUE; }
static LRESULT DispatchMessage(const MSG *m) { (void) m; return 0; }
static int GetProfileInt(LPCSTR a, LPCSTR k, int d) { (void) a; (void) k; (void) d; return 3; }
static int GetProfileString(LPCSTR a, LPCSTR k, LPCSTR d, LPSTR o, int n)
{ (void) a; (void) k; lstrcpyn(o, d, n); return lstrlen(o); }

/* ------------------------------------------------------------- ports série */
/* COM1 (id 1) : synthèse ; COM3 (id 3) : plage braille */
typedef struct { BYTE in[4096], out[65536]; int in_n, out_n; } SIMCOM;
static SIMCOM sim_com[4];
static int OpenComm(LPCSTR name, UINT a, UINT b) { (void) a; (void) b; return name[3] - '0'; }
static int CloseComm(int p) { (void) p; return 0; }
static int BuildCommDCB(LPCSTR c, DCB *d) { (void) c; memset(d, 0, sizeof(*d)); return 0; }
static int SetCommState(const DCB *d) { (void) d; return 0; }
static int GetCommError(int p, COMSTAT *s) { (void) p; s->cbOutQue = 0; return 0; }
static int FlushComm(int p, int q) { (void) p; (void) q; return 0; }
static int ReadComm(int p, LPSTR b, int n)
{
    SIMCOM *c = &sim_com[p & 3];
    if (n > c->in_n) n = c->in_n;
    memcpy(b, c->in, n);
    memmove(c->in, c->in + n, c->in_n - n);
    c->in_n -= n;
    return n;
}
static int WriteComm(int p, LPCSTR b, int n)
{
    SIMCOM *c = &sim_com[p & 3];
    if (c->out_n + n > (int) sizeof(c->out)) { memmove(c->out, c->out + 32768, c->out_n - 32768); c->out_n -= 32768; }
    memcpy(c->out + c->out_n, b, n);
    c->out_n += n;
    return n;
}
#endif
