/* EPAULARD.EXE — boîtes de dialogue dessinées (SDM de Word, Excel, Access). */
#include "ep.h"

/* ------------------------------------------- rectangle de focus (boîtes de Word) */

/* Les boîtes de dialogue de Word dessinent leurs boutons, listes et cases sans
   fenêtres : seul un rectangle pointillé montre l'élément qui a le focus. On lit le
   texte qu'il entoure ; une barre de défilement à sa droite signale une liste,
   dont le libellé est dessiné au-dessus. */
HWND frect_hwnd;
RECT frect_said;
BOOL tab_waiting;        /* Ctrl+Tab : la page sera dite avant l'élément */
DWORD frect_tick;
int frect_check = -1;    /* case ou bouton radio annoncé : sdm_check() ; -1 aucun */
HWND said_hwnd;          /* fenêtre de frect_said, tant qu'elle garde le focus */

/* Case à cocher ou bouton radio de Word à gauche du rectangle de focus. Image 14×13
   copiée de la bande des cases de Windows : colonne 0 vide, 14 marquée (28 et 42 :
   enfoncée) ; rangée 0 case, 13 bouton radio, 26 case à trois états (marquée :
   indéterminée). Renvoie rangée * 2 + marquée, ou -1. */
int sdm_check(HWND h, const RECT *r)
{
    int w, ht, xs, ys;
    if (!EpOsmGlyph(h, r->left - 30, r->top - 2, r->left, r->bottom + 2, &w, &ht, &xs, &ys) ||
        w != 14 || ht != 13 || ys / 13 > 2)
        return -1;
    return ys / 13 * 2 + (xs / 14) % 2;
}

/* Titre du groupe d'une case ou d'un bouton radio de Word : le texte le plus proche
   au-dessus, commencé un peu à gauche de la case (cadre « Insérer », « Attributs »).
   S'il vient de plus loin à gauche (case de la colonne de gauche, titre centré) ou
   finit par « : » (libellé d'un autre contrôle), la case n'ouvre pas de groupe. */
char sdm_group[TXT];     /* dernier groupe annoncé */
HWND sdm_group_hwnd;     /* ... dans cette boîte */

void group_title(HWND h, const RECT *r, char *out, int n)
{
    int t;
    out[0] = 0;
    for (t = r->top - 6; t >= r->top - 80; t -= 4)
        if (EpOsmRect(h, r->left - 40, t, r->left - 8, t + 4, out, n)) {
            if (ends_colon(out) || EpOsmCharAt(h, r->left - 44, t + 2))
                out[0] = 0;
            clean_label(out, NULL);
            return;
        }
}

const char *check_state(int c)
{
    if (CHK_ROW(c) == 1)
        return CHK_ON(c) ? "sélectionné" : "non sélectionné";
    if (CHK_ON(c))
        return CHK_ROW(c) == 2 ? "indéterminé" : "coché";
    return "non coché";
}

/* Flèche de liste déroulante (7×9) à droite du rectangle */
BOOL sdm_combo(HWND h, const RECT *r)
{
    int w, ht, xs, ys;
    return EpOsmGlyph(h, r->right, r->top - 2, r->right + 40, r->bottom + 2,
                      &w, &ht, &xs, &ys) && w <= 9 && ht <= 10;
}

/* Rectangle de focus qui n'occupe qu'une rangée d'une fenêtre bien plus haute, hors
   des boîtes de Word : élément d'une liste dessinée (Microsoft Mail) */
BOOL list_row(HWND h, const RECT *r)
{
    RECT w;
    if (is_sdm(h))
        return FALSE;
    GetWindowRect(h, &w);
    return r->bottom - r->top < 30 && w.bottom - w.top > 3 * (r->bottom - r->top) &&
           r->right - r->left > (w.right - w.left) / 2;
}

