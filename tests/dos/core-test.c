/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "core.c"
#include "dialog.c"
static WORD video[ED_CELLS];
static int vx=12, vy=4, vcols=80, vrows=25, textmode=1, writable=1;
static unsigned char output[2][65536];
static int outn[2], incoming[2][256], ins[2], outs[2], serial_calls;
static int inject_key;
int hw_tx(int p, BYTE b)
{
    if (!writable) return 0;
    assert(outn[p] < 65536); output[p][outn[p]++] = b; return 1;
}
int hw_rx(int p)
{
    if (inject_key) {
        inject_key=0;
        assert(ed_scan(82,0)); assert(ed_scan(72,0));
        assert(ed_scan(72|128,0)); assert(ed_scan(82|128,0));
    }
    return outs[p] == ins[p] ? -1 : incoming[p][outs[p]++];
}
void hw_serial(int p,int div) { (void)p; (void)div; ++serial_calls; }
int hw_screen(int *c,int *r,int *x,int *y,WORD *s)
{
    *c=vcols; *r=vrows; *x=vx; *y=vy;
    memcpy(s,video,vcols*vrows*sizeof(WORD)); return textmode;
}
void hw_overlay(int r,const char *s) { (void)r; (void)s; }
void hw_restore(void) {}
void hw_quiet(void) {}
static int guest;               /* hw_guest : ED_SLEEP, ED_RUN ou ED_SKIP */
int hw_guest(int k) { (void)k; return guest; }
EDBOX ed_box;
WORD hw_lock(void) { return 0; }
void hw_unlock(WORD flags) { (void)flags; }
static void run(int n) { while (n--) ed_tick(); }
static void put(int x,int y,const char *s,int a)
{ while (*s) video[y*vcols+x++]=(WORD)((a<<8)|(BYTE)*s++); }
static void key(int s) { assert(ed_scan((BYTE)s,0)); assert(ed_scan((BYTE)(s|128),0)); run(1); }
static void combo(int s)
{ assert(ed_scan(82,0)); key(s); assert(ed_scan(82|128,0)); run(3); }
static void contains(const char *s)
{
    output[0][outn[0]]=0;
    if (!strstr((char *)output[0],s)) { fprintf(stderr,"attendu %s dans : %s\n",s,output[0]); assert(0); }
}
static void receive(int p,int b) { incoming[p][ins[p]++]=b; }
/* Touche laissée à l'application : flèche E0 ou touche ordinaire. */
static void nav(int s)
{ assert(!ed_scan(0xe0,0)); assert(!ed_scan((BYTE)s,0)); assert(!ed_scan(0xe0,0)); assert(!ed_scan((BYTE)(s|128),0)); }
static void app(int s) { assert(!ed_scan((BYTE)s,0)); assert(!ed_scan((BYTE)(s|128),0)); }
static void absent(const char *s)
{
    output[0][outn[0]]=0;
    if (strstr((char *)output[0],s)) { fprintf(stderr,"inattendu %s dans : %s\n",s,output[0]); assert(0); }
}
/* Octet absent de la parole (strstr du Watcom ignore ici les octets >= 80h). */
static void nobyte(int b)
{ int i; for (i=0;i<outn[0];++i) if (output[0][i]==b) { fprintf(stderr,"octet %02x inattendu\n",b); assert(0); } }
static void clear_row(int y) { int x; for (x=0;x<vcols;++x) video[y*vcols+x]=0x0720; }
/* Cadre simple ou double, intérieur effacé, titre centré dans le bord haut. */
static void box(int x0,int y0,int x1,int y1,int a,const char *title,int dbl)
{
    int x,y,n=0;
    for (y=y0;y<=y1;++y) for (x=x0;x<=x1;++x) {
        int c=' ';
        if (y==y0||y==y1) c=dbl?0xcd:0xc4;
        if (x==x0||x==x1) c=dbl?0xba:0xb3;
        if (y==y0&&x==x0) c=dbl?0xc9:0xda; if (y==y0&&x==x1) c=dbl?0xbb:0xbf;
        if (y==y1&&x==x0) c=dbl?0xc8:0xc0; if (y==y1&&x==x1) c=dbl?0xbc:0xd9;
        video[y*vcols+x]=(WORD)((a<<8)|c);
    }
    if (title) { while (title[n]) ++n; put((x0+x1-n)/2,y0,title,a); }
}
static void fill(int x0,int y0,int x1,int y1,int c,int a)
{ int x,y; for (y=y0;y<=y1;++y) for (x=x0;x<=x1;++x) video[y*vcols+x]=(WORD)((a<<8)|c); }
/* Fenêtre de document de Word 6 pour DOS, relevée sous QEMU : bord haut
   « ■═══ Document1 ═══ » sans coin, fini par 12h, barre de défilement en colonne 79,
   ligne d'état en rangée 23 et message en rangée 24. */
