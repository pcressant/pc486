/* SPDX-License-Identifier: GPL-3.0-or-later
 * Épaudos : moteur borné, sans allocation, sans DOS et sans attente série.
 * Le même fichier est exécuté par le banc natif.
 */
#include "core.h"
#include "br850.h"
#include "dialog.h"

volatile BYTE ed_windows, ed_disabled, ed_save, ed_resume;
/* Mode invité : machine DOS de Windows, parole relayée par Épaulard. */
volatile BYTE ed_guest;
WORD ed_debug[12]; /* État consultable dans les scénarios QEMU. */
BYTE ed_cfg[ED_CFG] = {3,8,1,1,0,0,1};
int ed_cols = 80, ed_rows = 25, ed_x, ed_y, ed_flat;
char ed_line[ED_TEXT];
static WORD screen[ED_CELLS], hashes[ED_ROWS];
/* Empreintes du texte seul (sans attributs) : écho écran et rangée du curseur. */
static WORD thashes[ED_ROWS], base_th[ED_ROWS], last_line[ED_COLS];
static int dirty_ticks, echo_wait; /* Écho écran : attendre un écran stable. */
static unsigned grace; /* Dernière touche de frappe ou de déplacement consommée. */
static WORD ahashes[ED_ROWS]; /* Disposition des attributs : une surbrillance qui bouge la change. */
static BYTE rowchg[ED_ROWS], nonblank[ED_ROWS];
static BYTE bg_counts[ED_ROWS][8];
static WORD bg_hashes[ED_ROWS][8];
static BYTE swallowed[128], prefix, pause_skip, insert, ctrl, shift, alt;
static volatile BYTE commands[16], cin, cout, stop, nav_word;
/* Dernière touche transmise à l'application : classe et instant (ISR clavier). */
#define KC_NONE 0
#define KC_TYPE 1
#define KC_BS 2
#define KC_DEL 3
#define KC_NAVV 4
#define KC_NAVH 5
#define KC_ENTER 6
#define KC_OTHER 7
static volatile BYTE key_class;
static volatile unsigned key_tick;
static unsigned ticks, menu_tick, probe;
static unsigned user_tick;
static int ready, rx, ry, last_x, last_y, left, right, top, bottom;
static int focus_hi, focus_x, focus_y;
static int menu, item, all, all_row, awaiting, echo_id, epoch;
static int pend_row = -1, echo_hold, nchg;
/* Un cadre d'application vient d'apparaître (Word, EDIT) : le prochain écho
   couvre tout l'écran, barre de menus et ligne d'état comprises. */
static int wide, framed;
static int br_width, br_offset, br_command, br_need, br_used, br_keys;
static int br_routable, br_origin, br_row, asleep;
static BYTE br_rx[16], br_tx[164];
static int br_len, br_pos, br_dirty, br_cursor;
static unsigned br_rx_tick;
static char speech[2048], braille[ED_TEXT];
static int speech_n, speech_pos;
static const char *const names[ED_CFG] = {
    "Débit", "Hauteur", "Voix", "Ponctuation", "Écho clavier", "Majuscules", "Écho écran"
};
static const BYTE maxima[ED_CFG] = {9,15,6,2,3,1,1};
/* Boîte de dialogue CUA sous le curseur (dialog.c) et dernière annoncée. */
static ED_DIALOG dlg, shown;
static int dlg_found, dlg_on, dlg_wait, dlg_lost;
static ED_DIALOG cand; /* Contrôle vu au passage précédent, pas encore annoncé. */
static char dtext[ED_TEXT+64];
static void dlg_say(int full);

