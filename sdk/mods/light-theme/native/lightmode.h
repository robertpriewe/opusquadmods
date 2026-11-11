/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPUS_LIGHTMODE_H
#define OPUS_LIGHTMODE_H
#include <stdint.h>
typedef int (*opus_patch_fn)(uintptr_t,void *,void **);
int opus_light_install(opus_patch_fn patch,opus_patch_fn inline_patch);
int opus_light_set(int on); /* JUCE message thread only; actual state returned */
int opus_light_on(void);
int opus_light_palette_set(int blue); /* 0=original R20, 1=blue/gray R21 */
int opus_light_palette(void);
uint32_t opus_light_surface(uint32_t color);
uint32_t opus_light_ink(uint32_t mapped_color);
void opus_light_repaint(void *display); /* JUCE thread, only after a state change */
#ifdef OPUS_LIGHT_TEST_PROBE
void opus_light_native_probe(void);
#endif
#endif
