"""Services réseau de l'hôte (scripts/reseau) et bascule RESEAU.EXE, sans VM.

Run: python3 -m unittest tests.test_reseau -v
Aucun accès à Internet : le « site » est un serveur HTTP local, le courrier une
boîte temporaire, Chromium un faux script.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import poplib
import shutil
import smtplib
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
import urllib.error
from urllib.parse import parse_qsl, urlsplit

ROOT = Path(__file__).resolve().parents[1]
os.environ['PC486_DEBIT'] = '0'     # débit des services en essai : sans limite
WATCOM = Path(os.environ.get('WATCOM', ROOT / 'tools/watcom')).resolve()
sys.path.insert(0, str(ROOT / 'scripts'))
from reseau import courrier, debit, html32, proxy  # noqa: E402


class Html32Tests(unittest.TestCase):
    def convert(self, markup, base='https://exemple.fr/a/', limit=60000):
        return html32.convert(markup, base, limit)

    def test_scripts_dropped_links_proxied_title_kept(self):
        page = self.convert('<html><head><title>Titre é</title><script>x()</script></head>'
                            '<body><p>Bonjour <a href="/b?c=1">lien</a>'
                            '<a href="javascript:void(0)">js</a><img src=x alt="Logo"></p>'
                            '<style>p{}</style></body></html>')
        body = page.parts[0]
        self.assertEqual(page.title, 'Titre é')
        self.assertNotIn('x()', body)
        self.assertNotIn('p{}', body)
        self.assertIn('<A HREF="http://exemple.fr/b?c=1">lien</A>', body)
        self.assertNotIn('javascript', body)
        self.assertIn('[Logo]', body)

    def test_search_redirects_are_unwrapped(self):
        body = self.convert('<a href="//duckduckgo.com/l/?uddg=https%3A%2F%2Ffr.wikipedia.org'
                            '%2Fwiki%2FX&rut=1">X</a>').parts[0]
        self.assertIn('HREF="http://fr.wikipedia.org/wiki/X"', body)

    def test_implied_end_tags_build_rows(self):
        body = self.convert('<table><caption>Prix</caption><tr><th>A<th>B<tr><td>1<td>2</table>'
                            '<p>un<ul><li>x<li>y</ul>').parts[0]
        self.assertEqual(body.count('<TR>'), 2)
        self.assertIn('<CAPTION>Prix</CAPTION>', body)
        self.assertRegex(body, r'<TR>\s*<TD>1</TD>\s*<TD>2</TD>\s*</TR>')
        self.assertRegex(body, r'<P>un</P>\s*<UL>')
        self.assertEqual(body.count('<LI>'), 2)

    def test_layout_table_is_linearised(self):
        body = self.convert('<table><tr><td>menu</td><td>texte</td></tr></table>').parts[0]
        self.assertNotIn('<TABLE', body)
        self.assertIn('menu', body)
        self.assertIn('texte', body)

    def test_hidden_and_controls_outside_forms_dropped(self):
        body = self.convert('<div hidden>caché</div><span aria-hidden="true">déco</span>'
                            '<p style="display: none">non</p><input type=checkbox id=m>'
                            '<form action="/f"><input name=q placeholder="Rechercher"></form>').parts[0]
        for word in ('caché', 'déco', '>non<', 'CHECKBOX'):
            self.assertNotIn(word, body)
        self.assertIn('<FORM ACTION="http://exemple.fr/f"', body)
        self.assertIn('Rechercher', body)
        self.assertIn('NAME="q"', body)

    def test_skip_link_to_main(self):
        body = self.convert('<nav><a href="/">Accueil</a></nav><main><h1>T</h1></main>').parts[0]
        self.assertTrue(body.startswith('<A HREF="#contenu">Aller au contenu</A>'), body[:80])
        self.assertIn('<A NAME="contenu"></A>', body)
        body = self.convert('<a href="#m">Sauter</a><main id=m><h1>T</h1></main>').parts[0]
        self.assertNotIn('Aller au contenu', body)
        self.assertIn('<A NAME="m"></A>', body)

    def test_long_page_split_outside_tables_and_lists(self):
        para = ''.join(f'<p>paragraphe {i} ' + 'mot ' * 40 + '</p>' for i in range(30))
        table = '<table><tr><th>t</th></tr>' + '<tr><td>' + 'c ' * 400 + '</td></tr>' * 4 + '</table>'
        liste = '<ul>' + '<li>élément ' * 200 + '</ul>'
        page = self.convert(para + table + liste + para, limit=2000)
        self.assertGreater(len(page.parts), 3)
        for part in page.parts:
            self.assertEqual(part.count('<TABLE'), part.count('</TABLE>'), part[:200])
            self.assertEqual(part.count('<UL>'), part.count('</UL>'))
        whole = ''.join(page.parts)
        self.assertEqual(whole.count('paragraphe 29'), 2)

    def test_encoding_translit_cp1252(self):
        # windows-1252 garde – ’ … ; l'émoji disparaît, le reste est translittéré.
        data = html32.encode('« Œuvre » – l’été… 😀 ★ → ő')
        self.assertEqual(data.decode('cp1252'), '« Œuvre » – l’été…  * -> o')

    def test_links_numbered_for_keyboard_jump(self):
        markup = ('<p><a href="/a">Un</a> <a href="#x">ancre</a> <a href="https://b.fr/">Deux</a></p>'
                  '<p id="x">x</p>')
        page = html32.convert(markup, 'https://site.fr/')
        body = page.parts[0]
        self.assertIn('>Un</A><!--pc486 num--> [1]', body)
        self.assertIn('>ancre</A> <', body)          # ancre interne : pas de numéro
        self.assertIn('>Deux</A><!--pc486 num--> [2]', body)
        self.assertEqual(page.links, ['http://site.fr/a', 'http://b.fr/'])
        plain = html32.convert(markup, 'https://site.fr/', numbers=False)
        self.assertNotIn('[1]', plain.parts[0])
        self.assertEqual(plain.links, [])

    def test_unreadable_link_text_uses_title_or_language(self):
        markup = ('<a href="https://ar.wikipedia.org/" title="arabe" lang="ar">العربية</a> '
                  '<a href="https://ja.wikipedia.org/" lang="ja">日本語</a> '
                  '<a href="https://de.wikipedia.org/" title="allemand">Deutsch</a>')
        body = html32.convert(markup, 'https://fr.wikipedia.org/').parts[0]
        self.assertIn('>arabe</A><!--pc486 num--> [1]', body)
        self.assertIn('>ja</A><!--pc486 num--> [2]', body)
        self.assertIn('>Deutsch</A><!--pc486 num--> [3]', body)          # texte lisible : gardé

    def test_meta_refresh(self):
        page = self.convert('<meta http-equiv="refresh" content="0; url=/suite">')
        self.assertEqual(page.refresh, 'http://exemple.fr/suite')


class Site(BaseHTTPRequestHandler):
    """Faux site : pages UTF-8/latin-1, formulaire, fichier binaire, gzip."""
    protocol_version = 'HTTP/1.0'

    def log_message(self, *a):
        pass

    def reply(self, data, kind='text/html; charset=utf-8', extra=()):
        self.send_response(200)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(data)))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        parts = urlsplit(self.path)
        self.server.seen.append(('GET', self.path))
        if parts.path == '/latin':
            self.reply('<title>Élan</title><p>Café</p>'.encode('latin-1'),
                       'text/html; charset=iso-8859-1')
        elif parts.path == '/cherche':
            q = dict(parse_qsl(parts.query)).get('q', '')
            self.reply(f'<title>R</title><p>Résultat pour {q}</p>'.encode())
        elif parts.path == '/fichier.zip':
            self.reply(b'PK\x03\x04\x00\xff', 'application/zip',
                       [('Content-Disposition', 'attachment; filename=f.zip')])
        elif parts.path == '/long':
            self.reply(('<title>Long</title>' + ''.join(f'<p>bloc {i} ' + 'x ' * 300 + '</p>'
                                                        for i in range(40))).encode())
        else:
            self.reply('<title>Accueil</title><p>Été <a href="https://ailleurs.fr/">là</a></p>'.encode())

    def do_POST(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        self.server.seen.append(('POST', body))
        nom = dict(parse_qsl(body.decode())).get('nom', '')
        self.reply(f'<p>Merci {nom}</p>'.encode())


class ProxyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.site = ThreadingHTTPServer(('127.0.0.1', 0), Site)
        cls.site.seen = []
        threading.Thread(target=cls.site.serve_forever, daemon=True).start()
        cls.base = f'http://127.0.0.1:{cls.site.server_address[1]}'

    @classmethod
    def tearDownClass(cls):
        cls.site.shutdown()
        cls.site.server_close()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='pc486-proxy-')
        self.sock = os.path.join(self.tmp.name, 'web.sock')
        self.env = os.environ.get('PC486_CHROMIUM')
        self.log, proxy.log = proxy.log, lambda msg: None
        os.environ['PC486_CHROMIUM'] = os.path.join(self.tmp.name, 'absent')
        self.server = proxy.Server(self.sock)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.site.seen.clear()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        proxy.log = self.log
        if self.env is None:
            os.environ.pop('PC486_CHROMIUM', None)
        else:
            os.environ['PC486_CHROMIUM'] = self.env
        self.tmp.cleanup()

    def request(self, raw):
        """Requête brute du navigateur d'époque ; (ligne d'état, en-têtes, corps)."""
        s = socket.socket(socket.AF_UNIX)
        s.connect(self.sock)
        s.sendall(raw)
        data = b''
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
        s.close()
        head, _, body = data.partition(b'\r\n\r\n')
        lines = head.decode('latin-1').split('\r\n')
        headers = dict(line.split(': ', 1) for line in lines[1:])
        self.assertEqual(int(headers['Content-Length']), len(body))
        return lines[0], headers, body

    def get(self, url):
        return self.request(f'GET {url} HTTP/1.0\r\nUser-Agent: Mozilla/3.01Gold (Win16; I)\r\n\r\n'
                            .encode('latin-1'))

    def test_html_converted_and_served_in_latin1(self):
        status, headers, body = self.get(self.base + '/')
        self.assertEqual(status, 'HTTP/1.0 200 OK')
        self.assertEqual(headers['Content-Type'], 'text/html; charset=iso-8859-1')
        self.assertEqual(headers['Connection'], 'close')
        text = body.decode('cp1252')
        self.assertTrue(text.startswith('<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2 Final//EN">'))
        self.assertIn('<TITLE>Accueil</TITLE>', text)
        self.assertIn('Été', text)
        self.assertIn('HREF="http://ailleurs.fr/"', text)
        self.assertIn('Accueil pc486', text)
        self.assertNotIn('Version JavaScript', text)
        status, _, body = self.get(self.base + '/latin')
        self.assertIn('Café', body.decode('cp1252'))

    def test_cp1252_query_and_form_reencoded_to_utf8(self):
        _, _, body = self.get(self.base + '/cherche?q=lecteur+d%27%E9cran')
        self.assertIn("Résultat pour lecteur d'écran", body.decode('cp1252'))
        self.assertIn('q=lecteur%20d%27%C3%A9cran', self.site.seen[-1][1])
        form = 'nom=J%E9r%F4me'.encode()
        _, _, body = self.request(f'POST {self.base}/envoi HTTP/1.0\r\n'
                                  'Content-Type: application/x-www-form-urlencoded\r\n'
                                  f'Content-Length: {len(form)}\r\n\r\n'.encode() + form)
        self.assertIn('Merci Jérôme', body.decode('cp1252'))
        self.assertEqual(self.site.seen[-1], ('POST', b'nom=J%C3%A9r%C3%B4me'))

    def test_binary_passes_through(self):
        _, headers, body = self.get(self.base + '/fichier.zip')
        self.assertEqual(body, b'PK\x03\x04\x00\xff')
        self.assertEqual(headers['Content-Type'], 'application/zip')
        self.assertEqual(headers['Content-Disposition'], 'attachment; filename=f.zip')

    def test_long_page_is_paginated(self):
        old = proxy.PAGE_LIMIT
        proxy.PAGE_LIMIT = 4000
        try:
            _, _, body = self.get(self.base + '/long')
        finally:
            proxy.PAGE_LIMIT = old
        text = body.decode('cp1252')
        self.assertIn('bloc 0', text)
        self.assertNotIn('bloc 39', text)
        self.assertRegex(text, r'Page 1 sur \d+')
        nxt = text.split('<A HREF="http://pc486/suite?id=')[1].split('"')[0].replace('&amp;', '&')
        _, _, body = self.get('http://pc486/suite?id=' + nxt)
        self.assertRegex(body.decode('cp1252'), r'Page 2 sur \d+')
        status, _, _ = self.get('http://pc486/suite?id=inconnu&n=2')
        self.assertEqual(status, 'HTTP/1.0 404 Not Found')

    def test_jump_to_numbered_link(self):
        _, _, body = self.get(self.base + '/')
        text = body.decode('cp1252')
        self.assertIn('>là</A><!--pc486 num--> [1]', text)
        form = text.split('<FORM ACTION="http://pc486/lien" METHOD=GET>')[1].split('</FORM>')[0]
        self.assertIn('NAME="n"', form)
        key = form.split('NAME="id" VALUE="')[1].split('"')[0]
        self.assertLess(text.index('pc486/lien'), text.index('>là</A>'))   # premier champ de la page
        status, headers, _ = self.get(f'http://pc486/lien?n=1&id={key}')
        self.assertEqual(status, 'HTTP/1.0 302 Found')
        self.assertEqual(headers['Location'], 'http://ailleurs.fr/')
        status, _, body = self.get(f'http://pc486/lien?n=+%5B1%5D+&id={key}')     # « [1] » tapé
        self.assertEqual(status, 'HTTP/1.0 302 Found')
        status, _, body = self.get(f'http://pc486/lien?n=7&id={key}')
        self.assertEqual(status, 'HTTP/1.0 404 Not Found')
        self.assertIn('Pas de lien n° 7 sur cette page (1 lien)', body.decode('cp1252'))
        status, _, body = self.get('http://pc486/lien?n=1&id=inconnu')
        self.assertIn('Page expirée', body.decode('cp1252'))

    def test_local_home_and_errors(self):
        status, _, body = self.get('http://pc486/')
        self.assertEqual(status, 'HTTP/1.0 200 OK')
        self.assertIn('ACTION="http://html.duckduckgo.com/html/"', body.decode('cp1252'))
        status, _, body = self.request(b'CONNECT exemple.fr:443 HTTP/1.0\r\n\r\n')
        self.assertTrue(status.startswith('HTTP/1.0 501'))
        # Port fermé : page d'erreur lisible, pas de connexion coupée
        dead = socket.socket()
        dead.bind(('127.0.0.1', 0))
        port = dead.getsockname()[1]
        dead.close()
        status, _, body = self.get(f'http://127.0.0.1:{port}/')
        self.assertTrue(status.startswith('HTTP/1.0 502'))
        self.assertIn('Page inaccessible', body.decode('cp1252'))

    def test_https_first_then_http_fallback(self):
        f = proxy.Fetcher()
        tried = []
        real = f.opener.open

        def opener(req, timeout=None):
            tried.append(req.full_url)
            if req.full_url.startswith('https://'):
                raise urllib.error.URLError(ConnectionRefusedError(111, 'refusé'))
            req.full_url = req.full_url.replace('http://vieux.site', self.base)
            return real(req, timeout=timeout)
        f.opener.open = opener
        status, _, _, data = f.fetch('GET', 'http://vieux.site/latin')
        self.assertEqual(status, 200)
        self.assertEqual(tried, ['https://vieux.site/latin', 'http://vieux.site/latin'])
        f.fetch('GET', 'http://vieux.site/')
        self.assertEqual(tried[2:], ['http://vieux.site/'], 'HTTPS retenté après un refus')

    def test_optional_chromium(self):
        fake = Path(self.tmp.name) / 'chromium'
        fake.write_text('#!/bin/sh\necho "<title>JS</title><p>rendu par $#</p>"\n')
        fake.chmod(0o755)
        os.environ['PC486_CHROMIUM'] = str(fake)
        _, _, body = self.get(self.base + '/')
        self.assertIn('Version JavaScript', body.decode('cp1252'))
        _, _, body = self.get('http://pc486/js?url=http%3A%2F%2Fexemple.fr%2F')
        text = body.decode('cp1252')
        self.assertIn('<TITLE>JS</TITLE>', text)
        self.assertIn('rendu par', text)
        os.environ['PC486_CHROMIUM'] = str(Path(self.tmp.name) / 'absent')
        status, _, body = self.get('http://pc486/js?url=http%3A%2F%2Fexemple.fr%2F')
        self.assertTrue(status.startswith('HTTP/1.0 502'))
        self.assertIn('Chromium absent', body.decode('cp1252'))


class MailTests(unittest.TestCase):
    """Eudora : SMTP puis POP3 sur la même boîte, sockets Unix comme pour la VM."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='pc486-courrier-')
        d = self.tmp.name
        self.logs = []
        self.servers = []
        for name, handler in (('smtp', courrier.Smtp), ('pop3', courrier.Pop3)):
            s = courrier.Server(f'{d}/{name}.sock', handler, f'{d}/donnees/courrier', self.logs.append)
            threading.Thread(target=s.serve_forever, daemon=True).start()
            self.servers.append(s)

    def tearDown(self):
        for s in self.servers:
            s.shutdown()
            s.server_close()
        self.tmp.cleanup()

    def connect(self, name):
        s = socket.socket(socket.AF_UNIX)
        s.connect(f'{self.tmp.name}/{name}.sock')
        return s

    def smtp(self):
        c = smtplib.SMTP()
        c.sock = self.connect('smtp')
        c.file = None
        c.getreply()
        return c

    def pop(self):
        class Pop(poplib.POP3):
            def __init__(p, sock):
                p._tls_established = False
                p.sock = sock
                p.file = sock.makefile('rb')
                p._debugging = 0
                p.welcome = p._getresp()
        return Pop(self.connect('pop3'))

    def test_send_then_retrieve_and_delete(self):
        c = self.smtp()
        c.ehlo('pc486')
        msg = ('From: moi@pc486\r\nTo: quelquun@exemple.fr\r\nSubject: essai\r\n\r\n'
               'Ligne 1\r\n.ligne avec point\r\nÉté\r\n').encode('latin-1')
        c.sendmail('moi@pc486', ['quelquun@exemple.fr'], msg)
        c.quit()
        self.assertTrue(any('déposé' in line for line in self.logs))
        p = self.pop()
        p.user('nimporte')
        p.pass_('quoi')
        count, _ = p.stat()
        self.assertEqual(count, 2)          # bienvenue + message envoyé
        texts = [b'\r\n'.join(p.retr(i)[1]) for i in (1, 2)]
        self.assertTrue(any(b'Bienvenue' in t for t in texts))
        sent = next(t for t in texts if b'Subject: essai' in t)
        self.assertIn(b'\r\n.ligne avec point\r\n', sent)
        self.assertIn('Été'.encode('latin-1'), sent)
        self.assertIn(b'Message-ID:', sent)
        top = p.top(2, 0)[1]
        self.assertNotIn(b'Ligne 1', b'\r\n'.join(top))
        p.dele(1)
        p.dele(2)
        p.quit()
        p = self.pop()
        self.assertEqual(p.stat()[0], 0)
        p.quit()
        # La bienvenue n'est déposée qu'une fois, même après redémarrage
        courrier.box(f'{self.tmp.name}/donnees/courrier')
        self.assertEqual(len(courrier.box(f'{self.tmp.name}/donnees/courrier')), 0)

    def test_rset_keeps_messages(self):
        p = self.pop()
        p.dele(1)
        p.rset()
        p.quit()
        p = self.pop()
        self.assertEqual(p.stat()[0], 1)
        p.quit()


