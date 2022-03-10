// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

/////////////////////////////////
// EsRectangle utility functions.
/////////////////////////////////

#if defined(SHARED_COMMON_WANT_RECTANGLES) || defined(SHARED_COMMON_WANT_ALL)

struct Corners8 { int8_t tl, tr, bl, br; };
struct Corners32 { int32_t tl, tr, bl, br; };
struct Rectangle8 { int8_t l, r, t, b; };
struct Rectangle16 { int16_t l, r, t, b; };
struct Rectangle32 { int32_t l, r, t, b; };

#define RECT16_TO_RECT(x) ((EsRectangle) { (x).l, (x).r, (x).t, (x).b })

inline int Width(EsRectangle rectangle) {
	return rectangle.r - rectangle.l;
}

inline int Height(EsRectangle rectangle) {
	return rectangle.b - rectangle.t;
}

EsRectangle Translate(EsRectangle rectangle, int x, int y) {
	rectangle.l += x, rectangle.r += x;
	rectangle.t += y, rectangle.b += y;
	return rectangle;
}

bool EsRectangleClip(EsRectangle parent, EsRectangle rectangle, EsRectangle *output) {
	EsRectangle current = parent;
	EsRectangle intersection;

	if (!((current.l > rectangle.r && current.r > rectangle.l)
			|| (current.t > rectangle.b && current.b > rectangle.t))) {
		intersection.l = current.l > rectangle.l ? current.l : rectangle.l;
		intersection.t = current.t > rectangle.t ? current.t : rectangle.t;
		intersection.r = current.r < rectangle.r ? current.r : rectangle.r;
		intersection.b = current.b < rectangle.b ? current.b : rectangle.b;
	} else {
		intersection = {};
	}

	if (output) {
		*output = intersection;
	}

	return intersection.l < intersection.r && intersection.t < intersection.b;
}

#endif

#ifdef SHARED_COMMON_WANT_ALL

EsRectangle EsRectangleAddBorder(EsRectangle rectangle, EsRectangle border) {
	rectangle.l += border.l;
	rectangle.r -= border.r;
	rectangle.t += border.t;
	rectangle.b -= border.b;
	return rectangle;
}

EsRectangle EsRectangleAdd(EsRectangle a, EsRectangle b) {
	a.l += b.l;
	a.t += b.t;
	a.r += b.r;
	a.b += b.b;
	return a;
}

EsRectangle EsRectangleTranslate(EsRectangle a, EsRectangle b) {
	a.l += b.l;
	a.t += b.t;
	a.r += b.l;
	a.b += b.t;
	return a;
}

EsRectangle EsRectangleSubtract(EsRectangle a, EsRectangle b) {
	a.l -= b.l;
	a.t -= b.t;
	a.r -= b.r;
	a.b -= b.b;
	return a;
}

EsRectangle EsRectangleBounding(EsRectangle a, EsRectangle b) {
	if (a.l > b.l) a.l = b.l;
	if (a.t > b.t) a.t = b.t;
	if (a.r < b.r) a.r = b.r;
	if (a.b < b.b) a.b = b.b;
	return a;
}

__attribute__((no_instrument_function))
EsRectangle EsRectangleIntersection(EsRectangle a, EsRectangle b) {
	if (a.l < b.l) a.l = b.l;
	if (a.t < b.t) a.t = b.t;
	if (a.r > b.r) a.r = b.r;
	if (a.b > b.b) a.b = b.b;
	return a;
}

EsRectangle EsRectangleCenter(EsRectangle parent, EsRectangle child) {
	int childWidth = Width(child), childHeight = Height(child);
	int parentWidth = Width(parent), parentHeight = Height(parent);
	child.l = parentWidth / 2 - childWidth / 2 + parent.l, child.r = child.l + childWidth;
	child.t = parentHeight / 2 - childHeight / 2 + parent.t, child.b = child.t + childHeight;
	return child;
}

EsRectangle EsRectangleFit(EsRectangle parent, EsRectangle child, bool allowScalingUp) {
	int childWidth = Width(child), childHeight = Height(child);
	int parentWidth = Width(parent), parentHeight = Height(parent);

	if (childWidth < parentWidth && childHeight < parentHeight && !allowScalingUp) {
		return EsRectangleCenter(parent, child);
	}

	float childAspectRatio = (float) childWidth / childHeight;
	int childMaximumWidth = parentHeight * childAspectRatio;
	int childMaximumHeight = parentWidth / childAspectRatio;

	if (childMaximumWidth > parentWidth) {
		return EsRectangleCenter(parent, ES_RECT_2S(parentWidth, childMaximumHeight));
	} else {
		return EsRectangleCenter(parent, ES_RECT_2S(childMaximumWidth, parentHeight));
	}
}

bool EsRectangleEquals(EsRectangle a, EsRectangle b) {
	return a.l == b.l && a.r == b.r && a.t == b.t && a.b == b.b;
}

bool EsRectangleContains(EsRectangle a, int32_t x, int32_t y) {
	return ES_RECT_VALID(a) && a.l <= x && a.r > x && a.t <= y && a.b > y;
}

bool EsRectangleContainsAll(EsRectangle parent, EsRectangle child) {
	return ES_RECT_VALID(parent) && child.l >= parent.l && child.r <= parent.r && child.t >= parent.t && child.b <= parent.b;
}

EsRectangle EsRectangleSplit(EsRectangle *a, int32_t amount, char side, int32_t gap) {
	EsRectangle b = *a;
	if (side == 'l') a->l += amount + gap, b.r = a->l - gap;
	if (side == 'r') a->r -= amount + gap, b.l = a->r + gap;
	if (side == 't') a->t += amount + gap, b.b = a->t - gap;
	if (side == 'b') a->b -= amount + gap, b.t = a->b + gap;
	return b;
}

EsRectangle EsRectangleCut(EsRectangle a, int32_t amount, char side) {
	return EsRectangleSplit(&a, amount, side, 0);
}

#endif

/////////////////////////////////
// Rendering.
/////////////////////////////////

#if defined(SHARED_COMMON_WANT_RENDERING) || defined(SHARED_COMMON_WANT_ALL)

ES_FUNCTION_OPTIMISE_O3 
__attribute__((no_instrument_function))
void BlendPixel(uint32_t *destinationPixel, uint32_t modified, bool fullAlpha) {
	if ((modified & 0xFF000000) == 0xFF000000) {
		*destinationPixel = modified;
		return;
	} else if ((modified & 0xFF000000) == 0x00000000) {
		return;
	}

	uint32_t m1, m2, a;
	uint32_t original = *destinationPixel;

	if ((*destinationPixel & 0xFF000000) != 0xFF000000 && fullAlpha) {
		uint32_t alpha1 = (modified & 0xFF000000) >> 24;
		uint32_t alpha2 = 255 - alpha1;
		uint32_t alphaD = (original & 0xFF000000) >> 24;
		uint32_t alphaD2 = alphaD * alpha2;
		uint32_t alphaOut = alpha1 + (alphaD2 >> 8);

		if (!alphaOut) {
			return;
		}

		m2 = alphaD2 / alphaOut;
		m1 = (alpha1 << 8) / alphaOut;
		if (m2 == 0x100) m2--;
		if (m1 == 0x100) m1--;
		a = alphaOut << 24;
	} else {
		m1 = (modified & 0xFF000000) >> 24;
		m2 = 255 - m1;
		a = 0xFF000000;
	}

	uint32_t r2 = m2 * (original & 0x00FF00FF);
	uint32_t g2 = m2 * (original & 0x0000FF00);
	uint32_t r1 = m1 * (modified & 0x00FF00FF);
	uint32_t g1 = m1 * (modified & 0x0000FF00);
	uint32_t result = a | (0x0000FF00 & ((g1 + g2) >> 8)) | (0x00FF00FF & ((r1 + r2) >> 8));
	*destinationPixel = result;
}

void _DrawBlock(uintptr_t stride, void *bits, EsRectangle bounds, uint32_t color, bool fullAlpha) {
	stride /= 4;
	uint32_t *lineStart = (uint32_t *) bits + bounds.t * stride + bounds.l;

	__m128i color4 = _mm_set_epi32(color, color, color, color);

	for (int i = 0; i < bounds.b - bounds.t; i++, lineStart += stride) {
		uint32_t *destination = lineStart;
		int j = bounds.r - bounds.l;

		if ((color & 0xFF000000) != 0xFF000000) {
			do {
				BlendPixel(destination, color, fullAlpha);
				destination++;
			} while (--j);
		} else {
			while (j >= 4) {
				_mm_storeu_si128((__m128i *) destination, color4);
				destination += 4;
				j -= 4;
			} 

			while (j > 0) {
				*destination = color;
				destination++;
				j--;
			} 
		}
	}
}

#endif

#ifdef SHARED_COMMON_WANT_ALL

uint32_t EsColorBlend(uint32_t under, uint32_t over, bool fullAlpha) {
	BlendPixel(&under, over, fullAlpha);
	return under;
}

struct EsPaintTarget {
	void *bits;
	uint32_t width, height, stride;
	bool fullAlpha, readOnly, fromBitmap, forWindowManager;
};

void EsDrawInvert(EsPainter *painter, EsRectangle bounds) {
	EsPaintTarget *target = painter->target;

	if (!EsRectangleClip(bounds, painter->clip, &bounds)) {
		return;
	}

	uintptr_t stride = target->stride / 4;
	uint32_t *lineStart = (uint32_t *) target->bits + bounds.t * stride + bounds.l;

	__m128i mask = _mm_set_epi32(0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF);

	for (int i = 0; i < bounds.b - bounds.t; i++, lineStart += stride) {
		uint32_t *destination = lineStart;
		int j = bounds.r - bounds.l;

		while (j >= 4) {
			_mm_storeu_si128((__m128i *) destination, _mm_xor_si128(_mm_loadu_si128((__m128i *) destination), mask));
			destination += 4;
			j -= 4;
		} 

		while (j > 0) {
			*destination ^= 0xFFFFFF;
			destination++;
			j--;
		} 
	}
}

