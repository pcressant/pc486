/* EPAULARD.EXE — lecture propre à certains programmes de Windows 3.1 dont la
   fenêtre est dessinée : Table de caractères, Fiches, Horloge, Aide, Gestionnaire de
   liaisons, Date/Heure, Enregistreur de sons et Diffuseur de médias. Voir
   docs/programmes-windows.md. */
#include "ep.h"

/* Boutons dessinés de l'Enregistreur de sons et du Diffuseur de médias (classe
   SBUTTON) : leur texte est un nom interne anglais précédé de « # ». */
void media_button_name(char *name)
{
    static const char *names[] = {
        "#Rewind", "Retour rapide", "#Forward", "Avance rapide", "#Play", "Lecture",
        "#Stop", "Arrêt", "#Record", "Enregistrement", "#Pause", "Pause",
        "#Eject", "Éjecter", NULL
    };
    int i;
    if (name[0] != '#')
        return;
    for (i = 0; names[i]; i += 2)
        if (lstrcmpi(name, names[i]) == 0) {
            lstrcpy(name, names[i + 1]);
            return;
        }
    lstrcpy(name, name + 1);
}

/* Table de caractères : le caractère choisi dans la grille, d'après la barre d'état
   (« Combinaison:! », « Combinaison:Alt+0233 », « Combinaison:Espace »). */
BOOL charmap_value(HWND h, char *out)
{
    char line[TXT * 2], *p, *k;
    int code;
    if (!status_line(top_level(h), line, sizeof(line)) ||
        (p = _fstrstr(line, "Combinaison")) == NULL || (p = _fstrchr(p, ':')) == NULL)
        return FALSE;
    for (p++; *p == ' '; p++)
        ;
    k = p;
    if (_fstrncmp(k, "Alt+", 4) == 0) {
        for (code = 0, k += 4; *k >= '0' && *k <= '9'; k++)
            code = code * 10 + *k - '0';
        char_name((char) code, out);
        append(out, p);
    } else if (lstrlen(k) == 1 || (lstrlen(k) == 5 && _fstrncmp(k, "Maj+", 4) == 0)) {
        char_name(k[lstrlen(k) - 1], out);
    } else if (*k)
        lstrcpyn(out, k, TXT);      /* Espace */
    else
        return FALSE;
    return TRUE;
}

/* Fiches (Cardfile), mode Fiche : ligne de titre de la fiche de devant, dessinée
   dans la fenêtre CARD (205) juste au-dessus de sa zone de texte (200). */
HWND card_edit(HWND app)
{
    HWND card = GetDlgItem(app, 205);
    return card ? GetDlgItem(card, 200) : (HWND) 0;
}

BOOL card_title(HWND app, char *out, int n)
{
    HWND card = GetDlgItem(app, 205), edit = card_edit(app);
    RECT r;
    out[0] = 0;
    if (!is_class(app, "CARDFILE") || !card || !edit || !IsWindowVisible(edit))
        return FALSE;
    GetWindowRect(edit, &r);
    if (!EpOsmRect(card, r.left, r.top - 20, r.right, r.top, out, n))
        EpOsmRect(app, r.left, r.top - 20, r.right, r.top, out, n);
    if (!out[0])
        lstrcpyn(out, "sans titre", n);
    return TRUE;
}

/* Heure de l'horloge du PC, pour l'Horloge de Windows (aiguilles dessinées) */
void clock_value(char *out)
{
    int h, m;
#ifdef EP_NATIVE_TEST
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    h = tm->tm_hour;
    m = tm->tm_min;
#else
    struct dostime_t t;
    _dos_gettime(&t);
    h = t.hour;
    m = t.minute;
#endif
    wsprintf(out, "%d heure%s %02d", h, h > 1 ? "s" : "", m);
}

/* Aide de Windows (WinHelp) : le focus reste sur MS_WINDOC ; le texte est dessiné dans
   la rubrique MS_WINTOPIC, qui défile, sous la zone fixe MS_WINNSR (titre de la
   rubrique dans certains fichiers d'aide). Pas de curseur : Tab choisit un lien en
   l'inversant. */
BOOL is_help(HWND h)
{
    return is_class(h, "MS_WINDOC") || is_class(h, "MS_WINTOPIC_SECONDARY");
}

