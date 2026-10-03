#include <Messages/NotifyFollowOffer.h>

void NotifyFollowOffer::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, OfferId);
    aWriter.WriteBits(Kind, 8);
    Serialization::WriteString(aWriter, TravellerName);
    Serialization::WriteString(aWriter, Destination);
}

void NotifyFollowOffer::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    OfferId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    uint64_t value = 0;
    aReader.ReadBits(value, 8);
    Kind = value & 0xFF;
    TravellerName = Serialization::ReadString(aReader);
    Destination = Serialization::ReadString(aReader);
}
