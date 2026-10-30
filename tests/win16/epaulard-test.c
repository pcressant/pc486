/* Banc d'Épaulard complet : vrais epaulard.c, osm.c et braille.c sur la simulation
   Win16, face à une fausse Apollo II (COM1, 15 caractères par seconde, échos @E), une
   fausse plage Baum 40 cellules (COM3) et de fausses applications : Write (texte
   dessiné qui défile sous un curseur immobile), Bloc-notes (EDIT standard) et le
   Gestionnaire de programmes (icônes en surbrillance, sans curseur texte).
   Le temps est simulé par pas de 10 ms. */
#define WIN16_FULL
#include "../../epaulard/src/osm.c"
#include "../../epaulard/src/braille.c"
#include "../../epaulard/src/page.c"
#include "epaulard-modules.h"
#include "navweb-essai.h"

HWND ep_hwnd_main;
BOOL ep_in_menu;
BOOL FAR PASCAL EpInstall(HWND h, int c) { (void) c; ep_hwnd_main = h; return TRUE; }
void FAR PASCAL EpRemove(void) {}
static BOOL cfg_mode_sim;       /* clavier donné au menu des réglages (EPAUHOOK.DLL) */
void FAR PASCAL EpCfgMode(BOOL on) { cfg_mode_sim = on; }
static BOOL pass_keys_sim;      /* touches laissées à la machine DOS (EPAUHOOK.DLL) */
void FAR PASCAL EpPassKeys(BOOL on) { pass_keys_sim = on; }
static int sim_br_epoch;
int FAR PASCAL EpBrailleEpoch(void) { return sim_br_epoch; }
static HWND sim_nav_view;       /* mode navigation posé dans EPAUHOOK.DLL */
static int sim_nav_mode;
void FAR PASCAL EpNavMode(HWND v, int m) { sim_nav_view = v; sim_nav_mode = v ? m : 0; }

static int failures;
#define CHECK(c) do { if (!(c)) { printf("ÉCHEC %s:%d : %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b))) { printf("ÉCHEC %s:%d : « %s » au lieu de « %s »\n", __FILE__, __LINE__, (a), (b)); failures++; } } while (0)

static void clear(HDC hdc, int l, int t, int r, int b)
{
    RECT rc;
    SetRect(&rc, l, t, r, b);
    EpExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rc, "", 0, NULL);
}

static int line_index_np(const char *t);

/* ------------------------------------------------------- fausse Apollo II */
#define AP_N 512
static struct { char t[1200]; BOOL echo; } ap_q[AP_N];
static int ap_n, ap_parsed;
static char ap_cur[1200];
static int ap_curn;
static DWORD ap_busy_until;     /* fin de l'énoncé en cours */
static char spoken[400][1200];   /* énoncés commencés, dans l'ordre */
static int spoken_n;
static DWORD idle_ms;           /* dire tout en cours, plus rien à dire, puis reprise */
static DWORD idle_run;          /* silence en cours (compté s'il est suivi d'un énoncé) */

static void ap_reset(void)
{
    ap_n = ap_curn = 0;
    ap_parsed = sim_com[1].out_n;
    spoken_n = 0;
    idle_ms = idle_run = 0;
    ap_busy_until = sim_now;
}

static void ap_step(void)
{
    SIMCOM *c = &sim_com[1];
    while (ap_parsed < c->out_n) {
        char b = (char) c->out[ap_parsed++];
        if (b == 0x18) {                /* Ctrl-X : silence, file effacée */
            ap_n = ap_curn = 0;
            ap_busy_until = sim_now;
        } else if (b == '\r') {
            ap_cur[ap_curn] = 0;
            if (ap_n >= AP_N) abort();
            ap_q[ap_n].echo = ap_curn == 3 && ap_cur[0] == '@' && ap_cur[1] == 'E';
            strcpy(ap_q[ap_n].t, ap_q[ap_n].echo ? ap_cur + 2 : ap_cur);
            if (ap_q[ap_n].echo || ap_cur[0] != '@')     /* réglages (@W4@F9...) : muets */
                ap_n++;
            ap_curn = 0;
        } else if (ap_curn < 1199)
            ap_cur[ap_curn++] = b;
    }
    while (sim_now >= ap_busy_until && ap_n) {
        if (ap_q[0].echo) {             /* lecture arrivée à l'écho : le renvoyer */
            c = &sim_com[1];
            c->in[c->in_n++] = (BYTE) ap_q[0].t[0];
        } else {
            char *t = ap_q[0].t;
            int k = (int) strlen(t);
            while (k && t[k - 1] == ' ') t[--k] = 0;
            if (spoken_n < 400) strcpy(spoken[spoken_n++], t);
            idle_ms += idle_run;
            idle_run = 0;
            ap_busy_until = sim_now + (DWORD) (strlen(ap_q[0].t) * 1000 / 15) + 10;
        }
        memmove(ap_q, ap_q + 1, (ap_n - 1) * sizeof(ap_q[0]));
        ap_n--;
    }
    if (sa.on && spoken_n && sim_now >= ap_busy_until && !ap_n)
        idle_run += 10;
    else if (!sa.on)
        idle_run = 0;
}

static void dump(const char *what)
{
    int i;
    printf("--- %s : %d énoncés, pause %lu ms\n", what, spoken_n, (unsigned long) idle_ms);
    for (i = 0; i < spoken_n; i++)
        printf("  %d « %s »\n", i, spoken[i]);
}

static int spoken_since(int from, const char *t)
{
    int i;
    for (i = from; i < spoken_n; i++)
        if (!strcmp(spoken[i], t)) return i;
    return -1;
}

/* ------------------------------------------------------- fausse plage Baum */
static char br_shown[64];       /* texte affiché (cellule marquée : lettre en minuscule) */
static char br_mask[64];        /* « # » : points 7 et 8 */
static BYTE br_raw[40];

static void baum_step(void)
{
    SIMCOM *c = &sim_com[3];
    int i = 0, k;
    while (i + 1 < c->out_n) {
        if (c->out[i] != 0x1b) { i++; continue; }
        if (c->out[i + 1] == 0x15) {    /* activation : 40 cellules */
            c->in[c->in_n++] = 0x1b; c->in[c->in_n++] = 1; c->in[c->in_n++] = 40;
            i += 3;
        } else if (c->out[i + 1] == 1) {
            int j = i + 2, n = 0;
            while (n < 40 && j < c->out_n) {
                if (c->out[j] == 0x1b) j++;
                br_raw[n++] = c->out[j++];
            }
            if (n < 40) break;
            for (k = 0; k < 40; k++) {
                int ch = 0, m;
                BOOL marked = (br_raw[k] & 0xc0) == 0xc0;
                for (m = 32; m < 127 && !marked; m++)
                    if (br_fr[m] == br_raw[k]) { ch = m; break; }
                if (marked)             /* majuscule ou minuscule : même cellule */
                    for (m = 'a'; m <= 'z'; m++)
                        if ((br_fr[m] | 0xc0) == br_raw[k] || (br_fr[m - 32] | 0xc0) == br_raw[k]) { ch = m; break; }
                if (!ch) ch = br_raw[k] & 0x3f ? '?' : ' ';
                br_shown[k] = (char) ch;
                br_mask[k] = marked ? '#' : ' ';
            }
            br_mask[40] = 0;
            br_shown[40] = 0;
            i = j;
        } else
            i += 2;
    }
    memmove(c->out, c->out + i, c->out_n - i);
    c->out_n -= i;
}

/* ------------------------------------------------------- fausses applications */
#define V_ROWS 8                /* rangées visibles de Write */
static struct {
    HWND top, doc;
    HDC dc;
    const char *lines[200];
    int n, top_line, caret, col;
} wr;

static void wr_row(int r)
{
    int l = wr.top_line + r;
    clear(wr.dc, 0, r * 16, 600, r * 16 + 16);
    if (l < wr.n && wr.lines[l][0])
        EpExtTextOut(wr.dc, 0, r * 16, 0, NULL, wr.lines[l], (UINT) strlen(wr.lines[l]), NULL);
}

static void wr_caret(void)
{
    EpPatBlt(wr.dc, wr.col * 8, (wr.caret - wr.top_line) * 16, 1, 16, DSTINVERT);
}

static LRESULT wr_key(UINT vk)
{
    if (vk == VK_DOWN && wr.caret < wr.n - 1) {
        wr.caret++;
        if (wr.caret - wr.top_line >= V_ROWS) {   /* défile, le curseur reste en bas */
            EpScrollWindow(wr.doc, 0, -16, NULL, NULL);
            wr.top_line++;
            wr_row(V_ROWS - 1);
        }
        if (wr.col > (int) strlen(wr.lines[wr.caret])) wr.col = (int) strlen(wr.lines[wr.caret]);
        wr_caret();
    } else if (vk == VK_UP && wr.caret > 0) {
        wr.caret--;
        if (wr.caret < wr.top_line) {
            EpScrollWindow(wr.doc, 0, 16, NULL, NULL);
            wr.top_line--;
            wr_row(0);
        }
        if (wr.col > (int) strlen(wr.lines[wr.caret])) wr.col = (int) strlen(wr.lines[wr.caret]);
        wr_caret();
    } else if (vk == VK_HOME) {
        wr.col = 0;
        wr_caret();
    } else if (vk == VK_RIGHT && wr.col < (int) strlen(wr.lines[wr.caret])) {
        wr.col++;
        wr_caret();
    }
    return 0;
}

static void wr_open(const char **lines, int n)
{
    int r;
    memset(&wr, 0, sizeof(wr));
    wr.top = sim_create(0, "MSWRITE_MENU", "Write - LISEZMOI.WRI", 0, 0, 0, 640, 480);
    wr.doc = sim_create(wr.top, "MSWRITE_DOC", "", 0, 10, 40, 610, 40 + 16 * V_ROWS);
    wr.dc = EpGetDC(wr.doc);
    memcpy(wr.lines, lines, n * sizeof(char *));
    wr.n = n;
    for (r = 0; r < V_ROWS; r++)
        wr_row(r);
    wr_caret();
    sim_active = wr.top;
    sim_focus = wr.doc;
}

/* Bloc-notes : contrôle EDIT multiligne */
static struct {
    HWND top, edit;
    char text[8000];
    int sel_a, sel_b;
} np;

static int np_line_start(int line)
{
    int i = 0, l = 0;
    while (l < line && np.text[i]) { if (np.text[i] == '\n') l++; i++; }
    return l == line ? i : -1;
}
static int np_line_of(int pos)
{
    int i, l = 0;
    for (i = 0; i < pos && np.text[i]; i++) if (np.text[i] == '\n') l++;
    return l;
}
static LRESULT np_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    int i, n, start;
    (void) h;
    switch (m) {
    case EM_GETLINECOUNT:
        for (i = 0, n = 1; np.text[i]; i++) if (np.text[i] == '\n') n++;
        return n;
    case EM_LINEINDEX:
        return np_line_start((int) w == -1 ? np_line_of(np.sel_a) : (int) w);
    case EM_LINEFROMCHAR:
        return np_line_of((int) w == -1 ? np.sel_a : (int) w);
    case EM_GETLINE:
        start = np_line_start((int) w);
        if (start < 0) return 0;
        for (n = 0; np.text[start + n] && np.text[start + n] != '\n'; n++)
            ((char *) l)[n] = np.text[start + n];
        return n;
    case EM_GETSEL:
        return MAKELONG(np.sel_a, np.sel_b);
    case EM_SETSEL:
        np.sel_a = LOWORD(l);
        np.sel_b = HIWORD(l);
        return 0;
    }
    return 0;
}
static LRESULT np_key(UINT vk)
{
    int line = np_line_of(np.sel_a), col = np.sel_a - np_line_start(line), s;
    if (vk == VK_DOWN || vk == VK_UP) {
        s = np_line_start(line + (vk == VK_DOWN ? 1 : -1));
        if (line + (vk == VK_DOWN ? 1 : -1) >= 0 && s >= 0) np.sel_a = np.sel_b = s + col;
    }
    return 0;
}
static void np_open(const char *text)
{
    memset(&np, 0, sizeof(np));
    strcpy(np.text, text);
    np.top = sim_create(0, "Notepad", "Bloc-notes - ESSAI.TXT", 0, 0, 0, 640, 480);
    np.edit = sim_create(np.top, "Edit", "", ES_MULTILINE, 0, 20, 640, 480);
    sim_win[np.edit].proc = np_proc;
    sim_active = np.top;
    sim_focus = np.edit;
}

/* Gestionnaire de programmes : grille d'icônes 3 × 2, sélection en surbrillance */
static struct {
    HWND top, grp;
    HDC dc;
    int sel;
} pm;
/* Préférences de Netscape : la zone de saisie dessine sa valeur */
static void netscape_paint(HWND h, const RECT *r)
{
    (void) r;
    EpExtTextOut(EpGetDC(h), 2, 2, 0, NULL, "600", 3, NULL);
}

static const char *pm_names[6] = { "Gestionnaire de fichiers", "Panneau de configuration",
    "Lisezmoi", "Windows Installation", "PIFedit", "Commandes MS-DOS" };

static void pm_draw(int i, BOOL hi)
{
    sim_dc[pm.dc].bk = hi ? GetSysColor(COLOR_HIGHLIGHT) : RGB(255, 255, 255);
    EpExtTextOut(pm.dc, (i % 3) * 210, (i / 3) * 70 + 40, 0, NULL, pm_names[i],
                 (UINT) strlen(pm_names[i]), NULL);
    sim_dc[pm.dc].bk = RGB(255, 255, 255);
}
static LRESULT pm_key(UINT vk)
{
    int next = pm.sel;
    if (vk == VK_RIGHT && pm.sel % 3 < 2) next++;
    if (vk == VK_LEFT && pm.sel % 3) next--;
    if (vk == VK_DOWN && pm.sel < 3) next += 3;
    if (vk == VK_UP && pm.sel >= 3) next -= 3;
    if (next != pm.sel) {
        pm_draw(pm.sel, FALSE);
        pm.sel = next;
        pm_draw(pm.sel, TRUE);
    }
    return 0;
}
static void pm_open(void)
{
    int i;
    memset(&pm, 0, sizeof(pm));
    pm.top = sim_create(0, "Progman", "Gestionnaire de programmes", 0, 0, 0, 640, 480);
    pm.grp = sim_create(pm.top, "PMGroup", "Groupe principal", 0, 0, 110, 640, 290);
    pm.dc = EpGetDC(pm.grp);
    for (i = 0; i < 6; i++)
        pm_draw(i, i == 0);
    sim_active = pm.top;
    sim_focus = pm.grp;
    sim_caret.x = 3;                    /* curseur système resté d'une autre fenêtre */
    sim_caret.y = 200;
}

/* Gestionnaire de fichiers : liste LISTBOX dessinée par le programme (sans
   LBS_HASSTRINGS), élément courant en surbrillance entouré d'un rectangle de focus */
static struct {
    HWND top, list;
    HDC dc;
    int sel;
} wf;
static const char *wf_names[4] = { "autoexec.bat", "command.com", "config.sys", "defrag.exe" };

static void wf_draw(int i, BOOL hi)
{
    sim_dc[wf.dc].bk = hi ? GetSysColor(COLOR_HIGHLIGHT) : RGB(255, 255, 255);
    EpExtTextOut(wf.dc, 2, i * 16, 0, NULL, wf_names[i], (UINT) strlen(wf_names[i]), NULL);
    sim_dc[wf.dc].bk = RGB(255, 255, 255);
    if (hi) {                           /* DrawFocusRect : traits inversés */
        EpPatBlt(wf.dc, 0, i * 16, 200, 1, PATINVERT);
        EpPatBlt(wf.dc, 0, i * 16 + 15, 200, 1, PATINVERT);
    }
}
static LRESULT wf_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void) h;
    switch (m) {
    case LB_GETCOUNT:
        return 4;
    case LB_GETCURSEL:
    case LB_GETCARETINDEX:
        return wf.sel;
    case LB_GETITEMRECT:
        SetRect((RECT *) l, 0, (int) w * 16, 200, (int) w * 16 + 16);
        return 0;
    }
    return 0;
}
static LRESULT wf_key(UINT vk)
{
    int next = wf.sel;
    if (vk == VK_DOWN && wf.sel < 3) next++;
    if (vk == VK_UP && wf.sel > 0) next--;
    if (next != wf.sel) {
        wf_draw(wf.sel, FALSE);
        wf.sel = next;
        wf_draw(wf.sel, TRUE);
    }
    return 0;
}
static void wf_open(void)
{
    int i;
    memset(&wf, 0, sizeof(wf));
    wf.top = sim_create(0, "WFS_Frame", "Gestionnaire de fichiers", 0, 0, 0, 640, 480);
    wf.list = sim_create(wf.top, "ListBox", "", LBS_OWNERDRAWFIXED, 20, 60, 220, 124);
    sim_win[wf.list].proc = wf_proc;
    wf.dc = EpGetDC(wf.list);
    for (i = 0; i < 4; i++)
        wf_draw(i, i == 0);
    sim_active = wf.top;
    sim_focus = wf.list;
}

/* ------------------------------------------------------- boucle du système */
/* Aide de Windows : focus sur MS_WINDOC, texte dans MS_WINTOPIC (6 rangées
   visibles) ; Page suivante avance de 5 rangées (une de recouvrement), Tab inverse le
   lien suivant. */
#define H_ROWS 6
static struct {
    HWND doc, topic;
    HDC dc;
    const char *lines[40];
    int n, top, link;
} hp;

static void hp_draw(void)
{
    int r, l;
    for (r = 0; r < H_ROWS; r++) {
        l = hp.top + r;
        clear(hp.dc, 0, r * 16, 400, r * 16 + 16);
        if (l < hp.n && hp.lines[l][0])
            EpExtTextOut(hp.dc, 0, r * 16, 0, NULL, hp.lines[l], (UINT) strlen(hp.lines[l]), NULL);
    }
}

static LRESULT hp_key(UINT vk)
{
    int max = hp.n > H_ROWS ? hp.n - H_ROWS : 0, old = hp.top;
    if (vk == VK_NEXT) hp.top = hp.top + H_ROWS - 1 > max ? max : hp.top + H_ROWS - 1;
    if (vk == VK_DOWN && hp.top < max) hp.top++;
    if (vk == VK_TAB) {                 /* liens : rangées 2 et 3 */
        hp.link = hp.link == 2 ? 3 : 2;
        hp_draw();
        EpPatBlt(hp.dc, 0, (hp.link - hp.top) * 16, 8 * (int) strlen(hp.lines[hp.link]), 16, DSTINVERT);
    }
    if (hp.top != old) hp_draw();
    return 0;
}


/* Éditeur de cellule d'Excel (F2) : formule dessinée une fois, puis seuls les trois
   caractères autour de l'ancien et du nouveau curseur sont redessinés */
static struct {
    HWND top, ed;
    HDC dc;
    const char *t;
    int col;
} xe;
static void xe_piece(int col)
{
    int a = col > 1 ? col - 1 : 0, n = (int) strlen(xe.t);
    int b = a + 3 < n ? a + 3 : n;
    if (b > a)
        EpExtTextOut(xe.dc, a * 8, 0, 0, NULL, xe.t + a, (UINT) (b - a), NULL);
}
static BOOL xe_alnum(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static LRESULT xe_key(UINT vk)
{
    int n = (int) strlen(xe.t), old = xe.col;
    if (vk == VK_LEFT && xe.col > 0) {
        if (sim_keys[VK_CONTROL]) {     /* début du mot précédent (ponctuation sautée) */
            for (xe.col--; xe.col > 0 && !xe_alnum(xe.t[xe.col]); xe.col--)
                ;
            for (; xe.col > 0 && xe_alnum(xe.t[xe.col - 1]); xe.col--)
                ;
        } else
            xe.col--;
    }
    if (vk == VK_RIGHT && xe.col < n) xe.col++;
    if (vk == VK_HOME) xe.col = 0;
    if (vk == VK_END) xe.col = n;
    xe_piece(old);
    xe_piece(xe.col);
    EpPatBlt(xe.dc, xe.col * 8, 0, 1, 16, DSTINVERT);
    return 0;
}

/* Installateur de QEMM 8 : zone d'une ligne dont WM_GETTEXT reste vide (le texte
   ne se lit que par EM_GETLINE) ; « Unité du disque : » */
static struct {
    HWND edit;
    char text[40];
    int pos;
} qm;
static LRESULT qm_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void) h; (void) w;
    switch (m) {
    case EM_GETLINE:
        strcpy((char *) l, qm.text);
        return (LRESULT) strlen(qm.text);
    case EM_GETSEL:
        return MAKELONG(qm.pos, qm.pos);
    }
    return 0;
}
static LRESULT qm_key(UINT vk)
{
    int n = (int) strlen(qm.text);
    if (vk == VK_LEFT && qm.pos > 0) qm.pos--;
    else if (vk == VK_RIGHT && qm.pos < n) qm.pos++;
    else if (vk == VK_HOME) qm.pos = 0;
    else if (vk == VK_END) qm.pos = n;
    return 0;
}

