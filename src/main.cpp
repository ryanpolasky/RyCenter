#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "geometry.h"
#include "resource.h"
#include "settings_ui.h"
#include "shortcut.h"

namespace {
constexpr wchar_t windowClass[] = L"RyCenter.MessageWindow";
constexpr wchar_t settingsKey[] = L"Software\\RyCenter";
constexpr wchar_t startupKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr UINT trayMessage = WM_APP + 1;
constexpr UINT settingsMessage = WM_APP + 2;
constexpr UINT settingsCommand = 10;
constexpr UINT quitCommand = 11;

struct Options {
    DWORD version = 1;
    DWORD hotkey = rycenter::defaultShortcut;
    DWORD automatic = 0;
};

struct PendingWindow {
    HWND hwnd;
    DWORD process;
    DWORD thread;
    UINT_PTR timer = 0;
    RECT bounds{};
};

HINSTANCE instance;
HWND messageWindow;
HWND settingsWindow;
Options options;
int hotkeyId = 1;
bool hotkeyRegistered = false;
HWINEVENTHOOK openHook;
HWINEVENTHOOK dragHook;
std::vector<PendingWindow> pending;
UINT_PTR nextTimer = 100;
NOTIFYICONDATAW tray{};
UINT taskbarCreated;
rycenter::ShortcutDraft shortcutDraft;
UINT capturedKey = 0;
bool shortcutSuspended = false;

void showError(HWND owner, const wchar_t *text) {
    MessageBoxW(owner, text, L"RyCenter", MB_OK | MB_ICONWARNING);
}

bool validHotkey(DWORD hotkey) { return rycenter::validShortcut(hotkey); }

bool loadOptions() {
    Options stored;
    DWORD size = sizeof(stored);
    if (RegGetValueW(HKEY_CURRENT_USER, settingsKey, L"Options", RRF_RT_REG_BINARY, nullptr,
                     &stored, &size) != ERROR_SUCCESS ||
        size != sizeof(stored) || stored.version != 1 || !validHotkey(stored.hotkey) ||
        stored.automatic > 1) {
        return false;
    }
    options = stored;
    return true;
}

bool saveOptions(const Options &value) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, settingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const auto result = RegSetValueExW(key, L"Options", 0, REG_BINARY,
                                       reinterpret_cast<const BYTE *>(&value), sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

std::wstring startupCommandLine() {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) {
        return {};
    }
    path.resize(length);
    return L"\"" + path + L"\"";
}

bool startsWithWindows() {
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, startupKey, L"RyCenter", RRF_RT_REG_SZ, nullptr, nullptr,
                        &size) == ERROR_SUCCESS;
}

bool setStartup(bool enabled) {
    HKEY key;
    auto result = RegCreateKeyExW(HKEY_CURRENT_USER, startupKey, 0, nullptr, 0, KEY_SET_VALUE,
                                  nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) {
        return false;
    }
    if (enabled) {
        const auto command = startupCommandLine();
        result = command.empty()
                     ? ERROR_BAD_PATHNAME
                     : RegSetValueExW(key, L"RyCenter", 0, REG_SZ,
                                      reinterpret_cast<const BYTE *>(command.c_str()),
                                      static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, L"RyCenter");
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS || (!enabled && result == ERROR_FILE_NOT_FOUND);
}

bool registerShortcut(DWORD hotkey, int id) {
    const auto flags = HIBYTE(hotkey);
    UINT modifiers = MOD_NOREPEAT;
    if (flags & HOTKEYF_CONTROL)
        modifiers |= MOD_CONTROL;
    if (flags & HOTKEYF_ALT)
        modifiers |= MOD_ALT;
    if (flags & HOTKEYF_SHIFT)
        modifiers |= MOD_SHIFT;
    if (flags & rycenter::Windows)
        modifiers |= MOD_WIN;
    return RegisterHotKey(messageWindow, id, modifiers, LOBYTE(hotkey)) != FALSE;
}

rycenter::Rect rectangle(const RECT &rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

bool eligible(HWND hwnd, bool automatic) {
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd) || IsZoomed(hwnd) ||
        GetAncestor(hwnd, GA_ROOT) != hwnd) {
        return false;
    }
    DWORD process;
    GetWindowThreadProcessId(hwnd, &process);
    const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const auto extended = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((process == GetCurrentProcessId() && hwnd != settingsWindow) || (style & WS_CHILD) ||
        !(style & (WS_CAPTION | WS_THICKFRAME)) ||
        (extended & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))) {
        return false;
    }
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked) {
        return false;
    }
    if (automatic) {
        wchar_t name[64]{};
        GetClassNameW(hwnd, name, static_cast<int>(std::size(name)));
        if (GetWindow(hwnd, GW_OWNER) || lstrcmpW(name, L"#32770") == 0) {
            return false;
        }
    }
    return true;
}