void on_focusrect(void)
{
    HWND h = frect_hwnd, c;
    RECT r, sb;
    char text[TXT], label[TXT], out[TXT * 2];
    const char *role = NULL;
    int kind;

    /* Contrôle standard (liste du Gestionnaire de fichiers...) : check_value() dit déjà
       l'élément ; son rectangle de focus le ferait redire avec « bouton ». */
    if (!h || !IsWindow(h) || in_menu || sa_active() || cfg_on || tab_waiting ||
        GetFocus() != h ||
        is_standard(h) || custom_value(h, text, &role) ||
        top_level(h) != GetActiveWindow() || (dlg_pending && h == dlg_hwnd))
        return;
    kind = EpOsmFocusRect(h, &r);
    if (!kind)
        return;
    if (EqualRect(&r, &frect_said) && (h == said_hwnd || GetTickCount() - frect_tick < 1500))
        return;     /* simple réaffichage, ou effacement en XOR : Tab vers un contrôle
                       sans rectangle (Orientation d'Excel) retrace l'ancien */
    EpOsmRect(h, r.left - 1, r.top - 1, r.right + 1, r.bottom + 1, text, sizeof(text));
    label[0] = 0;
    if (kind == 2) {
        /* onglet : Word dessine la rangée d'onglets dans un bitmap */
        clean_label(text, NULL);
        out[0] = 0;
        append(out, text);
        append(out, "onglet");
        frect_check = -1;
        sdm_hwnd = h;
        lstrcpyn(sdm_text, out, sizeof(sdm_text));
        frect_said = r;
        said_hwnd = h;
        frect_tick = GetTickCount();
        lstrcpyn(hilite_said, text, sizeof(hilite_said));
        hilite_tick = frect_tick;
        say(out, GetTickCount() - focus_tick > 400);
        return;
    }
    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!is_class(c, "SCROLLBAR") || !IsWindowVisible(c))
            continue;
        GetWindowRect(c, &sb);
        if (sb.left >= r.right - 4 && sb.left <= r.right + 30 &&
            (r.top + r.bottom) / 2 >= sb.top && (r.top + r.bottom) / 2 < sb.bottom) {
            RECT list;
            role = "liste";
            SetRect(&list, r.left, sb.top, sb.left, sb.bottom);
            label_drawn(h, &list, label, sizeof(label), TRUE);
            break;
        }
    }
    if (!role)
        label_drawn(h, &r, label, sizeof(label), FALSE);
    frect_check = role ? -1 : sdm_check(h, &r);
    if (frect_check < 0 || h != sdm_group_hwnd)
        sdm_group[0] = 0;               /* hors d'un groupe, ou autre boîte */
    sdm_group_hwnd = h;
    out[0] = 0;
    clean_label(text, NULL);
    if (role) {
        /* libellé de la liste seulement en y entrant, pas à chaque élément */
        if (!(frect_said.left == r.left && frect_said.right == r.right &&
              GetTickCount() - frect_tick < 5000)) {
            append(out, label);
            append(out, role);
        }
        append(out, text[0] ? text : "vide");
    } else if (frect_check >= 0) {
        char group[TXT];
        group_title(h, &r, group, sizeof(group));
        if (group[0] && lstrcmp(group, sdm_group) != 0) {
            append(out, "groupe");      /* entrée dans un autre groupe */
            lstrcat(out, " ");
            lstrcat(out, group);
            lstrcpyn(sdm_group, group, sizeof(sdm_group));
        }   /* (sans titre au-dessus : colonne de droite du même groupe) */
        append(out, label);
        append(out, text);
        append(out, CHK_ROW(frect_check) == 1 ? "bouton radio" : "case à cocher");
        append(out, check_state(frect_check));
    } else if (sdm_combo(h, &r)) {
        append(out, label);
        append(out, "liste déroulante");
        append(out, text[0] ? text : "vide");
    } else if (list_row(h, &r)) {
        /* rangée d'une liste dessinée (dossiers et messages de Mail) : l'élément seul */
        append(out, text[0] ? text : "vide");
    } else {
        if (!text[0])
            append(out, label);         /* un bouton porte son nom : pas le voisin */
        append(out, text);
        append(out, "bouton");
    }
    /* braille : l'élément complet, avec son libellé même dans une liste */
    sdm_hwnd = h;
    if (role) {
        sdm_text[0] = 0;
        append(sdm_text, label);
        append(sdm_text, role);
        append(sdm_text, text[0] ? text : "vide");
    } else
        lstrcpyn(sdm_text, out, sizeof(sdm_text));
    frect_said = r;
    said_hwnd = h;
    frect_tick = GetTickCount();
    lstrcpyn(hilite_said, text, sizeof(hilite_said));
    hilite_tick = frect_tick;
    /* juste après le titre d'une boîte qui s'ouvre : à la suite */
    say(out, GetTickCount() - focus_tick > 400);
}

