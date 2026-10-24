# Toon Estates

The client treats a home as a server-allocated dynamic zone. The Map-tab Home
control sends `EstateManager.getEstateZone(avId, name)`; the manager replies
with `setEstateZone(ownerId, zoneId)`, after which normal zone interest creates
`DistributedEstate`, six `DistributedHouse` plots, furniture objects, and any
`DistributedPet` objects. The estate object is acknowledged with
`setClientReady`, and `exitEstate` returns to the previous zone. These fields
and methods are from `ref/open-toontown/etc/toon.dc` and
`toontown/estate/EstateManager.py`.

Each house advertises its plot index, house type, owner avatar, display name,
color, and garden position. The local house door uses the inherited
`DistributedDoor.requestEnter`/`requestExit` methods and creates a
`DistributedHouseInterior`; furniture positions and modes are authoritative
server state from `DistributedFurnitureItem`. The client renders compact
terrain, house, interior, and starter-furniture assets from
`data/estate/`. Mailbox and bank use their upstream `avatarEnter` and
`transferMoney` flows. The closet opens a bottom-screen panel on `enterAvatar`,
reuses the tailor DNA encoding, and sends `setDNA` for the current owned outfit.
Gardening remains protocol/spec-only.

Pets inherit `DistributedSmoothNode`. The nine PetDNA values (head, ears, nose,
tail, body/species, color, color scale, eye color, gender) follow
`toontown/pets/PetDNA.py`. This client uses the neutral Doodle mesh and the
latest server pose as a bounded fallback; future animation clips can be added
without changing the network contract. A production interpolation path should
use the same timestamped component smoothing used by remote toons.
