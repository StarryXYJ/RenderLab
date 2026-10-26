#include "apps/sandbox/debug_panel.hpp"
#include "platform/text.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>

namespace renderlab::sandbox
{
namespace
{
enum : int
{
    View = 100,
    Quality,
    Shadows,
    Freeze,
    Reset,
    Darker,
    Brighter
};
constexpr std::array<uint32_t, 4> Resolutions{0, 128, 512, 1024};
} // namespace
struct DebugPanel::Impl
{
    HWND window = nullptr, view = nullptr, quality = nullptr, shadows = nullptr, freeze = nullptr,
         exposure = nullptr, statistics = nullptr;
    DebugControls controls;
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == WM_CLOSE)
        {
            ShowWindow(window, SW_HIDE);
            return 0;
        }
        if (self && message == WM_COMMAND)
        {
            const auto id = LOWORD(wParam);
            if (HIWORD(wParam) == CBN_SELCHANGE && id == View)
                self->controls.render.view =
                    renderer::SceneView(SendMessageW(self->view, CB_GETCURSEL, 0, 0));
            else if (HIWORD(wParam) == CBN_SELCHANGE && id == Quality)
            {
                const auto index = SendMessageW(self->quality, CB_GETCURSEL, 0, 0);
                if (index >= 0 && size_t(index) < Resolutions.size())
                    self->controls.render.shadowResolution = Resolutions[size_t(index)];
            }
            else if (id == Shadows)
                self->controls.shadows =
                    SendMessageW(self->shadows, BM_GETCHECK, 0, 0) == BST_CHECKED;
            else if (id == Freeze)
                self->controls.freezeCamera =
                    SendMessageW(self->freeze, BM_GETCHECK, 0, 0) == BST_CHECKED;
            else if (id == Reset)
                self->controls.resetCamera = true;
            else if (id == Darker || id == Brighter)
                self->controls.exposure =
                    std::clamp(self->controls.exposure + (id == Darker ? -.5f : .5f), -16.f, 16.f);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    HWND Control(const wchar_t* type, const wchar_t* text, DWORD style, int id, int x, int y,
                 int width, int height)
    {
        const auto handle = CreateWindowExW(
            0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, window,
            reinterpret_cast<HMENU>(intptr_t(id)), GetModuleHandleW(nullptr), nullptr);
        if (!handle)
            throw std::runtime_error("Cannot create debug panel control");
        SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                     TRUE);
        return handle;
    }
    ~Impl()
    {
        if (window)
            DestroyWindow(window);
    }
};
DebugPanel::DebugPanel(platform::NativeSurface owner, DebugControls controls)
    : impl_(std::make_unique<Impl>())
{
    auto& self = *impl_;
    self.controls = controls;
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = Impl::Procedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    type.lpszClassName = L"RenderLab.DebugPanel";
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("Cannot register debug panel");
    self.window =
        CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"RenderLab - Debug",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 420,
                        590, static_cast<HWND>(owner.window), nullptr, type.hInstance, &self);
    if (!self.window)
        throw std::runtime_error("Cannot create debug panel");
    self.Control(L"STATIC", L"Texture", 0, 0, 12, 12, 100, 20);
    self.view =
        self.Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, View, 112, 10, 280, 180);
    for (const auto* name : {L"Final (ACES)", L"CameraColor (linear HDR)",
                             L"MainLightShadow (depth)", L"Environment atlas", L"BRDF LUT"})
        SendMessageW(self.view, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    SendMessageW(self.view, CB_SETCURSEL, WPARAM(controls.render.view), 0);
    self.Control(L"STATIC", L"Shadow quality", 0, 0, 12, 46, 100, 20);
    self.quality =
        self.Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, Quality, 112, 44, 280, 150);
    size_t selected = 0;
    for (const auto* name : {L"Scene setting", L"Low (128)", L"Medium (512)", L"High (1024)"})
        SendMessageW(self.quality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    for (size_t i = 0; i < Resolutions.size(); ++i)
        if (Resolutions[i] == controls.render.shadowResolution)
            selected = i;
    SendMessageW(self.quality, CB_SETCURSEL, selected, 0);
    self.shadows = self.Control(L"BUTTON", L"Main light shadows", BS_AUTOCHECKBOX | WS_TABSTOP,
                                Shadows, 12, 82, 190, 24);
    SendMessageW(self.shadows, BM_SETCHECK, controls.shadows ? BST_CHECKED : BST_UNCHECKED, 0);
    self.freeze = self.Control(L"BUTTON", L"Freeze camera", BS_AUTOCHECKBOX | WS_TABSTOP, Freeze,
                               215, 82, 180, 24);
    self.Control(L"BUTTON", L"Reset camera", BS_PUSHBUTTON | WS_TABSTOP, Reset, 12, 118, 110, 26);
    self.Control(L"BUTTON", L"Exposure -", BS_PUSHBUTTON | WS_TABSTOP, Darker, 132, 118, 125, 26);
    self.Control(L"BUTTON", L"Exposure +", BS_PUSHBUTTON | WS_TABSTOP, Brighter, 267, 118, 125, 26);
    self.exposure = self.Control(L"STATIC", L"", 0, 0, 12, 156, 380, 22);
    self.statistics =
        self.Control(L"STATIC", L"Waiting for a retired frame...", 0, 0, 12, 190, 380, 280);
    self.Control(L"STATIC",
                 L"F1: show/hide panel\nWASD/QE: move  |  Arrow keys: look\nInput goes to the "
                 L"focused window.",
                 0, 0, 12, 495, 380, 60);
    ShowWindow(self.window, SW_SHOWNOACTIVATE);
}
DebugPanel::~DebugPanel() = default;
DebugControls& DebugPanel::Controls()
{
    return impl_->controls;
}
void DebugPanel::Toggle()
{
    ShowWindow(impl_->window, IsWindowVisible(impl_->window) ? SW_HIDE : SW_SHOWNOACTIVATE);
}
void DebugPanel::Update(std::string_view statistics)
{
    const auto text = platform::Utf8ToWide(statistics);
    SetWindowTextW(impl_->statistics, text.c_str());
    const auto exposure = std::format(L"Exposure: {:.1f} stops", impl_->controls.exposure);
    SetWindowTextW(impl_->exposure, exposure.c_str());
}
void DebugPanel::VerifyControls()
{
    auto& self = *impl_;
    const auto saved = self.controls;
    SendMessageW(self.view, CB_SETCURSEL, 2, 0);
    SendMessageW(self.window, WM_COMMAND, MAKEWPARAM(View, CBN_SELCHANGE), 0);
    SendMessageW(self.window, WM_COMMAND, Brighter, 0);
    SendMessageW(self.window, WM_COMMAND, Reset, 0);
    SendMessageW(self.quality, CB_SETCURSEL, 3, 0);
    SendMessageW(self.window, WM_COMMAND, MAKEWPARAM(Quality, CBN_SELCHANGE), 0);
    SendMessageW(self.freeze, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(self.window, WM_COMMAND, Freeze, 0);
    RECT client{}, control{};
    GetClientRect(self.window, &client);
    GetWindowRect(self.statistics, &control);
    MapWindowPoints(nullptr, self.window, reinterpret_cast<POINT*>(&control), 2);
    if (self.controls.render.view != renderer::SceneView::Shadow || !self.controls.resetCamera ||
        self.controls.exposure != std::clamp(saved.exposure + .5f, -16.f, 16.f) ||
        self.controls.render.shadowResolution != 1024 || !self.controls.freezeCamera ||
        control.right > client.right || control.bottom > client.bottom)
        throw std::runtime_error("Native debug controls failed");
    self.controls = saved;
    SendMessageW(self.view, CB_SETCURSEL, WPARAM(saved.render.view), 0);
    for (size_t i = 0; i < Resolutions.size(); ++i)
        if (Resolutions[i] == saved.render.shadowResolution)
            SendMessageW(self.quality, CB_SETCURSEL, i, 0);
    SendMessageW(self.freeze, BM_SETCHECK, saved.freezeCamera ? BST_CHECKED : BST_UNCHECKED, 0);
}
} // namespace renderlab::sandbox
