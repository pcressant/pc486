/* EPAUHOOK.DLL — crochets système d'Épaulard (Windows 3.1x, 16 bits).
 *
 * Windows 3.1 n'a pas d'API d'accessibilité : la DLL installe des crochets globaux
 * et transmet ce qu'elle voit à EPAULARD.EXE par PostMessage, qui fait la lecture.
 *   WH_KEYBOARD    : touches de commande (Inser + touche, Ctrl = silence) ; toutes
 *                    les touches pendant le menu des réglages (Inser+E)
 *   WH_GETMESSAGE  : caractères tapés (WM_CHAR), pour l'écho
 *   WH_CALLWNDPROC : menus (WM_MENUSELECT)
 *   (osm.c : modèle hors écran, texte affiché par GDI)
 *   WH_CBT         : changements de focus et de fenêtre active (Windows réutilise
 *                    aussitôt les identifiants de fenêtre : la scrutation seule ne voit
 *                    pas qu'un nouveau contrôle a pris la place d'un ancien) ;
 *                    destruction de fenêtre : textes oubliés par osm.c
 */
#include <windows.h>
#include "epaulard.h"
#include "braille.h"

/* Paramètres d'un message envoyé, vus par WH_CALLWNDPROC (absent des en-têtes Watcom) */
typedef struct {
    LPARAM lParam;
    WPARAM wParam;
    UINT message;
    HWND hwnd;
} EPCWP;

static HINSTANCE hinst;
HWND ep_hwnd_main;              /* fenêtre d'EPAULARD.EXE, destinataire des messages */
static HHOOK hk_kbd, hk_msg, hk_wnd, hk_cbt;
static BOOL ins_down;           /* touche Inser enfoncée (modificateur d'Épaulard) */
static BOOL ins_used;           /* Inser a servi de modificateur depuis son appui */
static BOOL ctrl_alone;         /* Ctrl appuyé seul : silence au relâchement */
static WORD pad_held;           /* relâchements des touches de revue déjà absorbées */
static int com_id = -1;         /* COM1 ouvert par EPAULARD.EXE */
BOOL ep_in_menu;                /* un menu est ouvert (WM_MENUSELECT) */
static BOOL cfg_mode;           /* menu des réglages d'Épaulard ouvert */
static BOOL pass_keys;          /* fenêtre DOS lue par Épaudos : toutes les touches à elle */
static DWORD cfg_tick;          /* sa dernière touche */
static BOOL tab_held;           /* Tab absorbé dans une page : son relâchement aussi */
static HWND nav_view;           /* page Web en mode navigation (navweb.c de l'EXE) */
static int nav_mode;            /* 1 navigation, 2 formulaire automatique (Échap seul) */
static BYTE nav_held[32];       /* touches absorbées : leur relâchement aussi */
static UINT nav_comma;          /* touche de la virgule (clavier français : ;,) */


/* Menu des réglages ouvert ou fermé par EPAULARD.EXE */
void FAR PASCAL __export EpCfgMode(BOOL on)
{
    cfg_mode = on;
    cfg_tick = GetTickCount();
}

/* Mode navigation d'une page Web, posé par EPAULARD.EXE (navweb.c) */
void FAR PASCAL __export EpNavMode(HWND view, int mode)
{
    nav_view = view;
    nav_mode = view ? mode : 0;
}

/* Touche du mode navigation : prise si le focus est dans la page */
static BOOL nav_take(WPARAM vk)
{
    HWND f = GetFocus();
    BOOL ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0;
    char cls[12];
    if (!nav_mode || !nav_view || !IsWindow(nav_view) || !f ||
        (f != nav_view && !IsChild(nav_view, f)))
        return FALSE;
    if (nav_mode == 2) {
        /* formulaire automatique : Échap revient en navigation, sauf liste dépliée */
        if (vk != VK_ESCAPE || ctrl || alt)
            return FALSE;
        GetClassName(f, cls, sizeof(cls));
        return lstrcmpi(cls, "ComboBox") != 0 || !SendMessage(f, CB_GETDROPPEDSTATE, 0, 0L);
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || vk == nav_comma ||
        vk == VK_RETURN || vk == VK_SPACE || vk == VK_TAB)
        return !ctrl && !alt;
    if (vk >= VK_PRIOR && vk <= VK_DOWN)
        return !alt || ctrl;    /* Alt seul : Netscape (Précédent) ; Ctrl+Alt : tableaux */
    return FALSE;
}

