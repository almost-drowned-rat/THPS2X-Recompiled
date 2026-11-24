#!/usr/bin/env python3
"""ctx_rewrite.py - move a port's hand-written C from global guest registers to option-c's register context.

option-c ("option C") hands every guest function the caller's register set: `void sub_X(RECOMP_PARAMS)` with
RECOMP_PARAMS = `X86Ctx *__restrict ctx, uint8_t *base`, calls pass RECOMP_ARGS (`ctx, base`), and g_eax .. g_edi,
g_esp, g_seh_ebp, g_fp_* ... are macros for fields of `ctx` (toolkit src/kernel/xbox_memory_layout.h). A port's
manual code written for the global register set (vulkan-backend and older) has to thread `ctx` through every
function that reads or writes a register, or calls one that does. Doing that by hand over thousands of lines is
slow and error-prone; this script does the mechanical part and lists what needs a person.

What it does (all files together, so a helper in one file called from another is found):
  1. Tokenises each C file (comments, strings, char literals and preprocessor lines are kept verbatim, never
     rewritten).
  2. Collects function definitions and prototypes at file scope.
  3. Marks a function as "needs ctx" when it
       - is a guest function or override: sub_XXXXXXXX or sub_XXXXXXXX_gen,
       - is an instruction hook named in the hooks files given (--hooks, `call = "name"`),
       - is a toolkit function that takes RECOMP_PARAMS in the target toolkit (--toolkit: its headers are scanned
         for prototypes with RECOMP_PARAMS),
       - uses a register name in its body (g_eax ... g_edi, g_esp, g_ebp, g_seh_ebp, g_fs_base, g_df, g_fp_*,
         g_mxcsr, g_xmm0-7, g_mm0-7, g_call_flags, g_eflags_sys),
       - or calls a function that needs ctx (repeated until nothing changes).
  4. Rewrites, for project functions that need ctx: the parameter list of each definition and prototype
     (`(void)` / `()` -> `(RECOMP_PARAMS)`, `(int a)` -> `(RECOMP_PARAMS, int a)`), and every call of a function
     that needs ctx (`f()` -> `f(RECOMP_ARGS)`, `f(x)` -> `f(RECOMP_ARGS, x)`). Calls of toolkit functions that take
     ctx get RECOMP_ARGS too; their prototypes are the toolkit's.
  5. Removes file-scope `extern` declarations of register globals (`extern uint32_t g_eax;` would expand the macro
     into `extern uint32_t (ctx->r_eax);`), replacing each with a comment.

What it does NOT decide (listed under "REVIEW" in the report, nothing is changed there):
  - a function that needs ctx but whose address is taken (callback, table, function pointer): its pointer type has
    to fit; a toolkit callback of a fixed signature cannot get ctx this way;
  - a function that needs ctx and is called from a function with no way to get one (host-side code such as a
    crash handler, a thread entry, an SDL/Win32 callback): flagged as "entry without ctx";
  - macros that use registers (a #define body mentioning g_eax is left as it is; it works where ctx is in scope);
  - since 2026-11-19: a #define whose body defines or calls guest / ctx functions with an empty parameter list, a
    function-pointer typedef with an empty list (a guest pointer such as recomp_func_t takes RECOMP_PARAMS; Vulkan
    PFN-style typedefs are listed too and stay as they are), and a call through a function's own parameter with no
    arguments (`fn()`: the pointer's type decides).

Usage:
  python -I tools/ctx_rewrite.py --toolkit <option-c worktree> --hooks config/hooks.toml [--write] src/*.c
Without --write it only prints the report (dry run). With --write each changed file is rewritten in place (same
line endings); run it on a clean git tree so `git diff` shows exactly what it did. Exit code 0; 2 on a parse error.

Written for THPS 2X 2.0 (2026-11-18: scripted, reusable for later ports).
"""
import argparse
import os
import re
import sys

