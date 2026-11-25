#include "renderer.h"
#include "editor.h"
#include "mode.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

namespace {

// 取路径最后一段作为文件名
std::string baseName(const std::string& p) {
    auto s = p.find_last_of("\\/");
    return s == std::string::npos ? p : p.substr(s + 1);
}

// 单字符样式：选区 > 当前命中(橙) > 其它命中(黄) > 当前行 > 普通
std::string styleFor(bool selected, bool cur_match, bool matched, bool current) {
    if(selected)  return "\033[48;5;60m";       // 选区背景（蓝）
    if(cur_match) return "\033[48;5;208;30m";   // 光标所在命中（橙底黑字）
    if(matched)   return "\033[48;5;142;30m";   // 其它搜索命中（黄底黑字）
    if(current)   return "\033[48;5;237m";      // 当前行背景（灰）
    return "\033[0m";
}

const char* RESET = "\033[0m";

} // namespace

void Renderer::draw(const Editor& editor) {
    std::ostringstream out;
    out << "\033[H";   // 光标归位

    const std::size_t top = editor.topLine();
    const int rows = editor.textRows();
    const int gutter = editor.gutterWidth();

    // 文本区 逐行输出（行号槽 + 带样式的正文）
    for(int i = 0; i < rows; ++i) {
        std::size_t buf_line = top + static_cast<std::size_t>(i);
        out << renderLine(editor, buf_line);
        out << "\033[K\n";
    }

    out << renderStatusBar(editor) << "\033[K\n";
    out << renderCmdline(editor)   << "\033[K";

    // 光标定位：屏幕行 = 相对顶部；屏幕列 = 行号槽 + 可见列偏移
    std::size_t screen_row = editor.curRow() - top + 1;
    std::size_t vis_col = (editor.curCol() >= editor.leftCol())
                        ? editor.curCol() - editor.leftCol() : 0;
    std::size_t screen_col = static_cast<std::size_t>(gutter) + vis_col + 1;
    out << "\033[" << screen_row << ";" << screen_col << "H";

    // Insert / Search 模式下需要看到光标
    bool show_cursor = (editor.mode() == Mode::Insert || editor.mode() == Mode::Search);
    out << (show_cursor ? "\033[?25h" : "\033[?25l");

    std::cout << out.str() << std::flush;
}

std::string Renderer::renderStatusBar(const Editor& e) const {
    const char* mode =
        e.mode() == Mode::Normal ? "NORMAL" :
        e.mode() == Mode::Insert ? "INSERT" :
        e.mode() == Mode::Visual ? "VISUAL" :
        e.mode() == Mode::Search ? "SEARCH" : "COMMAND";

    std::size_t total = e.lineCount();
    std::size_t pct = 0;
    if(total > 1)
        pct = e.curRow() * 100 / (total - 1);

    std::string name = e.filePath().empty() ? "[No Name]" : baseName(e.filePath());

    std::ostringstream left;
    left << " -- " << mode << " --  " << name;
    if(e.dirty()) left << " [+]";
    if(!e.statusMsg().empty()) left << "  |  " << e.statusMsg();

    std::ostringstream right;
    right << "row " << e.curRow() + 1 << "/" << total
          << "  col " << e.curCol() + 1
          << "  " << pct << "% ";

    // 反色状态栏：左段 + 补齐空格使右段贴右，整行铺满屏宽
    std::string L = left.str();
    std::string R = right.str();
    int total_w = e.textCols();   // 状态栏用整屏宽
    int pad = total_w - static_cast<int>(L.size()) - static_cast<int>(R.size());
    if(pad < 1) pad = 1;

    std::ostringstream os;
    os << "\033[7m" << L
       << std::string(static_cast<std::size_t>(pad), ' ')
       << R << RESET;
    return os.str();
}

