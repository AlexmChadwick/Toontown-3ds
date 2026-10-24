// Small shared background loader for single baked files (.ttm models, .tta clips).
//
// A fixed pool of slots; the loader thread (prio 0x31, core 0, like ToonCache's) reads a
// queued file into the slot's STAGING Model/Anim; the main thread polls the ticket and
// swaps the staging object into its own (Model::swap / Anim::swap, no GPU work, no
// allocation). Draw nothing until ready.
//
// Linear memory: Model::load / Anim::load take tt::mem::LinearGuard themselves around the
// short alloc/free calls and never hold it across file IO, so the main thread's own
// linear allocations (audio, UI, toon cache) interleave safely.
//
// Main thread only: request*, poll, take*, cancel. Tickets carry a generation, so a stale
// ticket (already taken or cancelled) is simply invalid.
#pragma once

#include <3ds.h>

#include "render/anim.h"
#include "render/model.h"

namespace tt::world {

using LoadTicket = u32; // 0 = invalid
constexpr LoadTicket kNoTicket = 0;

enum class LoadStatus : u8 { Invalid, Pending, Ready, Failed };

class AssetLoader {
public:
    static constexpr int kSlots = 24; // fixed queue: one cchar spawn needs 5

    // Starts the thread (idempotent). Without a thread the work runs from pump().
    static void init();
    // Cancels everything and joins the thread. Call before the linear heap is torn down.
    static void shutdown();

    // Queue a file; kNoTicket when every slot is busy (try again next frame).
    static LoadTicket requestModel(const char* path);
    static LoadTicket requestAnim(const char* path);

    static LoadStatus poll(LoadTicket t);
    // Ready -> moves the staged data into `dst` and frees the ticket. `dst` must be empty:
    // its old contents would land in the freed slot (and only be released by that slot's
    // next load). Failed tickets are freed by cancel().
    static bool takeModel(LoadTicket t, render::Model& dst);
    static bool takeAnim(LoadTicket t, render::Anim& dst);
    // Frees the ticket; a load in flight is discarded by the loader thread.
    static void cancel(LoadTicket t);

    // Number of queued + loading slots (debug).
    static int pending();
};

} // namespace tt::world
