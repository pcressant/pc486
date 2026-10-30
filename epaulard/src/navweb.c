/* EPAULARD.EXE — mode navigation des pages Web (Netscape 3, Communicator 4.08),
   comparable à celui de NVDA.

   Netscape 16 bits ne donne pas son document : il ne dessine que la partie visible
   (modèle hors écran) et ses champs de formulaire sont des fenêtres filles de la vue,
   créées dans l'ordre du document. Le proxy de pc486, qui a produit le HTML 3.2 reçu
   par Netscape, en calcule la structure (scripts/reseau/tampon.py) : texte coupé en
   lignes et éléments (titres, liens, listes, tableaux, champs...). Épaulard demande
   par DDE l'adresse de la page affichée, puis cette structure par Winsock (navnet.c),
   et vérifie le titre et quelques mots visibles.

   Mode navigation : un curseur virtuel parcourt ce tampon ; les flèches, les lettres
   de navigation rapide et Entrée sont prises par EPAUHOOK.DLL (EpNavMode) et données
   ici (EPM_NAV). Mode formulaire : les touches vont à Netscape. Inser+Espace bascule ;
   Échap sort d'un mode formulaire entré tout seul (Entrée sur un champ de saisie).
   Les champs sont lus et activés par leurs fenêtres (rang dans le document = rang
   par identifiant croissant, vérifié par classe et style). Les liens sont ouverts
   par DDE (WWW_OpenURL). */
#include "ep.h"

#define NAV_TEXT   30000u       /* tampon.py : MAX_TEXT */
#define NAV_ITEMS  1500         /* tampon.py : MAX_ITEMS */
#define NAV_STR    24000u
#define NAV_NONE   0xFFFFu
#define NAV_OUTSIDE 0xFFFEu     /* position hors de tout conteneur */
#define NAV_FIELDS 160
#define NAV_EDITS  16
#define NAV_OUT    700
#define NAV_PAGE   20           /* lignes par Page suivante */
#define NAV_STABLE 600          /* page inchangée depuis : demander sa structure */
#define NAV_CHAIN  8

typedef struct {
    char kind;
    BYTE level;
    WORD start, end, parent, a, b, c, d;
    WORD s;                     /* chaîne (dans nav_str) ou NAV_NONE */
} NAVITEM;

static struct {
    HWND view, frame;
    BOOL loaded;                /* tampon prêt pour view */
    BOOL loading;               /* demande en cours */
    BOOL want;                  /* page changée : demander sa structure quand elle est dessinée */
    BOOL keep;                  /* même page redemandée : garder la position */
    BOOL autoread;              /* nouvelle page : dire tout dès que le tampon est prêt */
    BOOL warned;                /* page affichée peut-être différente : déjà dit */
    BOOL prefix;                /* « mode formulaire » à dire avec le champ qui prend le focus */
    int mode;                   /* 0 navigation, 1 formulaire, 2 formulaire automatique */
    DWORD sig, sig_tick, auto_tick;
    char seen[TXT];             /* titre de la fenêtre de Netscape */
    char url[256], dtitle[TXT]; /* adresse et titre donnés par DDE */
    char title[128];            /* titre du tampon */
    WORD len, n, nstr, pos, keep_pos;
    /* lecture du tampon reçu */
    int pstate;                 /* 0 en-tête, 1 texte, 2 éléments, 3 fini, 9 erreur */
    BOOL pok;
    WORD premain;
    char pline[300];
    int plen;
    /* champs : fenêtres de la vue dans l'ordre du document */
    HWND fields[NAV_FIELDS];
    int nfields;
    BOOL mapped;
    HWND quiet;                 /* champ que l'on vient d'activer nous-mêmes */
    int check;                  /* case à relire après activation */
    /* tableaux : ligne et colonne suivies (cellules fusionnées) */
    WORD ttable;
    int trow, tcol;
    /* braille : ligne affichée */
    WORD br_pos;
    int br_col;
    char br_text[NAV_OUT];
    WORD sa_prev;               /* dire tout : début de la ligne précédente */
} nav;

static char *nav_text;
static NAVITEM *nav_items;
static char *nav_str;
static struct { WORD a; char v[48]; } nav_edits[NAV_EDITS];
static UINT nav_vk_comma;

/* Annonce composée et, pour le braille, position du texte de chaque caractère */
static char nav_out[NAV_OUT];
static WORD nav_at[NAV_OUT];
static int nav_n;
static BOOL nav_br;
static BOOL nav_gap;            /* un mot ajouté : espace avant le texte qui suit */

/* --------------------------------------------------------------- tampon */

static BOOL nav_alloc(void)
{
    if (!nav_text) {
        nav_text = (char *) GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, NAV_TEXT + 2));
        nav_items = (NAVITEM *) GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                                                       (DWORD) NAV_ITEMS * sizeof(NAVITEM)));
        nav_str = (char *) GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, NAV_STR));
    }
    return nav_text && nav_items && nav_str;
}

static const char *nav_s(const NAVITEM *it)
{
    return it->s == NAV_NONE ? "" : nav_str + it->s;
}

static BOOL nav_is(char k, const char *kinds)
{
    return k && strchr(kinds, k) != NULL;
}

static BOOL nav_brk(char c)
{
    return c == '\n' || c == '\x0b';
}

static WORD nav_line_start(WORD p)
{
    while (p > 0 && !nav_brk(nav_text[p - 1]))
        p--;
    return p;
}

/* fin de la ligne : son caractère de coupure (ou la fin du texte) */
static WORD nav_line_end(WORD p)
{
    while (p < nav.len && !nav_brk(nav_text[p]))
        p++;
    return p;
}

/* Position valide : un caractère de la ligne, ou la coupure d'une ligne vide */
static WORD nav_fix(WORD p)
{
    if (!nav.len)
        return 0;
    if (p >= nav.len)
        p = nav.len - 1;
    while (p > 0 && nav_brk(nav_text[p]) && nav_line_start(p) != p)
        p--;
    return p;
}

static WORD nav_next_line(WORD p)
{
    WORD e = nav_line_end(p);
    return e + 1 < nav.len ? e + 1 : NAV_NONE;
}

static WORD nav_prev_line(WORD p)
{
    WORD s = nav_line_start(p);
    return s ? nav_line_start(s - 1) : NAV_NONE;
}

/* premier élément commencé en p ou après (éléments dans l'ordre de leur début) */
static int nav_first(WORD p)
{
    int lo = 0, hi = nav.n;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (nav_items[m].start < p)
            lo = m + 1;
        else
            hi = m;
    }
    return lo;
}

static BOOL nav_contains(const NAVITEM *it, WORD p)
{
    return it->start <= p && (p < it->end || (it->start == it->end && p == it->start));
}

static BOOL nav_layout(WORD i)
{
    return i != NAV_NONE && i < nav.n && nav_items[i].kind == 't' && !nav_items[i].level;
}

/* élément le plus intérieur, de l'un des genres donnés, qui contient p */
static WORD nav_inner(WORD p, const char *kinds)
{
    int i;
    for (i = nav_first(p + 1) - 1; i >= 0; i--)
        if (nav_is(nav_items[i].kind, kinds) && nav_contains(&nav_items[i], p))
            return (WORD) i;
    return NAV_NONE;
}

/* cellule d'un tableau de données */
static WORD nav_cell(WORD p)
{
    WORD c = nav_inner(p, "c");
    if (c == NAV_NONE || nav_items[c].parent >= nav.n || nav_layout(nav_items[c].parent))
        return NAV_NONE;
    return c;
}

/* conteneurs annoncés qui contiennent p, du plus intérieur au plus extérieur */
static int nav_chain(WORD p, WORD *out)
{
    int n = 0;
    WORD i = nav_inner(p, "ltqdFmoic");
    while (i != NAV_NONE && i < nav.n && n < NAV_CHAIN) {
        if (nav_is(nav_items[i].kind, "ltqdFmo") && !nav_layout(i))
            out[n++] = i;
        i = nav_items[i].parent;
    }
    return n;
}

/* ------------------------------------------------- réception du tampon */

void nav_begin(void)
{
    nav.loaded = FALSE;
    EpNavMode(NULL, 0);
    nav.pstate = nav_alloc() ? 0 : 9;
    nav.pok = FALSE;
    nav.plen = 0;
    nav.len = nav.n = nav.nstr = 0;
    nav.title[0] = 0;
}