class DebitTests(unittest.TestCase):
    """Débit total vers la VM borné (sinon Windows ne lit plus le clavier)."""

    def test_shared_bucket_limits_all_connections(self):
        import io
        import time
        bucket = debit.Bucket(128 * 1024)
        outs = [io.BytesIO(), io.BytesIO()]
        t0 = time.monotonic()
        threads = [threading.Thread(target=debit.Limited(o, bucket).write, args=(b'x' * 40000,))
                   for o in outs]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        took = time.monotonic() - t0
        self.assertEqual([len(o.getvalue()) for o in outs], [40000, 40000])
        # 80 000 octets, rafale initiale 13 107 : au moins 0,5 s à 128 Ko/s
        self.assertGreater(took, (80000 - 13107) / (128 * 1024) * 0.9)
        self.assertLess(took, 1.5)

    def test_zero_means_unlimited(self):
        import io
        import time
        t0 = time.monotonic()
        debit.Limited(io.BytesIO(), debit.Bucket(0)).write(b'x' * 1_000_000)
        self.assertLess(time.monotonic() - t0, 0.5)


class ServicesTests(unittest.TestCase):
    def test_module_starts_and_cleans_sockets(self):
        with tempfile.TemporaryDirectory(prefix='pc486-svc-') as d:
            env = dict(os.environ, PYTHONPATH=str(ROOT / 'scripts'))
            p = subprocess.Popen([sys.executable, '-P', '-m', 'reseau', '--run', f'{d}/run',
                                  '--donnees', f'{d}/data'], env=env, stderr=subprocess.PIPE, text=True)
            line = p.stderr.readline()
            self.assertIn('services prêts', line)
            for name in ('web', 'pop3', 'smtp'):
                self.assertTrue(Path(f'{d}/run/{name}.sock').is_socket())
            p.terminate()
            self.assertEqual(p.wait(10), 0)
            p.stderr.close()
            self.assertEqual(list(Path(f'{d}/run').iterdir()), [])


