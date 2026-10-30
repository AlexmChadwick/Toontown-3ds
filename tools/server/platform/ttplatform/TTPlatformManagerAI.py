"""AI side: nothing to do (the stock AI does not load platform.dc; this keeps it loadable)."""
from direct.distributed.DistributedObjectGlobalAI import DistributedObjectGlobalAI


class TTPlatformManagerAI(DistributedObjectGlobalAI):
    pass