REG_NAMES = set(
    ["g_eax", "g_ecx", "g_edx", "g_ebx", "g_esp", "g_ebp", "g_esi", "g_edi", "g_seh_ebp", "g_fs_base",
     "g_eflags_sys", "g_call_flags", "g_df", "g_fp_stack", "g_fp_top", "g_fp_cmp", "g_fp_control_word", "g_fp_cc",
     "g_mxcsr"] + ["g_xmm%d" % i for i in range(8)] + ["g_mm%d" % i for i in range(8)])
GUEST_FN = re.compile(r"^sub_[0-9A-Fa-f]{8}(_gen)?$")
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "do", "else", "case", "goto", "defined",
            "_Alignof", "__alignof__", "alignof", "__attribute__", "__declspec", "_Static_assert", "typeof",
            "__typeof__", "_Generic"}

TOKEN_RE = re.compile(r"""
    (?P<ws>[ \t\r\n]+)
  | (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<pp>(?<![^\n])[ \t]*\#(?:[^\n\\]|\\\r?\n|\\.)*)
  | (?P<str>"(?:[^"\\\n]|\\.)*")
  | (?P<chr>'(?:[^'\\\n]|\\.)*')
  | (?P<id>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<num>\.?[0-9](?:[A-Za-z0-9_.]|[eEpP][+-])*)
  | (?P<op>->|\+\+|--|<<=|>>=|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^!~<>=?:;,.(){}\[\]])
""", re.S | re.X)


class Tok:
    __slots__ = ("kind", "text")

    def __init__(self, kind, text):
        self.kind = kind
        self.text = text


def tokenize(src, path):
    toks = []
    pos = 0
    n = len(src)
    while pos < n:
        m = TOKEN_RE.match(src, pos)
        if not m:
            raise SyntaxError("%s: cannot tokenise at offset %d: %r" % (path, pos, src[pos:pos + 40]))
        toks.append(Tok(m.lastgroup, m.group()))
        pos = m.end()
    return toks


def sig(toks):
    """indices of the significant tokens (not whitespace, comments, preprocessor lines)"""
    return [i for i, t in enumerate(toks) if t.kind not in ("ws", "comment", "pp")]


def match_paren(toks, s, k):
    """s = significant index list, k = position in s of '(' (or '{'); returns the position in s of its partner"""
    open_t = toks[s[k]].text
    close_t = {"(": ")", "{": "}", "[": "]"}[open_t]
    depth = 0
    for j in range(k, len(s)):
        t = toks[s[j]].text
        if t == open_t:
            depth += 1
        elif t == close_t:
            depth -= 1
            if depth == 0:
                return j
    raise SyntaxError("unbalanced %s" % open_t)