enum class CenterResult { Skipped, Centered, Failed };

CenterResult centerWindow(HWND hwnd, bool automatic) {
    if (!eligible(hwnd, automatic))
        return CenterResult::Skipped;
    RECT outer;
    if (!GetWindowRect(hwnd, &outer))
        return CenterResult::Failed;
    RECT visible = outer;
    if (FAILED(
            DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible)))) {
        visible = outer;
    }
    if (visible.right <= visible.left || visible.bottom <= visible.top)
        return CenterResult::Skipped;
    MONITORINFO monitor{sizeof(MONITORINFO), {}, {}, 0};
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor) ||
        rycenter::coversMonitor(rectangle(visible), rectangle(monitor.rcMonitor))) {
        return CenterResult::Skipped;
    }
    const auto position =
        rycenter::centeredPosition(rectangle(outer), rectangle(visible), rectangle(monitor.rcWork));
    if (position.x == outer.left && position.y == outer.top)
        return CenterResult::Centered;
    const auto moved = SetWindowPos(hwnd, nullptr, position.x, position.y, 0, 0,
                                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                                        SWP_ASYNCWINDOWPOS);
    return moved ? CenterResult::Centered : CenterResult::Failed;
}

void forgetPending(HWND hwnd) {
    for (auto it = pending.begin(); it != pending.end();) {
        if (it->hwnd == hwnd) {
            if (it->timer)
                KillTimer(messageWindow, it->timer);
            it = pending.erase(it);
        } else {
            ++it;
        }
    }
}

void CALLBACK windowEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG object, LONG child, DWORD,
                          DWORD) {
    if (!hwnd || object != OBJID_WINDOW || child != CHILDID_SELF)
        return;
    if (event == EVENT_OBJECT_DESTROY || event == EVENT_SYSTEM_MOVESIZESTART) {
        forgetPending(hwnd);
        return;
    }
    if (event == EVENT_OBJECT_CREATE) {
        if (GetAncestor(hwnd, GA_ROOT) != hwnd)
            return;
        forgetPending(hwnd);
        DWORD process = 0;
        const auto thread = GetWindowThreadProcessId(hwnd, &process);
        if (thread && process != GetCurrentProcessId()) {
            pending.push_back({hwnd, process, thread});
        }
        return;
    }
    if (event == EVENT_OBJECT_SHOW) {
        const auto it = std::find_if(pending.begin(), pending.end(),
                                     [hwnd](const auto &item) { return item.hwnd == hwnd; });
        if (it == pending.end() || it->timer)
            return;
        if (!eligible(hwnd, true) || !GetWindowRect(hwnd, &it->bounds)) {
            forgetPending(hwnd);
            return;
        }
        it->timer = SetTimer(messageWindow, ++nextTimer, 250, nullptr);
        if (!it->timer)
            forgetPending(hwnd);
    }
}

