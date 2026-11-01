/* SPDX-License-Identifier: GPL-3.0-or-later
 * S3WPROBE.EXE : EPAUS3.DLL sous Windows au rythme d'un vrai 486 (86Box).
 *
 * Charge EPAUS3.DLL à côté de l'exécutable et la pompe comme EPAULARD.EXE
 * (s3_tick de parole.c : trois messages postés au plus après une écriture, puis
 * minuterie de 55 ms, quatre pompes par passage). Joue un scénario : mots isolés
 * (périphérique rouvert à chaque fois), mots enchaînés coupés par Ctrl-X comme dans
 * un menu, phrase longue. Le son est enregistré par l'hôte ; le programme note
 * l'heure de chaque demande et les compteurs de la DLL dans C:\S3WPROB.TXT, puis
 * quitte Windows. Argument facultatif : texte entier d'un scénario (une ligne par
 * demande, « + » en tête : sans Ctrl-X, « ~ » : attente de 300 ms seulement).
 */
#include <windows.h>
#include <string.h>

#define EPM_S3 (WM_USER + 201)
#define TIMER_S3 14

static UINT (FAR PASCAL *s3_open)(void);
static int (FAR PASCAL *s3_write)(LPCSTR s, int n);
static int (FAR PASCAL *s3_read)(LPSTR buf, int n);
static BOOL (FAR PASCAL *s3_pump)(void);
static void (FAR PASCAL *s3_close)(void);
static BOOL (FAR PASCAL *s3_busy)(void);
static WORD FAR *(FAR PASCAL *s3_info)(void);
static HWND hwnd;
static int chain;
static DWORD t_start, t_next;
static int step, waiting;
static char report[3000];
static int rlen;

/* Scénario : CP850 (é = 82h). « ~ » : 300 ms avant la suivante, sinon on attend
   l'écho de fin puis 2 s de silence (le périphérique est rendu après 1 s). */
static const char *script_default[] = {
    "Fichier", "Option", "Options", "\x90" "dition", "Affichage", "Fen\x88tre",
    "~Fichier", "~\x90" "dition", "~Affichage", "~Options", "~Fen\x88tre", "Aide",
    "Bonjour Michel. Un \x82l\x8Ave prend un caf\x82. Le Gestionnaire de fichiers est ouvert.",
    NULL
};
static const char **script = script_default;
static char script_text[2000];
static const char *script_lines[40];

/* Scénario lu dans un fichier (argument) : une demande par ligne, CP850. */
static void load_script(LPSTR name)
{
    OFSTRUCT of;
    HFILE f = OpenFile(name, &of, OF_READ);
    int n, i, k = 0;
    if (f == HFILE_ERROR) return;
    n = _lread(f, script_text, sizeof(script_text) - 1);
    _lclose(f);
    if (n <= 0) return;
    script_text[n] = 0;
    for (i = 0; i < n && k < 39; ) {
        script_lines[k++] = script_text + i;
        while (i < n && script_text[i] != '\r' && script_text[i] != '\n') i++;
        while (i < n && (script_text[i] == '\r' || script_text[i] == '\n')) script_text[i++] = 0;
    }
    script_lines[k] = NULL;
    script = script_lines;
}

static DWORD t_wait, t_beat;

/* Rapport réécrit à chaque ligne : il reste lisible si Windows ne se ferme pas. */
static void note(const char *s)
{
    int n = lstrlen(s);
    HFILE f;
    OFSTRUCT of;
    if (rlen + n < sizeof(report) - 1) { lstrcpy(report + rlen, s); rlen += n; }
    f = OpenFile("C:\\S3WPROB.TXT", &of, OF_CREATE | OF_WRITE);
    if (f != HFILE_ERROR) {
        _lwrite(f, report, rlen);
        _asm {                  /* INT 21h 68h : écrire sur le disque, pas en cache */
            mov ah, 68h
            mov bx, f
            int 21h
        }
        _lclose(f);
    }
}

/* Compteurs de la DLL après une demande (début de S3W_INFO, s3win.c). */
static void counters(void)
{
    struct {
        char sig[8];
        WORD version, rate, open_error, opened;
        DWORD pumps, played, written, busy_ms, busy_max;
        WORD underruns, resets, buffers, queued;
    } FAR *in = (void FAR *)s3_info();
    char line[120];
    wsprintf(line, "  sous_alimentations=%u ecrits=%lu pompe_max_ms=%lu ouvert=%u\r\n",
             in->underruns, in->written, in->busy_max, in->opened);
    note(line);
}

static void tick(BOOL timer)
{
    int i;
    BOOL more = FALSE;
    if (timer) chain = 0;
    for (i = 0; i < (timer ? 4 : 1); i++)
        if (!(more = s3_pump())) break;
    if (more && !timer && ++chain < 3) PostMessage(hwnd, EPM_S3, 0, 0L);
}

static void say(const char *s, char mark)
{
    char buf[200];
    int n;
    wsprintf(buf, "\x18%s\r@E%c\r", (LPSTR)s, mark);
    n = lstrlen(buf);
    note("  avant_ecriture\r\n");
    n = s3_write(buf, n) - n;
    note(n ? "  ecriture_incomplete\r\n" : "  ecrit\r\n");
    PostMessage(hwnd, EPM_S3, 0, 0L);
}

