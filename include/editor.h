#pragma once
#include <string>
#include <vector>
#include <cstddef>
#include <memory>

#include "terminal_session.h"
#include "view.h"
#include "mode.h"
#include "renderer.h"
#include "input_reader.h"

class Editor {
public:
    Editor() = default;
    void bootstrap(const std::string& path);
    void run();
    // 偏移校正函数
    void ensureCursorVisible(std::size_t row, std::size_t col);
    // 外部访问函数
    const std::vector<std::string>& lines() const { return lines_; }
    std::size_t curRow() const { return cur_row_; }
    std::size_t curCol() const { return cur_col_; }
    std::size_t topLine() const { return top_line_; }
    int textRows() const { return view_.rows(); }
    Mode mode() const { return mode_; }
    const std::string& statusMsg() const { return status_msg_; }
    const std::string& cmdline() const { return cmdline_; }
private:
    void openFile(const std::string& path);
    bool saveFile();
    void initState();
    // 分发与模式处理
    void dispatch(const KeyEvent& key);
    void handleNormal(const KeyEvent& key);
    void handleInsert(const KeyEvent& key);
    void handleCommand(const KeyEvent& key);
    void handleVisual(const KeyEvent& key);
    // Normal 动作
    void moveCursor(int drow, int dcol);
    void enterInsert(Mode m);
    void deleteLine();
    void yankLine();
    void paste();
    void pushUndo();
    void undo();
    // 工具
    void clampCursor();
    void setStatus(const std::string& msg);
    void executeCommand(const std::string& cmd);

    std::unique_ptr<TerminalSession> session_;
    std::vector<std::string> lines_;
    std::string filepath_;
    std::string pending_keys_;
    bool is_new_file_   = false;
    bool dirty_         = false;

    size_t cur_row_ = 0;
    size_t cur_col_ = 0;
    size_t top_line_ = 0;
    size_t left_col_ = 0;

    Mode mode_ = Mode::Normal;
    bool running_ = false;
    std::string status_msg_;
    std::string cmdline_;

    std::vector<std::string>    yank_buffer_;
    std::vector<Snapshot>       undo_stack_;

    View        view_;
    Renderer    renderer_;
};