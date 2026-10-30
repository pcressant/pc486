# SPDX-License-Identifier: GPL-3.0-or-later
"""Proxy HTTP/1.0 de la VM : pages d'aujourd'hui converties en HTML 3.2.

Le navigateur d'époque (proxy HTTP réglé sur 10.0.2.2:8080) demande http://site/… ;
le proxy essaie HTTPS puis HTTP, garde les cookies, convertit le HTML (html32.py),
coupe les pages trop longues et laisse passer tels quels les fichiers (GIF, JPEG,
ZIP…). Les pages http://pc486/ sont locales : accueil, recherche, suites de pages.
Chromium sans fenêtre, s'il est présent, peut exécuter le JavaScript d'une page
avant conversion (lien « Version JavaScript ») ; il n'est jamais nécessaire.
"""
from email.message import Message
import codecs
from http.server import BaseHTTPRequestHandler
import gzip
import hashlib
import http.cookiejar
import os
from pathlib import Path
import shutil
import socketserver
import ssl
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import zlib
from collections import OrderedDict
from contextlib import suppress
from urllib.parse import (parse_qsl, quote, unquote_to_bytes, urlencode, urlsplit,
                          urlunsplit)

from . import debit, html32, tampon

LOCAL = 'pc486'
HOME = f'http://{LOCAL}/'
AGENT = 'Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0'
MAX_BODY = 64 * 1024 * 1024
TIMEOUT = 30
PAGE_LIMIT = int(os.environ.get('PC486_PAGE', '60000'))
NUMBERS = os.environ.get('PC486_LIENS', '1') != '0'   # liens numérotés, « Aller au lien n° »
SEARCH = 'http://html.duckduckgo.com/html/'


def log(msg):
    print(time.strftime('%H:%M:%S'), msg, file=sys.stderr, flush=True)


def reencode(raw):
    """Octets d'URL ou de formulaire du navigateur d'époque (windows-1252) en UTF-8.
    Les adresses écrites par le proxy sont déjà en UTF-8 encodé % : laissées."""
    try:
        raw.decode('utf-8')
        return raw
    except UnicodeDecodeError:
        return raw.decode('cp1252', 'replace').encode('utf-8')


def fix_query(query):
    if not query:
        return query
    pairs = []
    for part in query.split('&'):
        k, eq, v = part.partition('=')
        k = quote(reencode(unquote_to_bytes(k.replace('+', ' '))), safe='')
        v = quote(reencode(unquote_to_bytes(v.replace('+', ' '))), safe='')
        pairs.append(k + eq + v)
    return '&'.join(pairs)


def fix_path(path):
    raw = unquote_to_bytes(path)
    if raw.isascii():
        return path
    return quote(reencode(raw), safe='/%;:@&=+$,!~*\'()')


class Cache:
    """Pages converties récentes (suites de pages longues, liens numérotés)."""

    def __init__(self, size=100):
        self.items, self.size, self.lock = OrderedDict(), size, threading.Lock()

    def put(self, page, source):
        key = hashlib.sha1(f'{source}{time.time()}'.encode()).hexdigest()[:12]
        with self.lock:
            self.items[key] = (page, source)
            while len(self.items) > self.size:
                self.items.popitem(last=False)
        return key

    def get(self, key):
        with self.lock:
            return self.items.get(key)