static DWORD nav_num(const char **p)
{
    DWORD v = 0;
    while (**p == ' ')
        (*p)++;
    while (**p >= '0' && **p <= '9')
        v = v * 10 + (*(*p)++ - '0');
    return v;
}

static void nav_parse(void)
{
    char *l = nav.pline;
    const char *p;
    NAVITEM *it;
    int k;

    if (nav.pstate == 0) {
        if (!lstrcmp(l, "PC486-TAMPON 1"))
            nav.pok = TRUE;
        else if (!_fstrncmp(l, "TITRE ", 6))
            lstrcpyn(nav.title, l + 6, sizeof(nav.title));
        else if (!_fstrncmp(l, "TEXTE ", 6)) {
            DWORD n;
            p = l + 6;
            n = nav_num(&p);
            if (!nav.pok || n > NAV_TEXT)
                nav.pstate = 9;
            else {
                nav.premain = (WORD) n;
                nav.pstate = n ? 1 : 2;
            }
        }
        return;
    }
    if (nav.pstate != 2 || !l[0] || !_fstrncmp(l, "ELEMENTS", 8))
        return;
    if (!lstrcmp(l, "FIN")) {
        nav.pstate = 3;
        return;
    }
    if (nav.n >= NAV_ITEMS || l[1] != ' ')
        return;
    it = &nav_items[nav.n];
    p = l + 1;
    it->kind = l[0];
    it->level = (BYTE) nav_num(&p);
    it->start = (WORD) nav_num(&p);
    it->end = (WORD) nav_num(&p);
    it->parent = (WORD) nav_num(&p);
    it->a = (WORD) nav_num(&p);
    it->b = (WORD) nav_num(&p);
    it->c = (WORD) nav_num(&p);
    it->d = (WORD) nav_num(&p);
    if (*p == ' ')
        p++;
    if (it->end > nav.len)
        it->end = nav.len;
    if (it->start > it->end)
        it->start = it->end;
    it->s = NAV_NONE;
    k = lstrlen(p);
    if (k && (DWORD) nav.nstr + k + 1 <= NAV_STR) {
        it->s = nav.nstr;
        memcpy(nav_str + nav.nstr, p, k + 1);
        nav.nstr += k + 1;
    }
    nav.n++;
}

/* Corps de la réponse du proxy, morceau par morceau */
void nav_feed(const char *p, int n)
{
    while (n > 0 && nav.pstate < 3) {
        if (nav.pstate == 1) {
            WORD k = n < (int) nav.premain ? (WORD) n : nav.premain;
            memcpy(nav_text + nav.len, p, k);
            nav.len += k;
            nav.premain -= k;
            p += k;
            n -= k;
            if (!nav.premain) {
                nav_text[nav.len] = 0;
                nav.pstate = 2;
            }
            continue;
        }
        if (*p == '\n') {
            nav.pline[nav.plen] = 0;
            nav_parse();
            nav.plen = 0;
        } else if (*p != '\r' && nav.plen < (int) sizeof(nav.pline) - 1)
            nav.pline[nav.plen++] = *p;
        p++;
        n--;
    }
}

/* ------------------------------------------------------------- champs */

static BOOL nav_field_kind(char k)
{
    return nav_is(k, "exrbCL");
}

static BOOL nav_class_ok(HWND h, char k)
{
    int st = (int) (GetWindowLong(h, GWL_STYLE) & 0x0F);
    if (k == 'e')
        return is_class(h, "EDIT");
    if (k == 'C')
        return is_class(h, "COMBOBOX");
    if (k == 'L')
        return is_class(h, "LISTBOX");
    if (!is_class(h, "BUTTON"))
        return FALSE;
    if (k == 'x')
        return st == BS_CHECKBOX || st == BS_AUTOCHECKBOX || st == BS_3STATE || st == BS_AUTO3STATE;
    if (k == 'r')
        return st == BS_RADIOBUTTON || st == BS_AUTORADIOBUTTON;
    return st == BS_PUSHBUTTON || st == BS_DEFPUSHBUTTON || st == BS_OWNERDRAW;  /* Communicator */
}

/* Fenêtres des champs : filles de la vue, triées par identifiant (ordre de création,
   celui du document). Valides seulement si leur nombre et leurs classes suivent les
   champs du tampon. */
static void nav_map(void)
{
    HWND c;
    int i, j, n = 0, want = 0;
    nav.mapped = FALSE;
    nav.nfields = 0;
    if (!nav.view || !IsWindow(nav.view))
        return;
    for (c = GetWindow(nav.view, GW_CHILD); c && n < NAV_FIELDS; c = GetWindow(c, GW_HWNDNEXT))
        if (is_class(c, "EDIT") || is_class(c, "BUTTON") || is_class(c, "COMBOBOX") ||
            is_class(c, "LISTBOX"))
            nav.fields[n++] = c;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && (WORD) GetWindowWord(nav.fields[j - 1], GWW_ID) >
                             (WORD) GetWindowWord(nav.fields[j], GWW_ID); j--) {
            c = nav.fields[j];
            nav.fields[j] = nav.fields[j - 1];
            nav.fields[j - 1] = c;
        }
    nav.nfields = n;
    for (i = 0; i < nav.n; i++) {
        NAVITEM *it = &nav_items[i];
        if (!nav_field_kind(it->kind) || it->a == NAV_NONE)
            continue;
        if (it->a >= n || !nav_class_ok(nav.fields[it->a], it->kind))
            return;
        if (it->a + 1 > want)
            want = it->a + 1;
    }
    nav.mapped = want == n;
}

static HWND nav_hwnd(WORD i)
{
    NAVITEM *it = &nav_items[i];
    if (!nav_field_kind(it->kind) || it->a == NAV_NONE)
        return NULL;
    if (!nav.mapped || it->a >= nav.nfields || !IsWindow(nav.fields[it->a]))
        nav_map();
    if (!nav.mapped || it->a >= nav.nfields)
        return NULL;
    return nav.fields[it->a];
}

/* élément du tampon d'une fenêtre de champ */
static WORD nav_item_of(HWND h)
{
    int i, k;
    if (!nav.mapped)
        nav_map();
    for (k = 0; k < nav.nfields; k++)
        if (nav.fields[k] == h)
            for (i = 0; i < nav.n; i++)
                if (nav_field_kind(nav_items[i].kind) && nav_items[i].a == (WORD) k)
                    return (WORD) i;
    return NAV_NONE;
}

static void nav_span(WORD i, char *out, int max)
{
    NAVITEM *it = &nav_items[i];
    int k = 0;
    WORD p;
    for (p = it->start; p < it->end && k < max - 1; p++)
        out[k++] = nav_brk(nav_text[p]) ? ' ' : nav_text[p];
    out[k] = 0;
    while (k && out[k - 1] == ' ')
        out[--k] = 0;
    for (k = 0; out[k] == ' '; k++)
        ;
    if (k)
        lstrcpy(out, out + k);
}

/* Valeur d'une zone d'édition : Netscape ne la donne pas (WM_GETTEXT vide). Lue dans
   le modèle hors écran quand elle est affichée, gardée pour la suite ; sinon valeur
   du tampon si la longueur correspond. */
static void nav_edit_value(WORD i, HWND h, char *v, int max)
{
    NAVITEM *it = &nav_items[i];
    int k, len, free_k = -1;
    char init[48];
    nav_span(i, init, sizeof(init));
    v[0] = 0;
    if (!h) {
        lstrcpyn(v, init, max);
        return;
    }
    len = (int) SendMessage(h, WM_GETTEXTLENGTH, 0, 0L);
    for (k = 0; k < NAV_EDITS; k++)
        if (nav_edits[k].a == it->a + 1)
            break;
        else if (!nav_edits[k].a && free_k < 0)
            free_k = k;
    if (IsWindowVisible(h)) {
        RECT r;
        char buf[TXT];
        GetWindowRect(h, &r);
        /* après une saisie, Netscape ne retrace que la fin du texte : le modèle peut
           n'en garder qu'un morceau ; longueur différente, faire repeindre le champ
           (un texte plus long que le champ reste vu en partie) */
        if (EpOsmLine(h, (r.top + r.bottom) / 2, buf, sizeof(buf)) != len ||
            lstrlen(buf) != len) {
            InvalidateRect(h, NULL, TRUE);
            UpdateWindow(h);
            EpOsmLine(h, (r.top + r.bottom) / 2, buf, sizeof(buf));
        }
        if (buf[0]) {
            if (k == NAV_EDITS)
                k = free_k;
            if (k >= 0) {
                nav_edits[k].a = it->a + 1;
                lstrcpyn(nav_edits[k].v, buf, sizeof(nav_edits[k].v));
            }
            lstrcpyn(v, buf, max);
            return;
        }
    }
    if (!len)
        return;
    if (k < NAV_EDITS && k >= 0 && nav_edits[k].a == it->a + 1 &&
        lstrlen(nav_edits[k].v) == len)
        lstrcpyn(v, nav_edits[k].v, max);
    else if (lstrlen(init) == len)
        lstrcpyn(v, init, max);
    else
        wsprintf(v, "%d caractères", len);
}

