#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

// SkyrimCoop fast-travel confirmation: everything a client says about a party fast travel.
struct FastTravelRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kFastTravelRequest;

    enum Action : uint8_t
    {
        kAsk = 0,     // I answered "Yes" in the map's fast-travel prompt (Destination and MarkerId are set)
        kAccept = 1,  // I accept the other player's fast travel
        kDecline = 2, // I decline it
        kCancel = 3,  // I withdraw my own request (e.g. closed the map)
        kArrived = 4, // I arrived after the approved fast travel (Cell/WorldSpace/Position are set)
    };

    FastTravelRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const FastTravelRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && RequestAction == acRhs.RequestAction && Destination == acRhs.Destination && CellId == acRhs.CellId &&
               WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && MarkerId == acRhs.MarkerId;
    }

    uint8_t RequestAction{kAsk};
    TiltedPhoques::String Destination{};
    GameId CellId{};
    GameId WorldSpaceId{};
    Vector3_NetQuantize Position{};
    GameId MarkerId{}; // the chosen map marker (so the host can be moved there first)
};
