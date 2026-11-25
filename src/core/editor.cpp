#include "editor.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <utility>

// ============================ 生命周期 ============================

void Editor::bootstrap(const std::string& path) {
    session_ = std::make_unique<TerminalSession>();
    openFile(path);
    initState();

    int text_rows = std::max(1, session_->rows()-2);
    view_.resize(text_rows, session_->cols());

    renderer_.draw(*this);
}

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

    eol_ = (content.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
    if(content.empty()) {
        lines_.push_back("");
        ends_with_newline_ = false;
        return;
    }
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
        ends_with_newline_ = true;
    } else {
        if(!cur.empty() && cur.back() == '\r') cur.pop_back();
        lines_.push_back(cur);
        ends_with_newline_ = false;
    }
    if(lines_.empty()) lines_.push_back("");
}

void Editor::initState() {
    cur_row_ = cur_col_ = top_line_ = left_col_ = 0;
    sel_row_ = sel_col_ = 0;
    mode_ = Mode::Normal;
    dirty_ = false;
    running_ = true;
    op_ = 0;
    motion_wait_ = 0;
    undo_stack_.clear();
    redo_stack_.clear();
    yank_buffer_.clear();
    yank_linewise_ = true;
    cmdline_.clear();
    status_msg_.clear();
    search_term_.clear();
    search_active_ = false;
    search_backward_ = false;
    search_origin_off_ = 0;
    search_op_ = 0;
}

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

// ============================ 光标可见 ============================

void Editor::ensureCursorVisible(std::size_t row, std::size_t col) {
    if(row < top_line_) top_line_ = row;
    if(row >= top_line_ + static_cast<std::size_t>(view_.rows()))
        top_line_ = row - view_.rows() + 1;

    int cols = textAreaCols();
    if(cols <= 0) { left_col_ = 0; return; }
    if(col < left_col_) left_col_ = col;
    if(col >= left_col_ + static_cast<std::size_t>(cols))
        left_col_ = col - static_cast<std::size_t>(cols) + 1;
}

// ============================ 分发 ============================

void Editor::dispatch(const KeyEvent& key) {
    status_msg_.clear();
    switch(mode_) {
        case Mode::Normal:  handleNormal(key);  break;
        case Mode::Insert:  handleInsert(key);  break;
        case Mode::Command: handleCommand(key); break;
        case Mode::Visual:  handleVisual(key);  break;
        case Mode::Search:  handleSearch(key);  break;
    }
    clampCursor();
}

// ============================ 扁平缓冲工具 ============================

std::string Editor::flatBuffer() const {
    std::string flat;
    for(std::size_t i = 0; i < lines_.size(); ++i) {
        if(i) flat.push_back('\n');
        flat += lines_[i];
    }
    return flat;
}

std::size_t Editor::offsetOf(std::size_t r, std::size_t c) const {
    std::size_t off = 0;
    for(std::size_t i = 0; i < r && i < lines_.size(); ++i)
        off += lines_[i].size() + 1;   // +1 for '\n'
    off += c;
    return off;
}

std::size_t Editor::cursorOffset() const {
    return offsetOf(cur_row_, cur_col_);
}

void Editor::offsetToRowCol(std::size_t off, std::size_t& r, std::size_t& c) const {
    r = 0; c = 0;
    for(std::size_t i = 0; i < lines_.size(); ++i) {
        std::size_t len = lines_[i].size();
        if(off <= len) { r = i; c = off; return; }
        off -= len + 1;
    }
    r = lines_.empty() ? 0 : lines_.size() - 1;
    c = lines_.empty() ? 0 : lines_.back().size();
}

void Editor::setFromOffset(std::size_t off) {
    offsetToRowCol(off, cur_row_, cur_col_);
}

void Editor::rebuildFromFlat(const std::string& flat) {
    lines_.clear();
    std::string cur;
    for(char ch : flat) {
        if(ch == '\n') { lines_.push_back(cur); cur.clear(); }
        else cur.push_back(ch);
    }
    lines_.push_back(cur);
}

