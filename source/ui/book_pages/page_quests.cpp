#include "ui/book_pages/page_quests.h"

#include <cstdio>
#include <cstring>

#include "net/quest_net.h"
#include "quest/quest_data.h"
#include "quest/quest_text.h"
#include "ui/quest_poster_art.h"
#include "ui/suit_heads_atlas.h"
#include "ui/theme.h"
#include "world/battle_fx.h"

namespace tt::ui {

namespace {

// QuestPoster.colors: the card is white, lightGreen once complete; the picture frames take the
// quest type's colour.
constexpr u32 kCardWhite = theme::rgb(0xFF, 0xFF, 0xFF);
constexpr u32 kCardDone = theme::rgb(0xC8, 0xFF, 0xDC);   // lightGreen (0.784, 1, 0.863)
constexpr u32 kFrameBlue = theme::rgb(0x73, 0x73, 0xCC);  // blue (0.45, 0.45, 0.8)
constexpr u32 kFrameLightBlue = theme::rgb(0x6B, 0xAB, 0xFF); // lightBlue (0.42, 0.671, 1)
constexpr u32 kFrameGreen = theme::rgb(0x73, 0xCC, 0x73); // green (0.45, 0.8, 0.45)
constexpr u32 kFrameRed = theme::rgb(0xCC, 0x73, 0x73);   // red (0.8, 0.45, 0.45)
constexpr u32 kFrameBrown = theme::rgb(0x85, 0x6B, 0x38); // brown (0.52, 0.42, 0.22)
constexpr u32 kNormalInk = theme::rgb(0x4C, 0x40, 0x33);  // normalTextColor (0.3, 0.25, 0.2)
constexpr u32 kDoneInk = theme::rgb(0x00, 0x4C, 0x00);    // textColor once complete (0, 0.3, 0)
constexpr u32 kRewardRed = theme::rgb(0xCC, 0x4C, 0x4C);  // rewardRed (0.8, 0.3, 0.3)
// questProgress, a DirectWaitBar: frameColor, barColor and its text colour.
constexpr u32 kBarFrame = theme::rgb(0xF1, 0xDF, 0xB4);
constexpr u32 kBarEdge = theme::rgb(0xA8, 0x94, 0x6C);
constexpr u32 kBarFill = theme::rgb(0x80, 0xB3, 0x80);
constexpr u32 kBarInk = theme::rgb(0x0D, 0x24, 0x66);
// Suit.medallionColors for c, l, m, s (CogTrackQuest's department icon).
constexpr u32 kMedallion[4] = {theme::rgb(0xDC, 0xC6, 0xC4), theme::rgb(0xBF, 0xC6, 0xD2),
                               theme::rgb(0xBF, 0xC4, 0xBF), theme::rgb(0xD7, 0xBE, 0xBE)};

// "Building\nStreet\nHood" as one line ("Building, Street, Hood") into out.
void flatten(const char* in, char* out, size_t cap) {
    size_t n = 0;
    for (const char* c = in; *c && n + 3 < cap; ++c) {
        if (*c != '\n') {
            out[n++] = *c;
        } else if (n > 0 && c[1]) {
            out[n++] = ',';
            out[n++] = ' ';
        }
    }
    out[n] = 0;
}

// QuestPoster's toNpcName (ToonHQ and the tailor have generic names).
const char* returnToName(uint32_t npc) {
    if (npc == quest::kToonHQ) return "HQ Officer";
    if (npc == quest::kToonTailor) return "Tailor";
    const char* n = quest::npcName(static_cast<uint16_t>(npc));
    return n ? n : "a Toon";
}

// What QuestPoster.update puts in a picture frame.
struct Icon {
    enum Kind { None, Art, CogHead, Npc, Gag } kind = None;
    QuestArt art = QuestArt::Cog;
    u32 tint = theme::kWhite;
    int a = 0, b = 0; // CogHead: suit head index; Npc: npc id; Gag: track, level
};

int suitHeadOf(int cogIndex) {
    if (cogIndex < 0 || uint32_t(cogIndex) >= quest::kNumCogs) return -1;
    const char* code = quest::kCogs[cogIndex].code;
    for (int i = 0; i < kNumSuitHeads; i++) {
        if (!strcmp(kSuitHeadSubTex[i].code, code)) return i;
    }
    return -1;
}

// The quest's own icon and frame colour (the left frame; the NPC goes right once complete).
Icon questIcon(const quest::QuestDef& d, u32* frame) {
    Icon ic;
    ic.kind = Icon::Art;
    *frame = kFrameBlue;
    switch (d.type) {
    case quest::QuestType::Cog:
    case quest::QuestType::CogNewbie: {
        const int head = d.p[2] >= 0 ? suitHeadOf(d.p[2]) : -1;
        if (head >= 0) {
            ic.kind = Icon::CogHead;
            ic.a = head;
        }
        break;
    }
    case quest::QuestType::CogLevel: break;
    case quest::QuestType::CogTrack: {
        // p[2]: 0..3 = c l m s, the medallion tinted with the department colour.
        static constexpr QuestArt kDept[4] = {QuestArt::CorpIcon, QuestArt::LegalIcon, QuestArt::MoneyIcon,
                                              QuestArt::SalesIcon};
        const int t = d.p[2] >= 0 && d.p[2] < 4 ? d.p[2] : 0;
        ic.art = kDept[t];
        ic.tint = kMedallion[t];
        break;
    }
    case quest::QuestType::Building:
    case quest::QuestType::BuildingNewbie: ic.art = QuestArt::Building; break;
    case quest::QuestType::Visit:
        ic.kind = Icon::Npc;
        ic.a = int(d.toNpc);
        *frame = kFrameBrown;
        break;
    case quest::QuestType::DeliverItem:
        ic.art = QuestArt::Package;
        *frame = kFrameRed;
        break;
    case quest::QuestType::DeliverGag:
        ic.kind = Icon::Gag;
        ic.a = d.p[1];
        ic.b = d.p[2];
        *frame = kFrameRed;
        break;
    case quest::QuestType::RecoverItem:
        ic.art = QuestArt::Package;
        *frame = kFrameGreen;
        break;
    case quest::QuestType::TrackChoice:
        ic.kind = Icon::Gag;
        ic.a = d.p[0];
        ic.b = 1;
        *frame = kFrameGreen;
        break;
    case quest::QuestType::Friend:
        ic.art = QuestArt::Friends;
        *frame = kFrameBrown;
        break;
    case quest::QuestType::Trolley:
        ic.art = QuestArt::Trolley;
        *frame = kFrameLightBlue;
        break;
    case quest::QuestType::Phone:
        ic.art = QuestArt::Phone;
        *frame = kFrameLightBlue;
        break;
    case quest::QuestType::Mailbox:
        ic.art = QuestArt::Package;
        *frame = kFrameLightBlue;
        break;
    default: ic.kind = Icon::None; break;
    }
    return ic;
}

// questPictureFrame (s x s at x, y) in `frame`'s colour with the icon inside.
void drawFrame(const Icon& ic, u32 frame, float x, float y, float s) {
    if (ic.kind == Icon::None) return;
    drawQuestArt(QuestArt::Frame, x, y, s, s, frame);
    const float in = s * 0.14f, is = s - 2 * in;
    switch (ic.kind) {
    case Icon::Art: drawQuestArt(ic.art, x + in, y + in, is, is, ic.tint, 0.52f); break;
    case Icon::CogHead:
        SuitHeadAtlas::instance().init();
        SuitHeadAtlas::instance().drawHead(ic.a / 8, ic.a % 8, x + in, y + in, is, 0.52f);
        break;
    case Icon::Npc:
        if (!drawQuestNpcHead(uint32_t(ic.a), x + in, y + in, is, 0.52f))
            drawQuestArt(QuestArt::Cog, x + in, y + in, is, is, kFrameBrown, 0.52f);
        break;
    case Icon::Gag:
        world::GagAtlas::instance().init();
        world::GagAtlas::instance().drawIcon(ic.a, ic.b, x + in, y + in, is, 0.52f);
        break;
    default: break;
    }
}

} // namespace

// QuestPoster cards for the ToonTasks we carry, stacked in (x, y, w, h); shared by the book's
// ToonTasks page and the Toon tab. Each is the questCard (lightGreen once complete) with the
// headline across the top, the quest's picture frame (and the NPC to return to once complete)
// on the left, the objective, questProgress bar, location and reward beside them.
int drawQuestPosters(PageText& text, const net::OwnQuests& q, float x, float y, float w, float h, int maxShown) {
    const int shown = q.count < maxShown ? q.count : maxShown;
    if (shown <= 0) return 0;
    const float gap = 4.0f;
    float cardH = (h - (shown - 1) * gap) / shown;
    if (cardH > 104.0f) cardH = 104.0f;
    char line[quest::kMaxText];
    for (int i = 0; i < shown; i++) {
        const net::QuestEntry& e = q.quests[i];
        const float cy = y + i * (cardH + gap), cx = x + 2, cw = w - 4;
        const quest::QuestDef* def = quest::findQuest(static_cast<uint16_t>(e.questId));
        const bool done = def && quest::isComplete(*def, e.progress);
        if (!drawQuestArt(QuestArt::Card, cx, cy, cw, cardH, done ? kCardDone : kCardWhite)) {
            theme::roundRect(cx, cy, cw, cardH, 6, theme::kPaperEdge);
            theme::roundRect(cx + 2, cy + 2, cw - 4, cardH - 4, 5, theme::kPaper);
        }
        const u32 ink = done ? kDoneInk : kNormalInk;
        // The parchment's rolled ends take about 6% of its width on each side.
        const float px = cx + cw * 0.06f, pw = cw * 0.88f;
        if (!def) {
            text.draw(px + 4, cy + 8, theme::kTextBody, ink, "ToonTask %u", unsigned(e.questId));
            continue;
        }
        float ly = cy + cardH * 0.08f;
        const float bottom = cy + cardH * 0.92f;
        quest::headline(*def, done, line, sizeof line);
        text.centered(cx + cw * 0.5f, ly, theme::kTextBody, ink, pw, "%s", line);
        ly += 15;

        // Picture frames: the quest's icon, then (complete) the NPC to return to.
        u32 frameColor = kFrameBlue;
        Icon left = questIcon(*def, &frameColor);
        Icon right;
        if (done && e.toNpc != quest::kAny) {
            right.kind = Icon::Npc;
            right.a = int(e.toNpc);
        }
        float fs = bottom - ly;
        if (fs > 58.0f) fs = 58.0f;
        const float fy = ly + (bottom - ly - fs) * 0.5f;
        float tx = px + 2;
        if (fs >= 24.0f) {
            if (left.kind != Icon::None) {
                drawFrame(left, frameColor, tx, fy, fs);
                tx += fs + 4;
            }
            if (right.kind != Icon::None) {
                drawFrame(right, frameColor, tx, fy, fs);
                tx += fs + 4;
            }
        }
        tx += 2;
        const float tw = px + pw - tx;

        // The objective, "Return to: <NPC>" once complete, then the progress bar, where to go
        // and the reward, as room allows.
        const bool bar = !done && quest::numQuestItems(*def) > 0 &&
                         quest::progressString(*def, e.progress, line, sizeof line) > 0;
        char progress[quest::kMaxText];
        if (bar) memcpy(progress, line, sizeof progress);
        const float barH = bar ? 13.0f : 0.0f;
        const float textBottom = bottom - barH;
        // Lines that fit: the objective (two at most), then the return / location / reward
        // lines, centred as a block beside the frame like the poster's questInfo.
        quest::objective(*def, line, sizeof line);
        if (def->type == quest::QuestType::Visit) snprintf(line, sizeof line, "%s", returnToName(e.toNpc)); // captions[0]
        const int room = int((textBottom - ly) / 12.5f);
        const int objLines = room >= 3 ? 2 : 1;
        {
            char info[quest::kMaxText];
            int lines = objLines > 1 && text.wrapLines(theme::kTextSmall, tw, 2, line) > 1 ? 2 : 1;
            if (done) lines++;
            if (quest::posterInfo(*def, static_cast<uint16_t>(e.toNpc), done, info, sizeof info) > 0) lines++;
            if (quest::rewardText(static_cast<uint16_t>(e.reward), true, info, sizeof info) > 0 && info[0]) lines++;
            const float block = lines * 12.5f;
            if (block < textBottom - ly) ly += (textBottom - ly - block) * 0.5f;
        }
        ly += 12.5f * text.wrap(tx, ly, theme::kTextSmall, ink, tw, 12.5f, objLines, line);
        if (done && ly + 12 <= textBottom) {
            text.fit(tx, ly, theme::kTextSmall, kDoneInk, tw, "Return to: %s", returnToName(e.toNpc));
            ly += 12.5f;
        }
        if (ly + 12 <= textBottom && quest::posterInfo(*def, static_cast<uint16_t>(e.toNpc), done, line, sizeof line) > 0) {
            char flat[160];
            flatten(line, flat, sizeof flat);
            text.fit(tx, ly, theme::kTextTiny, kNormalInk, tw, "%s", flat);
            ly += 12.0f;
        }
        if (ly + 12 <= textBottom && quest::rewardText(static_cast<uint16_t>(e.reward), true, line, sizeof line) > 0 &&
            line[0]) {
            text.fit(tx, ly, theme::kTextTiny, kRewardRed, tw, "%s", line); // poster strings carry "Reward:"
        }
        if (bar) {
            // DirectWaitBar (relief SUNKEN): the cream frame, the green fill, the count over it.
            const int need = quest::numQuestItems(*def);
            uint32_t have = def->type == quest::QuestType::RecoverItem ? (e.progress & 0xFFFFu) : e.progress;
            if (have > uint32_t(need)) have = uint32_t(need);
            const float by = bottom - barH + 1, bw = tw;
            theme::roundRect(tx, by, bw, barH - 1, 2, kBarEdge, 0.52f);
            theme::roundRect(tx + 1, by + 1, bw - 2, barH - 3, 2, kBarFrame, 0.53f);
            if (have > 0) theme::roundRect(tx + 1, by + 1, (bw - 2) * have / need, barH - 3, 2, kBarFill, 0.54f);
            text.centered(tx + bw * 0.5f, by + 1, theme::kTextTiny, kBarInk, bw - 4, "%s", progress);
        }
    }
    return shown;
}

void PageQuests::init() { text_.init(2048); }

void PageQuests::shutdown() { text_.shutdown(); }

// QuestPage: one poster per carried ToonTask (headline, objective, where to go, progress and
// the reward), as many as the carry limit allows.
void PageQuests::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    const int count = bdc.quests ? bdc.quests->count : 0;
    const int limit = bdc.quests ? bdc.quests->carryLimit : 1;
    text_.draw(x + 8, y + 3, theme::kTextBody, theme::kInk, "ToonTasks  %d / %d", count, limit);
    if (bdc.quests) {
        text_.right(x + w - 8, y + 4, theme::kTextSmall, theme::kInkSoft, 0, "Tasks done: %u",
                    unsigned(bdc.quests->historyCount));
    }
    if (count == 0) {
        theme::paper(x + 20, y + 40, w - 40, 84, 8.0f);
        text_.centered(x + w * 0.5f, y + 54, theme::kTextTitle, theme::kRedDark, w - 60, "No ToonTasks yet!");
        text_.centered(x + w * 0.5f, y + 80, theme::kTextSmall, theme::kInk, w - 56,
                       "Visit an HQ Officer in any Toon HQ");
        text_.centered(x + w * 0.5f, y + 96, theme::kTextSmall, theme::kInk, w - 56, "to take on a ToonTask.");
        return;
    }
    const int shown = drawQuestPosters(text_, *bdc.quests, x, y + 22, w, h - 24, 4);
    if (count > shown) {
        text_.right(x + w - 8, y + h - 12, theme::kTextTiny, theme::kInkSoft, 0, "+%d more", count - shown);
    }
}


} // namespace tt::ui