static LRESULT app_key(HWND h, UINT vk)
{
    if (h == qm.edit && qm.edit) return qm_key(vk);
    if (h == xe.ed && xe.ed) return xe_key(vk);
    if (h == hp.doc && hp.doc) return hp_key(vk);
    if (h == wr.doc && wr.doc) return wr_key(vk);
    if (h == np.edit && np.edit) return np_key(vk);
    if (h == pm.grp && pm.grp) return pm_key(vk);
    if (h == wf.list && wf.list) return wf_key(vk);
    return 0;
}

/* Champs d'une page de Netscape (fenêtres filles de la vue) : état des cases, longueur
   du texte (WM_GETTEXT reste vide, comme chez Netscape), liste « Paris, Lyon, Marseille » */
static int nsf_check[SIM_WN];
static char nsf_value[SIM_WN][40];
static HWND nsf_clicked;
static LRESULT nsf_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    int i, st = (int) (sim_win[h].style & 0x0F);
    switch (m) {
    case BM_GETCHECK: return nsf_check[h];
    case WM_GETTEXTLENGTH: return (LRESULT) strlen(nsf_value[h]);
    case CB_GETCURSEL: return 1;
    case CB_GETLBTEXTLEN: return 4;
    case CB_GETLBTEXT:                  /* sans CBS_HASSTRINGS : donnée de l'élément */
        if ((sim_win[h].style & CBS_OWNERDRAWFIXED) && !(sim_win[h].style & CBS_HASSTRINGS)) {
            strcpy((char *) l, "\x8a" "k&");
            return 4;
        }
        strcpy((char *) l, "Lyon"); return 4;
    case WM_KEYUP:                      /* Espace relâché : clic */
        if (w != VK_SPACE || !is_class(h, "BUTTON"))
            return 0;
        if (st == BS_AUTOCHECKBOX)
            nsf_check[h] = !nsf_check[h];
        else if (st == BS_RADIOBUTTON) {
            for (i = 1; i < SIM_WN; i++)
                if (sim_win[i].used && sim_win[i].proc == nsf_proc &&
                    sim_win[i].parent == sim_win[h].parent && (sim_win[i].style & 0x0F) == BS_RADIOBUTTON)
                    nsf_check[i] = 0;
            nsf_check[h] = 1;
        } else
            nsf_clicked = h;
        return 0;
    }
    return 0;
}

/* champ de Netscape repeint : son texte entier */
static void nsf_update(HWND h, const RECT *r)
{
    HDC dc;
    (void) r;
    if (sim_win[h].proc != nsf_proc || !nsf_value[h][0])
        return;
    dc = GetDC(h);
    EpExtTextOut(dc, 2, 2, 0, NULL, nsf_value[h], (UINT) strlen(nsf_value[h]), NULL);
    DeleteDC(dc);
}

/* l'application qui a le focus lit ses touches ; le crochet clavier signale celles du
   clavier, pas celles qu'un programme a postées (dire tout d'Épaulard) */
static void app_keys(void)
{
    MSG m;
    int k;
    for (k = 0; k < 64 && sim_focus && sim_take(sim_focus, &m); k++) {
        if (m.message == WM_KEYDOWN) {
            app_key(m.hwnd, m.wParam);
            if (m.time)
                PostMessage(hwnd_main, EPM_KEY, m.wParam, m.lParam |     /* comme EPAUHOOK.DLL */
                        (sim_keys[VK_CONTROL] ? EPK_CTRL : 0) | (sim_keys[VK_SHIFT] ? EPK_SHIFT : 0));
        }
        if (IsWindow(m.hwnd) && sim_win[m.hwnd].proc == nsf_proc)
            nsf_proc(m.hwnd, m.message, m.wParam, m.lParam);
    }
}

static void step(void)
{
    MSG m;
    int i;
    sim_now += 10;
    app_keys();
    while (sim_take(hwnd_main, &m))
        MainProc(hwnd_main, m.message, m.wParam, m.lParam);
    for (i = 1; i < 16; i++)
        if (sim_timer[i] && sim_now >= sim_timer_due[i]) {
            sim_timer_due[i] = sim_now + sim_timer[i];
            MainProc(hwnd_main, WM_TIMER, i, 0);
        }
    ap_step();
    baum_step();
}

static void run(DWORD ms)
{
    DWORD end = sim_now + ms;
    while (sim_now < end) step();
}

/* touche de l'utilisateur, envoyée à l'application qui a le focus */
static void key(UINT vk)
{
    sim_hw = TRUE;                      /* touche de l'utilisateur : vue par le crochet */
    PostMessage(sim_focus, WM_KEYDOWN, vk, MAKELPARAM(1, 0x100));
    PostMessage(sim_focus, WM_KEYUP, vk, MAKELPARAM(1, 0xC100));
    sim_hw = FALSE;
}
static void insert_cmd(UINT vk) { PostMessage(hwnd_main, EPM_CMD, vk, 0); }
static void ctrl_alone(void) { PostMessage(hwnd_main, EPM_SILENCE, 0, 0); }

static BOOL keep_ini;            /* redémarrage : garder EPAULARD.INI */
static void boot(void)
{
    static const int cfg_default[CFG_N] = { 3, 8, 1, 1, 0, 0 };
    if (!keep_ini)
        sim_ini_n = 0;
    keep_ini = FALSE;
    memcpy(cfg, cfg_default, sizeof(cfg));
    cfg_on = FALSE;
    cfg_mode_sim = FALSE;
    menu_pending[0] = 0;
    typed_n = 0;
    tab_waiting = FALSE;
    memset(sim_menu_n, 0, sizeof(sim_menu_n));
    memset(sim_win, 0, sizeof(sim_win));
    memset(sim_dc, 0, sizeof(sim_dc));
    memset(sim_com, 0, sizeof(sim_com));
    memset(&wr, 0, sizeof(wr));
    memset(&np, 0, sizeof(np));
    memset(&pm, 0, sizeof(pm));
    memset(&wf, 0, sizeof(wf));
    memset(&hp, 0, sizeof(hp));
    memset(&qm, 0, sizeof(qm));
    sim_focus = sim_active = 0;
    sim_qn = 0;
    osm_remove();
    osm_install();
    hwnd_main = sim_create(0, "Epaulard", "Épaulard", 0, 0, 0, 0, 0);
    sim_win[hwnd_main].style &= ~WS_VISIBLE;
    com_open();
    cfg_load(0);
    voice_apply();
    memset(&nav, 0, sizeof(nav));
    nav_init();
    EpInstall(hwnd_main, com);
    br_open();
    SetTimer(hwnd_main, TIMER_ID, POLL_MS, NULL);
    ap_reset();
    run(300);
}

/* ------------------------------------------------------- scénarios */

/* Document du type LISEZMOI.WRI : paragraphes séparés par une à cinq lignes vides,
   dont plusieurs au bas de l'écran, là où le texte défile sous le curseur. */
static const char *doc_lines[] = {
    "Complement d'informations sur Windows", "pour Workgroups version 3.11", "",
    "Ce document contient des informations", "importantes qui ne figurent pas.", "", "",
    "Remarque : si vous avez l'intention", "d'utiliser un programme resident,", "", "", "",
    "Utilisation de Write", "", "", "", "", "",
    "Pour parcourir le document,", "appuyez sur HAUT et BAS.", "",
    "Sommaire", "", "1.0 Execution de Windows", "2.0 Prevention de la pile", "", "", "",
    "3.0 Acces disque 32 bits", "", "", "", "", "Fin du document.", ""
};
#define DOC_N ((int) (sizeof(doc_lines) / sizeof(doc_lines[0])))

static void test_write_say_all(void)
{
    int i, at = 0, expected = 0;

    boot();
    wr_open(doc_lines, DOC_N);
    run(500);
    ap_reset();
    insert_cmd(VK_DOWN);
    run(200000);
    /* toutes les lignes non vides, dans l'ordre, une seule fois */
    for (i = 0; i < DOC_N; i++) {
        if (!doc_lines[i][0]) continue;
        expected++;
        if (at < spoken_n && !strcmp(spoken[at], doc_lines[i]))
            at++;
        else
            printf("ÉCHEC dire tout Write : ligne %d « %s » non lue à sa place (lu : « %s »)\n",
                   i, doc_lines[i], at < spoken_n ? spoken[at] : "(rien)"), failures++;
    }
    CHECK(spoken_n == expected);
    CHECK(!sa.on);                      /* arrêt en fin de document */
    CHECK(idle_ms == 0);                /* aucune pause : la file n'a jamais été vide */
    CHECK(wr.caret >= DOC_N - 2);
}

/* Interruption : Ctrl ramène le curseur sur la ligne en cours de lecture, sans
   l'annoncer ; Bas pendant la lecture va à la ligne qui la suit et l'annonce. */
static int line_index(const char *t)
{
    int i;
    for (i = 0; i < DOC_N; i++) if (!strcmp(doc_lines[i], t)) return i;
    return -1;
}

static void test_write_interrupt(void)
{
    int reading, before, i;

    boot();
    wr_open(doc_lines, DOC_N);
    run(500);
    ap_reset();
    insert_cmd(VK_DOWN);
    run(16000);
    CHECK(sa.on);
    reading = line_index(spoken[spoken_n - 1]);
    CHECK(reading > 0);
    CHECK(wr.caret > reading);          /* le curseur précède la parole */
    before = spoken_n;
    ctrl_alone();
    run(2000);
    CHECK(!sa.on);
    CHECK(wr.caret == reading);
    CHECK(spoken_n == before);          /* rien d'annoncé après Ctrl */

    /* nouvelle lecture depuis le début ; appuyer sur Bas quand le curseur a de
       l'avance sur la parole (cas à corriger) */
    boot();
    wr_open(doc_lines, DOC_N);
    run(500);
    ap_reset();
    insert_cmd(VK_DOWN);
    run(4000);
    for (i = 0; i < 100 && wr.caret <= line_index(spoken[spoken_n - 1]); i++)
        run(100);
    reading = line_index(spoken[spoken_n - 1]);
    CHECK(wr.caret > reading);
    before = spoken_n;
    key(VK_DOWN);
    run(2000);
    CHECK(!sa.on);
    CHECK(wr.caret == reading + 1);
    CHECK(spoken_n == before + 1);
    if (spoken_n == before + 1)
        CHECK_STR(spoken[before], doc_lines[reading + 1][0] ? doc_lines[reading + 1] : "vide");
}

/* Fin du document : le curseur est en bas, la parole lit encore les lignes envoyées
   d'avance. Haut y ramène le curseur et annonce la ligne qui précède celle lue
   (régression : les descentes du dire tout, attendues en retour du crochet clavier
   qui ne les voit pas, faisaient avaler cette flèche). */
static void test_write_up_at_end(void)
{
    int reading, before, i;

    boot();
    wr_open(doc_lines, DOC_N);
    run(500);
    ap_reset();
    insert_cmd(VK_DOWN);
    run(1000);
    for (i = 0; i < 2000 && sa.on && !sa.eof; i++)
        run(100);
    CHECK(sa.on && sa.eof);             /* curseur en fin, lecture en cours */
    reading = line_index(spoken[spoken_n - 1]);
    CHECK(reading > 0 && wr.caret > reading);
    before = spoken_n;
    key(VK_UP);
    run(2000);
    CHECK(!sa.on);
    CHECK(wr.caret == reading - 1);
    CHECK(spoken_n == before + 1);
    if (spoken_n == before + 1)
        CHECK_STR(spoken[before], doc_lines[reading - 1][0] ? doc_lines[reading - 1] : "vide");
}

/* Bloc-notes : le curseur suit la ligne lue ; arrêt à la fin */
static void test_notepad_say_all(void)
{
    boot();
    cfg[CFG_PUNCT] = 0;                 /* « DOS=HIGH » tel quel, comparé au texte */
    np_open("[MENU]\nMENUITEM=DOS\n\n\n[COMMON]\nFILES=40\n\n\n\n\nDOS=HIGH,UMB\n[WIN]");
    run(500);
    ap_reset();
    insert_cmd(VK_DOWN);
    run(1500);
    CHECK(sa.on);
    CHECK(np_line_of(np.sel_a) == line_index_np(spoken[spoken_n - 1]));
    run(60000);
    CHECK(!sa.on);
    CHECK(spoken_n == 6);
    CHECK(spoken_since(0, "DOS=HIGH,UMB") == 4);
    CHECK(spoken_since(0, "[WIN]") == 5);
    CHECK(idle_ms == 0);
}

/* Gestionnaire de programmes : l'icône seule, jamais « vide » */
static void test_progman_icons(void)
{
    int i;
    boot();
    pm_open();
    run(1500);
    ap_reset();
    key(VK_RIGHT); run(1500);
    key(VK_DOWN); run(1500);
    key(VK_DOWN); run(1500);        /* dernière rangée : ne bouge pas */
    key(VK_LEFT); run(1500);
    insert_cmd(VK_UP); run(1500);    /* relire : l'élément en surbrillance */
    for (i = 0; i < spoken_n; i++)
        if (!strcmp(spoken[i], "vide"))
            printf("ÉCHEC icônes : « vide » annoncé (énoncé %d)\n", i), failures++;
    CHECK(spoken_n == 4);
    if (spoken_n == 4) {
        CHECK_STR(spoken[0], "Panneau de configuration");
        CHECK_STR(spoken[1], "PIFedit");
        CHECK_STR(spoken[2], "Windows Installation");
        CHECK_STR(spoken[3], "Windows Installation");
    }
}

/* Gestionnaire de fichiers : chaque élément dit une seule fois. Le rectangle de focus
   d'une liste standard ne doit pas relancer la lecture prévue pour les boîtes de Word
   (« defrag.exe », coupé, puis « defrag.exe bouton »). */
static void test_winfile_list(void)
{
    boot();
    wf_open();
    run(1500);
    ap_reset();
    key(VK_DOWN); run(1500);
    key(VK_DOWN); run(1500);
    key(VK_UP); run(1500);
    CHECK(spoken_n == 3);
    if (spoken_n == 3) {
        CHECK_STR(spoken[0], "command.com");
        CHECK_STR(spoken[1], "config.sys");
        CHECK_STR(spoken[2], "command.com");
    }
}

/* Menus : Droite depuis un menu ouvert. Windows resélectionne l'ancien titre, puis
   le nouveau, puis son premier élément : seuls les deux derniers sont dits. */
static void menu_select(UINT item, UINT flags, HMENU m) { PostMessage(hwnd_main, EPM_MENU, item, MAKELPARAM(flags, m)); }
static void test_menus(void)
{
    boot();
    np_open("texte");
    run(1000);
    sim_menu_n[1] = 2;                  /* barre : Fichier (menu 2), Edition (menu 3) */
    sim_menu[1][0].name = "&Fichier"; sim_menu[1][0].sub = 2;
    sim_menu[1][1].name = "&Edition"; sim_menu[1][1].sub = 3;
    sim_menu_n[2] = 1;
    sim_menu[2][0].name = "&Nouveau...\tCtrl+N"; sim_menu[2][0].id = 101;
    sim_menu_n[3] = 1;
    sim_menu[3][0].name = "&Annuler"; sim_menu[3][0].id = 201;
    run(3000);
    ap_reset();
    menu_select(2, MF_POPUP | 0x80, 1);             /* Alt : la barre */
    run(3000);
    menu_select(2, MF_POPUP | 0x80, 1);             /* Bas : le menu s'ouvre */
    menu_select(101, 0x80, 2);
    run(4000);
    menu_select(2, MF_POPUP | 0x80, 1);             /* Droite */
    menu_select(3, MF_POPUP | 0x80, 1);
    menu_select(201, 0x80, 3);
    run(3000);
    menu_select(2, MF_POPUP | 0x80, 1);             /* Gauche, sans premier élément */
    menu_select(3, MF_POPUP | 0x80, 1);
    menu_select(2, MF_POPUP | 0x80, 1);
    run(3000);
    CHECK(spoken_n == 6);
    if (spoken_n == 6) {
        CHECK_STR(spoken[0], "Fichier, sous-menu");
        CHECK_STR(spoken[1], "Fichier, sous-menu");
        CHECK_STR(spoken[2], "Nouveau..., Ctrl plus N");
        CHECK_STR(spoken[3], "Edition, sous-menu");
        CHECK_STR(spoken[4], "Annuler");
        CHECK_STR(spoken[5], "Fichier, sous-menu");
    }
    menu_select(0, 0xFFFF, 0);                      /* fermé */
    run(500);
}

/* Boîte Caractères de Word : onglets dessinés dans un bitmap, libellé le plus proche,
   bouton sans le libellé voisin, page annoncée après Ctrl+Tab */
static struct { HWND dlg, edit; HDC dc, strip; } wd;
/* rangée d'onglets dans un bitmap : page affichée en gras, focus 0 (aucun) ou n° */
static void wd_strip(int page, int focus)
{
    HDC m = wd.strip;
    EpPatBlt(m, 0, 0, 400, 24, PATCOPY);
    sim_dc[m].bold = page == 1;
    EpExtTextOut(m, 16, 4, 0, NULL, "Police, style et attributs", 26, NULL);
    sim_dc[m].bold = page == 2;
    EpExtTextOut(m, 230, 4, 0, NULL, "Espacement", 10, NULL);
    sim_dc[m].bold = FALSE;
    if (focus) {                        /* pointillés de Word : haut, bas, côtés */
        int x = focus == 1 ? 14 : 228, w = focus == 1 ? 212 : 84;
        EpPatBlt(m, x, 3, w, 1, 0x00A50065L);
        EpPatBlt(m, x, 20, w - 1, 1, 0x00A50065L);
        EpPatBlt(m, x, 3, 1, 18, 0x00A50065L);
        EpPatBlt(m, x + w - 1, 3, 1, 17, 0x00A50065L);
    }
    EpBitBlt(wd.dc, 10, 4, 400, 24, m, 0, 0, SRCCOPY);
}
static void wd_frame(int x, int y, int w, int h)
{
    EpPatBlt(wd.dc, x, y, w, 1, 0x00A50065L);
    EpPatBlt(wd.dc, x, y + h - 1, w, 1, 0x00A50065L);
    EpPatBlt(wd.dc, x, y, 1, h, 0x00A50065L);
    EpPatBlt(wd.dc, x + w - 1, y, 1, h, 0x00A50065L);
}
static void test_word_characters(void)
{
    boot();
    memset(&wd, 0, sizeof(wd));
    wd.dlg = sim_create(0, "bosa_sdm_Microsoft Word 6.0", "Caractères", 0, 60, 60, 580, 400);
    wd.edit = sim_create(wd.dlg, "OpusFedt", "", 0, 360, 158, 420, 176);
    wd.dc = EpGetDC(wd.dlg);
    wd.strip = sim_memdc();
    EpExtTextOut(wd.dc, 20, 100, 0, NULL, "Espacement:", 11, NULL);
    EpExtTextOut(wd.dc, 150, 140, 0, NULL, "Normal", 6, NULL);
    EpExtTextOut(wd.dc, 270, 140, 0, NULL, "De:", 3, NULL);
    EpExtTextOut(wd.dc, 270, 200, 0, NULL, "De:", 3, NULL);
    EpExtTextOut(wd.dc, 400, 200, 0, NULL, "Par défaut...", 13, NULL);
    wd_strip(1, 0);
    run(300);
    ap_reset();
    sim_active = wd.dlg;
    sim_focus = wd.edit;
    sim_win[wd.edit].rc.left = 360; sim_win[wd.edit].rc.right = 420;   /* « Normal De: [  ] » */
    sim_win[wd.edit].rc.top = 198; sim_win[wd.edit].rc.bottom = 216;   /* (écran) */
    run(4000);
    CHECK(spoken_n == 1 && !strncmp(spoken[0], "Caractères, De:, zone d'édition", 31));
    ap_reset();
    sim_focus = wd.dlg;                 /* Maj+Tab : les onglets */
    wd_strip(1, 1);
    run(4000);
    wd_strip(1, 0);                     /* Tab : le bouton (le rectangle de l'onglet */
    wd_frame(398, 199, 108, 18);        /* disparaît du bitmap) */
    run(4000);
    CHECK(spoken_n == 2);
    if (spoken_n == 2) {
        CHECK_STR(spoken[0], "Police, style et attributs, onglet");
        CHECK_STR(spoken[1], "Par défaut..., bouton");
    }
    ap_reset();
    sim_keys[VK_CONTROL] = 1;           /* Ctrl+Tab : page Espacement */
    key(VK_TAB);
    app_keys();                         /* l'application lit la touche, Ctrl enfoncé */
    sim_keys[VK_CONTROL] = 0;           /* relâché avant qu'Épaulard ne la traite */
    run(700);                           /* Word met du temps à redessiner la page, */
    wd_strip(2, 0);
    wd_frame(398, 199, 108, 18);        /* et retrace le focus (même bouton) */
    wd_frame(398, 199, 108, 18);
    run(5000);
    sim_focus = wd.dlg;                 /* Maj+Tab... jusqu'aux onglets, Droite */
    wd_strip(2, 2);
    run(3000);
    CHECK(spoken_n == 3);
    if (spoken_n == 3) {
        CHECK_STR(spoken[0], "Espacement, onglet");
        CHECK_STR(spoken[1], "Par défaut..., bouton");
        CHECK_STR(spoken[2], "Espacement, onglet");
    }
}

