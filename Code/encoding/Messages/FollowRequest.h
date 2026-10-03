#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

// SkyrimCoop follow offers: a player arrived somewhere new (fast travel or load door) and offers the
// others to follow, or a player accepts such an offer.
struct FollowRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kFollowRequest;

    enum Action : uint8_t
    {
        kOffer = 0,  // I arrived; Kind, Destination, CellId, WorldSpaceId and Position are set
        kAccept = 1, // I want to follow offer OfferId
    };

    enum TravelKind : uint8_t
    {
        kFastTravel = 0,
        kDoor = 1,
    };

    FollowRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const FollowRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && RequestAction == acRhs.RequestAction && Kind == acRhs.Kind && Destination == acRhs.Destination && CellId == acRhs.CellId &&
               WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && OfferId == acRhs.OfferId;
    }

    uint8_t RequestAction{kOffer};
    uint8_t Kind{kFastTravel};
    TiltedPhoques::String Destination{};
    GameId CellId{};
    GameId WorldSpaceId{};
    Vector3_NetQuantize Position{};
    uint32_t OfferId{};
};
