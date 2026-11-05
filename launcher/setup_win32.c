/* setup_win32.c -- the setup window of "THPS 2X Recomp.exe" in the look of the Ninja Gaiden 2 launcher
 * (maintainer 2026-11-05: THPS 2X takes over the launcher rework of Panzer Dragoon Orta Recompiled, which follows
 * NinjaGaiden2-Recompiled launcher/ng2_launcher.cpp): Windows 95 style, drawn with plain GDI - grey #C0C0C0 face, 3D
 * bevels, a navy title bar with white bold text, MS Sans Serif (a system font, not shipped), a blocky progress bar.
 * Only the style; no Microsoft graphics.
 *
 * THPS 2X needs two lines (no patches, no DLC): the disc image and the folder to install to, then Go. Go copies this
 * exe into that folder and runs the copy there with the toolkit's launcher (--image <iso> --no-start
 * --setup-parent <window>): it installs the game, unpacks the payload, translates, compiles and links, all inside
 * that folder (toolkit src/launcher, unchanged). The copy reports its progress into this window (WM_COPYDATA), so
 * the player sees one window from Go to Play. Later starts of the installed exe start the game at once.
 *
 * The same file supplies the toolkit's progress UI (game_setup_ui.h: xr_setup_ui_*) in this look, so the toolkit's
 * SDL debug-text window never shows in this launcher: with a parent window the progress goes there, without one
 * (a repair, an image put into game\ by hand) a small window of the same style opens. All six functions of
 * game_setup_ui.c are defined here, so the linker never pulls that archive member (a new function there would show
 * as a duplicate symbol at link time - then add it here).
 *
 * Windows only (the launcher is the Windows release's exe); plain Win32 + GDI, works under Wine. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "game_setup_ui.h"
#include "setup_win32.h"
#include "vk_check.h"

#define SETUP_TITLE L"THPS 2X Recomp Setup"
#define SETUP_EXE   L"THPS 2X Recomp.exe"
#define SETUP_DIR   L"THPS 2X Recomp"           /* the suggested install folder's name */

/* Windows 95 palette (NG2 launcher) */
#define C_FACE       RGB(192, 192, 192)
#define C_LIGHT      RGB(255, 255, 255)
#define C_SHADOW     RGB(128, 128, 128)
#define C_DARK       RGB(0, 0, 0)
#define C_TITLE      RGB(0, 0, 128)
#define C_TITLE_TEXT RGB(255, 255, 255)
#define C_TEXT       RGB(0, 0, 0)

#define TITLE_H 18
#define MSG_PROGRESS (WM_APP + 1)
#define MSG_DONE     (WM_APP + 2)

enum { ID_ISO = 100, ID_TARGET, ID_BROWSE_ISO = 200, ID_BROWSE_TARGET, ID_GO = 300, ID_QUIT };

static HFONT  g_font, g_bold, g_marlett;
static HBRUSH g_face_brush;

/* ── drawing (NG2 launcher Bevel / Text / DrawButton, in C) ───────────────────────────────────────────────────── */

static void line_c(HDC dc, int x0, int y0, int x1, int y1, COLORREF c)
{
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x0, y0, NULL);
    LineTo(dc, x1, y1);
    SelectObject(dc, old);
    DeleteObject(pen);
}

/* the classic two-pixel bevel: raised (buttons, the window) or sunken (fields, the progress groove) */
static void bevel(HDC dc, RECT r, int raised)
{
    COLORREF otl = raised ? C_LIGHT : C_SHADOW, obr = raised ? C_DARK : C_LIGHT;
    COLORREF itl = raised ? C_FACE : C_DARK, ibr = raised ? C_SHADOW : C_FACE;
    line_c(dc, r.left, r.bottom - 1, r.left, r.top, otl);
    line_c(dc, r.left, r.top, r.right - 1, r.top, otl);
    line_c(dc, r.right - 1, r.top, r.right - 1, r.bottom - 1, obr);
    line_c(dc, r.right - 1, r.bottom - 1, r.left - 1, r.bottom - 1, obr);
    line_c(dc, r.left + 1, r.bottom - 2, r.left + 1, r.top + 1, itl);
    line_c(dc, r.left + 1, r.top + 1, r.right - 2, r.top + 1, itl);
    line_c(dc, r.right - 2, r.top + 1, r.right - 2, r.bottom - 2, ibr);
    line_c(dc, r.right - 2, r.bottom - 2, r.left, r.bottom - 2, ibr);
}