/* Boîte Options de Word : contrôles dans une page enfant (sa_sdm_…) dont BeginPaint
   ne doit pas effacer les libellés ; cases et boutons radio (rangée 13 de la bande
   des cases), titre de groupe ; onglet actif en gras au-dessus des libellés en gras
   de la page, page dite après Ctrl+Tab puis l'élément de la page */
static HDC wo_box;              /* bitmap des cases (14×13) */
static void wo_glyph(HDC dc, int x, int y, int xs, int ys) { EpBitBlt(dc, x, y, 14, 13, wo_box, xs, ys, SRCCOPY); }
static void wo_tabs(HDC dc, int page)
{
    sim_dc[dc].bold = page == 1;
    EpExtTextOut(dc, 20, 26, 0, NULL, "Affichage", 9, NULL);
    sim_dc[dc].bold = page == 2;
    EpExtTextOut(dc, 130, 26, 0, NULL, "Général", 7, NULL);
    EpExtTextOut(dc, 4, 60, 0, NULL, "Aperçu", 6, NULL);    /* gras, plus bas et à gauche */
    sim_dc[dc].bold = FALSE;
    EpExtTextOut(dc, 20, 6, 0, NULL, "Révisions", 9, NULL);
}
static void test_word_options(void)
{
    HWND dlg, page;
    HDC dd, pd;
    PAINTSTRUCT ps;

    boot();
    wo_box = sim_memdc();
    dlg = sim_create(0, "bosa_sdm_Microsoft Word 6.0", "Options", 0, 0, 0, 600, 440);
    page = sim_create(dlg, "sa_sdm_Microsoft Word 6.0", "", 0, 10, 50, 590, 430);
    dd = EpGetDC(dlg);
    pd = EpGetDC(page);
    wo_tabs(dd, 1);
    sim_dc[pd].bold = TRUE;             /* titres de groupe en gras, sous les onglets */
    EpExtTextOut(pd, 10, 4, 0, NULL, "Affichage", 9, NULL);
    sim_dc[pd].bold = FALSE;
    wo_glyph(pd, 20, 26, 0, 0);
    EpExtTextOut(pd, 38, 26, 0, NULL, "Police brouillon", 16, NULL);
    wo_glyph(pd, 20, 46, 14, 0);
    EpExtTextOut(pd, 38, 46, 0, NULL, "Ajuster à la fenêtre", 20, NULL);
    EpExtTextOut(pd, 10, 104, 0, NULL, "Insérer", 7, NULL);
    wo_glyph(pd, 20, 126, 14, 13);
    EpExtTextOut(pd, 38, 126, 0, NULL, "Saut de page", 12, NULL);
    wo_glyph(pd, 200, 126, 0, 13);
    EpExtTextOut(pd, 218, 126, 0, NULL, "Saut de colonne", 15, NULL);
    SetRect(&sim_paint, 0, 0, 580, 380);  /* la page se repeint par morceaux */
    EpBeginPaint(page, &ps);
    sim_active = dlg;
    run(3000);
    ap_reset();
    sim_focus = page;
    wd.dc = pd;
    wd_frame(36, 25, 100, 15);          /* Police brouillon */
    run(3000);
    wo_glyph(pd, 20, 26, 14, 0);        /* Espace */
    run(1500);
    wd_frame(36, 25, 100, 15);
    wd_frame(36, 45, 140, 15);          /* Tab */
    run(3000);
    wd_frame(36, 45, 140, 15);
    wd_frame(36, 125, 90, 15);          /* Tab : groupe Insérer */
    run(3000);
    wd_frame(36, 125, 90, 15);
    wo_glyph(pd, 20, 126, 0, 13);       /* Droite : l'autre bouton du groupe */
    wo_glyph(pd, 200, 126, 14, 13);
    wd_frame(216, 125, 110, 15);
    run(3000);
    CHECK(spoken_n == 5);
    if (spoken_n == 5) {
        CHECK_STR(spoken[0], "groupe Affichage, Police brouillon, case à cocher, non coché");
        CHECK_STR(spoken[1], "coché");
        CHECK_STR(spoken[2], "Ajuster à la fenêtre, case à cocher, coché");
        CHECK_STR(spoken[3], "groupe Insérer, Saut de page, bouton radio, sélectionné");
        CHECK_STR(spoken[4], "Saut de colonne, bouton radio, sélectionné");
    }
    ap_reset();
    sim_keys[VK_CONTROL] = 1;           /* Ctrl+Tab : page Général */
    key(VK_TAB);
    app_keys();
    sim_keys[VK_CONTROL] = 0;
    run(400);
    wo_tabs(dd, 2);
    run(3000);
    CHECK(spoken_n == 2);
    if (spoken_n == 2) {
        CHECK_STR(spoken[0], "Général, onglet");
        CHECK_STR(spoken[1], "Saut de colonne, bouton radio, sélectionné");
    }
    memset(&wd, 0, sizeof(wd));
}

/* Réglages (Inser+E) : valeur appliquée aussitôt, enregistrée, annulée par Échap,
   relue au démarrage */
static BOOL com1_has(const char *s)
{
    SIMCOM *c = &sim_com[1];
    int i, n = (int) strlen(s);
    for (i = 0; i + n <= c->out_n; i++)
        if (!memcmp(c->out + i, s, n)) return TRUE;
    return FALSE;
}
static void cfg_press(UINT vk) { PostMessage(hwnd_main, EPM_CFGKEY, vk, 0); run(2500); }
static void test_settings(void)
{
    boot();
    CHECK(com1_has("@W3@F8@V1@P0\r"));
    np_open("texte");
    run(1000);
    ap_reset();
    insert_cmd('E');
    run(4000);
    CHECK(cfg_on && cfg_mode_sim);
    cfg_press(VK_RIGHT);                /* débit 4 */
    CHECK(com1_has("@W4@F8@V1@P0\r"));
    cfg_press(VK_DOWN);
    cfg_press(VK_DOWN);
    cfg_press(VK_DOWN);                 /* ponctuation */
    cfg_press(VK_RIGHT);                /* toute */
    CHECK(com1_has("@W4@F8@V1@P1\r"));
    cfg_press(VK_RIGHT);                /* déjà au bout */
    cfg_press(VK_RETURN);
    CHECK(!cfg_on && !cfg_mode_sim);
    CHECK(GetPrivateProfileInt("Parole", "Debit", 0, "") == 4);
    CHECK(GetPrivateProfileInt("Parole", "Ponctuation", 0, "") == 2);
    CHECK_STR(sim_ini_file, "C:\\EPAULARD\\EPAULARD.INI");
    CHECK(spoken_n == 9);
    if (spoken_n == 9) {
        CHECK_STR(spoken[0], "Réglages d'Épaulard");
        CHECK_STR(spoken[1], "Débit, 3");
        CHECK_STR(spoken[2], "4");
        CHECK_STR(spoken[3], "Hauteur, 8");
        CHECK_STR(spoken[5], "Ponctuation, certaine");
        CHECK_STR(spoken[6], "toute");
        CHECK_STR(spoken[7], "toute");  /* déjà au bout */
        CHECK_STR(spoken[8], "Réglages enregistrés");
    }
    ap_reset();
    insert_cmd('E');                    /* Échap : tout est rétabli */
    run(4000);
    cfg_press(VK_RIGHT);
    CHECK(cfg[CFG_RATE] == 5);
    cfg_press(VK_ESCAPE);
    CHECK(cfg[CFG_RATE] == 4 && !cfg_on);
    CHECK_STR(spoken[spoken_n - 1], "Réglages annulés");
    keep_ini = TRUE;                    /* redémarrage : réglages relus */
    boot();
    CHECK(cfg[CFG_RATE] == 4 && cfg[CFG_PUNCT] == 2);
    CHECK(com1_has("@W4@F8@V1@P1\r"));
}

/* Écho du clavier, noms des signes, ponctuation dans le texte */
static void typed_chars(const char *s) { for (; *s; s++) { PostMessage(hwnd_main, EPM_CHAR, (BYTE) *s, 0); run(400); } run(1000); }
static void test_echo_and_signs(void)
{
    boot();
    np_open("");
    run(1000);
    ap_reset();
    typed_chars("a.");
    CHECK(spoken_n == 2 && !strcmp(spoken[0], "a") && !strcmp(spoken[1], "point"));
    cfg[CFG_ECHO] = 1;                  /* mots : ni lettres, ni espace après le point */
    typed_chars("doc");                 /* mot laissé dans une autre fenêtre */
    sim_focus = np.top;
    run(1500);
    sim_focus = np.edit;
    run(2500);
    ap_reset();
    typed_chars("le chat. ");
    cfg[CFG_ECHO] = 3;                  /* aucun */
    typed_chars("x ");
    cfg[CFG_ECHO] = 0;
    cfg[CFG_CAPS] = 1;
    typed_chars("B");
    say("c:\\winword\\a_b.doc", TRUE);
    run(6000);
    say("Sommaire ________________________________ 1", TRUE);   /* trait : tu */
    run(3000);
    cfg[CFG_PUNCT] = 0;
    say("a/b", TRUE);
    run(600);
    CHECK(spoken_n == 6);
    if (spoken_n == 6) {
        CHECK_STR(spoken[0], "le");
        CHECK_STR(spoken[1], "chat");
        CHECK_STR(spoken[2], "majuscule B");
        CHECK_STR(spoken[3], "c: barre oblique inverse winword barre oblique inverse a souligné b.doc");
        CHECK_STR(spoken[4], "Sommaire 1");
        CHECK_STR(spoken[5], "a/b");
    }
}

/* Inser+PgSuiv : barre d'état au bas de la fenêtre */
static void test_status_bar(void)
{
    HWND top, bar;
    HDC dc;
    boot();
    top = sim_create(0, "OpusApp", "Microsoft Word - ESSAI.DOC", 0, 0, 0, 640, 480);
    bar = sim_create(top, "OpusStat", "", 0, 0, 460, 640, 480);
    dc = EpGetDC(bar);
    EpExtTextOut(dc, 4, 2, 0, NULL, "Page 1", 6, NULL);
    EpExtTextOut(dc, 200, 2, 0, NULL, "Col 1", 5, NULL);
    sim_active = top;
    sim_focus = top;
    run(1000);
    ap_reset();
    insert_cmd(VK_NEXT);
    run(600);
    CHECK(spoken_n == 1 && !strncmp(spoken[0], "Page 1", 6) && strstr(spoken[0], "Col 1"));
}

/* Liste à sélection multiple dont l'élément courant a sim_item_len octets */
static int sim_item_len, sim_item_sel;
static LRESULT long_list_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    (void) h; (void) w;
    if (msg == LB_GETCOUNT) return 1;
    if (msg == LB_GETTEXTLEN) return sim_item_len;
    if (msg == LB_GETTEXT) {
        memset((char *) l, 'x', sim_item_len);
        ((char *) l)[sim_item_len] = 0;
    }
    if (msg == LB_GETSEL) return sim_item_sel;
    return 0;
}

/* Valeur d'une liste dans un tampon de TXT octets, sentinelles derrière :
   « sélectionné » n'est jamais perdu, l'élément est raccourci à sa place. */
static void list_value(HWND list, int len, int sel, char *value)
{
    struct { char value[TXT]; char guard[32]; } out;
    sim_item_len = len;
    sim_item_sel = sel;
    memset(&out, 0, sizeof(out));
    memset(out.guard, 'g', sizeof(out.guard));
    describe_value(list, out.value);
    CHECK(out.guard[0] == 'g' && out.guard[31] == 'g');
    strcpy(value, out.value);
}

static int ends_with(const char *s, const char *end)
{
    size_t a = strlen(s), b = strlen(end);
    return a >= b && strcmp(s + a - b, end) == 0;
}

static int run_of_x(const char *s)
{
    int n = 0;
    while (s[n] == 'x') n++;
    return n;
}

static void test_value_bounds(void)
{
    HWND dlg, label, list;
    char v[TXT], out[TXT * 2], big[TXT];
    const int suffix = (int) strlen(", sélectionné");     /* 13 octets en CP1252 */
    const int fits = TXT - 1 - suffix;                     /* 242 : plus long élément entier */

    boot();
    list = sim_create(0, "LISTBOX", "", LBS_MULTIPLESEL, 0, 0, 100, 100);
    sim_win[list].proc = long_list_proc;

    list_value(list, 3, 1, v);                             /* court, sélectionné */
    CHECK_STR(v, "xxx, sélectionné");
    list_value(list, 3, 0, v);                             /* court, non sélectionné */
    CHECK_STR(v, "xxx");
    list_value(list, fits, 1, v);                          /* juste à la limite */
    CHECK(run_of_x(v) == fits && ends_with(v, ", sélectionné") &&
          (int) strlen(v) == TXT - 1);
    list_value(list, fits + 1, 1, v);                      /* un octet de trop */
    CHECK(run_of_x(v) == fits && ends_with(v, ", sélectionné"));
    list_value(list, TXT - 1, 1, v);                       /* plus long possible */
    CHECK(run_of_x(v) == fits && ends_with(v, ", sélectionné") &&
          (int) strlen(v) == TXT - 1);
    list_value(list, TXT - 1, 0, v);                       /* non sélectionné : entier */
    CHECK(run_of_x(v) == TXT - 1 && (int) strlen(v) == TXT - 1);
    list_value(list, TXT, 1, v);                           /* longueur refusée */
    CHECK(!strstr(v, "xxx"));

    /* Annonce complète : libellé et élément de 255 octets, liste grisée. Le rôle,
       la valeur et l'état restent ; seul le libellé est raccourci. */
    dlg = sim_create(0, "#32770", "Boîte", 0, 0, 0, 400, 300);
    memset(big, 'L', TXT - 1);
    big[TXT - 1] = 0;
    label = sim_create(dlg, "STATIC", big, 0, 10, 10, 300, 30);
    list = sim_create(dlg, "LISTBOX", "", LBS_MULTIPLESEL | WS_DISABLED, 10, 40, 300, 200);
    sim_win[list].proc = long_list_proc;
    (void) label;
    sim_item_len = TXT - 1;
    sim_item_sel = 1;
    describe(list, out);
    CHECK(strstr(out, "LLLL, liste, xxxx") != NULL);
    CHECK(ends_with(out, "x, sélectionné, grisé"));
    CHECK((int) strlen(out) < TXT * 2);
    sim_item_sel = 0;
    describe(list, out);
    CHECK(ends_with(out, "x, grisé") && !strstr(out, "sélectionné"));
    sim_item_len = 3;
    sim_item_sel = 1;
    sim_win[list].style &= ~WS_DISABLED;
    describe(list, out);
    CHECK(ends_with(out, "L, liste, xxx, sélectionné") &&
          strlen(out) == (size_t) (TXT - 1) + strlen(", liste, xxx, sélectionné"));
}

static void test_braille_modal_handoff(void)
{
    boot();
    np_open("Retour au document");
    run(1000);
    sim_sysmodal = sim_create(0, "#32770", "Boîte système", 0, 0, 0, 200, 100);
    br_text("boite systeme", -1, FALSE);
    br_tick(); br_tick(); br_tick();
    sim_br_epoch++;
    braille_poll();
    CHECK(!braille_valid);
    baum_step();
    CHECK_STR(br_shown, "boite systeme                           ");
    br_text("ligne modale assez longue pour défiler au-delà des quarante cellules de la plage", -1, FALSE);
    br_pan(1);
    CHECK(offset > 0);
    sim_sysmodal = 0;
    sim_br_epoch++;              /* fermeture pendant le défilement, focus inchangé */
    braille_poll();
    CHECK(braille_valid);
    CHECK(offset == 0);
    br_tick(); br_tick();
    baum_step();
    CHECK_STR(br_shown, "retour au document                      ");
}

/* Braille : points 7-8 sous la sélection (EDIT, puis texte dessiné de Write) */
static void test_braille_selection(void)
{
    boot();
    np_open("Bonjour le monde\nsuite");
    run(1000);
    CHECK(br_enabled());
    CHECK_STR(br_shown, "bonjour le monde                        ");
    CHECK_STR(br_mask,  "#                                       ");     /* curseur */
    np.sel_a = 8; np.sel_b = 10;            /* « le » */
    run(300);
    CHECK_STR(br_mask,  "        ##                              ");
    np.sel_a = np.sel_b = 3;
    run(300);
    CHECK_STR(br_mask,  "   #                                    ");

    boot();
    wr_open(doc_lines, DOC_N);
    run(1000);
    EpPatBlt(wr.dc, 8 * 4, 0, 8 * 5, 16, DSTINVERT);     /* « ement » de Complement */
    run(300);
    CHECK(!strncmp(br_shown, "complement d'informations", 25));
    CHECK(!strncmp(br_mask,  "#   #####                ", 25));
}

static int line_index_np(const char *t)
{
    int l, s, n;
    for (l = 0; (s = np_line_start(l)) >= 0; l++) {
        for (n = 0; np.text[s + n] && np.text[s + n] != '\n'; n++) ;
        if ((int) strlen(t) == n && !strncmp(np.text + s, t, n)) return l;
    }
    return -1;
}

/* Même arbre que CALC.EXE français observé sous Windows : aucun bouton
   numérique enfant, focus SciCalc, EDIT caché et deux afficheurs statiques. */
static void test_calculator(void)
{
    HWND top, normal, sci, hidden;
    int n;
    boot();
    top = sim_create(0, "SciCalc", "Calculatrice", 0, 80, 20, 350, 300);
    hidden = sim_create(top, "Edit", "secret", 0, 0, 0, 20, 10);
    sim_win[hidden].id = 495;
    sim_win[hidden].style &= ~WS_VISIBLE;
    sci = sim_create(top, "Static", "ancien", SS_RIGHT, 0, 0, 200, 16);
    sim_win[sci].id = 413;
    sim_win[sci].style &= ~WS_VISIBLE;
    normal = sim_create(top, "Static", " 0,", SS_RIGHT, 0, 0, 200, 16);
    sim_win[normal].id = 414;
    sim_active = sim_focus = top;
    run(3000);
    CHECK(spoken_since(0, "0") >= 0);
    CHECK(!strncmp(br_shown, "0,", 2));
    ap_reset();
    strcpy(sim_win[normal].text, " 12,");
    PostMessage(hwnd_main, EPM_CHAR, '2', 0);
    run(1500);
    CHECK(spoken_n == 1);
    CHECK_STR(spoken[0], "12");
    n = spoken_n; run(1500); CHECK(spoken_n == n); /* aucun bavardage */
    PostMessage(hwnd_main, EPM_CHAR, '+', 0); run(1000);
    CHECK(spoken_since(n, "plus") >= 0);
    strcpy(sim_win[normal].text, " 15,");
    PostMessage(hwnd_main, EPM_CHAR, '=', 0); run(1500);
    CHECK(spoken_since(n, "15") >= 0);
    n = spoken_n;
    PostMessage(hwnd_main, EPM_CHAR, '=', 0); run(1500);
    CHECK(spoken_since(n, "15") >= 0);  /* résultat identique néanmoins annoncé */
    cfg[CFG_PUNCT] = 0;
    strcpy(sim_win[normal].text, " -0,25"); run(2000);
    CHECK(spoken_since(n, " moins 0 virgule 25") >= 0);
    CHECK(!strncmp(br_shown, "-0,25", 5));
    n = spoken_n;
    insert_cmd(VK_UP); run(2000);
    CHECK(spoken_since(n, " moins 0 virgule 25") >= 0);
    strcpy(sim_win[normal].text, "Division par zero"); run(2000);
    CHECK(spoken_since(n, "Division par zero") >= 0);
    sim_win[normal].style &= ~WS_VISIBLE;
    sim_win[sci].style |= WS_VISIBLE;
    strcpy(sim_win[sci].text, " 256,"); run(2000);
    CHECK(spoken_since(n, "256") >= 0);
    CHECK(!strncmp(br_shown, "256,", 4));
    /* Une autre application ne doit pas hériter du suivi de la calculatrice. */
    sim_active = sim_focus = sim_create(0, "Other", "Autre", 0, 0, 0, 100, 100);
    run(1500); n = spoken_n;
    strcpy(sim_win[sci].text, " 999,"); run(1500);
    CHECK(spoken_n == n);
    sim_active = sim_focus = top; run(1500);
    CHECK(spoken_since(n, "999") >= 0);
}

