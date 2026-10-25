#!/usr/bin/env python3
"""Exercise the current factory protocol and real TLV reader without a GPU bake.

The former Factory/FCT1 runtime was replaced by Level/LevelSpec. Generate two
small real upstream specs and run the existing reader/placement host test.
"""
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]


def main():
    header = (ROOT / 'source/net/dc_generated.h').read_text()
    client = (ROOT / 'source/net/client.h').read_text()
    level = (ROOT / 'source/world/level.h').read_text()
    assert 'constexpr uint32_t kHash = 547155168u;' in header
    for name in ('DistributedFactory', 'DistributedLift', 'DistributedCrate',
                 'DistributedGoon', 'DistributedStomper', 'DistributedBattleFactory'):
        assert f'namespace {name}' in header, name
    for token in ('LevelEntity', 'sendCratePush', 'sendLevelGoonBattle'):
        assert token in client, token
    assert 'class Level' in level
    out = ROOT / 'sdmc-out/factory-test'
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, OPENBLAS_NUM_THREADS='1', OMP_NUM_THREADS='1')
    subprocess.run([sys.executable, '-I', '-B', 'tools/pipeline/bake_level.py',
                    '--level', 'sellbot_factory,CashbotMintLobby_Battle00',
                    '--tlv-only', '--out', str(out)], cwd=ROOT, env=env, check=True)
    executable = out / 'level_spec_test'
    subprocess.run(['c++', '-std=c++17', '-fno-exceptions', '-fno-rtti',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-g', '-O1', '-Isource', 'tools/level_spec_test/main.cpp',
                    'source/world/level_spec.cpp', '-o', str(executable)],
                   cwd=ROOT, check=True)
    subprocess.run([str(executable), str(out)], cwd=ROOT, check=True)
    print('factory protocol and real LevelSpec host test: PASS')


if __name__ == '__main__':
    main()
