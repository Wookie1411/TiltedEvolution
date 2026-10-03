#include <Services/CoopOverlayService.h>
#include <Services/ImguiService.h>

#include <World.h>
#include <Components.h>
#include <imgui.h>

#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/NotifyPlayerList.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>

#include <BSGraphics/BSGraphicsRenderer.h>
#include <Camera/PlayerCamera.h>
#include <Interface/UI.h>
#include <Games/Skyrim/Interface/Menus/HUDMenuUtils.h>
#include <NetImmerse/NiCamera.h>
#include <PlayerCharacter.h>
#include <Actor.h>

#include <TiltedCore/Filesystem.hpp>

namespace
{
constexpr float kPi = 3.14159265f;
const ImU32 kMarkerColor = IM_COL32(70, 200, 255, 235);
const ImU32 kMarkerOutline = IM_COL32(0, 0, 0, 200);

// Forward direction of the game camera. NiAVObject::world.rotate is at 0x7C (CommonLibSSE layout,
// matches STR's sizeof(NiAVObject) == 0x110); a Gamebryo camera looks along the first column.
bool GetCameraForward(float& aX, float& aY) noexcept
{
    PlayerCamera* pCamera = PlayerCamera::Get();
    NiCamera* pNiCamera = pCamera ? pCamera->GetNiCamera() : nullptr;
    if (!pNiCamera)
        return false;

    const float* pRotate = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(pNiCamera) + 0x7C); // 3x3 row-major
    aX = pRotate[0]; // entry[0][0]
    aY = pRotate[3]; // entry[1][0]
    return aX != 0.f || aY != 0.f;
}

float ReadFloat(const std::filesystem::path& acPath, const char* acSection, const char* acKey, float aDefault) noexcept
{
    char buffer[64]{};
    GetPrivateProfileStringA(acSection, acKey, "", buffer, sizeof(buffer), acPath.string().c_str());
    if (!buffer[0])
        return aDefault;
    char* pEnd = nullptr;
    const float cValue = std::strtof(buffer, &pEnd);
    return pEnd != buffer ? cValue : aDefault;
}
} // namespace

CoopOverlayService::CoopOverlayService(World& aWorld, entt::dispatcher& aDispatcher, ImguiService& aImguiService)
    : m_world(aWorld)
    , m_settingsPath(TiltedPhoques::GetPath() / "SkyrimCoopOverlay.ini")
{
    m_drawConnection = aImguiService.OnDraw.connect<&CoopOverlayService::OnDraw>(this);
    m_chatConnection = aDispatcher.sink<NotifyChatMessageBroadcast>().connect<&CoopOverlayService::OnChatMessage>(this);
    m_playerListConnection = aDispatcher.sink<NotifyPlayerList>().connect<&CoopOverlayService::OnPlayerList>(this);
    m_playerJoinedConnection = aDispatcher.sink<NotifyPlayerJoined>().connect<&CoopOverlayService::OnPlayerJoined>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&CoopOverlayService::OnPlayerLeft>(this);
}

void CoopOverlayService::OnPlayerList(const NotifyPlayerList& acMessage) noexcept
{
    std::scoped_lock lock(m_mutex);
    for (const auto& [id, name] : acMessage.Players)
        m_playerNames[id] = name;
}

void CoopOverlayService::OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept
{
    std::scoped_lock lock(m_mutex);
    m_playerNames[acMessage.PlayerId] = acMessage.Username;
}

void CoopOverlayService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    std::scoped_lock lock(m_mutex);
    m_playerNames.erase(acMessage.PlayerId);
    m_bubbles.erase(acMessage.PlayerId);
}

// The line another party member picked in a dialogue arrives as a kPlayerDialogue chat message.
void CoopOverlayService::OnChatMessage(const NotifyChatMessageBroadcast& acMessage) noexcept
{
    if (acMessage.MessageType != kPlayerDialogue || acMessage.ChatMessage.empty())
        return;

    std::scoped_lock lock(m_mutex);
    for (const auto& [id, name] : m_playerNames)
    {
        if (name == acMessage.PlayerName)
        {
            // Long lines stay up longer: 3 s + ~70 ms per character, at most 10 s.
            const auto cDuration = std::chrono::milliseconds(std::min<size_t>(3000 + acMessage.ChatMessage.size() * 70, 10000));
            m_bubbles[id] = Bubble{acMessage.ChatMessage, std::chrono::steady_clock::now() + cDuration};
            spdlog::info("[SkyrimCoop] Speech bubble for '{}' (player {}): {}", name.c_str(), id, acMessage.ChatMessage.c_str());
            return;
        }
    }
    spdlog::warn("[SkyrimCoop] Speech bubble: no player named '{}'", acMessage.PlayerName.c_str());
}

