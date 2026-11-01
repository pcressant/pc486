/* SPDX-License-Identifier: GPL-3.0-or-later
 * Programme autonome : texte Latin-1 ou CP850 -> WAV PCM 16 bits à 10 kHz.
 * Aucun port série, aucune dépendance au lecteur d'écran ou à l'hôte. */
#include "s3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __DOS__
#include <conio.h>
#include "sb.h"
static int produce(short *pcm, unsigned capacity, void *context)
{
    (void)context;
    return (int)s3_read(pcm, capacity);
}
#endif

static short block[512];
static char text[241];

static int le16(FILE *f, unsigned v)
{
    return fputc(v & 255, f) != EOF && fputc((v >> 8) & 255, f) != EOF;
}
static int le32(FILE *f, unsigned long v)
{
    return le16(f, (unsigned)(v & 65535UL)) && le16(f, (unsigned)(v >> 16));
}
static int header(FILE *f, unsigned long samples)
{
    return fseek(f, 0, SEEK_SET) == 0 &&
        fwrite("RIFF", 1, 4, f) == 4 && le32(f, 36 + samples * 2) &&
        fwrite("WAVEfmt ", 1, 8, f) == 8 && le32(f, 16) &&
        le16(f, 1) && le16(f, 1) && le32(f, S3_HZ) && le32(f, S3_HZ * 2UL) &&
        le16(f, 2) && le16(f, 16) && fwrite("data", 1, 4, f) == 4 && le32(f, samples * 2);
}

static int level(const char *s)
{
    char *end;
    long value = strtol(s, &end, 10);
    return *s && !*end && value >= 0 && value <= 15 ? (int)value : -1;
}

int main(int argc, char **argv)
{
    const char *output = "MICHEL.WAV", *input = NULL;
    int rate = 3, pitch = 5, latin1 = 0, direct = 0, output_set = 0, i, status = 0;
    unsigned n, j;
    unsigned long total = 0;
    clock_t start;
    FILE *f;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-l")) latin1 = 1;
#ifdef __DOS__
        else if (!strcmp(argv[i], "-s")) direct = 1;
#endif
        else if ((!strcmp(argv[i], "-o") || !strcmp(argv[i], "-f") ||
                  !strcmp(argv[i], "-r") || !strcmp(argv[i], "-p")) && i+1 < argc) {
            char option = argv[i][1];
            const char *value = argv[++i];
            if (option == 'o') { output = value; output_set = 1; }
            else if (option == 'f') input = value;
            else if (option == 'r') rate = level(value);
            else pitch = level(value);
        } else {
            unsigned used = (unsigned)strlen(text), len = (unsigned)strlen(argv[i]);
            if (argv[i][0] == '-' || used + len + (used != 0) > 240) goto usage;
            if (used) strcat(text, " ");
            strcat(text, argv[i]);
        }
    }
    if (rate < 0 || pitch < 0 || (input && text[0]) || (direct && output_set)) goto usage;
    if (input) {
        f = fopen(input, "rb");
        if (!f) { perror(input); return 1; }
        n = (unsigned)fread(text, 1, 240, f);
        if (ferror(f) || fgetc(f) != EOF || memchr(text, 0, n)) {
            fclose(f); goto usage;
        }
        fclose(f);
        text[n] = 0;
    }
    if (!text[0]) goto usage;
    if (!latin1) {
        /* Table CP850 -> Latin-1 générée et vérifiée par les tests. */
        static const unsigned char cp850[128] = {
#include "cp850.inc"
        };
        for (i = 0; text[i]; i++)
            if ((unsigned char)text[i] >= 128) text[i] = (char)cp850[(unsigned char)text[i]-128];
    }
    s3_init();
    start = clock();
    if (s3_start(text, rate, pitch) != S3_OK) {
        fputs("Texte refusé (240 octets au plus, marques internes réservées).\n", stderr);
        return 1;
    }
#ifdef __DOS__
    if (direct) {
        SB_STATS stats;
        status = sb_run(produce, NULL, &stats);
        s3_cancel();
        if (status == SB_ERROR) puts("Vérifier BLASTER : A220 I5 D1, DSP >= 2, DOS réel.");
        printf("S3DIRECT : %lu produits, %lu joués, %u sous-alimentations, état %d.\n",
               (unsigned long)stats.produced, (unsigned long)stats.played, stats.underruns, status);
        return status;
    }
#endif
    f = fopen(output, "wb");
    if (!f) { perror(output); s3_cancel(); return 1; }
    if (!header(f, 0)) status = 1;
    while (!status && (n = s3_read(block, 512)) != 0) {
        for (j = 0; j < n; j++) if (!le16(f, (unsigned short)block[j])) { status = 1; break; }
        total += j;
#ifdef __DOS__
        if (kbhit()) { getch(); s3_cancel(); status = 2; }
#endif
    }
    s3_cancel();
    if (!header(f, total)) status = 1;
    if (fclose(f)) status = 1;
    printf("S3DOS : %lu échantillons, %lu ticks à %lu Hz, état %d.\n", total,
           (unsigned long)(clock()-start), (unsigned long)CLOCKS_PER_SEC, status);
    return status;
usage:
    fputs("S3DOS [-r 0..15] [-p 0..15] [-o MICHEL.WAV] [-l] [-f TEXTE.TXT | texte]\n"
          "Texte CP850 par défaut ; -l : Latin-1. 240 octets maximum.\n", stderr);
#ifdef __DOS__
    fputs("S3DOS -s : parler directement, sans WAV. Ctrl ou une touche : arrêt.\n", stderr);
#endif
    return 1;
}
