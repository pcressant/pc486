/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SONDE.EXE : isolement réseau vu de Windows 3.11 (Winsock 1.1 de TCP/IP-32).
   Lancé par « WIN C:\SONDE.EXE » : tente chaque connexion de la liste, écrit une
   ligne par essai dans C:\SONDE.TXT puis quitte Windows. Seuls les services de
   l'hôte (10.0.2.2 : 8080, 110, 25) doivent répondre ; tout le reste échoue.
   « SONDE.EXE CHARGE n » : charge réseau pendant n secondes (RETR POP3 répétés du
   premier message), pilotée par messages comme un navigateur (WSAAsyncSelect) ;
   débit écrit dans C:\CHARGE.TXT, Windows reste ouvert.
   Winsock est chargé par LoadLibrary : aucune bibliothèque d'importation. */
#include <windows.h>
#include <winsock.h>
#include <string.h>

typedef int (FAR PASCAL *STARTUP)(WORD, LPWSADATA);
typedef SOCKET (FAR PASCAL *SOCKETF)(int, int, int);
typedef int (FAR PASCAL *CONNECTF)(SOCKET, const struct sockaddr FAR *, int);
typedef int (FAR PASCAL *IOCTLF)(SOCKET, long, u_long FAR *);
typedef int (FAR PASCAL *SELECTF)(int, fd_set FAR *, fd_set FAR *, fd_set FAR *,
                                  const struct timeval FAR *);
typedef int (FAR PASCAL *SENDF)(SOCKET, const char FAR *, int, int);
typedef int (FAR PASCAL *RECVF)(SOCKET, char FAR *, int, int);
typedef int (FAR PASCAL *CLOSEF)(SOCKET);
typedef int (FAR PASCAL *VOIDF)(void);
typedef int (FAR PASCAL *ASYNCF)(SOCKET, HWND, u_int, long);

static STARTUP pStartup;
static SOCKETF pSocket;
static CONNECTF pConnect;
static IOCTLF pIoctl;
static SELECTF pSelect;
static SENDF pSend;
static RECVF pRecv;
static CLOSEF pClose;
static VOIDF pCleanup, pLastError;
static ASYNCF pAsync;

static const struct { BYTE a, b, c, d; WORD port; const char *send; } targets[] = {
    {10, 0, 2, 2, 8080, "GET http://pc486/ HTTP/1.0\r\n\r\n"},
    {10, 0, 2, 2, 110, 0},
    {10, 0, 2, 2, 25, 0},
    {10, 0, 2, 2, 80, 0},
    {10, 0, 2, 2, 22, 0},
    {10, 0, 2, 254, 8080, 0},
    {10, 0, 2, 3, 53, 0},
    {1, 1, 1, 1, 80, 0},
    {127, 0, 0, 1, 8080, 0},
};

static HFILE out;

static void put(const char *s)
{
    _lwrite(out, s, lstrlen(s));
}

static WORD swap(WORD v)
{
    return (WORD)((v >> 8) | (v << 8));
}

static int ready(SOCKET s, int write, int seconds)
{
    fd_set set;
    struct timeval tv;
    set.fd_count = 1;
    set.fd_array[0] = s;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    return pSelect(0, write ? 0 : &set, write ? &set : 0, 0, &tv);
}

static void probe(int i)
{
    char line[160], buf[48];
    struct sockaddr_in sa;
    SOCKET s;
    u_long on = 1;
    int n, k;
    wsprintf(line, "%d.%d.%d.%d:%u ", targets[i].a, targets[i].b, targets[i].c, targets[i].d,
             targets[i].port);
    put(line);
    s = pSocket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        put("ERR socket\r\n");
        return;
    }
    pIoctl(s, FIONBIO, &on);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = swap(targets[i].port);
    sa.sin_addr.S_un.S_un_b.s_b1 = targets[i].a;
    sa.sin_addr.S_un.S_un_b.s_b2 = targets[i].b;
    sa.sin_addr.S_un.S_un_b.s_b3 = targets[i].c;
    sa.sin_addr.S_un.S_un_b.s_b4 = targets[i].d;
    if (pConnect(s, (struct sockaddr FAR *)&sa, sizeof sa) == SOCKET_ERROR
        && pLastError() != WSAEWOULDBLOCK) {
        wsprintf(line, "ERR %d\r\n", pLastError());
        put(line);
        pClose(s);
        return;
    }
    if (ready(s, 1, 8) != 1) {
        put("ECHEC\r\n");               /* délai dépassé ou connexion refusée */
        pClose(s);
        return;
    }
    if (targets[i].send)
        pSend(s, targets[i].send, lstrlen(targets[i].send), 0);
    n = 0;
    while (n < (int)sizeof buf - 1 && ready(s, 0, 8) == 1) {
        k = pRecv(s, buf + n, sizeof buf - 1 - n, 0);
        if (k <= 0) break;
        n += k;
        if (memchr(buf, '\n', n)) break;
    }
    buf[n] = 0;
    for (k = 0; k < n; k++)
        if (buf[k] == '\r' || buf[k] == '\n') buf[k] = 0;
    put(n ? "OK " : "VIDE");
    put(buf);
    put("\r\n");
    pClose(s);
}

