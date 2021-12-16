// Ported by nakst.
//
// Unfortunately, Uxn doesn't have a proper platform layer, so this port is a bit of a bodge.
// Perhaps once there is a more stable release of Uxn, someone can invest the time into making a proper port.
//
// TODO Keyboard support.
// TODO Audio support.
// TODO File support.
// TODO Time and date support.

#include <essence.h>

#define PPW (sizeof(unsigned int) * 2)
#define realloc EsCRTrealloc
#define memset EsCRTmemset
#define Uint32 uint32_t

typedef struct Ppu {
	uint16_t width, height;
	uint8_t *pixels, reqdraw;
} Ppu;

#ifdef DEBUG_BUILD
#include "../../bin/uxn/src/uxn.c"
#else
#include "../../bin/uxn/src/uxn-fast.c"
#endif
#include "../../bin/uxn/src/devices/ppu.c"

/*
Copyright (c) 2021 Devine Lu Linvega

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

static Ppu ppu;
static Uxn u;
static Device *devsystem, *devscreen, *devmouse, *devctrl;
static Uint32 palette[16];

static unsigned int reqdraw = 0;

static int
clamp(int val, int min, int max)
{
	return (val >= min) ? (val <= max) ? val : max : min;
}

static int
error(char *msg, const char *err)
{
	EsPrint("%z: %z\n", msg, err);
	return 0;
}

static void
domouse(int mx, int my, bool pressed, bool released, bool right)
{
	Uint8 flag = 0x00;
	Uint16 x = clamp(mx, 0, ppu.width - 1);
	Uint16 y = clamp(my, 0, ppu.height - 1);
	poke16(devmouse->dat, 0x2, x);
	poke16(devmouse->dat, 0x4, y);
	flag = right ? 0x10 : 0x01;
	if (pressed) devmouse->dat[6] |= flag;
	if (released) devmouse->dat[6] &= ~flag;
}

void
set_palette(Uint8 *addr)
{
	int i;
	for(i = 0; i < 4; ++i) {
		Uint8
			r = (*(addr + i / 2) >> (!(i % 2) << 2)) & 0x0f,
			g = (*(addr + 2 + i / 2) >> (!(i % 2) << 2)) & 0x0f,
			b = (*(addr + 4 + i / 2) >> (!(i % 2) << 2)) & 0x0f;
		palette[i] = 0xff000000 | (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
	}
	for(i = 4; i < 16; ++i)
		palette[i] = palette[i / 4];
	reqdraw = 1;
}

static Uint8
system_dei(Device *d, Uint8 port)
{
	switch(port) {
	case 0x2: return d->u->wst.ptr;
	case 0x3: return d->u->rst.ptr;
	default: return d->dat[port];
	}
}

static void
system_deo(Device *d, Uint8 port)
{
	switch(port) {
	case 0x2: d->u->wst.ptr = d->dat[port]; break;
	case 0x3: d->u->rst.ptr = d->dat[port]; break;
	}
	if(port > 0x7 && port < 0xe)
		set_palette(&d->dat[0x8]);
}

static Uint8
screen_dei(Device *d, Uint8 port)
{
	switch(port) {
	case 0x2: return ppu.width >> 8;
	case 0x3: return ppu.width;
	case 0x4: return ppu.height >> 8;
	case 0x5: return ppu.height;
	default: return d->dat[port];
	}
}

static void
screen_deo(Device *d, Uint8 port)
{
		switch(port) {
		case 0x1: d->vector = peek16(d->dat, 0x0); break;
		case 0xe: {
			Uint16 x = peek16(d->dat, 0x8);
			Uint16 y = peek16(d->dat, 0xa);
			Uint8 layer = d->dat[0xe] & 0x40;
			ppu_write(&ppu, !!layer, x, y, d->dat[0xe] & 0x3);
			if(d->dat[0x6] & 0x01) poke16(d->dat, 0x8, x + 1); /* auto x+1 */
			if(d->dat[0x6] & 0x02) poke16(d->dat, 0xa, y + 1); /* auto y+1 */
			break;
		}
		case 0xf: {
			Uint16 x = peek16(d->dat, 0x8);
			Uint16 y = peek16(d->dat, 0xa);
			Uint8 layer = d->dat[0xf] & 0x40;
			Uint8 *addr = &d->mem[peek16(d->dat, 0xc)];
			if(d->dat[0xf] & 0x80) {
				ppu_2bpp(&ppu, !!layer, x, y, addr, d->dat[0xf] & 0xf, d->dat[0xf] & 0x10, d->dat[0xf] & 0x20);
				if(d->dat[0x6] & 0x04) poke16(d->dat, 0xc, peek16(d->dat, 0xc) + 16); /* auto addr+16 */
			} else {
				ppu_1bpp(&ppu, !!layer, x, y, addr, d->dat[0xf] & 0xf, d->dat[0xf] & 0x10, d->dat[0xf] & 0x20);
				if(d->dat[0x6] & 0x04) poke16(d->dat, 0xc, peek16(d->dat, 0xc) + 8); /* auto addr+8 */
			}
			if(d->dat[0x6] & 0x01) poke16(d->dat, 0x8, x + 8); /* auto x+8 */
			if(d->dat[0x6] & 0x02) poke16(d->dat, 0xa, y + 8); /* auto y+8 */
			break;
		}
		}
}

