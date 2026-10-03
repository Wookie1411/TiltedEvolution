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

    struct Settings
    {
        bool CompassEnabled = true;
        float CompassCenterX = 0.5f;   // fraction of screen width
        float CompassY = 0.055f;       // fraction of screen height (vanilla compass sits at the top)
        float CompassHalfWidth = 0.29f; // half the compass width, as a fraction of screen HEIGHT (HUD scales with height)
        float CompassHalfAngle = 90.f; // degrees from the centre to the compass edge
        float CompassMarkerSize = 0.011f; // fraction of screen height

        bool BubblesEnabled = true;
        float BubbleHeadOffset = 25.f;  // game units above the character's height
        float BubbleMaxDistance = 3000.f; // game units
        float BubbleFontScale = 1.6f;
        float BubbleMaxWidth = 0.25f;   // fraction of screen width
    };

    struct Bubble
    {
        String Text;
        std::chrono::steady_clock::time_point Until;
    };

    World& m_world;
    Settings m_settings;
    std::filesystem::path m_settingsPath;
    std::filesystem::file_time_type m_settingsTime{};
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
