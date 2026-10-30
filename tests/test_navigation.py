# SPDX-License-Identifier: GPL-3.0-or-later
"""Mode navigation d'Épaulard, côté proxy : structure des pages (scripts/reseau/tampon.py)
et point http://pc486/tampon?url=… ; l'en-tête du banc natif suit le proxy.

Run: python3 -m unittest tests.test_navigation -v
"""
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests'))
os.environ.setdefault('PC486_CHROMIUM', '/nonexistent')

from reseau import html32, proxy, tampon  # noqa: E402
import navweb_essai  # noqa: E402


def parse(data):
    """Lecture du format, comme navweb.c : (titre, url, texte, éléments)."""
    head, rest = data.split(b'\nTEXTE ', 1)
    n, rest = rest.split(b'\n', 1)
    n = int(n)
    text = rest[:n].decode('cp1252')
    lines = rest[n:].decode('cp1252').split('\n')
    assert lines[0] == '' and lines[1].startswith('ELEMENTS ') and lines[-2] == 'FIN'
    items = []
    for line in lines[2:-2]:
        f = line.split(' ', 9)
        items.append({'kind': f[0], 'level': int(f[1]), 'start': int(f[2]), 'end': int(f[3]),
                      'parent': int(f[4]), 'a': int(f[5]), 'b': int(f[6]), 'c': int(f[7]),
                      'd': int(f[8]), 's': f[9] if len(f) > 9 else ''})
    title = head.split(b'TITRE ', 1)[1].split(b'\n', 1)[0].decode('cp1252')
    return title, text, items


def essai():
    p = proxy.Proxy()
    p.handle('GET', 'http://pc486/essai', None, None)
    p.documents.visit('http://pc486/')
    return parse(p.documents.tampon('http://pc486/essai'))


class TamponTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.title, cls.text, cls.items = essai()

    def of(self, kind):
        return [i for i in self.items if i['kind'] == kind]

    def span(self, it):
        return self.text[it['start']:it['end']]

    def test_title_and_positions(self):
        self.assertEqual(self.title, "Page d'essai du mode navigation")
        starts = [i['start'] for i in self.items]
        self.assertEqual(starts, sorted(starts), 'éléments dans l’ordre de leur début')
        for it in self.items:
            self.assertLessEqual(it['start'], it['end'])
            self.assertLessEqual(it['end'], len(self.text))
            if it['parent'] != 65535:
                p = self.items[it['parent']]
                self.assertTrue(p['start'] <= it['start'] and it['end'] <= p['end'], it)

    def test_headings_and_paragraphs(self):
        h = [(i['level'], self.span(i)) for i in self.of('h')]
        self.assertEqual(h[:4], [(1, "Page d'essai du mode navigation"), (2, 'Titres et paragraphes'),
                                 (3, 'Sous-titre de niveau trois'), (4, 'Niveau quatre')])
        self.assertTrue(any('Troisième paragraphe' in self.span(p) for p in self.of('p')))

    def test_links_visited_and_without_numbers(self):
        links = {self.span(a): a for a in self.of('a')}
        self.assertEqual(links['lien déjà visité']['a'], 1)
        self.assertEqual(links['lien jamais visité']['a'], 0)
        self.assertEqual(links['lien jamais visité']['s'], 'http://pc486/jamais-visite')
        self.assertEqual(links['Titres']['s'], '#titres')
        self.assertNotRegex(self.text, r'\[\d+\]', 'numéros des liens absents du texte lu')
        self.assertIn('titres', [n['s'] for n in self.of('n')])

    def test_lists_nested_and_counted(self):
        lists = self.of('l')
        fruits = next(l for l in lists if 'Fruits' in self.span(l))
        self.assertEqual((fruits['a'], fruits['b'], fruits['level']), (3, 0, 1))
        nested = next(l for l in lists if self.span(l).startswith('• Pomme'))
        self.assertEqual((nested['a'], nested['level']), (3, 2))
        self.assertEqual(self.items[self.items[nested['parent']]['parent']], fruits)
        ordered = next(l for l in lists if '1. Premier' in self.span(l))
        self.assertEqual((ordered['a'], ordered['b']), (2, 1))
        self.assertEqual('\x95', '•'.encode('cp1252').decode('latin-1'))

    def test_table_cells_headers_spans_and_empty(self):
        t = self.of('t')[0]
        self.assertEqual((t['a'], t['b'], t['level'], t['s']), (6, 3, 1, 'Températures de la semaine'))
        cells = {(c['a'], c['b']): c for c in self.of('c')}
        self.assertEqual(self.span(cells[(2, 2)]), '12')
        self.assertEqual(cells[(2, 2)]['s'], 'Matin\tLundi')
        self.assertEqual(self.span(cells[(3, 2)]).strip(), '', 'cellule vide gardée')
        self.assertGreater(cells[(3, 2)]['end'], cells[(3, 2)]['start'])
        rain = cells[(4, 2)]
        self.assertEqual((rain['c'], rain['d'], self.span(rain)), (1, 2, 'pluie toute la journée'))
        thursday = cells[(5, 1)]
        self.assertEqual((thursday['c'], thursday['level']), (2, 1))
        self.assertNotIn((6, 1), cells, 'couverte par Jeudi (rowspan)')
        self.assertEqual(cells[(6, 2)]['s'], 'Matin\tJeudi')

    def test_empty_rows_not_counted(self):
        html = ('<TABLE BORDER=1><CAPTION>c</CAPTION><TR></TR><TR><TD ROWSPAN=2>a</TD><TD>b</TD></TR>'
                '<TR><TD>d</TD></TR><TR></TR><TR><TD>e</TD></TR></TABLE>')
        t = tampon.Tampon(html, 'http://x/')
        cells = [(i.a, i.b, i.c) for i in t.items if i.kind == 'c']
        self.assertEqual(cells, [(1, 1, 2), (1, 2, 1), (2, 2, 1), (3, 1, 1)])
        self.assertEqual(t.items[0].a, 3)

    def test_layout_table_without_headers(self):
        html = '<TABLE><TR><TD>a</TD><TD>b</TD></TR></TABLE>'
        t = tampon.Tampon(html, 'http://x/')
        self.assertEqual(t.items[0].kind, 't')
        self.assertEqual(t.items[0].level, 0)

    def test_form_fields_in_netscape_window_order(self):
        fields = [i for i in self.items if i['kind'] in 'exrbCL']
        self.assertEqual([f['a'] for f in fields], list(range(13)))
        self.assertEqual(''.join(f['kind'] for f in fields), 'ebeexxrrrCebb')
        by = {f['s']: f for f in fields}
        self.assertEqual(self.span(by['Nom :']), 'Dupont')
        self.assertEqual(by['Mot de passe :']['level'], 2)
        self.assertEqual((by['Recevoir la lettre']['b'], by['Recevoir les offres']['b']), (1, 0))
        self.assertEqual(by['Vert']['b'], 1)
        self.assertEqual((by['Ville :']['b'], self.span(by['Ville :'])), (3, 'Lyon'))
        self.assertEqual((by['Remarque :']['level'], self.span(by['Remarque :'])), (3, 'Texte initial'))
        self.assertTrue(all(f['end'] > f['start'] for f in fields), 'un caractère au moins')

    def test_hidden_inputs_have_no_window(self):
        t = tampon.Tampon('<FORM><INPUT TYPE=HIDDEN NAME=a VALUE=1><INPUT NAME=b>'
                          '<INPUT TYPE=IMAGE ALT=Go></FORM>', 'http://x/')
        fields = [i for i in t.items if i.kind in 'exrbCL']
        self.assertEqual([(f.kind, f.a) for f in fields], [('e', 0), ('b', 65535)])

    def test_regions_image_quote_separator(self):
        regions = [r['s'] for r in self.of('d')]
        self.assertEqual(regions, ['bannière', 'navigation', 'principal', 'informations'])
        self.assertEqual(self.span(self.of('g')[0]), '[Soleil sur la colline]')
        self.assertIn('Le plus grand voyageur', self.span(self.of('q')[0]))
        self.assertEqual(len(self.of('s')), 2)

    def test_lines_wrapped_without_moving_positions(self):
        for line in self.text.replace('\x0b', '\n').split('\n'):
            self.assertLessEqual(len(line), tampon.LINE + 1)
        self.assertIn('\x0b', self.text)

    def test_limits(self):
        html = '<P>' + ' '.join(f'mot{i}' for i in range(20000)) + '</P>'
        t = tampon.Tampon(html, 'http://x/')
        self.assertLessEqual(len(t.text), tampon.MAX_TEXT + 1)
        html = ''.join(f'<A HREF="/{i}">l{i}</A> ' for i in range(3000))
        t = tampon.Tampon(html, 'http://x/')
        self.assertLessEqual(len(t.items), tampon.MAX_ITEMS)
        long = 'http://x/' + 'a' * 400
        t = tampon.Tampon(f'<A HREF="{long}">x</A>', 'http://x/')
        _, _, items = parse(t.serialize())
        self.assertEqual(len(items[0]['s']), tampon.MAX_LINK)

    def test_converter_marks_regions_and_frames(self):
        doc = html32.convert('<nav><a href="/a">A</a></nav><iframe src="/f" title="pub"></iframe>'
                             '<video src="/v.mp4"></video>', 'http://x/', 100000, True)
        t = tampon.Tampon(''.join(doc.parts), 'http://x/')
        kinds = [i.kind for i in t.items]
        self.assertIn('d', kinds)
        self.assertIn('m', kinds)
        self.assertIn('o', kinds)


