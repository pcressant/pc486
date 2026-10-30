/* EPAULARD.EXE — tableurs : grille dessinée, sans contrôle standard ni curseur.
 *
 * Méthode générale, valable pour tout tableur qui affiche des en-têtes de colonnes
 * (A, B…) et de lignes (1, 2…) et, dans une barre de formule, la référence de la cellule
 * active suivie de son contenu (Excel 5 : « B3 =B2*3 ») :
 *   1. la référence et le contenu sont lus dans la barre (modèle hors écran) ;
 *   2. la cellule est retrouvée à l'écran par ses en-têtes : le texte « B » en haut de
 *      la grille donne la colonne, « 3 » à gauche donne la ligne ; les bornes sont à
 *      mi-chemin des en-têtes voisins ;
 *   3. la valeur affichée est le texte qui commence dans ce rectangle (pas celui d'une
 *      cellule voisine qui déborde), sa police donne la mise en forme.
 * Un profil nomme les classes de fenêtres : application, grille (qui a le focus), barre
 * de formule, onglets des feuilles (facultatif : sinon, dans la grille). Profils intégrés ci-dessous ; d'autres
 * dans EPAULARD.INI, section [Tableurs] : nom=application,grille,barre[,onglets].
 */
#include "ep.h"

#define SHEET_N 8
typedef struct { char app[20], grid[20], bar[20], tabs[20]; } SHEETPROF;
static SHEETPROF sheets[SHEET_N] = {
    { "XLMAIN", "EXCEL9", "EXCEL6", "" }          /* Microsoft Excel 5.0 */
};
static int sheet_count = 1;

static HWND sheet_hwnd;         /* grille suivie */
static char sheet_ref[16];      /* dernière cellule annoncée */
static char sheet_content[TXT];
static char sheet_tab[40];      /* feuille annoncée dans la grille tab_hwnd */
static HWND tab_hwnd;
static DWORD sheet_tick;        /* contenu changé, en attente de stabilité */
static char sheet_range[24];    /* plage sélectionnée annoncée (« A1 à B3 »), ou vide */

/* Profils supplémentaires de EPAULARD.INI */
void sheet_load(const char *ini)
{
    char keys[256], line[96], *k, *p, *f[4];
    int i, n;
    GetPrivateProfileString("Tableurs", NULL, "", keys, sizeof(keys), ini);
    for (k = keys; *k && sheet_count < SHEET_N; k += lstrlen(k) + 1) {
        GetPrivateProfileString("Tableurs", k, "", line, sizeof(line), ini);
        for (n = 0, p = line; n < 4 && *p; n++) {
            f[n] = p;
            while (*p && *p != ',')
                p++;
            if (*p)
                *p++ = 0;
        }
        if (n < 3)
            continue;
        for (i = 0; i < n; i++) {
            AnsiUpper(f[i]);
            while (*f[i] == ' ')
                f[i]++;
        }
        lstrcpyn(sheets[sheet_count].app, f[0], sizeof(sheets[0].app));
        lstrcpyn(sheets[sheet_count].grid, f[1], sizeof(sheets[0].grid));
        lstrcpyn(sheets[sheet_count].bar, f[2], sizeof(sheets[0].bar));
        lstrcpyn(sheets[sheet_count].tabs, n > 3 ? f[3] : "", sizeof(sheets[0].tabs));
        sheet_count++;
    }
}

/* Fenêtre visible de classe cls parmi les descendants de h */
HWND find_class(HWND h, const char *cls)
{
    HWND c, r;
    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!IsWindowVisible(c))
            continue;
        if (is_class(c, cls))
            return c;
        if ((r = find_class(c, cls)) != NULL)
            return r;
    }
    return NULL;
}

/* Profil de la grille h (qui a le focus), ou NULL */
static SHEETPROF *sheet_of(HWND h)
{
    int i;
    HWND top = top_level(h);
    for (i = 0; h && i < sheet_count; i++)
        if (is_class(h, sheets[i].grid) && is_class(top, sheets[i].app))
            return &sheets[i];
    return NULL;
}