static void nav_put(const char *s, WORD at)
{
    while (*s && nav_n < NAV_OUT - 1) {
        nav_at[nav_n] = at;
        nav_out[nav_n++] = *s++;
    }
    nav_out[nav_n] = 0;
}

static void nav_word(const char *s, WORD at)
{
    if (!*s)
        return;
    if (nav_n && nav_out[nav_n - 1] != ' ')
        nav_put(" ", at);
    nav_put(s, at);
    nav_gap = TRUE;
}

/* Champ, avec son état réel (fenêtre de Netscape) */
static void nav_field(WORD i, WORD at)
{
    NAVITEM *it = &nav_items[i];
    HWND h = nav_hwnd(i);
    char v[80];
    int st;
    switch (it->kind) {
    case 'e':
        if (it->level == 2) {
            nav_word(nav_br ? "mdp" : "mot de passe", at);
            if (h && !SendMessage(h, WM_GETTEXTLENGTH, 0, 0L))
                nav_word(nav_br ? "" : "vide", at);
            return;
        }
        nav_word(nav_br ? "ed" : it->level == 3 ? "zone d'édition multiligne" : "zone d'édition", at);
        nav_edit_value(i, h, v, sizeof(v));
        nav_word(v[0] ? v : nav_br ? "" : "vide", at);
        return;
    case 'x':
    case 'r':
        st = h ? (int) SendMessage(h, BM_GETCHECK, 0, 0L) : it->b;
        if (nav_br)
            nav_word(it->kind == 'x' ? (st ? "[x]" : "[ ]") : (st ? "(x)" : "( )"), at);
        else if (it->kind == 'x')
            nav_word(st ? "case à cocher cochée" : "case à cocher non cochée", at);
        else
            nav_word(st ? "bouton radio coché" : "bouton radio non coché", at);
        return;
    case 'b':
        nav_word(nav_br ? "btn" : "bouton", at);
        nav_span(i, v, sizeof(v));
        nav_word(v, at);
        return;
    case 'C':
    case 'L':
        nav_word(nav_br ? (it->kind == 'C' ? "cbo" : "lst") :
                 it->kind == 'C' ? "liste déroulante" : "liste", at);
        v[0] = 0;
        if (h) {
            BOOL combo = it->kind == 'C';
            long st = GetWindowLong(h, GWL_STYLE);
            int sel = (int) SendMessage(h, combo ? CB_GETCURSEL : LB_GETCURSEL, 0, 0L);
            /* liste dessinée par Communicator, sans textes (CBS_HASSTRINGS absent) :
               CB_GETLBTEXT rend une donnée. Ce qu'elle affiche, sinon le choix du
               tampon s'il n'a pas changé, sinon son rang. */
            if ((st & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) && !(st & CBS_HASSTRINGS)) {
                if (!IsWindowVisible(h) || EpOsmWindow(h, v, sizeof(v)) <= 0) {
                    if (sel == (int) it->c)
                        nav_span(i, v, sizeof(v));
                    else if (sel >= 0)
                        wsprintf(v, "élément %d sur %u", sel + 1, it->b);
                }
            } else if (sel >= 0 &&
                       SendMessage(h, combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN, sel, 0L) <
                       (LRESULT) sizeof(v))
                SendMessage(h, combo ? CB_GETLBTEXT : LB_GETTEXT, sel, (LPARAM) (LPSTR) v);
        } else
            nav_span(i, v, sizeof(v));
        nav_word(v, at);
        return;
    }
}

/* ------------------------------------------------------------- annonces */

static void nav_enter(WORD i, WORD at)
{
    NAVITEM *it = &nav_items[i];
    char t[80];
    if (nav_br)
        return;
    switch (it->kind) {
    case 'l':
        wsprintf(t, it->a == 1 ? "liste de %u élément," : "liste de %u éléments,", it->a);
        break;
    case 't':
        wsprintf(t, "tableau de %u %s et %u %s,", it->a, it->a > 1 ? "lignes" : "ligne",
                 it->b, it->b > 1 ? "colonnes" : "colonne");
        break;
    case 'q': lstrcpy(t, "citation,"); break;
    case 'd': wsprintf(t, "région %s,", (LPSTR) nav_s(it)); break;
    case 'F': lstrcpy(t, "formulaire,"); break;
    case 'm': lstrcpy(t, "cadre,"); break;
    case 'o': lstrcpy(t, "objet,"); break;
    default: return;
    }
    nav_word(t, at);
}

static void nav_leave(WORD i, WORD at)
{
    static const char kinds[] = "ltqdFmo";
    static const char *const names[] = { "fin de liste,", "fin de tableau,",
        "fin de citation,", "fin de région,", "fin de formulaire,", "fin de cadre,",
        "fin d'objet," };
    const char *k = strchr(kinds, nav_items[i].kind);
    if (k && !nav_br)
        nav_word(names[k - kinds], at);
}

static BOOL nav_in(WORD i, const WORD *a, int n)
{
    while (n-- > 0)
        if (a[n] == i)
            return TRUE;
    return FALSE;
}

/* conteneurs quittés puis entrés en allant de prev à p */
static void nav_moves(WORD prev, WORD p)
{
    WORD a[NAV_CHAIN], b[NAV_CHAIN];
    int na = nav_chain(prev, a), nb = nav_chain(p, b), i;
    for (i = 0; i < na; i++)
        if (!nav_in(a[i], b, nb))
            nav_leave(a[i], p);
    for (i = nb - 1; i >= 0; i--)
        if (!nav_in(b[i], a, na))
            nav_enter(b[i], p);
}

/* Cellule d'un tableau de données atteinte depuis prev : ligne et colonne qui changent */
static void nav_cell_moves(WORD prev, WORD p)
{
    WORD c = nav_cell(p), o = prev == NAV_NONE ? NAV_NONE : nav_cell(prev);
    NAVITEM *it, *old;
    BOOL same;
    char t[40];
    if (c == NAV_NONE || c == o || nav_br)
        return;
    it = &nav_items[c];
    old = o == NAV_NONE ? NULL : &nav_items[o];
    same = old && old->parent == it->parent;
    if (!same || old->a != it->a) {
        wsprintf(t, "ligne %u", it->a);
        nav_word(t, p);
    }
    if (!same || old->b != it->b) {
        wsprintf(t, "colonne %u", it->b);
        nav_word(t, p);
    }
    if (it->d > 1) {
        wsprintf(t, "sur %u colonnes", it->d);
        nav_word(t, p);
    }
    if (it->c > 1) {
        wsprintf(t, "sur %u lignes", it->c);
        nav_word(t, p);
    }
}

/* Préfixe d'un élément qui commence en at ; renvoie la fin d'un élément décrit à la
   place de son texte (champ, image, séparateur), sinon at. */
static WORD nav_prefix(WORD i, WORD at)
{
    NAVITEM *it = &nav_items[i];
    char t[40];
    switch (it->kind) {
    case 'h':
        wsprintf(t, nav_br ? "t%u" : "titre niveau %u", it->level);
        nav_word(t, at);
        return at;
    case 'a': {
        const char *u = nav_s(it);
        if (nav_br)
            nav_word(it->a ? "lnv" : "ln", at);
        else
            nav_word(u[0] == '#' ? "lien dans la page" : it->a ? "lien visité" : "lien", at);
        return at;
    }
    case 'g': {
        char alt[TXT];
        nav_span(i, alt, sizeof(alt));
        if (alt[0] == '[' && alt[lstrlen(alt) - 1] == ']') {
            alt[lstrlen(alt) - 1] = 0;
            lstrcpy(alt, alt + 1);
        }
        nav_word(nav_br ? "img" : "image", at);
        nav_word(alt, at);
        return it->end;
    }
    case 's':
        nav_word(nav_br ? "---" : "séparateur", at);
        return it->end;
    default:
        if (nav_field_kind(it->kind)) {
            nav_field(i, at);
            return it->end > at ? it->end : at + 1;
        }
        return at;
    }
}