static int
file_talk(Device *d, Uint8 b0, Uint8 w)
{
	Uint8 read = b0 == 0xd;
	if(w && (read || b0 == 0xf)) {
		char *name = (char *)&d->mem[peek16(d->dat, 0x8)];
		Uint16 result = 0, length = peek16(d->dat, 0xa);
		long offset = (peek16(d->dat, 0x4) << 16) + peek16(d->dat, 0x6);
		Uint16 addr = peek16(d->dat, b0 - 1);

		size_t pathBytes;
		char *path = EsStringAllocateAndFormat(&pathBytes, "|Settings:/%z", name);
		EsFileInformation file = EsFileOpen(path, pathBytes, read ? (ES_FILE_READ | ES_NODE_FAIL_IF_NOT_FOUND) : (ES_FILE_WRITE));
		EsHeapFree(path, 0, NULL);

		if (file.error != ES_SUCCESS) {
			result = 0;
		} else if (read) {
			result = EsFileReadSync(file.handle, offset, length, &d->mem[addr]);
			EsHandleClose(file.handle);
		} else {
			if (!offset) EsFileResize(file.handle, length);
			result = EsFileWriteSync(file.handle, offset, length, &d->mem[addr]);
			EsHandleClose(file.handle);
		}

		poke16(d->dat, 0x2, result);
	}
	return 1;
}

uint16_t file_init(void *filename) { return 0; }
uint16_t file_read(void *dest, uint16_t len) { return 0; }
uint16_t file_write(void *dest, uint16_t len, uint8_t flags) { return 0; }
uint16_t file_stat(void *dest, uint16_t len) { return 0; }
uint16_t file_delete() { return 0; }

static void
file_deo(Device *d, Uint8 port)
{
	switch(port) {
	case 0x1: d->vector = peek16(d->dat, 0x0); break;
	case 0x9: poke16(d->dat, 0x2, file_init(&d->mem[peek16(d->dat, 0x8)])); break;
	case 0xd: poke16(d->dat, 0x2, file_read(&d->mem[peek16(d->dat, 0xc)], peek16(d->dat, 0xa))); break;
	case 0xf: poke16(d->dat, 0x2, file_write(&d->mem[peek16(d->dat, 0xe)], peek16(d->dat, 0xa), d->dat[0x7])); break;
	case 0x5: poke16(d->dat, 0x2, file_stat(&d->mem[peek16(d->dat, 0x4)], peek16(d->dat, 0xa))); break;
	case 0x6: poke16(d->dat, 0x2, file_delete()); break;
	}
}