BOOL is_sheet(HWND h)
{
    return sheet_of(h) != NULL;
}

/* Lettres de la colonne c (1 = A, 27 = AA) */
static void col_letters(int c, char *out)
{
    if (c > 26) {
        out[0] = (char) ('A' + (c - 1) / 26 - 1);
        out[1] = (char) ('A' + (c - 1) % 26);
        out[2] = 0;
    } else {
        out[0] = (char) ('A' + c - 1);
        out[1] = 0;
    }
}

/* « B3 » ou « L3C2 » (notation L1C1 d'Excel français) -> colonne, ligne */
BOOL sheet_parse(const char *ref, int *col, int *row)
{
    const char *p = ref;
    int c = 0, r = 0;
    if ((p[0] == 'L' || p[0] == 'R') && p[1] >= '0' && p[1] <= '9') {
        for (p++; *p >= '0' && *p <= '9'; p++)
            r = r * 10 + *p - '0';
        if (*p != 'C' && *p != 'K')
            return FALSE;
        for (p++; *p >= '0' && *p <= '9'; p++)
            c = c * 10 + *p - '0';
    } else {
        for (; (*p >= 'A' && *p <= 'Z') || *p == '$'; p++)
            if (*p != '$')
                c = c * 26 + *p - 'A' + 1;
        if (*p == '$')
            p++;
        for (; *p >= '0' && *p <= '9'; p++)
            r = r * 10 + *p - '0';
    }
    if (*p || c < 1 || c > 256 || r < 1 || r > 16384)
        return FALSE;
    *col = c;
    *row = r;
    return TRUE;
}

/* Premier mot de s, s'il désigne une cellule */
static BOOL first_ref(const char *s, char *ref)
{
    const char *e;
    int c, r;
    for (; *s == ' '; s++)
        ;
    for (e = s; *e && *e != ' '; e++)
        ;
    if (e == s || e - s >= 16)
        return FALSE;
    lstrcpyn(ref, s, (int) (e - s) + 1);
    AnsiUpper(ref);
    return sheet_parse(ref, &c, &r);
}

/* Référence et contenu de la cellule active. Le contenu est le texte de la barre de
   formule ; la référence, le texte à sa gauche sur la même rangée (zone Nom
   d'Excel), ou à défaut le premier mot de la barre elle-même. */
static BOOL sheet_bar(HWND grid, SHEETPROF *p, char *ref, char *content)
{
    static char buf[TXT * 2];
    HWND top = top_level(grid), bar = find_class(top, p->bar);
    RECT b, w;
    char *s;
    int n;

    ref[0] = content[0] = 0;
    if (!bar)
        return FALSE;
    GetWindowRect(bar, &b);
    GetWindowRect(top, &w);
    EpOsmStartIn(bar, b.left, b.top, b.right, b.bottom, buf, sizeof(buf), NULL);
    s = buf;
    if (EpOsmStartIn(top, w.left, b.top, b.left, b.bottom, content, TXT, NULL) &&
        first_ref(content, ref))
        ;
    else if (first_ref(buf, ref)) {
        for (; *s == ' '; s++)
            ;
        for (; *s && *s != ' '; s++)
            ;
    } else {
        ref[0] = content[0] = 0;
        return FALSE;
    }
    for (; *s == ' '; s++)
        ;
    lstrcpyn(content, s, TXT);
    for (n = lstrlen(content); n > 0 && content[n - 1] == ' '; n--)
        content[n - 1] = 0;
    return TRUE;
}

/* Centre d'un en-tête trouvé dans la bande (l, t, r, b), horizontal ou vertical */
static BOOL head_center(HWND grid, const RECT *band, const char *name, BOOL vertical, int *c)
{
    RECT r;
    if (!EpOsmFind(grid, band->left, band->top, band->right, band->bottom, name, &r))
        return FALSE;
    *c = vertical ? (r.top + r.bottom) / 2 : (r.left + r.right) / 2;
    return TRUE;
}