/* États reproduits d'après CALC.EXE français : les radios 127..129 passent de
   Deg/Rad/Grad à Dmot/Mot/Octet selon la base. Le témoin mémoire est 400 ou 402. */
static struct {
    HWND top, normal, sci, mem, mem_sci;
    int base, unit, inverse, hyper, command, commands;
    char memory[40];
} ca;

static LRESULT ca_setting(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    int id = sim_win[h].id;
    (void) w; (void) l;
    if (msg == BM_GETCHECK)
        return id == ca.base || id == ca.unit ||
            (id == 125 && ca.inverse) || (id == 126 && ca.hyper);
    return 0;
}

static LRESULT ca_command(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    (void) h; (void) l;
    if (msg != WM_COMMAND) return 0;
    ca.command = (int) w;
    ca.commands++;
    if (w == 114) {
        strcpy(ca.memory, sim_win[ca.normal].text);
        strcpy(sim_win[ca.mem].text, " M");
    } else if (w == 113) strcpy(sim_win[ca.normal].text, ca.memory);
    else if (w == 112) sim_win[ca.mem].text[0] = 0;
    else if (w == 81) strcpy(sim_win[ca.normal].text, " 0,");
    return 0;
}

static HWND ca_child(const char *cls, const char *text, int id, BOOL visible)
{
    HWND h = sim_create(ca.top, cls, text, 0, 20, 60, 180, 80);
    sim_win[h].id = (WORD) id;
    if (!visible) sim_win[h].style &= ~WS_VISIBLE;
    return h;
}

static void test_calculator_states(void)
{
    HWND h, other;
    char out[TXT * 2];
    int i, n, count;
    boot();
    memset(&ca, 0, sizeof(ca));
    ca.base = 123; ca.unit = 127;
    ca.top = sim_create(0, "SciCalc", "Calculatrice", 0, 0, 0, 600, 400);
    sim_win[ca.top].proc = ca_command;
    ca.normal = ca_child("Static", " 15,", 414, TRUE);
    ca.sci = ca_child("Static", " 15,", 413, FALSE);
    ca.mem = ca_child("Static", "", 402, TRUE);
    ca.mem_sci = ca_child("Static", "", 400, FALSE);
    for (i = 121; i <= 129; i++) {
        h = ca_child("Button", "abrégé", i, FALSE);
        sim_win[h].proc = ca_setting;
        sim_win[h].style |= i == 125 || i == 126 ? BS_AUTOCHECKBOX : BS_RADIOBUTTON;
    }
    sim_active = sim_focus = ca.top;
    run(6000);
    n = spoken_n;
    /* Parcourir ne déclenche aucune commande et le focus de CALC reste intact. */
    for (i = 0; i < 4; i++) { insert_cmd(VK_RIGHT); run(1000); }
    CHECK(ca.commands == 0);
    CHECK(sim_focus == ca.top);
    {
        POINT pt;
        HWND scope;
        CHECK(!review_screen_point(&pt, &scope));
    }
    CHECK(spoken_since(n, "mémoriser le résultat, bouton") >= 0);
    CHECK(!strncmp(br_shown, "m", 1));
    insert_cmd(VK_RETURN); run(5000);
    CHECK(ca.command == 114);
    CHECK(spoken_since(n, "mémoire occupée") >= 0);
    n = spoken_n;
    /* La frappe habituelle rend le braille au résultat et quitte le parcours. */
    PostMessage(hwnd_main, EPM_KEY, VK_ESCAPE, 0);
    strcpy(sim_win[ca.normal].text, " 0,"); run(2000);
    CHECK(!strncmp(br_shown, "0,", 2));
    insert_cmd(VK_HOME); run(1000);
    for (i = 0; i < 4; i++) { insert_cmd(VK_RIGHT); run(1000); }
    insert_cmd(VK_RETURN); run(4000);
    CHECK(ca.command == 113);
    CHECK(spoken_since(n, "15") >= 0);
    insert_cmd(VK_RIGHT); run(1000); insert_cmd(VK_RETURN); run(4000);
    CHECK(ca.command == 112);
    CHECK(spoken_since(n, "mémoire vide") >= 0);
    n = spoken_n; run(2000); CHECK(spoken_n == n);
    /* Le bilan est également consultable en braille. */
    insert_cmd(VK_NEXT); run(5000);
    CHECK(!strncmp(br_shown, "mode standard", 13));
    insert_cmd(VK_UP); run(2000);
    CHECK(!strncmp(br_shown, "15,", 3));
    /* Les fenêtres cachées ne doivent fournir ni mémoire ni réglage fantômes. */
    strcpy(sim_win[ca.mem].text, " M");
    sim_win[ca.mem].style &= ~WS_VISIBLE;
    sim_win[ca.normal].style &= ~WS_VISIBLE;
    sim_win[ca.sci].style |= WS_VISIBLE;
    sim_win[ca.mem_sci].style |= WS_VISIBLE;
    for (i = 1; i < SIM_WN; i++)
        if (sim_win[i].used && sim_win[i].parent == ca.top && sim_win[i].id >= 121 && sim_win[i].id <= 129)
            sim_win[i].style |= WS_VISIBLE;
    run(7000);
    n = spoken_n;
    ca.base = 124; run(5000);
    CHECK(spoken_since(n, "base hexadécimale, double mot 32 bits") >= 0);
    ca.unit = 128; run(4000);
    CHECK(spoken_since(n, "mot 16 bits") >= 0);
    ca.base = 123; run(5000);
    CHECK(spoken_since(n, "base décimale, radians") >= 0);
    ca.inverse = 1; run(4000);
    CHECK(spoken_since(n, "fonctions inverses activées") >= 0);
    ca.hyper = 1; run(4000);
    CHECK(spoken_since(n, "fonctions hyperboliques activées") >= 0);
    n = spoken_n;
    insert_cmd(VK_HOME); run(1000);
    for (i = 0; i < 70; i++) { insert_cmd(VK_RIGHT); run(1000); }
    CHECK(spoken_since(n, "arc sinus hyperbolique, bouton") >= 0);
    n = spoken_n; run(5000); CHECK(spoken_n == n);
    /* Le parcours ignore les chiffres et fonctions indisponibles selon la base. */
    ca.base = 121;
    n = spoken_n;
    insert_cmd(VK_HOME); run(1000);
    for (i = 0; i < 70; i++) { insert_cmd(VK_RIGHT); run(1000); }
    CHECK(spoken_since(n, "un, bouton") >= 0);
    CHECK(spoken_since(n, "deux, bouton") < 0);
    CHECK(spoken_since(n, "arc sinus hyperbolique, bouton") < 0);
    CHECK(spoken_since(n, "A, bouton") < 0);
    /* Tabulation conserve les vrais rôles et les noms longs des radios. */
    for (h = GetWindow(ca.top, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        if (GetWindowWord(h, GWW_ID) == 128) break;
    describe(h, out);
    CHECK_STR(out, "mot 16 bits, bouton radio, sélectionné");
    sim_focus = h; run(3000);
    CHECK(!strncmp(br_shown, "mot 16 bits", 11));
    /* Ni un menu ni une autre application n'héritent d'une activation virtuelle. */
    count = ca.commands;
    in_menu = TRUE;
    insert_cmd(VK_RETURN); run(1000);
    CHECK(ca.commands == count);
    in_menu = FALSE;
    other = sim_create(0, "Other", "Autre", 0, 0, 0, 100, 100);
    sim_active = sim_focus = other;
    insert_cmd(VK_RETURN); run(1000);
    CHECK(ca.commands == count);
}

static void test_disabled_controls(void)
{
    HWND dlg, button, check;
    char out[TXT * 2];
    boot();
    dlg = sim_create(0, "#32770", "Essai", 0, 0, 0, 200, 200);
    button = sim_create(dlg, "Button", "&Appliquer", WS_DISABLED, 0, 0, 100, 20);
    describe(button, out);
    CHECK_STR(out, "Appliquer, bouton, grisé");
    sim_win[button].style &= ~WS_DISABLED;
    describe(button, out);
    CHECK_STR(out, "Appliquer, bouton");
    check = sim_create(dlg, "Button", "Option", BS_AUTOCHECKBOX | WS_DISABLED, 0, 30, 100, 50);
    describe(check, out);
    CHECK_STR(out, "Option, case à cocher, non coché, grisé");
}

static LRESULT tree_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void) h;
    if (m == LB_GETCOUNT) return 2;
    if (m == LB_GETCURSEL || m == LB_GETCARETINDEX) return 0;
    if (m == LB_GETITEMRECT) SetRect((RECT *) l, 0, (int) w * 16, 200, (int) w * 16 + 16);
    return 0;
}

static LRESULT empty_list_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void) h; (void) w; (void) l;
    return m == LB_GETCURSEL || m == LB_GETCARETINDEX ? LB_ERR : 0;
}

/* Programmes divers : liste vide (Gestionnaire d'impression), libellé désigné par
   l'ordre des fenêtres (Dame de Pique), message d'une boîte d'une autre classe que
   #32770 (Microsoft Backup), titre du document MDI au Ctrl+F6 (Sysedit). */
static void test_generic_windows(void)
{
    HWND dlg, list, edit, frame, client, doc1, doc2, ed1, ed2, ok;
    char out[TXT * 2];
    int i;
    BOOL found;

    boot();
    dlg = sim_create(0, "PrintManager", "Gestionnaire d'impression", 0, 0, 0, 400, 300);
    list = sim_create(dlg, "LISTBOX", "", LBS_OWNERDRAWFIXED, 0, 20, 400, 300);
    sim_win[list].proc = empty_list_proc;
    describe_value(list, out);
    CHECK_STR(out, "vide");

    dlg = sim_create(0, "#32770", "La Dame de Pique réseau", 0, 0, 0, 400, 300);
    sim_create(dlg, "STATIC", "Bienvenue à la Dame de Pique réseau.", 0, 10, 10, 390, 30);
    /* libellé à gauche, un peu plus bas que la zone : la géométrie ne le voit pas */
    sim_create(dlg, "STATIC", "Quel est votre nom?", 0, 10, 72, 150, 90);
    edit = sim_create(dlg, "EDIT", "", 0, 10, 54, 200, 70);
    describe(edit, out);
    CHECK_STR(out, "Quel est votre nom?, zone d'édition, vide");

    boot();
    dlg = sim_create(0, "NBMSGCLASS", "Microsoft Backup", 0, 0, 0, 300, 120);
    sim_create(dlg, "STATIC", "Microsoft Backup n'a pas été configuré.", 0, 40, 10, 290, 40);
    ok = sim_create(dlg, "BUTTON", "&Oui", 0, 40, 60, 100, 80);
    sim_active = dlg;
    sim_focus = ok;
    ap_reset();
    run(1000);
    found = FALSE;
    for (i = 0; i < spoken_n; i++)
        if (strstr(spoken[i], "n'a pas été configuré")) found = TRUE;
    CHECK(found);

    boot();
    frame = sim_create(0, "Sysedit", "Editeur de configuration système", 0, 0, 0, 600, 400);
    client = sim_create(frame, "MDIClient", "", 0, 0, 20, 600, 400);
    doc1 = sim_create(client, "SysEditDoc", "C:\\AUTOEXEC.BAT", 0, 0, 20, 300, 200);
    ed1 = sim_create(doc1, "EDIT", "", ES_MULTILINE, 0, 40, 300, 200);
    doc2 = sim_create(client, "SysEditDoc", "C:\\CONFIG.SYS", 0, 20, 40, 320, 220);
    ed2 = sim_create(doc2, "EDIT", "", ES_MULTILINE, 20, 60, 320, 220);
    sim_active = frame;
    sim_focus = ed1;
    run(1000);
    ap_reset();
    sim_focus = ed2;
    run(1000);
    CHECK(spoken_n >= 1 && strstr(spoken[0], "CONFIG.SYS") && strstr(spoken[0], "zone de texte"));
    ap_reset();
    sim_focus = ed1;
    run(1000);
    CHECK(spoken_n >= 1 && strstr(spoken[0], "AUTOEXEC.BAT"));

    /* Couleurs : liste déroulante dessinée dont le texte porte des données ;
       bouton « Palette de couleurs >> » */
    boot();
    dlg = sim_create(0, "#32770", "Couleurs", 0, 0, 0, 400, 300);
    list = sim_create(dlg, "ComboBox", "", CBS_OWNERDRAWFIXED, 10, 30, 300, 50);
    EpExtTextOut(EpGetDC(list), 2, 2, 0, NULL, "Standard Microsoft", 18, NULL);
    describe_value(list, out);
    CHECK_STR(out, "Standard Microsoft");
    ok = sim_create(dlg, "Button", "&Palette de couleurs >>", 0, 10, 60, 200, 80);
    describe(ok, out);
    CHECK_STR(out, "Palette de couleurs, bouton");
    /* InstallShield (Netscape) : « < Précédent », « Suivant > » ; « 5 > 3 » reste */
    ok = sim_create(dlg, "Button", "&Suivant >", 0, 10, 90, 200, 110);
    describe(ok, out);
    CHECK_STR(out, "Suivant, bouton");
    ok = sim_create(dlg, "Button", "< &Précédent", 0, 10, 120, 200, 140);
    describe(ok, out);
    CHECK_STR(out, "Précédent, bouton");
    ok = sim_create(dlg, "Button", "5 > 3", 0, 10, 150, 200, 170);
    describe(ok, out);
    CHECK_STR(out, "5 > 3, bouton");

    /* Préférences de Netscape : zone de saisie sans texte rendu (WM_GETTEXT vide),
       pas encore peinte à l'annonce du focus ; son dessin donne la valeur */
    boot();
    dlg = sim_create(0, "#32770", "Preferences", 0, 0, 0, 400, 300);
    sim_create(dlg, "Static", "Cache en &mémoire:", 0, 10, 10, 100, 26);
    edit = sim_create(dlg, "Edit", "", 0, 110, 10, 160, 26);
    sim_update = netscape_paint;
    InvalidateRect(edit, NULL, TRUE);
    describe(edit, out);
    CHECK_STR(out, "Cache en mémoire:, zone d'édition, 600");
    sim_update = NULL;

    /* Licence de Netscape : texte en zone de lecture seule, message dit à l'ouverture */
    boot();
    dlg = sim_create(0, "#32770", "Licence d'utilisation Netscape", 0, 0, 0, 400, 300);
    ok = sim_create(dlg, "Button", "J'accepte", BS_DEFPUSHBUTTON, 10, 250, 100, 270);
    sim_create(dlg, "Button", "Je refuse", 0, 290, 250, 390, 270);
    sim_create(dlg, "Edit", "CONTRAT DE LICENCE", ES_MULTILINE | ES_READONLY | WS_VSCROLL,
               10, 40, 390, 200);
    sim_create(dlg, "Static", "Netscape est un logiciel sous licence.", SS_CENTER, 10, 10, 390, 30);
    sim_create(dlg, "Static", "Si vous acceptez, appuyez sur J'accepte.", SS_CENTER, 10, 210, 390, 240);
    sim_active = dlg;
    sim_focus = ok;
    ap_reset();
    run(3000);
    CHECK(spoken_n >= 1 && strstr(spoken[0], "Licence d'utilisation Netscape") &&
          strstr(spoken[0], "logiciel sous licence") && strstr(spoken[0], "appuyez sur J'accepte") &&
          strstr(spoken[0], "J'accepte, bouton"));
    /* même boîte, zone modifiable : formulaire, libellés non dits d'avance */
    boot();
    dlg = sim_create(0, "#32770", "Formulaire", 0, 0, 0, 400, 300);
    ok = sim_create(dlg, "Button", "OK", BS_DEFPUSHBUTTON, 10, 250, 100, 270);
    sim_create(dlg, "Edit", "", 0, 10, 40, 390, 60);
    sim_create(dlg, "Static", "Texte d'aide du formulaire", SS_CENTER, 10, 10, 390, 30);
    sim_active = dlg;
    sim_focus = ok;
    ap_reset();
    run(3000);
    CHECK(spoken_n >= 1 && !strstr(spoken[0], "aide du formulaire"));
    /* Eudora : corps du message sous « Attachments: », libellé du champ précédent */
    boot();
    dlg = sim_create(0, "#32770", "", 0, 0, 0, 400, 300);
    sim_create(dlg, "Static", "Attachments:", SS_RIGHT, 10, 80, 90, 96);
    sim_create(dlg, "Edit", "", ES_MULTILINE, 100, 80, 390, 96);
    list = sim_create(dlg, "Edit", "", ES_MULTILINE, 10, 100, 390, 290);
    describe(list, out);
    CHECK(!strstr(out, "Attachments"));
    describe(dlg + 2, out);
    CHECK(strstr(out, "Attachments") == out);

    /* assistant de profil de Communicator : une seule boîte, les zones des pages
       suivantes sont cachées ; le texte de la page visible est dit */
    boot();
    dlg = sim_create(0, "#32770", "Configuration du nouveau profil", 0, 0, 0, 400, 300);
    sim_create(dlg, "Static", "CREATION D'UN NOUVEAU PROFIL", 0, 10, 10, 390, 30);
    sim_create(dlg, "Static", "Nom entier:", 0, 10, 40, 100, 56);
    sim_win[sim_create(dlg, "Edit", "", 0, 110, 40, 390, 56)].style &= ~WS_VISIBLE;
    sim_win[dlg + 2].style &= ~WS_VISIBLE;
    ok = sim_create(dlg, "Button", "Suivant >", BS_DEFPUSHBUTTON, 10, 250, 100, 270);
    sim_active = dlg;
    sim_focus = ok;
    ap_reset();
    run(3000);
    CHECK(spoken_n >= 1 && strstr(spoken[0], "NOUVEAU PROFIL") && !strstr(spoken[0], "Nom entier"));
    /* Suivant : même boîte, autre page ; son explication avant le champ, pas le
       libellé « Nom entier: » une seconde fois */
    sim_win[dlg + 1].style &= ~WS_VISIBLE;
    sim_create(dlg, "Static", "Pour commencer, entrez votre nom.", 0, 10, 10, 390, 30);
    sim_win[dlg + 2].style |= WS_VISIBLE;
    sim_win[dlg + 3].style |= WS_VISIBLE;
    sim_focus = dlg + 3;
    ap_reset();
    run(3000);
    CHECK(spoken_n == 1 && !strcmp(spoken[0], "Pour commencer, entrez votre nom., Nom entier:, zone d'\xe9" "dition, vide"));
    /* champ suivant de la même page : rien de plus */
    sim_focus = ok;
    ap_reset();
    run(3000);
    CHECK(spoken_n == 1 && !strstr(spoken[0], "Pour commencer"));

    /* Souris : curseur nommé par son cadre de groupe, valeur en pour cent */
    boot();
    dlg = sim_create(0, "#32770", "Souris", 0, 0, 0, 400, 300);
    sim_create(dlg, "Button", "Vitesse du double-clic", BS_GROUPBOX, 10, 10, 300, 80);
    sim_create(dlg, "Static", "Lente", 0, 20, 30, 60, 46);
    list = sim_create(dlg, "ScrollBar", "", 0, 20, 50, 290, 66);
    sim_scroll_pos = 548;
    sim_scroll_max = 1000;
    describe(list, out);
    CHECK_STR(out, "Vitesse du double-clic, barre de défilement, 54 %");
    /* Clavier : deux curseurs dans le même cadre, chacun après son libellé à
       raccourci puis ses textes d'extrémités */
    sim_create(dlg, "Button", "Clavier", BS_GROUPBOX, 10, 100, 300, 260);
    sim_create(dlg, "Static", "&Délai avant la première répétition", 0, 20, 110, 290, 126);
    sim_create(dlg, "Static", "Longue", 0, 20, 130, 60, 146);
    sim_create(dlg, "Static", "Courte", 0, 250, 130, 290, 146);
    list = sim_create(dlg, "ScrollBar", "", 0, 70, 130, 240, 146);
    describe(list, out);
    CHECK_STR(out, "Délai avant la première répétition, barre de défilement, 54 %");
    sim_scroll_pos = sim_scroll_max = 0;

    /* International : boutons homonymes distingués par leur cadre, dit en y
       entrant ; Date/Heure : champs nommés, séparateurs ignorés */
    boot();
    dlg = sim_create(0, "#32770", "International", 0, 0, 0, 400, 300);
    sim_create(dlg, "Button", "Format de date", BS_GROUPBOX, 10, 10, 190, 100);
    ok = sim_create(dlg, "Button", "&Modifier...", 0, 100, 60, 180, 80);
    sim_create(dlg, "Button", "Format horaire", BS_GROUPBOX, 210, 10, 390, 100);
    edit = sim_create(dlg, "Button", "M&odifier...", 0, 300, 60, 380, 80);
    sim_active = dlg;
    sim_focus = ok;
    ap_reset();
    run(5000);
    CHECK(spoken_n == 1 && !strcmp(spoken[0], "International, Format de date, Modifier..., bouton"));
    ap_reset();
    sim_focus = edit;
    run(3000);
    CHECK(spoken_n == 1 && !strcmp(spoken[0], "Format horaire, Modifier..., bouton"));
    dlg = sim_create(0, "#32770", "Date & Heure", 0, 0, 0, 400, 300);
    sim_create(dlg, "Static", "/", 0, 50, 10, 55, 26);
    edit = sim_create(dlg, "Edit", "10", ES_MULTILINE, 60, 10, 80, 26);
    sim_win[edit].id = 705;
    sim_win[sim_create(dlg, "Edit", "28", ES_MULTILINE, 10, 10, 40, 26)].id = 704;
    sim_win[sim_create(dlg, "Edit", "2026", ES_MULTILINE, 110, 10, 150, 26)].id = 706;
    sim_win[sim_create(dlg, "CPArrow", "", 0, 90, 10, 100, 26)].id = 711;
    describe(edit, out);
    CHECK_STR(out, "mois, zone d'édition, 10");

    /* 386 étendu : zone réglée par le bouton radio qui la précède */
    dlg = sim_create(0, "#32770", "Etendu", 0, 0, 0, 400, 300);
    sim_create(dlg, "Button", "&Inactivité (secs)", BS_AUTORADIOBUTTON, 10, 10, 150, 26);
    edit = sim_create(dlg, "Edit", "2", 0, 160, 10, 200, 26);
    describe(edit, out);
    CHECK_STR(out, "Inactivité (secs), zone d'édition, 2");

    /* Bureau : zone numérique, unité après elle, flèches de réglage */
    boot();
    dlg = sim_create(0, "#32770", "Bureau", 0, 0, 0, 400, 300);
    sim_create(dlg, "Static", "&Délai:", 0, 10, 10, 60, 26);
    edit = sim_create(dlg, "Edit", "2", 0, 70, 10, 110, 26);
    sim_create(dlg, "Static", "Minutes", 0, 115, 10, 170, 26);
    describe(edit, out);
    CHECK_STR(out, "Délai:, zone d'édition, 2 Minutes");
    sim_active = dlg;
    sim_focus = edit;
    run(3000);
    ap_reset();
    PostMessage(hwnd_main, EPM_KEY, VK_UP, 0);
    strcpy(sim_win[edit].text, "3");
    run(2000);
    CHECK(spoken_n == 1 && !strcmp(spoken[0], "3 Minutes"));

    /* Arbre du Gestionnaire de fichiers : focus donné avant le premier dessin */
    boot();
    frame = sim_create(0, "WFS_Frame", "Gestionnaire de fichiers", 0, 0, 0, 600, 400);
    list = sim_create(frame, "ListBox", "", LBS_OWNERDRAWFIXED, 10, 40, 210, 300);
    sim_win[list].proc = tree_proc;
    sim_active = frame;
    sim_focus = list;
    ap_reset();
    run(100);
    CHECK(spoken_n >= 1 && strstr(spoken[spoken_n - 1], "élément 1"));
    EpExtTextOut(EpGetDC(list), 2, 0, 0, NULL, "c:\\", 3, NULL);
    run(5000);                          /* (annonce précédente : 3 s à 15 car./s) */
    CHECK(spoken_n >= 2 && strstr(spoken[spoken_n - 1], "c:") &&
          !strstr(spoken[spoken_n - 1], "élément"));
}

