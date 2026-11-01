/* SPDX-License-Identifier: GPL-3.0-or-later
 * Sound Blaster : anneau DMA auto-initialisé, deux demi-tampons de 51,2 ms.
 * DSP 2.0 ou ultérieur ; DOS réel seulement. Voir README.md pour les limites. */
#include "sb.h"
#include "s3time.h"
#include <dos.h>
#include <bios.h>
#include <conio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

volatile SB_STATE sb_state;
typedef char sb_layout_check[(sizeof(SB_STATE) == 28 &&
    offsetof(SB_STATE, ready) == 8 && offsetof(SB_STATE, completed) == 20 &&
    offsetof(SB_STATE, played) == 22 && offsetof(SB_STATE, underruns) == 26) ? 1 : -1];
void __cdecl s3_unsigned8(const short *, unsigned char *, unsigned);
void __cdecl s3_dma8(unsigned, unsigned long, unsigned, unsigned);
void __interrupt __far s3_sb_irq(void);
int __cdecl s3_sb_commit(unsigned, unsigned, unsigned);
void __cdecl s3_sb_wait(unsigned);

static unsigned irq, vector, maskbit, oldmask, allocation;
static unsigned char __far *dma;
static unsigned long physical;
static void (__interrupt __far *old_isr)(void);
static short pcm[SB_HALF];

static int command(unsigned value)
{
    unsigned attempts = 65535;
    while (inp(sb_state.base+12) & 128) if (!--attempts) return 0;
    outp(sb_state.base+12, value);
    return 1;
}
static int reset(void)
{
    unsigned attempts = 65535;
    outp(sb_state.base+6, 1);
    delay(1);
    outp(sb_state.base+6, 0);
    while (!(inp(sb_state.base+14) & 128)) if (!--attempts) return 0;
    return inp(sb_state.base+10) == 0xaa;
}
static int dsp_byte(void)
{
    unsigned attempts = 65535;
    while (!(inp(sb_state.base+14) & 128)) if (!--attempts) return -1;
    return inp(sb_state.base+10);
}
static int settings(void)
{
    const char *p = getenv("BLASTER");
    unsigned found = 0;
    if (!p) return 0;
    while (*p) {
        char key, *end;
        unsigned long value;
        while (*p == ' ') p++;
        if (!*p) break;
        key = *p++;
        value = strtoul(p, &end, key == 'A' || key == 'a' ? 16 : 10);
        if (end == p || (*end && *end != ' ') || value > 65535) return 0;
        if (key == 'A' || key == 'a') { sb_state.base = (unsigned)value; found |= 1; }
        if (key == 'I' || key == 'i') { irq = (unsigned)value; found |= 2; }
        if (key == 'D' || key == 'd') { sb_state.channel = (unsigned)value; found |= 4; }
        p = end;
    }
    return found == 7 && sb_state.base >= 0x210 && sb_state.base <= 0x280 &&
        !(sb_state.base & 15) && (irq == 5 || irq == 7) &&
        (sb_state.channel == 1 || sb_state.channel == 3);
}

/* La conversion ne masque pas les IRQ. Le demi-tampon n'est rendu disponible
 * qu'une fois conversion et silence terminal entièrement écrits. */
static int fill(unsigned bank, SB_SOURCE source, void *context, SB_STATS *stats,
                SB_PROFILE *profile, unsigned long begin)
{
    unsigned i;
    unsigned long stamp = profile ? s3_ticks() : 0;
    int n = source(pcm, SB_HALF, context);
    if (profile) {
        profile->source_ticks += s3_ticks()-stamp;
        if (!stats->produced && n > 0) profile->first_buffer_ticks = s3_ticks()-begin;
    }
    if (n < 0 || n > SB_HALF) return -1;
    s3_unsigned8(pcm, dma + bank*SB_HALF, (unsigned)n);
    for (i = (unsigned)n; i < SB_HALF; i++) dma[bank*SB_HALF+i] = 128;
    if (!s3_sb_commit(bank, (unsigned)n, n < SB_HALF)) return -1;
    stats->produced += n;
    return n < SB_HALF;
}

static int cancelled(void)
{
    /* Ctrl seul n'ajoute rien au tampon BIOS, il faut aussi lire ses modificateurs. */
    if (_bios_keybrd(_KEYBRD_SHIFTSTATUS) & 4) return 1;
    if (kbhit()) { getch(); return 1; }
    return 0;
}

