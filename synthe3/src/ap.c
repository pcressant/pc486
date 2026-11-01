/* SPDX-License-Identifier: GPL-3.0-or-later
 * Apollo II local devant Synthé3 : voir ap.h. Comportements repris d'Apolo-emu et
 * de la documentation Apollo (docs/protocole-apollo.md), limités à ce qu'émettent
 * Épaudos et Épaulard :
 *   texte CP850, propositions terminées par CR ou . , : ; ! ? (au plus 240 octets,
 *   coupées de préférence à une espace) ; Ctrl-X : silence immédiat, file, marques
 *   et proposition en cours abandonnées, réglages conservés (même ceux encore dans
 *   la file : appliqués au passage) ; @W1..9 débit ;
 *   @F0..F hauteur ; @V1..6 voix (une seule voix locale : accepté sans effet) ;
 *   @P0/@P1 ponctuation naturelle ou nommée ; @E<c> : <c> renvoyé quand la
 *   restitution atteint cet endroit. Autre commande : @, lettre et un argument ignorés.
 */
#include "ap.h"
#include "s3.h"
#include <string.h>

AP_LINK ap_link;
AP_STATS ap_stats;
char ap_trace[AP_TRACE];
unsigned ap_trace_pos;

static const unsigned char cp850[128] = {
#include "cp850.inc"
};

#define TEXT_MAX 240
#define SPLIT_FROM 200
static char text[TEXT_MAX + 1], spill[TEXT_MAX + 1];
static unsigned text_n, spill_n;
static int state, command;      /* 0 texte, 1 lettre de commande, 2 argument */
static int active, ready;       /* proposition dans le moteur ; proposition complète */
static unsigned cuts_seen;
static unsigned long produced;
static struct { unsigned long at; unsigned char c; } marks[AP_MARKS];
static unsigned mark_head, mark_tail;
/* Volume @A de l'Apollo II (0 1 2 3 5 7 A F, défaut 3), en 1/256 : 3 dB par niveau
   autour du niveau d'origine du moteur (@A3). La voix plafonne vers la moitié de la
   pleine échelle : au-delà de @A7, les crêtes sont écrêtées. */
static unsigned gain = 256;
static const unsigned gains[16] = {
    64, 128, 181, 256, 304, 362, 430, 512, 560, 610, 645, 690, 730, 770, 790, 812
};

/* Noms dits avec @P1, en CP850 (même table de conversion que le texte). */
static const char *sign(unsigned char c)
{
    switch (c) {
    case '.': return "point"; case ',': return "virgule";
    case ';': return "point virgule"; case ':': return "deux points";
    case '!': return "point d'exclamation"; case '?': return "point d'interrogation";
    case '\'': return "apostrophe"; case '"': return "guillemet";
    case '-': return "tiret"; case '(': return "parenth\x8ase ouvrante";
    case ')': return "parenth\x8ase fermante"; case '[': return "crochet ouvrant";
    case ']': return "crochet fermant"; case '{': return "accolade ouvrante";
    case '}': return "accolade fermante"; case '#': return "di\x8ase";
    case '$': return "dollar"; case '%': return "pour cent";
    case '&': return "et commercial"; case '*': return "\x82toile";
    case '+': return "plus"; case '/': return "barre oblique";
    case '\\': return "barre oblique inverse"; case '=': return "\x82gal";
    case '<': return "inf\x82rieur"; case '>': return "sup\x82rieur";
    case '|': return "barre verticale"; case '~': return "tilde";
    case '_': return "soulign\x82"; case '^': return "circonflexe";
    case '`': return "accent grave";
    }
    return 0;
}
static int ending(unsigned char c)
{
    return c == '.' || c == ',' || c == ':' || c == ';' || c == '!' || c == '?';
}
static unsigned char latin1(unsigned char c)
{
    if (c < 32 || c == 127) return ' ';
    if (c >= 128) c = cp850[c - 128];
    /* EDh et F8h sont les marques internes du moteur. */
    if (c == 0xed) return 'i';
    if (c == 0xf8) return 'o';
    return c;
}

