"""In-game Groups, ported from the Android host suite.

The Python package under tools/groups is the UberDOG and layout reference.
The 3DS binary does not link it. No server is contacted.
"""
import os
import re
import sys
import types
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PY = os.path.join(ROOT, 'tools', 'groups')
sys.path.insert(0, PY)
from ttgroups import context, groups as G, strings as S  # noqa: E402
from ttgroups.layout import list_frame, list_message, page_layout, picker_back_z  # noqa: E402
from ttgroups import wire  # noqa: E402
from ttgroups.office import Office  # noqa: E402

DC = os.path.join(PY, 'groups.dc')
PLATFORM_DC = os.path.join(ROOT, 'tools', 'platform', 'platform.dc')
CPP_LOGIC = os.path.join(ROOT, 'source', 'net', 'groups_logic.h')
CPP_CLIENT = os.path.join(ROOT, 'source', 'net', 'groups_client.cpp')


def _srgb_linear(channel):
    c = channel / 255.0
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


def _contrast(text, face):
    def lum(rgb):
        return (0.2126 * _srgb_linear(rgb[0]) + 0.7152 * _srgb_linear(rgb[1]) +
                0.0722 * _srgb_linear(rgb[2]))
    hi, lo = lum(text), lum(face)
    if hi < lo:
        hi, lo = lo, hi
    return (hi + 0.05) / (lo + 0.05)


def _theme_colors():
    path = os.path.join(ROOT, 'source', 'ui', 'theme.h')
    with open(path, encoding='utf-8') as f:
        text = f.read()
    found = {}
    for match in re.finditer(
            r'constexpr u32 (k\w+) = rgb\(0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+)\)',
            text):
        found[match.group(1)] = tuple(int(part, 16) for part in match.groups()[1:])
    return found


def _hex_triple(src):
    parts = [int(part, 16) for part in re.findall(r'0x([0-9A-Fa-f]+)', src)]
    if len(parts) != 3:
        raise AssertionError(src)
    return tuple(parts)


def _impress_width(text, scale):
    """Pixel width of ASCII text in romfs impress.bcfnt at a citro2d scale.

    citro2d multiplies the char-width sum by 30/cellHeight, then by the draw scale.
    kTextSmall is 0.40. The empty line is wrapped at 288.
    """
    import struct
    path = os.path.join(ROOT, 'romfs', 'baked', 'fonts', 'impress.bcfnt')
    with open(path, 'rb') as f:
        src = f.read()
    header_size, = struct.unpack_from('<H', src, 6)
    tglp_ptr, cwdh_ptr, cmap_ptr = struct.unpack_from('<III', src, header_size + 16)
    cell_h = src[tglp_ptr - 8 + 9]
    code_begin, _code_end, method, _, _nxt = struct.unpack_from('<HHHHI', src, cmap_ptr)
    if method != 0:
        raise AssertionError('impress cmap is not a direct map')
    first, = struct.unpack_from('<H', src, cmap_ptr + 12)
    start, end, _nxt = struct.unpack_from('<HHI', src, cwdh_ptr)
    widths = {}
    off = cwdh_ptr + 8
    for glyph in range(start, end + 1):
        _left, _gw, char_w = struct.unpack_from('<bBB', src, off)
        off += 3
        widths[glyph] = char_w
    total = 0
    for ch in text:
        glyph = first + (ord(ch) - code_begin)
        total += widths[glyph]
    return total * (30.0 / cell_h) * scale

# DCFile numbers for otp.dc + toon.dc + platform.dc + groups.dc.
# The nine TTGroupInfo members sit between platform and the manager.
GROUPS_FIELDS = (
    'requestBoard', 'stopBoard', 'setContext', 'createGroup', 'joinGroup', 'leaveGroup',
    'setBoard', 'groupChanged', 'groupGone', 'result', 'noteActivity', 'avatarGone',
)
GROUPS_BASE = 2283
PLATFORM_FIELDS = (
    ('setMyPlatform', 2270),
    ('requestPlatforms', 2271),
    ('setPlatforms', 2272),
    ('clearPlatform', 2273),
)
STRUCT_FIELDS = (
    ('groupId', 2274),
    ('leaderId', 2275),
    ('kind', 2276),
    ('target', 2277),
    ('hoodId', 2278),
    ('shardId', 2279),
    ('maxSize', 2280),
    ('flags', 2281),
    ('members', 2282),
)
CLSEND = {'requestBoard', 'stopBoard', 'setContext', 'createGroup', 'joinGroup', 'leaveGroup'}


def ott_etc():
    """Directory that holds otp.dc and toon.dc. OTT, then the pinned submodule."""
    cands = []
    if os.environ.get('OTT'):
        cands.append(os.path.join(os.environ['OTT'], 'etc'))
    cands.append(os.path.join(ROOT, 'ref', 'open-toontown', 'etc'))
    for cand in cands:
        if os.path.isfile(os.path.join(cand, 'otp.dc')) and os.path.isfile(os.path.join(cand, 'toon.dc')):
            return cand
    return None


class Clock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


class _Field:
    def __init__(self, number):
        self.number = number

    def getNumber(self):
        return self.number

    def getName(self):
        return 'avatarGone'


class _FieldClass:
    """Stand-in for the DC class. The wire id is not the class-local index."""

    def __init__(self, number):
        self.number = number

    def getFieldByName(self, name):
        return _Field(self.number)


class _Tasks:
    def __init__(self):
        self.named = {}

    def doMethodLater(self, delay, func, name):
        self.named[name] = func

    def hasTaskNamed(self, name):
        return name in self.named

    def remove(self, name):
        self.named.pop(name, None)


def board():
    c = Clock()
    return G.GroupBoard(clock=c), c


class BoardTests(unittest.TestCase):
    def test_create_join_leave(self):
        b, _ = board()
        code, gid, ch = b.create(1, G.BOSS, G.SELLBOT, 11000, 5, 8)
        self.assertEqual((code, ch[0][0]), (G.OK, 'changed'))
        self.assertEqual(b.join(2, gid)[0], G.OK)
        self.assertEqual(b.groups[gid].members, [1, 2])
        self.assertEqual(b.leave(2)[0], G.OK)
        code, _, ch = b.leave(1)
        self.assertEqual((code, ch), (G.OK, [('gone', gid)]))
        self.assertEqual(b.groups, {})

    def test_leader_hands_off_to_oldest_member(self):
        b, _ = board()
        _, gid, _ = b.create(1, G.BUILDING, 1, 2000, 5, 4)
        b.join(2, gid)
        b.join(3, gid)
        b.leave(1)
        self.assertEqual(b.groups[gid].leader_id, 2)

    def test_rejections(self):
        b, c = board()
        self.assertEqual(b.create(1, 9, 0, 1, 1, 4)[0], G.ERR_BAD_ARGS)
        self.assertEqual(b.create(1, G.TASK, 6, 1, 1, 4)[0], G.ERR_BAD_ARGS)
        self.assertEqual(b.create(1, G.TASK, 0, 2000, 1, 1)[0], G.ERR_BAD_ARGS)
        _, gid, _ = b.create(1, G.TASK, 0, 2000, 1, 2)
        self.assertEqual(b.create(1, G.TASK, 0, 2000, 1, 2)[0], G.ERR_IN_GROUP)
        self.assertEqual(b.join(1, gid)[0], G.ERR_IN_GROUP)
        self.assertEqual(b.join(2, 99)[0], G.ERR_NOT_FOUND)
        b.join(2, gid)
        self.assertEqual(b.join(3, gid)[0], G.ERR_FULL)
        self.assertEqual(b.leave(7)[0], G.ERR_NOT_IN_GROUP)
        b.leave(1)
        b.leave(2)
        self.assertEqual(b.create(1, G.TASK, 0, 2000, 1, 2)[0], G.ERR_RATE)
        c.t += G.CREATE_COOLDOWN + 1
        self.assertEqual(b.create(1, G.TASK, 0, 2000, 1, 2)[0], G.OK)

    def test_expiry_and_in_progress(self):
        b, c = board()
        _, gid, _ = b.create(1, G.BOSS, 0, 11000, 1, 8)
        self.assertEqual(b.mark_in_progress(gid)[0][0], 'changed')
        self.assertEqual(b.mark_in_progress(gid), [])
        c.t += G.EXPIRY + 1
        self.assertEqual(b.expire(), [('gone', gid)])
        self.assertNotIn(1, b.of_avatar)

    def test_version_bumps_and_wire_roundtrip(self):
        b, _ = board()
        v0 = b.version
        _, gid, _ = b.create(1, G.BOSS, G.LAWBOT, 13000, 7, 6)
        self.assertGreater(b.version, v0)
        w = b.snapshot()[0]
        self.assertEqual(len(w), 9)  # TTGroupInfo has nine fields
        g = G.Group.from_wire(w)
        self.assertEqual((g.group_id, g.kind, g.target, g.hood_id, g.members), (gid, G.BOSS, G.LAWBOT, 13000, [1]))


