"""Extract pinned suit data by evaluating literal tables and generateSuit only."""
import ast
from collections import Counter
from types import SimpleNamespace
from panda3d.core import VBase4
from battle_common import UP,RES,literal_table


def source_data():
    dna_path = 'toontown/suit/SuitDNA.py'
    codes = literal_table(dna_path,'suitHeadTypes')
    bodies = {c:body for body in 'abc' for c in literal_table(dna_path,f'suit{body.upper()}Types')}
    depts = literal_table(dna_path,'suitDepts')
    tree = ast.parse((UP/'toontown/suit/Suit.py').read_text())
    tables = {}
    for node in tree.body:
        if isinstance(node,ast.Assign) and isinstance(node.targets[0],ast.Name):
            try:
                tables[node.targets[0].id] = ast.literal_eval(node.value)
            except (ValueError,TypeError):
                pass
    fn = next(n for c in tree.body if isinstance(c,ast.ClassDef) and c.name=='Suit'
              for n in c.body if isinstance(n,ast.FunctionDef) and n.name=='generateSuit')
    colors = SimpleNamespace(corpPolyColor=VBase4(.95,.75,.75,1),legalPolyColor=VBase4(.75,.75,.95,1),
        moneyPolyColor=VBase4(.65,.95,.85,1),salesPolyColor=VBase4(.95,.75,.95,1))
    namespace = dict(cSize=4.14,bSize=5.29,aSize=6.06,SuitDNA=colors,VBase4=VBase4,
        SuitBattleGlobals=SimpleNamespace(SuitAttributes={c:dict(name=c) for c in codes}))
    exec(compile(ast.Module(body=[fn],type_ignores=[]),'Suit.generateSuit','exec'),namespace)
    specs = {}
    for index,code in enumerate(codes):
        heads = []
        suit = SimpleNamespace(style=SimpleNamespace(name=code),generateBody=lambda:None,
            generateHead=heads.append,setName=lambda _:None,generateHealthBar=lambda:None,
            generateCorporateMedallion=lambda:None,getGeomNode=lambda:SimpleNamespace(setScale=lambda _:None))
        suit.setHeight = lambda height:setattr(suit,'height',height)
        namespace['generateSuit'](suit)
        specs[code] = dict(code=code,body=bodies[code],dept=depts[index//8],parts=heads,
            scale=suit.scale,height=suit.height,nametag_z=suit.height+1,
            hand_color=list(suit.handColor),head_color=list(suit.headColor) if suit.headColor is not None else None,
            head_texture=suit.headTexture)
    attacks_tree = ast.parse((UP/'toontown/battle/SuitBattleGlobals.py').read_text())
    attacks = next(n.value for n in attacks_tree.body if isinstance(n,ast.Assign)
                   and isinstance(n.targets[0],ast.Name) and n.targets[0].id=='SuitAttacks')
    anim_of = {ast.literal_eval(k):ast.literal_eval(v.elts[0]) for k,v in zip(attacks.keys,attacks.values)}
    attrs = next(n.value for n in attacks_tree.body if isinstance(n,ast.Assign)
                 and isinstance(n.targets[0],ast.Name) and n.targets[0].id=='SuitAttributes')
    for key,value in zip(attrs.keys,attrs.values):
        code = ast.literal_eval(key)
        if code in specs:
            entry = next(v for k,v in zip(value.keys,value.values) if ast.literal_eval(k)=='attacks')
            specs[code]['attacks'] = [ast.literal_eval(a.elts[0]) for a in entry.elts]
    return specs,tables,anim_of


def animations(body,specs,tables,anim_of):
    phase = 3.5 if body == 'c' else 4
    prefix = f'/models/char/suit{body.upper()}-'
    clips = {}
    for key,ph in [('AllSuits',phase),('AllSuitsMinigame',4),('AllSuitsTutorialBattle',phase),('AllSuitsBattle',5)]:
        for name,suffix in tables[key]:
            if name in {'neutral','walk','pie-small-react','squirt-small-react','lose','victory',
                        'sidestep-left','sidestep-right','slip-forward','flail','landing'}:
                clips[name] = f'phase_{ph:g}{prefix}{suffix}'
    counts = Counter()
    variants = {}
    for code,spec in specs.items():
        if spec['body'] != body:
            continue
        used = Counter(anim_of[a] for a in spec['attacks'])
        for name,suffix,ph in tables[code]:
            path = f'phase_{ph:g}{prefix}{suffix}'
            if (RES/(path+'.bam')).is_file():
                counts[name] += used[name]
                variants.setdefault(name,Counter())[path] += max(1,used[name])
    selected = [name for name,_ in sorted(counts.items(),key=lambda item:(-item[1],item[0])) if counts[name]][:12]
    for name in selected:
        clips[name] = sorted(variants[name].items(),key=lambda item:(-item[1],item[0]))[0][0]
    return clips,{name:counts[name] for name in selected}
