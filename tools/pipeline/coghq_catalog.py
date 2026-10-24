"""Cog HQ exterior/lobby catalog: zone ids, upstream models, tunnel/door wiring and music.

Sources (all under ref/open-toontown/toontown/ unless noted):
  toonbase/ToontownGlobals.py   zone ids (BossbotHQ 10000 ... LawbotOfficeExt 13200)
  coghq/{Sellbot,Cashbot,Lawbot,Bossbot}CogHQLoader.py   model paths, tunnel renames, music
  hood/{CSHoodData,CashbotHQData,LawbotHQData,BossbotHQData}AI.py   which door leads where

The HQ loaders do not assemble DNA: each zone is a single .bam scene whose nodes the loader
renames at load time (``Tunnel1`` -> ``linktunnel_dg_5316_DNARoot`` ...). The baker repeats
those renames so Place::exitAt/arrival (source/world/place.cpp) sees the same owner names the
streets use. Doors are the one addition: HQ doors are DistributedCogHQDoor objects upstream
(no DNA block number), so the baker renames each ``door_<i>`` ancestor of a ``door_trigger_<i>``
to ``cogdoor_<destination zone>_<i>_DNARoot`` and Place::exitAt reads the destination from it.
"""

# zone -> config
#   model        .bam under ref/resources
#   tunnels      {scene node name: new name}; collision spheres below become 'tunnel_trigger'
#   doors        {door index: destination zone}
#   drop         glob patterns stashed before extraction (sky boxes, mirrored 'underground' copies)
#   hood         music/hood id (zone - zone % 1000)
CATALOG = {
    # Sellbot HQ ------------------------------------------------------------------
    11000: dict(
        name='Sellbot HQ exterior', model='phase_9/models/cogHQ/SellbotHQExterior.bam',
        tunnels={'Tunnel1': 'linktunnel_dg_5316_DNARoot', 'Tunnel2': 'linktunnel_sellhq_11200_DNARoot'},
        doors={0: 11100, 1: 11100, 2: 11100, 3: 11100}, drop=()),
    11100: dict(
        name='Sellbot HQ lobby', model='phase_9/models/cogHQ/SellbotHQLobby.bam',
        tunnels={}, doors={0: 11000}, drop=('skyGeo',)),
    11200: dict(
        name='Sellbot factory exterior', model='phase_9/models/cogHQ/SellbotFactoryExterior.bam',
        tunnels={'tunnel_group2': 'linktunnel_sellhq_11000_DNARoot'}, doors={}, drop=()),
    # Cashbot HQ ------------------------------------------------------------------
    12000: dict(
        name='Cashbot HQ exterior', model='phase_10/models/cogHQ/CashBotShippingStation.bam',
        tunnels={'LinkTunnel1': 'linktunnel_dl_9252_DNARoot'}, doors={0: 12100}, drop=('SkyBox1',)),
    12100: dict(
        name='Cashbot HQ lobby', model='phase_10/models/cogHQ/VaultLobby.bam',
        tunnels={}, doors={0: 12000}, drop=('SkyBox',)),
    # Lawbot HQ -------------------------------------------------------------------
    13000: dict(
        name='Lawbot HQ plaza', model='phase_11/models/lawbotHQ/LawbotPlaza.bam',
        tunnels={'TunnelEntrance1': 'linktunnel_br_3326_DNARoot'}, doors={0: 13200, 1: 13100},
        drop=('underground', 'LB_Sky')),
    13100: dict(
        name='Lawbot HQ lobby', model='phase_11/models/lawbotHQ/LB_CH_Lobby.bam',
        tunnels={}, doors={0: 13000}, drop=('underground', 'CH_SkyBox')),
    13200: dict(
        name='Lawbot office exterior', model='phase_11/models/lawbotHQ/LB_DA_Lobby.bam',
        tunnels={}, doors={0: 13000}, drop=('underground',)),
    # Bossbot HQ ------------------------------------------------------------------
    10000: dict(
        name='Bossbot HQ hub', model='phase_12/models/bossbotHQ/CogGolfHub.bam',
        tunnels={'LinkTunnel1': 'linktunnel_gz_17000_DNARoot'}, doors={0: 10100}, drop=('SkyBox',)),
    10100: dict(
        name='Bossbot HQ courtyard', model='phase_12/models/bossbotHQ/CogGolfCourtyard.bam',
        tunnels={}, doors={0: 10000}, drop=('SkyBox',)),
}
ZONES = tuple(sorted(CATALOG))

# hood id -> (output name, upstream .ogg). BossbotCogHQLoader picks one of
# Bossbot_Entry_v1..3 at random per load; the baker ships v1.
MUSIC = {
    10000: 'phase_12/audio/bgm/Bossbot_Entry_v1.ogg',
    11000: 'phase_9/audio/bgm/encntr_suit_HQ_nbrhood.ogg',
    12000: 'phase_9/audio/bgm/encntr_suit_HQ_nbrhood.ogg',
    13000: 'phase_11/audio/bgm/LB_courtyard.ogg',
}

HOOD_ZONES = {
    10000: (10000, 10100),
    11000: (11000, 11100, 11200),
    12000: (12000, 12100),
    13000: (13000, 13100, 13200),
}