def namespaces_allowed():
    probe = 'import os; os.unshare(0x10000000 | 0x40000000)'
    return subprocess.run([sys.executable, '-c', probe], capture_output=True).returncode == 0


@unittest.skipUnless(namespaces_allowed(), 'espaces de noms utilisateur interdits sur cet hôte')
class NetnsTests(unittest.TestCase):
    """Isolement de 86Box : seuls les relais des services sont joignables."""

    def test_only_relayed_services_reachable(self):
        with tempfile.TemporaryDirectory(prefix='pc486-netns-') as d:
            web = socket.socket(socket.AF_UNIX)
            web.bind(f'{d}/web.sock')
            web.listen()

            def answer():
                c, _ = web.accept()
                c.sendall(b'service web')
                c.close()
            threading.Thread(target=answer, daemon=True).start()
            outside = socket.socket()
            outside.bind(('127.0.0.1', 0))
            outside.listen()
            probe = f'''
import socket
def t(host, port):
    try:
        s = socket.create_connection((host, port), timeout=3)
        return s.recv(100).decode() or 'connecté'
    except OSError as e:
        return type(e).__name__
print(t('127.0.0.1', 8080)); print(t('127.0.0.1', 25)); print(t('127.0.0.1', {outside.getsockname()[1]}))
print(t('1.1.1.1', 80)); print(t('10.0.2.2', 8080))
'''
            env = dict(os.environ, PYTHONPATH=str(ROOT / 'scripts'))
            r = subprocess.run([sys.executable, '-P', '-m', 'reseau.netns', '--run', d,
                                '--service', 'web:8080', '--service', 'smtp:25', '--',
                                sys.executable, '-c', probe],
                               env=env, capture_output=True, text=True, timeout=30)
            outside.close()
            web.close()
            self.assertEqual(r.stdout.split('\n')[:5],
                             ['service web',
                              'connecté',               # relais présent, service absent : refermé
                              'ConnectionRefusedError',  # port de l'hôte invisible
                              'OSError', 'OSError'],     # aucune route
                             r.stderr)


