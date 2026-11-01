/* SPDX-License-Identifier: GPL-3.0-or-later
 * Banc DOS : PIT haute résolution, aucun WAV ni écriture pendant les mesures PCM.
 * Les ticks sont du temps écoulé. Activité estimée = total moins attente HLT. */
#include "s3.h"
#include "sb.h"
#include "s3time.h"
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <bios.h>

#ifdef S3_PROFILE
extern unsigned long s3_prof_periods, s3_prof_prepares, s3_prof_loop_ticks,
                     s3_prof_control_ticks, s3_prof_prepare_ticks;
#endif
unsigned __cdecl s3_cpu_class(void);
static char text[241], segment[256];
static short pcm[512];
static unsigned short weights[256];
typedef struct { int started, signal; unsigned calls; } SOURCE;
static void audio_marker(void);
static int produce(short *out, unsigned capacity, void *context)
{
    SOURCE *source = context;
    if (!source->started) {
        source->started = 1;
        if (s3_start(text, 3, 5) != S3_OK) return -1;
    }
    /* Troisième appel : le DMA joue déjà, les deux premiers ont prérempli l'anneau. */
    if (++source->calls == 3 && source->signal) audio_marker();
    return (int)s3_read(out, capacity);
}
static unsigned long hash(unsigned long value, unsigned short sample)
{
    value = (value ^ (sample & 255)) * 16777619UL;
    return (value ^ (sample >> 8)) * 16777619UL;
}
static void audio_marker(void)
{
    const char *p = "S3_BENCH_AUDIO\r\n";
    union REGS in, out;
    memset(&in, 0, sizeof(in));
    while (*p) {
        in.h.ah = 0x0e; in.h.al = *p++; in.h.bl = 7;
        int86(0x10, &in, &out);
    }
}
int main(int argc, char **argv)
{
    FILE *f;
    unsigned n, i, repeat;
    unsigned long before, pure = 0, samples = 0, checksum = 2166136261UL;
    unsigned long phon = 0, kernel = 2166136261UL, scalar_ticks = 0, prepared_ticks = 0, warm_ticks = 0;
    unsigned long output_ticks = 0, state[2];
    unsigned long minimum = 0xffffffffUL;
    unsigned long init_ticks;
    SB_STATS stats;
    SB_PROFILE profile;
    SOURCE source = {0};
    int status;
    int audio_only = argc == 3 && !strcmp(argv[1], "-s");
    static const unsigned char cp850[128] = {
#include "cp850.inc"
    };
    if ((argc != 2 && !audio_only) || !s3_clock_init()) return 1;
    f = fopen(argv[argc-1], "rb");
    if (!f) return 1;
    n = (unsigned)fread(text, 1, 240, f);
    if (ferror(f) || fgetc(f) != EOF || memchr(text, 0, n)) { fclose(f); return 1; }
    fclose(f); text[n] = 0;
    for (i = 0; i < n; i++)
        if ((unsigned char)text[i] >= 128) text[i] = (char)cp850[(unsigned char)text[i]-128];
    for (i = 0; i < 100; i++) {
        unsigned long after;
        before = s3_ticks(); after = s3_ticks();
        if (after-before < minimum) minimum = after-before;
    }
    before = s3_ticks(); s3_init(); init_ticks = s3_ticks()-before;
    if (!audio_only) {
    before = s3_ticks();
    status = s3_start(text, 3, 5);
    phon = s3_ticks()-before;
    if (status != S3_OK) return 1;
    do {
        before = s3_ticks(); n = s3_read(pcm, 512); pure += s3_ticks()-before;
        samples += n;
        /* Empreinte hors des fenêtres chronométrées. */
        for (i = 0; i < n; i++) checksum = hash(checksum, (unsigned short)pcm[i]);
    } while (n);
    s3_cancel();
    for (i = 0; i < 256; i++) segment[i] = (char)((73*i+19) & 255);
    before = s3_ticks();
    for (repeat = 0; repeat < 200; repeat++)
        for (i = 0; i < 101; i++) kernel = hash(kernel,
            (unsigned short)s3_window((short)i, segment, 97, 101, 31, 1));
    scalar_ticks = s3_ticks()-before;
    before = s3_ticks();
    for (repeat = 0; repeat < 200; repeat++) {
        s3_weights(97, weights);
        for (i = 0; i < 101; i++) kernel = hash(kernel,
            (unsigned short)s3_weighted((short)i, segment, 97, 101, 31, 1, weights));
    }
    prepared_ticks = s3_ticks()-before;
    s3_weights(97, weights);
    before = s3_ticks();
    for (repeat = 0; repeat < 200; repeat++)
        for (i = 0; i < 101; i++) kernel = hash(kernel,
            (unsigned short)s3_weighted((short)i, segment, 97, 101, 31, 1, weights));
    warm_ticks = s3_ticks()-before;
    state[0] = state[1] = 32768;
    before = s3_ticks();
    for (repeat = 0; repeat < 200; repeat++)
        for (i = 0; i < 101; i++) kernel = hash(kernel,
            (unsigned short)s3_output((short)(i*131-6000), (long *)state));
    output_ticks = s3_ticks()-before;
    }
    s3_init();
    source.signal = audio_only;
    status = sb_run_measured(produce, &source, &stats, &profile);
    s3_cancel();
    printf("hz=%lu pair_min=%lu init=%lu phon=%lu pcm=%lu samples=%lu hash=%08lx\n",
        S3_PIT_HZ, minimum, init_ticks, phon, pure, samples, checksum);
    printf("window_calls=%u scalar=%lu prepared=%lu prepared_warm=%lu output=%lu checksum=%08lx\n",
        audio_only ? 0 : 20200, scalar_ticks, prepared_ticks, warm_ticks, output_ticks, kernel);
    printf("cpu_class=%u fpu=%u variant=%s\n", s3_cpu_class(), (_bios_equiplist() & 2) ? 1 : 0, s3_variant());
#ifdef S3_PROFILE
    /* Temps PIT inclus dans pcm ; chaque borne coûte une lecture d'horloge. */
    printf("periods=%lu prepares=%lu loop=%lu control=%lu prepare=%lu\n", s3_prof_periods,
        s3_prof_prepares, s3_prof_loop_ticks, s3_prof_control_ticks, s3_prof_prepare_ticks);
#endif
    printf("total=%lu source=%lu idle=%lu busy_est=%lu audio=%lu\n", profile.total_ticks,
        profile.source_ticks, profile.idle_ticks, profile.total_ticks-profile.idle_ticks, profile.audio_ticks);
    printf("first_buffer=%lu submit=%lu stop_command=%lu acoustic_start=ND acoustic_stop=ND\n",
        profile.first_buffer_ticks, profile.submit_ticks, profile.stop_command_ticks);
    printf("status=%d produced=%lu played=%lu underruns=%u\n", status,
        (unsigned long)stats.produced, (unsigned long)stats.played, stats.underruns);
    printf("clock_fault=%u\n", s3_clock_fault);
    if (s3_clock_fault) return 1;
    return status;
}