// 捕获 [lo, hiExclusive) 到寄存器（字符块）并删除；不自动 pushUndo
void Editor::replaceFlatRange(std::size_t lo, std::size_t hiExclusive, bool addUndo) {
    if(addUndo) pushUndo();
    std::string flat = flatBuffer();
    if(lo > flat.size()) lo = flat.size();
    if(hiExclusive > flat.size()) hiExclusive = flat.size();
    if(hiExclusive < lo) hiExclusive = lo;

    std::string captured = flat.substr(lo, hiExclusive - lo);
    // 寄存器：按行拆分
    std::vector<std::string> buf;
    std::string acc;
    for(char ch : captured) {
        if(ch == '\n') { buf.push_back(acc); acc.clear(); }
        else acc.push_back(ch);
    }
    buf.push_back(acc);
    yank_buffer_ = std::move(buf);
    yank_linewise_ = false;

    flat.erase(lo, hiExclusive - lo);
    rebuildFromFlat(flat);
    setFromOffset(lo);
    dirty_ = true;
}

// ============================ Normal 模式 ============================

// 解析一个“动作”，把结果写入 out（相对当前光标）
bool Editor::wordMotion(char m, Motion& out) const {
    const std::string& line = lines_[cur_row_];
    std::size_t len = line.size();
    auto isword = [](unsigned char c){ return std::isalnum(c) || c == '_'; };

    out.ok = true;
    out.linewise = false;
    out.inclusive = false;

    if(m == 'e') {
        std::size_t i = cur_col_;
        if(i >= len) { out.ok = false; return false; }
        // 跳过当前词
        if(isword((unsigned char)line[i])) { while(i < len && isword((unsigned char)line[i])) ++i; }
        else { while(i < len && !isword((unsigned char)line[i]) && line[i] != ' ') ++i; }
        // 走到下一个词尾
        while(i < len && line[i] == ' ') ++i;
        if(i >= len) { out.row = cur_row_; out.col = len ? len-1 : 0; return true; }
        // i 是某个词/标点的起点，移到其结尾
        while(i + 1 < len && isword((unsigned char)line[i+1]) == isword((unsigned char)line[i]) && line[i+1] != ' ')
            ++i;
        out.row = cur_row_; out.col = i; out.inclusive = true;
        return true;
    }

    // w / b : 跨行遍历
    std::size_t r = cur_row_, c = cur_col_;
    if(m == 'w') {
        if(c < len) {
            if(isword((unsigned char)line[c])) { while(c < len && isword((unsigned char)line[c])) ++c; }
            else if(line[c] != ' ') { while(c < len && !isword((unsigned char)line[c]) && line[c] != ' ') ++c; }
            while(c < len && line[c] == ' ') ++c;
        }
        if(c >= len) {
            if(r + 1 < lines_.size()) { r += 1; c = 0;
                while(c < lines_[r].size() && lines_[r][c] == ' ') ++c;
            } else { out.ok = false; return false; }
        }
        out.row = r; out.col = c; return true;
    }

    // b 后退到上一个词首
    if(c == 0) {
        if(r == 0) { out.ok = false; return false; }
        --r;
        c = lines_[r].empty() ? 0 : lines_[r].size() - 1;
    } else --c;
    const std::string& L = lines_[r];
    while(c > 0 && L[c] == ' ') --c;
    if(isword((unsigned char)L[c])) { while(c > 0 && isword((unsigned char)L[c-1])) --c; }
    else { while(c > 0 && !isword((unsigned char)L[c-1]) && L[c-1] != ' ') --c; }
    out.row = r; out.col = c; return true;
}

bool Editor::findMotion(char ft, char target, Motion& out) const {
    const std::string& line = lines_[cur_row_];
    out.ok = false; out.linewise = false; out.inclusive = true;
    if(ft == 'f' || ft == 't') {
        for(std::size_t i = cur_col_ + 1; i < line.size(); ++i) {
            if(line[i] == target) {
                out.ok = true;
                out.row = cur_row_;
                out.col = (ft == 't') ? i - 1 : i;
                return true;
            }
        }
    } else { // F / T
        if(cur_col_ == 0) return false;
        for(std::size_t i = cur_col_; i-- > 0; ) {
            if(line[i] == target) {
                out.ok = true;
                out.row = cur_row_;
                out.col = (ft == 'T') ? i + 1 : i;
                out.inclusive = (ft == 'F');
                return true;
            }
        }
    }
    return false;
}

