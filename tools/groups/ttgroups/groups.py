"""Pure Groups logic shared by the UberDOG, the AI and every Python client (no Panda needed)."""
import time

GROUPS_MANAGER_DO_ID = 4722

BOSS, BUILDING, TASK = range(3)
KINDS = (BOSS, BUILDING, TASK)
# Targets are presets, never free text. Boss and building targets are Cog departments;
# a task group's target is the playground the Toon Task points at.
SELLBOT, CASHBOT, LAWBOT, BOSSBOT = range(4)
DEPARTMENTS = (SELLBOT, CASHBOT, LAWBOT, BOSSBOT)
MAX_TARGET = {BOSS: 3, BUILDING: 3, TASK: 5}
DEFAULT_MAX_SIZE = {BOSS: 8, BUILDING: 4, TASK: 4}
MIN_SIZE, MAX_SIZE = 2, 8

# Activity a client reports with setContext
ACT_NONE, ACT_PLAYGROUND, ACT_STREET, ACT_BOSS_LOBBY, ACT_BUILDING = range(5)

# result(op, code, groupId)
OP_CREATE, OP_JOIN, OP_LEAVE = range(3)
OK, ERR_BAD_ARGS, ERR_IN_GROUP, ERR_NOT_FOUND, ERR_FULL, ERR_RATE, ERR_NOT_IN_GROUP = range(7)

FLAG_IN_PROGRESS = 1
CREATE_COOLDOWN = 10.0
EXPIRY = 30 * 60.0
MAX_GROUPS = 200
# Playgrounds a Toon Task group is announced in, same order as strings.TaskNames.
# Canonical hood ids: Toontown Central, Donald's Dock, The Brrrgh,
# Minnie's Melodyland, Daisy Gardens, Donald's Dreamland.
TASK_PLACE_HOODS = (2000, 1000, 3000, 4000, 5000, 9000)
MAX_TASK_HOODS = 4  # ToontownGlobals.MaxQuestCarryLimit


class Group:
    __slots__ = ('group_id', 'leader_id', 'kind', 'target', 'hood_id', 'shard_id', 'max_size',
                 'flags', 'members', 'touched')

    def __init__(self, group_id, leader_id, kind, target, hood_id, shard_id, max_size, now=0.0):
        self.group_id, self.leader_id = group_id, leader_id
        self.kind, self.target = kind, target
        self.hood_id, self.shard_id = hood_id, shard_id
        self.max_size, self.flags = max_size, 0
        self.members = [leader_id]
        self.touched = now

    def is_full(self):
        return len(self.members) >= self.max_size

    def to_wire(self):
        """Tuple in the field order of the TTGroupInfo struct in groups.dc."""
        return (self.group_id, self.leader_id, self.kind, self.target, self.hood_id, self.shard_id,
                self.max_size, self.flags, list(self.members))

    @classmethod
    def from_wire(cls, w):
        g = cls(w[0], w[1], w[2], w[3], w[4], w[5], w[6])
        g.flags, g.members = w[7], list(w[8])
        return g


def valid_create(kind, target, max_size):
    return (kind in KINDS and 0 <= target <= MAX_TARGET[kind]
            and MIN_SIZE <= max_size <= MAX_SIZE)


def clean_task_hoods(hoods, cap=MAX_TASK_HOODS, valid=None):
    """Task hoods a client may store: known playgrounds only, unique, capped.

    Invalid entries are dropped and do not consume a slot. Extra valid hoods
    past the cap are dropped, so a client cannot claim every playground.
    """
    allow = set(TASK_PLACE_HOODS if valid is None else valid)
    out = []
    for raw in hoods or ():
        try:
            hood = int(raw)
        except (TypeError, ValueError):
            continue
        if hood in allow and hood not in out:
            out.append(hood)
            if len(out) >= cap:
                break
    return tuple(out)