void disableAutomatic() {
    if (openHook)
        UnhookWinEvent(openHook);
    if (dragHook)
        UnhookWinEvent(dragHook);
    openHook = dragHook = nullptr;
    for (const auto &item : pending) {
        if (item.timer)
            KillTimer(messageWindow, item.timer);
    }
    std::vector<PendingWindow>().swap(pending);
}

bool enableAutomatic() {
    if (openHook)
        return true;
    constexpr auto flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    openHook =
        SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr, windowEvent, 0, 0, flags);
    dragHook = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZESTART, nullptr,
                               windowEvent, 0, 0, flags);
    if (!openHook || !dragHook) {
        disableAutomatic();
        return false;
    }
    return true;
}

void handleTimer(UINT_PTR id) {
    KillTimer(messageWindow, id);
    const auto it = std::find_if(pending.begin(), pending.end(),
                                 [id](const auto &item) { return item.timer == id; });
    if (it == pending.end())
        return;
    const auto item = *it;
    pending.erase(it);
    DWORD process = 0;
    const auto thread = GetWindowThreadProcessId(item.hwnd, &process);
    RECT now;
    if (thread == item.thread && process == item.process && GetWindowRect(item.hwnd, &now) &&
        EqualRect(&now, &item.bounds)) {
        centerWindow(item.hwnd, true);
    }
}

void updateTooltip() {
    std::wstring label = L"RyCenter";
    if (hotkeyRegistered) {
        label += L" — ";
        const auto flags = HIBYTE(options.hotkey);
        if (flags & HOTKEYF_CONTROL)
            label += L"Ctrl+";
        if (flags & HOTKEYF_ALT)
            label += L"Alt+";
        if (flags & HOTKEYF_SHIFT)
            label += L"Shift+";
        if (flags & rycenter::Windows)
            label += L"Win+";
        auto scan = MapVirtualKeyW(LOBYTE(options.hotkey), MAPVK_VK_TO_VSC);
        if (flags & HOTKEYF_EXT)
            scan |= 0x100;
        wchar_t name[64]{};
        GetKeyNameTextW(static_cast<LONG>(scan << 16), name, static_cast<int>(std::size(name)));
        label += name;
    } else {
        label += L" — shortcut unavailable; open Settings";
    }
    lstrcpynW(tray.szTip, label.c_str(), static_cast<int>(std::size(tray.szTip)));
    tray.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &tray);
}

bool addTrayIcon() {
    tray.cbSize = sizeof(tray);
    tray.hWnd = messageWindow;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    tray.uCallbackMessage = trayMessage;
    tray.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    lstrcpynW(tray.szTip, L"RyCenter", static_cast<int>(std::size(tray.szTip)));
    if (!Shell_NotifyIconW(NIM_ADD, &tray))
        return false;
    tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &tray);
    updateTooltip();
    return true;
}

std::wstring shortcutLabel(DWORD shortcut) {
    const auto flags = HIBYTE(shortcut);
    std::wstring label;
    if (flags & HOTKEYF_CONTROL)
        label += L"Ctrl + ";
    if (flags & HOTKEYF_ALT)
        label += L"Alt + ";
    if (flags & HOTKEYF_SHIFT)
        label += L"Shift + ";
    if (flags & rycenter::Windows)
        label += L"Win + ";
    auto scan = MapVirtualKeyW(LOBYTE(shortcut), MAPVK_VK_TO_VSC);
    if (flags & HOTKEYF_EXT)
        scan |= 0x100;
    wchar_t name[64]{};
    if (!GetKeyNameTextW(static_cast<LONG>(scan << 16), name, static_cast<int>(std::size(name))))
        lstrcpynW(name, L"Key", static_cast<int>(std::size(name)));
    return label + name;
}

void settingsStatus(HWND dialog, const wchar_t *text, bool error = false) {
    rycenter::ui::setStatus(dialog, text, error);
}

