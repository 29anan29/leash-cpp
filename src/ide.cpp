// Leash Vim 风格 IDE — 支持插入/普通/命令模式、hjkl、dd/yy/p、:w/:q/:run
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "checker/typecheck.hpp"
#include "codegen/compiler.hpp"
#include "vm/vm.hpp"
#include "host/host.hpp"
#include "host/native.hpp"
#include "common/error.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <cctype>

using namespace leash;

#define CSI "\x1b["
#define RESET CSI"0m"
#define BOLD CSI"1m"
#define RED CSI"31m"
#define GREEN CSI"32m"
#define YELLOW CSI"33m"
#define BLUE CSI"34m"
#define MAGENTA CSI"35m"
#define CYAN CSI"36m"
#define GRAY CSI"90m"
#define BG_BLUE CSI"44m"
#define BG_DARK CSI"100m"

static termios orig;
static void setRaw(bool on) {
    static bool raw = false;
    if (on && !raw) {
        tcgetattr(STDIN_FILENO, &orig);
        termios r = orig;
        r.c_iflag &= ~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);
        r.c_oflag &= ~(OPOST);
        r.c_cflag |= CS8;
        r.c_lflag &= ~(ECHO|ICANON|IEXTEN|ISIG);
        r.c_cc[VMIN] = 0; r.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &r);
        raw = true;
    } else if (!on && raw) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
        raw = false;
    }
}
static int tw() { winsize w; ioctl(1, TIOCGWINSZ, &w); return w.ws_col?:80; }
static int th() { winsize w; ioctl(1, TIOCGWINSZ, &w); return w.ws_row?:24; }
static void gotoxy(int x,int y) { std::cout << CSI << y << ";" << x << "H"; }
static void clearScr() { std::cout << CSI "2J" CSI "H"; }
static void clearLine() { std::cout << CSI "2K"; }

// ======== 语法高亮 ========
static bool isKW(const std::string& w) {
    static const char* k[] = {"fn","let","mut","if","else","while","return","true","false",
        "out","in","requires","package","import","agent","tool","chain",
        "int","float","bool","str","type","cap","unsafe","break","continue","and","or","not"};
    for (auto x : k) if (w == x) return true;
    return false;
}
static std::string hl(const std::string& s) {
    std::string o; int i=0,n=(int)s.size();
    while (i<n) {
        if (s[i]=='/'&&i+1<n&&s[i+1]=='/') { o+=GRAY+s.substr(i)+RESET; break; }
        if (s[i]=='"') {
            o+=GREEN; int j=i;
            while (j<n&&!(s[j]=='"'&&(j==i||s[j-1]!='\\'))) j++;
            if (j<n) j++;
            o+=s.substr(i,j-i)+RESET; i=j; continue;
        }
        if (std::isdigit(s[i])||(s[i]=='-'&&i+1<n&&std::isdigit(s[i+1]))) {
            o+=MAGENTA; int j=i;
            if (s[j]=='-')j++;
            bool dot=false;
            while (j<n&&(std::isdigit(s[j])||(!dot&&s[j]=='.'))){if(s[j]=='.')dot=true;j++;}
            o+=s.substr(i,j-i)+RESET; i=j; continue;
        }
        if (std::isalpha(s[i])||s[i]=='_') {
            int j=i; while(j<n&&(std::isalnum(s[j])||s[j]=='_'))j++;
            std::string w=s.substr(i,j-i);
            if (isKW(w)) o+=CYAN+w+RESET;
            else o+=w;
            i=j; continue;
        }
        o+=s[i]; i++;
    }
    return o;
}

// ======== VimBuffer ========
struct VimBuffer {
    std::vector<std::string> lines;
    int cx=0, cy=0, sx=0, sy=0;
    std::string filename;
    bool modified=false;
    std::vector<std::string> yank;
    bool running=true;

