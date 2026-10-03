#include <Services/FastTravelService.h>

#include <GameServer.h>
#include <World.h>

#include <Events/UpdateEvent.h>
#include <Messages/FastTravelRequest.h>
#include <Messages/NotifyFastTravel.h>
#include <Messages/TeleportCommandResponse.h>

namespace
{
// Time the others have to answer.
constexpr auto kAnswerTimeout = std::chrono::seconds(30);
// Time the requester has to arrive after approval before the request is dropped.
constexpr auto kArrivalTimeout = std::chrono::seconds(90);

bool Contains(const Vector<uint32_t>& acIds, uint32_t aId) noexcept
{
    return std::find(acIds.begin(), acIds.end(), aId) != acIds.end();
}
} // namespace

FastTravelService::FastTravelService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&FastTravelService::OnUpdate>(this);
    m_requestConnection = aDispatcher.sink<PacketEvent<FastTravelRequest>>().connect<&FastTravelService::OnFastTravelRequest>(this);
}

void FastTravelService::SendToParty(uint32_t aPartyId, const NotifyFastTravel& acMessage) const noexcept
{
    if (const auto* pParty = m_world.GetPartyService().GetById(aPartyId))
    {
        for (Player* pMember : pParty->Members)
            pMember->Send(acMessage);
    }
}

void FastTravelService::Cancel(uint32_t aPartyId, const char* acReason) noexcept
{
    auto it = m_requests.find(aPartyId);
    if (it == m_requests.end())
        return;

    NotifyFastTravel notify{};
    notify.TravelEvent = NotifyFastTravel::kCancelled;
    notify.RequesterId = it->second.RequesterId;
    notify.RequesterName = it->second.RequesterName;
    notify.Destination = it->second.Destination;
    notify.AnswerName = acReason;

    spdlog::info("[SkyrimCoop] Fast travel in party {} cancelled ({})", aPartyId, acReason);
    SendToParty(aPartyId, notify);
    m_requests.erase(aPartyId);
}

// Drops requests nobody answered, whose requester never arrived, or whose party changed.
void FastTravelService::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_requests.empty())
        return;

    const auto cNow = std::chrono::steady_clock::now();
    Vector<std::pair<uint32_t, const char*>> toCancel;

    for (const auto& [partyId, request] : m_requests)
    {
        const auto* pParty = m_world.GetPartyService().GetById(partyId);
        const bool cRequesterInParty = pParty && std::any_of(pParty->Members.begin(), pParty->Members.end(), [&](const Player* p) { return p->GetId() == request.RequesterId; });

        if (!pParty || pParty->Members.size() < 2 || !cRequesterInParty)
            toCancel.emplace_back(partyId, "party changed");
        else if (!request.Approved && cNow - request.Started > kAnswerTimeout)
            toCancel.emplace_back(partyId, "no answer");
        else if (request.Approved && cNow - request.Started > kArrivalTimeout)
            toCancel.emplace_back(partyId, "no arrival");
    }

    for (const auto& [partyId, reason] : toCancel)
        Cancel(partyId, reason);
}

void FastTravelService::OnFastTravelRequest(const PacketEvent<FastTravelRequest>& acMessage) noexcept
{
    Player* pPlayer = acMessage.pPlayer;
    const auto& cPacket = acMessage.Packet;

    const auto cPartyId = pPlayer->GetParty().JoinedPartyId;
    const auto* pParty = cPartyId ? m_world.GetPartyService().GetById(*cPartyId) : nullptr;

    NotifyFastTravel notify{};

    if (cPacket.RequestAction == FastTravelRequest::kAsk)
    {
        notify.RequesterId = pPlayer->GetId();
        notify.RequesterName = pPlayer->GetUsername();
        notify.Destination = cPacket.Destination;

        // Alone: nothing to ask, travel right away.
        if (!pParty || pParty->Members.size() < 2)
        {
            notify.TravelEvent = NotifyFastTravel::kApproved;
            pPlayer->Send(notify);
            return;
        }

        if (m_requests.find(*cPartyId) != m_requests.end())
            Cancel(*cPartyId, "replaced by a new request");

        Request request{};
        request.RequesterId = pPlayer->GetId();
        request.RequesterName = pPlayer->GetUsername();
        request.Destination = cPacket.Destination;
        request.Started = std::chrono::steady_clock::now();
        m_requests[*cPartyId] = std::move(request);

        spdlog::info("[SkyrimCoop] Fast travel in party {}: '{}' asks to travel to '{}'", *cPartyId, pPlayer->GetUsername().c_str(), cPacket.Destination.c_str());

        for (Player* pMember : pParty->Members)
        {
            notify.TravelEvent = pMember == pPlayer ? NotifyFastTravel::kWaiting : NotifyFastTravel::kAsked;
            pMember->Send(notify);
        }
        return;
    }

    if (!pParty || m_requests.find(*cPartyId) == m_requests.end())
        return;

    Request& request = m_requests[*cPartyId];
    notify.RequesterId = request.RequesterId;
    notify.RequesterName = request.RequesterName;
    notify.Destination = request.Destination;
    notify.AnswerName = pPlayer->GetUsername();

    switch (cPacket.RequestAction)
    {
    case FastTravelRequest::kAccept:
    {
        if (request.Approved || pPlayer->GetId() == request.RequesterId)
            return;
        if (!Contains(request.Accepted, pPlayer->GetId()))
            request.Accepted.push_back(pPlayer->GetId());

        spdlog::info("[SkyrimCoop] Fast travel in party {}: '{}' accepted", *cPartyId, pPlayer->GetUsername().c_str());

        const bool cEveryoneAccepted = std::all_of(pParty->Members.begin(), pParty->Members.end(),
                                                   [&](const Player* p) { return p->GetId() == request.RequesterId || Contains(request.Accepted, p->GetId()); });
        if (cEveryoneAccepted)
        {
            request.Approved = true;
            request.Started = std::chrono::steady_clock::now();
            notify.TravelEvent = NotifyFastTravel::kApproved;
            SendToParty(*cPartyId, notify);
        }
        break;
    }
    case FastTravelRequest::kDecline:
    {
        if (request.Approved || pPlayer->GetId() == request.RequesterId)
            return;

        spdlog::info("[SkyrimCoop] Fast travel in party {}: '{}' declined", *cPartyId, pPlayer->GetUsername().c_str());
        notify.TravelEvent = NotifyFastTravel::kDeclined;
        SendToParty(*cPartyId, notify);
        m_requests.erase(*cPartyId);
        break;
    }
    case FastTravelRequest::kCancel:
    {
        if (pPlayer->GetId() == request.RequesterId && !request.Approved)
            Cancel(*cPartyId, "withdrawn");
        break;
    }
    case FastTravelRequest::kArrived:
    {
        if (pPlayer->GetId() != request.RequesterId || !request.Approved)
            return;

        // Bring everyone else to where the requester arrived (they load in after the requester).
        TeleportCommandResponse teleport{};
        teleport.CellId = cPacket.CellId;
        teleport.WorldSpaceId = cPacket.WorldSpaceId;
        teleport.Position = cPacket.Position;

        spdlog::info("[SkyrimCoop] Fast travel in party {}: '{}' arrived at '{}', bringing the others", *cPartyId, pPlayer->GetUsername().c_str(), request.Destination.c_str());
        for (Player* pMember : pParty->Members)
        {
            if (pMember != pPlayer)
                pMember->Send(teleport);
        }
        m_requests.erase(*cPartyId);
        break;
    }
    default: break;
    }
}
