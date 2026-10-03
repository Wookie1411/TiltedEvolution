#pragma once

#include <Events/EventDispatcher.h>
#include <Games/Events.h>

struct World;
struct TransportService;
struct UpdateEvent;
struct NotifyFastTravel;

/**
 * @brief SkyrimCoop fast-travel confirmation (client side).
 * The map's "Yes" is held back by the FastTravelConfirm hook and turned into a request; the other
 * party members answer with Y / N. On approval the requester travels and reports the arrival;
 * the server then moves the others there.
 */
class FastTravelService final : public BSTEventSink<TESFastTravelEndEvent>
{
public:
    FastTravelService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport);

    // Called from the hook (game thread) when the local player confirmed a fast travel while in a party.
    void OnLocalFastTravelConfirmed(const String& acDestination, uint32_t aMarkerFormId) noexcept;

private:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnNotifyFastTravel(const NotifyFastTravel&) noexcept;
    BSTEventResult OnEvent(const TESFastTravelEndEvent*, const EventDispatcher<TESFastTravelEndEvent>*) override;

    void SendAction(uint8_t aAction) noexcept;
    void SendArrived() noexcept;
    void MoveToMarker(const NotifyFastTravel& acMessage) noexcept;

    World& m_world;
    TransportService& m_transport;

    bool m_waitingForAnswers = false; // my own request is open
    bool m_awaitingArrival = false;   // my approved fast travel is in progress
    bool m_awaitingMoveArrival = false; // host only: moved to a partner's destination, waiting to land
    NiPoint3 m_moveTarget{};
    std::chrono::steady_clock::time_point m_moveDeadline{};
    bool m_askedToAnswer = false;     // someone else's request waits for my Y / N
    std::chrono::steady_clock::time_point m_askedUntil{};
    bool m_yWasDown = false;
    bool m_nWasDown = false;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_notifyConnection;
};