/* Fenêtre DOS au premier plan, lue par Épaudos dans sa machine virtuelle : ses
   commandes (Inser + touche, pavé) doivent lui parvenir. Posé par EPAULARD.EXE. */
void FAR PASCAL __export EpPassKeys(BOOL on)
{
    pass_keys = on;
    ins_down = FALSE;
}

/* Parole directe, depuis la tâche qui affiche une fenêtre modale système : tant
   qu'elle est ouverte, Windows ne donne plus la main à EPAULARD.EXE. */
static void direct_say(const char FAR *s, BOOL cut)
{
    char buf[300];
    int n, i;
    if (com_id < 0)
        return;
    if (cut) {
        buf[0] = 0x18;
        WriteComm(com_id, buf, 1);
    }
    lstrcpyn(buf, s, sizeof(buf) - 2);
    AnsiToOem(buf, buf);
    for (i = 0; buf[i]; i++)
        if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == 0x18 || buf[i] == '@' || buf[i] == '&')
            buf[i] = ' ';
    n = lstrlen(buf);
    buf[n++] = '\r';
    WriteComm(com_id, buf, n);
}

/* Braille direct, pour la même raison : la DLL écrit seule sur la plage. Une minuterie
   de la tâche de la boîte (sa boucle modale distribue WM_TIMER) lit les touches de
   défilement ; elle s'arrête d'elle-même à la fermeture de la boîte. */
static int br_epoch;            /* change quand la DLL a remplacé l'affichage de l'EXE */
static UINT sys_timer;

void CALLBACK __export EpSysTimer(HWND h, UINT msg, UINT id, DWORD t)
{
    int key;
    (void) h; (void) msg; (void) id; (void) t;
    if (!GetSysModalWindow()) {
        KillTimer(NULL, sys_timer);
        sys_timer = 0;
        br_epoch++;            /* forcer la reprise, même si le focus est inchangé */
        return;
    }
    br_tick();
    key = br_key();
    if (key == BR_BACK || key == BR_FORWARD)
        br_pan(key == BR_BACK ? -1 : 1);
    else if (key == BR_HOME)
        br_pan(0);
    br_tick();
}

static void direct_braille(const char FAR *s)
{
    char buf[300];
    int n = 0;
    if (!br_enabled())
        return;
    for (; *s && n < (int) sizeof(buf) - 1; s++)
        if (*s != '&' || s[1] == '&')   /* « &Oui » -> « Oui » */
            buf[n++] = *s == '&' ? *++s : *s;
    buf[n] = 0;
    br_text(buf, -1, FALSE);
    br_select(-1, -1);
    br_epoch++;
    br_tick();
    if (!sys_timer)
        sys_timer = SetTimer(NULL, 0, 100, (TIMERPROC) EpSysTimer);
}

int FAR PASCAL __export EpBrailleEpoch(void)
{
    return br_epoch;
}

static void add_text(char *dst, HWND h, int max)
{
    int n = lstrlen(dst);
    if (n && n < max - 3) {
        lstrcat(dst, ", ");
        n += 2;
    }
    if (n < max - 1)
        GetWindowText(h, dst + n, max - n);
}

static void add_control(char *out, HWND h, int max)
{
    char cls[16];
    add_text(out, h, max);
    GetClassName(h, cls, sizeof(cls));
    if (lstrcmpi(cls, "Button") == 0 && lstrlen(out) < max - 9)
        lstrcat(out, ", bouton");
}

/* Fenêtre modale système qui s'active : titre, textes et contrôle qui a le focus ;
   changement de focus ensuite : nom du contrôle. Windows pose le focus avant
   d'activer la fenêtre : ce premier focus est dit avec le titre. */