void CoopOverlayService::ReloadSettingsIfChanged() noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < m_nextSettingsCheck)
        return;
    m_nextSettingsCheck = cNow + std::chrono::seconds(1);

    std::error_code ec;
    const auto cTime = std::filesystem::last_write_time(m_settingsPath, ec);
    if (ec || cTime == m_settingsTime)
        return;
    m_settingsTime = cTime;

    Settings s{};
    const auto& p = m_settingsPath;
    s.CompassEnabled = ReadFloat(p, "Compass", "Enabled", 1.f) != 0.f;
    s.CompassY = ReadFloat(p, "Compass", "Y", s.CompassY);
    s.CompassHalfWidth = ReadFloat(p, "Compass", "HalfWidth", s.CompassHalfWidth);
    s.CompassHalfAngle = ReadFloat(p, "Compass", "HalfAngle", s.CompassHalfAngle);
    s.CompassMarkerSize = ReadFloat(p, "Compass", "MarkerSize", s.CompassMarkerSize);
    s.BubblesEnabled = ReadFloat(p, "Bubbles", "Enabled", 1.f) != 0.f;
    s.BubbleHeadOffset = ReadFloat(p, "Bubbles", "HeadOffset", s.BubbleHeadOffset);
    s.BubbleMaxDistance = ReadFloat(p, "Bubbles", "MaxDistance", s.BubbleMaxDistance);
    s.BubbleTextSize = ReadFloat(p, "Bubbles", "TextSize", s.BubbleTextSize);
    s.BubbleMaxWidth = ReadFloat(p, "Bubbles", "MaxWidth", s.BubbleMaxWidth);
    m_settings = s;
    spdlog::info("[SkyrimCoop] Overlay settings loaded from {}", m_settingsPath.string());
}

void CoopOverlayService::OnDraw() noexcept
{
    ReloadSettingsIfChanged();

    auto* pViewport = BSGraphics::GetMainWindow();
    if (!pViewport || !PlayerCharacter::Get())
        return;

    // No overlay during loading screens or while a full-screen menu is open (map, inventory, ...).
    if (UI* pUI = UI::Get())
    {
        static const char* kHidingMenus[] = {"Loading Menu", "MapMenu", "InventoryMenu", "MagicMenu", "StatsMenu", "Journal Menu", "ContainerMenu", "BarterMenu", "Console"};
        for (const char* pName : kHidingMenus)
        {
            if (pUI->GetMenuOpen(BSFixedString(pName)))
                return;
        }
    }

    const float cWidth = static_cast<float>(pViewport->uiWindowWidth);
    const float cHeight = static_cast<float>(pViewport->uiWindowHeight);

    if (m_settings.CompassEnabled)
        DrawCompassMarkers(cWidth, cHeight);
    if (m_settings.BubblesEnabled)
        DrawSpeechBubbles(cWidth, cHeight);
}

void CoopOverlayService::DrawCompassMarkers(float aWidth, float aHeight) noexcept
{
    float forwardX = 0.f, forwardY = 0.f;
    if (!GetCameraForward(forwardX, forwardY))
        return;

    // Skyrim: +Y is north, +X is east; headings go clockwise from north.
    const float cCameraHeading = std::atan2(forwardX, forwardY);
    const NiPoint3 cMe = PlayerCharacter::Get()->position;
    ImDrawList* pDraw = ImGui::GetForegroundDrawList();

    auto view = m_world.view<FormIdComponent, PlayerComponent>();
    for (auto entity : view)
    {
        const uint32_t cFormId = view.get<FormIdComponent>(entity).Id;
        if (cFormId == 0x14)
            continue; // that's us
        auto* pActor = Cast<Actor>(TESForm::GetById(cFormId));
        if (!pActor)
            continue;

        const float cDx = pActor->position.x - cMe.x;
        const float cDy = pActor->position.y - cMe.y;
        if (cDx * cDx + cDy * cDy < 1.f)
            continue;

        float relative = (std::atan2(cDx, cDy) - cCameraHeading) * 180.f / kPi;
        while (relative > 180.f)
            relative -= 360.f;
        while (relative < -180.f)
            relative += 360.f;
        if (std::abs(relative) > m_settings.CompassHalfAngle)
            continue;

        // HUD stage (1280x720) scaled to fit and centred, like the game's own HUD.
        const float cScale = HudScale(aWidth, aHeight);
        const float cStageTop = (aHeight - 720.f * cScale) * 0.5f;
        const float cX = aWidth * 0.5f + relative / m_settings.CompassHalfAngle * m_settings.CompassHalfWidth * cScale;
        const float cY = cStageTop + m_settings.CompassY * cScale;
        const float cSize = m_settings.CompassMarkerSize * cScale;

        // A diamond: different from the game's quest/location markers.
        const ImVec2 cTop{cX, cY - cSize}, cRight{cX + cSize * 0.7f, cY}, cBottom{cX, cY + cSize}, cLeft{cX - cSize * 0.7f, cY};
        pDraw->AddQuadFilled(cTop, cRight, cBottom, cLeft, kMarkerColor);
        pDraw->AddQuad(cTop, cRight, cBottom, cLeft, kMarkerOutline, 2.f);
    }
}

