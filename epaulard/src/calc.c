/* EPAULARD.EXE — Calculatrice de Windows (SciCalc). */
#include "ep.h"

/* Calculatrice de Windows 3.1 : SciCalc garde le focus, ses touches sont
   dessinées. La valeur est un STATIC (414 en standard, 413 en scientifique),
   pas l'EDIT caché 495. Lire le contrôle visible, même sans dessin GDI récent. */
HWND calc_owner;
char calc_last[TXT];
BOOL calc_force;

/* États et parcours propres à CALC.EXE 3.1, sans déplacer son focus. */
char calc_previous[CALC_PARTS][64];
int calc_view, calc_button = -1; /* 0 résultat, 1 bouton, 2 états */
BOOL calc_queue;

/* Identifiants WM_COMMAND observés dans CALC.EXE français (3.10). Certains
   changent de sens avec le mode : 103 = racine en standard, carré en scientifique. */
const CALCBUTTON calc_buttons[] = {
    {81, "effacer tout", CALC_BOTH}, {82, "effacer l'entrée", CALC_BOTH},
    {83, "retour arrière", CALC_BOTH},
    {114, "mémoriser le résultat", CALC_BOTH},
    {113, "rappeler la mémoire", CALC_BOTH},
    {112, "effacer la mémoire", CALC_BOTH},
    {115, "ajouter à la mémoire", CALC_BOTH},
    {48, "zéro", CALC_BOTH}, {49, "un", CALC_BOTH},
    {50, "deux", CALC_BOTH}, {51, "trois", CALC_BOTH},
    {52, "quatre", CALC_BOTH}, {53, "cinq", CALC_BOTH},
    {54, "six", CALC_BOTH}, {55, "sept", CALC_BOTH},
    {56, "huit", CALC_BOTH}, {57, "neuf", CALC_BOTH},
    {85, "virgule", CALC_BOTH | CALC_DEC}, {80, "changer de signe", CALC_BOTH},
    {92, "plus", CALC_BOTH}, {93, "moins", CALC_BOTH},
    {91, "multiplier", CALC_BOTH}, {90, "diviser", CALC_BOTH},
    {111, "égal", CALC_BOTH},
    {103, "racine carrée", CALC_STD}, {108, "pourcentage", CALC_STD},
    {106, "inverse du nombre", CALC_BOTH | CALC_DEC},
    {110, "pi", CALC_SCI | CALC_DEC},
    {103, "carré", CALC_SCI | CALC_DEC}, {104, "cube", CALC_SCI | CALC_DEC},
    {95, "puissance", CALC_SCI | CALC_DEC},
    {98, "sinus", CALC_SCI | CALC_DEC}, {99, "cosinus", CALC_SCI | CALC_DEC},
    {100, "tangente", CALC_SCI | CALC_DEC},
    {101, "logarithme naturel", CALC_SCI | CALC_DEC},
    {102, "logarithme décimal", CALC_SCI | CALC_DEC},
    {105, "factorielle", CALC_SCI | CALC_DEC},
    {40, "ouvrir la parenthèse", CALC_SCI}, {41, "fermer la parenthèse", CALC_SCI},
    {65, "A", CALC_SCI | CALC_HEX}, {66, "B", CALC_SCI | CALC_HEX},
    {67, "C", CALC_SCI | CALC_HEX}, {68, "D", CALC_SCI | CALC_HEX},
    {69, "E", CALC_SCI | CALC_HEX}, {70, "F", CALC_SCI | CALC_HEX},
    {124, "base hexadécimale", CALC_SCI}, {123, "base décimale", CALC_SCI},
    {122, "base octale", CALC_SCI}, {121, "base binaire", CALC_SCI},
    {127, "degrés", CALC_SCI}, {128, "radians", CALC_SCI},
    {129, "grades", CALC_SCI},
    {125, "fonctions inverses", CALC_SCI | CALC_DEC},
    {126, "fonctions hyperboliques", CALC_SCI | CALC_DEC}
};
const int calc_nbuttons = sizeof(calc_buttons) / sizeof(calc_buttons[0]);

