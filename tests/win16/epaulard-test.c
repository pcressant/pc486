/* Banc d'Épaulard complet : vrais epaulard.c, osm.c et braille.c sur la simulation
   Win16, face à une fausse Apollo II (COM1, 15 caractères par seconde, échos @E), une
   fausse plage Baum 40 cellules (COM3) et de fausses applications : Write (texte
   dessiné qui défile sous un curseur immobile), Bloc-notes (EDIT standard) et le
   Gestionnaire de programmes (icônes en surbrillance, sans curseur texte).
   Le temps est simulé par pas de 10 ms. */
#define WIN16_FULL
#include "../../epaulard/src/osm.c"
#include "../../epaulard/src/braille.c"
#include "../../epaulard/src/epaulard.c"

HWND ep_hwnd_main;
BOOL ep_in_menu;
BOOL FAR PASCAL EpInstall(HWND h, int c) { (void) c; ep_hwnd_main = h; return TRUE; }
void FAR PASCAL EpRemove(void) {}
static BOOL cfg_mode_sim;       /* clavier donné au menu des réglages (EPAUHOOK.DLL) */
void FAR PASCAL EpCfgMode(BOOL on) { cfg_mode_sim = on; }
static int sim_br_epoch;
int FAR PASCAL EpBrailleEpoch(void) { return sim_br_epoch; }

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
static struct { char t[300]; BOOL echo; } ap_q[AP_N];
static int ap_n, ap_parsed;
static char ap_cur[300];
static int ap_curn;
static DWORD ap_busy_until;     /* fin de l'énoncé en cours */
static char spoken[400][300];   /* énoncés commencés, dans l'ordre */
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
        } else if (ap_curn < 299)
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

static LRESULT app_key(HWND h, UINT vk)
{
    if (h == hp.doc && hp.doc) return hp_key(vk);
    if (h == wr.doc && wr.doc) return wr_key(vk);
    if (h == np.edit && np.edit) return np_key(vk);
    if (h == pm.grp && pm.grp) return pm_key(vk);
    if (h == wf.list && wf.list) return wf_key(vk);
    return 0;
}

/* l'application qui a le focus lit ses touches ; le crochet clavier les signale */
static void app_keys(void)
{
    MSG m;
    int k;
    for (k = 0; k < 64 && sim_focus && sim_take(sim_focus, &m); k++)
        if (m.message == WM_KEYDOWN) {
            app_key(m.hwnd, m.wParam);
            PostMessage(hwnd_main, EPM_KEY, m.wParam, m.lParam |     /* comme EPAUHOOK.DLL */
                        (sim_keys[VK_CONTROL] ? EPK_CTRL : 0) | (sim_keys[VK_SHIFT] ? EPK_SHIFT : 0));
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
    PostMessage(sim_focus, WM_KEYDOWN, vk, MAKELPARAM(1, 0x100));
    PostMessage(sim_focus, WM_KEYUP, vk, MAKELPARAM(1, 0xC100));
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
    sim_focus = sim_active = 0;
    sim_qn = 0;
    osm_remove();
    osm_install();
    hwnd_main = sim_create(0, "Epaulard", "Épaulard", 0, 0, 0, 0, 0);
    sim_win[hwnd_main].style &= ~WS_VISIBLE;
    com_open();
    cfg_load(0);
    voice_apply();
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

int main(void)
{
    { int f = failures; test_disabled_controls(); if (failures > f) dump("test_disabled_controls"); }
    { int f = failures; test_calculator_states(); if (failures > f) dump("test_calculator_states"); }
    { int f = failures; test_calculator(); if (failures > f) dump("test_calculator"); }
    { int f = failures; test_write_say_all(); if (failures > f) dump("test_write_say_all"); }
    { int f = failures; test_write_interrupt(); if (failures > f) dump("test_write_interrupt"); }
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
    { int f = failures; test_packager(); if (failures > f) dump("test_packager"); }
    { int f = failures; test_mail(); if (failures > f) dump("test_mail"); }
    test_value_bounds();
    test_braille_modal_handoff();
    { int f = failures; test_braille_selection(); if (failures > f) dump("test_braille_selection"); }
    if (failures)
        printf("%d échec(s)\n", failures);
    else
        printf("epaulard : tous les scénarios réussis\n");
    return failures != 0;
}