void CoopOverlayService::DrawSpeechBubbles(float aWidth, float aHeight) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    const NiPoint3 cMe = PlayerCharacter::Get()->position;
    ImDrawList* pDraw = ImGui::GetForegroundDrawList();
    ImFont* pFont = ImGui::GetFont();
    const float cScale = HudScale(aWidth, aHeight);
    const float cFontSize = m_settings.BubbleTextSize * cScale;
    const float cWrap = m_settings.BubbleMaxWidth * cScale;
    const float cPad = cFontSize * 0.5f;

    std::scoped_lock lock(m_mutex);
    if (m_bubbles.empty())
        return;

    auto view = m_world.view<FormIdComponent, PlayerComponent>();
    for (auto entity : view)
    {
        const uint32_t cPlayerId = view.get<PlayerComponent>(entity).Id;
        auto it = m_bubbles.find(cPlayerId);
        if (it == m_bubbles.end() || cNow > it->second.Until)
            continue;

        auto* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
        if (!pActor)
            continue;

        const float cDx = pActor->position.x - cMe.x, cDy = pActor->position.y - cMe.y, cDz = pActor->position.z - cMe.z;
        if (cDx * cDx + cDy * cDy + cDz * cDz > m_settings.BubbleMaxDistance * m_settings.BubbleMaxDistance)
            continue;

        NiPoint3 head = pActor->position;
        head.z += pActor->GetHeight() + m_settings.BubbleHeadOffset;
        NiPoint3 screen{};
        HUDMenuUtils::WorldPtToScreenPt3(head, screen);
        if (screen.z < 0.f || screen.x < 0.f || screen.x > 1.f || screen.y < 0.f || screen.y > 1.f)
            continue;

        const ImVec2 cAnchor{screen.x * aWidth, (1.f - screen.y) * aHeight};
        const char* pText = it->second.Text.c_str();
        const ImVec2 cTextSize = pFont->CalcTextSizeA(cFontSize, FLT_MAX, cWrap, pText);

        const ImVec2 cBoxMin{cAnchor.x - cTextSize.x * 0.5f - cPad, cAnchor.y - cTextSize.y - cPad * 2.f - cPad};
        const ImVec2 cBoxMax{cAnchor.x + cTextSize.x * 0.5f + cPad, cAnchor.y - cPad};

        pDraw->AddRectFilled(cBoxMin, cBoxMax, IM_COL32(20, 20, 20, 210), cPad);
        pDraw->AddRect(cBoxMin, cBoxMax, IM_COL32(230, 230, 230, 220), cPad, 0, 2.f);
        // Small tail pointing at the head.
        pDraw->AddTriangleFilled({cAnchor.x - cPad, cBoxMax.y}, {cAnchor.x + cPad, cBoxMax.y}, {cAnchor.x, cAnchor.y}, IM_COL32(20, 20, 20, 210));
        pDraw->AddText(pFont, cFontSize, {cBoxMin.x + cPad, cBoxMin.y + cPad}, IM_COL32(255, 255, 255, 255), pText, nullptr, cWrap);
    }

    // Drop expired bubbles.
    Vector<uint32_t> expired;
    for (const auto& [id, bubble] : m_bubbles)
    {
        if (cNow > bubble.Until)
            expired.push_back(id);
    }
    for (uint32_t id : expired)
        m_bubbles.erase(id);
}
