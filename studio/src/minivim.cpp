// Leash 终端编辑器：打开文件 / 语法高亮 / Vim 风格快捷键 / 运行(Ctrl+R)
//   i a I A o O 进入插入       Esc 返回普通
//   h j k l / 方向键 移动       w b 词移   0 $ 行首/尾   gg G 文件首/尾
//   x 删字符  dd 删行  yy 复制  p/P 粘贴    u 撤销  Ctrl+Y 重做
//   / 向下搜索  ? 向上搜索  n 下一处  N 上一处
//   Ctrl+S 保存  Ctrl+O 打开(图形框)  Ctrl+N 新建(图形框)  Ctrl+R 运行  Ctrl+Q 退出  Ctrl+H 帮助
//   :w :q :wq :e <文件> :run :数字(跳转行) 命令模式
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>

using namespace std;

#define CSI   "\x1b["
#define RESET CSI "0m"
#define GRAY  CSI "90m"
#define GREEN CSI "32m"
#define MAG   CSI "35m"
#define CYAN  CSI "36m"
#define BG_BLUE CSI "44m"
#define BG_DARK CSI "100m"
#define BG_YELLOW CSI "43m"

static int TWIN() { struct winsize w; ioctl(1, TIOCGWINSZ, &w); int c = w.ws_col; return c > 0 ? c : 80; }
static int TWIH() { struct winsize w; ioctl(1, TIOCGWINSZ, &w); int r = w.ws_row; return r > 0 ? r : 24; }

static bool isKW(const string& w) {
    static const char* k[] = {"fn","let","mut","if","else","while","return","true","false",
        "out","in","requires","package","import","agent","tool","chain",
        "int","float","bool","str","type","cap","unsafe","break","continue","and","or","not",nullptr};
    for (int i = 0; k[i]; i++) if (w == k[i]) return true;
    return false;
}
// 逐字符标注 token 类别：k=关键字 s=字符串 n=数字 c=注释 i=标识符 p=标点
static vector<char> tokenClass(const string& s) {
    int n = (int)s.size(); vector<char> tc(n, 'p');
    int i = 0;
    while (i < n) {
        if (s[i] == '/' && i + 1 < n && s[i+1] == '/') { for (int j = i; j < n; j++) tc[j] = 'c'; break; }
        if (s[i] == '"') { int j = i; while (j < n && !(s[j] == '"' && (j == i || s[j-1] != '\\'))) j++; if (j < n) j++; for (int k = i; k < j; k++) tc[k] = 's'; i = j; continue; }
        if (isdigit((unsigned char)s[i]) || (s[i] == '-' && i + 1 < n && isdigit((unsigned char)s[i+1]))) {
            int j = i; if (s[j] == '-') j++; bool dot = false;
            while (j < n && (isdigit((unsigned char)s[j]) || (!dot && s[j] == '.'))) { if (s[j]=='.') dot=true; j++; }
            for (int k = i; k < j; k++) tc[k] = 'n';
            i = j; continue;
        }
        if (isalpha((unsigned char)s[i]) || s[i] == '_') {
            int j = i; while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
            string w = s.substr(i, j - i); char c = isKW(w) ? 'k' : 'i';
            for (int k = i; k < j; k++) tc[k] = c;
            i = j; continue;
        }
        i++;
    }
    return tc;
}
// 渲染一行可见窗口 [colOff, colOff+cols)：仅做 token 配色 + 搜索命中高亮（不在整行铺底色）
static string paintLine(const string& s, int colOff, int cols, const string& searchTerm) {
    if (cols <= 0) return "";
    int n = (int)s.size();
    int end = colOff + cols; if (end > n) end = n;
    vector<bool> mt(max(end - colOff, 0), false);
    if (!searchTerm.empty()) {
        size_t p = 0;
        while ((p = s.find(searchTerm, p)) != string::npos) {
            int a = (int)p, b = (int)p + (int)searchTerm.size();
            for (int k = max(a, colOff); k < min(b, end); k++) mt[k - colOff] = true;
            p += searchTerm.size();
        }
    }
    vector<char> tc = tokenClass(s);
    string o;
    string last;
    for (int k = colOff; k < end; k++) {
        char cls = (k < n) ? tc[k] : 'p';
        string base;
        switch (cls) {
            case 'k': base = CYAN; break;
            case 's': base = GREEN; break;
            case 'n': base = MAG; break;
            case 'c': base = GRAY; break;
            default: base = "";
        }
        bool m = mt[k - colOff];
        string d = m ? (BG_YELLOW + base) : base;
        if (d != last) { if (d.empty()) o += RESET; else o += d; last = d; }
        o += s[k];
    }
    if (!last.empty()) o += RESET;
    return o;
}
struct TermMode {
    termios orig;
    TermMode() {
        tcgetattr(STDIN_FILENO, &orig);
        termios r = orig;
        r.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        r.c_iflag &= ~(IXON | ICRNL);
        r.c_oflag |= OPOST | ONLCR;   // 让 \n 自动回车到行首（运行/帮助输出才不会错位）
        r.c_cc[VMIN] = 0; r.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &r);
        cout << CSI "6 q";   // 细条光标，避免落在当前行高亮上时形成灰块、影响阅读
        cout.flush();
    }
    ~TermMode() { cout << CSI "0 q"; cout.flush(); tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig); }
};
static void gotoxy(int x, int y) { cout << CSI << y << ";" << x << "H"; }
static void clearLine() { cout << CSI "2K"; }
static void clearScreen() { cout << CSI "2J" CSI "H"; }

