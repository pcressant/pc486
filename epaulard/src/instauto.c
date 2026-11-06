/* INSTAUTO.EXE : installation sans intervention d'un programme Windows 3.1.
 *
 * Lancé par load= pendant une étape de scripts/installer, jamais dans une VM
 * d'usage. Section [pc486] de WIN.INI :
 *   commande=C:\INSTALL\DISK1\INSTALL.EXE /Q1 ...   programme à lancer
 *   etat=                                            vide, lance, fini (écrit ici)
 *   reponse1=Titre de boîte|texte{TAB}texte{ENTREE}  réponses aux installateurs à
 *   reponse2=...                                     questions (Lotus), dans l'ordre
 *   residents=NAVTSRW FASSIST                        modules laissés résidents par
 *                                                    l'installateur (Norton), ignorés
 * Une réponse est jouée quand la fenêtre active a un titre qui commence par le sien :
 * texte (WM_CHAR), {TAB}, {ENTREE}, {ECHAP}, {ESPACE}, {BAS}, {ALT+lettre} (menu ou
 * raccourci de boîte), une touche toutes les 250 ms, postée
 * à la fenêtre qui a alors le focus (IsDialogMessage de l'installateur la traite
 * comme une frappe). La fenêtre doit être restée active une seconde : le fond de
 * l'installateur Lotus, qui porte le titre de son dernier message, ne l'est qu'un
 * instant entre deux boîtes. Les réponses jouées sont comptées dans « faites ».
 * Assistant dont toutes les pages sont dans une même fenêtre (QEMM 8) : règle
 * « Titre#Texte|touches », jouée quand un contrôle visible d'une fenêtre de la tâche
 * active commence par Texte (en-tête de la page), vu à deux relevés successifs.
 * Premier démarrage : lance la commande. Toutes les tâches autres que le
 * Gestionnaire de programmes, INSTAUTO et les résidents terminées depuis 10 s : quitte
 * Windows. Si l'installateur redémarre Windows (état « lance », ou « fini » quand
 * il relance Windows en même temps qu'INSTAUTO le quitte), attend de même puis quitte. Les titres des fenêtres visibles sont notés dans
 * C:\INSTAUTO.LOG, pour le diagnostic hors de la VM.
 */
#include <windows.h>
#include <toolhelp.h>
#include <string.h>

#define IDLE_END 10             /* secondes sans autre tâche */
#define LOG_FILE "C:\\INSTAUTO.LOG"

static HTASK self_task;
static BOOL seen;               /* une tâche lancée a été vue */
static int idle;
static char last_titles[512];
static char keys[160];          /* réponse en cours : touches restantes */
static int key_pos;
static HWND answered;           /* fenêtre de la dernière réponse */
static char answered_title[80]; /* et son titre : Windows réutilise aussitôt le handle
                                   d'une boîte détruite pour la suivante */
static int done;                /* réponses jouées */
static char residents[128];     /* modules à ignorer, séparés par des espaces */
static int page_seen;           /* relevés successifs où la page attendue est visible */

static void log_line(LPCSTR text)
{
    HFILE f;
    OFSTRUCT of;
    f = OpenFile(LOG_FILE, &of, OF_WRITE | OF_SHARE_DENY_NONE);
    if (f == HFILE_ERROR)
        f = OpenFile(LOG_FILE, &of, OF_CREATE | OF_WRITE);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0L, 2);
    _lwrite(f, text, lstrlen(text));
    _lwrite(f, "\r\n", 2);
    _lclose(f);
}

/* Titres des fenêtres principales visibles, séparés par « | ». */
static void log_windows(void)
{
    char titles[512], title[80];
    HWND w;
    int n;
    titles[0] = '\0';
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
        if (!IsWindowVisible(w) || GetWindowTask(w) == self_task)
            continue;
        n = GetWindowText(w, title, sizeof(title));
        if (n == 0 || lstrlen(titles) + n + 4 >= (int)sizeof(titles))
            continue;
        if (titles[0])
            lstrcat(titles, " | ");
        lstrcat(titles, title);
    }
    if (lstrcmp(titles, last_titles) != 0) {
        lstrcpy(last_titles, titles);
        log_line(titles[0] ? titles : "(aucune fenêtre)");
    }
}

