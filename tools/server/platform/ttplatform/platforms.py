"""Platform enum and the pure logic every port shares (no Panda imports).

Wire value of TTPlatformManager.setMyPlatform(uint8) / setPlatforms(..., uint8[]).
Cell index in tools/platform-badges/atlas_*.png == enum value.
"""
import os
import sys

UNKNOWN = 0
WEB = 1
MAC = 2
IOS = 3
ANDROID = 4
WINDOWS = 5
VR = 6
N3DS = 7
LINUX = 8

NAMES = ('UNKNOWN', 'WEB', 'MAC', 'IOS', 'ANDROID', 'WINDOWS', 'VR', 'N3DS', 'LINUX')
MAX_PLATFORM = LINUX

# The global object's doId (unused by OtpDoGlobals / ToontownGlobals / astrond.yml).
PLATFORM_MANAGER_DO_ID = 4721
# CLIENT_HELLO keeps the stock otp.dc + toon.dc hash; never the 3-file hash.
PINNED_DC_HASH = 547155168

MAX_IDS_PER_REQUEST = 50
FLUSH_INTERVAL = 0.25


def clamp(value):
    """Any out-of-range value is UNKNOWN."""
    try:
        value = int(value)
    except (TypeError, ValueError):
        return UNKNOWN
    return value if 0 <= value <= MAX_PLATFORM else UNKNOWN


def from_name(name):
    """'mac', 'N3DS', '3ds', 'ipados' ... -> enum (UNKNOWN when unrecognised)."""
    if name is None:
        return UNKNOWN
    key = str(name).strip().upper()
    aliases = {'3DS': N3DS, 'IPHONE': IOS, 'IPAD': IOS, 'IPADOS': IOS, 'MACOS': MAC, 'DARWIN': MAC,
               'WIN': WINDOWS, 'WIN32': WINDOWS, 'QUEST': VR, 'BROWSER': WEB}
    if key in aliases:
        return aliases[key]
    if key.isdigit():
        return clamp(key)
    return NAMES.index(key) if key in NAMES else UNKNOWN


def should_show(own, other, is_local=False):
    """Badge rule: exact compare, unknown hidden, never on our own toon."""
    other = clamp(other)
    return (not is_local) and other != UNKNOWN and other != clamp(own)


def detect_native_platform(override=None):
    """Desktop Python ports: debug override (TT_PLATFORM env or a prc value), else the OS."""
    o = override if override is not None else os.environ.get('TT_PLATFORM')
    if o:
        return from_name(o)
    if sys.platform == 'darwin':
        return MAC
    if sys.platform.startswith('win'):
        return WINDOWS
    if sys.platform.startswith('linux'):
        return ANDROID if 'ANDROID_ROOT' in os.environ else LINUX
    return UNKNOWN


class RequestBatcher:
    """Queue avIds of toons we see; take() at most MAX_IDS_PER_REQUEST every FLUSH_INTERVAL.

    Ids already answered (cache) or already sent (in flight) are not queued again; the
    server pushes later values (registration, VR changes) to us on its own.
    """

    def __init__(self, max_ids=MAX_IDS_PER_REQUEST):
        self.max_ids = max_ids
        self.queue = []
        self.queued = set()
        self.sent = set()

    def add(self, avId, own_av_id=0):
        if not avId or avId == own_av_id or avId in self.queued or avId in self.sent:
            return False
        self.queue.append(avId)
        self.queued.add(avId)
        return True

    def take(self):
        batch, self.queue = self.queue[:self.max_ids], self.queue[self.max_ids:]
        for avId in batch:
            self.queued.discard(avId)
            self.sent.add(avId)
        return batch

    def forget(self, avId):
        """Toon left view for good: allow a fresh request if it comes back."""
        self.sent.discard(avId)
        if avId in self.queued:
            self.queued.discard(avId)
            self.queue.remove(avId)

    def reset(self):
        self.queue, self.queued, self.sent = [], set(), set()