void updateShortcutControl(HWND dialog) {
    rycenter::ui::setRecording(dialog, shortcutDraft.recording);
    SetDlgItemTextW(dialog, IDC_HOTKEY,
                    shortcutDraft.recording && !capturedKey
                        ? L"Press a shortcut…"
                        : shortcutLabel(shortcutDraft.value).c_str());
}

void finishRecording(HWND dialog, bool accept) {
    if (!accept)
        shortcutDraft.cancel();
    shortcutDraft.recording = false;
    capturedKey = 0;
    if (shortcutSuspended) {
        hotkeyRegistered = registerShortcut(options.hotkey, hotkeyId);
        shortcutSuspended = false;
        updateTooltip();
    }
    updateShortcutControl(dialog);
    settingsStatus(
        dialog,
        hotkeyRegistered ? L"" : L"Your saved shortcut is unavailable. Choose another and Save.",
        !hotkeyRegistered);
}

void beginRecording(HWND dialog) {
    if (shortcutDraft.recording)
        return;
    shortcutDraft.begin();
    capturedKey = 0;
    if (hotkeyRegistered) {
        UnregisterHotKey(messageWindow, hotkeyId);
        hotkeyRegistered = false;
        shortcutSuspended = true;
    }
    updateShortcutControl(dialog);
    settingsStatus(dialog, L"Press Ctrl, Alt, or Win with another key. Escape cancels.");
}

BYTE keyboardModifiers() {
    BYTE flags = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        flags |= HOTKEYF_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000)
        flags |= HOTKEYF_ALT;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        flags |= HOTKEYF_SHIFT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000)
        flags |= rycenter::Windows;
    return flags;
}

