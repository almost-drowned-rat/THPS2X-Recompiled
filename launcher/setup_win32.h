/* setup_win32.h -- THPS 2X's setup window in the NG2 launcher look (setup_win32.c). */
#ifndef THPS_SETUP_WIN32_H
#define THPS_SETUP_WIN32_H

#ifdef __cplusplus
extern "C" {
#endif

/* The setup window (disc image, install folder, Go / Play); returns the exit code. Checks Vulkan 1.3 first. */
int thps_setup_window(void);
/* 1 when a Vulkan 1.3 device is there, else the apology box (not with RECOMP_NO_DIALOGS=1) and 0. */
int thps_vk_gate(void);
/* --setup-parent <hwnd>: the toolkit launcher's progress goes into that window. */
void thps_setup_set_parent(unsigned long long hwnd);

#ifdef __cplusplus
}
#endif

#endif
