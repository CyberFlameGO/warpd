/* Copyright © 2019 Raheman Vaiya.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <ctype.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/XInput2.h>
#include <assert.h>
#include <unistd.h>

#include "input.h"
#include "impl.h"

static int nr_grabbed_device_ids = 0;
static int grabbed_device_ids[64];

/* clear the X keyboard state. */
static void reset_keyboard()
{
	size_t i;
	char keymap[32];

	XQueryKeymap(dpy, keymap);

	for (i = 0; i < 256; i++) {
		if(0x01 & keymap[i/8] >> (i%8))
			XTestFakeKeyEvent(dpy, i, 0, CurrentTime);
	}

	XSync(dpy, False);
}

static void grab(int device_id)
{
	int rc;

	XIEventMask mask;

	mask.deviceid = XIAllDevices;
	mask.mask_len = XIMaskLen(XI_LASTEVENT);
	mask.mask = calloc(mask.mask_len, sizeof(char));

	XISetMask(mask.mask, XI_KeyPress);
	XISetMask(mask.mask, XI_KeyRelease);

	if((rc = XIGrabDevice(dpy, 
				device_id,
				DefaultRootWindow(dpy),
				CurrentTime,
				None,
				GrabModeAsync,
				GrabModeAsync,
				False, &mask))) {
		int n;

		XIDeviceInfo *info = XIQueryDevice(dpy, device_id, &n);
		fprintf(stderr, "FATAL: Failed to grab keyboard %s: %d\n", info->name, rc);
		exit(-1);
	}

	XSync(dpy, False);
}

/* timeout in ms. */
static XEvent *get_next_xev(int timeout)
{
	static int xfd = 0;
	static XEvent ev;

	fd_set fds;

	if(!xfd) 
		xfd = XConnectionNumber(dpy);

	if(XPending(dpy)) {
		XNextEvent(dpy, &ev);
		return &ev;
	}

	FD_ZERO(&fds);
	FD_SET(xfd, &fds);
	select(xfd+1, &fds, NULL, NULL, timeout ? &(struct timeval){0, timeout*1000} : NULL);

	if(XPending(dpy)) {
		XNextEvent(dpy, &ev);
		return &ev;
	} else
		return NULL;

}

static uint8_t sym_table[] = {
	[XK_q] = 24,
	[XK_w] = 25,
	[XK_e] = 26,
	[XK_r] = 27,
	[XK_t] = 28,
	[XK_y] = 29,
	[XK_u] = 30,
	[XK_i] = 31,
	[XK_o] = 32,
	[XK_p] = 33,
	[XK_a] = 38,
	[XK_s] = 39,
	[XK_d] = 40,
	[XK_f] = 41,
	[XK_g] = 42,
	[XK_h] = 43,
	[XK_j] = 44,
	[XK_k] = 45,
	[XK_l] = 46,
	[XK_semicolon] = 47,
	[XK_apostrophe] = 48,
	[XK_grave] = 49,
	[XK_backslash] = 51,
	[XK_z] = 52,
	[XK_x] = 53,
	[XK_c] = 54,
	[XK_v] = 55,
	[XK_b] = 56,
	[XK_n] = 57,
	[XK_m] = 58,
	[XK_comma] = 59,
	[XK_period] = 60,
	[XK_slash] = 61,
	[XK_space] = 65,
};

static const size_t sym_table_sz = sizeof(sym_table)/sizeof(sym_table[0]);

static uint8_t normalize_keycode(uint8_t code) {
	/* 
	 * NOTE: In theory the X server doesn't encode any information in the
	 * keycode itself and relies on the client to convert it into a keysym.
	 * In practice the code appears to correspond to the evdev code which
	 * generated the X code + 8. This is the basis for our X code map.
	 */

	KeySym sym = XKeycodeToKeysym(dpy, code, 0);

	if (sym < sym_table_sz && sym_table[sym])
		code = sym_table[sym];

	return code-8;
}

/* returns a key code or 0 on failure. */
static uint8_t process_xinput_event(XEvent *ev, int *state, int *mods)
{
	XGenericEventCookie *cookie = &ev->xcookie;

	static int xiop = 0;
	if(!xiop) {
		int ev, err;

		if (!XQueryExtension(dpy, "XInputExtension", &xiop, &ev, &err)) {
			fprintf(stderr, "FATAL: X Input extension not available.\n");
			exit(-1);
		}
	}

	/* not a xinput event.. */
	if (cookie->type != GenericEvent ||
	    cookie->extension != xiop ||
	    !XGetEventData(dpy, cookie))
		return 0;

	switch(cookie->evtype) {
		uint16_t code;
		XIDeviceEvent *dev;

	case XI_KeyPress:
		dev = (XIDeviceEvent*)(cookie->data);
		code = normalize_keycode(dev->detail);

		*state = (dev->flags & XIKeyRepeat) ? 2 : 1;
		*mods = dev->mods.effective;

		XFreeEventData(dpy, cookie);

		return code;
	case XI_KeyRelease:
		dev = (XIDeviceEvent*)(cookie->data);
		code = normalize_keycode(dev->detail);

		*state = 0;
		*mods = dev->mods.effective;

		XFreeEventData(dpy, cookie);

		return code;
	}

	fprintf(stderr, "FATAL: Unrecognized xinput event\n");
	exit(-1);
}

