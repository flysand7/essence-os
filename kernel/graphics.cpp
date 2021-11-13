// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#ifndef IMPLEMENTATION

#define CURSOR_SHADOW_OFFSET (1)

struct Surface : EsPaintTarget {
	bool Resize(size_t newResX, size_t newResY, uint32_t clearColor = 0, bool copyOldBits = false);
	void Copy(Surface *source, EsPoint destinationPoint, EsRectangle sourceRegion, bool addToModifiedRegion); 
	void Draw(Surface *source, EsRectangle destinationRegion, int sourceX, int sourceY, uint16_t alpha);
	void BlendWindow(Surface *source, EsPoint destinationPoint, EsRectangle sourceRegion, int material, uint8_t alpha, EsRectangle materialRegion);
	void Blur(EsRectangle region, EsRectangle clip);
	void SetBits(K_USER_BUFFER const void *bits, uintptr_t stride, EsRectangle region);
	void Scroll(EsRectangle region, ptrdiff_t delta, bool vertical);
	void CreateCursorShadow(Surface *source);

	EsRectangle modifiedRegion;
};

struct Graphics {
	KGraphicsTarget *target;
        size_t width, height; 
	Surface frameBuffer;
	bool debuggerActive;
	size_t totalSurfaceBytes;
};

void GraphicsUpdateScreen(K_USER_BUFFER void *bits = nullptr, EsRectangle *bounds = nullptr, uintptr_t stride = 0);

Graphics graphics;

#else

void GraphicsUpdateScreen(K_USER_BUFFER void *bits, EsRectangle *bounds, uintptr_t bitsStride) {
	KMutexAssertLocked(&windowManager.mutex);

	if (windowManager.resizeWindow && windowManager.resizeStartTimeStampMs + RESIZE_FLICKER_TIMEOUT_MS > KGetTimeInMs()
			&& !windowManager.inspectorWindowCount /* HACK see note in the SET_BITS syscall */) {
		return;
	}

	if (bounds && (Width(*bounds) <= 0 || Height(*bounds) <= 0)) {
		return;
	}

	int cursorX = windowManager.cursorX + windowManager.cursorImageOffsetX - (bounds ? bounds->l : 0);
	int cursorY = windowManager.cursorY + windowManager.cursorImageOffsetY - (bounds ? bounds->t : 0);

	Surface *sourceSurface;
	Surface _sourceSurface;
	EsRectangle _bounds;

	if (bits) {
		sourceSurface = &_sourceSurface;
		EsMemoryZero(sourceSurface, sizeof(Surface));
		sourceSurface->bits = bits;
		sourceSurface->width = Width(*bounds);
		sourceSurface->height = Height(*bounds);
		sourceSurface->stride = bitsStride;
	} else {
		sourceSurface = &graphics.frameBuffer;
		_bounds = ES_RECT_4(0, sourceSurface->width, 0, sourceSurface->height);
		bounds = &_bounds;
	}

	EsRectangle cursorBounds = ES_RECT_4(cursorX, cursorX + windowManager.cursorSwap.width, cursorY, cursorY + windowManager.cursorSwap.height);
	EsRectangleClip(ES_RECT_4(0, Width(*bounds), 0, Height(*bounds)), cursorBounds, &cursorBounds);

	windowManager.cursorSwap.Copy(sourceSurface, ES_POINT(0, 0), cursorBounds, true);
	windowManager.changedCursorImage = false;

	int cursorImageWidth = windowManager.cursorSurface.width, cursorImageHeight = windowManager.cursorSurface.height;
	sourceSurface->Draw(&windowManager.cursorSurface, ES_RECT_4(cursorX, cursorX + cursorImageWidth, cursorY, cursorY + cursorImageHeight), 0, 0, 0xFF);

	if (bits) {
		graphics.target->updateScreen((K_USER_BUFFER const uint8_t *) bits, 
				sourceSurface->width, sourceSurface->height, 
				sourceSurface->stride, bounds->l, bounds->t);
	} else {
		if (Width(sourceSurface->modifiedRegion) > 0 && Height(sourceSurface->modifiedRegion) > 0) {
			uint8_t *bits = (uint8_t *) sourceSurface->bits 
				+ sourceSurface->modifiedRegion.l * 4 
				+ sourceSurface->modifiedRegion.t * sourceSurface->stride;
			graphics.target->updateScreen(bits, Width(sourceSurface->modifiedRegion), Height(sourceSurface->modifiedRegion), 
					sourceSurface->width * 4, sourceSurface->modifiedRegion.l, sourceSurface->modifiedRegion.t);
			sourceSurface->modifiedRegion = { (int32_t) graphics.width, 0, (int32_t) graphics.height, 0 };
		}
	}

	sourceSurface->Copy(&windowManager.cursorSwap, ES_POINT(cursorBounds.l, cursorBounds.t), ES_RECT_4(0, Width(cursorBounds), 0, Height(cursorBounds)), true);
}

