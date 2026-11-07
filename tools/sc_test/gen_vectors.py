#!/usr/bin/env python3
"""Reference SpeedChat ToonTask phrases from upstream Quests.getSCStrings -> vectors.txt.

    tools/server/.venv/bin/python tools/sc_test/gen_vectors.py <open-toontown checkout> tools/sc_test/vectors.txt

Runs the real toontown.quest.Quests (needs the Panda3D fork from tools/server/setup*.sh) with a
stub `base` (config defaults, hoodMgr names from ToontownGlobals). One line per phrase:
    questId<TAB>toNpcId<TAB>progress<TAB>msgIndex<TAB>text
plus "questId<TAB>toNpcId<TAB>progress<TAB>-<TAB>" for an empty list. Only the quest classes the
3DS renders itself (tools/gen_quest_tables.py CLASS_TYPES) are listed.
"""
import builtins
import sys

sys.path.insert(0, sys.argv[1])
from toontown.toonbase import ToontownGlobals  # noqa: E402


class _Cfg:
    def GetBool(self, k, d=False): return d
    def GetInt(self, k, d=0): return d
    def GetString(self, k, d=''): return d
    def GetFloat(self, k, d=0.0): return d


class _HoodMgr:
    def getFullnameFromId(self, h): return ToontownGlobals.hoodNameMap[h][-1]


class _Cr:
    hoodMgr = _HoodMgr()


class _Base:
    cr = _Cr()
    config = _Cfg()
    def destroy(self): pass


builtins.base = _Base()
from toontown.quest import Quests  # noqa: E402
from toontown.toon import NPCToons  # noqa: E402

CLASSES = {'CogQuest', 'CogNewbieQuest', 'CogLevelQuest', 'CogTrackQuest', 'BuildingQuest',
           'BuildingNewbieQuest', 'VisitQuest', 'DeliverItemQuest', 'RecoverItemQuest', 'DeliverGagQuest',
           'TrackChoiceQuest', 'FriendQuest', 'TrolleyQuest', 'PhoneQuest', 'MailboxQuest'}


def pick_npcs():
    """HQ, the tailor, one playground NPC and one street NPC (fixed: the lowest ids)."""
    pg = st = None
    for npcId in sorted(NPCToons.NPCToonDict):
        zone = NPCToons.NPCToonDict[npcId][0]
        if not isinstance(zone, int) or zone <= 0 or zone % 1000 < 500 or npcId in (Quests.ToonHQ, Quests.ToonTailor):
            continue
        branch = zone - zone % 100 - 500
        if branch % 1000 == 0 and pg is None:
            pg = npcId
        elif branch % 1000 != 0 and st is None:
            st = npcId
    return [Quests.ToonHQ, Quests.ToonTailor, pg, st, 0]


def main():
    npcs = pick_npcs()
    out = open(sys.argv[2], 'w', newline='\n')
    for questId in sorted(Quests.QuestDict):
        q = Quests.getQuest(questId)
        if q is None or type(q).__name__ not in CLASSES or Quests.OBSOLETE in Quests.QuestDict[questId]:
            continue  # retired quests are not in the 3DS tables either (gen_quest_tables.py)
        n = q.getNumQuestItems() if hasattr(q, 'getNumQuestItems') else 1
        if n is None or n < 1:
            n = 1
        for npc in npcs:
            for progress in sorted({0, n}):
                # The destination NPC only matters once a task is done, or for tasks that are a
                # visit at heart; elsewhere one NPC (HQ) is enough.
                visitLike = type(q).__name__ in ('VisitQuest', 'DeliverItemQuest', 'DeliverGagQuest',
                                                 'TrackChoiceQuest')
                if npc != Quests.ToonHQ and progress == 0 and not visitLike:
                    continue
                # The finish / visit lists depend on the NPC alone (RecoverItem's also name the
                # item), so every NPC is checked on a fixed sample of the other quests.
                if npc != Quests.ToonHQ and type(q).__name__ != 'RecoverItemQuest' and questId % 7:
                    continue
                try:
                    msgs = q.getSCStrings(npc, progress)
                except Exception:
                    continue  # upstream itself cannot render this combination (e.g. npc 0 lookups)
                if type(msgs) != type([]):
                    msgs = [msgs]
                if not msgs:
                    out.write('%d\t%d\t%d\t-\t\n' % (questId, npc, progress))
                for i, m in enumerate(msgs):
                    out.write('%d\t%d\t%d\t%d\t%s\n' % (questId, npc, progress, i, m))


main()