struct Vim {
    vector<string> lines;
    int cx = 0, cy = 0, rowOff = 0, colOff = 0;
    enum Mode { NORMAL, INSERT, CMD, SEARCH } mode = NORMAL;
    string filename, statusMsg, cmdLine;
    string searchTerm, searchInput;
    string promptMsg;   // 打开/新建 等交互提示（区别于状态消息）
    int searchDir = 1;
    int statusTimer = 0;
    bool modified = false, running = true;
    vector<string> yank;
    vector<vector<string>> undoStack, redoStack;

    Vim(const string& f) : filename(f) {
        if (!filename.empty()) {
            ifstream file(filename);
            if (file) { string l; while (getline(file, l)) lines.push_back(l); }
        }
        if (lines.empty()) lines.push_back("");
    }
    void setStatus(const string& m) { statusMsg = m; statusTimer = 40; markFull(); }

    void saveUndo() {
        undoStack.push_back(lines);
        if (undoStack.size() > 200) undoStack.erase(undoStack.begin());
        redoStack.clear();
    }
    void undo() {
        if (undoStack.empty()) { setStatus("无可撤销"); return; }
        redoStack.push_back(lines);
        lines = undoStack.back(); undoStack.pop_back(); clamp(); modified = true; setStatus("已撤销");
    }
    void redo() {
        if (redoStack.empty()) { setStatus("无可重做"); return; }
        undoStack.push_back(lines);
        lines = redoStack.back(); redoStack.pop_back(); clamp(); modified = true; setStatus("已重做");
    }
    void clamp() {
        if (cy >= (int)lines.size()) cy = (int)lines.size() - 1;
        if (cy < 0) cy = 0;
        if (cx > (int)lines[cy].size()) cx = (int)lines[cy].size();
        if (cx < 0) cx = 0;
    }

