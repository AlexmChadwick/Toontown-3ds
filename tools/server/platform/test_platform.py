#!/usr/bin/env python3
"""Unit tests for platform badges: enum mapping, should-show rule, batching, the UD
registry (rate limits, pending pushes, logout) and DC numbering/hash pinning.

    tools/server/.venv/bin/python -I tools/server/platform/test_platform.py
(The DC tests need panda3d; they are skipped with another interpreter.)
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from ttplatform import platforms as P  # noqa: E402
from ttplatform.registry import PlatformRegistry  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))


def etc_dir():
    for cand in (os.environ.get('OTT_DIR', ''), os.path.join(ROOT, 'ref', 'open-toontown'),
                 os.path.join(ROOT, 'ref', 'ref', 'open-toontown'),
                 '/workspace/toontown/claude/Toontown-3ds/ref/open-toontown'):
        if cand and os.path.exists(os.path.join(cand, 'etc', 'otp.dc')):
            return os.path.join(cand, 'etc')
    return None


class EnumTest(unittest.TestCase):
    def test_values_are_the_wire_contract(self):
        self.assertEqual([P.UNKNOWN, P.WEB, P.MAC, P.IOS, P.ANDROID, P.WINDOWS, P.VR, P.N3DS, P.LINUX],
                         list(range(9)))
        self.assertEqual(P.NAMES[P.N3DS], 'N3DS')
        self.assertEqual(P.PLATFORM_MANAGER_DO_ID, 4721)
        self.assertEqual(P.PINNED_DC_HASH, 547155168)

    def test_matches_atlas_json(self):
        import json
        for path in (os.path.join(ROOT, 'tools', 'platform-badges', 'atlas.json'),):
            with open(path) as f:
                meta = json.load(f)
            self.assertEqual([(p['id'], p['name']) for p in meta['platforms']], list(enumerate(P.NAMES)))
            for size, atlas in meta['atlases'].items():
                for name, r in atlas['rects'].items():
                    self.assertEqual(r['px'][0], P.NAMES.index(name) * int(size))

    def test_clamp_and_names(self):
        for bad in (-1, 9, 255, 'x', None):
            self.assertEqual(P.clamp(bad), P.UNKNOWN)
        self.assertEqual(P.from_name('mac'), P.MAC)
        self.assertEqual(P.from_name('ipados'), P.IOS)
        self.assertEqual(P.from_name('3ds'), P.N3DS)
        self.assertEqual(P.from_name('7'), P.N3DS)
        self.assertEqual(P.from_name('nope'), P.UNKNOWN)

    def test_detect_override(self):
        self.assertEqual(P.detect_native_platform('vr'), P.VR)
        self.assertIn(P.detect_native_platform(), (P.LINUX, P.MAC, P.WINDOWS, P.ANDROID))


class ShouldShowTest(unittest.TestCase):
    def test_exact_compare(self):
        self.assertTrue(P.should_show(P.IOS, P.MAC))     # iPhone viewer sees the Mac badge
        self.assertTrue(P.should_show(P.MAC, P.IOS))     # and vice versa
        self.assertFalse(P.should_show(P.MAC, P.MAC))
        self.assertTrue(P.should_show(P.WEB, P.VR))

    def test_unknown_hidden(self):
        self.assertFalse(P.should_show(P.MAC, P.UNKNOWN))
        self.assertFalse(P.should_show(P.MAC, 200))
        self.assertTrue(P.should_show(P.UNKNOWN, P.MAC))  # we don't know ourselves: still badge others

    def test_self_hidden(self):
        self.assertFalse(P.should_show(P.MAC, P.N3DS, is_local=True))


class BatcherTest(unittest.TestCase):
    def test_dedupe_cap_and_self(self):
        b = P.RequestBatcher()
        for i in range(1, 121):
            b.add(i, own_av_id=7)
        b.add(5)
        self.assertEqual(len(b.take()), 50)
        self.assertEqual(len(b.take()), 50)
        rest = b.take()
        self.assertEqual(len(rest), 19)  # 120 ids minus our own
        self.assertNotIn(7, rest)
        self.assertEqual(b.take(), [])
        self.assertFalse(b.add(3))      # already sent: the server pushes changes itself
        b.forget(3)
        self.assertTrue(b.add(3))
        b.forget(3)
        self.assertEqual(b.take(), [])


class Clock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


def ch(acct, av):
    return acct << 32 | av


class RegistryTest(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.r = PlatformRegistry(self.clock)

    def test_round_trip(self):
        a, b = ch(1, 100), ch(2, 200)
        self.assertEqual(self.r.set_platform(a, P.N3DS), [])
        self.assertEqual(self.r.set_platform(b, P.MAC), [])
        self.assertEqual(self.r.request(a, [200]), [(a, [200], [P.MAC])])
        self.assertEqual(self.r.request(b, [100, 100, 200, 0]), [(b, [100], [P.N3DS])])

    def test_pending_push_and_change(self):
        a, b = ch(1, 100), ch(2, 200)
        self.assertEqual(self.r.request(a, [200]), [])          # not registered yet: nothing
        self.assertEqual(self.r.set_platform(b, P.WEB), [(a, [200], [P.WEB])])  # pushed once it registers
        self.clock.t += 10
        self.assertEqual(self.r.set_platform(b, P.WEB), [])     # same value: silent
        self.clock.t += 10
        self.assertEqual(self.r.set_platform(b, P.VR), [(a, [200], [P.VR])])    # entered VR

    def test_clamp_and_no_avatar(self):
        self.assertIsNone(self.r.set_platform(ch(1, 0), P.MAC))
        self.assertIsNone(self.r.request(ch(1, 0), [5]))
        self.r.set_platform(ch(1, 100), 99)
        self.assertNotIn(100, self.r.platforms)

    def test_rate_limit(self):
        a = ch(1, 100)
        ok = [self.r.set_platform(a, P.WEB + (i % 2)) is not None for i in range(10)]
        self.assertEqual(ok.count(True), PlatformRegistry.SET_BURST)
        self.clock.t += 5.0
        self.assertIsNotNone(self.r.set_platform(a, P.MAC))
        ok = [self.r.request(a, [1]) is not None for i in range(40)]
        self.assertEqual(ok.count(True), PlatformRegistry.REQ_BURST)
        self.assertGreater(self.r.dropped, 0)

    def test_request_capped_at_50(self):
        a = ch(1, 100)
        for i in range(1, 80):
            self.r.platforms[1000 + i] = P.WEB
        (reply,) = self.r.request(a, [1000 + i for i in range(1, 80)])
        self.assertEqual(len(reply[1]), 50)

    def test_clear_on_logout(self):
        a, b = ch(1, 100), ch(2, 200)
        self.r.set_platform(b, P.ANDROID)
        self.r.request(a, [200])
        self.assertEqual(self.r.clear(200), [(a, [200], [P.UNKNOWN])])
        self.assertNotIn(200, self.r.platforms)
        self.assertEqual(self.r.clear(200), [])

    def test_bounded(self):
        self.r.MAX_ENTRIES = 10
        for i in range(1, 30):
            self.r.set_platform(ch(i, i), P.WEB)
        self.assertEqual(len(self.r.platforms), 10)


@unittest.skipUnless(etc_dir(), 'open-toontown etc/ not found')
class DCNumberingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            from panda3d.direct import DCFile
            from panda3d.core import Filename
        except ImportError:
            raise unittest.SkipTest('panda3d not available')
        etc = etc_dir()

        def load(names):
            f = DCFile()
            for n in names:
                assert f.read(Filename.fromOsSpecific(n)), n
            return f
        cls.old = load([os.path.join(etc, 'otp.dc'), os.path.join(etc, 'toon.dc')])
        cls.count = staticmethod(lambda f: next(n for n in range(1 << 16) if f.getFieldByIndex(n) is None))
        cls.new = load([os.path.join(etc, 'otp.dc'), os.path.join(etc, 'toon.dc'), os.path.join(HERE, 'platform.dc')])

    def test_stock_hash_pinned(self):
        self.assertEqual(self.old.getHash(), 547155168)
        self.assertNotEqual(self.new.getHash(), 547155168)  # why manual_dc_hash is required

    def test_existing_ids_unchanged(self):
        old, new = self.old, self.new
        self.assertEqual(new.getNumClasses(), old.getNumClasses() + 1)
        for i in range(old.getNumClasses()):
            a, b = old.getClass(i), new.getClass(i)
            self.assertEqual((a.getName(), a.getNumber()), (b.getName(), b.getNumber()))
            self.assertEqual(a.getNumFields(), b.getNumFields())
            for j in range(a.getNumFields()):
                fa, fb = a.getField(j), b.getField(j)
                self.assertEqual((fa.getName(), fa.getNumber()), (fb.getName(), fb.getNumber()))
        self.assertEqual(self.count(new), self.count(old) + 4)

    def test_new_class_numbers(self):
        c = self.new.getClassByName('TTPlatformManager')
        self.assertEqual(c.getNumber(), self.old.getNumClasses())
        names = [c.getField(j).getName() for j in range(c.getNumFields())]
        self.assertEqual(names, ['setMyPlatform', 'requestPlatforms', 'setPlatforms', 'clearPlatform'])
        nums = [c.getField(j).getNumber() for j in range(c.getNumFields())]
        self.assertEqual(nums, list(range(self.count(self.old), self.count(self.old) + 4)))
        self.assertTrue(c.getFieldByName('setMyPlatform').isClsend())
        self.assertTrue(c.getFieldByName('requestPlatforms').isClsend())
        self.assertFalse(c.getFieldByName('setPlatforms').isClsend())
        self.assertFalse(c.getFieldByName('clearPlatform').isClsend())
        print('\n  TTPlatformManager class %d; fields setMyPlatform=%d requestPlatforms=%d '
              'setPlatforms=%d clearPlatform=%d' % ((c.getNumber(),) + tuple(nums)))


if __name__ == '__main__':
    unittest.main(verbosity=2)