/* Enregistreur de sons : boutons « #Play » nommés en français, position du son au
   lieu de la graduation, état (Arrêté, Lecture) annoncé quand il change. */
static void test_sound_recorder(void)
{
    HWND app, state, pos, bar, play;
    char out[TXT * 2];
    int i;
    BOOL found;

    boot();
    app = sim_create(0, "SoundRec", "Enregisteur de sons - (Sans titre)", 0, 0, 0, 300, 200);
    state = sim_create(app, "NoFlickerText", "Arrêté", 0, 10, 10, 290, 30);
    sim_win[state].id = 200;
    pos = sim_create(app, "NoFlickerText", "1,20 sec.", 0, 10, 40, 60, 60);
    sim_win[pos].id = 201;
    bar = sim_create(app, "ScrollBar", "", 0, 10, 100, 290, 116);
    play = sim_create(app, "SButton", "#Play", 0, 100, 120, 140, 150);
    describe(play, out);
    CHECK_STR(out, "Lecture, bouton");
    describe(bar, out);
    CHECK_STR(out, "Position, curseur, 1,20 sec.");
    sim_active = app;
    sim_focus = play;
    run(1000);
    ap_reset();
    strcpy(sim_win[state].text, "Lecture");
    run(1000);
    found = FALSE;
    for (i = 0; i < spoken_n; i++)
        if (strstr(spoken[i], "Lecture")) found = TRUE;
    CHECK(found);
}

/* Fiches (Cardfile) : titre de la fiche de devant, dessiné dans CARD au-dessus de sa
   zone de texte, dit au focus puis à chaque changement de fiche avec la ligne. */
static void test_cardfile(void)
{
    HWND app, card, edit;
    HDC dc;
    char out[TXT * 2];
    int i;
    BOOL found;

    boot();
    app = sim_create(0, "Cardfile", "Répertoire - (Sans titre)", 0, 0, 0, 440, 330);
    card = sim_create(app, "Card", "", 0, 4, 40, 434, 323);
    sim_win[card].id = 205;
    edit = sim_create(card, "Edit", "", ES_MULTILINE, 17, 146, 338, 311);
    sim_win[edit].id = 200;
    dc = EpGetDC(card);
    EpExtTextOut(dc, 10, 70, 0, NULL, "Albert", 6, NULL);  /* fiche de derrière */
    EpExtTextOut(dc, 10, 88, 0, NULL, "Martin", 6, NULL);  /* fiche de devant */
    describe(edit, out);
    CHECK_STR(out, "Martin, zone de texte, vide");
    sim_active = app;
    sim_focus = edit;
    run(1000);
    ap_reset();
    EpExtTextOut(dc, 10, 88, 0, NULL, "Albert", 6, NULL);  /* Page suivante */
    EpExtTextOut(dc, 10, 70, 0, NULL, "Martin", 6, NULL);
    run(1000);
    found = FALSE;
    for (i = 0; i < spoken_n; i++)
        if (!strcmp(spoken[i], "Albert, vide")) found = TRUE;
    CHECK(found);
}

/* Table de caractères : la grille annonce le caractère d'après la barre d'état */
static void test_charmap(void)
{
    HWND app, grid;
    HDC dc;
    char out[TXT * 2];

    boot();
    app = sim_create(0, "MyDlgClass", "Table de Caractères", 0, 0, 0, 620, 250);
    grid = sim_create(app, "CharGridWClass", "", 0, 20, 90, 520, 220);
    dc = EpGetDC(app);
    EpExtTextOut(dc, 450, 232, 0, NULL, "Combinaison:!", 13, NULL);
    describe(grid, out);
    CHECK_STR(out, "grille de caractères, point d'exclamation");
    EpExtTextOut(dc, 450, 232, 0, NULL, "Combinaison:Alt+0233", 20, NULL);
    describe(grid, out);
    CHECK_STR(out, "grille de caractères, é, Alt+0233");
    sim_active = app;
    sim_focus = grid;
    run(1000);
    ap_reset();
    EpExtTextOut(dc, 450, 232, 0, NULL, "Combinaison:A", 13, NULL);
    run(1000);
    CHECK(spoken_n == 1 && !strcmp(spoken[0], "A"));
}

/* Aide de Windows : première ligne d'une rubrique, lien choisi par Tab, défilement,
   dire tout page par page sans répéter la rangée de recouvrement, fin. */
static void test_winhelp(void)
{
    static const char *lines[] = {
        "Index de l'Aide", "Le Panneau de configuration", "Description du Panneau",
        "Changement de la date", "Gestion du son", "Polices", "Ports", "Imprimantes",
        "Clavier", "Souris", "Fin de la rubrique"
    };
    int i, k, n;

    boot();
    hp.doc = sim_create(0, "MS_WINDOC", "Aide du Panneau de configuration", 0, 0, 0, 420, 160);
    hp.topic = sim_create(hp.doc, "MS_WINTOPIC", "", 0, 10, 40, 410, 40 + 16 * H_ROWS);
    hp.dc = EpGetDC(hp.topic);
    memcpy(hp.lines, lines, sizeof(lines));
    hp.n = 11;
    hp_draw();
    sim_active = sim_focus = hp.doc;
    ap_reset();
    run(5000);
    CHECK(spoken_n == 2 && !strcmp(spoken[0], "Aide du Panneau de configuration") &&
          !strcmp(spoken[1], "Index de l'Aide"));
    ap_reset();
    key(VK_TAB);
    run(3000);
    CHECK(spoken_n == 1 && !strcmp(spoken[0], "Description du Panneau, lien"));
    ap_reset();
    key(VK_TAB);
    run(3000);
    CHECK(spoken_n == 1 && !strcmp(spoken[0], "Changement de la date, lien"));
    ap_reset();
    key(VK_DOWN);                       /* une rangée apparaît en bas */
    run(3000);
    CHECK(spoken_n == 1 && !strcmp(spoken[0], "Ports"));

    hp.top = 0;
    hp_draw();
    run(3000);
    ap_reset();
    insert_cmd(VK_DOWN);
    run(30000);
    n = 0;
    for (i = 0; i < spoken_n; i++)
        for (k = 0; k < 11; k++)
            if (!strcmp(spoken[i], lines[k]) && k == n) n++;
    CHECK(n == 11);                     /* toutes les rangées, dans l'ordre */
    CHECK(spoken_n == 11);              /* chacune une fois */
    CHECK(!sa.on);                      /* fin de la rubrique */
}

/* Netscape : la page défile par sa barre (WM_VSCROLL) ; ses champs suivent */
static LRESULT ns_view_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    int i, rows = 0, max = hp.n > H_ROWS ? hp.n - H_ROWS : 0;
    (void) l;
    if (m != WM_VSCROLL)
        return 0;
    if (w == SB_PAGEUP)
        rows = hp.top < H_ROWS - 1 ? -hp.top : -(H_ROWS - 1);
    if (w == SB_PAGEDOWN)
        rows = hp.top + H_ROWS - 1 > max ? max - hp.top : H_ROWS - 1;
    hp.top += rows;
    for (i = 1; i < SIM_WN; i++)
        if (sim_win[i].used && sim_win[i].parent == h) {
            sim_win[i].rc.top -= rows * 16;
            sim_win[i].rc.bottom -= rows * 16;
        }
    hp_draw();
    return 0;
}

/* Netscape : vue MFC (AFX_IDW_PANE_FIRST) sans curseur, page dessinée qui défile.
   Nouvelle page : titre de la fenêtre, puis lecture d'office une fois la page
   dessinée ; Bas lit la rangée apparue ; une touche avant la fin du dessin annule
   la lecture d'office. */
static void test_netscape(void)
{
    static const char *lines[] = {
        "Aller au lien n\xb0", "Accueil pc486", "Les pages d'aujourd'hui", "Recherche sur le web",
        "Quelques adresses", "Wikip\xe9" "dia [1]", "Wiktionnaire [2]", "Win3x.Org [3]",
        "DuckDuckGo [4]", "Fin de la page"
    };
    HWND frame;
    int i, k, n;

    boot();
    frame = sim_create(0, "AfxFrameOrView", "Netscape - [Accueil]", 0, 0, 0, 420, 200);
    hp.doc = hp.topic = sim_create(frame, "AfxWnd", "", 0, 0, 40, 420, 40 + 16 * H_ROWS);
    sim_win[hp.doc].id = 0xE900;
    hp.dc = EpGetDC(hp.doc);
    memcpy(hp.lines, lines, sizeof(lines));
    hp.n = 1;
    hp.top = 0;
    hp_draw();
    sim_active = frame;
    sim_focus = hp.doc;
    run(3000);
    CHECK(page_view(hp.doc) && !page_view(frame));
    /* rangée de recouvrement retrouvée avec ou sans sa puce */
    CHECK(same_row("\xb7Se connecter [14]", "Se connecter [14]") &&
          !same_row("Se connecter [14]", "Se connecter [15]"));
    /* lien suivi : nouveau titre, page dessinée en deux temps */
    ap_reset();
    strcpy(sim_win[frame].text, "Netscape - [Accueil pc486]");
    run(400);
    hp.n = 10;
    hp_draw();
    run(30000);
    CHECK(spoken_n >= 1 && !strcmp(spoken[0], "Netscape - [Accueil pc486]"));
    n = 0;
    for (i = 0; i < spoken_n; i++)
        for (k = 0; k < 10; k++)
            if (!strcmp(spoken[i], lines[k]) && k == n) n++;
    CHECK(n == 10);                     /* toute la page, dans l'ordre, page suivante comprise */
    CHECK(spoken_n == 11);
    CHECK(!sa.on);
    if (spoken_n != 11 || sa.on) dump("test_netscape (lecture)");
    /* défilement d'une rangée : la rangée apparue */
    hp.top = 0;
    hp_draw();
    run(3000);
    ap_reset();
    key(VK_DOWN);
    run(3000);
    CHECK(spoken_n == 1 && !strcmp(spoken[0], lines[H_ROWS]));
    /* liste : puce « · » dessinée en grande police, haute de trois rangées et
       centrée sur la sienne ; aucune rangée sautée */
    hp.top = 0;
    hp.n = 10;
    hp_draw();
    sim_text_height = 46;
    EpExtTextOut(hp.dc, 200, 4 * 16 + 8 - 23, 0, NULL, "\xb7", 1, NULL);  /* rangée 4 */
    sim_text_height = 0;
    {
        POINT pt;
        RECT r;
        char row[TXT];
        int col;
        GetWindowRect(hp.doc, &r);
        pt.x = r.left + 2;
        pt.y = r.top + 3 * 16 + 8;      /* rangée 3 */
        CHECK(EpOsmReviewLine(hp.doc, &pt, 1, row, sizeof(row), &col) && strstr(row, lines[4]) &&
              strchr(row, '\xb7'));
        CHECK(EpOsmReviewLine(hp.doc, &pt, 1, row, sizeof(row), &col) && !strcmp(row, lines[5]));
        pt.y = r.top + 5 * 16 + 8;
        CHECK(EpOsmReviewLine(hp.doc, &pt, -1, row, sizeof(row), &col) && strstr(row, lines[4]) &&
              strchr(row, '\xb7'));
        CHECK(EpOsmReviewLine(hp.doc, &pt, -1, row, sizeof(row), &col) && !strcmp(row, lines[3]));
    }
    hp_draw();

    /* Tab : de la page à ses champs (libellé dessiné à gauche), dans l'ordre de
       lecture, puis retour à la page ; Maj+Tab en sens inverse */
    {
        HWND field, go, box;
        hp.top = 0;
        hp.n = 1;
        hp_draw();
        /* ordre de création différent de l'ordre de lecture */
        box = sim_create(hp.doc, "Button", "Retenir", BS_AUTOCHECKBOX, 10, 72, 100, 88);
        field = sim_create(hp.doc, "Edit", "", 0, 150, 40, 200, 56);
        go = sim_create(hp.doc, "Button", "Aller", 0, 300, 40, 360, 56);
        run(3000);
        ap_reset();
        CHECK(EpPageTab(FALSE) && sim_focus == field);
        run(3000);
        CHECK(spoken_n == 1 && !strcmp(spoken[0], "Aller au lien n\xb0, zone d'\xe9" "dition, vide"));
        CHECK(EpPageTab(FALSE) && sim_focus == go);
        CHECK(EpPageTab(FALSE) && sim_focus == box);
        CHECK(EpPageTab(FALSE) && sim_focus == hp.doc);
        CHECK(EpPageTab(TRUE) && sim_focus == box);
        CHECK(EpPageTab(TRUE) && sim_focus == go);
        CHECK(EpPageTab(TRUE) && sim_focus == field);
        CHECK(EpPageTab(TRUE) && sim_focus == hp.doc);
        sim_focus = frame;
        CHECK(!EpPageTab(FALSE) && sim_focus == frame);      /* hors d'une page : rien */
        sim_destroy(go);
        sim_destroy(field);
        sim_destroy(box);
        /* champ au-dessus de la partie visible : la page défile jusqu'à lui */
        hp.n = 10;
        hp.top = 5;
        hp_draw();
        sim_win[hp.doc].proc = ns_view_proc;
        field = sim_create(hp.doc, "Edit", "", 0, 150, 40 - 4 * 16, 200, 40 - 3 * 16);
        sim_focus = hp.doc;
        CHECK(EpPageTab(FALSE) && sim_focus == field);
        CHECK(hp.top == 0 && sim_win[field].rc.top >= 40);
        sim_win[hp.doc].proc = NULL;
        sim_destroy(field);
        sim_focus = hp.doc;
        hp.n = 10;
        hp_draw();
        run(3000);
    }

    /* lien suivi depuis « Aller au lien n° » : le champ disparaît avec l'ancienne
       page, focus perdu ; la nouvelle page est dite et le focus rendu à la page */
    {
        HWND field = sim_create(hp.doc, "Edit", "", 0, 150, 40, 200, 56), bar;
        sim_focus = field;
        run(3000);
        ap_reset();
        sim_destroy(field);
        sim_focus = 0;
        strcpy(sim_win[frame].text, "Netscape - [Win3x.Org]");
        hp.top = 0;
        hp.n = 3;
        hp_draw();
        run(15000);
        CHECK(sim_focus == hp.doc);
        CHECK(spoken_n == 4 && !strcmp(spoken[0], "Netscape - [Win3x.Org]") &&
              !strcmp(spoken[3], lines[2]));
        /* adresse tapée dans la barre : titre dit, focus laissé, pas de lecture */
        bar = sim_create(frame, "Edit", "http://pc486/", 0, 80, 10, 400, 26);
        sim_focus = bar;
        run(3000);
        ap_reset();
        strcpy(sim_win[frame].text, "Netscape - [Accueil pc486]");
        run(8000);
        CHECK(sim_focus == bar && spoken_n == 1 && !strcmp(spoken[0], "Netscape - [Accueil pc486]"));
        sim_destroy(bar);
        sim_focus = hp.doc;
        hp.n = 10;
        hp_draw();
        run(3000);
    }

    /* touche pendant le chargement : titre seul */
    ap_reset();
    strcpy(sim_win[frame].text, "Netscape - [Autre]");
    run(300);
    key(VK_NEXT);
    run(8000);
    CHECK(spoken_n >= 1 && !strcmp(spoken[0], "Netscape - [Autre]"));
    CHECK(!sa.on && spoken_n <= 2);
    hp.doc = hp.topic = 0;
}

/* Gestionnaire de liaisons : volet actif d'après son sous-titre en surbrillance
   (texte égal à celui de la fenêtre, sans barre de titre), dit une seule fois */
static void test_packager(void)
{
    HWND app, sub1, sub2;
    HDC d1, d2;
    char out[TXT * 2];
    const char *role;

    boot();
    app = sim_create(0, "AppClass", "Gestionnaire de liaisons - Ensemble", 0, 0, 0, 520, 200);
    sim_create(app, "PaneClass", "", 0, 0, 40, 240, 190);
    sim_create(app, "PaneClass", "", 0, 240, 40, 520, 190);
    sub1 = sim_create(app, "SubtitleClass", "--Apparence--", 0, 0, 20, 240, 38);
    sub2 = sim_create(app, "SubtitleClass", "--Contenu--", 0, 240, 20, 520, 38);
    d1 = EpGetDC(sub1);
    d2 = EpGetDC(sub2);
    EpExtTextOut(d1, 0, 0, 0, NULL, "--Apparence--", 13, NULL);
    sim_dc[d2].bk = GetSysColor(COLOR_HIGHLIGHT);
    EpExtTextOut(d2, 0, 0, 0, NULL, "--Contenu--", 11, NULL);
    sim_dc[d2].bk = RGB(255, 255, 255);
    CHECK(custom_value(app, out, &role));
    CHECK_STR(out, "Contenu, volet, vide");
    sim_active = sim_focus = app;
    run(3000);
    ap_reset();
    EpExtTextOut(d2, 0, 0, 0, NULL, "--Contenu--", 11, NULL);       /* Tab */
    sim_dc[d1].bk = GetSysColor(COLOR_HIGHLIGHT);
    EpExtTextOut(d1, 0, 0, 0, NULL, "--Apparence--", 13, NULL);
    sim_dc[d1].bk = RGB(255, 255, 255);
    run(3000);
    CHECK(spoken_n == 1 && !strcmp(spoken[0], "Apparence, volet, vide"));
}

/* Microsoft Mail : boîte Sdi dont le texte est dessiné dans sa fille (lu à
   l'ouverture), champ « Window » nommé par le libellé dessiné à sa gauche */
