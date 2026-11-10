"""Where the Toon is and what they are doing, for badge matching and for filling a new group's place."""
from . import groups as G

# Cog HQ hood ids (Bossbot, Sellbot, Cashbot, Lawbot) in department order of groups.DEPARTMENTS
# (Sellbot, Cashbot, Lawbot, Bossbot).
HQ_HOODS = {G.SELLBOT: 11000, G.CASHBOT: 12000, G.LAWBOT: 13000, G.BOSSBOT: 10000}
HOOD_DEPARTMENT = {v: k for k, v in HQ_HOODS.items()}
# Playgrounds a Toon can stand in, plus the four Cog HQ hoods below.
# Outdoor Zone (6000) and Golf Zone (17000) are visited hoods. Funny Farm (7000)
# is not: setDefaultZone sends that hood back to Toontown Central, it is absent
# from HoodsForTeleportAll, and its DNA is unfinished. A dynamic interior
# (zone 61000 and up) is not one of these.
PLAYGROUND_HOODS = {1000, 2000, 3000, 4000, 5000, 6000, 8000, 9000, 17000}
PUBLISH_HOODS = PLAYGROUND_HOODS | set(HQ_HOODS.values())


def activity_for(hood_id, is_playground, is_street, is_interior):
    """Activity code from where the Toon stands (all inputs derived from the zone id)."""
    if hood_id in HOOD_DEPARTMENT:
        return G.ACT_BOSS_LOBBY
    if is_interior:
        return G.ACT_BUILDING
    if is_street:
        return G.ACT_STREET
    if is_playground:
        return G.ACT_PLAYGROUND
    return G.ACT_NONE


# Same cutoffs as toontown.hood.ZoneUtil / ToontownGlobals. Kept here so the
# UberDOG can turn a state-server zone into a hood without importing the game.
_WELCOME_VALLEY_TOKEN = 0
_WELCOME_VALLEY_BEGIN = 22000
_WELCOME_VALLEY_END = 61000
_TOONTOWN_CENTRAL = 2000
_GOOFY_SPEEDWAY = 8000


def canonical_zone(zone_id):
    """ZoneUtil.getCanonicalZoneId for the zones a group place cares about."""
    try:
        zone_id = int(zone_id)
    except (TypeError, ValueError):
        return 0
    if zone_id == _WELCOME_VALLEY_TOKEN:
        return _TOONTOWN_CENTRAL
    if _WELCOME_VALLEY_BEGIN <= zone_id < _WELCOME_VALLEY_END:
        zone_id = zone_id % 2000
        if zone_id < 1000:
            return zone_id + _TOONTOWN_CENTRAL
        return zone_id - 1000 + _GOOFY_SPEEDWAY
    return zone_id


def hood_of_zone(zone_id):
    """Canonical playground or HQ hood. 0 for quiet zones and dynamic interiors."""
    zone_id = canonical_zone(zone_id)
    if zone_id <= 0:
        return 0
    hood = zone_id - (zone_id % 1000)
    if hood not in PUBLISH_HOODS:
        return 0
    return hood


def place_for_new_group(kind, target, hood_id, task_hoods):
    """Hood a new group is announced in: a boss group at its department's HQ, a task group at the
    task's playground, a building group where the Toon stands."""
    if kind == G.BOSS:
        return HQ_HOODS[target]
    if kind == G.TASK:
        return task_hoods[target]
    return hood_id


def read():
    """Live context from the running game: (hood_id, shard_id, activity, task_hoods)."""
    from toontown.hood import ZoneUtil
    av = base.localAvatar
    zone = getattr(base.cr, 'zoneId', None) or av.zoneId or 0
    hood = ZoneUtil.getCanonicalHoodId(zone) if zone else 0
    activity = activity_for(hood, ZoneUtil.isPlayground(zone), ZoneUtil.getHoodId(zone) != zone and not ZoneUtil.isInterior(zone),
                            ZoneUtil.isInterior(zone))
    shard = base.localAvatar.defaultShard if hasattr(base.localAvatar, 'defaultShard') else 0
    shard = getattr(base.cr, 'currentDistrictId', None) or shard
    return hood, shard, activity, task_hoods_of(av)


def task_hoods_of(av):
    """Hoods of the Toon Tasks' target Toons (quest entries are [id, from, to, reward, progress])."""
    from toontown.hood import ZoneUtil
    from toontown.toon import NPCToons
    hoods = set()
    for quest in getattr(av, 'quests', ()):
        try:
            zone = NPCToons.getNPCZone(quest[2])
            if zone:
                hoods.add(ZoneUtil.getCanonicalHoodId(zone))
        except Exception:
            continue
    return tuple(sorted(hoods))
