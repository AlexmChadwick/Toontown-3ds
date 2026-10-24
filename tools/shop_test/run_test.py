#!/usr/bin/env python3
"""Host test: source/ui/shop_rules.cpp (gag shop rules, inventory blob) against upstream.

    .venv/bin/python -I tools/shop_test/run_test.py [--count 20000] [--seed 1] [--upstream DIR]

The expectations come from the real upstream code, not a re-implementation: a fake toon is
plugged into toontown/toon/InventoryBase.py, whose addItem / makeNetString / validatePurchase
(the exact functions DistributedNPCClerkAI.setInventory runs) produce the reference results.
Compared per random case: the 49-byte inventory blob, validatePurchase accept/reject, the
addItem result code (PurchaseBase.handlePurchase), and full buy/sell sequences. Exit 1 on any
mismatch. The upstream tree is ref/open-toontown (or --upstream / $TT_UPSTREAM in a worktree
whose ref/ is empty).
"""
import argparse
import builtins
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))


def find_upstream(arg):
    for cand in (arg, os.environ.get('TT_UPSTREAM'), os.path.join(ROOT, 'ref', 'open-toontown'),
                 '/Users/alexchadwick/toontown-3ds/ref/open-toontown'):
        if cand and os.path.isfile(os.path.join(cand, 'toontown', 'toon', 'InventoryBase.py')):
            return cand
    sys.exit('upstream open-toontown not found (use --upstream)')


ap = argparse.ArgumentParser()
ap.add_argument('--count', type=int, default=20000)
ap.add_argument('--seed', type=int, default=1)
ap.add_argument('--upstream')
args = ap.parse_args()
UPSTREAM = find_upstream(args.upstream)
sys.path.insert(0, UPSTREAM)

# Builtins that upstream's ShowBase/PythonUtil installs at runtime.
builtins.choice = lambda cond, a, b: a if cond else b
builtins.StackTrace = lambda *a, **k: type('ST', (), {'compact': lambda self: ''})()


class _Cfg:
    def GetBool(self, *a, **k):
        return False


class _Air:
    def writeServerEvent(self, *a, **k):
        pass


builtins.simbase = type('SB', (), {'config': _Cfg(), 'air': _Air()})()

import types  # noqa: E402

from panda3d.core import loadPrcFileData  # noqa: E402

loadPrcFileData('', 'notify-level error')  # upstream warns on every rejected purchase

# panda3d.otp (libpandaotp) is not in the stock wheel; OTPGlobals only star-imports it.
sys.modules.setdefault('panda3d.otp', types.ModuleType('panda3d.otp'))

from otp.otpbase import OTPGlobals  # noqa: E402
from toontown.toon import Experience, InventoryBase  # noqa: E402
from toontown.toonbase import ToontownBattleGlobals as B  # noqa: E402

InventoryBase.InventoryBase.notify.setWarning(False)
NT, NL = len(B.Tracks), 7
assert NT == 7 and all(len(x) == NL for x in B.Levels)


class FakeToon:
    """The slice of DistributedToonAI that InventoryBase touches."""

    doId = 100000001
    DISLid = 1

    def __init__(self, exp, access, max_carry):
        self.experience = Experience.Experience()
        self.experience.experience = list(exp)
        self.trackArray = list(access)
        self.maxCarry = max_carry

    def hasTrackAccess(self, track):
        return self.trackArray[track]

    def getMaxCarry(self):
        return self.maxCarry

    def getGameAccess(self):
        return OTPGlobals.AccessFull


def blob_of(flat):
    return bytes(flat)


def flat_of(inv):
    return [inv[t][l] for t in range(NT) for l in range(NL)]


def make_inv(toon, flat):
    return InventoryBase.InventoryBase(toon, blob_of(flat))


def profile_str(exp, access, mc):
    return ' '.join(map(str, list(exp) + list(access) + [mc]))


def rand_profile(rng):
    exp = []
    for t in range(NT):
        mode = rng.random()
        if mode < 0.15:
            exp.append(0)
        elif mode < 0.85:
            lv = rng.randrange(NL)
            exp.append(min(B.Levels[t][lv] + rng.choice((0, 0, 1, 5)), B.MaxSkill))
        else:
            exp.append(rng.randrange(0, 10501))
    access = [1 if rng.random() < 0.6 else 0 for _ in range(NT)]
    return exp, access, rng.choice((20, 25, 30, 35, 40, 50, 80))


def pick_gag(rng, toon):
    """Mostly gags the toon could really own (unlocked level on an accessible track)."""
    if rng.random() < 0.75:
        opts = [(t, l) for t in range(NT) if toon.trackArray[t]
                for l in range(toon.experience.getExpLevel(t) + 1)]
        if opts:
            return rng.choice(opts)
    return rng.randrange(NT), rng.randrange(NL)


def rand_inv(rng, toon, fill):
    """Mostly legal inventories, with an occasional count above the carry limit or on a locked track."""
    flat = [0] * (NT * NL)
    inv = make_inv(toon, flat)
    for _ in range(rng.randrange(0, fill + 1)):
        t, l = pick_gag(rng, toon)
        inv.addItem(t, l)
    flat = flat_of(inv.inventory)
    if rng.random() < 0.08:
        flat[rng.randrange(len(flat))] = rng.randrange(0, 40)
    return flat


