#include "quest/quest_talk.h"

#include <cstdio>
#include <cstring>

namespace tt::quest {

namespace {

// TTLocalizerEnglish.py:577-580.
constexpr const char* kQuestChoiceCancel = "Come back later if you need a ToonTask! Bye!";
constexpr const char* kTrackChoiceCancel = "Come back when you are ready to decide! Bye!";
constexpr const char* kQuestChoice = "Choose a ToonTask.";
constexpr const char* kTrackChoice = "Ready to decide? Choose a track, or come back later.";

// Appends `s` (and a page break \x07 before it when `out` is not empty) to out, truncating.
void appendPage(char* out, int cap, const char* s) {
    if (!s || !*s) return;
    int n = static_cast<int>(strlen(out));
    if (n && n < cap - 1) out[n++] = '\x07';
    while (*s && n < cap - 1) out[n++] = *s++;
    out[n] = 0;
}

} // namespace

void Talk::setLocal(uint32_t avId, const char* name) {
    avId_ = avId;
    snprintf(avName_, sizeof(avName_), "%s", name ? name : "");
}

void Talk::setPhase(Phase p, float timer) {
    phase_ = p;
    timer_ = timer;
}

void Talk::post(TalkAction::Kind kind, int value) {
    if (actionCount_ >= kMaxActions) return;
    actions_[(actionHead_ + actionCount_) % kMaxActions] = TalkAction{kind, npcDoId_, value};
    actionCount_++;
}

bool Talk::popAction(TalkAction& out) {
    if (!actionCount_) return false;
    out = actions_[actionHead_];
    actionHead_ = (actionHead_ + 1) % kMaxActions;
    actionCount_--;
    return true;
}

bool Talk::begin(uint32_t npcDoId) {
    if (phase_ != Phase::Idle) return false;
    npcDoId_ = npcDoId;
    npcId_ = 0;
    numPages_ = page_ = 0;
    numChoices_ = numTracks_ = 0;
    setPhase(Phase::Waiting, kWaitTimeoutSec);
    post(TalkAction::AvatarEnter, 0);
    return true;
}

void Talk::reset() {
    phase_ = Phase::Idle;
    npcDoId_ = 0;
    numPages_ = page_ = 0;
    actionCount_ = 0;
    lineAge_ = 99.0f;
}

void Talk::buildPages(const char* text) {
    snprintf(text_, sizeof(text_), "%s", text ? text : "");
    numPages_ = 0;
    page_ = 0;
    // setPageChat: pages are separated by \x07; a page longer than a balloon is split further.
    char* p = text_;
    while (*p && numPages_ < kMaxPages) {
        pages_[numPages_++] = p;
        char* end = strchr(p, '\x07');
        if (end) *end = 0;
        // Split overlong pages at a space.
        while (static_cast<int>(strlen(pages_[numPages_ - 1])) > kPageChars && numPages_ < kMaxPages) {
            char* s = text_ + (pages_[numPages_ - 1] - text_); // points into text_
            char* cut = s + kPageChars;
            while (cut > s && *cut != ' ') --cut;
            if (cut == s) break;
            *cut = 0;
            pages_[numPages_++] = cut + 1;
        }
        if (!end) break;
        p = end + 1;
    }
}

void Talk::say(const char* text, bool toPages) {
    if (toPages) {
        buildPages(text);
        setPhase(Phase::Pages, 0.0f);
    } else {
        snprintf(line_, sizeof(line_), "%s", text);
        lineNpc_ = npcDoId_;
        lineAge_ = 0.0f;
    }
}

void Talk::onMovie(const net::NpcMovie& m) {
    const bool mine = m.avId == avId_ && avId_ != 0;
    if (m.mode == net::kMovieClear) return;
    if (m.mode == net::kMovieTimeout) {
        if (mine && m.npcDoId == npcDoId_) reset();
        return;
    }
    if (!mine) return; // other toons' conversations: upstream shows them over that NPC; skipped
    if (m.npcDoId != npcDoId_ && phase_ != Phase::Idle) return;
    npcDoId_ = m.npcDoId;
    npcId_ = static_cast<uint16_t>(m.npcId);
    char text[kMaxText] = {};
    char tmp[kMaxText];
    uint16_t toNpc = 0;
    switch (m.mode) {
    case net::kMovieReject:
    case net::kMovieTierNotDone: {
        defaultDialog(m.mode == net::kMovieReject ? kReject : kTierNotDone, pick(), tmp, sizeof(tmp));
        fillInNames(tmp, avName_, npcId_, 0, text, sizeof(text));
        say(text, false);
        phase_ = Phase::Idle; // the toon is free at once
        return;
    }
    case net::kMovieComplete: {
        if (m.numValues < 3) return;
        const uint16_t questId = m.values[0], rewardId = m.values[1];
        toNpc = m.values[2];
        if (chooseDialog(questId, kGreeting, pick(), tmp, sizeof(tmp))) appendPage(text, sizeof(text), tmp);
        chooseDialog(questId, kComplete, pick(), tmp, sizeof(tmp));
        appendPage(text, sizeof(text), tmp);
        if (rewardId && rewardText(rewardId, false, tmp, sizeof(tmp))) appendPage(text, sizeof(text), tmp);
        if (chooseDialog(questId, kLeaving, pick(), tmp, sizeof(tmp))) appendPage(text, sizeof(text), tmp);
        break;
    }
    case net::kMovieIncomplete: {
        if (m.numValues < 3) return;
        const uint16_t questId = m.values[0];
        const int status = m.values[1];
        toNpc = m.values[2];
        if (chooseDialog(questId, kGreeting, pick(), tmp, sizeof(tmp))) appendPage(text, sizeof(text), tmp);
        chooseDialog(questId, status, pick(), tmp, sizeof(tmp));
        appendPage(text, sizeof(text), tmp);
        if (chooseDialog(questId, kLeaving, pick(), tmp, sizeof(tmp))) appendPage(text, sizeof(text), tmp);
        break;
    }
    case net::kMovieAssign: {
        if (m.numValues < 3) return;
        const uint16_t questId = m.values[0];
        toNpc = m.values[2];
        chooseDialog(questId, kQuest, pick(), tmp, sizeof(tmp));
        appendPage(text, sizeof(text), tmp);
        if (chooseDialog(questId, kLeaving, pick(), tmp, sizeof(tmp))) appendPage(text, sizeof(text), tmp);
        break;
    }
    case net::kMovieQuestChoiceCancel:
        snprintf(text, sizeof(text), "%s", kQuestChoiceCancel);
        break;
    case net::kMovieTrackChoiceCancel:
        snprintf(text, sizeof(text), "%s", kTrackChoiceCancel);
        break;
    case net::kMovieQuestChoice:
        numChoices_ = 0;
        for (int i = 0; i + 2 < m.numValues && numChoices_ < kMaxChoices; i += 3) {
            choices_[numChoices_++] = Choice{m.values[i], m.values[i + 1], m.values[i + 2]};
        }
        say(kQuestChoice, false);
        setPhase(Phase::QuestChoice, kChoiceTimeoutSec);
        return;
    case net::kMovieTrackChoice:
        numTracks_ = 0;
        for (int i = 0; i < m.numValues && numTracks_ < kMaxTracks; i++) tracks_[numTracks_++] = static_cast<int8_t>(m.values[i]);
        say(kTrackChoice, false);
        setPhase(Phase::TrackChoice, kChoiceTimeoutSec);
        return;
    case net::kMovieBusy:
    default:
        return;
    }
    fillInNames(text, avName_, npcId_, toNpc, tmp, sizeof(tmp));
    say(tmp, true);
}

void Talk::onFree(uint32_t npcDoId) {
    if (npcDoId == npcDoId_ && phase_ != Phase::Pages) reset();
}

void Talk::onNpcGone(uint32_t npcDoId) {
    if (npcDoId == npcDoId_) reset();
}

void Talk::update(float dt) {
    lineAge_ += dt;
    if (timer_ > 0.0f) {
        timer_ -= dt;
        if (timer_ <= 0.0f) {
            if (phase_ == Phase::QuestChoice) chooseQuest(-1);
            else if (phase_ == Phase::TrackChoice) chooseTrack(-1);
            else if (phase_ == Phase::Waiting) reset(); // the NPC never answered
        }
    }
}

void Talk::advance() {
    if (phase_ != Phase::Pages) return;
    if (page_ + 1 < numPages_) {
        page_++;
        return;
    }
    post(TalkAction::MovieDone, 0); // DistributedNPCToon.finishMovie: setMovieDone
    phase_ = Phase::Idle;
    numPages_ = 0;
}

void Talk::chooseQuest(int index) {
    if (phase_ != Phase::QuestChoice) return;
    post(TalkAction::ChooseQuest, index >= 0 && index < numChoices_ ? choices_[index].questId : 0);
    setPhase(Phase::Waiting, kWaitTimeoutSec);
}

void Talk::chooseTrack(int index) {
    if (phase_ != Phase::TrackChoice) return;
    post(TalkAction::ChooseTrack, index >= 0 && index < numTracks_ ? tracks_[index] : -1);
    setPhase(Phase::Waiting, kWaitTimeoutSec);
}

const char* Talk::npcName() const {
    const char* n = quest::npcName(npcId_);
    return n ? n : "";
}

const char* Talk::speech(uint32_t* npcDoIdOut) const {
    if (phase_ == Phase::Pages && page_ < numPages_) {
        *npcDoIdOut = npcDoId_;
        return pages_[page_];
    }
    if (lineAge_ < kSpeechHoldSec && line_[0]) {
        *npcDoIdOut = lineNpc_;
        return line_;
    }
    return nullptr;
}

} // namespace tt::quest
