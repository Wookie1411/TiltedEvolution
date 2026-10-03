#include <TiltedOnlinePCH.h>

#include <World.h>
#include <Services/FastTravelService.h>
#include <Interface/UI.h>
#include <ExtraData/ExtraDataList.h>

// SkyrimCoop fast-travel confirmation.
// The map's "Fast travel to ...?" prompt calls FastTravelConfirmCallback::Run(button) when answered.
// Layout and Address Library ids as described by CommonLibSSE(-NG) (RE::FastTravelConfirmCallback,
// VTABLE___FastTravelConfirmCallback = 216523 for AE). Verified on 1.7.104 by disassembling Run:
// button 1 = "Yes"; the chosen map marker's handle is read from MapMenu + 0x58.

struct MapMenu;

struct FastTravelConfirmCallback
{
    virtual ~FastTravelConfirmCallback();  // 00
    virtual void Run(uint8_t aButton) = 0; // 01 (IMessageBoxCallback)

    volatile uint32_t refCount; // 08 (BSIntrusiveRefCounted)
    uint32_t pad0C;             // 0C
    MapMenu* pMapMenu;          // 10
    int32_t cursorPosX;         // 18
    int32_t cursorPosY;         // 1C
};
static_assert(offsetof(FastTravelConfirmCallback, refCount) == 0x8);
static_assert(offsetof(FastTravelConfirmCallback, pMapMenu) == 0x10);
static_assert(sizeof(FastTravelConfirmCallback) == 0x20);

// Map marker data (CommonLibSSE: ExtraMapMarker::mapData -> MapMarkerData::locationName (TESFullName)).
struct MapMarkerData
{
    void* pFullNameVtable;  // 00 TESFullName (BaseFormComponent vtable)
    const char* pFullName;  // 08 TESFullName::fullName (BSFixedString data)
};
struct ExtraMapMarker : BSExtraData
{
    MapMarkerData* pMapData; // 10
};
static_assert(sizeof(ExtraMapMarker) == 0x18);

TP_THIS_FUNCTION(TFastTravelConfirmRun, void, FastTravelConfirmCallback, uint8_t);
static TFastTravelConfirmRun* RealFastTravelConfirmRun = nullptr;
static void* s_fastTravelConfirmVtable = nullptr;

constexpr uint8_t kButtonYes = 1;
constexpr uint32_t kMapMenuSelectedMarkerOffset = 0x58;
constexpr ExtraDataType kExtraMapMarkerType = static_cast<ExtraDataType>(0x2C);

// The answered prompt we are holding back until the party agrees.
static MapMenu* s_pendingMapMenu = nullptr;
static int32_t s_pendingCursorX = 0;
static int32_t s_pendingCursorY = 0;
static bool s_runningApproved = false;

static TESObjectREFR* GetSelectedMarker(MapMenu* apMapMenu) noexcept
{
    const uint32_t cHandle = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(apMapMenu) + kMapMenuSelectedMarkerOffset);
    return TESObjectREFR::GetByHandle(cHandle);
}

static String GetMarkerName(TESObjectREFR* pMarker) noexcept
{
    if (!pMarker)
        return {};

    const auto* pExtra = static_cast<const ExtraMapMarker*>(pMarker->extraData.GetByType(kExtraMapMarkerType));
    if (!pExtra || !pExtra->pMapData || !pExtra->pMapData->pFullName)
        return {};

    return pExtra->pMapData->pFullName;
}

void ClearPendingFastTravel() noexcept
{
    s_pendingMapMenu = nullptr;
}

// Runs the held-back "Yes" (party approved). Only works while the same map menu is still open.
bool ExecutePendingFastTravel() noexcept
{
    MapMenu* pMapMenu = s_pendingMapMenu;
    s_pendingMapMenu = nullptr;

    UI* pUI = UI::Get();
    if (!pMapMenu || !pUI || reinterpret_cast<MapMenu*>(pUI->FindMenuByName(BSFixedString("MapMenu"))) != pMapMenu)
        return false;

    // A stand-in for the game's callback object: Run() only reads the map menu pointer from it.
    struct
    {
        void* pVtable;
        uint32_t refCount;
        uint32_t pad0C;
        MapMenu* pMapMenu;
        int32_t cursorPosX;
        int32_t cursorPosY;
    } callback{s_fastTravelConfirmVtable, 1, 0, pMapMenu, s_pendingCursorX, s_pendingCursorY};

    s_runningApproved = true;
    TiltedPhoques::ThisCall(RealFastTravelConfirmRun, reinterpret_cast<FastTravelConfirmCallback*>(&callback), kButtonYes);
    s_runningApproved = false;
    return true;
}

void TP_MAKE_THISCALL(HookFastTravelConfirmRun, FastTravelConfirmCallback, uint8_t aButton)
{
    World& world = World::Get();
    const bool cConnected = world.GetTransport().IsConnected();
    const auto& partyService = world.GetPartyService();
    const bool cInParty = cConnected && partyService.IsInParty() && partyService.GetPartyMembers().size() >= 2;

    spdlog::info("[SkyrimCoop] Fast travel prompt answered: button {} ({}), in party: {}", aButton, aButton == kButtonYes ? "yes" : "no", cInParty);

    if (aButton != kButtonYes || !cInParty || s_runningApproved || !apThis->pMapMenu)
    {
        TiltedPhoques::ThisCall(RealFastTravelConfirmRun, apThis, aButton);
        return;
    }

    // Hold the travel back and ask the party first; the map stays open meanwhile.
    s_pendingMapMenu = apThis->pMapMenu;
    s_pendingCursorX = apThis->cursorPosX;
    s_pendingCursorY = apThis->cursorPosY;

    TESObjectREFR* pMarker = GetSelectedMarker(apThis->pMapMenu);
    const String cDestination = GetMarkerName(pMarker);
    spdlog::info("[SkyrimCoop] Fast travel to '{}' (marker {:X}) held back, asking the party", cDestination.c_str(), pMarker ? pMarker->formID : 0);
    world.ctx().at<FastTravelService>().OnLocalFastTravelConfirmed(cDestination, pMarker ? pMarker->formID : 0);
}

static TiltedPhoques::Initializer s_fastTravelConfirmHooks(
    []()
    {
        VersionDbPtr<void*> vtable(216523);
        s_fastTravelConfirmVtable = vtable.Get();
        RealFastTravelConfirmRun = reinterpret_cast<TFastTravelConfirmRun*>(vtable.Get()[1]);

        TP_HOOK(&RealFastTravelConfirmRun, HookFastTravelConfirmRun);
    });
