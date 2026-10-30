/* EPAULARD.EXE — lecteur d'écran pour Windows 3.1x (synthèse Apollo sur COM1).
 *
 * Suit la fenêtre active et le contrôle qui a le focus (scrutation toutes les 100 ms),
 * lit les menus et les caractères tapés (signalés par EPAUHOOK.DLL), et parle en
 * envoyant le protocole Apollo sur COM1 : texte terminé par CR, Ctrl-X pour se taire.
 *
 * Commandes (Inser + touche) :
 *   Inser+T      titre de la fenêtre active
 *   Inser+Tab    relire le contrôle qui a le focus
 *   Inser+Haut   relire la ligne courante
 *   Inser+Bas    dire tout : lire jusqu'à la fin du document, le curseur suit la lecture
 *   Inser+B      lire tout le contenu de la fenêtre (boîte de dialogue)
 *   Inser+O      lire tout le texte affiché dans la fenêtre (modèle hors écran)
 *   Inser+F      police et mise en forme sous le curseur (ou le point de revue)
 *   Inser+Maj+Haut  lire la sélection
 *   / et * du pavé  clic gauche, clic droit ; Maj : verrouiller le bouton
 *   Inser+/      amener la souris au point de revue ; Inser+* : revue à la souris
 *   Inser+PgSuiv lire la barre d'état
 *   Inser+E      réglages (débit, hauteur, voix, ponctuation, écho, majuscules),
 *                enregistrés dans EPAULARD.INI à côté du programme
 *   Inser+H      aide sur les commandes
 *   Inser+F4     quitter Épaulard
 *   Ctrl         couper la parole
 */
/* Ce fichier : suivi du focus, scrutation, menus, surbrillance, commandes et fenêtre
   principale. Les autres modules sont décrits dans ep.h. */
#include "ep.h"

int com = -1;
HWND hwnd_main;
char braille_menu[TXT * 2];

/* État de la dernière lecture, pour n'annoncer que les changements */
HWND last_active, last_focus;
int last_sel = -2, last_line = -2, last_check = -2;
DWORD last_esel;
BOOL typed;              /* caractère tapé : le prochain déplacement du curseur n'est pas lu */
BOOL in_menu;
BOOL new_active;         /* une fenêtre vient d'être activée : dire son titre */
DWORD last_page;         /* dialog_page() de last_active */
DWORD focus_tick;        /* heure de la dernière annonce du focus */
char focus_said[TXT * 2];/* texte de cette annonce (pour ne pas la répéter) */
POINT last_caret;        /* curseur texte (écran) dans une fenêtre non standard */
BOOL line_key;           /* navigation verticale, même si l'écran défile */
UINT last_vk;            /* dernière touche transmise au programme */
DWORD line_key_tick;
BOOL word_key;           /* Ctrl+Gauche ou Ctrl+Droite : lire le mot */
DWORD caret_settle;      /* curseur ramené après un dire tout (heure), à attendre */
BOOL caret_quiet;        /* ... sans annoncer sa nouvelle position */

char hilite_said[TXT];   /* dernier élément en surbrillance annoncé */
DWORD hilite_tick;
DWORD popup_tick;        /* annonce d'un menu déroulant : son 1er élément suit sans couper */
char menu_pending[TXT * 2];  /* titre de menu sélectionné, pas encore dit */
BOOL flat_review;
HWND review_window;
POINT review_point;
int review_column;

BOOL sel_key;            /* Maj + déplacement : annoncer la sélection */
DWORD sel_key_tick;
char sel_said[TXT * 2];  /* texte sélectionné (dessiné) à la dernière annonce */
HWND sdm_hwnd;           /* boîte de Word : élément qui a le focus, pour le braille */
char sdm_text[TXT * 2];

/* -------------------------------------------------------------- scrutation */

char custom_said[TXT];   /* valeur du contrôle reconnu annoncée en dernier */

char last_etext[TXT];     /* zone de saisie d'une ligne : texte (flèches de réglage) */

void remember_value(HWND h)
{
    char cls[32];
    const char *role;
    last_etext[0] = 0;
    if (is_class(h, "EDIT") && !is_multiline(h) && !(GetWindowLong(h, GWL_STYLE) & ES_PASSWORD))
        edit_text(h, last_etext, sizeof(last_etext));
    if (!custom_value(h, custom_said, &role))
        custom_said[0] = 0;
    class_of(h, cls, sizeof(cls));
    last_sel = last_line = last_check = -2;
    last_esel = 0;
    if (lstrcmp(cls, "LISTBOX") == 0)
        last_sel = (int) SendMessage(h, LB_GETCURSEL, 0, 0L) * 1000 +
                   (int) SendMessage(h, LB_GETCARETINDEX, 0, 0L);
    else if (lstrcmp(cls, "COMBOBOX") == 0)
        last_sel = (int) SendMessage(h, CB_GETCURSEL, 0, 0L);
    else if (lstrcmp(cls, "BUTTON") == 0)
        last_check = (int) SendMessage(h, BM_GETCHECK, 0, 0L);
    else if (lstrcmp(cls, "EDIT") == 0) {
        last_esel = SendMessage(h, EM_GETSEL, 0, 0L);
        last_line = is_multiline(h) ?
            (int) SendMessage(h, EM_LINEFROMCHAR, (WPARAM) -1, 0L) : 0;
    }
}

/* Texte d'une boîte de dialogue qui vient de s'ouvrir : les textes statiques qui ne
   sont pas le libellé du contrôle suivant (le message d'une boîte « Enregistrer les
   modifications ? », par exemple). */
static void dialog_texts(HWND dlg, char *out, int max, BOOL page);

void dialog_text(HWND dlg, char *out, int max)
{
    dialog_texts(dlg, out, max, FALSE);
}

/* Empreinte des textes statiques visibles d'une boîte : elle change quand un
   assistant (profil de Communicator) passe à la page suivante sans changer de boîte */
DWORD dialog_page(HWND dlg)
{
    HWND c;
    DWORD sig = 0;
    for (c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (is_class(c, "STATIC") && has_style(c, WS_VISIBLE))
            sig = sig * 31 + GetWindowWord(c, GWW_ID) + 1;
    return sig;
}

/* page : nouvelle page d'un assistant, formulaire compris ; ses explications sont
   dites, pas les libellés (terminés par « : »), dits avec leur contrôle */
static void dialog_texts(HWND dlg, char *out, int max, BOOL page)
{
    HWND c, n;
    static char buf[640];               /* paragraphe entier (accueil de l'installateur Lotus) */

    /* Seulement pour les boîtes de message : dans un formulaire (zones d'édition,
       listes), les textes statiques sont des libellés, dits avec leur contrôle. Une
       zone de texte en lecture seule (licence de Netscape) n'est qu'un affichage. Les
       contrôles cachés (pages suivantes d'un assistant : profil de Communicator) ne
       comptent pas. */
    for (c = GetWindow(dlg, GW_CHILD); !page && c; c = GetWindow(c, GW_HWNDNEXT))
        if (has_style(c, WS_VISIBLE) &&
            ((is_class(c, "EDIT") && !has_style(c, ES_READONLY)) ||
             is_class(c, "LISTBOX") || is_class(c, "COMBOBOX") ||
             is_class(c, "SCROLLBAR")))
            return;                     /* (Souris : « Lente », « Rapide » des curseurs) */
    for (c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!is_class(c, "STATIC") || !has_style(c, WS_VISIBLE) ||
            (GetWindowLong(c, GWL_STYLE) & 0x0F) > SS_RIGHT)
            continue;
        n = GetWindow(c, GW_HWNDNEXT);
        if (!page && n && (is_class(n, "EDIT") || is_class(n, "LISTBOX") || is_class(n, "COMBOBOX")))
            continue;                   /* libellé : dit avec son contrôle */
        GetWindowText(c, buf, sizeof(buf));
        if (page && ends_colon(buf))
            continue;
        if (n && picture_button(n) && _fstrchr(buf, '&'))
            continue;                   /* « Table &vierge » d'un bouton à image (Access) */
        clean_label(buf, NULL);
        append_n(out, buf, max);
    }
}