static void test_mail(void)
{
    HWND dlg, form, field;
    HDC dc;
    char out[TXT * 2];
    int i;
    BOOL found = FALSE;

    boot();
    dlg = sim_create(0, "Sdi", "Bienvenue dans Microsoft Mail", 0, 0, 0, 480, 300);
    form = sim_create(dlg, "Window", "", 0, 0, 20, 480, 300);
    field = sim_create(form, "Window", "", 0, 150, 60, 400, 76);
    dc = EpGetDC(form);
    EpExtTextOut(dc, 100, 4, 0, NULL, "Avant que vous puissiez utiliser Mail,", 38, NULL);
    EpExtTextOut(dc, 10, 40, 0, NULL, "Mot de passe:", 13, NULL);
    EpExtTextOut(EpGetDC(field), 2, 0, 0, NULL, "PASSWORD", 8, NULL);
    describe(field, out);
    CHECK_STR(out, "Mot de passe:, zone d'édition, PASSWORD");
    sim_active = dlg;
    sim_focus = field;
    ap_reset();
    run(8000);
    for (i = 0; i < spoken_n; i++)
        if (strstr(spoken[i], "Avant que vous puissiez")) found = TRUE;
    CHECK(found);
}

/* Grille d'Excel 5 (tableur.c) : zone Nom à gauche de la barre de formule (EXCEL6),
   en-têtes A, B, C et 1, 2, 3 dessinés dans la feuille (EXCEL9), onglet actif en gras.
   Entrée repeint toute la grille sous une région limitée aux deux cellules : les
   en-têtes et les autres valeurs doivent rester dans le modèle. */
static HDC xl_name, xl_bar, xl_grid;
static BOOL xl_bold;            /* B1 mis en gras : seulement visible à la repeinte */
static void xl_paint(HWND h, const RECT *r)
{
    (void) h;
    if (r->left <= 160 && r->right > 160 && r->top <= 18 && r->bottom > 18) {
        sim_dc[xl_grid].bold = xl_bold;
        EpExtTextOut(xl_grid, 160, 18, ETO_OPAQUE, NULL, "12", 2, NULL);
        sim_dc[xl_grid].bold = FALSE;
    }
}
static void xl_select(const char *ref, const char *content)
{
    EpExtTextOut(xl_name, 0, 0, ETO_OPAQUE, NULL, ref, (UINT) strlen(ref), NULL);
    EpPatBlt(xl_bar, 0, 0, 460, 18, PATCOPY);
    if (content[0])
        EpExtTextOut(xl_bar, 2, 0, 0, NULL, content, (UINT) strlen(content), NULL);
}
/* InvertRgn d'Excel : un ou deux rectangles (coordonnées de la grille) en une région */
static HDC xl_inv_dc;
static void xl_invert(int l, int t, int r, int b, int l2, int t2, int r2, int b2)
{
    HRGN a = CreateRectRgn(l, t, r, b);
    if (r2 > l2) {
        HRGN c = CreateRectRgn(l2, t2, r2, b2);
        CombineRgn(a, a, c, RGN_OR);
        DeleteObject(c);
    }
    EpInvertRgn(xl_inv_dc, a);
    DeleteObject(a);
}
static void test_excel_grid(void)
{
    HWND top, grid, tabs, name, bar;
    HDC dc, tdc;
    int from;

    boot();
    top = sim_create(0, "XLMAIN", "Microsoft Excel - Classeur1", 0, 0, 0, 640, 480);
    name = sim_create(top, "XLCOMBO", "", 0, 4, 100, 100, 118);
    bar = sim_create(top, "EXCEL6", "", 0, 170, 100, 630, 118);
    grid = sim_create(top, "EXCEL9", "Classeur1", 0, 0, 116, 620, 460);
    tabs = sim_create(grid, "SCROLLBAR", "", SBS_HORZ, 380, 440, 610, 456);
    xl_name = EpGetDC(name);
    xl_bar = EpGetDC(bar);
    dc = EpGetDC(grid);
    xl_inv_dc = dc;
    EpExtTextOut(dc, 61, 1, 0, NULL, "A", 1, NULL);
    EpExtTextOut(dc, 141, 1, 0, NULL, "B", 1, NULL);
    EpExtTextOut(dc, 221, 1, 0, NULL, "C", 1, NULL);
    EpExtTextOut(dc, 9, 18, 0, NULL, "1", 1, NULL);
    EpExtTextOut(dc, 9, 35, 0, NULL, "2", 1, NULL);
    EpExtTextOut(dc, 9, 52, 0, NULL, "3", 1, NULL);
    EpExtTextOut(dc, 160, 18, 0, NULL, "12", 2, NULL);      /* B1, aligné à droite */
    EpExtTextOut(dc, 160, 35, 0, NULL, "36", 2, NULL);      /* B2 = B1*3 */
    (void) tabs;                        /* onglets dessinés dans la feuille */
    tdc = EpGetDC(grid);
    sim_dc[tdc].bold = TRUE;
    EpExtTextOut(tdc, 74, 326, 0, NULL, "Feuil1", 6, NULL);
    sim_dc[tdc].bold = FALSE;
    EpExtTextOut(tdc, 123, 326, 0, NULL, "Feuil2", 6, NULL);
    xl_select("B2", "=B1*3");
    sim_active = top;
    sim_focus = grid;
    ap_reset();
    run(6000);                          /* (titre dit à 15 caractères par seconde) */
    from = 0;
    CHECK(spoken_since(from, "Feuil1, B2, 36, formule égal B1 étoile 3") >= 0);
    /* Entrée : B3, repeint limité à B2 et B3 */
    from = spoken_n;
    sim_clip(dc, 100, 34, 180, 68);
    EpPatBlt(dc, 0, 0, 620, 344, PATCOPY);
    {
        RECT r;
        CHECK(EpOsmFind(grid, 0, 116, 620, 460, "A", &r) && r.left == 61);
        CHECK(!EpOsmFind(grid, 0, 116, 620, 460, "36", &r));   /* B2 repeint */
    }
    EpExtTextOut(dc, 160, 35, 0, NULL, "36", 2, NULL);
    xl_select("B3", "");
    run(800);
    CHECK(spoken_since(from, "B3, vide") >= 0);
    from = spoken_n;
    xl_select("B2", "=B1*3");           /* valeur toujours lue dans la grille */
    run(3000);
    CHECK(spoken_since(from, "B2, 36, formule égal B1 étoile 3") >= 0);
    from = spoken_n;
    xl_select("B1", "12");
    run(800);
    CHECK(spoken_since(from, "B1, 12") >= 0);
    from = spoken_n;
    insert_cmd(VK_UP);
    run(600);
    CHECK(spoken_since(from, "B1, 12") >= 0);
    /* Ctrl+G : Excel change l'attribut sans redessiner le texte ; Inser+F fait
       repeindre la cellule avant de lire sa police */
    xl_grid = dc;
    xl_bold = TRUE;
    sim_update = xl_paint;
    from = spoken_n;
    insert_cmd('F');
    run(3000);
    sim_update = NULL;
    CHECK(spoken_n > from && strstr(spoken[from], "gras") != NULL);
    /* Suppr : contenu effacé sans changer de cellule ; il est annoncé */
    EpPatBlt(xl_bar, 0, 0, 460, 18, PATCOPY);
    EpPatBlt(dc, 105, 17, 80, 17, PATCOPY);
    from = spoken_n;
    key(VK_DELETE);
    run(1500);
    CHECK(spoken_since(from, "vide") >= 0);
    /* Maj+Droite, Maj+Bas depuis B2 : la plage s'étend, la cellule active reste B2 ;
       les cellules ajoutées sont inversées */
    xl_select("B2", "=B1*3");
    run(1500);
    from = spoken_n;
    xl_invert(186, 35, 264, 51, 0, 0, 0, 0);             /* C2 */
    run(800);
    CHECK(spoken_since(from, "sélection B2 à C2") >= 0);
    from = spoken_n;
    xl_invert(106, 52, 264, 68, 0, 0, 0, 0);             /* B3:C3 */
    run(800);
    CHECK(spoken_since(from, "sélection B2 à C3") >= 0);
    from = spoken_n;
    xl_invert(186, 35, 264, 51, 106, 52, 264, 68);       /* Échap : plage moins B2 */
    run(800);
    CHECK(spoken_since(from, "B2") >= 0);
    CHECK(sim_rgn_live <= 1);           /* région de la grille seulement, aucune fuite */
    /* Maj+Bas puis Maj+Droite : équerre C2 + B3:C3 en une seule région */
    from = spoken_n;
    xl_invert(106, 52, 185, 68, 0, 0, 0, 0);             /* B3 */
    run(800);
    CHECK(spoken_since(from, "sélection B2 à B3") >= 0);
    from = spoken_n;
    xl_invert(186, 35, 264, 51, 186, 52, 264, 68);       /* C2:C3 */
    run(800);
    CHECK(spoken_since(from, "sélection B2 à C3") >= 0);
    /* Entrée : grille repeinte (fond opaque) ; la sélection disparaît du modèle */
    {
        RECT r;
        EpPatBlt(EpGetDC(grid), 0, 0, 620, 344, PATCOPY);
        CHECK(!EpOsmInvBox(grid, 0, 0, 640, 480, &r));
    }
}

/* Invite MS-DOS (dosbox.c) : fenêtre TTY active, Épaudos en mode invité. Sa parole
   est relayée de la boîte aux lettres vers l'Apollo II, les échos lui reviennent,
   les touches lui sont laissées ; en quittant, Épaulard reprend le clavier. */
static BYTE dos_mem[2048];
static void dos_put(const char *t)
{
    DOSBOX *b = (DOSBOX *) (dos_mem + 16);
    for (; *t; t++) {
        dos_mem[b->buf + dos_mem[b->head]] = (BYTE) *t;
        dos_mem[b->head]++;
    }
}
/* Trames Baum d'Épaudos (file de son port 1) et réponses reçues (rx port 1) */
static void dos_baum(const char *t, int n)
{
    DOSBOX *b = (DOSBOX *) (dos_mem + 16);
    for (; n > 0; n--, t++) {
        dos_mem[b->buf + 256 + dos_mem[b->head + 1]] = (BYTE) *t;
        dos_mem[b->head + 1]++;
    }
}
static BOOL dos_brx_has(const char *t, int n)
{
    DOSBOX *b = (DOSBOX *) (dos_mem + 16);
    BYTE got[256];
    int k = 0, i;
    while (dos_mem[b->rxtail + 1] != dos_mem[b->rxhead + 1]) {
        got[k++] = dos_mem[b->rxbuf + 256 + dos_mem[b->rxtail + 1]];
        dos_mem[b->rxtail + 1]++;
    }
    for (i = 0; i + n <= k; i++)
        if (!memcmp(got + i, t, n)) return TRUE;
    printf("trames reçues par Épaudos :");
    for (i = 0; i < k; i++) printf(" %02x", got[i]);
    printf("\n");
    return FALSE;
}
static void baum_in(const char *t, int n)
{
    SIMCOM *c = &sim_com[3];
    for (; n > 0; n--) c->in[c->in_n++] = (BYTE) *t++;
}
static void test_dos_box(void)
{
    HWND pm, tty;
    DOSBOX *b = (DOSBOX *) (dos_mem + 16);
    int from;

    boot();
    memset(dos_mem, 0, sizeof(dos_mem));
    memcpy(b->sig, "EPBX", 4);
    b->version = 2;
    b->head = 100; b->tail = 102; b->buf = 256;        /* (uart_head[2], uart_tail[2]...) */
    b->rxhead = 104; b->rxtail = 106; b->rxbuf = 1024;
    sim_epaudos_seg = dos_mem;
    sim_epaudos_box = 16;
    box_tried = FALSE;
    box = NULL;
    pm = sim_create(0, "Progman", "Gestionnaire de programmes", 0, 0, 0, 640, 480);
    tty = sim_create(0, "TTY", "COMMAND", 0, 0, 0, 640, 400);
    sim_active = sim_focus = pm;
    run(3000);
    dos_put("vieux\r");                 /* avant le relais : oublié */
    sim_active = sim_focus = tty;
    run(3000);
    CHECK(dos_relay && b->relay == 1 && pass_keys_sim);
    /* plein écran : boucle éveillée après une frappe, endormie ensuite */
    run(5000);
    CHECK(!dos_spin());
    b->keys++;
    run(100);
    CHECK(dos_spin());
    from = spoken_n;
    dos_put("C:\\>dir\r@EA\r");
    run(3000);
    CHECK(spoken_since(from, "C: barre oblique inverse > dir") >= 0 ||
          spoken_since(from, "C:\\>dir") >= 0);
    CHECK(spoken_since(0, "vieux") < 0);
    CHECK(dos_mem[b->rxhead] == 1 && dos_mem[b->rxbuf] == 'A');   /* écho rendu */
    CHECK(b->alive != 0);
    /* braille : sonde d'Épaudos, largeur répondue sans passer par la plage */
    dos_baum("\x1b\x15\x01", 3);
    run(200);
    CHECK(dos_brx_has("\x1b\x01\x28", 3));
    /* ses cellules (déjà traduites) affichées, ESC doublé compris */
    {
        BYTE f[90];
        int n = 0, i;
        const char *t = "c:\\>dir";
        f[n++] = 0x1b; f[n++] = 1;
        for (i = 0; i < 40; i++) {
            BYTE c = i < (int) strlen(t) ? br_fr[(BYTE) t[i]] : 0;
            if (i == 7) c |= 0xc0;      /* curseur */
            f[n++] = c;
            if (c == 0x1b) f[n++] = 0x1b;
        }
        dos_baum((const char *) f, n);
    }
    run(300);
    baum_step();
    CHECK(!strncmp(br_shown, "c:\\>dir", 7));
    /* touches de la plage : à Épaudos, pas au défilement d'Épaulard */
    from = br_offset();
    baum_in("\x1b\x24\x10\x1b\x24\x00\x1b\x27\x03", 9);
    run(300);
    CHECK(dos_brx_has("\x1b\x24\x10\x1b\x24\x00\x1b\x27\x03", 9));
    CHECK(br_offset() == from);
    /* trame coupée (Épaudos interrompu) puis une neuve : recalage */
    dos_baum("\x1b\x01\x01\x02", 4);
    dos_baum("\x1b\x15\x01", 3);
    run(200);
    CHECK(dos_brx_has("\x1b\x01\x28", 3));
    sim_active = sim_focus = pm;
    run(1500);
    CHECK(!dos_relay && b->relay == 0 && !pass_keys_sim);
    baum_step();
    CHECK(strncmp(br_shown, "c:\\>dir", 7) != 0);    /* affichage d'Épaulard revenu */
    sim_epaudos_seg = NULL;
}

/* Installateur ACME : jauge « 45% » dessinée, dite par paliers ; le focus rendu au
   bouton Annuler à chaque fichier copié n'est dit qu'une fois. */
static void test_installer_progress(void)
{
    HWND dlg, gauge, cancel;
    HDC dc;
    int i, n = 0, from;

    boot();
    dlg = sim_create(0, "#32770", "Installation de Microsoft Excel 5.0c", 0, 100, 100, 500, 260);
    gauge = sim_create(dlg, "Gauge", "", 0, 120, 160, 480, 180);
    cancel = sim_create(dlg, "Button", "Annuler", 0, 260, 200, 340, 224);
    dc = EpGetDC(gauge);
    EpExtTextOut(dc, 170, 2, ETO_OPAQUE, NULL, "12%", 3, NULL);
    sim_active = dlg;
    sim_focus = cancel;
    run(4000);
    from = spoken_n;
    for (i = 0; i < 6; i++) {
        sim_focus = dlg;
        run(300);
        sim_focus = cancel;
        run(700);
    }
    for (i = from; i < spoken_n; i++)
        if (strstr(spoken[i], "Annuler")) n++;
    CHECK(n <= 1);                      /* (au plus une fois : première annonce composée) */
    from = spoken_n;
    EpExtTextOut(dc, 170, 2, ETO_OPAQUE, NULL, "17%", 3, NULL);
    run(500);
    EpExtTextOut(dc, 170, 2, ETO_OPAQUE, NULL, "23%", 3, NULL);
    run(2000);
    CHECK(spoken_since(from, "17 pour cent") < 0);
    CHECK(spoken_since(from, "23 pour cent") >= 0);
    /* une touche : la même annonce redevient légitime (Tab dans une boîte) */
    from = spoken_n;
    key(VK_TAB);
    run(100);
    sim_focus = dlg;
    run(300);
    sim_focus = cancel;
    run(2000);
    for (i = from, n = 0; i < spoken_n; i++)
        if (strstr(spoken[i], "Annuler")) n++;
    CHECK(n == 1);
}

/* Installateur Lotus (Ami Pro) : boutons à icône sans texte, libellé STATIC à droite */
static void test_icon_buttons(void)
{
    HWND dlg, b1, b2, out;
    char d[TXT * 2];

    boot();
    dlg = sim_create(0, "#32770", "Menu principal", 0, 90, 120, 550, 320);
    sim_create(dlg, "Static", "Sélectionnez l'une des options suivantes :", 0, 98, 150, 400, 166);
    b1 = sim_create(dlg, "Button", "", BS_OWNERDRAW, 120, 168, 168, 212);
    sim_create(dlg, "Static", "Ami Pro sur ordinateur monoposte", 0, 176, 182, 400, 198);
    b2 = sim_create(dlg, "Button", "", BS_OWNERDRAW, 152, 216, 200, 260);
    sim_create(dlg, "Static", "Ami Pro en réseau", 0, 210, 230, 400, 246);
    out = sim_create(dlg, "Button", "Quitter", 0, 460, 150, 545, 172);
    (void) out;
    describe(b1, d);
    CHECK_STR(d, "Ami Pro sur ordinateur monoposte, bouton");
    describe(b2, d);
    CHECK_STR(d, "Ami Pro en réseau, bouton");
}

/* Boîte Format de cellule d'Excel (bosa_sdm_XL) : formulaire avec listes, pas un
   message, donc pas lue en entier à l'ouverture ; au changement d'onglet, Excel
   efface la page sous une région qui exclut ses boutons : leur texte reste. */
static void test_excel_dialog(void)
{
    HWND dlg, sb, ed;
    HDC dd;
    int i;
    RECT r;

    boot();
    dlg = sim_create(0, "bosa_sdm_XL", "Format de cellule", 0, 60, 60, 580, 400);
    sb = sim_create(dlg, "SCROLLBAR", "", SBS_VERT, 260, 140, 276, 260);
    ed = sim_create(dlg, "EDTBX", "", 0, 80, 300, 300, 318);
    (void) sb; (void) ed;
    dd = EpGetDC(dlg);
    sim_dc[dd].bold = TRUE;
    EpExtTextOut(dd, 20, 6, 0, NULL, "Nombre", 6, NULL);
    sim_dc[dd].bold = FALSE;
    EpExtTextOut(dd, 90, 6, 0, NULL, "Alignement", 10, NULL);
    EpExtTextOut(dd, 20, 40, 0, NULL, "Catégorie:", 10, NULL);
    EpExtTextOut(dd, 20, 60, 0, NULL, "Tous", 4, NULL);
    EpExtTextOut(dd, 200, 40, 0, NULL, "Codes des formats:", 18, NULL);
    EpExtTextOut(dd, 452, 40, 0, NULL, "OK", 2, NULL);
    EpExtTextOut(dd, 440, 70, 0, NULL, "Annuler", 7, NULL);
    run(300);
    ap_reset();
    sim_active = dlg;
    sim_focus = dlg;
    run(4000);
    CHECK(spoken_n >= 1 && !strcmp(spoken[0], "Format de cellule"));
    for (i = 0; i < spoken_n; i++)
        CHECK(strstr(spoken[i], "Catégorie") == NULL);
    /* Ctrl+Tab : page Alignement, effacée sous une région sans les boutons */
    sim_clip(dd, 0, 30, 520, 340);
    sim_clip_hole(dd, 430, 30, 520, 120);
    EpPatBlt(dd, 0, 30, 520, 310, PATCOPY);
    CHECK(!EpOsmFind(dlg, 60, 60, 580, 400, "Tous", &r));
    CHECK(EpOsmFind(dlg, 60, 60, 580, 400, "OK", &r));
    CHECK(EpOsmFind(dlg, 60, 60, 580, 400, "Annuler", &r));
    /* Tab vers Orientation (sans rectangle de focus) : Excel efface en XOR celui de
       « Bas », tracé une seconde fois ; ce n'est pas un nouveau focus */
    EpExtTextOut(dd, 210, 130, 0, NULL, "Bas", 3, NULL);
    wd.dc = dd;
    wd_frame(208, 129, 30, 15);
    run(3000);
    ap_reset();
    key(VK_TAB);
    wd_frame(208, 129, 30, 15);
    run(3000);
    CHECK(spoken_n == 0);
    memset(&wd, 0, sizeof(wd));
}

/* Accueil de l'installateur Lotus : trois longs paragraphes STATIC, plus de 512
   caractères en tout, tous lus avant le bouton */