class FileInfo:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            raw = f.read()
        self.crlf = b"\r\n" in raw
        self.src = raw.decode("utf-8")
        self.toks = tokenize(self.src, path)
        self.s = sig(self.toks)
        self.defs = {}     # name -> (k_name, k_lparen, k_rparen, k_lbrace, k_rbrace) positions in self.s
        self.protos = []   # (name, k_name, k_lparen, k_rparen) file-scope prototypes
        self.reg_externs = []   # (k_first, k_semicolon) file-scope extern declarations of register globals
        self.scan()

    def t(self, k):
        return self.toks[self.s[k]].text

    def kind(self, k):
        return self.toks[self.s[k]].kind

    def scan(self):
        s = self.s
        k = 0
        stmt_start = 0
        while k < len(s):
            t = self.t(k)
            if t == "{":
                # file-scope brace: a function body if the previous tokens are ident ( ... ) [attrs]
                end = match_paren(self.toks, s, k)
                fn = self.function_head(stmt_start, k)
                if fn:
                    name, kn, kl, kr = fn
                    self.defs[name] = (kn, kl, kr, k, end)
                k = end + 1
                # a struct/union/enum body is followed by declarators up to ';'
                if not fn:
                    while k < len(s) and self.t(k) != ";":
                        k += 1
                    k += 1
                stmt_start = k
                continue
            if t == ";":
                self.declaration(stmt_start, k)
                stmt_start = k + 1
            elif t == "(":
                k = match_paren(self.toks, s, k)
            elif t == "=":
                # initialiser: skip to the ';' (braces of an initialiser list included)
                j = k + 1
                while j < len(s) and self.t(j) != ";":
                    if self.t(j) in "({[":
                        j = match_paren(self.toks, s, j)
                    j += 1
                self.declaration(stmt_start, j)
                k = j
                stmt_start = k + 1
            k += 1

    def function_head(self, a, b):
        """tokens a..b-1 = what precedes a file-scope '{'. A function definition: ... name ( params ) [attrs]"""
        j = b - 1
        # skip trailing __attribute__((...)) or similar
        while j > a and self.t(j) == ")" and not self.is_param_close(a, j):
            j = self.open_of(a, j) - 1
        if j <= a or self.t(j) != ")":
            return None
        kl = self.open_of(a, j)
        if kl is None or kl - 1 < a or self.kind(kl - 1) != "id":
            return None
        name = self.t(kl - 1)
        if name in KEYWORDS:
            return None
        return (name, kl - 1, kl, j)

    def is_param_close(self, a, j):
        kl = self.open_of(a, j)
        return kl is not None and kl - 1 >= a and self.kind(kl - 1) == "id" and \
            self.t(kl - 1) not in ("__attribute__", "__declspec")

    def open_of(self, a, j):
        depth = 0
        for i in range(j, a - 1, -1):
            if self.t(i) == ")":
                depth += 1
            elif self.t(i) == "(":
                depth -= 1
                if depth == 0:
                    return i
        return None

    def declaration(self, a, b):
        """a file-scope declaration a..b-1 (b = the ';')"""
        if a >= b:
            return
        texts = [self.t(i) for i in range(a, b)]
        ids = [self.t(i) for i in range(a, b) if self.kind(i) == "id"]
        if "extern" in texts and ids and "(" not in texts:
            declared = [x for x in ids if x.startswith("g_")]
            if declared and all(x in REG_NAMES for x in declared):
                self.reg_externs.append((a, b))
                return
        # prototype: ... name ( params ) ;  (not a function-pointer declaration "(*name)(...)")
        j = b - 1
        while j > a and self.t(j) == ")" and not self.is_param_close(a, j):
            j = self.open_of(a, j) - 1
        if j > a and self.t(j) == ")":
            kl = self.open_of(a, j)
            if kl and kl - 1 >= a and self.kind(kl - 1) == "id" and self.t(kl - 1) not in KEYWORDS:
                if not (kl - 2 >= a and self.t(kl - 2) == "*"):
                    self.protos.append((self.t(kl - 1), kl - 1, kl, j))


KEEP_MARK = "ctx_rewrite: keep"


def has_keep_mark(fi, name):
    """a comment `ctx_rewrite: keep` in the body or right before the definition: a person fixed this function's
    signature by hand (a callback of a fixed signature); a re-run leaves its parameter list alone"""
    kn, kl, kr, kb, ke = fi.defs[name]
    lo = fi.s[kn - 1] + 1 if kn > 0 else 0
    for i in range(lo, fi.s[ke]):
        if fi.toks[i].kind == "comment" and KEEP_MARK in fi.toks[i].text:
            return True
    return False


def body_facts(fi, name):
    """registers used?, set of called names, set of names whose address is taken (id not followed by '(')"""
    kn, kl, kr, kb, ke = fi.defs[name]
    uses_reg = False
    calls = set()
    refs = set()
    for k in range(kb + 1, ke):
        if fi.kind(k) != "id":
            continue
        t = fi.t(k)
        if t in REG_NAMES:
            uses_reg = True
            continue
        prev = fi.t(k - 1) if k > 0 else ""
        if prev in (".", "->"):
            continue
        nxt = fi.t(k + 1) if k + 1 < len(fi.s) else ""
        if nxt == "(":
            calls.add(t)
        else:
            refs.add(t)
    return uses_reg, calls, refs


