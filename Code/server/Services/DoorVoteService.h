#pragma once

#include <Events/PacketEvent.h>

struct World;
struct UpdateEvent;
struct DoorVoteRequest;
struct NotifyDoorVote;

/**
 * @brief SkyrimCoop door voting: a load door only opens once every member of the party
 * has pressed E on the same door. Holds one open vote per party.
 */
class DoorVoteService
{
public:
    DoorVoteService(World& aWorld, entt::dispatcher& aDispatcher);

private:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDoorVoteRequest(const PacketEvent<DoorVoteRequest>& acMessage) noexcept;

    void SendToParty(uint32_t aPartyId, const NotifyDoorVote& acMessage) const noexcept;

    struct Vote
    {
        GameId DoorId;
        Vector<uint32_t> Voters;
        std::chrono::steady_clock::time_point LastVote;
    };

    World& m_world;
    TiltedPhoques::Map<uint32_t, Vote> m_votes; // party id -> open vote

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_doorVoteConnection;
};
