#include "ui/MediaKeys.hpp"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <atomic>
#include <chrono>

static constexpr int HK_BASE_ID = 1000;
static constexpr UINT kMediaVKeys[4] = {
    VK_MEDIA_PLAY_PAUSE,
    VK_MEDIA_NEXT_TRACK,
    VK_MEDIA_PREV_TRACK,
    VK_MEDIA_STOP
};
static constexpr UINT kModCombos[8] = {
    0,
    MOD_CONTROL,
    MOD_SHIFT,
    MOD_ALT,
    MOD_CONTROL | MOD_SHIFT,
    MOD_CONTROL | MOD_ALT,
    MOD_SHIFT | MOD_ALT,
    MOD_CONTROL | MOD_SHIFT | MOD_ALT
};
static constexpr int HK_TOTAL = 4 * 8;

static WNDPROC g_origWndProc = nullptr;
static std::atomic<int> g_pendingHotkey{0};

static LRESULT CALLBACK mediaKeysWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_HOTKEY) {
        int id = static_cast<int>(wParam);
        if (id >= HK_BASE_ID && id < HK_BASE_ID + HK_TOTAL) {
            int actionIdx = (id - HK_BASE_ID) / 8;
            g_pendingHotkey.store(actionIdx + 1);
            return 0;
        }
    }
    return CallWindowProcW(g_origWndProc, hwnd, msg, wParam, lParam);
}

void MediaKeys::init(GLFWwindow* window) {
    m_hwnd = glfwGetWin32Window(window);
    if (m_hwnd && !g_origWndProc) {
        HWND hwnd = static_cast<HWND>(m_hwnd);
        g_origWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(mediaKeysWndProc))
        );
    }
    updateHotkeys();
}

void MediaKeys::shutdown() {
    m_enabled = false;
    m_exclusive = false;
    updateHotkeys();
    if (m_hwnd && g_origWndProc) {
        HWND hwnd = static_cast<HWND>(m_hwnd);
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
        g_origWndProc = nullptr;
    }
}

void MediaKeys::setConfig(bool enabled, bool exclusive, bool forceFocus) {
    m_enabled = enabled;
    m_exclusive = exclusive;
    m_forceFocus = forceFocus;
    updateHotkeys();
}

void MediaKeys::updateHotkeys() {
    if (!m_hwnd) return;
    HWND hwnd = static_cast<HWND>(m_hwnd);
    if (m_hotkeysRegistered) {
        for (int i = 0; i < HK_TOTAL; ++i) {
            UnregisterHotKey(hwnd, HK_BASE_ID + i);
        }
        m_hotkeysRegistered = false;
    }
    if (m_enabled && m_exclusive) {
        constexpr UINT kNoRepeat = 0x4000; // MOD_NOREPEAT
        bool anyOk = false;
        for (int a = 0; a < 4; ++a) {
            for (int m = 0; m < 8; ++m) {
                if (RegisterHotKey(hwnd, HK_BASE_ID + a * 8 + m, kModCombos[m] | kNoRepeat, kMediaVKeys[a])) {
                    anyOk = true;
                }
            }
        }
        m_hotkeysRegistered = anyOk;
    }
}

void MediaKeys::focusAppWindow() {
    if (!m_hwnd) return;
    HWND hwnd = static_cast<HWND>(m_hwnd);
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

MediaAction MediaKeys::pollGlobalMediaKey() {
    if (!m_enabled) return MediaAction::None;

    MediaAction act = MediaAction::None;

    int hk = g_pendingHotkey.exchange(0);
    if (hk == 1) act = MediaAction::PlayPause;
    else if (hk == 2) act = MediaAction::Next;
    else if (hk == 3) act = MediaAction::Prev;
    else if (hk == 4) act = MediaAction::Stop;

    bool pp = (GetAsyncKeyState(VK_MEDIA_PLAY_PAUSE) & 0x8000) != 0;
    bool nx = (GetAsyncKeyState(VK_MEDIA_NEXT_TRACK) & 0x8000) != 0;
    bool pv = (GetAsyncKeyState(VK_MEDIA_PREV_TRACK) & 0x8000) != 0;
    bool st = (GetAsyncKeyState(VK_MEDIA_STOP) & 0x8000) != 0;

    if (act == MediaAction::None) {
        if (pp && !m_prevPlayPause) act = MediaAction::PlayPause;
        else if (nx && !m_prevNext) act = MediaAction::Next;
        else if (pv && !m_prevPrev) act = MediaAction::Prev;
        else if (st && !m_prevStop) act = MediaAction::Stop;
    }

    m_prevPlayPause = pp;
    m_prevNext = nx;
    m_prevPrev = pv;
    m_prevStop = st;

    if (act != MediaAction::None) {
        static auto s_lastTrigger = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastTrigger).count() < 220) {
            return MediaAction::None;
        }
        s_lastTrigger = now;
    }

    return act;
}

std::string MediaKeys::keyName(int glfwKey) {
    if (glfwKey == GLFW_KEY_SPACE) return "Space";
    if (glfwKey == GLFW_KEY_LEFT) return "Left";
    if (glfwKey == GLFW_KEY_RIGHT) return "Right";
    if (glfwKey == GLFW_KEY_UP) return "Up";
    if (glfwKey == GLFW_KEY_DOWN) return "Down";
    if (glfwKey == GLFW_KEY_ESCAPE) return "Esc";
    if (glfwKey == GLFW_KEY_ENTER) return "Enter";
    const char* name = glfwGetKeyName(glfwKey, 0);
    if (name && name[0]) {
        std::string s(name);
        for (char& c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        return s;
    }
    return "Key " + std::to_string(glfwKey);
}
