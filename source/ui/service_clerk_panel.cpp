#include "render/ui_font.h"
#include "ui/service_clerk_panel.h"
#include "core/log.h"
#include "ui/clerk_tables.h"
#include "ui/purchase_art.h"
#include "ui/theme.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace tt::ui {
bool ServiceClerkPanel::init() {
    buffer_ = C2D_TextBufNew(2048);
    return buffer_ != nullptr;
}
void ServiceClerkPanel::shutdown() {
    if (buffer_)
        C2D_TextBufDelete(buffer_);
    buffer_ = nullptr;
    active_ = false;
}
void ServiceClerkPanel::result(const char *message) {
    std::snprintf(message_, sizeof(message_), "%s", message);
    page_ = Page::Result;
    resultUntil_ = now_ + 5000;
    active_ = dirty_ = true;
}
void ServiceClerkPanel::update(net::Client &client, uint32_t now) {
    client_ = &client;
    now_ = now;
    net::ClerkMovie movie;
    while (client.pollClerkMovie(movie)) {
        if (movie.mode == 0) {
            if (clerk_ == movie.clerk && page_ != Page::Result)
                active_ = false;
            continue;
        }
        clerk_ = movie.clerk;
        kind_ = movie.kind;
        deadline_ = movie.deadline;
        if (movie.mode == 1) {
            active_ = true;
            page_ = Page::Menu;
            selection_ = offer_ = body_ = accessory_ = 0;
            diagnosticDone_ = false;
            message_[0] = 0;
            dirty_ = true;
        } else if (movie.mode == 255)
            result("The clerk is busy. Please try again.");
        else if (movie.mode == 8)
            result("Need more time to think? Your session expired.");
        else if (kind_ == net::ClerkKind::Party) {
            switch (movie.mode) {
            case 6:
                result("Parties are not available yet.");
                break;
            case 7:
                result("You need more jellybeans to plan a party.");
                break;
            case 3:
                result("You are already hosting too many parties.");
                break;
            case 5:
                result("Only paid accounts can host parties.");
                break;
            default:
                result("Maybe next time! Party planning is unavailable here.");
                break;
            }
        } else {
            switch (movie.mode) {
            case 2:
                result(kind_ == net::ClerkKind::Kart ? "Thanks for visiting the kart shop!"
                                                     : "Thank you for the fish!");
                break;
            case clerks::kSellNoFish:
                result("Your fish tank is empty.");
                break;
            case clerks::kSellTrophy:
                std::snprintf(message_, sizeof(message_), "Fishing trophy! %u of %u species collected.",
                              unsigned(movie.extra[0]), unsigned(movie.extra[1]));
                page_ = Page::Result;
                resultUntil_ = now + 5000;
                active_ = dirty_ = true;
                break;
            case 9:
                result("Your Doodle has been returned.");
                break;
            case 10:
                result("Enjoy your new Doodle!");
                break;
            case 11:
                result("See you later! Adoption cancelled.");
                break;
            default:
                result("The clerk ended the transaction.");
                break;
            }
        }
    }
    if (!active_)
        return;
    if (!client.inZone() || !client.findNpc(clerk_)) {
        active_ = false;
        return;
    }
    if (page_ == Page::Result) {
        if (int32_t(now - resultUntil_) >= 0)
            active_ = false;
    } else if (int32_t(deadline_ - now) <= 2000) {
        client.sendServiceClerkDone();
        result("The clerk's timer has expired.");
    }
    const auto &profile = client.clerkProfile();
    const auto &stats = client.ownStats();
    const int remaining = int32_t(deadline_ - now) / 1000;
    const int seeds = client.clerkSession().seedsReceived ? client.clerkSession().seedCount : -1;
    if (version_ != profile.version || tickets_ != client.ownBook().tickets || money_ != stats.money ||
        bank_ != stats.bankMoney || tank_ != client.ownFish().tankCount || second_ != remaining ||
        seeds_ != seeds) {
        version_ = profile.version;
        tickets_ = client.ownBook().tickets;
        money_ = stats.money;
        bank_ = stats.bankMoney;
        tank_ = client.ownFish().tankCount;
        second_ = remaining;
        if (seeds_ != seeds) {
            seeds_ = seeds;
            refreshPet();
        }
        dirty_ = true;
    }
}
void ServiceClerkPanel::refreshPet() {
    havePet_ = false;
    if (!client_ || offer_ >= client_->clerkSession().seedCount)
        return;
    havePet_ =
        clerks::generatePet(client_->clerkSession().seeds[offer_], client_->zoneId() / 1000 * 1000, &pet_);
    if (havePet_)
        name_ = pet_.nameIndex;
}
void ServiceClerkPanel::choose(int delta) {
    if (page_ == Page::Pets) {
        const int count = client_->clerkSession().seedCount;
        if (count)
            offer_ = (offer_ + delta * 2 + count) % count;
        refreshPet();
    } else if (page_ == Page::Bodies)
        body_ = (body_ + delta + 3) % 3;
    else if (page_ == Page::Accessories)
        accessory_ = (accessory_ + delta + 92) % 92;
    else if (page_ == Page::Menu) {
        const int rows = kind_ == net::ClerkKind::Pet ? 4 : kind_ == net::ClerkKind::Kart ? 3 : 1;
        selection_ = (selection_ + delta + rows) % rows;
    }
    dirty_ = true;
}
void ServiceClerkPanel::cancel() {
    if (page_ == Page::Result) {
        active_ = false;
        return;
    }
    if (page_ != Page::Menu) {
        page_ = Page::Menu;
        dirty_ = true;
        return;
    }
    client_->sendServiceClerkDone();
    result("Waiting for the clerk to finish...");
}
void ServiceClerkPanel::activate() {
    if (!client_)
        return;
    if (page_ == Page::Result) {
        active_ = false;
        return;
    }
    if (page_ == Page::ConfirmAdopt) {
        if (client_->sendPetAdopt(offer_, name_)) {
            client_->sendServiceClerkDone();
            result("Waiting for adoption confirmation...");
        }
    } else if (page_ == Page::ConfirmReturn) {
        if (client_->sendPetReturn()) {
            client_->sendServiceClerkDone();
            result("Waiting for return confirmation...");
        }
    } else if (page_ == Page::ConfirmBuy) {
        const bool sent =
            buyBody_ ? client_->sendClerkKartBody(body_) : client_->sendClerkKartAccessory(accessory_);
        if (sent) {
            page_ = buyBody_ ? Page::Bodies : Page::Accessories;
            std::snprintf(message_, sizeof(message_), "Purchase sent. Waiting for the AI's resend.");
        }
    } else if (page_ == Page::Pets) {
        if (havePet_ && pet_.cost <= money_ + bank_)
            page_ = Page::ConfirmAdopt;
    } else if (page_ == Page::Bodies) {
        if (clerks::canBuyKart(body_, client_->clerkProfile().kart[0], client_->ownBook().tickets)) {
            buyBody_ = true;
            page_ = Page::ConfirmBuy;
        }
    } else if (page_ == Page::Accessories) {
        if (clerks::canBuyAccessory(accessory_, client_->clerkProfile().kart[0],
                                    client_->clerkProfile().accessories, client_->ownBook().tickets)) {
            buyBody_ = false;
            page_ = Page::ConfirmBuy;
        }
    } else if (kind_ == net::ClerkKind::Pet) {
        if (selection_ == 0 && client_->clerkFishTotal() > 0) {
            if (client_->sendServiceFishSale(true))
                result("Waiting for the fish sale...");
        } else if (selection_ == 1) {
            page_ = Page::Pets;
            refreshPet();
        } else if (selection_ == 2 && client_->clerkProfile().petId && client_->clerkSession().seedCount)
            page_ = Page::ConfirmReturn;
        else if (selection_ == 3)
            cancel();
    } else if (kind_ == net::ClerkKind::Kart) {
        if (selection_ == 0)
            page_ = Page::Bodies;
        else if (selection_ == 1)
            page_ = Page::Accessories;
        else
            cancel();
    } else if (kind_ == net::ClerkKind::Fisherman) {
        if (client_->sendServiceFishSale(true))
            result("Waiting for the fish sale...");
    } else if (kind_ == net::ClerkKind::Party)
        cancel();
    dirty_ = true;
}
u32 ServiceClerkPanel::handleInput(u32 down, const touchPosition &touch) {
    if (!active_)
        return 0;
    // ---- padnav: the clerk pages were already button-driven; they now log like the others ----
    if (down & KEY_B) {
        TT_LOG("padnav: clerk activate cancel");
        cancel();
    } else if (down & KEY_A) {
        TT_LOG("padnav: clerk activate %d", selection_);
        activate();
    } else if (down & (KEY_DUP | KEY_DDOWN)) {
        choose((down & KEY_DUP) ? -1 : 1);
        TT_LOG("padnav: clerk focus %d", selection_);
    } else if (page_ == Page::Pets && (down & (KEY_DLEFT | KEY_DRIGHT))) {
        name_ = (name_ + ((down & KEY_DLEFT) ? clerks::kPetNames - 1 : 1)) % clerks::kPetNames;
        dirty_ = true;
    } else if (page_ == Page::Pets && (down & KEY_Y)) {
        if ((offer_ ^ 1) < client_->clerkSession().seedCount)
            offer_ ^= 1;
        refreshPet();
        dirty_ = true;
    } else if (down & KEY_L)
        choose(-1);
    else if (down & KEY_R)
        choose(1);
    // ---- end padnav ----
    if (down & KEY_TOUCH) {
        if (touch.py >= 202) {
            if (touch.px < 75)
                choose(-1);
            else if (touch.px < 150)
                choose(1);
            else if (touch.px < 235)
                activate();
            else
                cancel();
        } else if (page_ == Page::Menu && touch.py >= 75 && touch.py < 171) {
            selection_ = std::min(kind_ == net::ClerkKind::Pet    ? 3
                                  : kind_ == net::ClerkKind::Kart ? 2
                                                                  : 0,
                                  int(touch.py - 75) / 24);
            activate();
        } else if (page_ == Page::Pets && touch.py >= 125 && touch.py < 155) {
            if ((offer_ ^ 1) < client_->clerkSession().seedCount)
                offer_ ^= 1;
            refreshPet();
            dirty_ = true;
        }
    }
    return down;
}
void ServiceClerkPanel::diagnostic(const char *label) {
    if (!active_ || diagnosticDone_ || !client_)
        return;
    if (page_ == Page::Result) {
        diagnosticDone_ = true;
        return;
    }
    if (kind_ == net::ClerkKind::Pet && !std::strcmp(label, "clerk: adopt")) {
        if (!client_->clerkSession().seedsReceived)
            return;
        if (client_->clerkSession().seedCount && havePet_ && pet_.cost <= money_ + bank_) {
            page_ = Page::ConfirmAdopt;
            activate();
        } else {
            TT_LOG("clerks: autotest adopt unavailable: seeds %u money %d",
                   unsigned(client_->clerkSession().seedCount), money_ + bank_);
            client_->sendServiceClerkDone();
        }
    } else if (kind_ == net::ClerkKind::Kart) {
        int body = -1;
        for (int index = 0; index < 3; ++index)
            if (clerks::canBuyKart(index, client_->clerkProfile().kart[0], client_->ownBook().tickets)) {
                body = index;
                break;
            }
        if (body >= 0 && client_->clerkProfile().kart[0] < 0)
            client_->sendClerkKartBody(body);
        else {
            for (int index = 0; index < 92; ++index)
                if (clerks::canBuyAccessory(index, client_->clerkProfile().kart[0],
                                            client_->clerkProfile().accessories, client_->ownBook().tickets)) {
                    client_->sendClerkKartAccessory(index);
                    break;
                }
        }
        client_->sendServiceClerkDone();
    } else if ((kind_ == net::ClerkKind::Pet || kind_ == net::ClerkKind::Fisherman) &&
               client_->clerkFishTotal() > 0)
        client_->sendServiceFishSale(true);
    else
        client_->sendServiceClerkDone();
    diagnosticDone_ = true;
}
bool ServiceClerkPanel::kartPreview(int8_t dna[9]) const {
    if (!active_ || kind_ != net::ClerkKind::Kart || !client_)
        return false;
    std::memcpy(dna, client_->clerkProfile().kart, 9);
    if (page_ == Page::Bodies || (page_ == Page::ConfirmBuy && buyBody_))
        dna[0] = body_;
    if (dna[0] < 0)
        dna[0] = 0;
    if (page_ == Page::Accessories || (page_ == Page::ConfirmBuy && !buyBody_))
        dna[clerks::kKartAccessories[accessory_].type] = accessory_;
    return true;
}
void ServiceClerkPanel::setPreviewFailed(bool failed) {
    if (previewFailed_ != failed) {
        previewFailed_ = failed;
        dirty_ = true;
    }
}
void ServiceClerkPanel::rebuild() {
    const auto &session = client_->clerkSession();
    const char *title = kind_ == net::ClerkKind::Pet         ? "Pet shop"
                        : kind_ == net::ClerkKind::Kart      ? "Kart shop"
                        : kind_ == net::ClerkKind::Fisherman ? "Fisherman"
                                                             : "Party planner";
    for (auto &line : strings_)
        line[0] = 0;
    std::snprintf(strings_[0], 128, "%s", title);
    if (kind_ == net::ClerkKind::Kart)
        std::snprintf(strings_[1], 128, "Jellybeans %d   Bank %d   Tickets %u", money_, bank_, unsigned(tickets_));
    else
        std::snprintf(strings_[1], 128, "Jellybeans %d   Bank %d", money_, bank_);
    if (page_ != Page::Result)
        std::snprintf(strings_[2], 128, "%d:%02d", std::max(0, second_) / 60, std::max(0, second_) % 60);
    if (page_ == Page::Result)
        std::snprintf(strings_[3], 128, "%s", message_);
    else if (page_ == Page::Menu) {
        if (kind_ == net::ClerkKind::Pet) {
            const char *returnLabel = !client_->clerkProfile().petId ? "No Doodle to return"
                                      : !session.seedCount           ? "Return unavailable on this server"
                                                                     : "Return Doodle (confirm)";
            const char *rows[] = {"Sell fish", "Adopt a Doodle", returnLabel, "Done"};
            for (int index = 0; index < 4; ++index)
                std::snprintf(strings_[3 + index], 128, "%s", rows[index]);
            std::snprintf(strings_[7], 128, "Tank: %d fish, value %d beans", tank_, client_->clerkFishTotal());
        } else if (kind_ == net::ClerkKind::Kart) {
            const char *rows[] = {"Kart bodies", "Accessories", "Done"};
            for (int index = 0; index < 3; ++index)
                std::snprintf(strings_[3 + index], 128, "%s", rows[index]);
        } else if (kind_ == net::ClerkKind::Fisherman)
            std::snprintf(strings_[3], 128, "Sell %d fish for %d jellybeans?", tank_,
                          client_->clerkFishTotal());
        else
            std::snprintf(strings_[3], 128, "Party planning is unavailable. A/B: decline.");
    } else if (page_ == Page::Pets || page_ == Page::ConfirmAdopt) {
        if (!havePet_)
            std::snprintf(strings_[3], 128, "%s",
                          session.seedsReceived ? "No Doodles offered by this server."
                                                : "Waiting for the server's Doodle seeds...");
        else {
            const float *color = clerks::petColor(pet_.dna[4], pet_.dna[5]);
            const float shade = .8f + .05f * pet_.dna[6];
            std::snprintf(strings_[3], 128, "Choice %d of 3: %s", offer_ / 2 + 1, clerks::petName(name_));
            std::snprintf(strings_[4], 128, "Doodle: %s  %d jellybeans", clerks::kBodyNames[pet_.dna[4]],
                          pet_.cost);
            std::snprintf(strings_[5], 128, "%s  RGB %u,%u,%u", offer_ % 2 ? "Boy" : "Girl",
                          unsigned(std::min(1.0f, color[0] * shade) * 255),
                          unsigned(std::min(1.0f, color[1] * shade) * 255),
                          unsigned(std::min(1.0f, color[2] * shade) * 255));
            std::snprintf(strings_[6], 128, "Y: gender   Left/Right: name");
            if (page_ == Page::ConfirmAdopt)
                std::snprintf(strings_[7], 128, "%s",
                              client_->clerkProfile().petId ? "Replace your existing Doodle? A: yes"
                                                            : "Adopt this Doodle? A: yes, B: back");
        }
    } else if (page_ == Page::ConfirmReturn) {
        std::snprintf(strings_[3], 128, "Return your Doodle permanently?");
        std::snprintf(strings_[4], 128, "A: confirm return. B: keep your Doodle.");
    } else {
        const bool body = page_ == Page::Bodies || (page_ == Page::ConfirmBuy && buyBody_);
        const auto &item = body ? clerks::kKartBodies[body_] : clerks::kKartAccessories[accessory_];
        std::snprintf(strings_[3], 128, "%s %d: %s", body ? "Body" : "Accessory", body ? body_ : accessory_,
                      item.name);
        std::snprintf(strings_[4], 128, "Cost: %d tickets", item.cost);
        const bool allowed = body ? clerks::canBuyKart(body_, client_->clerkProfile().kart[0], tickets_)
                                  : clerks::canBuyAccessory(accessory_, client_->clerkProfile().kart[0],
                                                            client_->clerkProfile().accessories, tickets_);
        std::snprintf(strings_[5], 128, "%s",
                      allowed ? "Available" : "Owned, trunk full, no kart, or insufficient tickets");
        if (page_ == Page::ConfirmBuy)
            std::snprintf(strings_[7], 128, "Buy this item? A: confirm, B: back");
        else if (session.pendingBody >= 0 || session.pendingAccessory >= 0)
            std::snprintf(strings_[7], 128, "Waiting for the AI's ticket/DNA update...");
    }
    if (kind_ == net::ClerkKind::Kart && previewFailed_)
        std::snprintf(strings_[8], 128, "Preview assets unavailable. Install clerks/karts.");
    std::snprintf(strings_[9], 128, "%s", page_ == Page::Result ? "OK" : "Choose");
    C2D_TextBufClear(buffer_);
    count_ = 10;
    for (int index = 0; index < count_; ++index) {
        tt::render::textParse(&texts_[index], buffer_, strings_[index]);
        C2D_TextOptimize(&texts_[index]);
    }
    static const char* const kButtons[3] = {"< Prev", "Next >", "Back"};
    for (int index = 0; index < 3; ++index) {
        tt::render::textParse(&btnText_[index], buffer_, kButtons[index]);
        C2D_TextOptimize(&btnText_[index]);
    }
    dirty_ = false;
}
// Upstream's clerk GUIs are cream DirectGui panels; here the paper card with the countdown badge,
// SpeedChat-style menu rows and the Prev / Next / Choose / Back buttons (handleInput's touch columns).
void ServiceClerkPanel::draw() {
    if (!client_)
        return;
    if (dirty_)
        rebuild();
    theme::sky();
    theme::paper(4, 4, 312, 194, 8.0f);
    auto line = [&](int index, float x, float y, float scale, u32 color) {
        if (index < count_ && strings_[index][0])
            theme::textFit(texts_[index], x, y, scale, color, 300 - x);
    };
    line(0, 14, 9, 0.62f, theme::kRedDark);
    if (strings_[2][0]) { // the AI's deadline, like the battle timer
        const bool low = second_ <= 10;
        theme::roundRect(268, 9, 40, 20, 7, low ? theme::kRed : theme::kYellow);
        theme::textCentered(texts_[2], 288, 10, theme::kTextBody, low ? theme::kWhite : theme::kInk, 36);
    }
    drawJarIcon(13, 29, 18); // our jellybeans (InventoryPage.moneyDisplay's jar)
    line(1, 32, 33, theme::kTextSmall, theme::kInkSoft);
    C2D_DrawRectSolid(12, 52, 0.5f, 296, 2, theme::kPaperShade);
    const int rows = page_ != Page::Menu ? 0 : kind_ == net::ClerkKind::Pet ? 4 : kind_ == net::ClerkKind::Kart ? 3 : 0;
    for (int index = 3; index <= 8; ++index) {
        const float y = index * 23 + 6;
        if (index - 3 < rows) {
            const bool on = selection_ == index - 3;
            theme::roundRect(12, y - 2, 296, 22, 5, theme::kScFrameEdge);
            theme::roundRect(13, y - 1, 294, 20, 4, on ? theme::kScRollover : theme::kScFrame);
            line(index, 22, y, theme::kTextBody, theme::kScText);
        } else {
            line(index, 14, y, theme::kTextBody, index == 7 && page_ != Page::Menu ? theme::kRedDark : theme::kInk);
        }
    }
    // Bottom buttons in the touch columns (x < 75 Prev, < 150 Next, < 235 A, else B).
    // Toontown's quit_button pills: the page arrows yellow, the main action cyan, cancel red.
    struct Btn { float x, w; theme::Pill style; const C2D_Text* label; };
    const Btn btns[4] = {{4, 68, theme::Pill::Yellow, &btnText_[0]},
                         {79, 68, theme::Pill::Yellow, &btnText_[1]},
                         {154, 78, theme::Pill::Cyan, &texts_[9]},
                         {239, 77, theme::Pill::Red, &btnText_[2]}};
    for (int i = 0; i < 4; ++i) {
        if (i == 3 && page_ == Page::Result) continue; // a result only needs OK
        // Prev / Next only where they page or move: not on a result, nor on a one-choice menu.
        if (i < 2 && (page_ == Page::Result || (page_ == Page::Menu && rows <= 1))) continue;
        const Btn& b = btns[i];
        theme::pill(b.x, 206, b.w, 30, b.style);
        theme::textCentered(*b.label, b.x + b.w * 0.5f, 212, theme::kTextBody, theme::pillInk(b.style), b.w - 14);
    }
}
} // namespace tt::ui