/* Bornes [*lo, *hi[ de la colonne ou de la ligne n, à mi-chemin des en-têtes voisins */
static BOOL head_span(HWND grid, const RECT *band, int n, BOOL vertical, int *lo, int *hi)
{
    char name[8];
    int c, p, q;
    BOOL hp, hq;

    if (vertical) wsprintf(name, "%d", n); else col_letters(n, name);
    if (!head_center(grid, band, name, vertical, &c))
        return FALSE;
    if (vertical) wsprintf(name, "%d", n - 1); else col_letters(n - 1, name);
    hp = n > 1 && head_center(grid, band, name, vertical, &p);
    if (vertical) wsprintf(name, "%d", n + 1); else col_letters(n + 1, name);
    hq = head_center(grid, band, name, vertical, &q);
    if (hp && hq) { *lo = (p + c) / 2; *hi = (c + q) / 2; }
    else if (hp) { *lo = (p + c) / 2; *hi = c + (c - p) / 2; }
    else if (hq) { *lo = c - (q - c) / 2; *hi = (c + q) / 2; }
    else { *lo = c - (vertical ? 8 : 40); *hi = c + (vertical ? 8 : 40); }
    return TRUE;
}

/* Rectangle (écran) de la cellule à l'intersection des en-têtes ; FALSE si elle
   n'est pas affichée */
BOOL sheet_cell(HWND grid, int col, int row, RECT *cell)
{
    RECT w, top, left;
    GetWindowRect(grid, &w);
    SetRect(&top, w.left, w.top, w.right, w.top + 32);       /* en-têtes de colonnes */
    SetRect(&left, w.left, w.top, w.left + 48, w.bottom);    /* en-têtes de lignes */
    return head_span(grid, &top, col, FALSE, &cell->left, &cell->right) &&
           head_span(grid, &left, row, TRUE, &cell->top, &cell->bottom);
}

/* Valeur affichée dans la cellule ; *at : rectangle du texte */
static BOOL sheet_value(HWND grid, const char *ref, char *out, int max, RECT *at)
{
    RECT cell;
    int c, r, n;
    out[0] = 0;
    if (!sheet_parse(ref, &c, &r) || !sheet_cell(grid, c, r, &cell))
        return FALSE;
    EpOsmStartIn(grid, cell.left, cell.top, cell.right, cell.bottom, out, max, at);
    for (n = lstrlen(out); n > 0 && out[n - 1] == ' '; n--)
        out[n - 1] = 0;
    while (out[0] == ' ')
        _fmemmove(out, out + 1, lstrlen(out));
    if (at)
        IntersectRect(at, at, &cell);
    return TRUE;
}

static BOOL all_digits(const char *s)
{
    for (; *s == ' '; s++)
        ;
    if (!*s)
        return FALSE;
    for (; *s; s++)
        if ((*s < '0' || *s > '9') && *s != ' ')
            return FALSE;
    return TRUE;
}

/* Feuille active : onglet en gras. Dans une fenêtre de la classe du profil ; sans
   classe, dans la grille elle-même, à gauche de sa barre de défilement horizontale
   (Excel dessine ses onglets sur la même rangée). */
static void sheet_tab_name(HWND grid, SHEETPROF *p, char *out, int n)
{
    HWND c;
    RECT r, g;
    out[0] = 0;
    for (c = GetWindow(grid, GW_CHILD); c && !out[0]; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!IsWindowVisible(c))
            continue;
        GetWindowRect(c, &r);
        if (p->tabs[0] && is_class(c, p->tabs))
            EpOsmBoldIn(c, r.left, r.top, r.right, r.bottom, out, n);
        else if (!p->tabs[0] && is_class(c, "SCROLLBAR") &&
                 (GetWindowLong(c, GWL_STYLE) & 0x0F) == SBS_HORZ) {
            /* un numéro de ligne en gras peut dépasser sur cette rangée : passer */
            int k, l;
            RECT f;
            GetWindowRect(grid, &g);
            for (k = 0, l = g.left; k < 4; k++) {
                if (!EpOsmBoldIn(grid, l, r.top, r.left, r.bottom, out, n) ||
                    !all_digits(out) ||
                    !EpOsmFind(grid, l, r.top, r.left, r.bottom, out, &f))
                    break;
                out[0] = 0;
                l = f.right;
            }
        }
    }
}

