#include "quest/script_player.h"

#include <cstring>

namespace tt::quest {

float ScriptPlayer::easeInOut(float t) {
    // panda/src/interval/cLerpInterval.cxx compute_delta, BT_ease_in_out.
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const float t2 = t * t;
    return 3.0f * t2 - 2.0f * t * t2;
}

void ScriptPlayer::start(ScriptId id, bool femaleToon, ScriptStage* stage) {
    stop();
    stage_ = stage;
    id_ = id;
    def_ = &scriptDef(id);
    female_ = femaleToon;
    cursor_ = 0;
    block_ = Block::None;
    waitLeft_ = 0;
    event_ = nullptr;
    finished_ = false;
    timeoutRun_ = false;
}

void ScriptPlayer::stop() {
    stage_ = nullptr;
    def_ = nullptr;
    block_ = Block::Done;
    event_ = nullptr;
    for (Lerp& l : lerps_) l.used = false;
}

bool ScriptPlayer::popFinished() {
    const bool f = finished_;
    finished_ = false;
    return f;
}

const char* ScriptPlayer::chatText(const ScriptCmd& cmd) const {
    if (female_ && cmd.textAlt) return cmd.textAlt;
    return cmd.text;
}

void ScriptPlayer::update(float dt) {
    if (!stage_) return;
    float budget = dt > 0 ? dt : 0;
    while (block_ != Block::Done) {
        if (block_ == Block::Wait || block_ == Block::Preview) {
            if (budget < waitLeft_) {
                waitLeft_ -= budget;
                advanceLerps(budget);
                return;
            }
            budget -= waitLeft_;
            advanceLerps(waitLeft_);
            waitLeft_ = 0;
            // The preview's own messenger.send('doneThrowSquirtPreview') starts the next chapter.
            block_ = Block::None;
        }
        if (block_ == Block::Chat || block_ == Block::Event) break;
        if (cursor_ >= def_->numCmds) {
            block_ = Block::Done;
            break;
        }
        const ScriptCmd& cmd = def_->cmds[cursor_++];
        if (cmd.flags & kScriptUponTimeout) continue; // only timeout() runs these
        execute(cmd, false);
    }
    advanceLerps(budget);
}

void ScriptPlayer::chatDone() {
    if (block_ == Block::Chat) block_ = Block::None;
}

bool ScriptPlayer::postEvent(const char* name) {
    if (block_ != Block::Event || !event_ || !name || strcmp(event_, name)) return false;
    block_ = Block::None;
    event_ = nullptr;
    return true;
}

void ScriptPlayer::timeout() {
    if (!stage_ || timeoutRun_) return;
    timeoutRun_ = true;
    finishLerps();
    for (int i = 0; i < def_->numCmds; i++) {
        const ScriptCmd& cmd = def_->cmds[i];
        if (cmd.flags & kScriptUponTimeout) execute(cmd, true);
    }
}

void ScriptPlayer::execute(const ScriptCmd& cmd, bool instant) {
    switch (cmd.op) {
    case ScriptOp::Wait:
        waitLeft_ = cmd.args[0];
        block_ = Block::Wait;
        return;
    case ScriptOp::LerpPos:
    case ScriptOp::LerpHpr:
    case ScriptOp::LerpScale:
    case ScriptOp::LerpPosHprScale:
    case ScriptOp::LerpColorScale:
    case ScriptOp::StartThrob:
        startLerp(cmd, instant);
        return;
    case ScriptOp::StopThrob:
        // throbIval.finish(): the loop's last state, its start colour scale.
        for (Lerp& l : lerps_) {
            if (l.used && l.kind == LerpKind::Throb) {
                stage_->setColorScale(l.var, l.from);
                l.used = false;
            }
        }
        stage_->run(cmd, cmd.text);
        return;
    case ScriptOp::Pos:
    case ScriptOp::Hpr:
    case ScriptOp::Scale:
    case ScriptOp::PosHprScale: {
        ScriptXform x;
        stage_->getXform(cmd.var, &x);
        const float* a = cmd.args;
        if (cmd.op == ScriptOp::Pos) memcpy(x.pos, a, sizeof(x.pos));
        if (cmd.op == ScriptOp::Hpr) memcpy(x.hpr, a, sizeof(x.hpr));
        if (cmd.op == ScriptOp::Scale) memcpy(x.scale, a, sizeof(x.scale));
        if (cmd.op == ScriptOp::PosHprScale) {
            memcpy(x.pos, a, sizeof(x.pos));
            memcpy(x.hpr, a + 3, sizeof(x.hpr));
            memcpy(x.scale, a + 6, sizeof(x.scale));
        }
        // A setPos on a node with a running lerp of the same channel: the lerp wins on its next
        // step upstream too, so nothing to cancel here.
        stage_->setXform(cmd.var, x);
        return;
    }
    case ScriptOp::ColorScale:
        stage_->setColorScale(cmd.var, cmd.args);
        return;
    case ScriptOp::ChatConfirm:
    case ScriptOp::LocalChatConfirm:
    case ScriptOp::CCChatConfirm:
    case ScriptOp::CCChatToConfirm:
        stage_->run(cmd, chatText(cmd));
        if (!instant) block_ = Block::Chat;
        return;
    case ScriptOp::WaitEvent:
        if (instant) return;
        event_ = cmd.text;
        block_ = Block::Event;
        stage_->run(cmd, cmd.text);
        return;
    case ScriptOp::ShowThrowSquirtPreview:
        stage_->run(cmd, cmd.text);
        if (instant) return;
        waitLeft_ = kThrowSquirtPreviewSec;
        block_ = Block::Preview;
        return;
    case ScriptOp::FinishQuestMovie:
        finished_ = true;
        stage_->run(cmd, cmd.text);
        return;
    default:
        stage_->run(cmd, chatText(cmd));
        return;
    }
}

void ScriptPlayer::startLerp(const ScriptCmd& cmd, bool instant) {
    Lerp* slot = nullptr;
    for (Lerp& l : lerps_) {
        if (!l.used) {
            slot = &l;
            break;
        }
    }
    Lerp tmp;
    Lerp& l = slot ? *slot : tmp;
    l = Lerp{};
    l.var = cmd.var;
    ScriptXform x;
    stage_->getXform(cmd.var, &x);
    const float* a = cmd.args;
    switch (cmd.op) {
    case ScriptOp::LerpPos:
        l.kind = LerpKind::Pos;
        memcpy(l.from, x.pos, sizeof(x.pos));
        memcpy(l.to, a, 3 * sizeof(float));
        l.duration = a[3];
        break;
    case ScriptOp::LerpHpr:
        l.kind = LerpKind::Hpr;
        memcpy(l.from, x.hpr, sizeof(x.hpr));
        memcpy(l.to, a, 3 * sizeof(float));
        l.duration = a[3];
        break;
    case ScriptOp::LerpScale:
        l.kind = LerpKind::Scale;
        memcpy(l.from, x.scale, sizeof(x.scale));
        memcpy(l.to, a, 3 * sizeof(float));
        l.duration = a[3];
        break;
    case ScriptOp::LerpPosHprScale:
        l.kind = LerpKind::PosHprScale;
        memcpy(l.from, x.pos, sizeof(x.pos));
        memcpy(l.from + 3, x.hpr, sizeof(x.hpr));
        memcpy(l.from + 6, x.scale, sizeof(x.scale));
        memcpy(l.to, a, 9 * sizeof(float));
        l.duration = a[9];
        break;
    case ScriptOp::LerpColorScale: // explicit start colour (startColorScale)
        l.kind = LerpKind::ColorScale;
        memcpy(l.from, a, 4 * sizeof(float));
        memcpy(l.to, a + 4, 4 * sizeof(float));
        l.duration = a[8];
        break;
    default: // StartThrob: start -> dest in t/2, back in t/2, looping
        l.kind = LerpKind::Throb;
        memcpy(l.from, a, 4 * sizeof(float));
        memcpy(l.to, a + 4, 4 * sizeof(float));
        l.duration = a[8];
        break;
    }
    // A lerp replaces one of the same kind still running on that node (the newer interval
    // drives the node on every frame after it starts).
    for (Lerp& other : lerps_) {
        if (&other != &l && other.used && other.var == l.var && other.kind == l.kind) other.used = false;
    }
    if (instant || l.duration <= 0.0f || !slot) {
        // No time to run (UPON_TIMEOUT, zero length, every slot busy): the end state now. A
        // throb's resting state is its start colour.
        if (l.kind == LerpKind::Throb) {
            stage_->setColorScale(l.var, l.from);
        } else {
            applyLerp(l, 1.0f);
        }
        l.used = false;
        return;
    }
    l.used = true;
    applyLerp(l, 0.0f);
}

void ScriptPlayer::applyLerp(Lerp& l, float u) {
    if (l.kind == LerpKind::ColorScale || l.kind == LerpKind::Throb) {
        float c[4];
        for (int i = 0; i < 4; i++) c[i] = l.from[i] + (l.to[i] - l.from[i]) * u;
        stage_->setColorScale(l.var, c);
        return;
    }
    ScriptXform x;
    stage_->getXform(l.var, &x);
    float v[9];
    const int n = l.kind == LerpKind::PosHprScale ? 9 : 3;
    for (int i = 0; i < n; i++) v[i] = l.from[i] + (l.to[i] - l.from[i]) * u;
    switch (l.kind) {
    case LerpKind::Pos: memcpy(x.pos, v, sizeof(x.pos)); break;
    case LerpKind::Hpr: memcpy(x.hpr, v, sizeof(x.hpr)); break;
    case LerpKind::Scale: memcpy(x.scale, v, sizeof(x.scale)); break;
    default:
        memcpy(x.pos, v, sizeof(x.pos));
        memcpy(x.hpr, v + 3, sizeof(x.hpr));
        memcpy(x.scale, v + 6, sizeof(x.scale));
        break;
    }
    stage_->setXform(l.var, x);
}

void ScriptPlayer::advanceLerps(float dt) {
    if (dt <= 0.0f) return;
    for (Lerp& l : lerps_) {
        if (!l.used) continue;
        l.t += dt;
        if (l.kind == LerpKind::Throb) {
            // Two easeInOut halves of duration/2, looping (Sequence(...).loop()).
            const float half = l.duration * 0.5f;
            float t = l.t;
            while (t >= l.duration) t -= l.duration;
            l.t = t;
            const bool back = t >= half;
            const float u = easeInOut((back ? t - half : t) / half);
            float c[4];
            for (int i = 0; i < 4; i++) {
                const float a = back ? l.to[i] : l.from[i];
                const float b = back ? l.from[i] : l.to[i];
                c[i] = a + (b - a) * u;
            }
            stage_->setColorScale(l.var, c);
            continue;
        }
        if (l.t >= l.duration) {
            applyLerp(l, 1.0f);
            l.used = false;
        } else {
            applyLerp(l, easeInOut(l.t / l.duration));
        }
    }
}

void ScriptPlayer::finishLerps() {
    for (Lerp& l : lerps_) {
        if (!l.used) continue;
        if (l.kind == LerpKind::Throb) {
            stage_->setColorScale(l.var, l.from);
        } else {
            applyLerp(l, 1.0f);
        }
        l.used = false;
    }
}

} // namespace tt::quest