def toolkit_ctx_functions(root):
    """(names taking RECOMP_PARAMS, names taking only `X86Ctx *ctx` first) from the toolkit's headers, including
    the runtime header the lifter's output includes (templates/runtime/recomp_types.h): the ICALL failure hooks,
    recomp_unimpl and the trace hooks a port defines are called by generated code with ctx only, no base."""
    full, ctx_only = set(), set()
    pat = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*RECOMP_PARAMS\b")
    pat_ctx = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*X86Ctx\s*\*\s*(?:__restrict\s+)?ctx\s*[,)]")
    dirs = [os.path.join(root, "src"), os.path.join(root, "templates", "runtime")]
    for top in dirs:
        for d, _, files in os.walk(top):
            for fn in files:
                if fn.endswith(".h"):
                    try:
                        with open(os.path.join(d, fn), encoding="utf-8", errors="replace") as f:
                            text = f.read()
                    except OSError:
                        continue
                    full.update(m.group(1) for m in pat.finditer(text))
                    ctx_only.update(m.group(1) for m in pat_ctx.finditer(text))
    full.discard("recomp_func_t")
    ctx_only -= full
    return full, ctx_only


def hook_names(paths):
    names = set()
    for p in paths:
        with open(p, encoding="utf-8") as f:
            for m in re.finditer(r'^\s*call\s*=\s*"([A-Za-z_][A-Za-z0-9_]*)"', f.read(), re.M):
                names.add(m.group(1))
    return names


# identifiers that may stand right before a call (so `id name(` after them is a call, not a declaration)
DECL_NOT_BEFORE = {"return", "else", "do", "case", "goto", "sizeof", "defined"}

FNPTR_TYPEDEF = re.compile(r"\btypedef\s+[^;{}]*?\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\(\s*(void)?\s*\)\s*;")
MACRO_GUEST = re.compile(r"\bsub_##|\b(sub_[0-9A-Fa-f]{8}(?:_gen)?|[A-Za-z_]\w*)\s*\(\s*(void)?\s*\)")


def pointer_and_macro_review(infos, ctx_names):
    """What the token rewrite cannot see (added 11-19 after THPS's first review): a function-pointer typedef with
    an empty parameter list (a guest function pointer such as recomp_func_t must take RECOMP_PARAMS); a call through
    a function's own parameter (`fn()` where fn is a parameter: its pointer type decides); a #define whose body
    defines or calls guest / ctx functions with an empty list (macro bodies are preprocessor lines, never rewritten)."""
    out = []
    for fi in infos:
        base_name = os.path.basename(fi.path)
        line_of = lambda off: fi.src.count("\n", 0, off) + 1
        for m in FNPTR_TYPEDEF.finditer(fi.src):
            out.append("%s:%d: function-pointer typedef %s() without parameters - a guest function pointer takes "
                       "RECOMP_PARAMS" % (base_name, line_of(m.start()), m.group(1)))
        pos = 0
        for t in fi.toks:
            if t.kind == "pp" and re.match(r"\s*#\s*define\b", t.text):
                hits = set()
                # a guest function pasted together (sub_##addr / sub_##addr##_gen) with an empty list; a macro
                # already on RECOMP_PARAMS is quiet
                empty = re.search(r"sub_##[\w#]*\s*\(\s*(void)?\s*\)", t.text)
                for m in MACRO_GUEST.finditer(t.text):
                    if (m.group(0).startswith("sub_##") and empty) or (m.group(1) in ctx_names):
                        hits.add(m.group(0).strip())
                if hits:
                    out.append("%s:%d: macro body with guest / ctx calls (%s) - rewrite the #define by hand"
                               % (base_name, line_of(pos), ", ".join(sorted(hits))))
            pos += len(t.text)
        for name, (kn, kl, kr, kb, ke) in fi.defs.items():
            params = set()
            for k in range(kl + 1, kr):
                # the last identifier before each ',' / ')' at depth 0 of the list is a parameter name
                if fi.kind(k) == "id" and fi.t(k + 1) in (",", ")"):
                    params.add(fi.t(k))
                # a function-pointer parameter: `void (*fn)(...)`
                if fi.t(k) == "(" and fi.t(k + 1) == "*" and fi.kind(k + 2) == "id":
                    params.add(fi.t(k + 2))
            for k in range(kb + 1, ke):
                if fi.kind(k) == "id" and fi.t(k) in params and fi.t(k + 1) == "(" and fi.t(k + 2) == ")":
                    out.append("%s: in %s(): call through parameter %s() with no arguments - if it is a guest "
                               "function pointer it needs RECOMP_ARGS" % (base_name, name, fi.t(k)))
    return out