class GroupBoard:
    """Authoritative set of open groups. Every mutator returns (code, groupId, changes) where
    changes is a list of ('changed', Group) / ('gone', groupId) for the manager to broadcast."""

    def __init__(self, clock=time.time):
        self.clock = clock
        self.groups = {}
        self.of_avatar = {}
        self.version = 0
        self.next_id = 1
        self.last_create = {}

    def _bump(self):
        self.version += 1

    def create(self, av_id, kind, target, hood_id, shard_id, max_size):
        now = self.clock()
        if not valid_create(kind, target, max_size) or not av_id:
            return ERR_BAD_ARGS, 0, []
        if av_id in self.of_avatar:
            return ERR_IN_GROUP, self.of_avatar[av_id], []
        if now - self.last_create.get(av_id, -CREATE_COOLDOWN) < CREATE_COOLDOWN or len(self.groups) >= MAX_GROUPS:
            return ERR_RATE, 0, []
        gid = self.next_id
        self.next_id += 1
        g = Group(gid, av_id, kind, target, hood_id, shard_id, max_size, now)
        self.groups[gid] = g
        self.of_avatar[av_id] = gid
        self.last_create[av_id] = now
        self._bump()
        return OK, gid, [('changed', g)]

    def join(self, av_id, group_id):
        g = self.groups.get(group_id)
        if g is None:
            return ERR_NOT_FOUND, group_id, []
        if av_id in self.of_avatar:
            return ERR_IN_GROUP, self.of_avatar[av_id], []
        if g.is_full():
            return ERR_FULL, group_id, []
        g.members.append(av_id)
        g.touched = self.clock()
        self.of_avatar[av_id] = group_id
        self._bump()
        return OK, group_id, [('changed', g)]

    def leave(self, av_id):
        gid = self.of_avatar.pop(av_id, None)
        if gid is None:
            return ERR_NOT_IN_GROUP, 0, []
        g = self.groups[gid]
        g.members.remove(av_id)
        self._bump()
        if not g.members:
            del self.groups[gid]
            return OK, gid, [('gone', gid)]
        if g.leader_id == av_id:
            g.leader_id = g.members[0]  # oldest member
        g.touched = self.clock()
        return OK, gid, [('changed', g)]

    def close(self, group_id):
        g = self.groups.pop(group_id, None)
        if g is None:
            return []
        for m in g.members:
            self.of_avatar.pop(m, None)
        self._bump()
        return [('gone', group_id)]

    def mark_in_progress(self, group_id):
        g = self.groups.get(group_id)
        if g is None or g.flags & FLAG_IN_PROGRESS:
            return []
        g.flags |= FLAG_IN_PROGRESS
        self._bump()
        return [('changed', g)]

    def expire(self):
        now = self.clock()
        gone = [gid for gid, g in self.groups.items() if now - g.touched > EXPIRY]
        changes = []
        for gid in gone:
            changes += self.close(gid)
        for av in [a for a, t in self.last_create.items() if now - t > CREATE_COOLDOWN]:
            del self.last_create[av]
        return changes

    def snapshot(self):
        return [g.to_wire() for g in sorted(self.groups.values(), key=lambda g: g.group_id)]


class ClientBoard:
    """Client-side cache fed by setBoard / groupChanged / groupGone. Tracks versions and tells the
    manager when a gap means it must request the full board again."""

    def __init__(self):
        self.groups, self.version = {}, 0

    def set_board(self, version, wires):
        self.groups = {w[0]: Group.from_wire(w) for w in wires}
        self.version = version

    def changed(self, version, wire):
        """Returns False when a delta was missed (caller re-requests the board)."""
        ok = version <= self.version + 1
        g = Group.from_wire(wire)
        self.groups[g.group_id] = g
        self.version = max(self.version, version)
        return ok

    def gone(self, version, group_id):
        ok = version <= self.version + 1
        self.groups.pop(group_id, None)
        self.version = max(self.version, version)
        return ok

    def rows(self, own_av_id=0):
        """Display order: the Toon's own group first, then open groups, then full ones."""
        def key(g):
            return (own_av_id not in g.members, g.is_full(), g.group_id)
        return sorted(self.groups.values(), key=key)

    def own(self, av_id):
        return next((g for g in self.groups.values() if av_id in g.members), None)


def matches(group, ctx, own_av_id=0):
    """Whether a new group should badge the Book. ctx: dict hood_id, shard_id, activity, task_hoods."""
    if own_av_id in group.members:
        return False
    if group.is_full() or group.flags & FLAG_IN_PROGRESS:
        return False
    act = ctx.get('activity', ACT_NONE)
    if group.kind == BOSS:
        return act == ACT_BOSS_LOBBY
    if group.kind == BUILDING:
        return act in (ACT_STREET, ACT_BUILDING) and group.hood_id == ctx.get('hood_id')
    if group.kind == TASK:
        return group.hood_id in ctx.get('task_hoods', ())
    return False


def badge_count(board, ctx, own_av_id=0):
    return sum(1 for g in board.groups.values() if matches(g, ctx, own_av_id))


# Teleport plan steps
GO_NONE, GO_FRIEND, GO_HOOD, GO_HOOD_AFTER_SHARD, GO_TELL = range(5)


def teleport_plan(group, my_shard, my_hood, friend_ids, teleport_hoods, available_hoods):
    """What Join should do. Returns (step, needs_shard_change)."""
    if group.shard_id == my_shard and group.hood_id == my_hood:
        return GO_NONE, False
    if group.leader_id in friend_ids and group.shard_id == my_shard:
        return GO_FRIEND, False
    if group.hood_id in teleport_hoods and group.hood_id in available_hoods:
        if group.shard_id != my_shard:
            return GO_HOOD_AFTER_SHARD, True
        return GO_HOOD, False
    return GO_TELL, False


def move_focus(index, count, step):
    """Next highlighted row for D-pad or arrow keys; -1 means nothing highlighted. Stops at the ends."""
    if count <= 0:
        return -1
    if index < 0:
        return 0 if step > 0 else count - 1
    return max(0, min(count - 1, index + step))


# Page content extent in page units (x +-0.8, z -0.75 .. 0.68), used to fill a standalone sheet
PAGE_HALF_W, PAGE_Z_LOW, PAGE_Z_HIGH = 0.8, -0.75, 0.68


def fit_scale(aspect, margin=0.04):
    """Scale and z offset that fill a standalone screen of this aspect ratio (width/height).

    A standalone sheet (the 3DS bottom screen) uses it. Inside the Book the page keeps 1.0.
    This is not the Android half-fold open-book scale.
    """
    half_h = (PAGE_Z_HIGH - PAGE_Z_LOW) / 2.0
    s = min(aspect / PAGE_HALF_W, 1.0 / half_h) * (1.0 - margin)
    return s, -s * (PAGE_Z_HIGH + PAGE_Z_LOW) / 2.0
