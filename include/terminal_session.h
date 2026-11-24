#pragma once
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <termios.h>
#include <csignal>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

class TerminalSession {
public:
    // 检测能力 保存现场 信号 原始输入 备用屏 尺寸
    TerminalSession();
    ~TerminalSession();

    // Meyer Singleton
    TerminalSession(const TerminalSession&) = delete;
    TerminalSession& operator=(const TerminalSession&) = delete;

    // 外部访问函数
    bool    isTTY() const {return is_tty_; }
    int     rows()  const {return rows_; }
    int     cols()  const {return cols_; }

    void refreshSize();
    bool consumeResizeFlag();

private:
    void enterRaw();
    void enterAltScreen();
    void leaveAltScreen();
    void restoreTermios();

#ifdef _WIN32
    DWORD in_mode_  = 0;
    DWORD out_mode_ = 0;
    UINT  saved_cp_ = 0;
    HANDLE hout_    = INVALID_HANDLE_VALUE;
#else
    static void signalHandler(int sig);
    termios orig_{};
    static inline TerminalSession* g_instance_ = nullptr;
#endif

    bool    is_tty_         = false;
    bool    raw_entered_    = false;
    bool    alt_entered_    = false;
    int     rows_           = 24;
    int     cols_           = 80;

#ifndef _WIN32
    static inline volatile sig_atomic_t g_resized_ = 0;
#endif
};