bool Editor::bracketMotion(Motion& out) const {
    const std::string& line = lines_[cur_row_];
    out.ok = false; out.linewise = false; out.inclusive = true;
    if(cur_col_ >= line.size()) return false;
    char c = line[cur_col_];
    char open, close;
    int dir;
    if(c == '(' || c == '[' || c == '{') { open = c;  close = (c=='(' ? ')' : c=='[' ? ']' : '}'); dir = 1;  }
    else if(c == ')' || c == ']' || c == '}') { close = c; open = (c==')' ? '(' : c==']' ? '[' : '{'); dir = -1; }
    else return false;

    std::string flat = flatBuffer();
    std::size_t off = offsetOf(cur_row_, cur_col_);
    int depth = 0;
    std::size_t i = off;
    while(true) {
        char ch = flat[i];
        if(dir > 0) {
            // 前向：以 open 计深、close 减深
            if(ch == open)  ++depth;
            else if(ch == close) { --depth; }
        } else {
            // 后向：以 close 计深、open 减深
            if(ch == close) ++depth;
            else if(ch == open) { --depth; }
        }
        if(depth == 0) {
            std::size_t r, col; offsetToRowCol(i, r, col);
            out.ok = true; out.row = r; out.col = col;
            return true;
        }
        if(dir > 0) { if(i + 1 >= flat.size()) break; ++i; }
        else        { if(i == 0) break; --i; }
    }
    return false;
}

void Editor::finishLineDelete(std::size_t lo, std::size_t hi) {
    // 删除整行 [lo,hi]，写入行寄存器
    std::vector<std::string> buf;
    for(std::size_t i = lo; i <= hi && i < lines_.size(); ++i) buf.push_back(lines_[i]);
    yank_buffer_ = std::move(buf);
    yank_linewise_ = true;

    if(lines_.size() <= (hi - lo + 1)) {
        // 全部删除 → 留一个空行
        lines_ = { "" };
        cur_row_ = 0; cur_col_ = 0;
    } else {
        lines_.erase(lines_.begin() + lo, lines_.begin() + hi + 1);
        cur_row_ = std::min(lo, lines_.size()-1);
        cur_col_ = 0;
    }
    dirty_ = true;
}

// 应用操作符 op 到动作 m；调用前已 pushUndo
void Editor::applyOperator(char op, std::size_t sr, std::size_t sc, const Motion& m) {
    if(m.linewise) {
        std::size_t lo = std::min(sr, m.row), hi = std::max(sr, m.row);
        if(op == 'y') {
            std::vector<std::string> buf;
            for(std::size_t i = lo; i <= hi; ++i) buf.push_back(lines_[i]);
            yank_buffer_ = std::move(buf);
            yank_linewise_ = true;
            cur_row_ = lo; cur_col_ = 0;
            return;
        }
        if(op == 'd') { finishLineDelete(lo, hi); return; }
        if(op == 'c') {
            std::vector<std::string> buf;
            for(std::size_t i = lo; i <= hi; ++i) buf.push_back(lines_[i]);
            // 行删除后进入插入
            if(lines_.size() <= (hi - lo + 1)) { lines_ = { "" }; cur_row_ = 0; }
            else { lines_.erase(lines_.begin()+lo, lines_.begin()+hi+1); cur_row_ = std::min(lo, lines_.size()-1); }
            yank_buffer_ = std::move(buf); yank_linewise_ = true;
            cur_col_ = 0; mode_ = Mode::Insert; dirty_ = true;
            return;
        }
    }

    // 字符级：先算 [lo, hiExclusive)
    std::size_t startOff = offsetOf(sr, sc);
    std::size_t endOff   = offsetOf(m.row, m.col);
    std::size_t lo, hiEx;
    if(startOff <= endOff) {
        // 前向：独占不含目标字符，含入则 +1
        lo = startOff;
        hiEx = endOff + (m.inclusive ? 1 : 0);
    } else {
        // 后向：独占不含光标字符，含入则 +1
        lo = endOff;
        hiEx = startOff + (m.inclusive ? 1 : 0);
    }

    if(op == 'y') {
        std::string flat = flatBuffer();
        if(hiEx > flat.size()) hiEx = flat.size();
        std::string captured = flat.substr(lo, hiEx - lo);
        std::vector<std::string> buf; std::string acc;
        for(char ch : captured) { if(ch=='\n'){buf.push_back(acc);acc.clear();} else acc.push_back(ch); }
        buf.push_back(acc);
        yank_buffer_ = std::move(buf); yank_linewise_ = false;
        setFromOffset(lo);
        return;
    }
    if(op == 'd') {
        replaceFlatRange(lo, hiEx, /*addUndo=*/false);
        return;
    }
    if(op == 'c') {
        replaceFlatRange(lo, hiEx, /*addUndo=*/false);
        mode_ = Mode::Insert;
        return;
    }
}