def expected_buy(toon, flat, money, t, l):
    if money <= 0:
        return 'nomoney'
    if l > B.LAST_REGULAR_GAG_LEVEL:
        # addItem would accept it, but validatePurchase rejects any increase of an uber gag.
        inv = make_inv(toon, flat)
        if inv.addItem(t, l) > 0:
            new = flat_of(inv.inventory)
            assert not make_inv(toon, flat).validatePurchase(
                make_inv(toon, new).inventory, money, money - 1), 'uber purchase accepted upstream?'
        return 'notforsale'
    inv = make_inv(toon, flat)
    ret = inv.addItem(t, l)
    return {-2: 'toomanyprops', -1: 'toomanygag', 0: 'noskill'}.get(ret, 'ok')


def expected_validate(toon, old, new, cur, nw):
    inv = make_inv(toon, old)
    ok = inv.validatePurchase(make_inv(toon, new).inventory, cur, nw)
    return 1 if ok else 0, bytes(make_inv(toon, new).makeNetString()).hex()


def expected_cart(toon, flat, money, ops):
    start_money = money
    """PurchaseBase.handlePurchase for buys; sells refund one bean and never go below the start."""
    start = list(flat)
    inv = make_inv(toon, flat)
    for op, t, l in ops:
        if op == 'b':
            if money <= 0 or l > B.LAST_REGULAR_GAG_LEVEL:
                continue
            if inv.addItem(t, l) > 0:
                money -= 1
        else:
            i = t * NL + l
            if inv.inventory[t][l] > start[i]:
                inv.inventory[t][l] -= 1
                inv.calcTotalProps()
                money += 1
    end = flat_of(inv.inventory)
    ok = make_inv(toon, start).validatePurchase(make_inv(toon, end).inventory, start_money, money)
    return money, bytes(end).hex(), 1 if ok else 0


def main():
    rng = random.Random(args.seed)
    lines, want = [], []
    for _ in range(args.count):
        exp, access, mc = rand_profile(rng)
        toon = FakeToon(exp, access, mc)
        prof = profile_str(exp, access, mc)
        old = rand_inv(rng, toon, 90)
        money = rng.choice((0, 1, 2, 5, 10, 20, 40, rng.randrange(0, 60)))
        kind = rng.randrange(3)
        if kind == 0:  # validatePurchase on a random edit of the inventory
            new = list(old)
            for _ in range(rng.randrange(0, 6)):
                i = rng.randrange(len(new))
                new[i] = max(0, min(255, new[i] + rng.choice((-2, -1, 1, 1, 2, 5))))
            nw = money - rng.choice((0, 0, 1, 2, 3, 5, -1)) if rng.random() < 0.9 else rng.randrange(0, 60)
            nw = max(-5, nw)
            lines.append('validate %s %s %s %d %d' % (prof, ' '.join(map(str, old)), ' '.join(map(str, new)), money, nw))
            ok, h = expected_validate(toon, old, new, money, nw)
            want.append('%d %s' % (ok, h))
        elif kind == 1:  # one purchase attempt
            t, l = pick_gag(rng, toon)
            lines.append('buy %s %s %d %d %d' % (prof, ' '.join(map(str, old)), money, t, l))
            want.append(expected_buy(toon, old, money, t, l))
        else:  # a whole visit
            ops = []
            for _ in range(rng.randrange(1, 25)):
                ops.append((rng.choice('bbbs'),) + pick_gag(rng, toon))
            lines.append('cart %s %s %d %d %s' % (prof, ' '.join(map(str, old)), money, len(ops),
                                                   ' '.join('%s %d %d' % o for o in ops)))
            m, h, ok = expected_cart(toon, old, money, ops)
            want.append('%d %s %d' % (m, h, ok))
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, 'shop_test')
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                        '-I', os.path.join(ROOT, 'source'), os.path.join(HERE, 'shop_test.cpp'),
                        os.path.join(ROOT, 'source', 'ui', 'shop_rules.cpp'),
                        os.path.join(ROOT, 'source', 'ui', 'gag_data.cpp'), '-o', exe], check=True)
        got = subprocess.run([exe], input='\n'.join(lines) + '\n', capture_output=True, text=True,
                             check=True).stdout.splitlines()
    bad = 0
    counts = {}
    for ln, g, w in zip(lines, got, want):
        cmd = ln.split()[0]
        key = cmd + ':' + w.split()[0] if cmd == 'buy' else cmd + ':' + (w.split()[0] if cmd == 'validate' else w.split()[-1])
        counts[key] = counts.get(key, 0) + 1
        if g != w:
            bad += 1
            if bad <= 10:
                print('MISMATCH\n  cmd : %s\n  cpp : %s\n  want: %s' % (ln, g, w))
    if len(got) != len(want):
        print('line count mismatch: %d vs %d' % (len(got), len(want)))
        bad += 1
    print('%d cases, %d mismatches; outcomes %s' % (len(lines), bad, sorted(counts.items())))
    sys.exit(1 if bad else 0)


main()