/* Annonce complète : « B3, 36, formule =B2*3 » ; « B3, vide » */
static void sheet_describe(HWND grid, const char *ref, const char *content, char *out)
{
    char value[TXT];
    BOOL shown = sheet_value(grid, ref, value, sizeof(value), NULL);
    out[0] = 0;
    append(out, ref);
    if (!shown && content[0])
        lstrcpyn(value, content, sizeof(value));    /* cellule hors de l'écran */
    append(out, value[0] ? value : "vide");
    if (content[0] == '=' || (content[0] && value[0] && lstrcmp(content, value) != 0 &&
                              content[0] != '\'' && !_fstrstr(value, "#"))) {
        if (content[0] == '=') {
            lstrcat(out, ", formule ");
            if (lstrlen(out) + lstrlen(content) < TXT * 2 - 1)
                lstrcat(out, content);
        } else if (lstrcmp(content, value) != 0) {
            lstrcat(out, ", saisi ");
            if (lstrlen(out) + lstrlen(content) < TXT * 2 - 1)
                lstrcat(out, content);
        }
    }
}

/* Plage sélectionnée : la cellule active n'est pas inversée, les autres cellules de la
   plage le sont. Depuis la cellule active, on étend colonnes et lignes tant que le
   centre de la voisine est dans l'enveloppe des inversions. Vide : cellule seule. */
static void sheet_selection(HWND grid, const char *ref, char *out)
{
    RECT w, cell, box, top, left;
    int c, r, c0, c1, r0, r1, lo, hi;
    char a[8], b[8];

    out[0] = 0;
    if (!sheet_parse(ref, &c, &r) || !sheet_cell(grid, c, r, &cell))
        return;
    GetWindowRect(grid, &w);
    if (!EpOsmInvBox(grid, w.left + 20, w.top + 12, w.right, w.bottom, &box))
        return;
    UnionRect(&box, &box, &cell);
    SetRect(&top, w.left, w.top, w.right, w.top + 32);
    SetRect(&left, w.left, w.top, w.left + 48, w.bottom);
    for (c0 = c; c0 > 1 && head_span(grid, &top, c0 - 1, FALSE, &lo, &hi) &&
         (lo + hi) / 2 >= box.left; c0--)
        ;
    for (c1 = c; c1 < 256 && head_span(grid, &top, c1 + 1, FALSE, &lo, &hi) &&
         (lo + hi) / 2 < box.right; c1++)
        ;
    for (r0 = r; r0 > 1 && head_span(grid, &left, r0 - 1, TRUE, &lo, &hi) &&
         (lo + hi) / 2 >= box.top; r0--)
        ;
    for (r1 = r; r1 < 16384 && head_span(grid, &left, r1 + 1, TRUE, &lo, &hi) &&
         (lo + hi) / 2 < box.bottom; r1++)
        ;
    if (c0 == c1 && r0 == r1)
        return;
    col_letters(c0, a);
    col_letters(c1, b);
    wsprintf(out, "%s%d à %s%d", (LPSTR) a, r0, (LPSTR) b, r1);
}

void sheet_reset(void)
{
    sheet_hwnd = NULL;
    sheet_ref[0] = sheet_content[0] = 0;   /* (feuille gardée : retour d'une saisie) */
    sheet_range[0] = 0;
    sheet_tick = 0;
}

/* Scrutation (POLL_MS) : annoncer la cellule quand la référence change (déplacement,
   Entrée, Atteindre) ou que son contenu change sans frappe (Suppr, annulation,
   collage). TRUE si le focus est sur une grille connue : la lecture générique du
   curseur n'a alors rien à faire. */
