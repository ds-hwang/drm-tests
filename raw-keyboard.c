/*
 * Copyright (C) 2012 The Chromium OS Authors <chromium-os-dev@chromium.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <unistd.h>
#include <linux/input.h>
#include <X11/extensions/XKBcommon.h>

struct xkb_desc  *create_xkb(void) {
	struct xkb_rule_names names;

	names.rules = "evdev";
	names.model = "pc105";
	names.layout = "us";
	names.variant = "";
	names.options = "";

	return xkb_compile_keymap_from_rules(&names);
}

void handle_key(struct xkb_desc *xkb,
		struct input_event *evp,
		uint32_t *modifiers)
{
	uint32_t code = evp->code + xkb->min_key_code;
	uint32_t level = 0;
	uint32_t sym;

	if ((*modifiers & XKB_COMMON_SHIFT_MASK) &&
	    XkbKeyGroupWidth(xkb, code, 0) > 1)
		level = 1;

	sym = XkbKeySymEntry(xkb, code, level, 0);

	if (evp->value)
		*modifiers |= xkb->map->modmap[code];
	else
		*modifiers &= xkb->map->modmap[code];

	printf("Code: %d, Sym: %d, Modifiers: %d, Ascii: %c, State: %s\n",
	       evp->code, sym, *modifiers, sym,
	       evp->value ? "up" : "down");
}

int main(int argc, char *argv[])
{
	struct input_event ev;
	struct xkb_desc *xkb;
	uint32_t modifiers;
	ssize_t bytes;
	int i;

	xkb = create_xkb();
	if (!xkb) {
		fprintf(stderr, "Failed to compile keymap\n");
		return 1;
	}

	while (1) {
		bytes = read(0, &ev, sizeof(ev));
		if (bytes != sizeof(ev))
			break;
		if (EV_KEY == ev.type)
			handle_key(xkb, &ev, &modifiers);
	}

	return 0;
}
