/* INSTAUTO.EXE : installation sans intervention d'un programme Windows 3.1.
 *
 * Lancé par load= pendant une étape de scripts/installer, jamais dans une VM
 * d'usage. Section [pc486] de WIN.INI :
 *   commande=C:\INSTALL\DISK1\INSTALL.EXE /Q1 ...   programme à lancer
 *   etat=                                            vide, lance, fini (écrit ici)
 * Premier démarrage : lance la commande. Toutes les tâches autres que le
 * Gestionnaire de programmes et INSTAUTO terminées depuis 10 s : quitte
 * Windows. Si l'installateur redémarre Windows (état « lance »), attend de
 * même puis quitte. Les titres des fenêtres visibles sont notés dans
 * C:\INSTAUTO.LOG, pour le diagnostic hors de la VM.
 */
#include <windows.h>
#include <toolhelp.h>

#define IDLE_END 10             /* secondes sans autre tâche */
#define LOG_FILE "C:\\INSTAUTO.LOG"

static HTASK self_task;
static BOOL seen;               /* une tâche lancée a été vue */
static int idle;
static char last_titles[512];

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

/* Nombre de tâches autres que le Gestionnaire de programmes et INSTAUTO. */
static int other_tasks(void)
{
    TASKENTRY te;
    int n = 0;
    te.dwSize = sizeof(te);
    if (!TaskFirst(&te))
        return 0;
    do {
        if (te.hTask != self_task && lstrcmpi(te.szModule, "PROGMAN") != 0)
            n++;
    } while (TaskNext(&te));
    return n;
}

LRESULT CALLBACK __export WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TIMER) {
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
    if (command[0] == '\0' || lstrcmp(state, "fini") == 0)
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
        log_line("Windows redémarré par l'installateur");
    }
    seen = TRUE;    /* après lancement ou redémarrage : attendre le calme */
    SetTimer(hwnd, 1, 1000, NULL);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
