#pragma once

struct World;
struct UpdateEvent;
struct Player;

/**
 * @brief SkyrimCoop tether: party members (partners) have to stay near the party leader (host).
 * Exterior: within the host's loaded 5x5 cell grid; interior: the same cell as the host.
 * Out of range for a while -> warnings; still out of range after kTeleportAfter -> moved to the host.
 */
class TetherService
{
public:
    TetherService(World& aWorld, entt::dispatcher& aDispatcher);

    // Joint travel (door vote, fast travel) in progress: don't warn or pull anyone in this party for a while.
    void Pause(uint32_t aPartyId, std::chrono::seconds aDuration) noexcept;

private:
    void OnUpdate(const UpdateEvent&) noexcept;

    bool IsTooFar(const Player* apPartner, const Player* apHost) const noexcept;
    static void SendSystemMessage(Player* apPlayer, const String& acText) noexcept;
    void BringToHost(Player* apPartner, const Player* apHost) noexcept;

    struct PartnerState
    {
        std::optional<std::chrono::steady_clock::time_point> TooFarSince;
        std::chrono::steady_clock::time_point LastWarning{};
        bool Warned = false;
    };

    World& m_world;
    TiltedPhoques::Map<uint32_t, PartnerState> m_partners; // player id -> state
    TiltedPhoques::Map<uint32_t, std::chrono::steady_clock::time_point> m_pausedUntil; // party id -> end of pause
    std::chrono::steady_clock::time_point m_nextCheck{};

    entt::scoped_connection m_updateConnection;
};
