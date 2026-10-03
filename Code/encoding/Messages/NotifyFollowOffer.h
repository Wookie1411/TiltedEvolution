#pragma once

#include "Message.h"

// SkyrimCoop follow offers: tells the other party members that someone travelled somewhere and
// they can follow by accepting OfferId (the client shows "Press Y within 10 s to follow").
struct NotifyFollowOffer final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyFollowOffer;

    NotifyFollowOffer()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyFollowOffer& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && OfferId == acRhs.OfferId && Kind == acRhs.Kind && TravellerName == acRhs.TravellerName && Destination == acRhs.Destination;
    }

    uint32_t OfferId{};
    uint8_t Kind{}; // FollowRequest::TravelKind
    TiltedPhoques::String TravellerName{};
    TiltedPhoques::String Destination{};
};