/* Texte de [from, to[, avec ses éléments ; prev : position d'où l'on vient (entrées
   et sorties de conteneurs, cellule), NAV_NONE pour ne pas les dire. braille :
   abréviations, nav_at[] donne la position de chaque caractère. */
static void nav_compose(WORD from, WORD to, WORD prev, BOOL braille)
{
    int i;
    WORD p, e;
    BOOL text = FALSE;

    nav_n = 0;
    nav_out[0] = 0;
    nav_br = braille;
    nav_gap = FALSE;
    if (prev != NAV_NONE) {
        nav_moves(prev, from);
        nav_cell_moves(prev, from);
    }
    /* titre ou lien commencé sur une ligne précédente */
    for (i = nav_first(from) - 1; i >= 0 && i >= nav_first(from) - 40; i--)
        if (nav_is(nav_items[i].kind, "ha") && nav_items[i].start < from &&
            nav_items[i].end > from) {
            nav_prefix((WORD) i, from);
            break;
        }
    if (from == to)                     /* ligne vide : séparateur ? */
        for (i = nav_first(from); i < nav.n && nav_items[i].start == from; i++)
            if (nav_items[i].kind == 's') {
                nav_prefix((WORD) i, from);
                text = TRUE;
            }
    i = nav_first(from);
    p = from;
    while (p < to) {
        BOOL moved = FALSE;
        for (; i < nav.n && nav_items[i].start <= p; i++)
            if (nav_items[i].start == p) {
                e = nav_prefix((WORD) i, p);
                if (e > p) {
                    p = e < to ? e : to;
                    text = moved = TRUE;
                }
            }
        if (moved)
            continue;
        if (nav_text[p] == '\x95') {
            nav_word(braille ? "*" : "puce", p);
        } else {
            char c[2];
            c[0] = nav_brk(nav_text[p]) ? ' ' : nav_text[p];
            c[1] = 0;
            if (nav_gap && c[0] != ' ' && nav_n && nav_out[nav_n - 1] != ' ')
                nav_put(" ", p);
            nav_gap = FALSE;
            nav_put(c, p);
            if (c[0] != ' ')
                text = TRUE;
        }
        p++;
    }
    if (!text && !braille)
        nav_word("vide", from);
    while (nav_n && nav_out[nav_n - 1] == ' ')
        nav_out[--nav_n] = 0;
    if (!braille && nav_out[0] == ' ') {
        int k = 0;
        while (nav_out[k] == ' ')
            k++;
        memmove(nav_out, nav_out + k, nav_n - k + 1);
        memmove(nav_at, nav_at + k, (nav_n - k) * sizeof(WORD));
        nav_n -= k;
    }
    if (!nav_n)
        nav_at[0] = from;
}

static void nav_say_range(WORD from, WORD to, WORD prev)
{
    nav_compose(from, to, prev, FALSE);
    say(nav_out, TRUE);
}

static void nav_say_line(WORD prev)
{
    WORD s = nav_line_start(nav.pos);
    nav_say_range(s, nav_line_end(s), prev);
}

static void nav_say_char(WORD prev)
{
    WORD f = nav_inner(nav.pos, "exrbCLg");
    char c = nav_text[nav.pos];
    if (prev != NAV_NONE) {
        nav_n = 0;
        nav_out[0] = 0;
        nav_br = FALSE;
        nav_moves(prev, nav.pos);
        nav_cell_moves(prev, nav.pos);
        if (nav_n)
            say(nav_out, TRUE);
    }
    if (f != NAV_NONE) {
        nav_compose(nav_items[f].start, nav_items[f].end, NAV_NONE, FALSE);
        say(nav_out, prev == NAV_NONE || !nav_n);
    } else if (nav_brk(c))
        say("vide", prev == NAV_NONE);
    else if (c == '\x95')
        say("puce", prev == NAV_NONE);
    else
        say_char(c, prev == NAV_NONE);
}

/* ------------------------------------------------------------- état */

static BOOL nav_inside(HWND f)
{
    return nav.view && IsWindow(nav.view) && f && (f == nav.view || IsChild(nav.view, f));
}

/* Mode navigation en cours dans la page qui a le focus */
BOOL nav_here(void)
{
    return nav.loaded && nav.mode == 0 && nav_inside(GetFocus());
}

static void nav_hook(void)
{
    if (!nav.loaded)
        EpNavMode(NULL, 0);
    else
        EpNavMode(nav.view, nav.mode == 0 ? 1 : nav.mode == 2 ? 2 : 0);
}

static void nav_unload(void)
{
    if (sa.on && sa.kind == 4)
        sa_stop(TRUE);
    nav.loaded = FALSE;
    nav.mode = 0;
    nav.mapped = FALSE;
    nav.ttable = NAV_NONE;
    nav.br_pos = NAV_NONE;
    memset(nav_edits, 0, sizeof(nav_edits));
    EpNavMode(NULL, 0);
}

static void nav_sync(void)
{
    WORD i = nav_item_of(GetFocus());
    if (i != NAV_NONE)
        nav.pos = nav_fix(nav_items[i].start);
}

static void nav_set_mode(int mode)
{
    if (mode == 0 && nav.mode != 0)
        nav_sync();             /* le curseur virtuel rejoint le champ quitté */
    nav.mode = mode;
    nav_hook();
}

/* --------------------------------------------------------- chargement */

static BOOL nav_words_in(const char *row)
{
    /* un des mots d'au moins cinq lettres de la rangée visible est dans le tampon ;
       les autres peuvent venir d'un champ (texte saisi, remis par un retour arrière) */
    char w[32];
    int k = 0;
    BOOL seen = FALSE;
    for (;; row++) {
        if (*row && IsCharAlpha(*row)) {
            if (k < (int) sizeof(w) - 1)
                w[k++] = *row;
            continue;
        }
        if (k >= 5) {
            WORD p;
            w[k] = 0;
            seen = TRUE;
            for (p = 0; p + k <= nav.len; p++)
                if (nav_text[p] == w[0] && !_fstrncmp(nav_text + p, w, k))
                    return TRUE;
        }
        k = 0;
        if (!*row)
            return !seen;       /* rien de vérifiable, ou aucun mot retrouvé */
    }
}

/* Rangées visibles retrouvées dans le tampon ? Deux rangées sans aucun mot connu :
   la page affichée n'est sans doute pas celle du tampon (un champ seul sur sa rangée
   peut ne contenir que du texte saisi) */
static BOOL nav_matches(void)
{
    RECT r;
    POINT pt;
    char row[TXT * 2];
    int col, k, bad = 0;
    if (!doc_area(nav.view, TRUE, &r))
        return TRUE;
    pt.x = r.left + 2;
    pt.y = r.top - 1;
    for (k = 0; k < 4; k++) {
        if (!EpOsmReviewLine(nav.view, &pt, 1, row, sizeof(row), &col) || pt.y >= r.bottom)
            break;
        if (!nav_words_in(row))
            bad++;
    }
    return bad < 2;
}

/* Le titre du tampon dans celui de la fenêtre (« Netscape - [titre] ») */
static BOOL nav_title_ok(void)
{
    char t[24];
    int n = lstrlen(nav.title), k;
    if (!n || !nav.dtitle[0])
        return TRUE;
    lstrcpyn(t, nav.title, sizeof(t));
    n = lstrlen(t);
    for (k = 0; nav.dtitle[k]; k++)
        if (!_fstrncmp(nav.dtitle + k, t, n))
            return TRUE;
    return FALSE;
}

static void nav_fallback(void)
{
    /* pas de tampon : lecture d'office comme avant (texte dessiné) */
    if (nav.autoread && (long) (key_tick - nav.auto_tick) < 0 && GetFocus() == nav.view)
        sa_start();
    nav.autoread = FALSE;
}

