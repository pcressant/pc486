/* EPAULARD.EXE — parole : Apollo II sur COM1 ou Synthé3 local (EPAUS3.DLL), signes,
   écho du clavier. */
#include "ep.h"

/* ---------------------------------------------------------------- parole */

/* Synthé3 local (EPAUS3.DLL, même répertoire qu'EPAULARD.EXE) au lieu de l'Apollo II
   sur COM1 : [Parole] Synthese=Synthe3 dans EPAULARD.INI. La DLL reçoit les mêmes
   octets Apollo et rend les mêmes échos ; elle ne calcule que dans sa pompe (s3_tick),
   appelée par la boucle de messages, jamais dans un crochet. */
HINSTANCE s3_lib;
static UINT (FAR PASCAL *s3_open)(void);      /* 0 : sortie audio prête */
static int (FAR PASCAL *s3_write)(LPCSTR s, int n);
static int (FAR PASCAL *s3_read)(LPSTR buf, int n);
static BOOL (FAR PASCAL *s3_pump)(void);
static void (FAR PASCAL *s3_close)(void);
static BOOL (FAR PASCAL *s3_busy)(void);
static BOOL s3_posted, s3_timer;
int s3_error;                           /* Synthé3 demandé mais indisponible : raison */

static BOOL s3_load(void)
{
    char path[144];
    int n = GetModuleFileName((HINSTANCE) GetWindowWord(hwnd_main, GWW_HINSTANCE), path, sizeof(path) - 12);
    UINT mode;
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != ':')
        n--;
    lstrcpy(path + n, "EPAUS3.DLL");
    mode = SetErrorMode(SEM_NOOPENFILEERRORBOX);
    s3_lib = LoadLibrary(path);
    SetErrorMode(mode);
    if (s3_lib < HINSTANCE_ERROR) {
        s3_error = (int) s3_lib;        /* 2 : EPAUS3.DLL absente ; 11 : pas un 386... */
        s3_lib = 0;
        return FALSE;
    }
    s3_open = (UINT (FAR PASCAL *)(void)) GetProcAddress(s3_lib, "S3OPEN");
    s3_write = (int (FAR PASCAL *)(LPCSTR, int)) GetProcAddress(s3_lib, "S3WRITE");
    s3_read = (int (FAR PASCAL *)(LPSTR, int)) GetProcAddress(s3_lib, "S3READ");
    s3_pump = (BOOL (FAR PASCAL *)(void)) GetProcAddress(s3_lib, "S3PUMP");
    s3_close = (void (FAR PASCAL *)(void)) GetProcAddress(s3_lib, "S3CLOSE");
    s3_busy = (BOOL (FAR PASCAL *)(void)) GetProcAddress(s3_lib, "S3BUSY");
    s3_error = 100;                     /* 100 : DLL d'une autre version ; 1000+ : waveOut */
    if (s3_open && s3_write && s3_read && s3_pump && s3_close && s3_busy)
        s3_error = (int) s3_open();
    if (s3_error) {
        if (s3_close) s3_close();
        FreeLibrary(s3_lib);
        s3_lib = 0;
        return FALSE;
    }
    EpSpeech((FARPROC) s3_write, (FARPROC) s3_pump);
    s3_error = 0;
    return TRUE;
}

/* Pompe de Synthé3. Après une écriture (EPM_S3) : un bloc, puis au plus deux autres
   messages EPM_S3, pour que le premier son parte vite. Ensuite la minuterie de 55 ms
   (TIMER_S3, tant que la DLL a de la parole ou le périphérique ouvert) calcule
   jusqu'à quatre blocs de 51 ms par tick. Une chaîne de messages postés sans fin
   priverait Épaulard de WM_TIMER (produit seulement file vide) : scrutation, relais
   de l'invite MS-DOS (Épaudos se croirait abandonné) et dire tout s'arrêteraient. */