def shadow_review(infos, ctx_fns):
    """A function that gets RECOMP_PARAMS must not declare its own `ctx` or `base` (parameter or local): the
    register macros expand to ctx->..., guest memory access uses base. Found by THPS's first option-c build
    (sub_0006F0D0: `uint32_t cam = g_ecx, base, ours;`). Listed, not renamed: the right new name is a person's call."""
    out = []
    for fi in infos:
        for name, (kn, kl, kr, kb, ke) in fi.defs.items():
            if name not in ctx_fns:
                continue
            for k in list(range(kl + 1, kr)) + list(range(kb + 1, ke)):
                if fi.kind(k) == "id" and fi.t(k) in ("ctx", "base") and fi.t(k + 1) in (",", ";", "=", "[", ")"):
                    prev = fi.t(k - 1)
                    # a declarator: after a type name / '*' / ',' inside a declaration - not after an operator
                    if (fi.kind(k - 1) == "id" and prev not in DECL_NOT_BEFORE and prev != "RECOMP_PARAMS") or \
                            (prev == "*" and fi.kind(k - 2) == "id") or (prev == "," and decl_comma(fi, k - 1, kb)):
                        out.append("%s: in %s(): own `%s` declared - clashes with RECOMP_PARAMS (rename it)"
                                   % (os.path.basename(fi.path), name, fi.t(k)))
    return out


def decl_comma(fi, kc, kb):
    """True when the ',' at kc belongs to a declaration statement (the statement starts with a type name followed by
    an identifier: `uint32_t a = 1, base;`) rather than an expression or an argument list."""
    j = kc
    depth = 0
    while j > kb:
        t = fi.t(j)
        if t == ")":
            depth += 1
        elif t == "(":
            if depth == 0:
                return False          # inside a call's / prototype's parentheses
            depth -= 1
        elif t in (";", "{", "}") and depth == 0:
            break
        j -= 1
    return fi.kind(j + 1) == "id" and fi.kind(j + 2) == "id" and fi.t(j + 1) not in DECL_NOT_BEFORE