static void test_long_message(void)
{
    HWND dlg, ok;
    static char p1[300], p2[300], p3[300];
    int i;

    boot();
    for (i = 0; i < 280; i++) {
        p1[i] = (char) ('a' + i % 26);
        p2[i] = (char) ('a' + i % 26);
        p3[i] = (char) ('a' + i % 26);
    }
    memcpy(p1, "Premier ", 8);
    memcpy(p2, "Deuxième ", 9);
    memcpy(p3, "Troisième ", 10);
    memcpy(p3 + 270, " fin.", 6);
    dlg = sim_create(0, "#32770", "Bienvenue dans le programme Install", 0, 40, 60, 600, 400);
    sim_create(dlg, "Static", p1, 0, 50, 90, 480, 140);
    sim_create(dlg, "Static", p2, 0, 50, 150, 480, 200);
    sim_create(dlg, "Static", p3, 0, 50, 210, 480, 260);
    ok = sim_create(dlg, "Button", "Continuer", 0, 490, 90, 580, 112);
    ap_reset();
    sim_active = dlg;
    sim_focus = ok;
    run(200);
    CHECK(spoken_n == 1);
    if (spoken_n == 1) {
        CHECK(strstr(spoken[0], "Troisième") != NULL);
        CHECK(strstr(spoken[0], "fin") != NULL);
        CHECK(strstr(spoken[0], "Continuer, bouton") != NULL);
    }
}

/* Nouvelle table d'Access 2.0 : boutons à image (CIRRUSSBUTTON) dont le texte est le
   nom du bitmap ; leur libellé est le STATIC centré qui les précède */
static void test_picture_buttons(void)
{
    HWND dlg, b1, b2;
    char d[TXT * 2];
    int i;

    boot();
    dlg = sim_create(0, "#32770", "Nouvelle table", 0, 150, 120, 470, 300);
    sim_create(dlg, "Static", "Table &vierge", SS_CENTER, 170, 140, 290, 156);
    b1 = sim_create(dlg, "CirrusSButton", "#bmpBlnkTbl", 0, 180, 160, 280, 240);
    sim_create(dlg, "Static", "&Assistants", SS_CENTER, 310, 140, 430, 156);
    b2 = sim_create(dlg, "CirrusSButton", "#bmpTblWiz", 0, 320, 160, 420, 240);
    sim_create(dlg, "Button", "Annuler", 0, 260, 260, 360, 284);
    describe(b1, d);
    CHECK_STR(d, "Table vierge, bouton");
    describe(b2, d);
    CHECK_STR(d, "Assistants, bouton");
    ap_reset();
    sim_active = dlg;
    sim_focus = b1;
    run(300);
    CHECK(spoken_n == 1);
    if (spoken_n == 1)
        CHECK_STR(spoken[0], "Nouvelle table, Table vierge, bouton");
    for (i = 0; i < spoken_n; i++)
        CHECK(strstr(spoken[i], "bmp") == NULL);
}

/* Feuille de données d'Access : grille OGrid (en-têtes, sélecteurs de ligne en images),
   éditeur OKttbx placé sur la cellule active ; ligne, colonne puis valeur */
static void grid_editor_at(HWND ed, int l, int t, int r, int b, const char *v)
{
    HDC e;
    sim_win[ed].rc.left = l; sim_win[ed].rc.top = t;
    sim_win[ed].rc.right = r; sim_win[ed].rc.bottom = b;
    e = EpGetDC(ed);
    EpExtTextOut(e, 2, 0, 0, NULL, v, (UINT) strlen(v), NULL);
}
static void test_grid_editor(void)
{
    HWND top, grid, ed;
    HDC g, m;
    int from;

    boot();
    top = sim_create(0, "OMain", "Microsoft Access", 0, 0, 0, 640, 480);
    grid = sim_create(top, "OGrid", "", 0, 60, 100, 600, 300);
    g = EpGetDC(grid);
    m = sim_memdc();
    EpExtTextOut(g, 40, 0, 0, NULL, "Nom", 3, NULL);
    EpExtTextOut(g, 140, 0, 0, NULL, "Ville", 5, NULL);
    EpBitBlt(g, 2, 17, 14, 12, m, 0, 0, SRCCOPY);       /* sélecteurs de ligne */
    EpBitBlt(g, 2, 33, 14, 12, m, 0, 0, SRCCOPY);
    EpExtTextOut(g, 22, 16, 0, NULL, "Dupont", 6, NULL);
    EpExtTextOut(g, 122, 16, 0, NULL, "Paris", 5, NULL);
    EpExtTextOut(g, 22, 32, 0, NULL, "Martin", 6, NULL);
    EpExtTextOut(g, 122, 32, 0, NULL, "Lyon", 4, NULL);
    ed = sim_create(grid, "OKttbx", "", 0, 180, 116, 280, 132);
    grid_editor_at(ed, 180, 116, 280, 132, "Paris");
    ap_reset();
    sim_active = top;
    sim_focus = ed;
    run(3000);
    CHECK(spoken_since(0, "Dupont, Ville, Paris") >= 0);
    from = spoken_n;
    grid_editor_at(ed, 180, 132, 280, 148, "Lyon");     /* Bas : même colonne */
    run(1500);
    CHECK(spoken_since(from, "Martin, Lyon") >= 0);
    from = spoken_n;
    grid_editor_at(ed, 80, 132, 180, 148, "Martin");    /* Maj+Tab : même ligne */
    run(1500);
    CHECK(spoken_since(from, "Nom, Martin") >= 0);
    from = spoken_n;
    insert_cmd(VK_UP);
    run(1500);
    CHECK(spoken_since(from, "Nom, Martin") >= 0);
    /* éditeur recréé sur la même cellule (Access) : rien de plus */
    from = spoken_n;
    {
        HWND ed2 = sim_create(grid, "OKttbx", "", 0, 80, 132, 180, 148);
        grid_editor_at(ed2, 80, 132, 180, 148, "Martin");
        sim_focus = ed2;
        run(1500);
        CHECK(spoken_n == from);
    }
}

/* F2 dans Excel : déplacement dans la formule, caractère par caractère et par mot,
   malgré les fragments redessinés autour du curseur */
static void xe_step(UINT vk, BOOL ctrl, const char *expect)
{
    int from = spoken_n;
    sim_keys[VK_CONTROL] = ctrl ? 1 : 0;
    key(vk);
    app_keys();
    sim_keys[VK_CONTROL] = 0;
    run(1500);
    if (!(spoken_n > from && !strcmp(spoken[spoken_n - 1], expect)))
        printf("ÉCHEC F2 Excel : « %s » attendu, lu « %s »\n", expect,
               spoken_n > from ? spoken[spoken_n - 1] : "(rien)"), failures++;
}
static void test_excel_edit(void)
{
    boot();
    memset(&xe, 0, sizeof(xe));
    xe.t = "=SOMME(B1:B3)*2";
    xe.top = sim_create(0, "XLMAIN", "Microsoft Excel - Classeur1", 0, 0, 0, 640, 480);
    xe.ed = sim_create(xe.top, "EXCEL7", "", 0, 28, 134, 300, 150);
    xe.dc = EpGetDC(xe.ed);
    EpExtTextOut(xe.dc, 0, 0, 0, NULL, xe.t, (UINT) strlen(xe.t), NULL);
    xe.col = (int) strlen(xe.t);
    EpPatBlt(xe.dc, xe.col * 8, 0, 1, 16, DSTINVERT);
    sim_active = xe.top;
    sim_focus = xe.ed;
    run(1500);
    ap_reset();
    xe_step(VK_LEFT, FALSE, "2");
    xe_step(VK_LEFT, FALSE, "étoile");
    xe_step(VK_LEFT, FALSE, "parenthèse fermante");
    xe_step(VK_HOME, FALSE, "égal");
    xe_step(VK_RIGHT, FALSE, "S");
    xe_step(VK_RIGHT, FALSE, "O");
    xe_step(VK_RIGHT, FALSE, "M");
    xe_step(VK_END, FALSE, "fin de ligne");
    xe_step(VK_LEFT, FALSE, "2");
    xe_step(VK_LEFT, TRUE, "B3");
    xe_step(VK_LEFT, TRUE, "B1");
    xe_step(VK_LEFT, TRUE, "SOMME");
    xe_step(VK_LEFT, TRUE, "égal");
}

/* ------------------------------------------------- mode navigation (pages Web) */

/* Faux DDEML (Netscape) et faux Winsock (proxy de pc486) */
static BOOL fn_on;
static char fn_info[300];               /* réponse de WWW_GetWindowInfo */
static char fn_opened[300];             /* élément du dernier WWW_OpenURL */
static char fn_request[700];            /* dernière requête reçue par le faux proxy */
static int fn_requests;
static const char *fn_body;             /* corps de la réponse ; NULL : 404 */
static int fn_body_n;
static char fn_str[16][300];
static int fn_nstr;
static HWND fn_hwnd;
static UINT fn_msg;
static char fn_resp[9000];
static int fn_resp_n, fn_resp_at;

