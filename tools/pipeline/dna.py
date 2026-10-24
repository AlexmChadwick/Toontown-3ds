"""Text DNA parser and storage resolver.

The DNA text format is a tree of elements::

    keyword "optional name" [ <values or child elements> ]

Values are numbers, quoted strings (no escapes) or bare identifiers such as
STREET_POINT. Commas are separators. Comments are // to end of line and
/* ... */. Property lines like ``pos [ 1 2 3 ]`` are elements with values and
no children, so one Node class covers everything.

Storage files (``model``/``hood_model``/``place_model`` with ``store_node``,
``store_texture``, ``store_font``) are loaded by :class:`Storage`, which
resolves a code to a Panda NodePath inside the loaded model.
"""
import os
import re

from panda3d.core import Filename, NodePath, TexturePool, getModelPath

_TOKEN = re.compile(r'''
    (?P<ws>\s+) |
    (?P<lcomment>//[^\n]*) |
    (?P<ccomment>/\*.*?\*/) |
    (?P<open>\[) |
    (?P<close>\]) |
    (?P<comma>,) |
    (?P<string>"[^"]*") |
    (?P<number>[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?) |
    (?P<ident>[A-Za-z_][A-Za-z0-9_]*)
''', re.VERBOSE | re.DOTALL)


class DNAParseError(Exception):
    pass


class Node:
    """One DNA element: ``kind "name" [ values... children... ]``."""
    __slots__ = ('kind', 'name', 'values', 'children', 'line')

    def __init__(self, kind, name=None, line=0):
        self.kind = kind
        self.name = name
        self.values = []
        self.children = []
        self.line = line

    def __repr__(self):
        return f'Node({self.kind!r}, {self.name!r}, values={self.values!r}, children={len(self.children)})'

    def find(self, kind):
        for c in self.children:
            if c.kind == kind:
                return c
        return None

    def find_all(self, kind):
        return [c for c in self.children if c.kind == kind]

    def get(self, kind, default=None):
        """Values of the first child property ``kind``; a single value is unwrapped."""
        c = self.find(kind)
        if c is None:
            return default
        return c.values[0] if len(c.values) == 1 else list(c.values)

    def vec(self, kind, default):
        c = self.find(kind)
        if c is None or len(c.values) < len(default):
            return tuple(default)
        return tuple(float(v) for v in c.values[:len(default)])

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()


def tokenize(text, warnings=None):
    """Split DNA text into (kind, value, line) tokens.

    Junk the game's lexer would choke on (one storage file starts with a stray
    "--" line) is skipped to end of line and reported in ``warnings``.
    """
    pos, line = 0, 1
    out = []
    n = len(text)
    while pos < n:
        m = _TOKEN.match(text, pos)
        if not m:
            end = text.find('\n', pos)
            end = n if end < 0 else end
            if warnings is None:
                raise DNAParseError(f'line {line}: unexpected character {text[pos]!r}')
            warnings.append(f'line {line}: skipped {text[pos:end]!r}')
            pos = end
            continue
        kind = m.lastgroup
        s = m.group()
        if kind == 'string':
            out.append(('string', s[1:-1], line))
        elif kind == 'number':
            out.append(('number', float(s), line))
        elif kind in ('open', 'close', 'ident'):
            out.append((kind, s, line))
        line += s.count('\n')
        pos = m.end()
    return out


def parse(text, warnings=None):
    """Parse DNA text into a synthetic root Node (kind 'root') holding top-level elements."""
    toks = tokenize(text, warnings)
    i = 0
    root = Node('root')
    stack = [root]
    n = len(toks)
    while i < n:
        kind, val, line = toks[i]
        if kind == 'close':
            if len(stack) == 1:
                raise DNAParseError(f'line {line}: unbalanced ]')
            stack.pop()
            i += 1
            continue
        # An element starts with an identifier followed by '[' or by "name" '['.
        if kind == 'ident':
            if i + 1 < n and toks[i + 1][0] == 'open':
                node = Node(val.lower(), None, line)
                stack[-1].children.append(node)
                stack.append(node)
                i += 2
                continue
            if i + 2 < n and toks[i + 1][0] == 'string' and toks[i + 2][0] == 'open':
                node = Node(val.lower(), toks[i + 1][1], line)
                stack[-1].children.append(node)
                stack.append(node)
                i += 3
                continue
        if kind == 'open':
            raise DNAParseError(f'line {line}: unexpected [')
        if len(stack) == 1:
            # Stray text at top level (a comment that lost its //): skip the line.
            if warnings is None:
                raise DNAParseError(f'line {line}: value {val!r} outside of any element')
            warnings.append(f'line {line}: skipped stray top-level text')
            while i < n and toks[i][2] == line:
                i += 1
            continue
        stack[-1].values.append(val)
        i += 1
    if len(stack) != 1:
        raise DNAParseError(f'unterminated element {stack[-1].kind!r} from line {stack[-1].line}')
    return root