LRESULT CALLBACK shortcutProc(HWND control, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                              DWORD_PTR) {
    const auto dialog = GetParent(control);
    if (message == WM_GETDLGCODE && shortcutDraft.recording) {
        const auto input = reinterpret_cast<const MSG *>(lparam);
        if (input && input->wParam == VK_TAB && keyboardModifiers() == 0)
            return DLGC_BUTTON;
        return DLGC_BUTTON | DLGC_WANTALLKEYS;
    }
    if (message == WM_SETFOCUS) {
        beginRecording(dialog);
    } else if (message == WM_KILLFOCUS) {
        if (shortcutDraft.recording)
            finishRecording(dialog, false);
    } else if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        if (shortcutDraft.recording) {
            if ((lparam & (1LL << 30)) || capturedKey)
                return 0;
            const auto flags = keyboardModifiers();
            if (wparam == VK_ESCAPE && !flags) {
                finishRecording(dialog, false);
                return 0;
            }
            if (wparam == VK_CONTROL || wparam == VK_MENU || wparam == VK_SHIFT ||
                wparam == VK_LWIN || wparam == VK_RWIN)
                return 0;
            const auto extended = (lparam & (1LL << 24)) ? HOTKEYF_EXT : 0;
            if (shortcutDraft.capture(static_cast<BYTE>(wparam),
                                      static_cast<BYTE>(flags | extended))) {
                capturedKey = static_cast<UINT>(wparam);
                updateShortcutControl(dialog);
            } else {
                settingsStatus(dialog, L"Include Ctrl, Alt, or Win with your shortcut.", true);
            }
            return 0;
        }
    } else if ((message == WM_KEYUP || message == WM_SYSKEYUP) && shortcutDraft.recording) {
        if (capturedKey == wparam)
            finishRecording(dialog, true);
        return 0;
    } else if (message == WM_CHAR || message == WM_SYSCHAR) {
        return 0;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

INT_PTR CALLBACK settingsProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    INT_PTR result = 0;
    if (rycenter::ui::handleMessage(dialog, message, wparam, lparam, result)) {
        if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC)
            return result;
        SetWindowLongPtrW(dialog, DWLP_MSGRESULT, result);
        return TRUE;
    }
    if (message == WM_INITDIALOG) {
        settingsWindow = dialog;
        SendMessageW(dialog, WM_SETICON, ICON_BIG,
                     reinterpret_cast<LPARAM>(LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP))));
        shortcutDraft = {};
        shortcutDraft.value = static_cast<WORD>(options.hotkey);
        capturedKey = 0;
        shortcutSuspended = false;
        rycenter::ui::initialize(dialog, instance);
        SetWindowSubclass(GetDlgItem(dialog, IDC_HOTKEY), shortcutProc, 1, 0);
        updateShortcutControl(dialog);
        CheckDlgButton(dialog, IDC_AUTO, options.automatic ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_STARTUP, startsWithWindows() ? BST_CHECKED : BST_UNCHECKED);
        SetFocus(GetDlgItem(dialog, IDOK));
        return FALSE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDC_HOTKEY) {
        beginRecording(dialog);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDC_RESET) {
        finishRecording(dialog, false);
        shortcutDraft.reset();
        updateShortcutControl(dialog);
        settingsStatus(dialog, L"Shortcut reset to Ctrl + Alt + C. Save to apply.");
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK) {
        if (shortcutDraft.recording)
            finishRecording(dialog, false);
        Options next = options;
        next.hotkey = shortcutDraft.value;
        next.automatic = IsDlgButtonChecked(dialog, IDC_AUTO) == BST_CHECKED;
        if (!validHotkey(next.hotkey)) {
            settingsStatus(dialog, L"Choose a shortcut with Ctrl, Alt, or Win and another key.",
                           true);
            return TRUE;
        }
        const bool changeHotkey =
            !rycenter::sameShortcut(next.hotkey, options.hotkey) || !hotkeyRegistered;
        const int nextId = hotkeyId == 1 ? 2 : 1;
        if (changeHotkey && !registerShortcut(next.hotkey, nextId)) {
            settingsStatus(dialog, L"That shortcut is in use or reserved by Windows. Try another.",
                           true);
            return TRUE;
        }
        const bool enable = next.automatic && !options.automatic;
        if (enable && !enableAutomatic()) {
            if (changeHotkey)
                UnregisterHotKey(messageWindow, nextId);
            settingsStatus(
                dialog,
                L"Windows could not enable automatic centering. Your settings were not changed.",
                true);
            return TRUE;
        }
        if (!saveOptions(next)) {
            if (enable)
                disableAutomatic();
            if (changeHotkey)
                UnregisterHotKey(messageWindow, nextId);
            settingsStatus(dialog, L"Could not save your settings in your Windows user profile.",
                           true);
            return TRUE;
        }
        const bool startup = IsDlgButtonChecked(dialog, IDC_STARTUP) == BST_CHECKED;
        if (!setStartup(startup)) {
            saveOptions(options);
            if (enable)
                disableAutomatic();
            if (changeHotkey)
                UnregisterHotKey(messageWindow, nextId);
            settingsStatus(dialog, L"Could not change launch at login. Settings were not changed.",
                           true);
            return TRUE;
        }
        if (changeHotkey) {
            if (hotkeyRegistered)
                UnregisterHotKey(messageWindow, hotkeyId);
            hotkeyId = nextId;
            hotkeyRegistered = true;
        }
        if (!next.automatic)
            disableAutomatic();
        options = next;
        updateTooltip();
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if ((message == WM_COMMAND && (LOWORD(wparam) == IDCANCEL || LOWORD(wparam) == IDC_CLOSE)) ||
        message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    if (message == WM_DESTROY) {
        finishRecording(dialog, false);
        rycenter::ui::destroy(dialog);
        settingsWindow = nullptr;
    }
    return FALSE;
}

void showSettings() {
    if (settingsWindow) {
        SetForegroundWindow(settingsWindow);
        return;
    }
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), messageWindow, settingsProc, 0);
}

