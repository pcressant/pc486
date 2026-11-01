"""État de Synthé3 lu dans la mémoire d'une VM (essais, vérification de l'installation).

S3RES (DOS) : bloc RES_INFO, signature « S3RESINF », puis RES_SB, AP_STATS et la
trace des propositions confiées au moteur (synthe3/src/s3res.h, ap.h).
EPAUS3.DLL (Windows) : bloc S3W_INFO, signature « S3WININF », avec somme de contrôle
(synthe3/src/s3win.c). La trace équivaut au journal d'Apolo-emu pour l'Apollo II.
"""
import os
from pathlib import Path
import re
import struct
import tempfile


def resident(memory):
    """RES_INFO, RES_SB, AP_STATS et trace, d'après la signature S3RESINF."""
    i = memory.find(b'S3RESINF')
    if i < 0:
        return None
    v = struct.unpack_from('<12H', memory, i + 8)
    info = dict(zip('version dgroup psp dma_block base irq dma state off_sb off_link '
                    'off_stats off_trace'.split(), v))
    info['pumps'], info['busy'], info['busy_max'] = struct.unpack_from('<3I', memory, i + 32)
    (info['starts'], info['idle_stops'], info['cut_stops'], info['skipped'],
     info['stack_low'], info['clock_fault']) = struct.unpack_from('<6H', memory, i + 44)
    (info['first_buffer'], info['submit'], info['first_buffer_max'],
     info['submit_max']) = struct.unpack_from('<4I', memory, i + 56)
    (info['wait_ticks'], info['wait_max'], info['requests'],
     info['clients']) = struct.unpack_from('<4H', memory, i + 72)
    base = info['dgroup'] * 16
    sb = struct.unpack_from('<4H4H4HIHHHH', memory, base + info['off_sb'])
    info.update(flags=sb[2], played=sb[12], completed=sb[13], silent=sb[14],
                underruns=sb[15], speaking=sb[16])
    info['windows'] = struct.unpack_from('<H', memory, base + info['off_sb'] + 42)[0]
    st = struct.unpack_from('<IIHHHHhhhh', memory, base + info['off_stats'])
    info.update(propositions=st[0], samples=st[1], cuts=st[2], marks=st[3], lost_marks=st[4],
                refused=st[5], rate=st[6], pitch=st[7])
    link = struct.unpack_from('<7H', memory, base + info['off_link'])
    info.update(zip(('link_head', 'link_tail', 'link_cuts', 'link_cut_at', 'echo_head', 'echo_tail',
                     'dropped'), link))
    trace = memory[base + info['off_trace']:base + info['off_trace'] + 512]
    info['trace'] = trace.split(b'\0')[0].decode('latin1')
    info['segment'] = base // 16
    info['info_off'] = i - base
    return info


FIELDS = ('sig version rate open_error opened pumps played written busy_ms busy_max '
          'underruns resets buffers queued propositions samples cuts marks lost_marks '
          'refused first_buffer submit first_buffer_max submit_max requests opens closes '
          'open_fails dropped skipped').split()
LAYOUT = '<8s4H5I4H2I4H5I4HI'


def win_info(memory):
    """Copie valide la plus récente du bloc S3W_INFO d'EPAUS3.DLL (somme de contrôle :
    une page physique peut garder une ancienne copie d'un segment déplacé)."""
    best = None
    size = struct.calcsize(LAYOUT) + 200
    for m in re.finditer(b'S3WININF', memory):
        raw = memory[m.start():m.start() + size + 2]
        if len(raw) < size + 2:
            continue
        words = struct.unpack_from(f'<{size // 2}H', raw)
        if sum(words) & 0xffff != struct.unpack_from('<H', raw, size)[0]:
            continue
        info = dict(zip(FIELDS, struct.unpack_from(LAYOUT, raw)))
        info['trace'] = raw[struct.calcsize(LAYOUT):size].split(b'\0')[0].decode('latin1')
        if best is None or info['pumps'] > best['pumps']:
            best = info
    return best


def linear_memory(q, size=0x100000):
    """Mémoire linéaire (QMP memsave) : S3RES chargé par LH est dans un UMB paginé
    par EMM386, que pmemsave (adresses physiques) ne montre pas."""
    fd, path = tempfile.mkstemp(prefix='pc486-s3-', suffix='.mem')
    os.close(fd)
    try:
        q.cmd('memsave', val=0, size=size, filename=path)
        return Path(path).read_bytes()
    finally:
        os.unlink(path)
