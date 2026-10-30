/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Banc de RESEAU.EXE : bascule des lignes réseau de SYSTEM.INI (fonction pure). */
#define RESEAU_TEST
#include "reseau.c"

static int fails;

#define CHECK(c, msg) do { if (!(c)) { printf("ÉCHEC : %s\n", msg); fails++; } } while (0)

static const char section[] =
    "[pc486.reseau]\r\n"
    "boot/network.drv=wfwnet.drv|\r\n"
    "386Enh/network=*vnetbios,*vwc,vnetsup.386|*dosnet,*vnetbios\r\n"
    "386Enh/netcard=ne2000.386|-\r\n"
    "386Enh/transport=vip.386,vtcp.386|-\r\n"
    "Network/winnet=wfwnet/00025100|nonet\r\n"
    "MSTCP/Interfaces=ms$ne2clone0|-\r\n";

/* Fichier de Windows sans réseau, tel que l'installation l'a écrit. */
static const char sans[] =
    "[boot]\r\nshell=progman.exe\r\nnetwork.drv=\r\nmouse.drv=mouse.drv\r\n\r\n"
    "[386Enh]\r\ndevice=*vpd\r\nnetwork=*dosnet,*vnetbios\r\nnetheapsize=16\r\n"
    "COM3Irq=9\r\n\r\n"
    "[Network]\r\nwinnet=nonet\r\nmultinet=nonet\r\n\r\n";

static char a[8192], b[8192], c[8192], d[8192];

static int build(char *dst, const char *body)
{
    strcpy(dst, body);
    strcat(dst, section);
    return (int)strlen(dst);
}

static void scenario_aller_retour(void)
{
    int n = build(a, sans), m, k, j;
    m = reseau_bascule(a, n, b, sizeof b, 1);
    CHECK(m > 0, "bascule avec carte");
    b[m < 0 ? 0 : m] = 0;
    CHECK(strstr(b, "network.drv=wfwnet.drv\r\nmouse.drv") != 0, "network.drv remplacé sur place");
    CHECK(strstr(b, "network=*vnetbios,*vwc,vnetsup.386\r\n") != 0, "network= remplacé");
    CHECK(strstr(b, "COM3Irq=9\r\nnetcard=ne2000.386\r\ntransport=vip.386,vtcp.386\r\n\r\n[Network]") != 0,
          "lignes ajoutées en fin de section, avant la ligne vide");
    CHECK(strstr(b, "winnet=wfwnet/00025100\r\nmultinet") != 0, "winnet remplacé");
    CHECK(strstr(b, "[MSTCP]\r\nInterfaces=ms$ne2clone0\r\n") != 0, "section absente ajoutée");
    CHECK(strstr(b, "[pc486.reseau]\r\nboot/network.drv=wfwnet.drv|\r\n") != 0, "section de bascule conservée");
    k = reseau_bascule(b, m, c, sizeof c, 1);
    CHECK(k == m && !memcmp(b, c, m), "idempotent avec carte");
    k = reseau_bascule(b, m, c, sizeof c, 0);
    CHECK(k > 0, "bascule sans carte");
    c[k < 0 ? 0 : k] = 0;
    CHECK(!strstr(c, "\nnetcard=") && !strstr(c, "\ntransport=") && !strstr(c, "\nInterfaces="),
          "lignes retirées sans carte");
    CHECK(strstr(c, "network.drv=\r\n") && strstr(c, "winnet=nonet") && strstr(c, "network=*dosnet,*vnetbios"),
          "valeurs d'origine rendues");
    /* La section [MSTCP] vide reste (titre seul) ; le reste est identique à l'origine. */
    j = reseau_bascule(c, k, d, sizeof d, 0);
    CHECK(j == k && !memcmp(c, d, k), "idempotent sans carte");
    CHECK(!strncmp(c, sans, strlen(sans) - 2), "sans carte : contenu d'origine en tête");
}

static void scenario_refus(void)
{
    int n;
    strcpy(a, sans);
    n = (int)strlen(a);
    CHECK(reseau_bascule(a, n, b, sizeof b, 1) == -1, "section [pc486.reseau] absente : refus");
    n = build(a, sans);
    CHECK(reseau_bascule(a, n, b, 40, 1) == -1, "sortie trop petite : refus");
    strcpy(a, sans);
    strcat(a, "[pc486.reseau]\r\nmauvaise-ligne=sans-barre\r\n");
    CHECK(reseau_bascule(a, (int)strlen(a), b, sizeof b, 1) == -1, "entrée mal formée : refus");
}

static void scenario_fins(void)
{
    int n, m;
    /* Fins de ligne Unix, casse différente, fin de fichier DOS (1Ah) conservée. */
    strcpy(a, "[BOOT]\nNetwork.Drv=\n[pc486.reseau]\nboot/network.drv=wfwnet.drv|\n\x1a");
    n = (int)strlen(a);
    m = reseau_bascule(a, n, b, sizeof b, 1);
    CHECK(m > 0, "fichier LF");
    b[m < 0 ? 0 : m] = 0;
    CHECK(!strncmp(b, "[BOOT]\nNetwork.Drv=wfwnet.drv\n", 30), "clé trouvée sans tenir compte de la casse");
    CHECK(m > 0 && b[m - 1] == 0x1a, "1Ah final conservé");
    /* Section absente d'un fichier LF terminé par 1Ah : ajoutée avant, en LF. */
    strcpy(a, "[boot]\nshell=progman.exe\n[pc486.reseau]\nMSTCP/Interfaces=ms$ne2clone0|-\n\x1a");
    n = (int)strlen(a);
    m = reseau_bascule(a, n, b, sizeof b, 1);
    CHECK(m > 0, "fichier LF, section absente");
    b[m < 0 ? 0 : m] = 0;
    CHECK(m > 0 && !strchr(b, '\r'), "ajouts en LF comme le fichier");
    CHECK(m > 0 && strstr(b, "\n[MSTCP]\nInterfaces=ms$ne2clone0\n\x1a") == b + m - 34,
          "section ajoutée avant la fin de fichier DOS");
}

static void scenario_doublon(void)
{
    int n, m;
    /* Windows lit la première occurrence : seule elle est basculée. */
    strcpy(a, "[386Enh]\r\nnetcard=vieux.386\r\nnetcard=autre.386\r\n"
              "[pc486.reseau]\r\n386Enh/netcard=ne2000.386|-\r\n");
    n = (int)strlen(a);
    m = reseau_bascule(a, n, b, sizeof b, 1);
    b[m < 0 ? 0 : m] = 0;
    CHECK(strstr(b, "[386Enh]\r\nnetcard=ne2000.386\r\nnetcard=autre.386\r\n") != 0,
          "clé en double : première remplacée, suivante gardée");
}

int main(void)
{
    scenario_aller_retour();
    scenario_refus();
    scenario_fins();
    scenario_doublon();
    if (fails) return 1;
    puts("RESEAU : bascule vérifiée.");
    return 0;
}