class EdgeCaseTests(unittest.TestCase):
    def test_leader_leaves_group_survives_and_new_leader_can_leave(self):
        b, _ = board()
        _, gid, _ = b.create(1, G.BOSS, 0, 11000, 1, 4)
        b.join(2, gid)
        code, _, ch = b.leave(1)
        self.assertEqual((code, ch[0][0]), (G.OK, 'changed'))
        self.assertEqual((b.groups[gid].leader_id, b.groups[gid].members), (2, [2]))
        self.assertNotIn(1, b.of_avatar)
        self.assertEqual(b.leave(2)[2], [('gone', gid)])

    def test_member_leave_keeps_leader(self):
        b, _ = board()
        _, gid, _ = b.create(1, G.TASK, 0, 2000, 1, 4)
        b.join(2, gid)
        b.join(3, gid)
        b.leave(2)
        self.assertEqual((b.groups[gid].leader_id, b.groups[gid].members), (1, [1, 3]))

    def test_full_group_frees_a_seat_on_leave(self):
        b, _ = board()
        _, gid, _ = b.create(1, G.TASK, 0, 2000, 1, 2)
        b.join(2, gid)
        self.assertEqual(b.join(3, gid)[0], G.ERR_FULL)
        b.leave(2)
        self.assertEqual(b.join(3, gid)[0], G.OK)
        self.assertEqual(b.groups[gid].members, [1, 3])

    def test_rejected_calls_change_nothing(self):
        b, _ = board()
        _, gid, _ = b.create(1, G.TASK, 0, 2000, 1, 2)
        v = b.version
        for res in (b.join(1, gid), b.join(2, 99), b.leave(5), b.create(1, G.TASK, 0, 2000, 1, 2),
                    b.create(0, G.TASK, 0, 2000, 1, 2)):
            self.assertNotEqual(res[0], G.OK)
            self.assertEqual(res[2], [])
        self.assertEqual((b.version, len(b.groups)), (v, 1))

    def test_group_cap_and_size_bounds(self):
        b, _ = board()
        for av in range(1, G.MAX_GROUPS + 1):
            self.assertEqual(b.create(av, G.TASK, 0, 2000, 1, 4)[0], G.OK)
        self.assertEqual(b.create(G.MAX_GROUPS + 1, G.TASK, 0, 2000, 1, 4)[0], G.ERR_RATE)
        b2, _ = board()
        self.assertEqual(b2.create(1, G.TASK, 0, 2000, 1, G.MAX_SIZE + 1)[0], G.ERR_BAD_ARGS)
        self.assertEqual(b2.create(1, G.TASK, 0, 2000, 1, G.MIN_SIZE - 1)[0], G.ERR_BAD_ARGS)
        self.assertEqual(b2.create(1, G.TASK, -1, 2000, 1, 4)[0], G.ERR_BAD_ARGS)

    def test_expiry_frees_members_and_touch_extends_life(self):
        b, c = board()
        _, gid, _ = b.create(1, G.BUILDING, 0, 2000, 1, 4)
        c.t += G.EXPIRY - 5
        b.join(2, gid)  # touches the group
        c.t += 10
        self.assertEqual(b.expire(), [])
        c.t += G.EXPIRY
        self.assertEqual(b.expire(), [('gone', gid)])
        self.assertEqual(b.of_avatar, {})
        _, g2, _ = b.create(1, G.TASK, 0, 2000, 1, 4)
        self.assertEqual(b.join(2, g2)[0], G.OK)

    def test_cooldown_table_is_pruned(self):
        b, c = board()
        b.create(1, G.TASK, 0, 2000, 1, 4)
        c.t += G.CREATE_COOLDOWN + 1
        b.expire()
        self.assertEqual(b.last_create, {})

    def test_group_ids_never_reused(self):
        b, _ = board()
        _, a, _ = b.create(1, G.TASK, 0, 2000, 1, 4)
        b.leave(1)
        b.last_create.clear()
        _, c2, _ = b.create(1, G.TASK, 0, 2000, 1, 4)
        self.assertGreater(c2, a)

    def test_client_board_stale_group_gone_and_old_versions(self):
        b, _ = board()
        _, gid, _ = b.create(1, G.TASK, 0, 2000, 1, 4)
        cb = G.ClientBoard()
        cb.set_board(b.version, b.snapshot())
        self.assertEqual(len(cb.rows(9)), 1)
        cb.gone(b.version + 1, gid)
        self.assertEqual(cb.rows(9), [])
        cb.gone(b.version + 2, 12345)  # unknown id must not raise
        self.assertIsNone(cb.own(1))

    def test_badge_only_for_matching_groups(self):
        b, _ = board()
        b.create(1, G.BOSS, G.SELLBOT, 11000, 1, 8)
        b.create(2, G.BUILDING, 0, 2000, 1, 4)
        b.create(3, G.TASK, 0, 1000, 1, 4)
        cb = G.ClientBoard()
        cb.set_board(b.version, b.snapshot())
        count = lambda ctx: G.badge_count(cb, ctx, own_av_id=9)
        self.assertEqual(count({'activity': G.ACT_STREET, 'hood_id': 2000}), 1)
        self.assertEqual(count({'activity': G.ACT_BOSS_LOBBY, 'hood_id': 11000}), 1)
        self.assertEqual(count({'activity': G.ACT_STREET, 'hood_id': 2000, 'task_hoods': (1000,)}), 2)
        self.assertEqual(count({'activity': G.ACT_PLAYGROUND, 'hood_id': 4000}), 0)
        self.assertEqual(count({}), 0)

    def test_teleport_plan_edges(self):
        g = G.Group(1, 50, G.BUILDING, 0, 2000, 3, 4)
        plan = lambda **k: G.teleport_plan(g, k.get('shard', 1), k.get('hood', 1000), k.get('friends', set()),
                                           k.get('access', {2000}), k.get('avail', {2000}))
        self.assertEqual(plan(shard=3, hood=2000), (G.GO_NONE, False))
        self.assertEqual(plan(shard=3, friends={50}), (G.GO_FRIEND, False))
        self.assertEqual(plan(shard=1, friends={50}), (G.GO_HOOD_AFTER_SHARD, True))
        self.assertEqual(plan(access=set()), (G.GO_TELL, False))
        self.assertEqual(plan(avail=set()), (G.GO_TELL, False))


