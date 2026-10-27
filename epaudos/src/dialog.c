/* SPDX-License-Identifier: GPL-3.0-or-later
 * Boîtes de dialogue CUA en mode texte : reconnaissance du contrôle sous le
 * curseur, de son libellé, de sa valeur, de son groupe et de la boîte.
 *
 * Les applications construites sur « Character Windows » placent le curseur
 * matériel dans le contrôle actif et dessinent les mêmes motifs :
 *   [texte·····]  champ (Word) ; zone encadrée de 3 rangées précédée du libellé (EDIT)
 *   [X]  [ ]      case à cocher, curseur entre les crochets
 *   (*)  ( )      bouton radio (puce 07h), curseur entre les parenthèses
 *   ┌──┐ ╔══╗     bouton de 3 rangées, double quand il a le focus (Word)
 *   < OK >        bouton (EDIT, QBasic)
 *   ┌──────┐      liste : cadre plus haut, libellé au-dessus ou dans le bord
 * La boîte elle-même est la zone de même fond que le contrôle ; son titre est
 * dans son bord supérieur (EDIT) ou dans une barre d'une autre couleur juste
 * au-dessus (Word). Une boîte touchant un bord de l'écran est refusée : c'est
 * alors une fenêtre de document, dont le texte peut contenir ces motifs.
 * Les caractères sont ceux de la page CP850, comme toute la mémoire vidéo lue.
 */
#include "dialog.h"

static const WORD *dl_s;
static int dl_w, dl_h;

static int dl_c(int x, int y)
{ return x < 0 || y < 0 || x >= dl_w || y >= dl_h ? 0 : dl_s[y*dl_w+x] & 255; }
static int dl_bg(int x, int y)
{ return x < 0 || y < 0 || x >= dl_w || y >= dl_h ? -1 : (dl_s[y*dl_w+x] >> 12) & 15; }
/* Blancs, y compris le point médian (FAh) qui remplit les champs de Word. */
static int dl_blank(int c) { return c == 0 || c == 32 || c == 255 || c == 250; }
/* Traits CP850 seulement : 181-184, 198-199, 207-216 y sont des lettres. */
static int dl_frame(int c)
{
    return c == 179 || c == 180 || (c >= 185 && c <= 188) || (c >= 191 && c <= 197) ||
           (c >= 200 && c <= 206) || c == 217 || c == 218;
}
static int dl_block(int c) { return (c >= 176 && c <= 178) || c == 219 || c == 220 || c == 223 || c == 254; }
static int dl_vert(int c) { return c == 179 || c == 186; }
static int dl_tl(int c) { return c == 218 || c == 201; }
static int dl_tr(int c) { return c == 191 || c == 187; }
static int dl_bl(int c) { return c == 192 || c == 200; }
static int dl_delim(int c) { return c == '[' || c == ']' || c == '(' || c == ')' || c == '<' || c == '>'; }
static int dl_stop(int c) { return (c > 0 && c < 32) || dl_frame(c) || dl_block(c) || dl_delim(c); }

/* Texte d'une rangée : traits, blocs et remplissages deviennent des blancs,
   les blancs répétés sont réduits, les bords supprimés. */
static void dl_text(int y, int x0, int x1, char *out, int cap)
{
    int n = 0, x, c, sp = 0;
    for (x = x0; x <= x1 && n < cap-2; ++x) {
        c = dl_c(x,y);
        if (c < 32 || dl_blank(c) || dl_frame(c) || dl_block(c)) { sp = n > 0; continue; }
        if (sp) out[n++] = ' ';
        sp = 0; out[n++] = (char)c;
    }
    out[n] = 0;
}
static void dl_colon(char *s)
{
    int n = 0;
    while (s[n]) ++n;
    while (n && (s[n-1] == ':' || s[n-1] == ' ')) --n;
    s[n] = 0;
}
/* Libellé collé à gauche (au plus deux blancs) ou à droite d'un contrôle ;
   il s'arrête à deux blancs de suite, à un trait ou à un autre contrôle. */