bool KGraphicsIsTargetRegistered() {
	return graphics.target ? true : false;
}

void KRegisterGraphicsTarget(KGraphicsTarget *target) {
	// TODO Locking.
	if (graphics.target) return;

	graphics.target = target;

	graphics.width = target->screenWidth;
	graphics.height = target->screenHeight;

	graphics.frameBuffer.Resize(graphics.width, graphics.height);

#ifdef START_DEBUG_OUTPUT
	StartDebugOutput();
	EsPrint("Hello\n");
#else
	windowManager.Initialise();

	_EsMessageWithObject m;
	EsMemoryZero(&m, sizeof(m));
	m.message.type = ES_MSG_SET_SCREEN_RESOLUTION;
	DesktopSendMessage(&m);
#endif
}

bool Surface::Resize(size_t newResX, size_t newResY, uint32_t clearColor, bool copyOldBits) {
	// Check the surface is within our working size limits.
	if (!newResX || !newResY || newResX >= 32767 || newResY >= 32767) {
		return false;
	}

	if (width == newResX && height == newResY) {
		return true;
	}

	uint8_t *newBits = (uint8_t *) EsHeapAllocate(newResX * newResY * 4, !copyOldBits, K_PAGED);

	if (!newBits) {
		return false;
	}

	int oldWidth = width, oldHeight = height, oldStride = stride;
	void *oldBits = bits;

	width = newResX, height = newResY, bits = newBits;
	stride = newResX * 4;

	EsPainter painter;
	painter.clip = ES_RECT_4(0, width, 0, height);
	painter.target = this;

	if (copyOldBits) {
		EsDrawBitmap(&painter, ES_RECT_4(0, oldWidth, 0, oldHeight), (uint32_t *) oldBits, oldStride, ES_DRAW_BITMAP_OPAQUE);

		if (clearColor) {
			EsDrawBlock(&painter, ES_RECT_4(oldWidth, width, 0, height), clearColor);
			EsDrawBlock(&painter, ES_RECT_4(0, oldWidth, oldHeight, height), clearColor);
		} else {
			EsDrawClear(&painter, ES_RECT_4(oldWidth, width, 0, height));
			EsDrawClear(&painter, ES_RECT_4(0, oldWidth, oldHeight, height));
		}
	}

	EsHeapFree(oldBits, 0, K_PAGED);

	__sync_fetch_and_add(&graphics.totalSurfaceBytes, newResX * newResY * 4 - oldWidth * oldHeight * 4);

	return true;
}

