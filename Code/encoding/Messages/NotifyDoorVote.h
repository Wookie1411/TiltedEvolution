#pragma once

#include "Message.h"

#include <Structs/GameId.h>

// SkyrimCoop: state of the party's door vote, sent to every party member.
struct NotifyDoorVote final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyDoorVote;

    enum Status : uint8_t
    {
        kWaiting = 0, // someone voted, not everyone yet
        kPassed = 1,  // everyone voted for DoorId: go through it now
        kCancelled = 2, // vote is gone, see CancelReason
        kHostGoing = 3, // to the partners: vote passed, the host (VoterName) goes through first; kPassed follows
    };

    enum CancelReason : uint8_t
    {
        kWithdrawn = 0,  // VoterId pressed E on the same door again
        kExpired = 1,    // nobody added a vote for a while
        kPlayerLeft = 2, // a voter left the party or the server
    };

    NotifyDoorVote()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyDoorVote& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && VoteStatus == acRhs.VoteStatus && DoorId == acRhs.DoorId && VoterId == acRhs.VoterId && VoterName == acRhs.VoterName &&
               Votes == acRhs.Votes && Needed == acRhs.Needed && Reason == acRhs.Reason;
    }

    uint8_t VoteStatus{kWaiting};
    GameId DoorId;
    uint32_t VoterId{};
    TiltedPhoques::String VoterName{};
    uint8_t Votes{};
    uint8_t Needed{};
    uint8_t Reason{};
};
