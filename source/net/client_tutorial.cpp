// The Toontorial, network side (docs/design/tutorial.md section 1): TutorialManager, the tutorial
// interior, the tutorial Flunky, the tutorial battle class, door answers and the tunnel blocker.
//
// Upstream client: toontown/tutorial/TutorialManager.py, distributed/ToontownClientRepository.py
// (enterTutorialQuestion), building/DistributedTutorialInterior.py, suit/DistributedTutorialSuit.py,
// building/DistributedDoor.py (setOtherZoneIdAndDoId, rejectEnter), toon/DistributedNPCBlocker.py.
#include <cstring>

#include "core/log.h"
#include "net/client.h"
#include "net/msg_types.h"

namespace tt::net {

namespace {

constexpr uint16_t kZoneInterest = 3;   // client.cpp: the zone interest handle
} // namespace

#define TT_NET_UPDATE(w, doId, cls, field)                                                  \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " is not clsend"); \
    beginUpdate(w, doId, dc::cls::k_##field)
#define TT_SIG(cls, field, expect) \
    static_assert(dc::sigEq(dc::cls::sig_##field, expect), #cls "." #field " layout changed")
TT_SIG(TutorialManager, requestTutorial, "");
TT_SIG(TutorialManager, enterTutorial, "u32,u32,u32,u32");
TT_SIG(TutorialManager, allDone, "");
TT_SIG(TutorialManager, toonArrived, "");
TT_SIG(DistributedTutorialInterior, setZoneIdAndBlock, "u32,u16");
TT_SIG(DistributedTutorialInterior, setTutorialNpcId, "u32");
TT_SIG(DistributedTutorialSuit, requestBattle, "i16/10,i16/10,i16/10,i16/10,i16/10,i16/10");
TT_SIG(DistributedDoor, setOtherZoneIdAndDoId, "u32,u32");
TT_SIG(DistributedDoor, rejectEnter, "i8");
TT_SIG(DistributedNPCBlocker, setMovie, "u8,u32,u32,i16");
TT_SIG(DistributedNPCToonBase, avatarEnter, "");

// The front end sets this before createAvatar (the "Play the Toontorial?" prompt), so the choice
// is known when the new toon's owner view arrives.
void Client::setTutorialChoice(TutorialChoice choice) {
    tutorialChoice_ = choice;
    TT_LOG("net: Toontorial choice: %s", choice == TutorialChoice::Play ? "play" : "skip");
}

// ToontownClientRepository.enterTutorialQuestion: a new toon that plays the Toontorial asks for it
// before entering any zone, and goes where enterTutorial says (playGame [Tutorial, shopZone]).
bool Client::tutorialHoldsZone() const {
    return tutorialChoice_ == TutorialChoice::Play && playingNewAvatar() && !tutorialDone_ &&
           !tutorialZones_.active();
}

void Client::maybeRequestTutorial() {
    if (!tutorialHoldsZone() || tutorialRequested_ || tutorialManagerId_ == 0 || !ownerReceived_ ||
        state_ != State::EnterZone || shardId_ == 0) {
        return;
    }
    sendRequestTutorial();
    tutorialRequested_ = true;
    stateSince_ = now_; // the AI answers at once; the step timeout counts from the request
    TT_LOG("net: requested the Toontorial for new toon %u", unsigned(avatarId_));
}

void Client::enterTutorialZone() {
    // The uber interest is open (that is where the TutorialManager lives); now the shop.
    cfg_.zoneId = tutorialZones_.shop;
    TT_LOG("net: entering the Toontorial: Tutorial Tom's interior, zone %u", unsigned(cfg_.zoneId));
    sendSetLocation(avatarId_, shardId_, cfg_.zoneId);
    zoneCtx_ = nextContext_;
    zoneDone_ = false;
    sendAddInterest(kZoneInterest, shardId_, cfg_.zoneId);
}

void Client::sendRequestTutorial() {
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, tutorialManagerId_, TutorialManager, requestTutorial);
    finish(w);
}

// PlayGame.enterTutorialHood: messenger.send('toonArrivedTutorial') -> TutorialManager.d_toonArrived.
// The AI then resets our quests, laff (15), experience and inventory (one cupcake, one squirting
// flower) for the scripted lessons (TutorialManagerAI.toonArrived).
void Client::sendTutorialToonArrived() {
    if (tutorialManagerId_ == 0) return;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, tutorialManagerId_, TutorialManager, toonArrived);
    if (finish(w)) {
        tutorialArrivedSent_ = true;
        TT_LOG("net: toonArrived (Toontorial)");
    }
}

bool Client::sendTutorialAllDone() {
    if (state_ != State::InZone || !tutorialZones_.active() || tutorialManagerId_ == 0) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, tutorialManagerId_, TutorialManager, allDone);
    if (!finish(w)) return false;
    tutorialDone_ = true;
    tutorialZones_.clear(); // ZoneUtil.overrideOff
    TT_LOG("net: allDone (Toontorial finished)");
    return true;
}

bool Client::sendBlockerAvatarEnter(uint32_t npcDoId) {
    const NpcToon* n = findNpc(npcDoId);
    if (state_ != State::InZone || !n || n->classId != dc::DistributedNPCBlocker::kClassId) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, npcDoId, DistributedNPCToonBase, avatarEnter);
    return finish(w);
}

