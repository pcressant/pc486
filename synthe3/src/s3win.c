/* SPDX-License-Identifier: GPL-3.0-or-later
 * EPAUS3.DLL : Synthé3 pour Épaulard sous Windows 3.1x, sortie waveOut.
 *
 * Même moteur et même Apollo local que S3RES (DOS) : seule la sortie change. La
 * DLL ne lit pas la mémoire d'un résident DOS et ne touche pas la Sound Blaster :
 * elle passe par le pilote audio de Windows (MMSYSTEM).
 *
 * Aucun rappel à l'interruption (CALLBACK_NULL) : sous Windows 3.1 un tel rappel
 * doit être en code fixe et ne peut presque rien appeler. La pompe, appelée par la
 * boucle de messages d'EPAULARD.EXE (ou par la minuterie d'une boîte modale
 * système, hook.c), relève les tampons rendus (WHDR_DONE), libère les échos @E
 * de ce qui a été joué et calcule au plus un tampon de 2048 échantillons par appel.
 * Rien n'est calculé dans un crochet USER/GDI : S3Write ne fait que déposer des
 * octets. Exige un 386 (noyaux en assembleur 386, comme sous DOS).
 */
#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include "s3.h"
#include "ap.h"

#ifndef WEP_SYSTEM_EXIT
#define WEP_SYSTEM_EXIT 1
#endif
/* Tampons d'au moins 2048 octets : SNDBLST2.DRV (Sound Blaster 1.5/2.0 de Windows
   3.1) perd environ 2048 octets au début de chaque lecture si le tampon soumis est
   plus petit (mesuré sous 86Box : 200 ms de parole manquants par phrase avec 512 ou
   1024, « fchier », « ption » ; aucune perte avec 2048 ou 4096). */
#ifndef W_BUFS
#define W_BUFS 4                /* ~0,8 s en file au plus */
#endif
#ifndef W_BANK
#define W_BANK 2048             /* échantillons du moteur par tampon */
#endif
#define W_BYTES (W_BANK + W_BANK / 8 + 16)  /* 512 à 11025 Hz : 565 octets */
#define W_IDLE_MS 1000          /* silence avant de rendre le périphérique */
#define W_RETRY_MS 100          /* périphérique pris : nouvel essai */
#define W_WAIT_MS 3000          /* au-delà, restitution à vide au rythme réel */

/* Diagnostic lu par les essais (signature « S3WININF »), copié à chaque pompe. */
typedef struct {
    char sig[8];
    WORD version, rate, open_error, opened;
    DWORD pumps, played, written, busy_ms, busy_max;
    WORD underruns, resets, buffers, queued;
    DWORD propositions, samples;
    WORD cuts, marks, lost_marks, refused;
    DWORD first_buffer, submit, first_buffer_max, submit_max, requests;
    WORD opens, closes, open_fails, dropped;
    DWORD skipped;              /* échantillons « joués » à vide, périphérique pris */
    char trace[200];
    WORD check;                 /* somme des mots précédents : page physique contiguë */
} S3W_INFO;

static char info_area[1024];
static S3W_INFO FAR *info;
static HWAVEOUT wave;
static WAVEHDR hdr[W_BUFS];
static HGLOBAL mem[W_BUFS];
static LPSTR mem_ptr[W_BUFS];   /* verrouillés de S3Open à S3Close */
static unsigned used[W_BUFS], q_head, q_n;    /* tampons en file : q_head.. */
static short pcm[W_BANK];
static unsigned staged;
static DWORD played, phase, request_at, idle_since, wait_since, last_try, vdue, vlast;
static short last;
static int out_rate, requested, measured_first;
static HTASK owner;
static HINSTANCE self;

void FAR PASCAL __export S3Close(void);

