#pragma once
#include <string>

enum class Key {
    Char        = 0,
    Esc         = 1,
    Enter       = 2,
    Backspace   = 3,
    Tab         = 4,
    Up          = 5,
    Down        = 6,
    Left        = 7,
    Right       = 8,
    Home        = 9,
    End         = 10,
    PageUp      = 11,
    PageDown    = 12,
    CtrlR       = 13,
    Unknown     = 14
};

struct KeyEvent {
    Key type = Key::Unknown;
    char ch = 0;
};

class InputReader {
public:
    KeyEvent readKey();
};