static int run(SB_SOURCE source, void *context, SB_STATS *stats,
                SB_PROFILE *profile, unsigned long begin)
{
    union REGS in, out;
    unsigned segment, seen;
    int ended, status = SB_OK, major, minor;
    clock_t last_irq;
    unsigned long run_start = 0, stop_detection = 0;
    if (!source || !stats) return SB_ERROR;
    memset(stats, 0, sizeof(*stats));
    memset((void *)&sb_state, 0, sizeof(sb_state));
    memset(&in, 0, sizeof(in)); in.w.ax = 0x1600;
    int86(0x2f, &in, &out);
    if ((out.h.al && out.h.al != 0x80) || !settings()) return SB_ERROR;
    if (_dos_allocmem(128, &allocation)) return SB_ERROR;
    segment = allocation;
    physical = (unsigned long)segment << 4;
    if ((physical & 65535UL) + 2*SB_HALF > 65536UL) {
        segment += 64; physical += 1024;
    }
    dma = (unsigned char __far *)MK_FP(segment, 0);
    if (!reset() || !command(0xe1)) { _dos_freemem(allocation); return SB_ERROR; }
    major = dsp_byte(); minor = dsp_byte();
    if (major < 2 || minor < 0) { _dos_freemem(allocation); return SB_ERROR; }
    vector = irq+8;
    maskbit = 1 << irq;
    oldmask = inp(0x21);
    old_isr = _dos_getvect(vector);
    _dos_setvect(vector, s3_sb_irq);
    outp(0x21, oldmask & ~maskbit);
    ended = fill(0, source, context, stats, profile, begin);
    if (ended == 0) ended = fill(1, source, context, stats, profile, begin);
    if (ended < 0) status = SB_ERROR;
    if (!stats->produced) goto close;
    if (cancelled()) status = SB_CANCELLED;
    if (!status) {
        if (!command(0xd1) || !command(0x40) || !command(156) ||
            !command(0x48) || !command((SB_HALF-1) & 255) || !command((SB_HALF-1) >> 8))
            status = SB_ERROR;
        else {
            s3_dma8(sb_state.channel, physical, 2*SB_HALF, 1);
            sb_state.flags = SB_RUNNING;
            if (profile) {
                run_start = s3_ticks();
                profile->submit_ticks = run_start-begin;
            }
            if (!command(0x1c)) status = SB_ERROR;
        }
    }
    seen = 0;
    last_irq = clock();
    while (!status && (sb_state.flags & SB_RUNNING)) {
        unsigned bank;
        if (cancelled()) {
            if (profile) stop_detection = s3_ticks();
            status = SB_CANCELLED; break;
        }
        bank = sb_state.playing ^ 1;
        if (!ended && !sb_state.ready[bank]) {
            ended = fill(bank, source, context, stats, profile, begin);
            if (ended < 0) { status = SB_ERROR; break; }
        }
        if (sb_state.completed != seen) {
            seen = sb_state.completed;
            last_irq = clock();
        } else if (clock()-last_irq > 3*CLOCKS_PER_SEC) {
            status = SB_ERROR; break;
        }
        if (profile) {
            unsigned long stamp = s3_ticks();
            s3_sb_wait(!ended);
            profile->idle_ticks += s3_ticks()-stamp;
        } else s3_sb_wait(!ended);
    }
close:
    command(0xd0);
    if (profile) {
        unsigned long stopped = s3_ticks();
        if (stop_detection) profile->stop_command_ticks = stopped-stop_detection;
        if (run_start) profile->audio_ticks = stopped-run_start;
    }
    command(0xda);
    reset();
    outp(0x0a, sb_state.channel | 4);
    outp(0x21, oldmask | maskbit);
    if (sb_state.flags & SB_STARVED) status = SB_UNDERRUN;
    stats->played = sb_state.played;
    stats->underruns = sb_state.underruns;
    sb_state.flags = 0;
    _dos_setvect(vector, old_isr);
    outp(0x21, oldmask);
    _dos_freemem(allocation);
    return status;
}

int sb_run(SB_SOURCE source, void *context, SB_STATS *stats)
{
    return run(source, context, stats, NULL, 0);
}

int sb_run_measured(SB_SOURCE source, void *context, SB_STATS *stats, SB_PROFILE *profile)
{
    int status;
    unsigned long begin;
    if (!profile || !s3_clock_init()) return SB_ERROR;
    memset(profile, 0, sizeof(*profile));
    begin = s3_ticks();
    status = run(source, context, stats, profile, begin);
    profile->total_ticks = s3_ticks()-begin;
    return status;
}
