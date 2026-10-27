/* Simulation minimale de l'API série Win16 pour le test du vrai braille.c. */
#ifdef WIN16_FULL
#include "win16sim.h"
#else
#ifndef TEST_WINDOWS_H
#define TEST_WINDOWS_H
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define FAR
#define TRUE 1
#define FALSE 0
#define BRAPI
typedef unsigned char BYTE;
typedef uint32_t DWORD;
typedef int BOOL;
typedef struct { int fOutxCtsFlow, fOutxDsrFlow, fInX, fOutX, fRtsDisable, fDtrDisable; } DCB;
typedef struct { int cbOutQue; } COMSTAT;
#define wsprintf sprintf
#define lstrlen(s) ((int)strlen(s))
#define lstrcpy strcpy
#define lstrcmp strcmp
static void lstrcpyn(char *dst, const char *src, int n) { snprintf(dst, n, "%s", src); }
static DWORD test_now;
static BYTE input[512], output[4096];
static int input_n, output_n, chunk = 4096, flushes;
static int GetProfileInt(const char *section, const char *key, int fallback)
{ (void)section; (void)key; (void)fallback; return 3; }
static int OpenComm(const char *name, int in, int out)
{ (void)name; (void)in; (void)out; return 3; }
static int CloseComm(int p) { (void)p; return 0; }
static int BuildCommDCB(const char *cfg, DCB *dcb) { (void)cfg; memset(dcb, 0, sizeof(*dcb)); return 0; }
static int SetCommState(DCB *dcb) { (void)dcb; return 0; }
static DWORD GetTickCount(void) { return test_now; }
static int GetCommError(int p, COMSTAT *s) { (void)p; s->cbOutQue=0; return 0; }
static int FlushComm(int p, int q) { (void)p; (void)q; flushes++; return 0; }
static int ReadComm(int p, char *buf, int max)
{ int n=input_n<max?input_n:max; (void)p; memcpy(buf,input,n); memmove(input,input+n,input_n-n); input_n-=n; return n; }
static int WriteComm(int p, const char *buf, int n)
{ (void)p; if(n>chunk)n=chunk; memcpy(output+output_n,buf,n); output_n+=n; return n; }
#endif
#endif