static void sysmodal_say(int code, HWND h)
{
    char out[300], cls[16];
    HWND c, f;
    out[0] = 0;
    if (code == HCBT_ACTIVATE) {
        add_text(out, h, sizeof(out));
        for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            GetClassName(c, cls, sizeof(cls));
            if (lstrcmpi(cls, "Static") == 0 && (GetWindowLong(c, GWL_STYLE) & 0x0F) <= SS_RIGHT)
                add_text(out, c, sizeof(out));
        }
        f = GetFocus();
        if (!f || GetParent(f) != h)    /* focus pas encore posé : le bouton par défaut */
            for (f = GetWindow(h, GW_CHILD); f; f = GetWindow(f, GW_HWNDNEXT)) {
                GetClassName(f, cls, sizeof(cls));
                if (lstrcmpi(cls, "Button") == 0 &&
                    (GetWindowLong(f, GWL_STYLE) & 0x0F) == BS_DEFPUSHBUTTON)
                    break;
            }
        if (f)
            add_control(out, f, sizeof(out));
        direct_say(out, TRUE);
        direct_braille(out);
    } else if (GetActiveWindow() == GetParent(h)) {
        add_control(out, h, sizeof(out));
        direct_say(out, TRUE);
        direct_braille(out);
    }
}

int PASCAL LibMain(HINSTANCE h, WORD ds, WORD heap, LPSTR cmd)
{
    (void) ds; (void) cmd;
    hinst = h;
    if (heap)
        UnlockData(0);
    return 1;
}

int FAR PASCAL __export WEP(int param)
{
    (void) param;
    return 1;
}

