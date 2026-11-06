#!/usr/bin/env bash
# Host tests for the 3DS bottom-screen panels (Map / Chat-SpeedChat / Friends / Toon / Gags /
# Book / tab-bar Laff pocket). No emulator, no libctru: the real panel sources compile against
# the citro stubs in stubs/, which record every draw as a rectangle; text metrics come from the
# committed ImpressBT bcfnt (see stubs/font_metrics.h). Exits non-zero on any failure.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/tt_gui_panel_test"
mkdir -p "$OUT"

CXX="${CXX:-}"
if [ -z "$CXX" ]; then
    for c in clang++ g++-14 g++; do
        if command -v "$c" >/dev/null 2>&1; then CXX="$c"; break; fi
    done
fi

# The panel sources compile as separate translation units with the host stubs first on the
# include path (their <3ds.h>, <citro2d.h>, render/model.h, world/battle_fx.h resolve to
# stubs/), like the device build but with the recorder instead of a GPU.
SRCS=(
    "$ROOT/source/render/ui.cpp"
    "$ROOT/source/render/ui_font.cpp"
    "$ROOT/source/core/profile.cpp"
    "$ROOT/source/quest/quest_data.cpp"
    "$ROOT/source/quest/quest_text.cpp"
    "$ROOT/source/quest/quest_talk.cpp"
    "$ROOT/source/net/client_book.cpp"
    "$ROOT/source/world/fish_tables.cpp"
    "$ROOT/source/world/toon_dna.cpp"
    "$ROOT/source/ui/theme.cpp"
    "$ROOT/source/ui/page_text.cpp"
    "$ROOT/source/ui/chat_menu.cpp"
    "$ROOT/source/ui/friends_art.cpp"
    "$ROOT/source/ui/laff_meter.cpp"
    "$ROOT/source/ui/book.cpp"
    "$ROOT/source/ui/book_pages/page_inventory.cpp"
    "$ROOT/source/ui/book_pages/page_quests.cpp"
    "$ROOT/source/ui/book_pages/page_cogs.cpp"
    "$ROOT/source/ui/book_pages/page_disguise.cpp"
    "$ROOT/source/ui/book_pages/page_fish.cpp"
    "$ROOT/source/ui/book_pages/page_trophies.cpp"
    "$ROOT/source/ui/book_pages/page_sos.cpp"
    "$ROOT/source/ui/book_pages/page_options.cpp"
    "$ROOT/source/ui/book_pages/page_map.cpp"
    "$ROOT/source/ui/battle_panel.cpp"
    "$ROOT/source/ui/quest_panel.cpp"
    "$ROOT/source/ui/quest_poster_art.cpp"
    "$ROOT/source/ui/shop_panel.cpp"
    "$ROOT/source/ui/service_clerk_panel.cpp"
    "$ROOT/source/ui/tailor_panel.cpp"
    "$ROOT/source/ui/closet_panel.cpp"
    "$ROOT/source/ui/fishing_panel.cpp"
    "$ROOT/source/ui/minigame_purchase_panel.cpp"
    "$ROOT/source/ui/book_tab_icons.cpp"
    "$ROOT/source/ui/gag_table.cpp"
    "$ROOT/source/ui/gag_data.cpp"
    "$ROOT/source/ui/speedchat_data.cpp"
    "$ROOT/source/ui/inventory_art.cpp"
    "$ROOT/source/ui/purchase_art.cpp"
    "$ROOT/source/ui/mat_art.cpp"
    "$ROOT/source/ui/suit_heads_atlas.cpp"
    "$ROOT/source/ui/shop_rules.cpp"
    "$ROOT/source/ui/map_view.cpp"
    "$ROOT/source/ui/toontown_timer.cpp"
    "$ROOT/source/ui/tutorial_art.cpp"
    "$ROOT/source/ui/focus.cpp"
    "$ROOT/source/ui/pad_tables.cpp"
    "$DIR/gui_panel_test.cpp"
    "$DIR/host_stub.cpp"
    "$ROOT/tools/nettest/log_host.cpp"
)

nice -n 10 "$CXX" -std=c++17 -Wall -Wextra -fno-rtti -fno-exceptions \
    -I"$DIR/stubs" -I"$ROOT/source" \
    "${SRCS[@]}" -lpthread -o "$OUT/gui_panel_test"

cd "$ROOT" # romfs/baked/fonts/... paths are read relative to the repo root
"$OUT/gui_panel_test" 2>&1 | tee "$OUT/gui_panel_test.log"
# run.sh exits non-zero when the binary fails (pipeexit via set -o pipefail)