void EsDrawClear(EsPainter *painter, EsRectangle bounds) {
	EsPaintTarget *target = painter->target;

	if (!EsRectangleClip(bounds, painter->clip, &bounds)) {
		return;
	}

	uintptr_t stride = target->stride / 4;
	uint32_t *lineStart = (uint32_t *) target->bits + bounds.t * stride + bounds.l;

	__m128i zero = {};

	for (int i = 0; i < bounds.b - bounds.t; i++, lineStart += stride) {
		uint32_t *destination = lineStart;
		int j = bounds.r - bounds.l;

		while (j >= 4) {
			_mm_storeu_si128((__m128i *) destination, zero);
			destination += 4;
			j -= 4;
		} 

		while (j > 0) {
			*destination = 0;
			destination++;
			j--;
		} 
	}
}

void EsDrawBlock(EsPainter *painter, EsRectangle bounds, uint32_t color) {
	if (!(color & 0xFF000000)) {
		return;
	}

	EsPaintTarget *target = painter->target;

	if (!EsRectangleClip(bounds, painter->clip, &bounds)) {
		return;
	}
	
	_DrawBlock(target->stride, target->bits, bounds, color, target->fullAlpha);
}

void EsDrawBitmap(EsPainter *painter, EsRectangle region, const uint32_t *sourceBits, uintptr_t sourceStride, uint16_t mode) {
	EsPaintTarget *target = painter->target;
	EsRectangle bounds;

	if (!EsRectangleClip(region, painter->clip, &bounds)) {
		return;
	}

	sourceStride /= 4;
	uintptr_t stride = target->stride / 4;
	uint32_t *lineStart = (uint32_t *) target->bits + bounds.t * stride + bounds.l;
	const uint32_t *sourceLineStart = sourceBits + (bounds.l - region.l) + sourceStride * (bounds.t - region.t);

	for (int i = 0; i < bounds.b - bounds.t; i++, lineStart += stride, sourceLineStart += sourceStride) {
		uint32_t *destination = lineStart;
		const uint32_t *source = sourceLineStart;
		int j = bounds.r - bounds.l;

		if (mode == 0xFF) {
			do {
				BlendPixel(destination, *source, target->fullAlpha);
				destination++;
				source++;
			} while (--j);
		} else if (mode <= 0xFF) {
			do {
				uint32_t modified = *source;
				modified = (modified & 0xFFFFFF) | (((((modified & 0xFF000000) >> 24) * mode) << 16) & 0xFF000000);
				BlendPixel(destination, modified, target->fullAlpha);
				destination++;
				source++;
			} while (--j);
		} else if (mode == ES_DRAW_BITMAP_XOR) {
			while (j >= 4) {
				__m128i *_destination = (__m128i *) destination;
				_mm_storeu_si128(_destination, _mm_xor_si128(_mm_loadu_si128((__m128i *) source), _mm_loadu_si128(_destination)));
				destination += 4;
				source += 4;
				j -= 4;
			} 

			while (j > 0) {
				*destination ^= *source;
				destination++;
				source++;
				j--;
			} 
		} else if (mode == ES_DRAW_BITMAP_OPAQUE) {
			__m128i fillAlpha = _mm_set1_epi32(0xFF000000);

			while (j >= 4) {
				_mm_storeu_si128((__m128i *) destination, _mm_or_si128(fillAlpha, _mm_loadu_si128((__m128i *) source)));
				destination += 4;
				source += 4;
				j -= 4;
			} 

			while (j > 0) {
				*destination = 0xFF000000 | *source;
				destination++;
				source++;
				j--;
			} 
		}
	}
}

void EsDrawRectangle(EsPainter *painter, EsRectangle r, uint32_t mainColor, uint32_t borderColor, EsRectangle borderSize) {
	EsDrawBlock(painter, ES_RECT_4(r.l, r.r, r.t, r.t + borderSize.t), borderColor);
	EsDrawBlock(painter, ES_RECT_4(r.l, r.l + borderSize.l, r.t + borderSize.t, r.b - borderSize.b), borderColor);
	EsDrawBlock(painter, ES_RECT_4(r.r - borderSize.r, r.r, r.t + borderSize.t, r.b - borderSize.b), borderColor);
	EsDrawBlock(painter, ES_RECT_4(r.l, r.r, r.b - borderSize.b, r.b), borderColor);
	EsDrawBlock(painter, ES_RECT_4(r.l + borderSize.l, r.r - borderSize.r, r.t + borderSize.t, r.b - borderSize.b), mainColor);
}

#ifndef KERNEL
void EsDrawBitmapScaled(EsPainter *painter, EsRectangle destinationRegion, EsRectangle sourceRegion, const uint32_t *sourceBits, uintptr_t sourceStride, uint16_t alpha) {
	EsRectangle bounds = EsRectangleIntersection(painter->clip, destinationRegion);
	uint32_t *destinationBits = (uint32_t *) painter->target->bits;
	uintptr_t destinationStride = painter->target->stride;
	bool fullAlpha = painter->target->fullAlpha;

	for (int32_t y = bounds.t; y < bounds.b; y++) {
		int32_t sy = LinearMap(destinationRegion.t, destinationRegion.b, sourceRegion.t, sourceRegion.b, y);
		float sxDelta = (float) (sourceRegion.l - sourceRegion.r) / (destinationRegion.l - destinationRegion.r);
		float sxFloat = LinearMap(destinationRegion.l, destinationRegion.r, sourceRegion.l, sourceRegion.r, bounds.l);

		for (int32_t x = bounds.l; x < bounds.r; x++) {
			int32_t sx = sxFloat;
			sxFloat += sxDelta;

			uint32_t *destinationPixel = destinationBits + x + y * destinationStride / 4;
			const uint32_t *sourcePixel = sourceBits + sx + sy * sourceStride / 4;
			uint32_t modified = *sourcePixel;

			if (alpha == ES_DRAW_BITMAP_OPAQUE) {
				*destinationPixel = modified;
			} else {
				if (alpha != 0xFF) {
					modified = (modified & 0xFFFFFF) | (((((modified & 0xFF000000) >> 24) * alpha) << 16) & 0xFF000000);
				}

				BlendPixel(destinationPixel, modified, fullAlpha);
			}
		}
	}
}

void EsDrawPaintTarget(EsPainter *painter, EsPaintTarget *source, EsRectangle destinationRegion, EsRectangle sourceRegion, uint8_t alpha) {
	bool scale = !(Width(destinationRegion) == Width(sourceRegion) && Height(destinationRegion) == Height(sourceRegion));

	if (scale) {
		EsDrawBitmapScaled(painter, destinationRegion, 
				sourceRegion, (uint32_t *) source->bits, 
				source->stride, source->fullAlpha ? alpha : 0xFFFF);
	} else {
		EsDrawBitmap(painter, destinationRegion, 
				(uint32_t *) source->bits + sourceRegion.l + sourceRegion.t * source->stride / 4, 
				source->stride, source->fullAlpha ? alpha : 0xFFFF);
	}
}
#endif

#endif

/////////////////////////////////
// String utility functions.
/////////////////////////////////

#ifdef SHARED_COMMON_WANT_ALL

size_t EsCStringLength(const char *string) {
	if (!string) {
		return 0;
	}

	size_t size = 0;

	while (true) {
		if (*string) {
			size++;
			string++;
		} else {
			return size;
		}
	}
}

typedef void (*FormatCallback)(int character, void *data); 

void _FormatInteger(FormatCallback callback, void *callbackData, long value, int pad = 0, bool simple = false) {
	char buffer[32];

	if (value < 0) {
		callback('-', callbackData);
	} else if (value == 0) {
		for (int i = 0; i < (pad ?: 1); i++) {
			callback('0', callbackData);
		}

		return;
	}

	int bp = 0;

	while (value) {
		int digit = (value % 10);
		if (digit < 0) digit = -digit;
		buffer[bp++] = '0' + digit;
		value /= 10;
	}

	int cr = bp % 3;

	for (int i = 0; i < pad - bp; i++) {
		callback('0', callbackData);
	}

	for (int i = bp - 1; i >= 0; i--, cr--) {
		if (!cr && !pad) {
			if (i != bp - 1 && !simple) callback(',', callbackData);
			cr = 3;
		}

		callback(buffer[i], callbackData);
	}
}

void WriteCStringToCallback(FormatCallback callback, void *callbackData, const char *cString) {
	while (cString && *cString) {
		callback(utf8_value(cString), callbackData);
		cString = utf8_advance(cString);
	}
}

