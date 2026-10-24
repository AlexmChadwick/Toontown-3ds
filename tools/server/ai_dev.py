"""Local test server only (AI_DEV=1 tools/server/start.sh): relax the AI's entry checks so a
fresh test toon can reach Cog buildings, facilities and boss elevators, and give it full laff
and a full gag bag (AI_DEV_STRONG=1: 137 laff and 6000 exp, for long fights such as the factory
foreman; AI_DEV_WEAK_COGS=1: every cog spawns with 1 HP, to reach the end of a facility).
Never used unless AI_DEV is set; ref/ (upstream) stays untouched: this
patches classes in memory, then runs the stock toontown.ai.AIStart."""
import os
import runpy
import sys


import importlib.abc


def _patch(name, mod):
    if name == 'toontown.toonbase.ToontownAccessAI':
        mod.canAccess = lambda *a, **k: True
    elif name == 'toontown.building.DistributedElevatorAI':
        mod.DistributedElevatorAI.checkBoard = lambda self, av: 0
        mod.DistributedElevatorAI.avIsOKToBoard = lambda self, av: self.accepting
    elif name == 'toontown.building.DistributedBossElevatorAI':
        mod.DistributedBossElevatorAI.checkBoard = lambda self, av: 0
    elif name == 'toontown.toon.DistributedToonAI':
        orig = mod.DistributedToonAI.announceGenerate

        def announceGenerate(self):
            orig(self)
            if type(self).__name__ != 'DistributedToonAI':
                return  # NPC toons share the class tree
            try:
                strong = os.environ.get('AI_DEV_STRONG') == '1'  # long fights (factory foreman)
                if self.getMaxHp() < (137 if strong else 60):
                    self.b_setMaxHp(137 if strong else 60)
                self.b_setHp(self.getMaxHp())
                if self.getMoney() < 40:
                    self.b_setMoney(40)
                self.b_setEmoteAccess([1] * 25)  # every SpeedChat emote (normally catalog unlocks)
                # Gags up to level 4 (800 exp) and a full pouch, so unattended battles can be won.
                exp = 6000 if strong else 800
                for track in range(len(self.experience.experience)):
                    if self.hasTrackAccess(track) and self.experience.getExp(track) < exp:
                        self.experience.setExp(track, exp)
                self.b_setExperience(self.experience.makeNetString())
                self.inventory.maxOutInv(filterUberGags=1)
                self.b_setInventory(self.inventory.makeNetString())
            except Exception as e:  # never break the district for a test convenience
                print('ai_dev: could not top up toon %s: %s' % (self.doId, e))
        mod.DistributedToonAI.announceGenerate = announceGenerate
    elif name == 'toontown.suit.DistributedSuitBaseAI':
        if os.environ.get('AI_DEV_WEAK_COGS') != '1':
            return
        orig_level = mod.DistributedSuitBaseAI.setLevel

        def setLevel(self, lvl=None):  # every cog spawns with 1 HP (test the end of a facility)
            orig_level(self, lvl)
            self.currHP = 1
        mod.DistributedSuitBaseAI.setLevel = setLevel
    else:
        return
    print('ai_dev: patched %s' % name, flush=True)


_TARGETS = {'toontown.toonbase.ToontownAccessAI', 'toontown.building.DistributedElevatorAI',
            'toontown.building.DistributedBossElevatorAI', 'toontown.toon.DistributedToonAI',
            'toontown.suit.DistributedSuitBaseAI'}


class _Hook(importlib.abc.MetaPathFinder):
    """Patches the target modules right after they finish importing (AIStart sets up the
    builtins they need first, so they cannot be imported up front)."""
    def find_spec(self, name, path, target=None):
        if name not in _TARGETS:
            return None
        sys.meta_path.remove(self)
        try:
            spec = importlib.util.find_spec(name)
        finally:
            sys.meta_path.insert(0, self)
        if spec is None or spec.loader is None:
            return None
        real = spec.loader.exec_module

        def exec_module(module, real=real, name=name):
            real(module)
            _patch(name, module)
        spec.loader.exec_module = exec_module
        return spec


def apply():
    import importlib.util  # noqa: F401  (used by _Hook)
    sys.meta_path.insert(0, _Hook())
    print('ai_dev: entry checks will be relaxed, toons topped up (local test server)', flush=True)


if __name__ == '__main__':
    apply()
    sys.argv[0] = 'AIStart'
    runpy.run_module('toontown.ai.AIStart', run_name='__main__', alter_sys=True)
