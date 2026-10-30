"""UberDOG side of TTPlatformManager (global doId 4721)."""
from direct.directnotify import DirectNotifyGlobal
from direct.distributed.DistributedObjectGlobalUD import DistributedObjectGlobalUD
from direct.distributed.MsgTypes import CLIENTAGENT_ADD_POST_REMOVE
from direct.distributed.PyDatagram import PyDatagram

from .registry import PlatformRegistry


class TTPlatformManagerUD(DistributedObjectGlobalUD):
    notify = DirectNotifyGlobal.directNotify.newCategory('TTPlatformManagerUD')

    def __init__(self, air):
        DistributedObjectGlobalUD.__init__(self, air)
        self.registry = PlatformRegistry()
        self.postRemoved = set()      # client channels that carry our clearPlatform post-remove

    def announceGenerate(self):
        DistributedObjectGlobalUD.announceGenerate(self)
        self.notify.info('TTPlatformManager ready (doId %d)' % self.doId)

    def _send(self, replies):
        for channel, avIds, plats in replies or ():
            self.sendUpdateToChannel(channel, 'setPlatforms', [avIds, plats])

    def setMyPlatform(self, platform):
        sender = self.air.getMsgSender()
        replies = self.registry.set_platform(sender, platform)
        if replies is None:
            self.notify.debug('setMyPlatform from %d dropped (no avatar or rate limit)' % sender)
            return
        avId = sender & 0xFFFFFFFF
        self.notify.info('avatar %d platform %d' % (avId, platform))
        if sender not in self.postRemoved:
            # On disconnect the client agent sends clearPlatform(avId) to us.
            self.postRemoved.add(sender)
            if len(self.postRemoved) > 100000:
                self.postRemoved.clear()
            field = self.dclass.getFieldByName('clearPlatform')
            cleanup = field.aiFormatUpdate(self.doId, self.doId, self.air.ourChannel, [avId])
            dg = PyDatagram()
            dg.addServerHeader(sender, self.air.ourChannel, CLIENTAGENT_ADD_POST_REMOVE)
            dg.addUint16(cleanup.getLength())
            dg.appendData(cleanup.getMessage())
            self.air.send(dg)
        self._send(replies)

    def requestPlatforms(self, avIds):
        sender = self.air.getMsgSender()
        replies = self.registry.request(sender, avIds)
        if replies is None:
            self.notify.debug('requestPlatforms from %d dropped (no avatar or rate limit)' % sender)
            return
        self._send(replies)

    def clearPlatform(self, avId):
        self.notify.info('avatar %d gone; platform cleared' % avId)
        self._send(self.registry.clear(avId))

    def setPlatforms(self, avIds, platforms):
        pass  # UD -> client only