static void text_at(HDC dc, int x, int y, const wchar_t *s, HFONT font, COLORREF color)
{
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOutW(dc, x, y, s, (int)wcslen(s));
    SelectObject(dc, old);
}

/* One window class for both uses: the setup window (two input lines) and the progress-only window. */
typedef struct {
    int      inputs;            /* 1: the setup window with its two lines and buttons */
    int      width, height;
    int      progress;          /* 0..1000 */
    wchar_t  status[1024];
    int      close_down;
    int      closed;            /* the progress-only window: closing it cancels */
    int      running;           /* the setup window: the build runs */
    wchar_t  installed[MAX_PATH];   /* after a successful build: Go becomes Play */
    HWND     edits[2];
    HANDLE   child;
} Win;

static void paint(HWND hwnd, Win *w)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client, title, close, status, groove;
    HBRUSH b;
    int top;

    GetClientRect(hwnd, &client);
    FillRect(dc, &client, g_face_brush);
    bevel(dc, client, 1);
    /* title bar: solid navy, the icon left, white bold caption, the close box right */
    SetRect(&title, 3, 3, client.right - 3, 3 + TITLE_H);
    b = CreateSolidBrush(C_TITLE);
    FillRect(dc, &title, b);
    DeleteObject(b);
    {
        HICON icon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
        if (icon)
            DrawIconEx(dc, title.left + 2, title.top + 1, icon, 16, 16, 0, NULL, DI_NORMAL);
    }
    text_at(dc, title.left + 21, title.top + 2, SETUP_TITLE, g_bold, C_TITLE_TEXT);
    SetRect(&close, title.right - 18, title.top + 2, title.right - 2, title.top + 16);
    FillRect(dc, &close, g_face_brush);
    bevel(dc, close, !w->close_down);
    text_at(dc, close.left + 3 + w->close_down, close.top + 1 + w->close_down, L"r", g_marlett, C_TEXT);

    top = 30;
    if (w->inputs) {
        text_at(dc, 14, 40, L"Disc image (.iso):", g_font, C_TEXT);
        text_at(dc, 14, 74, L"Install to:", g_font, C_TEXT);
        top = 110;
        /* an etched group line above the progress area */
        line_c(dc, 10, top, client.right - 10, top, C_SHADOW);
        line_c(dc, 10, top + 1, client.right - 10, top + 1, C_LIGHT);
        top += 10;
    }
    SetRect(&status, 14, top, client.right - 14, top + 32);
    {
        HGDIOBJ old = SelectObject(dc, g_font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, C_TEXT);
        DrawTextW(dc, w->status, -1, &status, DT_WORDBREAK);
        SelectObject(dc, old);
    }
    SetRect(&groove, 14, top + 38, client.right - 14, top + 58);
    FillRect(dc, &groove, g_face_brush);
    bevel(dc, groove, 0);
    {
        /* blocks of ~10 px + 2 px gap spread over the whole inner width, so a full bar ends at the groove's edge */
        int inner_l = groove.left + 3, inner_w = groove.right - groove.left - 6, gap = 2;
        int slots = (inner_w + gap) / (10 + gap), blocks, k;
        int p = w->progress < 0 ? 0 : w->progress > 1000 ? 1000 : w->progress;
        if (slots < 1)
            slots = 1;
        blocks = (slots * p + 500) / 1000;
        b = CreateSolidBrush(C_TITLE);
        for (k = 0; k < blocks; k++) {
            RECT r;
            SetRect(&r, inner_l + k * (inner_w + gap) / slots, groove.top + 3,
                    inner_l + (k + 1) * (inner_w + gap) / slots - gap, groove.bottom - 3);
            FillRect(dc, &r, b);
        }
        DeleteObject(b);
    }
    EndPaint(hwnd, &ps);
}

