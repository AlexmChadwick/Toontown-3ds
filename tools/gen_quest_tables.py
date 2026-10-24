#!/usr/bin/env python3
"""Generate source/quest/quest_data.{h,cpp} from the upstream ToonTask tables.

    .venv/bin/python -I tools/gen_quest_tables.py            # writes both files
    .venv/bin/python -I tools/gen_quest_tables.py --check    # exit 1 if they are stale

TT_UPSTREAM=<dir> overrides ref/open-toontown (extra checkouts do not populate submodules).

Nothing upstream is imported (the modules need Panda3D's otp/toontown libs). Each module is
parsed with ast and evaluated lazily by a small interpreter that understands literals,
tuples/lists/dicts, names, Module.attr, + % * -, unary minus and subscripts. Star imports are
followed (ToontownGlobals -> OTPGlobals).

Sources (docs/design/quests.md section 5):
  toontown/quest/Quests.py            QuestDict (id -> tier, from/to NPC, reward, next, dialog,
                                      quest class + parameters), RewardDict + Reward.getString
  toontown/toonbase/TTLocalizerEnglish.py  every template, the QuestDialog_* dicts, NPC names,
                                      zone titles, street/hood names, item names, gag names
  toontown/toon/NPCToons.py           NPCToonDict (npcId -> zone, name)
  toontown/battle/SuitBattleGlobals.py SuitAttributes[code] singular/plural names
  toontown/toonbase/ToontownGlobals.py hoodNameMap / StreetNames (via TTLocalizer)

Quest classes the runtime renders itself are mapped to a QuestType; every other class becomes
QuestType::Other (the runtime shows a generic fallback line).
"""
import argparse
import ast
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
UPSTREAM = os.environ.get('TT_UPSTREAM', os.path.join(ROOT, 'ref', 'open-toontown'))
TT = os.path.join(UPSTREAM, 'toontown')
OUT_H = os.path.join(ROOT, 'source', 'quest', 'quest_data.h')
OUT_CPP = os.path.join(ROOT, 'source', 'quest', 'quest_data.cpp')


# ---------------------------------------------------------------- upstream evaluator
class Cls:
    """A class defined in an upstream module (only its name matters)."""

    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return 'Cls(%s)' % self.name


class Unres:
    """An element that could not be evaluated (e.g. CogDisguiseGlobals parts)."""

    def __init__(self, what):
        self.what = what

    def __repr__(self):
        return 'Unres(%s)' % self.what


class Mod:
    def __init__(self, path, mods):
        with open(os.path.join(TT, path), encoding='utf-8') as f:
            self.tree = ast.parse(f.read())
        self.mods = mods
        self.defs = {}
        self.cache = {}
        self.stack = set()
        self.extra = {}
        self.stars = []
        for n in self.tree.body:
            if isinstance(n, ast.Assign):
                for t in n.targets:
                    if isinstance(t, ast.Name):
                        self.defs[t.id] = n.value
            elif isinstance(n, ast.ClassDef):
                self.defs[n.name] = Cls(n.name)
            elif isinstance(n, ast.ImportFrom) and n.names[0].name == '*':
                p = n.module.replace('.', '/') + '.py'
                p = p[len('toontown/'):] if p.startswith('toontown/') else '../' + p
                if os.path.exists(os.path.join(TT, p)):
                    self.stars.append(Mod(p, mods))

    def get(self, name):
        if name in self.cache:
            return self.cache[name]
        if name in self.extra:
            return self.extra[name]
        if name not in self.defs:
            for st in self.stars:
                try:
                    return st.get(name)
                except KeyError:
                    pass
            raise KeyError(name)
        if name in self.stack:
            raise RecursionError(name)
        self.stack.add(name)
        d = self.defs[name]
        v = d if isinstance(d, Cls) else self.ev(d)
        self.stack.discard(name)
        self.cache[name] = v
        return v

    def ev(self, n):
        if isinstance(n, ast.Constant):
            return n.value
        if isinstance(n, (ast.Tuple, ast.List)):
            out = []
            for e in n.elts:
                try:
                    out.append(self.ev(e))
                except KeyError as ex:
                    out.append(Unres(str(ex)))
            return tuple(out) if isinstance(n, ast.Tuple) else out
        if isinstance(n, ast.Dict):
            return {self.ev(k): self.ev(v) for k, v in zip(n.keys, n.values)}
        if isinstance(n, ast.Name):
            return self.get(n.id)
        if isinstance(n, ast.Attribute):
            b = n.value
            if isinstance(b, ast.Name) and b.id in self.mods:
                return self.mods[b.id].get(n.attr)
            raise KeyError(ast.dump(n))
        if isinstance(n, ast.BinOp):
            a, b = self.ev(n.left), self.ev(n.right)
            if isinstance(n.op, ast.Add):
                return a + b
            if isinstance(n.op, ast.Mod):
                return a % b
            if isinstance(n.op, ast.Mult):
                return a * b
            if isinstance(n.op, ast.Sub):
                return a - b
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, ast.USub):
            return -self.ev(n.operand)
        if isinstance(n, ast.Subscript):
            return self.ev(n.value)[self.ev(n.slice)]
        raise KeyError(ast.dump(n)[:160])