@unittest.skipUnless((WATCOM / 'binl64/wcl386').exists(), 'Open Watcom absent')
class ReseauExeTests(unittest.TestCase):
    """Vraie fonction de bascule de RESEAU.EXE (dos/src/reseau.c), compilée pour Linux."""

    def test_bench(self):
        with tempfile.TemporaryDirectory(prefix='reseau-native-') as d:
            for name in ('reseau.c', 'reseau.h'):
                shutil.copy(ROOT / 'dos/src' / name, d)
            shutil.copy(ROOT / 'tests/dos/reseau-test.c', Path(d) / 'test.c')
            env = dict(os.environ, WATCOM=str(WATCOM), INCLUDE=str(WATCOM / 'lh'),
                       PATH=f"{WATCOM / 'binl64'}:{WATCOM / 'binl'}:{os.environ.get('PATH', '')}")
            r = subprocess.run(['wcl386', '-q', '-bt=linux', '-l=linux', '-za99', '-wx', '-we',
                                '-wcd=202', '-wcd=303', 'test.c', '-fe=bench'],
                               cwd=d, env=env, capture_output=True)
            self.assertEqual(r.returncode, 0, r.stdout.decode('latin1') + r.stderr.decode('latin1'))
            r = subprocess.run([f'{d}/bench'], capture_output=True, timeout=15)
            self.assertEqual(r.returncode, 0, r.stdout.decode('latin1') + r.stderr.decode('latin1'))

    def test_options_vm_section_read_by_reseau_exe(self):
        """SYSTEM.INI écrit par options_vm.py, basculé par le vrai code de RESEAU.EXE."""
        import importlib.util
        import options_vm
        spec = importlib.util.spec_from_file_location('cb', ROOT / 'scripts/configure-braille.py')
        cb = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cb)
        original = ('[boot]\r\nshell=progman.exe\r\nnetwork.drv=\r\n\r\n[boot.description]\r\n'
                    'network.drv=Aucun r\x82seau d\'install\x82\r\n\r\n[386Enh]\r\n'
                    'network=*dosnet,*vnetbios\r\nnetheapsize=16\r\nCOM3Irq=9\r\n\r\n'
                    '[Network]\r\nwinnet=nonet\r\n')
        text = options_vm.configure_system(original, cb.setting)
        with tempfile.TemporaryDirectory(prefix='reseau-fichier-') as d:
            for name in ('reseau.c', 'reseau.h'):
                shutil.copy(ROOT / 'dos/src' / name, d)
            shutil.copy(ROOT / 'tests/dos/reseau-fichier.c', Path(d) / 'test.c')
            env = dict(os.environ, WATCOM=str(WATCOM), INCLUDE=str(WATCOM / 'lh'),
                       PATH=f"{WATCOM / 'binl64'}:{WATCOM / 'binl'}:{os.environ.get('PATH', '')}")
            r = subprocess.run(['wcl386', '-q', '-bt=linux', '-l=linux', '-za99', '-wx', '-we',
                                '-wcd=202', '-wcd=303', 'test.c', '-fe=bascule'],
                               cwd=d, env=env, capture_output=True)
            self.assertEqual(r.returncode, 0, r.stdout.decode('latin1') + r.stderr.decode('latin1'))
            ini = Path(d) / 'SYSTEM.INI'
            ini.write_bytes(text.encode('latin-1'))
            states = {}
            for on in ('1', '0'):
                r = subprocess.run([f'{d}/bascule', on, str(ini), f'{ini}.{on}'])
                self.assertEqual(r.returncode, 0)
                states[on] = Path(f'{ini}.{on}').read_bytes().decode('latin-1')
        get = options_vm.get
        on, off = states['1'], states['0']
        self.assertEqual(get(on, 'Network', 'winnet'), 'wfwnet/00025100')
        self.assertEqual(get(on, '386Enh', 'netcard'), 'ne2000.386')
        self.assertEqual(get(on, 'boot', 'network.drv'), 'wfwnet.drv')
        self.assertEqual(get(off, 'Network', 'winnet'), 'nonet')
        self.assertIsNone(get(off, '386Enh', 'netcard'))
        self.assertIsNone(get(off, '386Enh', 'transport'))
        self.assertEqual(get(off, '386Enh', 'network'), '*dosnet,*vnetbios')
        self.assertEqual(get(off, 'boot.description', 'network.drv'), 'Aucun r\x82seau d\'install\x82')
        self.assertEqual(get(off, '386Enh', 'COM3Irq'), '9')
        # Réglages permanents gardés dans les deux états ; options_vm relancé : identique
        for state in (on, off):
            self.assertEqual(get(state, 'ms$ne2clone0', 'IPAddress'), '10.0.2.15')
            self.assertEqual(options_vm.configure_system(state, cb.setting).split('[pc486.reseau]')[1],
                             text.split('[pc486.reseau]')[1])

    def test_dos_build(self):
        r = subprocess.run(['bash', str(ROOT / 'dos/build.sh')], capture_output=True)
        self.assertEqual(r.returncode, 0, r.stdout.decode('latin1') + r.stderr.decode('latin1'))
        self.assertTrue((ROOT / 'dos/build/RESEAU.EXE').exists())


if __name__ == '__main__':
    unittest.main()