def param_text(fi, kl, kr):
    return "".join(fi.toks[i].text for i in range(fi.s[kl] + 1, fi.s[kr])).strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--toolkit", required=True, help="the target option-c toolkit worktree")
    ap.add_argument("--hooks", action="append", default=[], help="hooks.toml / game.toml with call = \"name\"")
    ap.add_argument("--write", action="store_true", help="rewrite the files in place (default: report only)")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()

    try:
        infos = [FileInfo(p) for p in a.files]
    except SyntaxError as e:
        print("ctx_rewrite: %s" % e, file=sys.stderr)
        return 2
    tk, tk_ctx = toolkit_ctx_functions(a.toolkit)
    hooks = hook_names(a.hooks)

    defined = {}
    for fi in infos:
        for name in fi.defs:
            defined.setdefault(name, []).append(fi)
    facts = {}
    for fi in infos:
        for name in fi.defs:
            facts[(fi.path, name)] = body_facts(fi, name)

    need = set(n for n in defined if GUEST_FN.match(n) or n in hooks)
    for (path, name), (uses_reg, calls, refs) in facts.items():
        if uses_reg:
            need.add(name)
    # guest functions declared but defined elsewhere (generated _gen, lifted sub_X) also take ctx
    for fi in infos:
        for (name, _, _, _) in fi.protos:
            if GUEST_FN.match(name):
                need.add(name)
    changed = True
    while changed:
        changed = False
        for (path, name), (uses_reg, calls, refs) in facts.items():
            if name not in need and (calls & (need | tk | tk_ctx)):
                need.add(name)
                changed = True

    # a project definition of a ctx-only hook (generated code calls it as name(ctx, ...)) gets exactly that
    # signature, not RECOMP_PARAMS; functions marked `ctx_rewrite: keep` keep theirs (their callers are not
    # changed either: they are callbacks, not called by name)
    keep = set(n for fi in infos for n in fi.defs if has_keep_mark(fi, n))
    project_need = need - tk - tk_ctx - keep
    need -= keep
    review = []
    for (path, name), (uses_reg, calls, refs) in facts.items():
        for r in sorted(refs & project_need):
            if not GUEST_FN.match(r):
                review.append("%s: in %s(): address of %s() taken (needs ctx) - check the pointer type / caller"
                              % (os.path.basename(path), name, r))
    entries = sorted(n for n in project_need if n in defined and not GUEST_FN.match(n) and n not in hooks and
                     not any(n in facts[(fi.path, m)][1] for fi in infos for m in fi.defs))
    for n in entries:
        review.append("entry without a ctx-taking caller: %s() (%s) - who calls it? host code needs a ctx source"
                      % (n, ", ".join(os.path.basename(f.path) for f in defined[n])))
    review += pointer_and_macro_review(infos, need | tk | tk_ctx)
    review += shadow_review(infos, project_need)

    total_defs = total_calls = total_protos = total_ext = 0
    for fi in infos:
        edits = []   # (start_char, end_char, new_text)
        offs = []
        pos = 0
        for t in fi.toks:
            offs.append(pos)
            pos += len(t.text)

        def span(k1, k2):   # chars strictly inside parens k1..k2
            return offs[fi.s[k1]] + 1, offs[fi.s[k2]]

        def ctx_only_sig(kl, kr):
            """edit for a ctx-only hook's parameter list (definition or prototype), or None if already right"""
            p = param_text(fi, kl, kr)
            if p.startswith("X86Ctx"):
                return None
            st, en = span(kl, kr)
            if p.startswith("RECOMP_PARAMS"):
                return (st, en, "\0X86Ctx *ctx" + p[len("RECOMP_PARAMS"):])
            return (st, en, "X86Ctx *ctx" if p in ("", "void") else "X86Ctx *ctx, " + p)

        for name, (kn, kl, kr, kb, ke) in fi.defs.items():
            if name in tk_ctx:
                e = ctx_only_sig(kl, kr)
                if e:
                    edits.append(e)
                    total_defs += 1
            if name in project_need:
                p = param_text(fi, kl, kr)
                if "RECOMP_PARAMS" not in p:
                    st, en = span(kl, kr)
                    edits.append((st, en, "RECOMP_PARAMS" if p in ("", "void") else "RECOMP_PARAMS, " + p))
                    total_defs += 1
            for k in range(kb + 1, ke):
                # `need` holds project functions only; toolkit functions that take ctx are called with it as well
                # (3ff289a and before checked `need` alone and left every toolkit call as it was - found 11-19:
                # xdk_DSB_* / xdk_XInput* / xdk_music_Release() in THPS)
                callee = fi.t(k)
                if fi.kind(k) == "id" and (callee in need or callee in tk or callee in tk_ctx) and \
                        callee not in keep and fi.t(k + 1) == "(" and fi.t(k - 1) not in (".", "->"):
                    kr2 = match_paren(fi.toks, fi.s, k + 1)
                    p = param_text(fi, k + 1, kr2)
                    st, en = span(k + 1, kr2)
                    if fi.kind(k - 1) == "id" and fi.t(k - 1) not in DECL_NOT_BEFORE:
                        # a block-scope prototype (`extern void sub_X_gen(void);` inside a body): a type name comes
                        # before the name, never before a call. It gets the parameter list, not the arguments
                        # (3ff289a..0c82b0c wrote RECOMP_ARGS into such declarations - found by the first build)
                        rest = p
                        if rest.startswith("RECOMP_ARGS"):
                            rest = rest[len("RECOMP_ARGS"):].lstrip()
                            rest = rest[1:].strip() if rest.startswith(",") else rest
                        if rest.startswith("RECOMP_PARAMS") or rest.startswith("X86Ctx"):
                            continue
                        if fi.t(k) in tk_ctx:
                            new = "X86Ctx *ctx" if rest in ("", "void") else "X86Ctx *ctx, " + rest
                        else:
                            new = "RECOMP_PARAMS" if rest in ("", "void") else "RECOMP_PARAMS, " + rest
                        if new != p:
                            edits.append((st, en, "\0" + new))   # literal: a declaration holds no calls
                            total_protos += 1
                        continue
                    if fi.t(k) in tk_ctx:              # ctx-only hook: name(ctx, ...)
                        if p.startswith("RECOMP_ARGS"):
                            edits.append((st, en, "\0ctx" + p[len("RECOMP_ARGS"):]))
                            total_calls += 1
                        elif not p.startswith("ctx"):
                            edits.append((st, en, "ctx" if p == "" else "ctx, " + p))
                            total_calls += 1
                        continue
                    if p.startswith("RECOMP_ARGS") or p.startswith("ctx"):
                        continue
                    edits.append((st, en, "RECOMP_ARGS" if p == "" else "RECOMP_ARGS, " + p))
                    total_calls += 1
        for (name, kn, kl, kr) in fi.protos:
            if name in tk_ctx:
                e = ctx_only_sig(kl, kr)
                if e:
                    edits.append(e)
                    total_protos += 1
                continue
            if name in project_need:
                p = param_text(fi, kl, kr)
                if "RECOMP_PARAMS" not in p:
                    st, en = span(kl, kr)
                    edits.append((st, en, "RECOMP_PARAMS" if p in ("", "void") else "RECOMP_PARAMS, " + p))
                    total_protos += 1
        for (k1, k2) in fi.reg_externs:
            st = offs[fi.s[k1]]
            en = offs[fi.s[k2]] + 1
            edits.append((st, en, "/* register globals: option-c names them as fields of ctx (xbox_memory_layout.h) */"))
            total_ext += 1

        # inner edits (calls inside a call's arguments) are applied first: sort by start descending, and an edit
        # that contains another keeps the other's result because we rebuild from the innermost outwards
        edits.sort(key=lambda e: (e[0], -e[1]))
        out = fi.src
        applied = []        # (start, length change) of edits already made - all start at or after this one
        for st, en, new in sorted(edits, key=lambda e: e[0], reverse=True):
            # an edit inside this one (a call in this call's arguments) moved this one's end
            en += sum(d for s2, d in applied if st <= s2 < en)
            inner_new = new
            # the original inside text may already contain edited calls: re-take it from the current string
            old_inside = out[st:en]
            stripped = old_inside.strip()
            if new.startswith("\0"):                   # literal replacement (a ctx-only hook fixed up)
                inner_new = new[1:]
            elif new.startswith(("RECOMP_ARGS, ", "RECOMP_PARAMS, ", "X86Ctx *ctx, ", "ctx, ")):
                prefix = new.split(", ", 1)[0]
                inner_new = prefix + ", " + stripped
            elif new.startswith("/*"):
                inner_new = new
            applied.append((st, len(inner_new) - (en - st)))
            out = out[:st] + inner_new + out[en:]
        if out != fi.src:
            print("%s: changed" % fi.path)
            if a.write:
                data = out if not fi.crlf else out.replace("\r\n", "\n").replace("\n", "\r\n")
                with open(fi.path, "wb") as f:
                    f.write(data.encode("utf-8"))

    print("needs ctx: %d project functions (%d toolkit functions with ctx known, %d hooks)"
          % (len(project_need & set(defined)), len(tk), len(hooks & set(defined))))
    print("rewritten: %d definitions, %d prototypes, %d calls, %d register externs%s"
          % (total_defs, total_protos, total_calls, total_ext, "" if a.write else " (dry run)"))
    print("REVIEW (%d):" % len(review))
    for r in review:
        print("  " + r)
    return 0


if __name__ == "__main__":
    sys.exit(main())