static void publish(void)
{
    WORD FAR *w = (WORD FAR *)info;
    WORD sum = 0;
    unsigned i, n = (unsigned)((char FAR *)&info->check - (char FAR *)info) / 2;
    int len = lstrlen(ap_trace), from = len > 199 ? len - 199 : 0;
    info->played = played;
    info->queued = q_n;
    info->dropped = ap_link.dropped;
    info->opened = wave != 0;
    info->propositions = ap_stats.propositions; info->samples = ap_stats.samples;
    info->cuts = ap_stats.cuts; info->marks = ap_stats.marks;
    info->lost_marks = ap_stats.lost_marks; info->refused = ap_stats.refused;
    lstrcpyn(info->trace, ap_trace + from, sizeof(info->trace));
    for (i = 0; i < n; i++) sum += w[i];
    info->check = sum;
}

/* PCM 16 bits -> 8 bits non signé ; 11025 Hz par interpolation linéaire si le
   pilote refuse 10 kHz. Renvoie le nombre d'octets écrits. */
static unsigned convert(BYTE FAR *out, unsigned n)
{
    unsigned i, k = 0;
    long a, b, s;
    if (out_rate == S3_HZ) {
        for (i = 0; i < n; i++) out[i] = (BYTE)(((unsigned)pcm[i] >> 8) ^ 0x80);
        return n;
    }
    /* phase : position de lecture en 1/65536 d'échantillon, relative au bloc ;
       le pas est 10000/11025. last : dernier échantillon du bloc précédent. */
    while (k < W_BYTES && (phase >> 16) < n) {
        i = (unsigned)(phase >> 16);
        a = i ? pcm[i - 1] : last;
        b = pcm[i];
        s = a + ((b - a) * (long)(phase & 0xffff) >> 16);
        out[k++] = (BYTE)(((unsigned)(short)s >> 8) ^ 0x80);
        phase += 59443UL;       /* 65536 × 10000 / 11025 */
    }
    phase -= (DWORD)n << 16;
    last = pcm[n - 1];
    return k;
}

/* Parole en attente ou en cours, quel que soit l'état du périphérique. */
static BOOL pending(void)
{
    return q_n || staged || vdue || ap_speaking() || ap_link.head != ap_link.tail;
}

static void dev_close(void)
{
    unsigned i;
    if (!wave) return;
    waveOutReset(wave);
    for (i = 0; i < W_BUFS; i++)
        if (hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wave, &hdr[i], sizeof(WAVEHDR));
    waveOutClose(wave);
    wave = 0;
    q_head = q_n = 0;
    info->closes++;
}

/* Le périphérique n'est tenu que pendant la parole : les autres programmes (sons du
   système, Magnétophone) y ont accès le reste du temps. 0, ou erreur MMSYSTEM. */
static UINT dev_open(void)
{
    PCMWAVEFORMAT f;
    unsigned i;
    UINT err;
    f.wf.wFormatTag = WAVE_FORMAT_PCM;
    f.wf.nChannels = 1;
    f.wf.nBlockAlign = 1;
    f.wBitsPerSample = 8;
    f.wf.nSamplesPerSec = f.wf.nAvgBytesPerSec = out_rate;
    err = waveOutOpen(&wave, WAVE_MAPPER, (LPWAVEFORMAT)&f, 0, 0, 0);
    if (err == WAVERR_BADFORMAT && out_rate == S3_HZ) {
        out_rate = 11025;       /* pilote limité aux fréquences usuelles */
        f.wf.nSamplesPerSec = f.wf.nAvgBytesPerSec = out_rate;
        err = waveOutOpen(&wave, WAVE_MAPPER, (LPWAVEFORMAT)&f, 0, 0, 0);
    }
    info->rate = out_rate;
    info->open_error = err;
    if (err) {
        wave = 0;
        info->open_fails++;
        return err;
    }
    for (i = 0; i < W_BUFS; i++) {
        hdr[i].lpData = (LPSTR)mem_ptr[i];
        hdr[i].dwBufferLength = W_BYTES;
        hdr[i].dwFlags = 0;
        if (waveOutPrepareHeader(wave, &hdr[i], sizeof(WAVEHDR))) break;
        hdr[i].dwFlags |= WHDR_DONE;
    }
    info->buffers = i;
    if (!i) {
        dev_close();
        info->open_fails++;
        return MMSYSERR_NOMEM;
    }
    phase = 0; last = 0;
    info->opens++;
    return 0;
}

