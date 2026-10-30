/* EPAULARD.EXE — invite MS-DOS : relais de la parole d'Épaudos.
 *
 * Les machines DOS de Windows en mode amélioré (fenêtre ou plein écran) ne passent
 * ni par USER ni par GDI : leur écran texte est dans la machine virtuelle. Épaudos,
 * chargé avant Windows, y tourne en « mode invité » : il lit l'écran et le clavier de
 * la machine DOS au premier plan, mais ne touche plus aux ports série, qui sont à
 * Windows. Sa parole passe par une boîte aux lettres en mémoire DOS commune à toutes
 * les machines (epaudos/src/dos.c, INT 2Fh AX=ED02h) : Épaulard, dans la machine
 * système, envoie sa file à l'Apollo II et lui rend les échos (@E) de la synthèse.
 *
 * Pendant le relais, toutes les touches vont à la machine DOS (EpPassKeys) : Inser et
 * le pavé numérique sont les commandes d'Épaudos. Sans Épaudos, la fenêtre DOS reste
 * lue comme une autre (titre).
 *
 * Braille (boîte version 2) : Épaudos écrit ses trames Baum dans la file de son port 1.
 * Épaulard ne les recopie pas octet par octet (une trame coupée à l'entrée ou à la
 * sortie de la fenêtre mélangerait les deux émetteurs sur COM3) : il en tire les
 * cellules, que son pilote affiche (br_cells), répond lui-même à la demande de
 * largeur, et rend à Épaudos les touches et le routage de la plage en trames Baum
 * (br_relay_read). Épaudos garde ainsi sa revue, son défilement et son routage.
 */
#include "ep.h"

static DOSBOX FAR *box;         /* boîte d'Épaudos (NULL : absent) */
static BYTE FAR *ed_seg;        /* segment d'Épaudos, pour les files */
static BOOL box_tried;
BOOL dos_relay;                 /* fenêtre DOS au premier plan, parole d'Épaudos */
static WORD dos_alive;
#define DOS_BUSY_MS 4000
static WORD dos_keys;           /* compteur de touches d'Épaudos déjà vu */
static DWORD dos_busy;          /* dernière frappe ou sortie relayée */

#ifdef EP_NATIVE_TEST
/* Banc : la boîte et son segment sont fournis par le test */
BYTE FAR *sim_epaudos_seg;
WORD sim_epaudos_box;
static DOSBOX FAR *box_find(void)
{
    if (!sim_epaudos_seg)
        return NULL;
    ed_seg = sim_epaudos_seg;
    return (DOSBOX FAR *) (ed_seg + sim_epaudos_box);
}
#else
/* Structure d'appel en mode réel de DPMI (INT 31h, AX=0300h) */
typedef struct {
    DWORD edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    WORD flags, es, ds, fs, gs, ip, cs, sp, ss;
} RMCALL;

static DOSBOX FAR *box_find(void)
{
    static RMCALL rm;
    RMCALL FAR *p = &rm;
    WORD ps = FP_SEG(p), po = FP_OFF(p), rseg;
    volatile WORD err = 1, sel = 0;     /* écrits par l'assembleur */

    memset(&rm, 0, sizeof(rm));
    rm.eax = 0xED02;
    _asm {
        push es
        push di
        mov es, ps
        mov di, po
        mov ax, 0300h
        mov bx, 002Fh
        xor cx, cx
        int 31h
        sbb ax, ax
        mov err, ax
        pop di
        pop es
    }
    /* (0300h : INT 2Fh simulée en mode réel ; retenue : échec) */
    if (err || LOWORD(rm.eax) != 0xEDFF || !rm.es)
        return NULL;
    rseg = rm.es;
    _asm {
        mov ax, 0002h
        mov bx, rseg
        int 31h
        sbb bx, bx
        not bx
        and ax, bx
        mov sel, ax
    }
    /* (0002h : segment -> sélecteur, jamais libéré : un seul par session) */
    if (!sel)
        return NULL;
    ed_seg = (BYTE FAR *) MAKELP(sel, 0);
    return (DOSBOX FAR *) (ed_seg + LOWORD(rm.ebx));
}
#endif