/* Module cité dans residents= */
static BOOL resident(LPCSTR module)
{
    char name[16];
    const char *p = residents;
    int n;
    while (*p) {
        while (*p == ' ')
            p++;
        for (n = 0; p[n] && p[n] != ' ' && n < (int)sizeof(name) - 1; n++)
            name[n] = p[n];
        name[n] = '\0';
        if (n && lstrcmpi(name, module) == 0)
            return TRUE;
        while (*p && *p != ' ')
            p++;
    }
    return FALSE;
}

/* Nombre de tâches autres que le Gestionnaire de programmes, INSTAUTO, le lecteur
   d'écran (bac à sable : Épaulard reste chargé pendant l'installation) et les
   résidents déclarés. */
static int other_tasks(void)
{
    TASKENTRY te;
    int n = 0;
    te.dwSize = sizeof(te);
    if (!TaskFirst(&te))
        return 0;
    do {
        if (te.hTask != self_task && lstrcmpi(te.szModule, "PROGMAN") != 0 &&
            lstrcmpi(te.szModule, "EPAULARD") != 0 && !resident(te.szModule))
            n++;
    } while (TaskNext(&te));
    return n;
}

static void post_key(HWND to, UINT vk)
{
    PostMessage(to, WM_KEYDOWN, vk, 1L);
    PostMessage(to, WM_KEYUP, vk, 0xC0000001L);
}

/* Alt+touche : bit 29 (Alt enfoncé) ; TranslateMessage en fait un WM_SYSCHAR, que
   IsDialogMessage (raccourci) ou DefWindowProc (menu, SC_KEYMENU) traite. */
static void post_alt(HWND to, UINT vk)
{
    PostMessage(to, WM_SYSKEYDOWN, vk, 0x20000001L);
    PostMessage(to, WM_SYSKEYUP, vk, 0xE0000001L);
}

/* Une touche (ou un texte jusqu'à la prochaine accolade) de la réponse en cours */
static void answer_step(HWND hwnd)
{
    HWND to = GetFocus();
    static const struct { const char *name; UINT vk; } named[] = {
        { "{ENTREE}", VK_RETURN }, { "{TAB}", VK_TAB }, { "{ECHAP}", VK_ESCAPE },
        { "{ESPACE}", VK_SPACE },     /* case à cocher : WM_CHAR ne la coche pas */
        { "{BAS}", VK_DOWN }          /* bouton radio suivant */
    };
    int i, n;
    if (!keys[key_pos]) {
        KillTimer(hwnd, 2);
        return;
    }
    if (!to)
        return;
    if (keys[key_pos] == '{') {
        if (_fstrnicmp(keys + key_pos, "{ALT+", 5) == 0 && keys[key_pos + 5] &&
            keys[key_pos + 6] == '}') {
            post_alt(to, (UINT)(BYTE)AnsiUpper((LPSTR)(DWORD)(BYTE)keys[key_pos + 5]));
            key_pos += 7;
            return;
        }
        for (i = 0; i < (int)(sizeof(named) / sizeof(named[0])); i++) {
            n = lstrlen(named[i].name);
            if (_fstrnicmp(keys + key_pos, named[i].name, n) == 0) {
                post_key(to, named[i].vk);
                key_pos += n;
                return;
            }
        }
    }
    for (; keys[key_pos] && keys[key_pos] != '{'; key_pos++)
        PostMessage(to, WM_CHAR, (BYTE) keys[key_pos], 1L);
}

/* Contrôle visible, descendant de w, dont le texte commence par text */
static BOOL page_shown(HWND w, LPCSTR text)
{
    char got[80];
    HWND c;
    for (c = GetWindow(w, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!IsWindowVisible(c))
            continue;
        GetWindowText(c, got, sizeof(got));
        if (_fstrnicmp(got, text, lstrlen(text)) == 0 || page_shown(c, text))
            return TRUE;
    }
    return FALSE;
}

/* Page affichée par la tâche de la fenêtre active : ses pages peuvent être des
   fenêtres surgissantes au même titre que le cadre (QEMM 8). */
static BOOL task_page(HWND act, LPCSTR text)
{
    HTASK task = GetWindowTask(act);
    HWND w;
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
        if (IsWindowVisible(w) && GetWindowTask(w) == task && page_shown(w, text))
            return TRUE;
    return FALSE;
}