static void reset_out(void)
{
    unsigned i;
    if (wave) waveOutReset(wave);
    for (i = 0; i < W_BUFS; i++) used[i] = 0;
    q_head = q_n = 0;
    staged = 0; played = 0; phase = 0; last = 0; vdue = 0;
    info->resets++;
}

static BOOL submit(unsigned n)
{
    unsigned i = (q_head + q_n) % W_BUFS;
    hdr[i].dwBufferLength = convert((BYTE FAR *)hdr[i].lpData, n);
    hdr[i].dwFlags &= ~WHDR_DONE;
    if (waveOutWrite(wave, &hdr[i], sizeof(WAVEHDR))) return FALSE;
    used[i] = n;
    q_n++;
    info->written++;
    return TRUE;
}

static void measured(DWORD FAR *value, DWORD FAR *max)
{
    *value = timeGetTime() - request_at;
    if (*value > *max) *max = *value;
}

/* Prépare la sortie. 0 si possible ; 1006 sans pilote audio (MMSYSERR_NODRIVER),
   2000 sans mémoire pour les tampons. Le périphérique n'est ouvert qu'à la parole. */
UINT FAR PASCAL __export S3Open(void)
{
    unsigned i;
    if (mem[0]) return 0;
    if (!waveOutGetNumDevs()) return 1000 + MMSYSERR_NODRIVER;
    owner = GetCurrentTask();
    s3_init();
    ap_init();
    out_rate = S3_HZ;
    for (i = 0; i < W_BUFS; i++) {
        mem[i] = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, W_BYTES);
        mem_ptr[i] = mem[i] ? (LPSTR)GlobalLock(mem[i]) : NULL;
        if (!mem_ptr[i]) { S3Close(); return 2000 + i; }
    }
    publish();
    return 0;
}

void FAR PASCAL __export S3Close(void)
{
    unsigned i;
    dev_close();
    for (i = 0; i < W_BUFS; i++)
        if (mem[i]) {
            if (mem_ptr[i]) GlobalUnlock(mem[i]);
            GlobalFree(mem[i]);
            mem[i] = 0;
            mem_ptr[i] = NULL;
        }
    publish();
}

/* Octets Apollo II (CP850) d'Épaulard ou, en relais, d'Épaudos. Ctrl-X : silence
   immédiat si l'appel vient de la tâche propriétaire ; sinon (boîte modale système,
   autre tâche) à la pompe suivante. Renvoie le nombre d'octets acceptés. */
int FAR PASCAL __export S3Write(LPCSTR s, int n)
{
    int i;
    BOOL cut = FALSE;
    for (i = 0; i < n; i++) {
        if (s[i] == 0x18) {
            cut = TRUE;
            if (wave && GetCurrentTask() == owner) waveOutReset(wave);
        } else if (!requested && (cut || !pending())) {
            /* demande partie d'un silence : mesures first_buffer et submit */
            request_at = timeGetTime(); requested = 1; measured_first = 0;
        }
        if (!ap_put((BYTE)s[i])) break;
    }
    return i;
}

/* Échos @E rendus quand la restitution les atteint. */
int FAR PASCAL __export S3Read(LPSTR buf, int n)
{
    int i, c;
    for (i = 0; i < n && (c = ap_echo()) >= 0; i++) buf[i] = (char)c;
    return i;
}

/* Restitution à vide : périphérique pris par un autre programme. Le texte avance au
   rythme réel et ses échos reviennent, sans son (comme S3RES sans carte). */
static void play_void(DWORD now)
{
    DWORD credit = (now - vlast) * (S3_HZ / 1000);
    unsigned n;
    vlast = now;
    if (credit > vdue) credit = vdue;
    played += credit;
    vdue -= credit;
    if (staged) { vdue += staged; info->skipped += staged; staged = 0; }
    if (vdue < 2 * W_BANK && (n = ap_render(pcm, W_BANK)) != 0) {
        vdue += n;
        info->skipped += n;
        if (requested) { requested = 0; info->requests++; }
    }
}

