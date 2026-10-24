/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Epoch is the only API callable outside the lock (including the input thread).
 * The remaining APIs and replay into the listener run under this recursive lock.
 * All upstream Hold-Tap entry points are wrapped with the same lock. */
uint32_t cline38_retro_epoch(void);
void cline38_retro_lock(void);
void cline38_retro_unlock(void);
bool cline38_retro_pending(void);
bool cline38_retro_commit(int64_t timestamp);