/* Page affichée d'une boîte de Word à onglets : l'onglet en gras le plus haut, dans
   les 80 premiers points (jusqu'à trois rangées d'onglets, boîte Options) */
int tab_page(HWND act, char *name, int n)
{
    POINT o;
    RECT rc;
    o.x = o.y = 0;
    ClientToScreen(act, &o);
    GetWindowRect(act, &rc);
    return EpOsmBoldIn(act, rc.left, o.y, rc.right, o.y + 80, name, n);  /* 3 rangées */
}

/* Ctrl+Tab dans une boîte de Word : annoncer la nouvelle page dès qu'elle est
   dessinée (l'onglet en gras change), ou au bout de 1,5 s */
char tab_before[TXT];
DWORD tab_tick;

void on_tabpage(void)
{
    HWND act = GetActiveWindow(), foc = GetFocus();
    RECT fr;
    char name[TXT], out[TXT * 2], d[TXT * 2];

    if (!is_sdm(act) || in_menu || cfg_on || !tab_page(act, name, sizeof(name))) {
        KillTimer(hwnd_main, TIMER_TAB);
        tab_waiting = FALSE;
        return;
    }
    if (lstrcmp(name, tab_before) == 0 && GetTickCount() - tab_tick < 1500)
        return;                         /* pas encore redessinée */
    KillTimer(hwnd_main, TIMER_TAB);
    tab_waiting = FALSE;
    if (is_sdm(foc) && EpOsmFocusRect(foc, &fr) == 2)
        return;                         /* l'onglet a le focus : déjà annoncé */
    clean_label(name, NULL);
    out[0] = 0;
    append(out, name);
    append(out, "onglet");
    say(out, TRUE);
    focus_tick = GetTickCount();        /* l'élément qui a le focus suit, sans couper */
    if (is_sdm(foc)) {                  /* la boîte, ou sa page (Options) */
        frect_hwnd = foc;
        SetRectEmpty(&frect_said);
        on_focusrect();
    } else if (foc && top_level(foc) == act) {
        describe(foc, d);
        if (d[0])
            say(d, FALSE);
    }
}

/* Colonne de libellés d'un formulaire (« Nom: Boîte aux lettres: Mot de passe: ») :
   chacun sera dit avec son champ */
BOOL labels_only(const char *s)
{
    int n = 0;
    for (; *s; s++)
        if (*s == ':')
            n++;
    return n >= 3;
}

void on_dialog_text(void)
{
    static char big[1024];
    char title[TXT];
    HWND h = dlg_hwnd;

    HWND src = dlg_src && IsWindow(dlg_src) ? dlg_src : h;

    dlg_pending = FALSE;
    dlg_src = NULL;
    if (!h || !IsWindow(h) || GetActiveWindow() != h || in_menu)
        return;
    GetWindowText(h, title, sizeof(title));
    if (EpOsmMainText(src, big, sizeof(big)) && lstrcmp(big, title) != 0 &&
        !(src != h && labels_only(big))) {
        say(big, FALSE);
        focus_tick = GetTickCount();    /* l'élément qui a le focus suit, sans couper */
    }
    if (GetFocus() == h) {
        frect_hwnd = h;
        SetRectEmpty(&frect_said);
        on_focusrect();
    }
}

/* Case cochée ou décochée (Espace) : annoncer le nouvel état */
void on_glyph(void)
{
    HWND h = frect_hwnd;
    RECT r;
    int c;
    if (!h || !IsWindow(h) || GetFocus() != h || frect_check < 0 ||
        !EpOsmFocusRect(h, &r) || !EqualRect(&r, &frect_said))
        return;
    c = sdm_check(h, &r);
    if (c >= 0 && c != frect_check) {
        frect_check = c;
        say(check_state(c), TRUE);
    }
}
