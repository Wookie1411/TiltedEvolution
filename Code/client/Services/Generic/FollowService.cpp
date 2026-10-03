#include <Services/FollowService.h>

#include <World.h>
#include <Events/UpdateEvent.h>
#include <Messages/FollowRequest.h>
#include <Messages/NotifyFollowOffer.h>

#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Interface/UI.h>

namespace
{
constexpr auto kOfferWindow = std::chrono::seconds(10);

bool IsLoading() noexcept
{
    UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen(BSFixedString("Loading Menu"));
}

// Only read Y while Skyrim has the focus and STR's own overlay (chat) isn't open.
bool IsKeyDownForGame(int aKey, const OverlayService& acOverlay) noexcept
{
    DWORD processId = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &processId);
    return processId == GetCurrentProcessId() && !acOverlay.GetActive() && (GetAsyncKeyState(aKey) & 0x8000);
}
} // namespace

FollowService::FollowService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport)
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&FollowService::OnUpdate>(this);
    m_offerConnection = aDispatcher.sink<NotifyFollowOffer>().connect<&FollowService::OnNotifyFollowOffer>(this);

    EventDispatcherManager::Get()->fastTravelEndEvent.RegisterSink(this);
}

void FollowService::OnLocalFastTravelConfirmed(const String& acDestination) noexcept
{
    m_world.GetRunner().Queue(
        [this, destination = acDestination]()
        {
            m_fastTravelling = true;
            m_fastTravelDestination = destination;
        });
}

void FollowService::OnLocalLoadDoorUsed(const String& acDestination, uint32_t aOriginCellId) noexcept
{
    m_world.GetRunner().Queue([this, destination = acDestination, aOriginCellId]()
                              { m_doorTrip = DoorTrip{destination, aOriginCellId, std::chrono::steady_clock::now() + std::chrono::seconds(30)}; });
}

BSTEventResult FollowService::OnEvent(const TESFastTravelEndEvent*, const EventDispatcher<TESFastTravelEndEvent>*)
{
    m_world.GetRunner().Queue(
        [this]()
        {
            if (!m_fastTravelling)
                return;
            m_fastTravelling = false;
            SendOffer(FollowRequest::kFastTravel, m_fastTravelDestination);
        });
    return BSTEventResult::kOk;
}

// Tells the server where we arrived, so the others can follow us there.
void FollowService::SendOffer(uint8_t aKind, const String& acDestination) noexcept
{
    const auto& partyService = m_world.GetPartyService();
    if (!m_transport.IsConnected() || !partyService.IsInParty() || partyService.GetPartyMembers().size() < 2)
        return;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectCELL* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
    if (!pCell)
        return;

    FollowRequest request{};
    request.RequestAction = FollowRequest::kOffer;
    request.Kind = aKind;
    request.Destination = acDestination;
    auto& modSystem = m_world.GetModSystem();
    modSystem.GetServerModId(pCell->formID, request.CellId);
    if (TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace())
        modSystem.GetServerModId(pWorldSpace->formID, request.WorldSpaceId);
    request.Position.x = pPlayer->position.x;
    request.Position.y = pPlayer->position.y;
    request.Position.z = pPlayer->position.z;
    m_transport.Send(request);

    spdlog::info("[SkyrimCoop] Follow offer sent: {} '{}', cell {:X} at ({:.0f}, {:.0f}, {:.0f})", aKind == FollowRequest::kDoor ? "door" : "fast travel", acDestination.c_str(),
                 pCell->formID, pPlayer->position.x, pPlayer->position.y, pPlayer->position.z);
}

void FollowService::OnUpdate(const UpdateEvent&) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();

    // My door trip: arrived once I'm in another cell and the loading screen is gone.
    if (m_doorTrip)
    {
        PlayerCharacter* pPlayer = PlayerCharacter::Get();
        const TESObjectCELL* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
        if (pCell && pCell->formID != m_doorTrip->OriginCellId && !IsLoading())
        {
            const String cDestination = m_doorTrip->Destination;
            m_doorTrip.reset();
            SendOffer(FollowRequest::kDoor, cDestination);
        }
        else if (cNow > m_doorTrip->Deadline)
            m_doorTrip.reset();
    }

    // Someone else's offer: Y to follow.
    if (!m_offer)
        return;

    auto& overlay = m_world.GetOverlayService();
    if (cNow > m_offer->Until)
    {
        m_offer.reset();
        return;
    }

    const bool cY = IsKeyDownForGame('Y', overlay);
    if (cY && !m_yWasDown)
    {
        FollowRequest request{};
        request.RequestAction = FollowRequest::kAccept;
        request.OfferId = m_offer->OfferId;
        m_transport.Send(request);
        overlay.SendSystemMessage(fmt::format("Following {}...", m_offer->TravellerName.c_str()));
        spdlog::info("[SkyrimCoop] Accepted follow offer {}", m_offer->OfferId);
        m_offer.reset();
    }
    m_yWasDown = cY;
}

void FollowService::OnNotifyFollowOffer(const NotifyFollowOffer& acMessage) noexcept
{
    const bool cDoor = acMessage.Kind == FollowRequest::kDoor;
    std::string text;
    if (acMessage.Destination.empty())
        text = fmt::format("{} {}.", acMessage.TravellerName.c_str(), cDoor ? "went through a door" : "fast travelled");
    else
        text = fmt::format("{} {} {}.", acMessage.TravellerName.c_str(), cDoor ? "went into" : "fast travelled to", acMessage.Destination.c_str());
    text += " Press Y within 10 s to follow.";

    m_world.GetOverlayService().SendSystemMessage(text);
    m_offer = PendingOffer{acMessage.OfferId, acMessage.TravellerName, std::chrono::steady_clock::now() + kOfferWindow};
    m_yWasDown = true; // ignore a Y that is already held down
}
