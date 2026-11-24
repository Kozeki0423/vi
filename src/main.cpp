#include <iostream>
#include <string>
#include <filesystem>
#include <exception>

#ifdef _WIN32
#include <io.h>
#include <cstdio>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "editor.h"

// 判断标准输入是否为交互式终端
static bool stdinIsTTY() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

// 可执行文件所在目录
static std::string exeDir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if(n == 0) return "";
    std::string p(buf, n);
#else
    char buf[4096] = {0};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if(n <= 0) return "";
    std::string p(buf, static_cast<std::size_t>(n));
#endif
    auto slash = p.find_last_of("\\/");
    return slash == std::string::npos ? "" : p.substr(0, slash);
}

// 把相对程序路径解析为绝对路径：以可执行文件目录为锚，逐级上溯查找
static std::string resolveFromExe(const std::string& rel) {
    namespace fs = std::filesystem;
    fs::path given(rel);
    if(given.is_absolute()) return given.string();
    fs::path dir = exeDir();
    for(int i = 0; i < 8 && !dir.empty(); ++i) {
        std::error_code ec;
        fs::path cand = dir / given;
        if(fs::exists(cand, ec)) return cand.string();
        fs::path parent = dir.parent_path();
        if(parent == dir) break;
        dir = parent;
    }
    return rel;   // 兜底：退回原相对路径
}

// 验证函数：以 path（相对程序路径）打开文件并进入编辑器。
// 读取发生在 bootstrap，写回通过 :w / :wq 触发。
// 终端状态由 TerminalSession 的 RAII 负责恢复，故正常退出与异常退出均安全。
int openEditor(const std::string& path) {
    if(!stdinIsTTY()) {
        std::cerr << "OctQuill requires an interactive terminal (TTY).\n";
        return 1;
    }
    try {
        Editor editor;
        editor.bootstrap(resolveFromExe(path));
        editor.run();
    } catch(const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    std::string path = (argc > 1) ? argv[1] : "assets/test.txt";
    return openEditor(path);
}
