/* Baum série 19200 8N1. Réponses brutes ; requêtes avec ESC doublé.
   Aucun appel bloquant, aucune allocation dynamique. Compilé dans EPAUHOOK.DLL :
   les fonctions exportées sont appelées depuis la pile d'une autre tâche. */
#include <windows.h>
#include <string.h>
#include "braille.h"
#include "brfr.h"

#define MAXCELLS 80
#define TEXTSIZE 512
static int port = -1, width, offset, cursor = -1, sel_from = -1, sel_to = -1;
static char text[TEXTSIZE];
static BYTE cells[MAXCELLS], sent[MAXCELLS];
static char tx[164];
static int tx_n, tx_pos;
static BOOL dirty, valid;
static DWORD probe_tick, rx_tick, tx_tick;
static int command, need, used;
static BYTE rx[16], keys;
static int events[16], event_in, event_out;

static void push_key(int key)
{
    int next = (event_in + 1) % 16;
    if (next != event_out) {
        events[event_in] = key;
        event_in = next;
    }
}

static void queue(int cmd, const BYTE *data, int n)
{
    int i;
    tx_tick = GetTickCount();
    tx_pos = 0;
    tx_n = 0;
    tx[tx_n++] = 0x1b;
    tx[tx_n++] = (char) cmd;
    for (i = 0; i < n; i++) {
        tx[tx_n++] = (char) data[i];
        if (data[i] == 0x1b) tx[tx_n++] = 0x1b;
    }
}

void BRAPI br_open(void)
{
    char name[12], config[32];
    int number = GetProfileInt("epaulard", "BraillePort", 0);
    DCB dcb;
    if (number < 1 || number > 4 || number == 1) return;
    wsprintf(name, "COM%d", number);
    port = OpenComm(name, 512, 512);
    if (port < 0) return;
    wsprintf(config, "COM%d:19200,n,8,1", number);
    if (BuildCommDCB(config, &dcb) != 0) {
        CloseComm(port);
        port = -1;
        return;
    }
    dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = 0;
    dcb.fInX = dcb.fOutX = 0;
    dcb.fRtsDisable = dcb.fDtrDisable = 0;
    if (SetCommState(&dcb) != 0) {
        CloseComm(port);
        port = -1;
        return;
    }
    probe_tick = GetTickCount() - 3000;
}

BOOL BRAPI br_enabled(void) { return port >= 0; }

void BRAPI br_close(void)
{
    if (port >= 0) CloseComm(port);
    port = -1;
}

void BRAPI br_text(const char FAR *s, int col, BOOL follow)
{
    int n;
    BOOL changed;
    static char next[TEXTSIZE];         /* pas sur la pile de la tâche appelante */
    lstrcpyn(next, s, sizeof(next));
    changed = lstrcmp(next, text) != 0;
    if (changed) {
        lstrcpy(text, next);
        dirty = TRUE;
    }
    n = lstrlen(text);
    if (col > n) col = n;
    if (col != cursor) {
        cursor = col;
        dirty = TRUE;
        follow = TRUE;
    }
    if (changed && col < 0) offset = 0;
    if (follow && width && col >= 0 && (col < offset || col >= offset + width)) {
        offset = (col / width) * width;
        dirty = TRUE;
    }
    if (offset > n) { offset = 0; dirty = TRUE; }
}

void BRAPI br_select(int from, int to)
{
    if (from < 0 || from >= to)
        from = to = -1;
    if (from != sel_from || to != sel_to) {
        sel_from = from;
        sel_to = to;
        dirty = TRUE;
    }
}

void BRAPI br_pan(int direction)
{
    int next;
    if (!width) return;
    next = direction == 0 ? (cursor < 0 ? 0 : cursor / width * width) :
           offset + direction * width;
    if (next < 0) next = 0;
    if (next > lstrlen(text)) return;
    if (offset != next) { offset = next; dirty = TRUE; }
}

int BRAPI br_offset(void) { return offset; }
int BRAPI br_key(void)
{
    int k;
    if (event_in == event_out) return 0;
    k = events[event_out];
    event_out = (event_out + 1) % 16;
    return k;
}

static void response(void)
{
    BYTE released;
    if (command == 1) {
        if (rx[0] > 0 && rx[0] <= MAXCELLS) {
            if (width != rx[0]) { width = rx[0]; offset = 0; }
            valid = FALSE;
            dirty = TRUE;
        }
    } else if (command == 0x24) {
        released = (BYTE) (keys & ~rx[0]);
        if (released) {
            if (keys == 2) push_key(BR_BACK);
            else if (keys == 16) push_key(BR_FORWARD);
            else if (keys == 1) push_key(BR_UP);
            else if (keys == 4) push_key(BR_DOWN);
            else if (keys == 8) push_key(BR_HOME);
            keys = 0; /* ignorer les autres relâchements de la combinaison */
        } else keys = rx[0];
    } else if (command == 0x27 && rx[0] > 0 && rx[0] <= width)
        push_key(BR_ROUTE + rx[0] - 1);
}

static void receive(BYTE b)
{
    if (!need) {
        if (!command) { if (b == 0x1b) command = -1; return; }
        command = b;
        used = 0;
        switch (b) {
        case 1: case 0x17: case 0x16: case 0x24: case 0x27: case 0x34: need = 1; break;
        case 0x33: need = 2; break;
        case 0x22: need = width > 40 ? 10 : 5; break;
        case 0x84: need = 16; break;
        case 0x8a: need = 8; break;
        default: command = b == 0x1b ? -1 : 0; break;
        }
        return;
    }
    rx[used++] = b;
    if (used == need) {
        response();
        command = need = 0;
    }
}

void BRAPI br_tick(void)
{
    char buf[64];
    BYTE on = 1;
    int n, i, len;
    DWORD now;
    COMSTAT st;
    if (port < 0) return;
    now = GetTickCount();
    if ((command || need) && now - rx_tick > 500) command = need = 0;
    /* Une seule lecture par passage : ne pas affamer la boucle Windows. */
    n = ReadComm(port, buf, sizeof(buf));
    if (n < 0) { GetCommError(port, &st); n = -n; }
    if (n > 0) {
        rx_tick = now;
        for (i = 0; i < n; i++) receive((BYTE) buf[i]);
    }
    if (tx_pos < tx_n && now - tx_tick > 2000) {
        FlushComm(port, 0);
        tx_pos = tx_n = 0;
        command = need = 0;
        valid = FALSE;
        dirty = TRUE;
        probe_tick = now - 3000;
    }
    if (tx_pos < tx_n) {
        n = WriteComm(port, tx + tx_pos, tx_n - tx_pos);
        if (n < 0) { GetCommError(port, &st); n = -n; }
        if (n > 0) tx_pos += n;
        return;
    }
    /* Réinitialiser la passerelle après une reconnexion, même sans mouvement. */
    if (now - probe_tick >= 3000) {
        queue(0x15, &on, 1);
        probe_tick = now;
        return;
    }
    if (!width || !dirty) return;
    len = lstrlen(text);
    for (i = 0; i < width; i++) {
        n = offset + i;
        cells[i] = n < len ? br_fr[(BYTE) text[n]] : 0;
        /* curseur fixe et texte sélectionné : points 7 et 8 */
        if (n == cursor || (n >= sel_from && n < sel_to)) cells[i] |= 0xc0;
    }
    dirty = FALSE;
    if (valid && memcmp(cells, sent, width) == 0) return;
    memcpy(sent, cells, width);
    valid = TRUE;
    queue(1, cells, width);
}
