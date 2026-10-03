#include <TiltedOnlinePCH.h>

#include <World.h>

// SkyrimCoop fast-travel confirmation.
// The map's "Fast travel to ...?" prompt calls FastTravelConfirmCallback::Run(button) when answered.
// Layout and Address Library ids as described by CommonLibSSE(-NG) (RE::FastTravelConfirmCallback,
// VTABLE___FastTravelConfirmCallback = 216523 for AE).
// Step 1: log only, no behavior change.

struct MapMenu;

struct FastTravelConfirmCallback
{
    virtual ~FastTravelConfirmCallback();      // 00
    virtual void Run(uint8_t aButton) = 0;     // 01 (IMessageBoxCallback)

    volatile uint32_t refCount; // 08 (BSIntrusiveRefCounted)
    uint32_t pad0C;             // 0C
    MapMenu* pMapMenu;          // 10
    int32_t cursorPosX;         // 18
    int32_t cursorPosY;         // 1C
};
static_assert(offsetof(FastTravelConfirmCallback, refCount) == 0x8);
static_assert(offsetof(FastTravelConfirmCallback, pMapMenu) == 0x10);
static_assert(sizeof(FastTravelConfirmCallback) == 0x20);

TP_THIS_FUNCTION(TFastTravelConfirmRun, void, FastTravelConfirmCallback, uint8_t);
static TFastTravelConfirmRun* RealFastTravelConfirmRun = nullptr;

void TP_MAKE_THISCALL(HookFastTravelConfirmRun, FastTravelConfirmCallback, uint8_t aButton)
{
    World& world = World::Get();
    const bool cConnected = world.GetTransport().IsConnected();
    const auto& partyService = world.GetPartyService();

    spdlog::info("[SkyrimCoop] Fast travel prompt answered: button {} (0 = yes?), cursor ({}, {}), connected: {}, party members: {}, leader: {}", aButton,
                 apThis->cursorPosX, apThis->cursorPosY, cConnected, partyService.GetPartyMembers().size(), partyService.IsLeader());

    TiltedPhoques::ThisCall(RealFastTravelConfirmRun, apThis, aButton);
}

static TiltedPhoques::Initializer s_fastTravelConfirmHooks(
    []()
    {
        VersionDbPtr<void*> vtable(216523);
        RealFastTravelConfirmRun = reinterpret_cast<TFastTravelConfirmRun*>(vtable.Get()[1]);

        TP_HOOK(&RealFastTravelConfirmRun, HookFastTravelConfirmRun);
    });