void s3_tick(BOOL timer)
{
    static int chain;
    int i;
    BOOL more = FALSE;
    s3_posted = FALSE;
    if (!s3_lib)
        return;
    if (timer)
        chain = 0;
    for (i = 0; i < (timer ? 4 : 1); i++)
        if (!(more = s3_pump()))
            break;
    if (more && !timer && ++chain < 3)
        s3_posted = PostMessage(hwnd_main, EPM_S3, 0, 0L);
    if (s3_busy() != s3_timer) {
        s3_timer = !s3_timer;
        if (s3_timer)
            SetTimer(hwnd_main, TIMER_S3, 55, NULL);
        else
            KillTimer(hwnd_main, TIMER_S3);
    }
}

/* Fermeture : laisser finir la dernière phrase (2 s au plus), puis libérer. */
void s3_stop(void)
{
    DWORD t = GetTickCount();
    if (!s3_lib)
        return;
    while (s3_busy() && GetTickCount() - t < 2000)
        s3_pump();
    EpSpeech(NULL, NULL);
    s3_close();
    FreeLibrary(s3_lib);
    s3_lib = 0;
}

/* S3RES chargé avant Windows (INT 2Fh D300h, renvoyé à la machine DOS) : l'utilisateur
   a choisi Synthé3 (Synthese=Synthe3 dans EPAUDOS.INI, lu par S3RES). Sous Windows il dort ; seule sa
   présence compte, Épaulard parle par EPAUS3.DLL. */
static BOOL s3_resident(void)
{
#ifdef EP_NATIVE_TEST
    return sim_s3res;
#else
    WORD r = 0;
    _asm {
        push es
        push di
        push bx
        mov ax, 0D300h
        xor bx, bx
        xor di, di
        int 2Fh
        mov r, ax
        pop bx
        pop di
        pop es
    }
    return r == 0xD3FF;
#endif
}

/* [Parole] Synthese : Apollo (COM1), Synthe3 (EPAUS3.DLL), Auto (défaut : Synthé3 si
   S3RES est résident, sinon Apollo). */
void com_open(void)
{
    DCB dcb;
    char engine[16];
    com = -1;
    GetPrivateProfileString("Parole", "Synthese", "Auto", engine, sizeof(engine), ini_path);
    if (lstrcmpi(engine, "Synthe3") == 0 || (lstrcmpi(engine, "Apollo") != 0 && s3_resident())) {
        if (s3_load())
            return;
        /* Synthé3 demandé mais indisponible (DLL absente, aucun pilote audio) :
           Apollo II sur COM1, qui annonce la raison au démarrage (s3_error). */
        MessageBeep(MB_ICONHAND);
    }
    com = OpenComm("COM1", 256, 4096);  /* file d'envoi : 7 s de parole à 9600 bauds */
    if (com < 0)
        return;
    if (BuildCommDCB("COM1:9600,n,8,1", &dcb) == 0)
        SetCommState(&dcb);
}

void com_write(const char FAR *s, int n)
{
    if (s3_lib && n > 0) {
        s3_write(s, n);
        if (!s3_posted)
            s3_posted = PostMessage(hwnd_main, EPM_S3, 0, 0L);
    } else if (com >= 0 && n > 0)
        WriteComm(com, (LPSTR) s, n);
}

void silence(void)
{
    static const char x = 0x18;         /* Ctrl-X : arrêt immédiat */
    com_write(&x, 1);
}

/* Débit, hauteur, voix, ponctuation et volume envoyés à l'Apollo II, dans une
   proposition vide : CR plutôt que le « . » de Sonolect, qui serait dit avec @P1.
   Volume 1..8 : niveaux @A de l'Apollo II 0 1 2 3 5 7 A F. */
void voice_apply(void)
{
    static const char volumes[] = "012357AF";
    char cmd[40];
    wsprintf(cmd, "@W%d@F%X@V%d@P%d@A%c\r", cfg[CFG_RATE], cfg[CFG_PITCH], cfg[CFG_VOICE],
             cfg[CFG_PUNCT] == 2 ? 1 : 0, volumes[cfg[CFG_VOLUME] - 1]);
    com_write(cmd, lstrlen(cmd));
}