BOOL sheet_poll(void)
{
    HWND h = GetFocus();
    SHEETPROF *p = sheet_of(h);
    char ref[16], content[TXT], tab[40], out[TXT * 2];
    BOOL first;

    if (!p) {
        sheet_hwnd = NULL;
        return FALSE;
    }
    first = h != sheet_hwnd;
    if (!sheet_bar(h, p, ref, content))
        return TRUE;
    sheet_tab_name(h, p, tab, sizeof(tab));
    if (h != tab_hwnd) {
        tab_hwnd = h;
        sheet_tab[0] = 0;
    }
    out[0] = 0;
    if (lstrcmp(tab, sheet_tab) != 0 && tab[0]) {
        append(out, tab);
        lstrcpyn(sheet_tab, tab, sizeof(sheet_tab));
        sheet_ref[0] = 0;               /* autre feuille : redire la cellule */
    }
    if (first || lstrcmp(ref, sheet_ref) != 0) {
        char d[TXT * 2];
        sheet_describe(h, ref, content, d);
        append(out, d);
        lstrcpyn(sheet_ref, ref, sizeof(sheet_ref));
        lstrcpyn(sheet_content, content, sizeof(sheet_content));
        sheet_selection(h, ref, sheet_range);
        sheet_hwnd = h;
        typed = FALSE;
        say(out, !first);
        return TRUE;
    }
    {
        /* Maj+flèches : la cellule active reste, la plage change */
        char range[24];
        sheet_selection(h, ref, range);
        if (lstrcmp(range, sheet_range) != 0) {
            lstrcpyn(sheet_range, range, sizeof(sheet_range));
            if (range[0]) {
                append(out, "sélection");
                lstrcat(out, " ");
                lstrcat(out, range);
            } else
                append(out, ref);       /* plage réduite à la cellule active */
        }
    }
    if (out[0])
        say(out, TRUE);
    if (lstrcmp(content, sheet_content) != 0) {
        /* caractère tapé (écho de la frappe) : rien ; sinon le nouveau contenu */
        if (typed) {                    /* (Suppr, Ctrl+Z : contenu annoncé) */
            typed = FALSE;
            lstrcpyn(sheet_content, content, sizeof(sheet_content));
            return TRUE;
        }
        lstrcpyn(sheet_content, content, sizeof(sheet_content));
        say(content[0] ? content : "vide", TRUE);
    }
    return TRUE;
}

/* Inser + touche dans une grille : Haut, la cellule ; F, sa mise en forme ;
   Tab, la feuille et la cellule. */
BOOL sheet_command(WPARAM vk)
{
    HWND h = GetFocus();
    SHEETPROF *p = sheet_of(h);
    char ref[16], content[TXT], out[TXT * 2], value[TXT], fmt[TXT];
    RECT at, cell;
    int c, r;

    if (!p || in_menu || (vk != VK_UP && vk != 'F' && vk != VK_TAB))
        return FALSE;
    if (!sheet_bar(h, p, ref, content)) {
        /* diagnostic pour régler un profil : la barre et son texte */
        HWND bar = find_class(top_level(h), p->bar);
        lstrcpy(out, bar ? "cellule inconnue, barre " : "barre de formule introuvable");
        if (bar) {
            EpOsmWindow(bar, value, sizeof(value));
            lstrcat(out, value);
        }
        say(out, TRUE);
        return TRUE;
    }
    if (vk == VK_UP) {
        sheet_describe(h, ref, content, out);
        say(out, TRUE);
        return TRUE;
    }
    if (vk == VK_TAB) {
        out[0] = 0;
        sheet_tab_name(h, p, value, sizeof(value));
        GetWindowText(h, fmt, sizeof(fmt));
        append(out, fmt);
        append(out, value);
        sheet_describe(h, ref, content, fmt);
        append(out, fmt);
        say(out, TRUE);
        return TRUE;
    }
    /* F : police de la valeur affichée, et son alignement dans la cellule. Excel
       ne repasse pas par le texte quand seul l'attribut change (Ctrl+G) : faire
       repeindre la cellule d'abord. */
    if (sheet_parse(ref, &c, &r) && sheet_cell(h, c, r, &cell)) {
        RECT k = cell;
        ScreenToClient(h, (POINT *) &k.left);
        ScreenToClient(h, (POINT *) &k.right);
        InvalidateRect(h, &k, FALSE);
        UpdateWindow(h);
    }
    if (!sheet_value(h, ref, value, sizeof(value), &at) || !value[0] ||
        !sheet_parse(ref, &c, &r) || !sheet_cell(h, c, r, &cell)) {
        say("cellule vide, mise en forme inconnue", TRUE);
        return TRUE;
    }
    out[0] = 0;
    append(out, ref);
    if (EpOsmFormat(h, (at.left + at.right) / 2, (at.top + at.bottom) / 2, fmt, sizeof(fmt)))
        append(out, fmt);
    if (at.left - cell.left <= 4 && cell.right - at.right > 6)
        append(out, "aligné à gauche");
    else if (cell.right - at.right <= 4 && at.left - cell.left > 6)
        append(out, "aligné à droite");
    else if (at.left - cell.left > 6 && cell.right - at.right > 6)
        append(out, "centré");
    if (content[0] && lstrcmp(content, value) != 0 && content[0] != '=') {
        lstrcat(out, ", saisi ");
        lstrcat(out, content);
    }
    say(out, TRUE);
    return TRUE;
}

