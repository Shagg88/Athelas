// The x87 stack cache. The generated code keeps the console's floating point stack in ctx.st, indexed through
// ctx.top at run time, so every operand is a load or store at a computed address the C++ compiler can't keep in a
// register. Within a run of code that no branch enters or leaves (no label, jump, call, return or hook), the stack's
// depth is known here at translation time: slot (x87_t0 + j) & 7, x87_t0 being the top as the run began, is the local
// x87_rj, loaded on its first use. Pushes and pops only move the depth here. Before anything that leaves the run, or
// an instruction that reads ctx.top or ctx.st itself, the locals that changed are written back and ctx.top is set.
// The same values in the same order as before: only where they are kept between instructions changes.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "codegen.h"

void CodeGen::Line(const std::string& s) {
    if (!x87_on_) {
        RawLine(s);
        return;
    }
    std::string pre;
    std::string t = X87Rewrite(s, pre);
    if (!pre.empty()) RawLine(pre);
    RawLine(t);
}

void CodeGen::X87Flush() {
    if (!x87_.t0) {
        x87_ = {};
        return;
    }
    std::string s;
    char buf[96];
    for (int j = 0; j < 8; j++)
        if (x87_.dirty >> j & 1) {
            std::snprintf(buf, sizeof(buf), " ctx.st[(x87_t0 + %du) & 7] = x87_r%d;", j, j);
            s += buf;
        }
    int d = x87_.d & 7;
    if (d) {
        std::snprintf(buf, sizeof(buf), " ctx.top = (x87_t0 + %du) & 7;", 8 - d);
        s += buf;
    }
    x87_ = {};
    if (!s.empty()) RawLine("\t{" + s + " }");
}

std::string CodeGen::X87Rewrite(const std::string& s, std::string& pre) {
    // what reads the stack through ctx: written back first, then left as it is
    if (s.find("ctx.top") != std::string::npos || s.find("ctx.st[") != std::string::npos ||
        s.find("x87_examine") != std::string::npos || s.find("ctx.mm[") != std::string::npos) {
        X87Flush();
        return s;
    }
    if (s.find("X87_ST(") == std::string::npos && s.find("x87_push(ctx") == std::string::npos &&
        s.find("x87_pop(ctx)") == std::string::npos && s.find("x87_status(ctx)") == std::string::npos)
        return s;
    char buf[128];
    auto need_t0 = [&]() {
        if (!x87_.t0) {
            pre += "\tx87_t0 = ctx.top;";
            x87_.t0 = true;
        }
    };
    // fresh: a push's new slot, written whole without being read; any other write may be conditional (fcmov), so
    // an unloaded slot is loaded first
    auto slot = [&](int k, bool write, bool fresh = false) {
        need_t0();
        int j = (k - x87_.d) & 7;
        if (!(x87_.loaded >> j & 1) && !fresh) {
            std::snprintf(buf, sizeof(buf), " x87_r%d = ctx.st[(x87_t0 + %du) & 7];", j, j);
            pre += buf;
        }
        x87_.loaded |= uint8_t(1u << j);
        if (write) x87_.dirty |= uint8_t(1u << j);
        std::snprintf(buf, sizeof(buf), "x87_r%d", j);
        return std::string(buf);
    };
    std::string out;
    size_t i = 0, n = s.size();
    auto starts = [&](const char* p) { return s.compare(i, std::strlen(p), p) == 0; };
    while (i < n) {
        if (starts("X87_ST(")) {
            size_t a = i + 7, b = s.find(')', a);
            int k = std::atoi(s.substr(a, b - a).c_str());
            // written when an assignment follows (not ==)
            size_t c = b + 1;
            while (c < n && s[c] == ' ') c++;
            bool write = c + 1 < n && s[c] == '=' && s[c + 1] != '=';
            out += slot(k, write);
            i = b + 1;
        } else if (starts("x87_pop(ctx);")) {
            x87_.d--;
            i += 13;
        } else if (starts("x87_status(ctx)")) {
            if (x87_.t0) {
                std::snprintf(buf, sizeof(buf), "(uint16_t)((ctx.fsw & ~0x3800) | (((x87_t0 + %du) & 7) << 11))", (8 - (x87_.d & 7)) & 7);
                out += buf;
            } else {
                out += "x87_status(ctx)";
            }
            i += 15;
        } else if (starts("x87_push(ctx, ")) {
            size_t a = i + 14, depth = 1, b = a;
            for (; b < n && depth; b++) {
                if (s[b] == '(') depth++;
                else if (s[b] == ')') depth--;
            }
            // b is past the closing parenthesis
            std::string inner = s.substr(a, b - 1 - a), inner_pre;
            std::string expr = X87Rewrite(inner, inner_pre);
            pre += inner_pre;
            x87_.d++;
            std::string dst = slot(0, true, true);
            out += dst + " = (" + expr + ")";
            i = b;
        } else {
            out += s[i++];
        }
    }
    return out;
}
