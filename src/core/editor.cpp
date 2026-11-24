#include "editor.h"
#include <algorithm>
#include <fstream>

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

// 打开文件
void Editor::openFile(const std::string& path) {
    filepath_ = path;
    lines_.clear();
    if(path.empty()) {
        is_new_file_ = true;
        lines_.push_back("");
        return;
    }
    std::ifstream fin(path);
    if(!fin.is_open()) {
        is_new_file_ = true;
        lines_.push_back("");
        status_msg_ = "new file: " + path;
        return;
    }
    std::string line;
    while(std::getline(fin, line))
        lines_.push_back(line);
    if(lines_.empty())
        lines_.push_back("");
    is_new_file_ = false;
}

// 初始化
void Editor::initState() {
    cur_row_ = cur_col_ = top_line_ = left_col_ = 0;
    mode_ = Mode::Normal;
    dirty_ = false;
    running_ = true;
    undo_stack_.clear();
    yank_buffer_.clear();
    cmdline_.clear();
    pending_keys_.clear();
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

// 光标可见
void Editor::ensureCursorVisible(std::size_t row, std::size_t col) {
    (void)col;
    if (row < top_line_) top_line_ = row;
    if (row >= top_line_ + static_cast<std::size_t>(view_.rows()))
        top_line_ = row - view_.rows() + 1;
}

// 四路分发入口
void Editor::dispatch(const KeyEvent& key) {
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
                    pushUndo();
                    if(cur_col_ < lines_[cur_row_].size()) {
                        lines_[cur_row_].erase(cur_col_, 1);
                        dirty_ = true;
                    }
                    break;
                case 'p': paste(); break;
                case 'u': undo(); break;
                case 'v': mode_ = Mode::Visual; break;
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
        case Key::Enter: {
            pushUndo();
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
                pushUndo();
                lines_[cur_row_].erase(--cur_col_, 1);
                dirty_ = true;
            } else if(cur_row_ > 0) {
                pushUndo();
                cur_col_ = lines_[cur_row_-1].size();
                lines_[cur_row_-1] += lines_[cur_row_];
                lines_.erase(lines_.begin() + cur_row_);
                --cur_row_;
                dirty_ = true;
            }
            break;
        case Key::Char:
            pushUndo();
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

// 保存文件 成功返回 true
bool Editor::saveFile() {
    if(filepath_.empty()) {
        setStatus("no file name");
        return false;
    }
    std::ofstream fout(filepath_);
    if(!fout.is_open()) {
        setStatus("cannot write: " + filepath_);
        return false;
    }
    for(const auto& line : lines_)
        fout << line << '\n';
    fout.close();
    if(fout.fail()) {
        setStatus("write failed: " + filepath_);
        return false;
    }
    dirty_ = false;
    is_new_file_ = false;
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
    if(mode_ == Mode::Normal && max_col > 0)
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
        if(mode_ == Mode::Normal && maxc > 0)
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
    if(lines_.size() == 1) {
        lines_[0].clear();
        cur_col_ = 0;
        dirty_ = true;
        return;
    }
    lines_.erase(lines_.begin() + cur_row_);
    if(cur_row_ >= lines_.size())
        cur_row_ = lines_.size() - 1;
    cur_col_ = 0;
    dirty_ = true;
}

void Editor::yankLine() {
    yank_buffer_ = {lines_[cur_row_] };
}

void Editor::paste() {
    if(yank_buffer_.empty()) return;
    pushUndo();
    lines_.insert(lines_.begin() + cur_row_ + 1, yank_buffer_.begin(), yank_buffer_.end());
    ++cur_row_;
    cur_col_ = 0;
    dirty_ = true;
}


void Editor::pushUndo() {
    undo_stack_.push_back({lines_, cur_row_, cur_col_});
    if(undo_stack_.size() > 100)
        undo_stack_.erase(undo_stack_.begin());
}

void Editor::undo() {
    if(undo_stack_.empty()) return;
    Snapshot s = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    lines_      = std::move(s.lines);
    cur_row_    = std::min(s.row, lines_.size()-1);
    cur_col_    = std::min(s.col, lines_[cur_row_].size());
    dirty_      = true;
}

void Editor::setStatus(const std::string& msg) {
    status_msg_ = msg;
}