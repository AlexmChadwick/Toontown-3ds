"""Client side of TTPlatformManager for Panda3D/Python ports (native, android, iOS).

    cr.platformManager = cr.generateGlobalObject(4721, 'TTPlatformManager')
    ...after the avatar enters the world: cr.platformManager.announce()
    ...in DistributedToon.announceGenerate (not the local toon): cr.platformManager.noteToon(doId)
    ...in the nametag: cr.platformManager.shouldShowBadge(doId) / getPlatform(doId)

Without the object (offline modes, old servers) nothing is sent and no badge is shown.
Event 'platformBadgeChanged' (avId, platform) fires whenever a value arrives.
"""
from direct.directnotify import DirectNotifyGlobal
from direct.distributed.DistributedObjectGlobal import DistributedObjectGlobal

from . import platforms as P


class TTPlatformManager(DistributedObjectGlobal):
    notify = DirectNotifyGlobal.directNotify.newCategory('TTPlatformManager')
    TASK = 'ttPlatformFlush'

    def __init__(self, cr):
        DistributedObjectGlobal.__init__(self, cr)
        self.ownPlatform = P.detect_native_platform(self._prcOverride())
        self.cache = {}
        self.batcher = P.RequestBatcher()
        self.announced = False

    @staticmethod
    def _prcOverride():
        try:
            from panda3d.core import ConfigVariableString
            v = ConfigVariableString('tt-platform', '').getValue()
            return v or None
        except Exception:
            return None

    def _ownAvId(self):
        av = getattr(base, 'localAvatar', None)
        return av.doId if av is not None else 0

    def announce(self, platform=None):
        """Send our platform (call after the avatar is in the world; again on VR enter/leave)."""
        if platform is not None:
            self.ownPlatform = P.clamp(platform)
        self.sendUpdate('setMyPlatform', [self.ownPlatform])
        if not self.announced:
            self.announced = True
            taskMgr.doMethodLater(P.FLUSH_INTERVAL, self._flush, self.TASK)
        messenger.send('platformBadgeOwnChanged', [self.ownPlatform])

    def noteToon(self, avId):
        self.batcher.add(avId, self._ownAvId())

    def forgetToon(self, avId):
        self.batcher.forget(avId)

    def _flush(self, task):
        batch = self.batcher.take()
        if batch:
            self.sendUpdate('requestPlatforms', [batch])
        return task.again

    def setPlatforms(self, avIds, platforms):
        for avId, p in zip(avIds, platforms):
            p = P.clamp(p)
            self.cache[avId] = p
            messenger.send('platformBadgeChanged', [avId, p])

    def getPlatform(self, avId):
        return self.cache.get(avId, P.UNKNOWN)

    def shouldShowBadge(self, avId):
        return P.should_show(self.ownPlatform, self.getPlatform(avId), avId == self._ownAvId())

    def delete(self):
        taskMgr.remove(self.TASK)
        self.batcher.reset()
        self.cache.clear()
        DistributedObjectGlobal.delete(self)
