#include <assert.h>
#include "../../epaulard/src/braille.c"

static void ticks(int n) { while(n--) { test_now+=100; br_tick(); } }
static void response_byte(BYTE b) { input[input_n++]=b; ticks(1); }
int main(void)
{
    int i;
    br_open();
    assert(br_enabled());
    ticks(2);
    assert(output_n==3 && output[0]==0x1b && output[1]==0x15 && output[2]==1);
    /* Réponse fragmentée : largeur, identité ignorée, touche 27 (ESC brut). */
    response_byte(0x1b); response_byte(1); response_byte(40);
    assert(width==40);
    br_text("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", 41, TRUE);
    assert(br_offset()==40);
    br_pan(-1); assert(br_offset()==0);
    br_text("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", 41, FALSE);
    assert(br_offset()==0); /* le suivi ne défait pas le défilement manuel */
    response_byte(0x1b); response_byte(0x27); response_byte(0x1b);
    assert(br_key()==BR_ROUTE+26);
    response_byte(0x1b); response_byte(0x24); response_byte(2);
    assert(br_key()==0);
    response_byte(0x1b); response_byte(0x24); response_byte(0);
    assert(br_key()==BR_BACK);
    /* Deux commandes dans une lecture : aucune ne doit être perdue. */
    input[0]=0x1b; input[1]=0x27; input[2]=1;
    input[3]=0x1b; input[4]=0x27; input[5]=2; input_n=6;
    ticks(1);
    assert(br_key()==BR_ROUTE && br_key()==BR_ROUTE+1 && br_key()==0);
    /* Trame interrompue : abandon après 500 ms, puis nouvelle largeur. */
    response_byte(0x1b); response_byte(0x84); ticks(6);
    response_byte(0x1b); response_byte(1); response_byte(80);
    assert(width==80);
    /* Sélection : points 7 et 8 sous chaque caractère sélectionné, jusqu'à la fin. */
    br_text("abc def", -1, FALSE); br_select(4, 7); ticks(3);
    assert(sent[3]==0 && sent[4]==(br_fr['d']|0xc0) && sent[6]==(br_fr['f']|0xc0));
    assert(sent[7]==0 && sent[0]==br_fr['a']);
    br_select(-1, -1); ticks(3);
    assert(sent[4]==br_fr['d'] && sent[6]==br_fr['f']);
    /* CP1252 : accents, majuscule et guillemets français. */
    assert(br_fr['a']==1 && br_fr['A']==0x41);
    assert(br_fr[0xe9]==0x3f && br_fr[0xe8]==0x2e && br_fr[0xe7]==0x2f);
    assert(br_fr[0xab]==0xb6 && br_fr[0xbb]==0x76);
    /* Écriture partielle : conserver exactement chaque octet de la trame. */
    tx_pos=tx_n=0; output_n=0; chunk=1;
    for(i=0;i<80;i++)cells[i]=0x1b;
    queue(1,cells,80);
    for(i=0;i<162;i++)br_tick();
    assert(output_n==162 && tx_pos==162);
    for(i=2;i<162;i++)assert(output[i]==0x1b);
    /* File bloquée : vider et relancer la détection. */
    chunk=0; queue(1,cells,1); ticks(21);
    assert(flushes==1);
    br_close(); assert(!br_enabled());
    return 0;
}