void Editor::handleNormal(const KeyEvent& key) {
    // ---- 1) 等待动作的字符参数（f/F/t/T/g 之后）----
    if(motion_wait_) {
        char w = motion_wait_;
        motion_wait_ = 0;
        if(key.type != Key::Char) { op_ = 0; return; }

        if(w == 'g') {
            if(key.ch == 'g') {
                Motion m; m.ok = true; m.row = 0; m.col = 0; m.linewise = true;
                if(op_) { pushUndo(); applyOperator(op_, cur_row_, cur_col_, m); op_ = 0; }
                else { cur_row_ = 0; cur_col_ = 0; }
            }
            op_ = 0;
            return;
        }
        // f/F/t/T
        Motion m;
        bool ok = findMotion(w, key.ch, m);
        if(!ok) { op_ = 0; return; }
        if(op_) {
            char op = op_; op_ = 0;
            pushUndo();
            applyOperator(op, cur_row_, cur_col_, m);
        } else {
            cur_row_ = m.row; cur_col_ = m.col;
        }
        return;
    }

    // ---- 2) 已有操作符，等待动作 ----
    if(op_) {
        char op = op_;
        // 重复操作符 => 整行 (dd/yy/cc)
        if(key.type == Key::Char && key.ch == op) {
            op_ = 0;
            pushUndo();
            Motion m; m.ok = true; m.row = cur_row_; m.col = cur_col_; m.linewise = true;
            applyOperator(op, cur_row_, cur_col_, m);
            return;
        }
        switch(key.type) {
            case Key::Char: {
                char c = key.ch;
                if(c >= '1' && c <= '9') { return; } // 暂不支持计数
                Motion m;
                if(c=='h'||c=='l'||c=='j'||c=='k'||c=='w'||c=='b'||c=='e'||c=='0'||c=='$'||c=='G') {
                    // 计算目标
                    std::size_t saveR = cur_row_, saveC = cur_col_;
                    bool ok = true;
                    if(c=='h') { m.ok = true; m.inclusive=false; m.linewise=false; m.row=cur_row_; m.col = cur_col_>0?cur_col_-1:0; }
                    else if(c=='l') { m.ok = true; m.inclusive=false; m.linewise=false; m.row=cur_row_; std::size_t mx = lines_[cur_row_].size(); m.col = (cur_col_+1<mx)?cur_col_+1:(mx?mx-1:0); }
                    else if(c=='j') { m.ok = true; m.linewise = true; m.row = std::min(cur_row_+1, lines_.size()-1); m.col = cur_col_; }
                    else if(c=='k') { m.ok = true; m.linewise = true; m.row = cur_row_>0?cur_row_-1:0; m.col = cur_col_; }
                    else if(c=='0') { m.ok = true; m.inclusive=false; m.linewise=false; m.row=cur_row_; m.col=0; }
                    else if(c=='$') { m.ok = true; m.inclusive=true; m.linewise=false; m.row=cur_row_; m.col = lines_[cur_row_].empty()?0:lines_[cur_row_].size()-1; }
                    else if(c=='G') { m.ok = true; m.linewise = true; m.row = lines_.size()-1; m.col = 0; }
                    else ok = wordMotion(c, m);
                    (void)saveR; (void)saveC;
                    op_ = 0;
                    if(ok && m.ok) { pushUndo(); applyOperator(op, cur_row_, cur_col_, m); }
                    return;
                }
                if(c=='f'||c=='F'||c=='t'||c=='T') { motion_wait_ = c; return; }
                if(c=='g') { motion_wait_ = 'g'; return; }
                if(c=='/') {
                    search_op_ = op;        // 记录操作符，提交搜索时应用
                    op_ = 0;
                    startSearch(false);
                    return;
                }
                if(c=='%') {
                    Motion bm;
                    bool ok = bracketMotion(bm);
                    op_ = 0;
                    if(ok) { pushUndo(); applyOperator(op, cur_row_, cur_col_, bm); }
                    return;
                }
                op_ = 0;   // 非法组合
                return;
            }
            case Key::Esc: op_ = 0; return;
            default: op_ = 0; return;
        }
    }

    // ---- 3) 无 pending：普通单键 ----
    switch(key.type) {
        case Key::Char:
            switch(key.ch) {
                case 'h': moveCursor(0, -1); break;
                case 'j': moveCursor(1, 0); break;
                case 'k': moveCursor(-1, 0); break;
                case 'l': moveCursor(0, 1); break;
                case '0': cur_col_ = 0; break;
                case '$': cur_col_ = lines_[cur_row_].empty()?0:lines_[cur_row_].size()-1; break;
                case 'G': cur_row_ = lines_.size()-1; cur_col_ = 0; break;
                case 'w': { Motion m; if(wordMotion('w', m)) { cur_row_=m.row; cur_col_=m.col; } break; }
                case 'b': { Motion m; if(wordMotion('b', m)) { cur_row_=m.row; cur_col_=m.col; } break; }
                case 'e': { Motion m; if(wordMotion('e', m)) { cur_row_=m.row; cur_col_=m.col; } break; }
                case 'i': enterInsert(Mode::Insert); break;
                case 'a':
                    if(cur_col_ < lines_[cur_row_].size()) ++cur_col_;
                    enterInsert(Mode::Insert);
                    break;
                case 'o':
                    pushUndo();
                    lines_.insert(lines_.begin() + cur_row_ + 1 , "");
                    ++cur_row_; cur_col_ = 0; mode_ = Mode::Insert; dirty_ = true;
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
                    sel_row_ = cur_row_; sel_col_ = cur_col_; mode_ = Mode::Visual; break;
                case 'd': case 'y': case 'c':
                    op_ = key.ch; break;
                case 'f': case 'F': case 't': case 'T':
                    motion_wait_ = key.ch; break;
                case '%': {
                    Motion m;
                    if(bracketMotion(m)) { cur_row_ = m.row; cur_col_ = m.col; }
                    break;
                }
                case 'n': doSearchStep(search_backward_); break;
                case 'N': doSearchStep(!search_backward_); break;
                case ':': mode_ = Mode::Command; cmdline_.clear(); break;
                case '/': startSearch(false); break;
                case '?': startSearch(true); break;
                default: break;
            }
            break;
        case Key::Up:       moveCursor(-1, 0); break;
        case Key::Down:     moveCursor(1, 0); break;
        case Key::Left:     moveCursor(0, -1); break;
        case Key::Right:    moveCursor(0, 1); break;
        case Key::Home:     cur_col_ = 0; break;
        case Key::End:      cur_col_ = lines_[cur_row_].empty()?0:lines_[cur_row_].size()-1; break;
        case Key::PageUp:   moveCursor(-view_.rows(), 0); break;
        case Key::PageDown: moveCursor( view_.rows(), 0); break;
        case Key::CtrlR:    redo(); break;
        case Key::Esc: break;
        default: break;
    }
}

