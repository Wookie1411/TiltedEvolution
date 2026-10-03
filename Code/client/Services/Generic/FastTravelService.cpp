#include <Services/FastTravelService.h>

#include <World.h>
#include <Events/UpdateEvent.h>
#include <Messages/FastTravelRequest.h>
#include <Messages/NotifyFastTravel.h>
#include <Structs/GridCellCoords.h>

#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Interface/UI.h>

// Defined in Games/Skyrim/Interface/Menus/FastTravelConfirm.cpp.
bool ExecutePendingFastTravel() noexcept;
void ClearPendingFastTravel() noexcept;

namespace
{
constexpr auto kAnswerWindow = std::chrono::seconds(30);

bool IsMapOpen() noexcept
{
    UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen(BSFixedString("MapMenu"));
}

// Only read Y / N while Skyrim has the focus and STR's own overlay (chat) isn't open.
bool IsKeyDownForGame(int aKey, const OverlayService& acOverlay) noexcept
{
    DWORD processId = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &processId);
    return processId == GetCurrentProcessId() && !acOverlay.GetActive() && (GetAsyncKeyState(aKey) & 0x8000);
}
} // namespace

FastTravelService::FastTravelService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport)
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&FastTravelService::OnUpdate>(this);
    m_notifyConnection = aDispatcher.sink<NotifyFastTravel>().connect<&FastTravelService::OnNotifyFastTravel>(this);

    EventDispatcherManager::Get()->fastTravelEndEvent.RegisterSink(this);
}

void FastTravelService::SendAction(uint8_t aAction) noexcept
{
    FastTravelRequest request{};
    request.RequestAction = aAction;
    m_transport.Send(request);
}

void FastTravelService::OnLocalFastTravelConfirmed(const String& acDestination, uint32_t aMarkerFormId) noexcept
{
    m_world.GetRunner().Queue(
        [this, destination = acDestination, aMarkerFormId]()
        {
            FastTravelRequest request{};
            request.RequestAction = FastTravelRequest::kAsk;
            request.Destination = destination;
            if (aMarkerFormId)
                m_world.GetModSystem().GetServerModId(aMarkerFormId, request.MarkerId);
            m_transport.Send(request);
            m_waitingForAnswers = true;
        });
}

void FastTravelService::OnUpdate(const UpdateEvent&) noexcept
{
    auto& overlay = m_world.GetOverlayService();

    // Closing the map while waiting withdraws the request.
    if (m_waitingForAnswers && !IsMapOpen())
    {
        m_waitingForAnswers = false;
        ClearPendingFastTravel();
        SendAction(FastTravelRequest::kCancel);
        overlay.SendSystemMessage("Fast travel cancelled: you closed the map.");
    }

    // Host moved to a partner's destination: arrived once the loading screen is gone and we stand there.
    if (m_awaitingMoveArrival)
    {
        PlayerCharacter* pPlayer = PlayerCharacter::Get();
        UI* pUI = UI::Get();
        const bool cLoading = pUI && pUI->GetMenuOpen(BSFixedString("Loading Menu"));
        if (pPlayer && !cLoading && glm::distance(glm::vec3(pPlayer->position), glm::vec3(m_moveTarget)) < 2000.f)
        {
            m_awaitingMoveArrival = false;
            SendArrived();
        }
        else if (std::chrono::steady_clock::now() > m_moveDeadline)
        {
            m_awaitingMoveArrival = false;
            spdlog::error("[SkyrimCoop] Fast travel: never arrived at the partner's destination");
        }
    }

    if (!m_askedToAnswer)
        return;

    if (std::chrono::steady_clock::now() > m_askedUntil)
    {
        m_askedToAnswer = false; // the server cancels it too and tells everyone
        return;
    }

    const bool cY = IsKeyDownForGame('Y', overlay);
    const bool cN = IsKeyDownForGame('N', overlay);

    if (cY && !m_yWasDown)
    {
        m_askedToAnswer = false;
        SendAction(FastTravelRequest::kAccept);
        overlay.SendSystemMessage("You accepted the fast travel.");
    }
    else if (cN && !m_nWasDown)
    {
        m_askedToAnswer = false;
        SendAction(FastTravelRequest::kDecline);
        overlay.SendSystemMessage("You declined the fast travel.");
    }

    m_yWasDown = cY;
    m_nWasDown = cN;
}