def load_upstream():
    mods = {}
    ttl = Mod('toonbase/TTLocalizerEnglish.py', mods)
    tg = Mod('toonbase/ToontownGlobals.py', mods)
    tbg = Mod('toonbase/ToontownBattleGlobals.py', mods)
    mods.update(TTLocalizer=ttl, ToontownGlobals=tg, ToontownBattleGlobals=tbg)
    q = Mod('quest/Quests.py', mods)
    mods['Quests'] = q
    # TTLocalizerEnglish uses the dialog status constants that Quests.py defines.
    for k in ('GREETING', 'QUEST', 'INCOMPLETE', 'INCOMPLETE_PROGRESS', 'INCOMPLETE_WRONG_NPC', 'COMPLETE',
              'LEAVING'):
        ttl.extra[k] = q.get(k)
    npcs = Mod('toon/NPCToons.py', mods)
    mods['NPCToons'] = npcs
    return ttl, tg, tbg, q, npcs


def suit_names():
    """code -> (singular, plural), in SuitAttributes order (read straight off the AST)."""
    ttl_mod = {}
    ttl = Mod('toonbase/TTLocalizerEnglish.py', ttl_mod)
    ttl_mod['TTLocalizer'] = ttl
    with open(os.path.join(TT, 'battle/SuitBattleGlobals.py'), encoding='utf-8') as f:
        tree = ast.parse(f.read())
    out = []
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], 'id', None) == 'SuitAttributes':
            for k, v in zip(node.value.keys, node.value.values):
                fields = {kk.value: vv for kk, vv in zip(v.keys, v.values) if isinstance(kk, ast.Constant)}
                out.append((k.value, ttl_eval(ttl, fields['singularname']), ttl_eval(ttl, fields['pluralname'])))
    return out


def ttl_eval(ttl, node):
    return ttl.ev(node)


# ---------------------------------------------------------------- model
# Mirrors enum class QuestType in source/quest/quest_data.h.
QUEST_TYPES = ['Other', 'Cog', 'CogNewbie', 'CogLevel', 'CogTrack', 'Building', 'BuildingNewbie', 'Visit',
               'DeliverItem', 'RecoverItem', 'DeliverGag', 'TrackChoice', 'Friend', 'Trolley', 'Phone',
               'Mailbox']
CLASS_TO_TYPE = {'CogQuest': 'Cog', 'CogNewbieQuest': 'CogNewbie', 'CogLevelQuest': 'CogLevel',
                 'CogTrackQuest': 'CogTrack', 'BuildingQuest': 'Building',
                 'BuildingNewbieQuest': 'BuildingNewbie', 'VisitQuest': 'Visit',
                 'DeliverItemQuest': 'DeliverItem', 'RecoverItemQuest': 'RecoverItem',
                 'DeliverGagQuest': 'DeliverGag', 'TrackChoiceQuest': 'TrackChoice',
                 'FriendQuest': 'Friend', 'TrolleyQuest': 'Trolley', 'PhoneQuest': 'Phone',
                 'MailboxQuest': 'Mailbox'}
TRACK_CODES = ['c', 'l', 'm', 's']  # CogTrackQuest/BuildingQuest.trackCodes
ANY = 1