/* owner-drawn push button: raised, sunken while pressed, a dotted focus rectangle like the original */
static void draw_button(const DRAWITEMSTRUCT *d)
{
    RECT r = d->rcItem, t;
    int down = (d->itemState & ODS_SELECTED) != 0;
    wchar_t text[64];
    HGDIOBJ old;

    FillRect(d->hDC, &r, g_face_brush);
    if (GetDlgCtrlID(d->hwndItem) == ID_GO) {           /* the default button gets the extra black frame */
        FrameRect(d->hDC, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        InflateRect(&r, -1, -1);
    }
    bevel(d->hDC, r, !down);
    GetWindowTextW(d->hwndItem, text, 64);
    t = r;
    if (down)
        OffsetRect(&t, 1, 1);
    old = SelectObject(d->hDC, g_font);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, IsWindowEnabled(d->hwndItem) ? C_TEXT : C_SHADOW);
    DrawTextW(d->hDC, text, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->hDC, old);
    if (d->itemState & ODS_FOCUS) {
        RECT f = r;
        InflateRect(&f, -4, -4);
        DrawFocusRect(d->hDC, &f);
    }
}

static HWND child_ctl(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int x, int y,
                      int cw, int ch, int id)
{
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, cw, ch, parent,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void set_status(HWND hwnd, Win *w, const wchar_t *s, int progress)
{
    wcsncpy(w->status, s, 1023);
    w->status[1023] = 0;
    if (progress >= 0)
        w->progress = progress;
    InvalidateRect(hwnd, NULL, FALSE);
}

static void field_text(Win *w, int i, wchar_t *out, size_t n)
{
    size_t a = 0, l;
    out[0] = 0;
    GetWindowTextW(w->edits[i], out, (int)n);
    l = wcslen(out);
    while (l && (out[l - 1] == L' ' || out[l - 1] == L'"' || out[l - 1] == L'\\'))
        out[--l] = 0;
    while (out[a] == L' ' || out[a] == L'"')
        a++;
    if (a)
        memmove(out, out + a, (wcslen(out + a) + 1) * sizeof(wchar_t));
}

static void enable_inputs(HWND hwnd, Win *w, int on)
{
    static const int ids[] = { ID_GO, ID_BROWSE_ISO, ID_BROWSE_TARGET };
    size_t i;
    for (i = 0; i < sizeof ids / sizeof ids[0]; i++)
        EnableWindow(GetDlgItem(hwnd, ids[i]), on);
    EnableWindow(w->edits[0], on);
    EnableWindow(w->edits[1], on);
}

static void box(HWND owner, const wchar_t *text, UINT icon)
{
    MessageBoxW(owner, text, SETUP_TITLE, MB_OK | icon);
}

/* The player's media\README.txt, written before the toolkit's launcher runs (it writes its own only when the file is
 * missing): the toolkit's text with the boot animation at its full 1080p60 (maintainer 2026-11-04/05, as the NG2
 * and Orta launchers) - the toolkit text says 720p30. THPS 2X keeps the soundtrack and shader lines. */
static void write_media_readme(const wchar_t *target)
{
    static const char text[] =
        "Your own media (only read, never changed):\r\n\r\n"
        "- Custom soundtracks: every folder below media that holds music (MP3, FLAC, Ogg Vorbis, WAV)\r\n"
        "  is one album. .m4a / .aac files are skipped - convert them to MP3 or Ogg first.\r\n"
        "- Boot animation: media\\boot.mpg (MPEG-1 video + MP2 audio) plays before the game. It cannot be skipped;\r\n"
        "  RECOMP OPTIONS > BOOT ANIMATION switches it off. Convert your clip once with ffmpeg, keeping its full\r\n"
        "  quality (1080p, 60 fps):\r\n"
        "  ffmpeg -i boot.mp4 -c:v mpeg1video -q:v 1 -qmin 1 -bf 2 -g 30 -pix_fmt yuv420p -c:a mp2 -b:a 256k boot.mpg\r\n"
        "- Shaders: RetroArch slang shader packs (zip files) in media\\shaders\\.\r\n";
    wchar_t dir[MAX_PATH + 16], file[MAX_PATH + 32];
    HANDLE h;
    _snwprintf(dir, MAX_PATH + 16, L"%ls\\media", target);
    dir[MAX_PATH + 15] = 0;
    CreateDirectoryW(dir, NULL);
    _snwprintf(file, MAX_PATH + 32, L"%ls\\README.txt", dir);
    file[MAX_PATH + 31] = 0;
    h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);   /* never overwrite */
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(h, text, (DWORD)(sizeof text - 1), &wr, NULL);
        CloseHandle(h);
    }
}

