/* Banc du modèle hors écran (vrai osm.c) : chaque scénario reproduit un chemin de
   dessin d'un programme Windows tel qu'Épaulard le voit après détournement de GDI. */
#include <setjmp.h>
#define WIN16_FULL
#include "../../epaulard/src/osm.c"

HWND ep_hwnd_main;
BOOL ep_in_menu;

static int failures;
#define CHECK(c) do { if (!(c)) { printf("ÉCHEC %s:%d : %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b))) { printf("ÉCHEC %s:%d : « %s » au lieu de « %s »\n", __FILE__, __LINE__, (a), (b)); failures++; } } while (0)

static void text(HDC hdc, int x, int y, const char *s)
{
    EpExtTextOut(hdc, x, y, 0, NULL, s, (UINT) strlen(s), NULL);
}

static void clear(HDC hdc, int l, int t, int r, int b)
{
    RECT rc;
    SetRect(&rc, l, t, r, b);
    EpExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rc, "", 0, NULL);
}

/* rangée de scope à l'ordonnée sy (écran) */
static char *row(HWND scope, int sy)
{
    static char out[512];
    POINT pt;
    int col;
    pt.x = 0;
    pt.y = sy;
    if (!EpOsmReviewLine(scope, &pt, 0, out, sizeof(out), &col))
        out[0] = 0;
    return out;
}

static void reset_world(void)
{
    memset(sim_win, 0, sizeof(sim_win));
    memset(sim_dc, 0, sizeof(sim_dc));
    sim_qn = 0;
    osm_remove();
    osm_install();
}

/* Write : texte écrit à l'écran, curseur et sélection inversés par PatBlt */
static void test_write(void)
{
    HWND top, doc;
    HDC dc;
    POINT pt;
    int a, b;
    char sel[128];
    DWORD sig;

    reset_world();
    top = sim_create(0, "MSWRITE", "Write", 0, 0, 0, 640, 480);
    doc = sim_create(top, "MSWRITE_DOC", "", 0, 10, 40, 610, 200);
    dc = EpGetDC(doc);
    text(dc, 0, 0, "Hello world");
    text(dc, 0, 16, "Seconde ligne");
    CHECK_STR(row(top, 48), "Hello world");
    CHECK_STR(row(top, 64), "Seconde ligne");
    CHECK(EpOsmCharAt(doc, 10 + 8 * 6 + 1, 42) == 'w');

    EpPatBlt(dc, 8, 16, 1, 16, DSTINVERT);      /* curseur : un point de large */
    CHECK(EpOsmCaret(doc, &pt) && pt.x == 18 && pt.y == 56);

    EpPatBlt(dc, 0, 0, 40, 16, DSTINVERT);      /* « Hello » sélectionné */
    CHECK(EpOsmRowSelection(top, 48, &a, &b) && a == 0 && b == 5);
    CHECK(!EpOsmRowSelection(top, 64, &a, &b) && a == -1);
    EpOsmSelection(top, sel, sizeof(sel));
    CHECK_STR(sel, "Hello");
    EpPatBlt(dc, 40, 0, 48, 16, DSTINVERT);     /* extension : « Hello world » */
    CHECK(EpOsmRowSelection(top, 48, &a, &b) && a == 0 && b == 11);
    EpPatBlt(dc, 0, 0, 88, 16, DSTINVERT);      /* tout réinversé : plus rien */
    CHECK(!EpOsmRowSelection(top, 48, &a, &b));

    /* défilement d'une ligne : le texte suit, l'empreinte change */
    sig = EpOsmSignature(top);
    EpScrollWindow(doc, 0, -16, NULL, NULL);
    CHECK_STR(row(top, 48), "Seconde ligne");
    CHECK(EpOsmSignature(top) != sig);
    /* le même écran redessiné dans l'autre ordre (autres places du tableau) :
       inchangée */
    text(dc, 0, 16, "Troisieme");
    sig = EpOsmSignature(top);
    clear(dc, 0, 0, 600, 32);
    text(dc, 0, 0, "Seconde ligne");
    text(dc, 0, 16, "Troisieme");
    CHECK(EpOsmSignature(top) == sig);
    /* une ligne effacée change l'empreinte */
    clear(dc, 0, 0, 600, 16);
    CHECK(EpOsmSignature(top) != sig);
    CHECK_STR(row(top, 48), "");
}

