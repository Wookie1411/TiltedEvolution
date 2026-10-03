#include <Services/TetherService.h>

#include <GameServer.h>
#include <World.h>
#include <Components.h>

#include <Events/UpdateEvent.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/TeleportCommandResponse.h>

#include <Setting.h>

namespace
{
Console::Setting bEnableTether{"SkyrimCoop:bEnableTether", "Party members must stay near the party leader (warning, then teleport)", true};

constexpr auto kCheckInterval = std::chrono::seconds(1);
// Being out of range this long counts (shorter gaps happen during loading screens).
constexpr auto kGracePeriod = std::chrono::seconds(3);
constexpr auto kWarningInterval = std::chrono::seconds(5);
// Still out of range this long after the first warning: move the partner to the host.
constexpr auto kTeleportAfter = std::chrono::seconds(10);
} // namespace

TetherService::TetherService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&TetherService::OnUpdate>(this);
}

void TetherService::Pause(uint32_t aPartyId, std::chrono::seconds aDuration) noexcept
{
    // Replaces any earlier pause, so e.g. "arrived" can shorten the long pause set at approval.
    m_pausedUntil[aPartyId] = std::chrono::steady_clock::now() + aDuration;
    spdlog::info("[SkyrimCoop] Tether paused for party {} ({} s)", aPartyId, aDuration.count());
}

void TetherService::BringToHost(Player* apPartner, const Player* apHost) noexcept
{
    const auto cHostCharacter = apHost->GetCharacter();
    const auto* pMovement = cHostCharacter ? m_world.try_get<MovementComponent>(*cHostCharacter) : nullptr;
    if (!pMovement)
        return;

    const auto& cHostCell = apHost->GetCellComponent();
    TeleportCommandResponse teleport{};
    teleport.CellId = cHostCell.Cell;
    teleport.WorldSpaceId = cHostCell.WorldSpaceId;
    teleport.Position.x = pMovement->Position.x;
    teleport.Position.y = pMovement->Position.y;
    teleport.Position.z = pMovement->Position.z;
    apPartner->Send(teleport);

    SendSystemMessage(apPartner, "You were brought back to " + apHost->GetUsername() + ".");
    spdlog::info("[SkyrimCoop] Tether: '{}' brought back to host '{}'", apPartner->GetUsername().c_str(), apHost->GetUsername().c_str());
}

void TetherService::SendSystemMessage(Player* apPlayer, const String& acText) noexcept
{
    NotifyChatMessageBroadcast message{};
    message.MessageType = kSystemMessage;
    message.ChatMessage = acText;
    apPlayer->Send(message);
}

bool TetherService::IsTooFar(const Player* apPartner, const Player* apHost) const noexcept
{
    const auto& cHostCell = apHost->GetCellComponent();
    const auto& cPartnerCell = apPartner->GetCellComponent();

    if (cHostCell.IsInInteriorCell() || cPartnerCell.IsInInteriorCell())
        return !(cPartnerCell.Cell == cHostCell.Cell);

    if (!(cPartnerCell.WorldSpaceId == cHostCell.WorldSpaceId))
        return true;

    // Same worldspace: compare the grid cells the two characters actually stand in.
    const auto cHostCharacter = apHost->GetCharacter();
    const auto cPartnerCharacter = apPartner->GetCharacter();
    if (!cHostCharacter || !cPartnerCharacter)
        return false;

    const auto* pHostMovement = m_world.try_get<MovementComponent>(*cHostCharacter);
    const auto* pPartnerMovement = m_world.try_get<MovementComponent>(*cPartnerCharacter);
    if (!pHostMovement || !pPartnerMovement)
        return false;

    const auto cHostCoords = GridCellCoords::CalculateGridCellCoords(pHostMovement->Position.x, pHostMovement->Position.y);
    const auto cPartnerCoords = GridCellCoords::CalculateGridCellCoords(pPartnerMovement->Position.x, pPartnerMovement->Position.y);
    return !GridCellCoords::IsCellInGridCell(cPartnerCoords, cHostCoords, false);
}

void TetherService::OnUpdate(const UpdateEvent&) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < m_nextCheck)
        return;
    m_nextCheck = cNow + kCheckInterval;

    if (!bEnableTether)
    {
        m_partners.clear();
        return;
    }

    auto& partyService = m_world.GetPartyService();
    auto& playerManager = m_world.GetPlayerManager();
    Vector<uint32_t> seen;

    for (Player* pPartner : playerManager)
    {
        const auto cPartyId = pPartner->GetParty().JoinedPartyId;
        const auto* pParty = cPartyId ? partyService.GetById(*cPartyId) : nullptr;
        if (!pParty || pParty->Members.size() < 2 || pParty->LeaderPlayerId == pPartner->GetId())
            continue;

        Player* pHost = playerManager.GetById(pParty->LeaderPlayerId);
        if (!pHost)
            continue;

        seen.push_back(pPartner->GetId());
        auto& state = m_partners[pPartner->GetId()];

        // Joint travel in progress (door vote, fast travel): stay quiet and start over afterwards.
        if (auto it = m_pausedUntil.find(*cPartyId); it != m_pausedUntil.end() && cNow < it->second)
        {
            state = {};
            continue;
        }

        if (!IsTooFar(pPartner, pHost))
        {
            if (state.Warned)
            {
                SendSystemMessage(pPartner, "You're back near " + pHost->GetUsername() + ".");
                spdlog::info("[SkyrimCoop] Tether: '{}' back near host '{}'", pPartner->GetUsername().c_str(), pHost->GetUsername().c_str());
            }
            state = {};
            continue;
        }

        if (!state.TooFarSince)
            state.TooFarSince = cNow;

        if (cNow - *state.TooFarSince < kGracePeriod)
            continue;

        if (state.Warned && cNow - *state.TooFarSince >= kGracePeriod + kTeleportAfter)
        {
            BringToHost(pPartner, pHost);
            state = {};
            Pause(*cPartyId, std::chrono::seconds(15)); // let the loading screen finish
            continue;
        }

        if (state.Warned && cNow - state.LastWarning < kWarningInterval)
            continue;

        const auto cSecondsLeft = std::chrono::duration_cast<std::chrono::seconds>(kGracePeriod + kTeleportAfter - (cNow - *state.TooFarSince)).count();
        SendSystemMessage(pPartner, "You're too far from " + pHost->GetUsername() + ". Go back, or you'll be brought back in " + std::to_string(cSecondsLeft).c_str() + " s.");
        if (!state.Warned)
            spdlog::info("[SkyrimCoop] Tether: '{}' too far from host '{}'", pPartner->GetUsername().c_str(), pHost->GetUsername().c_str());
        state.Warned = true;
        state.LastWarning = cNow;
    }

    // Forget players who left, left the party or became the host.
    Vector<uint32_t> stale;
    for (const auto& [id, state] : m_partners)
    {
        if (std::find(seen.begin(), seen.end(), id) == seen.end())
            stale.push_back(id);
    }
    for (uint32_t id : stale)
        m_partners.erase(id);
}