typedef struct { HWND hwnd; HANDLE proc; } WaitArg;

static DWORD WINAPI wait_child(LPVOID p)
{
    WaitArg *a = (WaitArg *)p;
    DWORD code = 1;
    WaitForSingleObject(a->proc, INFINITE);
    GetExitCodeProcess(a->proc, &code);
    PostMessageW(a->hwnd, MSG_DONE, (WPARAM)code, 0);
    free(a);
    return 0;
}

/* Go: the checks, the copy of this exe into the target, the build there. */
static void go(HWND hwnd, Win *w)
{
    wchar_t iso[MAX_PATH * 2], target[MAX_PATH * 2], self[MAX_PATH], dst[MAX_PATH * 2 + 40], cmd[MAX_PATH * 6];
    DWORD attr;
    int rc;

    field_text(w, 0, iso, MAX_PATH * 2);
    field_text(w, 1, target, MAX_PATH * 2);
    attr = iso[0] ? GetFileAttributesW(iso) : INVALID_FILE_ATTRIBUTES;
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        box(hwnd, L"Please select your Tony Hawk's Pro Skater 2X (USA) disc image (.iso).", MB_ICONSTOP);
        return;
    }
    if (!target[0]) {
        box(hwnd, L"Please choose the folder to install to.", MB_ICONSTOP);
        return;
    }
    if (wcslen(target) > MAX_PATH - 60) {
        box(hwnd, L"The install folder's path is too long - please choose a shorter one (for example C:\\Games\\THPS2X).",
            MB_ICONSTOP);
        return;
    }
    rc = SHCreateDirectoryExW(hwnd, target, NULL);
    if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS) {
        box(hwnd, L"The install folder cannot be created there. Please choose another folder.", MB_ICONSTOP);
        return;
    }
    GetModuleFileNameW(NULL, self, MAX_PATH);
    _snwprintf(dst, MAX_PATH * 2 + 40, L"%ls\\" SETUP_EXE, target);
    dst[MAX_PATH * 2 + 39] = 0;
    if (_wcsicmp(self, dst) != 0 && !CopyFileW(self, dst, FALSE)) {
        box(hwnd, L"The setup could not copy itself into the install folder. Is the folder writable, and is the game "
                  L"there still running?", MB_ICONSTOP);
        return;
    }
    write_media_readme(target);
    {
        /* the release zip's tools\download\ (the pinned compiler zip, so the first start needs no internet; see
         * tools/release_zip.ps1) goes along into the install folder; without it the toolkit downloads the compiler */
        wchar_t sdir[MAX_PATH], pat[MAX_PATH + 40], from[MAX_PATH + 300], to[MAX_PATH * 2 + 340], *s;
        WIN32_FIND_DATAW fd;
        HANDLE f;
        wcscpy(sdir, self);
        s = wcsrchr(sdir, L'\\');
        if (s)
            *s = 0;
        _snwprintf(pat, MAX_PATH + 40, L"%ls\\tools\\download\\*.zip", sdir);
        pat[MAX_PATH + 39] = 0;
        f = _wcsicmp(self, dst) != 0 ? FindFirstFileW(pat, &fd) : INVALID_HANDLE_VALUE;
        if (f != INVALID_HANDLE_VALUE) {
            _snwprintf(to, MAX_PATH * 2 + 340, L"%ls\\tools\\download", target);
            to[MAX_PATH * 2 + 339] = 0;
            SHCreateDirectoryExW(NULL, to, NULL);
            set_status(hwnd, w, L"Copying the compiler into the install folder ...", 0);
            UpdateWindow(hwnd);
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                _snwprintf(from, MAX_PATH + 300, L"%ls\\tools\\download\\%ls", sdir, fd.cFileName);
                from[MAX_PATH + 299] = 0;
                _snwprintf(to, MAX_PATH * 2 + 340, L"%ls\\tools\\download\\%ls", target, fd.cFileName);
                to[MAX_PATH * 2 + 339] = 0;
                CopyFileW(from, to, FALSE);   /* a failed copy only means the toolkit downloads it */
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    _snwprintf(cmd, MAX_PATH * 6, L"\"%ls\" --image \"%ls\" --no-start --setup-parent %llu", dst, iso,
               (unsigned long long)(UINT_PTR)hwnd);
    cmd[MAX_PATH * 6 - 1] = 0;
    {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        WaitArg *a;
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        if (!CreateProcessW(dst, cmd, NULL, NULL, FALSE, 0, NULL, target, &si, &pi)) {
            box(hwnd, L"The setup in the install folder could not be started.", MB_ICONSTOP);
            return;
        }
        CloseHandle(pi.hThread);
        w->child = pi.hProcess;
        a = (WaitArg *)malloc(sizeof *a);
        a->hwnd = hwnd;
        a->proc = pi.hProcess;
        CloseHandle(CreateThread(NULL, 0, wait_child, a, 0, NULL));
    }
    wcsncpy(w->installed, target, MAX_PATH - 1);
    w->installed[MAX_PATH - 1] = 0;
    w->running = 1;
    enable_inputs(hwnd, w, 0);
    set_status(hwnd, w, L"Starting the setup ...", 0);
}

static void play(HWND hwnd, Win *w)
{
    wchar_t exe[MAX_PATH + 40], cmd[MAX_PATH + 48];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    _snwprintf(exe, MAX_PATH + 40, L"%ls\\" SETUP_EXE, w->installed);
    exe[MAX_PATH + 39] = 0;
    _snwprintf(cmd, MAX_PATH + 48, L"\"%ls\"", exe);
    cmd[MAX_PATH + 47] = 0;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, w->installed, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        DestroyWindow(hwnd);
    } else {
        box(hwnd, L"The game could not be started.", MB_ICONSTOP);
    }
}

