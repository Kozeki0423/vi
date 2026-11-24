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
    std::size_t leftCol() const { return left_col_; }
    int textRows() const { return view_.rows(); }
    int textCols() const { return view_.cols(); }
    Mode mode() const { return mode_; }
    const std::string& statusMsg() const { return status_msg_; }
    const std::string& cmdline() const { return cmdline_; }
    const std::string& filePath() const { return filepath_; }
    bool dirty() const { return dirty_; }
    std::size_t lineCount() const { return lines_.size(); }
    // 行号槽宽度（数字位数 + 1 个空格）
    int gutterWidth() const {
        std::size_t d = 1, n = lines_.size();
        while(n >= 10) { n /= 10; ++d; }
        return static_cast<int>(d) + 1;
    }
    // 文本区可用列数（总宽减去行号槽）
    int textAreaCols() const {
        int c = view_.cols() - gutterWidth();
        return c > 0 ? c : 1;
    }
    // 可视选区（仅在 Visual 模式下有意义）
    std::size_t selStartRow() const;
    std::size_t selStartCol() const;
    std::size_t selEndRow() const;
    std::size_t selEndCol() const;
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
    void redo();
    // 选区动作
    void normalizeSelection(std::size_t& sr, std::size_t& sc,
                            std::size_t& er, std::size_t& ec) const;
    void deleteSelection();
    void yankSelection();
    // 工具
    void clampCursor();
    void setStatus(const std::string& msg);
    void executeCommand(const std::string& cmd);

    std::unique_ptr<TerminalSession> session_;
    std::vector<std::string> lines_;
    std::string filepath_;
    std::string pending_keys_;
    bool dirty_         = false;

    std::size_t cur_row_ = 0;
    std::size_t cur_col_ = 0;
    std::size_t top_line_ = 0;
    std::size_t left_col_ = 0;

    Mode mode_ = Mode::Normal;
    bool running_ = false;
    std::string status_msg_;
    std::string cmdline_;

    // 选区锚点（进入 Visual 时记录）
    std::size_t sel_row_ = 0;
    std::size_t sel_col_ = 0;

    // 寄存器
    std::vector<std::string>    yank_buffer_;
    bool                        yank_linewise_ = true;   // true=整行, false=字符

    std::vector<Snapshot>       undo_stack_;
    std::vector<Snapshot>       redo_stack_;

    // 换行风格与尾换行（读写保真）
    std::string eol_ = "\n";
    bool ends_with_newline_ = true;

    View        view_;
    Renderer    renderer_;
};