HWND calc_child(int id)
{
    HWND c;
    for (c = GetWindow(GetActiveWindow(), GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (GetWindowWord(c, GWW_ID) == id && IsWindowVisible(c))
            return c;
    return NULL;
}

BOOL calc_scientific(void) { return calc_child(413) != NULL; }
BOOL calc_checked(int id)
{
    HWND c = calc_child(id);
    return c && SendMessage(c, BM_GETCHECK, 0, 0L) != 0;
}

int calc_base(void)
{
    if (!calc_scientific()) return 10;
    if (calc_checked(124)) return 16;
    if (calc_checked(122)) return 8;
    if (calc_checked(121)) return 2;
    return 10;
}

BOOL calc_available(int i)
{
    const CALCBUTTON *b = &calc_buttons[i];
    int base = calc_base();
    if (!(b->modes & (calc_scientific() ? CALC_SCI : CALC_STD))) return FALSE;
    if ((b->modes & CALC_DEC) && base != 10) return FALSE;
    if ((b->modes & CALC_HEX) && base != 16) return FALSE;
    /* Inv change aussi le sens des puissances/logarithmes : ne pas proposer
       ces fonctions sous un nom devenu faux. Les trigonométriques sont nommées. */
    if (calc_scientific() && calc_checked(125) && b->id >= 95 && b->id <= 110 &&
        (b->id < 98 || b->id > 100)) return FALSE;
    if (b->id >= 48 && b->id <= 57 && (int) b->id - 48 >= base) return FALSE;
    return TRUE;
}

const char *calc_name(int i)
{
    int id = calc_buttons[i].id;
    if (id >= 98 && id <= 100) {
        static const char *const trig[4][3] = {
            { "sinus", "cosinus", "tangente" },
            { "arc sinus", "arc cosinus", "arc tangente" },
            { "sinus hyperbolique", "cosinus hyperbolique", "tangente hyperbolique" },
            { "arc sinus hyperbolique", "arc cosinus hyperbolique", "arc tangente hyperbolique" }
        };
        return trig[(calc_checked(125) ? 1 : 0) + (calc_checked(126) ? 2 : 0)][id - 98];
    }
    if (id >= 127 && id <= 129 && calc_base() != 10) {
        static const char *const sizes[] = { "double mot 32 bits", "mot 16 bits", "octet 8 bits" };
        return sizes[id - 127];
    }
    return calc_buttons[i].name;
}

/* Même libellé long pour les véritables radios/cases parcourus par Tab. */
void calc_control_name(HWND h, char *out)
{
    int i, id = GetWindowWord(h, GWW_ID);
    if (!is_class(GetParent(h), "SCICALC") || GetParent(h) != GetActiveWindow()) return;
    for (i = 0; i < CALC_BUTTONS; i++)
        if (calc_buttons[i].id == id && calc_available(i)) {
            lstrcpy(out, calc_name(i));
            return;
        }
}

void calc_states(char parts[CALC_PARTS][64])
{
    BOOL sci = calc_scientific();
    HWND m = calc_child(sci ? 400 : 402);
    char text[32];
    int i, id;
    for (i = 0; i < CALC_PARTS; i++) parts[i][0] = 0;
    lstrcpy(parts[0], sci ? "mode scientifique" : "mode standard");
    text[0] = 0;
    if (m) GetWindowText(m, text, sizeof(text));
    lstrcpy(parts[1], _fstrchr(text, 'M') ? "mémoire occupée" : "mémoire vide");
    if (!sci) return;
    lstrcpy(parts[2], calc_base() == 16 ? "base hexadécimale" :
                        calc_base() == 8 ? "base octale" :
                        calc_base() == 2 ? "base binaire" : "base décimale");
    for (id = 127; id <= 129; id++)
        if (calc_checked(id))
            for (i = 0; i < CALC_BUTTONS; i++)
                if (calc_buttons[i].id == id) lstrcpy(parts[3], calc_name(i));
    lstrcpy(parts[4], calc_checked(125) ? "fonctions inverses activées" : "fonctions inverses désactivées");
    lstrcpy(parts[5], calc_checked(126) ? "fonctions hyperboliques activées" : "fonctions hyperboliques désactivées");
}

void calc_summary(char *out)
{
    char parts[CALC_PARTS][64];
    int i;
    calc_states(parts);
    out[0] = 0;
    for (i = 0; i < CALC_PARTS; i++) append(out, parts[i]);
}

BOOL calc_value(char *out)
{
    HWND h = GetActiveWindow(), c;
    int id, n;
    out[0] = 0;
    if (!is_class(h, "SCICALC"))
        return FALSE;
    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        id = GetWindowWord(c, GWW_ID);
        if ((id != 413 && id != 414) || !is_class(c, "STATIC") ||
            !IsWindowVisible(c))
            continue;
        GetWindowText(c, out, TXT);
        while (out[0] == ' ')
            _fmemmove(out, out + 1, lstrlen(out));
        n = lstrlen(out);
        while (n && out[n - 1] == ' ')
            out[--n] = 0;
        return n != 0;
    }
    return FALSE;
}

/* Les signes d'un nombre doivent rester audibles même si la ponctuation est
   désactivée. La virgule finale de l'afficheur signifie un entier. */
void calc_say(const char *value, BOOL cut)
{
    char out[TXT * 2];
    int n = 0;
    const char *w;
    for (; *value && n < sizeof(out) - 16; value++) {
        w = NULL;
        if (*value == '-') w = " moins ";
        else if (*value == '+') w = " plus ";
        else if (*value == ',' || *value == '.') {
            if (!value[1]) continue;
            w = " virgule ";
        }
        if (w) {
            lstrcpy(out + n, w);
            n += lstrlen(w);
        } else out[n++] = *value;
    }
    out[n] = 0;
    say(out, cut);
}

void calc_poll(void)
{
    char value[TXT], parts[CALC_PARTS][64], out[TXT * 2];
    HWND act = GetActiveWindow();
    BOOL first = calc_owner != act, changed;
    int i;
    if (!calc_value(value)) {
        calc_owner = NULL;
        calc_last[0] = 0;
        calc_force = calc_queue = FALSE;
        calc_view = 0;
        calc_button = -1;
        return;
    }
    if (first) {
        calc_view = 0;
        calc_button = -1;
    }
    changed = first || calc_force || lstrcmp(value, calc_last);
    if (changed) {
        calc_view = 0;
        calc_say(value, !first && !calc_queue);
        lstrcpyn(calc_last, value, sizeof(calc_last));
    }
    calc_states(parts);
    out[0] = 0;
    for (i = 0; i < CALC_PARTS; i++) {
        if (first || lstrcmp(parts[i], calc_previous[i])) {
            /* Les deux options désactivées sont dans le bilan, pas dans l'annonce
               initiale du mode scientifique. Leur désactivation reste annoncée. */
            if (i < 4 || (!first && calc_previous[i][0]) || calc_checked(i == 4 ? 125 : 126))
                append(out, parts[i]);
            lstrcpyn(calc_previous[i], parts[i], sizeof(calc_previous[i]));
        }
    }
    if (out[0]) say(out, !changed && !calc_queue);
    calc_owner = act;
    calc_force = calc_queue = FALSE;
}

/* Appelé seulement pour une commande Inser, jamais depuis le crochet de l'appli.
   SendMessage laisse CALC.EXE faire son calcul avant la prochaine scrutation. */
BOOL calc_command(WPARAM vk)
{
    char out[TXT * 2], value[TXT];
    int i, direction;
    if (in_menu || !calc_value(value)) return FALSE;
    if (calc_view == 1 && (calc_button < 0 || !calc_available(calc_button))) {
        calc_view = 0;
        calc_button = -1;
    }
    if (vk == VK_NEXT) {
        calc_view = 2;
        calc_summary(out);
        say(out, TRUE);
        return TRUE;
    }
    if (vk == VK_UP) {
        calc_view = 0;
        calc_say(value, TRUE);
        return TRUE;
    }
    if (vk == VK_RETURN) {
        if (calc_view != 1 || calc_button < 0 || !calc_available(calc_button)) {
            say("choisir un bouton avec Inser gauche ou droite", TRUE);
            return TRUE;
        }
        say(calc_name(calc_button), TRUE);
        SendMessage(GetActiveWindow(), WM_COMMAND, calc_buttons[calc_button].id, 0L);
        calc_view = 0;
        calc_force = calc_queue = TRUE;
        return TRUE;
    }
    if (vk != VK_LEFT && vk != VK_RIGHT && vk != VK_HOME && vk != VK_END &&
        !(vk == VK_TAB && calc_view == 1)) return FALSE;
    direction = (vk == VK_LEFT || vk == VK_END) ? -1 : 1;
    if (vk == VK_HOME || vk == VK_END) calc_button = -1;
    if (vk != VK_TAB) {
        i = calc_button;
        do {
            if (i < 0) i = direction > 0 ? 0 : CALC_BUTTONS - 1;
            else i = (i + direction + CALC_BUTTONS) % CALC_BUTTONS;
        } while (!calc_available(i));
        calc_button = i;
    }
    calc_view = 1;
    lstrcpy(out, calc_name(calc_button));
    append(out, "bouton");
    say(out, TRUE);
    return TRUE;
}