class FitTests(unittest.TestCase):
    def test_fit_fills_without_overflow(self):
        for aspect in (4 / 3.0, 1.0, 0.9, 2.0, 16 / 9.0):
            s, z = G.fit_scale(aspect)
            self.assertLessEqual(G.PAGE_HALF_W * s, aspect)
            self.assertLessEqual(s * G.PAGE_Z_HIGH + z, 1.0)
            self.assertGreaterEqual(s * G.PAGE_Z_LOW + z, -1.0)
        s, _ = G.fit_scale(4 / 3.0)
        self.assertGreater(s, 1.2)  # the 3DS bottom screen gets a real enlargement
        self.assertLess(G.fit_scale(0.5)[0], G.fit_scale(1.5)[0])  # tall narrow halves shrink to the width


class UDManagerTests(unittest.TestCase):
    """Run TTGroupsManagerUD against stubbed Panda modules: subscriptions and disconnect cleanup."""

    def setUp(self):
        self.saved = {k: sys.modules.get(k) for k in ('direct', 'direct.distributed',
                      'direct.distributed.DistributedObjectGlobalUD', 'direct.task')}

        class Base:
            def __init__(self, air):
                self.air = air
                self.sent = []

            def announceGenerate(self):
                pass

            def delete(self):
                pass

            def sendUpdateToAvatarId(self, av, field, args):
                self.sent.append((av, field, args))

        mods = {'direct': types.ModuleType('direct'),
                'direct.distributed': types.ModuleType('direct.distributed'),
                'direct.distributed.DistributedObjectGlobalUD': types.ModuleType('x'),
                'direct.task': types.ModuleType('direct.task')}
        mods['direct.distributed.DistributedObjectGlobalUD'].DistributedObjectGlobalUD = Base
        mods['direct.task'].Task = types.SimpleNamespace(again=1, done=0)
        sys.modules.update(mods)
        sys.modules.pop('ttgroups.TTGroupsManagerUD', None)
        from ttgroups import TTGroupsManagerUD as M
        self.M = M
        self.air = types.SimpleNamespace(sender=0, ourChannel=1000000, takes_bytes=True,
                                         locations={}, pending=[], defer=False, msgSender=0,
                                         getAvatarIdFromSender=lambda: self.air.sender,
                                         getMsgSender=lambda: self.air.msgSender)
        self.ud = M.TTGroupsManagerUD(self.air)
        self.ud.dclass = _FieldClass(2294)
        self.tasks = _Tasks()
        M.taskMgr = self.tasks

        def getLocation(do_id, callback):
            if self.air.defer:
                self.air.pending.append((do_id, callback))
                return
            parent, zone = self.air.locations.get(do_id, (0, 0))
            callback(do_id, parent, zone)

        self.air.getLocation = getLocation

    def tearDown(self):
        sys.modules.pop('ttgroups.TTGroupsManagerUD', None)
        for k, v in self.saved.items():
            if v is None:
                sys.modules.pop(k, None)
            else:
                sys.modules[k] = v

    def call(self, av, name, *args):
        self.air.sender = av
        getattr(self.ud, name)(*args)

    def stood(self, av, hood, shard, activity, task_hoods=(), place=None):
        """setContext is matching only. place is the state-server location (shard, zone)."""
        self.call(av, 'setContext', hood, shard, activity, task_hoods)
        if place is None:
            place = (shard, hood)
        self.air.locations[av] = place

    def fields(self, av):
        return [f for a, f, _ in self.ud.sent if a == av]

    def channel_of(self, av):
        return (1003 << 32) | av

    def disconnect(self, av, sender=None, nonce=1):
        """Login remembered this channel and nonce. The repository sets getMsgSender first."""
        channel = self.channel_of(av)
        self.ud.remember_client(av, channel, nonce)
        self.air.msgSender = channel if sender is None else sender
        self.ud.avatarGone(av, nonce)

    def test_subscribers_get_changes_and_non_subscribers_do_not(self):
        self.call(2, 'requestBoard')
        self.stood(3, 2000, 1, G.ACT_PLAYGROUND)
        self.stood(1, 2000, 1, G.ACT_STREET)
        self.call(1, 'createGroup', G.BUILDING, 0, 4)
        self.assertEqual(self.fields(2), ['setBoard', 'groupChanged'])
        self.assertEqual(self.fields(3), [])  # playground does not match a building group
        self.assertEqual(self.fields(1), ['result'])
        self.call(2, 'stopBoard')
        self.call(1, 'leaveGroup')
        self.assertEqual(self.fields(2), ['setBoard', 'groupChanged'])

    def test_matching_context_hears_about_group(self):
        self.stood(3, 2000, 1, G.ACT_STREET)
        self.stood(1, 2000, 1, G.ACT_STREET)
        self.call(1, 'createGroup', G.BUILDING, 0, 4)
        self.assertEqual(self.ud.office.board.groups[1].hood_id, 2000)
        self.assertEqual(self.fields(3), ['groupChanged'])

    def test_disconnect_leaves_group_and_drops_subscriptions(self):
        self.stood(1, 2000, 1, G.ACT_STREET)
        self.call(1, 'createGroup', G.BUILDING, 0, 4)
        self.call(2, 'joinGroup', 1)
        self.call(3, 'requestBoard')
        self.disconnect(1)
        self.assertEqual(self.ud.office.board.groups[1].members, [2])
        self.assertEqual(self.ud.office.board.groups[1].leader_id, 2)
        self.disconnect(2)
        self.assertEqual(self.ud.office.board.groups, {})
        self.assertEqual(self.fields(3)[-1], 'groupGone')
        self.disconnect(3)
        self.assertEqual((self.ud.office.page_subs, self.ud.office.contexts), (set(), {}))
        self.ud.avatarGone(77)  # never seen: must not raise

    def test_avatar_gone_logs_the_registered_field_number(self):
        class Log:
            def __init__(self):
                self.lines = []

            def info(self, line):
                self.lines.append(line)

        self.ud.notify = Log()
        self.stood(1, 2000, 1, G.ACT_STREET)
        self.call(1, 'createGroup', G.BUILDING, 0, 4)
        self.disconnect(1)
        self.assertTrue(any(line.startswith('avatarGone 1 field 2294') for line in self.ud.notify.lines))

    def test_unknown_sender_is_ignored(self):
        self.call(0, 'createGroup', G.BUILDING, 0, 4)
        self.call(0, 'joinGroup', 1)
        self.call(0, 'leaveGroup')
        self.assertEqual((self.ud.sent, self.ud.office.board.groups), ([], {}))

    def test_note_activity_only_for_leader_of_same_kind(self):
        self.stood(1, 2000, 1, G.ACT_BOSS_LOBBY)
        self.call(1, 'createGroup', G.BOSS, 0, 8)
        self.call(2, 'joinGroup', 1)
        self.assertEqual(self.ud.office.board.groups[1].hood_id, 11000)
        self.ud.noteActivity(2, G.BOSS, 0)
        self.ud.noteActivity(1, G.BUILDING, 0)
        self.assertEqual(self.ud.office.board.groups[1].flags, 0)
        self.ud.noteActivity(1, G.BOSS, 0)
        self.assertTrue(self.ud.office.board.groups[1].flags & G.FLAG_IN_PROGRESS)

    def test_spoofed_context_does_not_change_the_published_place(self):
        self.stood(1, 9000, 99, G.ACT_STREET, place=(7, 5000))
        self.call(1, 'createGroup', G.BUILDING, 0, 4)
        g = self.ud.office.board.groups[1]
        self.assertEqual((g.hood_id, g.shard_id), (5000, 7))
        self.assertEqual(self.ud.office.contexts[1]['hood_id'], 9000)
        self.assertEqual(self.fields(1), ['result'])
        self.air.locations[2] = (0, 0)
        self.call(2, 'createGroup', G.BUILDING, 0, 4)
        self.assertIsNone(self.ud.office.board.of_avatar.get(2))
        sent = [args for av, field, args in self.ud.sent if av == 2 and field == 'result']
        self.assertEqual(sent[-1][1], G.ERR_BAD_ARGS)

    def test_a_location_that_never_arrives_is_rejected(self):
        self.air.defer = True
        self.call(4, 'createGroup', G.BUILDING, 0, 4)
        self.assertEqual(self.fields(4), [])
        self.assertIn(4, self.ud._loc_wait)
        self.assertIn('groups-locate-4', self.tasks.named)
        self.ud._forget_locate(4)
        sent = [args for av, field, args in self.ud.sent if av == 4 and field == 'result']
        self.assertEqual(sent[-1][1], G.ERR_BAD_ARGS)
        self.assertIsNone(self.ud.office.board.of_avatar.get(4))
        self.assertNotIn('groups-locate-4', self.tasks.named)

    def test_late_location_after_avatar_gone_does_not_create(self):
        self.air.defer = True
        self.stood(1, 2000, 1, G.ACT_STREET)
        self.call(1, 'createGroup', G.BUILDING, 0, 4)
        do_id, callback = self.air.pending[0]
        self.assertIn('groups-locate-1', self.tasks.named)
        self.disconnect(1)
        self.assertNotIn(1, self.ud._loc_wait)
        self.assertNotIn('groups-locate-1', self.tasks.named)
        callback(do_id, 7, 5000)
        self.assertIsNone(self.ud.office.board.of_avatar.get(1))
        self.assertEqual(self.ud.office.board.groups, {})
        self.assertEqual(self.fields(1), [])

    def test_stale_location_does_not_pop_the_newer_create(self):
        self.air.defer = True
        self.stood(1, 2000, 1, G.ACT_STREET, place=(7, 5000))
        self.call(1, 'createGroup', G.BUILDING, 0, 4)
        _do_id, first = self.air.pending[0]
        self.call(1, 'createGroup', G.BUILDING, 1, 4)
        _do_id, second = self.air.pending[1]
        first(1, 7, 5000)
        self.assertIsNone(self.ud.office.board.of_avatar.get(1))
        self.assertIn(1, self.ud._loc_wait)
        self.assertEqual(self.fields(1), [])
        second(1, 9, 2000)
        g = self.ud.office.board.groups[1]
        self.assertEqual((g.hood_id, g.shard_id, g.target), (2000, 9, 1))

    def test_stale_locate_timeout_does_not_drop_the_newer_job(self):
        self.air.defer = True
        self.stood(1, 2000, 1, G.ACT_STREET, place=(9, 2000))
        self.call(1, 'createGroup', G.BUILDING, 0, 4)
        old_timeout = self.tasks.named['groups-locate-1']
        old_gen = self.ud._loc_wait[1][0]
        self.call(1, 'createGroup', G.BUILDING, 1, 4)
        newer = self.ud._loc_wait[1]
        self.assertNotEqual(old_gen, newer[0])
        self.assertIsNot(self.tasks.named['groups-locate-1'], old_timeout)
        old_timeout(None)
        self.assertEqual(self.ud._loc_wait[1], newer)
        self.assertEqual(self.fields(1), [])
        self.assertIn('groups-locate-1', self.tasks.named)

    def test_outdoor_zone_and_golf_zone_can_create_a_group(self):
        self.assertEqual(context.hood_of_zone(6000), 6000)
        self.assertEqual(context.hood_of_zone(17000), 17000)
        self.assertEqual(context.hood_of_zone(7000), 0)
        self.assertNotIn(7000, context.PLAYGROUND_HOODS)
        for av, hood in ((1, 6000), (2, 17000)):
            self.stood(av, hood, 3, G.ACT_PLAYGROUND, place=(3, hood))
            self.call(av, 'createGroup', G.BUILDING, 0, 4)
            g = self.ud.office.board.groups[self.ud.office.board.of_avatar[av]]
            self.assertEqual((g.hood_id, g.shard_id), (hood, 3))

    def test_dynamic_interior_is_not_a_published_hood(self):
        self.stood(1, 2000, 1, G.ACT_STREET, place=(7, 61050))
        self.call(1, 'createGroup', G.BUILDING, 0, 4)
        self.assertIsNone(self.ud.office.board.of_avatar.get(1))
        sent = [args for av, field, args in self.ud.sent if av == 1 and field == 'result']
        self.assertEqual(sent[-1][1], G.ERR_BAD_ARGS)

    def test_avatar_gone_ignores_a_different_sender(self):
        self.stood(1, 2000, 1, G.ACT_STREET)
        self.call(1, 'createGroup', G.BUILDING, 0, 4)
        self.call(2, 'joinGroup', 1)
        self.disconnect(1, sender=self.channel_of(1) + 9)
        self.assertEqual(self.ud.office.board.groups[1].members, [1, 2])
        self.assertIn(1, self.ud._client_channels)
        self.disconnect(1)
        self.assertEqual(self.ud.office.board.groups[1].members, [2])
        self.assertNotIn(1, self.ud._client_channels)

    def test_context_set_after_create_pushes_the_task_group(self):
        self.stood(12, 5000, 9, G.ACT_NONE, ())
        self.call(12, 'createGroup', G.TASK, 0, 4)
        self.assertEqual(self.ud.office.board.groups[1].hood_id, 2000)
        self.assertEqual(self.fields(10), [])
        self.stood(10, 1000, 1, G.ACT_NONE, (2000,))
        self.assertEqual(self.fields(10), ['groupChanged'])
        self.stood(11, 5000, 1, G.ACT_NONE, (1000,))
        self.assertEqual(self.fields(11), [])

    def test_disconnect_datagram_frees_the_seat(self):
        self.stood(1, 2000, 1, G.ACT_STREET)
        self.call(1, 'createGroup', G.BUILDING, 0, 4)
        self.call(2, 'joinGroup', 1)
        client = self.channel_of(1)
        self.ud.remember_client(1, client, 11)
        self.ud.remember_client(2, self.channel_of(2), 22)
        outer = wire.post_remove_bytes(client, self.air.ourChannel, 1, 2294, 11)
        inner = wire.unwrap_post_remove(outer)
        parsed = wire.parse_field_update(inner)
        self.assertEqual(parsed[0], G.GROUPS_MANAGER_DO_ID)
        self.assertEqual(parsed[1], client)
        self.assertEqual(parsed[2], G.GROUPS_MANAGER_DO_ID)
        self.assertEqual(parsed[3], 2294)
        self.assertNotEqual(parsed[3], 11)
        self.ud.dispatch_server_datagram(inner)
        self.assertEqual(self.air.msgSender, client)
        self.assertEqual(self.ud.office.board.groups[1].members, [2])
        self.assertEqual(self.ud.office.board.groups[1].leader_id, 2)
        self.assertNotIn(1, self.ud.office.contexts)
        outer2 = wire.post_remove_bytes(self.channel_of(2), self.air.ourChannel, 2, 2294, 22)
        self.ud.dispatch_server_datagram(wire.unwrap_post_remove(outer2))
        self.assertEqual(self.ud.office.board.groups, {})

    def test_late_post_remove_from_the_previous_login_is_ignored(self):
        self.stood(1, 2000, 1, G.ACT_STREET)
        self.call(1, 'createGroup', G.BUILDING, 0, 4)
        self.call(2, 'joinGroup', 1)
        channel = self.channel_of(1)
        self.ud.remember_client(1, channel, 11)
        stale = wire.avatar_gone_bytes(channel, 1, 2294, 11)
        self.ud.remember_client(1, channel, 22)
        self.ud.dispatch_server_datagram(stale)
        self.assertEqual(self.ud.office.board.groups[1].members, [1, 2])
        self.assertEqual(self.ud._client_channels[1], (channel, 22))
        current = wire.avatar_gone_bytes(channel, 1, 2294, 22)
        self.ud.dispatch_server_datagram(current)
        self.assertEqual(self.ud.office.board.groups[1].members, [2])
        self.assertNotIn(1, self.ud._client_channels)

    def test_task_group_reaches_a_matching_non_subscriber(self):
        self.stood(10, 1000, 1, G.ACT_NONE, (2000,))
        self.stood(11, 5000, 1, G.ACT_NONE, (1000,))
        self.stood(12, 5000, 9, G.ACT_NONE, ())
        self.call(12, 'createGroup', G.TASK, 0, 4)
        g = self.ud.office.board.groups[1]
        self.assertEqual((g.hood_id, g.shard_id), (2000, 9))
        self.assertEqual(self.fields(10), ['groupChanged'])
        self.assertEqual(self.fields(11), [])