# TTLocalizer names the runtime refers to (kS_<name> constants in quest_data.h).
STRING_NAMES = [
    'Period', 'QuestsCompleteString', 'QuestsNotChosenString', 'QuestsLocationString',
    'QuestInLocationString', 'QuestsLocationArticle', 'QuestsItemNameAndNum', 'Cog', 'Cogs', 'TheCogs',
    'AFish', 'Level', 'QuestsRecoverItemQuestHolderString',
    # Cog / newbie
    'QuestsCogQuestDefeat', 'QuestsCogQuestDefeatDesc', 'QuestsCogQuestProgress', 'QuestsCogQuestHeadline',
    'QuestsCogNewNewbieQuestObjective', 'QuestsCogOldNewbieQuestObjective', 'QuestsNewbieQuestHeadline',
    'QuestsCogLevelQuestDefeat', 'QuestsCogLevelQuestDesc', 'QuestsCogLevelQuestDescC',
    'QuestsCogLevelQuestProgress', 'QuestsCogLevelQuestHeadline',
    'QuestsCogTrackQuestDefeat', 'QuestsCogTrackDefeatDesc', 'QuestsCogTrackQuestProgress',
    'QuestsCogTrackQuestHeadline',
    # buildings
    'QuestsBuildingQuestString', 'QuestsBuildingQuestDesc', 'QuestsBuildingQuestDescF',
    'QuestsBuildingQuestDescC', 'QuestsBuildingQuestDescCF', 'QuestsBuildingQuestProgressString',
    'QuestsBuildingQuestHeadline',
    # visit / deliver / recover
    'QuestsVisitQuestStringShort', 'QuestsVisitQuestStringLong', 'QuestsVisitQuestProgress',
    'QuestsVisitQuestHeadline',
    'QuestsDeliverItemQuestString', 'QuestsDeliverItemQuestStringLong', 'QuestsDeliverItemQuestProgress',
    'QuestsDeliverItemQuestHeadline',
    'QuestsRecoverItemQuestString', 'QuestsRecoverItemQuestProgress', 'QuestsRecoverItemQuestHeadline',
    'QuestsDeliverGagQuestString', 'QuestsDeliverGagQuestStringLong', 'QuestsDeliverGagQuestInstructions',
    'QuestsDeliverGagQuestProgress', 'QuestsDeliverGagQuestHeadline',
    'QuestsTrackChoiceQuestString', 'QuestsTrackChoiceQuestHeadline',
    'QuestsFriendQuestString', 'QuestsFriendQuestHeadline',
    'QuestsTrolleyQuestString', 'QuestsTrolleyQuestHeadline',
    'QuestsPhoneQuestString', 'QuestsPhoneQuestHeadline',
    'QuestsMailboxQuestString', 'QuestsMailboxQuestHeadline',
    # NPC locations (getNpcLocationDialog / fillInQuestNames / poster)
    'QuestsStreetLocationThisPlayground', 'QuestsStreetLocationThisStreet',
    'QuestsStreetLocationNamedPlayground', 'QuestsStreetLocationNamedStreet', 'QuestsLocationParagraph',
    'QuestsLocationBuilding', 'QuestsLocationBuildingVerb',
    'QuestsHQOfficerFillin', 'QuestsHQWhereFillin', 'QuestsHQBuildingNameFillin',
    'QuestsHQLocationNameFillin', 'QuestsTailorFillin', 'QuestsTailorWhereFillin',
    'QuestsTailorBuildingNameFillin', 'QuestsTailorLocationNameFillin',
    'QuestPageDestination', 'QuestPosterHQOfficer', 'QuestPosterHQBuildingName', 'QuestPosterHQStreetName',
    'QuestPosterHQLocationName', 'QuestPosterTailor', 'QuestPosterTailorBuildingName',
    'QuestPosterTailorStreetName', 'QuestPosterTailorLocationName', 'QuestPosterAnywhere',
    'QuestPosterComplete', 'QuestPosterFun',
    # NPC chat
    'QuestMovieQuestChoiceCancel', 'QuestMovieTrackChoiceCancel', 'QuestMovieQuestChoice',
    'QuestMovieTrackChoice', 'QuestChoiceGuiCancel', 'TrackChoiceGuiChoose', 'TrackChoiceGuiCancel',
    'TrackChoiceGuiHEAL', 'TrackChoiceGuiTRAP', 'TrackChoiceGuiLURE', 'TrackChoiceGuiSOUND',
    'TrackChoiceGuiDROP',
]
# Tuples of strings: the default NPC lines (random.choice in upstream).
TUPLE_NAMES = ['QuestsDefaultGreeting', 'QuestsDefaultIncomplete', 'QuestsDefaultIncompleteProgress',
               'QuestsDefaultIncompleteWrongNPC', 'QuestsDefaultComplete', 'QuestsDefaultLeaving',
               'QuestsDefaultReject', 'QuestsDefaultTierNotDone', 'QuestsDefaultVisitQuestDialog']


