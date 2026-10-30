# SPDX-License-Identifier: GPL-3.0-or-later
"""Conversion d'une page web moderne en HTML 3.2 pour Netscape 3/4 et Épaulard.

Ce qui reste : titres, paragraphes, listes, liens, tableaux de données, formulaires
et texte, dans l'ordre du document. Ce qui part : scripts, styles, images (leur texte
de remplacement reste), éléments masqués, cadres et médias (remplacés par un lien).
Les liens https:// deviennent http:// : le navigateur les redemande au proxy, qui
choisit lui-même HTTPS. Sortie en windows-1252, translittérée si besoin.
"""
from html.parser import HTMLParser
import codecs
import re
import unicodedata
from urllib.parse import parse_qsl, urljoin, urlsplit, urlunsplit

VOID = {'area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta',
        'param', 'source', 'track', 'wbr'}
# Contenu jamais rendu (le titre de la page est relevé à part).
DROP = {'script', 'style', 'template', 'svg', 'canvas', 'math', 'head', 'object',
        'embed', 'link', 'meta', 'base', 'map', 'dialog'}
BLOCK = {'html', 'body', 'main', 'article', 'section', 'header', 'footer', 'nav',
         'aside', 'div', 'figure', 'figcaption', 'address', 'center', 'details',
         'summary', 'fieldset', 'legend', 'hgroup', 'search', 'noscript', 'picture'}
SAME = {'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'ul', 'ol', 'li', 'dl', 'dt', 'dd',
        'blockquote', 'pre', 'sub', 'sup', 'small', 'big'}
INLINE = {'b': 'B', 'strong': 'B', 'mark': 'B', 'i': 'I', 'em': 'I', 'cite': 'I',
          'var': 'I', 'dfn': 'I', 'u': 'U', 'ins': 'U', 'code': 'TT', 'kbd': 'TT',
          'samp': 'TT', 'tt': 'TT', 's': 'STRIKE', 'strike': 'STRIKE', 'del': 'STRIKE'}
# Ouvrir l'un de ces éléments ferme le précédent du même groupe, jusqu'au conteneur.
IMPLIED = {'p': ({'p'}, {'div', 'section', 'article', 'main', 'body', 'td', 'th', 'li',
                         'blockquote', 'form'}),
           'li': ({'li'}, {'ul', 'ol', 'menu'}),
           'dt': ({'dt', 'dd'}, {'dl'}), 'dd': ({'dt', 'dd'}, {'dl'}),
           'tr': ({'tr', 'td', 'th'}, {'table', 'thead', 'tbody', 'tfoot'}),
           'td': ({'td', 'th'}, {'tr'}), 'th': ({'td', 'th'}, {'tr'}),
           'option': ({'option'}, {'select', 'optgroup', 'datalist'})}
# Un bloc ouvert dans un paragraphe le ferme.
P_CLOSERS = {'ul', 'ol', 'dl', 'div', 'table', 'form', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6',
             'pre', 'blockquote', 'section', 'article', 'main', 'nav', 'header', 'footer',
             'aside', 'figure', 'hr', 'address', 'fieldset', 'details', 'menu'}
P_RULE = IMPLIED['p']
# Conteneurs entre lesquels une page longue ne doit pas être coupée.
NOCUT = {'TABLE', 'FORM', 'UL', 'OL', 'DL', 'PRE', 'BLOCKQUOTE', 'SELECT', 'TEXTAREA'}
TEXT_INPUTS = {'text', 'search', 'email', 'url', 'tel', 'number', 'date', 'time',
               'datetime-local', 'month', 'week', 'color', 'range', ''}