void FastTravelService::OnNotifyFastTravel(const NotifyFastTravel& acMessage) noexcept
{
    auto& overlay = m_world.GetOverlayService();
    const bool cMine = acMessage.RequesterId == m_transport.GetLocalPlayerId();
    const char* cDestination = acMessage.Destination.empty() ? "somewhere" : acMessage.Destination.c_str();

    switch (acMessage.TravelEvent)
    {
    case NotifyFastTravel::kWaiting: overlay.SendSystemMessage(fmt::format("Waiting for your partner to accept the fast travel to {}...", cDestination)); break;

    case NotifyFastTravel::kAsked:
        m_askedToAnswer = true;
        m_askedUntil = std::chrono::steady_clock::now() + kAnswerWindow;
        // Ignore a key that is already held down when the question arrives.
        m_yWasDown = m_nWasDown = true;
        overlay.SendSystemMessage(fmt::format("{} wants to fast travel to {}. Press Y to accept or N to decline (30 s).", acMessage.RequesterName.c_str(), cDestination));
        break;

    case NotifyFastTravel::kHostGoFirst:
        m_askedToAnswer = false;
        overlay.SendSystemMessage(fmt::format("Travelling to {} first ({}'s request). They'll follow you.", cDestination, acMessage.RequesterName.c_str()));
        MoveToMarker(acMessage);
        break;

    case NotifyFastTravel::kApproved:
        m_askedToAnswer = false;
        if (acMessage.HostFirst)
        {
            // The host goes first; the requester's held-back travel is dropped and the map closed.
            if (cMine)
            {
                m_waitingForAnswers = false;
                ClearPendingFastTravel();
                if (UI* pUI = UI::Get())
                    pUI->CloseAllMenus();
            }
            overlay.SendSystemMessage(fmt::format("Fast travel to {} accepted. The host travels first, you'll follow once they've arrived.", cDestination));
        }
        else if (cMine)
        {
            m_waitingForAnswers = false;
            if (ExecutePendingFastTravel())
            {
                m_awaitingArrival = true;
                spdlog::info("[SkyrimCoop] Fast travel to '{}' approved, travelling", cDestination);
            }
            else
            {
                SendAction(FastTravelRequest::kCancel);
                overlay.SendSystemMessage("Fast travel cancelled: the map is no longer open.");
            }
        }
        else
            overlay.SendSystemMessage(fmt::format("Fast travel to {} accepted. You'll follow {} once they've arrived.", cDestination, acMessage.RequesterName.c_str()));
        break;

    case NotifyFastTravel::kDeclined:
        m_askedToAnswer = m_waitingForAnswers = false;
        ClearPendingFastTravel();
        overlay.SendSystemMessage(fmt::format("{} declined the fast travel to {}.", acMessage.AnswerName.c_str(), cDestination));
        break;

    case NotifyFastTravel::kCancelled:
        m_askedToAnswer = m_waitingForAnswers = m_awaitingArrival = false;
        ClearPendingFastTravel();
        overlay.SendSystemMessage(fmt::format("Fast travel to {} cancelled ({}).", cDestination, acMessage.AnswerName.c_str()));
        break;

    default: break;
    }
}

void FastTravelService::SendArrived() noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectCELL* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
    if (!pCell)
    {
        spdlog::error("[SkyrimCoop] Fast travel arrived, but the player has no cell");
        return;
    }

    FastTravelRequest request{};
    request.RequestAction = FastTravelRequest::kArrived;
    auto& modSystem = m_world.GetModSystem();
    modSystem.GetServerModId(pCell->formID, request.CellId);
    if (TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace())
        modSystem.GetServerModId(pWorldSpace->formID, request.WorldSpaceId);
    request.Position.x = pPlayer->position.x;
    request.Position.y = pPlayer->position.y;
    request.Position.z = pPlayer->position.z;

    spdlog::info("[SkyrimCoop] Fast travel arrived in cell {:X} at ({:.0f}, {:.0f}, {:.0f})", pCell->formID, pPlayer->position.x, pPlayer->position.y, pPlayer->position.z);
    m_transport.Send(request);
}

void FastTravelService::MoveToMarker(const NotifyFastTravel& acMessage) noexcept
{
    const uint32_t cMarkerId = m_world.GetModSystem().GetGameId(acMessage.MarkerId);
    TESObjectREFR* pMarker = Cast<TESObjectREFR>(TESForm::GetById(cMarkerId));
    if (!pMarker)
    {
        spdlog::error("[SkyrimCoop] Fast travel: map marker {:X} not found", cMarkerId);
        m_world.GetOverlayService().SendSystemMessage("Fast travel failed: destination not found.");
        SendAction(FastTravelRequest::kCancel);
        return;
    }

    // Exterior markers: load the grid cell at the marker; interior markers: their own cell.
    TESObjectCELL* pCell = nullptr;
    if (TESWorldSpace* pWorldSpace = pMarker->GetWorldSpace())
    {
        const GridCellCoords cCoords = GridCellCoords::CalculateGridCellCoords(pMarker->position.x, pMarker->position.y);
        pCell = pWorldSpace->LoadCell(cCoords.X, cCoords.Y);
    }
    if (!pCell)
        pCell = pMarker->GetParentCellEx();
    if (!pCell)
    {
        spdlog::error("[SkyrimCoop] Fast travel: no cell for map marker {:X}", cMarkerId);
        SendAction(FastTravelRequest::kCancel);
        return;
    }

    spdlog::info("[SkyrimCoop] Fast travel: host moving to marker {:X} in cell {:X} at ({:.0f}, {:.0f}, {:.0f})", cMarkerId, pCell->formID, pMarker->position.x, pMarker->position.y,
                 pMarker->position.z);
    m_moveTarget = pMarker->position;
    m_moveDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    m_awaitingMoveArrival = true;
    PlayerCharacter::Get()->MoveTo(pCell, pMarker->position);
}

BSTEventResult FastTravelService::OnEvent(const TESFastTravelEndEvent*, const EventDispatcher<TESFastTravelEndEvent>*)
{
    if (m_awaitingArrival)
    {
        m_awaitingArrival = false;
        m_world.GetRunner().Queue([this]() { SendArrived(); });
    }
    return BSTEventResult::kOk;
}