HWND top_level(HWND h)
{
    while (h && (GetWindowLong(h, GWL_STYLE) & WS_CHILD))
        h = GetParent(h);
    return h;
}

/* Boîte de Word qui vient de s'ouvrir : son texte (message, astuce) sera lu quand
   elle aura fini de se dessiner, avant l'élément qui a le focus. */
HWND dlg_hwnd;
HWND dlg_src;            /* fenêtre où ce texte est dessiné, si autre (Mail) */
BOOL dlg_pending;

/* Boîte de Word à onglets : ses contrôles sont dans une page enfant (sa_sdm_…) */
BOOL has_page(HWND h)
{
    HWND c;
    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (is_sdm_page(c))
            return TRUE;
    return FALSE;
}

BOOL has_child_class(HWND h, const char *cls)
{
    HWND c;
    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (is_class(c, cls))
            return TRUE;
    return FALSE;
}

HWND grp_dlg;            /* cadre de groupe annoncé, et sa boîte */
char grp_said[TXT];

HWND status_hwnd;        /* voir status_poll() */
DWORD page_wait;         /* page de Netscape annoncée, pas encore lue */
DWORD page_sig, page_stable;
#define PAGE_STABLE_MS 1000
char status_said[TXT];

/* Fenêtre de document MDI qui contient h (Gestionnaire de fichiers, Sysedit) */
HWND last_mdi;

HWND mdi_child(HWND h)
{
    HWND p;
    for (; h; h = p) {
        p = GetParent(h);
        if (!p || !(GetWindowLong(h, GWL_STYLE) & WS_CHILD))
            return NULL;
        if (is_class(p, "MDICLIENT"))
            return h;
    }
    return NULL;
}

void announce_focus(BOOL cut)
{
    static char out[1000];              /* titre, texte d'une boîte de message, focus */
    char title[TXT];
    HWND act = GetActiveWindow(), foc = GetFocus();

    /* Focus posé dans une fenêtre pas encore active (boîte de dialogue en cours de
       création) : attendre son activation, on la lira alors avec son titre. */
    if ((foc && act && top_level(foc) != act) || (act && !IsWindowVisible(act)))
        return;
    out[0] = 0;
    if (act && act == last_active && !new_active && is_class(act, "#32770") &&
        dialog_page(act) != last_page) {
        dialog_texts(act, out, sizeof(out), TRUE);   /* page suivante d'un assistant */
    } else if (act && (act != last_active || new_active)) {
        new_active = FALSE;
        GetWindowText(act, title, sizeof(title));
        append_n(out, title, sizeof(out));
        if (is_class(act, "#32770") || has_child_class(act, "STATIC"))
            dialog_text(act, out, sizeof(out));      /* boîte de dialogue, même d'une autre classe */
        else if (is_class(act, "SDI") && GetWindow(act, GW_CHILD)) {
            /* boîte de Microsoft Mail : texte dessiné dans sa fenêtre fille */
            dlg_hwnd = act;
            dlg_src = GetWindow(act, GW_CHILD);
            dlg_pending = TRUE;
            SetTimer(hwnd_main, TIMER_DLG, 350, NULL);
        } else if (is_class(act, "MS_WINNOTE")) {
            /* définition affichée par l'Aide : lue quand elle est dessinée */
            dlg_hwnd = act;
            dlg_pending = TRUE;
            SetTimer(hwnd_main, TIMER_DLG, 350, NULL);
        } else if (is_class(act, "CLOCK")) {
            char t[32];
            clock_value(t);
            append_n(out, t, sizeof(out));
        }
        else if (is_sdm(act) && !has_child_class(act, "OPUSFEDT") && !has_page(act) &&
                 !has_child_class(act, "EDTBX") && !has_child_class(act, "SCROLLBAR")) {
            /* boîte de Word sans zone de saisie (message, astuce) ; Excel : EDTBX,
               listes (barres de défilement) signalent un formulaire */
            dlg_hwnd = act;
            dlg_pending = TRUE;
            SetTimer(hwnd_main, TIMER_DLG, 350, NULL);
        }
    }
    if (foc && foc != act) {
        char d[TXT * 2];
        HWND m = mdi_child(foc);
        /* Autre fenêtre de document (Ctrl+F6) : son titre, puis le focus */
        if (m && m != last_mdi && m != foc && !IsZoomed(m)) {
            GetWindowText(m, title, sizeof(title));
            append_n(out, title, sizeof(out));
        }
        last_mdi = m;
        item_unknown = FALSE;
        if (is_grid_edit(foc)) {
            d[0] = 0;                   /* cellule dite ensuite par grid_poll */
            grid_reset();
        } else if (nav_focus(foc, d)) {
            /* page Web en mode navigation : le curseur virtuel parle (navweb.c) */
        } else if (is_sheet(foc)) {
            d[0] = 0;                   /* cellule dite ensuite par sheet_poll */
            sheet_reset();
        } else
            describe(foc, d);
        {
            /* entrée dans un autre cadre de groupe : son nom d'abord (les quatre
               boutons « Modifier » d'International) */
            char g[TXT];
            if (act != grp_dlg)
                grp_said[0] = 0;
            grp_dlg = act;
            if (!is_standard(foc) || !group_of(foc, g, sizeof(g)))
                g[0] = 0;
            if (lstrcmp(g, grp_said) != 0) {
                lstrcpy(grp_said, g);
                if (g[0] && _fstrncmp(d, g, lstrlen(g)) != 0)
                    append_n(out, g, sizeof(out));
            }
        }
        if (item_unknown)               /* fenêtre qui vient de s'ouvrir */
            SetTimer(hwnd_main, TIMER_LB, 400, NULL);
        if (foc == card_edit(act)) {
            /* fiche ajoutée : son titre peut être dessiné après cette annonce ;
               status_poll() dira le bon s'il diffère */
            card_title(act, status_said, sizeof(status_said));
            status_hwnd = act;
        }
        append_n(out, d, sizeof(out));
    } else if (!out[0] && act && !is_sdm(act)) {
        /* (boîte de Word : l'élément qui a le focus est annoncé par son rectangle) */
        GetWindowText(act, title, sizeof(title));
        append_n(out, title, sizeof(out));
    }
    if (foc != said_hwnd)
        said_hwnd = NULL;               /* focus sorti de la boîte : tout sera redit */
    last_active = act;
    last_page = act ? dialog_page(act) : 0;
    last_focus = foc;
    typed_n = 0;                        /* mot tapé ailleurs : oublié */
    line_key = FALSE;
    sel_key = FALSE;
    sel_said[0] = 0;
    if (foc)
        caret_screen(foc, &last_caret);
    if (foc)
        remember_value(foc);
    if (!out[0])
        return;
    if (GetTickCount() - focus_tick < 700 && lstrlen(out) <= lstrlen(focus_said) &&
        lstrcmp(focus_said + lstrlen(focus_said) - lstrlen(out), out) == 0)
        return;     /* déjà dit à l'instant, éventuellement avec le titre (scrutation + crochet) */
    {
        /* Focus rendu par le programme, sans touche, à un élément annoncé depuis la
           dernière touche (barre de progression d'ACME : la boîte puis « Annuler,
           bouton » à chaque fichier copié) : ne pas le redire. */
        static char recent[4][TXT];
        static int next;
        int i;
        if ((long) (key_tick - focus_tick) >= 0 || GetTickCount() - focus_tick > 20000)
            for (i = 0; i < 4; i++)
                recent[i][0] = 0;       /* touche frappée : tout peut être redit */
        for (i = 0; i < 4; i++)
            if (lstrcmp(recent[i], out) == 0) {
                focus_tick = GetTickCount();
                return;
            }
        lstrcpyn(recent[next], out, TXT);
        next = (next + 1) % 4;
    }
    lstrcpyn(focus_said, out, sizeof(focus_said));
    focus_tick = GetTickCount();
    say(out, cut);
}