    void insertChar(char c) { saveUndo(); if (cy == (int)lines.size()) lines.push_back(""); lines[cy].insert(cx, 1, c); cx++; modified = true; }
    void newLine() { saveUndo(); string r = lines[cy].substr(cx); lines[cy] = lines[cy].substr(0, cx); lines.insert(lines.begin() + cy + 1, r); cy++; cx = 0; modified = true; }
    void backspace() {
        if (cx > 0) { saveUndo(); lines[cy].erase(cx - 1, 1); cx--; modified = true; }
        else if (cy > 0) { saveUndo(); int o = lines[cy-1].size(); lines[cy-1] += lines[cy]; lines.erase(lines.begin() + cy); cy--; cx = o; modified = true; }
    }
    void delChar() {
        if (cx < (int)lines[cy].size()) { saveUndo(); lines[cy].erase(cx, 1); modified = true; }
        else if (cy + 1 < (int)lines.size()) { saveUndo(); lines[cy] += lines[cy+1]; lines.erase(lines.begin() + cy + 1); modified = true; }
    }
    void delLine() {
        saveUndo();
        if (lines.size() > 1) { yank = { lines[cy] }; lines.erase(lines.begin() + cy); if (cy >= (int)lines.size()) cy--; }
        else { yank = {""}; lines = {""}; }
        cx = 0; modified = true;
    }
    void yankLine() { yank = { lines[cy] }; setStatus("已复制行"); }
    void pasteBelow() { if (yank.empty()) return; saveUndo(); for (auto& l : yank) lines.insert(lines.begin() + cy + 1, l); cy++; cx = 0; modified = true; }
    void pasteAbove() { if (yank.empty()) return; saveUndo(); for (auto& l : yank) lines.insert(lines.begin() + cy, l); cx = 0; modified = true; }

    void moveLeft() { if (cx > 0) cx--; else if (cy > 0) { cy--; cx = lines[cy].size(); } }
    void moveRight() { if (cx < (int)lines[cy].size()) cx++; else if (cy + 1 < (int)lines.size()) { cy++; cx = 0; } }
    void moveUp() { if (cy > 0) { cy--; if (cx > (int)lines[cy].size()) cx = lines[cy].size(); } }
    void moveDown() { if (cy + 1 < (int)lines.size()) { cy++; if (cx > (int)lines[cy].size()) cx = lines[cy].size(); } }
    void moveWord() { while (cx < (int)lines[cy].size() && !isalnum((unsigned char)lines[cy][cx])) cx++; while (cx < (int)lines[cy].size() && isalnum((unsigned char)lines[cy][cx])) cx++; }
    void moveBWord() { while (cx > 0 && !isalnum((unsigned char)lines[cy][cx-1])) cx--; while (cx > 0 && isalnum((unsigned char)lines[cy][cx-1])) cx--; }

    void scroll() {
        int rows = TWIH() - 2;
        if (cy < rowOff) rowOff = cy;
        if (cy >= rowOff + rows) rowOff = cy - rows + 1;
        int cols = TWIN() - 5;
        if (cx < colOff) colOff = cx;
        if (cx >= colOff + cols) colOff = cx - cols + 1;
        if (colOff < 0) colOff = 0;
    }

