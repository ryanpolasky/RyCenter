#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "resource.h"
#include "settings_ui.h"

namespace rycenter::ui {
namespace {
using namespace Gdiplus;
constexpr wchar_t appearanceProperty[] = L"RyCenter.Appearance";
constexpr float width = 500;
constexpr float height = 700;
const Color background(255, 245, 245, 247);
const Color ink(255, 32, 33, 36);
const Color muted(255, 139, 139, 148);
const Color blue(255, 0, 122, 255);
const Color white(255, 255, 255, 255);
const Color edge(255, 231, 232, 237);

struct Appearance {
    ULONG_PTR token = 0;
    HICON icon = nullptr;
    float scale = 1;
    HWND hovered = nullptr;
    bool recording = false;
    bool error = false;
    bool resizing = false;
    bool highContrast = false;
    HBRUSH fallbackBrush = nullptr;
    HFONT fallbackFont = nullptr;

    ~Appearance() {
        if (icon)
            DestroyIcon(icon);
        if (fallbackBrush)
            DeleteObject(fallbackBrush);
        if (fallbackFont)
            DeleteObject(fallbackFont);
        if (token)
            GdiplusShutdown(token);
    }
};

Appearance *appearance(HWND dialog) {
    return reinterpret_cast<Appearance *>(GetPropW(dialog, appearanceProperty));
}

int pixels(float value, float scale) { return static_cast<int>(std::lround(value * scale)); }

bool highContrast() {
    HIGHCONTRASTW value{sizeof(value), 0, nullptr};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(value), &value, 0) &&
           (value.dwFlags & HCF_HIGHCONTRASTON);
}

void rounded(Graphics &graphics, RectF rect, float radius, Color fill,
             Color border = Color(0, 0, 0, 0), float weight = 1) {
    GraphicsPath path;
    const float diameter = radius * 2;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
    SolidBrush brush(fill);
    graphics.FillPath(&brush, &path);
    if (border.GetAlpha()) {
        Pen pen(border, weight);
        graphics.DrawPath(&pen, &path);
    }
}

void text(Graphics &graphics, const std::wstring &value, RectF rect, float size, Color color,
          bool semibold = false, StringAlignment alignment = StringAlignmentNear) {
    Font font(semibold ? L"Segoe UI Semibold" : L"Segoe UI", size, FontStyleRegular, UnitPixel);
    StringFormat format;
    format.SetAlignment(alignment);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush brush(color);
    graphics.DrawString(value.c_str(), -1, &font, rect, &format, &brush);
}

void preview(Graphics &graphics) {
    rounded(graphics, {28, 182, 444, 112}, 12, Color(255, 233, 238, 244),
            Color(255, 223, 229, 237));
    rounded(graphics, {176, 200, 148, 79}, 6, Color(255, 215, 228, 240), Color(255, 150, 166, 182),
            3);
    Pen guide(Color(80, 109, 153, 189), 1);
    guide.SetDashStyle(DashStyleDash);
    graphics.DrawLine(&guide, 250.0f, 202.0f, 250.0f, 277.0f);
    graphics.DrawLine(&guide, 178.0f, 239.0f, 322.0f, 239.0f);
    rounded(graphics, {211, 215, 78, 51}, 5, Color(18, 70, 105, 140));
    rounded(graphics, {211, 212, 78, 51}, 5, white);
    rounded(graphics, {218, 219, 23, 3}, 1.5f, Color(255, 196, 214, 231));
    rounded(graphics, {218, 228, 64, 28}, 2, Color(255, 241, 245, 249));
}

void paintBackground(Graphics &graphics, const Appearance &state) {
    graphics.Clear(background);
    preview(graphics);
    rounded(graphics, {28, 314, 444, 94}, 13, white, Color(255, 236, 236, 240));
    rounded(graphics, {28, 426, 444, 140}, 13, white, Color(255, 236, 236, 240));
    Pen divider(Color(255, 237, 237, 241), 1);
    graphics.DrawLine(&divider, 46.0f, 496.0f, 454.0f, 496.0f);
    SolidBrush footer(Color(255, 248, 248, 250));
    graphics.FillRectangle(&footer, 0.0f, 632.0f, width, 68.0f);
    graphics.DrawLine(&divider, 0.0f, 632.0f, width, 632.0f);
    text(graphics, L"RyCenter", {18, 0, 180, 36}, 11, muted);
    if (state.icon) {
        Bitmap icon(state.icon);
        graphics.DrawImage(&icon, RectF(224, 47, 52, 52));
    }
}

void keycaps(Graphics &graphics, const std::wstring &value, RectF rect, bool focused) {
    std::vector<std::wstring> keys;
    std::size_t start = 0;
    for (;;) {
        const auto end = value.find(L" + ", start);
        keys.push_back(value.substr(start, end == std::wstring::npos ? end : end - start));
        if (end == std::wstring::npos)
            break;
        start = end + 3;
    }
    Font font(L"Segoe UI", 11, FontStyleRegular, UnitPixel);
    std::vector<float> widths;
    float total = 0;
    for (const auto &key : keys) {
        RectF measured;
        graphics.MeasureString(key.c_str(), -1, &font, PointF(0, 0), &measured);
        const float keyWidth = std::max(30.0f, measured.Width + 10);
        widths.push_back(keyWidth);
        total += keyWidth + 5;
    }
    total -= 5;
    const float fit = std::min(1.0f, (rect.Width - 10) / total);
    float x = rect.GetRight() - total * fit - 5;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        RectF cap(x, rect.Y + 5, widths[index] * fit, rect.Height - 11);
        rounded(graphics, {cap.X, cap.Y + 1, cap.Width, cap.Height}, 6, Color(255, 217, 218, 224));
        rounded(graphics, cap, 6, focused ? Color(255, 236, 244, 255) : background, edge);
        text(graphics, keys[index], cap, 11 * fit, ink, false, StringAlignmentCenter);
        x += (widths[index] + 5) * fit;
    }
}

