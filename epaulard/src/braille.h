/* Baum série, indépendant de la parole Apollo. Le pilote est dans EPAUHOOK.DLL :
   l'EXE l'utilise d'ordinaire, la DLL l'utilise seule quand une boîte modale système
   bloque l'EXE (même état, même trame en cours, aucun mélange sur COM3). */
#ifndef BRAPI
#define BRAPI FAR PASCAL __export
#endif
#define BR_BACK 1
#define BR_FORWARD 2
#define BR_UP 3
#define BR_DOWN 4
#define BR_HOME 5
#define BR_ROUTE 256
void BRAPI br_open(void);
void BRAPI br_close(void);
BOOL BRAPI br_enabled(void);
void BRAPI br_tick(void);
void BRAPI br_text(const char FAR *text, int cursor, BOOL follow);
void BRAPI br_pan(int direction);
/* Texte sélectionné, colonnes [from, to[ du texte affiché : points 7 et 8 ;
   from >= to : aucune sélection. À redonner après chaque br_text. */
void BRAPI br_select(int from, int to);
int BRAPI br_key(void);
int BRAPI br_offset(void);
int BRAPI br_width(void);
/* Invite MS-DOS : la plage est à Épaudos (cellules, touches en trames Baum) */
void BRAPI br_relay(BOOL on);
void BRAPI br_cells(const BYTE FAR *cells, int n);
int BRAPI br_relay_read(BYTE FAR *out, int max);