    void saveFile() {
        if (filename.empty()) { setStatus("先用 Ctrl+O 或 :e 文件名 命名"); return; }
        ofstream o(filename);
        for (size_t i = 0; i < lines.size(); i++) { o << lines[i]; if (i + 1 < lines.size()) o << "\n"; }
        modified = false; setStatus("已保存: " + filename);
    }
    // 相对路径基于当前文件所在目录解析，并统一规范为绝对路径（避免重复拼接）
    string resolvePath(const string& p) {
        if (p.empty()) return p;
        if (p[0] == '/' || (p.size() > 2 && p[1] == ':')) return p;   // 已是绝对路径
        string dir = filename.empty() ? "" : filename;
        auto pos = dir.find_last_of("/\\");
        dir = (pos == string::npos) ? "" : dir.substr(0, pos);
        string combined = dir.empty() ? p : dir + "/" + p;
        if (combined.empty() || combined[0] != '/') {                 // 转成绝对路径
            char cwdbuf[4096];
            string cwd = (getcwd(cwdbuf, sizeof cwdbuf) ? cwdbuf : ".");
            combined = cwd + "/" + combined;
        }
        return combined;
    }
    // 新建文件时若用户未给扩展名，自动补上 .ae（Leash 源文件）
    string ensureAeExt(const string& p) {
        if (p.empty()) return p;
        size_t slash = p.find_last_of("/\\");
        string name = p.substr(slash == string::npos ? 0 : slash + 1);
        if (name.find('.') != string::npos) return p;
        return p + ".ae";
    }
    void openFile(const string& p, bool reload = false) {
        string path = resolvePath(p);
        ifstream f(path);
        bool exists = (bool)f;
        filename = path; lines.clear();
        if (exists) { string l; while (getline(f, l)) lines.push_back(l); }
        if (lines.empty()) lines.push_back("");
        cy = cx = rowOff = colOff = 0; modified = false; undoStack.clear(); redoStack.clear();
        setStatus(exists ? (reload ? "已重新加载: " : "已打开: ") + path
                         : "新建文件: " + path);
    }
    // 有未保存修改时，确认是否丢弃（y 确认 / 其余取消）
    // 注意：终端为非阻塞读（VMIN=0/VTIME=1），需用 select 等待，否则 read 会立即返回 0
    bool maybeDiscard() {
        if (!modified) return true;
        setStatus("未保存的修改将被丢弃，确认打开/重载? (y/N)");
        markFull(); render();
        char c;
        while (true) {
            fd_set s; FD_ZERO(&s); FD_SET(STDIN_FILENO, &s);
            timeval tv = {100, 0};   // 100ms 超时后重试，持续等待按键
            if (select(STDIN_FILENO + 1, &s, nullptr, nullptr, &tv) <= 0) continue;
            if (read(STDIN_FILENO, &c, 1) != 1) continue;
            if (c == 'y' || c == 'Y') return true;
            if (c == '\n' || c == '\r' || c == 'n' || c == 'N' || c == 27) return false;
        }
    }

    void runLeash() {
        // 临时运行文件放在当前文件所在目录，保证 import 的相对路径可解析（多文件项目）
        string base = "/tmp";
        if (!filename.empty()) {
            auto pos = filename.find_last_of("/\\");
            base = (pos == string::npos) ? "." : filename.substr(0, pos);
        }
        string tmp = base + "/.leash_run_" + to_string(getpid()) + ".ae";
        { ofstream o(tmp); for (auto& l : lines) o << l << "\n"; }
        string cmd = string("./leash \"") + tmp + "\" 2>&1";
        FILE* p = popen(cmd.c_str(), "r");
        string out;
        if (!p) { out = "无法启动 leash 编译器（请确认 ./leash 存在）"; }
        else { char buf[4096]; while (fgets(buf, sizeof buf, p)) out += buf; pclose(p); }
        remove(tmp.c_str());
        showOutput(out);
    }
    void showOutput(const string& out) {
        cout << "\x1b[?1049h";   // 切换到备用屏幕，保留编辑器原屏幕
        cout << "=== 运行结果 (Ctrl+R) ===\n\n";
        cout << out;
        if (out.empty() || out.back() != '\n') cout << "\n";
        cout << "\n-- 按任意键返回 --\n";
        cout.flush();
        char c; while (read(STDIN_FILENO, &c, 1) != 1);
        cout << "\x1b[?1049l";   // 切回主屏幕，编辑器原样恢复（不擦除）
        cout.flush();
        statusMsg = "已运行";
    }

    // ---- 增量渲染：只在内容/光标变化时刷新对应行，不整屏重绘 ----
    bool dirty = true;      // 是否需要重绘
    int  redrawFrom = -1;   // -1=整屏；>=0=仅从该逻辑行向下局部重绘

    void markFull() { dirty = true; redrawFrom = -1; }
    void markLine(int idx) {
        if (idx < 0) idx = 0;
        if (!dirty) { dirty = true; redrawFrom = idx; }
        else if (redrawFrom >= 0 && idx < redrawFrom) redrawFrom = idx;
    }
    bool onScreen() { int r = TWIH() - 2; return cy >= rowOff && cy < rowOff + r; }

