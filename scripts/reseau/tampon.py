# SPDX-License-Identifier: GPL-3.0-or-later
"""Tampon du mode navigation d'Épaulard : la structure d'une page HTML 3.2, celle-là
même que le proxy a envoyée à Netscape, pour qu'Épaulard la parcoure comme le mode
navigation de NVDA (titres, liens, listes, tableaux, champs).

Netscape 16 bits n'expose pas son document : il ne dessine que la partie visible
(modèle hors écran d'Épaulard) et ses champs de formulaire sont des fenêtres sans
lien connu avec le texte. Le proxy, qui a produit le HTML, en calcule donc le texte
et les éléments ; Épaulard le demande par Winsock (http://pc486/tampon?url=…), avec
l'adresse que Netscape donne par DDE, et vérifie le titre.

Format (windows-1252, lignes terminées par LF) :
    PC486-TAMPON 1
    TITRE <titre>
    URL <adresse>
    TEXTE <n>
    <n octets de texte : LF fin de bloc, VT (0B) ligne coupée dans un bloc>
    ELEMENTS <m>
    <genre> <niveau> <début> <fin> <parent> <a> <b> <c> <d> <chaîne>   (m lignes)
    FIN
Positions en octets du texte ; parent : rang de l'élément conteneur, 65535 aucun.
Genres :
    h titre (niveau 1-6)          p paragraphe              a lien (a : visité ; chaîne : adresse)
    n ancre (chaîne : nom)        l liste (a : éléments, b : numérotée, niveau : profondeur)
    i élément de liste (a : rang) t tableau (a : lignes, b : colonnes, niveau : 1 données,
                                    0 mise en page ; chaîne : légende)
    c cellule (a : ligne, b : colonne, c : lignes couvertes, d : colonnes couvertes,
               niveau : 1 en-tête ; chaîne : en-tête de colonne TAB en-tête de ligne)
    q citation   s séparateur   g image (chaîne : texte de remplacement)
    d région (chaîne : nom)     F formulaire     m cadre     o objet incorporé (vidéo, son)
    e zone d'édition (niveau : 1 texte, 2 mot de passe, 3 zone de texte)
    x case à cocher   r bouton radio (b : cochée au chargement)   b bouton
    C liste déroulante   L liste (b : nombre de choix, c : rang du choix initial)
    Champs : a = rang de la fenêtre que Netscape crée pour lui (ordre du document),
    65535 sans fenêtre ; chaîne : libellé.
"""
from html.parser import HTMLParser
import re
from urllib.parse import urljoin, urlsplit, urlunsplit

LINE = 100              # caractères par ligne au plus (comme NVDA)
MAX_TEXT = 30000        # un segment de 64 Ko côté Épaulard (Win16), positions sur 16 bits
MAX_ITEMS = 1500
MAX_LINK = 255          # adresse d'un lien (ouverte par DDE)
MAX_STRING = 120        # autres chaînes : libellés, en-têtes, noms
HARD, SOFT = '\n', '\x0b'
NONE = 65535

BLOCKS = {'p', 'div', 'center', 'address', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'li', 'dt',
          'dd', 'blockquote', 'pre', 'table', 'tr', 'td', 'th', 'caption', 'form', 'ul',
          'ol', 'dl', 'menu', 'dir', 'hr', 'textarea'}
CONTAINERS = set('litcqdFmo')
FIELDS = set('exrbCL')
REGIONS = {'banner': 'bannière', 'navigation': 'navigation', 'main': 'principal',
           'contentinfo': 'informations', 'complementary': 'complémentaire',
           'search': 'recherche', 'form': 'formulaire', 'region': 'région'}


def key(url):
    """Adresse sans fragment, schéma et hôte en minuscules : celle que Netscape donne
    par DDE et celle de sa requête au proxy se comparent ainsi."""
    p = urlsplit(url.strip())
    return urlunsplit((p.scheme.lower(), p.netloc.lower(), p.path or '/', p.query, ''))


class Item:
    __slots__ = ('kind', 'level', 'start', 'end', 'parent', 'a', 'b', 'c', 'd', 's')

    def __init__(self, kind, start, parent, level=0, s=''):
        self.kind, self.level, self.start, self.end = kind, level, start, start
        self.parent, self.a, self.b, self.c, self.d, self.s = parent, 0, 0, 0, 0, s


class Table:
    def __init__(self, item):
        self.item = item
        self.row = -1
        self.col = 0
        self.taken = set()       # (ligne, colonne) occupées, fusions comprises
        self.cells = []          # (élément, ligne, colonne, lignes, colonnes, en-tête, texte)
        self.header = False