static wchar_t *pick_file(HWND owner, wchar_t *out, size_t n)
{
    OPENFILENAMEW ofn;
    out[0] = 0;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Disc images (*.iso)\0*.iso\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = out;
    ofn.nMaxFile = (DWORD)n;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    return GetOpenFileNameW(&ofn) ? out : NULL;
}

static wchar_t *pick_folder(HWND owner, wchar_t *out)
{
    BROWSEINFOW bi;
    PIDLIST_ABSOLUTE pidl;
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Folder to install THPS 2X Recomp into";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
        return NULL;
    out[0] = 0;
    SHGetPathFromIDListW(pidl, out);
    CoTaskMemFree(pidl);
    return out[0] ? out : NULL;
}

static int in_close_box(HWND hwnd, LPARAM lp)
{
    RECT c;
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    GetClientRect(hwnd, &c);
    return x >= c.right - 21 && x < c.right - 5 && y >= 5 && y < 19;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Win *w = (Win *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lp)->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_CREATE:
        w = (Win *)((CREATESTRUCTW *)lp)->lpCreateParams;
        if (w->inputs) {
            int i;
            for (i = 0; i < 2; i++) {
                w->edits[i] = child_ctl(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 130,
                                        37 + i * 34, 290, 21, ID_ISO + i);
                child_ctl(hwnd, L"BUTTON", L"Browse...", BS_OWNERDRAW | WS_TABSTOP, 0, 428, 37 + i * 34, 76, 22,
                          ID_BROWSE_ISO + i);
            }
            child_ctl(hwnd, L"BUTTON", L"Go", BS_OWNERDRAW | WS_TABSTOP, 0, w->width - 184, w->height - 40, 80, 24,
                      ID_GO);
            child_ctl(hwnd, L"BUTTON", L"Quit", BS_OWNERDRAW | WS_TABSTOP, 0, w->width - 96, w->height - 40, 80, 24,
                      ID_QUIT);
            {
                /* a suggested install folder: a subfolder next to the downloaded exe */
                wchar_t self[MAX_PATH], *s;
                GetModuleFileNameW(NULL, self, MAX_PATH);
                s = wcsrchr(self, L'\\');
                if (s) {
                    wcscpy(s + 1, SETUP_DIR);
                    SetWindowTextW(w->edits[1], self);
                }
            }
            SetFocus(GetDlgItem(hwnd, ID_BROWSE_ISO));
        }
        return 0;
    case WM_PAINT:
        paint(hwnd, w);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        draw_button((const DRAWITEMSTRUCT *)lp);
        return TRUE;
    case WM_NCHITTEST: {   /* drag by the title bar */
        POINT p;
        RECT c;
        p.x = (short)LOWORD(lp);
        p.y = (short)HIWORD(lp);
        ScreenToClient(hwnd, &p);
        GetClientRect(hwnd, &c);
        if (p.y >= 3 && p.y < 3 + TITLE_H && p.x < c.right - 22)
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN:
        if (in_close_box(hwnd, lp)) {
            w->close_down = 1;
            InvalidateRect(hwnd, NULL, FALSE);
            SetCapture(hwnd);
        }
        return 0;
    case WM_LBUTTONUP:
        if (w->close_down) {
            w->close_down = 0;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            if (in_close_box(hwnd, lp))
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        wchar_t p[MAX_PATH * 2];
        if (id == ID_QUIT)
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        else if (id == ID_BROWSE_ISO && pick_file(hwnd, p, MAX_PATH * 2))
            SetWindowTextW(w->edits[0], p);
        else if (id == ID_BROWSE_TARGET && pick_folder(hwnd, p)) {
            /* a folder picked: install into a new subfolder there unless it is already one of ours or empty */
            wchar_t probe[MAX_PATH * 2 + 40];
            WIN32_FIND_DATAW fd;
            HANDLE f;
            int empty = 1;
            _snwprintf(probe, MAX_PATH * 2 + 40, L"%ls\\*", p);
            probe[MAX_PATH * 2 + 39] = 0;
            f = FindFirstFileW(probe, &fd);
            if (f != INVALID_HANDLE_VALUE) {
                do
                    if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L".."))
                        empty = 0;
                while (empty && FindNextFileW(f, &fd));
                FindClose(f);
            }
            _snwprintf(probe, MAX_PATH * 2 + 40, L"%ls\\" SETUP_EXE, p);
            if (!empty && GetFileAttributesW(probe) == INVALID_FILE_ATTRIBUTES) {
                size_t l = wcslen(p);
                if (l + 30 < MAX_PATH * 2)
                    wcscpy(p + l, l && p[l - 1] == L'\\' ? SETUP_DIR : L"\\" SETUP_DIR);
            }
            SetWindowTextW(w->edits[1], p);
        } else if (id == ID_GO && !w->running) {
            if (w->installed[0] && !w->child)
                play(hwnd, w);
            else
                go(hwnd, w);
        }
        return 0;
    }
    case WM_COPYDATA: {   /* progress from the build in the install folder: dwData = 0..1000, the text UTF-8 */
        const COPYDATASTRUCT *cd = (const COPYDATASTRUCT *)lp;
        wchar_t t[1024];
        int n = 0;
        if (cd->cbData && cd->lpData)
            n = MultiByteToWideChar(CP_UTF8, 0, (const char *)cd->lpData, (int)cd->cbData, t, 1023);
        t[n > 0 ? n : 0] = 0;
        if (n > 0 && t[n - 1] == 0)
            ;
        set_status(hwnd, w, t, (int)cd->dwData);
        return TRUE;
    }
    case MSG_DONE:
        CloseHandle(w->child);
        w->child = NULL;
        w->running = 0;
        if (wp == 0) {
            set_status(hwnd, w, L"THPS 2X Recomp is installed. Press Play to start it - later, start "
                                L"\"" SETUP_EXE L"\" in the install folder.", 1000);
            SetWindowTextW(GetDlgItem(hwnd, ID_GO), L"Play");
            EnableWindow(GetDlgItem(hwnd, ID_GO), TRUE);
            SetFocus(GetDlgItem(hwnd, ID_GO));
        } else {
            w->installed[0] = 0;
            enable_inputs(hwnd, w, 1);
            set_status(hwnd, w, L"Setup stopped. The message said what to do; data\\launcher.log in the install "
                                L"folder has the details.", 0);
        }
        return 0;
    case WM_CLOSE:
        if (w->inputs && w->running) {
            if (MessageBoxW(hwnd, L"Setup is still running. Stop it and quit?", SETUP_TITLE,
                            MB_YESNO | MB_ICONQUESTION) != IDYES)
                return 0;
            if (w->child)
                TerminateProcess(w->child, 1);
        }
        if (!w->inputs) {   /* the progress-only window: closing it cancels the toolkit's job */
            w->closed = 1;
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (w->inputs)
            PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void init_gdi(void)
{
    static int done;
    HDC screen;
    int h;
    WNDCLASSEXW wc;
    if (done)
        return;
    done = 1;
    /* 8 pt MS Sans Serif, the Windows 95 dialog font (a system font) */
    screen = GetDC(NULL);
    h = -MulDiv(8, GetDeviceCaps(screen, LOGPIXELSY), 72);
    ReleaseDC(NULL, screen);
    g_font = CreateFontW(h, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"MS Sans Serif");
    g_bold = CreateFontW(h, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"MS Sans Serif");
    g_marlett = CreateFontW(h, 0, 0, 0, FW_NORMAL, 0, 0, 0, SYMBOL_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Marlett");
    g_face_brush = CreateSolidBrush(C_FACE);
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));   /* IDC_ARROW */
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hbrBackground = g_face_brush;
    wc.lpszClassName = L"Thps2xRecompSetup";
    RegisterClassExW(&wc);
}

