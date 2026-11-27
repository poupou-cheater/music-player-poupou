#pragma once

#include <GLFW/glfw3.h>
#include <string>

enum class MediaAction {
    None = 0,
    PlayPause,
    Next,
    Prev,
    Stop
};

class MediaKeys {
public:
    void init(GLFWwindow* window);
    void shutdown();
    void setConfig(bool enabled, bool exclusive, bool forceFocus);
    void focusAppWindow();

    MediaAction pollGlobalMediaKey();
    static std::string keyName(int glfwKey);

private:
    void updateHotkeys();

    bool m_enabled = true;
    bool m_exclusive = true;
    bool m_forceFocus = false;
    bool m_hotkeysRegistered = false;
    void* m_hwnd = nullptr;

    bool m_prevPlayPause = false;
    bool m_prevNext = false;
    bool m_prevPrev = false;
    bool m_prevStop = false;
};
