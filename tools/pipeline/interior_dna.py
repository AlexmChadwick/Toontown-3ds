"""DNA storage loader and catalog code resolver for interior generation.

Wraps tools/pipeline/dna.Storage to add catalog code indexing by category.
Upstream DistributedToonInterior, DistributedGagshopInterior, etc. use
DNAStorage.getNumCatalogCodes(category) and DNAStorage.getCatalogCode(category, index)
to select catalog items (TI_room, TI_wallpaper, TI_wainscotting, TI_chair, etc.).
"""
import os
from panda3d.core import TexturePool

import dna


class InteriorDNAStore:
    """Storage resolver with category -> [codes] catalog indexing."""

    def __init__(self, loader, resources):
        self.loader = loader
        self.resources = os.path.abspath(resources)
        self.storage = dna.Storage(loader, resources)
        self.catalog = {}       # category -> list of codes in definition order
        self.categories = {}    # code -> category
        self.textures = {}      # code -> Texture

    def load(self, path):
        """Load a storage DNA file and index catalog categories."""
        self.storage.load(path)
        warn = []
        root = dna.parse_file(path, warn)
        for el in root.children:
            if el.kind in ('model', 'hood_model', 'place_model'):
                for sn in el.find_all('store_node'):
                    if len(sn.values) >= 2:
                        cat, code = str(sn.values[0]), str(sn.values[1])
                        codes = self.catalog.setdefault(cat, [])
                        if code not in codes:
                            codes.append(code)
                        self.categories[code] = cat
            elif el.kind == 'store_texture':
                vals = el.values
                if len(vals) >= 3:
                    cat, code, tpath = str(vals[0]), str(vals[1]), str(vals[2])
                    codes = self.catalog.setdefault(cat, [])
                    if code not in codes:
                        codes.append(code)
                    self.categories[code] = cat
                elif len(vals) >= 2:
                    code, tpath = str(vals[0]), str(vals[1])
                else:
                    continue

                tex = self.storage.find_texture(code)
                if tex is None:
                    tex = TexturePool.loadTexture(tpath)
                if tex is not None:
                    self.textures[code] = tex

    def getNumCatalogCodes(self, category):
        return len(self.catalog.get(category, []))

    def getCatalogCode(self, category, index):
        codes = self.catalog.get(category, [])
        if 0 <= index < len(codes):
            return codes[index]
        raise IndexError(f'Catalog index {index} out of range for category {category!r}')

    def findNode(self, code):
        return self.storage.find_node(code)

    def findTexture(self, code):
        return self.textures.get(code) or self.storage.find_texture(code)

    find_node = findNode
    find_texture = findTexture
