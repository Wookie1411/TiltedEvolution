#include <Messages/FollowRequest.h>

void FollowRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(RequestAction, 8);
    aWriter.WriteBits(Kind, 8);
    Serialization::WriteString(aWriter, Destination);
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, OfferId);
}

void FollowRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    uint64_t value = 0;
    aReader.ReadBits(value, 8);
    RequestAction = value & 0xFF;
    aReader.ReadBits(value, 8);
    Kind = value & 0xFF;
    Destination = Serialization::ReadString(aReader);
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    OfferId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