static void finish(void)
{
    WORD FAR *w = s3_info();
    char line[300];
    /* Champs de S3W_INFO (s3win.c) utiles à l'essai. */
    struct {
        char sig[8];
        WORD version, rate, open_error, opened;
        DWORD pumps, played, written, busy_ms, busy_max;
        WORD underruns, resets, buffers, queued;
        DWORD propositions, samples;
        WORD cuts, marks, lost_marks, refused;
        DWORD first_buffer, submit, first_buffer_max, submit_max, requests;
        WORD opens, closes, open_fails, dropped;
        DWORD skipped;
    } FAR *in = (void FAR *)w;
    wsprintf(line, "rate=%u open_error=%u opens=%u closes=%u open_fails=%u buffers=%u\r\n"
             "pumps=%lu written=%lu busy_max_ms=%lu underruns=%u resets=%u cuts=%u\r\n"
             "first_buffer_max_ms=%lu submit_max_ms=%lu requests=%lu skipped=%lu lost_marks=%u refused=%u\r\n",
             in->rate, in->open_error, in->opens, in->closes, in->open_fails, in->buffers,
             in->pumps, in->written, in->busy_max, in->underruns, in->resets, in->cuts,
             in->first_buffer_max, in->submit_max, in->requests, in->skipped, in->lost_marks, in->refused);
    note(line);
    wsprintf(line, "occupe_a_la_fin=%d\r\nS3WPROBE=fin\r\n", s3_busy());
    note(line);
    s3_close();
    ExitWindows(0, 0);
}

static void scenario(void)
{
    DWORD now = GetTickCount();
    char line[300], echo[4];
    const char *s;
    if (waiting) {
        if ((s3_read(echo, 1) > 0 && echo[0] == 'A' + step) || now - t_wait > 15000) {
            wsprintf(line, "%s%d=%lu\r\n", (LPSTR)(now - t_wait > 15000 ? "sans_echo" : "fin"),
                     step, now - t_start);
            note(line);
            counters();
            waiting = 0;
            t_next = now + 2000;
        }
        return;
    }
    if (now < t_next) return;
    s = script[step];
    if (!s) {
        if (!s3_busy() || now - t_next > 5000) finish();
        return;
    }
    step++;
    wsprintf(line, "demande%d=%lu %s\r\n", step, now - t_start, (LPSTR)s);
    note(line);
    if (*s == '~') {
        say(s + 1, 'A' + step);
        t_next = now + 300;
        return;
    }
    say(s, 'A' + step);
    waiting = 1;
    t_wait = now;
}

long FAR PASCAL __export WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case EPM_S3: tick(FALSE); return 0;
    case WM_TIMER:
        tick(TRUE);
        scenario();
        if (GetTickCount() - t_beat >= 1000) {
            char line[80];
            t_beat = GetTickCount();
            wsprintf(line, "  battement=%lu occupe=%d\r\n", t_beat - t_start, s3_busy());
            note(line);
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HINSTANCE lib;
    char path[144];
    int n;
    UINT err;
    (void)prev; (void)show;
    if (cmd && *cmd) load_script(cmd);
    n = GetModuleFileName(inst, path, sizeof(path));
    while (n && path[n - 1] != '\\') n--;
    lstrcpy(path + n, "EPAUS3.DLL");
    lib = LoadLibrary(path);
    if (lib < HINSTANCE_ERROR) {
        wsprintf(report, "erreur=LoadLibrary %d\r\nS3WPROBE=fin\r\n", (int)lib);
        rlen = lstrlen(report);
        s3_info = NULL;
        { OFSTRUCT of; HFILE f = OpenFile("C:\\S3WPROB.TXT", &of, OF_CREATE | OF_WRITE);
          if (f != HFILE_ERROR) { _lwrite(f, report, rlen); _lclose(f); } }
        ExitWindows(0, 0);
        return 0;
    }
    s3_open = (UINT (FAR PASCAL *)(void))GetProcAddress(lib, "S3OPEN");
    s3_write = (int (FAR PASCAL *)(LPCSTR, int))GetProcAddress(lib, "S3WRITE");
    s3_read = (int (FAR PASCAL *)(LPSTR, int))GetProcAddress(lib, "S3READ");
    s3_pump = (BOOL (FAR PASCAL *)(void))GetProcAddress(lib, "S3PUMP");
    s3_close = (void (FAR PASCAL *)(void))GetProcAddress(lib, "S3CLOSE");
    s3_busy = (BOOL (FAR PASCAL *)(void))GetProcAddress(lib, "S3BUSY");
    s3_info = (WORD FAR *(FAR PASCAL *)(void))GetProcAddress(lib, "S3INFO");
    err = s3_open && s3_info ? s3_open() : 100;
    if (err) {
        wsprintf(report, "erreur=S3Open %u\r\nS3WPROBE=fin\r\n", err);
        rlen = lstrlen(report);
        { OFSTRUCT of; HFILE f = OpenFile("C:\\S3WPROB.TXT", &of, OF_CREATE | OF_WRITE);
          if (f != HFILE_ERROR) { _lwrite(f, report, rlen); _lclose(f); } }
        ExitWindows(0, 0);
        return 0;
    }
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = "S3WPROBE";
    RegisterClass(&wc);
    hwnd = CreateWindow("S3WPROBE", "S3WPROBE", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100,
                        NULL, NULL, inst, NULL);
    t_start = GetTickCount();
    t_next = t_start + 3000;            /* son de démarrage de Windows */
    SetTimer(hwnd, TIMER_S3, 55, NULL);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
