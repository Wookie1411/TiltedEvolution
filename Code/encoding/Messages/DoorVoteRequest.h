#pragma once

#include "Message.h"

#include <Structs/GameId.h>

// SkyrimCoop: the local player pressed E on a load door while in a party.
struct DoorVoteRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kDoorVoteRequest;

    DoorVoteRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const DoorVoteRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && DoorId == acRhs.DoorId && CellId == acRhs.CellId; }

    GameId DoorId;
    GameId CellId;
};
