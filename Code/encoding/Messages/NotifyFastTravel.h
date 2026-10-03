#pragma once

#include "Message.h"

// SkyrimCoop fast-travel confirmation: what the server tells party members.
struct NotifyFastTravel final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyFastTravel;

    enum Event : uint8_t
    {
        kAsked = 0,     // to the others: RequesterName wants to travel to Destination (answer Y/N)
        kWaiting = 1,   // to the requester: waiting for the others
        kApproved = 2,  // to everyone: all accepted; the requester travels now, the others follow after arrival
        kDeclined = 3,  // to everyone: AnswerName declined
        kCancelled = 4, // to everyone: the requester withdrew, someone left, or nobody answered in time
    };

    NotifyFastTravel()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyFastTravel& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && TravelEvent == acRhs.TravelEvent && RequesterId == acRhs.RequesterId && RequesterName == acRhs.RequesterName &&
               Destination == acRhs.Destination && AnswerName == acRhs.AnswerName;
    }

    uint8_t TravelEvent{kAsked};
    uint32_t RequesterId{};
    TiltedPhoques::String RequesterName{};
    TiltedPhoques::String Destination{};
    TiltedPhoques::String AnswerName{};
};
