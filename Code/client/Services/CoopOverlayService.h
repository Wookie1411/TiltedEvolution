#pragma once

struct World;
struct ImguiService;
struct NotifyChatMessageBroadcast;
struct NotifyPlayerList;
struct NotifyPlayerJoined;
struct NotifyPlayerLeft;

/**
 * @brief SkyrimCoop on-screen helpers drawn with ImGui every frame:
 * - a marker for each other player on the compass,
 * - a speech bubble above another player's head showing the dialogue line they picked.
 * Position/size settings come from SkyrimCoopOverlay.ini next to SkyrimTogether.exe and are
 * re-read while the game runs, so they can be tuned live.
 */
class CoopOverlayService
{
public:
    CoopOverlayService(World& aWorld, entt::dispatcher& aDispatcher, ImguiService& aImguiService);

private:
    void OnDraw() noexcept;
    void OnChatMessage(const NotifyChatMessageBroadcast&) noexcept;
    void OnPlayerList(const NotifyPlayerList&) noexcept;
    void OnPlayerJoined(const NotifyPlayerJoined&) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft&) noexcept;

    void ReloadSettingsIfChanged() noexcept;
    void DrawCompassMarkers(float aWidth, float aHeight) noexcept;
    void DrawSpeechBubbles(float aWidth, float aHeight) noexcept;
    static float HudScale(float aWidth, float aHeight) noexcept { return std::min(aWidth / 1280.f, aHeight / 720.f); }

    struct Settings
    {
        // Sizes and positions are in HUD units: Skyrim lays its HUD out on a 1280x720 stage that is
        // scaled by min(width / 1280, height / 720) and centred on the screen.
        bool CompassEnabled = true;
        // Measured on PC 2's vanilla compass (2880x1800 screenshots): E->S (90 deg) = 276 HUD units,
        // visible track +-150 HUD units = +-49 deg, centre line 41.8 HUD units below the top edge.
        float CompassY = 41.8f;          // HUD units from the top of the screen (top HUD elements are anchored to the top)
        float CompassHalfWidth = 150.f;  // HUD units from the compass centre to the visible edge
        float CompassHalfAngle = 49.f;   // degrees from the centre to the visible edge
        float CompassMarkerSize = 7.9f;  // HUD units

        bool BubblesEnabled = true;
        float BubbleHeadOffset = 25.f;    // game units above the character's height
        float BubbleMaxDistance = 3000.f; // game units
        float BubbleTextSize = 9.f;       // HUD units (font height)
        float BubbleMaxWidth = 260.f;     // HUD units
    };

    struct Bubble
    {
        String Text;
        std::chrono::steady_clock::time_point Until;
    };

    World& m_world;
    Settings m_settings;
    std::filesystem::path m_settingsPath;         // defaults shipped with the build
    std::filesystem::path m_userSettingsPath;     // config\SkyrimCoopOverlay.ini: per-PC overrides, kept by the installer
    std::filesystem::file_time_type m_settingsTime{};
    std::filesystem::file_time_type m_userSettingsTime{};
    std::chrono::steady_clock::time_point m_nextSettingsCheck{};

    std::mutex m_mutex; // messages arrive on the game thread, drawing happens on the render thread
    TiltedPhoques::Map<uint32_t, String> m_playerNames;  // player id -> username
    TiltedPhoques::Map<uint32_t, Bubble> m_bubbles;      // player id -> current bubble

    entt::scoped_connection m_drawConnection;
    entt::scoped_connection m_chatConnection;
    entt::scoped_connection m_playerListConnection;
    entt::scoped_connection m_playerJoinedConnection;
    entt::scoped_connection m_playerLeftConnection;
};