void Surface::Copy(Surface *source, EsPoint destinationPoint, EsRectangle sourceRegion, bool addToModifiedRegion) {
	EsRectangle destinationRegion = ES_RECT_4(destinationPoint.x, destinationPoint.x + Width(sourceRegion), 
			destinationPoint.y, destinationPoint.y + Height(sourceRegion));

	if (addToModifiedRegion) {
		modifiedRegion = EsRectangleBounding(destinationRegion, modifiedRegion);
		EsRectangleClip(modifiedRegion, ES_RECT_4(0, width, 0, height), &modifiedRegion);
	}

	EsPainter painter;
	painter.clip = ES_RECT_4(0, width, 0, height);
	painter.target = this;
	uint8_t *sourceBits = (uint8_t *) source->bits + source->stride * sourceRegion.t + 4 * sourceRegion.l;
	EsDrawBitmap(&painter, destinationRegion, (uint32_t *) sourceBits, source->stride, ES_DRAW_BITMAP_OPAQUE);
}

void Surface::SetBits(K_USER_BUFFER const void *_bits, uintptr_t sourceStride, EsRectangle bounds) {
	if (Width(bounds) < 0 || Height(bounds) < 0 || bounds.l < 0 || bounds.t < 0 || bounds.r > (int32_t) width || bounds.b > (int32_t) height) {
		KernelPanic("Surface::SetBits - Invalid bounds %R for surface %x.\n", bounds, this);
	}

	if (Width(bounds) == 0 || Height(bounds) == 0) {
		return;
	}

	modifiedRegion = EsRectangleBounding(bounds, modifiedRegion);

	uint32_t *rowStart = (uint32_t *) bits + bounds.l + bounds.t * stride / 4;
	K_USER_BUFFER const uint32_t *sourceRowStart = (K_USER_BUFFER const uint32_t *) _bits;

	for (uintptr_t i = bounds.t; i < (uintptr_t) bounds.b; i++, rowStart += stride / 4, sourceRowStart += sourceStride / 4) {
		size_t count = Width(bounds);
		uint32_t *destination = rowStart;
		K_USER_BUFFER const uint32_t *bits = sourceRowStart;

		do {
			*destination = *bits;
			destination++, bits++, count--;
		} while (count);
	}
}

void Surface::Scroll(EsRectangle region, ptrdiff_t delta, bool vertical) {
	if (vertical) {
		if (delta > 0) {
			for (intptr_t i = region.t; i < region.b; i++) {
				for (intptr_t j = region.l; j < region.r; j++) {
					((uint32_t *) bits)[j + (i - delta) * stride / 4] = ((uint32_t *) bits)[j + i * stride / 4];
				}
			}
		} else {
			for (intptr_t i = region.b - 1; i >= region.t; i--) {
				for (intptr_t j = region.l; j < region.r; j++) {
					((uint32_t *) bits)[j + (i - delta) * stride / 4] = ((uint32_t *) bits)[j + i * stride / 4];
				}
			}
		}
	} else {
		if (delta > 0) {
			for (intptr_t i = region.t; i < region.b; i++) {
				for (intptr_t j = region.l; j < region.r; j++) {
					((uint32_t *) bits)[j - delta + i * stride / 4] = ((uint32_t *) bits)[j + i * stride / 4];
				}
			}
		} else {
			for (intptr_t i = region.t; i < region.b; i++) {
				for (intptr_t j = region.r - 1; j >= region.l; j--) {
					((uint32_t *) bits)[j - delta + i * stride / 4] = ((uint32_t *) bits)[j + i * stride / 4];
				}
			}
		}
	}
}

#define C0(p) ((p & 0x000000FF) >> 0x00)
#define C1(p) ((p & 0x0000FF00) >> 0x08)
#define C2(p) ((p & 0x00FF0000) >> 0x10)
#define C3(p) ((p & 0xFF000000) >> 0x18)

