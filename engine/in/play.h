/* SPDX-License-Identifier: MIT */
#ifndef TR64_PLAY_H
#define TR64_PLAY_H
/* Enable only with initialized display and no surface held by the renderer. */
void tr64_play_message_ready(int ready);
/* At a completed-frame boundary; no renderer surface may be held. */
void tr64_play_message(const char *title,const char *message);
/* Called after platform/GPU/code boot and heap freeze; never a scene route. */
void tr64_play_run(void) __attribute__((noreturn));
#endif