static HMODULE fn_load(LPCSTR n)
{
    if (!fn_on) return 2;
    if (!strcmp(n, "DDEML.DLL")) return 40;
    if (!strcmp(n, "WINSOCK.DLL")) return 41;
    return 2;
}
static UINT fn_ddeinit(DWORD *inst, FARPROC cb, DWORD a, DWORD b) { (void) cb; (void) a; (void) b; *inst = 1; return 0; }
static DWORD fn_strnew(DWORD inst, LPCSTR t, int cp)
{
    (void) inst; (void) cp;
    lstrcpyn(fn_str[fn_nstr % 16], t, 300);
    return (DWORD) (fn_nstr++ % 16) + 1;
}
static BOOL fn_strfree(DWORD inst, DWORD h) { (void) inst; (void) h; return TRUE; }
static DWORD fn_connect(DWORD inst, DWORD svc, DWORD top, void *c) { (void) inst; (void) c; return strcmp(fn_str[svc - 1], "NETSCAPE") ? 0 : top; }
static BOOL fn_disc(DWORD c) { (void) c; return TRUE; }
static DWORD fn_trans(void *d, DWORD cb, DWORD conv, DWORD item, UINT fmt, UINT type, DWORD to, DWORD *res)
{
    (void) d; (void) cb; (void) fmt; (void) type; (void) to; (void) res;
    if (!strcmp(fn_str[conv - 1], "WWW_GetWindowInfo")) return 1;
    if (!strcmp(fn_str[conv - 1], "WWW_OpenURL")) { strcpy(fn_opened, fn_str[item - 1]); return 2; }
    return 0;
}
static DWORD fn_get(DWORD h, void *out, DWORD max, DWORD off)
{
    const char *t = h == 1 ? fn_info : "0x1";
    (void) off;
    lstrcpyn((char *) out, t, (int) max);
    return (DWORD) strlen((char *) out) + 1;
}
static BOOL fn_free(DWORD h) { (void) h; return TRUE; }
static int fn_startup(WORD v, void *d) { (void) v; (void) d; return 0; }
static UINT fn_socket(int a, int b, int c) { (void) a; (void) b; (void) c; return 7; }
static int fn_async(UINT s, HWND h, UINT m, long ev) { (void) s; (void) ev; if (m) { fn_hwnd = h; fn_msg = m; } return 0; }
static int fn_wconnect(UINT s, const void *sa, int n)
{
    (void) sa; (void) n;
    PostMessage(fn_hwnd, fn_msg, s, MAKELONG(0x10, 0));
    return -1;                          /* WSAEWOULDBLOCK */
}
static int fn_send(UINT s, const char *b, int n, int f)
{
    (void) f;
    memcpy(fn_request, b, n < 699 ? n : 699);
    fn_request[n < 699 ? n : 699] = 0;
    fn_requests++;
    if (fn_body) {
        strcpy(fn_resp, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n");
        fn_resp_n = (int) strlen(fn_resp);
        memcpy(fn_resp + fn_resp_n, fn_body, fn_body_n);
        fn_resp_n += fn_body_n;
    } else {
        strcpy(fn_resp, "HTTP/1.0 404 Not Found\r\n\r\nPC486-TAMPON 0\nabsent\n");
        fn_resp_n = (int) strlen(fn_resp);
    }
    fn_resp_at = 0;
    PostMessage(fn_hwnd, fn_msg, s, MAKELONG(0x01, 0));
    PostMessage(fn_hwnd, fn_msg, s, MAKELONG(0x20, 0));
    return n;
}
static int fn_recv(UINT s, char *b, int n, int f)
{
    int k = fn_resp_n - fn_resp_at;
    (void) s; (void) f;
    if (k <= 0) return -1;
    if (k > n) k = n;
    memcpy(b, fn_resp + fn_resp_at, k);
    fn_resp_at += k;
    return k;
}
static int fn_close(UINT s) { (void) s; return 0; }
static FARPROC fn_proc(HMODULE m, LPCSTR n)
{
    static const struct { const char *name; FARPROC f; } t[] = {
        {"DdeInitialize", (FARPROC) fn_ddeinit}, {"DdeCreateStringHandle", (FARPROC) fn_strnew},
        {"DdeFreeStringHandle", (FARPROC) fn_strfree}, {"DdeConnect", (FARPROC) fn_connect},
        {"DdeDisconnect", (FARPROC) fn_disc}, {"DdeClientTransaction", (FARPROC) fn_trans},
        {"DdeGetData", (FARPROC) fn_get}, {"DdeFreeDataHandle", (FARPROC) fn_free},
        {"WSAStartup", (FARPROC) fn_startup}, {"socket", (FARPROC) fn_socket},
        {"connect", (FARPROC) fn_wconnect}, {"send", (FARPROC) fn_send},
        {"recv", (FARPROC) fn_recv}, {"closesocket", (FARPROC) fn_close},
        {"WSAAsyncSelect", (FARPROC) fn_async}
    };
    int i;
    (void) m;
    for (i = 0; i < (int) (sizeof(t) / sizeof(t[0])); i++)
        if (!strcmp(t[i].name, n)) return t[i].f;
    return 0;
}

/* touche prise par EPAUHOOK.DLL en mode navigation ; fl : 1 Maj, 2 Ctrl, 4 Alt */
static void nav_press(UINT vk, int fl)
{
    ap_reset();
    PostMessage(hwnd_main, EPM_NAV, vk, fl);
    run(6000);
}
static const char *said(void) { return spoken_n ? spoken[spoken_n - 1] : ""; }
static void show(const char *what) { int i; printf("[%s]", what); for (i = 0; i < spoken_n; i++) printf(" « %s »", spoken[i]); printf("\n"); }

static HWND nav_frame, nav_fields[13];
static void nav_make_fields(void);

/* Netscape affiche http://pc486/essai : vue, rangées visibles, champs du formulaire */
static void nav_page(void)
{
    static const char *rows[] = { "Aller au lien n\xb0", "Aller au contenu",
        "En-t\xeate du site d'essai : Accueil pc486", "Titres", "Tableaux", "Formulaire",
        "Page d'essai du mode navigation" };
    int i;
    nav_frame = sim_create(0, "AfxFrameOrView", "Netscape - [Page d'essai du mode navigation]",
                           0, 0, 0, 420, 200);
    hp.doc = hp.topic = sim_create(nav_frame, "AfxWnd", "", 0, 0, 40, 420, 40 + 16 * H_ROWS);
    sim_win[hp.doc].id = 0xE900;
    hp.dc = EpGetDC(hp.doc);
    for (i = 0; i < 7; i++)
        hp.lines[i] = rows[i];
    hp.n = 7;
    hp.top = 0;
    hp_draw();
    nav_make_fields();
    sim_active = nav_frame;
    sim_focus = hp.doc;
}

/* champs du formulaire, créés dans l'ordre du document ; hors de la partie affichée :
   cachés */
static void nav_make_fields(void)
{
    static const struct { const char *cls; LONG style; const char *value; int check; } f[13] = {
        {"Edit", 0, "", 0}, {"Button", BS_PUSHBUTTON, "", 0}, {"Edit", 0, "Dupont", 0},
        {"Edit", ES_PASSWORD, "", 0}, {"Button", BS_AUTOCHECKBOX, "", 1},
        {"Button", BS_AUTOCHECKBOX, "", 0}, {"Button", BS_RADIOBUTTON, "", 0},
        {"Button", BS_RADIOBUTTON, "", 1}, {"Button", BS_RADIOBUTTON, "", 0},
        {"ComboBox", 3, "", 0}, {"Edit", ES_MULTILINE, "Texte initial", 0},
        {"Button", BS_PUSHBUTTON, "", 0}, {"Button", BS_PUSHBUTTON, "", 0}
    };
    int i;
    for (i = 0; i < 13; i++) {
        HWND c = sim_create(hp.doc, f[i].cls, i == 1 ? "Aller" : "", f[i].style,
                            i < 2 ? 150 + 60 * i : 71, i < 2 ? 40 : -111, i < 2 ? 200 + 60 * i : 295,
                            i < 2 ? 56 : -87);
        sim_win[c].id = (WORD) (25018 + i);
        sim_win[c].proc = nsf_proc;
        if (i >= 2)
            sim_win[c].style &= ~WS_VISIBLE;
        strcpy(nsf_value[c], f[i].value);
        nsf_check[c] = f[i].check;
        nav_fields[i] = c;
    }
}

static char said_all[6000];
static BOOL heard(const char *t)
{
    int i;
    said_all[0] = 0;
    for (i = 0; i < spoken_n; i++) {
        if (strlen(said_all) + strlen(spoken[i]) + 4 >= sizeof(said_all)) break;
        if (i) strcat(said_all, " / ");
        strcat(said_all, spoken[i]);
    }
    if (strstr(said_all, t)) return TRUE;
    printf("  entendu : « %s » ; attendu : « %s »\n", said_all, t);
    return FALSE;
}
static BOOL br_starts(const char *t)
{
    int i;
    for (i = 0; t[i]; i++)
        if ((br_shown[i] | 0x20) != (t[i] | 0x20)) {
            printf("  plage : « %s » ; attendu : « %s »\n", br_shown, t);
            return FALSE;
        }
    return TRUE;
}
#define NAV(vk, fl, t) do { nav_press((vk), (fl)); CHECK(heard(t)); } while (0)

/* Mode navigation d'une page de Netscape : structure demandée au proxy (faux Winsock)
   pour l'adresse donnée par DDE ; titres, liens, listes, tableaux, champs, activation,
   modes, dire tout, braille, changement de page, page hors proxy, titre différent. */
static void test_navweb(void)
{
    HWND *f = nav_fields;
    WORD h1;
    int n;

    boot();
    sim_load_hook = fn_load;
    sim_proc_hook = fn_proc;
    fn_on = TRUE;
    net_state = 0;
    fn_body = nav_essai;
    fn_body_n = NAV_ESSAI_LEN;
    fn_requests = 0;
    strcpy(fn_info, "\"http://pc486/essai\",\"Netscape - [Page d'essai du mode navigation]\"");
    nav_page();
    ap_reset();
    run(5000);
    CHECK(nav.loaded && nav.mapped && sim_nav_view == hp.doc && sim_nav_mode == 1);
    CHECK(fn_requests == 1 && !strncmp(fn_request,
          "GET http://pc486/tampon?url=http%3A%2F%2Fpc486%2Fessai HTTP/1.0\r\nHost: pc486\r\n", 75));
    CHECK(spoken_n <= 1);               /* page déjà là au lancement : rien d'office */

    /* titres : H, chiffres (niveau), Maj en arrière */
    NAV(VK_HOME, 2, "Aller au lien n\xb0 zone d'\xe9" "dition vide bouton Aller");
    NAV('H', 0, "fin de formulaire, r\xe9gion principal, titre niveau 1 Page d'essai du mode navigation");
    h1 = nav.pos;
    NAV('H', 0, "titre niveau 2 Titres et paragraphes");
    NAV('3', 0, "titre niveau 3 Sous-titre de niveau trois");
    NAV('4', 0, "titre niveau 4 Niveau quatre");
    NAV('4', 0, "pas de titre de niveau 4 suivant");
    NAV('2', 1, "titre niveau 2 Titres et paragraphes");
    NAV('H', 1, "titre niveau 1 Page d'essai du mode navigation");
    NAV('H', 1, "pas de titre pr\xe9" "c\xe9" "dent");
    CHECK(nav.pos == h1);
    /* braille : la ligne du curseur, abrégée */
    CHECK(br_starts("t1 page d'essai"));

    /* liens : K, visités (V), non visités (U) */
    NAV('K', 0, "lien visit\xe9 lien d\xe9j\xe0 visit\xe9");
    NAV('K', 0, "lien lien jamais visit\xe9");
    NAV('K', 1, "lien visit\xe9 lien d\xe9j\xe0 visit\xe9");
    NAV('U', 0, "lien lien jamais visit\xe9");
    NAV(VK_HOME, 2, "Aller au lien");
    NAV('V', 0, "r\xe9gion banni\xe8re, lien visit\xe9 Accueil pc486");
    NAV('U', 1, "lien dans la page Aller au contenu");

    /* listes, éléments, conteneurs (virgule), citation, séparateur */
    NAV('L', 0, "liste de 3 \xe9l\xe9ments, puce lien dans la page Titres");
    NAV('L', 0, "fin de liste, fin de r\xe9gion, r\xe9gion principal, liste de 3 \xe9l\xe9ments, puce Fruits");
    NAV(VK_DOWN, 0, "liste de 3 \xe9l\xe9ments, puce Pomme");
    NAV('I', 0, "puce Poire");
    NAV(0xBC, 0, "fin de liste, puce L\xe9gumes");
    NAV(0xBC, 1, "puce Fruits");
    NAV('L', 0, "liste de 3 \xe9l\xe9ments, puce Pomme");
    NAV('L', 0, "fin de liste, fin de liste, liste de 2 \xe9l\xe9ments, 1. Premier");
    NAV('Q', 1, "citation, Une citation");
    NAV('S', 0, "fin de citation, s\xe9parateur");
    NAV('G', 0, "image Soleil sur la colline");
    NAV('D', 1, "r\xe9gion principal, titre niveau 1");
    NAV('N', 0, "au milieu de la phrase.");
    NAV('M', 0, "pas de cadre suivant");
    NAV('O', 0, "pas d'objet incorpor\xe9 suivant");

    /* tableaux : T, cellules au clavier, en-têtes, cellule vide, cellules fusionnées */
    NAV('T', 0, "tableau de 6 lignes et 3 colonnes, Temp\xe9ratures de la semaine");
    NAV(VK_DOWN, 6, "ligne 1, colonne 1, Jour");    /* depuis la légende : 1re cellule */
    NAV('T', 1, "tableau de 6 lignes");
    NAV(VK_DOWN, 0, "ligne 1 colonne 1 Jour");
    NAV(VK_DOWN, 6, "ligne 2, Lundi");
    NAV(VK_DOWN, 6, "ligne 3, Mardi");
    NAV(VK_RIGHT, 6, "Matin, colonne 2, vide");
    NAV(VK_DOWN, 6, "Mercredi, ligne 4, sur 2 colonnes, pluie toute la journ\xe9" "e");
    NAV(VK_RIGHT, 6, "bord du tableau");
    NAV(VK_DOWN, 6, "Jeudi, ligne 5, 10");
    NAV(VK_LEFT, 6, "Jour, colonne 1, sur 2 lignes, Jeudi");
    NAV(VK_DOWN, 6, "bord du tableau");
    NAV(VK_NEXT, 6, "ligne 6, sur 2 lignes, Jeudi");
    NAV(VK_RIGHT, 6, "Matin, colonne 2, 11");
    NAV(VK_UP, 6, "Jeudi, ligne 5, 10");
    NAV(VK_DOWN, 0, "colonne 3 15");
    NAV('T', 1, "tableau de 6 lignes");
    NAV(VK_RIGHT, 6, "ligne 1, colonne 1, Jour");
    NAV(VK_HOME, 2, "Aller au lien");
    NAV(VK_RIGHT, 6, "pas dans un tableau");

    /* champs : libellés, états réels (fenêtres de Netscape) */
    /* champ visible dont le modèle n'a gardé qu'un morceau retracé (Netscape redessine
       la fin du texte après une saisie) : repeint, puis lu en entier */
    {
        HDC dc = GetDC(f[0]);
        strcpy(nsf_value[f[0]], "Marc");
        EpExtTextOut(dc, 2 + 2 * 8, 2, 0, NULL, "rc", 2, NULL);
        sim_update = nsf_update;
        NAV(VK_HOME, 2, "Aller au lien n\xb0 zone d'\xe9" "dition Marc bouton");
        sim_update = NULL;
        nsf_value[f[0]][0] = 0;
        clear(dc, 0, 0, 200, 20);
        DeleteDC(dc);
    }
    NAV(VK_HOME, 2, "Aller au lien");
    NAV('X', 0, "formulaire, Recevoir la lettre, case \xe0 cocher coch\xe9" "e");
    ap_reset();
    NAV(VK_RETURN, 0, "non coch\xe9" "e");
    CHECK(nsf_check[f[4]] == 0 && sim_focus == f[4] && sim_nav_mode == 1);
    CHECK(spoken_n == 1);               /* le focus posé sur la case n'est pas annoncé */
    /* focus déjà sur la case (cas réel : Netscape traite la touche après la
       scrutation) : le changement d'état n'est dit qu'une fois */
    NAV(VK_RETURN, 0, "coch\xe9" "e");
    CHECK(nsf_check[f[4]] == 1 && spoken_n == 1);
    NAV(VK_RETURN, 0, "non coch\xe9" "e");
    CHECK(spoken_n == 1);
    NAV('X', 0, "Recevoir les offres, case \xe0 cocher non coch\xe9" "e");
    NAV(VK_SPACE, 0, "coch\xe9" "e");
    CHECK(nsf_check[f[5]] == 1);
    NAV('R', 0, "Rouge, bouton radio non coch\xe9");
    NAV('R', 0, "Vert, bouton radio coch\xe9");
    NAV('R', 1, "Rouge, bouton radio non coch\xe9");
    NAV(VK_RETURN, 0, "coch\xe9");
    CHECK(nsf_check[f[6]] == 1 && nsf_check[f[7]] == 0);
    NAV('C', 0, "Ville :, liste d\xe9roulante Lyon");
    NAV('B', 0, "bouton Envoyer");
    NAV('F', 1, "Remarque :, zone d'\xe9" "dition multiligne Texte initial");
    NAV('E', 1, "Mot de passe :, mot de passe vide");
    NAV('E', 1, "Nom :, zone d'\xe9" "dition Dupont");
    /* Entrée sur une zone d'édition : focus, mode formulaire ; Échap : retour */
    NAV(VK_RETURN, 0, "mode formulaire, Nom :, zone d'\xe9" "dition Dupont");
    CHECK(sim_focus == f[2] && sim_nav_mode == 2 && !nav_here());
    NAV(VK_ESCAPE, 0, "mode navigation");
    CHECK(sim_nav_mode == 1 && nav.pos == nav_items[nav_item_of(f[2])].start);
    NAV('E', 0, "Mot de passe :, mot de passe vide");
    /* Inser+Espace : formulaire (focus au champ du curseur), puis navigation */
    ap_reset();
    insert_cmd(VK_SPACE);
    run(3000);
    CHECK(heard("mode formulaire") && sim_nav_mode == 0 && sim_focus == f[3]);
    ap_reset();
    insert_cmd(VK_SPACE);
    run(3000);
    CHECK(heard("mode navigation") && sim_nav_mode == 1);
    /* bouton : envoi, puis structure redemandée, position gardée */
    n = fn_requests;
    NAV('B', 0, "bouton Envoyer");
    h1 = nav.pos;
    nav_press(VK_RETURN, 0);
    CHECK(nsf_clicked == f[11] && fn_requests == n + 1 && nav.loaded && nav.pos == h1);
    /* Communicator 4 : boutons de commande dessinés par lui (BS_OWNERDRAW) */
    sim_win[f[11]].style = (sim_win[f[11]].style & ~0x0FL) | BS_OWNERDRAW;
    sim_win[f[12]].style = (sim_win[f[12]].style & ~0x0FL) | BS_OWNERDRAW;
    nav.mapped = FALSE;
    nsf_clicked = 0;
    nav_press(VK_RETURN, 0);
    CHECK(nsf_clicked == f[11]);
    /* Communicator 4 : liste déroulante dessinée par lui, sans textes */
    sim_win[f[9]].style |= CBS_OWNERDRAWFIXED;
    NAV('C', 1, "Ville :, liste d\xe9roulante Lyon");

    /* liens : Entrée ouvre l'adresse par DDE ; lien dans la page : son ancre */
    NAV(VK_HOME, 2, "Aller au lien");
    NAV('K', 0, "lien dans la page Aller au contenu");
    NAV(VK_RETURN, 0, "titre niveau 1 Page d'essai");
    NAV('K', 0, "lien visit\xe9 lien d\xe9j\xe0 visit\xe9");
    NAV('K', 0, "lien jamais");
    fn_opened[0] = 0;
    nav_press(VK_RETURN, 0);
    CHECK_STR(fn_opened, "\"http://pc486/jamais-visite\",,0xFFFFFFFF,0x0,,,");

    /* lecture : caractères, mots, lignes, paragraphes, début et fin */
    NAV(VK_HOME, 2, "Aller au lien");
    NAV(VK_DOWN, 0, "fin de formulaire, lien dans la page Aller au contenu");
    NAV(VK_DOWN, 0, "r\xe9gion banni\xe8re, En-t\xeate du site d'essai : lien visit\xe9 Accueil pc486");
    NAV(VK_RIGHT, 0, "n");
    NAV(VK_RIGHT, 2, "du");
    NAV(VK_LEFT, 2, "En-t\xeate");
    NAV(VK_END, 0, "6");
    NAV(VK_HOME, 0, "E");
    NAV(VK_DOWN, 2, "fin de r\xe9gion, r\xe9gion navigation, liste de 3 \xe9l\xe9ments, puce");
    NAV(VK_UP, 0, "fin de liste, fin de r\xe9gion, r\xe9gion banni\xe8re, En-t\xeate");
    NAV(VK_END, 2, "Accueil pc486");
    NAV(VK_DOWN, 0, "bas du document");
    NAV(VK_PRIOR, 0, "Mot de passe");

    /* dire tout depuis le curseur, interrompu par Ctrl : le curseur reste sur la ligne lue */
    NAV(VK_HOME, 2, "Aller au lien");
    NAV('H', 0, "titre niveau 1");
    h1 = nav.pos;
    ap_reset();
    insert_cmd(VK_DOWN);
    run(20000);
    CHECK(sa.on && sa.kind == 4);
    CHECK(heard("titre niveau 1 Page d'essai du mode navigation / Cette page sert"));
    ctrl_alone();
    run(500);
    CHECK(!sa.on && nav.pos > h1);
    h1 = nav.pos;
    nav_press(VK_DOWN, 0);
    CHECK(nav.pos == nav_next_line(h1));

    /* braille : Bas de la plage, routage (deux fois : activer) */
    NAV(VK_HOME, 2, "Aller au lien");
    run(500);
    CHECK(br_starts("aller au lien n? ed btn aller"));
    ap_reset();
    nav_br_key(BR_DOWN);
    run(2000);
    CHECK(heard("Aller au contenu") && br_starts("ln aller au contenu"));
    nav_br_key(BR_ROUTE + 3);
    nav_br_key(BR_ROUTE + 3);
    run(3000);
    CHECK(heard("titre niveau 1"));

    /* nouvelle page hors proxy : titre, puis lecture d'office du texte dessiné ; les
       champs de l'ancienne page disparaissent avec elle (focus perdu) */
    for (n = 0; n < 13; n++)
        sim_destroy(f[n]);
    sim_focus = 0;
    fn_body = NULL;
    strcpy(fn_info, "\"file:///C|/autre.htm\",\"Netscape - [Autre]\"");
    ap_reset();
    strcpy(sim_win[nav_frame].text, "Netscape - [Autre]");
    run(400);
    CHECK(!nav.loaded && sim_nav_mode == 0);
    run(15000);
    CHECK(!nav.loaded && heard("Netscape - [Autre] / Aller au lien"));
    ap_reset();
    insert_cmd(VK_SPACE);
    run(2000);
    CHECK(heard("pas de mode navigation pour cette page"));
    /* retour à la page d'essai : structure, puis dire tout depuis le haut */
    fn_body = nav_essai;
    strcpy(fn_info, "\"http://pc486/essai\",\"Netscape - [Page d'essai du mode navigation]\"");
    nav_make_fields();
    {
        /* retour arrière : Netscape remet le texte saisi (absent du tampon) ; ce n'est
           pas une page différente */
        clear(hp.dc, 0, 0, 400, 16);
        EpExtTextOut(hp.dc, 0, 0, 0, NULL, "MartinTexte initial", 19, NULL);
    }
    ap_reset();
    strcpy(sim_win[nav_frame].text, "Netscape - [Page d'essai du mode navigation]");
    run(12000);
    CHECK(nav.loaded && sa.on && sa.kind == 4);
    CHECK(heard("Netscape - [Page d'essai du mode navigation] / formulaire, Aller au lien"));
    CHECK(!nav.warned);
    ctrl_alone();
    run(500);
    /* contenu changé sous le même titre : deux rangées inconnues, alerte entendue */
    clear(hp.dc, 0, 0, 400, 32);
    EpExtTextOut(hp.dc, 0, 0, 0, NULL, "Zorglub xylophones", 18, NULL);
    EpExtTextOut(hp.dc, 0, 16, 0, NULL, "Quokkas wombats zzz", 19, NULL);
    ap_reset();
    insert_cmd(VK_F5);
    run(12000);
    CHECK(nav.warned && heard("Attention : la structure lue peut diff\xe9rer"));
    hp_draw();
    ctrl_alone();
    run(500);
    /* page affichée différente (titre) : structure refusée */
    strcpy(fn_info, "\"http://pc486/essai\",\"Netscape - [Une autre page]\"");
    strcpy(sim_win[nav_frame].text, "Netscape - [Une autre page]");
    run(8000);
    CHECK(!nav.loaded && sim_nav_mode == 0);

    sim_load_hook = NULL;
    sim_proc_hook = NULL;
    fn_on = FALSE;
    net_state = 0;
    hp.doc = hp.topic = 0;
}

/* Zone d'édition qui ne répond pas à WM_GETTEXT (installateur de QEMM 8) : texte
   lu par EM_GETLINE, au focus comme sous le curseur (avant : « fin de ligne »). */
static void test_empty_gettext_edit(void)
{
    HWND dlg;
    int from;

    boot();
    dlg = sim_create(0, "#32770", "Quarterdeck QEMM - Installation", 0, 0, 0, 400, 200);
    sim_create(dlg, "STATIC", "&Unité du disque :", 0, 130, 140, 350, 160);
    qm.edit = sim_create(dlg, "EDIT", "", 0x0088L, 90, 160, 380, 184);
    sim_win[qm.edit].proc = qm_proc;
    strcpy(qm.text, "E:\\QEMM\\DISK01");
    qm.pos = (int) strlen(qm.text);
    sim_active = dlg;
    sim_focus = qm.edit;
    run(800);
    CHECK(spoken_n > 0 && strstr(spoken[spoken_n - 1], "DISK01"));
    from = spoken_n;
    key(VK_LEFT);
    run(400);
    CHECK(spoken_since(from, "1") >= 0);
    from = spoken_n;
    key(VK_LEFT);
    run(400);
    CHECK(spoken_since(from, "0") >= 0);
    from = spoken_n;
    key(VK_HOME);
    run(400);
    CHECK(spoken_since(from, "E") >= 0);
    from = spoken_n;
    key(VK_END);
    run(400);
    CHECK(spoken_since(from, "fin de ligne") >= 0);
}

/* Alt+Tab (bascule rapide de Windows 3.1) : le titre du programme suivant est dit à
   chaque Tab, Alt tenu, dans l'ordre de Windows : fenêtres sans propriétaire, visibles,
   de haut en bas ; une fenêtre bloquée par sa boîte modale reste un programme. */
#define ALT_HELD 0x20000000L
static void alt_down(void) { PostMessage(hwnd_main, EPM_KEY, VK_MENU, ALT_HELD | 1L); }
static void alt_tab(BOOL shift)
{
    PostMessage(hwnd_main, EPM_KEY, VK_TAB, ALT_HELD | 1L | (shift ? EPK_SHIFT : 0));
}
static void test_alt_tab(void)
{
    HWND pm_top, hidden, qemm, box;
    int from;

    boot();
    pm_top = sim_create(0, "Progman", "Gestionnaire de programmes", 0, 0, 0, 640, 480);
    sim_create(0, "Notepad", "Bloc-notes - (Sans titre)", 0, 0, 0, 640, 480);
    hidden = sim_create(0, "Cache", "Fenêtre cachée", 0, 0, 0, 10, 10);
    sim_win[hidden].style &= ~WS_VISIBLE;
    qemm = sim_create(0, "AfxFrame", "Quarterdeck QEMM - Installation", WS_DISABLED, 0, 0, 640, 480);
    box = sim_create(0, "#32770", "", 0, 100, 100, 500, 400);
    sim_win[box].owner = qemm;
    sim_active = pm_top;
    sim_focus = pm_top;
    run(500);

    from = spoken_n;
    alt_down();
    alt_tab(FALSE);
    run(100);
    CHECK(spoken_since(from, "Bloc-notes - (Sans titre)") >= 0);
    from = spoken_n;
    alt_tab(FALSE);
    run(100);
    CHECK(spoken_since(from, "Quarterdeck QEMM - Installation") >= 0);
    from = spoken_n;
    alt_tab(FALSE);
    run(100);
    CHECK(spoken_since(from, "Gestionnaire de programmes") >= 0);

    /* Alt relâché puis rappuyé : nouvelle bascule depuis la fenêtre active ; Maj
       remonte (la boîte possédée n'est pas un programme) */
    from = spoken_n;
    alt_down();
    alt_tab(TRUE);
    run(100);
    CHECK(spoken_since(from, "Quarterdeck QEMM - Installation") >= 0);
    CHECK(spoken_since(from, "Fenêtre cachée") < 0);
}

int main(void)
{
    { int f = failures; test_empty_gettext_edit(); if (failures > f) dump("test_empty_gettext_edit"); }
    { int f = failures; test_alt_tab(); if (failures > f) dump("test_alt_tab"); }
    { int f = failures; test_navweb(); if (failures > f) dump("test_navweb"); }
    { int f = failures; test_disabled_controls(); if (failures > f) dump("test_disabled_controls"); }
    { int f = failures; test_calculator_states(); if (failures > f) dump("test_calculator_states"); }
    { int f = failures; test_calculator(); if (failures > f) dump("test_calculator"); }
    { int f = failures; test_write_say_all(); if (failures > f) dump("test_write_say_all"); }
    { int f = failures; test_write_interrupt(); if (failures > f) dump("test_write_interrupt"); }
    { int f = failures; test_write_up_at_end(); if (failures > f) dump("test_write_up_at_end"); }
    { int f = failures; test_notepad_say_all(); if (failures > f) dump("test_notepad_say_all"); }
    { int f = failures; test_progman_icons(); if (failures > f) dump("test_progman_icons"); }
    { int f = failures; test_winfile_list(); if (failures > f) dump("test_winfile_list"); }
    { int f = failures; test_menus(); if (failures > f) dump("test_menus"); }
    { int f = failures; test_word_characters(); if (failures > f) dump("test_word_characters"); }
    { int f = failures; test_word_options(); if (failures > f) dump("test_word_options"); }
    { int f = failures; test_settings(); if (failures > f) dump("test_settings"); }
    { int f = failures; test_echo_and_signs(); if (failures > f) dump("test_echo_and_signs"); }
    { int f = failures; test_status_bar(); if (failures > f) dump("test_status_bar"); }
    { int f = failures; test_generic_windows(); if (failures > f) dump("test_generic_windows"); }
    { int f = failures; test_sound_recorder(); if (failures > f) dump("test_sound_recorder"); }
    { int f = failures; test_cardfile(); if (failures > f) dump("test_cardfile"); }
    { int f = failures; test_charmap(); if (failures > f) dump("test_charmap"); }
    { int f = failures; test_winhelp(); if (failures > f) dump("test_winhelp"); }
    { int f = failures; test_netscape(); if (failures > f) dump("test_netscape"); }
    { int f = failures; test_packager(); if (failures > f) dump("test_packager"); }
    { int f = failures; test_mail(); if (failures > f) dump("test_mail"); }
    { int f = failures; test_excel_grid(); if (failures > f) dump("test_excel_grid"); }
    { int f = failures; test_dos_box(); if (failures > f) dump("test_dos_box"); }
    { int f = failures; test_installer_progress(); if (failures > f) dump("test_installer_progress"); }
    { int f = failures; test_icon_buttons(); if (failures > f) dump("test_icon_buttons"); }
    { int f = failures; test_excel_dialog(); if (failures > f) dump("test_excel_dialog"); }
    { int f = failures; test_long_message(); if (failures > f) dump("test_long_message"); }
    { int f = failures; test_picture_buttons(); if (failures > f) dump("test_picture_buttons"); }
    { int f = failures; test_grid_editor(); if (failures > f) dump("test_grid_editor"); }
    { int f = failures; test_excel_edit(); if (failures > f) dump("test_excel_edit"); }
    test_value_bounds();
    test_braille_modal_handoff();
    { int f = failures; test_braille_selection(); if (failures > f) dump("test_braille_selection"); }
    if (failures)
        printf("%d échec(s)\n", failures);
    else
        printf("epaulard : tous les scénarios réussis\n");
    return failures != 0;
}
