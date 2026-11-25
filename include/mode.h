#pragma once
#include <vector>
#include <string>
#include <cstddef>

enum class Mode {
    Normal  = 0,
    Insert  = 1,
    Visual  = 2,
    Command = 3,
    Search  = 4
};

struct Snapshot {
    std::vector<std::string> lines;
    std::size_t row = 0;
    std::size_t col = 0;
};