class ClientBoardTests(unittest.TestCase):
    def test_gap_requests_a_new_board(self):
        b, _ = board()
        b.create(1, G.BOSS, 0, 11000, 1, 8)
        cb = G.ClientBoard()
        cb.set_board(b.version, b.snapshot())
        _, gid2, _ = b.create(2, G.BUILDING, 0, 2000, 1, 4)
        ok = cb.changed(b.version, b.groups[gid2].to_wire())
        self.assertTrue(ok)
        b.leave(2)
        b.create(3, G.TASK, 1, 1000, 1, 4)
        gap = cb.changed(b.version, b.groups[max(b.groups)].to_wire())
        self.assertFalse(gap)

    def test_rows_put_own_group_first_and_full_last(self):
        b, _ = board()
        _, a, _ = b.create(1, G.BOSS, 0, 11000, 1, 2)
        b.join(2, a)
        _, o, _ = b.create(3, G.TASK, 0, 2000, 1, 4)
        _, m, _ = b.create(4, G.BUILDING, 0, 2000, 1, 4)
        cb = G.ClientBoard()
        cb.set_board(b.version, b.snapshot())
        self.assertEqual([g.group_id for g in cb.rows(4)], [m, o, a])
        self.assertEqual(cb.own(4).group_id, m)


class MatchTests(unittest.TestCase):
    def make(self, kind, hood=2000):
        return G.Group(1, 50, kind, 0, hood, 1, 4)

    def test_rules(self):
        boss, bld, task = self.make(G.BOSS, 11000), self.make(G.BUILDING), self.make(G.TASK)
        lobby = {'activity': G.ACT_BOSS_LOBBY, 'hood_id': 11000}
        street = {'activity': G.ACT_STREET, 'hood_id': 2000}
        self.assertTrue(G.matches(boss, lobby))
        self.assertFalse(G.matches(boss, street))
        self.assertTrue(G.matches(bld, street))
        self.assertFalse(G.matches(bld, {'activity': G.ACT_STREET, 'hood_id': 1000}))
        self.assertFalse(G.matches(bld, {'activity': G.ACT_PLAYGROUND, 'hood_id': 2000}))
        self.assertTrue(G.matches(task, {'task_hoods': (2000,)}))
        self.assertFalse(G.matches(task, {'task_hoods': (1000,)}))

    def test_own_full_and_started_groups_never_badge(self):
        street = {'activity': G.ACT_STREET, 'hood_id': 2000}
        g = self.make(G.BUILDING)
        self.assertFalse(G.matches(g, street, own_av_id=50))
        g.flags = G.FLAG_IN_PROGRESS
        self.assertFalse(G.matches(g, street))
        g.flags = 0
        g.members = [1, 2, 3, 4]
        self.assertFalse(G.matches(g, street))

    def test_badge_count_clears_when_group_goes(self):
        b, _ = board()
        _, gid, _ = b.create(1, G.BUILDING, 0, 2000, 1, 4)
        cb = G.ClientBoard()
        cb.set_board(b.version, b.snapshot())
        ctx = {'activity': G.ACT_STREET, 'hood_id': 2000}
        self.assertEqual(G.badge_count(cb, ctx, own_av_id=9), 1)
        cb.gone(b.version + 1, gid)
        self.assertEqual(G.badge_count(cb, ctx, own_av_id=9), 0)

    def test_activity_and_places(self):
        self.assertEqual(context.activity_for(11000, False, False, False), G.ACT_BOSS_LOBBY)
        self.assertEqual(context.activity_for(2000, False, False, True), G.ACT_BUILDING)
        self.assertEqual(context.activity_for(2000, False, True, False), G.ACT_STREET)
        self.assertEqual(context.activity_for(2000, True, False, False), G.ACT_PLAYGROUND)
        self.assertEqual(context.place_for_new_group(G.BOSS, G.CASHBOT, 2000, S.TaskHoods), 12000)
        self.assertEqual(context.place_for_new_group(G.TASK, 2, 2000, S.TaskHoods), 3000)
        self.assertEqual(context.place_for_new_group(G.BUILDING, 0, 1000, S.TaskHoods), 1000)