    void drawHeader() {
        int w = TWIN();
        gotoxy(1, 1);
        clearLine();
        cout << BG_BLUE << " " << (filename.empty() ? "未命名" : filename);
        if (modified) cout << " [+]";
        cout << "  ";
        string modeStr = mode == INSERT ? "-- 插入 --" : (mode == CMD ? "-- 命令 --" : "-- 普通 --");
        cout << modeStr;
        string right = " Ctrl+S 保存  Ctrl+O 打开  Ctrl+N 新建  Ctrl+R 运行  Ctrl+Q 退出  Ctrl+H 帮助 ";
        string head = " " + (filename.empty() ? "未命名" : filename) + (modified ? " [+]" : "") + "  " + modeStr + right;
        int pad = w - (int)head.size(); if (pad < 0) pad = 0;
        for (int i = 0; i < pad; i++) cout << ' ';
        cout << right << RESET;
    }
    void drawLineAt(int idx, int r) {
        int w = TWIN(), gw = 5;
        gotoxy(1, r + 2);
        clearLine();
        if (idx >= (int)lines.size()) { cout << GRAY << "~" << RESET; return; }
        string ln = to_string(idx + 1);
        int gpad = gw - 1 - (int)ln.size(); if (gpad < 0) gpad = 0;
        // 当前行：行号用青色标记（不再整行铺灰底，保证可读性）
        if (idx == cy) cout << CYAN << ln << string(gpad, ' ') << " " << RESET;
        else cout << GRAY << ln << string(gpad, ' ') << " " << RESET;
        int cols = w - gw; if (cols < 0) cols = 0;
        cout << paintLine(lines[idx], colOff, cols, searchTerm);
    }
    void drawStatus() {
        gotoxy(1, TWIH());
        clearLine();
        if (statusTimer > 0) { cout << statusMsg; statusTimer--; }
        else if (mode == CMD) { cout << ":" << cmdLine; }
        else if (mode == SEARCH) { cout << (searchDir == 1 ? "/" : "?") << searchInput; }
        else if (!promptMsg.empty()) { cout << promptMsg; }
        else { cout << "  " << (cy + 1) << "," << (cx + 1) << "  " << lines.size() << " 行"; }
    }

    // 从当前位置起（不含光标处）查找 term；forward=true 向下/向后，false 向上/向前；支持回绕
    void doSearch(const string& term, bool forward) {
        if (term.empty()) { setStatus("无搜索词"); return; }
        int n = (int)lines.size();
        int startLine = cy, startCol = forward ? cx + 1 : cx - 1;
        for (int step = 0; step < n; step++) {
            int li = forward ? (startLine + step) % n : (startLine - step + n) % n;
            int from = (step == 0) ? startCol : (forward ? 0 : (int)lines[li].size());
            size_t found;
            if (forward) found = lines[li].find(term, from);
            else found = lines[li].rfind(term, from);
            if (found != string::npos) {
                cy = li; cx = (int)found; clamp();
                searchTerm = term;
                scroll(); markFull();
                setStatus("匹配: 第 " + to_string(li + 1) + " 行");
                return;
            }
        }
        setStatus("未找到: " + term);
    }
    void positionCursor() {
        int gw = 5;
        int cr = cy - rowOff + 2;
        int cc = (cx - colOff) + gw + 1;
        if (cr < 2) cr = 2;
        if (cr > TWIH() - 1) cr = TWIH() - 1;
        if (cc < gw + 1) cc = gw + 1;
        if (cc > TWIN()) cc = TWIN();
        gotoxy(cc, cr);
    }
    void render() {
        scroll();

        drawHeader();
        int rows = TWIH() - 2;
        if (redrawFrom < 0) {
            for (int r = 0; r < rows; r++) drawLineAt(rowOff + r, r);
        } else {
            for (int r = 0; r < rows; r++) {
                int idx = rowOff + r;
                if (idx >= redrawFrom) drawLineAt(idx, r);
            }
        }
        drawStatus();
        positionCursor();
        cout.flush();
    }

