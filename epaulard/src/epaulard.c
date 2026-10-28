/* EPAULARD.EXE — lecteur d'écran pour Windows 3.1x (synthèse Apollo sur COM1).
 *
 * Suit la fenêtre active et le contrôle qui a le focus (scrutation toutes les 100 ms),
 * lit les menus et les caractères tapés (signalés par EPAUHOOK.DLL), et parle en
 * envoyant le protocole Apollo sur COM1 : texte terminé par CR, Ctrl-X pour se taire.
 *
 * Commandes (Inser + touche) :
 *   Inser+T      titre de la fenêtre active
 *   Inser+Tab    relire le contrôle qui a le focus
 *   Inser+Haut   relire la ligne courante
 *   Inser+Bas    dire tout : lire jusqu'à la fin du document, le curseur suit la lecture
 *   Inser+B      lire tout le contenu de la fenêtre (boîte de dialogue)
 *   Inser+O      lire tout le texte affiché dans la fenêtre (modèle hors écran)
 *   Inser+F      police et mise en forme sous le curseur (ou le point de revue)
 *   Inser+Maj+Haut  lire la sélection
 *   / et * du pavé  clic gauche, clic droit ; Maj : verrouiller le bouton
 *   Inser+/      amener la souris au point de revue ; Inser+* : revue à la souris
 *   Inser+PgSuiv lire la barre d'état
 *   Inser+E      réglages (débit, hauteur, voix, ponctuation, écho, majuscules),
 *                enregistrés dans EPAULARD.INI à côté du programme
 *   Inser+H      aide sur les commandes
 *   Inser+F4     quitter Épaulard
 *   Ctrl         couper la parole
 */
#include <windows.h>
#include <string.h>
#ifdef EP_NATIVE_TEST
#include <time.h>
#else
#include <dos.h>
#endif
#include "epaulard.h"
#include "braille.h"

#define TIMER_ID   1
#define TIMER_HI   2            /* regroupement des morceaux de texte en surbrillance */
#define TIMER_SA   3            /* lecture continue (dire tout) */
#define TIMER_FR   4            /* rectangle de focus : attendre la fin du dessin */
#define TIMER_GL   5            /* case cochée : attendre l'image définitive */
#define TIMER_DLG  6            /* boîte de Word ouverte : attendre son texte */
#define TIMER_MS   7            /* clic de routage : remettre ensuite le pointeur */
#define TIMER_MENU 8            /* titre de menu : attendre la fin de la rafale */
#define TIMER_TAB  9            /* Ctrl+Tab dans une boîte de Word : page affichée */
#define TIMER_LB   10           /* liste dessinée annoncée avant d'être peinte */
#define POLL_MS    100
#define SA_MS      50
#define TXT        256

static int com = -1;
static HWND hwnd_main;
static char braille_menu[TXT * 2];

/* État de la dernière lecture, pour n'annoncer que les changements */
static HWND last_active, last_focus;
static int last_sel = -2, last_line = -2, last_check = -2;
static DWORD last_esel;
static BOOL typed;              /* caractère tapé : le prochain déplacement du curseur n'est pas lu */
static BOOL in_menu;
static BOOL new_active;         /* une fenêtre vient d'être activée : dire son titre */
static DWORD focus_tick;        /* heure de la dernière annonce du focus */
static char focus_said[TXT * 2];/* texte de cette annonce (pour ne pas la répéter) */
static POINT last_caret;        /* curseur texte (écran) dans une fenêtre non standard */
static BOOL line_key;           /* navigation verticale, même si l'écran défile */
static UINT last_vk;            /* dernière touche transmise au programme */
static DWORD line_key_tick;
static BOOL word_key;           /* Ctrl+Gauche ou Ctrl+Droite : lire le mot */
static DWORD caret_settle;      /* curseur ramené après un dire tout (heure), à attendre */
static BOOL caret_quiet;        /* ... sans annoncer sa nouvelle position */

static char hilite_said[TXT];   /* dernier élément en surbrillance annoncé */
static DWORD hilite_tick;
static DWORD popup_tick;        /* annonce d'un menu déroulant : son 1er élément suit sans couper */
static char menu_pending[TXT * 2];  /* titre de menu sélectionné, pas encore dit */
static BOOL flat_review;
static HWND review_window;
static POINT review_point;
static int review_column;

static BOOL sel_key;            /* Maj + déplacement : annoncer la sélection */
static DWORD sel_key_tick;
static char sel_said[TXT * 2];  /* texte sélectionné (dessiné) à la dernière annonce */
static HWND sdm_hwnd;           /* boîte de Word : élément qui a le focus, pour le braille */
static char sdm_text[TXT * 2];

/* -------------------------------------------------------------- réglages */

/* Réglages (Inser+E), lus et enregistrés dans EPAULARD.INI, section [Parole] */
#define CFG_N 6
static int cfg[CFG_N] = { 3, 8, 1, 1, 0, 0 };   /* débit, hauteur : défauts de l'Apollo II */
#define CFG_RATE  0             /* débit Apollo II @W, 1 à 9 */
#define CFG_PITCH 1             /* hauteur @F, 0 à 15 */
#define CFG_VOICE 2             /* voix préréglée @V, 1 à 6 */
#define CFG_PUNCT 3             /* ponctuation : aucune, certaine, toute */
#define CFG_ECHO  4             /* écho du clavier : caractères, mots, les deux, aucun */
#define CFG_CAPS  5             /* dire « majuscule » devant une lettre seule */
static const char *const cfg_key[CFG_N] = {
    "Debit", "Hauteur", "Voix", "Ponctuation", "Echo", "Majuscules" };
static const char *const cfg_label[CFG_N] = {
    "Débit", "Hauteur", "Voix", "Ponctuation", "Écho du clavier", "Majuscules" };
static const int cfg_min[CFG_N] = { 1, 0, 1, 0, 0, 0 };
static const int cfg_max[CFG_N] = { 9, 15, 6, 2, 3, 1 };
static char ini_path[144];
static int typed_n;             /* longueur du mot en cours de frappe (écho par mots) */
static BOOL cfg_on;             /* menu des réglages ouvert */
static DWORD cfg_tick;          /* dernière touche dans ce menu */
static void cfg_close(BOOL keep);
static void cfg_open(void);

static void cfg_load(HINSTANCE hinst)
{
    int i, n, v;
    n = GetModuleFileName(hinst, ini_path, sizeof(ini_path) - 14);
    while (n > 0 && ini_path[n - 1] != '\\' && ini_path[n - 1] != ':')
        n--;
    lstrcpy(ini_path + n, "EPAULARD.INI");
    for (i = 0; i < CFG_N; i++) {
        v = GetPrivateProfileInt("Parole", cfg_key[i], cfg[i], ini_path);
        if (v >= cfg_min[i] && v <= cfg_max[i])
            cfg[i] = v;
    }
}

static void cfg_save(void)
{
    char v[8];
    int i;
    for (i = 0; i < CFG_N; i++) {
        wsprintf(v, "%d", cfg[i]);
        WritePrivateProfileString("Parole", cfg_key[i], v, ini_path);
    }
}

/* ---------------------------------------------------------------- parole */

static void com_open(void)
{
    DCB dcb;
    com = OpenComm("COM1", 256, 4096);  /* file d'envoi : 7 s de parole à 9600 bauds */
    if (com < 0)
        return;
    if (BuildCommDCB("COM1:9600,n,8,1", &dcb) == 0)
        SetCommState(&dcb);
}

static void com_write(const char FAR *s, int n)
{
    if (com >= 0 && n > 0)
        WriteComm(com, (LPSTR) s, n);
}

static void silence(void)
{
    static const char x = 0x18;         /* Ctrl-X : arrêt immédiat */
    com_write(&x, 1);
}

/* Débit, hauteur, voix et ponctuation envoyés à l'Apollo II, dans une proposition
   vide : CR plutôt que le « . » de Sonolect, qui serait dit avec @P1. */
static void voice_apply(void)
{
    char cmd[32];
    wsprintf(cmd, "@W%d@F%X@V%d@P%d\r", cfg[CFG_RATE], cfg[CFG_PITCH], cfg[CFG_VOICE],
             cfg[CFG_PUNCT] == 2 ? 1 : 0);
    com_write(cmd, lstrlen(cmd));
}

/* Nom d'un signe : dit pour un caractère seul (la synthèse tait la ponctuation), et
   dans le texte selon le réglage de ponctuation */
static const char *sign_name(char c)
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
static BOOL sign_some(char c)
{
    return c && _fstrchr("@#$%&*+/\\=<>|~_^", c) != NULL;
}

/* Caractères Windows sans équivalent dans la page de codes DOS 850 : AnsiToOem les
   remplacerait par n'importe quoi (™ devenait « Ö »). */
static void ansi_plain(const char FAR *s, char *out, int max)
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
static void punct_words(const char *in, char *out, int max)
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
static void say(const char FAR *s, BOOL cut)
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
static void say_mark(const char FAR *s, char mark)
{
    char cmd[4];
    say(s, FALSE);
    cmd[0] = '@';
    cmd[1] = 'E';
    cmd[2] = mark;
    cmd[3] = '\r';
    com_write(cmd, 4);
}

static void sa_mark(char c);

/* Caractère dit seul : nom des signes, « majuscule » selon le réglage */
static void say_char(char c, BOOL cut)
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
static void com_read(void)
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
            sa_mark(buf[i]);
    }
}

/* ------------------------------------------------------------- utilitaires */
/* Mot du texte qui commence ou passe à la colonne col */
static void say_word(char *text, int col)
{
    int start = col, end = col, n = lstrlen(text);
    if (col >= n) {
        say("fin de ligne", TRUE);
        return;
    }
    while (start > 0 && text[start - 1] != ' ' && text[start - 1] != '\t')
        start--;
    while (end < n && text[end] != ' ' && text[end] != '\t')
        end++;
    text[end] = 0;
    say(text[start] ? text + start : "espace", TRUE);
}


/* Retire les « & » des libellés (« &Fichier » -> « Fichier ») et coupe à la tabulation
   (raccourci des menus), renvoyée dans *accel. */
static void clean_label(char *s, char **accel)
{
    char *d = s, *p = s;
    if (accel)
        *accel = NULL;
    for (; *p; p++) {
        if (*p == '&') {
            if (p[1] == '&')
                *d++ = *++p;
            continue;
        }
        if (*p == '\t') {
            *d++ = 0;
            if (accel)
                *accel = d;
            continue;
        }
        *d++ = *p;
    }
    *d = 0;
    /* « Palette de couleurs >> » : flèches de décor, pas « supérieur supérieur » */
    while (d - s >= 2 && (_fstrncmp(d - 2, ">>", 2) == 0 || _fstrncmp(d - 2, "<<", 2) == 0 ||
                          d[-1] == ' ')) {
        d -= d[-1] == ' ' ? 1 : 2;
        *d = 0;
    }
}

static void class_of(HWND h, char *cls, int n)
{
    GetClassName(h, cls, n);
    AnsiUpper(cls);
}

/* Style WS_VISIBLE etc. du contrôle lui-même : IsWindowVisible() répond non tant que
   la boîte de dialogue qui le contient n'est pas encore affichée. */
static BOOL has_style(HWND h, long st)
{
    return (GetWindowLong(h, GWL_STYLE) & st) != 0;
}

static BOOL is_class(HWND h, const char *name)
{
    char cls[32];
    if (!h)
        return FALSE;
    class_of(h, cls, sizeof(cls));
    return lstrcmp(cls, name) == 0;
}

/* Contrôle standard, lu directement (sinon : modèle hors écran) */
static BOOL is_standard(HWND h)
{
    return is_class(h, "BUTTON") || is_class(h, "EDIT") || is_class(h, "LISTBOX") ||
           is_class(h, "COMBOBOX") || is_class(h, "SCROLLBAR");
}

/* Curseur texte, en coordonnées écran : celui que le programme dessine lui-même s'il y
   en a un dans cette fenêtre (Write), sinon le curseur système de Windows. */
static void caret_screen(HWND h, POINT *pt)
{
    if (EpOsmCaret(h, pt))
        return;
    GetCaretPos(pt);
    ClientToScreen(h, pt);
}

/* Texte sans lettre ni chiffre (« / », « : » entre les champs de Date/Heure) */
static BOOL no_word(const char *s)
{
    for (; *s; s++)
        if (IsCharAlphaNumeric(*s))
            return FALSE;
    return TRUE;
}

static BOOL ends_colon(const char *s)
{
    int n = lstrlen(s);
    while (n > 0 && s[n - 1] == ' ')
        n--;
    return n > 0 && s[n - 1] == ':';
}

/* Libellé dessiné par la fenêtre parente (boîtes de dialogue de Word, sans contrôle
   STATIC) : texte du modèle terminé par « : » juste au-dessus du rectangle, une
   rangée plus haut (« Répertoires: » au-dessus du chemin), ou à sa gauche. Sans
   « : », le texte juste au-dessus seulement si any est vrai (listes, zones de
   saisie, qui ont toujours un libellé). */
/* Libellé à gauche, sur la même rangée : le texte terminé par « : » le plus proche
   (« De: » et non « Normal De: » quand une liste déroulante le précède) */
static BOOL label_left(HWND parent, const RECT *rc, char *out, int n)
{
    char buf[TXT];
    int l;
    out[0] = 0;
    for (l = rc->left - 200; l < rc->left; l += 4) {
        if (!EpOsmRect(parent, l, rc->top, rc->left, rc->bottom, buf, sizeof(buf)))
            break;
        if (ends_colon(buf))
            lstrcpyn(out, buf, n);      /* de plus en plus près */
    }
    return out[0] != 0;
}

static void label_drawn(HWND parent, const RECT *rc, char *out, int n, BOOL any)
{
    char near_[TXT], far_[TXT];

    out[0] = 0;
    if (!parent)
        return;
    EpOsmRect(parent, rc->left - 4, rc->top - 22, rc->right, rc->top, near_, sizeof(near_));
    if (ends_colon(near_))
        lstrcpyn(out, near_, n);
    else if (EpOsmRect(parent, rc->left - 4, rc->top - 44, rc->right, rc->top - 22,
                       far_, sizeof(far_)) && ends_colon(far_)) {
        lstrcpyn(out, far_, n);
        if (near_[0] && lstrlen(out) + lstrlen(near_) + 2 < n) {
            lstrcat(out, " ");
            lstrcat(out, near_);
        }
    } else if (label_left(parent, rc, far_, sizeof(far_)))
        lstrcpyn(out, far_, n);
    else if (any)
        lstrcpyn(out, near_, n);
    clean_label(out, NULL);
}

/* Fenêtre SDM : boîte de dialogue de Word, dont les contrôles sont dessinés, ou page
   d'une boîte à onglets (Options : fenêtre enfant sa_sdm_…) */
static BOOL is_sdm_page(HWND h)
{
    char cls[32];
    if (!h)
        return FALSE;
    GetClassName(h, cls, sizeof(cls));
    return _fstrncmp(cls, "sa_sdm", 6) == 0;
}

static BOOL is_sdm(HWND h)
{
    char cls[32];
    if (!h)
        return FALSE;
    GetClassName(h, cls, sizeof(cls));
    return _fstrncmp(cls, "bosa_sdm", 8) == 0 || _fstrncmp(cls, "sa_sdm", 6) == 0;
}

/* Convention des boîtes de dialogue : le libellé précède son contrôle dans l'ordre
   des fenêtres (c'est lui qui porte le raccourci), éventuellement après des textes
   intermédiaires (« Longue », « Courte » aux extrémités d'un curseur du Clavier).
   La géométrie, ensuite, peut désigner un texte plus proche mais étranger
   (« Bienvenue… » au-dessus de « Quel est votre nom? » dans la Dame de Pique). */
static BOOL label_zorder(HWND h, char *out, int n)
{
    HWND p = GetWindow(h, GW_HWNDPREV);
    long st;
    out[0] = 0;
    /* zone juste après un bouton radio ou une case qui la règle :
       « Inactivité (secs) [2] » de 386 étendu */
    if (p && is_class(p, "BUTTON") && is_class(h, "EDIT") && has_style(p, WS_VISIBLE) &&
        ((st = GetWindowLong(p, GWL_STYLE) & 0x0F) == BS_RADIOBUTTON ||
         st == BS_AUTORADIOBUTTON || st == BS_CHECKBOX || st == BS_AUTOCHECKBOX)) {
        GetWindowText(p, out, n);
        clean_label(out, NULL);
        if (out[0])
            return TRUE;
    }
    for (p = GetWindow(h, GW_HWNDPREV); p && is_class(p, "STATIC") &&
         (GetWindowLong(p, GWL_STYLE) & 0x0F) <= SS_RIGHT; p = GetWindow(p, GW_HWNDPREV)) {
        if (!has_style(p, WS_VISIBLE) || GetWindowTextLength(p) == 0)
            continue;
        GetWindowText(p, out, n);
        if (no_word(out))
            break;                      /* séparateur : pas un libellé */
        if (ends_colon(out) || _fstrchr(out, '&') || _fstrchr(out, '?')) {
            clean_label(out, NULL);
            return TRUE;
        }
        out[0] = 0;
    }
    return FALSE;
}

/* Libellé d'un contrôle sans texte propre : le texte statique le plus proche
   au-dessus de lui (colonnes qui se chevauchent) ou à sa gauche (même hauteur). */
static void label_before(HWND h, char *out, int n)
{
    HWND p, best = NULL;
    RECT rc, r;
    long d, best_d = 0x7fffffffL;

    out[0] = 0;
    if (label_zorder(h, out, n))
        return;
    GetWindowRect(h, &rc);
    for (p = GetWindow(GetParent(h), GW_CHILD); p; p = GetWindow(p, GW_HWNDNEXT)) {
        if (p == h || !has_style(p, WS_VISIBLE) || !is_class(p, "STATIC") ||
            (GetWindowLong(p, GWL_STYLE) & 0x0F) > SS_RIGHT ||
            GetWindowTextLength(p) == 0 || GetWindowTextLength(p) > TXT - 1)
            continue;
        GetWindowText(p, out, n);
        if (no_word(out))
            continue;                   /* « / » entre deux champs */
        GetWindowRect(p, &r);
        if (r.bottom <= rc.top + 4 && r.left < rc.right && r.right > rc.left)
            d = (long) (rc.top - r.bottom) * 2 + (r.left > rc.left ? r.left - rc.left : 0);
        else if (r.right <= rc.left + 4 && r.top < rc.bottom && r.bottom > rc.top)
            d = rc.left - r.right;
        else
            continue;
        if (d < best_d) {
            best_d = d;
            best = p;
        }
    }
    out[0] = 0;
    if (best) {
        GetWindowText(best, out, n);
        clean_label(out, NULL);
    }
}

