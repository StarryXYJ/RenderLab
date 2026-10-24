#include "platform/window.hpp"

#include "platform/text.hpp"

#include <Windows.h>
#include <array>
#include <memory>
#include <stdexcept>

namespace renderlab::platform
{
struct Window::Impl
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    HWND window = nullptr;
    WindowState state;
    std::array<bool, static_cast<size_t>(Key::Count)> keys{};

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            // CreateWindowEx sends messages synchronously, before the Window constructor returns.
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self != nullptr)
        {
            switch (message)
            {
            case WM_KEYDOWN:
            case WM_KEYUP:
            {
                const std::array<WPARAM, 10> codes{'W', 'S',     'A',      'D',   'E',
                                                   'Q', VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN};
                for (size_t i = 0; i < codes.size(); ++i)
                    if (wParam == codes[i])
                        self->keys[i] = message == WM_KEYDOWN;
                break;
            }
            case WM_KILLFOCUS:
                self->keys.fill(false);
                break;
            case WM_CLOSE:
                // Let the host retire the device while its HWND is still valid.
                self->state.closeRequested = true;
                return 0;
            case WM_SIZE:
                self->state.minimized = wParam == SIZE_MINIMIZED;
                self->state.width = LOWORD(lParam);
                self->state.height = HIWORD(lParam);
                ++self->state.resizeEvents;
                if (self->state.minimized)
                {
                    ++self->state.minimizeEvents;
                }
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_NCDESTROY:
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                break;
            default:
                break;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    ~Impl()
    {
        if (window != nullptr)
        {
            DestroyWindow(window);
        }
    }
};

bool Window::KeyDown(Key key) const
{
    const auto index = static_cast<size_t>(key);
    return index < impl_->keys.size() && impl_->keys[index];
}

Window::Window(std::string_view title, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || width > 16384 || height > 16384)
    {
        throw std::invalid_argument("Window dimensions must be in [1, 16384]");
    }
    auto instance = std::make_unique<Impl>();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = Impl::Procedure;
    windowClass.hInstance = instance->instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"RenderLab.Window";
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        throw std::runtime_error("RegisterClassExW failed");
    }
    RECT rectangle{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (!AdjustWindowRectEx(&rectangle, WS_OVERLAPPEDWINDOW, FALSE, 0))
    {
        throw std::runtime_error("AdjustWindowRectEx failed");
    }
    const auto wideTitle = Utf8ToWide(title);
    instance->window = CreateWindowExW(
        0, windowClass.lpszClassName, wideTitle.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top, nullptr,
        nullptr, instance->instance, instance.get());
    if (instance->window == nullptr)
    {
        throw std::runtime_error("CreateWindowExW failed");
    }
    ShowWindow(instance->window, SW_SHOW);
    impl_ = instance.release();
}

Window::~Window()
{
    delete impl_;
}

NativeSurface Window::GetNativeSurface() const
{
    return {impl_->window, impl_->instance};
}

const WindowState& Window::GetState() const
{
    return impl_->state;
}

bool Window::PumpEvents()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
        {
            impl_->state.closeRequested = true;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return !impl_->state.closeRequested;
}

void Window::WaitForEvents(uint32_t milliseconds) const
{
    MsgWaitForMultipleObjectsEx(0, nullptr, milliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}

void Window::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || width > 16384 || height > 16384)
    {
        throw std::invalid_argument("Window dimensions must be in [1, 16384]");
    }
    RECT rectangle{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (!AdjustWindowRectEx(&rectangle, WS_OVERLAPPEDWINDOW, FALSE, 0) ||
        !SetWindowPos(impl_->window, nullptr, 0, 0, rectangle.right - rectangle.left,
                      rectangle.bottom - rectangle.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
    {
        throw std::runtime_error("Window resize failed");
    }
}

void Window::Minimize()
{
    ShowWindow(impl_->window, SW_MINIMIZE);
}

void Window::Restore()
{
    ShowWindow(impl_->window, SW_RESTORE);
}

void Window::RequestClose()
{
    PostMessageW(impl_->window, WM_CLOSE, 0, 0);
}
} // namespace renderlab::platform
