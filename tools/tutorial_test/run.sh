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
# Every script has its upstream trigger wired in the game (docs/design/tutorial.md "Where each
# script starts"), and real player actions answer the WAIT_EVENTs (not only the autotest).
main = Path('source/main.cpp').read_text()
talk = Path('source/quest/quest_talk.cpp').read_text()
for needle in ('ScriptId::TutorialMickey', 'ScriptId::QuestAssign101', 'ScriptId::QuestAssign110',
               'ScriptId::TutorialBlocker', 'net.sendBlockerAvatarEnter(', 'talk.takeScript()', 'talk.scriptDone()'):
    assert needle in main, f'main.cpp does not wire {needle}'
assert 'questId == 101 || questId == 110' in talk and 'Phase::Script' in talk, 'assign movie does not play the QuestScript'
scripts = Path('source/quest/quest_scripts.cpp').read_text()
# gag_intro (inventory-selection) belongs to NewbiePurchaseManager (the first trolley gag purchase), not the Toontorial.
for event in sorted(set(re.findall(r'ScriptOp::WaitEvent[^"]*"([^"]+)"', scripts)) - {'inventory-selection'}):
    assert f'!strcmp(ev, "{event}")' in main, f'no player action answers WAIT_EVENT {event}'
assert 'k_setTutorialAck' in client and 'ownTutorialAck_ == 0' in source, 'tutorialAck 0 must force the Toontorial'
print('tutorial_test: generated scripts and %d sender contracts: PASS' % len(sent))
PY

tmp=$(mktemp -d "${TMPDIR:-/tmp}/toontown-tutorial.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
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