static int length(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void copy(char *d, const char *s, int cap)
{ int i = 0; while (s[i] && i < cap-1) { d[i] = s[i]; ++i; } d[i] = 0; }
static void append(char *d, const char *s, int cap)
{ int n = length(d); copy(d+n,s,cap-n); }
static void number(char *s, int n)
{
    char b[6]; int i = 0, j;
    do { b[i++] = (char)('0'+n%10); n /= 10; } while (n);
    for (j = 0; j < i; ++j) s[j] = b[i-j-1]; s[i] = 0;
}
static int blank(int c) { return c == 0 || c == 32 || c == 255 || c == 9; }
static int ch(int x, int y) { return screen[y*ed_cols+x] & 255; }
static void cancel(void)
{
    speech_pos = speech_n = 0; awaiting = all = 0;
    epoch = (epoch+1)%26;
    speech[speech_n++] = 24;
    speech[speech_n] = 0;
}
static void br_text(const char *s, int cursor)
{
    copy(braille,s,sizeof(braille)); br_cursor = cursor;
    if (cursor >= 0 && br_width && (cursor < br_offset || cursor >= br_offset+br_width))
        br_offset = cursor/br_width*br_width;
    if (br_offset > length(s)) br_offset = 0;
    br_dirty = 1;
}
static void voice(void);
static void say_text(const char *s)
{
    int i, j;
    const char *word;
    for (i = 0; s[i] && speech_n < (int)sizeof(speech)-40; ++i) {
        word = 0;
        if (ed_cfg[3] == 2) switch ((BYTE)s[i]) {
        case '.': word=" point "; break; case ',': word=" virgule "; break;
        case ':': word=" deux points "; break; case ';': word=" point virgule "; break;
        case '!': word=" point d'exclamation "; break; case '?': word=" point d'interrogation "; break;
        case '(': word=" parenthèse ouvrante "; break; case ')': word=" parenthèse fermante "; break;
        case '[': word=" crochet ouvrant "; break; case ']': word=" crochet fermant "; break;
        case '{': word=" accolade ouvrante "; break; case '}': word=" accolade fermante "; break;
        case '+': word=" plus "; break; case '-': word=" moins "; break;
        case '*': word=" astérisque "; break; case '/': word=" barre oblique "; break;
        case '\\': word=" barre oblique inverse "; break; case '=': word=" égal "; break;
        case '<': word=" inférieur "; break; case '>': word=" supérieur "; break;
        case '"': word=" guillemet "; break; case '#': word=" dièse "; break;
        case '&': word=" et commercial "; break; case '_': word=" souligné "; break;
        case '@': word=" arobase "; break; case '%': word=" pour cent "; break;
        }
        if (word) for (j=0; word[j]; ++j) speech[speech_n++]=word[j];
        else if (!ed_cfg[3] && (s[i]=='.'||s[i]==','||s[i]==':'||s[i]==';'||s[i]=='!'||s[i]=='?'))
            speech[speech_n++]=' ';
        else speech[speech_n++] = s[i] == '@' ? ' ' : s[i];
    }
    speech[speech_n++] = '\r'; speech[speech_n] = 0;
}
/* Annonce : coupe la parole en cours, renvoie les réglages de voix puis le texte. */
static void say(const char *s) { voice(); say_text(s); }
/* Écho écran : s'ajoute à la file sans rien couper. L'écho `@E` qui suit chaque
   ligne tient l'extension burst-interrupt d'Apolo-emu à l'écart : sans lui, une
   ligne arrivant 55 ms après la précédente l'interromprait. L'octet renvoyé
   (minuscule, distinct du dire tout) est ignoré. Faute de place, la ligne est
   perdue : Ctrl vide la file. */
static void say_more(const char *s)
{
    int i;
    if (speech_pos >= speech_n) speech_pos = speech_n = 0;
    else if (speech_pos) {
        for (i = speech_pos; i < speech_n; ++i) speech[i-speech_pos] = speech[i];
        speech_n -= speech_pos; speech_pos = 0;
    }
    if (speech_n+length(s)+8 > (int)sizeof(speech)-40) return;
    say_text(s);
    speech[speech_n++] = '@'; speech[speech_n++] = 'E';
    speech[speech_n++] = (char)('a'+epoch); speech[speech_n++] = '\r';
    speech[speech_n] = 0;
}
static void message(const char *s) { say(s); br_text(s,-1); br_routable = 0; }
static void br_screen(const char *s, int cursor, int origin, int row)
{
    br_text(s,cursor); br_origin = origin; br_row = row; br_routable = 1;
}
static void voice(void)
{
    char buf[32], n[6]; int i;
    static const char codes[3] = {'W','F','V'};
    cancel(); buf[0] = 0;
    for (i = 0; i < 3; ++i) {
        append(buf,"@",sizeof(buf)); n[0] = codes[i]; n[1] = 0;
        append(buf,n,sizeof(buf)); number(n,ed_cfg[i]); append(buf,n,sizeof(buf));
        append(buf,"\r",sizeof(buf));
    }
    copy(speech+1,buf,sizeof(speech)-1); speech_n = length(speech);
}
static void say_char(int c)
{
    char s[32]; const char *name = 0; int n;
    switch (c) {
    case ' ': name="espace"; break; case '.': name="point"; break;
    case ',': name="virgule"; break; case ':': name="deux points"; break;
    case ';': name="point virgule"; break; case '!': name="point d'exclamation"; break;
    case '?': name="point d'interrogation"; break; case '(': name="parenthèse ouvrante"; break;
    case ')': name="parenthèse fermante"; break; case '+': name="plus"; break;
    case '-': name="moins"; break; case '*': name="astérisque"; break;
    case '/': name="barre oblique"; break; case '\\': name="barre oblique inverse"; break;
    case '=': name="égal"; break; case '"': name="guillemet"; break;
    case '<': name="inférieur"; break; case '>': name="supérieur"; break;
    case '@': name="arobase"; break;
    }
    if (name) { say(name); return; }
    s[0] = (char)c; s[1] = 0;
    if (ed_cfg[5] && c >= 'A' && c <= 'Z') {
        copy(s,"majuscule ",sizeof(s)); n = length(s); s[n] = (char)c; s[n+1] = 0;
    }
    say(s);
}
static void line(int y, int lo, int hi)
{
    int i, n = 0, c;
    for (i = lo; i <= hi && n < ED_TEXT-1; ++i) {
        c = ch(i,y); ed_line[n++] = (char)(c < 32 || c == 255 ? 32 : c);
    }
    while (n && ed_line[n-1] == ' ') --n;
    ed_line[n] = 0;
}
static void speak_line(int y)
{
    line(y,left,right); say(ed_line[0] ? ed_line : "vide");
    br_screen(ed_line,(ed_flat ? rx : focus_hi ? focus_x : ed_x)-left,left,y);
}
/* Cadres OEM simples et doubles. On retient le plus petit rectangle fermé
   contenant le curseur ; aucune heuristique de couleur ne déplace le curseur. */
static int tl(int c) { return c == 218 || c == 201 || c == 213 || c == 214; }
static int tr(int c) { return c == 191 || c == 187 || c == 183 || c == 184; }
static int bl(int c) { return c == 192 || c == 200 || c == 211 || c == 212; }
static int br(int c) { return c == 217 || c == 188 || c == 189 || c == 190; }
static int horizontal(int c) { return c == 196 || c == 205; }
static int vertical(int c)
{
    return c == 179 || c == 186 || c == 180 || c == 181 || c == 182 ||
           c == 185 || c == 195 || c == 198 || c == 199 || c == 204;
}
static void bounds(void)
{
    int x,y,x2,y2,i,ok,ax = focus_hi ? focus_x : ed_x, ay = focus_hi ? focus_y : ed_y;
    left = 0; right = ed_cols-1; top = 0; bottom = ed_rows-1;
    for (y = ay; y >= 0; --y) for (x = ax; x >= 0; --x) {
        if (!tl(ch(x,y))) continue;
        for (x2 = x+1; x2 < ed_cols && !tr(ch(x2,y)); ++x2) {}
        if (x2 <= ax || x2 >= ed_cols || !tr(ch(x2,y))) continue;
        for (y2 = y+1; y2 < ed_rows; ++y2) {
            if (!vertical(ch(x,y2)) || !vertical(ch(x2,y2))) break;
        }
        if (y2 <= ay || y2 >= ed_rows || !bl(ch(x,y2)) || !br(ch(x2,y2))) continue;
        ok = 1;
        for (i = x+1; i < x2; ++i) if (!horizontal(ch(i,y2))) ok = 0;
        if (ok) { left = x+1; right = x2-1; top = y+1; bottom = y2-1; return; }
    }
    /* Cadre ouvert (EDIT, QBasic) : bord haut avec ses coins et bord gauche
       continus ; le bord droit porte une barre de défilement et le bas est
       occupé par la barre horizontale ou la ligne d'état. */
    for (x = ax; x >= 0; --x) {
        if (!vertical(ch(x,ay))) continue;
        for (y = ay; y > 0 && vertical(ch(x,y)); --y) {}
        if (!tl(ch(x,y)) && ch(x,y) != 254) continue;
        for (x2 = x+1; x2 < ed_cols && !tr(ch(x2,y)); ++x2) if (!horizontal(ch(x2,y)) && !blank(ch(x2,y)) && !(ch(x2,y) > 32 && ch(x2,y) < 176)) break;
        /* Word : le bord haut (case de fermeture ■) va jusqu'au bord de
           l'écran et finit par le bouton d'agrandissement (12h) ; la dernière
           colonne porte la barre de défilement. */
        if (x2 >= ed_cols-1 && !tr(ch(ed_cols-1,y))) x2 = ed_cols-1;
        else if (x2 >= ed_cols || !tr(ch(x2,y))) continue;
        if (x2 <= ax) continue;
        for (y2 = ay; y2+1 < ed_rows && vertical(ch(x,y2+1)); ++y2) {}
        left = x+1; right = x2-1; top = y+1; bottom = y2; return;
    }
}
static void enqueue(int c)
{
    WORD flags = hw_lock();
    BYTE next = (BYTE)((cin+1)&15);
    if (next != cout) { commands[cin] = (BYTE)c; cin = next; }
    ed_debug[6] = (WORD)c; ed_debug[7] = cin;
    hw_unlock(flags);
}
static int step(int p, int dir)
{
    int x = p%ed_cols, y = p/ed_cols;
    x += dir;
    if (x < left) { if (y == top) return p; x = right; --y; }
    if (x > right) { if (y == bottom) return p; x = left; ++y; }
    return y*ed_cols+x;
}
static void review(int unit, int dir)
{
    int p, end, start, next;
    if (!ed_flat) { rx = focus_hi ? focus_x : ed_x; ry = focus_hi ? focus_y : ed_y; }
    if (rx < left || rx > right || ry < top || ry > bottom) { rx = left; ry = top; }
    if (unit == 0) {
        ry += dir; if (ry < top) ry = top; if (ry > bottom) ry = bottom;
        if (dir) rx = left;
        speak_line(ry); return;
    }
    p = ry*ed_cols+rx; start = top*ed_cols+left; end = bottom*ed_cols+right;
    if (unit == 2) p = step(p,dir);
    else if (dir > 0) {
        while (p < end && !blank(screen[p]&255)) { next = step(p,1); if (next == p) break; p = next; }
        while (p < end && blank(screen[p]&255)) { next = step(p,1); if (next == p) break; p = next; }
    } else {
        if (dir < 0 && p > start) p = step(p,-1);
        while (p > start && blank(screen[p]&255)) { next = step(p,-1); if (next == p) break; p = next; }
        while (p > start && !blank(screen[p-1]&255) && p%ed_cols > left) --p;
    }
    if (p < start) p = start; if (p > end) p = end;
    rx = p%ed_cols; ry = p/ed_cols;
    if (rx < left) rx = left; if (rx > right) rx = right;
    line(ry,left,right);
    if (unit == 2) {
        say_char(ch(rx,ry));
    } else {
        char s[ED_TEXT]; int n = 0;
        for (p = rx; p <= right && !blank(ch(p,ry)); ++p) s[n++] = (char)ch(p,ry);
        s[n] = 0; say(n ? s : "espace");
    }
    br_screen(ed_line,rx-left,left,ry);
}
static void menu_read(void)
{
    char b[80],n[6];
    static const char *const punctuation[3] = {"aucune","certaines","toutes"};
    static const char *const echo[4] = {"caractères","mots","caractères et mots","aucun"};
    copy(b,"Épaudos : ",sizeof(b)); append(b,names[item],sizeof(b));
    append(b," ",sizeof(b));
    if (item == 3) append(b,punctuation[ed_cfg[item]],sizeof(b));
    else if (item == 4) append(b,echo[ed_cfg[item]],sizeof(b));
    else if (item >= 5) append(b,ed_cfg[item] ? "oui" : "non",sizeof(b));
    else { number(n,ed_cfg[item]); append(b,n,sizeof(b)); }
    hw_overlay(ed_rows-1,b); message(b); menu_tick = ticks;
}
static void command(int c)
{
    int i;
    if (c == 'e') { menu = 1; item = 0; menu_read(); return; }
    if (menu) {
        if (c == 1 || c == 28) {
            menu = 0; hw_restore(); ed_save = 1; voice(); echo_hold = 2; return;
        }
        if (c == 72) item = (item+ED_CFG-1)%ED_CFG;
        if (c == 80) item = (item+1)%ED_CFG;
        if (c == 75 && ed_cfg[item] > (item == 0 || item == 2 ? 1 : 0)) --ed_cfg[item];
        if (c == 77 && ed_cfg[item] < maxima[item]) ++ed_cfg[item];
        menu_read(); return;
    }
    switch (c) {
    case 'h': message("Épaudos. Pavé : 7 8 9 lignes, 4 5 6 mots, 1 2 3 caractères. Inser moins revue. Inser plus écho écran. Inser 5 curseur. Inser Haut ligne. Inser Bas dire tout. Inser E réglages. Ctrl silence."); break;
    case 'f': ed_flat = !ed_flat; rx = ed_x; ry = ed_y;
        message(ed_flat ? "Revue à plat" : "Revue normale"); break;
    case 'p': ed_cfg[6] = (BYTE)!ed_cfg[6]; pend_row = -1;
        message(ed_cfg[6] ? "Écho écran activé" : "Écho écran désactivé"); break;
    case 'c': if (dlg_found) { dlg_say(1); shown = dlg; dlg_on = 1; break; }
        rx = focus_hi ? focus_x : ed_x; ry = focus_hi ? focus_y : ed_y; speak_line(ry); break;
    case 'a': all = 1; all_row = ed_flat ? ry : ed_y; awaiting = 0; break;
    case 'b': all = 1; all_row = top; awaiting = 0; break;
    case 't': if (dlg_found && dlg.title[0]) { message(dlg.title); break; }
        line(top,left,right); message(ed_line); break;
    case 's': line(bottom,left,right); message(ed_line); break;
    case 'd': {
        char b[80],n[6];
        copy(b,"Ligne ",sizeof(b)); number(n,ed_y+1); append(b,n,sizeof(b));
        append(b," colonne ",sizeof(b)); number(n,ed_x+1); append(b,n,sizeof(b));
        append(b," attribut ",sizeof(b)); number(n,screen[ed_y*ed_cols+ed_x]>>8); append(b,n,sizeof(b));
        message(b); break;
    }
    default:
        for (i = 0; i < 9; ++i) if (c == 16+i) review(i/3,(i%3)-1);
    }
}

/* Classe d'une touche laissée à l'application : le suivi du curseur s'en sert
   pour décider ce qu'un changement d'écran signifie (ligne, caractère, frappe,
   effacement ou sortie de programme), sans deviner d'après le seul curseur. */
static BYTE classify(int code, int ext, BYTE shifts)
{
    int pad = code >= 71 && code <= 83;
    if (alt) return KC_OTHER;
    if (code == 14) return ctrl ? KC_OTHER : KC_BS;
    if (code == 28) return KC_ENTER;
    /* Pavé sans préfixe E0 : chiffres si Verr Num (ou Maj) l'impose. */
    if (pad && !ext && (((shifts&32) != 0) != (shift != 0))) return KC_TYPE;
    if (pad) {
        if (code == 83) return KC_DEL;
        if (code == 72 || code == 80 || code == 73 || code == 81) return KC_NAVV;
        if (code == 71 || code == 79) return ctrl ? KC_NAVV : KC_NAVH;
        if (code == 75 || code == 77) return KC_NAVH;
        return KC_TYPE; /* moins et plus du pavé */
    }
    if (ctrl) return KC_OTHER;
    if ((code >= 2 && code <= 13) || (code >= 16 && code <= 27) || (code >= 30 && code <= 41) ||
        code == 43 || (code >= 44 && code <= 53) || code == 55 || code == 57 || code == 86)
        return KC_TYPE;
    return KC_OTHER;
}
/* Scancodes AT set 1, appelés par le BIOS avant KEYB FR. Les préfixes E0/E1
   restent intégralement transmis ; seules les touches réellement prises sont
   absorbées, relâchement compris. Le pavé et les flèches E0 restent distincts. */
int ed_scan(BYTE scan, BYTE shifts)
{
    int code = scan&127, up = scan&128, ext, c = 0;
    if (pause_skip) { --pause_skip; return 0; }
    if (scan == 0xe0) { prefix = 1; return 0; }
    if (scan == 0xe1) { pause_skip = 5; prefix = 0; return 0; }
    ext = prefix; prefix = 0;
    if (ext && (code == 42 || code == 54)) return 0;
    if (code == 29) { ctrl = (BYTE)!up; if (!up) stop = 1; }
    if (code == 42 || code == 54) shift = (BYTE)!up;
    if (code == 56) alt = (BYTE)!up;
    if (up && swallowed[code]) { swallowed[code] = 0; if (code == 82) insert = 0; return 1; }
    if ((ed_windows && hw_guest(1) != ED_RUN) || ed_disabled) { insert = 0; return 0; }
    if (code == 82 && (ext || !(shifts&32)) && !ctrl && !alt && !shift) {
        insert = (BYTE)!up; swallowed[82] = (BYTE)!up; return 1;
    }
    if (up) return 0;
    user_tick = ticks;
    nav_word = (BYTE)(ctrl && (code == 75 || code == 77));
    if (menu) { enqueue(code); swallowed[code] = 1; return 1; }
    if (!ctrl && !alt && !shift) {
        if (insert) {
            switch (code) {
            case 35: c = 'h'; break; case 18: c = 'e'; break;
            case 74: c = 'f'; break; case 78: c = 'p'; break;
            case 76: case 72: c = 'c'; break;
            case 80: c = 'a'; break; case 48: case 24: c = 'b'; break;
            case 20: c = 't'; break; case 81: c = 's'; break; case 32: c = 'd'; break;
            }
        } else if (!ext && !(shifts&32)) {
            switch (code) {
            case 71: c = 16; break; case 72: c = 17; break; case 73: c = 18; break;
            case 75: c = 19; break; case 76: c = 20; break; case 77: c = 21; break;
            case 79: c = 22; break; case 80: c = 23; break; case 81: c = 24; break;
            }
        }
    }
    if (c) { stop = 1; enqueue(c); swallowed[code] = 1; return 1; }
    /* Une frappe d'application interrompt le dire tout, sans être absorbée. */
    key_class = classify(code,ext,shifts); key_tick = ticks;
    if (all) stop = 1;
    return 0;
}
static int key_peek(unsigned *when)
{
    *when = key_tick;
    return (unsigned)(ticks-*when) < 18 ? key_class : KC_NONE;
}
static void key_take(unsigned when)
{
    WORD flags = hw_lock();
    if (key_tick == when) key_class = KC_NONE;
    hw_unlock(flags);
}
static void br_queue(int cmd, const BYTE *data, int n)
{
    int i; br_pos = 0; br_len = 0;
    br_tx[br_len++] = 27; br_tx[br_len++] = (BYTE)cmd;
    for (i = 0; i < n; ++i) {
        br_tx[br_len++] = data[i]; if (data[i] == 27) br_tx[br_len++] = 27;
    }
}
static void br_response(void)
{
    int released;
    if (br_command == 1 && br_rx[0] && br_rx[0] <= 80) { br_width = br_rx[0]; br_dirty = 1; }
    if (br_command == 0x24) {
        released = br_keys & ~br_rx[0];
        if (released) {
            if (br_keys == 2) br_offset -= br_width;
            if (br_keys == 16) br_offset += br_width;
            if (br_keys == 8) { br_offset = 0; if (!menu) enqueue('c'); }
            if (br_keys == 1 || br_keys == 4) {
                if (menu) enqueue(br_keys == 1 ? 72 : 80);
                else { ed_flat = 1; enqueue(br_keys == 1 ? 16 : 18); }
            }
            if (br_offset < 0) br_offset = 0;
            if (br_offset >= length(braille)) br_offset = length(braille)/br_width*br_width;
            br_dirty = 1; br_keys = 0;
        } else br_keys = br_rx[0];
    }
    if (br_command == 0x27 && br_rx[0] && br_rx[0] <= br_width && br_routable && !menu) {
        ed_flat = 1; rx = br_origin+br_offset+br_rx[0]-1; ry = br_row;
        if (rx > right) rx = right;
        review(2,0);
    }
}
static void br_receive(int b)
{
    if (!br_need) {
        if (!br_command) { if (b == 27) br_command = -1; return; }
        br_command = b; br_used = 0;
        switch (b) {
        case 1: case 0x24: case 0x27: case 0x16: case 0x17: case 0x34: br_need = 1; break;
        case 0x33: br_need = 2; break;
        case 0x22: br_need = br_width > 40 ? 10 : 5; break;
        case 0x84: br_need = 16; break; case 0x8a: br_need = 8; break;
        default: br_command = b == 27 ? -1 : 0;
        } return;
    }
    br_rx[br_used++] = (BYTE)b;
    if (br_used == br_need) { br_response(); br_command = br_need = 0; }
}
/* Émission : parole, trame braille en cours, sonde, nouvelle trame. Appelée aussi à
   la fin de chaque passage : sous Windows, une machine DOS inactive (invite qui
   attend une touche) n'a parfois plus de passage pendant des secondes, et la
   cellule de la frappe attendait jusque-là, la parole partie depuis longtemps. */
static void send(void)
{
    int i,b,n,k; BYTE data[80];
    for (i = 0; i < 64 && speech_pos < speech_n; ++i) {
        if (!hw_tx(0,(BYTE)speech[speech_pos])) break; ++speech_pos;
    }
    for (k = 0; k < 4; ++k) {
        while (br_pos < br_len) { if (!hw_tx(1,br_tx[br_pos])) return; ++br_pos; }
        if ((unsigned)(ticks-probe) >= 54) { data[0] = 1; br_queue(0x15,data,1); probe = ticks; continue; }
        if (!br_width || !br_dirty) return;
        n = length(braille);
        for (i = 0; i < br_width; ++i) {
            b = br_offset+i; data[i] = b < n ? br850[(BYTE)braille[b]] : 0;
            if (b == br_cursor) data[i] |= 0xc0;
        }
        br_queue(1,data,br_width); br_dirty = 0;
    }
}
static void serial(void)
{
    int i,b;
    for (i = 0; i < 32 && (b = hw_rx(0)) >= 0; ++i)
        if (awaiting && b == echo_id) { awaiting = 0; ++all_row; }
    if (br_command && (unsigned)(ticks-br_rx_tick) > 10) br_command = br_need = 0;
    for (i = 0; i < 32 && (b = hw_rx(1)) >= 0; ++i) { br_rx_tick = ticks; br_receive(b); }
    send();
}
void ed_suspend(void)
{
    int i;
    if (!asleep) { hw_quiet(); asleep = 1; }
    if (menu) hw_restore();
    menu = insert = all = awaiting = 0; speech_n = speech_pos = 0;
    br_pos = br_len = br_need = br_command = 0; cin = cout = 0; ready = 0;
    key_class = KC_NONE; pend_row = -1; echo_hold = 0; wide = framed = 0; dlg_on = dlg_wait = dlg_lost = 0;
    for (i = 0; i < 128; ++i) swallowed[i] = 0;
}
void ed_init(void)
{
    int i;
    for (i = 0; i < ED_CFG; ++i) if (ed_cfg[i] > maxima[i]) ed_cfg[i] = maxima[i];
    if (!ed_cfg[0]) ed_cfg[0] = 1; if (!ed_cfg[2]) ed_cfg[2] = 1;
    hw_serial(0,12); hw_serial(1,6); voice();
    asleep = 0;
    probe = (unsigned)-54; last_x = last_y = -1;
}
/* Écho de la frappe : les caractères apparus entre l'ancienne et la nouvelle
   position du curseur, ou le mot terminé par un blanc selon le réglage. */
static void typed(void)
{
    int from, to, row = ed_y, n = 0, p, flat;
    char s[ED_TEXT];
    if (ed_cfg[4] == 3) return;
    if (ed_y != last_y) {
        if (ed_x == 0) { row = last_y; from = last_x; to = last_x; }
        else { from = ed_x-1; to = ed_x-1; }
    } else if (ed_x > last_x) { from = last_x; to = ed_x-1; }
    else return;
    if (to-from > 40) from = to-40;
    if ((ed_cfg[4] == 1 || ed_cfg[4] == 2) && blank(ch(to,row))) {
        if (to <= left) return;
        flat = ed_flat; rx = to; ry = row; ed_flat = 1; review(1,-1); ed_flat = flat;
        return;
    }
    if (ed_cfg[4] == 1) return;
    if (from == to) { say_char(ch(to,row)); return; }
    for (p = from; p <= to; ++p) s[n++] = (char)(ch(p,row) < 32 ? 32 : ch(p,row));
    s[n] = 0; say(s);
}
/* Écho écran : lit les rangées dont le texte a changé sans qu'une touche de
   déplacement ou de frappe l'explique. La comparaison se fait avec le dernier
   écran stable (`base_th`) une fois que l'écran n'a plus changé pendant un
   passage, car un éditeur lent redessine sa fenêtre sur plusieurs passages ;
   une sortie continue est vidée de force tous les ED_SETTLE passages. Un
   défilement vertical est reconnu en comparant les empreintes décalées, pour
   ne lire que les rangées nouvelles. La rangée du curseur, souvent en cours
   d'écriture, n'est lue qu'une fois stable ou quittée. */
#define ED_SETTLE 6
/* Rangée déjà vue : même texte à la rangée décalée de k, ou à une rangée
   voisine (une ligne insérée ou supprimée au milieu d'un éditeur décale
   localement le bas de la fenêtre sans que ce soit un nouveau texte). */
static int seen(int y, int k)
{
    int o = y+k;
    if (o >= top && o <= bottom && thashes[y] == base_th[o]) return 1;
    if (o-1 >= top && o-1 <= bottom && thashes[y] == base_th[o-1]) return 1;
    if (o+1 >= top && o+1 <= bottom && thashes[y] == base_th[o+1]) return 1;
    return 0;
}
static int shift_find(void)
{
    int k, y, best = 0, bestn = ED_ROWS+1, n, live;
    for (k = 0; k <= bottom-top; ++k) {
        n = 0; live = 0;
        for (y = top; y <= bottom; ++y) {
            if (!nonblank[y]) continue;
            if (y+k <= bottom && thashes[y] == base_th[y+k]) live = 1;
            if (!seen(y,k)) ++n;
        }
        if (k && !live) continue;
        if (n < bestn) { bestn = n; best = k; }
        if (!n) break;
    }
    return best;
}
static void echo_row(int y)
{
    line(y,left,right); say_more(ed_line); br_screen(ed_line,-1,left,y);
}
/* Rangée hors de la fenêtre, sur toute la largeur ; filets, blocs et trames
   CP850 deviennent des blancs (les lettres accentuées de B5h-D8h restent). */
static int boxch(int c)
{
    return (c >= 176 && c <= 180) || (c >= 185 && c <= 188) || (c >= 191 && c <= 197) ||
           (c >= 200 && c <= 206) || (c >= 217 && c <= 220) || c == 223 || c == 254;
}
static void echo_plain(int y)
{
    int i, n;
    line(y,0,ed_cols-1);
    for (i = 0; ed_line[i]; ++i) if (boxch(ed_line[i]&255)) ed_line[i] = ' ';
    for (n = i; n && ed_line[n-1] == ' '; --n) {}
    ed_line[n] = 0;
    for (i = 0; ed_line[i] == ' '; ++i) {}
    if (ed_line[i]) { say_more(ed_line); br_screen(ed_line,-1,0,y); }
}
static void echo_screen(void)
{
    int y, k = 0, changed;
    if (echo_hold) { pend_row = -1; wide = 0; return; }
    if (nchg && dirty_ticks < ED_SETTLE) { echo_wait = 1; return; }
    if (wide) {
        /* Sur une machine rapide, Word dessine sa fenêtre en un passage : sans
           cela, seul l'intérieur (vide) du document serait comparé. Les rangées
           de la fenêtre restent lues entre ses bords, les autres (menus, ligne
           d'état, titre) sur toute la largeur, sans les cadres. */
        wide = 0;
        for (y = 0; y < ed_rows; ++y) {
            if (!nonblank[y] || thashes[y] == base_th[y]) continue;
            if (y < top || y > bottom) echo_plain(y);
            else if (y == ed_y) pend_row = y;
            else echo_row(y);
        }
        return;
    }
    k = shift_find();
    if (pend_row >= 0) {
        pend_row -= k;
        if (pend_row < top || pend_row > bottom) pend_row = -1;
    }
    if (pend_row >= 0) {
        changed = !seen(pend_row,k);
        if (pend_row != ed_y || !changed) {
            if (!changed && nonblank[pend_row]) echo_row(pend_row);
            pend_row = -1;
        }
    }
    for (y = top; y <= bottom; ++y) {
        if (!nonblank[y] || seen(y,k)) continue;
        if (y == ed_y) pend_row = y; else echo_row(y);
    }
}
/* Boîtes de dialogue : chaque morceau est une proposition Apollo (CR), sans
   ponctuation ajoutée qui serait nommée en mode « toutes ». */
static void dlg_part(const char *s)
{
    int n = length(dtext);
    if (!s[0]) return;
    if (n && n < (int)sizeof(dtext)-1) { dtext[n++] = '\r'; dtext[n] = 0; }
    append(dtext,s,sizeof(dtext));
}
static void dlg_state(void)
{
    if (dlg.kind == DK_CHECK) dlg_part(dlg.state ? "cochée" : "non cochée");
    else if (dlg.kind == DK_RADIO) dlg_part(dlg.state ? "sélectionné" : "non sélectionné");
    else if (dlg.kind == DK_FIELD || dlg.kind == DK_LIST) dlg_part(dlg.value[0] ? dlg.value : "vide");
}
static void dlg_braille(void)
{
    int i;
    if (dlg.kind == DK_FIELD || dlg.kind == DK_LIST) {
        line(ed_y,dlg.x0+1,dlg.x1-1); br_screen(ed_line,ed_x-dlg.x0-1,dlg.x0+1,ed_y);
        return;
    }
    for (i = 0; dtext[i]; ++i) if (dtext[i] == '\r') dtext[i] = ' ';
    br_text(dtext,-1); br_routable = 0;
}
/* Annonce du contrôle actif ; `full` : boîte nouvelle, titre et message. */
static void dlg_say(int full)
{
    static const char *const kinds[6] = {"", "champ", "case à cocher", "bouton radio", "bouton", "liste"};
    dtext[0] = 0;
    if (full) {
        dlg_part(dlg.title);
        dlg_part("boîte de dialogue");
        /* Boîte d'alerte : focus sur un bouton, le message est le texte libre. */
        if (dlg.kind == DK_BUTTON) {
            dlg_static(screen,ed_cols,ed_rows,&dlg,ed_line,ED_TEXT); dlg_part(ed_line);
        }
    }
    if (dlg.gy >= 0 && (full || dlg.gx != shown.gx || dlg.gy != shown.gy)) {
        dlg_part(dlg.group); dlg_part("groupe");
    }
    dlg_part(dlg.label); dlg_part(kinds[dlg.kind]); dlg_state();
    say(dtext); dlg_braille();
}
static int same(const char *a, const char *b)
{ while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static int same_ctl(const ED_DIALOG *a, const ED_DIALOG *b)
{ return a->id == b->id && a->kind == b->kind && a->x0 == b->x0 && a->y == b->y; }
/* Suivi des boîtes : un autre contrôle est annoncé, un état ou une valeur qui
   change (case, élément de liste) est seul relu. La frappe et les déplacements
   dans un champ restent à `track`. Rendre 0 laisse le suivi ordinaire agir.
   Word déplace brièvement le curseur pour dessiner (une case cochée) : un
   contrôle n'est annoncé que vu deux passages de suite sur un écran stable,
   au plus cinq passages d'attente, et la boîte n'est tenue pour fermée
   qu'après trois passages sans contrôle. */
static int dialog(int cls, unsigned when)
{
    int full, ctl;
    if (!dlg_found) {
        cand.kind = DK_NONE; dlg_wait = 0;
        if (!dlg_on) return 0;
        if (++dlg_lost < 3 || (nchg && dlg_lost < 6)) { echo_wait = 1; return 1; }
        /* Fermeture : l'écran restauré n'est pas un texte nouveau ; la ligne
           du curseur est lue une fois le redessin terminé. */
        dlg_on = dlg_lost = 0; echo_hold = 2; pend_row = -1;
        speak_line(ed_y); key_take(when); grace = ticks;
        return 1;
    }
    dlg_lost = 0;
    full = !dlg_on || dlg.id != shown.id;
    ctl = full || dlg.kind != shown.kind || dlg.x0 != shown.x0 || dlg.y != shown.y;
    if (ctl && dlg_wait < 5 && (nchg || !same_ctl(&dlg,&cand))) {
        cand = dlg; ++dlg_wait; echo_wait = 1; return 1;
    }
    dlg_wait = 0; cand.kind = DK_NONE;
    if (ctl) dlg_say(full);
    else if (dlg.kind == DK_FIELD && (cls == KC_TYPE || cls == KC_BS || cls == KC_DEL || cls == KC_NAVH)) {
        shown = dlg; return 0;
    } else if (dlg.state != shown.state || !same(dlg.value,shown.value) ||
               (dlg.kind == DK_LIST && cls == KC_NAVV)) {
        /* Une flèche sans effet dans une liste (premier élément choisi,
           bout de liste) redit l'élément. */
        dtext[0] = 0; dlg_state(); say(dtext); dlg_braille();
    }
    /* Sinon rien : pas d'écho écran dans une boîte, où un contrôle en remplit
       un autre (EDIT recopie le répertoire choisi dans le nom du fichier). */
    shown = dlg; dlg_on = 1;
    key_take(when); grace = ticks; pend_row = -1;
    return 1;
}
/* Suivi du curseur : la classe de la dernière touche transmise décide. */
static void track(int cls, unsigned when, int moved, int vmoved, int curchg)
{
    int used = 0;
    switch (cls) {
    case KC_NAVV:
        if (vmoved || curchg) { speak_line(ed_y); used = 1; }
        break;
    case KC_NAVH:
        if (moved || curchg) { rx = ed_x; ry = ed_y; review(nav_word ? 1 : 2,0); used = 1; }
        break;
    case KC_TYPE:
        if (moved || curchg) { typed(); used = 1; }
        break;
    case KC_BS:
        if (moved || curchg) {
            if (!vmoved && ed_x < last_x) say_char(last_line[ed_x]&255);
            else speak_line(ed_y);
            used = 1;
        }
        break;
    case KC_DEL:
        if (moved || curchg) {
            if (!moved) say_char(last_line[ed_x]&255); else speak_line(ed_y);
            used = 1;
        }
        break;
    default:
        /* Un éditeur lent finit de redessiner après la touche : ces passages
           sans touche restent attribués à la frappe ou au déplacement. */
        if (cls == KC_NONE && (unsigned)(ticks-grace) < 3) break;
        if (ed_cfg[6]) {
            echo_screen(); used = moved || nchg;
            /* Tab ou Échap dans une boîte : autre rangée, texte inchangé. */
            if (cls == KC_OTHER && vmoved && !nchg) speak_line(ed_y);
        } else if (moved) {
            if (vmoved || curchg) speak_line(ed_y);
            else { rx = ed_x; ry = ed_y; review(2,0); }
            used = 1;
        }
        break;
    }
    if (used) {
        key_take(when);
        if (cls != KC_NONE && cls != KC_ENTER && cls != KC_OTHER) { pend_row = -1; grace = ticks; }
    }
    if (cls != KC_NONE && cls != KC_ENTER && cls != KC_OTHER && (moved || curchg)) {
        line(ed_y,left,right); br_screen(ed_line,ed_x-left,left,ed_y);
    }
}
static int ran;                 /* passage où le moteur a travaillé : envoyer à la fin */
static void tick(void);
void ed_tick(void)
{
    ran = 0;
    tick();
    if (ran) send();
}
static void tick(void)
{
    int x,y,cols,rows,i,moved,vmoved,was_disabled,cls,curchg,c;
    WORD hash,thash,ahash; int start;
    WORD flags; int pending = 0; unsigned when;
    BYTE counts[8]; WORD bhash[8];
    int hy = -1, hx = 0, he = 0, score = -1, value, b, letters, fills, k;
    ++ticks;
    ed_debug[0] = ticks; ed_debug[1] = 1;
    if (ed_windows) {
        i = hw_guest(0);
        if (i == ED_SKIP) return;
        if (i == ED_SLEEP) { ed_guest = 0; ed_suspend(); return; }
        if (!ed_guest) { ed_suspend(); ed_guest = 1; }      /* entrée : état neuf */
    } else if (ed_guest) { ed_guest = 0; ed_suspend(); }
    if (ed_resume) { ed_suspend(); ed_resume = 0; }
    was_disabled = ed_disabled;
    ed_disabled = (BYTE)!hw_screen(&cols,&rows,&x,&y,screen);
    if (ed_disabled) { ed_suspend(); return; }
    if (was_disabled || asleep) { ed_init(); }
    if (cols != ed_cols || rows != ed_rows) { ready = 0; ed_cols = cols; ed_rows = rows; }
    ed_x = x; ed_y = y;
    ed_debug[2] = (WORD)x; ed_debug[3] = (WORD)y;
    if (ed_x != last_x || ed_y != last_y) focus_hi = 0;
    /* Prendre ensemble l'interruption et sa commande. Une touche reçue pendant
       l'analyse attendra le passage suivant ; sinon son Ctrl-X différé pouvait
       annuler la parole que sa propre commande venait de préparer. */
    flags = hw_lock();
    if (stop) { cancel(); stop = 0; }
    if (cin != cout) {
        pending = commands[cout]; cout = (BYTE)((cout+1)&15);
        ed_debug[8] = cout; ed_debug[9] = (WORD)pending;
    }
    hw_unlock(flags);
    serial(); ran = 1;
    ed_debug[1] = 2; ed_debug[4] = speech_pos; ed_debug[5] = speech_n;
    if ((ed_save == 2 || ed_save == 3) && !pending) {
        message(ed_save == 2 ? "Réglages enregistrés" : "Échec de sauvegarde des réglages");
        ed_save = 0;
        return;
    }
    if (menu) {
        if ((unsigned)(ticks-menu_tick) > 1092) { menu = 0; hw_restore(); echo_hold = 2; }
        else if (pending) command(pending);
        return;
    }
    bounds();
    k = left || top || right != ed_cols-1 || bottom != ed_rows-1;
    if (k && !framed && ready) wide = 1;
    framed = k;
    /* Dans une boîte reconnue, la revue et l'écho se limitent à la boîte. */
    dlg_found = dlg_find(screen,ed_cols,ed_rows,ed_x,ed_y,&dlg);
    if (dlg_found) { left = dlg.left; right = dlg.right; top = dlg.top; bottom = dlg.bottom; }
    cls = key_peek(&when);
    nchg = 0;
    for (y = 0; y < ed_rows; ++y) {
        hash = 0; thash = 0; ahash = 0; nonblank[y] = 0;
        for (x = 0; x < ed_cols; ++x) {
            c = screen[y*ed_cols+x];
            hash = (WORD)((hash<<1 | hash>>15)^c);
            /* Blancs confondus (EDIT remplit en 0 ou en espaces) ; les
               barres de défilement et cadres ne sont pas du texte : le curseur
               de la barre d'EDIT bouge à chaque défilement. Les glyphes sous
               20h, jamais prononcés, non plus : la marque de fin de document
               de Word (04h) quitte la ligne à chaque Entrée. */
            k = c&255;
            if (blank(k) || k < 32 || (k >= 176 && k <= 223)) k = 32;
            thash = (WORD)((thash<<1 | thash>>15)^k);
            /* Somme pondérée : une rotation avec XOR annulait tout changement
               uniforme sur 16 cellules (70h -> 07h d'une barre de 32 colonnes). */
            ahash = (WORD)(ahash*31 + (c>>8));
            if (k != 32) nonblank[y] = 1;
        }
        rowchg[y] = (BYTE)(ready && thash != thashes[y]);
        if (rowchg[y]) ++nchg;
        for (i = 0; i < 8; ++i) { counts[i] = 0; bhash[i] = 0; }
        for (x = 0; x < ed_cols; ++x) {
            b = (screen[y*ed_cols+x]>>12)&7; ++counts[b];
            bhash[b] = (WORD)((bhash[b]<<1 | bhash[b]>>15)^screen[y*ed_cols+x]^x);
        }
        /* Surbrillance apparue après une touche, sauf frappe ou effacement :
           la coloration d'un mot saisi (QBasic) n'est pas une sélection. Un
           texte qui change dans une zone colorée fixe (compteur de ligne d'une
           barre d'état) n'en est pas une non plus : les attributs doivent bouger. */
        if (ready && ahash != ahashes[y] && (unsigned)(ticks-user_tick) < 20 &&
            cls != KC_TYPE && cls != KC_BS && cls != KC_DEL)
            for (b = 0; b < 8; ++b) {
                if (counts[b] < 2 || counts[b] > ed_cols*3/4 || bhash[b] == bg_hashes[y][b]) continue;
                for (x = 0; x < ed_cols; ++x) {
                    if (((screen[y*ed_cols+x]>>12)&7) != b) continue;
                    start = x; letters = fills = 0;
                    while (x < ed_cols && ((screen[y*ed_cols+x]>>12)&7) == b) {
                        k = ch(x,y);
                        if (k > 32 && (k < 176 || k > 223)) ++letters;
                        if (k >= 176 && k <= 178) ++fills;
                        ++x;
                    }
                    /* Barre de titre d'une boîte de Word, remplie de ▒, qui
                       apparaît avant que le curseur n'y entre. */
                    if (letters < 2 || fills > letters) continue;
                    value = 200-counts[b]+(counts[b] > bg_counts[y][b] ? 100 : 0)+(b == 0 ? 20 : 0);
                    if (value > score) { score=value; hy=y; hx=start; he=x-1; }
                }
            }
        for (i = 0; i < 8; ++i) { bg_counts[y][i] = counts[i]; bg_hashes[y][i] = bhash[i]; }
        hashes[y] = hash; thashes[y] = thash; ahashes[y] = ahash;
    }
    if (echo_hold) --echo_hold;
    if (nchg) ++dirty_ticks;
    echo_wait = 0;
    moved = ed_x != last_x || ed_y != last_y;
    vmoved = ed_y != last_y;
    curchg = ready && rowchg[ed_y];
    if (!ready) {
        ready = 1; rx = ed_x; ry = ed_y; last_x = ed_x; last_y = ed_y;
        pend_row = -1; key_take(when);
        message("Épaudos prêt");
        if (!pending) { for (x = 0; x < ed_cols; ++x) last_line[x] = screen[ed_y*ed_cols+x]; return; }
    }
    if (pending) {
        command(pending);
    } else if (all) {
        if (!awaiting && speech_pos >= speech_n) {
            if (all_row > bottom) { all = 0; }
            else {
                line(all_row,left,right);
                say(ed_line); all = 1;
                speech[speech_n++] = '@'; speech[speech_n++] = 'E';
                echo_id = 'A'+epoch; speech[speech_n++] = (char)echo_id;
                speech[speech_n++] = '\r'; speech[speech_n] = 0; awaiting = 1;
                ry = all_row; br_screen(ed_line,-1,left,ry);
            }
        }
    } else if (dialog(cls,when)) {
    } else if (hy >= 0 && !dlg_found) {
        /* Dans une boîte, l'élément choisi d'une liste est dit par dialog(). */
        focus_hi = 1; focus_x = hx; focus_y = hy;
        while (focus_x < he && blank(ch(focus_x,hy))) ++focus_x;
        bounds(); line(hy,hx,he); say(ed_line); br_screen(ed_line,-1,hx,hy); rx = focus_x; ry = hy;
        key_take(when); pend_row = -1;
    } else {
        track(cls,when,moved,vmoved,curchg);
    }
    for (x = 0; x < ed_cols; ++x) last_line[x] = screen[ed_y*ed_cols+x];
    /* Changements expliqués ou lus : l'écran courant devient la référence. */
    if (!echo_wait) { for (y = 0; y < ed_rows; ++y) base_th[y] = thashes[y]; dirty_ticks = 0; }
    last_x = ed_x; last_y = ed_y;
    ed_debug[1] = 3; ed_debug[10] = ed_disabled; ed_debug[11] = ed_windows;
}
