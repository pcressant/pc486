/* EPAULARD.EXE — description des contrôles (nom, rôle, valeur, état). */
#include "ep.h"

/* ------------------------------------------------- description d'un contrôle */

/* Élément i d'une liste dessinée par le programme : le texte affiché à sa place */
BOOL osm_item(HWND h, int i, char *out)
{
    RECT r;
    POINT a, b;
    if (SendMessage(h, LB_GETITEMRECT, i, (LPARAM) (LPRECT) &r) == LB_ERR)
        return FALSE;
    a.x = r.left; a.y = r.top;
    b.x = r.right; b.y = r.bottom;
    ClientToScreen(h, &a);
    ClientToScreen(h, &b);
    return EpOsmRect(h, a.x, a.y, b.x, b.y, out, TXT) > 0;
}

BOOL item_unknown;        /* describe_value() n'a trouvé que « élément n » */

/* Valeur seule (ce qui change pendant qu'on manipule le contrôle) */
void describe_value(HWND h, char *out)
{
    char cls[32], buf[TXT];
    long style = GetWindowLong(h, GWL_STYLE);
    int i;
    long length;

    out[0] = 0;
    class_of(h, cls, sizeof(cls));
    if (lstrcmp(cls, "BUTTON") == 0) {
        int t = (int) (style & 0x0F);
        if (t == BS_CHECKBOX || t == BS_AUTOCHECKBOX || t == BS_3STATE || t == BS_AUTO3STATE) {
            i = (int) SendMessage(h, BM_GETCHECK, 0, 0L);
            lstrcpy(out, i == 1 ? "coché" : i == 2 ? "indéterminé" : "non coché");
        } else if (t == BS_RADIOBUTTON || t == BS_AUTORADIOBUTTON) {
            i = (int) SendMessage(h, BM_GETCHECK, 0, 0L);
            lstrcpy(out, i ? "sélectionné" : "non sélectionné");
        }
    } else if (lstrcmp(cls, "EDIT") == 0) {
        if (style & ES_PASSWORD)
            lstrcpy(out, "mot de passe");
        else if (is_multiline(h))
            edit_line(h, out, TXT);
        else
            edit_text(h, out, TXT);     /* (Bureau : « Délai », texte non rendu) */
        if (!out[0] && !is_multiline(h) && GetUpdateRect(h, NULL, FALSE)) {
            /* Préférences de Netscape : texte rendu seulement en dessin, et boîte pas
               encore peinte quand le focus est annoncé */
            UpdateWindow(h);
            EpOsmWindow(h, out, TXT);
        }
        if (out[0] && !out[1] && sign_name(out[0]))
            lstrcpy(out, sign_name(out[0]));    /* « ; » : séparateur de listes */
        if (!out[0])
            lstrcpy(out, "vide");
    } else if (lstrcmp(cls, "LISTBOX") == 0) {
        if (SendMessage(h, LB_GETCOUNT, 0, 0L) <= 0) {
            lstrcpy(out, "vide");       /* (sinon « élément 1 » : curseur à 0) */
            return;
        }
        i = (int) SendMessage(h, LB_GETCURSEL, 0, 0L);
        if (i < 0)
            i = (int) SendMessage(h, LB_GETCARETINDEX, 0, 0L);
        if (i >= 0 && (!(style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) ||
                       (style & LBS_HASSTRINGS))
            && (length = SendMessage(h, LB_GETTEXTLEN, i, 0L)) >= 0 && length < TXT) {
            SendMessage(h, LB_GETTEXT, i, (LPARAM) (LPSTR) buf);
            lstrcpy(out, buf);
            if ((style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) &&
                SendMessage(h, LB_GETSEL, i, 0L) > 0)
                append_keep(out, "sélectionné", TXT);
        } else if (i < 0)
            lstrcpy(out, "aucun élément");
        else if (!osm_item(h, i, out)) {
            wsprintf(out, "élément %d", i + 1);
            item_unknown = TRUE;        /* pas encore dessiné : relu par TIMER_LB */
        }
    } else if (lstrcmp(cls, "COMBOBOX") == 0) {
        /* liste dessinée par le programme : ce qu'elle affiche (le texte de l'élément
           porte parfois des données : « Standard Microsoft=C0C0C0,FFFFFF... ») */
        if ((style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) &&
            EpOsmWindow(h, out, TXT) > 0)
            return;
        i = (int) SendMessage(h, CB_GETCURSEL, 0, 0L);
        if (i >= 0 && (length = SendMessage(h, CB_GETLBTEXTLEN, i, 0L)) >= 0 && length < TXT)
            SendMessage(h, CB_GETLBTEXT, i, (LPARAM) (LPSTR) out);
        else
            GetWindowText(h, out, TXT);
    } else if (lstrcmp(cls, "SCROLLBAR") == 0) {
        int lo, hi, pos = GetScrollPos(h, SB_CTL);
        GetScrollRange(h, SB_CTL, &lo, &hi);
        if (hi > lo)                    /* graduation propre au programme : en pour cent */
            wsprintf(out, "%d %%", (int) ((long) (pos - lo) * 100 / (hi - lo)));
        else
            wsprintf(out, "%d", pos);
    }
}

