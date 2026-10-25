"""Local test server only (AI_DEV=1 tools/server/start.sh): relax the AI's entry checks so a
fresh test toon can reach Cog buildings, facilities and boss elevators, and give it full laff
and a full gag bag (AI_DEV_STRONG=1: 137 laff, every gag track and 6000 exp, for long fights such
as the factory foreman; AI_DEV_WEAK_COGS=1: every cog spawns, and revives, with 1 HP, to reach the end of a facility).
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
    elif name in _OWN_CHECK_BOARD:  # boss / club elevators with their own promotion / room checks
        cls = getattr(mod, name.rsplit('.', 1)[1])
        cls.checkBoard = lambda self, av: 0
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
                if strong:  # every gag track (sound hits a whole cog group) and the biggest pouch
                    self.b_setTrackAccess([1] * 7)
                    if self.getMaxCarry() < 80:  # ToontownGlobals MaxCarryLimit
                        self.b_setMaxCarry(80)
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
        if os.environ.get('AI_DEV_STRONG') == '1':
            orig_zone = mod.DistributedToonAI.handleLogicalZoneChange

            def handleLogicalZoneChange(self, newZoneId, oldZoneId):
                orig_zone(self, newZoneId, oldZoneId)
                # A long unattended script reaches each facility from a playground or Cog HQ
                # courtyard: start it healed and stocked, as a player would after a toon-up and
                # a gag shop, so laff lost in one facility cannot end the next one early.
                if type(self).__name__ != 'DistributedToonAI' or newZoneId is None:
                    return
                if newZoneId % 1000 != 0 and not 10000 <= newZoneId < 14000:
                    return
                try:
                    if self.getHp() < self.getMaxHp():
                        self.b_setHp(self.getMaxHp())
                    self.inventory.maxOutInv(filterUberGags=1)
                    self.b_setInventory(self.inventory.makeNetString())
                except Exception as e:  # never break the district for a test convenience
                    print('ai_dev: could not heal toon %s: %s' % (self.doId, e))
            mod.DistributedToonAI.handleLogicalZoneChange = handleLogicalZoneChange
    elif name in _BOSS_GENERATORS:
        if os.environ.get('AI_DEV_WEAK_COGS') != '1':
            return
        cls = getattr(mod, name.rsplit('.', 1)[1])
        original = cls.generateSuits

        def generateSuits(self, battleNumber):
            result = original(self, battleNumber)
            # Boss rounds generate both active and deferred reserve Cogs. Clamp
            # at this boundary, after planner setup, and include maxHP: battle
            # reserve thresholds and revives also consult it, not just currHP.
            suits = list(result['activeSuits'])
            suits.extend(entry[0] for entry in result['reserveSuits'])
            for suit in suits:
                suit.maxHP = 1
                suit.currHP = 1
            print('ai_dev: weak boss Cogs %s round %s count %s' %
                  (type(self).__name__, battleNumber, len(suits)), flush=True)
            return result
        cls.generateSuits = generateSuits
    # ---- cogdo local test switches; production AI remains unchanged ----
    elif name == 'toontown.building.DistributedBuildingAI':
        if os.environ.get('AI_DEV_COGDO') != '1':
            return
        original = mod.DistributedBuildingAI.enterToon

        def enterToon(self):
            original(self)
            if not (2100 <= self.canonicalZoneId < 2200) or getattr(self.air, '_cogdoTestBuilding', None):
                return
            self.air._cogdoTestBuilding = self.doId

            def takeOver(task):
                if not self.isDeleted() and self.isToonBlock():
                    self.cogdoTakeOver('s', 0, 1)
                    print('ai_dev: cogdo takeover zone %s block %s' % (self.zoneId, self.block), flush=True)
                return task.done
            import builtins
            manager = getattr(mod, 'taskMgr', getattr(builtins, 'taskMgr', None))
            if manager is not None:
                manager.doMethodLater(5, takeOver, self.uniqueName('cogdo-dev-takeover'))
        mod.DistributedBuildingAI.enterToon = enterToon
    elif name == 'toontown.cogdominium.DistributedCogdoInteriorAI':
        game = os.environ.get('AI_DEV_COGDO_GAME', '')
        if game not in ('maze', 'crane', 'boardroom'):
            return
        import builtins
        repository = getattr(mod, 'simbase', getattr(builtins, 'simbase', None))
        if repository is not None:
            repository.forcedCogdoGame = game
            print('ai_dev: forcing cogdo game %s' % game, flush=True)
    # ---- end cogdo local switches ----
    elif name == 'toontown.suit.DistributedSuitBaseAI':
        if os.environ.get('AI_DEV_WEAK_COGS') != '1':
            return
        orig_level = mod.DistributedSuitBaseAI.setLevel

        def setLevel(self, lvl=None):  # every cog spawns with 1 HP (test the end of a facility)
            orig_level(self, lvl)
            self.maxHP = 1
            self.currHP = 1
            # Level Cogs get their level after generate: tell clients, or their movies
            # never show the deaths the AI computes.
            if self.isGenerated():
                self.d_setHP(1)
        mod.DistributedSuitBaseAI.setLevel = setLevel
        orig_revive = mod.DistributedSuitBaseAI.useSkeleRevive

        def useSkeleRevive(self):  # ... and revives with 1 HP (mint / DA office skelecogs)
            orig_revive(self)
            self.currHP = 1
            if self.isGenerated():
                self.d_setHP(1)
        mod.DistributedSuitBaseAI.useSkeleRevive = useSkeleRevive
    else:
        return
    print('ai_dev: patched %s' % name, flush=True)


# Subclasses that override checkBoard (REJECT_PROMOTION without a Cog suit ready for promotion).
_OWN_CHECK_BOARD = {'toontown.building.DistributedVPElevatorAI', 'toontown.building.DistributedBBElevatorAI',
                    'toontown.building.DistributedClubElevatorAI'}
_BOSS_GENERATORS = {'toontown.suit.Distributed%sBossAI' % name
                    for name in ('Sellbot', 'Cashbot', 'Lawbot', 'Bossbot')}
_TARGETS = {'toontown.toonbase.ToontownAccessAI', 'toontown.building.DistributedElevatorAI',
            'toontown.building.DistributedBossElevatorAI', 'toontown.toon.DistributedToonAI',
            'toontown.suit.DistributedSuitBaseAI', 'toontown.building.DistributedBuildingAI',
            'toontown.cogdominium.DistributedCogdoInteriorAI'} | _OWN_CHECK_BOARD | _BOSS_GENERATORS


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
