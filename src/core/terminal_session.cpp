#include "terminal_session.h"
#include <cstdio>

// ============================ Windows ============================
#ifdef _WIN32

// 构造函数
TerminalSession::TerminalSession() {
    // 检测标准输入/输出是否为控制台
    is_tty_ = (GetFileType(GetStdHandle(STD_INPUT_HANDLE))  == FILE_TYPE_CHAR) &&
              (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_CHAR);
    if(!is_tty_) return;

    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    hout_      = GetStdHandle(STD_OUTPUT_HANDLE);
    if(hin == INVALID_HANDLE_VALUE || hout_ == INVALID_HANDLE_VALUE)
        throw std::runtime_error("[W]invalid console handle.");
    // 保存原始控制台模式
    if(!GetConsoleMode(hin, &in_mode_) || !GetConsoleMode(hout_, &out_mode_))
        throw std::runtime_error("[W]GetConsoleMode failed.");

    saved_cp_ = GetConsoleOutputCP();

    enterRaw();
    enterAltScreen();
    refreshSize();
}

// 析构函数
TerminalSession::~TerminalSession() {
    leaveAltScreen();
    restoreTermios();
}

// 切换控制台至原始输入模式（关闭行输入/回显，开启 VT 处理）
void TerminalSession::enterRaw() {
    // 开启输出 VT 序列处理，供 ANSI 转义序列生效
    SetConsoleMode(hout_, out_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    DWORD im = in_mode_;
    im &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    im |= ENABLE_EXTENDED_FLAGS;
    if(SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), im))
        raw_entered_ = true;

    // 输出使用 UTF-8
    SetConsoleOutputCP(CP_UTF8);
}

// 切换至备用屏幕 清除屏幕 光标归位 隐藏光标
void TerminalSession::enterAltScreen() {
    const char seq[] = "\033[?1049h\033[2J\033[H\033[?25l";
    std::fputs(seq, stdout);
    std::fflush(stdout);
    alt_entered_ = true;
}

// 恢复原始终端属性
void TerminalSession::restoreTermios() {
    if(!raw_entered_) return;
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), in_mode_);
    SetConsoleMode(hout_, out_mode_);
    if(saved_cp_) SetConsoleOutputCP(saved_cp_);
    raw_entered_ = false;
}

// 恢复光标显示 切换至主屏幕
void TerminalSession::leaveAltScreen() {
    if(!alt_entered_) return;
    const char seq[] = "\033[?25h\033[?1049l";
    std::fputs(seq, stdout);
    std::fflush(stdout);
    alt_entered_ = false;
}

// 根据窗口大小刷新尺寸
void TerminalSession::refreshSize() {
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if(GetConsoleScreenBufferInfo(hout_, &csbi)) {
        rows_ = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        cols_ = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
    } else {
        rows_ = 24;
        cols_ = 80;
    }
}

// 消费标志位函数：Windows 无 SIGWINCH，比较当前窗口尺寸与缓存值
bool TerminalSession::consumeResizeFlag() {
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if(!GetConsoleScreenBufferInfo(hout_, &csbi)) return false;
    int r = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    int c = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
    return (r != rows_ || c != cols_);
}

// ============================ POSIX ============================
#else

// 构造函数
TerminalSession::TerminalSession() {
    // 检测代码是否位于交互式终端 若检测不通过则终止构造
    is_tty_ = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    if(!is_tty_) return;
    // 获取终端属性配置
    if(tcgetattr(STDIN_FILENO, &orig_) != 0)
        throw std::runtime_error("[W]tcgetattr failed.");
    // 类内获得静态成员指针用于接管三类信号
    g_instance_ = this;
    std::signal(SIGINT,     signalHandler);
    std::signal(SIGTERM,    signalHandler);
    std::signal(SIGWINCH,   signalHandler);

    enterRaw();
    enterAltScreen();
    refreshSize();
}

// 析构函数
TerminalSession::~TerminalSession() {
    // 离开备用屏后恢复termios
    leaveAltScreen();
    restoreTermios();
}

// 切换终端至原始输入模式
void TerminalSession::enterRaw() {
    termios raw = orig_;
    // 关闭行缓冲 关闭回显 关闭信号生成
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    // 关闭软件流控 关闭回车转换行
    raw.c_iflag &= ~(IXON | ICRNL);
    // 设置至少读到1字节时返回 无时间限制
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    // 清空IO缓冲区
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0)
        raw_entered_ = true;
}

// 切换至备用屏幕 清除屏幕 光标归位 隐藏光标
void TerminalSession::enterAltScreen() {
    const char seq[] = "\033[?1049h\033[2J\033[H\033[?25l";
    ::write(STDOUT_FILENO, seq, sizeof(seq)-1);
    alt_entered_ = true;
}

// 恢复原始终端属性
void TerminalSession::restoreTermios() {
    if(!raw_entered_) return;
    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_);
    raw_entered_ = false;
}

// 恢复光标显示 切换至主屏幕
void TerminalSession::leaveAltScreen() {
    if(!alt_entered_) return;
    const char seq[] = "\033[?25h\033[?1049l";
    ::write(STDOUT_FILENO, seq, sizeof(seq)-1);
    alt_entered_ = false;
}

// 根据窗口大小刷新尺寸
void TerminalSession::refreshSize() {
    winsize ws{};
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        rows_ = ws.ws_row;
        cols_ = ws.ws_col;
    } else {
        rows_ = 24;
        cols_ = 80;
    }
}

// 消费标志位函数
bool TerminalSession::consumeResizeFlag() {
    if(g_resized_) {
        g_resized_ = 0;
        return true;
    } return false;
}

// 信号处理函数
void TerminalSession::signalHandler(int sig) {
    // 设标志位后立即返回
    if(sig == SIGWINCH) {
        g_resized_ = 1;
        return;
    }
    // 恢复现场
    if(g_instance_) {
        if(g_instance_->alt_entered_) {
            const char restore[] = "\033[?25h\033[?1049l";
            ::write(STDOUT_FILENO, restore, sizeof(restore)-1);
        }
        if(g_instance_->raw_entered_) {
            ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_instance_->orig_);
        }
    }
    // 信号重抛
    ::signal(sig, SIG_DFL);
    ::raise(sig);
    // 手动退出
    ::_exit(128 + sig);
}

#endif // _WIN32
