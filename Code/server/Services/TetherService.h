#pragma once

struct World;
struct UpdateEvent;
struct Player;

/**
 * @brief SkyrimCoop tether: party members (partners) have to stay near the party leader (host).
 * Exterior: within the host's loaded 5x5 cell grid; interior: the same cell as the host.
 * Step 5: warnings only.
 */
class TetherService
{
public:
    TetherService(World& aWorld, entt::dispatcher& aDispatcher);

private:
    void OnUpdate(const UpdateEvent&) noexcept;

    bool IsTooFar(const Player* apPartner, const Player* apHost) const noexcept;
    static void SendSystemMessage(Player* apPlayer, const String& acText) noexcept;

    struct PartnerState
    {
        std::optional<std::chrono::steady_clock::time_point> TooFarSince;
        std::chrono::steady_clock::time_point LastWarning{};
        bool Warned = false;
    };

    World& m_world;
    TiltedPhoques::Map<uint32_t, PartnerState> m_partners; // player id -> state
    std::chrono::steady_clock::time_point m_nextCheck{};

    entt::scoped_connection m_updateConnection;
};