/* Word, mode Normal : lignes préparées dans un bitmap puis copiées à l'écran */
static void test_word_normal(void)
{
    HWND top, doc;
    HDC win, bmp, cur;
    POINT pt;

    reset_world();
    top = sim_create(0, "OpusApp", "Microsoft Word", 0, 0, 0, 640, 480);
    doc = sim_create(top, "OpusWwd", "", 0, 24, 123, 624, 400);
    win = EpGetDC(doc);
    bmp = sim_memdc();
    cur = sim_memdc();
    text(bmp, 0, 0, "Ligne de Word");
    CHECK_STR(row(top, 123 + 8), "");           /* pas encore à l'écran */
    EpBitBlt(win, 0, 32, 600, 16, bmp, 0, 0, SRCCOPY);
    CHECK_STR(row(top, 123 + 40), "Ligne de Word");
    EpBitBlt(win, 16, 32, 2, 15, cur, 0, 0, 0x00690145L);
    CHECK(EpOsmCaret(doc, &pt) && pt.x == 40 && pt.y == 155);
    CHECK_STR(row(top, 123 + 40), "Ligne de Word");
    /* ligne déjà sélectionnée, redessinée en couleurs de sélection */
    text(bmp, 0, 16, "abc ");
    sim_dc[bmp].bk = GetSysColor(COLOR_HIGHLIGHT);
    text(bmp, 32, 16, "def");
    sim_dc[bmp].bk = RGB(255, 255, 255);
    EpBitBlt(win, 0, 48, 600, 16, bmp, 0, 16, SRCCOPY);
    CHECK_STR(row(top, 123 + 56), "abc def");
    {
        int a, b;
        CHECK(EpOsmRowSelection(top, 123 + 56, &a, &b) && a == 4 && b == 7);
    }
}

/* Word, mode Page (en-tête et pied de page) : bitmap à viewport décalé, et curseur
   copié d'un bitmap qui a lui-même reçu du texte. Régression du 25 octobre 2026 : le
   curseur effaçait la ligne du modèle et n'était plus suivi. */
static void test_word_page_header(void)
{
    HWND top, doc;
    HDC win, bmp, cur;
    POINT pt;

    reset_world();
    top = sim_create(0, "OpusApp", "Microsoft Word", 0, 0, 0, 640, 480);
    doc = sim_create(top, "OpusWwd", "", 0, 24, 123, 624, 400);
    win = EpGetDC(doc);
    bmp = sim_memdc();
    cur = sim_memdc();
    sim_dc[bmp].vy = -64;
    text(bmp, 0, 64, "Mon entete");
    text(cur, 0, 0, "x");                       /* texte resté dans le bitmap du curseur */
    EpBitBlt(win, 0, 64, 600, 15, bmp, 0, 64, SRCCOPY);
    CHECK_STR(row(top, 123 + 70), "Mon entete");
    EpBitBlt(win, 40, 64, 2, 15, cur, 0, 16, 0x00690145L);   /* sur « entete » */
    CHECK_STR(row(top, 123 + 70), "Mon entete");
    CHECK(EpOsmCaret(doc, &pt) && pt.x == 64 && pt.y == 187);
}

/* bitmap recopié dans un autre avant l'écran */
static void test_bitmap_chain(void)
{
    HWND top, doc;
    HDC win, a, b;

    reset_world();
    top = sim_create(0, "OpusApp", "Word", 0, 0, 0, 640, 480);
    doc = sim_create(top, "OpusWwd", "", 0, 0, 100, 600, 400);
    win = EpGetDC(doc);
    a = sim_memdc();
    b = sim_memdc();
    text(a, 0, 0, "Pied de page");
    EpBitBlt(b, 0, 32, 600, 16, a, 0, 0, SRCCOPY);
    EpBitBlt(win, 0, 200, 600, 16, b, 0, 32, SRCCOPY);
    CHECK_STR(row(top, 308), "Pied de page");
    /* bitmap copié sur lui-même : le texte est décalé */
    EpBitBlt(b, 0, 0, 600, 64, b, 0, 32, SRCCOPY);
    EpBitBlt(win, 0, 0, 600, 16, b, 0, 0, SRCCOPY);
    CHECK_STR(row(top, 108), "Pied de page");
}