class TeleportTests(unittest.TestCase):
    def plan(self, g, shard=1, hood=2000, friends=(), access=(2000, 1000), avail=(2000, 1000)):
        return G.teleport_plan(g, shard, hood, set(friends), set(access), set(avail))

    def test_steps(self):
        here = G.Group(1, 50, G.TASK, 0, 2000, 1, 4)
        self.assertEqual(self.plan(here)[0], G.GO_NONE)
        self.assertEqual(self.plan(G.Group(1, 50, G.TASK, 0, 1000, 1, 4), friends=(50,))[0], G.GO_FRIEND)
        self.assertEqual(self.plan(G.Group(1, 50, G.TASK, 0, 1000, 1, 4))[0], G.GO_HOOD)
        self.assertEqual(self.plan(G.Group(1, 50, G.TASK, 0, 1000, 2, 4)), (G.GO_HOOD_AFTER_SHARD, True))
        self.assertEqual(self.plan(G.Group(1, 50, G.BOSS, 0, 11000, 1, 4))[0], G.GO_TELL)
        self.assertEqual(self.plan(G.Group(1, 50, G.TASK, 0, 1000, 1, 4), avail=())[0], G.GO_TELL)


class StringsTests(unittest.TestCase):
    def test_presets_line_up(self):
        self.assertEqual(len(S.TaskNames), len(S.TaskHoods))
        self.assertEqual(len(S.TaskNames), G.MAX_TARGET[G.TASK] + 1)
        self.assertEqual(len(S.DepartmentNames), G.MAX_TARGET[G.BOSS] + 1)
        self.assertEqual(len(S.KindButtons), len(G.KINDS))
        self.assertEqual(len(S.KindShort), len(G.KINDS))
        self.assertEqual(S.TaskHoods, G.TASK_PLACE_HOODS)
        self.assertEqual(S.task_place(2), 'The Brrrgh')
        for code in range(1, 7):
            self.assertIn(code, S.Errors)
        for kind in G.KINDS:
            for target in range(G.MAX_TARGET[kind] + 1):
                self.assertTrue(S.group_title(kind, target))

    def test_button_contrast(self):
        theme = _theme_colors()
        with open(os.path.join(ROOT, 'source', 'ui', 'book_pages', 'page_groups.cpp'),
                  encoding='utf-8') as f:
            page = f.read()
        self.assertIn('on ? theme::kBlueDark : theme::kGrey', page)
        self.assertIn('on ? theme::kWhite : theme::kInk', page)
        self.assertIn('!enabled ? theme::kGrey : leave ? theme::kRedDark : theme::kGreenDark', page)
        self.assertIn('enabled ? theme::kWhite : theme::kInk', page)
        self.assertIn('theme::kBlueDark', page)
        self.assertIn('theme::kRedDark', page)
        states = (
            ('enabled', 'kWhite', 'kGreenDark'),
            ('disabled', 'kInk', 'kGrey'),
            ('full', 'kInk', 'kGrey'),
            ('own Leave', 'kWhite', 'kRedDark'),
            ('enabled filter', 'kWhite', 'kBlueDark'),
            ('disabled filter', 'kInk', 'kGrey'),
        )
        with open(CPP_LOGIC, encoding='utf-8') as f:
            header = f.read()
        rows = re.findall(r'\{"([^"]+)", \{([^}]+)\}, \{([^}]+)\}\}', header)
        self.assertEqual([name for name, _text, _face in states], [row[0] for row in rows])
        for (name, text_tok, face_tok), (_name, text_src, face_src) in zip(states, rows):
            text = theme[text_tok]
            face = theme[face_tok]
            self.assertEqual(text, _hex_triple(text_src), name)
            self.assertEqual(face, _hex_triple(face_src), name)
            ratio = _contrast(text, face)
            self.assertGreaterEqual(ratio, 4.5, '%s is %.2f' % (name, ratio))

    def test_empty_line_names_the_filters(self):
        with open(CPP_LOGIC, encoding='utf-8') as f:
            header = f.read()
        found = re.search(r'kEmpty = "([^"]*)"', header)
        self.assertIsNotNone(found)
        text = found.group(1)
        self.assertEqual(text, S.EmptyList)
        for word in ('Boss', 'Building', 'Task'):
            self.assertIn(word, text)
            self.assertIn(word, S.EmptyList)
        self.assertNotIn('press Create', text)
        self.assertNotIn('press Create', S.EmptyList)
        self.assertLessEqual(_impress_width(text, 0.40), 288.0)

    def test_rejects_press_create(self):
        needle = 'press Create'
        paths = [
            CPP_LOGIC,
            os.path.join(ROOT, 'source', 'ui', 'book_pages', 'page_groups.cpp'),
            os.path.join(PY, 'ttgroups', 'strings.py'),
            os.path.join(ROOT, 'docs', 'design', 'groups.md'),
            os.path.join(ROOT, 'tools', 'groups', 'render_3ds.py'),
        ]
        for path in paths:
            with open(path, encoding='utf-8') as f:
                self.assertNotIn(needle, f.read(), path)

    def test_user_strings_have_no_ellipsis(self):
        with open(os.path.join(PY, 'ttgroups', 'strings.py'), encoding='utf-8') as f:
            text = f.read()
        self.assertNotIn('...', text)
        self.assertNotIn('\u2026', text)
        self.assertNotEqual(S.Connecting, '')

    def test_no_dashes_or_banned_wording(self):
        banned = re.compile('|'.join(['toon\\s*' + 'hq', 'toon' + 'hq']), re.I)
        files = [os.path.abspath(__file__), CPP_LOGIC, CPP_CLIENT]
        for base in (os.path.join(PY, 'ttgroups'), os.path.join(ROOT, 'source', 'ui', 'book_pages')):
            for name in os.listdir(base):
                if name.startswith('page_groups') or name.endswith('.py') or name == 'groups.dc':
                    files.append(os.path.join(base, name))
        files.append(DC)
        for path in files:
            if os.path.isfile(path):
                with open(path, encoding='utf-8') as f:
                    text = f.read()
                self.assertNotIn(chr(0x2014), text, path)
                self.assertIsNone(banned.search(text), path)


