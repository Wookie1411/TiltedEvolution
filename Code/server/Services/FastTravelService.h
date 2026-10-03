#pragma once

#include <Events/PacketEvent.h>

struct World;
struct UpdateEvent;
struct FastTravelRequest;
struct NotifyFastTravel;

/**
 * @brief SkyrimCoop fast-travel confirmation. When a party member confirms a fast travel on the
 * map, the other members are asked first. If all accept, the requester travels; once they report
 * their arrival, the others are moved to them. One open request per party.
 */
class FastTravelService
{
public:
    FastTravelService(World& aWorld, entt::dispatcher& aDispatcher);

private:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnFastTravelRequest(const PacketEvent<FastTravelRequest>& acMessage) noexcept;

    void SendToParty(uint32_t aPartyId, const NotifyFastTravel& acMessage) const noexcept;
    void Cancel(uint32_t aPartyId, const char* acReason) noexcept;

    struct Request
    {
        uint32_t RequesterId{};
        String RequesterName{};
        String Destination{};
        Vector<uint32_t> Accepted;
        bool Approved = false;
        std::chrono::steady_clock::time_point Started;
    };

    World& m_world;
    TiltedPhoques::Map<uint32_t, Request> m_requests; // party id -> open request

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_requestConnection;
};
