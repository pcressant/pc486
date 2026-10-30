/* EPAULARD.EXE — outils : classes, libellés, zones d’édition, sélection. */
#include "ep.h"

/* ------------------------------------------------------------- utilitaires */
/* Lettre ou chiffre ; apostrophe, point et virgule entre deux d'entre eux
   (« aujourd'hui », « 12,50 ») */
static BOOL alnum_at(const char *t, int i, int n)
{
    BYTE c;
    if (i < 0 || i >= n)
        return FALSE;
    c = (BYTE) t[i];
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           c == '_' || c >= 0xC0;
}
static BOOL word_at(const char *t, int i, int n)
{
    BYTE c;
    if (alnum_at(t, i, n))
        return TRUE;
    c = (BYTE) t[i];
    return (c == '\'' || c == '.' || c == ',' || c == 0x92) &&
           alnum_at(t, i - 1, n) && alnum_at(t, i + 1, n);
}

/* Mot du texte qui commence ou passe à la colonne col : lettres et chiffres, ou
   ponctuation (comme Ctrl+flèches de Word et d'Excel : « SOMME », « ( », « B1 ») */
void say_word(char *text, int col)
{
    int start = col, end = col, n = lstrlen(text);
    BOOL w;
    if (col >= n) {
        say("fin de ligne", TRUE);
        return;
    }
    if (text[col] == ' ' || text[col] == '\t') {
        say("espace", TRUE);
        return;
    }
    w = word_at(text, col, n);
    while (start > 0 && text[start - 1] != ' ' && text[start - 1] != '\t' &&
           word_at(text, start - 1, n) == w)
        start--;
    while (end < n && text[end] != ' ' && text[end] != '\t' && word_at(text, end, n) == w)
        end++;
    text[end] = 0;
    say(text + start, TRUE);
}

/* Retire les « & » des libellés (« &Fichier » -> « Fichier ») et coupe à la tabulation
   (raccourci des menus), renvoyée dans *accel. */
void clean_label(char *s, char **accel)
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
    /* « Palette de couleurs >> », « Suivant > » : flèches de décor, pas « supérieur » */
    while (d - s >= 2 && (_fstrncmp(d - 2, ">>", 2) == 0 || _fstrncmp(d - 2, "<<", 2) == 0 ||
                          _fstrncmp(d - 2, " >", 2) == 0 || _fstrncmp(d - 2, " <", 2) == 0 ||
                          d[-1] == ' ')) {
        d -= d[-1] == ' ' ? 1 : 2;
        *d = 0;
    }
    /* « < Précédent » (InstallShield) */
    for (p = s; *p == '<'; p++)
        ;
    if (p != s && p - s <= 2 && *p == ' ') {
        while (*p == ' ')
            p++;
        _fmemmove(s, p, lstrlen(p) + 1);
    }
}

void class_of(HWND h, char *cls, int n)
{
    GetClassName(h, cls, n);
    AnsiUpper(cls);
}

/* Style WS_VISIBLE etc. du contrôle lui-même : IsWindowVisible() répond non tant que
   la boîte de dialogue qui le contient n'est pas encore affichée. */
BOOL has_style(HWND h, long st)
{
    return (GetWindowLong(h, GWL_STYLE) & st) != 0;
}

BOOL is_class(HWND h, const char *name)
{
    char cls[32];
    if (!h)
        return FALSE;
    class_of(h, cls, sizeof(cls));
    return lstrcmp(cls, name) == 0;
}

/* Contrôle standard, lu directement (sinon : modèle hors écran) */
BOOL is_standard(HWND h)
{
    return is_class(h, "BUTTON") || is_class(h, "EDIT") || is_class(h, "LISTBOX") ||
           is_class(h, "COMBOBOX") || is_class(h, "SCROLLBAR");
}

/* Curseur texte, en coordonnées écran : celui que le programme dessine lui-même s'il y
   en a un dans cette fenêtre (Write), sinon le curseur système de Windows. */
void caret_screen(HWND h, POINT *pt)
{
    if (EpOsmCaret(h, pt))
        return;
    GetCaretPos(pt);
    ClientToScreen(h, pt);
}

/* Texte sans lettre ni chiffre (« / », « : » entre les champs de Date/Heure) */
BOOL no_word(const char *s)
{
    for (; *s; s++)
        if (IsCharAlphaNumeric(*s))
            return FALSE;
    return TRUE;
}

BOOL ends_colon(const char *s)
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
BOOL label_left(HWND parent, const RECT *rc, char *out, int n)
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

/* Champ d'une page web : le texte le plus proche à sa gauche sur la même rangée,
   avec ou sans « : » (« Aller au lien n° », « Rechercher ») */
BOOL label_nearest_left(HWND parent, const RECT *rc, char *out, int n)
{
    char buf[TXT];
    int l;
    out[0] = 0;
    for (l = rc->left - 300; l < rc->left; l += 4) {
        if (!EpOsmRect(parent, l, rc->top, rc->left, rc->bottom, buf, sizeof(buf)))
            break;
        if (!no_word(buf))
            lstrcpyn(out, buf, n);      /* de plus en plus près */
    }
    return out[0] != 0;
}