class Builder(HTMLParser):
    def __init__(self, base):
        super().__init__(convert_charrefs=True)
        self.base = base
        self.text = []
        self.n = 0
        self.items = []
        self.stack = []          # (balise, rang de l'élément ou None)
        self.space = False
        self.pre = 0
        self.title = ''
        self.in_title = False
        self.tables = []
        self.lists = []          # [élément, compteur]
        self.select = None       # [élément, options, choisie, multiple]
        self.textarea = None
        self.fields = 0
        self.full = False
        self.skip = False

    # --- texte
    def container(self):
        for _, i in reversed(self.stack):
            if i is not None and self.items[i].kind in CONTAINERS:
                return i
        return NONE

    def put(self, s):
        if self.full or not s:
            return
        if self.n + len(s) > MAX_TEXT:
            self.full = True
            return
        self.text.append(s)
        self.n += len(s)

    def last(self):
        return self.text[-1][-1] if self.text else HARD

    def block(self):
        if self.n and self.last() != HARD:
            self.put(HARD)
        self.space = False

    def words(self, data):
        if self.pre:
            self.put(data.replace('\r\n', '\n').replace('\r', '\n').replace('\t', ' '))
            return
        data = data.replace('\xa0', ' ')
        lead = data[:1].isspace()
        tail = data[-1:].isspace()
        parts = data.split()
        if not parts:
            self.space = self.space or bool(data)
            return
        if (self.space or lead) and self.last() not in (HARD, ' '):
            self.put(' ')
        self.put(' '.join(parts))
        self.space = tail

    def flush(self):
        """Espace en attente avant un élément en ligne (lien, champ) : écrite avant
        son début, pas dedans."""
        if self.space and self.last() not in (HARD, ' '):
            self.put(' ')
        self.space = False

    def add(self, kind, level=0, s=''):
        if len(self.items) >= MAX_ITEMS or self.full:
            return None
        self.items.append(Item(kind, self.n, self.container(), level, s))
        return len(self.items) - 1

    def close_item(self, i):
        if i is not None:
            self.items[i].end = self.n

    # --- balises
    def handle_starttag(self, tag, attrs):
        a = {k: (v if v is not None else '') for k, v in attrs}
        if tag == 'title':
            self.in_title = True
            return
        if tag in BLOCKS:
            self.block()
        i = None
        if tag in ('h1', 'h2', 'h3', 'h4', 'h5', 'h6'):
            i = self.add('h', int(tag[1]))
        elif tag in ('p', 'div', 'center', 'address', 'dd', 'dt'):
            i = self.add('p')
        elif tag == 'pre':
            self.pre += 1
            i = self.add('p')
        elif tag == 'blockquote':
            i = self.add('q')
        elif tag in ('ul', 'ol', 'menu', 'dir', 'dl'):
            i = self.add('l', sum(1 for x in self.lists) + 1)
            if i is not None:
                self.items[i].b = 1 if tag == 'ol' else 0
            self.lists.append([i, 0, tag == 'ol'])
        elif tag == 'li':
            i = self.add('i')
            if self.lists:
                self.lists[-1][1] += 1
                if i is not None:
                    self.items[i].a = self.lists[-1][1]
                self.put(f'{self.lists[-1][1]}. ' if self.lists[-1][2] else '• ')
        elif tag == 'a':
            if 'name' in a:
                n = self.add('n', s=a['name'])
                self.close_item(n)
            if 'href' in a:
                self.flush()
                href = a['href'].strip()
                i = self.add('a', s=href if href.startswith('#') else urljoin(self.base, href))
        elif tag == 'table':
            i = self.add('t')
            if i is not None:
                t = Table(i)
                t.header = 'border' in a
                self.tables.append(t)
        elif tag == 'tr':
            if self.tables:
                t = self.tables[-1]
                # rangée sans cellule (<TR></TR> laissé par la conversion) : Netscape ne
                # l'affiche pas, elle ne compte pas
                if t.row < 0 or any(r == t.row for r, _ in t.taken):
                    t.row += 1
                t.col = 0
        elif tag in ('td', 'th'):
            i = self.add('c', 1 if tag == 'th' else 0)
            if self.tables and i is not None:
                t = self.tables[-1]
                if t.row < 0:
                    t.row = 0
                while (t.row, t.col) in t.taken:
                    t.col += 1
                rows = max(1, min(int(a.get('rowspan', '1') or 1), 100)) if a.get('rowspan', '1').isdigit() else 1
                cols = max(1, min(int(a.get('colspan', '1') or 1), 100)) if a.get('colspan', '1').isdigit() else 1
                for r in range(t.row, t.row + rows):
                    for c in range(t.col, t.col + cols):
                        t.taken.add((r, c))
                it = self.items[i]
                it.a, it.b, it.c, it.d = t.row + 1, t.col + 1, rows, cols
                t.cells.append(i)
                t.col += cols
                if tag == 'th':
                    t.header = True
        elif tag == 'caption':
            i = self.add('p')
            if self.tables:
                self.tables[-1].header = True
        elif tag == 'hr':
            i = self.add('s')               # ligne vide à lui : « séparateur »
            self.put(HARD)
            self.close_item(i)
            return
        elif tag == 'br':
            self.put(HARD)
            self.space = False
            return
        elif tag == 'form':
            i = self.add('F')
        elif tag == 'input':
            self.input(a)
            return
        elif tag == 'select':
            self.flush()
            i = self.add('L' if 'multiple' in a or (a.get('size', '1').isdigit() and int(a.get('size', '1')) > 1)
                         else 'C')
            if i is not None:
                self.items[i].a = self.fields
            self.fields += 1
            self.select = [i, [], None]
            return
        elif tag == 'option':
            if self.select is not None:
                self.select[1].append('')
                if 'selected' in a and self.select[2] is None:
                    self.select[2] = len(self.select[1]) - 1
            return
        elif tag == 'textarea':
            i = self.add('e', 3)
            if i is not None:
                self.items[i].a = self.fields
            self.fields += 1
            self.textarea = [i, []]
            return
        self.stack.append((tag, i))

    def input(self, a):
        kind = a.get('type', 'text').lower()
        if kind == 'hidden':
            return
        level = 0
        if kind in ('text', 'password', ''):
            k, level, text = 'e', 2 if kind == 'password' else 1, ('' if kind == 'password' else a.get('value', ''))
        elif kind == 'checkbox':
            k, text = 'x', ''
        elif kind == 'radio':
            k, text = 'r', ''
        elif kind in ('submit', 'reset', 'button', 'image'):
            k = 'b'
            text = a.get('value') or a.get('alt') or ('Effacer' if kind == 'reset' else 'Envoyer')
        else:
            k, level, text = 'e', 1, a.get('value', '')
        self.flush()
        i = self.add(k, level)
        if i is None:
            return
        it = self.items[i]
        it.a = NONE if kind == 'image' else self.fields
        if kind != 'image':
            self.fields += 1
        it.b = 1 if 'checked' in a else 0
        # un caractère au moins : le curseur virtuel peut se poser sur le champ
        self.put(' '.join(text.split()) or ' ')
        self.close_item(i)
        self.space = True

    def handle_endtag(self, tag):
        if tag == 'title':
            self.in_title = False
            return
        if tag == 'select' and self.select is not None:
            i, options, chosen = self.select
            self.select = None
            if i is not None:
                it = self.items[i]
                it.b = len(options)
                it.c = chosen or 0
                if options:
                    self.put(options[chosen or 0].strip())
                self.close_item(i)
            self.space = True
            return
        if tag == 'textarea' and self.textarea is not None:
            i, parts = self.textarea
            self.textarea = None
            self.put(' '.join(''.join(parts).split())[:200] or ' ')
            self.close_item(i)
            self.block()
            return
        # balise ouverte la plus proche de ce nom
        for k in range(len(self.stack) - 1, -1, -1):
            if self.stack[k][0] == tag:
                break
        else:
            return
        while len(self.stack) > k:
            t, i = self.stack.pop()
            self.end(t, i)

    def end(self, tag, i):
        if tag in ('td', 'th') and i is not None and self.items[i].start == self.n:
            self.put(' ')               # cellule vide : une ligne à elle (« vide »)
        if tag == 'pre':
            self.pre -= 1
        if tag in ('ul', 'ol', 'menu', 'dir', 'dl') and self.lists:
            li = self.lists.pop()
            if li[0] is not None:
                self.items[li[0]].a = li[1]
        self.close_item(i)
        if tag == 'table' and self.tables:
            self.finish_table(self.tables.pop())
        if tag == 'caption' and self.tables and i is not None:
            it = self.items[i]
            self.items[self.tables[-1].item].s = ' '.join(''.join(self.text)[it.start:it.end].split())
        if tag in BLOCKS:
            self.block()

    def finish_table(self, t):
        it = self.items[t.item]
        cells = [self.items[i] for i in t.cells]
        it.a = max((c.a + c.c - 1 for c in cells), default=0)
        it.b = max((c.b + c.d - 1 for c in cells), default=0)
        it.level = 1 if t.header else 0
        text = ''.join(self.text)
        heads = {}
        for c in cells:
            if c.level:
                heads[(c.a, c.b)] = c
        for c in cells:
            col = row = ''
            # en-tête de colonne : cellule d'en-tête de la première ligne qui la couvre
            for h in heads.values():
                if h is not c and h.a == 1 and h.b <= c.b < h.b + h.d and c.a > 1:
                    col = ' '.join(text[h.start:h.end].split())
                if h is not c and h.b == 1 and h.a <= c.a < h.a + h.c and c.b > 1:
                    row = ' '.join(text[h.start:h.end].split())
            c.s = col + '\t' + row

    def handle_data(self, data):
        if self.in_title:
            self.title += data
            return
        if self.select is not None:
            if self.select[1]:
                self.select[1][-1] += data
            return
        if self.textarea is not None:
            self.textarea[1].append(data)
            return
        if self.skip:
            self.skip = False
            data = re.sub(r'^\s*\[\d+\]', '', data)
        self.words(data)

    def handle_comment(self, data):
        data = data.strip()
        if data == 'pc486 num':
            self.skip = True                # « [3] » des liens numérotés : pas lu
        elif data.startswith('pc486 g'):
            self.stack.append(('!g', self.add('g', s=data[8:].strip())))
        elif data.startswith('pc486 d'):
            self.stack.append(('!d', self.add('d', s=REGIONS.get(data[8:].strip(), data[8:].strip()))))
        elif data in ('pc486 m', 'pc486 o'):
            self.stack.append(('!' + data[-1], self.add(data[-1])))
        elif data in ('/pc486 g', '/pc486 d', '/pc486 m', '/pc486 o'):
            self.handle_endtag('!' + data[-1])

    # --- fin
    def finish(self):
        self.close()                    # HTMLParser : fin des données
        while self.stack:
            t, i = self.stack.pop()
            self.end(t, i)
        self.block()
        text = ''.join(self.text)
        self.labels(text)
        return wrap(text), self.items

    def labels(self, text):
        fields = [it for it in self.items if it.kind in FIELDS]
        for k, it in enumerate(fields):
            if it.kind == 'b':
                it.s = ' '.join(text[it.start:it.end].split())
                continue
            if it.kind in 'xr':
                stop = fields[k + 1].start if k + 1 < len(fields) else len(text)
                end = text.find(HARD, it.end)
                stop = min(stop, end if end >= 0 else len(text))
                it.s = ' '.join(text[it.end:stop].split())
            else:
                start = fields[k - 1].end if k else 0
                begin = text.rfind(HARD, 0, it.start) + 1
                it.s = ' '.join(text[max(start, begin):it.start].split())
                if not it.s and begin > start:
                    # libellé seul sur la ligne d'avant (« Remarque : » <BR>)
                    prev = text.rfind(HARD, 0, begin - 1) + 1
                    above = ' '.join(text[max(prev, start):begin].split())
                    if above.endswith(':'):
                        it.s = above