ES_FUNCTION_OPTIMISE_O3
void BlurRegionOfImage(uint32_t *image, int width, int height, int stride, uint16_t *k, uintptr_t repeat) {
	if (width <= 3 || height <= 3) {
		return;
	}

	for (int y = 0; y < height; y++) {
		for (uintptr_t i = 0; i < repeat; i++) {
			uint32_t *start = image + stride * y;
			uint32_t a = start[0], b = start[0], c = start[0], d = start[0], e = start[1], f = start[2], g = 0;
			uint32_t *u = start, *v = start + 3;

			for (int i = 0; i < width; i++, u++, v++) {
				if (i + 3 < width) g = *v;
				*u =      (((C0(a) * k[0] + C0(b) * k[1] + C0(c) * k[2] + C0(d) * k[3] + C0(e) * k[4] + C0(f) * k[5] + C0(g) * k[6]) >> 8) << 0x00)
					+ (((C1(a) * k[0] + C1(b) * k[1] + C1(c) * k[2] + C1(d) * k[3] + C1(e) * k[4] + C1(f) * k[5] + C1(g) * k[6]) >> 8) << 0x08)
					+ (((C2(a) * k[0] + C2(b) * k[1] + C2(c) * k[2] + C2(d) * k[3] + C2(e) * k[4] + C2(f) * k[5] + C2(g) * k[6]) >> 8) << 0x10)
					+ (C3(d) << 0x18);
				a = b, b = c, c = d, d = e, e = f, f = g;
			}
		}
	}

	for (int x = 0; x < width; x++) {
		for (uintptr_t i = 0; i < repeat; i++) {
			uint32_t *start = image + x;
			uint32_t a = start[0], b = start[0], c = start[0], d = start[0], e = start[stride], f = start[stride * 2], g = 0;
			uint32_t *u = start, *v = start + 3 * stride;

			for (int i = 0; i < height; i++, u += stride, v += stride) {
				if (i + 3 < height) g = *v;
				*u =      (((C0(a) * k[0] + C0(b) * k[1] + C0(c) * k[2] + C0(d) * k[3] + C0(e) * k[4] + C0(f) * k[5] + C0(g) * k[6]) >> 8) << 0x00)
					+ (((C1(a) * k[0] + C1(b) * k[1] + C1(c) * k[2] + C1(d) * k[3] + C1(e) * k[4] + C1(f) * k[5] + C1(g) * k[6]) >> 8) << 0x08)
					+ (((C2(a) * k[0] + C2(b) * k[1] + C2(c) * k[2] + C2(d) * k[3] + C2(e) * k[4] + C2(f) * k[5] + C2(g) * k[6]) >> 8) << 0x10)
					+ (C3(d) << 0x18);
				a = b, b = c, c = d, d = e, e = f, f = g;
			}
		}
	}
}

ES_FUNCTION_OPTIMISE_O3
void BlurRegionOfImage(uint32_t *image, int width, int height, int stride, uintptr_t repeat) {
	if (width <= 3 || height <= 3) {
		return;
	}

	for (uintptr_t i = 0; i < repeat; i++) {
		uint32_t *start = image;

		for (int y = 0; y < height; y++) {
			uint32_t a = start[0], b = start[0], c = start[0], d = start[0], e = start[1], f = start[2], g = 0;
			uint32_t *u = start, *v = start + 3;

			for (int i = 0; i < width; i++, u++, v++) {
				if (i + 3 < width) g = *v;
				*u = 	  (((C0(a) * 0x07 + C0(b) * 0x1A + C0(c) * 0x38 + C0(d) * 0x4D + C0(e) * 0x38 + C0(f) * 0x1A + C0(g) * 0x07) >> 8) << 0x00)
					+ (((C1(a) * 0x07 + C1(b) * 0x1A + C1(c) * 0x38 + C1(d) * 0x4D + C1(e) * 0x38 + C1(f) * 0x1A + C1(g) * 0x07) >> 8) << 0x08)
					+ (((C2(a) * 0x07 + C2(b) * 0x1A + C2(c) * 0x38 + C2(d) * 0x4D + C2(e) * 0x38 + C2(f) * 0x1A + C2(g) * 0x07) >> 8) << 0x10)
					+ (C3(d) << 0x18);
				a = b, b = c, c = d, d = e, e = f, f = g;
			}

			start += stride;
		}

		start = image;

		for (int x = 0; x < width; x++) {
			uint32_t a = start[0], b = start[0], c = start[0], d = start[0], e = start[stride], f = start[stride * 2], g = 0;
			uint32_t *u = start, *v = start + 3 * stride;

			for (int i = 0; i < height; i++, u += stride, v += stride) {
				if (i + 3 < height) g = *v;
				*u = 	  (((C0(a) * 0x07 + C0(b) * 0x1A + C0(c) * 0x38 + C0(d) * 0x4D + C0(e) * 0x38 + C0(f) * 0x1A + C0(g) * 0x07) >> 8) << 0x00)
					+ (((C1(a) * 0x07 + C1(b) * 0x1A + C1(c) * 0x38 + C1(d) * 0x4D + C1(e) * 0x38 + C1(f) * 0x1A + C1(g) * 0x07) >> 8) << 0x08)
					+ (((C2(a) * 0x07 + C2(b) * 0x1A + C2(c) * 0x38 + C2(d) * 0x4D + C2(e) * 0x38 + C2(f) * 0x1A + C2(g) * 0x07) >> 8) << 0x10)
					+ (C3(d) << 0x18);
				a = b, b = c, c = d, d = e, e = f, f = g;
			}

			start++;
		}
	}
}

