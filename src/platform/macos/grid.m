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

#include <Cocoa/Cocoa.h>
#include "impl.h"

struct grid {
	NSColor *color;
	size_t nc;
	size_t nr;
	float sz;

	float w;
	float h;
	float x;
	float y;

	long mtime;
};


static long ts = 0;

/* all grids are drawn over a single overlay window */
static struct window *ow = NULL;

static struct grid grids[5];

static size_t nr_grids = 0;

static void draw_box(NSColor *col, float x, float y, float w, float h)
{
	float sw, sh;
	NSScreen *scr;

	[col setFill];

	scr = [NSScreen mainScreen];
	sw = [scr frame].size.width;
	sh = [scr frame].size.height;

	CGContextFillRect([NSGraphicsContext currentContext].CGContext, CGRectMake(x, sh-y-h, w, h));
}

static void redraw(struct grid *g)
{
	size_t i;
	float gap;
	float offset;

	float ux = g->x+g->w;
	float lx = g->x;
	float uy = g->y+g->h;
	float ly = g->y;

	offset = lx;
	gap = (ux-lx)/g->nc;

	for(i = 0; i < g->nc+1; i++) {
		draw_box(g->color, offset-(g->sz/2), ly-g->sz/2, g->sz, uy-ly+g->sz);
		offset += gap;
	}


	offset = ly;
	gap = (uy-ly)/g->nr;

	for(i = 0; i < g->nr+1; i++) {
		draw_box(g->color, lx-(g->sz/2), offset-(g->sz/2), ux-lx+g->sz, g->sz);
		offset += gap;
	}
}

static void overlay_redraw(NSView *view)
{
	while (1) {
		struct grid *g = NULL;
		size_t i;

		for (i = 0; i < nr_grids; i++) {
			if (grids[i].mtime != -1 && 
				(!g || (grids[i].mtime < g->mtime)))
				g = &grids[i];
		}

		if (!g)
			break;

		redraw(g);
		g->mtime = -1;
	}
}

static void init()
{
	ow = create_overlay_window(overlay_redraw);
}

struct grid *create_grid(const char *color, size_t width, size_t nc, size_t nr)
{
	assert(nr_grids < sizeof(grids)/sizeof(grids[0]));
	struct grid *g = &grids[nr_grids++];

	if (!ow)
		init();

	g->nc = nc;
	g->nr = nr;
	g->sz = width;
	g->color = parse_color(color);

	return g;
}

void grid_hide(struct grid *g)
{
	/* FIXME: technically we should only hide the grid of interest. */
	window_hide(ow);
}

void grid_draw(struct grid *g, int x, int y, int w, int h)
{
	g->x = (float)x + g->sz/2;
	g->y = (float)y + g->sz/2;
	g->w = (float)w - g->sz;
	g->h = (float)h - g->sz;

	g->mtime = ts++;

	window_show(ow);
}

void grid_commit()
{
	window_commit(ow);
}
