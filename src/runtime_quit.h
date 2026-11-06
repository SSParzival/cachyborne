// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef CACHYBORNE_RUNTIME_QUIT_H
#define CACHYBORNE_RUNTIME_QUIT_H
#ifdef __cplusplus
extern "C" {
#endif
/* Arm only after the player confirms our Exit to desktop question. */
void runtime_quit_confirmed(void);
void runtime_quit_cancel(void);
void runtime_quit_at_title(void);
/* Called after releasing the save-data mount lock. */
void runtime_quit_on_save_done(void);
int runtime_savedata_mounted(void);
#ifdef __cplusplus
}
#endif
#endif
