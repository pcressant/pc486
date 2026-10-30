/* EPAULARD.EXE : déclarations communes aux modules. */
#ifndef EP_H
#define EP_H
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
#define TIMER_DOS  11           /* invite MS-DOS : relais rapide de la parole et du braille */
#define TIMER_NAV  12           /* mode navigation : état d'une case après activation */
#define TIMER_NET  13           /* mode navigation : délai de la demande au proxy */
#define EPM_NET    (WM_USER + 200)  /* Winsock (WSAAsyncSelect), interne à l'EXE */
#define POLL_MS    100
#define SA_MS      50
#define TXT        256
#define AUTO_ROWS 40
#define CFG_N 6
#define CFG_RATE  0             /* débit Apollo II @W, 1 à 9 */
#define CFG_PITCH 1             /* hauteur @F, 0 à 15 */
#define CFG_VOICE 2             /* voix préréglée @V, 1 à 6 */
#define CFG_PUNCT 3             /* ponctuation : aucune, certaine, toute */
#define CFG_ECHO  4             /* écho du clavier : caractères, mots, les deux, aucun */
#define CFG_CAPS  5             /* dire « majuscule » devant une lettre seule */
#define CALC_STD 1
#define CALC_SCI 2
#define CALC_BOTH 3
#define CALC_DEC 4
#define CALC_HEX 8
#define CALC_PARTS 6
#define CALC_BUTTONS calc_nbuttons
#define CHK_ROW(c)  ((c) / 2)
#define CHK_ON(c)   ((c) % 2)
#define ME_LEFTDOWN  0x02
#define ME_LEFTUP    0x04
#define ME_RIGHTDOWN 0x08
#define ME_RIGHTUP   0x10
#define SA_AHEAD 3              /* lignes envoyées dont la lecture n'est pas finie */

typedef struct { UINT id; const char *name; int modes; } CALCBUTTON;
typedef struct {
    int kind;                   /* 0 : contrôle, 1 : EDIT, 2 : texte dessiné */
    HWND scope;
    POINT pt;
    int row, col;
    int sel_from, sel_to;       /* braille : colonnes sélectionnées (-1 : aucune) */
    char text[TXT * 2];
} REVIEW;
typedef struct {
    char mark;                  /* écho attendu à la fin de sa lecture */
    int len;
    int moves;                  /* texte dessiné : descentes depuis la ligne précédente */
    int line;                   /* EDIT : numéro de la ligne */
} SALINE;
typedef struct {
    BOOL on;
    int kind;                   /* 1 : EDIT, 2 : texte dessiné, 3 : Aide (par page),
                                   4 : page Web en mode navigation (navweb.c) */
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
} SA_STATE;

extern int com;
extern HWND hwnd_main;
extern char braille_menu[TXT * 2];
extern HWND last_active, last_focus;
extern int last_sel, last_line, last_check;
extern DWORD last_esel;
extern BOOL typed;
extern BOOL in_menu;
extern BOOL new_active;
extern DWORD focus_tick;
extern char focus_said[TXT * 2];
extern POINT last_caret;
extern BOOL line_key;
extern UINT last_vk;
extern DWORD line_key_tick;
extern BOOL word_key;
extern DWORD caret_settle;
extern BOOL caret_quiet;
extern char hilite_said[TXT];
extern DWORD hilite_tick;
extern DWORD popup_tick;
extern char menu_pending[TXT * 2];
extern BOOL flat_review;
extern HWND review_window;
extern POINT review_point;
extern int review_column;
extern BOOL sel_key;
extern DWORD sel_key_tick;
extern char sel_said[TXT * 2];
extern HWND sdm_hwnd;
extern char sdm_text[TXT * 2];
extern char custom_said[TXT];
extern char last_etext[TXT];
extern HWND dlg_hwnd;
extern HWND dlg_src;
extern BOOL dlg_pending;
extern HWND grp_dlg;
extern char grp_said[TXT];
extern HWND status_hwnd;
extern char status_said[TXT];
extern HWND last_mdi;
extern HWND auto_hwnd;
extern DWORD auto_sig, auto_tick;
extern DWORD auto_rows[AUTO_ROWS];
extern int auto_n;
extern DWORD key_tick;
extern HWND hilite_hwnd;
extern int cfg[CFG_N];
extern const char *const cfg_key[CFG_N];
extern const char *const cfg_label[CFG_N];
extern const int cfg_min[CFG_N];
extern const int cfg_max[CFG_N];
extern char ini_path[144];
extern int typed_n;
extern BOOL cfg_on;
extern DWORD cfg_tick;
extern int cfg_item;
extern int cfg_before[CFG_N];
extern char typed_word[40];
extern HWND calc_owner;
extern char calc_last[TXT];
extern BOOL calc_force;
extern char calc_previous[CALC_PARTS][64];
extern int calc_view, calc_button;
extern BOOL calc_queue;
extern const CALCBUTTON calc_buttons[];
extern const int calc_nbuttons;
extern BOOL item_unknown;
extern HWND frect_hwnd;
extern RECT frect_said;
extern HWND said_hwnd;
extern BOOL tab_waiting;
extern DWORD frect_tick;
extern int frect_check;
extern char sdm_group[TXT];
extern HWND sdm_group_hwnd;
extern char tab_before[TXT];
extern DWORD tab_tick;
extern FARPROC mouse_fn;
extern int mouse_locked;
extern POINT mouse_back;
extern REVIEW braille_live, braille_view;
extern BOOL braille_valid;
extern int braille_epoch;
extern int route_key;
extern DWORD route_tick;
extern SA_STATE sa;

