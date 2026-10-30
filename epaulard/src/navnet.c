/* EPAULARD.EXE — accès réseau du mode navigation (navweb.c).
     DDE avec Netscape : adresse et titre de la page affichée (WWW_GetWindowInfo),
     ouverture d'un lien dans la même fenêtre (WWW_OpenURL).
     Winsock : structure de la page, demandée au proxy de pc486
     (GET http://pc486/tampon?url=…), lue sans attendre (WSAAsyncSelect, EPM_NET).
   DDEML.DLL et WINSOCK.DLL sont chargées par LoadLibrary, et seulement quand Netscape
   a déjà chargé Winsock : sans réseau, Épaulard ne le charge jamais lui-même et le
   mode navigation reste indisponible, sans autre changement. Proxy : EPAULARD.INI,
   [Navigation] Proxy=10.0.2.2:8080 (valeur par défaut). */
#include "ep.h"

/* DDEML (16 bits) : identifiants sur 32 bits */
typedef DWORD NHSZ, NHCONV, NHDATA;
typedef UINT (FAR PASCAL *DDEINIT)(DWORD FAR *, FARPROC, DWORD, DWORD);
typedef NHSZ (FAR PASCAL *DDESTR)(DWORD, LPCSTR, int);
typedef BOOL (FAR PASCAL *DDEFREESTR)(DWORD, NHSZ);
typedef NHCONV (FAR PASCAL *DDECONN)(DWORD, NHSZ, NHSZ, void FAR *);
typedef BOOL (FAR PASCAL *DDEDISC)(NHCONV);
typedef NHDATA (FAR PASCAL *DDETRANS)(void FAR *, DWORD, NHCONV, NHSZ, UINT, UINT, DWORD,
                                      DWORD FAR *);
typedef DWORD (FAR PASCAL *DDEGET)(NHDATA, void FAR *, DWORD, DWORD);
typedef BOOL (FAR PASCAL *DDEFREEDATA)(NHDATA);
#define NET_CLIENTONLY 0x00000010L
#define NET_CP_ANSI    1004
#define NET_REQUEST    0x20B0
#define NET_CF_TEXT    1

/* Winsock 1.1 (16 bits) */
typedef UINT NSOCKET;
#define NET_INVALID    ((NSOCKET) ~0)
typedef struct { short family; WORD port; BYTE addr[4]; char zero[8]; } NSOCKADDR;
typedef int (FAR PASCAL *WSTARTUP)(WORD, void FAR *);
typedef NSOCKET (FAR PASCAL *WSOCKET)(int, int, int);
typedef int (FAR PASCAL *WCONNECT)(NSOCKET, const NSOCKADDR FAR *, int);
typedef int (FAR PASCAL *WSEND)(NSOCKET, const char FAR *, int, int);
typedef int (FAR PASCAL *WRECV)(NSOCKET, char FAR *, int, int);
typedef int (FAR PASCAL *WCLOSE)(NSOCKET);
typedef int (FAR PASCAL *WASYNC)(NSOCKET, HWND, UINT, long);
#define NET_FD_READ    0x01
#define NET_FD_CONNECT 0x10
#define NET_FD_CLOSE   0x20

static int net_state;                   /* 0 pas encore, 1 prêt, -1 indisponible */
static DWORD net_inst;
static DDESTR pStr;
static DDEFREESTR pFreeStr;
static DDECONN pConnect;
static DDEDISC pDisconnect;
static DDETRANS pTrans;
static DDEGET pGet;
static DDEFREEDATA pFreeData;
static WSOCKET pSocket;
static WCONNECT pWConnect;
static WSEND pSend;
static WRECV pRecv;
static WCLOSE pClose;
static WASYNC pAsync;
static BYTE net_ip[4] = { 10, 0, 2, 2 };
static WORD net_port = 8080;
static NSOCKET net_sock = NET_INVALID;
static char net_req[600];
static int net_hstate;                  /* en-têtes HTTP : 0 statut, 1 en-têtes, 2 corps */
static BOOL net_status_ok;
static char net_hline[80];
static int net_hlen;
BOOL net_in_dde;                        /* transaction DDE en cours (boucle modale) */

NHDATA CALLBACK __export NetDdeCb(UINT t, UINT f, NHCONV c, NHSZ a, NHSZ b, NHDATA d,
                                  DWORD x, DWORD y)
{
    (void) t; (void) f; (void) c; (void) a; (void) b; (void) d; (void) x; (void) y;
    return 0;
}

static void net_proxy_setting(void)
{
    char v[40];
    const char *p = v;
    int k;
    GetPrivateProfileString("Navigation", "Proxy", "10.0.2.2:8080", v, sizeof(v), ini_path);
    for (k = 0; k < 4; k++) {
        WORD n = 0;
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        net_ip[k] = (BYTE) n;
        if (*p == '.' || *p == ':')
            p++;
    }
    if (p > v && p[-1] == ':') {
        WORD n = 0;
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        if (n)
            net_port = n;
    }
}