    enum Mode { NORMAL, INSERT, CMD } mode = NORMAL;
    std::string cmdLine;
    std::string statusMsg;
    int statusTimer=0;

    // 用于撤销的简单快照
    std::vector<std::string> undoLines;
    int undoCx=0, undoCy=0;

    VimBuffer() { lines={""}; }
    std::string& line() { return lines[cy]; }

    void saveUndo() { undoLines=lines; undoCx=cx; undoCy=cy; }
    void undo() { std::swap(lines,undoLines); std::swap(cx,undoCx); std::swap(cy,undoCy); modified=true; }

    void insertChar(char c) {
        saveUndo();
        line().insert(line().begin()+cx,c); cx++; modified=true;
    }
    void newLine() {
        saveUndo();
        std::string r=line().substr(cx); line().erase(cx);
        lines.insert(lines.begin()+cy+1,r); cy++; cx=0; modified=true;
    }
    void openBelow() {
        saveUndo();
        cy++; lines.insert(lines.begin()+cy, ""); cx=0; modified=true;
    }
    void openAbove() {
        saveUndo();
        lines.insert(lines.begin()+cy, ""); cx=0; modified=true;
    }
    void backspace() {
        if (cx>0) { saveUndo(); line().erase(cx-1,1); cx--; modified=true; }
        else if (cy>0) {
            saveUndo(); int o=(int)lines[cy-1].size();
            lines[cy-1]+=line(); lines.erase(lines.begin()+cy); cy--; cx=o; modified=true;
        }
    }
    void delChar() {
        if (cx<(int)line().size()) { saveUndo(); line().erase(cx,1); modified=true; }
        else if (cy+1<(int)lines.size()) {
            saveUndo(); line()+=lines[cy+1]; lines.erase(lines.begin()+cy+1); modified=true;
        }
    }
    void delLine() {
        if (lines.size()>1) { saveUndo(); yank={lines[cy]}; lines.erase(lines.begin()+cy);
            if (cy>=(int)lines.size()) cy--;
            cx=std::min(cx,(int)lines[cy].size()); modified=true; }
        else { yank={""}; lines={""}; cx=cy=0; modified=true; }
    }
    void yankLine() { yank={lines[cy]}; }
    void pasteBelow() {
        if (yank.empty()) return;
        saveUndo();
        for (auto& l : yank) { cy++; lines.insert(lines.begin()+cy, l); }
        cx=0; modified=true;
    }
    void pasteAbove() {
        if (yank.empty()) return;
        saveUndo();
        for (auto& l : yank) { lines.insert(lines.begin()+cy, l); cy++; }
        cy-= (int)yank.size(); cx=0; modified=true;
    }

    void moveLeft()  { if (cx>0) cx--; else if(cy>0){cy--;cx=(int)lines[cy].size();} }
    void moveRight() { if (cx<(int)line().size()) cx++; else if(cy+1<(int)lines.size()){cy++;cx=0;} }
    void moveUp()    { if(cy>0){cy--;cx=std::min(cx,(int)lines[cy].size());} }
    void moveDown()  { if(cy+1<(int)lines.size()){cy++;cx=std::min(cx,(int)lines[cy].size());} }
    void moveWord()  { while(cx<(int)line().size()&&!std::isalnum(line()[cx]))cx++; while(cx<(int)line().size()&&std::isalnum(line()[cx]))cx++; }
    void moveHome()  { cx=0; }
    void moveEnd()   { cx=(int)line().size(); }
    void moveLineStart() {
        int i=0; while(i<(int)line().size()&&(line()[i]==' '||line()[i]=='\t')) i++;
        cx=i;
    }

    void scroll() {
        int rows=th()-3;
        if (cy<sy) sy=cy;
        if (cy>=sy+rows) sy=cy-rows+1;
    }

    void setStatus(const std::string& msg) { statusMsg=msg; statusTimer=30; }