static HWND open_window(Win *w)
{
    int x = (GetSystemMetrics(SM_CXSCREEN) - w->width) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - w->height) / 2;
    HWND hwnd;
    init_gdi();
    hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"Thps2xRecompSetup", SETUP_TITLE,
                           WS_POPUP | WS_VISIBLE | WS_MINIMIZEBOX | WS_SYSMENU, x, y, w->width, w->height, NULL, NULL,
                           GetModuleHandleW(NULL), w);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

/* ── the setup window (launcher/main.c: a fresh download, no arguments) ───────────────────────────────────────── */

int thps_vk_gate(void)
{
    char found[1024], msg[1600];
    wchar_t wmsg[1600];
    const char *nd = getenv("RECOMP_NO_DIALOGS");
    if (thps_vk_check(found, sizeof found))
        return 1;
    thps_vk_message(msg, sizeof msg, found);
    if (!(nd && *nd == '1')) {
        MultiByteToWideChar(CP_UTF8, 0, msg, -1, wmsg, 1600);
        MessageBoxW(NULL, wmsg, SETUP_TITLE, MB_OK | MB_ICONSTOP);
    }
    return 0;
}

int thps_setup_window(void)
{
    static Win w;
    MSG m;
    HWND hwnd;

    if (!thps_vk_gate())
        return 1;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    memset(&w, 0, sizeof w);
    w.inputs = 1;
    w.width = 520;
    w.height = 240;
    wcscpy(w.status, L"Choose your Tony Hawk's Pro Skater 2X (USA) disc image and the folder to install into, then "
                     L"press Go. The game is built on this PC once (a few minutes) and played from that folder.");
    hwnd = open_window(&w);
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    CoUninitialize();
    return 0;
}