/* Texte d'un contrôle enfant de la fenêtre active, d'après son identifiant */
BOOL child_text(HWND parent, int id, char *out, int n)
{
    HWND c = GetDlgItem(parent, id);
    out[0] = 0;
    if (c)
        GetWindowText(c, out, n);
    return out[0] != 0;
}

/* Ligne de la barre d'état dessinée en bas de la fenêtre top (vide sinon) */
BOOL status_line(HWND top, char *out, int n)
{
    RECT rc;
    POINT pt;
    int col;
    out[0] = 0;
    if (!top)
        return FALSE;
    GetWindowRect(top, &rc);
    pt.x = rc.left + 4;
    pt.y = rc.bottom - 6;
    if (!EpOsmReviewLine(top, &pt, 2, out, n, &col) || rc.bottom - pt.y >= 32)
        out[0] = 0;
    return out[0] != 0;
}

/* Nom d'un caractère dit seul : signe nommé, « majuscule » selon le réglage */
void char_name(char c, char *out)
{
    const char *name = sign_name(c);
    if (name)
        lstrcpy(out, name);
    else if (cfg[CFG_CAPS] && IsCharUpper(c))
        wsprintf(out, "majuscule %c", c);
    else {
        out[0] = c;
        out[1] = 0;
    }
}

/* Contrôle dessiné reconnu : sa valeur (rôle dans *role). FALSE : contrôle ordinaire. */
BOOL custom_value(HWND h, char *out, const char **role)
{
    out[0] = 0;
    if (is_help(h)) {
        /* lien choisi par Tab : le texte inversé */
        *role = NULL;
        if (EpOsmSelection(h, out, TXT - 8))
            lstrcat(out, ", lien");
        return TRUE;
    }
    if (is_class(h, "APPCLASS") && GetWindow(h, GW_CHILD) &&
        is_class(GetWindow(h, GW_CHILD), "PANECLASS")) {
        *role = NULL;
        if (!packager_value(h, out))
            out[0] = 0;
        return TRUE;
    }
    if (is_class(h, "CHARGRIDWCLASS")) {
        *role = "grille de caractères";
        if (!charmap_value(h, out))
            lstrcpy(out, "aucun caractère");
        return TRUE;
    }
    return FALSE;
}

/* Unité écrite après une zone de saisie (« Minutes ») : texte statique suivant, court,
   sans raccourci ni deux-points (ce serait le libellé du contrôle suivant) */
void edit_unit(HWND h, char *val)
{
    HWND n = GetWindow(h, GW_HWNDNEXT);
    char unit[24];
    if (is_multiline(h) || !n || !is_class(n, "STATIC") || !has_style(n, WS_VISIBLE) ||
        GetWindowTextLength(n) == 0 || GetWindowTextLength(n) >= (int) sizeof(unit))
        return;
    GetWindowText(n, unit, sizeof(unit));
    if (_fstrchr(unit, '&') || _fstrchr(unit, ':') || no_word(unit) ||
        lstrlen(val) + lstrlen(unit) + 2 >= TXT)
        return;
    lstrcat(val, " ");
    lstrcat(val, unit);
}

/* Cadre de groupe (BUTTON BS_GROUPBOX) qui entoure le contrôle h */
BOOL group_of(HWND h, char *out, int n)
{
    HWND c;
    RECT rc, r;
    out[0] = 0;
    GetWindowRect(h, &rc);
    for (c = GetWindow(GetParent(h), GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!is_class(c, "BUTTON") || (GetWindowLong(c, GWL_STYLE) & 0x0F) != BS_GROUPBOX)
            continue;
        GetWindowRect(c, &r);
        if (rc.left >= r.left && rc.right <= r.right && rc.top >= r.top && rc.bottom <= r.bottom) {
            GetWindowText(c, out, n);
            clean_label(out, NULL);
            return out[0] != 0;
        }
    }
    return FALSE;
}

