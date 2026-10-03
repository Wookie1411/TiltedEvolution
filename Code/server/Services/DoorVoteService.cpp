#include <Services/DoorVoteService.h>

#include <GameServer.h>
#include <World.h>

#include <Events/UpdateEvent.h>
#include <Messages/DoorVoteRequest.h>
#include <Messages/NotifyDoorVote.h>

namespace
{
// A vote nobody has added to for this long is cancelled.
constexpr auto kVoteTimeout = std::chrono::seconds(60);

bool Contains(const Vector<uint32_t>& acIds, uint32_t aId) noexcept
{
    return std::find(acIds.begin(), acIds.end(), aId) != acIds.end();
}
} // namespace

DoorVoteService::DoorVoteService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&DoorVoteService::OnUpdate>(this);
    m_doorVoteConnection = aDispatcher.sink<PacketEvent<DoorVoteRequest>>().connect<&DoorVoteService::OnDoorVoteRequest>(this);
}

void DoorVoteService::SendToParty(uint32_t aPartyId, const NotifyDoorVote& acMessage) const noexcept
{
    if (const auto* pParty = m_world.GetPartyService().GetById(aPartyId))
    {
        for (Player* pMember : pParty->Members)
            pMember->Send(acMessage);
    }
}

// Cancels votes that expired or lost a voter (left the party or the server).
void DoorVoteService::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_votes.empty())
        return;

    const auto cNow = std::chrono::steady_clock::now();
    Vector<uint32_t> finished;

    for (auto& [partyId, vote] : m_votes)
    {
        const auto* pParty = m_world.GetPartyService().GetById(partyId);
        if (!pParty)
        {
            finished.push_back(partyId);
            continue;
        }

        NotifyDoorVote notify{};
        notify.VoteStatus = NotifyDoorVote::kCancelled;
        notify.DoorId = vote.DoorId;
        notify.Needed = static_cast<uint8_t>(pParty->Members.size());

        // Nobody left to wait for (e.g. the partner disconnected) also counts as "a player left".
        bool voterLeft = pParty->Members.size() < 2;
        for (uint32_t voterId : vote.Voters)
        {
            const bool cStillMember = std::any_of(pParty->Members.begin(), pParty->Members.end(), [voterId](const Player* pMember) { return pMember->GetId() == voterId; });
            if (!cStillMember)
            {
                voterLeft = true;
                notify.VoterId = voterId;
            }
        }

        if (voterLeft)
            notify.Reason = NotifyDoorVote::kPlayerLeft;
        else if (cNow - vote.LastVote > kVoteTimeout)
            notify.Reason = NotifyDoorVote::kExpired;
        else
            continue;

        spdlog::info("[SkyrimCoop] Door vote in party {} cancelled ({})", partyId, voterLeft ? "voter left" : "expired");
        SendToParty(partyId, notify);
        finished.push_back(partyId);
    }

    for (uint32_t partyId : finished)
        m_votes.erase(partyId);
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

    notify.Needed = static_cast<uint8_t>(pParty->Members.size());

    const auto cNow = std::chrono::steady_clock::now();
    // (TiltedPhoques::Map iterators are read-only, so modify through operator[].)
    Vote* pOpenVote = m_votes.find(*cPartyId) != m_votes.end() ? &m_votes[*cPartyId] : nullptr;

    // Pressing E again on the door you already voted for withdraws your vote.
    if (pOpenVote && pOpenVote->DoorId == cDoorId && Contains(pOpenVote->Voters, pVoter->GetId()) && cNow - pOpenVote->LastVote <= kVoteTimeout)
    {
        auto& voters = pOpenVote->Voters;
        voters.erase(std::find(voters.begin(), voters.end(), pVoter->GetId()));

        notify.VoteStatus = NotifyDoorVote::kCancelled;
        notify.Reason = NotifyDoorVote::kWithdrawn;
        notify.Votes = static_cast<uint8_t>(voters.size());

        spdlog::info("[SkyrimCoop] Door vote in party {}: '{}' withdrew from door {:X}:{:X}", *cPartyId, pVoter->GetUsername().c_str(), cDoorId.ModId, cDoorId.BaseId);
        SendToParty(*cPartyId, notify);

        if (voters.empty())
            m_votes.erase(*cPartyId);
        return;
    }

    auto& vote = m_votes[*cPartyId];

    // A vote for another door, or a stale one, starts a new vote.
    if (!(vote.DoorId == cDoorId) || cNow - vote.LastVote > kVoteTimeout)
    {
        vote.DoorId = cDoorId;
        vote.Voters.clear();
    }
    vote.LastVote = cNow;

    if (!Contains(vote.Voters, pVoter->GetId()))
        vote.Voters.push_back(pVoter->GetId());

    // Count only voters who are still in the party.
    uint32_t votes = 0;
    for (const Player* pMember : pParty->Members)
    {
        if (Contains(vote.Voters, pMember->GetId()))
            ++votes;
    }

    const bool cPassed = votes >= pParty->Members.size();
    notify.VoteStatus = cPassed ? NotifyDoorVote::kPassed : NotifyDoorVote::kWaiting;
    notify.Votes = static_cast<uint8_t>(votes);

    spdlog::info("[SkyrimCoop] Door vote in party {}: '{}' voted for door {:X}:{:X} ({}/{}){}", *cPartyId, pVoter->GetUsername().c_str(), cDoorId.ModId, cDoorId.BaseId,
                 notify.Votes, notify.Needed, cPassed ? " -> passed" : "");

    SendToParty(*cPartyId, notify);

    if (cPassed)
        m_votes.erase(*cPartyId);
}