/* Fenêtre active à laquelle la prochaine réponse s'adresse : la préparer */
static void answer_check(HWND hwnd)
{
    char name[16], rule[256], title[80], *bar, *page;
    static HWND stable;
    static char stable_title[80];
    HWND act = GetActiveWindow();
    GetWindowText(act, title, sizeof(title));
    if (act != answered || lstrcmp(title, answered_title) != 0)
        answered = NULL;                /* (fenêtre revenue plus tard : nouvelle réponse) */
    if (act != stable || lstrcmp(title, stable_title) != 0) {
        stable = act;                   /* active depuis moins d'une seconde */
        lstrcpy(stable_title, title);
        return;
    }
    if (keys[key_pos] || !act)
        return;
    wsprintf(name, "reponse%d", done + 1);
    GetProfileString("pc486", name, "", rule, sizeof(rule));
    if (!rule[0] || (bar = _fstrchr(rule, '|')) == NULL)
        return;
    *bar = 0;
    page = _fstrchr(rule, '#');
    if (page)
        *page++ = 0;
    else if (act == answered)
        return;
    if (_fstrnicmp(title, rule, lstrlen(rule)) != 0)
        return;
    if (page) {
        /* même fenêtre que la réponse précédente : seule la page change */
        if (!task_page(act, page)) {
            page_seen = 0;
            return;
        }
        if (++page_seen < 2)
            return;
        page_seen = 0;
    }
    answered = act;
    lstrcpy(answered_title, title);
    lstrcpyn(keys, bar + 1, sizeof(keys));
    key_pos = 0;
    done++;
    wsprintf(name, "%d", done);
    WriteProfileString("pc486", "faites", name);
    log_line(title);
    log_line(keys);
    SetTimer(hwnd, 2, 250, NULL);
}

LRESULT CALLBACK __export WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TIMER && wp == 2) {
        answer_step(hwnd);
        return 0;
    }
    if (msg == WM_TIMER) {
        answer_check(hwnd);
        log_windows();
        if (other_tasks() > 0) {
            seen = TRUE;
            idle = 0;
        } else if (++idle >= IDLE_END && seen) {
            KillTimer(hwnd, 1);
            WriteProfileString("pc486", "etat", "fini");
            log_line("fin : sortie de Windows");
            if (!ExitWindows(0, 0)) {
                log_line("ExitWindows refusé");
                SetTimer(hwnd, 1, 1000, NULL);
                idle = 0;
            }
        }
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    WNDCLASS wc;
    HWND hwnd;
    MSG m;
    char command[256], state[16];
    UINT r;

    (void)cmdline; (void)show;
    if (prev)
        return 0;
    self_task = GetCurrentTask();
    GetProfileString("pc486", "commande", "", command, sizeof(command));
    GetProfileString("pc486", "etat", "", state, sizeof(state));
    GetProfileString("pc486", "residents", "", residents, sizeof(residents));
    if (command[0] == '\0')
        return 0;

    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = inst;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "InstAuto";
    if (!RegisterClass(&wc))
        return 0;
    hwnd = CreateWindow("InstAuto", "INSTAUTO", WS_OVERLAPPED, 0, 0, 0, 0,
                        NULL, NULL, inst, NULL);
    if (!hwnd)
        return 0;

    if (state[0] == '\0') {
        WriteProfileString("pc486", "etat", "lance");
        log_line("lancement :");
        log_line(command);
        r = WinExec(command, SW_SHOWNORMAL);
        if (r < 32) {
            log_line("échec de WinExec");
            WriteProfileString("pc486", "etat", "fini");
            ExitWindows(0, 0);
            return 0;
        }
    } else {
        /* « fini » : l'installateur a relancé Windows après sa dernière boîte (QEMM 8,
           pour MagnaRAM), alors qu'INSTAUTO allait le quitter : quitter de même. */
        log_line(lstrcmp(state, "fini") == 0 ? "Windows redémarré après la fin"
                                              : "Windows redémarré par l'installateur");
        done = GetProfileInt("pc486", "faites", 0);
    }
    seen = TRUE;    /* après lancement ou redémarrage : attendre le calme */
    SetTimer(hwnd, 1, 1000, NULL);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