int ap_put(unsigned char b)
{
    unsigned head = ap_link.head;
    if (b == 0x18) {                /* rien n'est stocké : la position suffit */
        ap_link.cut_at = head;
        ap_link.cuts++;             /* publié en dernier */
        return 1;
    }
    if (((head + 1) & (AP_INPUT - 1)) == ap_link.tail) { ap_link.dropped++; return 0; }
    ap_link.buf[head] = b;
    ap_link.head = (head + 1) & (AP_INPUT - 1);
    return 1;
}
int ap_echo(void)
{
    unsigned tail = ap_link.echo_tail;
    int c;
    if (tail == ap_link.echo_head) return -1;
    c = ap_link.echo[tail];
    ap_link.echo_tail = (tail + 1) & (AP_ECHOES - 1);
    return c;
}
static void echo(unsigned char c)
{
    unsigned head = ap_link.echo_head;
    if (((head + 1) & (AP_ECHOES - 1)) == ap_link.echo_tail) return;  /* lecteur absent */
    ap_link.echo[head] = c;
    ap_link.echo_head = (head + 1) & (AP_ECHOES - 1);
    ap_stats.marks++;
}

void ap_init(void)
{
    ap_link.head = ap_link.tail = ap_link.echo_head = ap_link.echo_tail = 0;
    ap_link.cuts = ap_link.cut_at = ap_link.dropped = 0;
    cuts_seen = 0;
    text_n = spill_n = 0; state = 0; active = ready = 0;
    produced = 0; mark_head = mark_tail = 0;
    ap_trace_pos = 0; ap_trace[0] = 0;
    memset(&ap_stats, 0, sizeof(ap_stats));
    ap_stats.rate = 3; ap_stats.pitch = 8; ap_stats.voice = 1; ap_stats.punct = 0;
    gain = 256;
    s3_cancel();
}

static void reset_speech(void)
{
    s3_cancel();
    active = ready = 0; text_n = spill_n = 0; state = 0;
    produced = 0; mark_head = mark_tail = 0;
}
static void apply(int letter, unsigned char arg);
/* Saute la file jusqu'au Ctrl-X : texte et marques sont abandonnés, mais les
   réglages (@W @F @V @P @A) restent appliqués, comme sur l'Apollo II qui les
   exécute à réception. Épaulard envoie ses réglages puis coupe aussitôt. */
static void skip_to(unsigned at)
{
    unsigned char b;
    while (ap_link.tail != at) {
        b = ap_link.buf[ap_link.tail];
        ap_link.tail = (ap_link.tail + 1) & (AP_INPUT - 1);
        if (state == 1) { command = b >= 'a' && b <= 'z' ? b - 32 : b; state = 2; }
        else if (state == 2) { if (command != 'E') apply(command, b); state = 0; }
        else if (b == '@') state = 1;
    }
}
int ap_cut(void)
{
    unsigned cuts, at;
    /* Paire cohérente : l'écrivain peut interrompre la pompe entre deux lectures. */
    do { cuts = ap_link.cuts; at = ap_link.cut_at; } while (cuts != ap_link.cuts);
    if (cuts == cuts_seen) return 0;
    cuts_seen = cuts;
    skip_to(at);                    /* le texte avant le dernier Ctrl-X est abandonné */
    reset_speech();
    ap_stats.cuts++;
    return 1;
}

static void mark(unsigned char c)
{
    unsigned next = (mark_tail + 1) & (AP_MARKS - 1);
    if (next == mark_head) {    /* file pleine : libérer la plus ancienne d'avance */
        echo(marks[mark_head].c);
        mark_head = (mark_head + 1) & (AP_MARKS - 1);
        ap_stats.lost_marks++;
    }
    marks[mark_tail].at = produced;
    marks[mark_tail].c = c;
    mark_tail = next;
}
static void apply(int letter, unsigned char arg)
{
    int v = arg >= '0' && arg <= '9' ? arg - '0' :
            arg >= 'A' && arg <= 'F' ? arg - 'A' + 10 :
            arg >= 'a' && arg <= 'f' ? arg - 'a' + 10 : -1;
    switch (letter) {
    case 'E': mark(arg); break;
    case 'W': if (v >= 1 && v <= 9) ap_stats.rate = v; break;
    case 'F': if (v >= 0) ap_stats.pitch = v; break;
    case 'V': if (v >= 0 && v <= 9) ap_stats.voice = v; break;
    case 'P': if (v == 0 || v == 1) ap_stats.punct = v; break;
    case 'A': if (v >= 0) gain = gains[v]; break;
    }
}
/* Proposition pleine sans fin : couper à la dernière espace après 200 octets. */
static void overflow(void)
{
    unsigned cut = TEXT_MAX, i;
    for (i = TEXT_MAX; i > SPLIT_FROM; i--) if (text[i - 1] == ' ') { cut = i; break; }
    spill_n = 0;
    for (i = cut; i < text_n; i++) spill[spill_n++] = text[i];
    text_n = cut;
    ready = 1;
}
static void add(unsigned char c)
{
    if (text_n == TEXT_MAX) overflow();
    if (!ready) text[text_n++] = (char)latin1(c);
    else if (spill_n < TEXT_MAX) spill[spill_n++] = (char)latin1(c);
}
static void add_word(const char *w)
{
    if (text_n && text[text_n - 1] != ' ') add(' ');
    while (*w) add((unsigned char)*w++);
    add(' ');
}
/* Lit la file jusqu'à une proposition complète ; s'arrête dès qu'un Ctrl-X est
   publié (ap_cut le traitera avant toute autre lecture). */