    void help() {
        cout << "\x1b[?1049h";   // 备用屏幕，不擦除编辑器
        cout << "=== Leash 编辑器快捷键 ===\n\n";
        cout << "移动: h j k l / 方向键, w 词后, b 词前, 0 行首, $ 行尾, gg 文件头, G 文件尾\n";
        cout << "插入: i 光标前, a 光标后, I 行首, A 行尾, o 下行, O 上行, Esc 退出插入\n";
        cout << "编辑: x 删字符, dd 删行, yy 复制行, p/P 粘贴, u 撤销, Ctrl+Y 重做\n";
        cout << "搜索: / 向下搜  ? 向上搜  n 下一处  N 上一处（命中处黄色高亮）\n";
        cout << "跳转: :数字 跳到第 N 行\n";
        cout << "文件: Ctrl+S 保存, Ctrl+O 打开(图形选择框), Ctrl+N 新建(选位置), Ctrl+R 运行, Ctrl+Q 退出, Ctrl+H 本帮助\n";
        cout << "运行: Ctrl+R 编译并运行当前缓冲区\n";
        cout << "命令: :w 保存  :q 退出  :wq 保存退出  :e <文件> 打开  :e 重载当前文件  :run 运行  :数字 跳转行\n";
        cout << "\n按任意键继续...\n";
        cout.flush();
        char c; while (read(STDIN_FILENO, &c, 1) != 1);
        cout << "\x1b[?1049l";   // 恢复编辑器
        cout.flush();
        statusMsg = "";
    }

