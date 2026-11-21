/*
 * This file is part of TinySurf, https://github.com/xyproto/tinysurf
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 */

#ifndef NETSURFY_HEADLESS_DUCK_H
#define NETSURFY_HEADLESS_DUCK_H

struct gui_window;

nserror headless_duck_start(const char *query);
bool headless_duck_active(void);
bool headless_duck_done(void);
int headless_duck_status(void);
void headless_duck_loading(void);
void headless_duck_loaded(struct gui_window *g);
bool headless_duck_console(const char *msg, size_t len);

#endif