/* Fenêtre d'une machine DOS (WINOLDAP) */
BOOL is_dos_window(HWND h)
{
    char c[8];
    if (!h)
        return FALSE;
    class_of(h, c, sizeof(c));
    return lstrcmpi(c, "tty") == 0;     /* « TTY » sous WfW 3.11 */
}

static void box_open(void)
{
    DOSBOX FAR *b;
    if (box_tried)
        return;
    box_tried = TRUE;
    b = box_find();
    if (b && b->sig[0] == 'E' && b->sig[1] == 'P' && b->sig[2] == 'B' && b->sig[3] == 'X' &&
        b->version >= 1)
        box = b;
}

/* --------------------------------------------------------------- braille */

/* Trame en cours de la file Baum d'Épaudos : ESC, commande, données (ESC doublé) */
static int bf_cmd = -1, bf_need, bf_n;
static BOOL bf_esc;
static BYTE bf_data[80];
static BOOL dos_braille;        /* relais du braille actif */

/* Trame pour Épaudos (file de réception de son port 1) */
static void brx_put(BYTE b)
{
    WORD hd = box->rxhead + 1;
    BYTE head = ed_seg[hd];
    if ((BYTE) (head + 1) == ed_seg[box->rxtail + 1])
        return;                         /* file pleine : Épaudos ne lit plus */
    ed_seg[box->rxbuf + 256 + head] = b;
    ed_seg[hd] = (BYTE) (head + 1);
}

static void brx_width(void)
{
    int w = br_width();
    if (!w)
        return;
    brx_put(0x1b);
    brx_put(1);
    brx_put((BYTE) w);
    if (w == 0x1b) brx_put(0x1b);
}

static void bf_done(void)
{
    if (bf_cmd == 1)
        br_cells(bf_data, bf_n);
    else if (bf_cmd == 0x15)
        brx_width();                    /* réinitialisation : la plage répond sa largeur */
    bf_cmd = -1;
}

static void bf_start(BYTE cmd)
{
    bf_cmd = cmd;
    bf_n = 0;
    bf_need = cmd == 1 ? br_width() : cmd == 0x15 ? 1 : 0;
    if (bf_need > (int) sizeof(bf_data))
        bf_need = sizeof(bf_data);
    if (bf_need == 0)
        bf_cmd = -1;                    /* commande inconnue : ignorée */
}

static void bf_byte(BYTE b)
{
    if (bf_esc) {                       /* octet après un ESC */
        bf_esc = FALSE;
        if (b != 0x1b || bf_cmd < 0) {  /* nouvelle trame (la précédente, coupée) */
            bf_start(b);
            return;
        }
    } else if (b == 0x1b) {
        bf_esc = TRUE;
        return;
    }
    if (bf_cmd < 0)
        return;                         /* hors trame : recalage au prochain ESC */
    bf_data[bf_n++] = b;
    if (bf_n >= bf_need)
        bf_done();
}

static void braille_relay(void)
{
    static BYTE buf[32];
    WORD hd = box->head + 1, tl = box->tail + 1;
    BYTE head = ed_seg[hd], tail = ed_seg[tl];
    int i, n;

    while (tail != head) {
        bf_byte(ed_seg[box->buf + 256 + tail]);
        tail++;
    }
    ed_seg[tl] = tail;
    br_tick();
    n = br_relay_read(buf, sizeof(buf));
    for (i = 0; i < n; i++)
        brx_put(buf[i]);
}

/* Parole d'Épaudos vers COM1 */
static void relay_out(void)
{
    char buf[64];
    BYTE head = ed_seg[box->head], tail = ed_seg[box->tail];
    int n = 0;
    while (tail != head) {
        buf[n++] = (char) ed_seg[box->buf + tail];
        tail++;
        if (n == sizeof(buf)) {
            com_write(buf, n);
            n = 0;
        }
    }
    if (n)
        com_write(buf, n);
    ed_seg[box->tail] = tail;
}

