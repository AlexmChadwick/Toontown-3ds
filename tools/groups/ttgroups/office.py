"""UberDOG rules with no network and no Panda.

TTGroupsManagerUD delegates here: who is subscribed, the last place each Toon
reported, which groups a non-subscriber hears, where a new group is, and what
happens when an avatar goes offline.
"""
import time

from . import context, groups as G


class Office(object):
    def __init__(self, board=None, clock=None):
        self.board = board if board is not None else G.GroupBoard(clock=clock or time.time)
        self.page_subs = set()
        self.contexts = {}

    def set_context(self, av, hood_id, shard_id, activity, task_hoods):
        if not av:
            return
        try:
            hood_id = int(hood_id)
            shard_id = int(shard_id)
            activity = int(activity)
        except (TypeError, ValueError):
            return
        if activity not in (G.ACT_NONE, G.ACT_PLAYGROUND, G.ACT_STREET, G.ACT_BOSS_LOBBY, G.ACT_BUILDING):
            activity = G.ACT_NONE
        if hood_id < 0:
            hood_id = 0
        if shard_id < 0:
            shard_id = 0
        self.contexts[av] = {
            'hood_id': hood_id,
            'shard_id': shard_id,
            'activity': activity,
            'task_hoods': G.clean_task_hoods(task_hoods),
        }

    def groups_for(self, av):
        """Open groups this Toon's stored context matches, including ones opened earlier."""
        ctx = self.contexts.get(av)
        if not ctx:
            return []
        return [g for g in self.board.groups.values() if G.matches(g, ctx, av)]

    def create(self, av, kind, target, max_size, shard_id, hood_id):
        """Publish at the state-server place. setContext is not a place.

        shard_id is the location parent (the district). hood_id is the canonical
        hood of the location zone. A missing location, or a hood that is not a
        playground or a Cog HQ, is rejected.
        """
        if not G.valid_create(kind, target, max_size) or not av:
            return G.ERR_BAD_ARGS, 0, []
        try:
            shard_id = int(shard_id)
            hood_id = int(hood_id)
        except (TypeError, ValueError):
            return G.ERR_BAD_ARGS, 0, []
        if shard_id <= 0 or hood_id not in context.PUBLISH_HOODS:
            return G.ERR_BAD_ARGS, 0, []
        hood = context.place_for_new_group(kind, target, hood_id, G.TASK_PLACE_HOODS)
        return self.board.create(av, kind, target, hood, shard_id, max_size)

    def recipients(self, group):
        """Page subscribers hear everything. Others hear only a group that matches them."""
        if group is None:
            return set(self.contexts) | set(self.page_subs)
        out = set(self.page_subs)
        for av, ctx in self.contexts.items():
            if G.matches(group, ctx, av):
                out.add(av)
        return out

    def avatar_gone(self, av):
        """Drop the Toon now. Returns the board changes to broadcast."""
        self.page_subs.discard(av)
        self.contexts.pop(av, None)
        return self.board.leave(av)[2]