static void parse(void)
{
    unsigned char b;
    const char *w;
    while (!ready && ap_link.tail != ap_link.head) {
        if (ap_link.cuts != cuts_seen) return;
        b = ap_link.buf[ap_link.tail];
        ap_link.tail = (ap_link.tail + 1) & (AP_INPUT - 1);
        if (state == 1) { command = b >= 'a' && b <= 'z' ? b - 32 : b; state = 2; continue; }
        if (state == 2) { apply(command, b); state = 0; continue; }
        if (b == '@') { state = 1; continue; }
        if (b == '\r') { if (text_n) ready = 1; continue; }
        if (ap_stats.punct == 1 && (w = sign(b)) != 0) {
            add_word(w);
            if (ending(b)) ready = 1;
            continue;
        }
        add(b);
        if (ending(b)) ready = 1;
    }
}
/* Trace chronologique : pleine, elle garde sa seconde moitié. */
static void trace(void)
{
    unsigned i, keep;
    if (ap_trace_pos + text_n + 2 > AP_TRACE) {
        keep = AP_TRACE / 2 < ap_trace_pos ? AP_TRACE / 2 : ap_trace_pos;
        memmove(ap_trace, ap_trace + ap_trace_pos - keep, keep);
        ap_trace_pos = keep;
    }
    for (i = 0; i < text_n && ap_trace_pos < AP_TRACE - 2; i++) ap_trace[ap_trace_pos++] = text[i];
    ap_trace[ap_trace_pos++] = '|';
    ap_trace[ap_trace_pos] = 0;
}
/* Débit Apollo W1..9 sur toute l'échelle de Synthé3 (0..15, facteur 2^(1/6) par
   pas, 3 = débit naturel) : W1 naturel, W5 deux fois plus vite, W9 quatre fois
   (l'ancien W9, débit 9, est devenu W5). Épaudos et Épaulard passent tous deux par
   cette table : un même réglage donne la même vitesse.
   Hauteur Apollo F0..F, défaut F8 : Synthé3 0..15, défaut 5 ; F8 -> 5. */
static const unsigned char rates[10] = { 3, 3, 5, 6, 8, 9, 11, 12, 14, 15 };
static int start(void)
{
    unsigned i;
    int pitch = ap_stats.pitch - 3;
    if (pitch < 0) pitch = 0;
    text[text_n] = 0;
    for (i = 0; i < text_n && text[i] == ' '; i++) {}
    if (i < text_n) {
        trace();
        if (s3_start(text, rates[ap_stats.rate], pitch) == S3_OK) {
            active = 1;
            ap_stats.propositions++;
        } else ap_stats.refused++;
    }
    for (i = 0; i < spill_n; i++) text[i] = spill[i];
    text_n = spill_n; spill_n = 0; ready = 0;
    return active;
}
/* Volume : multiplication et écrêtage, avant la conversion 8 bits de la sortie. */
static void scale(short *pcm, unsigned n)
{
    long v;
    unsigned i;
    for (i = 0; i < n; i++) {
        v = (long)pcm[i] * (long)gain >> 8;
        pcm[i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}
unsigned ap_render(short *pcm, unsigned capacity)
{
    unsigned n = 0, got;
    while (n < capacity) {
        if (active) {
            got = s3_read(pcm + n, capacity - n);
            if (gain != 256) scale(pcm + n, got);
            n += got; produced += got;
            ap_stats.samples += got;
            if (!got) active = 0;
            continue;
        }
        parse();
        if (!ready) break;
        start();
    }
    return n;
}
int ap_speaking(void) { return active || ready; }
void ap_played(unsigned long played)
{
    while (mark_head != mark_tail && marks[mark_head].at <= played) {
        echo(marks[mark_head].c);
        mark_head = (mark_head + 1) & (AP_MARKS - 1);
    }
}
unsigned ap_pending_marks(void) { return (mark_tail - mark_head) & (AP_MARKS - 1); }
