#include "view.h"
#include <algorithm>

void View::resize(int rows, int cols) {
    rows_ = std::max(1, rows);
    cols_ = std::max(1, cols);
}