/* Fin de la réponse du proxy (navnet.c) */
void nav_done(BOOL ok)
{
    nav.loading = FALSE;
    if (!ok || nav.pstate != 3 || !nav.len || !nav_title_ok()) {
        nav.len = nav.n = 0;
        nav_fallback();
        return;
    }
    nav.loaded = TRUE;
    nav.mode = 0;
    nav.pos = nav.keep ? nav_fix(nav.keep_pos) : 0;
    nav.keep = FALSE;
    nav.ttable = NAV_NONE;
    nav.br_pos = NAV_NONE;
    nav_map();
    nav_hook();
    if (!nav_matches() && !nav.warned) {
        /* dit seul : pas de lecture d'office qui le couperait */
        nav.warned = TRUE;
        nav.autoread = FALSE;
        say("Attention : la structure lue peut différer de la page affichée.", FALSE);
    }
    if (nav.autoread && (long) (key_tick - nav.auto_tick) < 0 && GetFocus() == nav.view) {
        nav.pos = 0;
        sa_start();
    }
    nav.autoread = FALSE;
}

static void nav_request(void)
{
    if (!net_page_info(nav.url, sizeof(nav.url), nav.dtitle, sizeof(nav.dtitle)) ||
        !net_fetch(nav.url)) {
        nav_fallback();
        return;
    }
    nav.loading = TRUE;
}

/* Scrutation : page de Netscape active, changée ? */
void nav_poll(void)
{
    HWND act = GetActiveWindow(), view = page_of(act);
    char t[TXT];
    DWORD s;

    if (nav.loaded && !IsWindow(nav.view))
        nav_unload();
    if (!view || !net_ready())
        return;
    GetWindowText(act, t, sizeof(t));
    if (view != nav.view || lstrcmp(t, nav.seen) != 0) {
        lstrcpy(nav.seen, t);
        nav.view = view;
        nav.frame = act;
        nav_unload();
        nav.keep = FALSE;
        nav.warned = FALSE;
        nav.want = TRUE;
        nav.sig = EpOsmSignature(view);
        nav.sig_tick = GetTickCount();
    }
    if (nav.want && !nav.loading && !net_busy()) {
        s = EpOsmSignature(view);
        if (s != nav.sig) {
            nav.sig = s;
            nav.sig_tick = GetTickCount();
        } else if (GetTickCount() - nav.sig_tick > NAV_STABLE) {
            nav.want = FALSE;
            nav_request();
        }
    }
}

/* Lecture d'office d'une nouvelle page (status_poll) : par le tampon si possible.
   FALSE : pas de réseau, lire le texte dessiné. */
BOOL nav_autoread(HWND page)
{
    if (!net_ready() || page != nav.view)
        return FALSE;
    if (nav.loaded) {
        nav.pos = 0;
        sa_start();
        return TRUE;
    }
    if (!nav.want && !nav.loading)
        return FALSE;           /* (structure déjà refusée pour cette page) */
    nav.autoread = TRUE;
    nav.auto_tick = GetTickCount();
    return TRUE;
}

/* Même page à relire (Ctrl+R, envoi d'un formulaire...) */
static void nav_refresh(void)
{
    if (!nav.view)
        return;
    nav.keep = nav.loaded;
    nav.keep_pos = nav.pos;
    nav.want = TRUE;
    nav.sig = 0;
    nav.sig_tick = GetTickCount();
}

/* Touche transmise à Netscape (EPM_KEY) */
void nav_key_seen(WPARAM vk, LPARAM lp)
{
    if (!nav.view || page_of(GetActiveWindow()) != nav.view)
        return;
    if ((vk == 'R' && (lp & EPK_CTRL)) || vk == VK_F5 ||
        (vk == VK_RETURN && nav.mode != 0 && nav_inside(GetFocus())))
        nav_refresh();
}

/* ------------------------------------------------------- déplacements */

static void nav_go_line(int dir)
{
    WORD old = nav.pos, p = dir > 0 ? nav_next_line(nav.pos) : nav_prev_line(nav.pos);
    if (p == NAV_NONE) {
        say(dir > 0 ? "bas du document" : "haut du document", TRUE);
        return;
    }
    nav.pos = p;
    nav_say_line(old);
}

static void nav_go_page(int dir)
{
    WORD old = nav.pos, p = nav.pos, q;
    int k;
    for (k = 0; k < NAV_PAGE; k++) {
        q = dir > 0 ? nav_next_line(p) : nav_prev_line(p);
        if (q == NAV_NONE)
            break;
        p = q;
    }
    if (!k) {
        say(dir > 0 ? "bas du document" : "haut du document", TRUE);
        return;
    }
    nav.pos = p;
    nav_say_line(old);
}

static void nav_go_char(int dir)
{
    WORD old = nav.pos, p = nav.pos, f = nav_inner(p, "exrbCLg");
    if (dir > 0) {
        if (f != NAV_NONE && nav_items[f].end > p + 1)
            p = nav_items[f].end - 1;   /* un champ compte pour un caractère */
        if (p + 1 >= nav.len) {
            say("bas du document", TRUE);
            return;
        }
        p++;
        while (p < nav.len - 1 && nav_brk(nav_text[p]) && nav_line_start(p) != p)
            p++;
    } else {
        if (f != NAV_NONE)
            p = nav_items[f].start;
        if (!p) {
            say("haut du document", TRUE);
            return;
        }
        p--;
        while (p > 0 && nav_brk(nav_text[p]) && nav_line_start(p) != p)
            p--;
        f = nav_inner(p, "exrbCLg");
        if (f != NAV_NONE)
            p = nav_items[f].start;
    }
    nav.pos = p;
    nav_say_char(nav_line_start(p) != nav_line_start(old) ? old : NAV_NONE);
}

static BOOL nav_space(char c)
{
    return c == ' ' || nav_brk(c);
}

static void nav_say_word(BOOL cut)
{
    WORD p = nav.pos, e = p;
    WORD f = nav_inner(p, "exrbCLg");
    if (f != NAV_NONE) {
        nav_compose(nav_items[f].start, nav_items[f].end, NAV_NONE, FALSE);
        say(nav_out, cut);
        return;
    }
    if (nav_brk(nav_text[p])) {
        say("vide", cut);
        return;
    }
    nav_n = 0;
    while (e < nav.len && !nav_space(nav_text[e]) && nav_n < 60) {
        nav_out[nav_n++] = nav_text[e] == '\x95' ? '-' : nav_text[e];
        e++;
    }
    nav_out[nav_n] = 0;
    if (nav_n == 1 && nav_out[0] == '-' && nav_text[p] == '\x95')
        lstrcpy(nav_out, "puce");
    say(nav_out, cut);
}

static void nav_go_word(int dir)
{
    WORD p = nav.pos, old = nav.pos;
    if (dir > 0) {
        while (p < nav.len && !nav_space(nav_text[p]))
            p++;
        while (p < nav.len && nav_space(nav_text[p]) && !(nav_brk(nav_text[p]) &&
               nav_line_start(p) == p && p != old))
            p++;
        if (p >= nav.len) {
            say("bas du document", TRUE);
            return;
        }
    } else {
        if (!p) {
            say("haut du document", TRUE);
            return;
        }
        p--;
        while (p > 0 && nav_space(nav_text[p]) && !(nav_brk(nav_text[p]) && nav_line_start(p) == p))
            p--;
        while (p > 0 && !nav_space(nav_text[p - 1]))
            p--;
    }
    nav.pos = nav_fix(p);
    nav_n = 0;
    nav_out[0] = 0;
    if (nav_line_start(nav.pos) != nav_line_start(old)) {
        nav_br = FALSE;
        nav_moves(old, nav.pos);
        nav_cell_moves(old, nav.pos);
        if (nav_n)
            say(nav_out, TRUE);
    }
    nav_say_word(!nav_n);
}

/* Paragraphe : texte entre deux coupures de bloc (LF), les lignes coupées (VT) comprises */
static void nav_go_para(int dir)
{
    WORD old = nav.pos, p = nav.pos, e;
    if (dir > 0) {
        while (p < nav.len && nav_text[p] != '\n')
            p++;
        if (p + 1 >= nav.len) {
            say("bas du document", TRUE);
            return;
        }
        p++;
    } else {
        while (p > 0 && nav_text[p - 1] != '\n')
            p--;
        if (!p) {
            say("haut du document", TRUE);
            return;
        }
        p--;
        while (p > 0 && nav_text[p - 1] != '\n')
            p--;
    }
    nav.pos = p;
    for (e = p; e < nav.len && nav_text[e] != '\n' && e - p < 400; e++)
        ;
    nav_say_range(p, e, old);
}