bool Client::pollDoorReply(DoorReply& out) {
    if (doorReplyHead_ == doorReplyTail_) return false;
    out = doorReplies_[doorReplyHead_++ % kTutorialQueue];
    return true;
}

bool Client::pollBlockerMovie(BlockerMovie& out) {
    if (blockerHead_ == blockerTail_) return false;
    out = blockerMovies_[blockerHead_++ % kTutorialQueue];
    return true;
}

bool Client::handleTutorialEnter(uint32_t doId, uint16_t classId, uint32_t zone, DatagramReader& r,
                                 const uint16_t* req, uint16_t reqCount, bool other) {
    if (classId == dc::DistributedTutorialInterior::kClassId) {
        TutorialInterior ti{};
        ti.doId = doId;
        auto apply = [&](uint16_t fieldId) {
            if (fieldId == dc::DistributedTutorialInterior::k_setZoneIdAndBlock) {
                ti.zoneId = r.u32();
                ti.block = r.u16();
            } else if (fieldId == dc::DistributedTutorialInterior::k_setTutorialNpcId) {
                ti.npcDoId = r.u32();
            } else {
                skipField(r, fieldId);
            }
        };
        for (uint16_t i = 0; i < reqCount && r.ok(); ++i) apply(req[i]);
        if (other) {
            const uint16_t n = r.u16();
            for (uint16_t i = 0; i < n && r.ok(); ++i) apply(r.u16());
        }
        if (!r.ok()) {
            TT_LOG("net: tutorial interior %u did not parse", unsigned(doId));
            return true;
        }
        ti.active = true;
        tutorialInterior_ = ti;
        TT_LOG("net: tutorial interior %u zone %u block %u, Tom is %u", unsigned(doId), unsigned(ti.zoneId),
               unsigned(ti.block), unsigned(ti.npcDoId));
        pushEvent(EventType::TutorialInterior, doId);
        return true;
    }
    if (classId == dc::DistributedTutorialSuit::kClassId) {
        // A DistributedSuitBase like the street cogs (same required fields), but it never gets a
        // path: DistributedTutorialSuit.enterWalk loops a fixed rectangle (world/tutorial.*).
        if (handleSuitEnter(doId, zone, r, req, reqCount, other)) {
            for (Suit& s : suits_) {
                if (s.active && s.doId == doId) s.tutorial = true;
            }
            tutorialSuitId_ = doId;
            TT_LOG("net: tutorial Flunky %u", unsigned(doId));
        }
        return true;
    }
    if (classId == dc::DistributedBattleTutorial::kClassId) {
        // DistributedBattleTutorial is a DistributedBattle (no new fields): the battle section
        // parses it; remember it is the tutorial one (no input timer, the tutorial reward movie).
        tutorialBattleId_ = doId;
        TT_LOG("net: tutorial battle %u", unsigned(doId));
        return false;
    }
    return false;
}