/* Nom + rôle + valeur */
void describe(HWND h, char *out)
{
    char cls[32], name[TXT], val[TXT];
    const char *role = NULL;
    long style = GetWindowLong(h, GWL_STYLE);

    out[0] = 0;
    class_of(h, cls, sizeof(cls));
    GetWindowText(h, name, sizeof(name));
    clean_label(name, NULL);
    if (lstrcmp(cls, "BUTTON") == 0) calc_control_name(h, name);
    describe_value(h, val);

    if (lstrcmp(cls, "BUTTON") == 0) {
        switch ((int) (style & 0x0F)) {
        case BS_CHECKBOX: case BS_AUTOCHECKBOX: case BS_3STATE: case BS_AUTO3STATE:
            role = "case à cocher"; break;
        case BS_RADIOBUTTON: case BS_AUTORADIOBUTTON:
            role = "bouton radio"; break;
        case BS_GROUPBOX:
            role = "groupe"; break;
        default:
            role = "bouton";
            if (!name[0] || picture_button(h))
                picture_label(h, name, sizeof(name));
        }
    } else if (lstrcmp(cls, "EDIT") == 0) {
        if (page_view(GetParent(h))) {
            RECT rc;                    /* champ d'une page : libellé dessiné à gauche, */
            GetWindowRect(h, &rc);      /* jamais le champ précédent (case, bouton) */
            if (label_nearest_left(GetParent(h), &rc, name, sizeof(name)))
                clean_label(name, NULL);
        } else if ((h != card_edit(top_level(h)) || !card_title(top_level(h), name, sizeof(name))) &&
                   !datetime_name(h, name))
            label_before(h, name, sizeof(name));
        role = is_multiline(h) ? "zone de texte" : "zone d'édition";
        edit_unit(h, val);              /* « 2 Minutes », « 75 Pixels » */
    } else if (lstrcmp(cls, "LISTBOX") == 0) {
        label_before(h, name, sizeof(name));
        role = "liste";
    } else if (lstrcmp(cls, "COMBOBOX") == 0) {
        label_before(h, name, sizeof(name));
        role = "liste déroulante";
    } else if (lstrcmp(cls, "SCROLLBAR") == 0) {
        role = "barre de défilement";
        /* son libellé, sinon son cadre (Souris : « Vitesse du double-clic ») */
        if (!label_zorder(h, name, sizeof(name)) && !group_of(h, name, sizeof(name)))
            label_before(h, name, sizeof(name));
        if (is_class(GetParent(h), "SOUNDREC")) {
            /* Enregistreur de sons : la position du son, plutôt que la graduation */
            lstrcpy(name, "Position");
            role = "curseur";
            child_text(GetParent(h), 201, val, sizeof(val));
        }
    } else if (custom_value(h, val, &role)) {
        ;
    } else if (lstrcmp(cls, "SBUTTON") == 0) {
        media_button_name(name);
        role = "bouton";
    } else if (_fstrstr(cls, "BUTTON") && name[0]) {
        role = "bouton";                /* MSW_WN_GRBUTTON de Microsoft Undelete... */
        if (picture_button(h))
            picture_label(h, name, sizeof(name));       /* CIRRUSSBUTTON d'Access */
    } else if (lstrcmp(cls, "SSCROLLBAR") == 0) {
        role = "curseur de position";
    } else if (lstrcmp(cls, "MDICLIENT") == 0) {
        name[0] = 0;
    } else if (lstrcmp(cls, "WINDOW") == 0 && (is_class(top_level(h), "SDI") ||
                                               is_class(top_level(h), "MS MAIL APPFRAME"))) {
        /* champ des formulaires de Microsoft Mail : libellé dessiné à sa gauche
           (« Mot de passe: ») ; sans libellé, c'est un bouton (rectangle de focus) */
        RECT rc;
        GetWindowRect(h, &rc);
        if (label_left(GetParent(h), &rc, name, sizeof(name))) {
            clean_label(name, NULL);
            role = "zone d'édition";
            if (!EpOsmWindow(h, val, sizeof(val)))
                lstrcpy(val, "vide");
        } else if (rc.bottom - rc.top > 60 && !EpOsmFocusRect(h, &rc)) {
            role = "zone de texte";     /* texte du message, sans libellé */
            if (!EpOsmWindow(h, val, sizeof(val)))
                lstrcpy(val, "vide");
        }
    } else if (lstrcmp(cls, "OPUSFEDT") == 0) {
        /* zone de saisie des boîtes de dialogue de Word : libellé dessiné à côté */
        RECT rc;
        GetWindowRect(h, &rc);
        label_drawn(GetParent(h), &rc, name, sizeof(name), TRUE);
        role = "zone d'édition";
        EpOsmWindow(h, val, sizeof(val));
        if (!val[0])
            lstrcpy(val, "vide");
    }
    /* Rôle, valeur et état d'abord : seul un libellé trop long est raccourci */
    fit_before(name, TXT * 2, role, val, IsWindowEnabled(h) ? NULL : "grisé");
    append(out, name);
    append(out, role);
    append(out, val);
    if (!IsWindowEnabled(h)) append(out, "grisé");
}
