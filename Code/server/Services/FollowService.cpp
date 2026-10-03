#include <Services/FollowService.h>

#include <GameServer.h>
#include <World.h>

#include <Messages/FollowRequest.h>
#include <Messages/NotifyFollowOffer.h>
#include <Messages/TeleportCommandResponse.h>

#include <Setting.h>

namespace
{
Console::Setting bEnableFollowOffers{"SkyrimCoop:bEnableFollowOffers", "After a fast travel or load door, offer the other party members to follow (press Y)", true};

// Clients show the offer for 10 s; accept a little longer to cover latency.
constexpr auto kOfferLifetime = std::chrono::seconds(15);
} // namespace

FollowService::FollowService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_requestConnection = aDispatcher.sink<PacketEvent<FollowRequest>>().connect<&FollowService::OnFollowRequest>(this);
}

void FollowService::OnFollowRequest(const PacketEvent<FollowRequest>& acMessage) noexcept
{
    Player* pPlayer = acMessage.pPlayer;
    const auto& cPacket = acMessage.Packet;
    const auto cNow = std::chrono::steady_clock::now();

    // Drop expired offers.
    Vector<uint32_t> expired;
    for (const auto& [id, offer] : m_offers)
    {
        if (cNow > offer.Expires)
            expired.push_back(id);
    }
    for (uint32_t id : expired)
        m_offers.erase(id);

    const auto cPartyId = pPlayer->GetParty().JoinedPartyId;
    const auto* pParty = cPartyId ? m_world.GetPartyService().GetById(*cPartyId) : nullptr;

    if (cPacket.RequestAction == FollowRequest::kOffer)
    {
        if (!bEnableFollowOffers || !pParty || pParty->Members.size() < 2)
            return;

        // A new trip replaces the traveller's older offer.
        Vector<uint32_t> older;
        for (const auto& [id, offer] : m_offers)
        {
            if (offer.TravellerId == pPlayer->GetId())
                older.push_back(id);
        }
        for (uint32_t id : older)
            m_offers.erase(id);

        const uint32_t cOfferId = m_nextOfferId++;
        m_offers[cOfferId] = Offer{pPlayer->GetId(), *cPartyId, cPacket.CellId, cPacket.WorldSpaceId, cPacket.Position, cNow + kOfferLifetime};

        NotifyFollowOffer notify{};
        notify.OfferId = cOfferId;
        notify.Kind = cPacket.Kind;
        notify.TravellerName = pPlayer->GetUsername();
        notify.Destination = cPacket.Destination;
        for (Player* pMember : pParty->Members)
        {
            if (pMember != pPlayer)
                pMember->Send(notify);
        }

        spdlog::info("[SkyrimCoop] Follow offer {}: '{}' {} '{}'", cOfferId, pPlayer->GetUsername().c_str(), cPacket.Kind == FollowRequest::kDoor ? "went into" : "fast travelled to",
                     cPacket.Destination.c_str());
        return;
    }

    if (cPacket.RequestAction == FollowRequest::kAccept)
    {
        auto it = m_offers.find(cPacket.OfferId);
        if (it == m_offers.end() || !cPartyId || it->second.PartyId != *cPartyId || it->second.TravellerId == pPlayer->GetId())
        {
            spdlog::info("[SkyrimCoop] Follow offer {}: '{}' accepted too late or not allowed", cPacket.OfferId, pPlayer->GetUsername().c_str());
            return;
        }

        TeleportCommandResponse teleport{};
        teleport.CellId = it->second.CellId;
        teleport.WorldSpaceId = it->second.WorldSpaceId;
        teleport.Position = it->second.Position;
        pPlayer->Send(teleport);

        spdlog::info("[SkyrimCoop] Follow offer {}: '{}' follows", cPacket.OfferId, pPlayer->GetUsername().c_str());
    }
}