class EndpointTests(unittest.TestCase):
    def test_tampon_of_displayed_page(self):
        p = proxy.Proxy()
        status, kind, doc, extra = p.handle('GET', 'http://pc486/tampon?url=http%3A%2F%2Fpc486%2Fessai',
                                            None, None)
        self.assertEqual(status, 404)
        p.handle('GET', 'http://pc486/essai', None, None)
        status, kind, data, extra = p.handle(
            'GET', 'http://pc486/tampon?url=HTTP%3A%2F%2FPC486%2Fessai%23formulaire', None, None)
        self.assertEqual((status, kind, extra.get('Cache-Control')), (200, 'text/plain', 'no-cache'))
        self.assertTrue(data.startswith(b'PC486-TAMPON 1\nTITRE Page d'))
        # le tampon lui-même n'est pas gardé comme page
        self.assertEqual(list(p.documents.items), ['http://pc486/essai'])

    def test_visits_kept_in_data_folder(self):
        with tempfile.TemporaryDirectory() as d:
            p = proxy.Proxy(data=d)
            p.handle('GET', 'http://pc486/essai', None, None)
            p.documents.visit('http://pc486/jamais-visite#x')
            q = proxy.Proxy(data=d)
            self.assertIn('http://pc486/jamais-visite', q.documents.visited)
            self.assertIn('http://pc486/essai', q.documents.visited)

    def test_latest_version_and_size_bound(self):
        docs = proxy.Documents(size=2)
        for i in range(3):
            docs.put(f'http://x/{i}', f'<TITLE>{i}</TITLE><P>page {i}')
        docs.put('http://x/2', '<TITLE>nouvelle</TITLE><P>autre')
        self.assertIsNone(docs.tampon('http://x/0'))
        self.assertIn(b'TITRE nouvelle', docs.tampon('http://x/2'))


class BenchFixtureTests(unittest.TestCase):
    def test_native_bench_header_follows_proxy(self):
        """tests/win16/navweb-essai.h : à régénérer par python3 tests/navweb_essai.py"""
        self.assertEqual(navweb_essai.HEADER.read_text(encoding='utf-8'), navweb_essai.header())


if __name__ == '__main__':
    unittest.main()