static void dl_before(int x, int y, char *out, int cap)
{
    int e = x-1, s, c;
    out[0] = 0;
    while (e >= 0 && e > x-3 && dl_blank(dl_c(e,y))) --e;
    if (e < 0 || dl_blank(dl_c(e,y)) || dl_stop(dl_c(e,y))) return;
    for (s = e; s > 0; --s) {
        c = dl_c(s-1,y);
        if (dl_stop(c) || (dl_blank(c) && dl_blank(dl_c(s-2,y)))) break;
    }
    dl_text(y,s,e,out,cap); dl_colon(out);
}
static void dl_after(int x, int y, char *out, int cap)
{
    int s = x, e, c;
    out[0] = 0;
    while (s < dl_w && s < x+2 && dl_blank(dl_c(s,y))) ++s;
    if (s >= dl_w || dl_blank(dl_c(s,y)) || dl_stop(dl_c(s,y))) return;
    for (e = s; e+1 < dl_w; ++e) {
        c = dl_c(e+1,y);
        if (dl_stop(c) || (dl_blank(c) && dl_blank(dl_c(e+2,y)))) break;
    }
    dl_text(y,s,e,out,cap); dl_colon(out);
}
/* Libellé au-dessus d'un cadre (EDIT : liste, champ), à une ou deux rangées :
   premier texte dans les colonnes du cadre, remonté au début de son mot. */
static void dl_above(int xl, int xr, int t, char *out, int cap)
{
    int r, i;
    out[0] = 0;
    for (r = t-1; r >= t-2 && r >= 0; --r) {
        for (i = xl; i < xr && dl_blank(dl_c(i,r)); ++i) {}
        if (i >= xr) continue;
        while (i > 0 && !dl_blank(dl_c(i-1,r)) && !dl_stop(dl_c(i-1,r))) --i;
        dl_after(i,r,out,cap);
        return;
    }
}
/* Délimiteurs open...close sur la rangée, de part et d'autre du curseur. */
static int dl_pair(int x, int y, int open, int close, int *xl, int *xr)
{
    int i, c;
    for (i = x-1; i >= 0 && i > x-82; --i) {
        c = dl_c(i,y);
        if (c == open) break;
        if (c == close || dl_frame(c)) return 0;
    }
    if (i < 0 || dl_c(i,y) != open) return 0;
    *xl = i;
    for (i = x; i < dl_w && i < x+82; ++i) {
        c = dl_c(i,y);
        if (c == close) { *xr = i; return 1; }
        if (c == open || dl_frame(c)) return 0;
    }
    return 0;
}
/* Cadre fermé autour du curseur, par son bord gauche : coins et hauteur. */
static int dl_box(int x, int y, int *xl, int *xr, int *t, int *b)
{
    int i, c;
    for (i = x-1; i >= 0; --i) {
        c = dl_c(i,y);
        if (dl_vert(c)) break;
        if (dl_frame(c)) return 0;
    }
    if (i < 0) return 0;
    *xl = i;
    for (*t = y; *t > 0 && dl_vert(dl_c(i,*t-1)); --*t) {}
    if (!dl_tl(dl_c(i,--*t))) return 0;
    for (*b = y; *b+1 < dl_h && dl_vert(dl_c(i,*b+1)); ++*b) {}
    if (!dl_bl(dl_c(i,++*b))) return 0;
    for (i = *xl+1; i < dl_w && !dl_tr(dl_c(i,*t)); ++i) {}
    if (i >= dl_w || i <= x) return 0;
    *xr = i;
    return 1;
}
/* Titre d'un cadre de groupe contenant le contrôle (Position, Afficher...). */
static void dl_group(ED_DIALOG *d, int y)
{
    int i, gy, c, e;
    for (i = d->x0-1; i > d->left; --i) {
        c = dl_c(i,y);
        if (dl_vert(c)) break;
        if (dl_frame(c)) return;
    }
    if (i <= d->left) return;
    for (gy = y; gy > d->top && dl_vert(dl_c(i,gy-1)); --gy) {}
    if (!dl_tl(dl_c(i,--gy))) return;
    for (e = i+1; e < dl_w && !dl_tr(dl_c(e,gy)); ++e) {}
    dl_text(gy,i+1,e-1,d->group,sizeof(d->group));
    if (d->group[0]) { d->gx = i; d->gy = gy; }
}