    // ---- 运行 Leash ----
    void runLeash() {
        std::string src;
        for (int i=0;i<(int)lines.size();i++) { src+=lines[i]; if(i+1<(int)lines.size()||true) src+="\n"; }
        try {
            initBuiltinPackages();
            auto toks=Lexer::tokenize(src);
            Parser p(toks); Program prog=p.parse();
            for (auto& pn : prog.packages) {
                if (pn=="agent") continue;
                auto* pk=findPackage(pn);
                if (!pk) continue;
                for (auto& fd : pk->funcs) {
                    bool dup=false;
                    for (auto& f : prog.fns) if (f->name==fd.name&&f->isNative){dup=true;break;}
                    if (dup) continue;
                    auto fn=std::make_shared<Stmt>(); fn->k=Stmt::SFn; fn->name=fd.name;
                    fn->params.resize(fd.arity); fn->isNative=true; fn->nativeFn=fd.fn;
                    prog.fns.push_back(fn);
                }
            }
            Checker ch; ch.check(prog, prog.packages);
            Compiler comp; auto funcs=comp.compile(prog, prog.packages);
            Store store; AuditLog audit; HostContext hc(store, audit);
            VM vm(funcs, hc, 10000000);
            vm.run();
            setStatus("运行完成 ✓");
        } catch (const std::exception& e) {
            setStatus(std::string("错误: ")+e.what());
        }
    }
};

// ======== 显示 ========
static void draw(VimBuffer& buf) {
    gotoxy(1,1);
    int w=tw(), h=th();
    int rows=h-3;
    buf.scroll();

    // 状态栏
    std::cout << BG_BLUE << " " << (buf.filename.empty()?"未命名":buf.filename);
    if (buf.modified) std::cout << " [+]";
    std::cout << "  ";
    switch (buf.mode) {
        case VimBuffer::NORMAL: std::cout << "-- 普通模式 --"; break;
        case VimBuffer::INSERT: std::cout << "-- 插入模式 --"; break;
        case VimBuffer::CMD:    std::cout << "-- 命令模式 --"; break;
    }
    std::string right = " :q 退出  :w 保存  :run 运行 ";
    int pad=w-(int)(std::string(" ")+(buf.filename.empty()?"未命名":buf.filename)+(buf.modified?" [+]":"")+right).size()-20;
    for (int i=0;i<pad;i++) std::cout << ' ';
    std::cout << right << RESET "\n";

    // 编辑区域
    for (int r=0;r<rows;r++) {
        int idx=buf.sy+r;
        if (idx>=(int)buf.lines.size()) { clearLine(); std::cout << "\n"; continue; }
        std::cout << GRAY;
        std::string ln=std::to_string(idx+1);
        int pad2=4-(int)ln.size(); if(pad2<1)pad2=1;
        for (int i=0;i<pad2;i++) std::cout << ' ';
        std::cout << ln << " " << RESET;
        std::string l=buf.lines[idx];
        std::string hlStr = hl(l);
        int start=buf.sx;
        std::string vis = (start<(int)hlStr.size()) ? hlStr.substr(start,w-5) : "";
        // 当前行高亮
        if (idx==buf.cy && buf.mode==VimBuffer::NORMAL) std::cout << BG_DARK;
        std::cout << vis;
        if (idx==buf.cy && buf.mode==VimBuffer::NORMAL) std::cout << RESET;
        clearLine(); std::cout << "\n";
    }

    // 消息栏
    if (buf.statusTimer>0) {
        std::cout << buf.statusMsg; buf.statusTimer--;
        clearLine();
    } else {
        if (buf.mode==VimBuffer::CMD) {
            std::cout << ":" << buf.cmdLine;
        } else {
            std::cout << "  " << (buf.cy+1) << "," << (buf.cx+1) << "  ";
            clearLine();
        }
    }
    std::cout << "\n";

    // 光标定位
    int cr = buf.cy-buf.sy+2;
    int cc = buf.cx-buf.sx+5;
    gotoxy(std::max(1,cc), std::max(1,std::min(cr,h)));
    std::cout.flush();
}