bool Client::handleTutorialField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    if (doId == tutorialManagerId_ && tutorialManagerId_ != 0 && fieldId == dc::TutorialManager::k_enterTutorial) {
        const uint32_t branch = r.u32();
        const uint32_t street = r.u32();
        const uint32_t shop = r.u32();
        const uint32_t hq = r.u32();
        if (!r.ok()) return true;
        if (!world::TutorialZones::valid(branch, street, shop, hq)) {
            TT_LOG("net: enterTutorial with unusable zones %u %u %u %u", unsigned(branch), unsigned(street),
                   unsigned(shop), unsigned(hq));
            return true;
        }
        if (!tutorialRequested_ || tutorialZones_.active()) {
            TT_LOG("net: unexpected enterTutorial ignored");
            return true;
        }
        tutorialZones_.branch = branch;
        tutorialZones_.street = street;
        tutorialZones_.shop = shop;
        tutorialZones_.hq = hq;
        tutorialArrivedSent_ = false;
        TT_LOG("net: enterTutorial branch %u street %u shop %u hq %u", unsigned(branch), unsigned(street),
               unsigned(shop), unsigned(hq));
        if (state_ == State::EnterZone) enterTutorialZone();
        pushEvent(EventType::TutorialEnter, doId);
        return true;
    }
    if (fieldId == dc::DistributedDoor::k_setOtherZoneIdAndDoId || fieldId == dc::DistributedDoor::k_rejectEnter) {
        bool known = false;
        for (size_t i = 0; i < numDoors_ && !known; ++i) known = doors_[i].doId == doId;
        if (!known) return false;
        DoorReply d{};
        d.doorDoId = doId;
        if (fieldId == dc::DistributedDoor::k_setOtherZoneIdAndDoId) {
            d.otherZone = r.u32();
            d.otherDoId = r.u32();
        } else {
            d.rejectCode = static_cast<uint8_t>(r.i8());
            d.rejected = true;
        }
        if (!r.ok()) return true;
        if (doorReplyTail_ - doorReplyHead_ == uint32_t(kTutorialQueue)) doorReplyHead_++; // drop the oldest
        doorReplies_[doorReplyTail_++ % kTutorialQueue] = d;
        TT_LOG("net: door %u %s %u", unsigned(doId), d.rejected ? "rejectEnter" : "opens to zone",
               unsigned(d.rejected ? d.rejectCode : d.otherZone));
        pushEvent(EventType::DoorReply, doId);
        return true;
    }
    if (fieldId == dc::DistributedNPCBlocker::k_setMovie) {
        const NpcToon* n = findNpc(doId);
        if (!n || n->classId != dc::DistributedNPCBlocker::kClassId) return false;
        BlockerMovie m{};
        m.npcDoId = doId;
        m.mode = r.u8();
        m.npcId = r.u32();
        m.avId = r.u32();
        r.i16(); // timestamp: the movie is local (QuestParser), nothing is seeked
        if (!r.ok()) return true;
        if (blockerTail_ - blockerHead_ == uint32_t(kTutorialQueue)) blockerHead_++;
        blockerMovies_[blockerTail_++ % kTutorialQueue] = m;
        TT_LOG("net: blocker %u setMovie mode %u for %u", unsigned(doId), unsigned(m.mode), unsigned(m.avId));
        pushEvent(EventType::BlockerMovie, doId);
        return true;
    }
    return false;
}

void Client::clearTutorialObjects() {
    tutorialInterior_ = TutorialInterior{};
    tutorialSuitId_ = 0;
    tutorialBattleId_ = 0;
    doorReplyHead_ = doorReplyTail_ = 0;
    blockerHead_ = blockerTail_ = 0;
}

// Connection reset: the AI destroys an unfinished tutorial when we leave
// (TutorialManagerAI.__handleUnexpectedExit), so a rejoin asks for a new one from the start.
void Client::resetTutorialSession() {
    clearTutorialObjects();
    if (tutorialZones_.active()) {
        tutorialZones_.clear();
        cfg_.zoneId = kTtcPlayground;
    }
    tutorialRequested_ = false;
    tutorialArrivedSent_ = false;
}

} // namespace tt::net
