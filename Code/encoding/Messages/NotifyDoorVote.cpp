#include <Messages/NotifyDoorVote.h>

void NotifyDoorVote::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(VoteStatus, 8);
    DoorId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, VoterId);
    Serialization::WriteString(aWriter, VoterName);
    aWriter.WriteBits(Votes, 8);
    aWriter.WriteBits(Needed, 8);
}

void NotifyDoorVote::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    uint64_t value = 0;
    aReader.ReadBits(value, 8);
    VoteStatus = value & 0xFF;
    DoorId.Deserialize(aReader);
    VoterId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    VoterName = Serialization::ReadString(aReader);
    aReader.ReadBits(value, 8);
    Votes = value & 0xFF;
    aReader.ReadBits(value, 8);
    Needed = value & 0xFF;
}
