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

#include "warpd.h"

/* constants */

static double v0, vf, a;
static int inc = 0;
static int sw, sh;

/* state */

static int left = 0;
static int right = 0;
static int up = 0;
static int down = 0;

static int opnum = 0;

static long get_time_us()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_nsec/1E3) + (ts.tv_sec * 1E6);
}

static int tonum(uint8_t code)
{
	char c = input_lookup_name(code)[0];

	if (c > '9' || c < '0')
		return -1;

	return c-'0';
}

static void get_mouse_position(double *x, double *y)
{
	int ix, iy;

	mouse_get_position(&ix, &iy);

	*x = (double)ix;
	*y = (double)iy;
}

static void process_tick()
{
	static long last_update = 0;
	static int resting = 1;

	static double cx = 0;
	static double cy = 0;
	static double v = 0;

	const long t = get_time_us();
	const double elapsed = (double)(t - last_update)/1E3;
	last_update = t;

	const double dx = right - left;
	const double dy = down - up;

	if (!dx && !dy) {
		resting = 1;
		return;
	}

	if (resting) {
		get_mouse_position(&cx, &cy);
		v = v0;
		resting = 0;
	}

	cx += v * elapsed * dx;
	cy += v * elapsed * dy;

	v += elapsed * a;
	if (v > vf)
		v = vf;

	cx = cx < 0 ? 0 : cx;
	cy = cy < 0 ? 0 : cy;
	cy = cy > sh ? sh : cy;
	cx = cx > sw ? sw : cx;

	mouse_move(cx, cy);
}

/* returns 1 if handled */
int mouse_process_key(struct input_event *ev, 
			const char *up_key, const char *down_key,
			const char *left_key, const char *right_key)
{
	int ret = 0;
	int n;

	/* timeout */
	if (!ev) {
		process_tick();
		return 0;
	}

	if ((n=tonum(ev->code)) != -1 && ev->mods == 0) {
		if (ev->pressed)
			opnum = opnum*10 + n;

		/* Allow 0 on its own to propagate as a special case. */
		if (opnum == 0)
			return 0;
		else
			return 1;
	}

	if (input_event_eq(ev, down_key)) {
		down = ev->pressed;
		ret = 1;
	} else if (input_event_eq(ev, left_key)) {
		left = ev->pressed;
		ret = 1;
	} else if (input_event_eq(ev, right_key)) {
		right = ev->pressed;
		ret = 1;
	} else if (input_event_eq(ev, up_key)) {
		up = ev->pressed;
		ret = 1;
	}

	if (opnum && ret) {
		double cx, cy;

		const int x = right - left;
		const int y = down - up;

		get_mouse_position(&cx, &cy);

		cx += inc*opnum*x;
		cy += inc*opnum*y;

		mouse_move(cx, cy);

		opnum = 0;

		left = 0;
		right = 0;
		up = 0;
		down = 0;

		return 1;
	}

	process_tick();
	return ret;
}

void mouse_reset()
{
	opnum = 0;
	left = 0;
	right = 0;
	up = 0;
	down = 0;

	process_tick();
}

void init_mouse()
{
	screen_get_dimensions(&sw, &sh);

	inc = sh / 100;

	/* pixels/ms */

	v0 = (double)cfg->speed / 1000.0;
	vf = (double)cfg->max_speed / 1000.0;
	a = (double)cfg->acceleration / 1000000.0;
}
