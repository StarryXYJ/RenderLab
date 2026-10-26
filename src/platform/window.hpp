#pragma once

#include <cstdint>
#include <string_view>

namespace renderlab::platform
{
/** @brief Borrowed native handles, valid only while their Window or external host remains alive. */
struct NativeSurface
{
    void* window = nullptr;
    void* instance = nullptr;
};

enum class Key
{
    Forward,
    Backward,
    Left,
    Right,
    Up,
    Down,
    TurnLeft,
    TurnRight,
    LookUp,
    LookDown,
    ToggleDebug,
    Count
};

struct WindowState
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t resizeEvents = 0;
    uint32_t minimizeEvents = 0;
    bool minimized = false;
    bool closeRequested = false;
};

/**
 * @brief Owns a Win32 window and processes its events on the creating thread.
 *
 * A close request leaves the native window alive so the host can shut down GPU objects first.
 * Access the window and its state only from the creating thread.
 */
class Window final
{
public:
    /**
     * @brief Creates and shows a window with a UTF-8 title and client dimensions in pixels.
     * @throws std::invalid_argument If either dimension is outside [1, 16384].
     * @throws std::runtime_error If native window creation fails.
     */
    Window(std::string_view title, uint32_t width, uint32_t height);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    NativeSurface GetNativeSurface() const;
    const WindowState& GetState() const;
    /// Dispatch pending thread messages; return false once a close or quit request is observed.
    bool PumpEvents();
    /// Wait for input or a timeout without dispatching messages; call PumpEvents afterwards.
    void WaitForEvents(uint32_t milliseconds) const;
    bool KeyDown(Key key) const;
    void Resize(uint32_t width, uint32_t height);
    void Minimize();
    void Restore();
    /// Post a close request; the host remains responsible for destruction after device cleanup.
    void RequestClose();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
} // namespace renderlab::platform