class Documents:
    """Pages HTML envoyées à Netscape, par adresse (tampon.key), et adresses déjà
    visitées : de quoi répondre à http://pc486/tampon?url=… pour la page affichée,
    même revenue de l'historique de Netscape. Les visites sont gardées dans
    `visites.txt` du dossier de données, s'il est donné."""

    def __init__(self, size=100, data=None):
        self.items, self.size, self.lock = OrderedDict(), size, threading.Lock()
        self.path = os.path.join(data, 'visites.txt') if data else None
        self.visited = set()
        if self.path:
            with suppress(OSError):
                with open(self.path, encoding='utf-8') as f:
                    self.visited = {line.strip() for line in f if line.strip()}

    def put(self, url, doc):
        k = tampon.key(url)
        with self.lock:
            self.items[k] = doc
            self.items.move_to_end(k)
            while len(self.items) > self.size:
                self.items.popitem(last=False)
        self.visit(url)

    def visit(self, url):
        k = tampon.key(url)
        with self.lock:
            if k in self.visited:
                return
            self.visited.add(k)
            if self.path:
                with suppress(OSError):
                    with open(self.path, 'a', encoding='utf-8') as f:
                        f.write(k + '\n')

    def tampon(self, url):
        with self.lock:
            doc = self.items.get(tampon.key(url))
            visited = frozenset(self.visited)
        if doc is None:
            return None
        # mêmes caractères que ceux reçus par Netscape (windows-1252, translittérés) :
        # les positions du tampon ne dépendent pas de la translittération
        return tampon.Tampon(html32.encode(doc).decode('cp1252'), url).serialize(visited)


class Fetcher:
    def __init__(self):
        self.jar = http.cookiejar.CookieJar()
        ctx = ssl.create_default_context()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar),
                                                  urllib.request.HTTPSHandler(context=ctx))
        self.http_only = set()

    def fetch(self, method, url, body=None, ctype=None):
        """(statut, adresse finale, en-têtes, octets) ; HTTPS d'abord pour http://."""
        parts = urlsplit(url)
        candidates = [url]
        if parts.scheme == 'http' and parts.hostname not in self.http_only and parts.port in (None, 80):
            candidates.insert(0, urlunsplit(('https', parts.hostname if parts.port is None else parts.netloc)
                                            + tuple(parts[2:])))
        error = None
        for i, target in enumerate(candidates):
            req = urllib.request.Request(target, data=body, method=method)
            req.add_header('User-Agent', AGENT)
            req.add_header('Accept', 'text/html,application/xhtml+xml,*/*;q=0.8')
            req.add_header('Accept-Language', 'fr-FR,fr;q=0.9,en;q=0.5')
            req.add_header('Accept-Encoding', 'gzip, deflate')
            if ctype:
                req.add_header('Content-Type', ctype)
            try:
                resp = self.opener.open(req, timeout=TIMEOUT)
            except urllib.error.HTTPError as e:
                resp = e
            except (urllib.error.URLError, OSError, ValueError) as e:
                error = e
                if i == 0 and len(candidates) > 1:
                    reason = getattr(e, 'reason', e)
                    if isinstance(reason, (ssl.SSLError, ConnectionRefusedError, ssl.CertificateError)):
                        self.http_only.add(parts.hostname)
                    continue
                raise
            with resp:
                data = resp.read(MAX_BODY + 1)
                status = resp.status if hasattr(resp, 'status') else resp.code
                headers = resp.headers
                final = resp.geturl()
            if len(data) > MAX_BODY:
                raise OSError('fichier trop gros pour le proxy (plus de 64 Mo)')
            enc = (headers.get('Content-Encoding') or '').lower()
            if enc == 'gzip':
                data = gzip.decompress(data)
            elif enc == 'deflate':
                try:
                    data = zlib.decompress(data)
                except zlib.error:
                    data = zlib.decompress(data, -zlib.MAX_WBITS)
            return status, final, headers, data
        raise error


def charset_of(headers, data):
    m = Message()
    m['content-type'] = headers.get('Content-Type', '')
    cs = m.get_param('charset')
    if not cs:
        head = data[:4096].decode('ascii', 'replace').lower()
        i = head.find('charset=')
        if i >= 0:
            cs = head[i + 8:].strip('"\' ').split('"')[0].split("'")[0].split(';')[0].split('>')[0].strip()
    try:
        return codecs.lookup(cs).name if cs else 'utf-8'
    except LookupError:
        return 'utf-8'