class DcTests(unittest.TestCase):
    def test_struct_matches_wire(self):
        with open(DC) as f:
            text = f.read()
        struct = re.search(r'struct TTGroupInfo \{(.*?)\};', text, re.S).group(1)
        fields = [l.strip() for l in struct.strip().splitlines()]
        self.assertEqual(len(fields), 9)
        self.assertTrue(fields[-1].startswith('uint32 members['))
        self.assertIn('from ttgroups import TTGroupsManager/AI/UD', text)
        self.assertEqual(text.count('{'), text.count('}'))
        self.assertRegex(text, r'setContext\(uint32, uint32, uint8, uint32\[\]\) clsend;')
        self.assertRegex(text, r'createGroup\(uint8, uint8, uint8\) clsend;')
        self.assertNotIn('createGroup(uint8, uint8, uint32, uint32, uint8)', text)

    def test_fields_the_code_calls_exist(self):
        with open(DC) as f:
            text = f.read()
        for name in ('requestBoard', 'stopBoard', 'setContext', 'createGroup', 'joinGroup', 'leaveGroup',
                     'setBoard', 'groupChanged', 'groupGone', 'result', 'noteActivity', 'avatarGone'):
            self.assertRegex(text, r'\b%s\(' % name)
        self.assertRegex(text, r'avatarGone\(uint32 avId, uint32 nonce\);')
        self.assertNotRegex(text, r'avatarGone\([^)]*\) clsend;')
        fields = re.findall(r'^\s*(\w+)\(', re.search(r'dclass TTGroupsManager : DistributedObject \{(.*?)\n\};',
                                                      text, re.S).group(1), re.M)
        # 11 is the class-local index. The wire id is assigned by DCFile and is not 11.
        self.assertEqual(fields.index('avatarGone'), 11)
        for name in ('requestBoard', 'stopBoard', 'setContext', 'createGroup', 'joinGroup', 'leaveGroup'):
            self.assertRegex(text, r'\b%s\([^)]*\) clsend;' % name)
        with open(CPP_CLIENT, encoding='utf-8') as f:
            client = f.read()
        for name in ('kSetBoard', 'kGroupChanged', 'kGroupGone', 'kResult'):
            self.assertIn(name, client)

    def test_avatar_gone_wire_id_is_2294(self):
        # Every id the 3DS hardcodes, from a real DCFile. No skip: a missing
        # wheel or a missing otp.dc fails the suite.
        try:
            from panda3d.core import Filename
            from panda3d.direct import DCFile
        except ImportError as exc:
            self.fail('Panda3D is required (pip install panda3d==1.10.15): %s' % exc)
        etc = ott_etc()
        if etc is None:
            self.fail('otp.dc and toon.dc not found. Set OTT= to the open-toontown checkout, '
                      'or init ref/open-toontown.')
        self.assertTrue(os.path.isfile(PLATFORM_DC), PLATFORM_DC)
        otp = os.path.join(etc, 'otp.dc')
        toon = os.path.join(etc, 'toon.dc')
        stock = DCFile()
        self.assertTrue(stock.read(Filename.fromOsSpecific(otp)), otp)
        self.assertTrue(stock.read(Filename.fromOsSpecific(toon)), toon)
        self.assertEqual(stock.getHash(), 547155168)
        dc = DCFile()
        for path in (otp, toon, PLATFORM_DC, DC):
            self.assertTrue(dc.read(Filename.fromOsSpecific(path)), path)
        plat = dc.getClassByName('TTPlatformManager')
        self.assertIsNotNone(plat)
        self.assertEqual(plat.getNumFields(), len(PLATFORM_FIELDS))
        for index, (name, number) in enumerate(PLATFORM_FIELDS):
            field = plat.getFieldByName(name)
            self.assertEqual(field.getNumber(), number, name)
            self.assertEqual(plat.getField(index).getName(), name)
        info = dc.getClassByName('TTGroupInfo')
        self.assertIsNotNone(info)
        for index, (name, number) in enumerate(STRUCT_FIELDS):
            field = info.getFieldByName(name)
            self.assertEqual(field.getNumber(), number, name)
            self.assertEqual(info.getField(index).getName(), name)
        cls = dc.getClassByName('TTGroupsManager')
        self.assertEqual(cls.getNumFields(), len(GROUPS_FIELDS))
        with open(CPP_LOGIC, encoding='utf-8') as f:
            logic = f.read()
        self.assertIn('kFieldBase = %d' % GROUPS_BASE, logic)
        self.assertIn('kAvatarGoneField = 2294', logic)
        for index, name in enumerate(GROUPS_FIELDS):
            field = cls.getFieldByName(name)
            number = GROUPS_BASE + index
            self.assertEqual(field.getNumber(), number, name)
            self.assertEqual(cls.getField(index).getName(), name)
            self.assertEqual(cls.getField(index).getNumber(), number)
            self.assertEqual(field.hasKeyword('clsend'), name in CLSEND, name)
            self.assertIn('k%s = kFieldBase + %d' % (name[0].upper() + name[1:], index), logic)
        self.assertEqual(cls.getFieldByName('avatarGone').getNumber(), 2294)
        self.assertFalse(cls.getFieldByName('avatarGone').hasKeyword('clsend'))
        self.assertEqual(cls.getField(11).getName(), 'avatarGone')
        self.assertNotEqual(cls.getField(11).getNumber(), 11)
        with open(os.path.join(ROOT, 'source', 'net', 'dc_generated.h'), encoding='utf-8') as f:
            generated = f.read()
        self.assertIn('kHash = 547155168u', generated)
        from ttgroups.postremove import add_groups_post_remove, send_groups_disconnect
        sent = []
        remembered = []

        class Mgr:
            def __init__(self):
                self.nonces = {}

            def remember_client(self, av, channel, nonce):
                remembered.append((av, channel, nonce))
                self.nonces[av] = nonce

            def client_nonce(self, av):
                return self.nonces.get(av)

        air = types.SimpleNamespace(
            ourChannel=1000000,
            dclassesByName={'TTGroupsManagerUD': cls},
            groupsManager=Mgr(),
            send=lambda dg: sent.append(bytes(dg.getMessage())),
        )
        channel = (1003 << 32) | 4
        add_groups_post_remove(air, channel, 4)
        self.assertEqual(remembered[0][0], 4)
        self.assertEqual(remembered[0][1], channel)
        nonce = remembered[0][2]
        inner = wire.unwrap_post_remove(sent[0])
        parsed = wire.parse_field_update(inner)
        self.assertEqual(parsed[3], 2294)
        self.assertEqual(parsed[1], channel)
        self.assertEqual(int.from_bytes(parsed[4][:4], 'little'), 4)
        self.assertEqual(int.from_bytes(parsed[4][4:], 'little'), nonce)
        send_groups_disconnect(air, channel, 4)
        sent_now = wire.parse_field_update(sent[1])
        self.assertEqual(sent_now[3], 2294)
        self.assertEqual(int.from_bytes(sent_now[4][4:], 'little'), nonce)

    def test_manager_id_matches_the_client(self):
        with open(CPP_LOGIC, encoding='utf-8') as f:
            text = f.read()
        self.assertIn('kManagerDoId = %d' % G.GROUPS_MANAGER_DO_ID, text)
        self.assertIn('kAvatarGoneField = 2294', text)

    def test_same_fields_on_both_ends(self):
        with open(os.path.join(PY, 'ttgroups', 'TTGroupsManagerUD.py')) as f:
            ud = f.read()
        for name in ('requestBoard', 'stopBoard', 'setContext', 'createGroup', 'joinGroup', 'leaveGroup', 'noteActivity', 'avatarGone'):
            self.assertIn('def %s(' % name, ud)
        self.assertRegex(ud, r'def createGroup\(self, kind, target, max_size\)')
        self.assertIn('self.office.create(av, kind, target, max_size, parent_id, hood)', ud)
        self.assertIn('self.air.getLocation', ud)
        self.assertIn('self.office.avatar_gone(av)', ud)
        self.assertIn('self.air.getMsgSender()', ud)
        self.assertIn('dispatch_server_datagram', ud)
        self.assertNotIn("ctx['shard_id']", ud)
        self.assertNotIn('_loc_seq', ud)
        self.assertNotIn('def handle_location', ud)
        self.assertIn("'avatarGone %s field %s %s'", ud)
        self.assertIn('self._avatar_gone_field()', ud)