void _StringFormat(FormatCallback callback, void *callbackData, const char *format, va_list arguments) {
	int c;
	int pad = 0;
	int decimalPlaces = -1;
	uint32_t flags = 0;

	char buffer[32];
	const char *hexChars = "0123456789ABCDEF";

	while ((c = utf8_value((char *) format))) {
		if (c == '%') {
			repeat:;
			format = utf8_advance((char *) format);
			c = utf8_value((char *) format);

			switch (c) {
				case 'd': {
					long value = va_arg(arguments, long);
					_FormatInteger(callback, callbackData, value, pad, flags & ES_STRING_FORMAT_SIMPLE);
				} break;

				case 'i': {
					int value = va_arg(arguments, int);
					_FormatInteger(callback, callbackData, value, pad, flags & ES_STRING_FORMAT_SIMPLE);
				} break;

				case 'D': {
					long value = va_arg(arguments, long);

					if (value == 0) {
						WriteCStringToCallback(callback, callbackData, interfaceString_CommonEmpty);
					} else if (value < 1000) {
						_FormatInteger(callback, callbackData, value, pad);
						WriteCStringToCallback(callback, callbackData, interfaceString_CommonUnitBytes);
					} else if (value < 1000000) {
						_FormatInteger(callback, callbackData, value / 1000, pad);
						callback('.', callbackData);
						_FormatInteger(callback, callbackData, (value / 100) % 10, pad);
						WriteCStringToCallback(callback, callbackData, interfaceString_CommonUnitKilobytes);
					} else if (value < 1000000000) {
						_FormatInteger(callback, callbackData, value / 1000000, pad);
						callback('.', callbackData);
						_FormatInteger(callback, callbackData, (value / 100000) % 10, pad);
						WriteCStringToCallback(callback, callbackData, interfaceString_CommonUnitMegabytes);
					} else {
						_FormatInteger(callback, callbackData, value / 1000000000, pad);
						callback('.', callbackData);
						_FormatInteger(callback, callbackData, (value / 100000000) % 10, pad);
						WriteCStringToCallback(callback, callbackData, interfaceString_CommonUnitGigabytes);
					}
				} break;

				case 'R': {
					EsRectangle value = va_arg(arguments, EsRectangle);
					callback('{', callbackData);
					_FormatInteger(callback, callbackData, value.l);
					callback('-', callbackData);
					callback('>', callbackData);
					_FormatInteger(callback, callbackData, value.r);
					callback(';', callbackData);
					_FormatInteger(callback, callbackData, value.t);
					callback('-', callbackData);
					callback('>', callbackData);
					_FormatInteger(callback, callbackData, value.b);
					callback('}', callbackData);
				} break;

				case 'X': {
					uintptr_t value = va_arg(arguments, uintptr_t);
					callback(hexChars[(value & 0xF0) >> 4], callbackData);
					callback(hexChars[(value & 0xF)], callbackData);
				} break;

				case 'W': {
					uintptr_t value = va_arg(arguments, uintptr_t);
					callback(hexChars[(value & 0xF000) >> 12], callbackData);
					callback(hexChars[(value & 0xF00) >> 8], callbackData);
					callback(hexChars[(value & 0xF0) >> 4], callbackData);
					callback(hexChars[(value & 0xF)], callbackData);
				} break;

				case 'x': {
					uintptr_t value = va_arg(arguments, uintptr_t);
					bool simple = flags & ES_STRING_FORMAT_SIMPLE;
					if (!simple) callback('0', callbackData);
					if (!simple) callback('x', callbackData);
					int bp = 0;
					while (value) {
						buffer[bp++] = hexChars[value % 16];
						value /= 16;
					}
					int j = 0, k = 0;
					for (int i = 0; i < 16 - bp; i++) {
						callback('0', callbackData);
						j++;k++;if (k != 16 && j == 4 && !simple) { callback('_',callbackData); } j&=3;
					}
					for (int i = bp - 1; i >= 0; i--) {
						callback(buffer[i], callbackData);
						j++;k++;if (k != 16 && j == 4 && !simple) { callback('_',callbackData); } j&=3;
					}
				} break;

				case 'c': {
					callback(va_arg(arguments, int), callbackData);
				} break;

				case '%': {
					callback('%', callbackData);
				} break;

				case 's': {
					ptrdiff_t length = va_arg(arguments, ptrdiff_t);
					char *string = va_arg(arguments, char *);

					if (length == -1) {
						WriteCStringToCallback(callback, callbackData, string ?: "[null]");
					} else {
						char *position = string;

						while (position < string + length) {
							callback(utf8_value(position), callbackData);
							position = utf8_advance(position);
						}
					}
				} break;

				case 'z': {
					const char *string = va_arg(arguments, const char *);
					if (!string) string = "[null]";
					WriteCStringToCallback(callback, callbackData, string);
				} break;

				case 'F': {
					double number = va_arg(arguments, double);

					if (__builtin_isnan(number)) {
						WriteCStringToCallback(callback, callbackData, "NaN");
						break;
					} else if (__builtin_isinf(number)) {
						if (number < 0) callback('-', callbackData);
						WriteCStringToCallback(callback, callbackData, "inf");
						break;
					}

					if (number < 0) {
						callback('-', callbackData);
						number = -number;
					}

					int digits[32];
					size_t digitCount = 0;
					const size_t maximumDigits = 12;

					int64_t integer = number;
					number -= integer;
					// number is now in the range [0,1).

					while (number && digitCount <= maximumDigits) {
						// Extract the fractional digits.
						number *= 10;
						int digit = number;
						number -= digit;
						digits[digitCount++] = digit;
					}

					if (digitCount > maximumDigits) {
						if (digits[maximumDigits] >= 5) {
							// Round up.
							for (intptr_t i = digitCount - 2; i >= -1; i--) {
								if (i == -1) { 
									integer++;
								} else {
									digits[i]++;

									if (digits[i] == 10) {
										digits[i] = 0;
									} else {
										break;
									}
								}
							}
						}

						// Hide the last digit.
						digitCount = maximumDigits;
					}

					// Trim trailing zeroes.
					while (digitCount) {
						if (!digits[digitCount - 1]) {
							digitCount--;
						} else {
							break;
						}
					}

					// Integer digits.
					_FormatInteger(callback, callbackData, integer, pad, flags & ES_STRING_FORMAT_SIMPLE);

					// Decimal separator.
					if (digitCount && decimalPlaces) {
						callback('.', callbackData);
					}

					// Fractional digits.
					for (uintptr_t i = 0; i < digitCount; i++) {
						if ((int) i == decimalPlaces) break;
						callback('0' + digits[i], callbackData);
					}

					decimalPlaces = -1;
				} break;

				case '*': {
					pad = va_arg(arguments, int);
					goto repeat;
				} break;

				case '.': {
					decimalPlaces = va_arg(arguments, int);
					goto repeat;
				} break;

				case 'f': {
					flags = va_arg(arguments, uint32_t);
					goto repeat;
				} break;
			}

			pad = 0;
			flags = 0;
		} else {
			callback(c, callbackData);
		}

		format = utf8_advance((char *) format);
	}
}

typedef struct {
	char *buffer;
	size_t bytesRemaining, bytesWritten;
	bool full;
} EsStringFormatInformation;

void StringFormatCallback(int character, void *_fsi) {
	EsStringFormatInformation *fsi = (EsStringFormatInformation *) _fsi;

	if (fsi->full) {
		return;
	}

	char data[4];
	size_t bytes = utf8_encode(character, data);

	if (fsi->buffer) {
		if (fsi->bytesRemaining < bytes && fsi->bytesRemaining != (size_t) ES_STRING_FORMAT_ENOUGH_SPACE) {
			fsi->full = true;
			return;
		} else {
			utf8_encode(character, fsi->buffer);
			fsi->buffer += bytes;
			fsi->bytesWritten += bytes;
			if (fsi->bytesRemaining != (size_t) ES_STRING_FORMAT_ENOUGH_SPACE) fsi->bytesRemaining -= bytes;
		}
	}
}

ptrdiff_t EsStringFormat(char *buffer, size_t bufferLength, const char *format, ...) {
	EsStringFormatInformation fsi = {buffer, bufferLength, 0};
	va_list arguments;
	va_start(arguments, format);
	_StringFormat(StringFormatCallback, &fsi, format, arguments);
	va_end(arguments);
	return fsi.bytesWritten;
}

ptrdiff_t EsStringFormatV(char *buffer, size_t bufferLength, const char *format, va_list arguments) {
	EsStringFormatInformation fsi = {buffer, bufferLength, 0};
	_StringFormat(StringFormatCallback, &fsi, format, arguments);
	return fsi.bytesWritten;
}

void StringFormatCallback2(int character, void *_buffer) {
	EsBuffer *buffer = (EsBuffer *) _buffer;
	char data[4];
	size_t bytes = utf8_encode(character, data);
	EsBufferWrite(buffer, data, bytes);
}

void EsBufferFormat(EsBuffer *buffer, EsCString format, ...) {
	va_list arguments;
	va_start(arguments, format);
	if (!buffer->error) _StringFormat(StringFormatCallback2, buffer, format, arguments);
	va_end(arguments);
}

void EsBufferFormatV(EsBuffer *buffer, EsCString format, va_list arguments) {
	if (!buffer->error) _StringFormat(StringFormatCallback2, buffer, format, arguments);
}

#ifndef KERNEL
char *EsStringAllocateAndFormatV(size_t *bytes, const char *format, va_list arguments1) {
	size_t needed = 0;

	va_list arguments2;
	va_copy(arguments2, arguments1);

	_StringFormat([] (int character, void *data) { 
		size_t *needed = (size_t *) data; 
		*needed = *needed + utf8_encode(character, nullptr); 
	}, &needed, format, arguments1);

	if (bytes) *bytes = needed;
	char *buffer = (char *) EsHeapAllocate(needed + 1, false);

	if (!buffer) {
		if (bytes) *bytes = 0;
		return nullptr;
	}

	char *position = buffer;
	buffer[needed] = 0;

	_StringFormat([] (int character, void *data) { 
		char **position = (char **) data; 
		*position = *position + utf8_encode(character, *position); 
	}, &position, format, arguments2);

	va_end(arguments2);

	return buffer;
}

char *EsStringAllocateAndFormat(size_t *bytes, const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	char *buffer = EsStringAllocateAndFormatV(bytes, format, arguments);
	va_end(arguments);
	return buffer;
}

const char *EsStringFormatTemporary(const char *format, ...) {
	EsMessageMutexCheck();
	static char *buffer = nullptr;
	EsHeapFree(buffer);
	va_list arguments;
	va_start(arguments, format);
	size_t bytes = 0;
	buffer = EsStringAllocateAndFormatV(&bytes, format, arguments);
	va_end(arguments);
	return buffer;
}
#endif