def wrap(text):
    """Lignes de LINE caractères au plus : une espace devient VT (même longueur, les
    positions des éléments ne bougent pas)."""
    chars = list(text)
    start = 0
    for i, ch in enumerate(chars):
        if ch == HARD:
            start = i + 1
        elif i - start >= LINE:
            j = i
            while j > start and chars[j] != ' ':
                j -= 1
            if j > start:
                chars[j] = SOFT
                start = j + 1
    return ''.join(chars)


class Tampon:
    def __init__(self, html, url):
        b = Builder(url)
        b.feed(html)
        self.text, self.items = b.finish()
        self.title = ' '.join(b.title.split())
        self.url = url

    def serialize(self, visited=frozenset()):
        out = ['PC486-TAMPON 1', 'TITRE ' + self.title, 'URL ' + self.url,
               f'TEXTE {len(self.text)}']
        head = '\n'.join(out) + '\n'
        lines = [f'ELEMENTS {len(self.items)}']
        for it in self.items:
            a = it.a
            if it.kind == 'a':
                a = 1 if not it.s.startswith('#') and key(it.s) in visited else 0
            s = it.s.replace('\n', ' ').replace('\r', ' ')
            s = s[:MAX_LINK if it.kind == 'a' else MAX_STRING]
            lines.append(f'{it.kind} {it.level} {it.start} {it.end} {it.parent} {a} {it.b} '
                         f'{it.c} {it.d} {s}')
        lines.append('FIN')
        return (head + self.text + '\n' + '\n'.join(lines) + '\n').encode('cp1252', 'replace')
