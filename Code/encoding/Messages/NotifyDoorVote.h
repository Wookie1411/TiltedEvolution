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
               Votes == acRhs.Votes && Needed == acRhs.Needed;
    }

    uint8_t VoteStatus{kWaiting};
    GameId DoorId;
    uint32_t VoterId{};
    TiltedPhoques::String VoterName{};
    uint8_t Votes{};
    uint8_t Needed{};
};
