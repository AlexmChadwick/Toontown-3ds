#!/usr/bin/env python3
"""Headless end-to-end test of TTPlatformManager against a real Astron + UD + AI started
with PLATFORM=1 (tools/server/start.sh), speaking the raw client protocol.

    SERVER_PORT=7298 SERVER_RUN=/tmp/pb-srv PLATFORM=1 tools/server/start.sh
    tools/server/.venv/bin/python -I tools/server/platform/test_e2e.py --port 7298

Checks:
  1. new client A (N3DS) and B (MAC) log in with HELLO hash 547155168, play, enter 2000
  2. A asks for B before B registered -> pushed B=MAC the moment B registers
  3. B asks for A -> A=N3DS; B switches to VR -> A is pushed B=VR
  4. an OLD client (stock smoke_test.py: 2-file DC, same hash) logs in and plays alongside;
     A asking for it gets nothing (unknown = no badge)
  5. requestPlatforms before an avatar is chosen is dropped silently (no eject)
  6. a client sending the UD-only setPlatforms is ejected by the client agent (anti-cheat)
  7. B disconnects -> A is pushed B=UNKNOWN (post-remove cleanup)
Exit code 0 = all passed.
"""
import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.dirname(HERE)
sys.path.insert(0, SERVER)
sys.path.insert(0, HERE)
import smoke_test as st  # noqa: E402
from ttplatform import platforms as P  # noqa: E402
from panda3d.core import Filename  # noqa: E402

MGR = P.PLATFORM_MANAGER_DO_ID


class NewDC(st.DC):
    """otp.dc + toon.dc + platform.dc (the class/field tables a new client generates)."""

    def __init__(self):
        st.DC.__init__(self)
        assert self.hash == P.PINNED_DC_HASH
        assert self.f.read(Filename.fromOsSpecific(os.path.join(HERE, 'platform.dc')))
        self.by_class = {self.f.getClass(i).getNumber(): self.f.getClass(i) for i in range(self.f.getNumClasses())}


def hello(c):
    v = st.VERSION.encode()
    c.send(struct.pack('<HIH', st.CLIENT_HELLO, P.PINNED_DC_HASH, len(v)) + v)  # pinned, never the 3-file hash
    mtype, _ = c.recv()
    assert mtype == st.CLIENT_HELLO_RESP, mtype
    threading.Thread(target=c.heartbeat_loop, daemon=True).start()


def login(c, token):
    c.set_field(st.LOGIN_MANAGER_ID, 'AstronLoginManager', 'requestLogin', token)
    ev = c.pump_until(lambda e: e[0] == 'set_field' and e[2] == 'loginResponse', 20, 'loginResponse')
    assert json.loads(ev[3][0]).get('returnCode') == 0, ev


def enter_world(c, zone):
    ctx = c.add_interest(st.GAME_ROOT_ID, st.DISTRICT_ZONE, 1)
    c.pump_until(lambda e: e[0] == 'done_interest' and e[1] == ctx, 20, 'district interest')
    shard = next(d for d, o in c.objects.items() if o['cls'] == 'ToontownDistrict')
    c.set_field(st.LOGIN_MANAGER_ID, 'AstronLoginManager', 'requestAvatarList')
    ev = c.pump_until(lambda e: e[0] == 'set_field' and e[2] == 'avatarListResponse', 20, 'avatar list')
    if ev[3][0]:
        av = ev[3][0][0][0]
    else:
        c.set_field(st.LOGIN_MANAGER_ID, 'AstronLoginManager', 'createAvatar', st.TEST_DNA, 0)
        av = c.pump_until(lambda e: e[0] == 'set_field' and e[2] == 'createAvatarResponse', 20, 'create')[3][0]
    c.set_field(st.LOGIN_MANAGER_ID, 'AstronLoginManager', 'requestPlayAvatar', av)
    c.pump_until(lambda e: e[0] == 'enter' and e[1] == av and e[5], 20, 'owner view')
    c.send(struct.pack('<HIII', st.CLIENT_OBJECT_LOCATION, av, shard, zone))
    ctx = c.add_interest(shard, st.UBER_ZONE, 2)
    c.pump_until(lambda e: e[0] == 'done_interest' and e[1] == ctx, 30, 'uber zone')
    ctx = c.add_interest(shard, zone, 3)
    c.pump_until(lambda e: e[0] == 'done_interest' and e[1] == ctx, 30, 'zone %d' % zone)
    return av


def sees_toon(c, av, timeout=20):
    if av in c.objects:
        return
    c.pump_until(lambda e: e[0] == 'enter' and e[1] == av, timeout, 'toon %d to appear' % av)


def platforms_event(c, av, timeout=10):
    ev = c.pump_until(lambda e: e[0] == 'set_field' and e[1] == MGR and e[2] == 'setPlatforms' and av in e[3][0],
                      timeout, 'setPlatforms for %d' % av)
    ids, plats = ev[3]
    return dict(zip(ids, plats))[av]


def no_platforms_event(c, seconds):
    end = time.time() + seconds
    while time.time() < end:
        try:
            ev = c.handle(*c.recv(max(0.05, end - time.time())))
        except socket.timeout:
            return
        if ev[0] == 'set_field' and ev[1] == MGR:
            raise AssertionError('unexpected %r' % (ev,))