/* Octet reçu de l'Apollo II pendant le relais : écho attendu par Épaudos */
void dos_rx(char c)
{
    BYTE head = ed_seg[box->rxhead];
    if ((BYTE) (head + 1) == ed_seg[box->rxtail])
        return;                         /* file pleine : Épaudos ne lit plus */
    ed_seg[box->rxbuf + head] = (BYTE) c;
    ed_seg[box->rxhead] = (BYTE) (head + 1);
}

static void relay_start(void)
{
    ed_seg[box->tail] = ed_seg[box->head];      /* ancienne parole : oubliée */
    ed_seg[box->rxtail] = ed_seg[box->rxhead];
    box->vm = 0;                                /* machine de la fenêtre active */
    dos_braille = box->version >= 2 && br_enabled();
    if (dos_braille) {
        ed_seg[box->tail + 1] = ed_seg[box->head + 1];
        ed_seg[box->rxtail + 1] = ed_seg[box->rxhead + 1];
        bf_cmd = -1;
        bf_esc = FALSE;
        br_cells((BYTE FAR *) bf_data, 0);      /* plage vide jusqu'à Épaudos */
        br_relay(TRUE);
    }
    dos_keys = box->version >= 2 ? box->keys : 0;
    dos_busy = GetTickCount();
    box->alive++;
    box->relay = 1;
    dos_relay = TRUE;
    EpPassKeys(TRUE);
    SetTimer(hwnd_main, TIMER_DOS, 55, NULL);
}

/* Minuteur rapide du relais (55 ms, la résolution de Windows) : parole et braille
   suivent Épaudos sans attendre la scrutation générale */
void dos_fast(void)
{
    if (!dos_relay || !box)
        return;
    if ((box->version >= 2 && box->keys != dos_keys) ||
        ed_seg[box->head] != ed_seg[box->tail] ||
        (dos_braille && ed_seg[box->head + 1] != ed_seg[box->tail + 1])) {
        dos_keys = box->version >= 2 ? box->keys : 0;
        dos_busy = GetTickCount();      /* frappe ou sortie : rester éveillé */
    }
    relay_out();
    if (dos_braille)
        braille_relay();
}

/* Machine DOS en plein écran au premier plan : la machine système, où tourne
   Épaulard, passe pour inactive tant qu'il attend un message, et le gestionnaire
   des machines virtuelles ne lui rend la main que toutes les 0,5 à 1 s ; ses
   minuteurs aussi. Pendant DOS_BUSY_MS après une frappe ou une sortie d'Épaudos, la
   boucle de messages ne s'endort pas (PeekMessage, qui cède aux autres tâches, puis
   ce relais) : elle reste ordonnancée et la parole et le braille suivent la frappe. Ensuite, GetMessage :
   aucun cœur de l'hôte occupé par une invite qui attend. */
BOOL dos_spin(void)
{
    if (!dos_relay || GetTickCount() - dos_busy > DOS_BUSY_MS)
        return FALSE;
    dos_fast();
    return TRUE;
}

void dos_stop(void)
{
    if (!dos_relay)
        return;
    dos_relay = FALSE;
    KillTimer(hwnd_main, TIMER_DOS);
    if (dos_braille) {
        dos_braille = FALSE;
        br_relay(FALSE);                /* l'affichage d'Épaulard revient */
    }
    EpPassKeys(FALSE);
    if (box) {
        box->relay = 0;
        ed_seg[box->tail] = ed_seg[box->head];
    }
    silence();
    voice_apply();                      /* Épaudos a pu changer débit et voix */
}

/* Scrutation (POLL_MS) : TRUE pendant le relais, Épaulard se tait alors. */
BOOL dos_poll(void)
{
    HWND act = GetActiveWindow();
    if (!is_dos_window(act)) {
        dos_stop();
        return FALSE;
    }
    box_open();
    if (!box)
        return FALSE;
    if (!dos_relay)
        relay_start();
    box->alive = ++dos_alive;
    relay_out();
    if (dos_braille)
        braille_relay();
    return TRUE;
}