/* ------------------------------------------------- navigation rapide */

typedef struct {
    char key;
    const char *kinds;
    const char *name;
    BOOL fem;
} NAVKEY;

static const NAVKEY nav_keys[] = {
    {'H', "h", "titre", FALSE}, {'T', "t", "tableau", FALSE}, {'K', "a", "lien", FALSE},
    {'L', "l", "liste", TRUE}, {'I', "i", "élément de liste", FALSE},
    {'F', "exrbCL", "champ de formulaire", FALSE}, {'E', "e", "zone d'édition", TRUE},
    {'B', "b", "bouton", FALSE}, {'X', "x", "case à cocher", TRUE},
    {'C', "CL", "liste déroulante", TRUE}, {'R', "r", "bouton radio", FALSE},
    {'U', "a", "lien non visité", FALSE}, {'V', "a", "lien visité", FALSE},
    {'N', "", "texte hors lien", FALSE}, {'Q', "q", "citation", TRUE},
    {'S', "s", "séparateur", FALSE}, {'G', "g", "image", TRUE}, {'D', "d", "région", TRUE},
    {'M', "m", "cadre", FALSE}, {'O', "o", "objet incorporé", FALSE},
    {'P', "p", "paragraphe", FALSE}, {0, 0, 0, FALSE}
};

static BOOL nav_blank(WORD i)
{
    WORD p;
    for (p = nav_items[i].start; p < nav_items[i].end; p++)
        if (!nav_space(nav_text[p]))
            return FALSE;
    return TRUE;
}

static BOOL nav_match(WORD i, char key, int level)
{
    NAVITEM *it = &nav_items[i];
    switch (key) {
    case 'H': return it->kind == 'h' && (!level || it->level == level);
    case 'T': return it->kind == 't' && it->level;
    case 'U': return it->kind == 'a' && !it->a;
    case 'V': return it->kind == 'a' && it->a;
    case 'P': return it->kind == 'p' && !nav_blank(i);
    }
    return TRUE;
}

/* Élément trouvé : y aller et le dire (conteneurs entrés, libellé d'un champ, élément) */
static void nav_say_item(WORD i, WORD old)
{
    static char buf[NAV_OUT];
    NAVITEM *it = &nav_items[i];
    WORD e = it->end;
    nav.pos = nav_fix(it->start);
    if (nav_is(it->kind, "hag") || nav_field_kind(it->kind)) {
        if (e > it->start + 300)
            e = it->start + 300;
    } else
        e = nav_line_end(it->start);
    nav_n = 0;
    nav_out[0] = 0;
    nav_br = FALSE;
    nav_moves(old, it->start);
    nav_cell_moves(old, it->start);
    {
        /* conteneur cherché (D, L, T...) depuis l'intérieur : le nommer quand même */
        WORD c[NAV_CHAIN];
        int n = nav_chain(old, c);
        if (nav_is(it->kind, "ltqdFmo") && nav_in(i, c, n))
            nav_enter(i, it->start);
    }
    lstrcpy(buf, nav_out);
    if (buf[0] && buf[lstrlen(buf) - 1] == ',')
        buf[lstrlen(buf) - 1] = 0;      /* (append_n met la virgule) */
    if (nav_field_kind(it->kind) && it->kind != 'b')
        append_n(buf, nav_s(it), sizeof(buf));
    nav_compose(it->start, e, NAV_NONE, FALSE);
    append_n(buf, nav_out, sizeof(buf));
    say(buf, TRUE);
}

/* Après des liens, le premier texte qui n'en est pas un (N) */
static void nav_not_link(BOOL back)
{
    WORD p, found = NAV_NONE, old = nav.pos;
    int j = 0;
    BOOL after = FALSE;
    for (p = 0; p < nav.len; p++) {
        while (j < nav.n && (nav_items[j].kind != 'a' || nav_items[j].end <= p))
            j++;
        if (j < nav.n && nav_items[j].start <= p) {
            after = TRUE;
            continue;
        }
        if (nav_space(nav_text[p]) || nav_text[p] == '\x95')
            continue;
        if (after) {
            after = FALSE;
            if (back ? p < old : p > old) {
                found = p;
                if (!back)
                    break;
            }
        }
    }
    if (found == NAV_NONE) {
        say(back ? "pas de texte hors lien précédent" : "pas de texte hors lien suivant", TRUE);
        return;
    }
    nav.pos = found;
    nav_say_range(found, nav_line_end(found), old);
}

static void nav_quick(char key, int level, BOOL back)
{
    const NAVKEY *k;
    int i;
    char t[80];
    for (k = nav_keys; k->key && k->key != key; k++)
        ;
    if (!k->key)
        return;
    if (key == 'N') {
        nav_not_link(back);
        return;
    }
    if (back) {
        for (i = nav_first(nav.pos) - 1; i >= 0; i--)
            if (nav_is(nav_items[i].kind, k->kinds) && nav_match((WORD) i, key, level))
                break;
    } else {
        for (i = nav_first(nav.pos + 1); i < nav.n; i++)
            if (nav_is(nav_items[i].kind, k->kinds) && nav_match((WORD) i, key, level))
                break;
        if (i >= nav.n)
            i = -1;
    }
    if (i < 0) {
        if (level)
            wsprintf(t, "pas de titre de niveau %d %s", level, back ? "précédent" : "suivant");
        else
            wsprintf(t, "pas %s%s %s%s", (LPSTR) (nav_is(k->name[0], "aeiouéAEIOU") ? "d'" : "de "),
                     (LPSTR) k->name, back ? "précédent" : "suivant", k->fem ? "e" : "");
        say(t, TRUE);
        return;
    }
    nav_say_item((WORD) i, nav.pos);
}

/* Virgule : après la fin du conteneur ; Maj : à son début */
static void nav_container(BOOL back)
{
    WORD c[NAV_CHAIN], old = nav.pos, p;
    if (!nav_chain(nav.pos, c)) {
        say("pas dans un conteneur", TRUE);
        return;
    }
    if (back) {
        nav.pos = nav_fix(nav_items[c[0]].start);
        nav_say_line(NAV_NONE);
        return;
    }
    p = nav_items[c[0]].end;
    while (p < nav.len && nav_brk(nav_text[p]) && nav_line_start(p) != p)
        p++;
    if (p >= nav.len) {
        say("bas du document", TRUE);
        return;
    }
    nav.pos = p;
    nav_say_line(old);
}

/* -------------------------------------------------------------- tableaux */

static WORD nav_cell_at(WORD t, int row, int col)
{
    int i;
    for (i = t + 1; i < nav.n && nav_items[i].start < nav_items[t].end; i++) {
        NAVITEM *c = &nav_items[i];
        if (c->kind == 'c' && c->parent == t && row >= (int) c->a && row < (int) (c->a + c->c) &&
            col >= (int) c->b && col < (int) (c->b + c->d))
            return (WORD) i;
    }
    return NAV_NONE;
}

static void nav_header(WORD c, BOOL col, char *out, int max)
{
    const char *s = nav_s(&nav_items[c]), *tab = strchr(s, '\t');
    int n;
    out[0] = 0;
    if (!tab)
        return;
    if (col) {
        n = (int) (tab - s) + 1;
        lstrcpyn(out, s, n < max ? n : max);
    } else
        lstrcpyn(out, tab + 1, max);
}

