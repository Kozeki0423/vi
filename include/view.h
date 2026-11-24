#pragma once
#include <cstddef>

class View {
public:
    void resize(int rows, int cols);

    int rows() const {return rows_; }
    int cols() const {return cols_; }
private:
    int rows_ = 24;
    int cols_ = 80;
};