/* Texte de la cellule pour le braille et la revue */
BOOL sheet_text(HWND h, char *out)
{
    SHEETPROF *p = sheet_of(h);
    char ref[16], content[TXT];
    if (!p || !sheet_bar(h, p, ref, content))
        return FALSE;
    sheet_describe(h, ref, content, out);
    return TRUE;
}

/* ------------------------------------------------- grilles à éditeur (Access) */

/* Feuilles de données et grilles de création d'Access : une fenêtre d'édition (OKTTBX)
   se place sur la cellule active d'une grille dessinée (OGRID). Quand elle se déplace,
   on dit l'étiquette de la ligne (texte à gauche de la cellule : « Taille du champ »,
   nom du champ), l'en-tête de la colonne si la grille en a (rangée du haut, au-dessus
   d'une colonne de sélecteurs de ligne, petites images à gauche), puis la valeur.
   Profils : [Grilles] nom=classe de l'éditeur,classe de la grille. */
#define GRID_N 6
typedef struct { char edit[20], grid[20]; } GRIDPROF;
static GRIDPROF grids[GRID_N] = {
    { "OKTTBX", "OGRID" }                       /* Microsoft Access 2.0 */
};
static int grid_count = 1;
static HWND grid_edit_hwnd;
static HWND grid_hwnd;          /* grille de la dernière annonce (gardée : Access recrée
                                   son éditeur à chaque cellule) */
static RECT grid_rect;          /* position annoncée de l'éditeur */
static DWORD grid_tick;
static char grid_value[TXT];
BOOL grid_typed;                /* frappe dans la cellule : son écho suffit */

void grid_load(const char *ini)
{
    char keys[256], line[64], *k, *c;
    GetPrivateProfileString("Grilles", NULL, "", keys, sizeof(keys), ini);
    for (k = keys; *k && grid_count < GRID_N; k += lstrlen(k) + 1) {
        GetPrivateProfileString("Grilles", k, "", line, sizeof(line), ini);
        if ((c = _fstrchr(line, ',')) == NULL)
            continue;
        *c++ = 0;
        while (*c == ' ')
            c++;
        AnsiUpper(line);
        AnsiUpper(c);
        lstrcpyn(grids[grid_count].edit, line, sizeof(grids[0].edit));
        lstrcpyn(grids[grid_count].grid, c, sizeof(grids[0].grid));
        grid_count++;
    }
}

BOOL is_grid_edit(HWND h)
{
    int i;
    for (i = 0; h && i < grid_count; i++)
        if (is_class(h, grids[i].edit) && is_class(GetParent(h), grids[i].grid))
            return TRUE;
    return FALSE;
}

void grid_reset(void)
{
    grid_edit_hwnd = NULL;              /* (position et valeur gardées, voir grid_poll) */
    grid_typed = FALSE;
}

