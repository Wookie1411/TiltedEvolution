#pragma once

#include <Events/PacketEvent.h>
#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

struct World;
struct FollowRequest;

/**
 * @brief SkyrimCoop follow offers. When a party member arrives somewhere after a fast travel or a
 * load door, the other members get a short offer to follow (press Y). Accepting moves them to the
 * spot where the traveller arrived. Nobody waits for anybody.
 */
class FollowService
{
public:
    FollowService(World& aWorld, entt::dispatcher& aDispatcher);

private:
    void OnFollowRequest(const PacketEvent<FollowRequest>& acMessage) noexcept;

    struct Offer
    {
        uint32_t TravellerId{};
        uint32_t PartyId{};
        GameId CellId{};
        GameId WorldSpaceId{};
        Vector3_NetQuantize Position{};
        std::chrono::steady_clock::time_point Expires;
    };

    World& m_world;
    TiltedPhoques::Map<uint32_t, Offer> m_offers; // offer id -> offer
    uint32_t m_nextOfferId = 1;

    entt::scoped_connection m_requestConnection;
};
