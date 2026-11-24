#include "editor.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

// 启动流程
void Editor::bootstrap(const std::string& path) {
    // 委托 TerminalSession
    session_ = std::make_unique<TerminalSession>();
    openFile(path);
    initState();

    int text_rows = std::max(1, session_->rows()-2);
    view_.resize(text_rows, session_->cols());

    renderer_.draw(*this);
}

// 打开文件（保留 CRLF/尾换行信息）
void Editor::openFile(const std::string& path) {
    filepath_ = path;
    lines_.clear();
    eol_ = "\n";
    ends_with_newline_ = true;
    if(path.empty()) {
        lines_.push_back("");
        return;
    }
    std::ifstream fin(path, std::ios::binary);
    if(!fin.is_open()) {
        lines_.push_back("");
        status_msg_ = "new file: " + path;
        return;
    }
    std::string content((std::istreambuf_iterator<char>(fin)),
                        std::istreambuf_iterator<char>());
    fin.close();

    // 检测换行风格
    eol_ = (content.find("\r\n") != std::string::npos) ? "\r\n" : "\n";

    if(content.empty()) {
        lines_.push_back("");
        ends_with_newline_ = false;   // 空文件保持为空
        return;
    }

    // 按 '\n' 切分，去掉行尾 '\r'
    std::string cur;
    for(char c : content) {
        if(c == '\n') {
            if(!cur.empty() && cur.back() == '\r') cur.pop_back();
            lines_.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if(content.back() == '\n') {
        ends_with_newline_ = true;    // 末尾换行已由上一行覆盖
    } else {
        if(!cur.empty() && cur.back() == '\r') cur.pop_back();
        lines_.push_back(cur);
        ends_with_newline_ = false;
    }
    if(lines_.empty()) lines_.push_back("");
}

// 初始化
void Editor::initState() {
    cur_row_ = cur_col_ = top_line_ = left_col_ = 0;
    sel_row_ = sel_col_ = 0;
    mode_ = Mode::Normal;
    dirty_ = false;
    running_ = true;
    undo_stack_.clear();
    redo_stack_.clear();
    yank_buffer_.clear();
    yank_linewise_ = true;
    cmdline_.clear();
    pending_keys_.clear();
    status_msg_.clear();
}

// 循环逻辑
void Editor::run() {
    InputReader input;
    while(running_) {
        if(session_->consumeResizeFlag()) {
            session_->refreshSize();
            int text_rows = std::max(1, session_->rows()-2);
            view_.resize(text_rows, session_->cols());
            top_line_ = std::min(top_line_, lines_.size()-1);
            renderer_.draw(*this);
        }
        KeyEvent key = input.readKey();
        dispatch(key);
        ensureCursorVisible(cur_row_, cur_col_);
        renderer_.draw(*this);
    }
}

// 光标可见（纵向与横向滚动）
void Editor::ensureCursorVisible(std::size_t row, std::size_t col) {
    if(row < top_line_) top_line_ = row;
    if(row >= top_line_ + static_cast<std::size_t>(view_.rows()))
        top_line_ = row - view_.rows() + 1;

    int cols = view_.cols();
    if(cols <= 0) { left_col_ = 0; return; }
    if(col < left_col_) left_col_ = col;
    if(col >= left_col_ + static_cast<std::size_t>(cols))
        left_col_ = col - cols + 1;
}

// 四路分发入口
void Editor::dispatch(const KeyEvent& key) {
    status_msg_.clear();   // 新按键清除上一条状态消息
    switch(mode_) {
        case Mode::Normal: handleNormal(key); break;
        case Mode::Insert:  handleInsert(key);  break;
        case Mode::Command: handleCommand(key); break;
        case Mode::Visual:  handleVisual(key);  break;
    }
    clampCursor();
}

// Normal 模式
void Editor::handleNormal(const KeyEvent& key) {
    // 解析双键命令
    if(!pending_keys_.empty()) {
        char second = (key.type == Key::Char) ? key.ch : '\0';
        pending_keys_.push_back(second);

        if(pending_keys_ == "dd") {
            pushUndo();
            deleteLine();
            pending_keys_.clear();
            return;
        }
        if(pending_keys_ == "yy") {
            yankLine();
            pending_keys_.clear();
            return;
        }
        if(pending_keys_ == "gg") {
            cur_row_ = 0;
            cur_col_ = 0;
            pending_keys_.clear();
            return;
        }
        pending_keys_.clear();
        return;
    }
    // 解析单键命令
    switch(key.type) {
        case Key::Char:
            switch(key.ch) {
                case 'h': moveCursor(0, -1); break;
                case 'j': moveCursor(1, 0); break;
                case 'k': moveCursor(-1, 0); break;
                case 'l': moveCursor(0, 1); break;
                case '0': cur_col_ = 0; break;
                case '$': cur_col_ = lines_[cur_row_].size(); break;
                case 'G': cur_row_ = lines_.size()-1; cur_col_ = 0; break;
                case 'i': enterInsert(Mode::Insert); break;
                case 'a':
                    if(cur_col_ < lines_[cur_row_].size())
                        ++cur_col_;
                    enterInsert(Mode::Insert);
                    break;
                case 'o':
                    pushUndo();
                    lines_.insert(lines_.begin() + cur_row_ + 1 , "");
                    ++cur_row_;
                    cur_col_ = 0;
                    mode_ = Mode::Insert;
                    dirty_ = true;
                    break;
                case 'x':
                    if(cur_col_ < lines_[cur_row_].size()) {
                        pushUndo();
                        yank_buffer_ = { lines_[cur_row_].substr(cur_col_, 1) };
                        yank_linewise_ = false;
                        lines_[cur_row_].erase(cur_col_, 1);
                        dirty_ = true;
                    }
                    break;
                case 'p': paste(); break;
                case 'u': undo(); break;
                case 'v':
                    sel_row_ = cur_row_;
                    sel_col_ = cur_col_;
                    mode_ = Mode::Visual;
                    break;
                case 'd': pending_keys_ = "d"; break;
                case 'y': pending_keys_ = "y"; break;
                case 'g': pending_keys_ = "g"; break;
                case ':':
                    mode_ = Mode::Command;
                    cmdline_.clear();
                    break;
                default: break;
            }
            break;
        case Key::Up:       moveCursor(-1, 0); break;
        case Key::Down:     moveCursor(1, 0); break;
        case Key::Left:     moveCursor(0, -1); break;
        case Key::Right:    moveCursor(0, 1); break;
        case Key::Home:     cur_col_ = 0; break;
        case Key::End:      cur_col_ = lines_[cur_row_].size(); break;
        case Key::PageUp:   moveCursor(-view_.rows(), 0); break;
        case Key::PageDown: moveCursor( view_.rows(), 0); break;
        case Key::CtrlR:    redo(); break;
        case Key::Esc:      pending_keys_.clear(); break;
        default: break;
    }
}

// Insert 模式
void Editor::handleInsert(const KeyEvent& key) {
    switch(key.type) {
        case Key::Esc:
            mode_ = Mode::Normal;
            if(cur_col_ > 0 && cur_col_ >= lines_[cur_row_].size())
                cur_col_ = lines_[cur_row_].empty() ? 0 : lines_[cur_row_].size()-1;
            break;
        case Key::CtrlR:
            mode_ = Mode::Normal;
            redo();
            break;
        case Key::Enter: {
            std::string& line = lines_[cur_row_];
            std::string tail = line.substr(cur_col_);
            line.erase(cur_col_);
            lines_.insert(lines_.begin() + cur_row_ + 1, tail);
            ++cur_row_;
            cur_col_ = 0;
            dirty_ = true;
            break;
        }
        case Key::Backspace:
            if(cur_col_ > 0) {
                lines_[cur_row_].erase(--cur_col_, 1);
                dirty_ = true;
            } else if(cur_row_ > 0) {
                cur_col_ = lines_[cur_row_-1].size();
                lines_[cur_row_-1] += lines_[cur_row_];
                lines_.erase(lines_.begin() + cur_row_);
                --cur_row_;
                dirty_ = true;
            }
            break;
        case Key::Tab: {
            const int stops = 4;
            int next = (static_cast<int>(cur_col_) / stops + 1) * stops;
            int n = next - static_cast<int>(cur_col_);
            lines_[cur_row_].insert(cur_col_, static_cast<std::size_t>(n), ' ');
            cur_col_ += static_cast<std::size_t>(n);
            dirty_ = true;
            break;
        }
        case Key::Char:
            lines_[cur_row_].insert(cur_col_, 1, key.ch);
            ++cur_col_;
            dirty_ = true;
            break;
        case Key::Left:  if (cur_col_ > 0) cur_col_--; break;
        case Key::Right: if (cur_col_ < lines_[cur_row_].size()) cur_col_++; break;
        case Key::Up:    moveCursor(-1, 0); break;
        case Key::Down:  moveCursor(1, 0);  break;
        default: break;
    }
}

// Command 模式
void Editor::handleCommand(const KeyEvent& key) {
    switch(key.type) {
        case Key::Esc:
            mode_ = Mode::Normal;
            cmdline_.clear();
            break;
        case Key::Enter:
            executeCommand(cmdline_);
            cmdline_.clear();
            mode_ = Mode::Normal;
            break;
        case Key::Backspace:
            if(!cmdline_.empty())
                cmdline_.pop_back();
            else mode_ = Mode::Normal;
            break;
        case Key::Char:
            cmdline_.push_back(key.ch);
            break;
        default: break;
    }
}

// 保存文件（按原换行风格与尾换行）成功返回 true
bool Editor::saveFile() {
    if(filepath_.empty()) {
        setStatus("no file name");
        return false;
    }
    std::ofstream fout(filepath_, std::ios::binary | std::ios::trunc);
    if(!fout.is_open()) {
        setStatus("cannot write: " + filepath_);
        return false;
    }
    for(std::size_t i = 0; i < lines_.size(); ++i) {
        fout << lines_[i];
        bool last = (i + 1 == lines_.size());
        if(!last || ends_with_newline_)
            fout << eol_;
    }
    fout.close();
    if(fout.fail()) {
        setStatus("write failed: " + filepath_);
        return false;
    }
    dirty_ = false;
    setStatus("written: " + filepath_);
    return true;
}

// 处理命令
void Editor::executeCommand(const std::string& cmd) {
    if(cmd == "w") {
        saveFile();
    } else if(cmd == "q") {
        if(dirty_)
            setStatus("unsaved changes (use :q!)");
        else running_ = false;
    } else if(cmd == "wq") {
        if(saveFile())
            running_ = false;
    } else if(cmd == "q!") {
        running_ = false;
    } else {
        setStatus("unknown command: " + cmd);
    }
}

// Visual 模式
void Editor::handleVisual(const KeyEvent& key) {
    if(key.type == Key::Esc || (key.type == Key::Char && key.ch == 'v')) {
        mode_ = Mode::Normal;
        return;
    }
    if(key.type == Key::Char) {
        switch(key.ch) {
            case 'h': moveCursor(0, -1); break;
            case 'j': moveCursor(1, 0);  break;
            case 'k': moveCursor(-1, 0); break;
            case 'l': moveCursor(0, 1);  break;
            case '0': cur_col_ = 0; break;
            case '$': cur_col_ = lines_[cur_row_].size(); break;
            case 'd':
            case 'x': deleteSelection(); break;
            case 'y': {
                yankSelection();
                std::size_t sr, sc, er, ec;
                normalizeSelection(sr, sc, er, ec);
                cur_row_ = sr; cur_col_ = sc;
                mode_ = Mode::Normal;
                break;
            }
            case 'c':
                deleteSelection();       // 删除并进入插入
                mode_ = Mode::Insert;
                break;
            default: break;
        }
    } else if (key.type == Key::Up)    moveCursor(-1, 0);
    else if (key.type == Key::Down)    moveCursor(1, 0);
    else if (key.type == Key::Left)    moveCursor(0, -1);
    else if (key.type == Key::Right)   moveCursor(0, 1);
}

// 辅助函数
void Editor::clampCursor() {
    if(lines_.empty())
        lines_.push_back("");
    if(cur_row_ >= lines_.size())
        cur_row_ = lines_.size() - 1;
    // 光标钳制
    std::size_t max_col = lines_[cur_row_].size();
    if((mode_ == Mode::Normal || mode_ == Mode::Visual) && max_col > 0)
        --max_col;
    if(cur_col_ > max_col)
        cur_col_ = max_col;
}

void Editor::moveCursor(int drow, int dcol) {
    if(drow > 0) {
        if(cur_row_ + static_cast<std::size_t>(drow) < lines_.size())
            cur_row_ += drow;
    } else if (drow < 0) {
        std::size_t step = static_cast<std::size_t>(-drow);
        cur_row_ = (step > cur_row_) ? 0 : cur_row_ - step;
    }
    if(dcol > 0) {
        std::size_t maxc = lines_[cur_row_].size();
        if((mode_ == Mode::Normal || mode_ == Mode::Visual) && maxc > 0)
            --maxc;
        cur_col_ = std::min(cur_col_ + static_cast<std::size_t>(dcol), maxc);
    } else if (dcol < 0) {
        std::size_t step = static_cast<std::size_t>(-dcol);
        cur_col_ = (step > cur_col_) ? 0 : cur_col_ - step;
    }
}

void Editor::enterInsert(Mode m) {
    pushUndo();
    mode_ = m;
}

void Editor::deleteLine() {
    // 删除整行时同时写入寄存器（行内文本）
    if(lines_.size() == 1) {
        yank_buffer_ = { lines_[0] };
        yank_linewise_ = true;
        lines_[0].clear();
        cur_col_ = 0;
        dirty_ = true;
        return;
    }
    yank_buffer_ = { lines_[cur_row_] };
    yank_linewise_ = true;
    lines_.erase(lines_.begin() + cur_row_);
    if(cur_row_ >= lines_.size())
        cur_row_ = lines_.size() - 1;
    cur_col_ = 0;
    dirty_ = true;
}

void Editor::yankLine() {
    yank_buffer_ = { lines_[cur_row_] };
    yank_linewise_ = true;
}

void Editor::paste() {
    if(yank_buffer_.empty()) return;
    pushUndo();
    if(yank_linewise_) {
        // 整行：插入到当前行下方
        lines_.insert(lines_.begin() + cur_row_ + 1, yank_buffer_.begin(), yank_buffer_.end());
        ++cur_row_;
        cur_col_ = 0;
    } else {
        // 字符块：把寄存器各行用 '\n' 连接后插入光标处
        std::string block;
        for(std::size_t i = 0; i < yank_buffer_.size(); ++i) {
            if(i) block.push_back('\n');
            block += yank_buffer_[i];
        }
        std::string& line = lines_[cur_row_];
        std::string head = line.substr(0, cur_col_);
        std::string tail = line.substr(cur_col_);
        // 把 block 中的 '\n' 展开为新行
        std::vector<std::string> pieces;
        std::string acc;
        for(char c : block) {
            if(c == '\n') { pieces.push_back(acc); acc.clear(); }
            else acc.push_back(c);
        }
        pieces.push_back(acc);
        std::vector<std::string> mids(pieces.begin() + 1, pieces.end());
        if(!mids.empty()) {
            lines_[cur_row_] = head + pieces.front();
            mids.back() += tail;
            lines_.insert(lines_.begin() + cur_row_ + 1, mids.begin(), mids.end());
            cur_row_ += mids.size();
            cur_col_ = mids.back().size() - tail.size();
        } else {
            // 单行字符块：插入到 head 与 tail 之间
            lines_[cur_row_] = head + pieces.front() + tail;
            cur_col_ = head.size() + pieces.front().size();
        }
    }
    dirty_ = true;
}

void Editor::pushUndo() {
    undo_stack_.push_back({lines_, cur_row_, cur_col_});
    if(undo_stack_.size() > 100)
        undo_stack_.erase(undo_stack_.begin());
    redo_stack_.clear();   // 新动作使重做栈失效
}

void Editor::undo() {
    if(undo_stack_.empty()) return;
    redo_stack_.push_back({lines_, cur_row_, cur_col_});
    Snapshot s = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    lines_      = std::move(s.lines);
    cur_row_    = std::min(s.row, lines_.size()-1);
    cur_col_    = std::min(s.col, lines_[cur_row_].size());
    dirty_      = true;
}

void Editor::redo() {
    if(redo_stack_.empty()) return;
    undo_stack_.push_back({lines_, cur_row_, cur_col_});
    Snapshot s = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    lines_      = std::move(s.lines);
    cur_row_    = std::min(s.row, lines_.size()-1);
    cur_col_    = std::min(s.col, lines_[cur_row_].size());
    dirty_      = true;
}

// 选区：把锚点与光标排序，返回包含两端的字符范围
void Editor::normalizeSelection(std::size_t& sr, std::size_t& sc,
                                std::size_t& er, std::size_t& ec) const {
    sr = sel_row_; sc = sel_col_;
    er = cur_row_; ec = cur_col_;
    if(sr > er || (sr == er && sc > ec)) {
        std::swap(sr, er);
        std::swap(sc, ec);
    }
}

void Editor::deleteSelection() {
    std::size_t sr, sc, er, ec;
    normalizeSelection(sr, sc, er, ec);

    // 捕获选中文本到寄存器（字符块）
    std::vector<std::string> buf;
    if(sr == er) {
        buf.push_back(lines_[sr].substr(sc, ec - sc + 1));
    } else {
        buf.push_back(lines_[sr].substr(sc));
        for(std::size_t r = sr + 1; r < er; ++r) buf.push_back(lines_[r]);
        buf.push_back(lines_[er].substr(0, ec + 1));
    }
    yank_buffer_ = std::move(buf);
    yank_linewise_ = false;

    pushUndo();
    if(sr == er) {
        lines_[sr].erase(sc, ec - sc + 1);
    } else {
        std::string head = lines_[sr].substr(0, sc);
        std::string tail = lines_[er].substr(std::min(ec + 1, lines_[er].size()));
        lines_[sr] = head + tail;
        lines_.erase(lines_.begin() + sr + 1, lines_.begin() + er + 1);
    }
    cur_row_ = sr;
    cur_col_ = sc;
    mode_ = Mode::Normal;
    dirty_ = true;
}

void Editor::yankSelection() {
    std::size_t sr, sc, er, ec;
    normalizeSelection(sr, sc, er, ec);
    std::vector<std::string> buf;
    if(sr == er) {
        buf.push_back(lines_[sr].substr(sc, ec - sc + 1));
    } else {
        buf.push_back(lines_[sr].substr(sc));
        for(std::size_t r = sr + 1; r < er; ++r) buf.push_back(lines_[r]);
        buf.push_back(lines_[er].substr(0, ec + 1));
    }
    yank_buffer_ = std::move(buf);
    yank_linewise_ = false;
}

// 选区访问器（供渲染层使用，返回排序后的坐标）
std::size_t Editor::selStartRow() const {
    std::size_t sr, sc, er, ec; normalizeSelection(sr, sc, er, ec); return sr;
}
std::size_t Editor::selStartCol() const {
    std::size_t sr, sc, er, ec; normalizeSelection(sr, sc, er, ec); return sc;
}
std::size_t Editor::selEndRow() const {
    std::size_t sr, sc, er, ec; normalizeSelection(sr, sc, er, ec); return er;
}
std::size_t Editor::selEndCol() const {
    std::size_t sr, sc, er, ec; normalizeSelection(sr, sc, er, ec); return ec;
}

void Editor::setStatus(const std::string& msg) {
    status_msg_ = msg;
}