static void trim(char *s)
{
    int n = lstrlen(s);
    while (n > 0 && s[n - 1] == ' ')
        s[--n] = 0;
    while (s[0] == ' ')
        _fmemmove(s, s + 1, lstrlen(s));
}

/* Valeur de la cellule : texte de l'éditeur, sinon celui de la grille sous lui */
static void grid_cell(HWND h, const RECT *r, char *out, int n)
{
    if (!EpOsmRect(h, r->left, r->top, r->right, r->bottom, out, n))
        EpOsmRect(GetParent(h), r->left + 1, r->top + 1, r->right - 1, r->bottom - 1, out, n);
    trim(out);
}

BOOL grid_poll(void)
{
    HWND h = GetFocus(), g;
    RECT r, w;
    char out[TXT * 2], part[TXT], value[TXT];
    int x, y, wd, ht, xs, ys;
    BOOL selectors;

    if (!is_grid_edit(h)) {
        grid_edit_hwnd = NULL;
        return FALSE;
    }
    g = GetParent(h);
    GetWindowRect(h, &r);
    GetWindowRect(g, &w);
    grid_cell(h, &r, value, sizeof(value));
    if (h != grid_edit_hwnd && g == grid_hwnd && EqualRect(&r, &grid_rect) &&
        lstrcmp(value, grid_value) == 0 && GetTickCount() - grid_tick < 2000)
        grid_edit_hwnd = h;             /* éditeur recréé sur la cellule déjà dite */
    if (g != grid_hwnd)
        SetRectEmpty(&grid_rect);       /* autre grille : tout dire */
    if (h == grid_edit_hwnd && EqualRect(&r, &grid_rect)) {
        if (lstrcmp(value, grid_value) != 0) {
            /* contenu changé sur place : frappe (écho) ou choix dans une liste */
            if (!grid_typed)
                say(value[0] ? value : "vide", TRUE);
            grid_typed = FALSE;
            lstrcpyn(grid_value, value, sizeof(grid_value));
        }
        return TRUE;
    }
    out[0] = 0;
    /* étiquette de la ligne : le texte à gauche de la cellule, sur sa rangée */
    if (r.top != grid_rect.top) {
        /* (Access dessine la ligne active par l'éditeur : textes notés dans sa fenêtre) */
        if (EpOsmRect(g, w.left, r.top + 1, r.left - 1, r.bottom - 1, part, sizeof(part)) ||
            EpOsmRect(h, w.left, r.top + 1, r.left - 1, r.bottom - 1, part, sizeof(part))) {
            trim(part);
            append(out, part);
        }
    }
    /* en-tête de colonne, si la grille a des sélecteurs de ligne et une rangée au-dessus */
    for (selectors = FALSE, y = r.top; y < w.bottom && y < r.top + 60; y += 8)
        for (x = w.left; x < w.left + 30 && !selectors; x += 8)
            if (EpOsmGlyph(g, x, y, x + 8, y + 8, &wd, &ht, &xs, &ys))
                selectors = TRUE;
    if (selectors && r.left != grid_rect.left && r.top >= w.top + (r.bottom - r.top) - 2) {
        /* rangée d'en-têtes : du haut de la grille à la cellule, une hauteur de rangée */
        if (EpOsmRect(g, r.left + 1, w.top, r.right - 1,
                      min(w.top + (r.bottom - r.top) + 6, r.top), part, sizeof(part))) {
            trim(part);
            append(out, part);
        }
    }
    append(out, value[0] ? value : "vide");
    grid_edit_hwnd = h;
    grid_hwnd = g;
    grid_rect = r;
    grid_tick = GetTickCount();
    lstrcpyn(grid_value, value, sizeof(grid_value));
    grid_typed = FALSE;
    say(out, TRUE);
    return TRUE;
}

/* Inser+Haut dans une grille à éditeur : ligne, colonne et valeur */
BOOL grid_command(WPARAM vk)
{
    if (vk != VK_UP || !is_grid_edit(GetFocus()))
        return FALSE;
    grid_edit_hwnd = NULL;
    grid_hwnd = NULL;
    SetRectEmpty(&grid_rect);
    grid_poll();
    return TRUE;
}
