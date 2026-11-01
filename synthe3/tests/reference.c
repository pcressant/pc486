/* SPDX-License-Identifier: GPL-3.0-or-later
 * Oracle : moteur copié sans modification, sortie native 10 kHz. */
#include "../vendor/engine.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    char text[241];
    const int16_t *pcm;
    size_t n, i;
    FILE *f;
    if (argc != 5) return 1;
    f = fopen(argv[1], "rb");
    if (!f) return 2;
    n = fread(text, 1, 240, f);
    text[n] = 0;
    fclose(f);
    s3_init();
    if (s3_render(text, atoi(argv[3]), atoi(argv[4]), NULL, &pcm, &n)) return 3;
    f = fopen(argv[2], "wb");
    if (!f) return 4;
    for (i = 0; i < n; i++) {
        fputc(pcm[i] & 255, f);
        fputc(((unsigned short)pcm[i]) >> 8, f);
    }
    s3_close();
    return fclose(f) != 0;
}