int64_t EsStringParseInteger(const char **string, size_t *length, int base) {
	int64_t value = 0;
	bool overflow = false;

	while (*length) {
		char c = (*string)[0];

		int64_t digit = 0;

		if (c >= 'a' && c <= 'z') {
			digit = c - 'a' + 10;
		} else if (c >= 'A' && c <= 'Z') {
			digit = c - 'A' + 10;
		} else if (c >= '0' && c <= '9') {
			digit = c - '0';
		} else {
			break;
		}

		if (digit >= base) {
			break;
		}

		int64_t oldValue = value;

		value *= base;
		value += digit;

		if (value / base != oldValue) {
			overflow = true;
		}

		(*string)++;
		(*length)--;
	}

	if (overflow) value = LONG_MAX;
	return value;
}

int EsStringCompareRaw(const char *s1, ptrdiff_t length1, const char *s2, ptrdiff_t length2) {
	if (length1 == -1) length1 = EsCStringLength(s1);
	if (length2 == -1) length2 = EsCStringLength(s2);

	while (length1 || length2) {
		if (!length1) return -1;
		if (!length2) return 1;

		char c1 = *s1;
		char c2 = *s2;

		if (c1 != c2) {
			return c1 - c2;
		}

		length1--;
		length2--;
		s1++;
		s2++;
	}

	return 0;
}

int EsStringCompare(const char *s1, ptrdiff_t _length1, const char *s2, ptrdiff_t _length2) {
	if (_length1 == -1) _length1 = EsCStringLength(s1);
	if (_length2 == -1) _length2 = EsCStringLength(s2);
	size_t length1 = _length1, length2 = _length2;

	while (length1 || length2) {
		if (!length1) return -1;
		if (!length2) return 1;

		// Skip over rich text markup.
		if (*s1 == '\a') while (length1 && *s1 != ']') s1++, length1--;
		if (*s2 == '\a') while (length2 && *s2 != ']') s2++, length2--;

		char c1 = *s1;
		char c2 = *s2;

		if (c1 >= '0' && c1 <= '9' && c2 >= '0' && c2 <= '9') {
			int64_t n1 = EsStringParseInteger(&s1, &length1, 10);
			int64_t n2 = EsStringParseInteger(&s2, &length2, 10);

			if (n1 != n2) {
				if (n1 > n2) return  1;
				if (n1 < n2) return -1;
			}
		} else {
			if (c1 >= 'a' && c1 <= 'z') c1 = c1 - 'a' + 'A';
			if (c2 >= 'a' && c2 <= 'z') c2 = c2 - 'a' + 'A';
			if (c1 == '.') c1 = ' '; else if (c1 == ' ') c1 = '.';
			if (c2 == '.') c2 = ' '; else if (c2 == ' ') c2 = '.';

			if (c1 != c2) {
				if (c1 > c2) return  1;
				if (c1 < c2) return -1;
			}

			length1--;
			length2--;
			s1++;
			s2++;
		}
	}

	return 0;
}

bool StringStartsWith(const char *string, intptr_t _stringBytes, const char *prefix, intptr_t _prefixBytes, bool caseInsensitive) {
	if (_stringBytes == -1) _stringBytes = EsCStringLength(string);
	if (_prefixBytes == -1) _prefixBytes = EsCStringLength(prefix);
	size_t stringBytes = _stringBytes, prefixBytes = _prefixBytes;

	while (true) {
		if (!prefixBytes) return true;
		if (!stringBytes) return false;

		char c1 = *string;
		char c2 = *prefix;

		if (caseInsensitive) {
			if (c1 >= 'a' && c1 <= 'z') c1 = c1 - 'a' + 'A';
			if (c2 >= 'a' && c2 <= 'z') c2 = c2 - 'a' + 'A';
		}

		if (c1 != c2) return false;

		stringBytes--;
		prefixBytes--;
		string++;
		prefix++;
	}
}

uint32_t EsColorParse(const char *string, ptrdiff_t bytes) {
	if (bytes == -1) {
		bytes = EsCStringLength(string);
	}

	int digits[8], digitCount = 0;
	ptrdiff_t position = 0;

	while (position != bytes && !EsCRTisxdigit(string[position])) {
		position++;
	}

	for (int i = 0; i < 8 && position != bytes; i++) {
		char c = string[position++];

		if (EsCRTisxdigit(c)) {
			digits[digitCount++] = EsCRTisdigit(c) ? (c - '0') : EsCRTisupper(c) ? (c - 'A' + 10) : (c - 'a' + 10);
		} else {
			break;
		}
	}

	uint32_t color = 0;

	if (digitCount == 3) {
		color = 0xFF000000 | (digits[0] << 20) | (digits[0] << 16) | (digits[1] << 12) | (digits[1] << 8) | (digits[2] << 4) | (digits[2] << 0);
	} else if (digitCount == 4) {
		color = (digits[0] << 28 | digits[0] << 24) | (digits[1] << 20) | (digits[1] << 16) 
			| (digits[2] << 12) | (digits[2] << 8) | (digits[3] << 4) | (digits[3] << 0);
	} else if (digitCount == 5) {
		color = (digits[0] << 28 | digits[1] << 24) | (digits[2] << 20) | (digits[2] << 16) 
			| (digits[3] << 12) | (digits[3] << 8) | (digits[4] << 4) | (digits[4] << 0);
	} else if (digitCount == 6) {
		color = 0xFF000000 | (digits[0] << 20) | (digits[1] << 16) | (digits[2] << 12) | (digits[3] << 8) | (digits[4] << 4) | (digits[5] << 0);
	} else if (digitCount == 8) {
		color = (digits[0] << 28) | (digits[1] << 24) | (digits[2] << 20) | (digits[3] << 16) 
			| (digits[4] << 12) | (digits[5] << 8) | (digits[6] << 4) | (digits[7] << 0);
	}

	return color;
}

static int64_t ConvertCharacterToDigit(int character, int base) {
	int64_t result = -1;

	if (character >= '0' && character <= '9') {
		result = character - '0';
	} else if (character >= 'A' && character <= 'Z') {
		result = character - 'A' + 10;
	} else if (character >= 'a' && character <= 'z') {
		result = character - 'a' + 10;
	}

	if (result >= base) {
		result = -1;
	}

	return result;
}

int64_t EsIntegerParse(const char *text, ptrdiff_t bytes) {
	if (bytes == -1) bytes = EsCStringLength(text);

	int base = 10;
	bool seenFirstRecognisedCharacter = false;

	if (bytes > 2 && text[0] == '0' && text[1] == 'x') {
		text += 2, bytes -= 2;
		base = 16;
		seenFirstRecognisedCharacter = true;
	}

	const char *end = text + bytes;

	bool negative = false;
	int64_t result = 0;

	while (text < end) {
		char c = *text;

		if (c == '-' && !seenFirstRecognisedCharacter) {
			negative = true;
			seenFirstRecognisedCharacter = true;
		} else if (c >= '0' && c <= '9') {
			result *= base;
			result += c - '0';
			seenFirstRecognisedCharacter = true;
		} else if (c >= 'A' && c <= 'F' && base == 16) {
			result *= base;
			result += c - 'A' + 10;
			seenFirstRecognisedCharacter = true;
		} else if (c >= 'a' && c <= 'f' && base == 16) {
			result *= base;
			result += c - 'a' + 10;
			seenFirstRecognisedCharacter = true;
		}

		text++;
	}

	return negative ? -result : result;
}

bool EsStringFormatAppendV(char *buffer, size_t bufferLength, size_t *bufferPosition, const char *format, va_list arguments) {
	buffer += *bufferPosition;
	bufferLength -= *bufferPosition;
	EsStringFormatInformation fsi = {buffer, bufferLength, 0};
	_StringFormat(StringFormatCallback, &fsi, format, arguments);
	*bufferPosition += fsi.bytesWritten;
	return !fsi.full;
}

bool EsStringFormatAppend(char *buffer, size_t bufferLength, size_t *bufferPosition, const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	bool result = EsStringFormatAppendV(buffer, bufferLength, bufferPosition, format, arguments);
	va_end(arguments);
	return result;
}

double EsDoubleParse(const char *nptr, ptrdiff_t maxBytes, char **endptr) {
	if (maxBytes == -1) maxBytes = EsCStringLength(nptr);
	const char *end = nptr + maxBytes;
	if (nptr == end) return 0;

	while (nptr != end && EsCRTisspace(*nptr)) {
		nptr++;
	}

	if (nptr == end) return 0;

	bool positive = true;

	if (*nptr == '+') {
		positive = true;
		nptr++;
	} else if (*nptr == '-') {
		positive = false;
		nptr++;
	}

	if (nptr == end) return 0;

	double value = 0, scale = 0.1;
	bool seenDecimalPoint = false;

	while (nptr != end) {
		char c = *nptr;

		if (c == '.' && !seenDecimalPoint) {
			seenDecimalPoint = true;
		} else if (c >= '0' && c <= '9') {
			if (seenDecimalPoint) {
				value += scale * (c - '0');
				scale *= 0.1;
			} else {
				value = value * 10;
				value += c - '0';
			}
		} else if (c == ',') {
		} else {
			break;
		}

		nptr++;
	}

	if (!positive) {
		value = -value;
	}

	if (endptr) *endptr = (char *) nptr;
	return value;
}

void PathGetName(const char *path, ptrdiff_t pathBytes, const char **name, ptrdiff_t *nameBytes) {
	if (pathBytes == -1) {
		pathBytes = EsCStringLength(path);
	}

	*name = path;
	*nameBytes = pathBytes;

	for (uintptr_t i = pathBytes; i > 0; i--) {
		if (path[i - 1] == '/') {
			*name = path + i;
			*nameBytes = pathBytes - i;
			break;
		}
	}
}

#endif

/////////////////////////////////
// Memory utility functions.
/////////////////////////////////

#ifdef SHARED_COMMON_WANT_ALL