LRESULT CALLBACK __export EpKbdProc(int code, WPARAM vk, LPARAM lp)
{
    BOOL up = (lp & 0x80000000L) != 0;
    BOOL repeat = !up && (lp & 0x40000000L) != 0;
    int scan = (int) ((lp >> 16) & 0xFF), pad = 0, action = 0;
    WORD bit;

    if (code < 0 || !ep_hwnd_main || (lp & EPK_OWN))
        return CallNextHookEx(hk_kbd, code, vk, lp);

    /* Menu des réglages : toutes les touches lui reviennent, rien pour l'application.
       Sans touche pendant une minute, ou si Épaulard a disparu, le clavier est rendu. */
    if (cfg_mode && (!IsWindow(ep_hwnd_main) || GetTickCount() - cfg_tick > 70000L))
        cfg_mode = FALSE;
    if (cfg_mode) {
        if (vk == VK_INSERT)
            ins_down = !up;
        else if (!up) {
            cfg_tick = GetTickCount();
            PostMessage(ep_hwnd_main, EPM_CFGKEY, vk, 0L);
        }
        ctrl_alone = FALSE;
        return 1;
    }

    if (pass_keys && IsWindow(ep_hwnd_main))
        return CallNextHookEx(hk_kbd, code, vk, lp);
    /* Une touche que l'on absorbe (valeur 1) disparaît dès le premier appel, même
       quand le programme ne fait que regarder la file (HC_NOREMOVE : Word) ; elle ne
       reviendra pas en HC_ACTION. Les touches absorbées sont donc traitées au premier
       appel, quel que soit le code ; les autres seulement en HC_ACTION. */

    /* Les flèches dédiées ont le bit étendu : ne jamais les confondre avec
       les mêmes touches virtuelles du pavé quand Verr Num est éteint. */
    if (!(lp & 0x01000000L)) {
        switch (scan) {
        case 0x47: pad = 7; break;
        case 0x48: pad = 8; break;
        case 0x49: pad = 9; break;
        case 0x4B: pad = 4; break;
        case 0x4C: pad = 5; break;
        case 0x4D: pad = 6; break;
        case 0x4F: pad = 1; break;
        case 0x50: pad = 2; break;
        case 0x51: pad = 3; break;
        case 0x4A: pad = 10; break;
        }
    }
    if (pad) {
        bit = (WORD) (1 << pad);
        if (up && (pad_held & bit)) {
            pad_held &= ~bit;
            return 1;
        }
        if (!(GetKeyState(VK_NUMLOCK) & 1) && GetKeyState(VK_SHIFT) >= 0 &&
            GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0) {
            if (ins_down && pad == 10)
                action = 10;
            else if (ins_down && pad == 5)
                action = 11;
            else if (!ins_down && pad <= 9)
                action = pad;
        }
        if (action && !up) {
            pad_held |= bit;
            ctrl_alone = FALSE;
            if (ins_down)
                ins_used = TRUE;
            if (!repeat || action <= 9)
                PostMessage(ep_hwnd_main, EPM_REVIEW, action, 0L);
            return 1;
        }
    }

    /* / et * du pavé, Verr Num éteint : clic gauche, clic droit ; avec Maj, bouton
       verrouillé (comme NVDA). Inser + / ou * : commandes ordinaires (EPM_CMD). */
    if (!ins_down && (vk == VK_DIVIDE || vk == VK_MULTIPLY)) {
        bit = (WORD) (vk == VK_DIVIDE ? 1 << 12 : 1 << 13);
        if (up && (pad_held & bit)) {
            pad_held &= ~bit;
            return 1;
        }
        if (!up && !(GetKeyState(VK_NUMLOCK) & 1) && GetKeyState(VK_CONTROL) >= 0 &&
            GetKeyState(VK_MENU) >= 0) {
            pad_held |= bit;
            ctrl_alone = FALSE;
            if (!repeat)
                PostMessage(ep_hwnd_main, EPM_MOUSE, (vk == VK_DIVIDE ? 1 : 2) |
                            (GetKeyState(VK_SHIFT) < 0 ? 4 : 0), 0L);
            return 1;
        }
    }

    if (vk == VK_INSERT) {
        if (!up && !repeat) {
            ins_down = TRUE;
            ins_used = FALSE;
        } else if (up) {
            ins_down = FALSE;
            /* Inser seul, appuyé deux fois de suite : on pourrait le rendre à l'appli */
        }
        return 1;                       /* Inser est réservé à Épaulard */
    }
    if (ins_down) {
        if (!up) {
            ins_used = TRUE;
            PostMessage(ep_hwnd_main, EPM_CMD, vk,
                        (GetKeyState(VK_SHIFT) < 0 ? 1L : 0L) | (repeat ? 2L : 0L));
        }
        return 1;                       /* Inser + touche : commande, rien pour l'appli */
    }

    if (vk < 256 && (nav_held[vk >> 3] & (1 << (vk & 7)))) {
        if (up) {
            nav_held[vk >> 3] &= ~(1 << (vk & 7));
            return 1;
        }
    }
    if (!up && vk < 256 && nav_take(vk)) {
        nav_held[vk >> 3] |= (BYTE) (1 << (vk & 7));
        ctrl_alone = FALSE;
        PostMessage(ep_hwnd_main, EPM_NAV, vk, (GetKeyState(VK_SHIFT) < 0 ? 1L : 0L) |
                    (GetKeyState(VK_CONTROL) < 0 ? 2L : 0L) |
                    (GetKeyState(VK_MENU) < 0 ? 4L : 0L) | (repeat ? 8L : 0L));
        return 1;
    }

    if (vk == VK_TAB && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0) {
        if (up && tab_held) {
            tab_held = FALSE;
            return 1;
        }
        if (!up && EpPageTab(GetKeyState(VK_SHIFT) < 0)) {
            tab_held = TRUE;
            ctrl_alone = FALSE;
            return 1;
        }
    }

    if (code == HC_ACTION) {
        if (vk == VK_CONTROL) {
            if (!up && !repeat)
                ctrl_alone = TRUE;
            else if (up && ctrl_alone) {
                if (GetSysModalWindow())
                    direct_say("", TRUE);
                else
                    PostMessage(ep_hwnd_main, EPM_SILENCE, 0, 0L);
            }
        } else if (!up) {
            ctrl_alone = FALSE;
            PostMessage(ep_hwnd_main, EPM_KEY, vk, (lp & ~(EPK_CTRL | EPK_SHIFT)) |
                        (GetKeyState(VK_CONTROL) < 0 ? EPK_CTRL : 0L) |
                        (GetKeyState(VK_SHIFT) < 0 ? EPK_SHIFT : 0L));
        }
    }
    return CallNextHookEx(hk_kbd, code, vk, lp);
}