std::wstring controlText(HWND control) {
    std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    const int length = GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
    value.resize(static_cast<std::size_t>(length));
    return value;
}

void paintControl(Graphics &graphics, HWND control, const Appearance &state, RectF rect) {
    const int id = GetDlgCtrlID(control);
    const bool focused = GetFocus() == control;
    const bool hovered = state.hovered == control;
    const bool pressed = (SendMessageW(control, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
    const auto label = controlText(control);
    if (id == IDC_AUTO || id == IDC_STARTUP) {
        graphics.Clear(white);
        if (hovered || pressed)
            rounded(graphics, {1, 1, rect.Width - 2, rect.Height - 2}, 10,
                    pressed ? Color(255, 239, 242, 248) : Color(255, 249, 250, 252));
        text(graphics, label, {6, 8, rect.Width - 74, 24}, 13, ink, true);
        text(graphics, id == IDC_AUTO ? L"Once, when they open." : L"Ready at login.",
             {6, 30, rect.Width - 74, 20}, 11, muted);
        const bool checked = SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const float toggleX = rect.Width - 42;
        const float toggleY = (rect.Height - 21) / 2;
        rounded(graphics, {toggleX, toggleY, 34, 21}, 10.5f,
                checked ? blue : Color(255, 217, 217, 223));
        SolidBrush knob(white);
        graphics.FillEllipse(&knob, toggleX + (checked ? 15 : 2), toggleY + 2, 17.0f, 17.0f);
    } else if (id == IDC_HOTKEY) {
        graphics.Clear(white);
        if (state.recording || hovered)
            rounded(graphics, {1, 1, rect.Width - 2, rect.Height - 2}, 8,
                    Color(255, 239, 246, 255));
        if (state.recording && label == L"Press a shortcut…")
            text(graphics, label, rect, 12, blue, false, StringAlignmentCenter);
        else
            keycaps(graphics, label, rect, focused || hovered);
    } else if (id == IDOK || id == IDCANCEL) {
        graphics.Clear(Color(255, 248, 248, 250));
        const bool primary = id == IDOK;
        const Color fill = primary ? (pressed   ? Color(255, 0, 101, 216)
                                      : hovered ? Color(255, 0, 111, 238)
                                                : blue)
                                   : (pressed   ? Color(255, 232, 234, 239)
                                      : hovered ? Color(255, 246, 247, 249)
                                                : white);
        rounded(graphics, {2, 2, rect.Width - 4, rect.Height - 5}, 7, fill,
                primary ? Color(255, 0, 114, 240) : Color(255, 217, 217, 224));
        text(graphics, label, {0, 0, rect.Width, rect.Height - 1}, 12, primary ? white : ink, false,
             StringAlignmentCenter);
    } else if (id == IDC_RESET || id == IDC_CLOSE) {
        graphics.Clear(id == IDC_RESET ? white : background);
        if (hovered || pressed)
            rounded(graphics, {1, 1, rect.Width - 2, rect.Height - 2}, 6,
                    id == IDC_CLOSE ? Color(255, 231, 232, 238) : Color(255, 237, 245, 255));
        if (id == IDC_CLOSE) {
            Pen pen(muted, 1.2f);
            const float x = rect.Width / 2;
            const float y = rect.Height / 2;
            graphics.DrawLine(&pen, x - 3, y - 3, x + 3, y + 3);
            graphics.DrawLine(&pen, x + 3, y - 3, x - 3, y + 3);
        } else {
            text(graphics, label, rect, 11, blue, false, StringAlignmentCenter);
        }
    } else {
        graphics.Clear(id == IDC_HINT || id == IDC_SHORTCUT_LABEL ? white
                       : id == IDC_FOOTER                         ? Color(255, 248, 248, 250)
                                                                  : background);
        const bool title = id == IDC_TITLE;
        const float size = title                                            ? 24.0f
                           : id == IDC_SUBTITLE || id == IDC_SHORTCUT_LABEL ? 13.0f
                                                                            : 11.0f;
        const auto alignment =
            title || id == IDC_SUBTITLE ? StringAlignmentCenter : StringAlignmentNear;
        text(graphics, label, rect, size,
             title || id == IDC_SHORTCUT_LABEL ? ink
             : id == IDC_STATUS && state.error ? Color(255, 191, 55, 55)
                                               : muted,
             title || id == IDC_SHORTCUT_LABEL, alignment);
    }
    if (focused && id != IDC_STATUS && id != IDC_FOOTER)
        rounded(graphics, {1, 1, rect.Width - 3, rect.Height - 3}, 7, Color(0, 0, 0, 0),
                Color(180, 0, 122, 255), 1.5f);
}

void paint(HWND window, HDC dc, Appearance &state, bool parent) {
    RECT client;
    GetClientRect(window, &client);
    const int w = client.right;
    const int h = client.bottom;
    if (w <= 0 || h <= 0)
        return;
    Bitmap buffer(w, h, PixelFormat32bppPARGB);
    Graphics graphics(&buffer);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    graphics.ScaleTransform(state.scale, state.scale);
    if (parent)
        paintBackground(graphics, state);
    else
        paintControl(
            graphics, window, state,
            {0, 0, static_cast<float>(w) / state.scale, static_cast<float>(h) / state.scale});
    Graphics target(dc);
    target.DrawImage(&buffer, 0, 0);
}

LRESULT CALLBACK controlProc(HWND control, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                             DWORD_PTR reference) {
    auto &state = *reinterpret_cast<Appearance *>(reference);
    if (!state.highContrast) {
        if (message == WM_ERASEBKGND)
            return 1;
        if (message == WM_PAINT) {
            PAINTSTRUCT ps;
            const auto dc = BeginPaint(control, &ps);
            paint(control, dc, state, false);
            EndPaint(control, &ps);
            return 0;
        }
        if (message == WM_PRINTCLIENT) {
            paint(control, reinterpret_cast<HDC>(wparam), state, false);
            return 0;
        }
    }
    if (message == WM_MOUSEMOVE && state.hovered != control) {
        state.hovered = control;
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, control, 0};
        TrackMouseEvent(&track);
        InvalidateRect(control, nullptr, FALSE);
    }
    if (message == WM_MOUSELEAVE && state.hovered == control) {
        state.hovered = nullptr;
        InvalidateRect(control, nullptr, FALSE);
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(control, controlProc, 2);
    const auto result = DefSubclassProc(control, message, wparam, lparam);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == BM_SETCHECK ||
        message == BM_SETSTATE || message == WM_SETTEXT || message == WM_ENABLE ||
        message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_KEYDOWN ||
        message == WM_KEYUP || message == WM_UPDATEUISTATE)
        InvalidateRect(control, nullptr, FALSE);
    return result;
}

void move(HWND dialog, int id, float x, float y, float w, float h, float scale) {
    SetWindowPos(GetDlgItem(dialog, id), nullptr, pixels(x, scale), pixels(y, scale),
                 pixels(w, scale), pixels(h, scale), SWP_NOZORDER | SWP_NOACTIVATE);
}
}