def parse_file(path, warnings=None):
    with open(path, 'r', encoding='latin-1') as f:
        return parse(f.read(), warnings)


class Storage:
    """Code -> NodePath / Texture / font path, from one or more storage DNA files.

    Lookup order matches the game: plain ``model`` entries, then ``hood_model``,
    then ``place_model``. Later files override earlier ones within a map.
    """

    def __init__(self, loader, resources):
        self.loader = loader
        self.resources = os.path.abspath(resources)
        mp = getModelPath()
        res = Filename.fromOsSpecific(self.resources)
        if not any(mp.getDirectory(k) == res for k in range(mp.getNumDirectories())):
            mp.prependDirectory(res)
        self.maps = {'model': {}, 'hood_model': {}, 'place_model': {}}
        self.categories = {}      # code -> category
        self.textures = {}        # code -> Texture
        self.fonts = {}           # code -> path string
        self.errors = []          # problems found while loading storage
        self._models = {}         # model path -> NodePath (cache)

    def model(self, path):
        if path not in self._models:
            fname = path
            if not os.path.splitext(path)[1]:
                # DNA omits the extension; the game's default-model-extension is .bam.
                for ext in ('.bam', '.egg', '.egg.pz'):
                    if os.path.exists(os.path.join(self.resources, path + ext)):
                        fname = path + ext
                        break
            np_ = self.loader.loadModel(fname, okMissing=True)
            if np_ is None:
                self.errors.append(f'model not found: {path}')
                np_ = NodePath()
            self._models[path] = np_
        return self._models[path]

    def load(self, path):
        warn = []
        root = parse_file(path, warn)
        self.errors.extend(f'{os.path.basename(path)}: {w}' for w in warn)
        for el in root.children:
            if el.kind in self.maps:
                model = self.model(el.name)
                for sn in el.find_all('store_node'):
                    self._store_node(el.kind, el.name, model, sn)
            elif el.kind == 'store_texture':
                vals = el.values
                code, tpath = (vals[1], vals[2]) if len(vals) >= 3 else (vals[0], vals[1])
                tex = TexturePool.loadTexture(tpath)
                if tex is None:
                    self.errors.append(f'texture not found: {tpath} ({code})')
                else:
                    self.textures[code] = tex
            elif el.kind == 'store_font':
                vals = el.values
                code, fpath = (vals[1], vals[2]) if len(vals) >= 3 else (vals[0], vals[1])
                self.fonts[code] = fpath
            # store_suit_point and anything else: not needed for geometry.

    def _store_node(self, kind, model_path, model, sn):
        vals = [str(v) for v in sn.values]
        if len(vals) < 2:
            self.errors.append(f'bad store_node in {model_path}: {vals}')
            return
        category, code = vals[0], vals[1]
        node_name = vals[2] if len(vals) >= 3 else code
        if model.isEmpty():
            return
        if node_name == '':
            found = model
        else:
            found = model.find('**/' + node_name)
        if found.isEmpty():
            self.errors.append(f'node {node_name!r} for code {code!r} not in {model_path}')
            return
        self.maps[kind][code] = found
        self.categories[code] = category

    def find_node(self, code):
        for kind in ('model', 'hood_model', 'place_model'):
            np_ = self.maps[kind].get(code)
            if np_ is not None:
                return np_
        return None

    def find_texture(self, code):
        return self.textures.get(code)
