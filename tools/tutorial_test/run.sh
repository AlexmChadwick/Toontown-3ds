#!/bin/sh
# Host checks for the Toontorial's generated QuestScripts, packet contracts and dynamic-zone map.
# No Panda3D emulator or server is started here.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$root"

python_bin=.venv/bin/python
if [ ! -x "$python_bin" ]; then
    echo "tutorial_test: UNAVAILABLE (.venv/bin/python is missing)"
    exit 0
fi

"$python_bin" -I tools/gen_quest_scripts.py --check

"$python_bin" -I - <<'PY'
from pathlib import Path
import re

source = Path('source/net/client_tutorial.cpp').read_text()
client = Path('source/net/client.cpp').read_text()
header = Path('source/net/dc_generated.h').read_text()

# Every field emitted by client_tutorial.cpp has both a compile-time TT_SIG and a generated
# clsend keyword.  This catches a DC regeneration that changes a packet before hardware testing.
sent = [p for p in re.findall(r'TT_NET_UPDATE\(w, [^,]+, (\w+), (\w+)\)', source) if p[0] != 'cls']
assert 'TT_NET_UPDATE(w, suitDoId, DistributedTutorialSuit, requestBattle)' in client
sent.append(('DistributedTutorialSuit', 'requestBattle'))
assert sent, 'no tutorial sender found'
for cls, field in sent:
    assert f'TT_SIG({cls}, {field},' in source, f'missing signature guard: {cls}.{field}'
    keyword = re.search(rf'constexpr uint16_t kw_{field} = (0x[0-9A-Fa-f]+|\d+);', header[header.index(f'namespace {cls} {{'):])
    assert keyword, f'missing generated keyword: {cls}.{field}'
    assert int(keyword.group(1), 0) & 0x10, f'not clsend: {cls}.{field}'

for marker in ('TutorialMickey', 'QuestAssign101', 'QuestAssign110', 'TutorialBlocker', 'GagIntro'):
    assert marker in Path('source/quest/quest_scripts.h').read_text(), marker

# Returning from either tutorial interior enters the dynamic street.  Unlike an ordinary
# 21xx street cell, its allocated id is >= DynamicZonesBegin, so client.cpp must identify it
# explicitly, include the allocated branch (where DistributedBuildings live), and translate
# any canonical PVS ids before opening the interest.
assert 'targetZone_ == tutorialZones_.street' in client
assert 'tutorialStreet ? tutorialZones_.branch' in client
assert 'visZones_[i] = tutorialZones_.toNetwork(visZones[i])' in client
print('tutorial_test: generated scripts and %d sender contracts: PASS' % len(sent))
PY

tmp=$(mktemp -d "${TMPDIR:-/tmp}/toontown-tutorial.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Isource \
    source/quest/quest_scripts.cpp source/quest/script_player.cpp source/world/tutorial.cpp \
    tools/tutorial_test/flow.cpp -o "$tmp/flow"
"$tmp/flow"
cat >"$tmp/zones.cpp" <<'CPP'
#include <cassert>
#include "world/tutorial_zones.h"

int main() {
    using tt::world::TutorialZones;
    TutorialZones z{61000, 61001, 61002, 61003};
    assert(TutorialZones::valid(z.branch, z.street, z.shop, z.hq));
    assert(z.toCanonical(61001) == tt::world::kTutorialStreet);
    assert(z.toCanonical(61002) == tt::world::kTutorialShop);
    assert(z.toCanonical(61003) == tt::world::kTutorialHq);
    assert(z.toNetwork(tt::world::kTutorialStreet) == 61001);
    assert(z.toNetwork(tt::world::kTutorialShop) == 61002);
    assert(z.toNetwork(tt::world::kTutorialHq) == 61003);
    assert(!TutorialZones::valid(60999, 61001, 61002, 61003));
    assert(!TutorialZones::valid(61000, 61000, 61002, 61003));
}
CPP
${CXX:-c++} -std=c++17 -Isource "$tmp/zones.cpp" -o "$tmp/zones"
"$tmp/zones"
echo "tutorial_test: dynamic-zone map: PASS"
