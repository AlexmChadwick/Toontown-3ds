"""Read-only access to the pinned Python specifications. Run with -I -B."""

from pathlib import Path
import builtins
import sys
import types

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT / "ref/ref/open-toontown"
sys.path.insert(0, str(UPSTREAM))
sys.modules.setdefault("panda3d.otp", types.ModuleType("panda3d.otp"))
from panda3d.core import VirtualFileSystem, Filename

vfs = VirtualFileSystem.getGlobalPtr()
vfs.mount(
    Filename(str(ROOT / "ref/ref/resources")),
    Filename(str(ROOT)),
    VirtualFileSystem.MFReadOnly,
)
builtins.vfs = vfs
from toontown.pets import PetUtil, PetDNA, PetTraits, PetConstants, PetNameGenerator
from toontown.racing import KartDNA
from toontown.fishing import FishTank, FishBase

# The pin expects Python 2 text from StreamReader. Stock Panda on Python 3
# returns bytes; adapt only that I/O boundary, leaving generation code intact.
_StreamReader = PetNameGenerator.StreamReader


class TextReader:
    def __init__(self, *args):
        self.reader = _StreamReader(*args)

    def readline(self):
        value = self.reader.readline()
        return value.decode("utf-8") if isinstance(value, bytes) else value


PetNameGenerator.StreamReader = TextReader
