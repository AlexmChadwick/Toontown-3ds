"""Server-side state of TTPlatformManagerUD, kept free of Panda for unit tests.

Every method returns a list of (clientChannel, avIds, platforms) replies to send with
setPlatforms; the DO glue does the sending.
"""
import collections
import time

from . import platforms as P


class TokenBucket:
    def __init__(self, capacity, per_second, now):
        self.capacity, self.rate = capacity, per_second
        self.tokens, self.stamp = float(capacity), now

    def take(self, now):
        self.tokens = min(self.capacity, self.tokens + (now - self.stamp) * self.rate)
        self.stamp = now
        if self.tokens >= 1.0:
            self.tokens -= 1.0
            return True
        return False


class PlatformRegistry:
    SET_BURST, SET_RATE = 4, 0.2          # setMyPlatform: 4 at once, then one per 5 s
    REQ_BURST, REQ_RATE = 20, 4.0          # requestPlatforms: 20 at once, then 4/s
    MAX_ENTRIES = 50000                    # avId -> platform (oldest evicted)
    MAX_WATCHERS = 128                     # channels told about one avId's changes
    MAX_LIMITERS = 20000
    REPLY_CHUNK = 100

    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.platforms = collections.OrderedDict()   # avId -> platform
        self.watchers = {}                            # avId -> OrderedDict(channel -> None)
        self.limiters = collections.OrderedDict()     # (channel, kind) -> TokenBucket
        self.dropped = 0

    # -- rate limiting ------------------------------------------------------
    def _allow(self, channel, kind):
        now = self.clock()
        key = (channel, kind)
        bucket = self.limiters.get(key)
        if bucket is None:
            burst, rate = (self.SET_BURST, self.SET_RATE) if kind == 'set' else (self.REQ_BURST, self.REQ_RATE)
            bucket = self.limiters[key] = TokenBucket(burst, rate, now)
            while len(self.limiters) > self.MAX_LIMITERS:
                self.limiters.popitem(last=False)
        else:
            self.limiters.move_to_end(key)
        ok = bucket.take(now)
        if not ok:
            self.dropped += 1
        return ok

    def _watch(self, avId, channel):
        w = self.watchers.get(avId)
        if w is None:
            if len(self.watchers) >= self.MAX_ENTRIES:
                self.watchers.pop(next(iter(self.watchers)))
            w = self.watchers[avId] = collections.OrderedDict()
        w[channel] = None
        w.move_to_end(channel)
        while len(w) > self.MAX_WATCHERS:
            w.popitem(last=False)

    # -- protocol -----------------------------------------------------------
    def set_platform(self, channel, value):
        """setMyPlatform from client `channel` (account << 32 | avId)."""
        avId = channel & 0xFFFFFFFF
        if not avId or not self._allow(channel, 'set'):
            return None
        value = P.clamp(value)
        old = self.platforms.get(avId)
        if value == P.UNKNOWN:
            self.platforms.pop(avId, None)
        else:
            self.platforms[avId] = value
            self.platforms.move_to_end(avId)
            while len(self.platforms) > self.MAX_ENTRIES:
                self.platforms.popitem(last=False)
        if old == value or (old is None and value == P.UNKNOWN):
            return []
        # Push the new value to everyone who asked about this toon (pending or cached).
        return [(ch, [avId], [value]) for ch in self.watchers.get(avId, ()) if ch != channel]

    def request(self, channel, avIds):
        """requestPlatforms from client `channel`; reply only with ids we know."""
        own = channel & 0xFFFFFFFF
        if not own or not self._allow(channel, 'req'):
            return None
        seen, ids, plats = set(), [], []
        for avId in list(avIds)[:P.MAX_IDS_PER_REQUEST]:
            avId &= 0xFFFFFFFF
            if not avId or avId == own or avId in seen:
                continue
            seen.add(avId)
            self._watch(avId, channel)
            p = self.platforms.get(avId)
            if p is not None:
                ids.append(avId)
                plats.append(p)
        out = []
        for i in range(0, len(ids), self.REPLY_CHUNK):
            out.append((channel, ids[i:i + self.REPLY_CHUNK], plats[i:i + self.REPLY_CHUNK]))
        return out

    def clear(self, avId):
        """The avatar's client went away (post-remove): forget it, tell watchers UNKNOWN."""
        old = self.platforms.pop(avId, None)
        watchers = self.watchers.pop(avId, {})
        if old is None:
            return []
        return [(ch, [avId], [P.UNKNOWN]) for ch in watchers]