/* Gestionnaire de programmes : libellé d'icône en couleurs de sélection */
static void test_hilite(void)
{
    HWND top, grp;
    HDC dc;
    char out[128];
    MSG m;

    reset_world();
    ep_hwnd_main = sim_create(0, "Epaulard", "", 0, 0, 0, 0, 0);
    top = sim_create(0, "Progman", "Gestionnaire de programmes", 0, 0, 0, 640, 480);
    grp = sim_create(top, "PMGroup", "Groupe principal", 0, 70, 110, 490, 290);
    dc = EpGetDC(grp);
    text(dc, 10, 50, "Panneau de");
    sim_dc[dc].bk = GetSysColor(COLOR_HIGHLIGHT);
    text(dc, 90, 50, "Gestionnaire");
    text(dc, 90, 66, "de fichiers");
    CHECK(EpOsmHilite(grp, out, sizeof(out)) == 2);
    CHECK_STR(out, "Gestionnaire de fichiers");
    CHECK(sim_take(ep_hwnd_main, &m) && m.message == EPM_HILITE && LOWORD(m.lParam) == grp);
    /* sélection déplacée : l'ancien libellé est redessiné normalement */
    sim_dc[dc].bk = RGB(255, 255, 255);
    text(dc, 90, 50, "Gestionnaire");
    text(dc, 90, 66, "de fichiers");
    sim_dc[dc].bk = GetSysColor(COLOR_HIGHLIGHT);
    text(dc, 10, 50, "Panneau de");
    EpOsmHilite(grp, out, sizeof(out));
    CHECK_STR(out, "Panneau de");
    /* pas de curseur texte dans une telle fenêtre */
    {
        POINT pt;
        CHECK(!EpOsmCaret(grp, &pt));
    }
    ep_hwnd_main = 0;
}

/* Boîte de Word : rectangle de focus en traits XOR, case à cocher en image */
static void test_sdm(void)
{
    HWND top, dlg;
    HDC dc, img;
    RECT r;
    int w, h, xs, ys;

    reset_world();
    top = sim_create(0, "OpusApp", "Word", 0, 0, 0, 640, 480);
    dlg = sim_create(0, "bosa_sdm_Microsoft Word 6.0", "Caractères", 0, 100, 100, 500, 400);
    dc = EpGetDC(dlg);
    img = sim_memdc();
    text(dc, 40, 20, "Gras");
    EpPatBlt(dc, 38, 18, 40, 1, PATINVERT);     /* haut */
    EpPatBlt(dc, 38, 36, 40, 1, PATINVERT);     /* bas */
    CHECK(EpOsmFocusRect(dlg, &r) && r.left == 138 && r.top == 118 && r.bottom == 137);
    EpBitBlt(dc, 20, 20, 14, 13, img, 14, 0, SRCCOPY);
    CHECK(EpOsmGlyph(dlg, 110, 110, 138, 140, &w, &h, &xs, &ys) && w == 14 && h == 13 && xs == 14);
    (void) top;
}

/* Repeinte : BeginPaint oublie la zone, sauf dans une boîte de Word ou sa page */
static void test_paint(void)
{
    HWND top, doc, dlg, page;
    HDC dc;
    PAINTSTRUCT ps;

    reset_world();
    top = sim_create(0, "Notepad", "Bloc-notes", 0, 0, 0, 640, 480);
    doc = sim_create(top, "Child", "", 0, 0, 20, 640, 480);
    dc = EpGetDC(doc);
    text(dc, 0, 0, "ancien");
    SetRect(&sim_paint, 0, 0, 640, 16);
    EpBeginPaint(doc, &ps);
    CHECK_STR(row(top, 28), "");
    dlg = sim_create(0, "bosa_sdm_x", "Boîte", 0, 100, 100, 300, 300);
    dc = EpGetDC(dlg);
    text(dc, 0, 0, "Libellé");
    SetRect(&sim_paint, 0, 0, 200, 200);
    EpBeginPaint(dlg, &ps);
    CHECK_STR(row(dlg, 108), "Libellé");
    page = sim_create(dlg, "sa_sdm_x", "", 0, 100, 130, 300, 300);   /* page d'Options */
    dc = EpGetDC(page);
    text(dc, 0, 0, "Police brouillon");
    SetRect(&sim_paint, 0, 0, 200, 100);
    EpBeginPaint(page, &ps);
    CHECK_STR(row(page, 138), "Police brouillon");
}