/* Nom d'un signe : dit pour un caractère seul (la synthèse tait la ponctuation), et
   dans le texte selon le réglage de ponctuation */
const char *sign_name(char c)
{
    switch (c) {
    case ' ': return "espace";
    case '\t': return "tabulation";
    case '.': return "point";
    case ',': return "virgule";
    case ';': return "point-virgule";
    case ':': return "deux-points";
    case '!': return "point d'exclamation";
    case '?': return "point d'interrogation";
    case '\'': return "apostrophe";
    case '"': return "guillemet";
    case '-': return "tiret";
    case '(': return "parenthèse ouvrante";
    case ')': return "parenthèse fermante";
    case '[': return "crochet ouvrant";
    case ']': return "crochet fermant";
    case '{': return "accolade ouvrante";
    case '}': return "accolade fermante";
    case '@': return "arobase";
    case '#': return "dièse";
    case '$': return "dollar";
    case '%': return "pour cent";
    case '&': return "et commercial";
    case '*': return "étoile";
    case '+': return "plus";
    case '/': return "barre oblique";
    case '\\': return "barre oblique inverse";
    case '=': return "égal";
    case '<': return "inférieur";
    case '>': return "supérieur";
    case '|': return "barre verticale";
    case '~': return "tilde";
    case '_': return "souligné";
    case '^': return "circonflexe";
    case '`': return "accent grave";
    }
    return NULL;
}

/* Signes nommés dans le texte au niveau « certaine » : ceux d'un chemin, d'une
   adresse ou d'une formule ; ni la ponctuation des phrases, ni les parenthèses et
   crochets (sections [WIN] des fichiers INI) */
BOOL sign_some(char c)
{
    return c && _fstrchr("@#$%&*+/\\=<>|~_^", c) != NULL;
}

/* Caractères Windows sans équivalent dans la page de codes DOS 850 : AnsiToOem les
   remplacerait par n'importe quoi (™ devenait « Ö »). */
void ansi_plain(const char FAR *s, char *out, int max)
{
    int n = 0;
    const char *r;
    for (; *s && n < max - 4; s++) {
        switch ((BYTE) *s) {
        case 0x91: case 0x92: r = "'"; break;
        case 0x93: case 0x94: r = "\""; break;
        case 0x96: case 0x97: case 0x95: r = "-"; break;
        case 0x85: r = "..."; break;
        case 0x99: r = " TM"; break;
        case 0x8C: r = "OE"; break;
        case 0x9C: r = "oe"; break;
        case 0x80: r = " euros"; break;
        default:
            out[n++] = *s;
            continue;
        }
        while (*r && n < max - 1)
            out[n++] = *r++;
    }
    out[n] = 0;
}

/* Signes nommés selon le réglage de ponctuation : « certaine » nomme ceux de
   sign_some() ; « toute » laisse l'Apollo II dire la ponctuation (@P1), sauf « @ »,
   qui ouvrirait une commande. Quatre signes identiques ou plus forment un trait de
   séparation, jamais dit. */
void punct_words(const char *in, char *out, int max)
{
    int n = 0;
    const char *w;
    for (; *in && n < max - 1; in++) {
        if (sign_some(*in) && in[1] == *in && in[2] == *in && in[3] == *in) {
            /* trait de séparation (« ______ », « ***** ») : décor, tu */
            while (in[1] == *in)
                in++;
            if (n && out[n - 1] == ' ' && in[1] == ' ')
                in++;                   /* une seule espace à sa place */
            else if (n && out[n - 1] != ' ' && in[1] && in[1] != ' ')
                out[n++] = ' ';
            continue;
        }
        if ((cfg[CFG_PUNCT] == 1 && sign_some(*in)) || (cfg[CFG_PUNCT] == 2 && *in == '@')) {
            w = sign_name(*in);
            if (n + lstrlen(w) + 2 >= max - 1)
                break;
            if (n && out[n - 1] != ' ')
                out[n++] = ' ';
            lstrcpy(out + n, w);
            n += lstrlen(w);
            if (in[1] && in[1] != ' ')
                out[n++] = ' ';
        } else
            out[n++] = *in;
    }
    out[n] = 0;
}

