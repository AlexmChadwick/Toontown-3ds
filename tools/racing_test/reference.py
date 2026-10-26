"""Execute the actual pinned __watchControls AST with stock Panda Euler physics."""
import ast
import math
from pathlib import Path
from types import SimpleNamespace
from panda3d.core import NodePath, Vec3, Mat3, loadPrcFileData
from panda3d.physics import ActorNode, PhysicsManager, LinearEulerIntegrator, ForceNode, LinearFrictionForce, LinearVectorForce
loadPrcFileData('', 'model-cache-dir\naudio-library-name null')
ROOT = Path(__file__).resolve().parents[2]
# The main checkout has ref/<submodule>; linked worktrees reach it through ref/ref.
REF = ROOT / 'ref' if (ROOT / 'ref/open-toontown/toontown').is_dir() else ROOT / 'ref/ref'
tree = ast.parse((REF/'open-toontown/toontown/racing/DistributedVehicle.py').read_text())
cls = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'DistributedVehicle')
method = next(n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name == '__watchControls')
# Old Panda-only debug pretty-printer is absent from stock 1.10; remove debug output only.
method.body = [node for node in method.body if not (isinstance(node, ast.Expr) and isinstance(node.value, ast.Call) and isinstance(node.value.func, ast.Attribute) and isinstance(node.value.func.value, ast.Name) and node.value.func.value.id == 'onScreenDebug')]
clock = SimpleNamespace(now=0, getDt=lambda: 1/30, getFrameTime=lambda: clock.now)
render = NodePath('render')
ns = dict(globalClock=clock, render=render, Vec3=Vec3, Mat3=Mat3, math=math,
          clampScalar=lambda x,a,b: min(b,max(a,x)), iceTurnFactor=.25, iceAccelFactor=.4,
          onScreenDebug=SimpleNamespace(append=lambda _: None), Task=SimpleNamespace(cont=0))
exec(compile(ast.Module(body=[method], type_ignores=[]), 'pinned DistributedVehicle.__watchControls', 'exec'), ns)
watch = ns['__watchControls']
for scenario in range(6):
    actor = ActorNode('kart')
    np = render.attachNewNode(actor)
    forward = np.attachNewNode('forward'); forward.setY(1)
    mgr = PhysicsManager(); mgr.attachLinearIntegrator(LinearEulerIntegrator()); mgr.attachPhysicalNode(actor)
    windNode = ForceNode('wind'); windNp = render.attachNewNode(windNode)
    wind = LinearFrictionForce(.2); windNode.addForce(wind); mgr.addLinearForce(wind)
    engineNode = ForceNode('engine'); engineNp = np.attachNewNode(engineNode)
    engine = LinearVectorForce(0,0,0); engineNode.addForce(engine); mgr.addLinearForce(engine)
    fake = SimpleNamespace(actorNode=actor, forward=forward, proRacer=0, stopped=scenario==5,
        arrowHorz=.6 if scenario in (1,2) else 0, arrowVert=-1 if scenario==4 else 1,
        turnRatio=40, groundType='ice' if scenario==2 else 'gravel', accelerationBase=20,
        turbo=scenario==3, engine=engine, cheatFactor=1, windResistance=wind,
        physicsEpoch=0, lastPhysicsFrame=0, physicsCalculationsPerSecond=60,
        maxPhysicsFrames=60, physicsDt=1/60, leanAmount=0, physicsMgr=mgr, imHitMult=1,
        cWallTrav=SimpleNamespace(traverse=lambda _: None), speedometer=None,
        cameraNode=SimpleNamespace(setH=lambda _: None), offGround=0,
        surfaceModifiers={'ice':dict(windResistance=.01,driftMin=0),'gravel':dict(windResistance=.2,driftMin=65)},
        getPos=lambda _: np.getPos(render), getH=np.getH, getP=lambda: 0, setH=np.setH,
        turnWheels=lambda _:None, updateParticles=lambda _:None, startSkid=lambda:None,
        stopSkid=lambda:None, stickCarToGround=lambda:None,
        notify=SimpleNamespace(debug=lambda _:None))
    setattr(fake,'__animate',lambda:None)
    setattr(fake,'__clampPosition',lambda:False)
    for frame in range(300):
        clock.now=(frame+1)/30+1e-8
        watch(fake,None)
    p=np.getPos(); v=actor.getPhysicsObject().getVelocity()
    print(scenario, *p, *v, np.getH(), fake.leanAmount)
    mgr.removePhysicalNode(actor)
    engineNp.removeNode(); windNp.removeNode(); np.removeNode()
