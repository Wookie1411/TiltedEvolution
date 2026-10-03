#include <TiltedOnlinePCH.h>

#include <World.h>
#include <Services/FollowService.h>
#include <Interface/UI.h>
#include <ExtraData/ExtraDataList.h>

// SkyrimCoop fast-travel confirmation.
// The map's "Fast travel to ...?" prompt calls FastTravelConfirmCallback::Run(button) when answered.
// Used to name the destination in the follow offer. Layout and Address Library ids as described by CommonLibSSE(-NG) (RE::FastTravelConfirmCallback,
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

constexpr uint8_t kButtonYes = 1;
constexpr uint32_t kMapMenuSelectedMarkerOffset = 0x58;
constexpr ExtraDataType kExtraMapMarkerType = static_cast<ExtraDataType>(0x2C);

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

// "Yes": remember the destination so FollowService can offer the others to follow after arrival.
// The travel itself is never held back.
void TP_MAKE_THISCALL(HookFastTravelConfirmRun, FastTravelConfirmCallback, uint8_t aButton)
{
    if (aButton == kButtonYes && apThis->pMapMenu)
    {
        const String cDestination = GetMarkerName(GetSelectedMarker(apThis->pMapMenu));
        spdlog::info("[SkyrimCoop] Fast travel to '{}'", cDestination.c_str());
        World::Get().ctx().at<FollowService>().OnLocalFastTravelConfirmed(cDestination);
    }

    TiltedPhoques::ThisCall(RealFastTravelConfirmRun, apThis, aButton);
}

static TiltedPhoques::Initializer s_fastTravelConfirmHooks(
    []()
    {
        VersionDbPtr<void*> vtable(216523);
        RealFastTravelConfirmRun = reinterpret_cast<TFastTravelConfirmRun*>(vtable.Get()[1]);

        TP_HOOK(&RealFastTravelConfirmRun, HookFastTravelConfirmRun);
    });