/* Revue : rangées voisines, fragments assemblés, abscisse d'un caractère */
static void test_review(void)
{
    HWND top, doc;
    HDC dc;
    POINT pt;
    char out[128];
    int col;

    reset_world();
    top = sim_create(0, "App", "App", 0, 0, 0, 640, 480);
    doc = sim_create(top, "Doc", "", 0, 0, 0, 640, 480);
    dc = EpGetDC(doc);
    text(dc, 0, 0, "un");
    text(dc, 40, 0, "deux");                    /* 3 caractères d'écart : une espace */
    text(dc, 0, 48, "trois");                   /* deux rangées vides au-dessus */
    pt.x = 48; pt.y = 8;
    CHECK(EpOsmReviewLine(top, &pt, 0, out, sizeof(out), &col) == 2);
    CHECK_STR(out, "un deux");
    CHECK(col == 4);
    CHECK(EpOsmReviewLine(top, &pt, 1, out, sizeof(out), &col));
    CHECK_STR(out, "trois");
    CHECK(pt.y == 56);
    CHECK(!EpOsmReviewLine(top, &pt, 1, out, sizeof(out), &col));
    pt.y = 8;
    CHECK(EpOsmReviewPoint(top, &pt, 3) && pt.x == 42);
}

static void nested_gdi_remove(void)
{
    CHECK(p_to.depth == 1);
    EpExtTextOut(0, 0, 0, 0, NULL, "imbriqué", 8, NULL);
    CHECK(!p_eto.depth && p_to.depth == 1);
    osm_remove();
    CHECK(p_to.alias && p_to.removing);
}

/* Chaque échec partiel rend les ressources ; un retrait pendant un appel
   original attend le dernier retour et ne réinstalle jamais le saut. */
static void test_patch_lifecycle(void)
{
    int i;
    osm_remove();
    CHECK(sim_fixed == 0 && sim_aliases == 0);
    for (i = 1; i <= 11; i++) {
        sim_alloc_count = 0;
        sim_alloc_fail = i;
        CHECK(!osm_install());
        CHECK(sim_fixed == 0 && sim_aliases == 0);
        CHECK(!p_eto.alias && !p_ddc.alias);
        osm_remove();
        CHECK(sim_fixed == 0);
    }
    sim_alloc_fail = 0;
    sim_presto_fail = 1;
    CHECK(!osm_install());
    CHECK(sim_fixed == 0 && sim_aliases == 0);
    sim_presto_fail = 0;
    CHECK(osm_install());
    CHECK(sim_fixed == 11 && sim_aliases == 11);
    CHECK(!osm_install());
    CHECK(sim_fixed == 11);
    patch_enter(&p_eto);
    patch_enter(&p_eto);
    osm_remove();
    CHECK(p_eto.alias && p_eto.depth == 2);
    CHECK(sim_fixed == 1 && sim_aliases == 1);
    patch_leave(&p_eto);
    CHECK(p_eto.alias && p_eto.depth == 1);
    patch_leave(&p_eto);
    CHECK(!p_eto.alias && !p_eto.depth);
    CHECK(sim_fixed == 0 && sim_aliases == 0);
    CHECK(osm_install());
    sim_gdi_callback = nested_gdi_remove;
    sim_textout_result = FALSE;
    CHECK(!EpTextOut(0, 0, 0, "test", 4));
    CHECK(!p_to.depth && !p_to.alias);
    CHECK(memcmp(sim_patch_area, p_to.orig, 5) == 0);
    CHECK(sim_fixed == 0 && sim_aliases == 0);
    sim_gdi_callback = NULL;
    sim_textout_result = TRUE;
    CHECK(osm_install());
    osm_remove();
    CHECK(sim_fixed == 0 && sim_aliases == 0);
}

/* Faute dans un rappel du vrai BeginPaint ou dans GDI : Windows termine la tâche et
   abandonne sa pile, sans revenir dans l'interception. Simulé par longjmp ; le
   noyau Windows 3.11 (libération du TDB, IsTask) n'est qu'approché. */
static jmp_buf task_killed;
static void task_faults(void)
{
    sim_dead_task = sim_task;
    longjmp(task_killed, 1);
}

static void other_task_paints(void)
{
    /* tâche vivante qui attend une autre (SendMessage dans WM_ERASEBKGND) */
    sim_task = 3;
    osm_reap();
    CHECK(p_bp.depth == 1 && p_bp.task == 2);
    CHECK(memcmp(sim_patch_area, p_bp.orig, 5) == 0);
    sim_task = 2;
}