/* Dit un texte ANSI. cut : couper d'abord ce qui est en cours. */
void say(const char FAR *s, BOOL cut)
{
    static char raw[1100], buf[1100];  /* (pile de Win16 : statiques) */
    int n, i;

    if (cut)
        silence();
    ansi_plain(s, raw, sizeof(raw) - 2);
    punct_words(raw, buf, sizeof(buf));
    AnsiToOem(buf, buf);                /* l'Apollo II attend du CP850 */
    for (i = 0; buf[i]; i++)
        if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == 0x18 || buf[i] == '@')
            buf[i] = ' ';               /* CR termine un message, @ ouvre une commande */
    n = lstrlen(buf);
    buf[n++] = '\r';
    com_write(buf, n);
}

/* Message marqué : quand sa lecture est finie, l'Apollo II renvoie l'octet mark sur
   COM1. Commande d'écho authentique @E (APOLLO.TXT) placée dans sa propre proposition
   après le texte : l'Apollo la traite quand la lecture atteint cette proposition, donc
   à la fin du texte. Un Ctrl-X efface l'écho avec le texte. S'ajoute à la file sans couper. */
void say_mark(const char FAR *s, char mark)
{
    char cmd[4];
    say(s, FALSE);
    cmd[0] = '@';
    cmd[1] = 'E';
    cmd[2] = mark;
    cmd[3] = '\r';
    com_write(cmd, 4);
}

/* Caractère dit seul : nom des signes, « majuscule » selon le réglage */
void say_char(char c, BOOL cut)
{
    char buf[24];
    const char *name = sign_name(c);
    if (name) {
        say(name, cut);
        return;
    }
    if (cfg[CFG_CAPS] && IsCharUpper(c))
        wsprintf(buf, "majuscule %c", c);
    else {
        buf[0] = c;
        buf[1] = 0;
    }
    say(buf, cut);
}

/* Octets renvoyés par l'Apollo II : échos @E, marques de fin de lecture */
void com_read(void)
{
    char buf[32];
    int n, i;
    COMSTAT st;

    if (com < 0 && !s3_lib)
        return;
    for (;;) {
        if (s3_lib)
            n = s3_read(buf, sizeof(buf));
        else
            n = ReadComm(com, buf, sizeof(buf));
        if (n < 0) {
            GetCommError(com, &st);
            n = -n;
        }
        if (n <= 0)
            return;
        for (i = 0; i < n; i++)
            if (dos_relay)
                dos_rx(buf[i]);         /* écho attendu par Épaudos */
            else
                sa_mark(buf[i]);
    }
}

/* ---------------------------------------------------------- écho du clavier */

char typed_word[40];     /* mot en cours de frappe (écho par mots) */

/* Caractère tapé : selon le réglage, le caractère, le mot qu'il termine, ou les deux */
void echo_char(char ch)
{
    int mode = cfg[CFG_ECHO];
    BOOL chars = mode == 0 || mode == 2, words = mode == 1 || mode == 2;

    if (ch != '\r')
        typed = grid_typed = TRUE;      /* le déplacement du curseur n'est pas lu */
    if (IsCharAlphaNumeric(ch)) {
        if (typed_n < (int) sizeof(typed_word) - 1)
            typed_word[typed_n++] = ch;
        if (chars)
            say_char(ch, TRUE);
        return;
    }
    typed_word[typed_n] = 0;
    if (words && typed_n)
        say(typed_word, TRUE);          /* le mot que ce signe termine */
    else if (chars && ch != '\r')
        say_char(ch, TRUE);             /* (écho par mots seuls : rien) */
    typed_n = 0;
}
