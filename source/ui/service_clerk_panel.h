#pragma once
#include "net/client.h"
#include "ui/clerk_rules.h"
#include <3ds.h>
#include <citro2d.h>
namespace tt::ui {
class ServiceClerkPanel {
public:
    bool init();
    void shutdown();
    void update(net::Client &client, uint32_t now);
    u32 handleInput(u32 down, const touchPosition &touch);
    void draw();
    void dismiss() { active_ = false; }
    bool active() const {
        return active_;
    }
    net::ClerkKind kind() const {
        return kind_;
    }
    uint32_t clerkDoId() const {
        return clerk_;
    }
    // Diagnostic actions go through the same guarded, confirmed panel path.
    void diagnostic(const char *label);
    bool kartPreview(int8_t dna[9]) const;
    void setPreviewFailed(bool failed);

private:
    enum class Page { Menu, Pets, Bodies, Accessories, ConfirmAdopt, ConfirmReturn, ConfirmBuy, Result };
    void activate();
    void cancel();
    void choose(int delta);
    void rebuild();
    void result(const char *message);
    void refreshPet();
    net::Client *client_ = nullptr;
    bool active_ = false;
    bool dirty_ = true;
    net::ClerkKind kind_ = net::ClerkKind::None;
    Page page_ = Page::Menu;
    uint32_t clerk_ = 0, deadline_ = 0, now_ = 0, resultUntil_ = 0;
    uint32_t version_ = 0xffffffff, tickets_ = 0;
    int money_ = -1, bank_ = -1, tank_ = -1, second_ = -1, seeds_ = -1;
    int selection_ = 0, offer_ = 0, body_ = 0, accessory_ = 0;
    uint32_t name_ = 0;
    clerks::PetOffer pet_;
    bool havePet_ = false;
    bool previewFailed_ = false;
    bool buyBody_ = false;
    bool diagnosticDone_ = false;
    char message_[128] = {};
    char strings_[10][128] = {};
    C2D_Text texts_[10];
    C2D_TextBuf buffer_ = nullptr;
    int count_ = 0;
};
} // namespace tt::ui
