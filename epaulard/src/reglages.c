/* EPAULARD.EXE — réglages (Inser+E) et EPAULARD.INI. */
#include "ep.h"

/* -------------------------------------------------------------- réglages */

/* Réglages (Inser+E), lus et enregistrés dans EPAULARD.INI, section [Parole] */
int cfg[CFG_N] = { 3, 8, 1, 1, 0, 0 };   /* débit, hauteur : défauts de l'Apollo II */
const char *const cfg_key[CFG_N] = {
    "Debit", "Hauteur", "Voix", "Ponctuation", "Echo", "Majuscules" };
const char *const cfg_label[CFG_N] = {
    "Débit", "Hauteur", "Voix", "Ponctuation", "Écho du clavier", "Majuscules" };
const int cfg_min[CFG_N] = { 1, 0, 1, 0, 0, 0 };
const int cfg_max[CFG_N] = { 9, 15, 6, 2, 3, 1 };
char ini_path[144];
int typed_n;             /* longueur du mot en cours de frappe (écho par mots) */
BOOL cfg_on;             /* menu des réglages ouvert */
DWORD cfg_tick;          /* dernière touche dans ce menu */

void cfg_load(HINSTANCE hinst)
{
    int i, n, v;
    n = GetModuleFileName(hinst, ini_path, sizeof(ini_path) - 14);
    while (n > 0 && ini_path[n - 1] != '\\' && ini_path[n - 1] != ':')
        n--;
    lstrcpy(ini_path + n, "EPAULARD.INI");
    for (i = 0; i < CFG_N; i++) {
        v = GetPrivateProfileInt("Parole", cfg_key[i], cfg[i], ini_path);
        if (v >= cfg_min[i] && v <= cfg_max[i])
            cfg[i] = v;
    }
    sheet_load(ini_path);
    grid_load(ini_path);
}

void cfg_save(void)
{
    char v[8];
    int i;
    for (i = 0; i < CFG_N; i++) {
        wsprintf(v, "%d", cfg[i]);
        WritePrivateProfileString("Parole", cfg_key[i], v, ini_path);
    }
}

/* ------------------------------------------------------- réglages (Inser+E) */

/* Menu parlé : Haut et Bas choisissent le réglage, Gauche et Droite changent sa
   valeur (appliquée aussitôt), Entrée enregistre, Échap rétablit. Pendant ce temps,
   EPAUHOOK.DLL donne toutes les touches à Épaulard (EpCfgMode). */
int cfg_item;
int cfg_before[CFG_N];

void cfg_value(int i, char *out)
{
    static const char *const punct[] = { "aucune", "certaine", "toute" };
    static const char *const echo[] = { "caractères", "mots", "caractères et mots", "aucun" };
    if (i == CFG_PUNCT)
        lstrcpy(out, punct[cfg[i]]);
    else if (i == CFG_ECHO)
        lstrcpy(out, echo[cfg[i]]);
    else if (i == CFG_CAPS)
        lstrcpy(out, cfg[i] ? "dire majuscule" : "ne rien dire");
    else
        wsprintf(out, "%d", cfg[i]);
}

void cfg_say(BOOL with_name, BOOL cut)
{
    char out[TXT], v[40];
    out[0] = 0;
    if (with_name)
        append(out, cfg_label[cfg_item]);
    cfg_value(cfg_item, v);
    append(out, v);
    say(out, cut);
}

void cfg_close(BOOL keep)
{
    cfg_on = FALSE;
    EpCfgMode(FALSE);
    if (keep) {
        cfg_save();
        say("Réglages enregistrés", TRUE);
    } else {
        memcpy(cfg, cfg_before, sizeof(cfg));
        silence();
        voice_apply();
        say("Réglages annulés", FALSE);
    }
}

void cfg_open(void)
{
    sa_stop(TRUE);
    memcpy(cfg_before, cfg, sizeof(cfg));
    cfg_on = TRUE;
    cfg_item = 0;
    cfg_tick = GetTickCount();
    EpCfgMode(TRUE);
    say("Réglages d'Épaulard", TRUE);
    {
        char out[TXT], v[40];
        out[0] = 0;
        append(out, cfg_label[cfg_item]);
        cfg_value(cfg_item, v);
        append(out, v);
        say(out, FALSE);
    }
}

void cfg_key_in(WPARAM vk)
{
    int d = 0;
    cfg_tick = GetTickCount();
    switch (vk) {
    case VK_UP:
        cfg_item = (cfg_item + CFG_N - 1) % CFG_N;
        cfg_say(TRUE, TRUE);
        return;
    case VK_DOWN:
    case VK_TAB:
        cfg_item = (cfg_item + 1) % CFG_N;
        cfg_say(TRUE, TRUE);
        return;
    case VK_LEFT:
        d = -1;
        break;
    case VK_RIGHT:
        d = 1;
        break;
    case VK_RETURN:
    case 'E':
        cfg_close(TRUE);
        return;
    case VK_ESCAPE:
        cfg_close(FALSE);
        return;
    case VK_CONTROL:
        silence();
        return;
    case 'H':
        say("Réglages : flèches haut et bas, choisir ; gauche et droite, changer ; "
            "Entrée, enregistrer ; Échap, annuler.", TRUE);
        return;
    default:
        return;
    }
    if (cfg[cfg_item] + d < cfg_min[cfg_item] || cfg[cfg_item] + d > cfg_max[cfg_item]) {
        cfg_say(FALSE, TRUE);           /* déjà au bout */
        return;
    }
    cfg[cfg_item] += d;
    silence();
    if (cfg_item <= CFG_PUNCT)
        voice_apply();                  /* entendre aussitôt la nouvelle voix */
    cfg_say(FALSE, FALSE);
}