/* Un pas : TRUE s'il reste à calculer et qu'un tampon est libre (rappeler bientôt,
   après avoir laissé passer les messages en attente). */
BOOL FAR PASCAL __export S3Pump(void)
{
    DWORD t0 = timeGetTime(), t;
    unsigned i;
    BOOL more;
    if (!mem[0]) return FALSE;
    info->pumps++;
    if (ap_cut()) reset_out();
    if (!wave && pending() && t0 - last_try >= W_RETRY_MS) {
        last_try = t0;
        if (!wait_since) wait_since = t0;
        if (!dev_open()) wait_since = 0;
    }
    if (wave) {
        while (q_n && (hdr[q_head].dwFlags & WHDR_DONE)) {
            i = q_head;
            played += used[i]; used[i] = 0;
            q_head = (q_head + 1) % W_BUFS; q_n--;
            if (!q_n && (staged || ap_speaking())) info->underruns++;
        }
        if (vdue) { played += vdue; vdue = 0; }     /* fin d'une restitution à vide */
        if (q_n < info->buffers) {
            if (!staged) {
                staged = ap_render(pcm, W_BANK);
                if (staged && requested && !measured_first) {
                    measured(&info->first_buffer, &info->first_buffer_max);
                    measured_first = 1;
                }
            }
            if (staged && submit(staged)) {
                staged = 0;
                if (requested) {
                    measured(&info->submit, &info->submit_max);
                    requested = 0; info->requests++;
                }
            }
        }
        if (pending()) idle_since = 0;
        else if (!idle_since) idle_since = t0;
        else if (t0 - idle_since >= W_IDLE_MS) { dev_close(); idle_since = 0; }
    } else if (wait_since && t0 - wait_since >= W_WAIT_MS) {
        if (!vdue && !staged) vlast = t0;
        play_void(t0);
    } else if (!pending()) {
        wait_since = 0;
    }
    ap_played(played);
    more = wave ? q_n < info->buffers && (staged || ap_speaking()) : FALSE;
    t = timeGetTime() - t0;
    info->busy_ms += t;
    if (t > info->busy_max) info->busy_max = t;
    publish();
    return more;
}

/* Parole en file ou en cours : EPAULARD.EXE garde sa minuterie et attend avant de
   fermer ; le périphérique encore ouvert compte aussi (fermeture après 1 s). */
BOOL FAR PASCAL __export S3Busy(void)
{
    return pending() || wave != 0;
}

/* Compteurs (S3W_INFO), pour S3WPROBE.EXE. */
void FAR * FAR PASCAL __export S3Info(void)
{
    return info;
}

/* Base linéaire d'un sélecteur (DPMI 0006h, modes standard et 386 étendu). */
static DWORD sel_base(WORD sel)
{
    WORD hi = 0, lo = 0;
    _asm {
        mov bx, sel
        mov ax, 6
        int 31h
        mov hi, cx
        mov lo, dx
    }
    return (DWORD)hi << 16 | lo;
}

int FAR PASCAL LibMain(HINSTANCE h, WORD ds, WORD heap, LPSTR cmd)
{
    DWORD base;
    unsigned off;
    (void)ds; (void)heap; (void)cmd;
    self = h;
    /* Diagnostic dans une seule page : les essais le lisent en mémoire physique. */
    base = sel_base(SELECTOROF((LPSTR)info_area)) + OFFSETOF((LPSTR)info_area);
    off = (unsigned)(4096 - (base & 4095)) < sizeof(S3W_INFO) ? (unsigned)(4096 - (base & 4095)) : 0;
    info = (S3W_INFO FAR *)(info_area + off);
    _fmemset(info, 0, sizeof(*info));
    _fmemcpy(info->sig, "S3WININF", 8);
    info->version = 0x0100;
    return 1;
}

int FAR PASCAL __export WEP(int code)
{
    if (code != WEP_SYSTEM_EXIT) S3Close();  /* fin de Windows : pilotes déjà arrêtés */
    return 1;
}
