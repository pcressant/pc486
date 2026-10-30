/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SONDENAV.EXE : ce que Netscape (3 ou 4, 16 bits) laisse voir à un autre programme,
   pour choisir l'architecture du mode navigation d'Épaulard. À lancer pendant que
   Netscape est ouvert ; écrit C:\SONDENAV.TXT puis se termine.
     1. DDE WWW_GetWindowInfo : adresse et titre de la page affichée ;
     2. DDE WWW_OpenURL vers http://pc486/essai, puis de nouveau WWW_GetWindowInfo ;
     3. arbre des fenêtres de Netscape : classe, identifiant, style, position, texte
        (WM_GETTEXT), état des cases, contenu des listes déroulantes. */
#include <windows.h>
#include <ddeml.h>
#include <string.h>

static HFILE out;
static DWORD inst;
static char line[600];

static void put(const char *s)
{
    _lwrite(out, s, lstrlen(s));
    _lwrite(out, "\r\n", 2);
}

HDDEDATA CALLBACK __export DdeCb(UINT t, UINT f, HCONV c, HSZ a, HSZ b, HDDEDATA d,
                                 DWORD x, DWORD y)
{
    (void) t; (void) f; (void) c; (void) a; (void) b; (void) d; (void) x; (void) y;
    return 0;
}

static void request(const char *topic, const char *item)
{
    HSZ svc = DdeCreateStringHandle(inst, "NETSCAPE", CP_WINANSI);
    HSZ top = DdeCreateStringHandle(inst, (LPSTR) topic, CP_WINANSI);
    HSZ it = DdeCreateStringHandle(inst, (LPSTR) item, CP_WINANSI);
    HCONV conv = DdeConnect(inst, svc, top, NULL);
    char buf[400];
    wsprintf(line, "DDE %s [%s] : conv %04x", (LPSTR) topic, (LPSTR) item, (UINT) conv);
    put(line);
    if (conv) {
        HDDEDATA d = DdeClientTransaction(NULL, 0, conv, it, CF_TEXT, XTYP_REQUEST, 15000, NULL);
        if (d) {
            DWORD n = DdeGetData(d, (LPBYTE) buf, sizeof(buf) - 1, 0);
            buf[n < sizeof(buf) ? (int) n : sizeof(buf) - 1] = 0;
            wsprintf(line, "  réponse (%ld octets) : %s", n, (LPSTR) buf);
            DdeFreeDataHandle(d);
        } else
            wsprintf(line, "  pas de réponse, erreur %04x", DdeGetLastError(inst));
        put(line);
        DdeDisconnect(conv);
    }
    DdeFreeStringHandle(inst, svc);
    DdeFreeStringHandle(inst, top);
    DdeFreeStringHandle(inst, it);
}

static void dump(HWND h, int depth)
{
    char cls[40], text[120];
    RECT r;
    HWND c;
    LONG style = GetWindowLong(h, GWL_STYLE);
    GetClassName(h, cls, sizeof(cls));
    GetWindowText(h, text, sizeof(text));
    GetWindowRect(h, &r);
    wsprintf(line, "%d %04x %s id=%d style=%08lx (%d,%d,%d,%d) texte=[%s] long=%d",
             depth, (UINT) h, (LPSTR) cls, GetWindowWord(h, GWW_ID), style, r.left, r.top,
             r.right, r.bottom, (LPSTR) text, (int) SendMessage(h, WM_GETTEXTLENGTH, 0, 0L));
    if (!lstrcmpi(cls, "Button")) {
        char x[24];
        wsprintf(x, " coche=%d", (int) SendMessage(h, BM_GETCHECK, 0, 0L));
        lstrcat(line, x);
    } else if (!lstrcmpi(cls, "ComboBox")) {
        char x[80], item[40];
        int sel = (int) SendMessage(h, CB_GETCURSEL, 0, 0L);
        item[0] = 0;
        if (sel >= 0)
            SendMessage(h, CB_GETLBTEXT, sel, (LPARAM) (LPSTR) item);
        wsprintf(x, " n=%d choix=%d [%s]", (int) SendMessage(h, CB_GETCOUNT, 0, 0L), sel,
                 (LPSTR) item);
        lstrcat(line, x);
    } else if (!lstrcmpi(cls, "Edit")) {
        char x[60];
        HLOCAL hl = (HLOCAL) SendMessage(h, EM_GETHANDLE, 0, 0L);
        wsprintf(x, " lignes=%d poignée=%04x", (int) SendMessage(h, EM_GETLINECOUNT, 0, 0L),
                 (UINT) hl);
        lstrcat(line, x);
    }
    put(line);
    if (depth < 6)
        for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            dump(c, depth + 1);
}

static HWND netscape(void)
{
    HWND w;
    char t[80];
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
        GetWindowText(w, t, sizeof(t));
        if (IsWindowVisible(w) && _fstrstr(t, "Netscape"))
            return w;
    }
    return NULL;
}

static void wait_ms(DWORD ms)
{
    MSG m;
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < ms) {
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
        Yield();
    }
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    OFSTRUCT of;
    FARPROC cb;
    HWND ns;
    (void) prev; (void) cmd; (void) show;
    out = OpenFile("C:\\SONDENAV.TXT", &of, OF_CREATE | OF_WRITE);
    if (out == HFILE_ERROR)
        return 1;
    cb = MakeProcInstance((FARPROC) DdeCb, hi);
    wsprintf(line, "DdeInitialize : %u", DdeInitialize(&inst, (PFNCALLBACK) cb,
                                                        APPCMD_CLIENTONLY, 0L));
    put(line);
    {
        int i;                          /* lancé par load= : attendre Netscape et sa page */
        for (i = 0; i < 300 && !netscape(); i++)
            wait_ms(1000);
        wait_ms(30000);
    }
    request("WWW_GetWindowInfo", "0xFFFFFFFF");
    request("WWW_ListWindows", "");
    request("WWW_OpenURL", "\"http://pc486/essai\",,0xFFFFFFFF,0x0,,,");
    wait_ms(25000);
    request("WWW_GetWindowInfo", "0xFFFFFFFF");
    ns = netscape();
    if (ns)
        dump(ns, 0);
    else
        put("fenêtre de Netscape introuvable");
    DdeUninitialize(inst);
    _lclose(out);
    return 0;
}