__attribute__((no_instrument_function))
void EsMemoryCopy(void *_destination, const void *_source, size_t bytes) {
	// TODO Prevent this from being optimised out in the kernel.

	if (!bytes) {
		return;
	}

	uint8_t *destination = (uint8_t *) _destination;
	uint8_t *source = (uint8_t *) _source;

#ifdef ES_ARCH_X86_64
	while (bytes >= 16) {
		_mm_storeu_si128((__m128i *) destination, 
				_mm_loadu_si128((__m128i *) source));

		source += 16;
		destination += 16;
		bytes -= 16;
	}
#endif

	while (bytes >= 1) {
		((uint8_t *) destination)[0] = ((uint8_t *) source)[0];

		source += 1;
		destination += 1;
		bytes -= 1;
	}
}

__attribute__((no_instrument_function))
void EsMemoryCopyReverse(void *_destination, const void *_source, size_t bytes) {
	// TODO Prevent this from being optimised out in the kernel.

	if (!bytes) {
		return;
	}

	uint8_t *destination = (uint8_t *) _destination;
	uint8_t *source = (uint8_t *) _source;

	destination += bytes - 1;
	source += bytes - 1;

	while (bytes >= 1) {
		((uint8_t *) destination)[0] = ((uint8_t *) source)[0];

		source -= 1;
		destination -= 1;
		bytes -= 1;
	}
}

__attribute__((no_instrument_function))
void EsMemoryZero(void *destination, size_t bytes) {
	// TODO Prevent this from being optimised out in the kernel.

	if (!bytes) {
		return;
	}

	for (uintptr_t i = 0; i < bytes; i++) {
		((uint8_t *) destination)[i] = 0;
	}
}

__attribute__((no_instrument_function))
void EsMemoryMove(void *_start, void *_end, intptr_t amount, bool zeroEmptySpace) {
	// TODO Prevent this from being optimised out in the kernel.

	uint8_t *start = (uint8_t *) _start;
	uint8_t *end = (uint8_t *) _end;

	if (end < start) {
		EsPanic("MemoryMove end < start: %x %x %x %d\n", start, end, amount, zeroEmptySpace);
		return;
	}

	if (amount > 0) {
		EsMemoryCopyReverse(start + amount, start, end - start);

		if (zeroEmptySpace) {
			EsMemoryZero(start, amount);
		}
	} else if (amount < 0) {
		EsMemoryCopy(start + amount, start, end - start);

		if (zeroEmptySpace) {
			EsMemoryZero(end + amount, -amount);
		}
	}
}

__attribute__((no_instrument_function))
int EsMemoryCompare(const void *a, const void *b, size_t bytes) {
	if (!bytes) {
		return 0;
	}

	const uint8_t *x = (const uint8_t *) a;
	const uint8_t *y = (const uint8_t *) b;

	for (uintptr_t i = 0; i < bytes; i++) {
		if (x[i] < y[i]) {
			return -1;
		} else if (x[i] > y[i]) {
			return 1;
		}
	}

	return 0;
}

__attribute__((no_instrument_function))
uint8_t EsMemorySumBytes(uint8_t *source, size_t bytes) {
	if (!bytes) {
		return 0;
	}

	uint8_t total = 0;

	for (uintptr_t i = 0; i < bytes; i++) {
		total += source[i];
	}

	return total;
}

__attribute__((no_instrument_function))
void EsMemoryFill(void *from, void *to, uint8_t byte) {
	uint8_t *a = (uint8_t *) from;
	uint8_t *b = (uint8_t *) to;
	while (a != b) *a = byte, a++;
}

#endif

/////////////////////////////////
// Printing.
/////////////////////////////////

#ifdef SHARED_COMMON_WANT_ALL

#ifdef USE_STB_SPRINTF
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"
#endif

#ifndef KERNEL

static struct {
	EsMutex mutex;

#define PRINT_BUFFER_SIZE (1024)
	char buffer[PRINT_BUFFER_SIZE];
	uintptr_t bufferPosition;
} printing;

void PrintCallback(int character, void *) {
	if (printing.bufferPosition >= PRINT_BUFFER_SIZE - 16) {
		EsSyscall(ES_SYSCALL_PRINT, (uintptr_t) printing.buffer, printing.bufferPosition, 0, 0);
		printing.bufferPosition = 0;
	}

	printing.bufferPosition += utf8_encode(character, printing.buffer + printing.bufferPosition); 
}

void EsPrint(const char *format, ...) {
	EsMutexAcquire(&printing.mutex);
	va_list arguments;
	va_start(arguments, format);
	_StringFormat(PrintCallback, nullptr, format, arguments);
	va_end(arguments);
	EsSyscall(ES_SYSCALL_PRINT, (uintptr_t) printing.buffer, printing.bufferPosition, 0, 0);
	printing.bufferPosition = 0;
	EsMutexRelease(&printing.mutex);
}

void EsPrintDirect(const char *string, ptrdiff_t stringLength) {
	if (stringLength == -1) stringLength = EsCStringLength(string);
	EsSyscall(ES_SYSCALL_PRINT, (uintptr_t) string, stringLength, 0, 0);
}

void EsPrintHelloWorld() {
	EsPrint("Hello, world.\n");
}

#endif

#endif

/////////////////////////////////
// Random number generator.
/////////////////////////////////

#ifdef SHARED_COMMON_WANT_ALL

struct RNGState {
	uint64_t s[4];
	EsSpinlock lock;
};

RNGState rngState;

void EsRandomAddEntropy(uint64_t x) {
	EsSpinlockAcquire(&rngState.lock);

	for (uintptr_t i = 0; i < 4; i++) {
		x += 0x9E3779B97F4A7C15;

		uint64_t result = x;
		result = (result ^ (result >> 30)) * 0xBF58476D1CE4E5B9;
		result = (result ^ (result >> 27)) * 0x94D049BB133111EB;
		rngState.s[i] ^= result ^ (result >> 31);
	}

	EsSpinlockRelease(&rngState.lock);
}

void EsRandomSeed(uint64_t x) {
	EsSpinlockAcquire(&rngState.lock);

	rngState.s[0] = rngState.s[1] = rngState.s[2] = rngState.s[3] = 0;

	for (uintptr_t i = 0; i < 4; i++) {
		x += 0x9E3779B97F4A7C15;

		uint64_t result = x;
		result = (result ^ (result >> 30)) * 0xBF58476D1CE4E5B9;
		result = (result ^ (result >> 27)) * 0x94D049BB133111EB;
		rngState.s[i] = result ^ (result >> 31);
	}

	EsSpinlockRelease(&rngState.lock);
}

uint64_t EsRandomU64() {
	EsSpinlockAcquire(&rngState.lock);

	uint64_t result = rngState.s[1] * 5;
	result = ((result << 7) | (result >> 57)) * 9;

	uint64_t t = rngState.s[1] << 17;
	rngState.s[2] ^= rngState.s[0];
	rngState.s[3] ^= rngState.s[1];
	rngState.s[1] ^= rngState.s[2];
	rngState.s[0] ^= rngState.s[3];
	rngState.s[2] ^= t;
	rngState.s[3] = (rngState.s[3] << 45) | (rngState.s[3] >> 19);

	EsSpinlockRelease(&rngState.lock);

	return result;
}

uint8_t EsRandomU8() {
	return (uint8_t) EsRandomU64();
}

#endif

/////////////////////////////////
// Standard algorithms.
/////////////////////////////////

#ifdef SHARED_COMMON_WANT_ALL

void EsSortWithSwapCallback(void *_base, size_t nmemb, size_t size, int (*compar)(const void *, const void *, EsGeneric), 
		EsGeneric argument, void (*swap)(const void *, const void *, EsGeneric)) {
	if (nmemb <= 1) return;
	uint8_t *base = (uint8_t *) _base;
	intptr_t i = -1, j = nmemb;

	while (true) {
		while (compar(base + ++i * size, base, argument) < 0);
		while (compar(base + --j * size, base, argument) > 0);
		if (i >= j) break;
		swap(base + i * size, base + j * size, argument);
	}

	EsSortWithSwapCallback(base, ++j, size, compar, argument, swap);
	EsSortWithSwapCallback(base + j * size, nmemb - j, size, compar, argument, swap);
}

void EsSort(void *_base, size_t nmemb, size_t size, int (*compar)(const void *, const void *, EsGeneric), EsGeneric argument) {
	if (nmemb <= 1) return;

	uint8_t *base = (uint8_t *) _base;
	uint8_t *swap = (uint8_t *) __builtin_alloca(size);

	intptr_t i = -1, j = nmemb;

	while (true) {
		while (compar(base + ++i * size, base, argument) < 0);
		while (compar(base + --j * size, base, argument) > 0);

		if (i >= j) break;

		EsMemoryCopy(swap, base + i * size, size);
		EsMemoryCopy(base + i * size, base + j * size, size);
		EsMemoryCopy(base + j * size, swap, size);
	}

	EsSort(base, ++j, size, compar, argument);
	EsSort(base + j * size, nmemb - j, size, compar, argument);
}

#endif

/////////////////////////////////
// Miscellaneous.
/////////////////////////////////

#ifdef SHARED_COMMON_WANT_ALL

void EnterDebugger() {
	// Do nothing.
}

#ifndef KERNEL
size_t EsPathFindUniqueName(char *buffer, size_t originalBytes, size_t bufferBytes) {
	if (originalBytes && buffer[originalBytes - 1] == '/') {
		originalBytes--;
	}

	size_t extensionPoint = originalBytes;

	for (uintptr_t i = 0; i < originalBytes; i++) {
		if (buffer[i] == '.') {
			extensionPoint = i;
		} else if (buffer[i] == '/') {
			extensionPoint = originalBytes;
		}
	}

	if (!EsPathExists(buffer, originalBytes)) {
		return originalBytes;
	}

	char *buffer2 = (char *) EsHeapAllocate(bufferBytes, false);

	if (!buffer2) {
		return 0;
	}

	EsDefer(EsHeapFree(buffer2));

	uintptr_t attempt = 2;

	// TODO Check that this runs in a reasonable amount of time when all files are already present.

	while (attempt < 1000) {
		size_t length = EsStringFormat(buffer2, bufferBytes, "%s %d%s", extensionPoint, buffer, 
				attempt, originalBytes - extensionPoint, buffer + extensionPoint);

		if (!EsPathExists(buffer2, length)) {
			EsMemoryCopy(buffer, buffer2, length);
			return length;
		} else {
			attempt++;
		}
	}

	return 0;
}

