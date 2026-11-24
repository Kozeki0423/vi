#include "input_reader.h"

#ifdef _WIN32
#include <conio.h>

KeyEvent InputReader::readKey() {
    int c = _getch();
    if(c == 0 || c == 0xE0) {
        // 功能键/方向键：第二字节标识具体按键
        int c2 = _getch();
        switch(c2) {
            case 72: return {Key::Up, 0};
            case 80: return {Key::Down, 0};
            case 75: return {Key::Left, 0};
            case 77: return {Key::Right, 0};
            case 71: return {Key::Home, 0};
            case 79: return {Key::End, 0};
            case 73: return {Key::PageUp, 0};
            case 81: return {Key::PageDown, 0};
            default: return {Key::Unknown, 0};
        }
    }
    switch(c) {
        case 27:  return {Key::Esc, 0};
        case '\r':
        case '\n': return {Key::Enter, 0};
        case 8:   return {Key::Backspace, 0};
        case '\t': return {Key::Tab, 0};
        default:
            if(c >= 0x20 && c < 0x7F) return {Key::Char, static_cast<char>(c)};
            return {Key::Unknown, 0};
    }
}

#else
#include <unistd.h>
#include <poll.h>

static int readByte() {
    unsigned char c;
    return (::read(STDIN_FILENO, &c, 1) == 1) ? c : -1;
}

KeyEvent InputReader::readKey() {
    int c = readByte();
    if(c == -1) return {Key::Unknown, 0};

    if(c == 0x1B) {
        pollfd pfd{STDIN_FILENO, POLLIN, 0};
        if(::poll(&pfd, 1, 10) <= 0) return {Key::Esc, 0};
        int c2 = readByte();
        if(c2 != '[' && c2 != 'O') return {Key::Esc, 0};
        int c3 = readByte();
        switch(c3) {
            case 'A': return {Key::Up, 0};
            case 'B': return {Key::Down, 0};
            case 'C': return {Key::Right, 0};
            case 'D': return {Key::Left, 0};
            case 'H': return {Key::Home, 0};
            case 'F': return {Key::End, 0};
            default:
                while(c3 >= '0' && c3 <= '9') c3 = readByte();
                return {Key::Unknown, 0};
        }
    }
    switch(c) {
        case '\r':
        case '\n': return {Key::Enter, 0};
        case 0x7F:
        case 0x08: return {Key::Backspace, 0};
        case '\t': return {Key::Tab, 0};
        default:
            if(c >= 0x20 && c < 0x7F) return {Key::Char, static_cast<char>(c)};
            return {Key::Unknown, 0};
    }
}

#endif // _WIN32
