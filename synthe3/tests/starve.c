/* SPDX-License-Identifier: GPL-3.0-or-later
 * Banc DOS : producteur volontairement trop lent puis reprise du pilote. */
#include "sb.h"
#include "s3time.h"
#include <dos.h>
#include <conio.h>
#include <assert.h>
#include <stdio.h>

static unsigned calls;
extern volatile SB_STATE sb_state;
void __cdecl s3_sb_wait(unsigned);
static void pending_keyboard(void)
{
    unsigned i, saved_tail, tail;
    unsigned char __far *shift = (unsigned char __far *)MK_FP(0x40, 0x17);
    unsigned __far *head = (unsigned __far *)MK_FP(0x40, 0x1a);
    unsigned __far *end = (unsigned __far *)MK_FP(0x40, 0x1c);
    unsigned char saved_shift = *shift;
    unsigned long before, elapsed;
    assert(s3_clock_init());
    sb_state.flags = SB_RUNNING;
    *shift |= 4;
    before = s3_ticks();
    for (i = 0; i < 20; i++) s3_sb_wait(0);
    elapsed = s3_ticks()-before;
    *shift = saved_shift;
    assert(elapsed < S3_PIT_HZ/100);
    saved_tail = *end;
    tail = *head+2; if (tail >= 0x3e) tail = 0x1e;
    *end = tail;
    before = s3_ticks();
    for (i = 0; i < 20; i++) s3_sb_wait(0);
    elapsed = s3_ticks()-before;
    *end = saved_tail;
    assert(elapsed < S3_PIT_HZ/100);
    sb_state.flags = 0;
    puts("clavier en attente : OK");
}
static int slow(short *pcm, unsigned capacity, void *context)
{
    unsigned i;
    (void)context;
    if (++calls == 3) delay(250);
    for (i = 0; i < capacity; i++) pcm[i] = 0;
    return (int)capacity;
}
static int finish(short *pcm, unsigned capacity, void *context)
{
    unsigned i, n = ++calls == 1 ? capacity : 100;
    (void)context;
    for (i = 0; i < n; i++) pcm[i] = 0;
    return (int)n;
}
int main(void)
{
    SB_STATS stats;
    void (__interrupt __far *previous)(void) = _dos_getvect(13);
    unsigned mask = inp(0x21);
    int status;
    pending_keyboard();
    status = sb_run(slow, NULL, &stats);
    printf("retard : etat %d, produits %lu, joues %lu, pertes %u\n", status,
           (unsigned long)stats.produced, (unsigned long)stats.played, stats.underruns);
    assert(status == SB_UNDERRUN && stats.underruns == 1 && stats.played == 1024);
    assert(inp(0x21) == mask && _dos_getvect(13) == previous);
    calls = 0;
    status = sb_run(finish, NULL, &stats);
    assert(status == SB_OK && stats.produced == 612 && stats.played == 612 && !stats.underruns);
    assert(inp(0x21) == mask && _dos_getvect(13) == previous);
    puts("reprise : OK");
    return 0;
}