static void append_n(char *dst, const char *s, int max)
{
    if (!s || !*s)
        return;
    if (*dst && lstrlen(dst) + lstrlen(s) + 2 < max)
        lstrcat(dst, ", ");
    if (lstrlen(dst) + lstrlen(s) < max)
        lstrcat(dst, s);
}

static void append(char *dst, const char *s)
{
    append_n(dst, s, TXT * 2);
}

/* Tronque s pour laisser la place, dans un tampon de max octets, aux ajouts que
   append_n ferait de chacun des morceaux essentiels (rôle, valeur, état). */
static void fit_before(char *s, int max, const char *a, const char *b, const char *c)
{
    int room = max - 1;
    if (a && *a) room -= lstrlen(a) + 2;
    if (b && *b) room -= lstrlen(b) + 2;
    if (c && *c) room -= lstrlen(c) + 2;
    if (room < 0)
        room = 0;
    if (lstrlen(s) > room)
        s[room] = 0;
}

/* État essentiel (« sélectionné »...) : raccourcir le libellé plutôt que le perdre */
static void append_keep(char *dst, const char *s, int max)
{
    fit_before(dst, max, s, NULL, NULL);
    append_n(dst, s, max);
}

/* Ligne courante d'une zone d'édition multiligne */
static int edit_line(HWND h, char *out, int n)
{
    int line = (int) SendMessage(h, EM_LINEFROMCHAR, (WPARAM) -1, 0L);
    int len;
    *(WORD *) out = (WORD) (n - 1);
    len = (int) SendMessage(h, EM_GETLINE, line, (LPARAM) (LPSTR) out);
    out[len < n ? len : n - 1] = 0;
    return line;
}

/* Zone de plusieurs lignes : style, et hauteur de deux lignes au moins (les champs
   de Date/Heure sont ES_MULTILINE pour centrer un seul nombre) */
static BOOL is_multiline(HWND h)
{
    RECT r;
    if (!(GetWindowLong(h, GWL_STYLE) & ES_MULTILINE))
        return FALSE;
    GetWindowRect(h, &r);
    return r.bottom - r.top >= 28;
}


/* Texte d'une zone d'édition entre les positions a (incluse) et b (exclue) ;
   lignes séparées par une espace. */
static void edit_range(HWND h, int a, int b, char *out, int max)
{
    char line[TXT * 2];
    int la, lb, l, start, from, to, n, k, len = 0;

    out[0] = 0;
    if (!is_multiline(h)) {
        GetWindowText(h, line, sizeof(line));
        n = lstrlen(line);
        if (b > n) b = n;
        if (a < b)
            lstrcpyn(out, line + a, min(b - a + 1, max));
        return;
    }
    la = (int) SendMessage(h, EM_LINEFROMCHAR, a, 0L);
    lb = (int) SendMessage(h, EM_LINEFROMCHAR, b, 0L);
    for (l = la; l <= lb && len < max - 2; l++) {
        start = (int) SendMessage(h, EM_LINEINDEX, l, 0L);
        *(WORD *) line = sizeof(line) - 1;
        n = (int) SendMessage(h, EM_GETLINE, l, (LPARAM) (LPSTR) line);
        line[n] = 0;
        from = l == la ? a - start : 0;
        to = l == lb ? b - start : n;
        if (from < 0) from = 0;
        if (to > n) to = n;
        if (l > la && len)
            out[len++] = ' ';
        if (from < to) {
            k = min(to - from, max - 1 - len);
            _fmemcpy(out + len, line + from, k);
            len += k;
        }
        out[len] = 0;
    }
}

/* « abc sélectionné », comme NVDA ; une longue sélection est seulement comptée */
static void say_selection(const char *text, int count, BOOL selected)
{
    char out[TXT * 2 + 40];
    const char *what = selected ? "sélectionné" : "désélectionné";
    int n;

    while (*text == ' ')
        text++;
    if (count > 400) {
        wsprintf(out, "%d caractères %s", count, (LPSTR) what);
    } else {
        lstrcpyn(out, *text ? text : "espace", TXT * 2);
        n = lstrlen(out);
        while (n > 0 && out[n - 1] == ' ')
            out[--n] = 0;
        lstrcat(out, " ");
        lstrcat(out, what);
    }
    say(out, TRUE);
}

/* Sélection d'une zone d'édition changée de old à cur (EM_GETSEL) : dire ce qui
   s'y ajoute ou s'en retire. FALSE si rien à dire (aucune sélection). */
static BOOL edit_selection(HWND h, DWORD old, DWORD cur, BOOL shift)
{
    int s0 = LOWORD(old), e0 = HIWORD(old), s1 = LOWORD(cur), e1 = HIWORD(cur);
    int a, b;
    BOOL selected = TRUE;
    char text[TXT * 2];

    if ((s0 == e0 && s1 == e1) || (GetWindowLong(h, GWL_STYLE) & ES_PASSWORD))
        return FALSE;
    if (s1 == e1) {                     /* sélection retirée */
        if (!shift)
            return FALSE;               /* flèche seule : on dira le caractère */
        a = s0; b = e0; selected = FALSE;
    } else if (s0 == e0) {
        a = s1; b = e1;
    } else if (s1 == s0 && e1 != e0) {
        a = min(e0, e1); b = max(e0, e1); selected = e1 > e0;
    } else if (e1 == e0 && s1 != s0) {
        a = min(s0, s1); b = max(s0, s1); selected = s1 < s0;
    } else {
        a = s1; b = e1;
    }
    edit_range(h, a, b, text, sizeof(text));
    say_selection(text, b - a, selected);
    return TRUE;
}

/* Texte dessiné (Write, Word) : comparer la sélection affichée à la précédente */
static void drawn_selection(const char *old, const char *cur)
{
    static char part[TXT * 2];
    int lo = lstrlen(old), ln = lstrlen(cur);

    if (lstrcmp(old, cur) == 0)
        return;
    if (ln > lo && _fstrncmp(cur, old, lo) == 0)
        say_selection(cur + lo, ln - lo, TRUE);
    else if (ln > lo && lstrcmp(cur + ln - lo, old) == 0) {
        lstrcpyn(part, cur, ln - lo + 1);
        say_selection(part, ln - lo, TRUE);
    } else if (lo > ln && _fstrncmp(old, cur, ln) == 0)
        say_selection(old + ln, lo - ln, FALSE);
    else if (lo > ln && lstrcmp(old + lo - ln, cur) == 0) {
        lstrcpyn(part, old, lo - ln + 1);
        say_selection(part, lo - ln, FALSE);
    } else if (ln)
        say_selection(cur, ln, TRUE);
    else
        say("aucune sélection", TRUE);
}

/* Calculatrice de Windows 3.1 : SciCalc garde le focus, ses touches sont
   dessinées. La valeur est un STATIC (414 en standard, 413 en scientifique),
   pas l'EDIT caché 495. Lire le contrôle visible, même sans dessin GDI récent. */
static HWND calc_owner;
static char calc_last[TXT];
static BOOL calc_force;

/* États et parcours propres à CALC.EXE 3.1, sans déplacer son focus. */
#define CALC_STD 1
#define CALC_SCI 2
#define CALC_BOTH 3
#define CALC_DEC 4
#define CALC_HEX 8
#define CALC_PARTS 6
static char calc_previous[CALC_PARTS][64];
static int calc_view, calc_button = -1; /* 0 résultat, 1 bouton, 2 états */
static BOOL calc_queue;

typedef struct { UINT id; const char *name; int modes; } CALCBUTTON;
/* Identifiants WM_COMMAND observés dans CALC.EXE français (3.10). Certains
   changent de sens avec le mode : 103 = racine en standard, carré en scientifique. */
static const CALCBUTTON calc_buttons[] = {
    {81, "effacer tout", CALC_BOTH}, {82, "effacer l'entrée", CALC_BOTH},
    {83, "retour arrière", CALC_BOTH},
    {114, "mémoriser le résultat", CALC_BOTH},
    {113, "rappeler la mémoire", CALC_BOTH},
    {112, "effacer la mémoire", CALC_BOTH},
    {115, "ajouter à la mémoire", CALC_BOTH},
    {48, "zéro", CALC_BOTH}, {49, "un", CALC_BOTH},
    {50, "deux", CALC_BOTH}, {51, "trois", CALC_BOTH},
    {52, "quatre", CALC_BOTH}, {53, "cinq", CALC_BOTH},
    {54, "six", CALC_BOTH}, {55, "sept", CALC_BOTH},
    {56, "huit", CALC_BOTH}, {57, "neuf", CALC_BOTH},
    {85, "virgule", CALC_BOTH | CALC_DEC}, {80, "changer de signe", CALC_BOTH},
    {92, "plus", CALC_BOTH}, {93, "moins", CALC_BOTH},
    {91, "multiplier", CALC_BOTH}, {90, "diviser", CALC_BOTH},
    {111, "égal", CALC_BOTH},
    {103, "racine carrée", CALC_STD}, {108, "pourcentage", CALC_STD},
    {106, "inverse du nombre", CALC_BOTH | CALC_DEC},
    {110, "pi", CALC_SCI | CALC_DEC},
    {103, "carré", CALC_SCI | CALC_DEC}, {104, "cube", CALC_SCI | CALC_DEC},
    {95, "puissance", CALC_SCI | CALC_DEC},
    {98, "sinus", CALC_SCI | CALC_DEC}, {99, "cosinus", CALC_SCI | CALC_DEC},
    {100, "tangente", CALC_SCI | CALC_DEC},
    {101, "logarithme naturel", CALC_SCI | CALC_DEC},
    {102, "logarithme décimal", CALC_SCI | CALC_DEC},
    {105, "factorielle", CALC_SCI | CALC_DEC},
    {40, "ouvrir la parenthèse", CALC_SCI}, {41, "fermer la parenthèse", CALC_SCI},
    {65, "A", CALC_SCI | CALC_HEX}, {66, "B", CALC_SCI | CALC_HEX},
    {67, "C", CALC_SCI | CALC_HEX}, {68, "D", CALC_SCI | CALC_HEX},
    {69, "E", CALC_SCI | CALC_HEX}, {70, "F", CALC_SCI | CALC_HEX},
    {124, "base hexadécimale", CALC_SCI}, {123, "base décimale", CALC_SCI},
    {122, "base octale", CALC_SCI}, {121, "base binaire", CALC_SCI},
    {127, "degrés", CALC_SCI}, {128, "radians", CALC_SCI},
    {129, "grades", CALC_SCI},
    {125, "fonctions inverses", CALC_SCI | CALC_DEC},
    {126, "fonctions hyperboliques", CALC_SCI | CALC_DEC}
};
#define CALC_BUTTONS (sizeof(calc_buttons) / sizeof(calc_buttons[0]))