LRESULT CALLBACK __export EpMsgProc(int code, WPARAM wp, LPARAM lp)
{
    osm_reap();                 /* interceptions laissées par une tâche terminée */
    if (code >= 0 && ep_hwnd_main) {
        MSG FAR *m = (MSG FAR *) lp;
        /* wParam : options de PeekMessage. Un coup d'œil sans retrait (Word regarde
           la file avant de lire) ne compte pas : le caractère sera revu au retrait. */
        if (m->message == WM_CHAR && (wp == 0 || (wp & PM_REMOVE)))
            PostMessage(ep_hwnd_main, EPM_CHAR, m->wParam, MAKELPARAM(m->hwnd, 0));
    }
    return CallNextHookEx(hk_msg, code, wp, lp);
}

LRESULT CALLBACK __export EpWndProc(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0 && ep_hwnd_main) {
        EPCWP FAR *c = (EPCWP FAR *) lp;
        if (c->message == WM_MENUSELECT) {
            /* wParam : identifiant de l'élément (ou menu déroulant) ; lParam : drapeaux, menu */
            ep_in_menu = !(LOWORD(c->lParam) == 0xFFFF && HIWORD(c->lParam) == 0);
            PostMessage(ep_hwnd_main, EPM_MENU, c->wParam, c->lParam);
        }
    }
    return CallNextHookEx(hk_wnd, code, wp, lp);
}

LRESULT CALLBACK __export EpCbtProc(int code, WPARAM wp, LPARAM lp)
{
    if ((code == HCBT_SETFOCUS || code == HCBT_ACTIVATE) && ep_hwnd_main && wp &&
        GetSysModalWindow())
        sysmodal_say(code, (HWND) wp);
    else if (code == HCBT_SETFOCUS && ep_hwnd_main && wp)
        PostMessage(ep_hwnd_main, EPM_FOCUS, wp, 0L);
    else if (code == HCBT_ACTIVATE && ep_hwnd_main && wp)
        PostMessage(ep_hwnd_main, EPM_ACTIVATE, wp, 0L);
    else if (code == HCBT_DESTROYWND && wp)
        osm_destroyed((HWND) wp);
    return CallNextHookEx(hk_cbt, code, wp, lp);
}

BOOL FAR PASCAL __export EpInstall(HWND hwnd, int com)
{
    ep_hwnd_main = hwnd;
    com_id = com;
    nav_comma = LOBYTE(VkKeyScan(','));
    hk_kbd = SetWindowsHookEx(WH_KEYBOARD, (HOOKPROC) EpKbdProc, hinst, NULL);
    hk_msg = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC) EpMsgProc, hinst, NULL);
    hk_wnd = SetWindowsHookEx(WH_CALLWNDPROC, (HOOKPROC) EpWndProc, hinst, NULL);
    hk_cbt = SetWindowsHookEx(WH_CBT, (HOOKPROC) EpCbtProc, hinst, NULL);
    return hk_kbd && hk_msg && hk_wnd && hk_cbt && osm_install();
}

void FAR PASCAL __export EpRemove(void)
{
    if (hk_kbd) UnhookWindowsHookEx(hk_kbd);
    if (hk_msg) UnhookWindowsHookEx(hk_msg);
    if (hk_wnd) UnhookWindowsHookEx(hk_wnd);
    if (hk_cbt) UnhookWindowsHookEx(hk_cbt);
    hk_kbd = hk_msg = hk_wnd = hk_cbt = NULL;
    nav_view = NULL;
    nav_mode = 0;
    if (osm_remove()) {
        /* Une tâche est dans la vraie fonction (boîte de message dans son
           WM_ERASEBKGND, par exemple) et reviendra dans notre code : décharger la
           DLL la ferait sauter dans un segment libéré. On la garde jusqu'à la fin de
           Windows ; son retour libérera le sélecteur. */
        char path[144];
        if (GetModuleFileName(hinst, path, sizeof(path)))
            LoadLibrary(path);
    }
    ep_hwnd_main = NULL;
}