HWND help_part(HWND doc, const char *cls)
{
    HWND c;
    for (c = GetWindow(doc, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (is_class(c, cls) && IsWindowVisible(c))
            return c;
    return NULL;
}

/* Page de Netscape (EpPageView, page.c) : texte dessiné qui défile, lu comme une
   rubrique d'Aide (Bas, Haut, Pg. suiv., dire tout). */
BOOL page_view(HWND h)
{
    return EpPageView(h);
}

/* Zone de texte d'une rubrique d'Aide ou d'une page, en coordonnées écran ; first :
   avec la zone fixe d'en haut (titre de la rubrique d'Aide) */
BOOL doc_area(HWND doc, BOOL first, RECT *r)
{
    HWND nsr, topic;
    if (is_help(doc)) {
        nsr = help_part(doc, "MS_WINNSR");
        topic = help_part(doc, "MS_WINTOPIC");
        if (!topic)
            return FALSE;
        GetWindowRect(first && nsr ? nsr : topic, r);
        return TRUE;
    }
    if (!page_view(doc))
        return FALSE;
    GetClientRect(doc, r);
    ClientToScreen(doc, (POINT FAR *) &r->left);
    ClientToScreen(doc, (POINT FAR *) &r->right);
    return TRUE;
}

/* Première rangée du texte (zone fixe comprise), ou dernière visible de la rubrique
   ou de la page ; *y : son ordonnée à l'écran */
BOOL help_row(HWND doc, BOOL last, char *out, int n, int *y)
{
    RECT r;
    POINT pt;
    int col;
    out[0] = 0;
    if (!doc_area(doc, !last, &r))
        return FALSE;
    pt.x = r.left + 2;
    pt.y = last ? r.bottom : r.top - 1;
    if (!EpOsmReviewLine(doc, &pt, last ? -1 : 1, out, n, &col))
        return FALSE;
    if (y)
        *y = pt.y;
    return TRUE;
}

/* Gestionnaire de liaisons (Packager) : le volet actif a son sous-titre
   (« --Contenu-- ») en couleurs de sélection ; son contenu est sous lui. */
BOOL packager_value(HWND app, char *out)
{
    HWND c, p;
    char t[TXT], v[TXT];
    RECT r, pr;
    int i;
    for (c = GetWindow(app, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!is_class(c, "SUBTITLECLASS") || !EpOsmHilite(c, t, sizeof(t)))
            continue;
        GetWindowText(c, t, sizeof(t));
        for (i = 0; t[i] == '-'; i++)
            ;
        lstrcpy(out, t + i);
        for (i = lstrlen(out); i > 0 && out[i - 1] == '-'; i--)
            out[i - 1] = 0;
        append(out, "volet");
        GetWindowRect(c, &r);
        for (p = GetWindow(app, GW_CHILD); p; p = GetWindow(p, GW_HWNDNEXT)) {
            GetWindowRect(p, &pr);
            if (is_class(p, "PANECLASS") && pr.left <= r.left + 4 && pr.right >= r.right - 4) {
                append(out, EpOsmWindow(p, v, sizeof(v)) ? v : "vide");
                break;
            }
        }
        return TRUE;
    }
    return FALSE;
}

/* Panneau de configuration, Date/Heure : champs 701 à 703 (heure) et 704 à 706 (date,
   dans l'ordre du format court de WIN.INI, de gauche à droite) */
BOOL datetime_name(HWND h, char *out)
{
    static const char *hms[] = { "heures", "minutes", "secondes" };
    int id = GetWindowWord(h, GWW_ID), rank = 0, i;
    char fmt[24], *f;
    RECT r, o;
    HWND d = GetParent(h);
    if (id < 701 || id > 706 || !GetDlgItem(d, 711) || !is_class(GetDlgItem(d, 711), "CPARROW"))
        return FALSE;
    if (id <= 703) {
        lstrcpy(out, hms[id - 701]);
        return TRUE;
    }
    GetWindowRect(h, &r);
    for (i = 704; i <= 706; i++)
        if (i != id && GetDlgItem(d, i)) {
            GetWindowRect(GetDlgItem(d, i), &o);
            if (o.left < r.left)
                rank++;
        }
    GetProfileString("intl", "sShortDate", "dd/MM/yyyy", fmt, sizeof(fmt));
    for (f = fmt; *f; f++) {
        if (*f != 'd' && *f != 'M' && *f != 'y')
            continue;
        if (rank-- == 0) {
            lstrcpy(out, *f == 'd' ? "jour" : *f == 'M' ? "mois" : "année");
            return TRUE;
        }
        while (f[1] == *f)
            f++;
    }
    return FALSE;
}