static void word_window(void)
{
    int y;
    fill(0,0,79,0,' ',0x70); put(2,0,"Fichier  Edition  Affiche  Insère  Format",0x70);
    fill(0,1,79,1,' ',0x97); put(1,1,"Style:[Normal\xfa\xfa\xfa]",0x97);
    fill(0,2,79,2,0xcd,0x30); put(0,2,"\xfe",0x30); put(34,2," Document1 ",0x30); put(79,2,"\x12",0x30);
    for (y=3;y<=21;++y) { fill(0,y,78,y,' ',0x17); put(0,y,"\xb3",0x17); put(79,y,"\xb0",0x70); }
    fill(0,22,79,22,0xb0,0x70); put(0,22,"\xc0 ",0x17); put(79,22,"\xd9",0x17);
    fill(0,23,79,23,' ',0x70); put(0,23,"Pg1 Co1         {}       <F1=Aide>",0x70);
    fill(0,24,79,24,' ',0x30); put(0,24,"Modifiez le document",0x30);
}
/* Boîte « Caractère » de Word : barre de titre d'une autre couleur, ombre. */
static void word_dialog(void)
{
    fill(11,5,67,5,0xb1,0x55); put(34,5," Caractère ",0x5f);
    fill(11,6,67,19,' ',0x70); fill(68,6,69,20,' ',0x08); fill(13,20,69,20,' ',0x08);
    put(13,6,"Police: [didot a\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa]",0x70);
    put(13,9,"[ ] Gras",0x70); put(13,10,"[X] Italique",0x70);
    box(45,10,64,13,0x70," Position ",0);
    put(48,11,"(\x07) Normale",0x70); put(48,12,"( ) Exposant",0x70);
    box(36,17,47,19,0x70,0,1); put(41,18,"OK",0x70);
    box(53,17,64,19,0x70,0,0); put(56,18,"Annuler",0x70);
}
int main(int argc,char **argv)
{
    int i,n;
    assert(argc==2);
    for (i=0;i<ED_CELLS;++i) video[i]=0x0720;
    put(0,4,"alpha beta gamma",7); put(0,5,"deuxieme ligne",7);
    ed_init(); run(10); outn[0]=outn[1]=0;
    if (!strcmp(argv[1],"review")) {
        key(76); run(10); contains("gamma");
        combo(74); key(73); run(10); contains("deuxieme ligne");
        key(71); run(10); contains("alpha beta gamma");
        assert(vx==12 && vy==4);
        assert(!ed_scan(0xe0,0)); assert(!ed_scan(72,0));
        assert(!ed_scan(72,32));
    } else if (!strcmp(argv[1],"menu")) {
        combo(18); assert(menu); assert(ed_cfg[0]==3);
        key(77); assert(ed_cfg[0]==4); key(1); assert(!menu && ed_save);
        run(20); contains("@W4");
        combo(18); run(1100); assert(!menu);
    } else if (!strcmp(argv[1],"sleep")) {
        textmode=0; run(1); n=outn[0]; run(100); assert(outn[0]==n);
        assert(!ed_scan(82,0)); textmode=1; run(10); assert(!ed_disabled);
        ed_windows=1; run(1); n=outn[0]; run(100); assert(outn[0]==n);
        assert(!ed_scan(82,0)); ed_windows=0; run(10); contains("prêt");
    } else if (!strcmp(argv[1],"guest")) {
        /* Windows : machine DOS relayée par Épaulard (mode invité) */
        ed_windows=1; guest=ED_SLEEP; run(5); n=outn[0]; run(50); assert(outn[0]==n);
        assert(!ed_scan(82,0));
        guest=ED_RUN; put(0,10,"C:\\>dir",7); vy=10; vx=8; run(20);
        assert(outn[0]>n); assert(ed_guest);
        /* braille relayé : largeur rendue par Épaulard, cellules de la ligne */
        i=outn[1]; receive(1,27); receive(1,1); receive(1,40); run(10);
        assert(br_width==40); assert(outn[1]>i+40);
        assert(output[1][outn[1]-42]==27 && output[1][outn[1]-41]==1);
        n=outn[0]; guest=ED_SKIP; run(50); assert(outn[0]==n); assert(ed_guest);
        assert(!ed_scan(82,0));
        guest=ED_RUN; key(72); run(20); assert(outn[0]>n);
        guest=ED_SLEEP; run(2); assert(!ed_guest);
        ed_windows=0; run(10); contains("prêt");
    } else if (!strcmp(argv[1],"echo")) {
        combo(80); run(20); assert(awaiting); n=outn[0]; run(100);
        assert(outn[0]==n); receive(0,echo_id); run(20);
        assert(all_row==5); contains("deuxieme ligne");
        assert(!ed_scan(29,0)); run(3); assert(!all && !awaiting);
    } else if (!strcmp(argv[1],"baum")) {
        receive(1,27); receive(1,1); receive(1,20); run(10); assert(br_width==20);
        combo(72); run(20); assert(br_pos==br_len);
        receive(1,27); receive(1,0x27); receive(1,7); run(3);
        assert(ed_flat && rx==6 && vx==12);
        /* Une réponse d'identité contient des ESC bruts, non échappés. */
        receive(1,27); receive(1,0x84);
        for(i=0;i<16;++i) receive(1,i==2?27:'A');
        receive(1,27); receive(1,1); receive(1,40); run(10); assert(br_width==40);
        assert(br850[130]==0x3f); /* é CP850 -> TBFR2007 */
    } else if (!strcmp(argv[1],"frame")) {
        int x,y;
        video[2*80+2]=0x07da; video[2*80+40]=0x07bf;
        video[10*80+2]=0x07c0; video[10*80+40]=0x07d9;
        for(x=3;x<40;++x) video[2*80+x]=video[10*80+x]=0x07c4;
        for(y=3;y<10;++y) video[y*80+2]=video[y*80+40]=0x07b3;
        run(2); assert(left==3 && right==39 && top==3 && bottom==9);
        put(4,6,"Ouvrir",0x70); run(10); contains("Ouvrir");
        /* Le titre et les séparateurs n'annulent pas le cadre. */
        put(12,2," Titre ",7);
        video[7*80+2]=0x07c3; video[7*80+40]=0x07b4;
        run(2); assert(left==3 && right==39 && bottom==9);
        ed_flat=1; rx=39; ry=5; review(2,1); assert(rx==3 && ry==6);
        /* Cadre ouvert d'EDIT : titre en haut, barre de défilement à droite,
           pas de coins en bas ; le bord gauche délimite jusqu'à la ligne d'état. */
        for(y=0;y<25;++y) clear_row(y);
        video[1*80+0]=0x07da; video[1*80+79]=0x07bf;
        for(x=1;x<79;++x) video[1*80+x]=0x07c4;
        put(30,1," CONFIG.SYS ",7);
        for(y=2;y<=23;++y) { video[y*80+0]=0x07b3; video[y*80+79]=0x07b0; }
        video[2*80+79]=0x0718; video[23*80+79]=0x0719;
        put(0,24," Editeur MS-DOS  F1=Aide ",0x30);
        put(1,2,"DEVICE=C:\\DOS\\EMM386.EXE NOEMS",7); vx=3; vy=2;
        run(2); assert(left==1 && right==78 && top==2 && bottom==23);
        combo(72); run(20); contains("DEVICE=C:"); nobyte(0xb3);
        /* Le curseur de la barre de défilement bouge : pas un texte nouveau. */
        outn[0]=0; run(3); video[5*80+79]=0x7020; video[6*80+79]=0x07b0; run(4); assert(outn[0]==0);
    } else if (!strcmp(argv[1],"format")) {
        put(0,4,"A+B @E",7); ed_cfg[3]=2; run(2);
        combo(72); run(20); contains("plus"); contains("arobase");
        ed_cfg[5]=1; ed_flat=1; rx=0; ry=4; review(2,0); run(10); contains("majuscule A");
        outn[0]=0; ed_cfg[4]=3; nav(77); vx=1; run(10); contains("plus");
        /* Une sortie de Windows doit réinitialiser les ports même si aucun
           timer du TSR n'a été exécuté pendant la session Windows. */
        n=serial_calls; ed_resume=1; run(1); assert(serial_calls>n);
    } else if (!strcmp(argv[1],"preemption")) {
        inject_key=1; run(10); contains("alpha beta gamma");
        assert(!stop);
    } else if (!strcmp(argv[1],"keys")) {
        static const BYTE pause[6]={0xe1,0x1d,0x45,0xe1,0x9d,0xc5};
        for(i=0;i<6;++i) assert(!ed_scan(pause[i],0));
        assert(!ctrl && !shift);
        assert(!ed_scan(0xe0,32)); assert(!ed_scan(0x2a,32));
        assert(!shift);
        assert(!ed_scan(0xe0,32)); assert(ed_scan(82,32));
        assert(ed_scan(35,32)); assert(ed_scan(35|128,32));
        assert(!ed_scan(0xe0,32)); assert(ed_scan(82|128,32));
        run(20); contains("Pavé");
    } else if (!strcmp(argv[1],"bounded")) {
        writable=0; combo(72); run(500); assert(speech_pos<speech_n);
        writable=1; run(30); contains("alpha beta gamma");
        vcols=132; vrows=50; vx=131; vy=49; run(5);
        assert(ed_cols==132 && ed_rows==50);
    } else if (!strcmp(argv[1],"scroll")) {
        /* EDIT : Bas en bas de fenêtre fait défiler le texte, le curseur reste
           sur la même rangée et la ligne d'état change. */
        put(0,24," Ligne: 5   Col: 13 ",0x30); run(3); outn[0]=0;
        nav(80); put(0,4,"deuxieme ligne  ",7); put(0,5,"DEVICE=C:\\DOS\\EMM386.EXE NOEMS",7);
        put(0,24," Ligne: 6   Col: 13 ",0x30); run(3);
        contains("deuxieme ligne"); absent("EMM386");
        /* Droite : la colonne de la ligne d'état change aussi. */
        outn[0]=0; nav(77); vx=13; put(0,24," Ligne: 6   Col: 14 ",0x30); run(3);
        contains("e\r"); absent("Ligne");
        /* Rappel d'historique : la rangée change et le curseur saute en fin. */
        outn[0]=0; nav(72); put(0,4,"dir /w          ",7); vx=6; run(3); contains("dir /w");
        /* Page précédente sans défilement possible : rien, puis expiration. */
        outn[0]=0; nav(73); run(30); assert(outn[0]==0);
    } else if (!strcmp(argv[1],"typing")) {
        ed_cfg[6]=0; put(0,24," Ligne: 5   Col: 13 ",0x30); run(3); outn[0]=0;
        app(16); put(12,4,"Q",7); vx=13; put(0,24," Ligne: 5   Col: 14 ",0x30); run(3);
        contains("Q\r"); absent("Ligne");
        /* Deux frappes dans le même passage : la suite est lue d'un coup. */
        outn[0]=0; app(30); app(31); put(13,4,"xy",7); vx=15; run(3); contains("xy\r");
        /* Retour arrière : le caractère effacé est nommé. */
        outn[0]=0; app(14); put(14,4," ",7); vx=14; run(3); contains("y\r");
        /* Suppr : le caractère sous le curseur disparaît. */
        outn[0]=0; put(14,4,"z",7); run(2); outn[0]=0;
        nav(83); put(14,4," ",7); run(3); contains("z\r");
        /* Redessin tardif après une frappe (éditeur lent) : pas d'écho écran. */
        outn[0]=0; ed_cfg[6]=1; app(30); put(14,4,"w",7); vx=15; run(2); contains("w\r");
        outn[0]=0; put(0,20,"ligne redessinee",7); run(4); absent("redessinee"); ed_cfg[6]=0;
        /* Écho par mots : l'espace tapé lit le mot terminé. */
        outn[0]=0; ed_cfg[4]=1; app(57); put(15,4," ",7); vx=16; run(3); contains("Qx");
        /* La frappe n'est pas prise pour une surbrillance, même colorée. */
        outn[0]=0; ed_cfg[4]=0; app(30); put(16,4,"PRINT",0x0a); vx=17; run(3);
        contains("P\r"); absent("PRINT");
    } else if (!strcmp(argv[1],"screen")) {
        int y;
        assert(ed_cfg[6]==1); run(3); outn[0]=0;
        /* Sortie d'un programme, aucune touche : lue sans couper, avec écho. */
        put(0,10,"sortie une",7); put(0,11,"sortie deux",7); run(3);
        contains("sortie une\r@E"); contains("\rsortie deux\r@E"); absent("\x18sortie deux");
        /* Défilement d'une rangée : seule la rangée nouvelle est lue. */
        outn[0]=0;
        for (y=0;y<24;++y) memcpy(video+y*vcols,video+(y+1)*vcols,vcols*sizeof(WORD));
        clear_row(24); put(0,24,"nouvelle",7); run(3);
        contains("nouvelle"); absent("sortie");
        /* Rangée du curseur : attendue stable (un passage), puis lue une fois.
           La parole part à la fin du passage qui la prépare. */
        outn[0]=0; vx=4; vy=22; put(0,22,"C:\\>",7); run(2); assert(outn[0]==0);
        run(1); contains("C:\\>"); outn[0]=0; run(5); assert(outn[0]==0);
        /* Entrée : la commande n'est pas relue, la sortie et l'invite le sont. */
        outn[0]=0; app(28); put(0,23,"resultat",7); put(0,24,"C:\\>",7); vx=4; vy=24; run(2);
        contains("resultat"); absent("C:\\>"); run(1); contains("C:\\>");
        /* Fenêtre redessinée sur deux passages (éditeur lent) : un défilement
           d'une rangée n'est pas lu comme du texte nouveau. */
        outn[0]=0; for (y=2;y<=20;++y) { clear_row(y); put(0,y,"ligne",7); } run(12); outn[0]=0;
        for (y=2;y<=10;++y) memcpy(video+y*vcols,video+(y+1)*vcols,vcols*sizeof(WORD)); run(1);
        for (y=11;y<=19;++y) memcpy(video+y*vcols,video+(y+1)*vcols,vcols*sizeof(WORD)); clear_row(20); run(4);
        absent("ligne");
        /* Sortie continue : vidée de force après ED_SETTLE passages. */
        outn[0]=0; for (y=0;y<8;++y) { clear_row(21); put(0,21,y&1?"flux a":"flux b",7); run(1); }
        contains("flux");
        /* Entrée au milieu d'un éditeur : le bas de la fenêtre descend d'une
           rangée sans être relu ; la nouvelle rangée vide ne dit rien. */
        outn[0]=0; put(0,14,"ligne a",7); put(0,15,"ligne b",7); put(0,16,"ligne c",7); run(3); outn[0]=0;
        vy=15; vx=0; app(28);
        put(0,17,"ligne c",7); put(0,16,"ligne b",7); clear_row(15); vy=16; run(3);
        absent("ligne b"); absent("ligne c");
        /* Tab dans une boîte : le curseur change de rangée sans texte nouveau. */
        outn[0]=0; app(15); vy=10; vx=0; run(3); contains("ligne\r");
        /* Bascule au clavier, puis silence. */
        outn[0]=0; combo(78); contains("désactivé"); assert(!ed_cfg[6]);
        outn[0]=0; put(0,12,"silence",7); run(3); absent("silence");
        combo(78); contains("activé"); outn[0]=0;
        /* Une file pleine perd la ligne sans déborder. */
        for (y=0;y<40;++y) { clear_row(12); put(0,12,y&1?"pair pair pair pair pair pair pair pair pair":"impair impair impair impair impair impair",7); run(1); }
        assert(speech_n<=(int)sizeof(speech)-4);
    } else if (!strcmp(argv[1],"word")) {
        /* Word 6 pour DOS : la barre d'état change à chaque frappe, sans
           jamais être lue ; les lignes sont lues sans le bord du document. */
        word_window(); put(1,3,"  Bonjour le monde.",0x17); vx=20; vy=3; run(10); outn[0]=0;
        assert(left==1 && right==78 && top==3 && bottom==21);
        app(16); put(20,3,"a",0x17); vx=21; run(1); put(0,23,"Pg1 Co21",0x70); run(5);
        contains("a\r"); absent("Pg1");
        /* Mise à jour tardive de la ligne d'état, sans touche. */
        outn[0]=0; run(10); put(0,23,"Pg1 Co22",0x70); run(6); absent("Pg1");
        /* Entrée : la marque de fin (04h) quitte la ligne, qui n'est pas relue. */
        put(22,3,"\x04",0x17); run(3); outn[0]=0;
        app(28); put(22,3," ",0x17); put(3,4,"\x04",0x17); vx=3; vy=4; put(0,23,"Pg1 Co1 ",0x70); run(6);
        absent("Pg1"); nobyte(0xb3); absent("Bonjour");
        outn[0]=0; combo(72); run(3); contains("vide");
        /* Des parenthèses dans le texte ne sont pas un bouton radio. */
        outn[0]=0; put(3,4,"(b)",0x17); vx=4; run(6); assert(!dlg_found); absent("radio");
    } else if (!strcmp(argv[1],"wordstart")) {
        /* Lancement de Word pour DOS : écran de présentation encadré, puis
           fenêtre principale. Sur une machine rapide (QEMU sans bridage, 86Box
           DX4/100), tout tient en un ou deux passages : la barre de menus et la
           ligne d'état doivent être lues comme sur une machine lente. */
        int y, pass, splash[2] = {1, 20};
        for (pass = 0; pass < 2; ++pass) {
            for (y=0;y<25;++y) clear_row(y);
            put(0,21,"C:\\>word",7); vx=8; vy=21; run(3); outn[0]=0;
            app(28);
            for (y=0;y<25;++y) clear_row(y);
            box(5,2,74,23,0x70,0,0); put(32,3,"Microsoft Word 6.0",0x70); vx=0; vy=25;
            run(splash[pass]);
            word_window(); vx=1; vy=3; run(30);
            contains("Fichier"); contains("Pg1");
            /* Ensuite, l'écho reste limité au document : la ligne d'état
               mise à jour par la frappe n'est pas relue. */
            outn[0]=0; app(16); put(1,3,"a",0x17); vx=2; run(1); put(0,23,"Pg1 Co2",0x70); run(6);
            contains("a\r"); absent("Pg1");
        }
    } else if (!strcmp(argv[1],"dialog")) {
        word_window(); put(1,3,"  Bonjour le monde.",0x17); vx=3; vy=3; run(10); outn[0]=0;
        /* Ouverture : titre, puis contrôle actif, après la fin du dessin. */
        app(28); word_dialog(); vx=29; vy=6; run(4);
        contains("Caractère\rboîte de dialogue\rPolice\rchamp\rdidot a\r"); absent("Pg1");
        assert(left==11 && right==67 && top==6 && bottom==19);
        outn[0]=0; app(15); vx=14; vy=9; run(3);
        contains("Gras\rcase à cocher\rnon cochée"); absent("Caractère");
        /* Espace : seul le nouvel état est dit, même si Word déplace un
           instant le curseur pour dessiner la croix. */
        outn[0]=0; app(57); vx=0; vy=0; run(1); put(14,9,"X",0x70); vx=14; vy=9; run(4);
        contains("cochée"); absent("Gras"); absent("non"); absent("Caractère");
        outn[0]=0; app(15); vx=49; vy=11; run(3);
        contains("Position\rgroupe\rNormale\rbouton radio\rsélectionné");
        /* Flèche : autre bouton du même groupe, sans répéter le groupe. */
        outn[0]=0; nav(80); put(49,11," ",0x70); put(49,12,"\x07",0x70); vy=12; run(3);
        contains("Exposant\rbouton radio\rsélectionné"); absent("groupe");
        outn[0]=0; app(15); vx=41; vy=18; run(3); contains("OK\rbouton"); absent("radio");
        /* Retour au champ, puis frappe : écho de la lettre seulement. */
        outn[0]=0; app(15); vx=29; vy=6; run(3); contains("Police\rchamp");
        outn[0]=0; app(31); put(29,6,"s",0x70); vx=30; run(3); contains("s\r"); absent("Police");
        /* Inser+Haut : le contrôle actif, avec la boîte. */
        outn[0]=0; combo(72); contains("Caractère\rboîte de dialogue\rPolice\rchamp\rdidot as");
        /* Fermeture : la ligne du document est lue, pas l'écran restauré. */
        outn[0]=0; app(1); word_window(); put(1,3,"  Bonjour le monde.",0x17); vx=3; vy=3; run(6);
        contains("Bonjour le monde."); absent("Pg1"); absent("Caractère"); assert(!dlg_on);
        /* Boîte d'alerte : focus sur un bouton, le message est lu. */
        outn[0]=0; app(62);
        fill(16,9,63,9,0xb1,0x55); put(32,9," Microsoft Word ",0x5f);
        fill(16,10,63,15,' ',0x70); fill(64,10,65,16,' ',0x08);
        put(18,11,"Enregistrer les changements?",0x70);
        box(20,13,30,15,0x70,0,1); put(24,14,"Oui",0x70); box(33,13,43,15,0x70,0,0); put(37,14,"Non",0x70);
        vx=24; vy=14; run(4);
        contains("Microsoft Word\rboîte de dialogue\rEnregistrer les changements?\rOui\rbouton");
        /* Word dessine la boîte curseur en (0,0) : sa barre de titre n'est pas
           une surbrillance de menu. */
        app(1); word_window(); vx=3; vy=3; run(6); outn[0]=0;
        app(62); vx=0; vy=0; fill(16,9,63,9,0xb1,0x55); put(31,9," Microsoft Word ",0x5f); run(1);
        fill(16,10,63,15,' ',0x70); put(18,11,"Enregistrer les changements?",0x70);
        box(20,13,30,15,0x70,0,1); put(24,14,"Oui",0x70); vx=24; vy=14; run(4);
        contains("Microsoft Word\rboîte de dialogue"); nobyte(0xb1);
    } else if (!strcmp(argv[1],"editdialog")) {
        /* EDIT de MS-DOS : titre dans le cadre, champ encadré, liste, < OK >. */
        int y;
        for (y=1;y<=23;++y) fill(0,y,79,y,' ',0x17);
        run(10); outn[0]=0;
        box(6,2,72,21,0x70," Ouvrir ",0); fill(73,3,74,22,' ',0x08);
        put(8,4,"Nom du fichier:",0x70); box(24,3,68,5,0x70,0,0); put(25,4,"*.TXT",0x70);
        put(28,7,"Fichiers",0x70); put(57,7,"Rép./Lecteur",0x70);
        box(8,8,52,17,0x70,0,0); box(51+4,8,70,17,0x70,0,0);
        put(57,9,"DOS",0x70); put(57,10,"EPAULARD",0x70);
        put(18,20,"< OK >",0x70); put(30,20,"< Annuler >",0x70);
        app(28); vx=30; vy=4; run(4);
        contains("Ouvrir\rboîte de dialogue\rNom du fichier\rchamp\r*.TXT");
        outn[0]=0; app(15); vx=57; vy=9; run(3); contains("Rép./Lecteur\rliste\rDOS");
        outn[0]=0; nav(80); vy=10; run(3); contains("EPAULARD"); absent("liste");
        /* Flèche sans effet (bout de liste) : l'élément est redit, une seule
           fois même si EDIT ne surligne l'élément qu'au passage suivant. */
        outn[0]=0; nav(80); run(1); put(56,10," EPAULARD     ",0x07); run(3);
        contains("EPAULARD"); assert(!strstr(strstr((char *)output[0],"EPAULARD")+1,"EPAULARD"));
        /* EDIT recopie ensuite le répertoire dans le champ : pas d'écho. */
        outn[0]=0; run(5); put(25,4,"EPAULARD\\*.TXT",0x70); run(6); absent("TXT");
        outn[0]=0; app(15); vx=20; vy=20; run(3); contains("OK\rbouton");
        /* Inser+B : lecture de la boîte, sans le cadre de l'éditeur. */
        assert(left==7 && right==71 && top==3);
        /* Chemin de l'aide : champ vide, libellé deux rangées au-dessus. */
        app(1); for (y=1;y<=23;++y) fill(0,y,79,y,' ',0x17); vx=1; vy=2; run(8); outn[0]=0;
        app(24); box(12,7,66,16,0x70," Chemin de l'aide ",0); fill(67,8,68,17,' ',0x08);
        put(23,9,"Emplacement du fichier EDIT.HLP :",0x70); box(14,11,64,13,0x70,0,0);
        put(22,15,"< OK >",0x70); vx=15; vy=12; run(4);
        contains("Chemin de l'aide\rboîte de dialogue\rEmplacement du fichier EDIT.HLP\rchamp\rvide");
    } else assert(0);
    puts("scénario réussi"); return 0;
}