std::string Renderer::renderLine(const Editor& e, std::size_t buf_line) const {
    const auto& lines = e.lines();
    const bool exists = buf_line < lines.size();
    const bool current = (buf_line == e.curRow());
    const int gutter = e.gutterWidth();
    const int text_cols = e.textAreaCols();

    // ---- 行号槽（固定宽度，不随水平滚动）----
    std::ostringstream g;
    if(exists) {
        std::ostringstream num; num << (buf_line + 1);
        std::string ns = num.str();
        // 右对齐到 gutter-1，再接一个空格
        std::string pad(static_cast<std::size_t>(std::max(0, gutter - 1 - static_cast<int>(ns.size()))), ' ');
        g << (current ? "\033[1;7m" : "\033[90m") << pad << ns << RESET;
    } else {
        // 超出文件末尾：~ 与行号右对齐同一列
        g << "\033[90m" << std::string(static_cast<std::size_t>(std::max(0, gutter - 2)), ' ') << "~" << RESET;
    }
    g << " ";

    if(!exists) return g.str();

    // ---- 正文（按 left_col 横向窗口裁剪）----
    const std::string& line = lines[buf_line];
    std::size_t left  = e.leftCol();
    std::size_t begin = std::min(left, line.size());
    std::size_t end   = std::min(left + static_cast<std::size_t>(text_cols), line.size());

    // 本行在选区内的区间 [ss, se]
    bool has_sel = false;
    std::size_t ss = 0, se = 0;
    if(e.mode() == Mode::Visual) {
        std::size_t sr = e.selStartRow(), sc = e.selStartCol();
        std::size_t er = e.selEndRow(),   ec = e.selEndCol();
        if(buf_line >= sr && buf_line <= er) {
            has_sel = true;
            std::size_t last = line.empty() ? 0 : line.size() - 1;
            if(sr == er)             { ss = sc; se = ec; }
            else if(buf_line == sr)  { ss = sc; se = last; }
            else if(buf_line == er)  { ss = 0;  se = ec; }
            else                     { ss = 0;  se = last; }
        }
    }

    // 本行内所有搜索命中区间
    std::vector<std::pair<std::size_t, std::size_t>> matches;
    if(e.searchActive() && !e.searchTerm().empty()) {
        const std::string& term = e.searchTerm();
        std::size_t pos = line.find(term);
        while(pos != std::string::npos) {
            matches.emplace_back(pos, pos + term.size() - 1);   // 含端点
            pos = line.find(term, pos + 1);
        }
    }
    auto in_match = [&](std::size_t i) {
        for(auto& m : matches) if(i >= m.first && i <= m.second) return true;
        return false;
    };
    // 仅搜索时：找出“包含光标”的那一处匹配（本行），其它命中保持黄色，当前命中标橙
    bool has_cur_match = false;
    std::size_t cs = 0, ce = 0;
    if(e.searchActive() && !matches.empty() && buf_line == e.curRow()) {
        std::size_t cc = e.curCol();
        for(auto& m : matches) {
            if(cc >= m.first && cc <= m.second) { has_cur_match = true; cs = m.first; ce = m.second; break; }
        }
    }

    std::ostringstream body;
    std::string prev;
    auto emit = [&](const std::string& st) {
        if(st != prev) { body << st; prev = st; }
    };
    for(std::size_t i = begin; i < end; ++i) {
        bool sel = has_sel && i >= ss && i <= se;
        bool cm  = has_cur_match && i >= cs && i <= ce;
        emit(styleFor(sel, cm, in_match(i), current));
        body << line[i];
    }
    // 当前行铺满文本区，使高亮成整条
    int used = static_cast<int>(end - begin);
    if(current && used < text_cols) {
        emit(styleFor(false, false, false, true));
        body << std::string(static_cast<std::size_t>(text_cols - used), ' ');
    }
    body << RESET;

    return g.str() + body.str();
}

std::string Renderer::renderCmdline(const Editor& e) const {
    std::string s = e.cmdline();
    if(e.mode() == Mode::Command)
        return ":" + s;
    if(e.mode() == Mode::Search)
        return (e.searchBackward() ? "?" : "/") + s;
    return s;
}
