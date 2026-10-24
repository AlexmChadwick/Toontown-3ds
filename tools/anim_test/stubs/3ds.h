#pragma once
#include <cstdint>
#include <cstdlib>
#include <mutex>
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using u64=uint64_t;using s32=int32_t;
constexpr u64 U64_MAX=UINT64_MAX,SYSCLOCK_ARM11=1000000000;
constexpr int RESET_ONESHOT=0;
struct LightLock { std::mutex mutex; };
inline void LightLock_Init(LightLock*) {}
inline void LightLock_Lock(LightLock* l) { l->mutex.lock(); }
inline void LightLock_Unlock(LightLock* l) { l->mutex.unlock(); }
struct LightEvent {};
inline void LightEvent_Init(LightEvent*,int) {}
inline void LightEvent_Signal(LightEvent*) {}
inline void LightEvent_Wait(LightEvent*) {}
using Thread=void*;
inline Thread threadCreate(void(*)(void*),void*,size_t,int,int,bool) { return nullptr; }
inline void threadJoin(Thread,u64) {}
inline void threadFree(Thread) {}
inline u64 svcGetSystemTick() { return 0; }
inline void svcSleepThread(int64_t) {}
void* linearAlloc(size_t);
void linearFree(void*);
inline void GSPGPU_FlushDataCache(void*,size_t) {}
