// Astron client message types (direct/distributed/MsgTypes.py, astron docs).
#pragma once

#include <cstdint>

namespace tt::net::msg {

constexpr uint16_t CLIENT_HELLO = 1;                              // u32 dcHash, string version
constexpr uint16_t CLIENT_HELLO_RESP = 2;                         // (empty)
constexpr uint16_t CLIENT_DISCONNECT = 3;                         // (empty)
constexpr uint16_t CLIENT_EJECT = 4;                              // u16 code, string reason
constexpr uint16_t CLIENT_HEARTBEAT = 5;                          // (empty)
constexpr uint16_t CLIENT_OBJECT_SET_FIELD = 120;                 // u32 doId, u16 field, value
constexpr uint16_t CLIENT_OBJECT_SET_FIELDS = 121;                // u32 doId, u16 n, (u16 field, value)*
constexpr uint16_t CLIENT_OBJECT_LEAVING = 132;                   // u32 doId
constexpr uint16_t CLIENT_OBJECT_LEAVING_OWNER = 161;             // u32 doId
constexpr uint16_t CLIENT_ENTER_OBJECT_REQUIRED = 142;            // u32 doId, u32 parent, u32 zone, u16 class, required...
constexpr uint16_t CLIENT_ENTER_OBJECT_REQUIRED_OTHER = 143;      // ... + u16 n, (u16 field, value)*
constexpr uint16_t CLIENT_ENTER_OBJECT_REQUIRED_OWNER = 172;      // like 142, owner view
constexpr uint16_t CLIENT_ENTER_OBJECT_REQUIRED_OTHER_OWNER = 173; // like 143, owner view
constexpr uint16_t CLIENT_OBJECT_LOCATION = 140;                  // u32 doId, u32 parent, u32 zone
constexpr uint16_t CLIENT_ADD_INTEREST = 200;                     // u32 context, u16 handle, u32 parent, u32 zone
constexpr uint16_t CLIENT_ADD_INTEREST_MULTIPLE = 201;            // u32 context, u16 handle, u32 parent, u16 n, u32 zone*
constexpr uint16_t CLIENT_REMOVE_INTEREST = 203;                  // u32 context, u16 handle
constexpr uint16_t CLIENT_DONE_INTEREST_RESP = 204;               // u32 context, u16 handle

} // namespace tt::net::msg