def capwords_first(s):
    return s.capitalize()


class Pool:
    def __init__(self):
        self.items = []
        self.index = {}

    def add(self, s):
        if s not in self.index:
            self.index[s] = len(self.items)
            self.items.append(s)
        return self.index[s]


def cpp_str(s):
    out = ['"']
    for b in s.encode('utf-8'):
        c = chr(b)
        if c == '"':
            out.append('\\"')
        elif c == '\\':
            out.append('\\\\')
        elif 0x20 <= b < 0x7F and c != '?':
            out.append(c)
        else:
            out.append('\\%03o' % b)  # three digits: never swallows the next character
    out.append('"')
    return ''.join(out)


def build():
    ttl, tg, tbg, q, npcmod = load_upstream()
    pool = Pool()
    T = ttl.get

    def S(s):
        return pool.add(s)

    consts = {n: q.get(n) for n in ('Any', 'Anywhere', 'NA', 'Same', 'ToonHQ', 'ToonTailor', 'VISIT_QUEST_ID',
                                    'NEWBIE_HP')}
    assert consts['Any'] == ANY

    # --- named strings
    named = []
    for n in STRING_NAMES:
        v = T(n)
        assert isinstance(v, str), n
        named.append((n, S(v)))

    # --- default dialog (status -> options)
    default_dialog = q.get('DefaultDialog')
    status_names = ['GREETING', 'QUEST', 'INCOMPLETE', 'INCOMPLETE_PROGRESS', 'INCOMPLETE_WRONG_NPC',
                    'COMPLETE', 'LEAVING']
    dialogs = []  # (questId, status, [strings])  questId 0 = defaults

    def opts(v):
        return list(v) if isinstance(v, (tuple, list)) else [v]

    for st in range(7):
        v = default_dialog.get(st)
        if v is not None:
            dialogs.append((0, st, opts(v)))
    # Not part of DefaultDialog: reject / tier-not-done / visit-quest lines. questId 0, status 100+.
    extra_defaults = {100: 'QuestsDefaultReject', 101: 'QuestsDefaultTierNotDone',
                      102: 'QuestsDefaultVisitQuestDialog'}
    for st, name in extra_defaults.items():
        dialogs.append((0, st, opts(T(name))))

    # --- quests
    qd = q.get('QuestDict')
    tier_names = {}
    quests = []
    cogs = suit_names()
    cog_index = {code: i for i, (code, _, _) in enumerate(cogs)}
    type_count = {}
    for qid in sorted(qd):
        ent = qd[qid]
        tier, start, desc, from_npc, to_npc, reward, nxt, dlg = ent
        if 'OBSOLETE' in (start, from_npc, to_npc, reward):
            continue  # retired quests: the AI never offers them
        cls = desc[0]
        assert isinstance(cls, Cls), (qid, desc)
        params = list(desc[1:])
        qtype = CLASS_TO_TYPE.get(cls.name, 'Other')
        type_count[qtype] = type_count.get(qtype, 0) + 1
        p = [0] * 6

        def trk(code):
            return -1 if code == ANY else TRACK_CODES.index(code)

        if qtype in ('Cog', 'CogNewbie'):
            p[0], p[1] = params[0], params[1]
            p[2] = -1 if params[2] == ANY else cog_index[params[2]]
            if qtype == 'CogNewbie':
                p[3] = params[3]
        elif qtype == 'CogLevel':
            p[0], p[1], p[2] = params[0], params[1], params[2]
        elif qtype == 'CogTrack':
            p[0], p[1], p[2] = params[0], params[1], TRACK_CODES.index(params[2])
        elif qtype in ('Building', 'BuildingNewbie'):
            p[0], p[1], p[2], p[3] = params[0], params[1], trk(params[2]), params[3]
            if qtype == 'BuildingNewbie':
                p[4] = params[4]
        elif qtype == 'DeliverItem':
            p[0] = params[0]
        elif qtype == 'RecoverItem':
            p[0], p[1], p[2], p[3] = params[0], params[1], params[2], params[3]
            holder = params[4]
            htype = params[5] if len(params) > 5 else 'type'
            if holder == ANY:
                p[4], p[5] = 0, 0
            elif holder == 4:  # AnyFish
                p[4], p[5] = 1, 0
            elif htype == 'type':
                p[4], p[5] = 2, cog_index[holder]
            elif htype == 'level':
                p[4], p[5] = 3, holder
            else:
                p[4], p[5] = 4, TRACK_CODES.index(holder)
        elif qtype == 'DeliverGag':
            p[0], p[1], p[2] = params[0], params[1], params[2]
        elif qtype == 'TrackChoice':
            p[0], p[1] = params[0], params[1]
        elif qtype == 'Other':
            p[0] = S(cls.name)
        # next quest: an int or a tuple/list of ints (forks) -> first id (0 = none)
        if isinstance(nxt, (tuple, list)):
            nxt = nxt[0] if nxt else 0
        if not isinstance(nxt, int):
            nxt = 0
        # dialog
        if dlg is not default_dialog and isinstance(dlg, dict):
            for st, v in sorted(dlg.items()):
                dialogs.append((qid, st, opts(v)))
        from_npc = 0 if from_npc is None else from_npc  # a few quests list None (nobody in particular)
        to_npc = 0 if to_npc is None else to_npc
        for name, val in (('from', from_npc), ('to', to_npc), ('reward', reward)):
            assert isinstance(val, int), (qid, name, val)
        quests.append(dict(id=qid, type=qtype, tier=tier, start=1 if start == 1 else 0, frm=from_npc,
                           to=to_npc, reward=reward, next=nxt, p=p))
    dialogs.sort(key=lambda d: (d[0], d[1]))
    for d in dialogs:
        for s in d[2]:
            assert isinstance(s, str), d

    # --- rewards, pre-rendered (nothing about them depends on runtime state)
    rd = q.get('RewardDict')
    tracks_cap = [capwords_first(t) for t in T('BattleGlobalTracks')]
    hood_map = tg.get('hoodNameMap')
    rewards = []
    for rid in sorted(rd):
        ent = rd[rid]
        cls = ent[0].name
        a = ent[1:]
        long_s = poster = None
        try:
            if cls == 'MaxHpReward':
                long_s, poster = T('QuestsMaxHpReward') % a[0], T('QuestsMaxHpRewardPoster') % a[0]
            elif cls == 'MoneyReward':
                if a[0] == 1:
                    long_s, poster = T('QuestsMoneyRewardSingular'), T('QuestsMoneyRewardPosterSingular')
                else:
                    long_s, poster = T('QuestsMoneyRewardPlural') % a[0], T('QuestsMoneyRewardPosterPlural') % a[0]
            elif cls == 'MaxMoneyReward':
                if a[0] == 1:
                    long_s = T('QuestsMaxMoneyRewardSingular')
                    poster = T('QuestsMaxMoneyRewardPosterSingular')
                else:
                    long_s = T('QuestsMaxMoneyRewardPlural') % a[0]
                    poster = T('QuestsMaxMoneyRewardPosterPlural') % a[0]
            elif cls == 'MaxGagCarryReward':
                long_s = T('QuestsMaxGagCarryReward') % {'name': a[1], 'num': a[0]}
                poster = T('QuestsMaxGagCarryRewardPoster') % {'name': a[1], 'num': a[0]}
            elif cls == 'MaxQuestCarryReward':
                long_s, poster = T('QuestsMaxQuestCarryReward') % a[0], T('QuestsMaxQuestCarryRewardPoster') % a[0]
            elif cls == 'TeleportReward':
                hood = hood_map[a[0]][-1]
                long_s, poster = T('QuestsTeleportReward') % hood, T('QuestsTeleportRewardPoster') % hood
            elif cls in ('TrackTrainingReward', 'TrackProgressReward', 'TrackCompleteReward'):
                track = a[0] or 0
                name = tracks_cap[track]
                if cls == 'TrackTrainingReward':
                    long_s, poster = T('QuestsTrackTrainingReward') % name, T('QuestsTrackTrainingRewardPoster')
                elif cls == 'TrackProgressReward':
                    long_s = T('QuestsTrackProgressReward') % {'frameNum': a[1], 'trackName': name}
                    poster = T('QuestsTrackProgressRewardPoster') % {'frameNum': a[1], 'trackName': name}
                else:
                    long_s, poster = T('QuestsTrackCompleteReward') % name, T('QuestsTrackCompleteRewardPoster') % name
            elif cls == 'ClothingTicketReward':
                long_s, poster = T('QuestsClothingTicketReward'), T('QuestsClothingTicketRewardPoster')
            elif cls == 'TIPClothingTicketReward':
                long_s, poster = T('TIPQuestsClothingTicketReward'), T('TIPQuestsClothingTicketRewardPoster')
            elif cls == 'CheesyEffectReward':
                effect, hood_id, minutes = a[0], a[1], a[2]
                desc_pair = T('CheesyEffectDescriptions')[effect]
                duration, tmpl = minutes, T('CheesyEffectMinutes')
                if duration > 90:
                    duration, tmpl = int((duration + 30) / 60), T('CheesyEffectHours')
                    if duration > 36:
                        duration, tmpl = int((duration + 12) / 24), T('CheesyEffectDays')
                if hood_id == 0:
                    while_s = ''
                elif hood_id == 1:
                    while_s = T('CheesyEffectExceptIn') % T('ToontownCentral')[-1]
                else:
                    while_s = T('CheesyEffectWhileYouAreIn') % hood_map[ast_hood(hood_id)][-1]
                if duration:
                    long_s = tmpl % {'time': duration, 'effectName': desc_pair[1], 'whileIn': while_s}
                else:
                    long_s = T('CheesyEffectIndefinite') % {'effectName': desc_pair[1], 'whileIn': while_s}
                poster = T('QuestsCheesyEffectRewardPoster') % desc_pair[0]
        except (KeyError, IndexError, TypeError):
            long_s = poster = None
        if long_s is None:  # CogSuitPartReward (CogDisguiseGlobals) and anything unresolved
            long_s, poster = 'A special reward.', 'Reward: special'
        rewards.append((rid, S(long_s), S(poster)))

    # --- NPCs, zone titles, places, items, gags, cogs
    npc_dict = npcmod.get('NPCToonDict')
    npcs = []
    for nid in sorted(npc_dict):
        zone, name = npc_dict[nid][0], npc_dict[nid][1]
        npcs.append((nid, zone, S(name)))
    titles = [(z, S(v[0]), S(v[1])) for z, v in sorted(T('zone2TitleDict').items())]
    hoods = [(h, S(v[0]), S(v[1]), S(v[-1])) for h, v in sorted(hood_map.items())]
    streets = [(h, S(v[0]), S(v[1]), S(v[-1])) for h, v in sorted(T('GlobalStreetNames').items())]
    items = [(i, S(v[0]), S(v[1]), S(v[2])) for i, v in sorted(T('QuestsItemDict').items())]
    cog_rows = [(code, S(sing), S(plur)) for code, sing, plur in cogs]
    sing = T('BattleGlobalAvPropStringsSingular')
    plur = T('BattleGlobalAvPropStringsPlural')
    gag_rows = [(S(sing[t][l]), S(plur[t][l])) for t in range(7) for l in range(7)]
    arrays = {
        'TrackNameCap': [S(n) for n in tracks_cap],
        'CogTrackS': [S(T(n)) for n in ('BossbotS', 'LawbotS', 'CashbotS', 'SellbotS')],
        'CogTrackP': [S(T(n)) for n in ('BossbotP', 'LawbotP', 'CashbotP', 'SellbotP')],
        'CogTrackName': [S(T(n)) for n in ('Bossbot', 'Lawbot', 'Cashbot', 'Sellbot')],
        'BuildingFloors': [S(x) for x in T('QuestsBuildingQuestFloorNumbers')],
    }
    # Tuples of default lines
    tup = {n: [S(x) for x in T(n)] for n in TUPLE_NAMES}
    # Dialog strings enter the pool last so their order does not disturb the above.
    dlg_rows = []
    dlg_idx = []
    for qid, st, options in dialogs:
        dlg_rows.append((qid, st, len(dlg_idx), len(options)))
        dlg_idx.extend(S(s) for s in options)
    return dict(pool=pool, named=named, quests=quests, dialogs=dlg_rows, dialog_idx=dlg_idx, rewards=rewards,
                npcs=npcs, titles=titles, hoods=hoods, streets=streets, items=items, cogs=cog_rows,
                gags=gag_rows, arrays=arrays, consts=consts, tup=tup, type_count=type_count)