// ======== 命令执行 ========
static void execCmd(VimBuffer& buf, const std::string& cmd) {
    buf.mode=VimBuffer::NORMAL;
    if (cmd=="q"||cmd=="q!") { buf.running=false; return; }
    if (cmd=="wq"||cmd=="x") {
        if (!buf.filename.empty()) {
            std::ofstream ofs(buf.filename);
            for (int i=0;i<(int)buf.lines.size();i++) { ofs<<buf.lines[i]; if(i+1<(int)buf.lines.size()) ofs<<"\n"; }
            buf.modified=false;
            buf.setStatus("已保存: "+buf.filename);
        } else buf.setStatus("文件名未设置");
        if (cmd=="q"||cmd=="x") buf.running=false;
        return;
    }
    if (cmd.substr(0,2)=="w ") {
        std::string path=cmd.substr(2);
        std::ofstream ofs(path);
        for (int i=0;i<(int)buf.lines.size();i++) { ofs<<buf.lines[i]; if(i+1<(int)buf.lines.size()) ofs<<"\n"; }
        buf.filename=path; buf.modified=false;
        buf.setStatus("已保存: "+path); return;
    }
    if (cmd=="w") {
        if (buf.filename.empty()) { buf.setStatus("文件名未设置，使用 :w 文件名"); return; }
        std::ofstream ofs(buf.filename);
        for (int i=0;i<(int)buf.lines.size();i++) { ofs<<buf.lines[i]; if(i+1<(int)buf.lines.size()) ofs<<"\n"; }
        buf.modified=false;
        buf.setStatus("已保存: "+buf.filename); return;
    }
    if (cmd.substr(0,2)=="e ") {
        std::string path=cmd.substr(2);
        std::ifstream ifs(path);
        if (!ifs) { buf.setStatus("无法打开: "+path); return; }
        buf.lines.clear(); std::string l;
        while(std::getline(ifs,l)) buf.lines.push_back(l);
        if(buf.lines.empty()) buf.lines={""};
        buf.filename=path; buf.cx=buf.cy=0; buf.modified=false;
        buf.setStatus("已打开: "+path); return;
    }
    if (cmd=="run"||cmd=="!") { buf.runLeash(); return; }
    if (cmd=="help") {
        buf.setStatus(":w 保存 :q 退出 :wq 保存退出 :e 文件名 打开 :run 运行");
        return;
    }
    buf.setStatus("未知命令: "+cmd);
}