def drain(c, seconds):
    """Read (and drop) traffic, e.g. while another client logs in."""
    end = time.time() + seconds
    while time.time() < end:
        try:
            c.handle(*c.recv(max(0.05, end - time.time())))
        except socket.timeout:
            return


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=7298)
    ap.add_argument('--zone', type=int, default=2000)
    a = ap.parse_args()
    tag = time.strftime('%H%M%S')
    dc = NewDC()
    cls = dc.cls('TTPlatformManager')
    print('platform.dc: TTPlatformManager class %d, fields %s' % (
        cls.getNumber(), {cls.getField(i).getName(): cls.getField(i).getNumber() for i in range(cls.getNumFields())}))
    ok = []

    def check(name, cond):
        print(('PASS ' if cond else 'FAIL ') + name, flush=True)
        ok.append(cond)
        if not cond:
            raise SystemExit(1)

    # 5. before an avatar is chosen: dropped, no eject
    e = st.Conn(a.host, a.port, dc, False)
    hello(e)
    login(e, 'pb-early-' + tag)
    e.set_field(MGR, 'TTPlatformManager', 'requestPlatforms', [1, 2, 3])
    e.set_field(MGR, 'TTPlatformManager', 'setMyPlatform', P.WEB)
    no_platforms_event(e, 1.5)
    e.send(struct.pack('<H', st.CLIENT_HEARTBEAT))
    no_platforms_event(e, 0.5)
    check('pre-avatar requests dropped, connection alive', e.alive)
    e.alive = False
    e.sock.close()

    # 1. two new clients
    A = st.Conn(a.host, a.port, dc, False)
    hello(A)
    login(A, 'pb-a-' + tag)
    avA = enter_world(A, a.zone)
    A.set_field(MGR, 'TTPlatformManager', 'setMyPlatform', P.N3DS)
    B = st.Conn(a.host, a.port, dc, False)
    hello(B)
    login(B, 'pb-b-' + tag)
    avB = enter_world(B, a.zone)
    sees_toon(A, avB)
    sees_toon(B, avA)
    check('A (%d) and B (%d) in zone %d see each other' % (avA, avB, a.zone), True)

    # 2. A asks before B registered; push on registration
    A.set_field(MGR, 'TTPlatformManager', 'requestPlatforms', [avB])
    no_platforms_event(A, 1.0)
    B.set_field(MGR, 'TTPlatformManager', 'setMyPlatform', P.MAC)
    check('A pushed B=MAC on registration', platforms_event(A, avB) == P.MAC)

    # 3. B asks for A; B enters VR
    B.set_field(MGR, 'TTPlatformManager', 'requestPlatforms', [avA, avB])
    check('B gets A=N3DS', platforms_event(B, avA) == P.N3DS)
    B.set_field(MGR, 'TTPlatformManager', 'setMyPlatform', P.VR)
    check('A pushed B=VR after B enters VR', platforms_event(A, avB) == P.VR)
    check('badge rule: A shows B (N3DS vs VR)', P.should_show(P.N3DS, P.VR))

    # 4. old client, stock smoke test (2-file DC), lingering in the same zone
    py = sys.executable
    old = subprocess.Popen([py, '-I', os.path.join(SERVER, 'smoke_test.py'), '--port', str(a.port),
                            '--token', 'pb-old-' + tag, '--zone', str(a.zone), '--linger', '6', '--move'],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    t0 = time.time()
    while time.time() - t0 < 40:
        drain(A, 0.5)
        drain(B, 0.1)
        toons = [d for d, o in A.objects.items() if o['cls'] == 'DistributedToon' and d not in (avA, avB)]
        if toons:
            break
    check('A sees the old client\'s toon', bool(toons))
    avOld = toons[0]
    A.set_field(MGR, 'TTPlatformManager', 'requestPlatforms', [avOld])
    no_platforms_event(A, 1.0)
    check('old client has no platform (no badge)', True)
    out, _ = old.communicate(timeout=60)
    check('old client (stock DC, hash 547155168) logged in and played: %s' % out.strip().splitlines()[-1],
          old.returncode == 0 and 'SMOKE TEST OK' in out)
    check('old client received nothing from 4721', 'doId=%d' % MGR not in out)

    # 6. anti-cheat: setPlatforms is not clsend
    X = st.Conn(a.host, a.port, dc, False)
    hello(X)
    login(X, 'pb-x-' + tag)
    X.set_field(MGR, 'TTPlatformManager', 'setPlatforms', [avA], [P.WEB])
    ejected = False
    try:
        X.pump_until(lambda e: False, 5, 'eject')
    except SystemExit as ex:
        ejected = 'EJECTED' in str(ex)
        print('  X: %s' % ex)
    except ConnectionError:
        ejected = True
    check('client sending UD-only setPlatforms is ejected', ejected)

    # 7. B leaves -> A told UNKNOWN
    B.alive = False
    B.send(struct.pack('<H', st.CLIENT_DISCONNECT))
    B.sock.close()
    check('A pushed B=UNKNOWN after B disconnects', platforms_event(A, avB, 15) == P.UNKNOWN)

    A.alive = False
    A.send(struct.pack('<H', st.CLIENT_DISCONNECT))
    A.sock.close()
    print('PLATFORM E2E OK (%d checks)' % len(ok))


if __name__ == '__main__':
    main()