static Uint8
nil_dei(Device *d, Uint8 port)
{
	return d->dat[port];
}

static void
nil_deo(Device *d, Uint8 port)
{
	if(port == 0x1) d->vector = peek16(d->dat, 0x0);
}

static const char *errors[] = {"underflow", "overflow", "division by zero"};

int
uxn_halt(Uxn *u, Uint8 error, char *name, int id)
{
	EsPrint("Halted: %z %z#%x, at 0x%x\n", name, errors[error - 1], id, u->ram.ptr);
	return 0;
}

bool Launch(const void *rom, size_t romBytes) {
	if (romBytes > sizeof(u.ram.dat) - PAGE_PROGRAM) {
		return false;
	}

	if (!uxn_boot(&u)) return false;
	EsMemoryCopy(u.ram.dat + PAGE_PROGRAM, rom, romBytes);
	reqdraw = 1;
	const Uint16 width = 64 * 8, height = 40 * 8;
	ppu_set_size(&ppu, width, height);

	/* system   */ devsystem = uxn_port(&u, 0x0, system_dei, system_deo);
	/* console  */ uxn_port(&u, 0x1, nil_dei, nil_deo);
	/* screen   */ devscreen = uxn_port(&u, 0x2, screen_dei, screen_deo);
	/* audio0   */ uxn_port(&u, 0x3, nil_dei, nil_deo);
	/* audio1   */ uxn_port(&u, 0x4, nil_dei, nil_deo);
	/* audio2   */ uxn_port(&u, 0x5, nil_dei, nil_deo);
	/* audio3   */ uxn_port(&u, 0x6, nil_dei, nil_deo);
	/* unused   */ uxn_port(&u, 0x7, nil_dei, nil_deo);
	/* control  */ devctrl = uxn_port(&u, 0x8, nil_dei, nil_deo);
	/* mouse    */ devmouse = uxn_port(&u, 0x9, nil_dei, nil_deo);
	/* file     */ uxn_port(&u, 0xa, nil_dei, file_deo);
	/* datetime */ uxn_port(&u, 0xb, nil_dei, nil_deo);
	/* unused   */ uxn_port(&u, 0xc, nil_dei, nil_deo);
	/* unused   */ uxn_port(&u, 0xd, nil_dei, nil_deo);
	/* unused   */ uxn_port(&u, 0xe, nil_dei, nil_deo);
	/* unused   */ uxn_port(&u, 0xf, nil_dei, nil_deo);

	uxn_eval(&u, PAGE_PROGRAM);

	return true;
}

EsElement *canvas;
uint32_t imageWidth, imageHeight;
uint32_t *imageBits;
uint16_t timeDeltaMs;
bool inImageBounds;

EsRectangle GetImageBounds(EsRectangle bounds /* canvas bounds */) {
	if (!imageWidth || !imageHeight) return ES_RECT_1(0);
	int zoomX = ES_RECT_WIDTH(bounds) / imageWidth, zoomY = ES_RECT_HEIGHT(bounds) / imageHeight;
	int zoom = zoomX < 1 || zoomY < 1 ? 1 : zoomX > zoomY ? zoomY : zoomX;
	return EsRectangleCenter(bounds, ES_RECT_2S(imageWidth * zoom, imageHeight * zoom));
}

void MouseEvent(bool pressed, bool released, bool right) {
	if (!imageWidth) return;
	EsPoint point = EsMouseGetPosition(canvas); 
	EsRectangle imageBounds = GetImageBounds(EsElementGetInsetBounds(canvas));
	inImageBounds = EsRectangleContains(imageBounds, point.x, point.y);
	int zoom = ES_RECT_WIDTH(imageBounds) / imageWidth;
	domouse((point.x - imageBounds.l) / zoom, (point.y - imageBounds.t) / zoom, pressed, released, right);
	uxn_eval(&u, peek16(devmouse->dat, 0));
}

int CanvasMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_PAINT) {
		EsRectangle bounds = EsPainterBoundsInset(message->painter);
		EsRectangle imageBounds = GetImageBounds(bounds);
		EsDrawBitmapScaled(message->painter, imageBounds, ES_RECT_2S(imageWidth, imageHeight), imageBits, imageWidth * 4, ES_DRAW_BITMAP_OPAQUE);
		EsDrawBlock(message->painter, ES_RECT_4(bounds.l, imageBounds.l, bounds.t, bounds.b), 0xFF000000);
		EsDrawBlock(message->painter, ES_RECT_4(imageBounds.r, bounds.r, bounds.t, bounds.b), 0xFF000000);
		EsDrawBlock(message->painter, ES_RECT_4(imageBounds.l, imageBounds.r, bounds.t, imageBounds.t), 0xFF000000);
		EsDrawBlock(message->painter, ES_RECT_4(imageBounds.l, imageBounds.r, imageBounds.b, bounds.b), 0xFF000000);
	} else if (message->type == ES_MSG_MOUSE_MOVED || message->type == ES_MSG_MOUSE_LEFT_DRAG || message->type == ES_MSG_MOUSE_RIGHT_DRAG) {
		MouseEvent(false, false, false);
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
		MouseEvent(true, false, false);
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP) {
		MouseEvent(false, true, false);
	} else if (message->type == ES_MSG_MOUSE_RIGHT_DOWN) {
		MouseEvent(true, false, true);
	} else if (message->type == ES_MSG_MOUSE_RIGHT_UP) {
		MouseEvent(false, true, false);
	} else if (message->type == ES_MSG_GET_CURSOR && inImageBounds) {
		message->cursorStyle = ES_CURSOR_BLANK;
	} else if (message->type == ES_MSG_ANIMATE) {
		message->animate.complete = false;
		message->animate.waitMs = 16;
		timeDeltaMs += message->animate.deltaMs;
		bool needsRedraw = reqdraw;

		while (timeDeltaMs > 16) {
			uxn_eval(&u, peek16(devscreen->dat, 0));
			if(devsystem->dat[0xe]) needsRedraw = true;
			timeDeltaMs -= 16;
		}

		if (needsRedraw || ppu.reqdraw) {
			if (imageWidth != ppu.width || imageHeight != ppu.height) {
				imageWidth = ppu.width, imageHeight = ppu.height;
				imageBits = (uint32_t *) EsHeapReallocate(imageBits, imageWidth * imageHeight * 4, false, NULL);
			}

			for (uint16_t y = 0; y < ppu.height; y++) {
				for (uint16_t x = 0; x < ppu.width; x++) {
					imageBits[x + y * ppu.width] = palette[ppu_read(&ppu, x, y)] | 0xFF000000;
				}
			}

			EsRectangle imageBounds = GetImageBounds(EsElementGetInsetBounds(element));
			EsElementRepaint(element, &imageBounds);
			reqdraw = false;
			ppu.reqdraw = false;
		}
	} else {
		return 0;
	}

	return ES_HANDLED;
}

int InstanceCallback(EsInstance *instance, EsMessage *message) {
	if (message->type == ES_MSG_INSTANCE_OPEN) {
		size_t romBytes;
		void *rom = EsFileStoreReadAll(message->instanceOpen.file, &romBytes);
		EsInstanceOpenComplete(instance, message->instanceOpen.file, rom && Launch(rom, romBytes), NULL, 0);
		EsHeapFree(rom, 0, NULL);
		return ES_HANDLED;
	}

	return 0;
}

void _start() {
	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			EsInstance *instance = EsInstanceCreate(message, "Uxn Emulator", -1);
			instance->callback = InstanceCallback;
			canvas = EsCustomElementCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);
			canvas->messageUser = (EsElementCallback) CanvasMessage;
			EsElementStartAnimating(canvas);
		}
	}
}