void Surface::Blur(EsRectangle region, EsRectangle clip) {
#if 1
	if (!EsRectangleClip(region, ES_RECT_4(0, width, 0, height), &region)) {
		return;
	}

	if (!EsRectangleClip(region, clip, &region)) {
		return;
	}

	BlurRegionOfImage((uint32_t *) ((uint8_t *) bits + region.l * 4 + region.t * stride), Width(region), Height(region), width, 1);
#else
	EsPainter painter;
	painter.clip = ES_RECT_4(0, width, 0, height);
	painter.target = this;
	EsDrawInvert(&painter, EsRectangleIntersection(region, clip));
#endif
}

ES_FUNCTION_OPTIMISE_O2 
void Surface::BlendWindow(Surface *source, EsPoint destinationPoint, EsRectangle sourceRegion, int material, uint8_t alpha, EsRectangle materialRegion) {
	if (destinationPoint.x < 0) { sourceRegion.l -= destinationPoint.x; destinationPoint.x = 0; }
	if (destinationPoint.y < 0) { sourceRegion.t  -= destinationPoint.y; destinationPoint.y = 0; }
	if (destinationPoint.x + sourceRegion.r  - sourceRegion.l >= (int) width) sourceRegion.r -= destinationPoint.x + sourceRegion.r  - sourceRegion.l - width;
	if (destinationPoint.y + sourceRegion.b - sourceRegion.t  >= (int) height) sourceRegion.b -= destinationPoint.y + sourceRegion.b - sourceRegion.t  - height;
	if (sourceRegion.r > (int) source->width) sourceRegion.r = source->width;
	if (sourceRegion.b > (int) source->height) sourceRegion.b = source->height;
	if (sourceRegion.r <= sourceRegion.l) return;
	if (sourceRegion.b <= sourceRegion.t) return;
	if (sourceRegion.l < 0) return;
	if (sourceRegion.t < 0) return;

	EsRectangle destinationRegion = ES_RECT_4(destinationPoint.x, destinationPoint.x + Width(sourceRegion), 
			destinationPoint.y, destinationPoint.y + Height(sourceRegion));
	modifiedRegion = EsRectangleBounding(destinationRegion, modifiedRegion);

	if (material == BLEND_WINDOW_MATERIAL_GLASS || material == BLEND_WINDOW_MATERIAL_LIGHT_BLUR) {
		int repeat = material == BLEND_WINDOW_MATERIAL_GLASS ? 3 : 1;

#ifndef SIMPLE_GRAPHICS
		if (alpha == 0xFF) {
			BlurRegionOfImage((uint32_t *) bits + materialRegion.l + materialRegion.t * width, 
					Width(materialRegion), Height(materialRegion), width, repeat);
		} else {
			uint16_t kernel[] = { 0x07, 0x1A, 0x38, 0, 0x38, 0x1A, 0x07 };
			uint16_t sum = 0;

			for (uintptr_t i = 0; i < 7; i++) {
				kernel[i] = kernel[i] * alpha / 0xFF;
				sum += kernel[i];
			}

			kernel[3] = 0xFF - sum;

			BlurRegionOfImage((uint32_t *) bits + materialRegion.l + materialRegion.t * width, 
					Width(materialRegion), Height(materialRegion), width, kernel, repeat);
		}
#else
		(void) materialRegion;
		(void) alpha;
#endif
	}

	intptr_t y = sourceRegion.t;

	uint8_t *destinationPixel = (uint8_t *) bits + destinationPoint.y * stride + destinationPoint.x * 4;
	uint8_t *sourcePixel = (uint8_t *) source->bits + sourceRegion.t * source->stride + sourceRegion.l * 4;

#ifndef SIMPLE_GRAPHICS
	__m128i constantAlphaMask = _mm_set1_epi32(0xFF000000);
	__m128i constantAlpha = _mm_set1_epi32(alpha);
	__m128i constant255 = _mm_set1_epi32(0xFF);
#endif

	while (y < sourceRegion.b) {
		size_t countX = sourceRegion.r - sourceRegion.l;

		uint8_t *a = destinationPixel, *b = sourcePixel;

		while (countX >= 4) {
			__m128i sourceValue 	 = _mm_loadu_si128((__m128i *) sourcePixel);

#ifndef SIMPLE_GRAPHICS
			__m128i destinationValue = _mm_loadu_si128((__m128i *) destinationPixel);

			if (alpha != 0xFF) {
				sourceValue = _mm_or_si128(_mm_andnot_si128(constantAlphaMask, sourceValue), 
						_mm_and_si128(_mm_slli_epi32(_mm_mullo_epi16(_mm_srli_epi32(sourceValue, 24), constantAlpha), 16), constantAlphaMask));
			}

			__m128i alpha = _mm_srli_epi32(sourceValue, 24);

			__m128i red 	= _mm_mullo_epi16(_mm_and_si128(_mm_srli_epi32(sourceValue, 0),  constant255), alpha);
			__m128i green 	= _mm_mullo_epi16(_mm_and_si128(_mm_srli_epi32(sourceValue, 8),  constant255), alpha);
			__m128i blue 	= _mm_mullo_epi16(_mm_and_si128(_mm_srli_epi32(sourceValue, 16), constant255), alpha);

			alpha = _mm_sub_epi32(constant255, alpha);

			red 	= _mm_srli_epi32(_mm_add_epi32(red,   _mm_mullo_epi16(_mm_and_si128(_mm_srli_epi32(destinationValue, 0),  constant255), alpha)), 8);
			green 	= _mm_srli_epi32(_mm_add_epi32(green, _mm_mullo_epi16(_mm_and_si128(_mm_srli_epi32(destinationValue, 8),  constant255), alpha)), 8);
			blue 	= _mm_srli_epi32(_mm_add_epi32(blue,  _mm_mullo_epi16(_mm_and_si128(_mm_srli_epi32(destinationValue, 16), constant255), alpha)), 8);

			sourceValue = _mm_or_si128(_mm_slli_epi32(red, 0), _mm_or_si128(_mm_slli_epi32(green, 8), _mm_slli_epi32(blue, 16)));
#endif

			_mm_storeu_si128((__m128i *) destinationPixel, sourceValue);

			destinationPixel += 16;
			sourcePixel += 16;
			countX -= 4;
		}

		while (countX >= 1) {
			uint32_t modified = *(uint32_t *) sourcePixel;
#ifndef SIMPLE_GRAPHICS
			uint32_t original = *(uint32_t *) destinationPixel;
			if (alpha != 0xFF) modified = (modified & 0xFFFFFF) | (((((modified & 0xFF000000) >> 24) * alpha) << 16) & 0xFF000000);
			uint32_t m1 = (modified & 0xFF000000) >> 24;
			uint32_t m2 = 255 - m1;
			uint32_t r2 = m2 * (original & 0x00FF00FF);
			uint32_t g2 = m2 * (original & 0x0000FF00);
			uint32_t r1 = m1 * (modified & 0x00FF00FF);
			uint32_t g1 = m1 * (modified & 0x0000FF00);
			uint32_t result = (0x0000FF00 & ((g1 + g2) >> 8)) | (0x00FF00FF & ((r1 + r2) >> 8));
#else
			uint32_t result = modified;
#endif

			*(uint32_t *) destinationPixel = result;

			destinationPixel += 4;
			sourcePixel += 4;
			countX -= 1;
		}

		y++;
		destinationPixel = a + stride;
		sourcePixel = b + source->stride;
	}
}