/* Ctrl+Alt+flèches (dr, dc) ; Ctrl+Alt+Début/Fin, Page préc./suiv. : edge 1..4 */
static void nav_table(int dr, int dc, int edge)
{
    WORD c = nav_cell(nav.pos), t, n;
    NAVITEM *it;
    int row, col;
    char out[TXT * 2], h[TXT], w[40];
    BOOL entering = FALSE;
    if (c == NAV_NONE) {
        /* dans le tableau hors cellule (légende, après T) : sa première cellule */
        WORD ch[NAV_CHAIN], j;
        int k, m = nav_chain(nav.pos, ch);
        for (k = 0; k < m && nav_items[ch[k]].kind != 't'; k++)
            ;
        if (k < m)
            for (j = ch[k] + 1; j < nav.n && nav_items[j].start < nav_items[ch[k]].end; j++)
                if (nav_items[j].kind == 'c' && nav_items[j].parent == ch[k]) {
                    c = j;
                    break;
                }
        if (c == NAV_NONE) {
            say("pas dans un tableau", TRUE);
            return;
        }
        entering = TRUE;
    }
    it = &nav_items[c];
    t = it->parent;
    if (nav.ttable != t || nav_cell_at(t, nav.trow, nav.tcol) != c || entering) {
        nav.ttable = t;
        nav.trow = entering ? 0 : it->a;
        nav.tcol = entering ? 0 : it->b;
    }
    row = entering ? it->a : nav.trow;
    col = entering ? it->b : nav.tcol;
    if (entering)
        dr = dc = edge = 0;
    if (dc > 0) col = it->b + it->d;
    if (dc < 0) col = it->b - 1;
    if (dr > 0) row = it->a + it->c;
    if (dr < 0) row = it->a - 1;
    if (edge == 1) col = 1;
    if (edge == 2) col = nav_items[t].b;
    if (edge == 3) row = 1;
    if (edge == 4) row = nav_items[t].a;
    n = row < 1 || col < 1 ? NAV_NONE : nav_cell_at(t, row, col);
    if (n == NAV_NONE) {
        say("bord du tableau", TRUE);
        return;
    }
    out[0] = 0;
    if (row != nav.trow || edge >= 3 || n == c) {
        nav_header(n, FALSE, h, sizeof(h));
        append_n(out, h, sizeof(out));
        wsprintf(w, "ligne %d", row);
        append_n(out, w, sizeof(out));
    }
    if (col != nav.tcol || edge == 1 || edge == 2) {
        nav_header(n, TRUE, h, sizeof(h));
        append_n(out, h, sizeof(out));
        wsprintf(w, "colonne %d", col);
        append_n(out, w, sizeof(out));
    }
    if (nav_items[n].d > 1) {
        wsprintf(w, "sur %u colonnes", nav_items[n].d);
        append_n(out, w, sizeof(out));
    }
    if (nav_items[n].c > 1) {
        wsprintf(w, "sur %u lignes", nav_items[n].c);
        append_n(out, w, sizeof(out));
    }
    nav.trow = row;
    nav.tcol = col;
    nav.pos = nav_fix(nav_items[n].start);
    nav_compose(nav_items[n].start, nav_line_end(nav_items[n].start) < nav_items[n].end ?
                nav_line_end(nav_items[n].start) : nav_items[n].end, NAV_NONE, FALSE);
    append_n(out, nav_out, sizeof(out));
    say(out, TRUE);
}

/* ------------------------------------------------------------ activation */

static void nav_reveal(WORD i, HWND h)
{
    EpPageReveal(nav.view, h, (int) ((DWORD) nav_items[i].start * 1000 / (nav.len ? nav.len : 1)));
}

/* Champ de saisie : focus et mode formulaire automatique */
static void nav_edit(WORD i, HWND h)
{
    nav_reveal(i, h);
    nav.mode = 2;
    nav_hook();
    nav.prefix = TRUE;
    nav.pos = nav_fix(nav_items[i].start);
    if (GetFocus() == h) {
        last_focus = NULL;      /* déjà là : le redire avec le mode */
        announce_focus(TRUE);
    } else
        SetFocus(h);
}

static void nav_anchor(const char *name)
{
    int i;
    for (i = 0; i < nav.n; i++)
        if (nav_items[i].kind == 'n' && !lstrcmp(nav_s(&nav_items[i]), name)) {
            WORD old = nav.pos;
            nav.pos = nav_fix(nav_items[i].start);
            nav_say_line(old);
            return;
        }
    say("ancre introuvable", TRUE);
}

static void nav_activate(void)
{
    WORD i = nav_inner(nav.pos, "aexrbCL");
    NAVITEM *it;
    HWND h;
    char url[256];
    if (i == NAV_NONE)
        return;
    it = &nav_items[i];
    if (it->kind == 'a') {
        lstrcpyn(url, nav_s(it), sizeof(url));
        if (!url[0])
            say("lien sans adresse", TRUE);
        else if (url[0] == '#')
            nav_anchor(url + 1);
        else if (!net_open_url(url))
            say("Netscape ne répond pas", TRUE);
        return;
    }
    h = nav_hwnd(i);
    if (!h) {
        say("champ introuvable dans Netscape", TRUE);
        return;
    }
    if (nav_is(it->kind, "eCL")) {
        nav_edit(i, h);
        return;
    }
    /* case, bouton radio, bouton : comme Espace sur lui */
    nav_reveal(i, h);
    nav.quiet = h;
    SetFocus(h);
    PostMessage(h, WM_KEYDOWN, VK_SPACE, MAKELPARAM(1, 0x39));
    PostMessage(h, WM_KEYUP, VK_SPACE, MAKELPARAM(1, 0xC039));
    if (it->kind == 'b')
        nav_refresh();          /* envoi : nouvelle page, peut-être de même titre */
    else {
        nav.check = i;
        SetTimer(hwnd_main, TIMER_NAV, 200, NULL);
    }
}

/* Champ de la page activé par le mode navigation : son nouvel état est dit par
   nav_timer, pas par la scrutation du focus (sinon deux fois) */
BOOL nav_owns(HWND h)
{
    return nav_here() && nav_item_of(h) != NAV_NONE;
}

/* Nouvel état d'une case activée */
void nav_timer(void)
{
    HWND h;
    KillTimer(hwnd_main, TIMER_NAV);
    if (!nav.loaded || nav.check < 0 || nav.check >= nav.n)
        return;
    h = nav_hwnd((WORD) nav.check);
    if (h) {
        BOOL on = SendMessage(h, BM_GETCHECK, 0, 0L) != 0;
        if (nav_items[nav.check].kind == 'x')
            say(on ? "cochée" : "non cochée", TRUE);
        else
            say(on ? "coché" : "non coché", TRUE);
    }
    nav.check = -1;
}

/* Tab : élément suivant que l'on peut activer ; champ de saisie : mode formulaire */
static void nav_tab(BOOL back)
{
    int i;
    if (back) {
        for (i = nav_first(nav.pos) - 1; i >= 0; i--)
            if (nav_is(nav_items[i].kind, "aexrbCL"))
                break;
    } else {
        for (i = nav_first(nav.pos + 1); i < nav.n; i++)
            if (nav_is(nav_items[i].kind, "aexrbCL"))
                break;
        if (i >= nav.n)
            i = -1;
    }
    if (i < 0) {
        say(back ? "début de la page" : "fin de la page", TRUE);
        return;
    }
    if (nav_is(nav_items[i].kind, "eCL") && nav_hwnd((WORD) i)) {
        nav_edit((WORD) i, nav_hwnd((WORD) i));
        return;
    }
    nav_say_item((WORD) i, nav.pos);
}

/* ------------------------------------------------------- entrées */

/* EPM_NAV : touche prise par la DLL en mode navigation (ou Échap en mode formulaire
   automatique). lp : 1 Maj, 2 Ctrl, 4 Alt, 8 répétition. */