void showTrayMenu(LPARAM lparam) {
    const auto menu = CreatePopupMenu();
    if (!menu)
        return;
    AppendMenuW(menu, MF_STRING, settingsCommand, L"Settings…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, quitCommand, L"Quit RyCenter");
    POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    if (point.x == -1 && point.y == -1)
        GetCursorPos(&point);
    SetForegroundWindow(settingsWindow ? settingsWindow : messageWindow);
    const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0,
                                        messageWindow, nullptr);
    DestroyMenu(menu);
    PostMessageW(messageWindow, WM_NULL, 0, 0);
    if (command == settingsCommand)
        showSettings();
    if (command == quitCommand) {
        if (settingsWindow)
            EndDialog(settingsWindow, IDCANCEL);
        DestroyWindow(messageWindow);
    }
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == taskbarCreated && taskbarCreated) {
        addTrayIcon();
        return 0;
    }
    switch (message) {
    case WM_HOTKEY:
        if (static_cast<int>(wparam) == hotkeyId) {
            const auto target = GetForegroundWindow();
            if (centerWindow(target, false) == CenterResult::Failed) {
                tray.uFlags = NIF_INFO;
                lstrcpynW(tray.szInfoTitle, L"Could not center this window",
                          static_cast<int>(std::size(tray.szInfoTitle)));
                lstrcpynW(tray.szInfo,
                          L"The app may be running as administrator or managing its own position.",
                          static_cast<int>(std::size(tray.szInfo)));
                tray.dwInfoFlags = NIIF_WARNING;
                Shell_NotifyIconW(NIM_MODIFY, &tray);
            }
        }
        return 0;
    case WM_TIMER:
        handleTimer(wparam);
        return 0;
    case trayMessage:
        if (LOWORD(lparam) == WM_CONTEXTMENU)
            showTrayMenu(static_cast<LPARAM>(wparam));
        if (LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == NIN_KEYSELECT)
            showSettings();
        return 0;
    case settingsMessage:
        showSettings();
        return 0;
    case WM_DESTROY:
        disableAutomatic();
        if (hotkeyRegistered)
            UnregisterHotKey(hwnd, hotkeyId);
        Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}
}

int WINAPI wWinMain(HINSTANCE current, HINSTANCE, PWSTR, int) {
    const auto mutex =
        CreateMutexW(nullptr, FALSE, L"Local\\RyCenter.6E05CC03-46A2-4EB1-9C4A-2909A480103F");
    if (!mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (const auto existing = FindWindowW(windowClass, nullptr)) {
            DWORD process = 0;
            GetWindowThreadProcessId(existing, &process);
            AllowSetForegroundWindow(process);
            PostMessageW(existing, settingsMessage, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }
    instance = current;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_HOTKEY_CLASS | ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        CloseHandle(mutex);
        return 1;
    }
    const bool configured = loadOptions();
    WNDCLASSW cls{};
    cls.lpfnWndProc = windowProc;
    cls.hInstance = instance;
    cls.lpszClassName = windowClass;
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&cls)) {
        CloseHandle(mutex);
        return 1;
    }
    messageWindow = CreateWindowExW(0, windowClass, L"RyCenter", WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                                    nullptr, instance, nullptr);
    if (!messageWindow) {
        CloseHandle(mutex);
        return 1;
    }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    hotkeyRegistered = registerShortcut(options.hotkey, hotkeyId);
    if (!addTrayIcon()) {
        showError(nullptr, L"Could not add RyCenter to the notification area. Please try again.");
        DestroyWindow(messageWindow);
        CloseHandle(mutex);
        return 1;
    }
    if (options.automatic && !enableAutomatic()) {
        options.automatic = 0;
        showError(nullptr, L"Automatic centering could not start. Your shortcut still works.");
    }
    if (!hotkeyRegistered) {
        showError(nullptr,
                  L"Your centering shortcut is already in use. Choose another in Settings.");
    }
    if (!configured || !hotkeyRegistered)
        PostMessageW(messageWindow, settingsMessage, 0, 0);
    MSG message;
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result == -1)
        DestroyWindow(messageWindow);
    CloseHandle(mutex);
    return result == -1 ? 1 : 0;
}