// ======== 主循环 ========
static void handleInput(VimBuffer& buf) {
    unsigned char c;
    if (read(0,&c,1)!=1) return;

    if (buf.mode==VimBuffer::CMD) {
        if (c==10||c==13) { execCmd(buf, buf.cmdLine); buf.cmdLine.clear(); return; }
        if (c==27) { buf.mode=VimBuffer::NORMAL; buf.cmdLine.clear(); return; }
        if (c==127||c==8) { if(!buf.cmdLine.empty()) buf.cmdLine.pop_back(); return; }
        if (c>=32&&c<127) { buf.cmdLine+= (char)c; return; }
        return;
    }

    if (buf.mode==VimBuffer::INSERT) {
        if (c==27) { buf.mode=VimBuffer::NORMAL; return; }
        if (c==10||c==13) { buf.newLine(); return; }
        if (c==127||c==8) { buf.backspace(); return; }
        if (c==9) { buf.insertChar(' '); buf.insertChar(' '); return; }
        if (c>=32&&c<127) { buf.insertChar((char)c); return; }
        return;
    }

    // ---- 普通模式 ----
    if (c==':' && buf.mode==VimBuffer::NORMAL) {
        buf.mode=VimBuffer::CMD; buf.cmdLine.clear(); return;
    }
    if (c=='i') { buf.mode=VimBuffer::INSERT; return; }
    if (c=='I') { buf.cx=0; buf.mode=VimBuffer::INSERT; return; }
    if (c=='a') { if(buf.cx<(int)buf.line().size()) buf.cx++; buf.mode=VimBuffer::INSERT; return; }
    if (c=='A') { buf.cx=(int)buf.line().size(); buf.mode=VimBuffer::INSERT; return; }
    if (c=='o') { buf.openBelow(); buf.mode=VimBuffer::INSERT; return; }
    if (c=='O') { buf.openAbove(); buf.mode=VimBuffer::INSERT; return; }
    if (c=='x') { buf.delChar(); return; }
    if (c=='u') { buf.undo(); return; }
    if (c==' ') { buf.scroll(); return; }
    if (c=='0') { buf.moveHome(); return; }
    if (c=='$'||c=='A') { buf.moveEnd(); return; }
    if (c=='^') { buf.moveLineStart(); return; }
    if (c=='w') { buf.moveWord(); return; }

    // hjkl
    if (c=='h') { buf.moveLeft(); return; }
    if (c=='j') { buf.moveDown(); return; }
    if (c=='k') { buf.moveUp(); return; }
    if (c=='l') { buf.moveRight(); return; }

    // 双键命令
    if (c=='d') {
        unsigned char c2; if (read(0,&c2,1)!=1) return;
        if (c2=='d') { buf.delLine(); }
        else if (c2=='d'&&buf.mode==VimBuffer::NORMAL){}
        return;
    }
    if (c=='y') {
        unsigned char c2; if (read(0,&c2,1)!=1) return;
        if (c2=='y') { buf.yankLine(); buf.setStatus("已复制"); }
        return;
    }
    if (c=='p') { buf.pasteBelow(); return; }
    if (c=='P') { buf.pasteAbove(); return; }

    if (c=='G') { buf.cy=(int)buf.lines.size()-1; buf.cx=std::min(buf.cx,(int)buf.line().size()); return; }
    if (c=='g') {
        unsigned char c2; if (read(0,&c2,1)!=1) return;
        if (c2=='g') { buf.cy=0; buf.cx=0; }
        return;
    }

    // 方向键
    if (c==27) {
        unsigned char c2,c3;
        if (read(0,&c2,1)!=1) return;
        if (c2==91) {
            if (read(0,&c3,1)!=1) return;
            switch(c3) {
                case 68: buf.moveLeft(); break;
                case 67: buf.moveRight(); break;
                case 65: buf.moveUp(); break;
                case 66: buf.moveDown(); break;
                case 72: buf.moveHome(); break;
                case 70: buf.moveEnd(); break;
            }
        }
        return;
    }

    if (c==17) { buf.running=false; return; } // Ctrl+Q
    if (c==19) { // Ctrl+S
        if (!buf.filename.empty()) {
            std::ofstream ofs(buf.filename);
            for (int i=0;i<(int)buf.lines.size();i++) { ofs<<buf.lines[i]; if(i+1<(int)buf.lines.size()) ofs<<"\n"; }
            buf.modified=false; buf.setStatus("已保存");
        } else buf.setStatus(":w 文件名");
        return;
    }
    if (c==18) { buf.runLeash(); return; } // Ctrl+R
}

int main(int argc, char** argv) {
    VimBuffer buf;
    if (argc>1) {
        std::ifstream ifs(argv[1]);
        if (ifs) { buf.lines.clear(); std::string l; while(std::getline(ifs,l)) buf.lines.push_back(l); }
        if (buf.lines.empty()) buf.lines={""};
        buf.filename=argv[1];
    }

    setRaw(true);
    while (buf.running) {
        draw(buf);
        handleInput(buf);
    }
    setRaw(false);
    clearScr();
    std::cout << "Bye!\n";
    return 0;
}
