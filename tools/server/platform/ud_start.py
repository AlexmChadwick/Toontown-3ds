"""UberDOG entry with TTPlatformManager, launched instead of toontown.uberdog.UDStart.

    python -I ud_start.py [UDStart args...]

Leaves the upstream checkout untouched: it puts this directory (ttplatform/) on sys.path,
makes the UD read otp.dc, toon.dc, then platform.dc (same order as astrond.yml), checks the
first two still hash to 547155168, generates the global object 4721 next to the stock ones,
then runs the stock UDStart.

Upstream location: $OTT_DIR, else <repo>/ref/open-toontown (or ref/ref/...), else the
directory already on PYTHONPATH that contains toontown/ (the VPS layout).
"""
import os
import runpy
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PLATFORM_DC = os.path.join(HERE, 'platform.dc')
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True


def find_upstream():
    root = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
    for cand in (os.environ.get('OTT_DIR', ''), os.path.join(root, 'ref', 'open-toontown'),
                 os.path.join(root, 'ref', 'ref', 'open-toontown')):
        if cand and os.path.isdir(os.path.join(cand, 'toontown')):
            return cand
    for cand in sys.path + os.environ.get('PYTHONPATH', '').split(os.pathsep):
        if cand and os.path.isdir(os.path.join(cand, 'toontown')):
            return cand
    raise SystemExit('ud_start: cannot find the open-toontown checkout (set OTT_DIR)')


OTT = find_upstream()
if OTT not in sys.path:
    sys.path.insert(1, OTT)

from ttplatform import platforms  # noqa: E402
from direct.distributed.ConnectionRepository import ConnectionRepository  # noqa: E402

_readDCFile = ConnectionRepository.readDCFile


def dc_path(name):
    """etc/otp.dc relative to the cwd (as Configrc.prc says), else the upstream etc/."""
    local = os.path.join('etc', name)
    return local if os.path.exists(local) else os.path.join(OTT, 'etc', name)


def readDCFile(self, dcFileNames=None):
    if dcFileNames is None and getattr(self, 'dcSuffix', None) == 'UD':
        from panda3d.direct import DCFile
        stock = [dc_path('otp.dc'), dc_path('toon.dc')]
        probe = DCFile()
        for f in stock:
            probe.read(f)
        if probe.getHash() != platforms.PINNED_DC_HASH:
            raise SystemExit('ud_start: otp.dc + toon.dc hash %d != %d' % (probe.getHash(), platforms.PINNED_DC_HASH))
        dcFileNames = stock + [PLATFORM_DC]
        _install_global(self)
    return _readDCFile(self, dcFileNames)


def _install_global(air):
    stockCreateGlobals = air.createGlobals

    def createGlobals():
        stockCreateGlobals()
        air.platformManager = air.generateGlobalObject(platforms.PLATFORM_MANAGER_DO_ID, 'TTPlatformManager')

    air.createGlobals = createGlobals


ConnectionRepository.readDCFile = readDCFile
sys.argv[0] = 'UDStart'
runpy.run_module('toontown.uberdog.UDStart', run_name='__main__', alter_sys=True)