/* Changement de valeur dans le contrôle qui a le focus (liste, case, curseur...) */
void check_value(void)
{
    HWND h = last_focus;
    char cls[32], out[TXT];
    int sel = last_sel, line = last_line, check = last_check;
    DWORD esel = last_esel;
    static char etext[TXT];

    if (!h || !IsWindow(h) || is_sdm(h))
        return;             /* (boîte de Word : suivie par son rectangle de focus) */
    {
        const char *role;
        if ((is_help(h) || page_view(h)) && line_key) {
            /* défilement : la ligne apparue (en bas avec Bas, sinon en haut) */
            if (GetTickCount() - line_key_tick < 300)
                return;
            line_key = FALSE;
            help_row(h, last_vk == VK_DOWN, out, TXT, NULL);
            say(out[0] ? out : "vide", TRUE);
            custom_value(h, custom_said, &role);
            return;
        }
        if (custom_value(h, out, &role)) {
            if (lstrcmp(out, custom_said) != 0) {
                lstrcpy(custom_said, out);
                if (out[0])
                    say(out, TRUE);
            }
            return;
        }
    }
    if (!is_standard(h)) {
        /* fenêtre dessinée par le programme : suivre le curseur texte dans le modèle
           hors écran (ligne si on change de ligne, sinon caractère) */
        POINT pt;
        if (caret_settle) {
            /* curseur ramené par la fin d'un dire tout : attendre qu'il soit posé */
            if (GetTickCount() - caret_settle < 500)
                return;
            caret_settle = 0;
            if (caret_quiet) {
                caret_quiet = FALSE;
                caret_screen(h, &last_caret);
                line_key = word_key = typed = FALSE;
                return;
            }
        }
        if (!EpOsmCaret(h, &pt) && EpOsmHilite(h, out, TXT)) {
            /* liste dessinée sans curseur texte (icônes du Gestionnaire de
               programmes) : on_hilite annonce l'élément ; la ligne sous le curseur
               système, absent, serait lue « vide ». */
            line_key = word_key = typed = FALSE;
            return;
        }
        if (sel_key) {
            /* Maj + déplacement : laisser le programme inverser la sélection, puis
               dire ce qui change. Sans sélection visible, lecture ordinaire. */
            static char cur[TXT * 2];
            if (GetTickCount() - sel_key_tick < 300)
                return;
            sel_key = FALSE;
            EpOsmSelection(h, cur, sizeof(cur));
            if (cur[0] || sel_said[0]) {
                drawn_selection(sel_said, cur);
                lstrcpyn(sel_said, cur, sizeof(sel_said));
                caret_screen(h, &last_caret);
                line_key = word_key = typed = FALSE;
                return;
            }
        }
        caret_screen(h, &pt);
        if (pt.x == last_caret.x && pt.y == last_caret.y) {
            /* Navigation verticale : laisser au programme le temps de redessiner son
               curseur (Word) ; s'il reste en place, c'est le texte qui a défilé. */
            if (!line_key || GetTickCount() - line_key_tick < 400)
                return;
        }
        if (typed)
            typed = FALSE;
        else if (pt.y != last_caret.y || line_key) {
            EpOsmLine(h, pt.y + 2, out, TXT);
            say(out[0] ? out : "vide", TRUE);
        } else if (word_key) {
            char row[TXT * 2];
            POINT q;
            int col;
            q.x = pt.x + 1;
            q.y = pt.y + 2;
            if (EpOsmReviewLine(h, &q, 0, row, sizeof(row), &col))
                say_word(row, col);
        } else {
            int c = EpOsmCharAt(h, pt.x + 1, pt.y + 2);
            if (c == 0)
                say("fin de ligne", TRUE);
            else
                say_char((char) c, TRUE);
        }
        last_caret = pt;
        line_key = FALSE;
        word_key = FALSE;
        return;
    }
    lstrcpy(etext, last_etext);
    remember_value(h);
    class_of(h, cls, sizeof(cls));

    if (lstrcmp(cls, "EDIT") == 0) {
        if (!is_multiline(h) && lstrcmp(etext, last_etext) != 0 &&
            (last_vk == VK_UP || last_vk == VK_DOWN || last_vk == VK_PRIOR || last_vk == VK_NEXT) &&
            !(GetWindowLong(h, GWL_STYLE) & ES_PASSWORD)) {
            /* valeur changée sans frappe : flèches de réglage (Bureau, Date/Heure) */
            describe_value(h, out);
            edit_unit(h, out);
            say(out, TRUE);
            return;
        }
        if (typed) {                    /* le caractère tapé a déjà été dit */
            typed = FALSE;
            return;
        }
        if (GetWindowLong(h, GWL_STYLE) & ES_PASSWORD)
            return;
        if (last_esel != esel && (sel_key || LOWORD(last_esel) != HIWORD(last_esel))) {
            BOOL shift = sel_key;
            sel_key = word_key = FALSE;
            if (edit_selection(h, esel, last_esel, shift))
                return;
        }
        if (last_line != line) {
            edit_line(h, out, TXT);
            say(out[0] ? out : "vide", TRUE);
        } else if (last_esel != esel) {
            /* déplacement dans la ligne : caractère sous le curseur */
            WORD pos = LOWORD(last_esel);
            int start = 0, col;
            if (is_multiline(h)) {
                start = (int) SendMessage(h, EM_LINEINDEX, (WPARAM) -1, 0L);
                edit_line(h, out, TXT);
            } else if (edit_text(h, out, TXT)) {
                /* texte seulement dessiné : le caractère sous le curseur, à l'écran */
                POINT pt;
                int c;
                word_key = FALSE;
                caret_screen(h, &pt);
                c = EpOsmCharAt(h, pt.x + 1, pt.y + 2);
                if (c)
                    say_char((char) c, TRUE);
                else
                    say("fin de ligne", TRUE);
                return;
            }
            col = pos - start;
            if (word_key && col >= 0) {
                word_key = FALSE;
                say_word(out, col);
            } else if (col >= 0 && col < lstrlen(out))
                say_char(out[col], TRUE);
            else
                say("fin de ligne", TRUE);
        }
    } else if ((last_sel != sel || last_check != check) && !nav_owns(h)) {
        describe_value(h, out);
        if (out[0])
            say(out, TRUE);
    }
}