/* Charger DDEML et Winsock, une seule fois ; seulement si Netscape a chargé Winsock */
BOOL net_ready(void)
{
    static char wsa[400];               /* WSADATA */
    HINSTANCE dde, ws;
    UINT old;
    DDEINIT init;
    WSTARTUP startup;
    if (net_state)
        return net_state > 0;
    if (!GetModuleHandle("WINSOCK"))
        return FALSE;                   /* pas encore : réessayé plus tard */
    net_state = -1;
    old = SetErrorMode(SEM_NOOPENFILEERRORBOX);
    dde = LoadLibrary("DDEML.DLL");
    ws = LoadLibrary("WINSOCK.DLL");
    SetErrorMode(old);
    if ((UINT) dde < 32 || (UINT) ws < 32)
        return FALSE;
    init = (DDEINIT) GetProcAddress(dde, "DdeInitialize");
    pStr = (DDESTR) GetProcAddress(dde, "DdeCreateStringHandle");
    pFreeStr = (DDEFREESTR) GetProcAddress(dde, "DdeFreeStringHandle");
    pConnect = (DDECONN) GetProcAddress(dde, "DdeConnect");
    pDisconnect = (DDEDISC) GetProcAddress(dde, "DdeDisconnect");
    pTrans = (DDETRANS) GetProcAddress(dde, "DdeClientTransaction");
    pGet = (DDEGET) GetProcAddress(dde, "DdeGetData");
    pFreeData = (DDEFREEDATA) GetProcAddress(dde, "DdeFreeDataHandle");
    startup = (WSTARTUP) GetProcAddress(ws, "WSAStartup");
    pSocket = (WSOCKET) GetProcAddress(ws, "socket");
    pWConnect = (WCONNECT) GetProcAddress(ws, "connect");
    pSend = (WSEND) GetProcAddress(ws, "send");
    pRecv = (WRECV) GetProcAddress(ws, "recv");
    pClose = (WCLOSE) GetProcAddress(ws, "closesocket");
    pAsync = (WASYNC) GetProcAddress(ws, "WSAAsyncSelect");
    if (!init || !pStr || !pFreeStr || !pConnect || !pDisconnect || !pTrans || !pGet ||
        !pFreeData || !startup || !pSocket || !pWConnect || !pSend || !pRecv || !pClose ||
        !pAsync)
        return FALSE;
    if (init(&net_inst, MakeProcInstance((FARPROC) NetDdeCb,
                                         (HINSTANCE) GetWindowWord(hwnd_main, GWW_HINSTANCE)),
             NET_CLIENTONLY, 0L))
        return FALSE;
    if (startup(0x0101, wsa))
        return FALSE;
    net_proxy_setting();
    net_state = 1;
    return TRUE;
}

/* Requête DDE à Netscape (sujet, élément) ; réponse texte dans out */
static BOOL net_dde(const char *topic, const char *item, char *out, int max)
{
    NHSZ svc, top, it;
    NHCONV conv;
    NHDATA d;
    BOOL ok = FALSE;
    if (net_state <= 0 || net_in_dde)
        return FALSE;
    net_in_dde = TRUE;
    svc = pStr(net_inst, "NETSCAPE", NET_CP_ANSI);
    top = pStr(net_inst, topic, NET_CP_ANSI);
    it = pStr(net_inst, item, NET_CP_ANSI);
    conv = pConnect(net_inst, svc, top, NULL);
    if (conv) {
        d = pTrans(NULL, 0, conv, it, NET_CF_TEXT, NET_REQUEST, 5000, NULL);
        if (d) {
            DWORD n = pGet(d, out, max - 1, 0);
            out[n < (DWORD) max ? (int) n : max - 1] = 0;
            pFreeData(d);
            ok = TRUE;
        }
        pDisconnect(conv);
    }
    pFreeStr(net_inst, svc);
    pFreeStr(net_inst, top);
    pFreeStr(net_inst, it);
    net_in_dde = FALSE;
    return ok;
}

/* "chaîne", suivante : copie sans les guillemets ; renvoie la suite */
static const char *net_quoted(const char *p, char *out, int max)
{
    int k = 0;
    while (*p && *p != '"')
        p++;
    if (*p)
        p++;
    while (*p && !(*p == '"' && (p[1] == ',' || !p[1]))) {
        if (k < max - 1)
            out[k++] = *p;
        p++;
    }
    out[k] = 0;
    return *p ? p + 1 : p;
}