static SOCKET open_pop(void)
{
    struct sockaddr_in sa;
    SOCKET s = pSocket(AF_INET, SOCK_STREAM, 0);
    u_long on = 1;
    if (s == INVALID_SOCKET) return s;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = swap(110);
    sa.sin_addr.S_un.S_un_b.s_b1 = 10;
    sa.sin_addr.S_un.S_un_b.s_b2 = 0;
    sa.sin_addr.S_un.S_un_b.s_b3 = 2;
    sa.sin_addr.S_un.S_un_b.s_b4 = 2;
    if (pConnect(s, (struct sockaddr FAR *)&sa, sizeof sa) == SOCKET_ERROR) {
        pClose(s);
        return INVALID_SOCKET;
    }
    pIoctl(s, FIONBIO, &on);
    return s;
}

/* Charge comme un vrai client Win16 : WSAAsyncSelect et boucle GetMessage, une
   lecture par message FD_READ ; RETR 1 relancé à chaque fin de message (« CRLF.CRLF »,
   reconnue même à cheval sur deux lectures). Arrêt par minuterie. */
#define WM_SOCKET (WM_USER + 1)
static SOCKET load_sock = INVALID_SOCKET;
static DWORD load_total;
static char load_tail[5];

LRESULT CALLBACK __export LoadProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static char buf[4096];
    int k, i;
    if (msg == WM_SOCKET && WSAGETSELECTEVENT(lp) == FD_READ) {
        k = pRecv(load_sock, buf, sizeof buf, 0);
        if (k > 0) {
            load_total += k;
            for (i = 0; i < k; i++) {
                memmove(load_tail, load_tail + 1, 4);
                load_tail[4] = buf[i];
            }
            if (!memcmp(load_tail, "\r\n.\r\n", 5)) {
                memset(load_tail, 0, sizeof load_tail);
                pSend(load_sock, "RETR 1\r\n", 8, 0);
            }
        }
        return 0;
    }
    if ((msg == WM_SOCKET && WSAGETSELECTEVENT(lp) == FD_CLOSE) || msg == WM_TIMER) {
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static void charge(HINSTANCE inst, int seconds)
{
    char line[80];
    DWORD start = GetTickCount();
    WNDCLASS wc;
    HWND hwnd;
    MSG msg;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = LoadProc;
    wc.hInstance = inst;
    wc.lpszClassName = "SondeCharge";
    if (!RegisterClass(&wc)) {
        put("ERR classe\r\n");
        return;
    }
    hwnd = CreateWindow("SondeCharge", "", 0, 0, 0, 0, 0, 0, 0, inst, 0);
    load_sock = open_pop();
    if (!hwnd || load_sock == INVALID_SOCKET) {
        put("ERR connexion POP3\r\n");
        return;
    }
    pAsync(load_sock, hwnd, WM_SOCKET, FD_READ | FD_CLOSE);
    SetTimer(hwnd, 1, 1000, 0);
    pSend(load_sock, "RETR 1\r\n", 8, 0);
    while (GetMessage(&msg, 0, 0, 0)) {
        if (msg.message == WM_TIMER && GetTickCount() - start < (DWORD)seconds * 1000)
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    pAsync(load_sock, hwnd, 0, 0);
    pSend(load_sock, "QUIT\r\n", 6, 0);
    pClose(load_sock);
    DestroyWindow(hwnd);
    wsprintf(line, "CHARGE %lu octets en %lu ms\r\n", load_total, GetTickCount() - start);
    put(line);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    HINSTANCE ws;
    WSADATA wsa;
    OFSTRUCT of;
    int i, load = 0;
    (void)prev; (void)show;
    if (!_fstrnicmp(cmd, "CHARGE", 6)) {
        for (cmd += 6; *cmd == ' '; cmd++) ;
        while (*cmd >= '0' && *cmd <= '9') load = load * 10 + *cmd++ - '0';
        if (!load) load = 60;
    }
    out = OpenFile(load ? "C:\\CHARGE.TXT" : "C:\\SONDE.TXT", &of, OF_CREATE | OF_WRITE);
    if (out == HFILE_ERROR) return 1;
    ws = LoadLibrary("WINSOCK.DLL");
    if ((UINT)ws < 32) {
        put("ERR winsock.dll absent\r\n");
    } else {
        pStartup = (STARTUP)GetProcAddress(ws, "WSAStartup");
        pSocket = (SOCKETF)GetProcAddress(ws, "socket");
        pConnect = (CONNECTF)GetProcAddress(ws, "connect");
        pIoctl = (IOCTLF)GetProcAddress(ws, "ioctlsocket");
        pSelect = (SELECTF)GetProcAddress(ws, "select");
        pSend = (SENDF)GetProcAddress(ws, "send");
        pRecv = (RECVF)GetProcAddress(ws, "recv");
        pClose = (CLOSEF)GetProcAddress(ws, "closesocket");
        pCleanup = (VOIDF)GetProcAddress(ws, "WSACleanup");
        pLastError = (VOIDF)GetProcAddress(ws, "WSAGetLastError");
        pAsync = (ASYNCF)GetProcAddress(ws, "WSAAsyncSelect");
        if (!pStartup || !pSocket || !pConnect || !pIoctl || !pSelect || !pSend || !pRecv
            || !pClose || !pCleanup || !pLastError || !pAsync) {
            put("ERR fonctions winsock\r\n");
        } else if (pStartup(0x0101, &wsa)) {
            put("ERR WSAStartup\r\n");
        } else {
            if (load)
                charge(inst, load);
            else
                for (i = 0; i < (int)(sizeof targets / sizeof targets[0]); i++)
                    probe(i);
            pCleanup();
        }
        FreeLibrary(ws);
    }
    put("FIN\r\n");
    _lclose(out);
    if (!load)
        ExitWindows(0, 0);
    return 0;
}