void nav_key(WPARAM vk, LPARAM lp)
{
    BOOL shift = (lp & 1) != 0, ctrl = (lp & 2) != 0, alt = (lp & 4) != 0;
    if (!nav.loaded) {
        nav_hook();
        return;
    }
    if (sa.on)
        sa_stop(TRUE);          /* (le curseur est sur la ligne qui était lue) */
    if (nav.mode == 2) {
        if (vk == VK_ESCAPE) {
            nav_set_mode(0);
            say("mode navigation", TRUE);
        }
        return;
    }
    if (nav.mode != 0)
        return;
    nav.pos = nav_fix(nav.pos);
    if (ctrl && alt) {
        switch (vk) {
        case VK_RIGHT: nav_table(0, 1, 0); break;
        case VK_LEFT: nav_table(0, -1, 0); break;
        case VK_DOWN: nav_table(1, 0, 0); break;
        case VK_UP: nav_table(-1, 0, 0); break;
        case VK_HOME: nav_table(0, 0, 1); break;
        case VK_END: nav_table(0, 0, 2); break;
        case VK_PRIOR: nav_table(0, 0, 3); break;
        case VK_NEXT: nav_table(0, 0, 4); break;
        }
        return;
    }
    switch (vk) {
    case VK_DOWN:
    case VK_UP:
        if (ctrl)
            nav_go_para(vk == VK_DOWN ? 1 : -1);
        else
            nav_go_line(vk == VK_DOWN ? 1 : -1);
        return;
    case VK_RIGHT:
    case VK_LEFT:
        if (ctrl)
            nav_go_word(vk == VK_RIGHT ? 1 : -1);
        else
            nav_go_char(vk == VK_RIGHT ? 1 : -1);
        return;
    case VK_HOME:
    case VK_END:
        if (ctrl) {
            WORD old = nav.pos;
            nav.pos = vk == VK_HOME ? 0 : nav_line_start(nav_fix(nav.len - 1));
            nav_say_line(old);
        } else {
            WORD s = nav_line_start(nav.pos), e = nav_line_end(nav.pos);
            nav.pos = vk == VK_HOME || e == s ? s : e - 1;
            nav_say_char(NAV_NONE);
        }
        return;
    case VK_PRIOR:
    case VK_NEXT:
        nav_go_page(vk == VK_NEXT ? 1 : -1);
        return;
    case VK_RETURN:
    case VK_SPACE:
        nav_activate();
        return;
    case VK_TAB:
        nav_tab(shift);
        return;
    }
    if (vk == nav_vk_comma)
        nav_container(shift);
    else if (vk >= '1' && vk <= '6')
        nav_quick('H', (int) (vk - '0'), shift);
    else if (vk >= 'A' && vk <= 'Z')
        nav_quick((char) vk, 0, shift);
}

/* Commandes Inser : Espace (bascule), Haut (ligne), F5 (relire la structure).
   FALSE : pas pour le mode navigation. */
BOOL nav_command(WPARAM vk, LPARAM lp)
{
    HWND page = page_of(GetActiveWindow());
    (void) lp;
    if (!page)
        return FALSE;
    if (vk == VK_SPACE) {
        if (!nav.loaded || page != nav.view) {
            say(nav.loading || nav.want ? "structure de la page en cours de lecture" :
                "pas de mode navigation pour cette page", TRUE);
            return TRUE;
        }
        if (nav.mode == 0) {
            WORD i = nav_inner(nav.pos, "exrbCL");
            HWND h = NULL;
            if (i != NAV_NONE)
                h = nav_hwnd(i);
            nav.mode = 1;
            nav_hook();
            say("mode formulaire", TRUE);
            if (h) {
                nav_reveal(i, h);
                SetFocus(h);
            }
        } else {
            nav_set_mode(0);
            say("mode navigation", TRUE);
        }
        return TRUE;
    }
    if (vk == VK_F5) {
        nav_refresh();
        say("relecture de la structure de la page", TRUE);
        return TRUE;
    }
    if (vk == VK_UP && !(lp & 1) && nav_here()) {
        nav_say_line(NAV_NONE);
        return TRUE;
    }
    return FALSE;
}

/* « mode formulaire, libellé, champ » d'après le tampon : un champ hors de la partie
   affichée n'a pas encore de libellé dessiné à côté de lui */
static void nav_form_mode(HWND foc, char *d)
{
    WORD i = nav_item_of(foc);
    lstrcpy(d, "mode formulaire");
    if (i == NAV_NONE) {
        lstrcat(d, ", ");
        describe(foc, d + lstrlen(d));
        return;
    }
    append_n(d, nav_s(&nav_items[i]), TXT * 2);
    nav_compose(nav_items[i].start, nav_items[i].end, NAV_NONE, FALSE);
    append_n(d, nav_out, TXT * 2);
}

/* Focus dans la page (announce_focus) : TRUE si le mode navigation s'en charge ;
   d : ce qu'il faut dire (vide : rien, le curseur virtuel parle lui-même) */
BOOL nav_focus(HWND foc, char *d)
{
    if (!nav.loaded || !nav_inside(foc))
        return FALSE;
    if (nav.mode == 0) {
        if (foc != nav.view && foc != nav.quiet &&
            (is_class(foc, "EDIT") || is_class(foc, "COMBOBOX") || is_class(foc, "LISTBOX"))) {
            /* champ de saisie qui prend le focus (clic) : mode formulaire automatique */
            nav.mode = 2;
            nav_hook();
            nav_sync();
            nav_form_mode(foc, d);
            return TRUE;
        }
        d[0] = 0;
        return TRUE;
    }
    nav.quiet = NULL;
    if (nav.mode == 2 && foc == nav.view) {
        nav_set_mode(0);        /* Tab hors des champs : retour au mode navigation */
        lstrcpy(d, "mode navigation");
        return TRUE;
    }
    if (nav.prefix) {
        nav.prefix = FALSE;
        nav_form_mode(foc, d);
        return TRUE;
    }
    return FALSE;
}

/* ---------------------------------------------------------- dire tout */

/* Dire tout depuis le curseur virtuel (diretout.c, genre 4) */
BOOL nav_sa_start(int *pos)
{
    if (!nav_here())
        return FALSE;
    *pos = nav_line_start(nav_fix(nav.pos));
    nav.sa_prev = NAV_OUTSIDE;          /* conteneurs de la première ligne : dits */
    return TRUE;
}

/* Ligne commençant en *pos, *pos passe à la suivante ; FALSE à la fin */
BOOL nav_sa_text(int *pos, char *out, int max)
{
    WORD s = (WORD) *pos, n;
    if (!nav.loaded || *pos < 0 || s >= nav.len)
        return FALSE;
    nav_compose(s, nav_line_end(s), nav.sa_prev, FALSE);
    lstrcpyn(out, lstrcmp(nav_out, "vide") ? nav_out : "", max);
    nav.sa_prev = s;
    n = nav_next_line(s);
    *pos = n == NAV_NONE ? (int) nav.len : (int) n;
    return TRUE;
}

/* Ligne en cours de lecture : le curseur virtuel la suit */
void nav_sa_at(int pos)
{
    if (nav.loaded && pos >= 0 && (WORD) pos < nav.len)
        nav.pos = (WORD) pos;
}

/* -------------------------------------------------------------- braille */

/* Ligne du curseur virtuel sur la plage ; FALSE hors du mode navigation */
BOOL nav_br_show(void)
{
    WORD s;
    int col;
    if (!nav_here())
        return FALSE;
    s = nav_line_start(nav.pos);
    nav_compose(s, nav_line_end(s), NAV_NONE, TRUE);
    for (col = 0; col < nav_n && nav_at[col] < nav.pos; col++)
        ;
    if (col >= nav_n && nav_n)
        col = nav_n - 1;
    if (nav.br_pos != nav.pos || nav.br_col != col || lstrcmp(nav.br_text, nav_out)) {
        nav.br_pos = nav.pos;
        nav.br_col = col;
        lstrcpy(nav.br_text, nav_out);
        br_text(nav_out, col, TRUE);
        br_select(-1, -1);
    }
    return TRUE;
}

/* Touches de la plage en mode navigation ; FALSE : à traiter ailleurs */
BOOL nav_br_key(int key)
{
    static int last_route;
    static DWORD route_at;
    int col;
    if (!nav_here() || !key)
        return FALSE;
    if (key == BR_UP || key == BR_DOWN) {
        if (sa.on)
            sa_stop(TRUE);
        nav_go_line(key == BR_DOWN ? 1 : -1);
        return TRUE;
    }
    if (key >= BR_ROUTE) {
        WORD s = nav_line_start(nav.pos);
        nav_compose(s, nav_line_end(s), NAV_NONE, TRUE);
        col = br_offset() + key - BR_ROUTE;
        if (col >= nav_n)
            return TRUE;
        if (sa.on)
            sa_stop(TRUE);
        nav.pos = nav_fix(nav_at[col]);
        if (key == last_route && GetTickCount() - route_at < 1000) {
            last_route = 0;
            nav_activate();     /* deux fois : activer (lien, case, bouton) */
        } else {
            last_route = key;
            route_at = GetTickCount();
            nav_say_char(NAV_NONE);
        }
        return TRUE;
    }
    return FALSE;
}

void nav_init(void)
{
    nav.ttable = NAV_NONE;
    nav.br_pos = NAV_NONE;
    nav.check = -1;
    nav_vk_comma = LOBYTE(VkKeyScan(','));
}
