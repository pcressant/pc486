/* SPDX-License-Identifier: GPL-3.0-or-later
 * S3PLAY : WAV PCM 16 bits à 10 kHz vers le double tampon Sound Blaster.
 * Le pilote partagé ne dépend pas des fichiers ni de la synthèse. */
#include "sb.h"
#include <stdio.h>
#include <string.h>

typedef struct { FILE *file; unsigned long remaining; } WAV_SOURCE;
static unsigned get16(const unsigned char *p) { return p[0] | ((unsigned)p[1] << 8); }
static unsigned long get32(const unsigned char *p)
{ return get16(p) | ((unsigned long)get16(p+2) << 16); }
static int produce(short *pcm, unsigned capacity, void *context)
{
    WAV_SOURCE *source = context;
    unsigned n = source->remaining > capacity ? capacity : (unsigned)source->remaining;
    if (fread(pcm, 2, n, source->file) != n) return -1;
    source->remaining -= n;
    return (int)n;
}
int main(int argc, char **argv)
{
    unsigned char header[44];
    WAV_SOURCE source;
    SB_STATS stats;
    int status;
    if (argc != 2) { puts("S3PLAY MICHEL.WAV ; Ctrl ou une touche arrête la lecture."); return 1; }
    source.file = fopen(argv[1], "rb");
    if (!source.file) { perror(argv[1]); return 1; }
    if (fread(header, 1, 44, source.file) != 44 || memcmp(header, "RIFF", 4) ||
        memcmp(header+8, "WAVEfmt ", 8) || get32(header+16) != 16 ||
        get16(header+20) != 1 || get16(header+22) != 1 || get32(header+24) != 10000 ||
        get16(header+34) != 16 || memcmp(header+36, "data", 4) ||
        (get32(header+40) & 1)) {
        puts("WAV attendu : format S3DOS, mono PCM 16 bits, 10 kHz.");
        fclose(source.file); return 1;
    }
    source.remaining = get32(header+40) / 2;
    status = sb_run(produce, &source, &stats);
    fclose(source.file);
    if (status == SB_ERROR) puts("Vérifier BLASTER : A220 I5 D1, DSP >= 2, DOS réel.");
    printf("S3PLAY : %lu produits, %lu joués, %u sous-alimentations, état %d.\n",
           (unsigned long)stats.produced, (unsigned long)stats.played, stats.underruns, status);
    return status;
}