uint8_t *EsImageLoad(const void *file, size_t fileSize, uint32_t *imageX, uint32_t *imageY, int imageChannels) {
#ifdef USE_STB_IMAGE
	int unused;
	uint32_t *image = (uint32_t *) stbi_load_from_memory((uint8_t *) file, fileSize, (int *) imageX, (int *) imageY, &unused, imageChannels);

	if (!image) {
		return nullptr;
	}

	for (uintptr_t j = 0; j < *imageY; j++) {
		for (uintptr_t i = 0; i < *imageX; i++) {
			uint32_t in = image[i + j * *imageX];
			uint32_t red = ((in & 0xFF) << 16), green = (in & 0xFF00FF00), blue = ((in & 0xFF0000) >> 16);
			image[i + j * *imageX] = red | green | blue;
		}
	}

	return (uint8_t *) image;
#else
	(void) imageChannels;
	PNGReader reader = {};
	reader.buffer = file;
	reader.bytes = fileSize;
	uint32_t *bits;
	bool success = PNGParse(&reader, &bits, imageX, imageY, [] (size_t s) { return EsHeapAllocate(s, false); }, [] (void *p) { EsHeapFree(p); });
	return success ? (uint8_t *) bits : nullptr;
#endif
}

void LoadImage(const void *path, ptrdiff_t pathBytes, void *destination, int destinationWidth, int destinationHeight, 
		bool fromMemory, bool *_coversDestination = nullptr) {
	int width = 0, height = 0;
	uint32_t *image = nullptr;

	if (!fromMemory) {
		size_t fileSize;
		void *file = EsFileReadAll((const char *) path, pathBytes, &fileSize);

		if (file) {
			image = (uint32_t *) EsImageLoad((uint8_t *) file, fileSize, (uint32_t *) &width, (uint32_t *) &height, 4);
			EsHeapFree(file);
		}
	} else {
		image = (uint32_t *) EsImageLoad((uint8_t *) path, pathBytes, (uint32_t *) &width, (uint32_t *) &height, 4);
	}

	int cx = destinationWidth / 2 - width / 2, cy = destinationHeight / 2 - height / 2;
	bool coversDestination = true;

	for (int j = 0; j < destinationHeight; j++) {
		uint32_t *pixel = (uint32_t *) ((uint8_t *) destination + j * destinationWidth * 4);

		for (int i = 0; i < destinationWidth; i++, pixel++) {
			if (i - cx >= 0 && i - cx < width && j - cy >= 0 && j - cy < height) {
				*pixel = image[i - cx + (j - cy) * width];
			} else {
				coversDestination = false;
			}
		}
	}

	if (_coversDestination) *_coversDestination = coversDestination;
	EsHeapFree(image);
}
#endif

#endif

/////////////////////////////////
// Synchronisation.
/////////////////////////////////

#ifdef SHARED_COMMON_WANT_ALL

__attribute__((no_instrument_function))
void EsSpinlockAcquire(EsSpinlock *spinlock) {
	__sync_synchronize();
	while (__sync_val_compare_and_swap(&spinlock->state, 0, 1));
	__sync_synchronize();
}

__attribute__((no_instrument_function))
void EsSpinlockRelease(EsSpinlock *spinlock) {
	__sync_synchronize();

	if (!spinlock->state) {
		EsPanic("EsSpinlockRelease - Spinlock %x not acquired.\n", spinlock);
	}

	spinlock->state = 0;
	__sync_synchronize();
}

#ifndef KERNEL

void EsMutexAcquire(EsMutex *mutex) {
	bool acquired = false;

	while (true) {
		EsSpinlockAcquire(&mutex->spinlock);

		if (mutex->event == ES_INVALID_HANDLE) {
			mutex->event = EsEventCreate(false);
		}

		if (mutex->state == 0) {
			acquired = true;
			mutex->state = 1;
		} 

		if (acquired) {
			// TODO Test this.
			EsSpinlockRelease(&mutex->spinlock);
			return;
		}

		__sync_fetch_and_add(&mutex->queued, 1); 
		EsEventReset(mutex->event);
		EsSpinlockRelease(&mutex->spinlock);
		EsWaitSingle(mutex->event);
		__sync_fetch_and_sub(&mutex->queued, 1);
	} 
}

void EsMutexRelease(EsMutex *mutex) {
	volatile bool queued = false;

	EsSpinlockAcquire(&mutex->spinlock);

	if (!mutex->state) {
		EsPanic("EsMutexRelease - Mutex not acquired.");
	}

	mutex->state = 0;

	if (mutex->queued) {
		queued = true;
		EsEventSet(mutex->event);
	}

	EsSpinlockRelease(&mutex->spinlock);

	if (queued) {
		EsSchedulerYield();
	}
}

void EsMutexDestroy(EsMutex *mutex) {
	EsAssert(!mutex->state && !mutex->queued && !mutex->spinlock.state);

	if (mutex->event != ES_INVALID_HANDLE) {
		EsHandleClose(mutex->event);
		mutex->event = ES_INVALID_HANDLE;
	}
}

#endif

#endif

/////////////////////////////////
// Byte swapping.
/////////////////////////////////

#ifdef SHARED_COMMON_WANT_ALL

uint16_t ByteSwap16(uint16_t x) {
	return (x << 8) | (x >> 8);
}

uint32_t ByteSwap32(uint32_t x) {
	return    ((x & 0xFF000000) >> 24) 
		| ((x & 0x000000FF) << 24) 
		| ((x & 0x00FF0000) >> 8) 
		| ((x & 0x0000FF00) << 8);
}

uint16_t SwapBigEndian16(uint16_t x) {
	return ByteSwap16(x);
}

uint32_t SwapBigEndian32(uint32_t x) {
	return ByteSwap32(x);
}

#endif

/////////////////////////////////
// C standard library.
/////////////////////////////////

#ifdef SHARED_COMMON_WANT_ALL

void *EsCRTmemset(void *s, int c, size_t n) {
	uint8_t *s8 = (uint8_t *) s;
	for (uintptr_t i = 0; i < n; i++) {
		s8[i] = (uint8_t) c;
	}
	return s;
}

void *EsCRTmemcpy(void *dest, const void *src, size_t n) {
	uint8_t *dest8 = (uint8_t *) dest;
	const uint8_t *src8 = (const uint8_t *) src;
	for (uintptr_t i = 0; i < n; i++) {
		dest8[i] = src8[i];
	}
	return dest;
}

void *EsCRTmemmove(void *dest, const void *src, size_t n) {
	if ((uintptr_t) dest < (uintptr_t) src) {
		return EsCRTmemcpy(dest, src, n);
	} else {
		uint8_t *dest8 = (uint8_t *) dest;
		const uint8_t *src8 = (const uint8_t *) src;
		for (uintptr_t i = n; i; i--) {
			dest8[i - 1] = src8[i - 1];
		}
		return dest;
	}
}

#ifndef KERNEL
char *EsCRTstrdup(const char *string) {
	if (!string) return nullptr;
	size_t length = EsCRTstrlen(string) + 1;
	char *memory = (char *) EsCRTmalloc(length);
	if (!memory) return nullptr;
	EsCRTmemcpy(memory, string, length);
	return memory;
}
#endif

size_t EsCRTstrlen(const char *s) {
	size_t n = 0;
	while (s[n]) n++;
	return n;
}

size_t EsCRTstrnlen(const char *s, size_t maxlen) {
	size_t n = 0;
	while (s[n] && maxlen--) n++;
	return n;
}

int EsCRTabs(int n) {
	if (n < 0)	return 0 - n;
	else		return n;
}

#ifndef KERNEL
volatile static size_t mallocCount;

void *EsCRTmalloc(size_t size) {
	void *x = EsHeapAllocate(size, false);
	if (x) __sync_fetch_and_add(&mallocCount, 1);
	return x;
}

void *EsCRTcalloc(size_t num, size_t size) {
	size_t c;
	if (__builtin_mul_overflow(num, size, &c)) return nullptr;
	void *x = EsHeapAllocate(c, true);
	if (x) __sync_fetch_and_add(&mallocCount, 1);
	return x;
}

void EsCRTfree(void *ptr) {
	if (ptr) __sync_fetch_and_sub(&mallocCount, 1);
	EsHeapFree(ptr);
}

void *EsCRTrealloc(void *ptr, size_t size) {
	// EsHeapReallocate handles this logic, but do it ourselves to keep mallocCount correct.
	if (!ptr) return EsCRTmalloc(size);
	else if (!size) return EsCRTfree(ptr), nullptr;
	else return EsHeapReallocate(ptr, size, false);
}
#endif

char *EsCRTgetenv(const char *name) {
	(void) name;
	return nullptr;
}

int EsCRTtoupper(int c) {
	if (c >= 'a' && c <= 'z') {
		return c - 'a' + 'A';
	} else {
		return c;
	}
}

int EsCRTtolower(int c) {
	if (c >= 'A' && c <= 'Z') {
		return c - 'A' + 'a';
	} else {
		return c;
	}
}

int EsCRTstrcasecmp(const char *s1, const char *s2) {
	while (true) {
		if (*s1 != *s2 && EsCRTtolower(*s1) != EsCRTtolower(*s2)) {
			if (*s1 == 0) return -1;
			else if (*s2 == 0) return 1;
			return *s1 - *s2;
		}

		if (*s1 == 0) {
			return 0;
		}

		s1++;
		s2++;
	}
}