// ============================ Insert 模式 ============================

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
            ++cur_row_; cur_col_ = 0; dirty_ = true;
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
                --cur_row_; dirty_ = true;
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
            ++cur_col_; dirty_ = true;
            break;
        case Key::Left:  if (cur_col_ > 0) cur_col_--; break;
        case Key::Right: if (cur_col_ < lines_[cur_row_].size()) cur_col_++; break;
        case Key::Up:    moveCursor(-1, 0); break;
        case Key::Down:  moveCursor(1, 0);  break;
        default: break;
    }
}

// ============================ Command 模式 ============================

void Editor::handleCommand(const KeyEvent& key) {
    switch(key.type) {
        case Key::Esc: mode_ = Mode::Normal; cmdline_.clear(); break;
        case Key::Enter: executeCommand(cmdline_); cmdline_.clear(); mode_ = Mode::Normal; break;
        case Key::Backspace:
            if(!cmdline_.empty()) cmdline_.pop_back();
            else mode_ = Mode::Normal;
            break;
        case Key::Char: cmdline_.push_back(key.ch); break;
        default: break;
    }
}

bool Editor::saveFile() {
    if(filepath_.empty()) { setStatus("no file name"); return false; }
    std::ofstream fout(filepath_, std::ios::binary | std::ios::trunc);
    if(!fout.is_open()) { setStatus("cannot write: " + filepath_); return false; }
    for(std::size_t i = 0; i < lines_.size(); ++i) {
        fout << lines_[i];
        bool last = (i + 1 == lines_.size());
        if(!last || ends_with_newline_) fout << eol_;
    }
    fout.close();
    if(fout.fail()) { setStatus("write failed: " + filepath_); return false; }
    dirty_ = false;
    setStatus("written: " + filepath_);
    return true;
}

