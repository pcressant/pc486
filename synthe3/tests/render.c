/* SPDX-License-Identifier: GPL-3.0-or-later
 * Rendu hôte de référence : un texte CP850 passé à l'Apollo local (ap.c) et au moteur
 * de production, PCM 16 bits brut à 10 kHz. Sert à comparer la parole enregistrée
 * sous 86Box (tests/test_synthe3_86box.py) à ce que le moteur a réellement produit.
 *   render "texte CP850" sortie.raw
 */
#include "ap.h"
#include "s3.h"
#include <stdio.h>

static short pcm[512];

int main(int argc, char **argv)
{
    FILE *f;
    const char *s;
    unsigned n;
    if (argc != 3 || (f = fopen(argv[2], "wb")) == NULL) return 2;
    s3_init();
    ap_init();
    for (s = argv[1]; *s; s++) ap_put((unsigned char)*s);
    ap_put('\r');
    while ((n = ap_render(pcm, 512)) != 0) fwrite(pcm, 2, n, f);
    fclose(f);
    return 0;
}
