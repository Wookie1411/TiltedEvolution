#include <Messages/DoorVoteRequest.h>

void DoorVoteRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    DoorId.Serialize(aWriter);
    CellId.Serialize(aWriter);
}

void DoorVoteRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    DoorId.Deserialize(aReader);
    CellId.Deserialize(aReader);
}
