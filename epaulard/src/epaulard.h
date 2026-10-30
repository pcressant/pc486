/* Messages de EPAUHOOK.DLL vers EPAULARD.EXE */
#define EPM_CMD      (WM_USER + 100)   /* Inser + touche : wParam = touche virtuelle, lParam bit 0 = Maj */
#define EPM_SILENCE  (WM_USER + 101)   /* Ctrl seul : couper la parole */
#define EPM_KEY      (WM_USER + 102)   /* touche appuyée (hors commandes) : wParam = touche,
                                          lParam d'origine + EPK_CTRL, EPK_SHIFT */
/* État de Ctrl et Maj au moment de l'appui, dans des bits réservés du lParam de
   WM_KEYDOWN : quand EPAULARD.EXE traite le message, ils sont souvent relâchés. */
#define EPK_CTRL     0x04000000L
#define EPK_SHIFT    0x08000000L
/* Touche postée par EPAULARD.EXE au programme (dire tout) : le crochet clavier de
   Windows 3.1 ne voit pas les messages postés ; s'il en voyait un, il le laisserait
   passer sans le signaler, pour ne pas le prendre pour une frappe de l'utilisateur. */
#define EPK_OWN      0x02000000L
#define EPM_CHAR     (WM_USER + 103)   /* caractère tapé : wParam = caractère ANSI */
#define EPM_MENU     (WM_USER + 104)   /* WM_MENUSELECT : wParam, lParam d'origine */
#define EPM_FOCUS    (WM_USER + 105)   /* le focus va passer à la fenêtre wParam */
#define EPM_ACTIVATE (WM_USER + 106)   /* la fenêtre wParam va devenir active */
#define EPM_HILITE   (WM_USER + 107)   /* texte affiché en surbrillance : wParam = entrée du
                                          modèle hors écran, LOWORD(lParam) = fenêtre */
#define EPM_REVIEW   (WM_USER + 108)   /* pavé 1..9 ; 10 = mode ; 11 = recentrer */
#define EPM_FOCUSRECT (WM_USER + 109)  /* rectangle de focus pointillé tracé : wParam = fenêtre */
#define EPM_GLYPH    (WM_USER + 110)   /* petite image changée (case cochée) : wParam = fenêtre */
#define EPM_MOUSE    (WM_USER + 111)   /* / ou * du pavé : wParam 1 gauche, 2 droit, +4 Maj */
#define EPM_CFGKEY   (WM_USER + 112)   /* menu des réglages ouvert : toute touche, wParam */
#define EPM_NAV      (WM_USER + 113)   /* mode navigation d'une page Web : touche prise,
                                          wParam ; lParam 1 Maj, 2 Ctrl, 4 Alt, 8 répétée */

BOOL FAR PASCAL EpInstall(HWND hwnd, int com);
void FAR PASCAL EpRemove(void);
/* Menu des réglages (Inser+E) : la DLL envoie alors toutes les touches à l'EXE */
void FAR PASCAL EpCfgMode(BOOL on);
int FAR PASCAL EpOsmGet(int slot, LPSTR out, int max);
/* Débogage : entrée slot (0 .. 319) ; 0 si libre */
int FAR PASCAL EpOsmEntry(int slot, HWND FAR *hwnd, RECT FAR *r, LPSTR out, int max);
BOOL FAR PASCAL EpOsmInvBox(HWND hwnd, int l, int t, int r, int b, RECT FAR *box);
BOOL FAR PASCAL EpOsmRopEntry(int i, LPSTR out);
BOOL FAR PASCAL EpOsmGlyphEntry(int slot, HWND FAR *hwnd, RECT FAR *r, int FAR *xs, int FAR *ys);
int FAR PASCAL EpOsmLine(HWND hwnd, int sy, LPSTR out, int max);
int FAR PASCAL EpOsmCharAt(HWND hwnd, int sx, int sy);
int FAR PASCAL EpOsmWindow(HWND top, LPSTR out, int max);
int FAR PASCAL EpOsmHilite(HWND hwnd, LPSTR out, int max);
BOOL FAR PASCAL EpOsmCaret(HWND hwnd, POINT FAR *pt);
BOOL FAR PASCAL EpPageView(HWND h);             /* page de Netscape (page.c) */
BOOL FAR PASCAL EpPageTab(BOOL back);           /* Tab dans une page : champ suivant */
/* Faire défiler la page jusqu'au champ c, même caché (hors de la partie affichée) ;
   permille : position estimée du champ dans la page */