class OfficeTests(unittest.TestCase):
    def test_task_hoods_are_capped_and_validated(self):
        self.assertEqual(G.clean_task_hoods([999, 2000, 2000, 'no', 1000, 3000, 4000, 5000]),
                         (2000, 1000, 3000, 4000))
        self.assertEqual(G.clean_task_hoods(None), ())

    def test_task_group_reaches_a_matching_non_subscriber(self):
        office = Office()
        office.set_context(10, 1000, 1, G.ACT_NONE, [999, 2000, 2000, 1000, 3000, 4000, 5000])
        self.assertEqual(office.contexts[10]['task_hoods'], (2000, 1000, 3000, 4000))
        office.set_context(11, 5000, 1, G.ACT_NONE, (1000,))
        office.set_context(12, 5000, 9, G.ACT_NONE, ())
        code, gid, _changes = office.create(12, G.TASK, 0, 4, 9, 5000)
        self.assertEqual(code, G.OK)
        g = office.board.groups[gid]
        self.assertEqual((g.hood_id, g.shard_id), (2000, 9))
        got = office.recipients(g)
        self.assertIn(10, got)
        self.assertNotIn(11, got)
        self.assertNotIn(12, got)
        office.page_subs.add(11)
        self.assertIn(11, office.recipients(g))

    def test_spoofed_context_does_not_change_the_published_place(self):
        office = Office()
        office.set_context(12, 9000, 99, G.ACT_STREET, ())
        code, gid, _changes = office.create(12, G.BUILDING, 0, 4, 7, 5000)
        self.assertEqual(code, G.OK)
        g = office.board.groups[gid]
        self.assertEqual((g.hood_id, g.shard_id), (5000, 7))
        self.assertEqual(office.contexts[12]['hood_id'], 9000)
        office.set_context(16, 9000, 99, G.ACT_NONE, ())
        code, boss_id, _changes = office.create(16, G.BOSS, G.CASHBOT, 8, 7, 2000)
        self.assertEqual(code, G.OK)
        boss = office.board.groups[boss_id]
        self.assertEqual((boss.hood_id, boss.shard_id), (12000, 7))
        self.assertEqual(office.create(99, G.BUILDING, 0, 4, 0, 5000)[0], G.ERR_BAD_ARGS)
        self.assertEqual(office.create(13, G.BUILDING, 0, 4, 3, 0)[0], G.ERR_BAD_ARGS)
        self.assertEqual(office.create(14, G.TASK, 0, 4, 0, 0)[0], G.ERR_BAD_ARGS)
        office.set_context(15, 2000, 3, 99, (-1, 2000))
        self.assertEqual(office.contexts[15]['activity'], G.ACT_NONE)
        self.assertEqual(office.contexts[15]['task_hoods'], (2000,))
        self.assertEqual(context.hood_of_zone(2100), 2000)
        self.assertEqual(context.hood_of_zone(1), 0)
        self.assertEqual(context.hood_of_zone(61050), 0)
        self.assertEqual(office.create(18, G.BUILDING, 0, 4, 7, 61000)[0], G.ERR_BAD_ARGS)
        self.assertNotIn(18, office.board.of_avatar)

    def test_avatar_gone_drops_the_toon_immediately(self):
        office = Office()
        office.set_context(12, 5000, 9, G.ACT_NONE, ())
        office.page_subs.add(12)
        code, gid, _changes = office.create(12, G.BUILDING, 0, 4, 9, 5000)
        self.assertEqual(code, G.OK)
        self.assertEqual(office.avatar_gone(12), [('gone', gid)])
        self.assertNotIn(gid, office.board.groups)
        self.assertNotIn(12, office.page_subs)
        self.assertNotIn(12, office.contexts)
        office.set_context(20, 5000, 9, G.ACT_NONE, ())
        office.set_context(21, 5000, 9, G.ACT_NONE, ())
        code, gid2, _changes = office.create(20, G.BUILDING, 0, 4, 9, 5000)
        self.assertEqual(code, G.OK)
        self.assertEqual(office.board.join(21, gid2)[0], G.OK)
        changes = office.avatar_gone(20)
        self.assertEqual(changes[0][0], 'changed')
        left = office.board.groups[gid2]
        self.assertEqual(left.leader_id, 21)
        self.assertEqual(office.avatar_gone(404), [])

    def test_client_create_sends_no_place(self):
        with open(CPP_CLIENT, encoding='utf-8') as f:
            client = f.read()
        self.assertIn('w.u8(kind)', client)
        self.assertIn('w.u8(target)', client)
        self.assertIn('w.u8(size)', client)
        self.assertNotIn('hood, shard', client)


