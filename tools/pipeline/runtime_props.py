"""Playground props and collision that distributed objects add at run time.

The zone bake only sees DNA + storage. Some playground objects are drawn (or made solid) by
a distributed object's client code instead, so a plain DNA bake is missing them:

Acorn Acres (6000), OZHoodDataAI:
  - createGameTables makes a DistributedPicnicTable at each DNA ``game_table_<n>`` prop's
    pos / hpr. Its client loads phase_6/models/golf/game_table and, in announceGenerate,
    adds CollisionSphere(0, 0, 0, 2) on seat1..6 and CollisionSphere(0, 0, -2, 5.5) on
    basket_locator. The DNA code ``prop_game_table`` is in no storage file, so stock draws
    only that object's copy; we register the model under the code so the assembler places
    it with the DNA transform the AI reads.
  - createPicnicTables makes a DistributedPicnicBasket for each ``picnic_table_<n>``. The
    table itself is the DNA prop (storage_OZ_sz); announceGenerate adds
    CollisionSphere(0, 0, 0, 2) on seat1..4 and CollisionSphere(0, 0, -1, 4) on
    basket_locator.
The spheres keep CollisionNode's default into mask, as upstream does.
"""
from panda3d.core import CollisionNode, CollisionSphere

GAME_TABLE_MODEL = 'phase_6/models/golf/game_table'

# zone -> [(DNA prop code, seat count, tablecloth sphere (x, y, z, r))]
_TABLES = {
    6000: [('prop_game_table', 6, (0, 0, -2, 5.5)),
           ('prop_picnic_table', 4, (0, 0, -1, 4))],
}


def register(store, zone_id):
    """Map run-time-only model codes into the storage before assembly."""
    if zone_id == 6000 and store.find_node('prop_game_table') is None:
        model = store.model(GAME_TABLE_MODEL)
        if not model.isEmpty():
            store.maps['place_model']['prop_game_table'] = model
            store.categories['prop_game_table'] = 'prop'


def add_collision(root, zone_id):
    """Attach each table object's seat and tablecloth spheres. Returns the sphere count."""
    added = 0
    for code, seats, cloth in _TABLES.get(zone_id, ()):
        for table in root.findAllMatches('**/%s_DNARoot' % code):
            for i in range(seats):
                seat = table.find('**/*seat%d' % (i + 1))
                if seat.isEmpty():
                    continue
                cn = CollisionNode('picnicTable_sphere_%d' % i)
                cn.addSolid(CollisionSphere(0, 0, 0, 2))
                seat.attachNewNode(cn)
                added += 1
            loc = table.find('**/basket_locator')
            if not loc.isEmpty():
                cn = CollisionNode('tablecloth_sphere')
                cn.addSolid(CollisionSphere(*cloth))
                loc.attachNewNode(cn)
                added += 1
    return added
