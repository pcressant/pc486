/* EPAULARD.EXE — parole : COM1, protocole Apollo II, signes, écho du clavier. */
#include "ep.h"

/* ---------------------------------------------------------------- parole */

void com_open(void)
{
    DCB dcb;
    com = OpenComm("COM1", 256, 4096);  /* file d'envoi : 7 s de parole à 9600 bauds */
    if (com < 0)
        return;
    if (BuildCommDCB("COM1:9600,n,8,1", &dcb) == 0)
        SetCommState(&dcb);
}

void com_write(const char FAR *s, int n)
{
    if (com >= 0 && n > 0)
        WriteComm(com, (LPSTR) s, n);
}

void silence(void)
{
    static const char x = 0x18;         /* Ctrl-X : arrêt immédiat */
    com_write(&x, 1);
}

/* Débit, hauteur, voix et ponctuation envoyés à l'Apollo II, dans une proposition
   vide : CR plutôt que le « . » de Sonolect, qui serait dit avec @P1. */
void voice_apply(void)
{
    char cmd[32];
    wsprintf(cmd, "@W%d@F%X@V%d@P%d\r", cfg[CFG_RATE], cfg[CFG_PITCH], cfg[CFG_VOICE],
             cfg[CFG_PUNCT] == 2 ? 1 : 0);
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

    if (com < 0)
        return;
    for (;;) {
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