static void test_abandoned_call(void)
{
    PAINTSTRUCT ps;
    HWND top;
    HDC dc;

    reset_world();
    sim_dead_task = 0;
    top = sim_create(0, "Appli", "Appli", 0, 0, 0, 300, 200);

    /* appel normal avec un rappel dans une autre tâche : rien n'est récupéré */
    sim_task = 2;
    sim_paint_callback = other_task_paints;
    EpBeginPaint(top, &ps);
    CHECK(!p_bp.depth && !p_bp.task && !pending);
    CHECK(memcmp(sim_patch_area, p_bp.jmp, 5) == 0);

    /* la tâche meurt dans son WM_ERASEBKGND : saut absent pour tous */
    sim_paint_callback = task_faults;
    if (!setjmp(task_killed))
        EpBeginPaint(top, &ps);
    sim_paint_callback = NULL;
    sim_task = 1;
    CHECK(p_bp.depth == 1 && pending == 1);
    CHECK(memcmp(sim_patch_area, p_bp.orig, 5) == 0);
    osm_reap();
    CHECK(!p_bp.depth && !p_bp.task && !pending);
    CHECK(memcmp(sim_patch_area, p_bp.jmp, 5) == 0);
    CHECK(sim_fixed == 11 && sim_aliases == 11);

    /* faute dans GDI pendant TextOut : le modèle doit continuer à noter le texte */
    sim_task = 4;
    sim_gdi_callback = task_faults;
    dc = EpGetDC(top);
    if (!setjmp(task_killed))
        EpTextOut(dc, 0, 0, "perdu", 5);
    sim_gdi_callback = NULL;
    sim_task = 1;
    CHECK(busy == 4 && p_to.depth == 1);
    osm_reap();
    CHECK(!busy && !p_to.depth && !pending);
    text(dc, 10, 20, "noté");
    CHECK_STR(row(top, 25), "noté");

    /* retrait pendant un appel abandonné : ressources rendues à la réinstallation */
    sim_task = 5;
    sim_paint_callback = task_faults;
    if (!setjmp(task_killed))
        EpBeginPaint(top, &ps);
    sim_paint_callback = NULL;
    sim_task = 1;
    sim_dead_task = 0;          /* tâche encore vivante au retrait */
    CHECK(osm_remove());
    CHECK(p_bp.alias && p_bp.removing && sim_aliases == 1);
    /* Réinstallation pendant que l'appel est actif : sélecteur réutilisé, pas de
       saut tant que l'original tourne ; son retour le repose. */
    CHECK(osm_install());
    CHECK(!p_bp.removing && p_bp.depth == 1 && sim_aliases == 11 && sim_fixed == 11);
    CHECK(!osm_install());
    patch_leave(&p_bp);
    CHECK(!pending && memcmp(sim_patch_area, p_bp.jmp, 5) == 0);
    /* même chose, mais la tâche meurt : le retrait s'achève au prochain passage */
    sim_task = 6;
    sim_paint_callback = task_faults;
    if (!setjmp(task_killed))
        EpBeginPaint(top, &ps);
    sim_paint_callback = NULL;
    sim_task = 1;
    sim_dead_task = 0;
    CHECK(osm_remove());
    CHECK(sim_aliases == 1);
    sim_dead_task = 6;
    osm_reap();
    CHECK(!p_bp.alias && !pending && sim_aliases == 0 && sim_fixed == 0);
    CHECK(memcmp(sim_patch_area, p_bp.orig, 5) == 0);
    sim_dead_task = 0;
    CHECK(osm_install());
}

/* Fenêtre détruite : son identifiant, aussitôt réutilisé par Windows, ne doit pas
   reprendre ses anciens textes (« Ligne de commande: » lu dans le glossaire). */
static void test_destroyed(void)
{
    HWND dlg, label, topic;
    HDC dc;

    reset_world();
    dlg = sim_create(0, "#32770", "Exécuter", 0, 100, 100, 400, 200);
    label = sim_create(dlg, "Static", "", 0, 110, 110, 300, 130);
    dc = EpGetDC(label);
    text(dc, 0, 0, "Ligne de commande:");
    CHECK_STR(row(dlg, 112), "Ligne de commande:");
    osm_destroyed(dlg);                 /* seule la boîte passe par le crochet */
    sim_destroy(label);
    sim_destroy(dlg);
    dlg = sim_create(0, "MS_WINTOPIC_SECONDARY", "glossaire", 0, 100, 100, 400, 200);
    topic = sim_create(dlg, "MS_WINTOPIC", "", 0, 110, 110, 300, 130);
    CHECK(topic == label);              /* même identifiant */
    CHECK_STR(row(dlg, 112), "");
}

int main(void)
{
    test_patch_lifecycle();
    test_abandoned_call();
    test_write();
    test_word_normal();
    test_word_page_header();
    test_bitmap_chain();
    test_hilite();
    test_sdm();
    test_paint();
    test_review();
    test_destroyed();
    if (failures)
        printf("%d échec(s)\n", failures);
    else
        printf("osm : tous les scénarios réussis\n");
    return failures != 0;
}