/* ── the toolkit's progress UI (game_setup_ui.h) in this look ─────────────────────────────────────────────────── */

static HWND s_parent;           /* --setup-parent: the setup window of the exe the player started */

void thps_setup_set_parent(unsigned long long hwnd)
{
    s_parent = (HWND)(UINT_PTR)hwnd;
    if (s_parent && !IsWindow(s_parent))
        s_parent = NULL;
}

struct XrSetupUi {
    Win   w;
    HWND  hwnd;
    DWORD last;
};

XrSetupUi *xr_setup_ui_open(const char *title)
{
    (void)title;
    return (XrSetupUi *)calloc(1, sizeof(XrSetupUi));
}

static void pump(void)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

void xr_setup_ui_progress(void *p, double fraction, const char *text)
{
    XrSetupUi *ui = (XrSetupUi *)p;
    DWORD now = GetTickCount();
    int pm = (int)(fraction * 1000.0 + 0.5);
    if (!ui)
        return;
    if (fraction < 1.0 && ui->last && now - ui->last < 50) {
        pump();
        return;                         /* 20 updates a second are plenty */
    }
    ui->last = now ? now : 1;
    if (pm < 0) pm = 0;
    if (pm > 1000) pm = 1000;
    if (s_parent) {
        COPYDATASTRUCT cd;
        DWORD_PTR r;
        cd.dwData = (ULONG_PTR)pm;
        cd.cbData = (DWORD)(text ? strlen(text) + 1 : 1);
        cd.lpData = (PVOID)(text ? text : "");
        SendMessageTimeoutW(s_parent, WM_COPYDATA, 0, (LPARAM)&cd, SMTO_ABORTIFHUNG, 500, &r);
        return;
    }
    if (!ui->hwnd) {
        ui->w.width = 520;
        ui->w.height = 120;
        ui->hwnd = open_window(&ui->w);
    }
    {
        wchar_t t[1024];
        int n = MultiByteToWideChar(CP_UTF8, 0, text ? text : "", -1, t, 1024);
        if (n <= 0)
            t[0] = 0;
        set_status(ui->hwnd, &ui->w, t, pm);
        UpdateWindow(ui->hwnd);
    }
    pump();
}

int xr_setup_ui_cancelled(void *p)
{
    XrSetupUi *ui = (XrSetupUi *)p;
    if (!ui)
        return 0;
    if (s_parent)
        return !IsWindow(s_parent);     /* the player closed the setup window */
    pump();
    return ui->w.closed;
}

void xr_setup_ui_close(XrSetupUi *ui)
{
    if (!ui)
        return;
    if (ui->hwnd)
        DestroyWindow(ui->hwnd);
    free(ui);
}

int xr_no_dialogs(void)
{
    const char *e = getenv("RECOMP_NO_DIALOGS");
    return e && *e == '1';
}

void xr_setup_ui_message(XrSetupUi *ui, int error, const char *text)
{
    wchar_t t[2048];
    HWND owner = s_parent ? s_parent : ui ? ui->hwnd : NULL;
    if (xr_no_dialogs())
        return;
    if (MultiByteToWideChar(CP_UTF8, 0, text ? text : "", -1, t, 2048) <= 0)
        t[0] = 0;
    MessageBoxW(owner, t, SETUP_TITLE, MB_OK | (error ? MB_ICONSTOP : MB_ICONWARNING));
}