void Surface::Draw(Surface *source, EsRectangle destinationRegion, int sourceX, int sourceY, uint16_t alpha) {
	modifiedRegion = EsRectangleBounding(destinationRegion, modifiedRegion);
	EsRectangleClip(modifiedRegion, ES_RECT_4(0, width, 0, height), &modifiedRegion);
	EsPainter painter;
	painter.clip = ES_RECT_4(0, width, 0, height);
	painter.target = this;
	uint8_t *sourceBits = (uint8_t *) source->bits + source->stride * sourceY + 4 * sourceX;
	EsDrawBitmap(&painter, destinationRegion, (uint32_t *) sourceBits, source->stride, alpha);
}

void Surface::CreateCursorShadow(Surface *temporary) {
	const uint32_t kernel[] = { 14, 43, 82, 43, 14 };
	uint32_t *bits1 = (uint32_t *) bits;
	uint32_t *bits2 = (uint32_t *) temporary->bits;

	for (int32_t i = 0; i < (int32_t) height; i++) {
		for (int32_t j = 0; j < (int32_t) width; j++) {
			uint32_t s = 0;

			for (int32_t k = 0; k < 5; k++) {
				int32_t l = j + k - 2;

				if (l >= 0 && l < (int32_t) width) {
					s += ((bits1[i * stride / 4 + l] & 0xFF000000) >> 24) * kernel[k];
				}
			}

			bits2[i * temporary->stride / 4 + j] = s;
		}
	}

	for (int32_t i = 0; i < (int32_t) height - CURSOR_SHADOW_OFFSET; i++) {
		for (int32_t j = 0; j < (int32_t) width - CURSOR_SHADOW_OFFSET; j++) {
			uint32_t s = 0;

			for (int32_t k = 0; k < 5; k++) {
				int32_t l = i + k - 2;

				if (l >= 0 && l < (int32_t) height) {
					s += bits2[l * temporary->stride / 4 + j] * kernel[k];
				}
			}

			uint32_t *out = &bits1[(i + CURSOR_SHADOW_OFFSET) * stride / 4 + (j + CURSOR_SHADOW_OFFSET)];
			*out = EsColorBlend((s >> 16) << 24, *out, true);
		}
	}
}