int EsCRTstrncasecmp(const char *s1, const char *s2, size_t n) {
	while (n--) {
		if (*s1 != *s2 && EsCRTtolower(*s1) != EsCRTtolower(*s2)) {
			if (*s1 == 0) return -1;
			else if (*s2 == 0) return 1;
			return *s1 - *s2;
		}

		if (*s1 == 0) {
			return 0;
		}

		s1++;
		s2++;
	}

	return 0;
}

int EsCRTstrcmp(const char *s1, const char *s2) {
	while (true) {
		if (*s1 != *s2) {
			if (*s1 == 0) return -1;
			else if (*s2 == 0) return 1;
			return *s1 - *s2;
		}

		if (*s1 == 0) {
			return 0;
		}

		s1++;
		s2++;
	}
}

int EsCRTstrncmp(const char *s1, const char *s2, size_t n) {
	while (n--) {
		if (*s1 != *s2) {
			if (*s1 == 0) return -1;
			else if (*s2 == 0) return 1;
			return *s1 - *s2;
		}

		if (*s1 == 0) {
			return 0;
		}

		s1++;
		s2++;
	}

	return 0;
}

int EsCRTisspace(int c) {
	if (c == ' ')  return 1;
	if (c == '\f') return 1;
	if (c == '\n') return 1;
	if (c == '\r') return 1;
	if (c == '\t') return 1;
	if (c == '\v') return 1;

	return 0;
}

uint64_t EsCRTstrtoul(const char *nptr, char **endptr, int base) {
	// TODO errno

	if (base > 36) return 0;

	while (EsCRTisspace(*nptr)) {
		nptr++;
	}

	if (*nptr == '+') {
		nptr++;
	} else if (*nptr == '-') {
		nptr++;
	}

	if (base == 0) {
		if (nptr[0] == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
			base = 16;
			nptr += 2;
		} else if (nptr[0] == '0') {
			EsPrint("WARNING: strtoul with base=0, detected octal\n");
			base = 8; // Why?!?
			nptr++;
		} else {
			base = 10;
		}
	}

	uint64_t value = 0;
	bool overflow = false;

	while (true) {
		int64_t digit = ConvertCharacterToDigit(*nptr, base);

		if (digit != -1) {
			nptr++;

			uint64_t x = value;
			value *= base;
			value += (uint64_t) digit;

			if (value / base != x) {
				overflow = true;
			}
		} else {
			break;
		}
	}

	if (overflow) {
		value = ULONG_MAX;
	}

	if (endptr) {
		*endptr = (char *) nptr;
	}

	return value;
}

float EsCRTstrtof(const char *nptr, char **endptr) {
	return EsDoubleParse(nptr, -1, endptr);
}

double EsCRTstrtod(const char *nptr, char **endptr) {
	return EsDoubleParse(nptr, -1, endptr);
}

float EsCRTatof(const char *nptr) {
	return EsDoubleParse(nptr, -1, nullptr);
}

double EsCRTatod(const char *nptr) {
	return EsDoubleParse(nptr, -1, nullptr);
}

size_t EsCRTstrcspn(const char *s, const char *reject) {
	size_t count = 0;

	while (true) {
		char character = *s;
		if (!character) return count;

		const char *search = reject;

		while (true) {
			char c = *search;

			if (!c) {
				goto match;
			} else if (character == c) {
				break;
			}

			search++;
		}

		return count;

		match:;
		count++;
		s++;
	}
}

char *EsCRTstrsep(char **stringp, const char *delim) {
	char *string = *stringp;

	if (!string) {
		return NULL;
	}

	size_t tokenLength = EsCRTstrcspn(string, delim);

	if (string[tokenLength] == 0) {
		*stringp = NULL;
	} else {
		string[tokenLength] = 0;
		*stringp = string + tokenLength + 1;
	}

	return string;
}

char *EsCRTstrcat(char *dest, const char *src) {
	char *o = dest;
	dest += EsCRTstrlen(dest);

	while (*src) {
		*dest = *src;
		src++;
		dest++;
	}

	*dest = 0;

	return o;
}

long int EsCRTstrtol(const char *nptr, char **endptr, int base) {
	// TODO errno

	if (base > 36) return 0;

	while (EsCRTisspace(*nptr)) {
		nptr++;
	}

	bool positive = true;

	if (*nptr == '+') {
		positive = true;
		nptr++;
	} else if (*nptr == '-') {
		positive = false;
		nptr++;
	}

	if (base == 0) {
		if (nptr[0] == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
			base = 16;
			nptr += 2;
		} else if (nptr[0] == '0') {
			EsPrint("WARNING: strtol with base=0, detected octal\n");
			base = 8; // Why?!?
			nptr++;
		} else {
			base = 10;
		}
	}

	int64_t value = 0;
	bool overflow = false;

	while (true) {
		int64_t digit = ConvertCharacterToDigit(*nptr, base);

		if (digit != -1) {
			nptr++;

			int64_t x = value;
			value *= base;
			value += digit;

			if (value / base != x) {
				overflow = true;
			}
		} else {
			break;
		}
	}

	if (!positive) {
		value = -value;
	}

	if (overflow) {
		value = positive ? LONG_MAX : LONG_MIN;
	}

	if (endptr) {
		*endptr = (char *) nptr;
	}

	return value;
}

int EsCRTatoi(const char *nptr) {
	return (int) EsCRTstrtol(nptr, NULL, 10);
}

char *EsCRTstrstr(const char *haystack, const char *needle) {
	size_t haystackLength = EsCRTstrlen(haystack);
	size_t needleLength = EsCRTstrlen(needle);

	if (haystackLength < needleLength) {
		return nullptr;
	}

	for (uintptr_t i = 0; i <= haystackLength - needleLength; i++) {
		for (uintptr_t j = 0; j < needleLength; j++) {
			if (haystack[i + j] != needle[j]) {
				goto tryNext;
			}
		}

		return (char *) haystack + i;

		tryNext:;
	}

	return nullptr;
}

void EsCRTqsort(void *_base, size_t nmemb, size_t size, EsCRTComparisonCallback compar) {
	if (nmemb <= 1) return;

	uint8_t *base = (uint8_t *) _base;
	uint8_t *swap = (uint8_t *) __builtin_alloca(size);

	intptr_t i = -1, j = nmemb;

	while (true) {
		while (compar(base + ++i * size, base) < 0);
		while (compar(base + --j * size, base) > 0);

		if (i >= j) break;

		EsCRTmemcpy(swap, base + i * size, size);
		EsCRTmemcpy(base + i * size, base + j * size, size);
		EsCRTmemcpy(base + j * size, swap, size);
	}

	EsCRTqsort(base, ++j, size, compar);
	EsCRTqsort(base + j * size, nmemb - j, size, compar);
}

void *EsCRTbsearch(const void *key, const void *base, size_t num, size_t size, EsCRTComparisonCallback compar) {
	if (!num) return nullptr;

	intptr_t low = 0;
	intptr_t high = num - 1;

	while (low <= high) {
		uintptr_t index = ((high - low) >> 1) + low;
		int result = compar(key, (uint8_t *) base + size * index);

		if (result < 0) {
			high = index - 1;
		} else if (result > 0) {
			low = index + 1;
		} else {
			return (uint8_t *) base + size * index;
		}
	}

	return nullptr;
}

char *EsCRTstrcpy(char *dest, const char *src) {
	size_t stringLength = EsCRTstrlen(src);
	EsCRTmemcpy(dest, src, stringLength + 1);
	return dest;
}

char *EsCRTstpcpy(char *dest, const char *src) {
	size_t stringLength = EsCRTstrlen(src);
	EsCRTmemcpy(dest, src, stringLength + 1);
	return dest + stringLength;
}

size_t EsCRTstrspn(const char *s, const char *accept) {
	size_t count = 0;

	while (true) {
		char character = *s;

		const char *search = accept;

		while (true) {
			char c = *search;

			if (!c) {
				break;
			} else if (character == c) {
				goto match;
			}

			search++;
		}

		return count;

		match:;
		count++;
		s++;
	}
}

char *EsCRTstrrchr(const char *s, int c) {
	const char *start = s;
	if (!s[0]) return NULL;
	s += EsCRTstrlen(s) - 1;

	while (true) {
		if (*s == c) {
			return (char *) s;
		}

		if (s == start) {
			return NULL;
		}

		s--;
	}
}

char *EsCRTstrchr(const char *s, int c) {
	while (true) {
		if (*s == c) {
			return (char *) s;
		}

		if (*s == 0) {
			return NULL;
		}

		s++;
	}
}

char *EsCRTstrncpy(char *dest, const char *src, size_t n) {
	size_t i;

	for (i = 0; i < n && src[i]; i++) {
		dest[i] = src[i];
	}

	for (; i < n; i++) {
		dest[i] = 0;
	}

	return dest;
}

char *EsCRTstrlcpy(char *dest, const char *src, size_t n) {
	size_t i;

	for (i = 0; i < n - 1 && src[i]; i++) {
		dest[i] = src[i];
	}

	for (; i < n; i++) {
		dest[i] = 0;
	}

	return dest;
}

int EsCRTmemcmp(const void *s1, const void *s2, size_t n) {
	return EsMemoryCompare((void *) s1, (void *) s2, n);
}

void *EsCRTmemchr(const void *_s, int _c, size_t n) {
	uint8_t *s = (uint8_t *) _s;
	uint8_t c = (uint8_t) _c;

	for (uintptr_t i = 0; i < n; i++) {
		if (s[i] == c) {
			return s + i;
		}
	}

	return nullptr;
}