# Caractères absents de windows-1252 : équivalents lisibles.
TRANSLIT = {
    '\u202f': ' ', '\u2009': ' ', '\u2007': ' ', '\u2002': ' ', '\u2003': ' ',
    '\u2004': ' ', '\u2005': ' ', '\u2008': ' ', '\u200a': ' ',
    '\u200b': '', '\u200c': '', '\u200d': '', '\u2060': '', '\ufeff': '',
    '\u2010': '-', '\u2011': '-', '\u2012': '-', '\u2212': '-', '\u2043': '-',
    '\u2192': '->', '\u2190': '<-', '\u2194': '<->', '\u21d2': '=>', '\u2191': '^',
    '\u2193': 'v', '\u2264': '<=', '\u2265': '>=', '\u2260': '!=', '\u2248': '~',
    '\u221e': 'infini', '\u2713': 'oui', '\u2714': 'oui', '\u2717': 'non', '\u2718': 'non',
    '\u2605': '*', '\u2606': '*', '\u25b6': '>', '\u25c0': '<', '\u25b2': '^',
    '\u25bc': 'v', '\u2715': 'x', '\u2032': "'", '\u2033': '"', '\u2116': 'No',
    '\u201f': '"', '\u201b': "'",
}

def _translit(err):
    bad = err.object[err.start:err.end]
    out = []
    for ch in bad:
        if ch in TRANSLIT:
            out.append(TRANSLIT[ch])
            continue
        if unicodedata.category(ch) in ('So', 'Cs', 'Co', 'Cn', 'Sk') or 0x1f000 <= ord(ch):
            continue                        # pictogrammes, émojis : rien
        base = ''.join(c for c in unicodedata.normalize('NFKD', ch)
                       if not unicodedata.combining(c))
        try:
            base.encode('cp1252')
            out.append(base)
        except UnicodeEncodeError:
            out.append('?')
    return ''.join(out), err.end


codecs.register_error('pc486', _translit)


def encode(text):
    """Texte en windows-1252, translittéré (jamais d'erreur)."""
    return text.encode('cp1252', 'pc486')


def readable(text):
    """Le texte garde-t-il une lettre ou un chiffre en windows-1252 ? (« العربية » : non)"""
    return any(c.isalnum() for c in encode(text).decode('cp1252'))


class El:
    __slots__ = ('tag', 'attrs', 'kids', 'parent')

    def __init__(self, tag, attrs=None, parent=None):
        self.tag, self.attrs, self.kids, self.parent = tag, attrs or {}, [], parent

    def text(self):
        out = []
        for k in self.kids:
            if isinstance(k, str):
                out.append(k)
            elif k.tag not in DROP and not hidden(k):
                out.append(k.text() if k.tag != 'img' else k.attrs.get('alt', ''))
        return ''.join(out)

    def iter(self):
        for k in self.kids:
            if isinstance(k, El):
                yield k
                yield from k.iter()


def hidden(el):
    a = el.attrs
    if 'hidden' in a or a.get('aria-hidden', '').lower() == 'true':
        return True
    style = a.get('style', '').replace(' ', '').lower()
    return 'display:none' in style or 'visibility:hidden' in style