def ast_hood(h):
    return h


# ---------------------------------------------------------------- emit
def emit(data):
    P = data['pool'].items
    consts = data['consts']
    h = ['// GENERATED by tools/gen_quest_tables.py from the open-toontown quest tables. Do not edit.',
         '// Spec: docs/design/quests.md. Strings are TTLocalizerEnglish templates and names; kS_<name>',
         '// constants index kStrings.',
         '#pragma once', '', '#include <cstdint>', '', 'namespace tt::quest {', '',
         'enum class QuestType : uint8_t {']
    h += ['    %s,' % t for t in QUEST_TYPES]
    h += ['};', '',
          'constexpr uint16_t kAny = %d;        // Quests.Any: any NPC / any cog' % consts['Any'],
          'constexpr uint16_t kNA = %d;         // Quests.NA: no reward / not applicable' % consts['NA'],
          'constexpr uint16_t kToonTailor = %d;' % consts['ToonTailor'],
          'constexpr uint16_t kToonHQ = %d;' % consts['ToonHQ'],
          'constexpr int32_t kAnywhere = %d;    // Quests.Anywhere (location parameter)' % consts['Anywhere'],
          'constexpr int kNewbieHp = %d;        // Quests.NEWBIE_HP' % consts['NEWBIE_HP'],
          '', '// Parameter layout p[] by type (see tools/gen_quest_tables.py):',
          '//   Cog          loc, num, cog index (-1 any)        CogNewbie  + p3 laff',
          '//   CogLevel     loc, num, level                     CogTrack   loc, num, track 0..3 (c l m s)',
          '//   Building     loc, num, track (-1 any), floors    BuildingNewbie + p4 laff',
          '//   DeliverItem  item                                RecoverItem loc, num, item, chance%,',
          '//                                                      holderKind (0 any, 1 fish, 2 cog type,',
          '//                                                      3 level, 4 track), holderValue',
          '//   DeliverGag   num, track, level                   TrackChoice trackA, trackB',
          '//   Other        p0 = kStrings index of the upstream class name',
          'struct QuestDef {', '    uint16_t id;', '    QuestType type;', '    uint8_t tier;',
          '    uint16_t fromNpc;   // npcId, kAny or kToonHQ', '    uint16_t toNpc;',
          '    uint16_t reward;    // reward id; kAny/kNA = none yet',
          '    uint16_t next;      // first successor quest id, 0 = end of chain',
          '    int32_t p[6];', '};', '',
          'struct DialogRef { uint16_t questId, status, first, count; }; // questId 0 = defaults (status 100+: extra)',
          'struct RewardDef { uint16_t id, text, poster; };',
          'struct NpcDef { uint16_t id; int32_t zone; uint16_t name; };',
          'struct ZoneTitle { uint16_t zone, title, article; };',
          'struct PlaceName { uint16_t id, to, in, name; };',
          'struct ItemDef { uint16_t id, name, plural, article; };',
          'struct CogName { const char* code; uint16_t singular, plural; };',
          'struct GagName { uint16_t singular, plural; };', '',
          'extern const char* const kStrings[];', 'extern const uint32_t kNumStrings;',
          'extern const QuestDef kQuests[];', 'extern const uint32_t kNumQuests;',
          'extern const DialogRef kDialogs[];', 'extern const uint32_t kNumDialogs;',
          'extern const uint16_t kDialogStrings[];',
          'extern const RewardDef kRewards[];', 'extern const uint32_t kNumRewards;',
          'extern const NpcDef kNpcs[];', 'extern const uint32_t kNumNpcs;',
          'extern const ZoneTitle kZoneTitles[];', 'extern const uint32_t kNumZoneTitles;',
          'extern const PlaceName kHoodNames[];', 'extern const uint32_t kNumHoodNames;',
          'extern const PlaceName kStreetNames[];', 'extern const uint32_t kNumStreetNames;',
          'extern const ItemDef kItems[];', 'extern const uint32_t kNumItems;',
          'extern const CogName kCogs[];', 'extern const uint32_t kNumCogs;',
          'extern const GagName kGags[49]; // [track * 7 + level]', '']
    for name, idx in data['named']:
        h.append('constexpr uint16_t kS_%s = %d;' % (name, idx))
    h.append('')
    for name, idxs in data['arrays'].items():
        h.append('constexpr uint16_t kA_%s[%d] = {%s};' % (name, len(idxs), ', '.join(map(str, idxs))))
    h.append('')
    for name, idxs in data['tup'].items():
        h.append('constexpr uint16_t kT_%s[%d] = {%s};' % (name, len(idxs), ', '.join(map(str, idxs))))
    h += ['', '} // namespace tt::quest', '']

    c = ['// GENERATED by tools/gen_quest_tables.py from the open-toontown quest tables. Do not edit.',
         '#include "quest/quest_data.h"', '', 'namespace tt::quest {', '',
         'const char* const kStrings[] = {']
    c += ['    %s,' % cpp_str(s) for s in P]
    c += ['};', 'const uint32_t kNumStrings = %d;' % len(P), '', 'const QuestDef kQuests[] = {']
    for qd in data['quests']:
        c.append('    {%d, QuestType::%s, %d, %d, %d, %d, %d, {%s}},' %
                 (qd['id'], qd['type'], qd['tier'], qd['frm'], qd['to'], qd['reward'], qd['next'],
                  ', '.join(map(str, qd['p']))))
    c += ['};', 'const uint32_t kNumQuests = %d;' % len(data['quests']), '', 'const DialogRef kDialogs[] = {']
    c += ['    {%d, %d, %d, %d},' % d for d in data['dialogs']]
    c += ['};', 'const uint32_t kNumDialogs = %d;' % len(data['dialogs']), '',
          'const uint16_t kDialogStrings[] = {']
    idx = data['dialog_idx']
    c += ['    ' + ', '.join(map(str, idx[i:i + 16])) + ',' for i in range(0, len(idx), 16)]
    c += ['};', '', 'const RewardDef kRewards[] = {']
    c += ['    {%d, %d, %d},' % r for r in data['rewards']]
    c += ['};', 'const uint32_t kNumRewards = %d;' % len(data['rewards']), '', 'const NpcDef kNpcs[] = {']
    c += ['    {%d, %d, %d},' % n for n in data['npcs']]
    c += ['};', 'const uint32_t kNumNpcs = %d;' % len(data['npcs']), '', 'const ZoneTitle kZoneTitles[] = {']
    c += ['    {%d, %d, %d},' % t for t in data['titles']]
    c += ['};', 'const uint32_t kNumZoneTitles = %d;' % len(data['titles']), '', 'const PlaceName kHoodNames[] = {']
    c += ['    {%d, %d, %d, %d},' % t for t in data['hoods']]
    c += ['};', 'const uint32_t kNumHoodNames = %d;' % len(data['hoods']), '', 'const PlaceName kStreetNames[] = {']
    c += ['    {%d, %d, %d, %d},' % t for t in data['streets']]
    c += ['};', 'const uint32_t kNumStreetNames = %d;' % len(data['streets']), '', 'const ItemDef kItems[] = {']
    c += ['    {%d, %d, %d, %d},' % t for t in data['items']]
    c += ['};', 'const uint32_t kNumItems = %d;' % len(data['items']), '', 'const CogName kCogs[] = {']
    c += ['    {%s, %d, %d},' % (cpp_str(code), s, p) for code, s, p in data['cogs']]
    c += ['};', 'const uint32_t kNumCogs = %d;' % len(data['cogs']), '', 'const GagName kGags[49] = {']
    c += ['    {%d, %d},' % g for g in data['gags']]
    c += ['};', '', '} // namespace tt::quest', '']
    return '\n'.join(h), '\n'.join(c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    data = build()
    h, c = emit(data)
    if args.check:
        stale = False
        for path, text in ((OUT_H, h), (OUT_CPP, c)):
            try:
                with open(path, encoding='utf-8') as f:
                    if f.read() != text:
                        stale = True
            except OSError:
                stale = True
        sys.exit(1 if stale else 0)
    os.makedirs(os.path.dirname(OUT_H), exist_ok=True)
    for path, text in ((OUT_H, h), (OUT_CPP, c)):
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    tc = data['type_count']
    print('quests %d (%s); strings %d; dialogs %d; rewards %d; npcs %d' %
          (len(data['quests']), ', '.join('%s %d' % kv for kv in sorted(tc.items())),
           len(data['pool'].items), len(data['dialogs']), len(data['rewards']), len(data['npcs'])))


if __name__ == '__main__':
    main()