void layout(HWND dialog, UINT dpi, const RECT *suggested) {
    auto *state = appearance(dialog);
    if (!state || state->resizing)
        return;
    state->resizing = true;
    MONITORINFO monitor{sizeof(monitor), {}, {}, 0};
    const auto handle = suggested ? MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST)
                                  : MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(handle, &monitor);
    const auto &work = monitor.rcWork;
    const float availableWidth = static_cast<float>(work.right - work.left - 24);
    const float availableHeight = static_cast<float>(work.bottom - work.top - 24);
    state->scale =
        std::min({static_cast<float>(dpi) / 96, availableWidth / width, availableHeight / height});
    const int w = pixels(width, state->scale);
    const int h = pixels(height, state->scale);
    const int x = suggested
                      ? std::clamp(static_cast<int>(suggested->left), static_cast<int>(work.left),
                                   static_cast<int>(work.right) - w)
                      : static_cast<int>(work.left + (work.right - work.left - w) / 2);
    const int y = suggested
                      ? std::clamp(static_cast<int>(suggested->top), static_cast<int>(work.top),
                                   static_cast<int>(work.bottom) - h)
                      : static_cast<int>(work.top + (work.bottom - work.top - h) / 2);
    SetWindowPos(dialog, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    move(dialog, IDC_CLOSE, 454, 4, 32, 28, state->scale);
    move(dialog, IDC_TITLE, 28, 108, 444, 34, state->scale);
    move(dialog, IDC_SUBTITLE, 28, 146, 444, 23, state->scale);
    move(dialog, IDC_SHORTCUT_LABEL, 44, 332, 158, 30, state->scale);
    move(dialog, IDC_HOTKEY, 214, 326, 244, 40, state->scale);
    move(dialog, IDC_HINT, 44, 372, 250, 22, state->scale);
    move(dialog, IDC_RESET, 354, 370, 104, 26, state->scale);
    move(dialog, IDC_AUTO, 40, 432, 420, 60, state->scale);
    move(dialog, IDC_STARTUP, 40, 500, 420, 60, state->scale);
    move(dialog, IDC_STATUS, 30, 578, 440, 40, state->scale);
    move(dialog, IDC_FOOTER, 28, 646, 230, 40, state->scale);
    move(dialog, IDCANCEL, 308, 650, 80, 34, state->scale);
    move(dialog, IDOK, 396, 650, 76, 34, state->scale);
    const auto font = CreateFontW(-pixels(13, state->scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                                  FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (font) {
        for (HWND control = GetWindow(dialog, GW_CHILD); control;
             control = GetWindow(control, GW_HWNDNEXT))
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        if (state->fallbackFont)
            DeleteObject(state->fallbackFont);
        state->fallbackFont = font;
    }
    const int corner = 2;
    if (FAILED(DwmSetWindowAttribute(dialog, 33, &corner, sizeof(corner)))) {
        const auto region = CreateRoundRectRgn(0, 0, w + 1, h + 1, pixels(16, state->scale),
                                               pixels(16, state->scale));
        if (!SetWindowRgn(dialog, region, TRUE))
            DeleteObject(region);
    }
    RedrawWindow(dialog, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    state->resizing = false;
}

bool initialize(HWND dialog, HINSTANCE instance) {
    auto state = std::make_unique<Appearance>();
    GdiplusStartupInput startup;
    if (GdiplusStartup(&state->token, &startup, nullptr) != Ok)
        return false;
    state->highContrast = highContrast();
    state->fallbackBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    state->icon = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR));
    if (!SetPropW(dialog, appearanceProperty, reinterpret_cast<HANDLE>(state.get())))
        return false;
    for (HWND control = GetWindow(dialog, GW_CHILD); control;
         control = GetWindow(control, GW_HWNDNEXT))
        SetWindowSubclass(control, controlProc, 2, reinterpret_cast<DWORD_PTR>(state.get()));
    state.release();
    const auto changeDpiBehavior =
        reinterpret_cast<decltype(&SetDialogDpiChangeBehavior)>(reinterpret_cast<void *>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetDialogDpiChangeBehavior")));
    if (changeDpiBehavior)
        changeDpiBehavior(dialog, DDC_DISABLE_ALL, DDC_DISABLE_ALL);
    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(dialog, &margins);
    layout(dialog, GetDpiForWindow(dialog));
    return true;
}

void destroy(HWND dialog) {
    auto *state = appearance(dialog);
    if (!state)
        return;
    for (HWND control = GetWindow(dialog, GW_CHILD); control;
         control = GetWindow(control, GW_HWNDNEXT))
        RemoveWindowSubclass(control, controlProc, 2);
    RemovePropW(dialog, appearanceProperty);
    delete state;
}

void setRecording(HWND dialog, bool recording) {
    if (auto *state = appearance(dialog)) {
        state->recording = recording;
        InvalidateRect(GetDlgItem(dialog, IDC_HOTKEY), nullptr, FALSE);
    }
}

void setStatus(HWND dialog, const wchar_t *value, bool error) {
    if (auto *state = appearance(dialog))
        state->error = error;
    SetDlgItemTextW(dialog, IDC_STATUS, value);
    NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED, GetDlgItem(dialog, IDC_STATUS), OBJID_CLIENT, 0);
}

bool handleMessage(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam, INT_PTR &result) {
    auto *state = appearance(dialog);
    if (!state)
        return false;
    result = 0;
    switch (message) {
    case WM_NCCALCSIZE:
        return wparam != 0;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(dialog, &point);
        result = point.y < pixels(36, state->scale) && point.x < pixels(450, state->scale)
                     ? HTCAPTION
                     : HTCLIENT;
        return true;
    }
    case WM_ERASEBKGND:
        if (!state->highContrast) {
            result = 1;
            return true;
        }
        return false;
    case WM_PAINT:
        if (!state->highContrast) {
            PAINTSTRUCT ps;
            const auto dc = BeginPaint(dialog, &ps);
            paint(dialog, dc, *state, true);
            EndPaint(dialog, &ps);
            return true;
        }
        return false;
    case WM_PRINTCLIENT:
        if (!state->highContrast) {
            paint(dialog, reinterpret_cast<HDC>(wparam), *state, true);
            return true;
        }
        return false;
    case WM_DPICHANGED:
        layout(dialog, HIWORD(wparam), reinterpret_cast<const RECT *>(lparam));
        return true;
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
        state->highContrast = highContrast();
        DeleteObject(state->fallbackBrush);
        state->fallbackBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        RedrawWindow(dialog, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
        return false;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
        if (state->highContrast) {
            SetTextColor(reinterpret_cast<HDC>(wparam), GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(reinterpret_cast<HDC>(wparam), GetSysColor(COLOR_WINDOW));
            result = reinterpret_cast<INT_PTR>(state->fallbackBrush);
            return true;
        }
        return false;
    default:
        return false;
    }
}
}
