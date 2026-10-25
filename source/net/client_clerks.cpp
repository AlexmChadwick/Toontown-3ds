#include "core/log.h"
#include "net/client.h"
#include "net/msg_types.h"
#include "ui/clerk_rules.h"
#include <cstring>
namespace tt::net {
static_assert(sizeof(ClerkMovie) == 56, "clerk FIFO budget");
static_assert(sizeof(ClerkSession) == 100, "clerk session budget");
static_assert(sizeof(ClerkProfile) == 36, "clerk owner-state budget");
namespace {
// PetConstants.PETCLERK_TIMER, KartShopGlobals.KARTCLERK_TIMER;
// FishermanAI/PartyPersonAI.avatarEnter schedule their timeout after 30 seconds.
constexpr uint32_t kPetKartTimeoutMs = 180000;
constexpr uint32_t kFishPartyTimeoutMs = 30000;
constexpr uint32_t kClerkEnterRetryMs = 5000;
// ToontownGlobals JELLYBEAN_FISHING_HOLIDAY / _MONTH; FishGlobals multiplier=2.
constexpr uint32_t kFishingHoliday = 98;
constexpr uint32_t kFishingHolidayMonth = 114;
bool readFishingHolidays(DatagramReader &reader, bool &holiday) {
    const uint16_t bytes = reader.u16();
    if (bytes % 4 || bytes > 1024) {
        reader.fail();
        return false;
    }
    bool found = false;
    for (int index = 0; index < bytes / 4; ++index) {
        const uint32_t id = reader.u32();
        found |= id == kFishingHoliday || id == kFishingHolidayMonth;
    }
    if (!reader.ok())
        return false;
    holiday = found;
    return true;
}
} // namespace

#define TT_NET_UPDATE(writer, id, cls, field)                                                                  \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " must be clsend");              \
    beginUpdate(writer, id, dc::cls::k_##field)
ClerkKind clerkKind(uint16_t id) {
    if (id == dc::DistributedNPCPetclerk::kClassId)
        return ClerkKind::Pet;
    if (id == dc::DistributedNPCKartClerk::kClassId)
        return ClerkKind::Kart;
    if (id == dc::DistributedNPCFisherman::kClassId)
        return ClerkKind::Fisherman;
    if (id == dc::DistributedNPCPartyPerson::kClassId)
        return ClerkKind::Party;
    return ClerkKind::None;
}
void Client::resetClerks(bool owner) {
    clerkSession_ = ClerkSession{};
    clerkHead_ = clerkTail_ = 0;
    if (owner) {
        clerkProfile_ = ClerkProfile{};
        clerkNewsManager_ = 0;
        clerkFishHoliday_ = false;
    }
}
bool Client::pollClerkMovie(ClerkMovie &movie) {
    if (clerkTail_ == clerkHead_)
        return false;
    movie = clerkMovies_[clerkTail_++ % kClerkMovieQueue];
    return true;
}
bool Client::clerksOpen(ClerkKind kind) const {
    const auto &movie = clerkSession_.movie;
    const auto *npc = findNpc(movie.clerk);
    return state_ == State::InZone && npc && clerkKind(npc->classId) == kind && movie.kind == kind &&
           movie.avatar == avatarId_ && avatarId_ && movie.mode == 1 && !clerkSession_.finishing &&
           int32_t(movie.deadline - now_) > 0;
}
bool Client::handleClerksEnter(uint32_t id, uint16_t classId, DatagramReader &reader, const uint16_t *required,
                               uint16_t count, bool other) {
    if (classId != dc::NewsManager::kClassId)
        return false;
    bool holiday = false;
    auto field = [&](uint16_t number) {
        if (number == dc::NewsManager::k_setHolidayIdList)
            readFishingHolidays(reader, holiday);
        else
            skipField(reader, number);
    };
    for (int index = 0; index < count && reader.ok(); ++index)
        field(required[index]);
    if (other) {
        const uint16_t extras = reader.u16();
        for (int index = 0; index < extras && reader.ok(); ++index)
            field(reader.u16());
    }
    if (reader.ok()) {
        clerkNewsManager_ = id;
        clerkFishHoliday_ = holiday;
        ++clerkProfile_.version;
    }
    return true;
}
bool Client::handleClerksField(DatagramReader &reader, uint32_t id, uint16_t field) {
    if (id == clerkNewsManager_ && id && field == dc::NewsManager::k_setHolidayIdList) {
        bool holiday = false;
        if (readFishingHolidays(reader, holiday)) {
            clerkFishHoliday_ = holiday;
            ++clerkProfile_.version;
            TT_LOG("clerks: recv setHolidayIdList fish multiplier %u", holiday ? 2 : 1);
        }
        return true;
    }
    const NpcToon *npc = findNpc(id);
    if (!npc || clerkKind(npc->classId) == ClerkKind::None)
        return false;
    const ClerkKind kind = clerkKind(npc->classId);
    if (!classHasField(*findClass(npc->classId), field))
        return false;
    if (field == dc::DistributedNPCPetclerk::k_setPetSeeds && kind == ClerkKind::Pet) {
        const uint16_t bytes = reader.u16();
        uint32_t seeds[6] = {};
        if (bytes % 4 || bytes > 24) {
            reader.fail();
            return true;
        }
        for (int index = 0; index < bytes / 4; ++index)
            seeds[index] = reader.u32();
        if (!reader.ok())
            return true;
        TT_LOG("clerks: recv setPetSeeds clerk %u count %u", unsigned(id), unsigned(bytes / 4));
        for (int index = 0; index < bytes / 4; ++index) {
            TT_LOG("clerks: setPetSeeds[%d] %u", index, unsigned(seeds[index]));
        }
        if (clerkSession_.pending == id || clerkSession_.movie.clerk == id) {
            std::memcpy(clerkSession_.seeds, seeds, sizeof(seeds));
            clerkSession_.seedCount = bytes / 4;
            clerkSession_.seedsReceived = true;
        }
        return true;
    }
    if (field == dc::DistributedNPCToonBase::k_freeAvatar) {
        if (clerkSession_.pending == id || clerkSession_.movie.clerk == id) {
            ClerkMovie movie = clerkSession_.movie;
            movie.clerk = id;
            movie.kind = kind;
            movie.mode = 255;
            if (clerkHead_ - clerkTail_ < kClerkMovieQueue)
                clerkMovies_[clerkHead_++ % kClerkMovieQueue] = movie;
            clerkSession_ = ClerkSession{};
            TT_LOG("clerks: recv freeAvatar clerk %u", unsigned(id));
        }
        return true;
    }
    const uint16_t expected = kind == ClerkKind::Pet         ? dc::DistributedNPCPetclerk::k_setMovie
                              : kind == ClerkKind::Kart      ? dc::DistributedNPCKartClerk::k_setMovie
                              : kind == ClerkKind::Fisherman ? dc::DistributedNPCFisherman::k_setMovie
                                                             : dc::DistributedNPCPartyPerson::k_setMovie;
    if (field != expected)
        return false;
    ClerkMovie movie;
    movie.clerk = id;
    movie.kind = kind;
    movie.mode = reader.u8();
    movie.npc = reader.u32();
    movie.avatar = reader.u32();
    const uint16_t bytes = reader.u16();
    if (bytes % 4 || bytes > 32) {
        reader.fail();
        return true;
    }
    movie.numExtra = bytes / 4;
    for (int index = 0; index < movie.numExtra; ++index)
        movie.extra[index] = reader.u32();
    const int16_t stamp = reader.i16();
    if (!reader.ok())
        return true;
    TT_LOG("clerks: recv setMovie kind %u clerk %u mode %u npc %u av %u args %u [%u,%u]", unsigned(kind),
           unsigned(id), unsigned(movie.mode), unsigned(movie.npc), unsigned(movie.avatar),
           unsigned(movie.numExtra), unsigned(movie.extra[0]), unsigned(movie.extra[1]));
    for (int index = 0; index < movie.numExtra; ++index) {
        TT_LOG("clerks: setMovie extra[%d] %u", index, unsigned(movie.extra[index]));
    }
    TT_LOG("clerks: setMovie timestamp %d", int(stamp));
    const bool ours = movie.avatar == avatarId_ && avatarId_;
    if ((!ours && movie.mode != 0) || (movie.mode == 0 && clerkSession_.movie.clerk != id))
        return true;
    if (movie.mode == 1 && clerkSession_.pending != id && clerkSession_.movie.clerk != id)
        return true;
    const uint32_t window =
        (kind == ClerkKind::Pet || kind == ClerkKind::Kart) ? kPetKartTimeoutMs : kFishPartyTimeoutMs;
    uint32_t start = now_;
    if (timeSynced_) {
        const uint32_t local = networkToLocal(stamp, now_);
        if (int32_t(now_ - local) >= 0)
            start = local;
    }
    movie.deadline = start + window;
    if (clerkHead_ - clerkTail_ >= kClerkMovieQueue) {
        TT_LOG("clerks: movie queue overflow; close session");
        clerkSession_ = ClerkSession{};
        return true;
    }
    clerkMovies_[clerkHead_++ % kClerkMovieQueue] = movie;
    clerkSession_.movie = movie;
    clerkSession_.pending = 0;
    return true;
}
bool Client::clerksOwnField(DatagramReader &reader, uint16_t field) {
    if (field == dc::DistributedToon::k_setPetId) {
        const uint32_t pet = reader.u32();
        if (reader.ok()) {
            TT_LOG("clerks: owner setPetId %u", unsigned(pet));
            clerkProfile_.petId = pet;
            ++clerkProfile_.version;
        }
        return true;
    }
    if (field == dc::DistributedToon::k_setKartAccessoriesOwned) {
        int8_t owned[16];
        for (auto &entry : owned)
            entry = reader.i8();
        if (!reader.ok())
            return true;
        for (int8_t entry : owned)
            if (entry < -1 || entry >= 92) {
                reader.fail();
                return true;
            }
        TT_LOG("clerks: owner setKartAccessoriesOwned");
        std::memcpy(clerkProfile_.accessories, owned, sizeof(owned));
        for (int8_t entry : owned)
            if (entry == clerkSession_.pendingAccessory)
                clerkSession_.pendingAccessory = -1;
        ++clerkProfile_.version;
        return true;
    }
    const uint16_t fields[] = {
        dc::DistributedToon::k_setKartBodyType,          dc::DistributedToon::k_setKartBodyColor,
        dc::DistributedToon::k_setKartAccessoryColor,    dc::DistributedToon::k_setKartEngineBlockType,
        dc::DistributedToon::k_setKartSpoilerType,       dc::DistributedToon::k_setKartFrontWheelWellType,
        dc::DistributedToon::k_setKartBackWheelWellType, dc::DistributedToon::k_setKartRimType,
        dc::DistributedToon::k_setKartDecalType};
    for (int index = 0; index < 9; ++index) {
        if (field != fields[index])
            continue;
        const int value = reader.i8();
        if (!reader.ok())
            return true;
        if (value < -1 || value >= (index == 0 ? 3 : 92)) {
            reader.fail();
            return true;
        }
        TT_LOG("clerks: owner kart field %d value %d", index, value);
        clerkProfile_.kart[index] = value;
        if (index == 0 && value == clerkSession_.pendingBody)
            clerkSession_.pendingBody = -1;
        ++clerkProfile_.version;
        return true;
    }
    return false;
}
bool Client::sendServiceClerkEnter(uint32_t id) {
    const auto *npc = findNpc(id);
    if (state_ != State::InZone || !npc || clerkKind(npc->classId) == ClerkKind::None)
        return false;
    if (clerkSession_.pending && now_ - clerkSession_.pendingMs < kClerkEnterRetryMs)
        return false;
    if (clerkSession_.movie.mode == 1 && int32_t(clerkSession_.movie.deadline - now_) > 0)
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, id, DistributedNPCToonBase, avatarEnter);
    if (!finish(writer))
        return false;
    clerkSession_ = ClerkSession{};
    clerkSession_.pending = id;
    clerkSession_.pendingMs = now_;
    TT_LOG("clerks: send avatarEnter kind %u clerk %u", unsigned(clerkKind(npc->classId)), unsigned(id));
    return true;
}
int Client::clerkFishTotal() const {
    int total = 0;
    for (int index = 0; index < ownFish_.tankCount && index < kMaxFishTank; ++index)
        total += clerks::fishValue(ownFish_.genus[index], ownFish_.species[index], ownFish_.weight[index]);
    return total * (clerkFishHoliday_ ? 2 : 1);
}
bool Client::sendPetAdopt(uint8_t index, uint32_t name) {
    if (!clerksOpen(ClerkKind::Pet) || clerkSession_.petAction || index >= clerkSession_.seedCount ||
        name >= clerks::kPetNames)
        return false;
    clerks::PetOffer offer;
    if (!clerks::generatePet(clerkSession_.seeds[index], zoneId() / 1000 * 1000, &offer) ||
        offer.cost > int(ownStats_.money) + ownStats_.bankMoney)
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, clerkSession_.movie.clerk, DistributedNPCPetclerk, petAdopted);
    writer.u8(index);
    writer.u32(name);
    if (!finish(writer))
        return false;
    clerkSession_.petAction = true;
    TT_LOG("clerks: send petAdopted index %u name %u", unsigned(index), unsigned(name));
    return true;
}
bool Client::sendPetReturn() {
    // Empty seeds are this pin's stub PetManagerAI: its delete/create methods do
    // not exist. Do not send a return to that unsupported service.
    if (!clerksOpen(ClerkKind::Pet) || clerkSession_.petAction || !clerkProfile_.petId ||
        !clerkSession_.seedCount)
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, clerkSession_.movie.clerk, DistributedNPCPetclerk, petReturned);
    if (!finish(writer))
        return false;
    clerkSession_.petAction = true;
    TT_LOG("clerks: send petReturned");
    return true;
}
bool Client::sendServiceFishSale(bool sell) {
    const auto kind = clerkSession_.movie.kind;
    if ((kind == ClerkKind::Pet && clerkSession_.petAction) || !clerksOpen(kind) ||
        (kind != ClerkKind::Pet && kind != ClerkKind::Fisherman) || (sell && clerkFishTotal() <= 0))
        return false;
    if (kind == ClerkKind::Pet && !sell)
        return sendServiceClerkDone();
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (kind == ClerkKind::Pet) {
        TT_NET_UPDATE(writer, clerkSession_.movie.clerk, DistributedNPCPetclerk, fishSold);
    } else {
        TT_NET_UPDATE(writer, clerkSession_.movie.clerk, DistributedNPCFisherman, completeSale);
        writer.u8(sell ? 1 : 0);
    }
    if (!finish(writer))
        return false;
    clerkSession_.finishing = true;
    TT_LOG("clerks: send %s sell %u total %d", kind == ClerkKind::Pet ? "fishSold" : "completeSale",
           sell ? 1 : 0, clerkFishTotal());
    return true;
}
bool Client::sendClerkKartBody(uint8_t body) {
    if (!clerksOpen(ClerkKind::Kart) || clerkSession_.pendingBody >= 0 || clerkSession_.pendingAccessory >= 0 ||
        !clerks::canBuyKart(body, clerkProfile_.kart[0], ownBook_.tickets))
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, clerkSession_.movie.clerk, DistributedNPCKartClerk, buyKart);
    writer.u8(body);
    if (!finish(writer))
        return false;
    clerkSession_.pendingBody = body;
    TT_LOG("clerks: send buyKart body %u tickets %u", unsigned(body), unsigned(ownBook_.tickets));
    return true;
}
bool Client::sendClerkKartAccessory(uint8_t accessory) {
    if (!clerksOpen(ClerkKind::Kart) || clerkSession_.pendingBody >= 0 || clerkSession_.pendingAccessory >= 0 ||
        !clerks::canBuyAccessory(accessory, clerkProfile_.kart[0], clerkProfile_.accessories, ownBook_.tickets))
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, clerkSession_.movie.clerk, DistributedNPCKartClerk, buyAccessory);
    writer.u8(accessory);
    if (!finish(writer))
        return false;
    clerkSession_.pendingAccessory = accessory;
    TT_LOG("clerks: send buyAccessory id %u tickets %u", unsigned(accessory), unsigned(ownBook_.tickets));
    return true;
}
bool Client::sendServiceClerkDone() {
    const auto kind = clerkSession_.movie.kind;
    if (!clerksOpen(kind))
        return false;
    if (kind == ClerkKind::Fisherman)
        return sendServiceFishSale(false);
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (kind == ClerkKind::Pet) {
        TT_NET_UPDATE(writer, clerkSession_.movie.clerk, DistributedNPCPetclerk, transactionDone);
    } else if (kind == ClerkKind::Kart) {
        TT_NET_UPDATE(writer, clerkSession_.movie.clerk, DistributedNPCKartClerk, transactionDone);
    } else if (kind == ClerkKind::Party) {
        // This server disables party purchases. If another server offers START,
        // decline its planner rather than allocate an unsupported party zone.
        TT_NET_UPDATE(writer, clerkSession_.movie.clerk, DistributedNPCPartyPerson, answer);
        writer.u8(0);
    } else
        return false;
    if (!finish(writer))
        return false;
    clerkSession_.finishing = true;
    TT_LOG("clerks: send %s clerk %u", kind == ClerkKind::Party ? "answer(0)" : "transactionDone",
           unsigned(clerkSession_.movie.clerk));
    return true;
}
} // namespace tt::net
