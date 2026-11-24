#pragma once
#include <string>
#include <cstddef>

class Editor;
class Renderer {
public:
    void draw(const Editor& editor);
private:
    std::string renderLine(const Editor& editor, std::size_t buf_line) const;
    std::string renderStatusBar(const Editor& editor) const;
    std::string renderCmdline(const Editor& editor) const;
};