/* Page de Netscape de la fenêtre active : celle qui a le focus ou contient le
   champ qui l'a, sinon sa vue (focus perdu après un lien suivi) */
HWND page_of(HWND act)
{
    HWND f = GetFocus(), c;
    if (page_view(f))
        return f;
    if (f && page_view(GetParent(f)))
        return GetParent(f);
    if (!act)
        return NULL;
    for (c = GetWindow(act, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (page_view(c) && IsWindowVisible(c))
            return c;
    return NULL;
}

/* État affiché par un programme hors du contrôle qui a le focus, annoncé quand il
   change : « Lecture », « Arrêté » de l'Enregistreur de sons ; titre de la fiche. */

void status_poll(void)
{
    HWND act = GetActiveWindow(), page;
    char buf[TXT], line[TXT];

    if (is_class(act, "SOUNDREC") && child_text(act, 200, buf, sizeof(buf))) {
        if (act == status_hwnd && lstrcmp(buf, status_said) != 0)
            say(buf, FALSE);
    } else if (is_help(act) && GetFocus() == act && help_row(act, FALSE, buf, sizeof(buf), NULL)) {
        /* autre rubrique (lien suivi, Index, Précédent) : sa première ligne. Pas
           pour un défilement (touche de déplacement) ni pendant le dire tout. */
        if (lstrcmp(buf, status_said) != 0 && !sa_active() &&
            (act != status_hwnd || !(last_vk >= VK_PRIOR && last_vk <= VK_DOWN) ||
             GetTickCount() - line_key_tick > 1500))
            say(buf, act == status_hwnd);
    } else if ((page = page_of(act)) != NULL && GetWindowText(act, buf, sizeof(buf))) {
        /* autre page (Netscape) : son titre, puis lecture dès qu'elle est dessinée */
        if (act == status_hwnd && lstrcmp(buf, status_said) != 0 && !sa_active()) {
            say(buf, TRUE);
            page_wait = GetTickCount();
            page_sig = 0;
            /* lien suivi depuis un champ de l'ancienne page : focus perdu (détruit
               avec elle) ; le rendre à la page pour la lire et la parcourir */
            if (!GetFocus() || GetFocus() == act || !IsChild(act, GetFocus()))
                SetFocus(page);
        } else if (page_wait && key_tick - page_wait < 0x80000000L) {
            page_wait = 0;              /* touche depuis : pas de lecture d'office */
        } else if (page_wait && GetFocus() != page) {
            page_wait = 0;              /* focus ailleurs (barre d'adresse) : titre seul */
        } else if (page_wait) {
            DWORD sig = EpOsmSignature(page);
            if (sig != page_sig) {
                page_sig = sig;         /* page encore en cours de dessin */
                page_stable = GetTickCount();
            } else if (GetTickCount() - page_stable > PAGE_STABLE_MS) {
                page_wait = 0;
                if (!nav_autoread(page))  /* par le mode navigation si sa structure vient */
                    sa_start();
            }
        }
    } else if (GetFocus() && GetFocus() == card_edit(act) && card_title(act, buf, sizeof(buf))) {
        /* autre fiche (Page suivante, Rechercher) : son titre et sa ligne courante */
        if (act == status_hwnd && lstrcmp(buf, status_said) != 0) {
            lstrcpy(line, buf);
            edit_line(GetFocus(), buf, sizeof(buf));
            append(line, buf[0] ? buf : "vide");
            say(line, TRUE);
            remember_value(GetFocus());
            card_title(act, buf, sizeof(buf));
        }
    } else {
        status_hwnd = NULL;
        return;
    }
    status_hwnd = act;
    lstrcpy(status_said, buf);
}

/* Zones dont le texte arrive de lui-même et doit être lu dès qu'il est affiché : données
   reçues par Terminal (DC_TERM), écrans du Didacticiel (TUTCLIENT). On compare les
   rangées visibles (empreintes) une fois l'affichage stable ; les nouvelles sont dites. */
HWND auto_hwnd;
DWORD auto_sig, auto_tick;
DWORD auto_rows[AUTO_ROWS];
int auto_n;
DWORD key_tick;          /* dernière touche transmise au programme */

HWND auto_scope(HWND act)
{
    HWND c;
    for (c = GetWindow(act, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if ((is_class(c, "DC_TERM") || is_class(c, "TUTCLIENT")) && IsWindowVisible(c))
            return c;
    return NULL;
}

DWORD row_hash(const char *s)
{
    DWORD h = 5381;
    for (; *s; s++)
        h = h * 33 + (BYTE) *s;
    return h;
}

/* Barre de progression dessinée (installateurs : ACME, Lotus…) : un texte « 45% » ou
   « 45 % » dans la fenêtre active. Dit par paliers de 10, et à 100, sans couper. */
static HWND prog_hwnd;
static int prog_said;
void progress_poll(void)
{
    static char big[1024];
    HWND act = GetActiveWindow();
    char *p, *q;
    int v = -1;

    if (!act || !EpOsmWindow(act, big, sizeof(big))) {
        prog_hwnd = NULL;
        return;
    }
    for (p = big; *p; p++) {
        if (*p != '%' || p == big)
            continue;
        q = p - 1;
        if (*q == ' ' && q > big)
            q--;
        if (*q < '0' || *q > '9')
            continue;
        while (q > big && q[-1] >= '0' && q[-1] <= '9')
            q--;
        if (q > big && q[-1] != ' ' && q[-1] != '/')
            continue;               /* collé à un mot : pas une jauge */
        v = 0;
        for (; *q >= '0' && *q <= '9'; q++)
            v = v * 10 + *q - '0';
        break;
    }
    if (v < 0 || v > 100 || GetMenu(act)) {   /* (fenêtre à menus : zone Zoom…) */
        prog_hwnd = NULL;
        return;
    }
    if (act != prog_hwnd) {
        prog_hwnd = act;
        prog_said = v;                  /* valeur de départ : pas annoncée */
        return;
    }
    if (v / 10 != prog_said / 10 || (v == 100 && prog_said != 100)) {
        char t[16];
        prog_said = v;
        wsprintf(t, "%d pour cent", v);
        say(t, FALSE);
    }
}

void auto_poll(void)
{
    HWND h = auto_scope(GetActiveWindow());
    static char row[TXT * 2], said[1024];
    DWORD rows[AUTO_ROWS], sig;
    RECT r;
    POINT pt;
    int n = 0, i, k, col;
    BOOL first = h != auto_hwnd, term = is_class(h, "DC_TERM");

    if (!h) {
        auto_hwnd = NULL;
        return;
    }
    sig = EpOsmSignature(h);
    if (first || sig != auto_sig) {     /* attendre la fin du dessin */
        auto_sig = sig;
        auto_tick = GetTickCount();
        if (!first)
            return;
    } else if (auto_tick == 0 || GetTickCount() - auto_tick < 400)
        return;
    auto_tick = 0;
    if (!term) {
        /* Didacticiel : la colonne de texte principale (pas la fausse fenêtre de
           démonstration à côté), quand elle change */
        auto_hwnd = h;
        if (EpOsmMainText(h, said, sizeof(said)) && row_hash(said) != auto_rows[0])
            say(said, FALSE);
        auto_rows[0] = row_hash(said);
        return;
    }
    GetWindowRect(h, &r);
    pt.x = r.left + 2;
    pt.y = r.top - 1;
    said[0] = 0;
    while (n < AUTO_ROWS && EpOsmReviewLine(h, &pt, 1, row, sizeof(row), &col) &&
           pt.y < r.bottom) {
        rows[n] = row_hash(row);
        for (i = 0, k = 0; i < auto_n && !k; i++)
            k = auto_rows[i] == rows[n];
        if (!k && (!first || !term) && lstrlen(said) + lstrlen(row) + 2 < (int) sizeof(said)) {
            if (said[0])
                lstrcat(said, " ");     /* (lignes d'un même paragraphe : pas de virgule) */
            lstrcat(said, row);
        }
        n++;
    }
    memcpy(auto_rows, rows, n * sizeof(DWORD));
    auto_n = n;
    auto_hwnd = h;
    /* écho de la frappe (Terminal) : déjà dit caractère par caractère */
    if (said[0] && (!term || GetTickCount() - key_tick > 700))
        say(said, FALSE);
}

void poll(void)
{
    com_read();                         /* marques en retard d'une lecture arrêtée */
    if (cfg_on) {
        if (GetTickCount() - cfg_tick > 60000L)
            cfg_close(FALSE);           /* menu oublié : rendre le clavier */
        return;
    }
    if (in_menu || sa_active())
        return;
    if (GetFocus() != last_focus || GetActiveWindow() != last_active)
        announce_focus(TRUE);
    if (dos_poll())
        return;                         /* invite MS-DOS : Épaudos parle */
    else {
        nav_poll();
        auto_poll();
        status_poll();
        progress_poll();              /* (avant check_value : fiche changée) */
        if (!sheet_poll() && !grid_poll() && !is_class(GetActiveWindow(), "SCICALC"))
            check_value();      /* (grille de tableur : cellule lue par sheet_poll) */
    }
    calc_poll();
}

/* -------------------------------------------------------------------- menus */

void on_menu(WPARAM wp, LPARAM lp)
{
    UINT flags = LOWORD(lp);
    HMENU hm = (HMENU) HIWORD(lp);
    char buf[TXT], out[TXT * 2], *accel;
    int i, n;

    if (flags == 0xFFFF && hm == NULL) {        /* menu fermé */
        in_menu = FALSE;
        menu_pending[0] = 0;
        KillTimer(hwnd_main, TIMER_MENU);
        last_focus = NULL;      /* relire le focus au prochain passage (et le titre si le
                                   menu a ouvert une autre fenêtre) */
        return;
    }
    if (!in_menu) {
        in_menu = TRUE;
    }
    if (flags & MF_SEPARATOR)
        return;
    buf[0] = 0;
    if (flags & MF_POPUP) {
        /* wParam est le menu déroulant : retrouver sa position dans hm */
        n = GetMenuItemCount(hm);
        for (i = 0; i < n; i++)
            if (GetSubMenu(hm, i) == (HMENU) wp) {
                GetMenuString(hm, i, buf, sizeof(buf), MF_BYPOSITION);
                break;
            }
    } else
        GetMenuString(hm, wp, buf, sizeof(buf), MF_BYCOMMAND);
    clean_label(buf, &accel);
    if (flags & MF_SYSMENU && !buf[0])
        lstrcpy(buf, "menu système");
    else if ((flags & MF_BITMAP) && !buf[0])
        lstrcpy(buf, "menu système du document");   /* icône de fenêtre MDI agrandie */
    out[0] = 0;
    append(out, buf);
    if (flags & MF_POPUP && !(flags & MF_SYSMENU) && !(flags & MF_BITMAP))
        append(out, "sous-menu");
    if (flags & MF_CHECKED)
        append(out, "coché");
    if (flags & (MF_GRAYED | MF_DISABLED))
        append(out, "grisé");
    if (accel && *accel)
        append(out, accel);
    if (!out[0])
        return;
    lstrcpyn(braille_menu, out, sizeof(braille_menu));
    if (flags & MF_POPUP) {
        /* Gauche ou Droite dans un menu ouvert : Windows resélectionne l'ancien
           titre, puis le nouveau, puis le premier élément de celui-ci, d'un trait.
           Un titre attend donc la fin de la rafale ; seul le dernier est dit. */
        lstrcpyn(menu_pending, out, sizeof(menu_pending));
        SetTimer(hwnd_main, TIMER_MENU, 60, NULL);
        return;
    }
    if (menu_pending[0]) {
        /* premier élément du menu qui s'ouvre : à la suite de son titre */
        KillTimer(hwnd_main, TIMER_MENU);
        say(menu_pending, TRUE);
        menu_pending[0] = 0;
        say(out, FALSE);
    } else
        say(out, !(popup_tick && GetTickCount() - popup_tick < 300));
    popup_tick = 0;
}

void on_menu_title(void)
{
    KillTimer(hwnd_main, TIMER_MENU);
    if (!menu_pending[0] || !in_menu)
        return;
    say(menu_pending, TRUE);
    menu_pending[0] = 0;
    popup_tick = GetTickCount();    /* son premier élément, s'il suit, ne le coupe pas */
}

/* --------------------------------------------------- surbrillance (hors écran) */

/* Texte affiché en couleurs de sélection dans la fenêtre active : l'élément courant
   d'une liste dessinée par le programme (icônes du Gestionnaire de programmes...). */
HWND hilite_hwnd;        /* fenêtre où un texte en surbrillance vient d'apparaître */

void on_hilite(void)
{
    char buf[TXT];
    HWND foc = GetFocus(), h = hilite_hwnd;
    int i;

    if (!h || !IsWindow(h) || in_menu || !foc || is_standard(foc) || sa_active())
        return;
    {
        const char *role;
        if (custom_value(foc, buf, &role))
            return;     /* contrôle reconnu : check_value() dit sa valeur */
    }
    {   /* fenêtre active, ou liste surgissante qui lui appartient ; listes
           déroulantes des boîtes de Word (classe _sdm_…, sans propriétaire) */
        HWND t = top_level(h), act = GetActiveWindow();
        char cls[8];
        GetClassName(t, cls, sizeof(cls));
        if (t != act && GetWindow(t, GW_OWNER) != act &&
            !(_fstrncmp(cls, "_sdm_", 5) == 0 && is_sdm(act)))
            return;
    }
    if (is_sdm(h))
        return;     /* boîte de Word : le rectangle de focus annonce l'élément */
    if (!EpOsmHilite(h, buf, sizeof(buf)))
        return;
    for (i = 0; buf[i] == ' '; i++)
        ;
    if (!buf[i])
        return;
    if (lstrcmp(buf + i, hilite_said) == 0 && GetTickCount() - hilite_tick < 1500)
        return;                         /* simple réaffichage */
    lstrcpyn(hilite_said, buf + i, sizeof(hilite_said));
    hilite_tick = GetTickCount();
    /* juste après l'annonce du focus (nom du groupe...) : à la suite, sans couper */
    say(buf + i, GetTickCount() - focus_tick > 400);
}

/* -------------------------------------------------------------- commandes */

void read_window(void)
{
    HWND act = GetActiveWindow(), c;
    char out[TXT * 2], d[TXT * 2];
    char cls[32];

    if (!act)
        return;
    GetWindowText(act, out, TXT);
    say(out, TRUE);
    for (c = GetWindow(act, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!IsWindowVisible(c))
            continue;
        class_of(c, cls, sizeof(cls));
        if (lstrcmp(cls, "STATIC") == 0) {
            GetWindowText(c, d, TXT);
            clean_label(d, NULL);
        } else
            describe(c, d);
        if (d[0])
            say(d, FALSE);
    }
}

/* Barre d'état : la rangée de texte au bas de la fenêtre active */
void say_status(void)
{
    char buf[TXT * 2];
    if (status_line(GetActiveWindow(), buf, sizeof(buf)))
        say(buf, TRUE);
    else
        say("pas de barre d'état", TRUE);
}

/* Alt+Tab, bascule rapide de Windows 3.1 (CoolSwitch) : tant qu'Alt est tenu, Windows
   affiche seulement le titre du programme suivant et ne l'active qu'au relâchement.
   On dit ce titre dès Tab, en suivant le même ordre : fenêtres principales (sans
   propriétaire) visibles, de haut en bas ; une fenêtre désactivée par sa boîte modale
   compte par cette boîte. Maj+Alt+Tab remonte. */
static HWND switch_hwnd;        /* dernier programme annoncé ; NULL : nouvelle bascule */

static HWND root_owner(HWND h)
{
    HWND o;
    h = top_level(h);
    while (h && (o = GetWindow(h, GW_OWNER)) != NULL)
        h = o;
    return h;
}

static BOOL switch_target(HWND w)
{
    HWND p;
    if (w == hwnd_main || !IsWindowVisible(w) || GetWindow(w, GW_OWNER))
        return FALSE;
    if (IsWindowEnabled(w))
        return TRUE;
    p = GetLastActivePopup(w);
    return p && p != w && IsWindowVisible(p) && IsWindowEnabled(p);
}

static void switch_next(BOOL back)
{
    HWND start, w;
    char title[TXT];

    if (!GetProfileInt("windows", "CoolSwitch", 1))
        return;                         /* bascule sans boîte : chaque fenêtre est activée */
    start = switch_hwnd && IsWindow(switch_hwnd) ? switch_hwnd : root_owner(GetActiveWindow());
    if (!start)
        return;
    for (w = start;;) {
        w = GetWindow(w, back ? GW_HWNDPREV : GW_HWNDNEXT);
        if (!w)
            w = GetWindow(start, back ? GW_HWNDLAST : GW_HWNDFIRST);
        if (!w || w == start)
            return;                     /* aucun autre programme */
        if (switch_target(w))
            break;
    }
    switch_hwnd = w;
    if (!GetWindowText(w, title, sizeof(title)))
        GetWindowText(GetLastActivePopup(w), title, sizeof(title));
    sa_stop(TRUE);
    say(title[0] ? title : "sans titre", TRUE);
}

void on_command(WPARAM vk, LPARAM lp)
{
    char buf[TXT];
    const char *role;

    /* lp bit 0 : Maj */
    if (vk == VK_UP && (lp & 1)) {
        say_selected();
        return;
    }
    if (calc_command(vk)) return;
    if (nav_command(vk, lp)) return;
    if (!(lp & 1) && (sheet_command(vk) || grid_command(vk))) return;
    switch (vk) {
    case 'T':
        GetWindowText(GetActiveWindow(), buf, sizeof(buf));
        say(buf[0] ? buf : "sans titre", TRUE);
        break;
    case VK_TAB:
        last_active = NULL;
        announce_focus(TRUE);
        break;
    case VK_DOWN:
        sa_start();
        break;
    case VK_UP:
        if (is_class(GetActiveWindow(), "CLOCK")) {
            clock_value(buf);
            say(buf, TRUE);
        } else if (last_focus && custom_value(last_focus, buf, &role) && buf[0]) {
            say(buf, TRUE);             /* lien de l'Aide, caractère choisi, volet */
        } else if (is_sdm(last_focus)) {
            frect_hwnd = last_focus;    /* boîte de Word : relire l'élément */
            SetRectEmpty(&frect_said);
            on_focusrect();
        } else if (last_focus && IsWindow(last_focus) && !is_standard(last_focus)) {
            POINT pt;
            if (EpOsmCaret(last_focus, &pt) || !EpOsmHilite(last_focus, buf, TXT)) {
                caret_screen(last_focus, &pt);
                EpOsmLine(last_focus, pt.y + 2, buf, TXT);
            }                           /* (sinon : élément en surbrillance) */
            say(buf[0] ? buf : "vide", TRUE);
        } else if (last_focus && IsWindow(last_focus)) {
            describe_value(last_focus, buf);
            say(buf[0] ? buf : "vide", TRUE);
        }
        break;
    case 'B':
        read_window();
        break;
    case 'E':
        cfg_open();
        break;
    case VK_NEXT:
        say_status();
        break;
    case 'F':
        say_format();
        break;
    case VK_DIVIDE:
        mouse_to_review();
        break;
    case VK_MULTIPLY:
        review_to_mouse();
        break;
    case 'D':                           /* débogage : focus, classe, curseur texte */
        if (lp & 1) {                   /* avec Maj : fenêtres de la fenêtre active */
            HWND c, a = GetFocus() ? top_level(GetFocus()) : GetActiveWindow();
            char cls[32], txt[40];
            int n = 0, depth = 1;
            class_of(a, cls, sizeof(cls));
            wsprintf(buf, "fenêtre %x %s, actif %x.", (UINT) a, (LPSTR) cls,
                     (UINT) GetActiveWindow());
            say(buf, TRUE);
            for (c = GetWindow(a, GW_CHILD); c && n < 40; n++) {
                class_of(c, cls, sizeof(cls));
                GetWindowText(c, txt, sizeof(txt));
                wsprintf(buf, "%d: %x %s id %d style %lx %s.", depth, (UINT) c, (LPSTR) cls,
                         GetWindowWord(c, GWW_ID), GetWindowLong(c, GWL_STYLE), (LPSTR) txt);
                say(buf, FALSE);
                if (GetWindow(c, GW_CHILD)) {
                    c = GetWindow(c, GW_CHILD);
                    depth++;
                } else {
                    while (c && !GetWindow(c, GW_HWNDNEXT) && c != a) {
                        c = GetParent(c);
                        depth--;
                    }
                    c = (c && c != a) ? GetWindow(c, GW_HWNDNEXT) : (HWND) 0;
                }
            }
            break;
        }
        {
            HWND f = GetFocus();
            char cls[32];
            POINT pt;
            GetCaretPos(&pt);
            GetClassName(f, cls, sizeof(cls));
            wsprintf(buf, "focus %x classe %s, curseur %d %d, actif %x, dernier %d %d, menu %d",
                     (UINT) f, (LPSTR) cls, pt.x, pt.y, (UINT) GetActiveWindow(),
                     last_caret.x, last_caret.y, in_menu);
            say(buf, TRUE);
            for (f = GetParent(f); f; f = GetParent(f)) {     /* parents et propriétaires */
                GetClassName(f, cls, sizeof(cls));
                wsprintf(buf, "parent %x %s style %lx", (UINT) f, (LPSTR) cls,
                         GetWindowLong(f, GWL_STYLE));
                say(buf, FALSE);
            }
        }
        break;
    case 'O':
        if (lp & 1) {                   /* avec Maj : entrées du modèle, une par une */
            HWND a = GetActiveWindow(), w;
            RECT r;
            char cls[32], t[96];
            int i, n = 0;
            say("modèle hors écran", TRUE);
            for (i = 0; i < 320 && n < 120; i++) {
                if (!EpOsmEntry(i, &w, &r, t, sizeof(t)) || !IsWindow(w) ||
                    top_level(w) != a)
                    continue;
                class_of(w, cls, sizeof(cls));
                wsprintf(buf, "%x %s %d %d %d %d: %s.", (UINT) w, (LPSTR) cls, r.left, r.top,
                         r.right - r.left, r.bottom - r.top, (LPSTR) t);
                say(buf, FALSE);
                n++;
            }
            for (i = 0; i < 64; i++) {
                int xs, ys;
                if (!EpOsmGlyphEntry(i, &w, &r, &xs, &ys) || !IsWindow(w) || top_level(w) != a)
                    continue;
                wsprintf(buf, "image %x %d %d %d %d source %d %d.", (UINT) w, r.left, r.top,
                         r.right - r.left, r.bottom - r.top, xs, ys);
                say(buf, FALSE);
            }
            for (i = 0; i < 8; i++)
                if (EpOsmRopEntry(i, t)) {
                    wsprintf(buf, "raster %s.", (LPSTR) t);
                    say(buf, FALSE);
                }
            break;
        }
        {
            static char big[1024];
            int n = EpOsmWindow(GetActiveWindow(), big, sizeof(big));
            say(n ? big : "rien à l'écran", TRUE);
        }
        break;
    case 'H':
        if (is_class(GetActiveWindow(), "SCICALC")) {
            say("Calculatrice. Avec Inser : gauche et droite, parcourir les boutons. "
                "Début et fin, premier et dernier bouton. Entrée, activer le bouton. "
                "Tabulation, relire le bouton choisi. Haut, relire le résultat. "
                "Page suivante, mémoire et réglages scientifiques. "
                "La frappe habituelle rend le suivi au résultat.", TRUE);
            break;
        }
        say("Commandes d'Épaulard, avec la touche Inser : T, titre. Tabulation, "
            "focus. Flèche haut, ligne courante. Flèche bas, dire tout. B, lire la "
            "fenêtre. O, lire tout le texte affiché. F, police et mise en forme. "
            "Page suivante, barre d'état. E, réglages de la voix. "
            "Majuscule flèche haut, lire la sélection. F4, quitter. Contrôle seul, "
            "silence.", TRUE);
        say("Pavé numérique, Verr Num éteint : 7, 8, 9, lignes. 4, 5, 6, mots. "
            "1, 2, 3, caractères. Inser moins, changer de revue. Inser 5, recentrer.", FALSE);
        say("Souris : barre oblique du pavé, clic gauche ; étoile, clic droit ; avec "
            "majuscule, verrouiller le bouton. Inser barre oblique, amener la souris "
            "au point de revue. Inser étoile, lire sous la souris.", FALSE);
        break;
    case VK_F4:
        say("Épaulard arrêté", TRUE);
        DestroyWindow(hwnd_main);
        break;
    }
}

/* ---------------------------------------------------------------- fenêtre */

LRESULT CALLBACK __export MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (net_in_dde)
            return 0;                   /* boucle modale du DDE : pas de réentrée */
        if (wp == TIMER_NAV)
            nav_timer();
        else if (wp == TIMER_NET) {
            KillTimer(h, TIMER_NET);
            net_timeout();
        } else if (wp == TIMER_HI) {
            KillTimer(h, TIMER_HI);
            on_hilite();
        } else if (wp == TIMER_SA)
            sa_tick();
        else if (wp == TIMER_FR) {
            KillTimer(h, TIMER_FR);
            on_focusrect();
        } else if (wp == TIMER_GL) {
            KillTimer(h, TIMER_GL);
            on_glyph();
        } else if (wp == TIMER_DLG) {
            KillTimer(h, TIMER_DLG);
            on_dialog_text();
        } else if (wp == TIMER_MS) {
            KillTimer(h, TIMER_MS);
            SetCursorPos(mouse_back.x, mouse_back.y);
        } else if (wp == TIMER_MENU)
            on_menu_title();
        else if (wp == TIMER_TAB)
            on_tabpage();
        else if (wp == TIMER_DOS)
            dos_fast();
        else if (wp == TIMER_LB) {
            char v[TXT];
            KillTimer(h, TIMER_LB);
            item_unknown = FALSE;
            if (GetFocus() == last_focus && last_focus && !in_menu) {
                describe_value(last_focus, v);
                if (!item_unknown)
                    say(v, FALSE);
            }
        }
        else {
            poll();
            if (br_enabled()) braille_poll();
        }
        return 0;
    case EPM_CMD:
        if (cfg_on) {
            cfg_key_in(wp);
            return 0;
        }
        if (wp != VK_DOWN)
            sa_interrupt(TRUE);
        on_command(wp, lp);
        return 0;
    case EPM_NAV:
        nav_key(wp, lp);
        return 0;
    case EPM_NET:
        net_event(wp, lp);
        return 0;
    case EPM_CFGKEY:
        if (cfg_on)
            cfg_key_in(wp);
        else
            EpCfgMode(FALSE);           /* (menu déjà fermé) */
        return 0;
    case EPM_REVIEW:
        sa_interrupt(TRUE);
        on_review((int) wp);
        return 0;
    case EPM_SILENCE:
        sa_interrupt(TRUE);
        silence();
        return 0;
    case EPM_MOUSE:
        sa_stop(TRUE);
        on_mouse((int) wp);
        return 0;
    case EPM_CHAR:
        if (!in_menu && is_class(GetActiveWindow(), "SCICALC")) {
            const char *op = NULL;
            if (wp == '+') op = "plus";
            else if (wp == '-') op = "moins";
            else if (wp == '*') op = "multiplié par";
            else if (wp == '/') op = "divisé par";
            else if (wp == '%') op = "pour cent";
            else if (wp == ',' || wp == '.') op = "virgule";
            if (wp == '=' || wp == '\r') calc_force = TRUE;
            if (op) say(op, TRUE);
            return 0;                   /* la valeur remplace l'écho de frappe */
        }
        /* écho des caractères tapés, sauf dans les boutons (Espace les active : on
           annonce plutôt le résultat) */
        if (in_menu || is_class(GetFocus(), "BUTTON") || is_sdm(GetFocus()))
            return 0;                   /* (boîte de Word : Espace coche une case) */
        if (wp >= ' ' || wp == '\r' || (wp == '\t' && is_class(GetFocus(), "EDIT")))
            echo_char((char) wp);
        return 0;
    case EPM_MENU:
        sa_stop(TRUE);
        on_menu(wp, lp);
        return 0;
    case EPM_HILITE:
        /* les morceaux (lignes d'un même libellé) arrivent l'un après l'autre */
        hilite_hwnd = (HWND) LOWORD(lp);
        SetTimer(h, TIMER_HI, 60, NULL);
        return 0;
    case EPM_FOCUSRECT:
        frect_hwnd = (HWND) wp;
        SetTimer(h, TIMER_FR, 80, NULL);
        return 0;
    case EPM_GLYPH:
        if ((HWND) wp == frect_hwnd)
            SetTimer(h, TIMER_GL, 150, NULL);
        return 0;
    case EPM_ACTIVATE:
        /* Toujours redire le titre : la nouvelle fenêtre a pu reprendre l'identifiant
           d'une fenêtre fermée. Les doublons sont écartés par announce_focus(). */
        sa_stop(TRUE);
        switch_hwnd = NULL;
        new_active = TRUE;
        last_focus = NULL;
        review_window = NULL;
        return 0;
    case EPM_FOCUS:
        /* signalé avant que le focus change : il a changé quand on traite ce message.
           Ne pas répéter ce que la scrutation vient d'annoncer. */
        if ((HWND) wp != sa.hwnd)
            sa_stop(TRUE);
        if (!in_menu)
            announce_focus(TRUE);
        return 0;
    case EPM_KEY:
        /* lParam bit 29 : Alt tenu ; bit 30 : répétition */
        if (wp == VK_MENU && !(lp & 0x40000000L))
            switch_hwnd = NULL;         /* Alt appuyé : nouvelle bascule */
        if (wp == VK_TAB && (lp & 0x20000000L) && !(lp & EPK_CTRL)) {
            key_tick = GetTickCount();
            switch_next((lp & EPK_SHIFT) != 0);
            return 0;
        }
        if (wp == VK_ESCAPE && (lp & 0x20000000L) && switch_hwnd) {
            switch_hwnd = NULL;         /* bascule annulée : on reste où l'on était */
            last_active = NULL;
            announce_focus(TRUE);
            return 0;
        }
        if (is_class(GetActiveWindow(), "SCICALC")) {
            calc_view = 0;
            calc_button = -1;
        }
        if (!in_menu && is_class(GetActiveWindow(), "SCICALC") &&
            (wp == VK_ESCAPE || wp == VK_DELETE || wp == VK_BACK))
            calc_force = TRUE;          /* redire zéro même si déjà affiché */
        if (sa.on) {
            /* toute autre touche arrête la lecture ; Haut et Bas partent de la
               ligne qui était lue (les autres touches agissent où est le curseur) */
            if (wp == VK_UP || wp == VK_DOWN)
                sa_interrupt(FALSE);
            else
                sa_stop(TRUE);
        }
        last_vk = wp;
        key_tick = GetTickCount();
        nav_key_seen(wp, lp);
        if (wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT ||
            ((wp == VK_HOME || wp == VK_END) && (lp & EPK_CTRL))) {
            line_key = TRUE;
            line_key_tick = GetTickCount();
            typed = FALSE;
        }
        if (wp == VK_BACK)
            typed = FALSE;              /* l'effacement déplace le curseur : on le dit */
        if (wp >= VK_PRIOR && wp <= VK_DOWN) {
            /* Maj + déplacement : la sélection change ; seul, il la retire */
            sel_key = (lp & EPK_SHIFT) != 0;
            sel_key_tick = GetTickCount();
            if (!sel_key)
                sel_said[0] = 0;
        }
        word_key = (wp == VK_LEFT || wp == VK_RIGHT) && (lp & EPK_CTRL);
        if (wp >= VK_PRIOR && wp <= VK_DOWN)
            typed_n = 0;                /* le mot en cours de frappe est abandonné */
        else if (wp == VK_BACK && typed_n)
            typed_n--;
        if (wp == VK_TAB && (lp & EPK_CTRL) && is_sdm(GetActiveWindow())) {
            /* page suivante d'une boîte de Word */
            tab_page(GetActiveWindow(), tab_before, sizeof(tab_before));
            tab_tick = GetTickCount();
            tab_waiting = TRUE;
            SetTimer(h, TIMER_TAB, 100, NULL);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(h, TIMER_ID);
        dos_stop();
        br_close();
        EpRemove();
        if (com >= 0) {
            /* laisser partir la dernière phrase avant de fermer le port */
            DWORD t = GetTickCount();
            COMSTAT st;
            while (GetTickCount() - t < 2000) {
                GetCommError(com, &st);
                if (st.cbOutQue == 0)
                    break;
            }
            CloseComm(com);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;

    (void) cmd; (void) show;
    if (hprev)
        return 0;                       /* une seule instance */
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hinst;
    wc.lpszClassName = "Epaulard";
    if (!RegisterClass(&wc))
        return 0;
    hwnd_main = CreateWindow("Epaulard", "Épaulard", WS_OVERLAPPED, 0, 0, 0, 0,
                             NULL, NULL, hinst, NULL);
    if (!hwnd_main)
        return 0;
    com_open();
    if (com < 0) {
        MessageBox(NULL, "Impossible d'ouvrir COM1.", "Épaulard", MB_OK | MB_ICONSTOP);
        return 0;
    }
    cfg_load(hinst);
    voice_apply();
    nav_init();
    if (!EpInstall(hwnd_main, com))
        say("Erreur : crochets non installés", TRUE);
    br_open();
    SetTimer(hwnd_main, TIMER_ID, POLL_MS, NULL);
    say("Épaulard prêt. Inser H pour l'aide.", TRUE);
    for (;;) {
        /* (invite MS-DOS en cours de frappe : rester éveillé, voir dos_spin) */
        if (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT)
                break;
        } else if (dos_spin())
            continue;
        else if (!GetMessage(&m, NULL, 0, 0))
            break;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
