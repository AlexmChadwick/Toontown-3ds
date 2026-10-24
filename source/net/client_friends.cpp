// Friends and whispers (docs/design/friends.md): the "friends" section of tt::net::Client.
//
// On the stock Open Toontown server no FriendManager object is ever generated, so friend
// requests report RequestState::NoManager; whispers and teleport queries still work between
// toons that can see each other.
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "net/client.h"
#include "net/msg_types.h"

namespace tt::net {

// Same guard as client.cpp: outgoing fields must be clsend (Astron ejects anything else).
#define TT_NET_UPDATE(w, doId, cls, field)                                                  \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " is not clsend"); \
    beginUpdate(w, doId, dc::cls::k_##field)

namespace {

#define TT_FSIG(cls, field, expect) \
    static_assert(dc::sigEq(dc::cls::sig_##field, expect), #cls "." #field " layout changed")
TT_FSIG(DistributedToon, setFriendsList, "{u32,u8}[]");
TT_FSIG(DistributedToon, friendsNotify, "i32,i8");
TT_FSIG(DistributedToon, setWhisperSCFrom, "u32,u16");
TT_FSIG(DistributedToon, setWhisperSCCustomFrom, "u32,u16");
TT_FSIG(DistributedToon, setWhisperSCEmoteFrom, "u32,u16");
TT_FSIG(DistributedToon, setSystemMessage, "u32,string");
TT_FSIG(DistributedToon, teleportQuery, "u32");
TT_FSIG(DistributedToon, teleportResponse, "u32,i8,u32,u32,u32");
TT_FSIG(DistributedToon, teleportGiveup, "u32");
TT_FSIG(FriendManager, friendQuery, "i32");
TT_FSIG(FriendManager, cancelFriendQuery, "i32");
TT_FSIG(FriendManager, inviteeFriendResponse, "i8,i32");
TT_FSIG(FriendManager, friendConsidering, "i8,i32");
TT_FSIG(FriendManager, friendResponse, "i8,i32");
TT_FSIG(FriendManager, inviteeFriendQuery, "i32,string,blob,i32");
TT_FSIG(FriendManager, inviteeCancelFriendQuery, "i32");
#undef TT_FSIG

constexpr uint32_t kRequestTimeoutMs = 30000;

} // namespace

void Client::friendsReset() {
    friends_ = FriendsState();
}

void Client::friendsUpdate() {
    FriendRequest& q = friends_.request;
    if ((q.state == RequestState::Checking || q.state == RequestState::Asking) && now_ - q.sinceMs > kRequestTimeoutMs) {
        q.state = RequestState::NotAvailable;
        FriendEvent ev{FriendEventType::RequestUpdate, q.targetId};
        friends_.events[friends_.evHead++ % 32] = ev;
    }
}

bool Client::pollFriendEvent(FriendEvent& out) {
    if (friends_.evTail == friends_.evHead) return false;
    out = friends_.events[friends_.evTail++ % 32];
    return true;
}

const FriendInfo* Client::findFriend(uint32_t doId) const {
    for (int i = 0; i < friends_.numFriends; i++)
        if (friends_.list[i].doId == doId) return &friends_.list[i];
    return nullptr;
}

bool Client::friendsHandleEnter(const dc::ClassDesc& cls, DatagramReader& r, uint32_t doId, const uint16_t* req,
                                uint16_t reqCount, bool other) {
    (void)cls, (void)r, (void)req, (void)reqCount, (void)other;
    if (cls.classId != dc::FriendManager::kClassId) return false;
    friends_.managerId = doId; // no required fields worth keeping
    return true;
}

// Owner-view fields of our own toon (generate and updates).
bool Client::friendsOwnField(DatagramReader& r, uint16_t fieldId) { return friendsHandleSetField(r, avatarId_, fieldId); }

void Client::friendsHandleLeaving(uint32_t doId) {
    if (doId == friends_.managerId) friends_.managerId = 0;
    for (int i = 0; i < friends_.numFriends; i++)
        if (friends_.list[i].doId == doId) friends_.list[i].nearby = false;
    if (friends_.request.targetId == doId &&
        (friends_.request.state == RequestState::Checking || friends_.request.state == RequestState::Asking)) {
        friends_.request.state = RequestState::WentAway;
        friends_.events[friends_.evHead++ % 32] = FriendEvent{FriendEventType::RequestUpdate, doId};
    }
}

bool Client::friendsHandleSetField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    auto push = [&](FriendEventType t, uint32_t id) { friends_.events[friends_.evHead++ % 32] = FriendEvent{t, id}; };
    auto logWhisper = [&](uint32_t from, WhisperKind kind, uint16_t index, const char* text) {
        Whisper& w = friends_.log[(friends_.logHead + friends_.logCount) % kWhisperLogLen];
        if (friends_.logCount < kWhisperLogLen) friends_.logCount++;
        else friends_.logHead = (friends_.logHead + 1) % kWhisperLogLen;
        w = Whisper{};
        w.fromId = from;
        w.toId = avatarId_;
        w.kind = kind;
        w.index = index;
        w.ms = now_;
        if (const RemoteToon* t = findToon(from)) snprintf(w.name, sizeof(w.name), "%s", t->name);
        else if (const FriendInfo* f = findFriend(from)) memcpy(w.name, f->name, sizeof(w.name)); // both kNameLen
        if (text) snprintf(w.text, sizeof(w.text), "%s", text);
        push(FriendEventType::Whisper, from);
    };
    if (doId == avatarId_ && avatarId_ != 0) {
        if (fieldId == dc::DistributedToon::k_setWhisperSCFrom || fieldId == dc::DistributedToon::k_setWhisperSCCustomFrom ||
            fieldId == dc::DistributedToon::k_setWhisperSCEmoteFrom) {
            const uint32_t from = r.u32();
            const uint16_t index = r.u16();
            if (!r.ok()) return true;
            const WhisperKind kind = fieldId == dc::DistributedToon::k_setWhisperSCFrom         ? WhisperKind::SpeedChat
                                     : fieldId == dc::DistributedToon::k_setWhisperSCCustomFrom ? WhisperKind::Custom
                                                                                               : WhisperKind::Emote;
            logWhisper(from, kind, index, nullptr);
            return true;
        }
        if (fieldId == dc::DistributedToon::k_setSystemMessage) {
            const uint32_t about = r.u32();
            char text[kWhisperTextLen];
            r.string(text, sizeof(text));
            if (r.ok()) logWhisper(about, WhisperKind::System, 0, text);
            return true;
        }
        if (fieldId == dc::DistributedToon::k_setFriendsList) {
            const uint16_t bytes = r.u16();
            friends_.numFriends = 0;
            for (uint16_t i = 0; i + 5 <= bytes && r.ok(); i += 5) {
                const uint32_t id = r.u32();
                const uint8_t code = r.u8();
                if (friends_.numFriends < kMaxFriends) {
                    FriendInfo& f = friends_.list[friends_.numFriends++];
                    f = FriendInfo{};
                    f.doId = id;
                    f.code = code;
                    if (const RemoteToon* t = findToon(id)) {
                        f.nearby = true;
                        f.hasName = true;
                        snprintf(f.name, sizeof(f.name), "%s", t->name);
                    }
                }
            }
            push(FriendEventType::ListChanged, 0);
            return true;
        }
        if (fieldId == dc::DistributedToon::k_friendsNotify) {
            const int32_t who = r.i32();
            friends_.notifyStatus = r.i8();
            if (r.ok()) push(FriendEventType::Notify, static_cast<uint32_t>(who));
            return true;
        }
        if (fieldId == dc::DistributedToon::k_teleportQuery) {
            // Someone wants to teleport to us. The 3DS client always answers "busy" for now:
            // we cannot hand out a drop point in every zone kind yet (docs/design/friends.md).
            const uint32_t requester = r.u32();
            if (!r.ok()) return true;
            if (findToon(requester) && state_ == State::InZone) {
                DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
                TT_NET_UPDATE(w, requester, DistributedToon, teleportResponse);
                w.u32(avatarId_);
                w.i8(0);
                w.u32(shardId_);
                w.u32(cfg_.zoneId - cfg_.zoneId % 1000);
                w.u32(cfg_.zoneId);
                finish(w);
            }
            push(FriendEventType::TeleportAsked, requester);
            return true;
        }
        if (fieldId == dc::DistributedToon::k_teleportResponse) {
            TeleportResult& t = friends_.teleport;
            t.avId = r.u32();
            t.available = r.i8();
            t.shardId = r.u32();
            t.hoodId = r.u32();
            t.zoneId = r.u32();
            t.ms = now_;
            t.valid = r.ok();
            push(FriendEventType::TeleportResult, t.avId);
            return true;
        }
        return false;
    }
    if (doId == friends_.managerId && friends_.managerId != 0) {
        FriendRequest& q = friends_.request;
        if (fieldId == dc::FriendManager::k_friendConsidering) {
            const int8_t yesNo = r.i8();
            const int32_t ctx = r.i32();
            if (!r.ok()) return true;
            q.context = ctx;
            q.state = yesNo == 1 ? RequestState::Asking
                      : yesNo == 2 ? RequestState::Already
                      : yesNo == 3 ? RequestState::Self
                      : yesNo == 4 ? RequestState::Ignored
                      : yesNo == 6 ? RequestState::NotAccepting
                      : yesNo == 7 ? RequestState::OtherTooMany
                                   : RequestState::NotAvailable;
            q.sinceMs = now_;
            push(FriendEventType::RequestUpdate, q.targetId);
        } else if (fieldId == dc::FriendManager::k_friendResponse) {
            const int8_t yesNoMaybe = r.i8();
            r.i32();
            if (!r.ok()) return true;
            q.state = yesNoMaybe == 1 ? RequestState::Yes
                      : yesNoMaybe == 2 ? RequestState::Maybe
                      : yesNoMaybe == 3 ? RequestState::TooMany
                                        : RequestState::No;
            push(FriendEventType::RequestUpdate, q.targetId);
        } else if (fieldId == dc::FriendManager::k_inviteeFriendQuery) {
            FriendInvite& in = friends_.invite;
            in = FriendInvite{};
            in.inviterId = static_cast<uint32_t>(r.i32());
            r.string(in.name, sizeof(in.name));
            Bytes b = r.blob();
            in.hasDna = r.ok() && b.len == kDnaSize;
            if (in.hasDna) memcpy(in.dna, b.data, kDnaSize);
            in.context = r.i32();
            in.sinceMs = now_;
            in.active = r.ok();
            if (in.active) {
                // inviteeFriendConsidering(1, context): we are showing the question.
                DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
                TT_NET_UPDATE(w, friends_.managerId, FriendManager, inviteeFriendConsidering);
                w.i8(1);
                w.i32(in.context);
                finish(w);
                push(FriendEventType::InviteIn, in.inviterId);
            }
        } else if (fieldId == dc::FriendManager::k_inviteeCancelFriendQuery) {
            const int32_t ctx = r.i32();
            if (r.ok() && friends_.invite.active && friends_.invite.context == ctx) {
                friends_.invite.active = false;
                DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
                TT_NET_UPDATE(w, friends_.managerId, FriendManager, inviteeAcknowledgeCancel);
                w.i32(ctx);
                finish(w);
                push(FriendEventType::InviteCancelled, friends_.invite.inviterId);
            }
        } else {
            skipField(r, fieldId);
        }
        return true;
    }
    return false;
}

bool Client::sendWhisperSC(uint32_t toDoId, uint16_t msgIndex) {
    if (state_ != State::InZone || !findToon(toDoId)) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, toDoId, DistributedToon, setWhisperSCFrom);
    w.u32(avatarId_);
    w.u16(msgIndex);
    if (!finish(w)) return false;
    Whisper& o = friends_.log[(friends_.logHead + friends_.logCount) % kWhisperLogLen];
    if (friends_.logCount < kWhisperLogLen) friends_.logCount++;
    else friends_.logHead = (friends_.logHead + 1) % kWhisperLogLen;
    o = Whisper{};
    o.fromId = avatarId_;
    o.toId = toDoId;
    o.kind = WhisperKind::Outgoing;
    o.index = msgIndex;
    o.ms = now_;
    if (const RemoteToon* t = findToon(toDoId)) snprintf(o.name, sizeof(o.name), "%s", t->name);
    return true;
}

bool Client::requestFriend(uint32_t avId) {
    FriendRequest& q = friends_.request;
    q = FriendRequest{};
    q.targetId = avId;
    q.sinceMs = now_;
    if (const RemoteToon* t = findToon(avId)) snprintf(q.targetName, sizeof(q.targetName), "%s", t->name);
    if (!friends_.managerId) {
        q.state = RequestState::NoManager;
        return false;
    }
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, friends_.managerId, FriendManager, friendQuery);
    w.i32(static_cast<int32_t>(avId));
    if (!finish(w)) return false;
    q.state = RequestState::Checking;
    return true;
}

void Client::cancelFriendRequest() {
    FriendRequest& q = friends_.request;
    if (friends_.managerId && (q.state == RequestState::Checking || q.state == RequestState::Asking)) {
        DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
        TT_NET_UPDATE(w, friends_.managerId, FriendManager, cancelFriendQuery);
        w.i32(q.context);
        finish(w);
    }
    q = FriendRequest{};
}

bool Client::answerInvite(bool accept) {
    FriendInvite& in = friends_.invite;
    if (!in.active || !friends_.managerId) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, friends_.managerId, FriendManager, inviteeFriendResponse);
    w.i8(accept ? 1 : 0);
    w.i32(in.context);
    in.active = false;
    return finish(w);
}

bool Client::sendTeleportQuery(uint32_t avId) {
    if (state_ != State::InZone || !findToon(avId)) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, avId, DistributedToon, teleportQuery);
    w.u32(avatarId_);
    friends_.teleportAskId = avId;
    return finish(w);
}

void Client::sendTeleportGiveup(uint32_t avId) {
    if (state_ != State::InZone || !findToon(avId)) return;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, avId, DistributedToon, teleportGiveup);
    w.u32(avatarId_);
    finish(w);
}

} // namespace tt::net