void Editor::executeCommand(const std::string& cmd) {
    if(cmd == "w") {
        saveFile();
    } else if(cmd == "q") {
        if(dirty_) setStatus("unsaved changes (use :q!)");
        else running_ = false;
    } else if(cmd == "wq") {
        if(saveFile()) running_ = false;
    } else if(cmd == "q!") {
        running_ = false;
    } else if(cmd == "noh" || cmd == "nohlsearch") {
        search_active_ = false;
        search_term_.clear();
    } else if(!cmd.empty() && std::all_of(cmd.begin(), cmd.end(),
              [](unsigned char c){ return std::isdigit(c); })) {
        // :行号 跳转
        long n = std::stol(cmd);
        if(n >= 1) {
            cur_row_ = std::min(static_cast<std::size_t>(n-1), lines_.size()-1);
            cur_col_ = 0;
        }
    } else {
        setStatus("unknown command: " + cmd);
    }
}

// ============================ Visual 模式 ============================

void Editor::handleVisual(const KeyEvent& key) {
    if(key.type == Key::Esc || (key.type == Key::Char && key.ch == 'v')) {
        mode_ = Mode::Normal; return;
    }
    if(key.type == Key::Char) {
        switch(key.ch) {
            case 'h': moveCursor(0, -1); break;
            case 'j': moveCursor(1, 0);  break;
            case 'k': moveCursor(-1, 0); break;
            case 'l': moveCursor(0, 1);  break;
            case '0': cur_col_ = 0; break;
            case '$': cur_col_ = lines_[cur_row_].empty()?0:lines_[cur_row_].size()-1; break;
            case 'w': { Motion m; if(wordMotion('w', m)) { cur_row_=m.row; cur_col_=m.col; } break; }
            case 'b': { Motion m; if(wordMotion('b', m)) { cur_row_=m.row; cur_col_=m.col; } break; }
            case 'e': { Motion m; if(wordMotion('e', m)) { cur_row_=m.row; cur_col_=m.col; } break; }
            case 'd':
            case 'x': deleteSelection(); break;
            case 'y': {
                yankSelection();
                std::size_t sr, sc, er, ec; normalizeSelection(sr, sc, er, ec);
                cur_row_ = sr; cur_col_ = sc; mode_ = Mode::Normal;
                break;
            }
            case 'c':
                deleteSelection();
                mode_ = Mode::Insert;
                break;
            default: break;
        }
    } else if (key.type == Key::Up)    moveCursor(-1, 0);
    else if (key.type == Key::Down)    moveCursor(1, 0);
    else if (key.type == Key::Left)    moveCursor(0, -1);
    else if (key.type == Key::Right)   moveCursor(0, 1);
}

// ============================ 搜索 ============================

void Editor::startSearch(bool backward) {
    search_backward_ = backward;
    search_origin_off_ = cursorOffset();
    if(search_op_ == 0) search_op_ = 0;   // 保留操作符
    search_active_ = true;
    if(!search_term_.empty()) {
        // 已有上次搜索词，仍进入输入模式以便修改
    }
    mode_ = Mode::Search;
    // 复用 cmdline_ 作为输入缓冲
    cmdline_ = search_term_;
}

bool Editor::searchFromOffset(std::size_t startOff, bool backward,
                              const std::string& term, std::size_t& outOff) const {
    if(term.empty()) return false;
    std::string flat = flatBuffer();
    if(backward) {
        if(startOff == 0) return false;
        std::size_t pos = flat.rfind(term, startOff - 1);
        if(pos == std::string::npos) {
            pos = flat.rfind(term);   // 回环
            if(pos == std::string::npos) return false;
        }
        outOff = pos; return true;
    } else {
        std::size_t pos = flat.find(term, startOff);
        if(pos == std::string::npos) {
            pos = flat.find(term, 0); // 回环
            if(pos == std::string::npos) return false;
        }
        outOff = pos; return true;
    }
}

