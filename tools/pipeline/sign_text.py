"""Offline DNA sign layout, independently implemented from observed DNA behaviour.

Baselines advance by scaled item width, alternate roll/offsets, and centre the
completed run. Curved baselines use chord widths on an ellipse. Flags c/b/d mean
uppercase, enlarged word initials, and shadow. Upstream source was consulted for
behaviour only; no upstream implementation is copied. All output is ordinary
Panda geometry, so the runtime needs neither fonts nor a text renderer.
"""
import math
from panda3d.core import DecalEffect, NodePath, TextNode

_FONT_TEXTURES = set()


def is_font_texture(tex):
    return int(tex.this) in _FONT_TEXTURES


class Layout:
    """Pure layout state shared by text and graphic items, with deterministic offsets."""
    def __init__(self, node):
        self.node = node
        self.scale = node.vec('scale', (1, 1, 1))
        self.width = float(node.get('width', 0))
        self.height = float(node.get('height', 0))
        self.kern = float(node.get('kern', 0))
        self.stumble = float(node.get('stumble', 0))
        self.stomp = float(node.get('stomp', 0))
        self.wiggle = float(node.get('wiggle', 0))
        self.indent = float(node.get('indent', 0))
        self.flags = str(node.get('flags', ''))
        self.count = 0
        self.advance = 0.0
        self.angle = 0.0
        self.centre_angle = 0.0
        self.blank = True

    @property
    def curved(self):
        return bool(self.width or self.height)

    def place(self, item, width, letters=None):
        scale = [a*b for a,b in zip(self.scale,item.vec('scale',(1,1,1)))]
        if letters is not None:
            first = bool(letters and not letters[0].isspace() and self.blank)
            self.blank = not letters or letters[0].isspace()
            if first and 'b' in self.flags:
                scale[0] *= 1.5
                scale[2] *= 1.5
        p = list(item.vec('pos',(0,0,0)))
        h = list(item.vec('nhpr',item.vec('hpr',(0,0,0))))
        sign = 1 if self.count % 2 else -1
        span = width * scale[0]
        if not self.curved:
            p[0] += self.advance + self.count*self.kern + sign*self.stumble
            p[2] += sign*self.stomp
            h[2] -= sign*self.wiggle
            self.advance += span
        else:
            rx,rz = self.width/2,self.height/2
            radius = rx if abs(rx)>1e-8 else (rz if abs(rz)>1e-8 else 1.0)
            inner = self.width<0
            indent = -self.indent if self.width<0 else self.indent
            offset = p[0] if self.width<0 else -p[0]
            angle = self.angle + math.radians(indent) + offset/radius
            radial = p[2] + sign*self.stomp
            if self.width<0:
                radial = -radial
            p[0] = (rx+radial)*math.cos(math.pi/2+angle)
            p[2] = (rz+radial)*math.sin(math.pi/2+angle)
            h[2] -= math.degrees(self.angle)+indent+sign*self.wiggle
            r = max(math.hypot(p[0],p[2]),1e-6)
            letter_arc = 2*math.asin(max(-1,min(1,span/(2*r))))
            before = self.angle
            self.angle -= letter_arc
            self.centre_angle = before if inner else self.angle
            gap = self.kern-sign*self.stumble
            self.angle -= 2*math.asin(max(-1,min(1,gap/(2*r))))
            if not inner:
                h[2] -= math.degrees(self.angle-before)/2
        self.count += 1
        return p,h,scale

    def centre(self):
        p=list(self.node.vec('pos',(0,0,0)))
        h=list(self.node.vec('nhpr',self.node.vec('hpr',(0,0,0))))
        a=math.radians(-h[2])
        if self.curved:
            p[0] -= self.width/2*math.cos(a+math.pi/2)
            p[2] -= self.height/2*math.sin(a+math.pi/2)
            h[2] += math.degrees(self.centre_angle)/2
        else:
            last=max(0,self.count-1)
            gap=last*self.kern+(self.stumble if last%2 else -self.stumble)
            half=(self.advance+gap)/2
            p[0] -= half*math.cos(a)
            p[2] -= half*math.sin(a)
        p[1] -= .05  # separate text/shadow from its sign board after quantization
        return p,h


def load_font(asm, code):
    store=asm.storage
    if not hasattr(store,'_sign_fonts'):
        store._sign_fonts={}
    if code not in store._sign_fonts:
        path=store.fonts.get(code)
        if path:
            if not path.lower().endswith(('.ttf','.bam','.egg')):
                path += '.bam'
            font=store.loader.loadFont(path,okMissing=True)
        else:
            model=store.find_node(code) if code else None
            if model is not None:
                from panda3d.core import StaticTextFont
                font=StaticTextFont(model.node())
            else:
                font=TextNode.getDefaultFont()
                if code:
                    asm.unresolved['font:'+code] += 1
        if font is None:
            raise ValueError('cannot load DNA sign font '+str(code))
        store._sign_fonts[code]=font
    return store._sign_fonts[code]


def assemble_baseline(asm, node, parent):
    items=[c for c in node.children if c.kind in ('text','graphic')]
    if not items:
        return None
    layout=Layout(node)
    if layout.width<0 or layout.height<0:
        items.reverse()
    root=parent.attachNewNode('baseline')
    root.setTag('sign_baseline','1')
    root.setTag('letters',''.join(str(c.get('letters','')) for c in items if c.kind=='text'))
    root.setColorOff()
    root.setDepthWrite(False)
    # Mark the nearest geometric ancestor as a decal base, matching sign layering.
    ancestor=parent
    while not ancestor.isEmpty():
        if ancestor.node().isGeomNode():
            ancestor.node().setEffect(DecalEffect.make())
            break
        ancestor=ancestor.getParent()
    for item in items:
        if item.kind=='text':
            letters=str(item.get('letters',''))
            if 'c' in layout.flags:
                letters=letters.upper()
            font=load_font(asm,str(item.get('code',node.get('code',''))))
            text=TextNode('sign_letters')
            text.setFont(font)
            color=item.vec('color',node.vec('color',(1,1,1,1)))
            text.setTextColor(*color)
            if 'd' in layout.flags:
                text.setShadow(.03,.03)
                text.setShadowColor(color[0]*.3,color[1]*.3,color[2]*.3,color[3]*.7)
            text.setText(letters)
            width=text.getWidth()
            geom=NodePath(text.generate())
            for tex in geom.findAllTextures():
                _FONT_TEXTURES.add(int(tex.this))
            geom.reparentTo(root)

            geom.setTag('sign_text','1')
            asm.sign_letters += len(letters)
            spec=layout.place(item,width,letters)
        else:
            src=asm._find(item.get('code',''))
            width=float(item.get('width',0))
            spec=layout.place(item,width)
            if src is None:
                continue
            geom=src.copyTo(root)
            geom.setColor(item.vec('color',node.vec('color',(1,1,1,1))))
        geom.setPosHprScale(*(tuple(v) for v in spec))
    p,h=layout.centre()
    root.setPosHpr(tuple(p),tuple(h))
    asm.sign_baselines += 1
    return root