int EsCRTisalpha(int c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int EsCRTisdigit(int c) {
	return (c >= '0' && c <= '9');
}

int EsCRTrand() {
	uint8_t a = EsRandomU8();
	uint8_t b = EsRandomU8();
	uint8_t c = EsRandomU8();
	return (a << 16) | (b << 8) | (c << 0);
}

int EsCRTisalnum(int c) {
	return EsCRTisalpha(c) || EsCRTisdigit(c);
}

int EsCRTiscntrl(int c) {
	return c < 0x20 || c == 0x7F;
}

int EsCRTisgraph(int c) {
	return c > ' ' && c < 0x7F;
}

int EsCRTislower(int c) {
	return c >= 'a' && c <= 'z';
}

int EsCRTisprint(int c) {
	return c >= ' ' && c < 127;
}

int EsCRTispunct(int c) {
	return c != ' ' && !EsCRTisalnum(c);
}

int EsCRTisupper(int c) {
	return c >= 'A' && c <= 'Z';
}

int EsCRTisxdigit(int c) {
	return EsCRTisdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

char *EsCRTsetlocale(int category, const char *locale) {
	(void) category;
	(void) locale;
	return nullptr;
}

void EsCRTsrand(unsigned int seed) {
	EsRandomSeed(seed);
}

#ifndef KERNEL
void EsCRTexit(int status) {
	EsProcessTerminate(ES_CURRENT_PROCESS, status);
}

void EsCRTabort() {
	EsPanic("EsCRTabort called.\n");
}
#endif

int EsCRTstrcoll(const char *s1, const char *s2) {
	return EsStringCompare(s1, EsCStringLength(s1), s2, EsCStringLength(s2));
}

char *EsCRTstrerror(int errnum) {
	(void) errnum;
	return (char*) "unknown operation failure";
}

char *EsCRTstrpbrk(const char *s, const char *accept) {
	size_t l1 = EsCStringLength(s), l2 = EsCStringLength(accept);

	for (uintptr_t i = 0; i <  l1; i++) {
		char c = s[i];

		for (uintptr_t j = 0; j < l2; j++) {
			if (accept[j] == c) {
				return (char *) (i + s);
			}
		}
	}

	return nullptr;
}

#ifdef USE_STB_SPRINTF
int EsCRTsprintf(char *buffer, const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	int length = stbsp_vsprintf(buffer, format, arguments);
	va_end(arguments);
	return length;
}

int EsCRTsnprintf(char *buffer, size_t bufferSize, const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	int length = stbsp_vsnprintf(buffer, bufferSize, format, arguments);
	va_end(arguments);
	return length;
}

int EsCRTvsnprintf(char *buffer, size_t bufferSize, const char *format, va_list arguments) {
	return stbsp_vsnprintf(buffer, bufferSize, format, arguments);
}
#endif

#endif

/////////////////////////////////
// Memory buffers.
/////////////////////////////////

#if defined(SHARED_COMMON_WANT_BUFFERS) || defined(SHARED_COMMON_WANT_ALL)

const void *EsBufferRead(EsBuffer *buffer, size_t readBytes) {
	if (!readBytes && buffer->position == buffer->bytes) {
		return buffer->in + buffer->position;
	} else if (buffer->position >= buffer->bytes || buffer->bytes - buffer->position < readBytes || buffer->error) {
		buffer->error = true;
		return NULL;
	} else {
		const void *pointer = buffer->in + buffer->position;
		buffer->position += readBytes;
		return pointer;
	}
}

bool EsBufferReadInto(EsBuffer *buffer, void *destination, size_t readBytes) {
	// TODO Support buffered reading from a EsFileStore.

	const void *source = EsBufferRead(buffer, readBytes);

	if (source) {
		EsMemoryCopy(destination, source, readBytes);
		return true;
	} else {
		EsMemoryZero(destination, readBytes);
		return false;
	}
}

const void *EsBufferReadMany(EsBuffer *buffer, size_t a, size_t b) {
	size_t c;

	if (__builtin_mul_overflow(a, b, &c)) {
		buffer->error = true;
		return NULL;
	} else {
		return EsBufferRead(buffer, c);
	}
}

float EsBufferReadFloat(EsBuffer *buffer) {
	const float *p = (const float *) EsBufferRead(buffer, sizeof(float));
	return p ? *p : 0;
}

uint8_t EsBufferReadByte(EsBuffer *buffer) {
	const uint8_t *p = (const uint8_t *) EsBufferRead(buffer, sizeof(uint8_t));
	return p ? *p : 0;
}

uint32_t EsBufferReadInt(EsBuffer *buffer) {
	const uint32_t *p = (const uint32_t *) EsBufferRead(buffer, sizeof(uint32_t));
	return p ? *p : 0;
}

#ifdef ES_API
void EsBufferFlushToFileStore(EsBuffer *buffer) {
	if (!buffer->position || buffer->error) return;
	EsAssert(buffer->fileStore && buffer->position <= buffer->bytes);
	buffer->error = !EsFileStoreAppend(buffer->fileStore, buffer->out, buffer->position);
	buffer->position = 0;
}
#endif

void *EsBufferWrite(EsBuffer *buffer, const void *source, size_t writeBytes) {
#ifdef ES_API
	tryAgain:;
#endif

	if (buffer->error) {
		return NULL;
#ifdef ES_API
	} else if (buffer->fileStore) {
		if (writeBytes > buffer->bytes) {
			EsBufferFlushToFileStore(buffer);

			if (!buffer->error) {
				buffer->error = !EsFileStoreAppend(buffer->fileStore, source, writeBytes);
			}
		} else {
			while (writeBytes && !buffer->error) {
				if (buffer->position == buffer->bytes) {
					EsBufferFlushToFileStore(buffer);
				} else {
					size_t bytesToWrite = writeBytes > buffer->bytes - buffer->position ? buffer->bytes - buffer->position : writeBytes;
					EsMemoryCopy(buffer->out + buffer->position, source, bytesToWrite);
					buffer->position += bytesToWrite;
					writeBytes -= bytesToWrite;
					source = (const uint8_t *) source + bytesToWrite;
				}
			}
		}

		return NULL;
#endif
	} else if (buffer->bytes - buffer->position < writeBytes) {
#ifdef ES_API
		if (buffer->canGrow) {
			size_t newBytes = buffer->bytes * 2 + 16;

			if (newBytes < writeBytes + buffer->position) {
				newBytes = writeBytes + buffer->position + 16;
			}

			char *newOut = (char *) EsHeapReallocate(buffer->out, newBytes, false);

			if (!newOut) {
				buffer->error = true;
			} else {
				buffer->bytes = newBytes;
				buffer->out = (uint8_t *) newOut;
				goto tryAgain;
			}
		} else {
			buffer->error = true;
		}
#else
		buffer->error = true;
#endif

		return NULL;
	} else {
		void *pointer = buffer->out + buffer->position;
		buffer->position += writeBytes;

		if (source) {
			EsMemoryCopy(pointer, source, writeBytes);
		} else {
			EsMemoryZero(pointer, writeBytes);
		}

		return pointer;
	}
}

bool EsBufferWriteInt8(EsBuffer *buffer, int8_t value) {
	EsBufferWrite(buffer, &value, sizeof(int8_t));
	return buffer->error;
}

bool EsBufferWriteInt32Endian(EsBuffer *buffer, int32_t value) {
#ifdef __BIG_ENDIAN__
	value = ByteSwap32(value);
#endif
	EsBufferWrite(buffer, &value, sizeof(int32_t));
	return buffer->error;
}

int32_t EsBufferReadInt32Endian(EsBuffer *buffer, int32_t errorValue) {
	int32_t *pointer = (int32_t *) EsBufferRead(buffer, sizeof(int32_t));
	if (!pointer) return errorValue;
#ifdef __BIG_ENDIAN__
	return ByteSwap32(*pointer);
#else
	return *pointer;
#endif
}

#endif

/////////////////////////////////
// Time and date.
/////////////////////////////////

#if defined(SHARED_COMMON_WANT_ALL)

const uint16_t daysBeforeMonthStart[] = { 
	0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, // Normal year.
	0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, // Leap year.
};

uint64_t DateToLinear(const EsDateComponents *components) {
	uint64_t dayCount = 365 * components->year + daysBeforeMonthStart[components->month - 1] + components->day - 1;
	uint16_t year = components->month < 3 ? components->year - 1 : components->year;
	dayCount += 1 + year / 4 - year / 100 + year / 400; // Add additional days for leap years, only including this year's if we're past February.
	return components->millisecond + 1000 * (components->second + 60 * (components->minute + 60 * (components->hour + 24 * dayCount)));
}

uint8_t DateCalculateDayOfWeek(const EsDateComponents *components) {
	return ((DateToLinear(components) / 86400000 + 5) % 7) + 1;
}

void DateToComponents(uint64_t x, EsDateComponents *components) {
	components->_unused = 0;

	components->millisecond = x % 1000, x /= 1000;
	components->second      = x %   60, x /=   60;
	components->minute      = x %   60, x /=   60;
	components->hour        = x %   24, x /=   24;
	// x = days since epoch.

	// 146097 days per 400 year block.
	components->year = (x / 146097) * 400;
	x %= 146097; // x = day within 400 year block.

	bool isLeapYear = true;

	// 36525 days in the first 100 year block, and 36524 per the other 100 year blocks.
	if (x < 36525) {
		components->year += (x / 1461) * 4;
		x %= 1461; // x = day within 4 year block.
	} else {
		x -= 36525;
		components->year += (x / 36524 + 1) * 100;
		x %= 36524; // x = day within 100 year block.

		// 1460 days in the first 4 year block, and 1461 per the other 4 year blocks.
		if (x < 1460) components->year += x / 365, x %= 365, isLeapYear = false;
		else components->year += ((x - 1460) / 1461 + 1) * 4, x = (x - 1460) % 1461;
	}

	if (x >= 366) components->year += (x - 1) / 365, x = (x - 1) % 365, isLeapYear = false;
	// x = day within year.

	for (uintptr_t i = 12; i >= 1; i--) {
		uint16_t offset = daysBeforeMonthStart[i + (isLeapYear ? 11 : -1)];

		if (x >= offset) {
			components->month = i;
			components->day = x - offset + 1;
			break;
		}
	}
}

#endif
