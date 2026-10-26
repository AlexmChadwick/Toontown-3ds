"""Execute upstream Piejectile.__updatePhysics with stock Panda Euler integration."""
import ast
from pathlib import Path
from types import SimpleNamespace
from panda3d.core import NodePath, Point3, Vec3, loadPrcFileData
from panda3d.physics import ActorNode, PhysicsManager, LinearEulerIntegrator, ForceNode, LinearFrictionForce, LinearVectorForce
loadPrcFileData('', 'model-cache-dir\naudio-library-name null')
ROOT = Path(__file__).resolve().parents[2]
# The main checkout has ref/<submodule>; linked worktrees reach it through ref/ref.
REF = ROOT / 'ref' if (ROOT / 'ref/open-toontown/toontown').is_dir() else ROOT / 'ref/ref'
source = ast.parse((REF/'open-toontown/toontown/racing/Piejectile.py').read_text())
cls = next(n for n in source.body if isinstance(n,ast.ClassDef) and n.name == 'Piejectile')
method = next(n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name == '__updatePhysics')
clock = SimpleNamespace(now=0, getDt=lambda:1/30, getFrameTime=lambda:clock.now)
namespace = dict(globalClock=clock, base=SimpleNamespace(cr=SimpleNamespace(doId2do={99:object()})), Point3=Point3, Vec3=Vec3, Task=SimpleNamespace(cont=0,done=1))
exec(compile(ast.Module(body=[method],type_ignores=[]),'pinned Piejectile.__updatePhysics','exec'),namespace)
for scenario in range(2):
    render = NodePath('render')
    actor = ActorNode('pie'); node = render.attachNewNode(actor); node.setH(25)
    manager = PhysicsManager(); manager.attachLinearIntegrator(LinearEulerIntegrator()); manager.attachPhysicalNode(actor)
    windNode = ForceNode('wind'); windNp = render.attachNewNode(windNode)
    wind = LinearFrictionForce(.2); windNode.addForce(wind); manager.addLinearForce(wind)
    engineNode = ForceNode('engine'); engineNp = node.attachNewNode(engineNode)
    engine = LinearVectorForce(0,0,3); engineNode.addForce(engine); manager.addLinearForce(engine)
    velocity = Vec3(10,30,0) + node.getQuat(render).xform(Vec3(0,50,0)) + Vec3(0,0,50)
    actor.getPhysicsObject().setVelocity(velocity)
    target = Point3(0,5000,30)
    fake = SimpleNamespace(deleting=False, startTime=0, maxTime=8, windResistance=wind,
        targetId=99 if scenario else 0, hasTarget=scenario, d2t=0, lastD2t=0,
        targetKart=SimpleNamespace(getVelocity=lambda:Vec3(20,0,0),getPos=lambda:target),
        lookAt=node.lookAt, splatTarget=lambda:None, count=0, physicsEpoch=0,
        physicsCalculationsPerSecond=60,lastPhysicsFrame=0,maxPhysicsFrames=60,physicsDt=1/60,
        engine=engine,gagNode=node.attachNewNode('gag'),rotH=0,rotP=0,rotR=0,physicsMgr=manager,
        checkTargetDistance=lambda:(node.getPos(render)-target).length() if scenario else 0)
    for frame in range(90):
        clock.now=(frame+1)/30+1e-8
        namespace['__updatePhysics'](fake,None)
    print(scenario,*node.getPos(render),*actor.getPhysicsObject().getVelocity())