void remember_value(HWND h);
void dialog_text(HWND dlg, char *out, int max);
HWND top_level(HWND h);
BOOL has_page(HWND h);
BOOL has_child_class(HWND h, const char *cls);
HWND mdi_child(HWND h);
void announce_focus(BOOL cut);
void check_value(void);
void status_poll(void);
void progress_poll(void);
HWND auto_scope(HWND act);
DWORD row_hash(const char *s);
void auto_poll(void);
void poll(void);
void on_menu(WPARAM wp, LPARAM lp);
void on_menu_title(void);
void on_hilite(void);
void read_window(void);
void say_status(void);
void on_command(WPARAM vk, LPARAM lp);
void cfg_load(HINSTANCE hinst);
void cfg_save(void);
void cfg_value(int i, char *out);
void cfg_say(BOOL with_name, BOOL cut);
void cfg_close(BOOL keep);
void cfg_open(void);
void cfg_key_in(WPARAM vk);
void com_open(void);
void com_write(const char FAR *s, int n);
void silence(void);
void voice_apply(void);
const char *sign_name(char c);
BOOL sign_some(char c);
void ansi_plain(const char FAR *s, char *out, int max);
void punct_words(const char *in, char *out, int max);
void say(const char FAR *s, BOOL cut);
void say_mark(const char FAR *s, char mark);
void say_char(char c, BOOL cut);
void com_read(void);
void echo_char(char ch);
void say_word(char *text, int col);
void clean_label(char *s, char **accel);
void class_of(HWND h, char *cls, int n);
BOOL has_style(HWND h, long st);
BOOL is_class(HWND h, const char *name);
BOOL is_standard(HWND h);
void caret_screen(HWND h, POINT *pt);
BOOL no_word(const char *s);
BOOL ends_colon(const char *s);
BOOL label_left(HWND parent, const RECT *rc, char *out, int n);
BOOL label_nearest_left(HWND parent, const RECT *rc, char *out, int n);
void label_drawn(HWND parent, const RECT *rc, char *out, int n, BOOL any);
BOOL is_sdm_page(HWND h);
BOOL is_sdm(HWND h);
BOOL label_zorder(HWND h, char *out, int n);
void label_before(HWND h, char *out, int n);
BOOL label_right(HWND h, char *out, int n);
BOOL picture_button(HWND h);
void picture_label(HWND h, char *name, int n);
void append_n(char *dst, const char *s, int max);
void append(char *dst, const char *s);
void fit_before(char *s, int max, const char *a, const char *b, const char *c);
void append_keep(char *dst, const char *s, int max);
int edit_line(HWND h, char *out, int n);
BOOL edit_text(HWND h, char *out, int n);
BOOL is_multiline(HWND h);
void edit_range(HWND h, int a, int b, char *out, int max);
void say_selection(const char *text, int count, BOOL selected);
BOOL edit_selection(HWND h, DWORD old, DWORD cur, BOOL shift);
void drawn_selection(const char *old, const char *cur);
HWND calc_child(int id);
BOOL calc_scientific(void);
BOOL calc_checked(int id);
int calc_base(void);
BOOL calc_available(int i);
const char *calc_name(int i);
void calc_control_name(HWND h, char *out);
void calc_states(char parts[CALC_PARTS][64]);
void calc_summary(char *out);
BOOL calc_value(char *out);
void calc_say(const char *value, BOOL cut);
void calc_poll(void);
BOOL calc_command(WPARAM vk);
BOOL osm_item(HWND h, int i, char *out);
void describe_value(HWND h, char *out);
void media_button_name(char *name);
BOOL child_text(HWND parent, int id, char *out, int n);
BOOL status_line(HWND top, char *out, int n);
void char_name(char c, char *out);
BOOL charmap_value(HWND h, char *out);
HWND card_edit(HWND app);
BOOL card_title(HWND app, char *out, int n);
void clock_value(char *out);
BOOL is_help(HWND h);
HWND help_part(HWND doc, const char *cls);
BOOL help_row(HWND doc, BOOL last, char *out, int n, int *y);
BOOL page_view(HWND h);
BOOL doc_area(HWND doc, BOOL first, RECT *r);
BOOL packager_value(HWND app, char *out);
BOOL custom_value(HWND h, char *out, const char **role);
void edit_unit(HWND h, char *val);
BOOL datetime_name(HWND h, char *out);
BOOL group_of(HWND h, char *out, int n);
void describe(HWND h, char *out);
int sdm_check(HWND h, const RECT *r);
void group_title(HWND h, const RECT *r, char *out, int n);
const char *check_state(int c);
BOOL sdm_combo(HWND h, const RECT *r);
BOOL list_row(HWND h, const RECT *r);
void on_focusrect(void);
int tab_page(HWND act, char *name, int n);
void on_tabpage(void);
BOOL labels_only(const char *s);
void on_dialog_text(void);
void on_glyph(void);
void mouse_raw(WORD flags);
void mouse_button(BOOL right, BOOL down);
void mouse_click(BOOL right);
void click_at(POINT pt, BOOL right, BOOL restore);
void on_mouse(int action);
void review_anchor(HWND h, POINT *pt);
BOOL review_edit_line(REVIEW *r, int row);
BOOL review_load(REVIEW *r);
BOOL review_line(REVIEW *r, int direction);
BOOL review_screen_point(POINT *pt, HWND *scope);
void mouse_to_review(void);
void review_to_mouse(void);
void say_format(void);
void say_selected(void);
BOOL braille_same(REVIEW *a, REVIEW *b);
void braille_selection(REVIEW *r);
void say_column(const char *text, int col);
void braille_route(int key);
void braille_poll(void);
BOOL review_space(char c);
void review_store(REVIEW *r);
void on_review(int key);
BOOL sa_edit_text(int line, char *out, int max);
int sa_edit_caret_line(int *col);
BOOL sa_blank(const char *s);
void sa_caret_row(char *out, BOOL from_caret);
void sa_edit_caret(int line);
void sa_push(const char *text, int moves, int line);
void sa_key(UINT vk, int scan);
void sa_stop(BOOL cut);
void sa_interrupt(BOOL quiet);
void sa_move(void);
BOOL sa_help_next(char *text);
void sa_help_resume(void);
void sa_fill(void);
void sa_end(void);
void sa_arrived(void);
void sa_pop(void);
void sa_mark(char c);
void sa_tick(void);
BOOL sa_active(void);
void sa_start(void);
BOOL same_row(const char *a, const char *b);