void GraphicsUpdateScreen32(K_USER_BUFFER const uint8_t *_source, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t sourceStride, 
		uint32_t destinationX, uint32_t destinationY,
		uint32_t screenWidth, uint32_t screenHeight, uint32_t stride, volatile uint8_t *pixel) {
	uint32_t *destinationRowStart = (uint32_t *) (pixel + destinationX * 4 + destinationY * stride);
	const uint32_t *sourceRowStart = (const uint32_t *) _source;

	if (destinationX > screenWidth || sourceWidth > screenWidth - destinationX
			|| destinationY > screenHeight || sourceHeight > screenHeight - destinationY) {
		KernelPanic("GraphicsUpdateScreen32 - Update region outside graphics target bounds.\n");
	}

	for (uintptr_t y = 0; y < sourceHeight; y++, destinationRowStart += stride / 4, sourceRowStart += sourceStride / 4) {
		uint32_t *destination = destinationRowStart;
		const uint32_t *source = sourceRowStart;

		for (uintptr_t x = 0; x < sourceWidth; x++) {
			*destination = *source;
			destination++, source++;
		}
	}
}

void GraphicsUpdateScreen24(K_USER_BUFFER const uint8_t *_source, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t sourceStride, 
		uint32_t destinationX, uint32_t destinationY,
		uint32_t screenWidth, uint32_t screenHeight, uint32_t stride, volatile uint8_t *pixel) {
	uint8_t *destinationRowStart = (uint8_t *) (pixel + destinationX * 3 + destinationY * stride);
	const uint8_t *sourceRowStart = _source;

	if (destinationX > screenWidth || sourceWidth > screenWidth - destinationX
			|| destinationY > screenHeight || sourceHeight > screenHeight - destinationY) {
		KernelPanic("GraphicsUpdateScreen32 - Update region outside graphics target bounds.\n");
	}

	for (uintptr_t y = 0; y < sourceHeight; y++, destinationRowStart += stride, sourceRowStart += sourceStride) {
		uint8_t *destination = destinationRowStart;
		const uint8_t *source = sourceRowStart;

		for (uintptr_t x = 0; x < sourceWidth; x++) {
			*destination++ = *source++;
			*destination++ = *source++;
			*destination++ = *source++;
			source++;
		}
	}
}