static const char *xerr_key = NULL;
static int input_xerr(Display *dpy, XErrorEvent *ev)
{
	fprintf(stderr, "ERROR: Failed to grab %s (ensure another application hasn't mapped it)\n", xerr_key);
	exit(1);
	return 0;
}

void xgrab_key(uint8_t code, uint8_t mods)
{
	XSetErrorHandler(input_xerr);
	int xcode = code + 8;
	int xmods = 0;

	if (!code)
		return;

	if (mods & MOD_CONTROL)
		xmods |= ControlMask;
	if (mods & MOD_SHIFT)
		xmods |= ShiftMask;
	if (mods & MOD_META)
		xmods |= Mod4Mask;
	if (mods & MOD_ALT)
		xmods |= Mod1Mask;

	xerr_key = input_lookup_name(code);

	XGrabKey(dpy,
		 xcode,
		 xmods,
		 DefaultRootWindow(dpy),
		 False,
		 GrabModeAsync, GrabModeAsync);

	XGrabKey(dpy,
		 xcode,
		 xmods, /* numlock */
		 DefaultRootWindow(dpy),
		 False,
		 GrabModeAsync, GrabModeAsync);

	XSync(dpy, False);

	XSetErrorHandler(NULL);
}


void input_grab_keyboard()
{
	int i, n;
	XIDeviceInfo* devices;

	if (nr_grabbed_device_ids != 0)
		return;

	devices = XIQueryDevice(dpy, XIAllDevices, &n);

	for (i = 0; i < n; i++) {
		if (devices[i].use == XISlaveKeyboard ||
		    devices[i].use == XIFloatingSlave) {
			if(!strstr(devices[i].name, "XTEST") && devices[i].enabled) {
				int id = devices[i].deviceid;

				grab(id);
				grabbed_device_ids[nr_grabbed_device_ids++] = id;
			}
		}
	}

	/* send a key up event for any depressed keys to avoid infinite repeat. */
	reset_keyboard();
	XIFreeDeviceInfo(devices);

	XSync(dpy, False);
}

void input_ungrab_keyboard()
{
	int i;

	if(!nr_grabbed_device_ids) 
		return;

	for (i = 0; i < nr_grabbed_device_ids; i++) {
		int n;
		XIDeviceInfo *info = XIQueryDevice(dpy, grabbed_device_ids[i], &n);

		assert(n == 1);

		/*
		 * NOTE: Attempting to ungrab a disabled xinput device
		 * causes X to crash.  
		 * 
		 * (see https://gitlab.freedesktop.org/xorg/lib/libxi/-/issues/11).
		 *
		 * This generally shouldn't happen unless the user
		 * switches virtual terminals while keyd is running. We
		 * used to explicitly check for this and perform weird
		 * hacks to mitigate against it, but now we only grab
		 * the keyboard when the program is in one if its
		 * active modes which reduces the likelihood
		 * sufficiently to not to warrant the additional
		 * complexity.
		 */

		assert(info->enabled);
		XIUngrabDevice(dpy, grabbed_device_ids[i], CurrentTime);
	}

	nr_grabbed_device_ids = 0;
	XSync(dpy, False);
}


uint8_t xmods_to_mods(int xmods) 
{
	uint8_t mods = 0;

	if (xmods & ShiftMask)
		mods |= MOD_SHIFT;
	if (xmods & ControlMask)
		mods |= MOD_CONTROL;
	if (xmods & Mod1Mask)
		mods |= MOD_ALT;
	if (xmods & Mod4Mask)
		mods |= MOD_META;

	return mods;
}

/* returns 0 on timeout. */
struct input_event *input_next_event(int timeout)
{
	static struct input_event ev;

	struct timeval start, end;
	gettimeofday(&start, NULL);
	int elapsed = 0;

	while(1) {
		int state;
		uint8_t code;
		XEvent *xev;

		xev = get_next_xev(timeout - elapsed);

		if (xev) {
			int xmods;
			code = process_xinput_event(xev, &state, &xmods);
			if (code && state != 2) {
				ev.pressed = state;
				ev.code = code;
				ev.mods = xmods_to_mods(xmods);

				return &ev;
			}
		}

		if(timeout) {
			gettimeofday(&end, NULL);
			elapsed = (end.tv_sec-start.tv_sec)*1000 + (end.tv_usec-start.tv_usec)/1000;

			if(elapsed >= timeout)
				return NULL;
		}
	}
}

struct input_event *input_wait(struct input_event *events, size_t sz)
{
	size_t i;
	static struct input_event ev;

	for (i = 0; i < sz; i++) {
		struct input_event *ev = &events[i];
		xgrab_key(ev->code, ev->mods);
	}

	while (1) {
		XEvent *xev = get_next_xev(0);

		if (xev->type == KeyPress || xev->type == KeyRelease) {
			ev.code = xev->xkey.keycode-8;
			ev.mods = xmods_to_mods(xev->xkey.state);
			ev.pressed = xev->type == KeyPress;

			input_grab_keyboard();
			return &ev;
		}
	}
}

uint8_t input_lookup_code(const char *name)
{
	size_t code = 0;

	for(code =0; code < sizeof(keycode_table)/sizeof(keycode_table[0]); code++) {
		if (keycode_table[code].name && !strcmp(keycode_table[code].name, name))
			return code;
	}

	return 0;
}

const char *input_lookup_name(uint8_t code)
{
	return keycode_table[code].name ? keycode_table[code].name : "UNDEFINED";
}