class LayoutTests(unittest.TestCase):
    def test_compact_320_uses_one_large_full_width_row(self):
        m = page_layout(320, 240, 1.0)
        self.assertTrue(m.compact)
        self.assertLessEqual(m.rows_shown, 2)
        self.assertGreaterEqual(m.place_px, 12)
        self.assertGreaterEqual(m.title_px, 14)
        self.assertTrue(m.full_width)
        self.assertGreater(m.button_w, 1.2)
        self.assertGreaterEqual(m.button_px_h, 48)
        self.assertGreaterEqual(m.button_px_w, 48)

    def test_phone_and_fold_buttons_meet_the_touch_floor(self):
        phone = page_layout(2400, 1080, 2.75)
        self.assertFalse(phone.compact)
        self.assertGreaterEqual(phone.button_px_h + 1e-6, 48 * 2.75)
        self.assertGreaterEqual(phone.button_px_w + 1e-6, 48 * 2.75)
        fold = page_layout(1080, 1100, 2.75)
        floor = 48 * 2.75
        self.assertGreaterEqual(fold.button_px_h + 1e-6, floor)
        self.assertGreaterEqual(fold.button_px_w + 1e-6, floor)
        self.assertGreaterEqual(fold.button_px_h + 1e-6, 44 * 2.75)

    def test_compact_back_button_fits_in_the_window(self):
        m = page_layout(320, 240, 1.0)
        z = picker_back_z(m)
        self.assertGreaterEqual(z - m.button_h / 2.0, -1.0)
        self.assertLessEqual(z + m.button_h / 2.0, 1.0)
        self.assertGreater(z, -0.82)

    def test_cover_spans_the_window(self):
        m = page_layout(320, 240, 1.0)
        aspect = 320 / 240.0
        self.assertLessEqual(m.cover[0], -aspect)
        self.assertGreaterEqual(m.cover[1], aspect)
        wide = page_layout(1280, 800, 1.0)
        self.assertGreaterEqual(wide.cover[1], 1280 / 800.0)
        tall = page_layout(1080, 1100, 2.75)
        self.assertLessEqual(tall.cover[2], -(1100 / 1080.0))

    def test_desktop_list_keeps_the_taller_sheet(self):
        m = page_layout(1280, 800, 1.0)
        top, bot = list_frame(m)
        self.assertGreaterEqual((top - bot) + 1e-9, 0.78)
        self.assertGreaterEqual(int((top - bot) / m.row_h + 1e-9), 4)
        compact = page_layout(320, 240, 1.0)
        ctop, cbot = list_frame(compact)
        self.assertLessEqual((ctop - cbot) + 1e-9, compact.row_h * 1.01)

    def test_desktop_button_is_at_least_48px(self):
        m = page_layout(1280, 800, 1.0)
        self.assertFalse(m.compact)
        self.assertGreaterEqual(m.button_px_h, 48)
        self.assertLess(m.button_w, 0.8)
        dense = page_layout(1280, 800, 1.5)
        self.assertGreaterEqual(dense.button_px_h, 72)

    def test_refresh_does_not_stack_connecting_and_empty(self):
        self.assertEqual(list_message(False, 0, ''), (S.Connecting, False))
        self.assertEqual(list_message(True, 0, S.Connecting), ('', True))
        self.assertEqual(list_message(True, 0, ''), ('', True))
        # An empty board shows the empty line even with a status on it (left
        # the last group), while a non-empty board keeps it off.
        self.assertEqual(list_message(True, 0, S.Errors[G.ERR_FULL]), (S.Errors[G.ERR_FULL], True))
        self.assertEqual(list_message(True, 2, 'x'), ('x', False))
        self.assertEqual(list_message(True, 2, ''), ('', False))


class MoveFocusTests(unittest.TestCase):
    def test_move_focus_stops_at_ends(self):
        mf = G.move_focus
        self.assertEqual(mf(-1, 0, 1), -1)
        self.assertEqual(mf(-1, 3, 1), 0)
        self.assertEqual(mf(-1, 3, -1), 2)
        self.assertEqual(mf(2, 3, 1), 2)
        self.assertEqual(mf(0, 3, -1), 0)
        self.assertEqual(mf(1, 3, 1), 2)


class WireTests(unittest.TestCase):
    def test_avatar_gone_bytes_match_the_client_vector(self):
        blob = wire.avatar_gone_bytes(0x100000004, 4, 2294, 11)
        self.assertEqual(blob.hex(), '0172120000000000000400000001000000e40772120000f608040000000b000000')
        parsed = wire.parse_field_update(blob)
        self.assertEqual(parsed[2], G.GROUPS_MANAGER_DO_ID)
        self.assertEqual(parsed[3], 2294)
        self.assertNotEqual(parsed[3], 11)


class CompileTests(unittest.TestCase):
    def test_logic_modules_compile(self):
        for base, _d, files in os.walk(os.path.join(PY, 'ttgroups')):
            for fn in files:
                if fn.endswith('.py'):
                    path = os.path.join(base, fn)
                    with open(path, encoding='utf-8') as f:
                        compile(f.read(), path, 'exec')


if __name__ == '__main__':
    unittest.main()