    int readKey() {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) return -1;
        if (c == 27) {
            char seq[3];
            timeval tv = {0, 10000}; fd_set s; FD_ZERO(&s); FD_SET(STDIN_FILENO, &s);
            if (select(STDIN_FILENO + 1, &s, nullptr, nullptr, &tv) <= 0) return 27;
            if (read(STDIN_FILENO, &seq[0], 1) != 1) return 27;
            if (seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                    switch (seq[1]) {
                        case 'A': return 1000; case 'B': return 1001;
                        case 'C': return 1002; case 'D': return 1003;
                    }
                }
            } else if (seq[0] == 'O') {
                if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[1] == 'H') return 1000;
                    if (seq[1] == 'F') return 1001;
                }
            }
            return 27;
        }
        return (unsigned char)c;
    }

    // ---- 图形文件选择框（GUI 弹窗）----
    // 有桌面环境且存在 zenity/kdialog 时使用系统文件选择框；
    // 否则返回 ""，由调用方回退到文本提示。
    static bool toolExists(const char* t) {
        string cmd = string("command -v ") + t + " >/dev/null 2>&1";
        return system(cmd.c_str()) == 0;
    }
    bool guiAvailable() {
        static int cached = -1;
        if (cached != -1) return cached;
        bool disp = getenv("DISPLAY") || getenv("WAYLAND_DISPLAY");
        bool tool = toolExists("zenity") || toolExists("kdialog");
        cached = (disp && tool) ? 1 : 0;
        return cached;
    }
    // save=true 时为“新建/另存为”模式（可选择目录并输入文件名）
    string guiFileDialog(bool save) {
        if (!guiAvailable()) return "";
        string tool = toolExists("zenity") ? "zenity" : "kdialog";
        string initial = filename.empty() ? "." : filename;
        string cmd;
        if (tool == "zenity") {
            cmd = string("zenity --file-selection ") + (save ? "--save " : "") +
                  "--title=\"" + (save ? "新建文件位置" : "打开文件") + "\" " +
                  "--filename=\"" + initial + "\" 2>/dev/null";
        } else {
            cmd = string("kdialog ") + (save ? "--getsavefilename " : "--getopenfilename ") +
                  "\"" + initial + "\" 2>/dev/null";
        }
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) return "";
        string line; char buf[4096];
        if (fgets(buf, sizeof buf, p)) line = buf;
        pclose(p);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        return line;
    }

    void promptOpen(const string& label = "打开", bool addExt = false) {
        string input;
        while (true) {
            promptMsg = label + ": " + input + "_";
            markFull(); render();
            char c;
            if (read(STDIN_FILENO, &c, 1) != 1) continue;
            if (c == '\n' || c == '\r') {
                bool opened = false;
                if (!input.empty() && maybeDiscard()) { openFile(addExt ? ensureAeExt(input) : input); opened = true; }
                promptMsg = "";
                if (!opened) statusMsg = "";
                break;
            }
            if (c == 27) { promptMsg = ""; statusMsg = ""; break; }
            if (c == 127 || c == 8) { if (!input.empty()) input.pop_back(); }
            else if (c >= 32 && c < 127) input += c;
        }
    }

    void execCmd() {
        mode = NORMAL;
        if (cmdLine == "q") { running = false; return; }
        bool numeric = !cmdLine.empty();
        for (char ch : cmdLine) if (!isdigit((unsigned char)ch)) { numeric = false; break; }
        if (numeric) {
            int t = stoi(cmdLine);
            if (t >= 1 && t <= (int)lines.size()) { cy = t - 1; cx = 0; markFull(); }
            else setStatus("行号越界: " + cmdLine);
            return;
        }
        if (cmdLine == "wq" || cmdLine == "x") { saveFile(); running = false; return; }
        if (cmdLine == "w") { saveFile(); return; }
        if (cmdLine == "run") { runLeash(); return; }
        if (cmdLine.rfind("w ", 0) == 0) { filename = cmdLine.substr(2); saveFile(); return; }
        if (cmdLine == "e") {
            if (filename.empty()) setStatus("无当前文件，无法重新加载");
            else if (maybeDiscard()) openFile(filename, true);
            return;
        }
        if (cmdLine.rfind("e ", 0) == 0) {
            string p = cmdLine.substr(2);
            size_t s = p.find_first_not_of(" \t");
            if (s != string::npos) p = p.substr(s);
            if (p.empty()) {
                if (filename.empty()) setStatus("无当前文件，无法重新加载");
                else if (maybeDiscard()) openFile(filename, true);
            } else if (maybeDiscard()) {
                openFile(p);
            }
            return;
        }
        if (cmdLine == "h" || cmdLine == "help") { help(); return; }
        setStatus("未知命令: " + cmdLine);
    }

    void onKey(int c) {
        if (c == -1) return;
        if (c == 19) { saveFile(); return; }          // Ctrl+S
        if (c == 17) { running = false; return; }      // Ctrl+Q
        if (c == 15) {                                  // Ctrl+O 打开
            string p = guiAvailable() ? guiFileDialog(false) : "";
            if (!p.empty()) { if (maybeDiscard()) openFile(p); }
            else promptOpen("打开");
            return;
        }
        if (c == 14) {                                  // Ctrl+N 新建文件（GUI 选位置，自动补 .ae；GUI 失败回退文本）
            string p = guiAvailable() ? guiFileDialog(true) : "";
            if (!p.empty()) { p = ensureAeExt(p); if (maybeDiscard()) openFile(p); }
            else promptOpen("新建文件", true);
            return;
        }
        if (c == 18) { runLeash(); markFull(); return; }   // Ctrl+R
        if (c == 7)  { help(); markFull(); return; }        // Ctrl+G
        if (c == 8)  { help(); markFull(); return; }        // Ctrl+H
        if (c == 25) { redo(); markFull(); return; }       // Ctrl+Y
        if (c >= 1000 && c <= 1003) {
            int oc = cy;
            if (c == 1000) moveUp(); else if (c == 1001) moveDown();
            else if (c == 1002) moveRight(); else moveLeft();
            if (!onScreen()) markFull(); else markLine(oc < cy ? oc : cy);
            return;
        }
        if (mode == SEARCH) {
            if (c == 27) { mode = NORMAL; searchInput.clear(); markFull(); return; }
            if (c == '\n' || c == '\r') {
                mode = NORMAL;
                if (!searchInput.empty()) {
                    searchTerm = searchInput;
                    doSearch(searchTerm, searchDir == 1);
                } else markFull();
                return;
            }
            if (c == 127) { if (!searchInput.empty()) searchInput.pop_back(); markFull(); return; }
            if (c >= 32 && c < 127) { searchInput += (char)c; markFull(); return; }
            return;
        }
        if (mode == CMD) {
            if (c == 27) { mode = NORMAL; cmdLine.clear(); markFull(); return; }
            if (c == '\n' || c == '\r') { execCmd(); markFull(); return; }
            if (c == 127) { if (!cmdLine.empty()) cmdLine.pop_back(); markFull(); return; }
            if (c >= 32 && c < 127) { cmdLine += (char)c; markFull(); return; }
            return;
        }
        if (mode == INSERT) {
            if (c == 27) { mode = NORMAL; if (cx > 0) cx--; markLine(cy); return; }
            if (c == '\n' || c == '\r') { newLine(); markFull(); return; }
            if (c == 127) {
                if (cx == 0 && cy > 0) markFull(); else markLine(cy);
                backspace(); return;
            }
            if (c >= 32) { insertChar((char)c); markLine(cy); return; }
            return;
        }
        // NORMAL
        static string pending;
        int oc = cy;
        switch (c) {
            case 'h': moveLeft(); break;
            case 'l': moveRight(); break;
            case 'k': moveUp(); break;
            case 'j': moveDown(); break;
            case 'w': moveWord(); break;
            case 'b': moveBWord(); break;
            case '0': cx = 0; break;
            case '$': cx = (int)lines[cy].size(); break;
            case 'G': cy = (int)lines.size() - 1; cx = (int)lines[cy].size(); break;
            case 'i': mode = INSERT; break;
            case 'a': if (cx < (int)lines[cy].size()) cx++; mode = INSERT; break;
            case 'I': cx = 0; mode = INSERT; break;
            case 'A': cx = (int)lines[cy].size(); mode = INSERT; break;
            case 'o': newLine(); mode = INSERT; break;
            case 'O': saveUndo(); lines.insert(lines.begin() + cy, ""); mode = INSERT; cx = 0; modified = true; break;
            case 'x': delChar(); break;
            case 'u': undo(); break;
            case '/': mode = SEARCH; searchInput.clear(); searchDir = 1; markFull(); return;
            case '?': mode = SEARCH; searchInput.clear(); searchDir = -1; markFull(); return;
            case 'n': if (!searchTerm.empty()) doSearch(searchTerm, true); return;
            case 'N': if (!searchTerm.empty()) doSearch(searchTerm, false); return;
            case ':': mode = CMD; cmdLine.clear(); break;
            case 'd': if (pending == "d") { delLine(); pending.clear(); } else pending = "d"; break;
            case 'y': if (pending == "y") { yankLine(); pending.clear(); } else pending = "y"; break;
            case 'p': pasteBelow(); break;
            case 'P': pasteAbove(); break;
            case 'g': if (pending == "g") { cy = 0; cx = 0; pending.clear(); } else pending = "g"; break;
            default: pending.clear(); break;
        }
        if (!pending.empty()) return;                  // 等待第二键，无需重绘
        if (c == 'o' || c == 'O') markFull();
        else if (c == 'x') markLine(cy);
        else if (c == 'u') markFull();
        else if (c == 'd') markFull();                 // dd
        else if (c == 'p' || c == 'P') markFull();
        else if (c == ':') markFull();
        else if (c == 'i' || c == 'a' || c == 'I' || c == 'A') markLine(oc);
        else if (c == 'y') { /* yy 仅复制 */ }
        else {                                         // 移动 / 0 / $ / G / gg
            if (!onScreen()) markFull(); else markLine(oc < cy ? oc : cy);
        }
    }
};

int main(int argc, char** argv) {
    string fname = argc > 1 ? argv[1] : "";
    Vim ed(fname);
    TermMode tm;
    clearScreen();
    while (ed.running) {
        if (ed.dirty) { ed.render(); ed.dirty = false; ed.redrawFrom = -1; }
        int c = ed.readKey();
        if (c == -1) continue;
        ed.onKey(c);
    }
    clearScreen();
    cout << "Bye!\n";
    return 0;
}
