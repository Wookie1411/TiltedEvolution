#include <Services/DoorVoteService.h>

#include <GameServer.h>
#include <World.h>

#include <Messages/DoorVoteRequest.h>
#include <Messages/NotifyDoorVote.h>

namespace
{
// A vote nobody has added to for this long starts over.
constexpr auto kVoteTimeout = std::chrono::seconds(60);
} // namespace

DoorVoteService::DoorVoteService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_doorVoteConnection = aDispatcher.sink<PacketEvent<DoorVoteRequest>>().connect<&DoorVoteService::OnDoorVoteRequest>(this);
}

void DoorVoteService::OnDoorVoteRequest(const PacketEvent<DoorVoteRequest>& acMessage) noexcept
{
    Player* pVoter = acMessage.pPlayer;
    const auto& cDoorId = acMessage.Packet.DoorId;

    NotifyDoorVote notify{};
    notify.DoorId = cDoorId;
    notify.VoterId = pVoter->GetId();
    notify.VoterName = pVoter->GetUsername();

    const auto cPartyId = pVoter->GetParty().JoinedPartyId;
    const auto* pParty = cPartyId ? m_world.GetPartyService().GetById(*cPartyId) : nullptr;

    // Alone (the client thought it had a partner, but the party changed meanwhile): just let it through.
    if (!pParty || pParty->Members.size() < 2)
    {
        notify.VoteStatus = NotifyDoorVote::kPassed;
        notify.Votes = notify.Needed = 1;
        pVoter->Send(notify);
        return;
    }

    const auto cNow = std::chrono::steady_clock::now();
    auto& vote = m_votes[*cPartyId];

    // A vote for another door, or a stale one, starts a new vote.
    if (!(vote.DoorId == cDoorId) || cNow - vote.LastVote > kVoteTimeout)
    {
        vote.DoorId = cDoorId;
        vote.Voters.clear();
    }
    vote.LastVote = cNow;

    if (std::find(vote.Voters.begin(), vote.Voters.end(), pVoter->GetId()) == vote.Voters.end())
        vote.Voters.push_back(pVoter->GetId());

    // Count only voters who are still in the party.
    uint32_t votes = 0;
    for (const Player* pMember : pParty->Members)
    {
        if (std::find(vote.Voters.begin(), vote.Voters.end(), pMember->GetId()) != vote.Voters.end())
            ++votes;
    }

    const bool cPassed = votes >= pParty->Members.size();
    notify.VoteStatus = cPassed ? NotifyDoorVote::kPassed : NotifyDoorVote::kWaiting;
    notify.Votes = static_cast<uint8_t>(votes);
    notify.Needed = static_cast<uint8_t>(pParty->Members.size());

    spdlog::info("[SkyrimCoop] Door vote in party {}: '{}' voted for door {:X}:{:X} ({}/{}){}", *cPartyId, pVoter->GetUsername().c_str(), cDoorId.ModId, cDoorId.BaseId,
                 notify.Votes, notify.Needed, cPassed ? " -> passed" : "");

    for (Player* pMember : pParty->Members)
        pMember->Send(notify);

    if (cPassed)
        m_votes.erase(*cPartyId);
}