def chromium():
    """Chromium sans fenêtre, facultatif : PC486_CHROMIUM ou le PATH."""
    path = os.environ.get('PC486_CHROMIUM')
    if path:
        return path if os.access(path, os.X_OK) else None
    for name in ('chromium', 'chromium-browser', 'google-chrome-stable', 'google-chrome'):
        found = shutil.which(name)
        if found:
            return found
    return None


def render_js(url):
    exe = chromium()
    if not exe:
        raise OSError('Chromium absent : la version JavaScript est indisponible')
    out = subprocess.run([exe, '--headless=new', '--disable-gpu', '--no-first-run',
                          '--virtual-time-budget=8000', f'--user-agent={AGENT}',
                          '--lang=fr-FR', '--dump-dom', url],
                         capture_output=True, timeout=90)
    if out.returncode or not out.stdout.strip():
        raise OSError('Chromium n’a pas rendu la page (code %d)' % out.returncode)
    return out.stdout.decode('utf-8', 'replace')


class Proxy:
    def __init__(self, data=None):
        self.fetcher = Fetcher()
        self.cache = Cache()
        self.documents = Documents(data=data)

    # --- pages
    def page(self, page, source, n=1, key=None):
        total = len(page.parts)
        if (total > 1 or page.links) and key is None:
            key = self.cache.put(page, source)
        body = page.parts[n - 1]
        nav = []
        if total > 1:
            nav.append(f'Page {n} sur {total}')
            if n > 1:
                nav.append(f'<A HREF="http://{LOCAL}/suite?id={key}&amp;n={n - 1}">Page précédente</A>')
            if n < total:
                nav.append(f'<A HREF="http://{LOCAL}/suite?id={key}&amp;n={n + 1}">Page suivante</A>')
        top = (' | '.join(nav) + '<BR>\n') if nav and n > 1 else ''
        if page.links:
            # Premier champ atteint par Tab : numéro d'un lien (« Accueil [3] »)
            top = (f'<FORM ACTION="http://{LOCAL}/lien" METHOD=GET>Aller au lien n° '
                   f'<INPUT TYPE=TEXT NAME="n" SIZE=4><INPUT TYPE=HIDDEN NAME="id" VALUE="{key}">'
                   f'<INPUT TYPE=SUBMIT VALUE="Aller"></FORM>\n') + top
        refresh = (f'<P><A HREF={html32.attr(page.refresh)}>Redirection : suivre le lien</A></P>\n'
                   if page.refresh else '')
        foot = ['<A HREF="http://%s/">Accueil pc486</A>' % LOCAL]
        if source != HOME:
            if chromium() and source.startswith(('http://', 'https://')):
                foot.append(f'<A HREF="http://{LOCAL}/js?url={quote(source, safe="")}">Version JavaScript</A>')
            foot.append('Adresse d’origine : ' + html32.esc(source))
        tail = '<HR>\n' + (' | '.join(nav) + '<BR>\n' if nav else '') + ' | '.join(foot)
        return html32.document(page.title or source, top + refresh + body + tail)

    @staticmethod
    def message(title, text):
        return html32.document(title, f'<H1>{html32.esc(title)}</H1>\n<P>{text}</P>\n'
                               f'<HR><A HREF="http://{LOCAL}/">Accueil pc486</A>')

    def home(self):
        js = ('<P>Version JavaScript : Chromium est disponible ; un lien en bas de chaque page '
              'la propose.</P>\n' if chromium() else '')
        markup = f'''<TITLE>Accueil pc486</TITLE><H1>Accueil pc486</H1>
<P>Les pages d’aujourd’hui sont simplifiées par le proxy de l’hôte : titres, liens,
listes, tableaux et formulaires, sans scripts ni images.</P>
<FORM ACTION="{SEARCH}" METHOD=GET>Recherche sur le web : <INPUT TYPE=TEXT NAME="q" SIZE=40>
<INPUT TYPE=SUBMIT VALUE="Chercher"></FORM>
<H2>Quelques adresses</H2>
<UL>
<LI><A HREF="http://fr.wikipedia.org/">Wikipédia en français</A>
<LI><A HREF="http://fr.m.wiktionary.org/">Wiktionnaire</A>
<LI><A HREF="http://www.win3x.org/">Win3x.Org</A>
<LI><A HREF="http://lite.duckduckgo.com/lite/">DuckDuckGo (version légère)</A>
</UL>
{js}<P>Les pages longues sont coupées : « Page suivante » en bas de page.
Les fichiers (ZIP, EXE…) passent tels quels. Les adresses https:// s’écrivent http://.
Les liens sont numérotés : taper le numéro dans « Aller au lien n° », en haut de chaque
page (touche Tab).</P>'''
        # Même conversion qu'une page du web : liens numérotés, « Aller au lien n° »
        return self.page(html32.convert(markup, HOME, PAGE_LIMIT, NUMBERS), HOME)

    def local(self, path, query):
        args = dict(parse_qsl(query))
        if path in ('', '/'):
            return 200, 'text/html', self.home()
        if path == '/tampon':
            # mode navigation d'Épaulard : structure de la page affichée par Netscape
            data = self.documents.tampon(args.get('url', ''))
            if data is None:
                return 404, 'text/plain', b'PC486-TAMPON 0\nabsent\n', {'Cache-Control': 'no-cache'}
            return 200, 'text/plain', data, {'Cache-Control': 'no-cache'}
        if path == '/essai':
            # page de référence du mode navigation d'Épaulard (essai.html)
            markup = (Path(__file__).parent / 'essai.html').read_text(encoding='utf-8')
            return 200, 'text/html', self.page(html32.convert(markup, f'http://{LOCAL}/essai',
                                                              PAGE_LIMIT, NUMBERS), f'http://{LOCAL}/essai')
        if path == '/suite':
            hit = self.cache.get(args.get('id', ''))
            try:
                n = int(args.get('n', '1'))
            except ValueError:
                n = 0
            if not hit or not 1 <= n <= len(hit[0].parts):
                return 404, 'text/html', self.message('Page expirée',
                                                      'Cette suite de page n’est plus en mémoire : '
                                                      'recharger la page d’origine.')
            return 200, 'text/html', self.page(hit[0], hit[1], n, args['id'])
        if path == '/lien':
            hit = self.cache.get(args.get('id', ''))
            try:
                n = int(args.get('n', '').strip().strip('[]'))
            except ValueError:
                n = 0
            if not hit:
                return 404, 'text/html', self.message('Page expirée',
                                                      'Cette page n’est plus en mémoire : la recharger.')
            if not 1 <= n <= len(hit[0].links):
                return 404, 'text/html', self.message('Lien inconnu',
                                                      f'Pas de lien n° {html32.esc(args.get("n", ""))} '
                                                      f'sur cette page ({len(hit[0].links)} lien'
                                                      f'{"s" if len(hit[0].links) > 1 else ""}).')
            target = hit[0].links[n - 1]
            return 302, 'text/html', self.message('Lien', f'<A HREF={html32.attr(target)}>'
                                                          f'{html32.esc(target)}</A>'), {'Location': target}
        if path == '/js' and args.get('url'):
            url = html32.proxied(args['url']) or args['url']
            target = urlunsplit(('https',) + tuple(urlsplit(url)[1:]))
            markup = render_js(target)
            return 200, 'text/html', self.page(html32.convert(markup, target, PAGE_LIMIT, NUMBERS), target)
        return 404, 'text/html', self.message('Page inconnue', 'Adresse locale inconnue du proxy pc486.')

    def handle(self, method, url, body, ctype):
        """(statut, type, document texte ou octets, en-têtes en plus). Page HTML
        envoyée : gardée pour le tampon d'Épaulard."""
        result = self.respond(method, url, body, ctype)
        status, kind, doc = result[:3]
        if status < 400 and kind == 'text/html' and isinstance(doc, str) and method != 'HEAD':
            self.documents.put(url, doc)
        return result

    def respond(self, method, url, body, ctype):
        parts = urlsplit(url)
        host = (parts.hostname or '').lower()
        if host in (LOCAL, f'{LOCAL}.local'):
            result = self.local(parts.path, parts.query)
            return result if len(result) == 4 else result + ({},)
        url = urlunsplit((parts.scheme, parts.netloc, fix_path(parts.path), fix_query(parts.query), ''))
        url = html32.unwrap(url)
        if body is not None and (ctype or '').startswith('application/x-www-form-urlencoded'):
            body = fix_query(body.decode('latin-1')).encode('ascii')
        status, final, headers, data = self.fetcher.fetch(method, url, body, ctype)
        kind = (headers.get('Content-Type') or 'application/octet-stream').split(';')[0].strip().lower()
        if kind in ('text/html', 'application/xhtml+xml'):
            markup = data.decode(charset_of(headers, data), 'replace')
            return status, 'text/html', self.page(html32.convert(markup, final, PAGE_LIMIT, NUMBERS), final), {}
        if kind == 'text/plain':
            text = data.decode(charset_of(headers, data), 'replace')
            title = urlsplit(final).path.rsplit('/', 1)[-1] or final
            return status, 'text/html', html32.document(title, '<PRE>' + html32.esc(text) + '</PRE>'), {}
        extra = {}
        if headers.get('Content-Disposition'):
            extra['Content-Disposition'] = headers['Content-Disposition']
        return status, kind, data, extra


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'
    server_version = 'pc486-proxy/1.0'

    def setup(self):
        super().setup()
        self.wfile = debit.Limited(self.wfile)

    def address_string(self):
        return 'vm'

    def log_message(self, fmt, *args):
        pass

    def respond(self, status, kind, doc, extra=None):
        data = html32.encode(doc) if isinstance(doc, str) else doc
        if isinstance(doc, str):
            kind += '; charset=iso-8859-1'
        self.send_response(status)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Connection', 'close')
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != 'HEAD':
            self.wfile.write(data)
        return len(data)

    def serve(self):
        start = time.monotonic()
        url = self.path
        if not url.startswith(('http://', 'https://')):
            host = self.headers.get('Host', LOCAL)
            url = f'http://{host}{url}'
        body = None
        if self.command == 'POST':
            n = int(self.headers.get('Content-Length') or 0)
            body = self.rfile.read(min(n, MAX_BODY))
        proxy = self.server.proxy
        try:
            status, kind, doc, extra = proxy.handle(self.command, url, body, self.headers.get('Content-Type'))
        except (OSError, urllib.error.URLError, ValueError, subprocess.SubprocessError) as e:
            reason = getattr(e, 'reason', None) or e
            status, kind, extra = 502, 'text/html', {}
            doc = proxy.message('Page inaccessible', f'Impossible d’ouvrir {html32.esc(url)} : '
                                                    f'{html32.esc(str(reason))}')
        size = self.respond(status, kind, doc, extra)
        log(f'{self.command} {url} {status} {size} o {time.monotonic() - start:.1f} s')

    def do_GET(self):
        self.serve()

    def do_POST(self):
        self.serve()

    def do_HEAD(self):
        self.serve()

    def do_CONNECT(self):
        self.respond(501, 'text/html', Proxy.message('HTTPS direct indisponible',
                                                     'Écrire http:// : le proxy choisit HTTPS lui-même.'))


class Server(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
    daemon_threads = True

    def __init__(self, path, proxy=None):
        if os.path.exists(path):
            os.unlink(path)
        super().__init__(path, Handler)
        self.proxy = proxy or Proxy()
