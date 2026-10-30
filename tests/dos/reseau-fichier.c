/* SPDX-License-Identifier: GPL-3.0-or-later */
/* reseau_bascule de RESEAU.EXE sur un fichier : « bascule 0|1 entrée sortie ». */
#define RESEAU_TEST
#include "reseau.c"

static char in[65536], out[65536];

int main(int argc, char **argv)
{
    FILE *f;
    int n;
    if (argc != 4 || !(f = fopen(argv[2], "rb"))) return 2;
    n = (int)fread(in, 1, sizeof in, f);
    fclose(f);
    n = reseau_bascule(in, n, out, sizeof out, argv[1][0] == '1');
    if (n < 0 || !(f = fopen(argv[3], "wb"))) return 1;
    fwrite(out, 1, n, f);
    fclose(f);
    return 0;
}
