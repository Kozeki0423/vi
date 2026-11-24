#include "renderer.h"
#include "editor.h"
#include "mode.h"
#include <algorithm>
#include <iostream>
#include <sstream>

void Renderer::draw(const Editor& editor) {
    // 光标归位
    std::ostringstream out;
    out << "\033[H";

    const auto& lines = editor.lines();
    std::size_t top = editor.topLine();
    int rows = editor.textRows();

    // 文本区显示 逐行输出文本
    for(int i = 0; i < rows; ++i) {
        std::size_t buf_line = top + static_cast<std::size_t>(i);
        if(buf_line < lines.size())
            out << renderLine(editor, buf_line);
        // 清除尾部并换行
        out << "\033[K\n";
    }

    out << renderStatusBar(editor)  << "\033[K\n";
    out << renderCmdline(editor)    << "\033[K";

    // 定位光标（屏幕行/列均随滚动偏移，列减去 leftCol）
    std::size_t screen_row = editor.curRow() - top + 1;
    std::size_t screen_col = (editor.curCol() >= editor.leftCol())
                           ? editor.curCol() - editor.leftCol() + 1 : 1;
    out << "\033[" << screen_row << ";" << screen_col << "H";

    // Insert 模式显示光标 其余隐藏
    out << (editor.mode() == Mode::Insert ? "\033[?25h" : "\033[?25l");

    // 统一输出
    std::cout << out.str() << std::flush;
}

std::string Renderer::renderStatusBar(const Editor& e) const {
    std::ostringstream os;
    os  << "\033[7m"
        << " " << (e.mode() == Mode::Normal ? "-- NORMAL --" :
                   e.mode() == Mode::Insert ? "-- INSERT --" :
                   e.mode() == Mode::Visual ? "-- VISUAL --" : "-- COMMAND --")
        << " | row " << e.curRow() + 1
        << "  col " << e.curCol() + 1
        << "  " << e.statusMsg()
        << "\033[0m";
    return os.str();
}

// 按横向滚动窗口渲染单行，并对 Visual 选区反色高亮
std::string Renderer::renderLine(const Editor& e, std::size_t buf_line) const {
    const auto& lines = e.lines();
    if(buf_line >= lines.size()) return "";
    const std::string& line = lines[buf_line];

    int cols = e.textCols();
    if(cols <= 0) return "";
    std::size_t left  = e.leftCol();
    std::size_t begin = std::min(left, line.size());
    std::size_t end   = std::min(left + static_cast<std::size_t>(cols), line.size());

    // 计算本行在选区内的字符区间 [ss, se]（含端点）
    bool has_sel = false;
    std::size_t ss = 0, se = 0;
    if(e.mode() == Mode::Visual) {
        std::size_t sr = e.selStartRow(), sc = e.selStartCol();
        std::size_t er = e.selEndRow(),   ec = e.selEndCol();
        if(buf_line >= sr && buf_line <= er) {
            has_sel = true;
            std::size_t last = line.empty() ? 0 : line.size() - 1;
            if(sr == er)        { ss = sc; se = ec; }
            else if(buf_line == sr) { ss = sc; se = last; }
            else if(buf_line == er) { ss = 0;  se = ec; }
            else                { ss = 0;  se = last; }
        }
    }

    std::string out;
    bool in_sel = false;
    for(std::size_t i = begin; i < end; ++i) {
        bool sel = has_sel && i >= ss && i <= se;
        if(sel && !in_sel)      { out += "\033[7m"; in_sel = true; }
        else if(!sel && in_sel) { out += "\033[0m"; in_sel = false; }
        out.push_back(line[i]);
    }
    if(in_sel) out += "\033[0m";
    return out;
}

// 最小实现 直接返回命令行内容
std::string Renderer::renderCmdline(const Editor& e) const {
    return e.cmdline();
}
