"""UberDOG Groups manager: the authoritative GroupBoard plus subscriptions."""
import struct

from direct.distributed.DistributedObjectGlobalUD import DistributedObjectGlobalUD
from direct.task import Task

from . import context, groups as G
from .office import Office
from .wire import parse_field_update


class TTGroupsManagerUD(DistributedObjectGlobalUD):
    def __init__(self, air):
        DistributedObjectGlobalUD.__init__(self, air)
        self.office = Office()
        # avId -> (generation, kind, target, max_size) while air.getLocation has not answered.
        self._loc_wait = {}
        # avId -> next generation. It only increases, so a callback from an older create stays stale.
        self._loc_gen = {}
        # avId -> (account channel, per-login nonce) recorded by LoadAvatarOperation.
        self._client_channels = {}

    def announceGenerate(self):
        DistributedObjectGlobalUD.announceGenerate(self)
        taskMgr.doMethodLater(60.0, self._expire, 'groups-expire')

    def delete(self):
        taskMgr.remove('groups-expire')
        DistributedObjectGlobalUD.delete(self)

    def _expire(self, task):
        self._broadcast(self.office.board.expire())
        return Task.again

    def _sender(self):
        return self.air.getAvatarIdFromSender()

    def _broadcast(self, changes):
        for what, value in changes:
            group = value if what == 'changed' else None
            for av in self.office.recipients(group):
                if what == 'changed':
                    self.sendUpdateToAvatarId(av, 'groupChanged', [self.office.board.version, value.to_wire()])
                else:
                    self.sendUpdateToAvatarId(av, 'groupGone', [self.office.board.version, value])

    def requestBoard(self):
        av = self._sender()
        if av:
            self.office.page_subs.add(av)
            self.sendUpdateToAvatarId(av, 'setBoard', [self.office.board.version, self.office.board.snapshot()])

    def stopBoard(self):
        self.office.page_subs.discard(self._sender())

    def setContext(self, hood_id, shard_id, activity, task_hoods):
        av = self._sender()
        if not av:
            return
        self.office.set_context(av, hood_id, shard_id, activity, task_hoods)
        version = self.office.board.version
        for g in self.office.groups_for(av):
            self.sendUpdateToAvatarId(av, 'groupChanged', [version, g.to_wire()])

    def _reply(self, av, op, result):
        code, gid, changes = result
        self.sendUpdateToAvatarId(av, 'result', [op, code, gid])
        self._broadcast(changes)

    def remember_client(self, av, channel, nonce):
        """LoadAvatarOperation records the account channel and this login's nonce."""
        try:
            av = int(av)
            channel = int(channel)
            nonce = int(nonce) & 0xFFFFFFFF
        except (TypeError, ValueError):
            return
        if av and channel:
            self._client_channels[av] = (channel, nonce)

    def client_nonce(self, av):
        rec = self._client_channels.get(int(av)) if av is not None else None
        if rec is None:
            return None
        return rec[1]

    def _locate_task(self, av):
        return 'groups-locate-%s' % av

    def _scheduler(self):
        # taskMgr is a Panda builtin once the UD is running.
        try:
            return taskMgr
        except NameError:
            return None

    def _remove_timeout(self, av):
        scheduler = self._scheduler()
        if scheduler is None:
            return
        name = self._locate_task(av)
        if scheduler.hasTaskNamed(name):
            scheduler.remove(name)

    def _ask_location(self, av, kind, target, max_size):
        """air.getLocation is the only locate path. The repository owns the reply.

        The callback closes over this request's generation. A later createGroup
        for the same Toon replaces the job, and the older callback must not pop it.
        """
        self._cancel_locate(av)
        gen = self._loc_gen.get(av, 0) + 1
        self._loc_gen[av] = gen
        self._loc_wait[av] = (gen, kind, target, max_size)
        scheduler = self._scheduler()
        if scheduler is not None:
            scheduler.doMethodLater(2.0, lambda task, av=av, gen=gen: self._forget_locate(av, gen), self._locate_task(av))
        self.air.getLocation(
            av,
            lambda do_id, parent_id, zone_id, av=av, gen=gen: self._on_location(av, do_id, parent_id, zone_id, gen))

    def _cancel_locate(self, av):
        """Drop a pending create. A late getLocation callback then does nothing."""
        self._loc_wait.pop(av, None)
        self._remove_timeout(av)

    def _forget_locate(self, av, gen=None):
        """No location arrived. Reject the create that this timeout belongs to."""
        job = self._loc_wait.get(av)
        if job is None or (gen is not None and job[0] != gen):
            return Task.done
        self._loc_wait.pop(av, None)
        self._remove_timeout(av)
        self._reply(av, G.OP_CREATE, (G.ERR_BAD_ARGS, 0, []))
        return Task.done

    def _on_location(self, av, do_id, parent_id, zone_id, gen):
        """Callback from air.getLocation: (doId, parentId, zoneId). gen is this request."""
        job = self._loc_wait.get(av)
        if job is None or job[0] != gen:
            return
        self._loc_wait.pop(av, None)
        self._remove_timeout(av)
        _gen, kind, target, max_size = job
        if do_id != av or parent_id <= 0 or zone_id <= 0:
            self._reply(av, G.OP_CREATE, (G.ERR_BAD_ARGS, 0, []))
            return
        hood = context.hood_of_zone(zone_id)
        self._reply(av, G.OP_CREATE, self.office.create(av, kind, target, max_size, parent_id, hood))

    def createGroup(self, kind, target, max_size):
        """Place comes from air.getLocation, not from setContext and not from this call."""
        av = self._sender()
        if not av:
            return
        if not G.valid_create(kind, target, max_size):
            self._reply(av, G.OP_CREATE, (G.ERR_BAD_ARGS, 0, []))
            return
        self._ask_location(av, kind, target, max_size)

    def joinGroup(self, group_id):
        av = self._sender()
        if av:
            self._reply(av, G.OP_JOIN, self.office.board.join(av, group_id))

    def leaveGroup(self):
        av = self._sender()
        if av:
            self._reply(av, G.OP_LEAVE, self.office.board.leave(av))

    def _avatar_gone_field(self):
        dclass = getattr(self, 'dclass', None)
        if dclass is None:
            classes = getattr(self.air, 'dclassesByName', None) or {}
            dclass = classes.get('TTGroupsManagerUD') or classes.get('TTGroupsManager')
        if dclass is None:
            return None
        field = dclass.getFieldByName('avatarGone')
        if field is None:
            return None
        return field.getNumber()

    def avatarGone(self, av, nonce=None):
        """Disconnect datagram or explicit unload.

        The sender must be the login's account channel, and nonce must be the
        value that login put in the datagram. The account channel is reused by
        the next login, so a late post-remove from the previous one is ignored.
        """
        try:
            av = int(av)
            nonce = int(nonce) & 0xFFFFFFFF
        except (TypeError, ValueError):
            return
        expected = self._client_channels.get(av)
        sender = int(self.air.getMsgSender())
        if expected is None or sender != expected[0] or nonce != expected[1]:
            return
        self._client_channels.pop(av, None)
        self._cancel_locate(av)
        changes = self.office.avatar_gone(av)
        log = getattr(self, 'notify', None)
        if log is not None:
            # Field number is the wire id this DCFile registered, so a smoke log can prove 2294.
            log.info('avatarGone %s field %s %s' % (av, self._avatar_gone_field(), changes))
        self._broadcast(changes)

    def dispatch_server_datagram(self, blob):
        """Deliver one state-server field update the way the repository does.

        The C++ reader sets the message sender from the datagram header, then
        calls avatarGone. This is that path for the host test.
        """
        parsed = parse_field_update(blob)
        if parsed is None:
            return
        channel, sender, do_id, field_id, payload = parsed
        if channel != G.GROUPS_MANAGER_DO_ID or do_id != G.GROUPS_MANAGER_DO_ID:
            return
        if field_id != self._avatar_gone_field() or len(payload) < 8:
            return
        self.air.msgSender = sender
        av, nonce = struct.unpack_from('<II', payload)
        self.avatarGone(av, nonce)

    def noteActivity(self, av_id, kind, target):
        gid = self.office.board.of_avatar.get(av_id)
        g = self.office.board.groups.get(gid)
        if g is not None and g.leader_id == av_id and g.kind == kind:
            self._broadcast(self.office.board.mark_in_progress(gid))