BOOL FAR PASCAL EpPageReveal(HWND view, HWND c, int permille);
/* Mode navigation (navweb.c) : 1 touches de navigation de view prises par la DLL
   (EPM_NAV), 2 seulement Échap (mode formulaire automatique), 0 aucune */
void FAR PASCAL EpNavMode(HWND view, int mode);
int FAR PASCAL EpOsmRect(HWND hwnd, int l, int t, int r, int b, LPSTR out, int max);
BOOL FAR PASCAL EpOsmFocusRect(HWND hwnd, RECT FAR *r);   /* 2 : onglet (bitmap) */
int FAR PASCAL EpOsmBoldIn(HWND hwnd, int l, int t, int r, int b, LPSTR out, int max);
int FAR PASCAL EpOsmMainText(HWND hwnd, LPSTR out, int max);
BOOL FAR PASCAL EpOsmGlyph(HWND hwnd, int l, int t, int r, int b,
                           int FAR *w, int FAR *h, int FAR *xs, int FAR *ys);
/* Ligne visible dans scope : direction -1/0/+1 ; 2 = ligne la plus proche.
   pt : position écran en entrée, ordonnée de la ligne en sortie ; colonne du texte. */
int FAR PASCAL EpOsmReviewLine(HWND scope, POINT FAR *pt, int direction,
                             LPSTR out, int max, int FAR *column);
/* Inverse de la précédente : abscisse écran (centre) du caractère column de la rangée
   d'ordonnée pt->y, telle que EpOsmReviewLine la compose. */
BOOL FAR PASCAL EpOsmReviewPoint(HWND scope, POINT FAR *pt, int column);
/* Police et attributs du texte de scope sous le point (sx, sy), ou du plus proche sur
   la même rangée ; 0 si aucun texte. */
int FAR PASCAL EpOsmFormat(HWND scope, int sx, int sy, LPSTR out, int max);
/* Texte sélectionné visible dans scope : zones inversées (Write, Word) ou affichées en
   couleurs de sélection, dans l'ordre de lecture. */
int FAR PASCAL EpOsmSelection(HWND scope, LPSTR out, int max);
/* Colonnes sélectionnées [*from, *to[ de la rangée d'ordonnée sy, composée comme
   par EpOsmReviewLine ; FALSE si rien n'y est sélectionné. */
BOOL FAR PASCAL EpOsmRowSelection(HWND scope, int sy, int FAR *from, int FAR *to);
/* Empreinte du texte affiché dans scope : change quand il défile ou est redessiné
   autrement (dire tout : fin du document si elle ne change pas). */
DWORD FAR PASCAL EpOsmSignature(HWND scope);
/* Morceau visible exactement égal à s dont le centre est dans le rectangle (écran) */
BOOL FAR PASCAL EpOsmFind(HWND scope, int l, int t, int r, int b, LPCSTR s, RECT FAR *found);
/* Textes visibles qui commencent dans le rectangle ; *at : premier morceau (si at) */
int FAR PASCAL EpOsmStartIn(HWND scope, int l, int t, int r, int b, LPSTR out, int max,
                            RECT FAR *at);
/* Relais de l'invite MS-DOS : touches laissées à la machine DOS (Épaudos) */
void FAR PASCAL EpPassKeys(BOOL on);
int FAR PASCAL EpBrailleEpoch(void);

/* osm.c, interne à la DLL */
BOOL osm_install(void);
BOOL osm_remove(void);          /* TRUE : un appel d'origine est encore actif */
void osm_reap(void);            /* appels abandonnés par une tâche terminée */
void osm_destroyed(HWND hwnd);  /* fenêtre détruite : oublier ses textes */
