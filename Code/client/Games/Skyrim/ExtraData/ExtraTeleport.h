#pragma once

#include "ExtraData.h"

// Destination of a load door. Layout as described by CommonLibSSE(-NG) (RE::DoorTeleportData).
struct DoorTeleportData
{
    uint32_t linkedDoor; // ObjectRefHandle of the door on the other side
    NiPoint3 position;
    NiPoint3 rotation;
    uint8_t flags;
};

static_assert(offsetof(DoorTeleportData, position) == 0x4);
static_assert(offsetof(DoorTeleportData, rotation) == 0x10);
static_assert(offsetof(DoorTeleportData, flags) == 0x1C);

// Present on doors that move the player to another cell ("load doors").
struct ExtraTeleport : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::Teleport;

    DoorTeleportData* pTeleportData;
};

static_assert(sizeof(ExtraTeleport) == 0x18);