void GraphicsDebugPutBlock32(uintptr_t x, uintptr_t y, bool toggle,
		unsigned screenWidth, unsigned screenHeight, unsigned stride, volatile uint8_t *linearBuffer) {
	(void) screenWidth;
	(void) screenHeight;

	if (toggle) {
		linearBuffer[y * stride + x * 4 + 0] += 0x4C;
		linearBuffer[y * stride + x * 4 + 1] += 0x4C;
		linearBuffer[y * stride + x * 4 + 2] += 0x4C;
	} else {
		linearBuffer[y * stride + x * 4 + 0] = 0xFF;
		linearBuffer[y * stride + x * 4 + 1] = 0xFF;
		linearBuffer[y * stride + x * 4 + 2] = 0xFF;
	}

	linearBuffer[(y + 1) * stride + (x + 1) * 4 + 0] = 0;
	linearBuffer[(y + 1) * stride + (x + 1) * 4 + 1] = 0;
	linearBuffer[(y + 1) * stride + (x + 1) * 4 + 2] = 0;
}

void GraphicsDebugClearScreen32(unsigned screenWidth, unsigned screenHeight, unsigned stride, volatile uint8_t *linearBuffer) {
	for (uintptr_t i = 0; i < screenHeight; i++) {
		for (uintptr_t j = 0; j < screenWidth * 4; j += 4) {

#if 0
			linearBuffer[i * stride + j + 2] = 0x18;
			linearBuffer[i * stride + j + 1] = 0x7E;
			linearBuffer[i * stride + j + 0] = 0xCF;
#else
			if (graphics.debuggerActive) {
				linearBuffer[i * stride + j + 2] = 0x18;
				linearBuffer[i * stride + j + 1] = 0x7E;
				linearBuffer[i * stride + j + 0] = 0xCF;
			} else {
				linearBuffer[i * stride + j + 2] >>= 1;
				linearBuffer[i * stride + j + 1] >>= 1;
				linearBuffer[i * stride + j + 0] >>= 1;
			}
#endif
		}
	}
}

#undef C0
#undef C1
#undef C2
#undef C3

#endif
