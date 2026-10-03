#pragma once

#include <Events/EventDispatcher.h>
#include <Games/Events.h>

struct World;
struct TransportService;
struct UpdateEvent;
struct NotifyFollowOffer;

/**
 * @brief SkyrimCoop follow offers (client side). Nobody waits for anybody: when the local player
 * arrives somewhere after a fast travel or a load door, the other party members get a 10 s offer
 * ("Press Y to follow"); accepting moves them to where this player arrived.
 */
class FollowService final : public BSTEventSink<TESFastTravelEndEvent>
{
public:
    FollowService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport);

    // Called from game hooks (any thread).
    void OnLocalFastTravelConfirmed(const String& acDestination) noexcept;
    void OnLocalLoadDoorUsed(const String& acDestination, uint32_t aOriginCellId) noexcept;

private:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnNotifyFollowOffer(const NotifyFollowOffer&) noexcept;
    BSTEventResult OnEvent(const TESFastTravelEndEvent*, const EventDispatcher<TESFastTravelEndEvent>*) override;

    void SendOffer(uint8_t aKind, const String& acDestination) noexcept;

    World& m_world;
    TransportService& m_transport;

    // My own trips, until I've arrived.
    bool m_fastTravelling = false;
    String m_fastTravelDestination;
    struct DoorTrip
    {
        String Destination;
        uint32_t OriginCellId{};
        std::chrono::steady_clock::time_point Deadline;
    };
    std::optional<DoorTrip> m_doorTrip;

    // Someone else's offer I can accept with Y.
    struct PendingOffer
    {
        uint32_t OfferId{};
        String TravellerName;
        std::chrono::steady_clock::time_point Until;
    };
    std::optional<PendingOffer> m_offer;
    bool m_yWasDown = false;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_offerConnection;
};
