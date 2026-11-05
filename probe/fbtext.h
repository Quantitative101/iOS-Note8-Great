/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>

/* S-Boot scanout buffer (mainline dreamlte.dts (S8, same SoC); unverified on the Note 8). */
#define HERO_FB_BASE 0xcc000000UL
#define HERO_FB_WIDTH 1440U
#define HERO_FB_HEIGHT 2960U

/* Let CPU writes reach the panel (DECON_F HW trigger), returns old value. */
uint32_t hero_fb_enable(void);
void hero_fb_text(unsigned row, const char *s);
void hero_fb_hex(unsigned row, const char *label, uint64_t value);
