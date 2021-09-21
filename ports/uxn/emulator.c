// Ported by nakst.
// TODO Keyboard support.
// TODO Audio support.
// TODO Time and date support.

#include <essence.h>

#define PPW (sizeof(unsigned int) * 2)
#define realloc EsCRTrealloc
#define Uint32 uint32_t

typedef struct Ppu {
	unsigned short width, height;
	unsigned int *dat, stride;
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

static Uint8
get_pixel(int x, int y)
{
	unsigned int i = x / PPW + y * ppu.stride, shift = x % PPW * 4;
	return (ppu.dat[i] >> shift) & 0xf;
}

void
update_palette(Uint8 *addr)
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

static int
system_talk(Device *d, Uint8 b0, Uint8 w)
{
	if(!w) { /* read */
		switch(b0) {
		case 0x2: d->dat[0x2] = d->u->wst.ptr; break;
		case 0x3: d->dat[0x3] = d->u->rst.ptr; break;
		}
	} else { /* write */
		switch(b0) {
		case 0x2: d->u->wst.ptr = d->dat[0x2]; break;
		case 0x3: d->u->rst.ptr = d->dat[0x3]; break;
		case 0xf: return 0;
		}
		if(b0 > 0x7 && b0 < 0xe)
			update_palette(&d->dat[0x8]);
	}
	return 1;
}

static int
console_talk(Device *d, Uint8 b0, Uint8 w)
{
	return 1;
}

static int
screen_talk(Device *d, Uint8 b0, Uint8 w)
{
	if(!w) switch(b0) {
		case 0x2: d->dat[0x2] = ppu.width >> 8; break;
		case 0x3: d->dat[0x3] = ppu.width; break;
		case 0x4: d->dat[0x4] = ppu.height >> 8; break;
		case 0x5: d->dat[0x5] = ppu.height; break;
		}
	else
		switch(b0) {
		case 0x5:
			ppu_set_size(&ppu, peek16(d->dat, 0x2), peek16(d->dat, 0x4));
			break;
		case 0xe: {
			Uint16 x = peek16(d->dat, 0x8);
			Uint16 y = peek16(d->dat, 0xa);
			Uint8 layer = d->dat[0xe] & 0x40;
			reqdraw |= ppu_pixel(&ppu, layer, x, y, d->dat[0xe] & 0x3);
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
				reqdraw |= ppu_2bpp(&ppu, layer, x, y, addr, d->dat[0xf] & 0xf, d->dat[0xf] & 0x10, d->dat[0xf] & 0x20);
				if(d->dat[0x6] & 0x04) poke16(d->dat, 0xc, peek16(d->dat, 0xc) + 16); /* auto addr+16 */
			} else {
				reqdraw |= ppu_1bpp(&ppu, layer, x, y, addr, d->dat[0xf] & 0xf, d->dat[0xf] & 0x10, d->dat[0xf] & 0x20);
				if(d->dat[0x6] & 0x04) poke16(d->dat, 0xc, peek16(d->dat, 0xc) + 8); /* auto addr+8 */
			}
			if(d->dat[0x6] & 0x01) poke16(d->dat, 0x8, x + 8); /* auto x+8 */
			if(d->dat[0x6] & 0x02) poke16(d->dat, 0xa, y + 8); /* auto y+8 */
			break;
		}
		}
	return 1;
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

static int
datetime_talk(Device *d, Uint8 b0, Uint8 w)
{
	poke16(d->dat, 0x0, 0);
	d->dat[0x2] = 0;
	d->dat[0x3] = 0;
	d->dat[0x4] = 0;
	d->dat[0x5] = 0;
	d->dat[0x6] = 0;
	d->dat[0x7] = 0;
	poke16(d->dat, 0x08, 0);
	d->dat[0xa] = 0;
	(void)b0;
	(void)w;
	return 1;
}

static int
nil_talk(Device *d, Uint8 b0, Uint8 w)
{
	(void)d;
	(void)b0;
	(void)w;
	return 1;
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

	/* system   */ devsystem = uxn_port(&u, 0x0, system_talk);
	/* console  */ uxn_port(&u, 0x1, console_talk);
	/* screen   */ devscreen = uxn_port(&u, 0x2, screen_talk);
	/* audio0   */ uxn_port(&u, 0x3, nil_talk);
	/* audio1   */ uxn_port(&u, 0x4, nil_talk);
	/* audio2   */ uxn_port(&u, 0x5, nil_talk);
	/* audio3   */ uxn_port(&u, 0x6, nil_talk);
	/* unused   */ uxn_port(&u, 0x7, nil_talk);
	/* control  */ devctrl = uxn_port(&u, 0x8, nil_talk);
	/* mouse    */ devmouse = uxn_port(&u, 0x9, nil_talk);
	/* file     */ uxn_port(&u, 0xa, file_talk);
	/* datetime */ uxn_port(&u, 0xb, datetime_talk);
	/* unused   */ uxn_port(&u, 0xc, nil_talk);
	/* unused   */ uxn_port(&u, 0xd, nil_talk);
	/* unused   */ uxn_port(&u, 0xe, nil_talk);
	/* unused   */ uxn_port(&u, 0xf, nil_talk);

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

		if (needsRedraw) {
			if (imageWidth != ppu.width || imageHeight != ppu.height) {
				imageWidth = ppu.width, imageHeight = ppu.height;
				imageBits = (uint32_t *) EsHeapReallocate(imageBits, imageWidth * imageHeight * 4, false, NULL);
			}

			for (uint16_t y = 0; y < ppu.height; y++) {
				for (uint16_t x = 0; x < ppu.width; x++) {
					imageBits[x + y * ppu.width] = palette[get_pixel(x, y)] | 0xFF000000;
				}
			}

			EsRectangle imageBounds = GetImageBounds(EsElementGetInsetBounds(element));
			EsElementRepaint(element, &imageBounds);
			reqdraw = false;
		}
	} else {
		return 0;
	}

	return ES_HANDLED;
}

void _start() {
	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			EsInstance *instance = EsInstanceCreate(message, "Uxn Emulator", -1);
			canvas = EsCustomElementCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);
			canvas->messageUser = (EsUICallback) CanvasMessage;
			EsElementStartAnimating(canvas);
		} else if (message->type == ES_MSG_INSTANCE_OPEN) {
			size_t romBytes;
			void *rom = EsFileStoreReadAll(message->instanceOpen.file, &romBytes);
			EsInstanceOpenComplete(message, rom && Launch(rom, romBytes), NULL, 0);
			EsHeapFree(rom, 0, NULL);
		}
	}
}