bool Editor::doSearchStep(bool backward) {
    if(search_term_.empty()) return false;
    std::size_t start = cursorOffset();
    // 下一次搜索从当前匹配之后开始
    if(!backward) start = start + 1;
    std::size_t off;
    if(!searchFromOffset(start, backward, search_term_, off)) {
        setStatus("pattern not found: " + search_term_);
        return false;
    }
    search_active_ = true;
    setFromOffset(off);
    return true;
}

void Editor::incrementalSearch() {
    // 输入时实时跳转到首个匹配（从起点开始）
    if(search_term_.empty()) { setFromOffset(search_origin_off_); return; }
    std::size_t off;
    if(searchFromOffset(search_origin_off_, search_backward_, search_term_, off))
        setFromOffset(off);
    else
        setFromOffset(search_origin_off_);
}

void Editor::handleSearch(const KeyEvent& key) {
    switch(key.type) {
        case Key::Esc:
            // 取消：回到起点，若由操作符触发则清操作符
            setFromOffset(search_origin_off_);
            search_op_ = 0;
            search_active_ = !search_term_.empty();  // 保留高亮（若有旧词）
            mode_ = Mode::Normal;
            cmdline_.clear();
            break;
        case Key::Enter: {
            search_term_ = cmdline_;
            if(search_term_.empty()) {
                mode_ = Mode::Normal;
                break;
            }
            char pendingOp = search_op_;
            search_op_ = 0;
            if(pendingOp) {
                // d/y/c + /pattern : 作用于起点到匹配处
                std::size_t off;
                if(searchFromOffset(search_origin_off_, search_backward_, search_term_, off)) {
                    setFromOffset(search_origin_off_);
                    std::size_t sr = cur_row_, sc = cur_col_;
                    setFromOffset(off);
                    Motion m; m.ok = true; m.row = cur_row_; m.col = cur_col_;
                    m.linewise = false; m.inclusive = false;   // 删到匹配起点（不含）
                    setFromOffset(search_origin_off_);
                    pushUndo();
                    applyOperator(pendingOp, sr, sc, m);
                } else {
                    setFromOffset(search_origin_off_);
                    setStatus("pattern not found: " + search_term_);
                }
            } else {
                // 普通提交：从起点跳转到匹配
                std::size_t off;
                if(searchFromOffset(search_origin_off_, search_backward_, search_term_, off)) {
                    search_active_ = true;
                    setFromOffset(off);
                } else {
                    setFromOffset(search_origin_off_);
                    setStatus("pattern not found: " + search_term_);
                }
            }
            // c 操作符需停留在 Insert；其余回到 Normal
            if(!(pendingOp == 'c'))
                mode_ = Mode::Normal;
            cmdline_.clear();
            break;
        }
        case Key::Backspace:
            if(!cmdline_.empty()) {
                cmdline_.pop_back();
                search_term_ = cmdline_;
                incrementalSearch();
            } else {
                setFromOffset(search_origin_off_);
                mode_ = Mode::Normal;
            }
            break;
        case Key::Char:
            cmdline_.push_back(key.ch);
            search_term_ = cmdline_;
            incrementalSearch();
            break;
        default: break;
    }
}

// ============================ 动作与辅助 ============================

void Editor::clampCursor() {
    if(lines_.empty()) lines_.push_back("");
    if(cur_row_ >= lines_.size()) cur_row_ = lines_.size() - 1;
    std::size_t max_col = lines_[cur_row_].size();
    if((mode_ == Mode::Normal || mode_ == Mode::Visual) && max_col > 0)
        --max_col;
    if(cur_col_ > max_col) cur_col_ = max_col;
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
        if((mode_ == Mode::Normal || mode_ == Mode::Visual) && maxc > 0) --maxc;
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
    pushUndo();
    finishLineDelete(cur_row_, cur_row_);
}

void Editor::yankLine() {
    yank_buffer_ = { lines_[cur_row_] };
    yank_linewise_ = true;
}