void label_drawn(HWND parent, const RECT *rc, char *out, int n, BOOL any)
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
BOOL is_sdm_page(HWND h)
{
    char cls[32];
    if (!h)
        return FALSE;
    GetClassName(h, cls, sizeof(cls));
    return _fstrncmp(cls, "sa_sdm", 6) == 0;
}

BOOL is_sdm(HWND h)
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
BOOL label_zorder(HWND h, char *out, int n)
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
void label_before(HWND h, char *out, int n)
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
        {
            /* libellé d'un autre champ, qui le suit (« Attachments: » au-dessus du
               corps d'un message d'Eudora) */
            HWND nx = GetWindow(p, GW_HWNDNEXT);
            if (nx && nx != h && has_style(nx, WS_VISIBLE) &&
                (is_class(nx, "EDIT") || is_class(nx, "LISTBOX") || is_class(nx, "COMBOBOX")))
                continue;
        }
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

/* Bouton dont le texte est le nom de son image (« #bmpBlnkTbl », Access 2.0) */
BOOL picture_button(HWND h)
{
    char t[4];
    char cls[32];
    class_of(h, cls, sizeof(cls));
    if (!_fstrstr(cls, "BUTTON"))
        return FALSE;
    GetWindowText(h, t, sizeof(t));
    return t[0] == '#' && t[1] != 0 && t[1] != ' ';
}

/* Libellé d'un bouton sans texte (icône : menu de l'installateur Lotus) : le texte
   statique, sinon dessiné, juste à sa droite et à la même hauteur. */
BOOL label_right(HWND h, char *out, int n)
{
    HWND p, best = NULL;
    RECT rc, r;
    int d, best_d = 32000;

    out[0] = 0;
    GetWindowRect(h, &rc);
    for (p = GetWindow(GetParent(h), GW_CHILD); p; p = GetWindow(p, GW_HWNDNEXT)) {
        if (p == h || !has_style(p, WS_VISIBLE) || !is_class(p, "STATIC") ||
            (GetWindowLong(p, GWL_STYLE) & 0x0F) > SS_RIGHT || GetWindowTextLength(p) == 0 ||
            GetWindowTextLength(p) > TXT - 1)
            continue;
        GetWindowRect(p, &r);
        if (r.left < rc.right - 4 || r.left - rc.right > 120 ||
            (r.top + r.bottom) / 2 < rc.top || (r.top + r.bottom) / 2 >= rc.bottom)
            continue;
        d = r.left - rc.right;
        if (d < best_d) {
            best_d = d;
            best = p;
        }
    }
    if (best)
        GetWindowText(best, out, n);
    else if (GetParent(h))
        EpOsmRect(GetParent(h), rc.right, rc.top, rc.right + 160, rc.bottom, out, n);
    clean_label(out, NULL);
    return out[0] != 0;
}

/* Bouton sans texte ou au nom d'image : le libellé à sa droite, sinon celui qui le
   précède (« Table &vierge » au-dessus d'un bouton d'Access) ; à défaut, le nom de
   l'image sans « # » (« #Play » : Lecture). */
void picture_label(HWND h, char *name, int n)
{
    char lab[TXT];
    if (label_right(h, lab, sizeof(lab)) || label_zorder(h, lab, sizeof(lab)))
        lstrcpyn(name, lab, n);
    else if (name[0] == '#')
        media_button_name(name);
}

void append_n(char *dst, const char *s, int max)
{
    if (!s || !*s)
        return;
    if (*dst && lstrlen(dst) + lstrlen(s) + 2 < max)
        lstrcat(dst, ", ");
    if (lstrlen(dst) + lstrlen(s) < max)
        lstrcat(dst, s);
}

void append(char *dst, const char *s)
{
    append_n(dst, s, TXT * 2);
}

/* Tronque s pour laisser la place, dans un tampon de max octets, aux ajouts que
   append_n ferait de chacun des morceaux essentiels (rôle, valeur, état). */
void fit_before(char *s, int max, const char *a, const char *b, const char *c)
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
void append_keep(char *dst, const char *s, int max)
{
    fit_before(dst, max, s, NULL, NULL);
    append_n(dst, s, max);
}

/* Ligne courante d'une zone d'édition multiligne */
int edit_line(HWND h, char *out, int n)
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
BOOL is_multiline(HWND h)
{
    RECT r;
    if (!(GetWindowLong(h, GWL_STYLE) & ES_MULTILINE))
        return FALSE;
    GetWindowRect(h, &r);
    return r.bottom - r.top >= 28;
}

/* Texte d'une zone d'édition entre les positions a (incluse) et b (exclue) ;
   lignes séparées par une espace. */
void edit_range(HWND h, int a, int b, char *out, int max)
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
void say_selection(const char *text, int count, BOOL selected)
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
BOOL edit_selection(HWND h, DWORD old, DWORD cur, BOOL shift)
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
void drawn_selection(const char *old, const char *cur)
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
