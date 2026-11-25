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
    // 搜索高亮（供渲染层）
    bool searchActive() const { return search_active_; }
    const std::string& searchTerm() const { return search_term_; }
    bool searchBackward() const { return search_backward_; }
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

    // 一个“动作”解析后的目标与性质
    struct Motion {
        bool        ok        = false;
        std::size_t row       = 0;
        std::size_t col       = 0;
        bool        linewise  = false;   // 整行动作(j/k/G/gg)
        bool        inclusive = false;   // 是否包含目标字符($/e/f/F/%)
    };

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
    void handleSearch(const KeyEvent& key);
    // 基础动作
    void moveCursor(int drow, int dcol);
    void enterInsert(Mode m);
    void deleteLine();
    void yankLine();
    void paste();
    void pushUndo();
    void undo();
    void redo();
    // 动作解析
    bool wordMotion(char m, Motion& out) const;       // w/b/e
    bool findMotion(char ft, char target, Motion& out) const;  // f/F/t/T
    bool bracketMotion(Motion& out) const;            // %
    // 操作符
    void applyOperator(char op, std::size_t sr, std::size_t sc, const Motion& m);
    void finishLineDelete(std::size_t lo, std::size_t hi);   // 行删除共享
    // 搜索
    void startSearch(bool backward);
    bool searchFromOffset(std::size_t startOff, bool backward,
                          const std::string& term, std::size_t& outOff) const;
    bool doSearchStep(bool backward);                 // n/N
    void incrementalSearch();                         // 输入时实时跳转
    // 扁平缓冲区工具（跨行字符级操作）
    std::string flatBuffer() const;
    std::size_t cursorOffset() const;
    std::size_t offsetOf(std::size_t r, std::size_t c) const;
    void setFromOffset(std::size_t off);
    void offsetToRowCol(std::size_t off, std::size_t& r, std::size_t& c) const;
    void rebuildFromFlat(const std::string& flat);
    // 替换区间：捕获到寄存器并删除 [lo,hiExclusive)
    void replaceFlatRange(std::size_t lo, std::size_t hiExclusive, bool addUndo);
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
    bool dirty_         = false;

    std::size_t cur_row_ = 0;
    std::size_t cur_col_ = 0;
    std::size_t top_line_ = 0;
    std::size_t left_col_ = 0;

    Mode mode_ = Mode::Normal;
    bool running_ = false;
    std::string status_msg_;
    std::string cmdline_;

    // Normal 模式多键命令状态
    char op_          = 0;    // 待定操作符 'd'/'y'/'c'
    char motion_wait_ = 0;    // 等待字符参数的动作 'f'/'F'/'t'/'T'/'g'

    // 搜索状态
    std::string search_term_;
    bool        search_active_ = false;
    bool        search_backward_ = false;
    std::size_t search_origin_off_ = 0;   // 进入搜索前光标偏移（取消时回退）
    char        search_op_ = 0;           // 若由操作符触发则记录 'd'/'y'/'c'

    // 选区锚点
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