static HWND calc_child(int id)
{
    HWND c;
    for (c = GetWindow(GetActiveWindow(), GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (GetWindowWord(c, GWW_ID) == id && IsWindowVisible(c))
            return c;
    return NULL;
}

static BOOL calc_scientific(void) { return calc_child(413) != NULL; }
static BOOL calc_checked(int id)
{
    HWND c = calc_child(id);
    return c && SendMessage(c, BM_GETCHECK, 0, 0L) != 0;
}

static int calc_base(void)
{
    if (!calc_scientific()) return 10;
    if (calc_checked(124)) return 16;
    if (calc_checked(122)) return 8;
    if (calc_checked(121)) return 2;
    return 10;
}

static BOOL calc_available(int i)
{
    const CALCBUTTON *b = &calc_buttons[i];
    int base = calc_base();
    if (!(b->modes & (calc_scientific() ? CALC_SCI : CALC_STD))) return FALSE;
    if ((b->modes & CALC_DEC) && base != 10) return FALSE;
    if ((b->modes & CALC_HEX) && base != 16) return FALSE;
    /* Inv change aussi le sens des puissances/logarithmes : ne pas proposer
       ces fonctions sous un nom devenu faux. Les trigonométriques sont nommées. */
    if (calc_scientific() && calc_checked(125) && b->id >= 95 && b->id <= 110 &&
        (b->id < 98 || b->id > 100)) return FALSE;
    if (b->id >= 48 && b->id <= 57 && (int) b->id - 48 >= base) return FALSE;
    return TRUE;
}

static const char *calc_name(int i)
{
    int id = calc_buttons[i].id;
    if (id >= 98 && id <= 100) {
        static const char *const trig[4][3] = {
            { "sinus", "cosinus", "tangente" },
            { "arc sinus", "arc cosinus", "arc tangente" },
            { "sinus hyperbolique", "cosinus hyperbolique", "tangente hyperbolique" },
            { "arc sinus hyperbolique", "arc cosinus hyperbolique", "arc tangente hyperbolique" }
        };
        return trig[(calc_checked(125) ? 1 : 0) + (calc_checked(126) ? 2 : 0)][id - 98];
    }
    if (id >= 127 && id <= 129 && calc_base() != 10) {
        static const char *const sizes[] = { "double mot 32 bits", "mot 16 bits", "octet 8 bits" };
        return sizes[id - 127];
    }
    return calc_buttons[i].name;
}

/* Même libellé long pour les véritables radios/cases parcourus par Tab. */
static void calc_control_name(HWND h, char *out)
{
    int i, id = GetWindowWord(h, GWW_ID);
    if (!is_class(GetParent(h), "SCICALC") || GetParent(h) != GetActiveWindow()) return;
    for (i = 0; i < CALC_BUTTONS; i++)
        if (calc_buttons[i].id == id && calc_available(i)) {
            lstrcpy(out, calc_name(i));
            return;
        }
}

static void calc_states(char parts[CALC_PARTS][64])
{
    BOOL sci = calc_scientific();
    HWND m = calc_child(sci ? 400 : 402);
    char text[32];
    int i, id;
    for (i = 0; i < CALC_PARTS; i++) parts[i][0] = 0;
    lstrcpy(parts[0], sci ? "mode scientifique" : "mode standard");
    text[0] = 0;
    if (m) GetWindowText(m, text, sizeof(text));
    lstrcpy(parts[1], _fstrchr(text, 'M') ? "mémoire occupée" : "mémoire vide");
    if (!sci) return;
    lstrcpy(parts[2], calc_base() == 16 ? "base hexadécimale" :
                        calc_base() == 8 ? "base octale" :
                        calc_base() == 2 ? "base binaire" : "base décimale");
    for (id = 127; id <= 129; id++)
        if (calc_checked(id))
            for (i = 0; i < CALC_BUTTONS; i++)
                if (calc_buttons[i].id == id) lstrcpy(parts[3], calc_name(i));
    lstrcpy(parts[4], calc_checked(125) ? "fonctions inverses activées" : "fonctions inverses désactivées");
    lstrcpy(parts[5], calc_checked(126) ? "fonctions hyperboliques activées" : "fonctions hyperboliques désactivées");
}

static void calc_summary(char *out)
{
    char parts[CALC_PARTS][64];
    int i;
    calc_states(parts);
    out[0] = 0;
    for (i = 0; i < CALC_PARTS; i++) append(out, parts[i]);
}

static BOOL calc_value(char *out)
{
    HWND h = GetActiveWindow(), c;
    int id, n;
    out[0] = 0;
    if (!is_class(h, "SCICALC"))
        return FALSE;
    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        id = GetWindowWord(c, GWW_ID);
        if ((id != 413 && id != 414) || !is_class(c, "STATIC") ||
            !IsWindowVisible(c))
            continue;
        GetWindowText(c, out, TXT);
        while (out[0] == ' ')
            _fmemmove(out, out + 1, lstrlen(out));
        n = lstrlen(out);
        while (n && out[n - 1] == ' ')
            out[--n] = 0;
        return n != 0;
    }
    return FALSE;
}

/* Les signes d'un nombre doivent rester audibles même si la ponctuation est
   désactivée. La virgule finale de l'afficheur signifie un entier. */
static void calc_say(const char *value, BOOL cut)
{
    char out[TXT * 2];
    int n = 0;
    const char *w;
    for (; *value && n < sizeof(out) - 16; value++) {
        w = NULL;
        if (*value == '-') w = " moins ";
        else if (*value == '+') w = " plus ";
        else if (*value == ',' || *value == '.') {
            if (!value[1]) continue;
            w = " virgule ";
        }
        if (w) {
            lstrcpy(out + n, w);
            n += lstrlen(w);
        } else out[n++] = *value;
    }
    out[n] = 0;
    say(out, cut);
}

static void calc_poll(void)
{
    char value[TXT], parts[CALC_PARTS][64], out[TXT * 2];
    HWND act = GetActiveWindow();
    BOOL first = calc_owner != act, changed;
    int i;
    if (!calc_value(value)) {
        calc_owner = NULL;
        calc_last[0] = 0;
        calc_force = calc_queue = FALSE;
        calc_view = 0;
        calc_button = -1;
        return;
    }
    if (first) {
        calc_view = 0;
        calc_button = -1;
    }
    changed = first || calc_force || lstrcmp(value, calc_last);
    if (changed) {
        calc_view = 0;
        calc_say(value, !first && !calc_queue);
        lstrcpyn(calc_last, value, sizeof(calc_last));
    }
    calc_states(parts);
    out[0] = 0;
    for (i = 0; i < CALC_PARTS; i++) {
        if (first || lstrcmp(parts[i], calc_previous[i])) {
            /* Les deux options désactivées sont dans le bilan, pas dans l'annonce
               initiale du mode scientifique. Leur désactivation reste annoncée. */
            if (i < 4 || (!first && calc_previous[i][0]) || calc_checked(i == 4 ? 125 : 126))
                append(out, parts[i]);
            lstrcpyn(calc_previous[i], parts[i], sizeof(calc_previous[i]));
        }
    }
    if (out[0]) say(out, !changed && !calc_queue);
    calc_owner = act;
    calc_force = calc_queue = FALSE;
}

/* Appelé seulement pour une commande Inser, jamais depuis le crochet de l'appli.
   SendMessage laisse CALC.EXE faire son calcul avant la prochaine scrutation. */
static BOOL calc_command(WPARAM vk)
{
    char out[TXT * 2], value[TXT];
    int i, direction;
    if (in_menu || !calc_value(value)) return FALSE;
    if (calc_view == 1 && (calc_button < 0 || !calc_available(calc_button))) {
        calc_view = 0;
        calc_button = -1;
    }
    if (vk == VK_NEXT) {
        calc_view = 2;
        calc_summary(out);
        say(out, TRUE);
        return TRUE;
    }
    if (vk == VK_UP) {
        calc_view = 0;
        calc_say(value, TRUE);
        return TRUE;
    }
    if (vk == VK_RETURN) {
        if (calc_view != 1 || calc_button < 0 || !calc_available(calc_button)) {
            say("choisir un bouton avec Inser gauche ou droite", TRUE);
            return TRUE;
        }
        say(calc_name(calc_button), TRUE);
        SendMessage(GetActiveWindow(), WM_COMMAND, calc_buttons[calc_button].id, 0L);
        calc_view = 0;
        calc_force = calc_queue = TRUE;
        return TRUE;
    }
    if (vk != VK_LEFT && vk != VK_RIGHT && vk != VK_HOME && vk != VK_END &&
        !(vk == VK_TAB && calc_view == 1)) return FALSE;
    direction = (vk == VK_LEFT || vk == VK_END) ? -1 : 1;
    if (vk == VK_HOME || vk == VK_END) calc_button = -1;
    if (vk != VK_TAB) {
        i = calc_button;
        do {
            if (i < 0) i = direction > 0 ? 0 : CALC_BUTTONS - 1;
            else i = (i + direction + CALC_BUTTONS) % CALC_BUTTONS;
        } while (!calc_available(i));
        calc_button = i;
    }
    calc_view = 1;
    lstrcpy(out, calc_name(calc_button));
    append(out, "bouton");
    say(out, TRUE);
    return TRUE;
}

/* ------------------------------------------------- description d'un contrôle */

/* Élément i d'une liste dessinée par le programme : le texte affiché à sa place */
static BOOL osm_item(HWND h, int i, char *out)
{
    RECT r;
    POINT a, b;
    if (SendMessage(h, LB_GETITEMRECT, i, (LPARAM) (LPRECT) &r) == LB_ERR)
        return FALSE;
    a.x = r.left; a.y = r.top;
    b.x = r.right; b.y = r.bottom;
    ClientToScreen(h, &a);
    ClientToScreen(h, &b);
    return EpOsmRect(h, a.x, a.y, b.x, b.y, out, TXT) > 0;
}

static BOOL item_unknown;        /* describe_value() n'a trouvé que « élément n » */

/* Valeur seule (ce qui change pendant qu'on manipule le contrôle) */
static void describe_value(HWND h, char *out)
{
    char cls[32], buf[TXT];
    long style = GetWindowLong(h, GWL_STYLE);
    int i;
    long length;

    out[0] = 0;
    class_of(h, cls, sizeof(cls));
    if (lstrcmp(cls, "BUTTON") == 0) {
        int t = (int) (style & 0x0F);
        if (t == BS_CHECKBOX || t == BS_AUTOCHECKBOX || t == BS_3STATE || t == BS_AUTO3STATE) {
            i = (int) SendMessage(h, BM_GETCHECK, 0, 0L);
            lstrcpy(out, i == 1 ? "coché" : i == 2 ? "indéterminé" : "non coché");
        } else if (t == BS_RADIOBUTTON || t == BS_AUTORADIOBUTTON) {
            i = (int) SendMessage(h, BM_GETCHECK, 0, 0L);
            lstrcpy(out, i ? "sélectionné" : "non sélectionné");
        }
    } else if (lstrcmp(cls, "EDIT") == 0) {
        if (style & ES_PASSWORD)
            lstrcpy(out, "mot de passe");
        else if (is_multiline(h))
            edit_line(h, out, TXT);
        else
            GetWindowText(h, out, TXT);
        if (!out[0] && !is_multiline(h))
            EpOsmWindow(h, out, TXT);   /* (Bureau : « Délai », texte non rendu) */
        if (out[0] && !out[1] && sign_name(out[0]))
            lstrcpy(out, sign_name(out[0]));    /* « ; » : séparateur de listes */
        if (!out[0])
            lstrcpy(out, "vide");
    } else if (lstrcmp(cls, "LISTBOX") == 0) {
        if (SendMessage(h, LB_GETCOUNT, 0, 0L) <= 0) {
            lstrcpy(out, "vide");       /* (sinon « élément 1 » : curseur à 0) */
            return;
        }
        i = (int) SendMessage(h, LB_GETCURSEL, 0, 0L);
        if (i < 0)
            i = (int) SendMessage(h, LB_GETCARETINDEX, 0, 0L);
        if (i >= 0 && (!(style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) ||
                       (style & LBS_HASSTRINGS))
            && (length = SendMessage(h, LB_GETTEXTLEN, i, 0L)) >= 0 && length < TXT) {
            SendMessage(h, LB_GETTEXT, i, (LPARAM) (LPSTR) buf);
            lstrcpy(out, buf);
            if ((style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) &&
                SendMessage(h, LB_GETSEL, i, 0L) > 0)
                append_keep(out, "sélectionné", TXT);
        } else if (i < 0)
            lstrcpy(out, "aucun élément");
        else if (!osm_item(h, i, out)) {
            wsprintf(out, "élément %d", i + 1);
            item_unknown = TRUE;        /* pas encore dessiné : relu par TIMER_LB */
        }
    } else if (lstrcmp(cls, "COMBOBOX") == 0) {
        /* liste dessinée par le programme : ce qu'elle affiche (le texte de l'élément
           porte parfois des données : « Standard Microsoft=C0C0C0,FFFFFF... ») */
        if ((style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) &&
            EpOsmWindow(h, out, TXT) > 0)
            return;
        i = (int) SendMessage(h, CB_GETCURSEL, 0, 0L);
        if (i >= 0 && (length = SendMessage(h, CB_GETLBTEXTLEN, i, 0L)) >= 0 && length < TXT)
            SendMessage(h, CB_GETLBTEXT, i, (LPARAM) (LPSTR) out);
        else
            GetWindowText(h, out, TXT);
    } else if (lstrcmp(cls, "SCROLLBAR") == 0) {
        int lo, hi, pos = GetScrollPos(h, SB_CTL);
        GetScrollRange(h, SB_CTL, &lo, &hi);
        if (hi > lo)                    /* graduation propre au programme : en pour cent */
            wsprintf(out, "%d %%", (int) ((long) (pos - lo) * 100 / (hi - lo)));
        else
            wsprintf(out, "%d", pos);
    }
}

/* Boutons dessinés de l'Enregistreur de sons et du Diffuseur de médias (classe
   SBUTTON) : leur texte est un nom interne anglais précédé de « # ». */
static void media_button_name(char *name)
{
    static const char *names[] = {
        "#Rewind", "Retour rapide", "#Forward", "Avance rapide", "#Play", "Lecture",
        "#Stop", "Arrêt", "#Record", "Enregistrement", "#Pause", "Pause",
        "#Eject", "Éjecter", NULL
    };
    int i;
    if (name[0] != '#')
        return;
    for (i = 0; names[i]; i += 2)
        if (lstrcmpi(name, names[i]) == 0) {
            lstrcpy(name, names[i + 1]);
            return;
        }
    lstrcpy(name, name + 1);
}

/* Texte d'un contrôle enfant de la fenêtre active, d'après son identifiant */
static BOOL child_text(HWND parent, int id, char *out, int n)
{
    HWND c = GetDlgItem(parent, id);
    out[0] = 0;
    if (c)
        GetWindowText(c, out, n);
    return out[0] != 0;
}

static HWND top_level(HWND h);

/* Ligne de la barre d'état dessinée en bas de la fenêtre top (vide sinon) */
static BOOL status_line(HWND top, char *out, int n)
{
    RECT rc;
    POINT pt;
    int col;
    out[0] = 0;
    if (!top)
        return FALSE;
    GetWindowRect(top, &rc);
    pt.x = rc.left + 4;
    pt.y = rc.bottom - 6;
    if (!EpOsmReviewLine(top, &pt, 2, out, n, &col) || rc.bottom - pt.y >= 32)
        out[0] = 0;
    return out[0] != 0;
}

/* Nom d'un caractère dit seul : signe nommé, « majuscule » selon le réglage */
static void char_name(char c, char *out)
{
    const char *name = sign_name(c);
    if (name)
        lstrcpy(out, name);
    else if (cfg[CFG_CAPS] && IsCharUpper(c))
        wsprintf(out, "majuscule %c", c);
    else {
        out[0] = c;
        out[1] = 0;
    }
}

/* Table de caractères : le caractère choisi dans la grille, d'après la barre d'état
   (« Combinaison:! », « Combinaison:Alt+0233 », « Combinaison:Espace »). */
static BOOL charmap_value(HWND h, char *out)
{
    char line[TXT * 2], *p, *k;
    int code;
    if (!status_line(top_level(h), line, sizeof(line)) ||
        (p = _fstrstr(line, "Combinaison")) == NULL || (p = _fstrchr(p, ':')) == NULL)
        return FALSE;
    for (p++; *p == ' '; p++)
        ;
    k = p;
    if (_fstrncmp(k, "Alt+", 4) == 0) {
        for (code = 0, k += 4; *k >= '0' && *k <= '9'; k++)
            code = code * 10 + *k - '0';
        char_name((char) code, out);
        append(out, p);
    } else if (lstrlen(k) == 1 || (lstrlen(k) == 5 && _fstrncmp(k, "Maj+", 4) == 0)) {
        char_name(k[lstrlen(k) - 1], out);
    } else if (*k)
        lstrcpyn(out, k, TXT);      /* Espace */
    else
        return FALSE;
    return TRUE;
}

/* Fiches (Cardfile), mode Fiche : ligne de titre de la fiche de devant, dessinée
   dans la fenêtre CARD (205) juste au-dessus de sa zone de texte (200). */
static HWND card_edit(HWND app)
{
    HWND card = GetDlgItem(app, 205);
    return card ? GetDlgItem(card, 200) : (HWND) 0;
}

static BOOL card_title(HWND app, char *out, int n)
{
    HWND card = GetDlgItem(app, 205), edit = card_edit(app);
    RECT r;
    out[0] = 0;
    if (!is_class(app, "CARDFILE") || !card || !edit || !IsWindowVisible(edit))
        return FALSE;
    GetWindowRect(edit, &r);
    if (!EpOsmRect(card, r.left, r.top - 20, r.right, r.top, out, n))
        EpOsmRect(app, r.left, r.top - 20, r.right, r.top, out, n);
    if (!out[0])
        lstrcpyn(out, "sans titre", n);
    return TRUE;
}

/* Heure de l'horloge du PC, pour l'Horloge de Windows (aiguilles dessinées) */
static void clock_value(char *out)
{
    int h, m;
#ifdef EP_NATIVE_TEST
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    h = tm->tm_hour;
    m = tm->tm_min;
#else
    struct dostime_t t;
    _dos_gettime(&t);
    h = t.hour;
    m = t.minute;
#endif
    wsprintf(out, "%d heure%s %02d", h, h > 1 ? "s" : "", m);
}

/* Aide de Windows (WinHelp) : le focus reste sur MS_WINDOC ; le texte est dessiné dans
   la rubrique MS_WINTOPIC, qui défile, sous la zone fixe MS_WINNSR (titre de la
   rubrique dans certains fichiers d'aide). Pas de curseur : Tab choisit un lien en
   l'inversant. */
static BOOL is_help(HWND h)
{
    return is_class(h, "MS_WINDOC") || is_class(h, "MS_WINTOPIC_SECONDARY");
}

static HWND help_part(HWND doc, const char *cls)
{
    HWND c;
    for (c = GetWindow(doc, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (is_class(c, cls) && IsWindowVisible(c))
            return c;
    return NULL;
}

/* Première rangée du texte (zone fixe comprise), ou dernière visible de la rubrique ;
   *y : son ordonnée à l'écran */
static BOOL help_row(HWND doc, BOOL last, char *out, int n, int *y)
{
    HWND nsr = help_part(doc, "MS_WINNSR"), topic = help_part(doc, "MS_WINTOPIC");
    RECT r;
    POINT pt;
    int col;
    out[0] = 0;
    if (!topic)
        return FALSE;
    GetWindowRect(last || !nsr ? topic : nsr, &r);
    pt.x = r.left + 2;
    pt.y = last ? r.bottom : r.top - 1;
    if (!EpOsmReviewLine(doc, &pt, last ? -1 : 1, out, n, &col))
        return FALSE;
    if (y)
        *y = pt.y;
    return TRUE;
}

/* Gestionnaire de liaisons (Packager) : le volet actif a son sous-titre
   (« --Contenu-- ») en couleurs de sélection ; son contenu est sous lui. */
static BOOL packager_value(HWND app, char *out)
{
    HWND c, p;
    char t[TXT], v[TXT];
    RECT r, pr;
    int i;
    for (c = GetWindow(app, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!is_class(c, "SUBTITLECLASS") || !EpOsmHilite(c, t, sizeof(t)))
            continue;
        GetWindowText(c, t, sizeof(t));
        for (i = 0; t[i] == '-'; i++)
            ;
        lstrcpy(out, t + i);
        for (i = lstrlen(out); i > 0 && out[i - 1] == '-'; i--)
            out[i - 1] = 0;
        append(out, "volet");
        GetWindowRect(c, &r);
        for (p = GetWindow(app, GW_CHILD); p; p = GetWindow(p, GW_HWNDNEXT)) {
            GetWindowRect(p, &pr);
            if (is_class(p, "PANECLASS") && pr.left <= r.left + 4 && pr.right >= r.right - 4) {
                append(out, EpOsmWindow(p, v, sizeof(v)) ? v : "vide");
                break;
            }
        }
        return TRUE;
    }
    return FALSE;
}

/* Contrôle dessiné reconnu : sa valeur (rôle dans *role). FALSE : contrôle ordinaire. */
static BOOL custom_value(HWND h, char *out, const char **role)
{
    out[0] = 0;
    if (is_help(h)) {
        /* lien choisi par Tab : le texte inversé */
        *role = NULL;
        if (EpOsmSelection(h, out, TXT - 8))
            lstrcat(out, ", lien");
        return TRUE;
    }
    if (is_class(h, "APPCLASS") && GetWindow(h, GW_CHILD) &&
        is_class(GetWindow(h, GW_CHILD), "PANECLASS")) {
        *role = NULL;
        if (!packager_value(h, out))
            out[0] = 0;
        return TRUE;
    }
    if (is_class(h, "CHARGRIDWCLASS")) {
        *role = "grille de caractères";
        if (!charmap_value(h, out))
            lstrcpy(out, "aucun caractère");
        return TRUE;
    }
    return FALSE;
}

/* Unité écrite après une zone de saisie (« Minutes ») : texte statique suivant, court,
   sans raccourci ni deux-points (ce serait le libellé du contrôle suivant) */
static void edit_unit(HWND h, char *val)
{
    HWND n = GetWindow(h, GW_HWNDNEXT);
    char unit[24];
    if (is_multiline(h) || !n || !is_class(n, "STATIC") || !has_style(n, WS_VISIBLE) ||
        GetWindowTextLength(n) == 0 || GetWindowTextLength(n) >= (int) sizeof(unit))
        return;
    GetWindowText(n, unit, sizeof(unit));
    if (_fstrchr(unit, '&') || _fstrchr(unit, ':') || no_word(unit) ||
        lstrlen(val) + lstrlen(unit) + 2 >= TXT)
        return;
    lstrcat(val, " ");
    lstrcat(val, unit);
}

/* Panneau de configuration, Date/Heure : champs 701 à 703 (heure) et 704 à 706 (date,
   dans l'ordre du format court de WIN.INI, de gauche à droite) */
static BOOL datetime_name(HWND h, char *out)
{
    static const char *hms[] = { "heures", "minutes", "secondes" };
    int id = GetWindowWord(h, GWW_ID), rank = 0, i;
    char fmt[24], *f;
    RECT r, o;
    HWND d = GetParent(h);
    if (id < 701 || id > 706 || !GetDlgItem(d, 711) || !is_class(GetDlgItem(d, 711), "CPARROW"))
        return FALSE;
    if (id <= 703) {
        lstrcpy(out, hms[id - 701]);
        return TRUE;
    }
    GetWindowRect(h, &r);
    for (i = 704; i <= 706; i++)
        if (i != id && GetDlgItem(d, i)) {
            GetWindowRect(GetDlgItem(d, i), &o);
            if (o.left < r.left)
                rank++;
        }
    GetProfileString("intl", "sShortDate", "dd/MM/yyyy", fmt, sizeof(fmt));
    for (f = fmt; *f; f++) {
        if (*f != 'd' && *f != 'M' && *f != 'y')
            continue;
        if (rank-- == 0) {
            lstrcpy(out, *f == 'd' ? "jour" : *f == 'M' ? "mois" : "année");
            return TRUE;
        }
        while (f[1] == *f)
            f++;
    }
    return FALSE;
}

/* Cadre de groupe (BUTTON BS_GROUPBOX) qui entoure le contrôle h */
static BOOL group_of(HWND h, char *out, int n)
{
    HWND c;
    RECT rc, r;
    out[0] = 0;
    GetWindowRect(h, &rc);
    for (c = GetWindow(GetParent(h), GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!is_class(c, "BUTTON") || (GetWindowLong(c, GWL_STYLE) & 0x0F) != BS_GROUPBOX)
            continue;
        GetWindowRect(c, &r);
        if (rc.left >= r.left && rc.right <= r.right && rc.top >= r.top && rc.bottom <= r.bottom) {
            GetWindowText(c, out, n);
            clean_label(out, NULL);
            return out[0] != 0;
        }
    }
    return FALSE;
}

/* Nom + rôle + valeur */
static void describe(HWND h, char *out)
{
    char cls[32], name[TXT], val[TXT];
    const char *role = NULL;
    long style = GetWindowLong(h, GWL_STYLE);

    out[0] = 0;
    class_of(h, cls, sizeof(cls));
    GetWindowText(h, name, sizeof(name));
    clean_label(name, NULL);
    if (lstrcmp(cls, "BUTTON") == 0) calc_control_name(h, name);
    describe_value(h, val);

    if (lstrcmp(cls, "BUTTON") == 0) {
        switch ((int) (style & 0x0F)) {
        case BS_CHECKBOX: case BS_AUTOCHECKBOX: case BS_3STATE: case BS_AUTO3STATE:
            role = "case à cocher"; break;
        case BS_RADIOBUTTON: case BS_AUTORADIOBUTTON:
            role = "bouton radio"; break;
        case BS_GROUPBOX:
            role = "groupe"; break;
        default:
            role = "bouton";
        }
    } else if (lstrcmp(cls, "EDIT") == 0) {
        if ((h != card_edit(top_level(h)) || !card_title(top_level(h), name, sizeof(name))) &&
            !datetime_name(h, name))
            label_before(h, name, sizeof(name));
        role = is_multiline(h) ? "zone de texte" : "zone d'édition";
        edit_unit(h, val);              /* « 2 Minutes », « 75 Pixels » */
    } else if (lstrcmp(cls, "LISTBOX") == 0) {
        label_before(h, name, sizeof(name));
        role = "liste";
    } else if (lstrcmp(cls, "COMBOBOX") == 0) {
        label_before(h, name, sizeof(name));
        role = "liste déroulante";
    } else if (lstrcmp(cls, "SCROLLBAR") == 0) {
        role = "barre de défilement";
        /* son libellé, sinon son cadre (Souris : « Vitesse du double-clic ») */
        if (!label_zorder(h, name, sizeof(name)) && !group_of(h, name, sizeof(name)))
            label_before(h, name, sizeof(name));
        if (is_class(GetParent(h), "SOUNDREC")) {
            /* Enregistreur de sons : la position du son, plutôt que la graduation */
            lstrcpy(name, "Position");
            role = "curseur";
            child_text(GetParent(h), 201, val, sizeof(val));
        }
    } else if (custom_value(h, val, &role)) {
        ;
    } else if (lstrcmp(cls, "SBUTTON") == 0) {
        media_button_name(name);
        role = "bouton";
    } else if (_fstrstr(cls, "BUTTON") && name[0]) {
        role = "bouton";                /* MSW_WN_GRBUTTON de Microsoft Undelete... */
    } else if (lstrcmp(cls, "SSCROLLBAR") == 0) {
        role = "curseur de position";
    } else if (lstrcmp(cls, "MDICLIENT") == 0) {
        name[0] = 0;
    } else if (lstrcmp(cls, "WINDOW") == 0 && (is_class(top_level(h), "SDI") ||
                                               is_class(top_level(h), "MS MAIL APPFRAME"))) {
        /* champ des formulaires de Microsoft Mail : libellé dessiné à sa gauche
           (« Mot de passe: ») ; sans libellé, c'est un bouton (rectangle de focus) */
        RECT rc;
        GetWindowRect(h, &rc);
        if (label_left(GetParent(h), &rc, name, sizeof(name))) {
            clean_label(name, NULL);
            role = "zone d'édition";
            if (!EpOsmWindow(h, val, sizeof(val)))
                lstrcpy(val, "vide");
        } else if (rc.bottom - rc.top > 60 && !EpOsmFocusRect(h, &rc)) {
            role = "zone de texte";     /* texte du message, sans libellé */
            if (!EpOsmWindow(h, val, sizeof(val)))
                lstrcpy(val, "vide");
        }
    } else if (lstrcmp(cls, "OPUSFEDT") == 0) {
        /* zone de saisie des boîtes de dialogue de Word : libellé dessiné à côté */
        RECT rc;
        GetWindowRect(h, &rc);
        label_drawn(GetParent(h), &rc, name, sizeof(name), TRUE);
        role = "zone d'édition";
        EpOsmWindow(h, val, sizeof(val));
        if (!val[0])
            lstrcpy(val, "vide");
    }
    /* Rôle, valeur et état d'abord : seul un libellé trop long est raccourci */
    fit_before(name, TXT * 2, role, val, IsWindowEnabled(h) ? NULL : "grisé");
    append(out, name);
    append(out, role);
    append(out, val);
    if (!IsWindowEnabled(h)) append(out, "grisé");
}

/* -------------------------------------------------------------- scrutation */

static char custom_said[TXT];   /* valeur du contrôle reconnu annoncée en dernier */

static char last_etext[TXT];     /* zone de saisie d'une ligne : texte (flèches de réglage) */

static void remember_value(HWND h)
{
    char cls[32];
    const char *role;
    last_etext[0] = 0;
    if (is_class(h, "EDIT") && !is_multiline(h) && !(GetWindowLong(h, GWL_STYLE) & ES_PASSWORD))
        GetWindowText(h, last_etext, sizeof(last_etext));
    if (!custom_value(h, custom_said, &role))
        custom_said[0] = 0;
    class_of(h, cls, sizeof(cls));
    last_sel = last_line = last_check = -2;
    last_esel = 0;
    if (lstrcmp(cls, "LISTBOX") == 0)
        last_sel = (int) SendMessage(h, LB_GETCURSEL, 0, 0L) * 1000 +
                   (int) SendMessage(h, LB_GETCARETINDEX, 0, 0L);
    else if (lstrcmp(cls, "COMBOBOX") == 0)
        last_sel = (int) SendMessage(h, CB_GETCURSEL, 0, 0L);
    else if (lstrcmp(cls, "BUTTON") == 0)
        last_check = (int) SendMessage(h, BM_GETCHECK, 0, 0L);
    else if (lstrcmp(cls, "EDIT") == 0) {
        last_esel = SendMessage(h, EM_GETSEL, 0, 0L);
        last_line = is_multiline(h) ?
            (int) SendMessage(h, EM_LINEFROMCHAR, (WPARAM) -1, 0L) : 0;
    }
}

/* Texte d'une boîte de dialogue qui vient de s'ouvrir : les textes statiques qui ne
   sont pas le libellé du contrôle suivant (le message d'une boîte « Enregistrer les
   modifications ? », par exemple). */
static void dialog_text(HWND dlg, char *out)
{
    HWND c, n;
    char buf[TXT];

    /* Seulement pour les boîtes de message : dans un formulaire (zones d'édition,
       listes), les textes statiques sont des libellés, dits avec leur contrôle. */
    for (c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (is_class(c, "EDIT") || is_class(c, "LISTBOX") || is_class(c, "COMBOBOX") ||
            is_class(c, "SCROLLBAR"))
            return;                     /* (Souris : « Lente », « Rapide » des curseurs) */
    for (c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!is_class(c, "STATIC") || !has_style(c, WS_VISIBLE) ||
            (GetWindowLong(c, GWL_STYLE) & 0x0F) > SS_RIGHT)
            continue;
        n = GetWindow(c, GW_HWNDNEXT);
        if (n && (is_class(n, "EDIT") || is_class(n, "LISTBOX") || is_class(n, "COMBOBOX")))
            continue;                   /* libellé : dit avec son contrôle */
        GetWindowText(c, buf, sizeof(buf));
        clean_label(buf, NULL);
        append(out, buf);
    }
}

static HWND top_level(HWND h)
{
    while (h && (GetWindowLong(h, GWL_STYLE) & WS_CHILD))
        h = GetParent(h);
    return h;
}

/* Boîte de Word qui vient de s'ouvrir : son texte (message, astuce) sera lu quand
   elle aura fini de se dessiner, avant l'élément qui a le focus. */
static HWND dlg_hwnd;
static HWND dlg_src;            /* fenêtre où ce texte est dessiné, si autre (Mail) */
static BOOL dlg_pending;

static BOOL is_sdm_page(HWND h);

/* Boîte de Word à onglets : ses contrôles sont dans une page enfant (sa_sdm_…) */
static BOOL has_page(HWND h)
{
    HWND c;
    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (is_sdm_page(c))
            return TRUE;
    return FALSE;
}

static BOOL has_child_class(HWND h, const char *cls)
{
    HWND c;
    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (is_class(c, cls))
            return TRUE;
    return FALSE;
}

static HWND grp_dlg;            /* cadre de groupe annoncé, et sa boîte */
static char grp_said[TXT];
static BOOL group_of(HWND h, char *out, int n);

static HWND status_hwnd;        /* voir status_poll() */
static char status_said[TXT];

/* Fenêtre de document MDI qui contient h (Gestionnaire de fichiers, Sysedit) */
static HWND last_mdi;

static HWND mdi_child(HWND h)
{
    HWND p;
    for (; h; h = p) {
        p = GetParent(h);
        if (!p || !(GetWindowLong(h, GWL_STYLE) & WS_CHILD))
            return NULL;
        if (is_class(p, "MDICLIENT"))
            return h;
    }
    return NULL;
}

static void announce_focus(BOOL cut)
{
    char out[TXT * 2], title[TXT];
    HWND act = GetActiveWindow(), foc = GetFocus();

    /* Focus posé dans une fenêtre pas encore active (boîte de dialogue en cours de
       création) : attendre son activation, on la lira alors avec son titre. */
    if ((foc && act && top_level(foc) != act) || (act && !IsWindowVisible(act)))
        return;
    out[0] = 0;
    if (act && (act != last_active || new_active)) {
        new_active = FALSE;
        GetWindowText(act, title, sizeof(title));
        append(out, title);
        if (is_class(act, "#32770") || has_child_class(act, "STATIC"))
            dialog_text(act, out);      /* boîte de dialogue, même d'une autre classe */
        else if (is_class(act, "SDI") && GetWindow(act, GW_CHILD)) {
            /* boîte de Microsoft Mail : texte dessiné dans sa fenêtre fille */
            dlg_hwnd = act;
            dlg_src = GetWindow(act, GW_CHILD);
            dlg_pending = TRUE;
            SetTimer(hwnd_main, TIMER_DLG, 350, NULL);
        } else if (is_class(act, "MS_WINNOTE")) {
            /* définition affichée par l'Aide : lue quand elle est dessinée */
            dlg_hwnd = act;
            dlg_pending = TRUE;
            SetTimer(hwnd_main, TIMER_DLG, 350, NULL);
        } else if (is_class(act, "CLOCK")) {
            char t[32];
            clock_value(t);
            append(out, t);
        }
        else if (is_sdm(act) && !has_child_class(act, "OPUSFEDT") && !has_page(act)) {
            /* boîte de Word sans zone de saisie (message, astuce) */
            dlg_hwnd = act;
            dlg_pending = TRUE;
            SetTimer(hwnd_main, TIMER_DLG, 350, NULL);
        }
    }
    if (foc && foc != act) {
        char d[TXT * 2];
        HWND m = mdi_child(foc);
        /* Autre fenêtre de document (Ctrl+F6) : son titre, puis le focus */
        if (m && m != last_mdi && m != foc && !IsZoomed(m)) {
            GetWindowText(m, title, sizeof(title));
            append(out, title);
        }
        last_mdi = m;
        item_unknown = FALSE;
        describe(foc, d);
        {
            /* entrée dans un autre cadre de groupe : son nom d'abord (les quatre
               boutons « Modifier » d'International) */
            char g[TXT];
            if (act != grp_dlg)
                grp_said[0] = 0;
            grp_dlg = act;
            if (!is_standard(foc) || !group_of(foc, g, sizeof(g)))
                g[0] = 0;
            if (lstrcmp(g, grp_said) != 0) {
                lstrcpy(grp_said, g);
                if (g[0] && _fstrncmp(d, g, lstrlen(g)) != 0)
                    append(out, g);
            }
        }
        if (item_unknown)               /* fenêtre qui vient de s'ouvrir */
            SetTimer(hwnd_main, TIMER_LB, 400, NULL);
        if (foc == card_edit(act)) {
            /* fiche ajoutée : son titre peut être dessiné après cette annonce ;
               status_poll() dira le bon s'il diffère */
            card_title(act, status_said, sizeof(status_said));
            status_hwnd = act;
        }
        append(out, d);
    } else if (!out[0] && act && !is_sdm(act)) {
        /* (boîte de Word : l'élément qui a le focus est annoncé par son rectangle) */
        GetWindowText(act, title, sizeof(title));
        append(out, title);
    }
    last_active = act;
    last_focus = foc;
    typed_n = 0;                        /* mot tapé ailleurs : oublié */
    line_key = FALSE;
    sel_key = FALSE;
    sel_said[0] = 0;
    if (foc)
        caret_screen(foc, &last_caret);
    if (foc)
        remember_value(foc);
    if (!out[0])
        return;
    if (GetTickCount() - focus_tick < 700 && lstrlen(out) <= lstrlen(focus_said) &&
        lstrcmp(focus_said + lstrlen(focus_said) - lstrlen(out), out) == 0)
        return;     /* déjà dit à l'instant, éventuellement avec le titre (scrutation + crochet) */
    lstrcpyn(focus_said, out, sizeof(focus_said));
    focus_tick = GetTickCount();
    say(out, cut);
}

/* Changement de valeur dans le contrôle qui a le focus (liste, case, curseur...) */
static void check_value(void)
{
    HWND h = last_focus;
    char cls[32], out[TXT];
    int sel = last_sel, line = last_line, check = last_check;
    DWORD esel = last_esel;
    static char etext[TXT];

    if (!h || !IsWindow(h) || is_sdm(h))
        return;             /* (boîte de Word : suivie par son rectangle de focus) */
    {
        const char *role;
        if (is_help(h) && line_key) {
            /* défilement : la ligne apparue (en bas avec Bas, sinon en haut) */
            if (GetTickCount() - line_key_tick < 300)
                return;
            line_key = FALSE;
            help_row(h, last_vk == VK_DOWN, out, TXT, NULL);
            say(out[0] ? out : "vide", TRUE);
            custom_value(h, custom_said, &role);
            return;
        }
        if (custom_value(h, out, &role)) {
            if (lstrcmp(out, custom_said) != 0) {
                lstrcpy(custom_said, out);
                if (out[0])
                    say(out, TRUE);
            }
            return;
        }
    }
    if (!is_standard(h)) {
        /* fenêtre dessinée par le programme : suivre le curseur texte dans le modèle
           hors écran (ligne si on change de ligne, sinon caractère) */
        POINT pt;
        if (caret_settle) {
            /* curseur ramené par la fin d'un dire tout : attendre qu'il soit posé */
            if (GetTickCount() - caret_settle < 500)
                return;
            caret_settle = 0;
            if (caret_quiet) {
                caret_quiet = FALSE;
                caret_screen(h, &last_caret);
                line_key = word_key = typed = FALSE;
                return;
            }
        }
        if (!EpOsmCaret(h, &pt) && EpOsmHilite(h, out, TXT)) {
            /* liste dessinée sans curseur texte (icônes du Gestionnaire de
               programmes) : on_hilite annonce l'élément ; la ligne sous le curseur
               système, absent, serait lue « vide ». */
            line_key = word_key = typed = FALSE;
            return;
        }
        if (sel_key) {
            /* Maj + déplacement : laisser le programme inverser la sélection, puis
               dire ce qui change. Sans sélection visible, lecture ordinaire. */
            static char cur[TXT * 2];
            if (GetTickCount() - sel_key_tick < 300)
                return;
            sel_key = FALSE;
            EpOsmSelection(h, cur, sizeof(cur));
            if (cur[0] || sel_said[0]) {
                drawn_selection(sel_said, cur);
                lstrcpyn(sel_said, cur, sizeof(sel_said));
                caret_screen(h, &last_caret);
                line_key = word_key = typed = FALSE;
                return;
            }
        }
        caret_screen(h, &pt);
        if (pt.x == last_caret.x && pt.y == last_caret.y) {
            /* Navigation verticale : laisser au programme le temps de redessiner son
               curseur (Word) ; s'il reste en place, c'est le texte qui a défilé. */
            if (!line_key || GetTickCount() - line_key_tick < 400)
                return;
        }
        if (typed)
            typed = FALSE;
        else if (pt.y != last_caret.y || line_key) {
            EpOsmLine(h, pt.y + 2, out, TXT);
            say(out[0] ? out : "vide", TRUE);
        } else if (word_key) {
            char row[TXT * 2];
            POINT q;
            int col;
            q.x = pt.x + 1;
            q.y = pt.y + 2;
            if (EpOsmReviewLine(h, &q, 0, row, sizeof(row), &col))
                say_word(row, col);
        } else {
            int c = EpOsmCharAt(h, pt.x + 1, pt.y + 2);
            if (c == 0)
                say("fin de ligne", TRUE);
            else
                say_char((char) c, TRUE);
        }
        last_caret = pt;
        line_key = FALSE;
        word_key = FALSE;
        return;
    }
    lstrcpy(etext, last_etext);
    remember_value(h);
    class_of(h, cls, sizeof(cls));

    if (lstrcmp(cls, "EDIT") == 0) {
        if (!is_multiline(h) && lstrcmp(etext, last_etext) != 0 &&
            (last_vk == VK_UP || last_vk == VK_DOWN || last_vk == VK_PRIOR || last_vk == VK_NEXT) &&
            !(GetWindowLong(h, GWL_STYLE) & ES_PASSWORD)) {
            /* valeur changée sans frappe : flèches de réglage (Bureau, Date/Heure) */
            describe_value(h, out);
            edit_unit(h, out);
            say(out, TRUE);
            return;
        }
        if (typed) {                    /* le caractère tapé a déjà été dit */
            typed = FALSE;
            return;
        }
        if (GetWindowLong(h, GWL_STYLE) & ES_PASSWORD)
            return;
        if (last_esel != esel && (sel_key || LOWORD(last_esel) != HIWORD(last_esel))) {
            BOOL shift = sel_key;
            sel_key = word_key = FALSE;
            if (edit_selection(h, esel, last_esel, shift))
                return;
        }
        if (last_line != line) {
            edit_line(h, out, TXT);
            say(out[0] ? out : "vide", TRUE);
        } else if (last_esel != esel) {
            /* déplacement dans la ligne : caractère sous le curseur */
            WORD pos = LOWORD(last_esel);
            int start = 0, col;
            if (is_multiline(h)) {
                start = (int) SendMessage(h, EM_LINEINDEX, (WPARAM) -1, 0L);
                edit_line(h, out, TXT);
            } else
                GetWindowText(h, out, TXT);
            col = pos - start;
            if (word_key && col >= 0) {
                word_key = FALSE;
                say_word(out, col);
            } else if (col >= 0 && col < lstrlen(out))
                say_char(out[col], TRUE);
            else
                say("fin de ligne", TRUE);
        }
    } else if (last_sel != sel || last_check != check) {
        describe_value(h, out);
        if (out[0])
            say(out, TRUE);
    }
}

static BOOL sa_active(void);

/* État affiché par un programme hors du contrôle qui a le focus, annoncé quand il
   change : « Lecture », « Arrêté » de l'Enregistreur de sons ; titre de la fiche. */

static void status_poll(void)
{
    HWND act = GetActiveWindow();
    char buf[TXT], line[TXT];

    if (is_class(act, "SOUNDREC") && child_text(act, 200, buf, sizeof(buf))) {
        if (act == status_hwnd && lstrcmp(buf, status_said) != 0)
            say(buf, FALSE);
    } else if (is_help(act) && GetFocus() == act && help_row(act, FALSE, buf, sizeof(buf), NULL)) {
        /* autre rubrique (lien suivi, Index, Précédent) : sa première ligne. Pas
           pour un défilement (touche de déplacement) ni pendant le dire tout. */
        if (lstrcmp(buf, status_said) != 0 && !sa_active() &&
            (act != status_hwnd || !(last_vk >= VK_PRIOR && last_vk <= VK_DOWN) ||
             GetTickCount() - line_key_tick > 1500))
            say(buf, act == status_hwnd);
    } else if (GetFocus() && GetFocus() == card_edit(act) && card_title(act, buf, sizeof(buf))) {
        /* autre fiche (Page suivante, Rechercher) : son titre et sa ligne courante */
        if (act == status_hwnd && lstrcmp(buf, status_said) != 0) {
            lstrcpy(line, buf);
            edit_line(GetFocus(), buf, sizeof(buf));
            append(line, buf[0] ? buf : "vide");
            say(line, TRUE);
            remember_value(GetFocus());
            card_title(act, buf, sizeof(buf));
        }
    } else {
        status_hwnd = NULL;
        return;
    }
    status_hwnd = act;
    lstrcpy(status_said, buf);
}

/* Zones dont le texte arrive de lui-même et doit être lu dès qu'il est affiché : données
   reçues par Terminal (DC_TERM), écrans du Didacticiel (TUTCLIENT). On compare les
   rangées visibles (empreintes) une fois l'affichage stable ; les nouvelles sont dites. */
#define AUTO_ROWS 40
static HWND auto_hwnd;
static DWORD auto_sig, auto_tick;
static DWORD auto_rows[AUTO_ROWS];
static int auto_n;
static DWORD key_tick;          /* dernière touche transmise au programme */

static HWND auto_scope(HWND act)
{
    HWND c;
    for (c = GetWindow(act, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if ((is_class(c, "DC_TERM") || is_class(c, "TUTCLIENT")) && IsWindowVisible(c))
            return c;
    return NULL;
}

static DWORD row_hash(const char *s)
{
    DWORD h = 5381;
    for (; *s; s++)
        h = h * 33 + (BYTE) *s;
    return h;
}

static void auto_poll(void)
{
    HWND h = auto_scope(GetActiveWindow());
    static char row[TXT * 2], said[1024];
    DWORD rows[AUTO_ROWS], sig;
    RECT r;
    POINT pt;
    int n = 0, i, k, col;
    BOOL first = h != auto_hwnd, term = is_class(h, "DC_TERM");

    if (!h) {
        auto_hwnd = NULL;
        return;
    }
    sig = EpOsmSignature(h);
    if (first || sig != auto_sig) {     /* attendre la fin du dessin */
        auto_sig = sig;
        auto_tick = GetTickCount();
        if (!first)
            return;
    } else if (auto_tick == 0 || GetTickCount() - auto_tick < 400)
        return;
    auto_tick = 0;
    if (!term) {
        /* Didacticiel : la colonne de texte principale (pas la fausse fenêtre de
           démonstration à côté), quand elle change */
        auto_hwnd = h;
        if (EpOsmMainText(h, said, sizeof(said)) && row_hash(said) != auto_rows[0])
            say(said, FALSE);
        auto_rows[0] = row_hash(said);
        return;
    }
    GetWindowRect(h, &r);
    pt.x = r.left + 2;
    pt.y = r.top - 1;
    said[0] = 0;
    while (n < AUTO_ROWS && EpOsmReviewLine(h, &pt, 1, row, sizeof(row), &col) &&
           pt.y < r.bottom) {
        rows[n] = row_hash(row);
        for (i = 0, k = 0; i < auto_n && !k; i++)
            k = auto_rows[i] == rows[n];
        if (!k && (!first || !term) && lstrlen(said) + lstrlen(row) + 2 < (int) sizeof(said)) {
            if (said[0])
                lstrcat(said, " ");     /* (lignes d'un même paragraphe : pas de virgule) */
            lstrcat(said, row);
        }
        n++;
    }
    memcpy(auto_rows, rows, n * sizeof(DWORD));
    auto_n = n;
    auto_hwnd = h;
    /* écho de la frappe (Terminal) : déjà dit caractère par caractère */
    if (said[0] && (!term || GetTickCount() - key_tick > 700))
        say(said, FALSE);
}

static void poll(void)
{
    com_read();                         /* marques en retard d'une lecture arrêtée */
    if (cfg_on) {
        if (GetTickCount() - cfg_tick > 60000L)
            cfg_close(FALSE);           /* menu oublié : rendre le clavier */
        return;
    }
    if (in_menu || sa_active())
        return;
    if (GetFocus() != last_focus || GetActiveWindow() != last_active)
        announce_focus(TRUE);
    else {
        auto_poll();
        status_poll();              /* (avant check_value : fiche changée) */
        if (!is_class(GetActiveWindow(), "SCICALC"))
            check_value();
    }
    calc_poll();
}

/* -------------------------------------------------------------------- menus */

static void on_menu(WPARAM wp, LPARAM lp)
{
    UINT flags = LOWORD(lp);
    HMENU hm = (HMENU) HIWORD(lp);
    char buf[TXT], out[TXT * 2], *accel;
    int i, n;

    if (flags == 0xFFFF && hm == NULL) {        /* menu fermé */
        in_menu = FALSE;
        menu_pending[0] = 0;
        KillTimer(hwnd_main, TIMER_MENU);
        last_focus = NULL;      /* relire le focus au prochain passage (et le titre si le
                                   menu a ouvert une autre fenêtre) */
        return;
    }
    if (!in_menu) {
        in_menu = TRUE;
    }
    if (flags & MF_SEPARATOR)
        return;
    buf[0] = 0;
    if (flags & MF_POPUP) {
        /* wParam est le menu déroulant : retrouver sa position dans hm */
        n = GetMenuItemCount(hm);
        for (i = 0; i < n; i++)
            if (GetSubMenu(hm, i) == (HMENU) wp) {
                GetMenuString(hm, i, buf, sizeof(buf), MF_BYPOSITION);
                break;
            }
    } else
        GetMenuString(hm, wp, buf, sizeof(buf), MF_BYCOMMAND);
    clean_label(buf, &accel);
    if (flags & MF_SYSMENU && !buf[0])
        lstrcpy(buf, "menu système");
    else if ((flags & MF_BITMAP) && !buf[0])
        lstrcpy(buf, "menu système du document");   /* icône de fenêtre MDI agrandie */
    out[0] = 0;
    append(out, buf);
    if (flags & MF_POPUP && !(flags & MF_SYSMENU) && !(flags & MF_BITMAP))
        append(out, "sous-menu");
    if (flags & MF_CHECKED)
        append(out, "coché");
    if (flags & (MF_GRAYED | MF_DISABLED))
        append(out, "grisé");
    if (accel && *accel)
        append(out, accel);
    if (!out[0])
        return;
    lstrcpyn(braille_menu, out, sizeof(braille_menu));
    if (flags & MF_POPUP) {
        /* Gauche ou Droite dans un menu ouvert : Windows resélectionne l'ancien
           titre, puis le nouveau, puis le premier élément de celui-ci, d'un trait.
           Un titre attend donc la fin de la rafale ; seul le dernier est dit. */
        lstrcpyn(menu_pending, out, sizeof(menu_pending));
        SetTimer(hwnd_main, TIMER_MENU, 60, NULL);
        return;
    }
    if (menu_pending[0]) {
        /* premier élément du menu qui s'ouvre : à la suite de son titre */
        KillTimer(hwnd_main, TIMER_MENU);
        say(menu_pending, TRUE);
        menu_pending[0] = 0;
        say(out, FALSE);
    } else
        say(out, !(popup_tick && GetTickCount() - popup_tick < 300));
    popup_tick = 0;
}

static void on_menu_title(void)
{
    KillTimer(hwnd_main, TIMER_MENU);
    if (!menu_pending[0] || !in_menu)
        return;
    say(menu_pending, TRUE);
    menu_pending[0] = 0;
    popup_tick = GetTickCount();    /* son premier élément, s'il suit, ne le coupe pas */
}

/* --------------------------------------------------- surbrillance (hors écran) */

/* Texte affiché en couleurs de sélection dans la fenêtre active : l'élément courant
   d'une liste dessinée par le programme (icônes du Gestionnaire de programmes...). */
static HWND hilite_hwnd;        /* fenêtre où un texte en surbrillance vient d'apparaître */

static void on_hilite(void)
{
    char buf[TXT];
    HWND foc = GetFocus(), h = hilite_hwnd;
    int i;

    if (!h || !IsWindow(h) || in_menu || !foc || is_standard(foc) || sa_active())
        return;
    {
        const char *role;
        if (custom_value(foc, buf, &role))
            return;     /* contrôle reconnu : check_value() dit sa valeur */
    }
    {   /* fenêtre active, ou liste surgissante qui lui appartient ; listes
           déroulantes des boîtes de Word (classe _sdm_…, sans propriétaire) */
        HWND t = top_level(h), act = GetActiveWindow();
        char cls[8];
        GetClassName(t, cls, sizeof(cls));
        if (t != act && GetWindow(t, GW_OWNER) != act &&
            !(_fstrncmp(cls, "_sdm_", 5) == 0 && is_sdm(act)))
            return;
    }
    if (is_sdm(h))
        return;     /* boîte de Word : le rectangle de focus annonce l'élément */
    if (!EpOsmHilite(h, buf, sizeof(buf)))
        return;
    for (i = 0; buf[i] == ' '; i++)
        ;
    if (!buf[i])
        return;
    if (lstrcmp(buf + i, hilite_said) == 0 && GetTickCount() - hilite_tick < 1500)
        return;                         /* simple réaffichage */
    lstrcpyn(hilite_said, buf + i, sizeof(hilite_said));
    hilite_tick = GetTickCount();
    /* juste après l'annonce du focus (nom du groupe...) : à la suite, sans couper */
    say(buf + i, GetTickCount() - focus_tick > 400);
}

/* ------------------------------------------- rectangle de focus (boîtes de Word) */

/* Les boîtes de dialogue de Word dessinent leurs boutons, listes et cases sans
   fenêtres : seul un rectangle pointillé montre l'élément qui a le focus. On lit le
   texte qu'il entoure ; une barre de défilement à sa droite signale une liste,
   dont le libellé est dessiné au-dessus. */
static HWND frect_hwnd;
static RECT frect_said;
static BOOL tab_waiting;        /* Ctrl+Tab : la page sera dite avant l'élément */
static DWORD frect_tick;
static int frect_check = -1;    /* case ou bouton radio annoncé : sdm_check() ; -1 aucun */

/* Case à cocher ou bouton radio de Word à gauche du rectangle de focus. Image 14×13
   copiée de la bande des cases de Windows : colonne 0 vide, 14 marquée (28 et 42 :
   enfoncée) ; rangée 0 case, 13 bouton radio, 26 case à trois états (marquée :
   indéterminée). Renvoie rangée * 2 + marquée, ou -1. */
#define CHK_ROW(c)  ((c) / 2)
#define CHK_ON(c)   ((c) % 2)
static int sdm_check(HWND h, const RECT *r)
{
    int w, ht, xs, ys;
    if (!EpOsmGlyph(h, r->left - 30, r->top - 2, r->left, r->bottom + 2, &w, &ht, &xs, &ys) ||
        w != 14 || ht != 13 || ys / 13 > 2)
        return -1;
    return ys / 13 * 2 + (xs / 14) % 2;
}

/* Titre du groupe d'une case ou d'un bouton radio de Word : le texte le plus proche
   au-dessus, commencé un peu à gauche de la case (cadre « Insérer », « Attributs »).
   S'il vient de plus loin à gauche (case de la colonne de gauche, titre centré) ou
   finit par « : » (libellé d'un autre contrôle), la case n'ouvre pas de groupe. */
static char sdm_group[TXT];     /* dernier groupe annoncé */
static HWND sdm_group_hwnd;     /* ... dans cette boîte */

static void group_title(HWND h, const RECT *r, char *out, int n)
{
    int t;
    out[0] = 0;
    for (t = r->top - 6; t >= r->top - 80; t -= 4)
        if (EpOsmRect(h, r->left - 40, t, r->left - 8, t + 4, out, n)) {
            if (ends_colon(out) || EpOsmCharAt(h, r->left - 44, t + 2))
                out[0] = 0;
            clean_label(out, NULL);
            return;
        }
}

static const char *check_state(int c)
{
    if (CHK_ROW(c) == 1)
        return CHK_ON(c) ? "sélectionné" : "non sélectionné";
    if (CHK_ON(c))
        return CHK_ROW(c) == 2 ? "indéterminé" : "coché";
    return "non coché";
}

/* Flèche de liste déroulante (7×9) à droite du rectangle */
static BOOL sdm_combo(HWND h, const RECT *r)
{
    int w, ht, xs, ys;
    return EpOsmGlyph(h, r->right, r->top - 2, r->right + 40, r->bottom + 2,
                      &w, &ht, &xs, &ys) && w <= 9 && ht <= 10;
}

/* Rectangle de focus qui n'occupe qu'une rangée d'une fenêtre bien plus haute, hors
   des boîtes de Word : élément d'une liste dessinée (Microsoft Mail) */
static BOOL list_row(HWND h, const RECT *r)
{
    RECT w;
    if (is_sdm(h))
        return FALSE;
    GetWindowRect(h, &w);
    return r->bottom - r->top < 30 && w.bottom - w.top > 3 * (r->bottom - r->top) &&
           r->right - r->left > (w.right - w.left) / 2;
}

static void on_focusrect(void)
{
    HWND h = frect_hwnd, c;
    RECT r, sb;
    char text[TXT], label[TXT], out[TXT * 2];
    const char *role = NULL;
    int kind;

    /* Contrôle standard (liste du Gestionnaire de fichiers...) : check_value() dit déjà
       l'élément ; son rectangle de focus le ferait redire avec « bouton ». */
    if (!h || !IsWindow(h) || in_menu || sa_active() || cfg_on || tab_waiting ||
        GetFocus() != h ||
        is_standard(h) || custom_value(h, text, &role) ||
        top_level(h) != GetActiveWindow() || (dlg_pending && h == dlg_hwnd))
        return;
    kind = EpOsmFocusRect(h, &r);
    if (!kind)
        return;
    if (EqualRect(&r, &frect_said) && GetTickCount() - frect_tick < 1500)
        return;                         /* simple réaffichage */
    EpOsmRect(h, r.left - 1, r.top - 1, r.right + 1, r.bottom + 1, text, sizeof(text));
    label[0] = 0;
    if (kind == 2) {
        /* onglet : Word dessine la rangée d'onglets dans un bitmap */
        clean_label(text, NULL);
        out[0] = 0;
        append(out, text);
        append(out, "onglet");
        frect_check = -1;
        sdm_hwnd = h;
        lstrcpyn(sdm_text, out, sizeof(sdm_text));
        frect_said = r;
        frect_tick = GetTickCount();
        lstrcpyn(hilite_said, text, sizeof(hilite_said));
        hilite_tick = frect_tick;
        say(out, GetTickCount() - focus_tick > 400);
        return;
    }
    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!is_class(c, "SCROLLBAR") || !IsWindowVisible(c))
            continue;
        GetWindowRect(c, &sb);
        if (sb.left >= r.right - 4 && sb.left <= r.right + 30 &&
            (r.top + r.bottom) / 2 >= sb.top && (r.top + r.bottom) / 2 < sb.bottom) {
            RECT list;
            role = "liste";
            SetRect(&list, r.left, sb.top, sb.left, sb.bottom);
            label_drawn(h, &list, label, sizeof(label), TRUE);
            break;
        }
    }
    if (!role)
        label_drawn(h, &r, label, sizeof(label), FALSE);
    frect_check = role ? -1 : sdm_check(h, &r);
    if (frect_check < 0 || h != sdm_group_hwnd)
        sdm_group[0] = 0;               /* hors d'un groupe, ou autre boîte */
    sdm_group_hwnd = h;
    out[0] = 0;
    clean_label(text, NULL);
    if (role) {
        /* libellé de la liste seulement en y entrant, pas à chaque élément */
        if (!(frect_said.left == r.left && frect_said.right == r.right &&
              GetTickCount() - frect_tick < 5000)) {
            append(out, label);
            append(out, role);
        }
        append(out, text[0] ? text : "vide");
    } else if (frect_check >= 0) {
        char group[TXT];
        group_title(h, &r, group, sizeof(group));
        if (group[0] && lstrcmp(group, sdm_group) != 0) {
            append(out, "groupe");      /* entrée dans un autre groupe */
            lstrcat(out, " ");
            lstrcat(out, group);
            lstrcpyn(sdm_group, group, sizeof(sdm_group));
        }   /* (sans titre au-dessus : colonne de droite du même groupe) */
        append(out, label);
        append(out, text);
        append(out, CHK_ROW(frect_check) == 1 ? "bouton radio" : "case à cocher");
        append(out, check_state(frect_check));
    } else if (sdm_combo(h, &r)) {
        append(out, label);
        append(out, "liste déroulante");
        append(out, text[0] ? text : "vide");
    } else if (list_row(h, &r)) {
        /* rangée d'une liste dessinée (dossiers et messages de Mail) : l'élément seul */
        append(out, text[0] ? text : "vide");
    } else {
        if (!text[0])
            append(out, label);         /* un bouton porte son nom : pas le voisin */
        append(out, text);
        append(out, "bouton");
    }
    /* braille : l'élément complet, avec son libellé même dans une liste */
    sdm_hwnd = h;
    if (role) {
        sdm_text[0] = 0;
        append(sdm_text, label);
        append(sdm_text, role);
        append(sdm_text, text[0] ? text : "vide");
    } else
        lstrcpyn(sdm_text, out, sizeof(sdm_text));
    frect_said = r;
    frect_tick = GetTickCount();
    lstrcpyn(hilite_said, text, sizeof(hilite_said));
    hilite_tick = frect_tick;
    /* juste après le titre d'une boîte qui s'ouvre : à la suite */
    say(out, GetTickCount() - focus_tick > 400);
}

/* Page affichée d'une boîte de Word à onglets : l'onglet en gras le plus haut, dans
   les 80 premiers points (jusqu'à trois rangées d'onglets, boîte Options) */
static int tab_page(HWND act, char *name, int n)
{
    POINT o;
    RECT rc;
    o.x = o.y = 0;
    ClientToScreen(act, &o);
    GetWindowRect(act, &rc);
    return EpOsmBoldIn(act, rc.left, o.y, rc.right, o.y + 80, name, n);  /* 3 rangées */
}

/* Ctrl+Tab dans une boîte de Word : annoncer la nouvelle page dès qu'elle est
   dessinée (l'onglet en gras change), ou au bout de 1,5 s */
static char tab_before[TXT];
static DWORD tab_tick;

static void on_tabpage(void)
{
    HWND act = GetActiveWindow(), foc = GetFocus();
    RECT fr;
    char name[TXT], out[TXT * 2], d[TXT * 2];

    if (!is_sdm(act) || in_menu || cfg_on || !tab_page(act, name, sizeof(name))) {
        KillTimer(hwnd_main, TIMER_TAB);
        tab_waiting = FALSE;
        return;
    }
    if (lstrcmp(name, tab_before) == 0 && GetTickCount() - tab_tick < 1500)
        return;                         /* pas encore redessinée */
    KillTimer(hwnd_main, TIMER_TAB);
    tab_waiting = FALSE;
    if (is_sdm(foc) && EpOsmFocusRect(foc, &fr) == 2)
        return;                         /* l'onglet a le focus : déjà annoncé */
    clean_label(name, NULL);
    out[0] = 0;
    append(out, name);
    append(out, "onglet");
    say(out, TRUE);
    focus_tick = GetTickCount();        /* l'élément qui a le focus suit, sans couper */
    if (is_sdm(foc)) {                  /* la boîte, ou sa page (Options) */
        frect_hwnd = foc;
        SetRectEmpty(&frect_said);
        on_focusrect();
    } else if (foc && top_level(foc) == act) {
        describe(foc, d);
        if (d[0])
            say(d, FALSE);
    }
}

/* Colonne de libellés d'un formulaire (« Nom: Boîte aux lettres: Mot de passe: ») :
   chacun sera dit avec son champ */
static BOOL labels_only(const char *s)
{
    int n = 0;
    for (; *s; s++)
        if (*s == ':')
            n++;
    return n >= 3;
}

static void on_dialog_text(void)
{
    static char big[1024];
    char title[TXT];
    HWND h = dlg_hwnd;

    HWND src = dlg_src && IsWindow(dlg_src) ? dlg_src : h;

    dlg_pending = FALSE;
    dlg_src = NULL;
    if (!h || !IsWindow(h) || GetActiveWindow() != h || in_menu)
        return;
    GetWindowText(h, title, sizeof(title));
    if (EpOsmMainText(src, big, sizeof(big)) && lstrcmp(big, title) != 0 &&
        !(src != h && labels_only(big))) {
        say(big, FALSE);
        focus_tick = GetTickCount();    /* l'élément qui a le focus suit, sans couper */
    }
    if (GetFocus() == h) {
        frect_hwnd = h;
        SetRectEmpty(&frect_said);
        on_focusrect();
    }
}

/* Case cochée ou décochée (Espace) : annoncer le nouvel état */
static void on_glyph(void)
{
    HWND h = frect_hwnd;
    RECT r;
    int c;
    if (!h || !IsWindow(h) || GetFocus() != h || frect_check < 0 ||
        !EpOsmFocusRect(h, &r) || !EqualRect(&r, &frect_said))
        return;
    c = sdm_check(h, &r);
    if (c >= 0 && c != frect_check) {
        frect_check = c;
        say(check_state(c), TRUE);
    }
}

/* -------------------------------------------------------------- commandes */

static void read_window(void)
{
    HWND act = GetActiveWindow(), c;
    char out[TXT * 2], d[TXT * 2];
    char cls[32];

    if (!act)
        return;
    GetWindowText(act, out, TXT);
    say(out, TRUE);
    for (c = GetWindow(act, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!IsWindowVisible(c))
            continue;
        class_of(c, cls, sizeof(cls));
        if (lstrcmp(cls, "STATIC") == 0) {
            GetWindowText(c, d, TXT);
            clean_label(d, NULL);
        } else
            describe(c, d);
        if (d[0])
            say(d, FALSE);
    }
}

/* Barre d'état : la rangée de texte au bas de la fenêtre active */
static void say_status(void)
{
    char buf[TXT * 2];
    if (status_line(GetActiveWindow(), buf, sizeof(buf)))
        say(buf, TRUE);
    else
        say("pas de barre d'état", TRUE);
}

/* ------------------------------------------------------------------ souris */

/* MOUSE_EVENT de USER, point d'entrée du pilote de souris : paramètres dans les
   registres (AX drapeaux, BX et CX position, DX nombre de boutons, SI:DI information
   supplémentaire). Les clics passent ainsi par la file système comme un vrai clic.
   Sans drapeau de déplacement, ils ont lieu à la position courante du pointeur. */
#define ME_LEFTDOWN  0x02
#define ME_LEFTUP    0x04
#define ME_RIGHTDOWN 0x08
#define ME_RIGHTUP   0x10

static FARPROC mouse_fn;
static int mouse_locked;        /* boutons verrouillés : 1 gauche, 2 droit */
static POINT mouse_back;        /* position à rétablir après un clic de routage */

static void mouse_raw(WORD flags)
{
    if (!mouse_fn)
        mouse_fn = GetProcAddress(GetModuleHandle("USER"), "MOUSE_EVENT");
    if (!mouse_fn)
        return;
#ifndef EP_NATIVE_TEST                  /* banc natif (tests/win16) : pas de pilote */
    _asm {
        push si
        push di
        mov ax, flags
        xor bx, bx
        xor cx, cx
        mov dx, 2
        xor si, si
        xor di, di
        pushf
        cli
        call dword ptr mouse_fn
        popf
        pop di
        pop si
    }
#endif
}

static void mouse_button(BOOL right, BOOL down)
{
    mouse_raw(right ? (down ? ME_RIGHTDOWN : ME_RIGHTUP) : (down ? ME_LEFTDOWN : ME_LEFTUP));
}

static void mouse_click(BOOL right)
{
    int bit = right ? 2 : 1;
    if (mouse_locked & bit) {           /* bouton verrouillé : le relâcher d'abord */
        mouse_button(right, FALSE);
        mouse_locked &= ~bit;
    }
    mouse_button(right, TRUE);
    mouse_button(right, FALSE);
}

/* Clic au point pt (écran) ; restore : remettre le pointeur où il était */
static void click_at(POINT pt, BOOL right, BOOL restore)
{
    if (restore) {
        GetCursorPos(&mouse_back);
        SetTimer(hwnd_main, TIMER_MS, 300, NULL);
    }
    SetCursorPos(pt.x, pt.y);
    mouse_click(right);
}

static void on_mouse(int action)
{
    BOOL right = (action & 3) == 2;
    int bit = right ? 2 : 1;

    if (action & 4) {                   /* Maj : verrouiller ou relâcher */
        mouse_locked ^= bit;
        say((mouse_locked & bit) ?
            (right ? "bouton droit verrouillé" : "bouton gauche verrouillé") :
            (right ? "bouton droit relâché" : "bouton gauche relâché"), TRUE);
        mouse_button(right, (mouse_locked & bit) != 0);
        return;
    }
    say(right ? "clic droit" : "clic gauche", TRUE);
    mouse_click(right);
}

/* --------------------------------------------------------- revue au pavé */

typedef struct {
    int kind;                   /* 0 : contrôle, 1 : EDIT, 2 : texte dessiné */
    HWND scope;
    POINT pt;
    int row, col;
    int sel_from, sel_to;       /* braille : colonnes sélectionnées (-1 : aucune) */
    char text[TXT * 2];
} REVIEW;

static void review_anchor(HWND h, POINT *pt)
{
    RECT rc;
    if (EpOsmCaret(h, pt))
        return;
    if (is_class(h, "EDIT")) {
        caret_screen(h, pt);
        return;
    }
    GetWindowRect(h, &rc);
    pt->x = (rc.left + rc.right) / 2;
    pt->y = (rc.top + rc.bottom) / 2;
}

static BOOL review_edit_line(REVIEW *r, int row)
{
    int n, count = (int) SendMessage(r->scope, EM_GETLINECOUNT, 0, 0L);
    if (row < 0 || row >= count)
        return FALSE;
    *(WORD *) r->text = sizeof(r->text) - 1;
    n = (int) SendMessage(r->scope, EM_GETLINE, row, (LPARAM) (LPSTR) r->text);
    r->text[n] = 0;
    r->row = row;
    return TRUE;
}

static BOOL review_load(REVIEW *r)
{
    HWND h = GetFocus(), top = GetActiveWindow();
    int col;
    if (!top || !IsWindow(top))
        return FALSE;
    if (!h) h = top;
    memset(r, 0, sizeof(*r));
    r->scope = h;
    if (!flat_review && calc_value(r->text)) {
        r->scope = top;
        if (calc_view == 1 && calc_button >= 0 && calc_available(calc_button)) {
            lstrcpyn(r->text, calc_name(calc_button), sizeof(r->text));
            append(r->text, "bouton");
        } else if (calc_view == 2) calc_summary(r->text);
        else if (h != top && is_standard(h) && IsWindowVisible(h)) describe(h, r->text);
        return TRUE;
    }
    if (flat_review) {
        r->kind = 2;
        r->scope = top;
        if (review_window == top) {
            r->pt = review_point;
            if (!EpOsmReviewLine(top, &r->pt, 2, r->text, sizeof(r->text), &col))
                return FALSE;
            r->col = review_column;
        } else {
            review_anchor(h, &r->pt);
            if (!EpOsmReviewLine(top, &r->pt, 2, r->text, sizeof(r->text), &r->col))
                return FALSE;
        }
        if (r->col > lstrlen(r->text)) r->col = lstrlen(r->text);
        review_window = top;
        review_point = r->pt;
        review_column = r->col;
    } else if (is_class(h, "EDIT")) {
        if (GetWindowLong(h, GWL_STYLE) & ES_PASSWORD) {
            lstrcpyn(r->text, "mot de passe", sizeof(r->text));
            return TRUE;
        }
        r->kind = 1;
        col = LOWORD(SendMessage(h, EM_GETSEL, 0, 0L));
        r->row = (int) SendMessage(h, EM_LINEFROMCHAR, col, 0L);
        r->col = col - (int) SendMessage(h, EM_LINEINDEX, r->row, 0L);
        if (!review_edit_line(r, r->row))
            return FALSE;
        if (r->col > lstrlen(r->text)) r->col = lstrlen(r->text);
    } else if (is_standard(h)) {
        describe(h, r->text);
    } else if (is_sdm(h) && h == sdm_hwnd && sdm_text[0]) {
        lstrcpyn(r->text, sdm_text, sizeof(r->text));     /* boîte de Word : élément du rectangle de focus */
    } else if (EpOsmCaret(h, &r->pt)) {
        r->kind = 2;
        r->pt.x += 1;                   /* caractère qui suit le curseur */
        r->pt.y += 2;
        EpOsmReviewLine(h, &r->pt, 0, r->text, sizeof(r->text), &r->col);
    } else if (!EpOsmHilite(h, r->text, sizeof(r->text))) {
        r->kind = 2;
        review_anchor(h, &r->pt);
        if (!EpOsmReviewLine(h, &r->pt, 2, r->text, sizeof(r->text), &r->col))
            return FALSE;
    }
    return TRUE;
}

static BOOL review_line(REVIEW *r, int direction)
{
    if (r->kind == 1)
        return review_edit_line(r, r->row + direction);
    if (r->kind == 2)
        return EpOsmReviewLine(r->scope, &r->pt, direction, r->text,
                              sizeof(r->text), &r->col) != 0;
    return direction == 0;
}

/* Position (écran) du point de revue : caractère de la revue à plat, sinon curseur de
   saisie, élément d'une boîte de Word ou milieu du contrôle qui a le focus.
   *scope : fenêtre dont le texte est concerné. */
static BOOL review_screen_point(POINT *pt, HWND *scope)
{
    REVIEW r;
    RECT rc;
    HWND h = GetFocus();
    char buf[TXT * 2];
    int col;

    if (flat_review) {
        if (!review_load(&r))
            return FALSE;
        *pt = r.pt;
        *scope = r.scope;
        return EpOsmReviewPoint(r.scope, pt, r.col);
    }
    /* Un bouton virtuel/bilan n'a pas de point écran : ne pas amener la souris
       au milieu de SciCalc, ce qui ferait viser un autre bouton. */
    if (calc_view && is_class(GetActiveWindow(), "SCICALC")) return FALSE;
    if (!h)
        h = GetActiveWindow();
    if (!h)
        return FALSE;
    *scope = h;
    if (is_sdm(h) && EpOsmFocusRect(h, &rc)) {
        pt->x = (rc.left + rc.right) / 2;
        pt->y = (rc.top + rc.bottom) / 2;
    } else if (EpOsmCaret(h, pt) || is_class(h, "EDIT")) {
        POINT q;
        caret_screen(h, pt);
        q.x = pt->x + 1;
        q.y = pt->y + 2;
        if (EpOsmReviewLine(h, &q, 0, buf, sizeof(buf), &col))
            pt->y = q.y;                /* milieu de la ligne */
        else
            pt->y += 2;
        pt->x++;
    } else {
        GetWindowRect(h, &rc);
        pt->x = (rc.left + rc.right) / 2;
        pt->y = (rc.top + rc.bottom) / 2;
    }
    return TRUE;
}

/* Inser + / : pointeur de la souris au point de revue */
static void mouse_to_review(void)
{
    POINT pt;
    HWND scope;
    if (!review_screen_point(&pt, &scope)) {
        say("aucune position", TRUE);
        return;
    }
    SetCursorPos(pt.x, pt.y);
    say("souris déplacée", TRUE);
}

/* Inser + * : lire la rangée sous le pointeur ; en revue à plat, y placer la revue */
static void review_to_mouse(void)
{
    POINT pt, q;
    HWND top;
    char text[TXT * 2];
    int col;

    GetCursorPos(&pt);
    top = top_level(WindowFromPoint(pt));
    q = pt;
    if (!top || !EpOsmReviewLine(top, &q, 2, text, sizeof(text), &col)) {
        say("rien sous la souris", TRUE);
        return;
    }
    if (flat_review && top == GetActiveWindow()) {
        review_window = top;
        review_point = q;
        review_point.x = pt.x;
        review_column = col;
    }
    say(text[0] ? text : "vide", TRUE);
}

/* Inser + F : police et attributs au point de revue */
static void say_format(void)
{
    POINT pt;
    HWND scope;
    char out[TXT];
    if (!review_screen_point(&pt, &scope) || !EpOsmFormat(scope, pt.x, pt.y, out, sizeof(out)))
        say("mise en forme inconnue", TRUE);
    else
        say(out, TRUE);
}

/* Inser + Maj + Haut : texte sélectionné */
static void say_selected(void)
{
    HWND h = GetFocus();
    static char text[TXT * 2];
    DWORD sel;

    text[0] = 0;
    if (h && is_class(h, "EDIT")) {
        sel = SendMessage(h, EM_GETSEL, 0, 0L);
        if (GetWindowLong(h, GWL_STYLE) & ES_PASSWORD)
            sel = 0;
        if (LOWORD(sel) != HIWORD(sel)) {
            edit_range(h, LOWORD(sel), HIWORD(sel), text, sizeof(text));
            if (HIWORD(sel) - LOWORD(sel) > 400) {
                wsprintf(text, "%d caractères sélectionnés", HIWORD(sel) - LOWORD(sel));
                say(text, TRUE);
                return;
            }
        }
    } else if (h && !is_standard(h))
        EpOsmSelection(h, text, sizeof(text));
    say(text[0] ? text : "aucune sélection", TRUE);
}

/* Le braille conserve sa propre ligne de revue ; un changement du focus,
   du texte ou du curseur ramène automatiquement au suivi du document. */
static REVIEW braille_live, braille_view;
static BOOL braille_valid;
static int braille_epoch;       /* la DLL a écrit seule (boîte modale système) */
static int route_key;
static DWORD route_tick;

static BOOL braille_same(REVIEW *a, REVIEW *b)
{
    return a->scope == b->scope && a->kind == b->kind && a->row == b->row &&
           a->col == b->col && a->pt.y == b->pt.y && a->sel_from == b->sel_from &&
           a->sel_to == b->sel_to && lstrcmp(a->text, b->text) == 0;
}

/* Partie sélectionnée de la ligne affichée en braille */
static void braille_selection(REVIEW *r)
{
    r->sel_from = r->sel_to = -1;
    if (r->kind == 1) {
        DWORD sel = SendMessage(r->scope, EM_GETSEL, 0, 0L);
        int base = is_multiline(r->scope) ?
            (int) SendMessage(r->scope, EM_LINEINDEX, r->row, 0L) : 0;
        int a = (int) LOWORD(sel) - base, b = (int) HIWORD(sel) - base;
        int n = lstrlen(r->text);
        if (a < 0) a = 0;
        if (b > n) b = n;
        if (a < b) {
            r->sel_from = a;
            r->sel_to = b;
        }
    } else if (r->kind == 2)
        EpOsmRowSelection(r->scope, r->pt.y, &r->sel_from, &r->sel_to);
}

static void say_column(const char *text, int col)
{
    int n = lstrlen(text);
    if (!n)
        say("vide", TRUE);
    else if (col >= n)
        say("fin de ligne", TRUE);
    else if (text[col] == ' ' || text[col] == '\t')
        say("espace", TRUE);
    else
        say_char(text[col], TRUE);
}

/* Curseur routine : zone d'édition, déplacer le curseur ; revue à plat, déplacer
   le point de revue (deux appuis : clic gauche à cet endroit) ; texte dessiné
   (Write, Word), cliquer sur le caractère pour y poser le curseur. */
static void braille_route(int key)
{
    REVIEW *v = &braille_view;
    HWND h = v->scope;
    POINT pt;
    int pos, base;
    BOOL again = key == route_key && GetTickCount() - route_tick < 1000;

    route_key = key;
    route_tick = GetTickCount();
    pos = br_offset() + key - BR_ROUTE;
    if (!IsWindow(h))
        return;
    if (v->kind == 1) {
        /* Ne jamais déplacer le curseur d'un ancien contrôle. */
        if (h != GetFocus() || (GetWindowLong(h, GWL_STYLE) & ES_PASSWORD) ||
            pos > lstrlen(v->text))
            return;
        base = (int) SendMessage(h, EM_LINEINDEX, v->row, 0L);
        if (base < 0)
            return;
        SendMessage(h, EM_SETSEL, 0, MAKELONG(base + pos, base + pos));
    } else if (v->kind == 2 && flat_review) {
        if (h != GetActiveWindow())
            return;
        if (pos > lstrlen(v->text))
            pos = lstrlen(v->text);
        review_window = h;
        review_point = v->pt;
        review_column = pos;
        pt = v->pt;
        if (again && EpOsmReviewPoint(h, &pt, pos)) {
            say("clic gauche", TRUE);
            click_at(pt, FALSE, FALSE);
            route_key = 0;
        } else
            say_column(v->text, pos);
    } else if (v->kind == 2 && h == GetFocus()) {
        pt = v->pt;
        if (pos > lstrlen(v->text) || !EpOsmReviewPoint(h, &pt, pos))
            return;
        click_at(pt, FALSE, TRUE);
    } else
        return;
    braille_valid = FALSE;
}

static void braille_poll(void)
{
    REVIEW r;
    int key, epoch = EpBrailleEpoch();
    if (epoch != braille_epoch) {       /* affichage remplacé par la DLL */
        braille_epoch = epoch;
        braille_valid = FALSE;
    }
    if (GetSysModalWindow())
        return;                 /* la DLL possède la plage pendant la boîte */
    if (in_menu) {
        br_text(braille_menu, -1, FALSE);
        br_select(-1, -1);
        braille_valid = FALSE;
    } else if (review_load(&r)) {
        braille_selection(&r);
        if (!braille_valid || !braille_same(&r, &braille_live)) {
            braille_live = r;
            braille_view = r;
            braille_valid = TRUE;
            br_text(r.text, r.kind ? r.col : -1, TRUE);
            br_select(r.sel_from, r.sel_to);
        }
    } else {
        br_text("", -1, FALSE);
        br_select(-1, -1);
        braille_valid = FALSE;
    }
    br_tick();
    key = br_key();
    if (key == BR_BACK || key == BR_FORWARD) br_pan(key == BR_BACK ? -1 : 1);
    else if (key == BR_HOME) {
        braille_valid = FALSE;
        br_pan(0);
    } else if (!in_menu && braille_valid && (key == BR_UP || key == BR_DOWN)) {
        if (review_line(&braille_view, key == BR_UP ? -1 : 1)) {
            braille_view.col = 0;
            if (flat_review && braille_view.kind == 2) {
                /* revue à plat : la parole et la souris suivent la ligne affichée */
                review_window = braille_view.scope;
                review_point = braille_view.pt;
                review_column = 0;
            }
            /* Une ligne de revue n'affiche pas le curseur de saisie. */
            braille_selection(&braille_view);
            br_text(braille_view.text, -1, FALSE);
            br_select(braille_view.sel_from, braille_view.sel_to);
        }
    } else if (!in_menu && braille_valid && key >= BR_ROUTE)
        braille_route(key);
}

static BOOL review_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void review_store(REVIEW *r)
{
    if (flat_review) {
        review_point = r->pt;
        review_column = r->col;
    }
}

static void on_review(int key)
{
    REVIEW r;
    int direction, n, start, end;

    if (key == 10) {
        flat_review = !flat_review;
        review_window = NULL;
        say(flat_review ? "revue à plat" : "revue normale", TRUE);
        return;
    }
    if (key == 11)
        review_window = NULL;
    if (!review_load(&r)) {
        say("aucun texte disponible", TRUE);
        return;
    }
    if (key == 11) {
        say("revue recentrée", TRUE);
        say(r.text[0] ? r.text : "vide", FALSE);
        return;
    }
    direction = key == 7 || key == 4 || key == 1 ? -1 :
                key == 9 || key == 6 || key == 3 ? 1 : 0;
    if (key >= 7) {
        if (direction && !review_line(&r, direction))
            goto boundary;
        if (direction) r.col = 0;
        review_store(&r);
        say(r.text[0] ? r.text : "vide", TRUE);
        return;
    }
    n = lstrlen(r.text);
    if (key <= 3) {
        if (direction < 0) {
            if (r.col > 0)
                r.col--;
            else {
                if (!review_line(&r, -1)) goto boundary;
                n = lstrlen(r.text);
                r.col = n ? n - 1 : 0;
            }
        } else if (direction > 0) {
            if (r.col < n)
                r.col++;
            else {
                if (!review_line(&r, 1)) goto boundary;
                r.col = 0;
            }
        }
        review_store(&r);
        n = lstrlen(r.text);
        if (!n) say("vide", TRUE);
        else if (r.col >= n) say("fin de ligne", TRUE);
        else if (review_space(r.text[r.col])) say("espace", TRUE);
        else say_char(r.text[r.col], TRUE);
        return;
    }
    if (direction < 0) {
        if (!r.col) {
            if (!review_line(&r, -1)) goto boundary;
            r.col = lstrlen(r.text);
        }
        while (r.col > 0 && review_space(r.text[r.col - 1])) r.col--;
        if (r.col > 0) r.col--;
        while (r.col > 0 && !review_space(r.text[r.col - 1])) r.col--;
    } else if (direction > 0) {
        while (r.col < n && !review_space(r.text[r.col])) r.col++;
        while (r.col < n && review_space(r.text[r.col])) r.col++;
        if (r.col >= n) {
            if (!review_line(&r, 1)) goto boundary;
            r.col = 0;
            while (review_space(r.text[r.col])) r.col++;
        }
    }
    review_store(&r);
    n = lstrlen(r.text);
    if (!n) say("vide", TRUE);
    else if (r.col >= n) say("fin de ligne", TRUE);
    else if (review_space(r.text[r.col])) say("espace", TRUE);
    else {
        start = r.col;
        while (start > 0 && !review_space(r.text[start - 1])) start--;
        end = r.col;
        while (r.text[end] && !review_space(r.text[end])) end++;
        r.text[end] = 0;
        say(r.text + start, TRUE);
    }
    return;
boundary:
    say(direction < 0 ? "début de la zone" : "fin de la zone", TRUE);
}

/* -------------------------------------------------- lecture continue (dire tout) */

/* Comme dans NVDA : lecture de la ligne du curseur jusqu'à la fin du document, ligne
   après ligne ; le curseur de l'application suit la lecture, ce qui fait défiler le
   document. Plusieurs lignes sont envoyées d'avance (SA_AHEAD) pour que la parole
   s'enchaîne sans pause, même quand il faut faire défiler l'écran ou traverser des
   lignes vides. Chaque ligne est suivie d'un écho @E : l'Apollo II le renvoie quand
   elle a été lue ; on envoie alors la suivante.
     EDIT (Bloc-notes) : lignes lues par EM_GETLINE, sans bouger le curseur ; celui-ci
     est posé par EM_SETSEL sur la ligne en cours de lecture.
     Texte dessiné (Word, Write) : on ne connaît que les rangées affichées. Le curseur
     est donc envoyé en avant, par les touches Bas puis Début, sur la dernière ligne
     envoyée : le programme fait défiler son texte pendant que la parole lit les
     lignes précédentes. Une interruption le ramène sur la ligne en cours de lecture. */

#define SA_AHEAD 3              /* lignes envoyées dont la lecture n'est pas finie */

typedef struct {
    char mark;                  /* écho attendu à la fin de sa lecture */
    int len;
    int moves;                  /* texte dessiné : descentes depuis la ligne précédente */
    int line;                   /* EDIT : numéro de la ligne */
} SALINE;

static struct {
    BOOL on;
    int kind;                   /* 1 : EDIT, 2 : texte dessiné, 3 : Aide (par page) */
    HWND hwnd;
    char seq;                   /* dernière marque attribuée */
    SALINE q[SA_AHEAD];         /* lignes envoyées, q[0] en cours de lecture */
    int n;
    DWORD head_tick;            /* début présumé de la lecture de q[0] */
    BOOL eof;                   /* plus rien à envoyer après q[n - 1] */
    int line;                   /* EDIT : prochaine ligne à examiner */
    BOOL moving;                /* curseur en cours de descente (texte dessiné) */
    DWORD move_tick;
    POINT caret;                /* position du curseur avant cette descente */
    DWORD sig;                  /* texte affiché avant cette descente */
    int moves;                  /* descentes depuis la dernière ligne envoyée */
    int blanks;                 /* lignes vides traversées d'affilée */
    int y;                      /* Aide (3) : ordonnée de la dernière rangée envoyée */
    char last[TXT];             /* ... et son texte, à retrouver après Page suivante */
} sa;

/* Touches envoyées au programme par la lecture continue : à ne pas prendre pour une
   frappe de l'utilisateur, même après l'arrêt de la lecture (retour du curseur). */
static int own_keys;
static DWORD own_tick;

static BOOL sa_edit_text(int line, char *out, int max)
{
    int n;
    if (line < 0 || line >= (int) SendMessage(sa.hwnd, EM_GETLINECOUNT, 0, 0L))
        return FALSE;
    *(WORD *) out = (WORD) (max - 1);
    n = (int) SendMessage(sa.hwnd, EM_GETLINE, line, (LPARAM) (LPSTR) out);
    out[n < max ? n : max - 1] = 0;
    return TRUE;
}

static int sa_edit_caret_line(int *col)
{
    int pos = LOWORD(SendMessage(sa.hwnd, EM_GETSEL, 0, 0L));
    int line = (int) SendMessage(sa.hwnd, EM_LINEFROMCHAR, pos, 0L);
    if (col)
        *col = pos - (int) SendMessage(sa.hwnd, EM_LINEINDEX, line, 0L);
    return line;
}

static BOOL sa_blank(const char *s)
{
    for (; *s; s++)
        if (*s != ' ' && *s != '\t')
            return FALSE;
    return TRUE;
}

/* Ligne du curseur ; from_caret : seulement à partir de la colonne du curseur */
static void sa_caret_row(char *out, BOOL from_caret)
{
    int col = 0;
    out[0] = 0;
    if (sa.kind == 1)
        sa_edit_text(sa_edit_caret_line(&col), out, TXT * 2);
    else {
        POINT pt;
        caret_screen(sa.hwnd, &pt);
        pt.y += 2;
        if (!EpOsmReviewLine(sa.hwnd, &pt, 0, out, TXT * 2, &col))
            out[0] = 0;
    }
    if (from_caret && col > 0 && col <= lstrlen(out))
        memmove(out, out + col, lstrlen(out + col) + 1);
}

static void sa_edit_caret(int line)
{
    int pos = (int) SendMessage(sa.hwnd, EM_LINEINDEX, line, 0L);
    if (pos >= 0)
        SendMessage(sa.hwnd, EM_SETSEL, 0, MAKELPARAM(pos, pos));
}

static void sa_push(const char *text, int moves, int line)
{
    SALINE *l = &sa.q[sa.n];
    sa.seq = (char) (sa.seq >= 'Z' || sa.seq < 'A' ? 'A' : sa.seq + 1);
    say_mark(text, sa.seq);
    l->mark = sa.seq;
    l->len = lstrlen(text);
    l->moves = moves;
    l->line = line;
    if (sa.n++ == 0) {
        sa.head_tick = GetTickCount();
        if (sa.kind == 1 && line != sa_edit_caret_line(NULL))
            sa_edit_caret(line);        /* lignes vides sautées : le curseur suit */
    }
}

static void sa_key(UINT vk, int scan)
{
    /* touche étendue (flèches dédiées), une répétition */
    PostMessage(sa.hwnd, WM_KEYDOWN, vk, MAKELPARAM(1, scan | 0x100));
    PostMessage(sa.hwnd, WM_KEYUP, vk, MAKELPARAM(1, scan | 0x100 | 0xC000));
    own_keys++;
    own_tick = GetTickCount();
}

static void sa_stop(BOOL cut)
{
    if (!sa.on)
        return;
    sa.on = FALSE;
    KillTimer(hwnd_main, TIMER_SA);
    if (cut)
        silence();
    if (sa.kind == 3 && IsWindow(sa.hwnd))   /* page défilée par la lecture : pas une */
        help_row(sa.hwnd, FALSE, status_said, sizeof(status_said), NULL);  /* autre rubrique */
    /* le curseur a bougé : ne pas relire la ligne où la lecture s'est arrêtée */
    line_key = FALSE;
    if (last_focus && IsWindow(last_focus)) {
        caret_screen(last_focus, &last_caret);
        remember_value(last_focus);
    }
}

/* Interruption par l'utilisateur : couper la parole et, pour le texte dessiné, ramener
   le curseur (envoyé en avant) sur la ligne qui était en cours de lecture. quiet : ne
   pas annoncer cette position (Ctrl, commande d'Épaulard). */
static void sa_interrupt(BOOL quiet)
{
    int i, up;
    if (!sa.on)
        return;
    if (sa.kind == 2 && sa.n && sa.hwnd == GetFocus()) {
        up = sa.moves + (sa.moving ? 1 : 0);
        for (i = 1; i < sa.n; i++)
            up += sa.q[i].moves;
        if (up) {
            while (up-- > 0)
                sa_key(VK_UP, 0x48);
            sa_key(VK_HOME, 0x47);
            caret_settle = GetTickCount();
            caret_quiet = quiet;
        }
    }
    sa_stop(TRUE);
}

/* Descendre le curseur d'une ligne (texte dessiné) */
static void sa_move(void)
{
    sa.moving = TRUE;
    sa.move_tick = GetTickCount();
    caret_screen(sa.hwnd, &sa.caret);
    sa.sig = EpOsmSignature(sa.hwnd);
    sa_key(VK_DOWN, 0x50);
    sa_key(VK_HOME, 0x47);
}

/* Aide : rangée suivant sa.y dans la rubrique visible ; FALSE en bas de la fenêtre */
static BOOL sa_help_next(char *text)
{
    HWND topic = help_part(sa.hwnd, "MS_WINTOPIC");
    RECT r;
    POINT pt;
    int col;
    if (!topic)
        return FALSE;
    GetWindowRect(topic, &r);
    pt.x = r.left + 2;
    pt.y = sa.y;
    if (!EpOsmReviewLine(sa.hwnd, &pt, 1, text, TXT * 2, &col) || pt.y >= r.bottom)
        return FALSE;
    sa.y = pt.y;
    return TRUE;
}

/* Aide : page suivante affichée ; reprendre après la dernière rangée lue si elle est
   restée visible (recouvrement), sinon au haut de la rubrique */
static void sa_help_resume(void)
{
    HWND topic = help_part(sa.hwnd, "MS_WINTOPIC");
    char text[TXT * 2];
    RECT r;
    int top;
    if (!topic) {
        sa.eof = TRUE;
        return;
    }
    GetWindowRect(topic, &r);
    top = sa.y = r.top - 1;
    if (sa.last[0])
        while (sa_help_next(text))
            if (lstrcmp(text, sa.last) == 0)
                return;                 /* sa.y : cette rangée */
    sa.y = top;
}

/* Envoyer d'avance les lignes suivantes, jusqu'à SA_AHEAD */
static void sa_fill(void)
{
    char text[TXT * 2];
    if (!sa.on)
        return;
    if (sa.kind == 3) {
        while (sa.n < SA_AHEAD && !sa.eof && !sa.moving) {
            if (sa_help_next(text)) {
                lstrcpyn(sa.last, text, sizeof(sa.last));
                if (!sa_blank(text))
                    sa_push(text, 0, 0);
            } else {
                /* bas de la fenêtre : page suivante */
                sa.moving = TRUE;
                sa.move_tick = GetTickCount();
                sa.sig = EpOsmSignature(sa.hwnd);
                sa_key(VK_NEXT, 0x51);
            }
        }
    } else if (sa.kind == 1) {
        while (sa.n < SA_AHEAD && !sa.eof) {
            if (!sa_edit_text(sa.line, text, sizeof(text))) {
                sa.eof = TRUE;
                break;
            }
            if (!sa_blank(text))
                sa_push(text, 0, sa.line);
            sa.line++;
        }
    } else if (!sa.moving && !sa.eof && sa.n < SA_AHEAD)
        sa_move();
    if (sa.eof && !sa.n && !sa.moving)
        sa_stop(FALSE);                 /* fin du document : tout a été lu */
}

static void sa_end(void)
{
    sa.moving = FALSE;
    sa.eof = TRUE;
    sa_fill();
}

/* Texte dessiné : le curseur a atteint la ligne suivante (ou n'a pas pu bouger) */
static void sa_arrived(void)
{
    char text[TXT * 2];
    POINT pt;
    BOOL moved;
    DWORD dt = GetTickCount() - sa.move_tick;

    if (sa.kind == 3) {
        /* Aide : attendre que la page soit redessinée ; inchangée, c'est la fin */
        if (EpOsmSignature(sa.hwnd) == sa.sig) {
            if (dt >= 700)
                sa_end();
            return;
        }
        if (dt < 300)
            return;
        sa.moving = FALSE;
        sa_help_resume();
        sa_fill();
        return;
    }
    caret_screen(sa.hwnd, &pt);
    moved = pt.x != sa.caret.x || pt.y != sa.caret.y;
    /* Laisser le programme traiter les touches. Si le curseur reste en place et que
       rien n'a changé à l'écran, c'est la fin du document ; si le texte a changé,
       il a défilé sous le curseur (bas de la fenêtre). */
    if (!moved && EpOsmSignature(sa.hwnd) == sa.sig) {
        if (dt >= 500)
            sa_end();
        return;
    }
    sa_caret_row(text, FALSE);
    if (sa_blank(text)) {
        /* ligne vide, ou pas encore dessinée après un défilement */
        if (dt < (DWORD) (moved ? 100 : 300))
            return;
        sa.moves++;
        if (++sa.blanks > 100)
            sa_end();
        else
            sa_move();
        return;
    }
    sa.moves++;
    sa.moving = FALSE;
    sa.blanks = 0;
    sa_push(text, sa.moves, 0);
    sa.moves = 0;
    sa_fill();
}

/* Fin de lecture de q[0] : passer à la ligne suivante */
static void sa_pop(void)
{
    int i;
    if (!sa.n)
        return;
    sa.n--;
    for (i = 0; i < sa.n; i++)
        sa.q[i] = sa.q[i + 1];
    sa.head_tick = GetTickCount();
    if (sa.kind == 1 && sa.n)
        sa_edit_caret(sa.q[0].line);
    sa_fill();
}

/* Écho renvoyé par l'Apollo II : une ligne a fini d'être lue (et celles d'avant) */
static void sa_mark(char c)
{
    int i, k;
    if (!sa.on || !c)
        return;
    for (i = 0; i < sa.n; i++)
        if (sa.q[i].mark == c) {
            for (k = 0; k <= i && sa.on; k++)
                sa_pop();
            return;
        }
}

static void sa_tick(void)
{
    if (!sa.on)
        return;
    if (!IsWindow(sa.hwnd) || GetFocus() != sa.hwnd) {
        sa_stop(TRUE);
        return;
    }
    com_read();
    if (sa.on && sa.moving)
        sa_arrived();
    if (sa.on && sa.n &&
        GetTickCount() - sa.head_tick > 3000 + 150 * (DWORD) sa.q[0].len)
        sa_pop();       /* pas d'écho en retour (synthèse muette ou débranchée) */
}

static BOOL sa_active(void)
{
    return sa.on;
}

static void sa_start(void)
{
    HWND h = GetFocus();
    char text[TXT * 2];
    int line;

    sa_stop(FALSE);
    memset(&sa, 0, sizeof(sa));
    if (!h)
        return;
    if (is_help(h)) {
        /* Aide : de la première rangée de la page affichée, puis page par page */
        HWND part = help_part(h, "MS_WINNSR");
        RECT r;
        if (!help_row(h, FALSE, text, sizeof(text), NULL))
            return;
        GetWindowRect(part ? part : help_part(h, "MS_WINTOPIC"), &r);
        sa.kind = 3;
        sa.hwnd = h;
        sa.on = TRUE;
        silence();
        sa.y = r.top - 1;               /* au-dessus de la première rangée */
        sa_fill();
        if (sa.on)
            SetTimer(hwnd_main, TIMER_SA, SA_MS, NULL);
        return;
    }
    if (is_class(h, "EDIT") && !(GetWindowLong(h, GWL_STYLE) & ES_PASSWORD))
        sa.kind = 1;
    else if (!is_standard(h))
        sa.kind = 2;
    else {
        read_window();                  /* boîte de dialogue : tout son contenu */
        return;
    }
    sa.hwnd = h;
    sa.on = TRUE;
    silence();
    sa_caret_row(text, TRUE);
    line = sa.kind == 1 ? sa_edit_caret_line(NULL) : 0;
    if (!sa_blank(text))
        sa_push(text, 0, line);
    sa.line = line + 1;
    sa_fill();
    if (sa.on)
        SetTimer(hwnd_main, TIMER_SA, SA_MS, NULL);
}

static void on_command(WPARAM vk, LPARAM lp)
{
    char buf[TXT];
    const char *role;

    /* lp bit 0 : Maj */
    if (vk == VK_UP && (lp & 1)) {
        say_selected();
        return;
    }
    if (calc_command(vk)) return;
    switch (vk) {
    case 'T':
        GetWindowText(GetActiveWindow(), buf, sizeof(buf));
        say(buf[0] ? buf : "sans titre", TRUE);
        break;
    case VK_TAB:
        last_active = NULL;
        announce_focus(TRUE);
        break;
    case VK_DOWN:
        sa_start();
        break;
    case VK_UP:
        if (is_class(GetActiveWindow(), "CLOCK")) {
            clock_value(buf);
            say(buf, TRUE);
        } else if (last_focus && custom_value(last_focus, buf, &role) && buf[0]) {
            say(buf, TRUE);             /* lien de l'Aide, caractère choisi, volet */
        } else if (is_sdm(last_focus)) {
            frect_hwnd = last_focus;    /* boîte de Word : relire l'élément */
            SetRectEmpty(&frect_said);
            on_focusrect();
        } else if (last_focus && IsWindow(last_focus) && !is_standard(last_focus)) {
            POINT pt;
            if (EpOsmCaret(last_focus, &pt) || !EpOsmHilite(last_focus, buf, TXT)) {
                caret_screen(last_focus, &pt);
                EpOsmLine(last_focus, pt.y + 2, buf, TXT);
            }                           /* (sinon : élément en surbrillance) */
            say(buf[0] ? buf : "vide", TRUE);
        } else if (last_focus && IsWindow(last_focus)) {
            describe_value(last_focus, buf);
            say(buf[0] ? buf : "vide", TRUE);
        }
        break;
    case 'B':
        read_window();
        break;
    case 'E':
        cfg_open();
        break;
    case VK_NEXT:
        say_status();
        break;
    case 'F':
        say_format();
        break;
    case VK_DIVIDE:
        mouse_to_review();
        break;
    case VK_MULTIPLY:
        review_to_mouse();
        break;
    case 'D':                           /* débogage : focus, classe, curseur texte */
        if (lp & 1) {                   /* avec Maj : fenêtres de la fenêtre active */
            HWND c, a = GetFocus() ? top_level(GetFocus()) : GetActiveWindow();
            char cls[32], txt[40];
            int n = 0, depth = 1;
            class_of(a, cls, sizeof(cls));
            wsprintf(buf, "fenêtre %x %s, actif %x.", (UINT) a, (LPSTR) cls,
                     (UINT) GetActiveWindow());
            say(buf, TRUE);
            for (c = GetWindow(a, GW_CHILD); c && n < 40; n++) {
                class_of(c, cls, sizeof(cls));
                GetWindowText(c, txt, sizeof(txt));
                wsprintf(buf, "%d: %x %s id %d style %lx %s.", depth, (UINT) c, (LPSTR) cls,
                         GetWindowWord(c, GWW_ID), GetWindowLong(c, GWL_STYLE), (LPSTR) txt);
                say(buf, FALSE);
                if (GetWindow(c, GW_CHILD)) {
                    c = GetWindow(c, GW_CHILD);
                    depth++;
                } else {
                    while (c && !GetWindow(c, GW_HWNDNEXT) && c != a) {
                        c = GetParent(c);
                        depth--;
                    }
                    c = (c && c != a) ? GetWindow(c, GW_HWNDNEXT) : (HWND) 0;
                }
            }
            break;
        }
        {
            HWND f = GetFocus();
            char cls[32];
            POINT pt;
            GetCaretPos(&pt);
            GetClassName(f, cls, sizeof(cls));
            wsprintf(buf, "focus %x classe %s, curseur %d %d, actif %x, dernier %d %d, menu %d",
                     (UINT) f, (LPSTR) cls, pt.x, pt.y, (UINT) GetActiveWindow(),
                     last_caret.x, last_caret.y, in_menu);
            say(buf, TRUE);
            for (f = GetParent(f); f; f = GetParent(f)) {     /* parents et propriétaires */
                GetClassName(f, cls, sizeof(cls));
                wsprintf(buf, "parent %x %s style %lx", (UINT) f, (LPSTR) cls,
                         GetWindowLong(f, GWL_STYLE));
                say(buf, FALSE);
            }
        }
        break;
    case 'O':
        {
            static char big[1024];
            int n = EpOsmWindow(GetActiveWindow(), big, sizeof(big));
            say(n ? big : "rien à l'écran", TRUE);
        }
        break;
    case 'H':
        if (is_class(GetActiveWindow(), "SCICALC")) {
            say("Calculatrice. Avec Inser : gauche et droite, parcourir les boutons. "
                "Début et fin, premier et dernier bouton. Entrée, activer le bouton. "
                "Tabulation, relire le bouton choisi. Haut, relire le résultat. "
                "Page suivante, mémoire et réglages scientifiques. "
                "La frappe habituelle rend le suivi au résultat.", TRUE);
            break;
        }
        say("Commandes d'Épaulard, avec la touche Inser : T, titre. Tabulation, "
            "focus. Flèche haut, ligne courante. Flèche bas, dire tout. B, lire la "
            "fenêtre. O, lire tout le texte affiché. F, police et mise en forme. "
            "Page suivante, barre d'état. E, réglages de la voix. "
            "Majuscule flèche haut, lire la sélection. F4, quitter. Contrôle seul, "
            "silence.", TRUE);
        say("Pavé numérique, Verr Num éteint : 7, 8, 9, lignes. 4, 5, 6, mots. "
            "1, 2, 3, caractères. Inser moins, changer de revue. Inser 5, recentrer.", FALSE);
        say("Souris : barre oblique du pavé, clic gauche ; étoile, clic droit ; avec "
            "majuscule, verrouiller le bouton. Inser barre oblique, amener la souris "
            "au point de revue. Inser étoile, lire sous la souris.", FALSE);
        break;
    case VK_F4:
        say("Épaulard arrêté", TRUE);
        DestroyWindow(hwnd_main);
        break;
    }
}

/* ---------------------------------------------------------- écho du clavier */

static char typed_word[40];     /* mot en cours de frappe (écho par mots) */

/* Caractère tapé : selon le réglage, le caractère, le mot qu'il termine, ou les deux */
static void echo_char(char ch)
{
    int mode = cfg[CFG_ECHO];
    BOOL chars = mode == 0 || mode == 2, words = mode == 1 || mode == 2;

    if (ch != '\r')
        typed = TRUE;                   /* le déplacement du curseur n'est pas lu */
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

/* ------------------------------------------------------- réglages (Inser+E) */

/* Menu parlé : Haut et Bas choisissent le réglage, Gauche et Droite changent sa
   valeur (appliquée aussitôt), Entrée enregistre, Échap rétablit. Pendant ce temps,
   EPAUHOOK.DLL donne toutes les touches à Épaulard (EpCfgMode). */
static int cfg_item;
static int cfg_before[CFG_N];

static void cfg_value(int i, char *out)
{
    static const char *const punct[] = { "aucune", "certaine", "toute" };
    static const char *const echo[] = { "caractères", "mots", "caractères et mots", "aucun" };
    if (i == CFG_PUNCT)
        lstrcpy(out, punct[cfg[i]]);
    else if (i == CFG_ECHO)
        lstrcpy(out, echo[cfg[i]]);
    else if (i == CFG_CAPS)
        lstrcpy(out, cfg[i] ? "dire majuscule" : "ne rien dire");
    else
        wsprintf(out, "%d", cfg[i]);
}

static void cfg_say(BOOL with_name, BOOL cut)
{
    char out[TXT], v[40];
    out[0] = 0;
    if (with_name)
        append(out, cfg_label[cfg_item]);
    cfg_value(cfg_item, v);
    append(out, v);
    say(out, cut);
}

static void cfg_close(BOOL keep)
{
    cfg_on = FALSE;
    EpCfgMode(FALSE);
    if (keep) {
        cfg_save();
        say("Réglages enregistrés", TRUE);
    } else {
        memcpy(cfg, cfg_before, sizeof(cfg));
        silence();
        voice_apply();
        say("Réglages annulés", FALSE);
    }
}

static void cfg_open(void)
{
    sa_stop(TRUE);
    memcpy(cfg_before, cfg, sizeof(cfg));
    cfg_on = TRUE;
    cfg_item = 0;
    cfg_tick = GetTickCount();
    EpCfgMode(TRUE);
    say("Réglages d'Épaulard", TRUE);
    {
        char out[TXT], v[40];
        out[0] = 0;
        append(out, cfg_label[cfg_item]);
        cfg_value(cfg_item, v);
        append(out, v);
        say(out, FALSE);
    }
}

static void cfg_key_in(WPARAM vk)
{
    int d = 0;
    cfg_tick = GetTickCount();
    switch (vk) {
    case VK_UP:
        cfg_item = (cfg_item + CFG_N - 1) % CFG_N;
        cfg_say(TRUE, TRUE);
        return;
    case VK_DOWN:
    case VK_TAB:
        cfg_item = (cfg_item + 1) % CFG_N;
        cfg_say(TRUE, TRUE);
        return;
    case VK_LEFT:
        d = -1;
        break;
    case VK_RIGHT:
        d = 1;
        break;
    case VK_RETURN:
    case 'E':
        cfg_close(TRUE);
        return;
    case VK_ESCAPE:
        cfg_close(FALSE);
        return;
    case VK_CONTROL:
        silence();
        return;
    case 'H':
        say("Réglages : flèches haut et bas, choisir ; gauche et droite, changer ; "
            "Entrée, enregistrer ; Échap, annuler.", TRUE);
        return;
    default:
        return;
    }
    if (cfg[cfg_item] + d < cfg_min[cfg_item] || cfg[cfg_item] + d > cfg_max[cfg_item]) {
        cfg_say(FALSE, TRUE);           /* déjà au bout */
        return;
    }
    cfg[cfg_item] += d;
    silence();
    if (cfg_item <= CFG_PUNCT)
        voice_apply();                  /* entendre aussitôt la nouvelle voix */
    cfg_say(FALSE, FALSE);
}

/* ---------------------------------------------------------------- fenêtre */

LRESULT CALLBACK __export MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (wp == TIMER_HI) {
            KillTimer(h, TIMER_HI);
            on_hilite();
        } else if (wp == TIMER_SA)
            sa_tick();
        else if (wp == TIMER_FR) {
            KillTimer(h, TIMER_FR);
            on_focusrect();
        } else if (wp == TIMER_GL) {
            KillTimer(h, TIMER_GL);
            on_glyph();
        } else if (wp == TIMER_DLG) {
            KillTimer(h, TIMER_DLG);
            on_dialog_text();
        } else if (wp == TIMER_MS) {
            KillTimer(h, TIMER_MS);
            SetCursorPos(mouse_back.x, mouse_back.y);
        } else if (wp == TIMER_MENU)
            on_menu_title();
        else if (wp == TIMER_TAB)
            on_tabpage();
        else if (wp == TIMER_LB) {
            char v[TXT];
            KillTimer(h, TIMER_LB);
            item_unknown = FALSE;
            if (GetFocus() == last_focus && last_focus && !in_menu) {
                describe_value(last_focus, v);
                if (!item_unknown)
                    say(v, FALSE);
            }
        }
        else {
            poll();
            if (br_enabled()) braille_poll();
        }
        return 0;
    case EPM_CMD:
        if (cfg_on) {
            cfg_key_in(wp);
            return 0;
        }
        if (wp != VK_DOWN)
            sa_interrupt(TRUE);
        on_command(wp, lp);
        return 0;
    case EPM_CFGKEY:
        if (cfg_on)
            cfg_key_in(wp);
        else
            EpCfgMode(FALSE);           /* (menu déjà fermé) */
        return 0;
    case EPM_REVIEW:
        sa_interrupt(TRUE);
        on_review((int) wp);
        return 0;
    case EPM_SILENCE:
        sa_interrupt(TRUE);
        silence();
        return 0;
    case EPM_MOUSE:
        sa_stop(TRUE);
        on_mouse((int) wp);
        return 0;
    case EPM_CHAR:
        if (!in_menu && is_class(GetActiveWindow(), "SCICALC")) {
            const char *op = NULL;
            if (wp == '+') op = "plus";
            else if (wp == '-') op = "moins";
            else if (wp == '*') op = "multiplié par";
            else if (wp == '/') op = "divisé par";
            else if (wp == '%') op = "pour cent";
            else if (wp == ',' || wp == '.') op = "virgule";
            if (wp == '=' || wp == '\r') calc_force = TRUE;
            if (op) say(op, TRUE);
            return 0;                   /* la valeur remplace l'écho de frappe */
        }
        /* écho des caractères tapés, sauf dans les boutons (Espace les active : on
           annonce plutôt le résultat) */
        if (in_menu || is_class(GetFocus(), "BUTTON") || is_sdm(GetFocus()))
            return 0;                   /* (boîte de Word : Espace coche une case) */
        if (wp >= ' ' || wp == '\r' || (wp == '\t' && is_class(GetFocus(), "EDIT")))
            echo_char((char) wp);
        return 0;
    case EPM_MENU:
        sa_stop(TRUE);
        on_menu(wp, lp);
        return 0;
    case EPM_HILITE:
        /* les morceaux (lignes d'un même libellé) arrivent l'un après l'autre */
        hilite_hwnd = (HWND) LOWORD(lp);
        SetTimer(h, TIMER_HI, 60, NULL);
        return 0;
    case EPM_FOCUSRECT:
        frect_hwnd = (HWND) wp;
        SetTimer(h, TIMER_FR, 80, NULL);
        return 0;
    case EPM_GLYPH:
        if ((HWND) wp == frect_hwnd)
            SetTimer(h, TIMER_GL, 150, NULL);
        return 0;
    case EPM_ACTIVATE:
        /* Toujours redire le titre : la nouvelle fenêtre a pu reprendre l'identifiant
           d'une fenêtre fermée. Les doublons sont écartés par announce_focus(). */
        sa_stop(TRUE);
        new_active = TRUE;
        last_focus = NULL;
        review_window = NULL;
        return 0;
    case EPM_FOCUS:
        /* signalé avant que le focus change : il a changé quand on traite ce message.
           Ne pas répéter ce que la scrutation vient d'annoncer. */
        if ((HWND) wp != sa.hwnd)
            sa_stop(TRUE);
        if (!in_menu)
            announce_focus(TRUE);
        return 0;
    case EPM_KEY:
        if (is_class(GetActiveWindow(), "SCICALC")) {
            calc_view = 0;
            calc_button = -1;
        }
        if (!in_menu && is_class(GetActiveWindow(), "SCICALC") &&
            (wp == VK_ESCAPE || wp == VK_DELETE || wp == VK_BACK))
            calc_force = TRUE;          /* redire zéro même si déjà affiché */
        /* touches envoyées par la lecture continue elle-même */
        if (own_keys > 0 && GetTickCount() - own_tick > 3000)
            own_keys = 0;               /* jamais revues : ne plus les attendre */
        if (own_keys > 0 && (wp == VK_DOWN || wp == VK_UP || wp == VK_HOME || wp == VK_NEXT)) {
            own_keys--;
            return 0;
        }
        if (sa.on) {
            /* toute autre touche arrête la lecture ; Haut et Bas partent de la
               ligne qui était lue (les autres touches agissent où est le curseur) */
            if (wp == VK_UP || wp == VK_DOWN)
                sa_interrupt(FALSE);
            else
                sa_stop(TRUE);
        }
        last_vk = wp;
        key_tick = GetTickCount();
        if (wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT ||
            ((wp == VK_HOME || wp == VK_END) && (lp & EPK_CTRL))) {
            line_key = TRUE;
            line_key_tick = GetTickCount();
            typed = FALSE;
        }
        if (wp == VK_BACK)
            typed = FALSE;              /* l'effacement déplace le curseur : on le dit */
        if (wp >= VK_PRIOR && wp <= VK_DOWN) {
            /* Maj + déplacement : la sélection change ; seul, il la retire */
            sel_key = (lp & EPK_SHIFT) != 0;
            sel_key_tick = GetTickCount();
            if (!sel_key)
                sel_said[0] = 0;
        }
        word_key = (wp == VK_LEFT || wp == VK_RIGHT) && (lp & EPK_CTRL);
        if (wp >= VK_PRIOR && wp <= VK_DOWN)
            typed_n = 0;                /* le mot en cours de frappe est abandonné */
        else if (wp == VK_BACK && typed_n)
            typed_n--;
        if (wp == VK_TAB && (lp & EPK_CTRL) && is_sdm(GetActiveWindow())) {
            /* page suivante d'une boîte de Word */
            tab_page(GetActiveWindow(), tab_before, sizeof(tab_before));
            tab_tick = GetTickCount();
            tab_waiting = TRUE;
            SetTimer(h, TIMER_TAB, 100, NULL);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(h, TIMER_ID);
        br_close();
        EpRemove();
        if (com >= 0) {
            /* laisser partir la dernière phrase avant de fermer le port */
            DWORD t = GetTickCount();
            COMSTAT st;
            while (GetTickCount() - t < 2000) {
                GetCommError(com, &st);
                if (st.cbOutQue == 0)
                    break;
            }
            CloseComm(com);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;

    (void) cmd; (void) show;
    if (hprev)
        return 0;                       /* une seule instance */
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hinst;
    wc.lpszClassName = "Epaulard";
    if (!RegisterClass(&wc))
        return 0;
    hwnd_main = CreateWindow("Epaulard", "Épaulard", WS_OVERLAPPED, 0, 0, 0, 0,
                             NULL, NULL, hinst, NULL);
    if (!hwnd_main)
        return 0;
    com_open();
    if (com < 0) {
        MessageBox(NULL, "Impossible d'ouvrir COM1.", "Épaulard", MB_OK | MB_ICONSTOP);
        return 0;
    }
    cfg_load(hinst);
    voice_apply();
    if (!EpInstall(hwnd_main, com))
        say("Erreur : crochets non installés", TRUE);
    br_open();
    SetTimer(hwnd_main, TIMER_ID, POLL_MS, NULL);
    say("Épaulard prêt. Inser H pour l'aide.", TRUE);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