/* dosbox.c : boîte aux lettres d'Épaudos, même disposition que EDBOX dans
   epaudos/src/core.h (offsets dans le segment d'Épaudos) */
typedef struct {
    char sig[4];
    BYTE version, relay, active, reserved;
    WORD vm, alive;
    WORD head, tail, buf;
    WORD rxhead, rxtail, rxbuf;
    WORD keys;                  /* version 2 : touches de la machine DOS */
} DOSBOX;
extern BOOL dos_relay;
BOOL is_dos_window(HWND h);
void dos_rx(char c);
void dos_stop(void);
BOOL dos_poll(void);
void dos_fast(void);
BOOL dos_spin(void);

/* navweb.c : mode navigation des pages Web */
void nav_init(void);
void nav_begin(void);
void nav_feed(const char *p, int n);
void nav_done(BOOL ok);
void nav_poll(void);
BOOL nav_here(void);
BOOL nav_autoread(HWND page);
void nav_key_seen(WPARAM vk, LPARAM lp);
void nav_key(WPARAM vk, LPARAM lp);
BOOL nav_owns(HWND h);
void nav_timer(void);
BOOL nav_command(WPARAM vk, LPARAM lp);
BOOL nav_focus(HWND foc, char *d);
BOOL nav_sa_start(int *pos);
BOOL nav_sa_text(int *pos, char *out, int max);
void nav_sa_at(int pos);
BOOL nav_br_show(void);
BOOL nav_br_key(int key);
HWND page_of(HWND act);
/* navnet.c : DDE avec Netscape, Winsock vers le proxy */
extern BOOL net_in_dde;
BOOL net_ready(void);
BOOL net_page_info(char *url, int nurl, char *title, int ntitle);
BOOL net_open_url(const char *url);
BOOL net_busy(void);
BOOL net_fetch(const char *url);
void net_event(WPARAM wp, LPARAM lp);
void net_timeout(void);
/* tableur.c */
void sheet_load(const char *ini);
HWND find_class(HWND h, const char *cls);
BOOL is_sheet(HWND h);
BOOL sheet_parse(const char *ref, int *col, int *row);
BOOL sheet_cell(HWND grid, int col, int row, RECT *cell);
void sheet_reset(void);
BOOL sheet_poll(void);
void grid_load(const char *ini);
BOOL is_grid_edit(HWND h);
void grid_reset(void);
BOOL grid_poll(void);
BOOL grid_command(WPARAM vk);
extern BOOL grid_typed;
BOOL sheet_command(WPARAM vk);
BOOL sheet_text(HWND h, char *out);

#endif