class Tree(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = El('#root')
        self.cur = self.root
        self.title = ''
        self._in_title = False

    def handle_starttag(self, tag, attrs):
        a = {k.lower(): (v if v is not None else '') for k, v in attrs}
        if tag == 'title':
            self._in_title = True
            return
        rule = IMPLIED.get(tag) or (P_RULE if tag in P_CLOSERS else None)
        if rule:
            # Ferme l'élément du même groupe le plus haut avant le conteneur :
            # un <tr> ferme la cellule ouverte et la ligne qui la contient.
            closes, scope = rule
            n, last = self.cur, None
            while n is not self.root and n.tag not in scope:
                if n.tag in closes:
                    last = n
                n = n.parent
            if last is not None:
                self.cur = last.parent
        el = El(tag, a, self.cur)
        self.cur.kids.append(el)
        if tag not in VOID:
            self.cur = el

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOID and self.cur.tag == tag:
            self.cur = self.cur.parent

    def handle_endtag(self, tag):
        if tag == 'title':
            self._in_title = False
            return
        n = self.cur
        while n is not self.root:
            if n.tag == tag:
                self.cur = n.parent
                return
            n = n.parent

    def handle_data(self, data):
        if self._in_title:
            self.title += data
        else:
            self.cur.kids.append(data)


def own_rows(table):
    """Lignes du tableau lui-même (thead, tbody, tfoot), pas celles des tableaux inclus."""
    for k in table.kids:
        if isinstance(k, El):
            if k.tag == 'tr':
                yield k
            elif k.tag in ('thead', 'tbody', 'tfoot'):
                yield from (r for r in k.kids if isinstance(r, El) and r.tag == 'tr')


def attr(s):
    return '"' + s.replace('&', '&amp;').replace('"', '&quot;').replace('<', '&lt;') + '"'


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def squash(s):
    return re.sub(r'\s+', ' ', s).strip()


# Liens de redirection des moteurs de recherche : la cible est dans un paramètre.
UNWRAP = (('duckduckgo.com', '/l/', 'uddg'), ('google.com', '/url', 'q'),
          ('bing.com', '/ck/a', 'u'))


def unwrap(url):
    parts = urlsplit(url)
    for host, path, key in UNWRAP:
        if parts.hostname and parts.hostname.endswith(host) and parts.path.startswith(path):
            for k, v in parse_qsl(parts.query):
                if k == key and v.startswith(('http://', 'https://')):
                    return v
    return url


def proxied(url):
    """Adresse vue par le navigateur : https:// -> http:// ; None si inutilisable."""
    parts = urlsplit(url)
    scheme = parts.scheme.lower()
    if scheme == 'https':
        return urlunsplit(('http',) + tuple(parts[1:]))
    if scheme in ('http', 'mailto', 'news', 'ftp'):
        return url
    return None


class Writer:
    def __init__(self, base, anchors, labelled, numbers=True):
        self.base, self.anchors, self.labelled = base, anchors, labelled
        self.numbers = numbers
        self.links = []         # cibles des liens numérotés, dans l'ordre (n = rang + 1)
        self.out = []
        self.cuts = []          # positions où une longue page peut être coupée
        self.nocut = 0
        self.pre = 0
        self.form = 0
        self.space = True       # dernier caractère émis : blanc ou début de bloc
        self.seen = {}

    # --- émission
    def tag(self, s):
        self.out.append(s)

    def text(self, s):
        if not s:
            return
        if self.pre:
            self.out.append(esc(s))
            self.space = s[-1:].isspace()
            return
        s = re.sub(r'\s+', ' ', s)
        if self.space:
            s = s.lstrip(' ')
        if s:
            self.out.append(esc(s))
            self.space = s.endswith(' ')

    def block(self):
        """Limite de bloc : saut de ligne dans le source, point de coupe possible."""
        if self.out and not self.out[-1].endswith('\n'):
            self.out.append('\n')
        self.space = True
        if not self.nocut:
            self.cuts.append(len(self.out))

    def open(self, name, attrs=''):
        if name in NOCUT:
            self.nocut += 1
        self.tag(f'<{name}{attrs}>')

    def close(self, name):
        self.tag(f'</{name}>')
        if name in NOCUT:
            self.nocut -= 1

    def url(self, href):
        if href is None:
            return None
        href = href.strip()
        if href.startswith('#'):
            return href
        try:
            return proxied(unwrap(urljoin(self.base, href)))
        except ValueError:
            return None

    # --- rendu
    def visible(self, el):
        """Un élément sans texte ni champ (liste vide, menu replié) n'est pas rendu."""
        key = id(el)
        if key not in self.seen:
            self.seen[key] = bool(squash(el.text())) or any(
                k.tag in ('input', 'select', 'textarea', 'iframe', 'video', 'audio', 'hr', 'br')
                and not hidden(k) for k in el.iter())
        return self.seen[key]

    def kids(self, el):
        for k in el.kids:
            if isinstance(k, str):
                self.text(k)
            else:
                self.node(k)

    def node(self, el):
        t = el.tag
        if t in DROP or hidden(el) and not (t == 'input' and self.form):
            return
        ident = el.attrs.get('id') or (el.attrs.get('name') if t == 'a' else None)
        if ident and ident in self.anchors:
            self.tag(f'<A NAME={attr(ident)}></A>')
            self.anchors.discard(ident)
        if t in BLOCK | SAME | {'p', 'table', 'form', 'a'} and t != 'pre' and not self.visible(el):
            return
        if t in BLOCK:
            self.block()
            if t == 'main':
                self.tag('<A NAME="contenu"></A>')
            region = self.region(el)
            if region:
                # marques pour le mode navigation d'Épaulard (tampon.py), ignorées par
                # Netscape
                self.tag(f'<!--pc486 d {region}-->')
            self.kids(el)
            if region:
                self.tag('<!--/pc486 d-->')
            self.block()
        elif t == 'p':
            self.block()
            self.tag('<P>')
            self.kids(el)
            self.tag('</P>')
            self.block()
        elif t in SAME:
            name = t.upper()
            if t in ('h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'ul', 'ol', 'dl', 'blockquote', 'pre'):
                self.block()
            if t == 'pre':
                self.pre += 1
            self.open(name)
            if t in ('li', 'dt', 'dd'):
                self.space = True
            self.kids(el)
            self.close(name)
            if t == 'pre':
                self.pre -= 1
            if t in ('h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'ul', 'ol', 'dl', 'blockquote', 'pre', 'li'):
                self.block()
        elif t in INLINE:
            self.tag(f'<{INLINE[t]}>')
            self.kids(el)
            self.tag(f'</{INLINE[t]}>')
        elif t == 'q':
            self.text('« ')
            self.kids(el)
            self.text(' »')
        elif t == 'br':
            self.tag('<BR>\n')
            self.space = True
        elif t == 'hr':
            self.block()
            self.tag('<HR>')
            self.block()
        elif t == 'a':
            self.link(el)
        elif t == 'img':
            alt = squash(el.attrs.get('alt', ''))
            if alt:
                self.text(' ')
                self.tag('<!--pc486 g-->')
                self.text(f'[{alt}]')
                self.tag('<!--/pc486 g-->')
                self.text(' ')
        elif t in ('iframe', 'frame'):
            href = self.url(el.attrs.get('src'))
            name = squash(el.attrs.get('title', '') or el.attrs.get('name', '')) or 'contenu inclus'
            if href:
                self.block()
                self.tag('<!--pc486 m-->')
                self.tag(f'<A HREF={attr(href)}>')
                self.text(f'[Cadre : {name}]')
                self.tag('</A>')
                self.tag('<!--/pc486 m-->')
                self.block()
        elif t in ('video', 'audio'):
            src = el.attrs.get('src') or next((s.attrs.get('src') for s in el.iter()
                                               if s.tag == 'source' and s.attrs.get('src')), None)
            href = self.url(src)
            if href:
                self.tag('<!--pc486 o-->')
                self.tag(f'<A HREF={attr(href)}>')
                self.text('[Vidéo]' if t == 'video' else '[Son]')
                self.tag('</A>')
                self.tag('<!--/pc486 o-->')
        elif t == 'table':
            self.table(el)
        elif t == 'form':
            self.formel(el)
        elif t in ('input', 'button', 'select', 'textarea'):
            if self.form:               # hors formulaire : rien à envoyer (menus CSS…)
                self.control(el)
        else:
            self.kids(el)

    REGIONS = {'nav': 'navigation', 'main': 'main', 'aside': 'complementary',
               'search': 'search'}

    def region(self, el):
        """Région (repère ARIA) : rôle explicite, sinon élément HTML5 ; en-tête et pied
        de page seulement hors d'un article ou d'une section."""
        role = el.attrs.get('role', '').strip().lower()
        if role in ('banner', 'navigation', 'main', 'contentinfo', 'complementary', 'search',
                    'region'):
            return role
        if el.tag in self.REGIONS:
            return self.REGIONS[el.tag]
        if el.tag in ('header', 'footer'):
            p = el.parent
            while p is not None and p.tag not in ('article', 'section', 'main', 'aside', 'nav'):
                p = p.parent
            if p is None:
                return 'banner' if el.tag == 'header' else 'contentinfo'
        return None

    def link(self, el):
        href = self.url(el.attrs.get('href'))
        label = squash(el.text())
        fallback = ''
        if not label or not readable(label):
            # sans texte, ou écriture absente de windows-1252 (langues de Wikipédia)
            fallback = squash(el.attrs.get('aria-label', '') or el.attrs.get('title', ''))
            if label and not fallback:
                fallback = el.attrs.get('hreflang') or el.attrs.get('lang') or ''
            if not fallback:
                path = urlsplit(el.attrs.get('href', '')).path.rstrip('/')
                fallback = path.rsplit('/', 1)[-1] or 'lien'
        if not href:
            self.kids(el)
            return
        self.tag(f'<A HREF={attr(href)}>')
        if fallback:
            self.text(fallback)
        else:
            self.kids(el)
        self.tag('</A>')
        if self.numbers and not href.startswith('#'):
            # Netscape 3 n'atteint pas les liens au clavier : « Aller au lien n° »
            self.links.append(href)
            # marqué : le tampon d'Épaulard ne le lit pas (navigation sans numéros)
            self.tag(f'<!--pc486 num--> [{len(self.links)}]')
            self.space = False

    def table(self, el):
        rows = [r for r in own_rows(el) if not hidden(r)]
        data = (any(isinstance(c, El) and c.tag == 'th' for r in rows for c in r.kids)
                or any(isinstance(c, El) and c.tag == 'caption' for c in el.kids))
        layout = el.attrs.get('role', '') in ('presentation', 'none')
        if not data or layout or not rows:
            # Tableau de mise en page : cellules lues l'une après l'autre.
            for r in rows or [el]:
                for c in (r.kids if rows else el.kids):
                    if isinstance(c, El) and c.tag in ('td', 'th'):
                        self.block()
                        self.kids(c)
                        self.block()
                    elif isinstance(c, El):
                        self.node(c)
                    else:
                        self.text(c)
            return
        self.block()
        self.open('TABLE', ' BORDER=1')
        for cap in (c for c in el.kids if isinstance(c, El) and c.tag == 'caption'):
            self.tag('<CAPTION>')
            self.kids(cap)
            self.tag('</CAPTION>')
        for r in rows:
            self.tag('<TR>')
            for c in r.kids:
                if not isinstance(c, El) or c.tag not in ('td', 'th') or hidden(c):
                    continue
                extra = ''.join(f' {k.upper()}={attr(c.attrs[k])}' for k in ('colspan', 'rowspan')
                                if c.attrs.get(k, '').isdigit())
                self.tag(f'<{c.tag.upper()}{extra}>')
                self.space = True
                self.kids(c)
                self.tag(f'</{c.tag.upper()}>')
            self.tag('</TR>\n')
        self.close('TABLE')
        self.block()

    def formel(self, el):
        if self.form:
            self.kids(el)
            return
        action = self.url(el.attrs.get('action') or self.base) or ''
        method = 'POST' if el.attrs.get('method', '').lower() == 'post' else 'GET'
        self.block()
        self.open('FORM', f' ACTION={attr(action)} METHOD={method}')
        self.form += 1
        self.kids(el)
        self.form -= 1
        self.close('FORM')
        self.block()

    def caption(self, el):
        """Libellé visible d'un champ sans étiquette <label> (aria-label, placeholder)."""
        if el.attrs.get('id') in self.labelled:
            return
        lab = squash(el.attrs.get('aria-label', '') or el.attrs.get('placeholder', '')
                     or el.attrs.get('title', ''))
        if lab:
            self.text(f' {lab} : ')

    def control(self, el):
        t, a = el.tag, el.attrs
        name = f' NAME={attr(a["name"])}' if a.get('name') else ''
        if t == 'input':
            kind = a.get('type', '').lower()
            if kind == 'hidden':
                self.tag(f'<INPUT TYPE=HIDDEN{name} VALUE={attr(a.get("value", ""))}>')
            elif kind in TEXT_INPUTS or kind == 'password':
                self.caption(el)
                size = a.get('size') if a.get('size', '').isdigit() else '30'
                typ = 'PASSWORD' if kind == 'password' else 'TEXT'
                val = f' VALUE={attr(a["value"])}' if a.get('value') else ''
                self.tag(f'<INPUT TYPE={typ}{name}{val} SIZE={size}>')
            elif kind in ('checkbox', 'radio'):
                self.caption(el)
                checked = ' CHECKED' if 'checked' in a else ''
                self.tag(f'<INPUT TYPE={kind.upper()}{name} VALUE={attr(a.get("value", "on"))}{checked}>')
            elif kind in ('submit', 'image', 'reset') and self.form:
                label = a.get('value') or a.get('alt') or ('Effacer' if kind == 'reset' else 'Envoyer')
                typ = 'RESET' if kind == 'reset' else 'SUBMIT'
                self.tag(f'<INPUT TYPE={typ}{name} VALUE={attr(squash(label))}>')
        elif t == 'button':
            kind = a.get('type', 'submit').lower()
            label = squash(el.text()) or squash(a.get('aria-label', '') or a.get('title', '')) or 'Envoyer'
            if self.form and kind == 'submit':
                val = f' VALUE={attr(squash(a.get("value", "")) or label)}'
                self.tag(f'<INPUT TYPE=SUBMIT{name}{val}>')
        elif t == 'select':
            self.caption(el)
            multiple = ' MULTIPLE' if 'multiple' in a else ''
            self.tag(f'<SELECT{name}{multiple}>')
            for o in el.iter():
                if o.tag != 'option':
                    continue
                label = squash(o.text()) or squash(o.attrs.get('label', ''))
                val = f' VALUE={attr(o.attrs["value"])}' if 'value' in o.attrs else ''
                sel = ' SELECTED' if 'selected' in o.attrs else ''
                self.tag(f'<OPTION{val}{sel}>{esc(label)}')
            self.tag('</SELECT>')
        elif t == 'textarea':
            self.caption(el)
            self.tag(f'<TEXTAREA{name} ROWS=6 COLS=60>{esc(el.text())}</TEXTAREA>')


class Page:
    """Résultat : titre, parties HTML 3.2 (une seule si la page est courte)."""

    def __init__(self, title, parts, refresh=None, links=()):
        self.title, self.parts, self.refresh, self.links = title, parts, refresh, list(links)


def convert(markup, base, limit=60000, numbers=True):
    tree = Tree()
    tree.feed(markup)
    tree.close()
    root = tree.root
    for el in root.iter():
        if el.tag == 'base' and el.attrs.get('href'):
            base = urljoin(base, el.attrs['href'])
            break
    refresh = None
    for el in root.iter():
        if el.tag == 'meta' and el.attrs.get('http-equiv', '').lower() == 'refresh':
            m = re.search(r'url\s*=\s*[\'"]?([^\'";]+)', el.attrs.get('content', ''), re.I)
            if m:
                refresh = proxied(urljoin(base, m.group(1).strip()))
    anchors = {el.attrs['href'][1:] for el in root.iter()
               if el.tag == 'a' and el.attrs.get('href', '').startswith('#') and len(el.attrs['href']) > 1}
    labelled = {el.attrs.get('for') for el in root.iter() if el.tag == 'label' and el.attrs.get('for')}
    labelled |= {k.attrs.get('id') for el in root.iter() if el.tag == 'label'
                 for k in el.iter() if k.tag in ('input', 'select', 'textarea')}
    w = Writer(base, anchors, labelled, numbers)
    first = next((el for el in root.iter() if el.tag == 'a' and el.attrs.get('href')), None)
    skip = first is not None and first.attrs['href'].startswith('#')
    if not skip and any(el.tag == 'main' for el in root.iter()):
        w.tag('<A HREF="#contenu">Aller au contenu</A>\n')
        w.cuts.append(len(w.out))
    w.kids(root)
    w.block()
    title = squash(tree.title)
    return Page(title, split(w.out, w.cuts, limit), refresh, w.links)


def split(out, cuts, limit):
    """Parties d'au moins limit caractères (sauf la dernière), coupées aux seules
    limites de bloc sûres : jamais dans un tableau, un formulaire ou une liste."""
    cutset = set(cuts)
    parts, start, size = [], 0, 0
    for i, s in enumerate(out):
        if i in cutset and i > start and size >= limit:
            parts.append(''.join(out[start:i]))
            start, size = i, 0
        size += len(s)
    parts.append(''.join(out[start:]))
    return [p for p in parts if p.strip()] or ['']


def document(title, body, charset='windows-1252'):
    """Document HTML 3.2 complet (texte), à encoder par encode()."""
    return ('<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2 Final//EN">\n<HTML><HEAD>\n'
            f'<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset={charset}">\n'
            f'<TITLE>{esc(title)}</TITLE>\n</HEAD>\n<BODY BGCOLOR="#FFFFFF" TEXT="#000000">\n'
            f'{body}\n</BODY></HTML>\n')