void Editor::paste() {
    if(yank_buffer_.empty()) return;
    pushUndo();
    if(yank_linewise_) {
        lines_.insert(lines_.begin() + cur_row_ + 1, yank_buffer_.begin(), yank_buffer_.end());
        ++cur_row_; cur_col_ = 0;
    } else {
        std::string block;
        for(std::size_t i = 0; i < yank_buffer_.size(); ++i) {
            if(i) block.push_back('\n');
            block += yank_buffer_[i];
        }
        std::string& line = lines_[cur_row_];
        std::string head = line.substr(0, cur_col_);
        std::string tail = line.substr(cur_col_);
        std::vector<std::string> pieces; std::string acc;
        for(char c : block) { if(c=='\n'){pieces.push_back(acc);acc.clear();} else acc.push_back(c); }
        pieces.push_back(acc);
        std::vector<std::string> mids(pieces.begin() + 1, pieces.end());
        if(!mids.empty()) {
            lines_[cur_row_] = head + pieces.front();
            mids.back() += tail;
            lines_.insert(lines_.begin() + cur_row_ + 1, mids.begin(), mids.end());
            cur_row_ += mids.size();
            cur_col_ = mids.back().size() - tail.size();
        } else {
            lines_[cur_row_] = head + pieces.front() + tail;
            cur_col_ = head.size() + pieces.front().size();
        }
    }
    dirty_ = true;
}

void Editor::pushUndo() {
    undo_stack_.push_back({lines_, cur_row_, cur_col_});
    if(undo_stack_.size() > 100) undo_stack_.erase(undo_stack_.begin());
    redo_stack_.clear();
}

void Editor::undo() {
    if(undo_stack_.empty()) return;
    redo_stack_.push_back({lines_, cur_row_, cur_col_});
    Snapshot s = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    lines_   = std::move(s.lines);
    cur_row_ = std::min(s.row, lines_.size()-1);
    cur_col_ = std::min(s.col, lines_[cur_row_].size());
    dirty_   = true;
}

void Editor::redo() {
    if(redo_stack_.empty()) return;
    undo_stack_.push_back({lines_, cur_row_, cur_col_});
    Snapshot s = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    lines_   = std::move(s.lines);
    cur_row_ = std::min(s.row, lines_.size()-1);
    cur_col_ = std::min(s.col, lines_[cur_row_].size());
    dirty_   = true;
}

void Editor::normalizeSelection(std::size_t& sr, std::size_t& sc,
                                std::size_t& er, std::size_t& ec) const {
    sr = sel_row_; sc = sel_col_;
    er = cur_row_; ec = cur_col_;
    if(sr > er || (sr == er && sc > ec)) { std::swap(sr, er); std::swap(sc, ec); }
}

void Editor::deleteSelection() {
    std::size_t sr, sc, er, ec; normalizeSelection(sr, sc, er, ec);
    std::size_t startOff = offsetOf(sr, sc);
    std::size_t endOff   = offsetOf(er, ec);
    replaceFlatRange(startOff, endOff + 1, /*addUndo=*/true);
    mode_ = Mode::Normal;
}

void Editor::yankSelection() {
    std::size_t sr, sc, er, ec; normalizeSelection(sr, sc, er, ec);
    std::size_t startOff = offsetOf(sr, sc);
    std::size_t endOff   = offsetOf(er, ec);
    std::string flat = flatBuffer();
    if(endOff + 1 > flat.size()) endOff = flat.empty()?0:flat.size()-1;
    std::string captured = flat.substr(startOff, endOff + 1 - startOff);
    std::vector<std::string> buf; std::string acc;
    for(char ch : captured) { if(ch=='\n'){buf.push_back(acc);acc.clear();} else acc.push_back(ch); }
    buf.push_back(acc);
    yank_buffer_ = std::move(buf);
    yank_linewise_ = false;
}

std::size_t Editor::selStartRow() const { std::size_t a,b,c,d; normalizeSelection(a,b,c,d); return a; }
std::size_t Editor::selStartCol() const { std::size_t a,b,c,d; normalizeSelection(a,b,c,d); return b; }
std::size_t Editor::selEndRow()   const { std::size_t a,b,c,d; normalizeSelection(a,b,c,d); return c; }
std::size_t Editor::selEndCol()   const { std::size_t a,b,c,d; normalizeSelection(a,b,c,d); return d; }

void Editor::setStatus(const std::string& msg) {
    status_msg_ = msg;
}