/* Adresse et titre de la page affichée par la fenêtre active de Netscape */
BOOL net_page_info(char *url, int nurl, char *title, int ntitle)
{
    char buf[600];
    const char *p;
    if (!net_dde("WWW_GetWindowInfo", "0xFFFFFFFF", buf, sizeof(buf)))
        return FALSE;
    p = net_quoted(buf, url, nurl);
    net_quoted(p, title, ntitle);
    return url[0] != 0;
}

/* Ouvrir une adresse dans la fenêtre active de Netscape (lien activé) */
BOOL net_open_url(const char *url)
{
    char item[300], buf[40];
    if (lstrlen(url) > 250)
        return FALSE;
    wsprintf(item, "\"%s\",,0xFFFFFFFF,0x0,,,", (LPSTR) url);
    return net_dde("WWW_OpenURL", item, buf, sizeof(buf));
}

BOOL net_busy(void)
{
    return net_sock != NET_INVALID;
}

static void net_finish(BOOL ok)
{
    if (net_sock == NET_INVALID)
        return;
    pAsync(net_sock, hwnd_main, 0, 0L);
    pClose(net_sock);
    net_sock = NET_INVALID;
    KillTimer(hwnd_main, TIMER_NET);
    nav_done(ok && net_status_ok && net_hstate == 2);
}

/* Demander la structure de la page d'adresse url au proxy */
BOOL net_fetch(const char *url)
{
    static const char hex[] = "0123456789ABCDEF";
    NSOCKADDR sa;
    char *q;
    if (net_state <= 0 || net_sock != NET_INVALID)
        return FALSE;
    lstrcpy(net_req, "GET http://pc486/tampon?url=");
    q = net_req + lstrlen(net_req);
    for (; *url && q < net_req + sizeof(net_req) - 60; url++) {
        BYTE c = (BYTE) *url;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
            *q++ = (char) c;
        else {
            *q++ = '%';
            *q++ = hex[c >> 4];
            *q++ = hex[c & 15];
        }
    }
    lstrcpy(q, " HTTP/1.0\r\nHost: pc486\r\nUser-Agent: Epaulard\r\n\r\n");
    net_sock = pSocket(2, 1, 0);        /* AF_INET, SOCK_STREAM */
    if (net_sock == NET_INVALID)
        return FALSE;
    if (pAsync(net_sock, hwnd_main, EPM_NET, NET_FD_READ | NET_FD_CONNECT | NET_FD_CLOSE)) {
        pClose(net_sock);
        net_sock = NET_INVALID;
        return FALSE;
    }
    memset(&sa, 0, sizeof(sa));
    sa.family = 2;
    sa.port = (WORD) ((net_port >> 8) | (net_port << 8));
    memcpy(sa.addr, net_ip, 4);
    net_hstate = 0;
    net_hlen = 0;
    net_status_ok = FALSE;
    nav_begin();
    pWConnect(net_sock, &sa, sizeof(sa));       /* (WSAEWOULDBLOCK : FD_CONNECT suivra) */
    SetTimer(hwnd_main, TIMER_NET, 10000, NULL);
    return TRUE;
}

/* Réponse HTTP : ligne d'état, en-têtes, puis corps donné à navweb.c */
static void net_data(const char *p, int n)
{
    while (n > 0 && net_hstate < 2) {
        char c = *p++;
        n--;
        if (c == '\r')
            continue;
        if (c != '\n') {
            if (net_hlen < (int) sizeof(net_hline) - 1)
                net_hline[net_hlen++] = c;
            continue;
        }
        net_hline[net_hlen] = 0;
        if (net_hstate == 0) {
            /* « HTTP/1.0 200 OK » */
            const char *s = net_hline;
            while (*s && *s != ' ')
                s++;
            net_status_ok = !_fstrncmp(s, " 200", 4);
            net_hstate = 1;
        } else if (!net_hlen)
            net_hstate = 2;
        net_hlen = 0;
    }
    if (n > 0)
        nav_feed(p, n);
}

/* EPM_NET : wParam socket, LOWORD(lParam) événement, HIWORD(lParam) erreur */
void net_event(WPARAM wp, LPARAM lp)
{
    char buf[512];
    int n;
    if ((NSOCKET) wp != net_sock || net_sock == NET_INVALID)
        return;
    if (HIWORD(lp)) {
        net_finish(FALSE);
        return;
    }
    if (LOWORD(lp) == NET_FD_CONNECT) {
        n = lstrlen(net_req);
        if (pSend(net_sock, net_req, n, 0) != n)
            net_finish(FALSE);
        return;
    }
    while ((n = pRecv(net_sock, buf, sizeof(buf), 0)) > 0)
        net_data(buf, n);
    if (LOWORD(lp) == NET_FD_CLOSE)
        net_finish(TRUE);
}

/* Délai dépassé */
void net_timeout(void)
{
    net_finish(FALSE);
}
