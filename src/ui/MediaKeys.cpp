#include "ui/MediaKeys.hpp"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <atomic>

static constexpr int HK_PLAY_PAUSE = 1001;
static constexpr int HK_NEXT = 1002;
static constexpr int HK_PREV = 1003;
static constexpr int HK_STOP = 1004;

static WNDPROC g_origWndProc = nullptr;
static std::atomic<int> g_pendingHotkey{0};

static LRESULT CALLBACK mediaKeysWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_HOTKEY) {
        if (wParam == HK_PLAY_PAUSE || wParam == HK_NEXT || wParam == HK_PREV || wParam == HK_STOP) {
            g_pendingHotkey.store(static_cast<int>(wParam));
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
        UnregisterHotKey(hwnd, HK_PLAY_PAUSE);
        UnregisterHotKey(hwnd, HK_NEXT);
        UnregisterHotKey(hwnd, HK_PREV);
        UnregisterHotKey(hwnd, HK_STOP);
        m_hotkeysRegistered = false;
    }
    if (m_enabled && m_exclusive) {
        BOOL ok1 = RegisterHotKey(hwnd, HK_PLAY_PAUSE, 0, VK_MEDIA_PLAY_PAUSE);
        BOOL ok2 = RegisterHotKey(hwnd, HK_NEXT, 0, VK_MEDIA_NEXT_TRACK);
        BOOL ok3 = RegisterHotKey(hwnd, HK_PREV, 0, VK_MEDIA_PREV_TRACK);
        BOOL ok4 = RegisterHotKey(hwnd, HK_STOP, 0, VK_MEDIA_STOP);
        m_hotkeysRegistered = (ok1 || ok2 || ok3 || ok4);
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
    if (hk == HK_PLAY_PAUSE) act = MediaAction::PlayPause;
    else if (hk == HK_NEXT) act = MediaAction::Next;
    else if (hk == HK_PREV) act = MediaAction::Prev;
    else if (hk == HK_STOP) act = MediaAction::Stop;

    if (act == MediaAction::None && (!m_exclusive || !m_hotkeysRegistered || m_forceFocus)) {
        bool pp = (GetAsyncKeyState(VK_MEDIA_PLAY_PAUSE) & 0x8000) != 0;
        bool nx = (GetAsyncKeyState(VK_MEDIA_NEXT_TRACK) & 0x8000) != 0;
        bool pv = (GetAsyncKeyState(VK_MEDIA_PREV_TRACK) & 0x8000) != 0;
        bool st = (GetAsyncKeyState(VK_MEDIA_STOP) & 0x8000) != 0;

        if (pp && !m_prevPlayPause) act = MediaAction::PlayPause;
        else if (nx && !m_prevNext) act = MediaAction::Next;
        else if (pv && !m_prevPrev) act = MediaAction::Prev;
        else if (st && !m_prevStop) act = MediaAction::Stop;

        m_prevPlayPause = pp;
        m_prevNext = nx;
        m_prevPrev = pv;
        m_prevStop = st;
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