int dlg_find(const WORD *cells, int cols, int rows, int x, int y, ED_DIALOG *d)
{
    int xl, xr, t, b, i, bg, L, R, T, B, tb, ok;
    WORD h;
    dl_s = cells; dl_w = cols; dl_h = rows;
    d->kind = DK_NONE; d->state = 0; d->gx = d->gy = -1; d->y = y;
    d->title[0] = d->label[0] = d->value[0] = d->group[0] = 0;
    if (x < 1 || x >= cols-1) return 0;
    if (dl_c(x-1,y) == '(' && dl_c(x+1,y) == ')') {
        d->kind = DK_RADIO; d->state = (BYTE)!dl_blank(dl_c(x,y));
        d->x0 = x-1; d->x1 = x+1; dl_after(x+2,y,d->label,sizeof(d->label));
    } else if (dl_c(x-1,y) == '[' && dl_c(x+1,y) == ']') {
        d->kind = DK_CHECK; d->state = (BYTE)!dl_blank(dl_c(x,y));
        d->x0 = x-1; d->x1 = x+1; dl_after(x+2,y,d->label,sizeof(d->label));
    } else if (dl_pair(x,y,'[',']',&xl,&xr)) {
        d->kind = DK_FIELD; d->x0 = xl; d->x1 = xr;
        dl_text(y,xl+1,xr-1,d->value,sizeof(d->value));
        dl_before(xl,y,d->label,sizeof(d->label));
    } else if (dl_pair(x,y,'<','>',&xl,&xr)) {
        d->kind = DK_BUTTON; d->x0 = xl; d->x1 = xr;
        dl_text(y,xl+1,xr-1,d->label,sizeof(d->label));
    } else if (dl_box(x,y,&xl,&xr,&t,&b)) {
        d->x0 = xl; d->x1 = xr;
        dl_text(y,xl+1,xr-1,d->value,sizeof(d->value));
        if (b-t == 2) {
            /* Trois rangées : bouton (Word), ou champ d'EDIT dont le libellé
               est à gauche ou au-dessus. Un bouton a toujours un texte et
               reste étroit ; le double trait marque le bouton actif de Word. */
            dl_before(xl,y,d->label,sizeof(d->label));
            if (!d->label[0] && (!d->value[0] || xr-xl > 30))
                dl_above(xl,xr,t,d->label,sizeof(d->label));
            if (d->label[0] && dl_c(xl,y) != 186) d->kind = DK_FIELD;
            else {
                d->kind = DK_BUTTON;
                for (i = 0; (d->label[i] = d->value[i]) != 0; ++i) {}
                d->value[0] = 0;
            }
        } else {
            d->kind = DK_LIST; d->y = t;
            dl_text(t,xl+1,xr-1,d->label,sizeof(d->label));
            if (!d->label[0]) dl_above(xl,xr,t,d->label,sizeof(d->label));
        }
    } else return 0;
    if (!d->label[0] && d->kind == DK_BUTTON) return 0;
    /* La boîte : cellules de même fond que le bord gauche du contrôle. */
    bg = dl_bg(d->x0,y);
    for (L = d->x0; L > 0 && dl_bg(L-1,y) == bg; --L) {}
    for (R = d->x1; R+1 < cols && dl_bg(R+1,y) == bg; ++R) {}
    for (T = y; T > 0 && dl_bg(L,T-1) == bg; --T) {}
    for (B = y; B+1 < rows && dl_bg(L,B+1) == bg; ++B) {}
    if (L == 0 || R == cols-1 || T == 0 || R-L < 10) return 0;
    if (dl_tl(dl_c(L,T)) && dl_tr(dl_c(R,T))) {
        dl_text(T,L+1,R-1,d->title,sizeof(d->title));
        d->left = L+1; d->right = R-1; d->top = T+1;
        d->bottom = dl_bl(dl_c(L,B)) ? B-1 : B;
    } else {
        d->left = L; d->right = R; d->top = T; d->bottom = B;
        /* Barre de titre de Word : une rangée d'un autre fond, exactement
           aussi large que la boîte. */
        tb = dl_bg(L,T-1); ok = tb != bg && dl_bg(L-1,T-1) != tb && dl_bg(R+1,T-1) != tb;
        for (i = L; ok && i <= R; ++i) if (dl_bg(i,T-1) != tb) ok = 0;
        if (ok) dl_text(T-1,L,R,d->title,sizeof(d->title));
    }
    if (d->kind == DK_RADIO || d->kind == DK_CHECK) dl_group(d,y);
    h = (WORD)(L*131+T*7+R);
    for (i = 0; d->title[i]; ++i) h = (WORD)((h<<1 | h>>15) ^ (BYTE)d->title[i]);
    d->id = h;
    return 1;
}

void dlg_static(const WORD *cells, int cols, int rows, const ED_DIALOG *d, char *out, int cap)
{
    int y, x, c, ok, n = 0;
    char row[ED_COLS+1];
    dl_s = cells; dl_w = cols; dl_h = rows; out[0] = 0;
    for (y = d->top; y <= d->bottom; ++y) {
        ok = 1;
        for (x = d->left; x <= d->right && ok; ++x) {
            c = dl_c(x,y);
            if (dl_frame(c) || dl_delim(c)) ok = 0;
        }
        if (!ok) continue;
        dl_text(y,d->left,d->right,row,sizeof(row));
        for (x = 0; row[x] && n < cap-2; ++x) out[n++] = row[x];
        if (row[0] && n < cap-1) out[n++] = ' ';
        out[n] = 0;
    }
    while (n && out[n-1] == ' ') out[--n] = 0;
}
