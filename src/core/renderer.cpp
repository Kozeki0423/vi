#include "renderer.h"
#include "editor.h"
#include "mode.h"
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
        std::size_t buf_line = top + i;
        if(buf_line < lines.size())
            out << renderLine(editor, buf_line);
        // 清除尾部并换行
        out << "\033[K\n";
    }

    out << renderStatusBar(editor)  << "\033[K\n";
    out << renderCmdline(editor)    << "\033[K";

    // 定位光标至(cur_row-top+1, cur_col+1)
    std::size_t screen_row = editor.curRow() - top + 1;
    std::size_t screen_col = editor.curCol() + 1;
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

// 最小实现 直接返回文本
std::string Renderer::renderLine(const Editor& e, std::size_t buf_line) const {
    const auto& lines = e.lines();
    return buf_line < lines.size() ? lines[buf_line] : "";
}

// 最小实现 直接返回命令行内容
std::string Renderer::renderCmdline(const Editor& e) const {
    return e.cmdline();
}