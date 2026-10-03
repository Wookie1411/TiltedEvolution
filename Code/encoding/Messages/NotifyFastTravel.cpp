#include <Messages/NotifyFastTravel.h>

void NotifyFastTravel::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(TravelEvent, 8);
    Serialization::WriteVarInt(aWriter, RequesterId);
    Serialization::WriteString(aWriter, RequesterName);
    Serialization::WriteString(aWriter, Destination);
    Serialization::WriteString(aWriter, AnswerName);
    MarkerId.Serialize(aWriter);
    Serialization::WriteBool(aWriter, HostFirst);
}

void NotifyFastTravel::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    uint64_t value = 0;
    aReader.ReadBits(value, 8);
    TravelEvent = value & 0xFF;
    RequesterId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RequesterName = Serialization::ReadString(aReader);
    Destination = Serialization::ReadString(aReader);
    AnswerName = Serialization::ReadString(aReader);
    MarkerId.Deserialize(aReader);
    HostFirst = Serialization::ReadBool(aReader);
}
