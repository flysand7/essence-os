// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#define THEME_LAYER_BOX     (1)
#define THEME_LAYER_TEXT    (2)
#define THEME_LAYER_METRICS (3)
#define THEME_LAYER_PATH    (4)

#define THEME_PAINT_SOLID           (1)
#define THEME_PAINT_LINEAR_GRADIENT (2)
#define THEME_PAINT_CUSTOM          (3)
#define THEME_PAINT_OVERWRITE       (4)
#define THEME_PAINT_RADIAL_GRADIENT (5)

#define THEME_LAYER_MODE_BACKGROUND (0)
#define THEME_LAYER_MODE_SHADOW     (1)
#define THEME_LAYER_MODE_CONTENT    (2)
#define THEME_LAYER_MODE_OVERLAY    (3)

#define THEME_OVERRIDE_I8    (1)
#define THEME_OVERRIDE_I16   (2)
#define THEME_OVERRIDE_F32   (3)
#define THEME_OVERRIDE_COLOR (4)

#define THEME_PRIMARY_STATE_ANY      (0)
#define THEME_PRIMARY_STATE_IDLE     (1)
#define THEME_PRIMARY_STATE_HOVERED  (2)
#define THEME_PRIMARY_STATE_PRESSED  (3)
#define THEME_PRIMARY_STATE_DISABLED (4)
#define THEME_PRIMARY_STATE_INACTIVE (5) // When the window has been deactivated.
#define THEME_PRIMARY_STATE_MASK (0x000F)

#define THEME_STATE_FOCUSED        (1 << 15)
#define THEME_STATE_CHECKED        (1 << 14)
#define THEME_STATE_INDETERMINATE  (1 << 13)
#define THEME_STATE_DEFAULT_BUTTON (1 << 12)
#define THEME_STATE_SELECTED       (1 << 11)
#define THEME_STATE_FOCUSED_ITEM   (1 << 10)
#define THEME_STATE_LIST_FOCUSED   (1 <<  9)
#define THEME_STATE_BEFORE_ENTER   (1 <<  8)
#define THEME_STATE_AFTER_EXIT     (1 <<  7)

#define THEME_STATE_CHECK(sequenceHeaderState, requestedState) \
	(((sequenceHeaderState & THEME_PRIMARY_STATE_MASK) == (requestedState & THEME_PRIMARY_STATE_MASK) \
		|| ((sequenceHeaderState & THEME_PRIMARY_STATE_MASK) == THEME_PRIMARY_STATE_ANY)) \
	&& !((sequenceHeaderState & ~THEME_PRIMARY_STATE_MASK) & ~(requestedState & ~THEME_PRIMARY_STATE_MASK))) \

#define THEME_LAYER_BOX_IS_BLURRED    (1 << 0)
#define THEME_LAYER_BOX_AUTO_CORNERS  (1 << 1)
#define THEME_LAYER_BOX_AUTO_BORDERS  (1 << 2)
#define THEME_LAYER_BOX_SHADOW_HIDING (1 << 3)

#define THEME_LAYER_PATH_FILL_EVEN_ODD (1 << 0)
#define THEME_LAYER_PATH_CLOSED        (1 << 1)

#define THEME_PATH_FILL_SOLID   (1 << 4)
#define THEME_PATH_FILL_CONTOUR (2 << 4)
#define THEME_PATH_FILL_DASHED  (3 << 4)

#define THEME_HEADER_SIGNATURE (0x96BC555A)

#define THEME_CHILD_TYPE_ONLY       (0)
#define THEME_CHILD_TYPE_FIRST      (1)
#define THEME_CHILD_TYPE_LAST       (2)
#define THEME_CHILD_TYPE_NONE       (3)
#define THEME_CHILD_TYPE_HORIZONTAL (1 << 4)

typedef enum ThemeCursor {
	THEME_CURSOR_NORMAL,
	THEME_CURSOR_TEXT,
	THEME_CURSOR_RESIZE_VERTICAL,
	THEME_CURSOR_RESIZE_HORIZONTAL,
	THEME_CURSOR_RESIZE_DIAGONAL_1,
	THEME_CURSOR_RESIZE_DIAGONAL_2,
	THEME_CURSOR_SPLIT_VERTICAL,
	THEME_CURSOR_SPLIT_HORIZONTAL,
	THEME_CURSOR_HAND_HOVER,
	THEME_CURSOR_HAND_DRAG,
	THEME_CURSOR_HAND_POINT,
	THEME_CURSOR_SCROLL_UP_LEFT,
	THEME_CURSOR_SCROLL_UP,
	THEME_CURSOR_SCROLL_UP_RIGHT,
	THEME_CURSOR_SCROLL_LEFT,
	THEME_CURSOR_SCROLL_CENTER,
	THEME_CURSOR_SCROLL_RIGHT,
	THEME_CURSOR_SCROLL_DOWN_LEFT,
	THEME_CURSOR_SCROLL_DOWN,
	THEME_CURSOR_SCROLL_DOWN_RIGHT,
	THEME_CURSOR_SELECT_LINES,
	THEME_CURSOR_DROP_TEXT,
	THEME_CURSOR_CROSS_HAIR_PICK,
	THEME_CURSOR_CROSS_HAIR_RESIZE,
	THEME_CURSOR_MOVE_HOVER,
	THEME_CURSOR_MOVE_DRAG,
	THEME_CURSOR_ROTATE_HOVER,
	THEME_CURSOR_ROTATE_DRAG,
	THEME_CURSOR_BLANK,
} ThemeCursor;

typedef struct ThemePaintSolid {
	uint32_t color;
} ThemePaintSolid;

typedef struct ThemeGradientStop {
	uint32_t color;
	int8_t position;
	uint8_t windowColorIndex;
	uint16_t _unused1;
} ThemeGradientStop;

typedef struct ThemePaintLinearGradient {
	float transform[3];
	uint8_t stopCount;
	int8_t useGammaInterpolation : 1, useDithering : 1;
	uint8_t repeatMode;
	uint8_t _unused0;
	// Followed by gradient stops.
} ThemePaintLinearGradient;

typedef struct ThemePaintRadialGradient {
	float transform[6];
	uint8_t stopCount;
	int8_t useGammaInterpolation : 1, useDithering : 1;
	uint8_t repeatMode;
	uint8_t _unused0;
	// Followed by gradient stops.
} ThemePaintRadialGradient;

#ifndef IN_DESIGNER
typedef uint32_t (*EsFragmentShaderCallback)(int x, int y, struct StyledBox *box);

typedef struct ThemePaintCustom {
	EsFragmentShaderCallback callback;
} ThemePaintCustom;
#else
typedef struct ThemePaintCustom {
} ThemePaintCustom;
#endif

typedef struct ThemeLayerBox {
	Rectangle8 borders, offset;
	Corners8 corners;
	int8_t flags, mainPaintType, borderPaintType;
	uint8_t _unused0;
	uint32_t _unused1; // Align to 8 bytes for ThemePaintCustom.
	// Followed by main paint data, then border paint data.
} ThemeLayerBox;

typedef struct ThemeLayerText {
	uint32_t color;
	uint16_t _unused0;
	int8_t blur;
	uint8_t _unused1;
} ThemeLayerText;

typedef struct ThemeLayerIcon {
	int8_t align;
	uint8_t _unused0;
	uint16_t alpha;
	Rectangle16 image;
} ThemeLayerIcon;

typedef struct ThemeLayerPathFillContour {
	float miterLimit;
	uint8_t internalWidth, externalWidth;
	uint8_t mode;
} ThemeLayerPathFillContour;

typedef struct ThemeLayerPathFillDash {
	ThemeLayerPathFillContour contour;
	uint8_t length, gap;
} ThemeLayerPathFillDash;

typedef struct ThemeLayerPathFill {
	uint8_t paintAndFillType;
	uint8_t dashCount;
	uint16_t _unused0;
	// Followed by paint data, then fill type specific data.
} ThemeLayerPathFill;

typedef struct ThemeLayerPath {
	uint8_t flags, fillCount;
	uint16_t alpha, pointCount, _unused0;
	// Followed by points (2*3 floats per point), then fills.
} ThemeLayerPath;

typedef struct ThemeLayer {
	uint32_t overrideListOffset;
	uint16_t overrideCount;
	uint16_t _unused;
	Rectangle8 offset; // (dpx)
	Rectangle8 position; // (percent)
	int8_t mode, type;
	uint16_t dataByteCount;
	// Followed by type-specific data.
} ThemeLayer;

typedef struct ThemeMetrics {
	Rectangle16 insets, clipInsets;
	uint8_t clipEnabled, cursor;
	uint16_t fontFamily; // TODO This needs to be validated when loading.
	int16_t preferredWidth, preferredHeight;
	int16_t minimumWidth, minimumHeight;
	int16_t maximumWidth, maximumHeight;
	int16_t gapMajor, gapMinor, gapWrap;
	uint32_t textColor, selectedBackground, selectedText, iconColor;
	int8_t textAlign, fontWeight;
	int16_t textSize, iconSize;
	uint8_t textFigures;
	bool isItalic, layoutVertical;
} ThemeMetrics;

typedef union ThemeVariant {
	int8_t i8;
	int16_t i16;
	uint32_t u32;
	float f32;
} ThemeVariant;

typedef struct ThemeOverride {
	uint16_t state, duration;
	uint16_t offset;
	uint8_t type, _unused;
	ThemeVariant data;
} ThemeOverride;

typedef struct ThemeStyle {
	// A list of uint32_t, giving offsets to ThemeLayer. First is the metrics layer.
	// **This must be the first field in the structure; see the end of Export in util/designer2.cpp.**
	uint32_t layerListOffset; 

	uint16_t id;
	uint8_t layerCount;
	uint8_t _unused1;
	Rectangle8 paintOutsets, opaqueInsets, approximateBorders;
} ThemeStyle;

typedef struct ThemeConstant {
	uint64_t hash;
	char cValue[12];
	bool scale;
} ThemeConstant;

typedef struct ThemeHeader {
	uint32_t signature;
	uint32_t styleCount, constantCount;
	// Followed by array of ThemeStyles and then an array of ThemeConstants.
} ThemeHeader;

//////////////////////////////////////////

#define THEME_RECT_WIDTH(_r) ((_r).r - (_r).l)
#define THEME_RECT_HEIGHT(_r) ((_r).b - (_r).t)
#define THEME_RECT_4(x, y, z, w) ((EsRectangle) { (x), (y), (z), (w) })
#define THEME_RECT_VALID(_r) (THEME_RECT_WIDTH(_r) > 0 && THEME_RECT_HEIGHT(_r) > 0)

EsRectangle ThemeRectangleIntersection(EsRectangle a, EsRectangle b) {
	if (a.l < b.l) a.l = b.l;
	if (a.t < b.t) a.t = b.t;
	if (a.r > b.r) a.r = b.r;
	if (a.b > b.b) a.b = b.b;
	return a;
}

typedef struct ThemePaintData {
	int8_t type;

	union {
		const ThemePaintSolid *solid;
		const ThemePaintLinearGradient *linearGradient;
		const ThemePaintCustom *custom;
	};
} ThemePaintData;

typedef struct GradientCache {
#define GRADIENT_CACHE_COUNT (256)
	uint32_t colors[GRADIENT_CACHE_COUNT];
#if 0
	uint32_t ditherColors[GRADIENT_CACHE_COUNT];
	uint32_t ditherThresholds[GRADIENT_CACHE_COUNT];
#endif
#define GRADIENT_COORD_BASE (16)
	int start, dx, dy;
	int ox, oy;
	bool opaque, dithered;
	void *context;
} GradientCache;

static const float gaussLookup[] = {
	1.000000, 0.999862, 0.999450, 0.998764, 0.997805, 0.996572, 0.995068, 0.993293,
	0.991249, 0.988937, 0.986360, 0.983520, 0.980418, 0.977058, 0.973442, 0.969573,
	0.965454, 0.961089, 0.956480, 0.951633, 0.946549, 0.941235, 0.935693, 0.929928,
	0.923946, 0.917749, 0.911344, 0.904735, 0.897927, 0.890926, 0.883736, 0.876364,
	0.868815, 0.861094, 0.853207, 0.845160, 0.836960, 0.828611, 0.820121, 0.811494,
	0.802738, 0.793858, 0.784861, 0.775752, 0.766539, 0.757227, 0.747823, 0.738333,
	0.728763, 0.719119, 0.709409, 0.699637, 0.689810, 0.679935, 0.670017, 0.660062,
	0.650077, 0.640067, 0.630038, 0.619995, 0.609946, 0.599894, 0.589846, 0.579807,
	0.569782, 0.559777, 0.549797, 0.539846, 0.529930, 0.520053, 0.510220, 0.500435,
	0.490704, 0.481029, 0.471416, 0.461867, 0.452388, 0.442982, 0.433653, 0.424403,
	0.415236, 0.406156, 0.397166, 0.388267, 0.379464, 0.370759, 0.362153, 0.353651,
	0.345253, 0.336962, 0.328780, 0.320708, 0.312749, 0.304903, 0.297173, 0.289559,
	0.282062, 0.274685, 0.267426, 0.260289, 0.253272, 0.246376, 0.239602, 0.232951,
	0.226422, 0.220016, 0.213732, 0.207571, 0.201532, 0.195614, 0.189819, 0.184144,
	0.178591, 0.173157, 0.167842, 0.162646, 0.157567, 0.152605, 0.147759, 0.143027,
	0.138409, 0.133903, 0.129508, 0.125223, 0.121047, 0.116978, 0.113014, 0.109155,
	0.105399, 0.101744, 0.098188, 0.094731, 0.091371, 0.088106, 0.084933, 0.081853,
	0.078863, 0.075961, 0.073146, 0.070415, 0.067769, 0.065203, 0.062718, 0.060310,
	0.057980, 0.055723, 0.053541, 0.051429, 0.049387, 0.047413, 0.045506, 0.043663,
	0.041883, 0.040165, 0.038507, 0.036907, 0.035364, 0.033876, 0.032442, 0.031060,
	0.029729, 0.028447, 0.027212, 0.026025, 0.024882, 0.023782, 0.022726, 0.021710,
	0.020734, 0.019796, 0.018895, 0.018031, 0.017201, 0.016405, 0.015642, 0.014910,
	0.014208, 0.013536, 0.012892, 0.012275, 0.011684, 0.011119, 0.010578, 0.010061,
	0.009567, 0.009094, 0.008642, 0.008211, 0.007798, 0.007405, 0.007029, 0.006671,
	0.006329, 0.006003, 0.005692, 0.005396, 0.005114, 0.004845, 0.004590, 0.004346,
	0.004114, 0.003894, 0.003684, 0.003485, 0.003295, 0.003115, 0.002944, 0.002782,
	0.002628, 0.002482, 0.002343, 0.002211, 0.002086, 0.001968, 0.001856, 0.001750,
	0.001649, 0.001554, 0.001464, 0.001378, 0.001298, 0.001221, 0.001149, 0.001081,
	0.001017, 0.000956, 0.000899, 0.000844, 0.000793, 0.000745, 0.000699, 0.000656,
	0.000616, 0.000578, 0.000542, 0.000508, 0.000476, 0.000446, 0.000418, 0.000391,
	0.000366, 0.000343, 0.000321, 0.000300, 0.000281, 0.000263, 0.000245, 0.000229,
	0.000214, 0.000200, 0.000187, 0.000174, 0.000163, 0.000152, 0.000141, 0.000000,
};

#ifndef IN_DESIGNER
struct UIStyleKey {
	uintptr_t part;
	float scale;
	uint16_t stateFlags;
	uint16_t _unused1;
};

struct {
	bool initialised;
	EsBuffer system;
	const ThemeHeader *header;
	EsPaintTarget cursors;
	EsHandle cursorData;
	float scale;
	HashStore<UIStyleKey, struct UIStyle *> loadedStyles;
	uint32_t windowColors[6];
} theming;
#endif

ES_FUNCTION_OPTIMISE_O2 
void ThemeFillRectangle(EsPainter *painter, EsRectangle bounds, ThemePaintData paint, GradientCache *gradient) {
	uint32_t *bits = (uint32_t *) painter->target->bits;
	int width = painter->target->width;
	bounds = ThemeRectangleIntersection(bounds, painter->clip);
	if (!THEME_RECT_VALID(bounds)) return;

	if (paint.type == THEME_PAINT_SOLID) {
#if !defined(IN_DESIGNER) || defined(DESIGNER2)
		_DrawBlock(painter->target->stride, bits, bounds, paint.solid->color, painter->target->fullAlpha);
#else
		uint32_t color = paint.solid->color;

		if ((color & 0xFF000000) == 0) {
		} else if ((color & 0xFF000000) == 0xFF000000) {
			for (int y = bounds.t; y < bounds.b; y++) {
				int x = bounds.l;
				uint32_t *b = bits + x + y * width;
				do { *b = color; x++, b++; } while (x < bounds.r);
			}
		} else {
			for (int y = bounds.t; y < bounds.b; y++) {
				int x = bounds.l;
				uint32_t *b = bits + x + y * width;
				do { BlendPixel(b, color, painter->target->fullAlpha); x++, b++; } while (x < bounds.r);
			}
		}
#endif
	} else if (paint.type == THEME_PAINT_LINEAR_GRADIENT) {
#if 0
		if (gradient->dithered) {
			uint32_t random = 0;

			for (int y = bounds.t; y < bounds.b; y++) {
				int x = bounds.l, p = (y - gradient->oy) * gradient->dy + (bounds.l - gradient->ox) * gradient->dx + gradient->start;
				uint32_t *b = bits + bounds.l + y * width;

				do { 
					random = (random * 1103515245) + 12345;
					uintptr_t index = ClampInteger(0, GRADIENT_CACHE_COUNT - 1, p >> GRADIENT_COORD_BASE);

					if (random > gradient->ditherThresholds[index]) {
						*b = gradient->ditherColors[index]; 
					} else {
						*b = gradient->colors[index]; 
					}

					x++, b++, p += gradient->dx; 
				} while (x < bounds.r);
			}
		} else
#endif
		if (gradient->opaque) {
			for (int y = bounds.t; y < bounds.b; y++) {
				int x = bounds.l, p = (y - gradient->oy) * gradient->dy + (bounds.l - gradient->ox) * gradient->dx + gradient->start;
				uint32_t *b = bits + bounds.l + y * width;

				do { 
					*b = gradient->colors[ClampInteger(0, GRADIENT_CACHE_COUNT - 1, p >> GRADIENT_COORD_BASE)]; 
					x++, b++, p += gradient->dx; 
				} while (x < bounds.r);
			}
		} else {
			for (int y = bounds.t; y < bounds.b; y++) {
				int x = bounds.l, p = (y - gradient->oy) * gradient->dy + (bounds.l - gradient->ox) * gradient->dx + gradient->start;
				uint32_t *b = bits + bounds.l + y * width;

				do { 
					BlendPixel(b, gradient->colors[ClampInteger(0, GRADIENT_CACHE_COUNT - 1, p >> GRADIENT_COORD_BASE)], painter->target->fullAlpha); 
					x++, b++, p += gradient->dx; 
				} while (x < bounds.r);
			}
		}
#ifndef IN_DESIGNER
	} else if (paint.type == THEME_PAINT_CUSTOM) {
		for (int y = bounds.t; y < bounds.b; y++) {
			int x = bounds.l;
			uint32_t *b = bits + bounds.l + y * width;

			do { 
				BlendPixel(b, paint.custom->callback(x, y, (StyledBox *) gradient->context), painter->target->fullAlpha);
				x++, b++; 
			} while (x < bounds.r);
		}
#endif
	} else if (paint.type == THEME_PAINT_OVERWRITE) {
		uint32_t color = paint.solid->color;

		for (int y = bounds.t; y < bounds.b; y++) {
			int x = bounds.l;
			uint32_t *b = bits + x + y * width;
			do { *b = color; x++, b++; } while (x < bounds.r);
		}
	}
}

ES_FUNCTION_OPTIMISE_O2 
void ThemeFillCorner(EsPainter *painter, EsRectangle bounds, int cx, int cy, 
		int border, int corner, ThemePaintData mainPaint, ThemePaintData borderPaint, 
		GradientCache *mainGradient, GradientCache *borderGradient) {
	uint32_t *bits = (uint32_t *) painter->target->bits;
	int width = painter->target->width;

	int oldLeft = bounds.l, oldTop = bounds.t;
	bounds = ThemeRectangleIntersection(bounds, painter->clip);
	if (!THEME_RECT_VALID(bounds)) return;
	cx += oldLeft - bounds.l, cy += oldTop - bounds.t;

#define STYLE_CORNER_OVERSAMPLING (3)
	border <<= STYLE_CORNER_OVERSAMPLING;
	corner <<= STYLE_CORNER_OVERSAMPLING;
	cx <<= STYLE_CORNER_OVERSAMPLING;
	cy <<= STYLE_CORNER_OVERSAMPLING;

	int mainRadius2 = corner > border ? corner - border : 0;
	int borderRadius2 = corner * corner;
	mainRadius2 *= mainRadius2;

	int oversampledWidth = THEME_RECT_WIDTH(bounds) << STYLE_CORNER_OVERSAMPLING;
	int oversampledHeight = THEME_RECT_HEIGHT(bounds) << STYLE_CORNER_OVERSAMPLING;

	uint32_t *b0 = bits + bounds.t * width;

	for (int j = 0; j < oversampledHeight; j += (1 << STYLE_CORNER_OVERSAMPLING), b0 += width) {
		int i = 0;
		uint32_t *b = b0 + bounds.l;

		do { 
			int mainCount = 0, borderCount = 0, outsideCount = 0;

			for (int x = 0; x < (1 << STYLE_CORNER_OVERSAMPLING); x++) {
				for (int y = 0; y < (1 << STYLE_CORNER_OVERSAMPLING); y++) {
					int dx = i + x - cx, dy = j + y - cy;
					int radius2 = dx * dx + dy * dy;
					if (radius2 <= mainRadius2) mainCount++;
					else if (radius2 <= borderRadius2) borderCount++;
					else outsideCount++;
				}
			}

			uint32_t mainColor, borderColor;

			if (mainPaint.type == THEME_PAINT_SOLID || mainPaint.type == THEME_PAINT_OVERWRITE) {
				mainColor = mainPaint.solid->color;
			} else if (mainPaint.type == THEME_PAINT_LINEAR_GRADIENT) {
				mainColor = mainGradient->colors[ClampInteger(0, GRADIENT_CACHE_COUNT - 1, 
					(((j >> STYLE_CORNER_OVERSAMPLING) - mainGradient->oy + bounds.t) * mainGradient->dy 
					+ ((i >> STYLE_CORNER_OVERSAMPLING) - mainGradient->ox + bounds.l) * mainGradient->dx + mainGradient->start)
					>> GRADIENT_COORD_BASE)]; 
#ifndef IN_DESIGNER
			} else if (mainPaint.type == THEME_PAINT_CUSTOM) {
				mainColor = mainPaint.custom->callback((i >> STYLE_CORNER_OVERSAMPLING) - mainGradient->ox + bounds.l, 
						(j >> STYLE_CORNER_OVERSAMPLING) - mainGradient->oy + bounds.t, (StyledBox *) mainGradient->context);
#endif
			} else {
				mainColor = 0;
			}

			if (borderPaint.type == THEME_PAINT_SOLID || borderPaint.type == THEME_PAINT_OVERWRITE) {
				borderColor = borderPaint.solid->color;
			} else if (borderPaint.type == THEME_PAINT_LINEAR_GRADIENT) {
				borderColor = borderGradient->colors[ClampInteger(0, GRADIENT_CACHE_COUNT - 1, 
					(((j >> STYLE_CORNER_OVERSAMPLING) - borderGradient->oy + bounds.t) * borderGradient->dy 
					+ ((i >> STYLE_CORNER_OVERSAMPLING) - borderGradient->ox + bounds.l) * borderGradient->dx + borderGradient->start)
					>> GRADIENT_COORD_BASE)]; 
			} else {
				borderColor = 0;
			}

			uint32_t mainAlpha = mainColor >> 24;
			uint32_t borderAlpha = borderColor >> 24;

			if (outsideCount == (1 << (2 * STYLE_CORNER_OVERSAMPLING))) {
			} else if (mainPaint.type == THEME_PAINT_OVERWRITE) {
				// TODO Support borders when using an overwrite main paint.
				// TODO Anti-aliasing (if it's possible).

				if (mainCount > (1 << (2 * STYLE_CORNER_OVERSAMPLING - 1))) {
					*b = mainColor;
				}
			} else if (outsideCount || ((borderColor & 0xFF000000) != 0xFF000000) || (mainColor & 0xFF000000) != 0xFF000000) {
				BlendPixel(b, (mainColor & 0x00FFFFFF) | (((mainAlpha * mainCount) << (24 - STYLE_CORNER_OVERSAMPLING * 2)) & 0xFF000000), 
						painter->target->fullAlpha);
				BlendPixel(b, (borderColor & 0x00FFFFFF) | (((borderAlpha * borderCount) << (24 - STYLE_CORNER_OVERSAMPLING * 2)) & 0xFF000000), 
						painter->target->fullAlpha);
			} else if (mainCount == (1 << (2 * STYLE_CORNER_OVERSAMPLING))) {
				BlendPixel(b, mainColor, painter->target->fullAlpha);
			} else if (borderCount == (1 << (2 * STYLE_CORNER_OVERSAMPLING))) {
				BlendPixel(b, borderColor, painter->target->fullAlpha);
			} else {
				uint32_t blend = mainColor;
				BlendPixel(&blend, (borderColor & 0x00FFFFFF) | (((borderAlpha * borderCount) << (24 - STYLE_CORNER_OVERSAMPLING * 2)) & 0xFF000000), true);
				BlendPixel(b, blend, painter->target->fullAlpha);
			}

			i += (1 << STYLE_CORNER_OVERSAMPLING), b++; 
		} while (i < oversampledWidth);
	}
}

ES_FUNCTION_OPTIMISE_O2 
void ThemeFillBlurCorner(EsPainter *painter, EsRectangle bounds, int cx, int cy, int border, int corner, GradientCache *gradient) {
	uint32_t *bits = (uint32_t *) painter->target->bits;
	int width = painter->target->width;
	cx += bounds.l, cy += bounds.t;
	bounds = ThemeRectangleIntersection(bounds, painter->clip);

	if (!THEME_RECT_VALID(bounds)) {
		return;
	}

	int dp = (GRADIENT_CACHE_COUNT << GRADIENT_COORD_BASE) / border;
	int mainRadius = corner > border ? corner - border : 0;

	for (int y = bounds.t; y < bounds.b; y++) {
		int x = bounds.l;
		uint32_t *b = bits + x + y * width;

		do { 
			int dx = x - cx, dy = y - cy;
			int radius2 = dx * dx + dy * dy;
			int p = (EsCRTsqrtf(radius2) - mainRadius) * dp;
			BlendPixel(b, gradient->colors[ClampInteger(0, GRADIENT_CACHE_COUNT - 1, p >> GRADIENT_COORD_BASE)], painter->target->fullAlpha); 
			x++, b++;
		} while (x < bounds.r);
	}
}

uint32_t WindowColorCalculate(uint8_t index) {
#ifdef IN_DESIGNER
	uint32_t windowColors[6] = {
		0xFF83B8F7, 0xFF6D9CDF, 0xFF4166B5, // Active container window gradient stops.
		0xFFD2D0F2, 0xFFCDCCFA, 0xFFB5BEDF, // Inactive container window gradient stops.
	};
#else
	uint32_t *windowColors = theming.windowColors;
#endif

	if (index >= 30 && index < 60) {
		return EsColorInterpolate(windowColors[0], windowColors[3], (index - 30) / 29.0f);
	} else if (index >= 60 && index < 90) {
		return EsColorInterpolate(windowColors[1], windowColors[4], (index - 60) / 29.0f);
	} else if (index >= 90 && index < 120) {
		return EsColorInterpolate(windowColors[2], windowColors[5], (index - 90) / 29.0f);
	} else {
		return 0;
	}
}

ES_FUNCTION_OPTIMISE_O2 
void GradientCacheSetup(GradientCache *cache, const ThemePaintLinearGradient *gradient, int width, int height, EsBuffer *data) {
	if (!gradient) {
		return;
	}

	width--, height--;

	cache->dx = gradient->transform[0] / width * (GRADIENT_CACHE_COUNT << GRADIENT_COORD_BASE);
	cache->dy = gradient->transform[1] / height * (GRADIENT_CACHE_COUNT << GRADIENT_COORD_BASE);
	cache->start = gradient->transform[2] * (GRADIENT_CACHE_COUNT << GRADIENT_COORD_BASE);

	cache->opaque = true;
	cache->dithered = gradient->useDithering;

	if (!gradient->stopCount) {
		return;
	}

	const ThemeGradientStop *stop0 = (ThemeGradientStop *) EsBufferRead(data, sizeof(ThemeGradientStop));

	for (uintptr_t stop = 0; stop < (uintptr_t) (gradient->stopCount - 1); stop++) {
		const ThemeGradientStop *stop1 = (ThemeGradientStop *) EsBufferRead(data, sizeof(ThemeGradientStop));

		if (!stop1) {
			return;
		}

		uint32_t color0 = stop0->color;
		uint32_t color1 = stop1->color;

		if (stop0->windowColorIndex) color0 = WindowColorCalculate(stop0->windowColorIndex);
		if (stop1->windowColorIndex) color1 = WindowColorCalculate(stop1->windowColorIndex);

		float fa = ((color0 >> 24) & 0xFF) / 255.0f;
		float fb = ((color0 >> 16) & 0xFF) / 255.0f;
		float fg = ((color0 >>  8) & 0xFF) / 255.0f;
		float fr = ((color0 >>  0) & 0xFF) / 255.0f;
		float ta = ((color1 >> 24) & 0xFF) / 255.0f;
		float tb = ((color1 >> 16) & 0xFF) / 255.0f;
		float tg = ((color1 >>  8) & 0xFF) / 255.0f;
		float tr = ((color1 >>  0) & 0xFF) / 255.0f;

		if (fa && !ta) { tr = fr, tg = fg, tb = fb; }
		if (ta && !fa) { fr = tr, fg = tg, fb = tb; }
		
		int fi = GRADIENT_CACHE_COUNT * (stop == 0 ? 0 : stop0->position / 100.0);
		int ti = GRADIENT_CACHE_COUNT * (stop == (uintptr_t) (gradient->stopCount - 2) ? 1 : stop1->position / 100.0);
		if (fi < 0) fi = 0;
		if (ti > GRADIENT_CACHE_COUNT) ti = GRADIENT_CACHE_COUNT;
		
		for (int i = fi; i < ti; i++) {
			float p = (float) (i - fi) / (ti - fi - 1);

			if (p < 0) p = 0;
			if (p > 1) p = 1;

			if (gradient->useGammaInterpolation) {
				cache->colors[i] = (uint32_t) (GammaInterpolate(fr, tr, p) * 255.0f) <<  0
						 | (uint32_t) (GammaInterpolate(fg, tg, p) * 255.0f) <<  8
						 | (uint32_t) (GammaInterpolate(fb, tb, p) * 255.0f) << 16
						 | (uint32_t) ((fa + (ta - fa) * p) * 255.0f) << 24;
			} else {
				cache->colors[i] = (uint32_t) (LinearInterpolate(fr, tr, p) * 255.0f) <<  0
						 | (uint32_t) (LinearInterpolate(fg, tg, p) * 255.0f) <<  8
						 | (uint32_t) (LinearInterpolate(fb, tb, p) * 255.0f) << 16
						 | (uint32_t) ((fa + (ta - fa) * p) * 255.0f) << 24;
			}

			if ((cache->colors[i] & 0xFF000000) != 0xFF000000) {
				cache->opaque = false;
			}
		}

		stop0 = stop1;
	}

#if 0
	if (gradient->useDithering) {
		for (uintptr_t i = 0; i < GRADIENT_CACHE_COUNT; i++) {
			uint32_t mainColor = cache->colors[i];
			uint32_t ditherColor = mainColor;
			int forwardSteps = 0, totalSteps = 0;

			for (uintptr_t j = i + 1; j < GRADIENT_CACHE_COUNT; j++) {
				forwardSteps++;

				if (cache->colors[j] != mainColor) {
					ditherColor = cache->colors[j];
					break;
				}
			}

			totalSteps = forwardSteps;

			for (intptr_t j = i - 1; j >= 0; j--) {
				totalSteps++;

				if (cache->colors[j] != mainColor) {
					break;
				}
			}

			if (!totalSteps) {
				totalSteps = 1;
			}

			cache->ditherColors[i] = ditherColor;
			cache->ditherThresholds[i] = (uint32_t) ((float) forwardSteps / totalSteps * 65535.0f) << 16;
		}
	}
#endif
}

void ThemeDrawBox(EsPainter *painter, EsRectangle rect, EsBuffer *data, float scale, 
		const ThemeLayer *layer, EsRectangle opaqueRegion, int childType) {
	const ThemeLayerBox *box = (const ThemeLayerBox *) EsBufferRead(data, sizeof(ThemeLayerBox));
	if (!box) return;

	if ((box->flags & THEME_LAYER_BOX_SHADOW_HIDING) && layer->offset.l == 0 && layer->offset.r == 0 && layer->offset.t == 0 && layer->offset.b == 0
			&& box->offset.l == 0 && box->offset.r == 0 && box->offset.t == 0 && box->offset.b == 0
			&& THEME_RECT_VALID(opaqueRegion)) {
		return;
	}

	rect.l += box->offset.l * scale;
	rect.r += box->offset.r * scale;
	rect.t += box->offset.t * scale;
	rect.b += box->offset.b * scale;

	int width = THEME_RECT_WIDTH(rect), height = THEME_RECT_HEIGHT(rect);

#ifdef IN_DESIGNER
	if (!THEME_RECT_VALID(UIRectangleIntersection(rect, painter->clip))) {
#else
	if (!THEME_RECT_VALID(EsRectangleIntersection(rect, painter->clip))) {
#endif
		return;
	}

	bool isBlurred = box->flags & THEME_LAYER_BOX_IS_BLURRED;

	ThemePaintData mainPaint, borderPaint;
	GradientCache mainGradient, borderGradient;

	mainPaint.type = box->mainPaintType;
	borderPaint.type = box->borderPaintType;

	if (mainPaint.type == 0) {
	} else if (mainPaint.type == THEME_PAINT_SOLID || mainPaint.type == THEME_PAINT_OVERWRITE) {
		mainPaint.solid = (const ThemePaintSolid *) EsBufferRead(data, sizeof(ThemePaintSolid));
	} else if (mainPaint.type == THEME_PAINT_LINEAR_GRADIENT) {
		mainPaint.linearGradient = (const ThemePaintLinearGradient *) EsBufferRead(data, sizeof(ThemePaintLinearGradient));
		GradientCacheSetup(&mainGradient, mainPaint.linearGradient, width, height, data);
		mainGradient.ox = rect.l;
		mainGradient.oy = rect.t;
	} else if (mainPaint.type == THEME_PAINT_CUSTOM && data->context) {
		mainPaint.custom = (const ThemePaintCustom *) EsBufferRead(data, sizeof(ThemePaintCustom));
		mainGradient.ox = rect.l;
		mainGradient.oy = rect.t;
		mainGradient.context = data->context;
	} else {
		data->error = true;
	}

	if (borderPaint.type == 0) {
	} else if (borderPaint.type == THEME_PAINT_SOLID || borderPaint.type == THEME_PAINT_OVERWRITE) {
		borderPaint.solid = (ThemePaintSolid *) EsBufferRead(data, sizeof(ThemePaintSolid));
	} else if (borderPaint.type == THEME_PAINT_LINEAR_GRADIENT) {
		borderPaint.linearGradient = (ThemePaintLinearGradient *) EsBufferRead(data, sizeof(ThemePaintLinearGradient));
		if (!data->error) GradientCacheSetup(&borderGradient, borderPaint.linearGradient, width, height, data);
		borderGradient.ox = rect.l;
		borderGradient.oy = rect.t;
	} else {
		data->error = true;
	}
	
	if (data->error) {
		return;
	}

	if (mainPaint.type == 0 && borderPaint.type == 0) {
		return;
	}

	if (isBlurred && borderPaint.type == THEME_PAINT_SOLID) {
		float alpha = borderPaint.solid->color >> 24;
		uint32_t color = borderPaint.solid->color & 0xFFFFFF;
		borderGradient.start = 0;
		borderGradient.opaque = false;
		borderGradient.dithered = false;

		for (uintptr_t i = 0; i < GRADIENT_CACHE_COUNT; i++) {
			borderGradient.colors[i] = color | (uint32_t) (alpha * gaussLookup[i]) << 24;
		}

		mainPaint.type = THEME_PAINT_SOLID;
		mainPaint.solid = borderPaint.solid;

		borderPaint.type = THEME_PAINT_LINEAR_GRADIENT;
		borderPaint.linearGradient = NULL;
	}

	Corners32 corners = { (int) (box->corners.tl * scale + 0.5f), (int) (box->corners.tr * scale + 0.5f), 
		(int) (box->corners.bl * scale + 0.5f), (int) (box->corners.br * scale + 0.5f) };

	if (box->flags & THEME_LAYER_BOX_AUTO_CORNERS) {
		bool horizontal = childType & THEME_CHILD_TYPE_HORIZONTAL;
		int c = childType & ~THEME_CHILD_TYPE_HORIZONTAL;

		if (c == THEME_CHILD_TYPE_FIRST) {
			if (horizontal) corners.tr = corners.br = 0; else corners.bl = corners.br = 0;
		} else if (c == THEME_CHILD_TYPE_LAST) {
			if (horizontal) corners.tl = corners.bl = 0; else corners.tl = corners.tr = 0;
		} else if (c != THEME_CHILD_TYPE_ONLY) {
			corners.tl = corners.tr = corners.bl = corners.br = 0;
		}
	}

	if (isBlurred) {
		corners.bl = corners.tr = corners.br = corners.tl;
	}

	if (corners.tl + corners.tr > width) { float p = (float) corners.tl / (corners.tl + corners.tr); corners.tl = p * width; corners.tr = (1 - p) * width; }
	if (corners.bl + corners.br > width) { float p = (float) corners.bl / (corners.bl + corners.br); corners.bl = p * width; corners.br = (1 - p) * width; }
	if (corners.tl + corners.bl > height) { float p = (float) corners.tl / (corners.tl + corners.bl); corners.tl = p * height; corners.bl = (1 - p) * height; }
	if (corners.tr + corners.br > height) { float p = (float) corners.tr / (corners.tr + corners.br); corners.tr = p * height; corners.br = (1 - p) * height; }

	Rectangle32 borders = { (int) (box->borders.l * scale), (int) (box->borders.r * scale), 
		(int) (box->borders.t * scale), (int) (box->borders.b * scale) };

	if (box->flags & THEME_LAYER_BOX_AUTO_BORDERS) {
		bool horizontal = childType & THEME_CHILD_TYPE_HORIZONTAL;
		int c = childType & ~THEME_CHILD_TYPE_HORIZONTAL;

		if (c == THEME_CHILD_TYPE_FIRST) {
			if (horizontal) borders.r = 0; else borders.b = 0;
		} else if (c == THEME_CHILD_TYPE_LAST) {
			if (horizontal) borders.l = 0; else borders.t = 0;
		} else if (c != THEME_CHILD_TYPE_ONLY) {
			if (horizontal) borders.l = borders.r = 0; else borders.t = borders.b = 0;
		}
	}

	if (isBlurred) {
		borders.r = borders.t = borders.b = borders.l;
	}

	if (borders.l + borders.r > width) { float p = (float) borders.l / (borders.l + borders.r); borders.l = p * width; borders.r = (1 - p) * width; }
	if (borders.t + borders.b > height) { float p = (float) borders.t / (borders.t + borders.b); borders.t = p * height; borders.b = (1 - p) * height; }

	if (isBlurred && (!borders.l || !borders.r || !borders.t || !borders.b)) {
		return;
	}

	Corners32 cornerBorders = { MaximumInteger(borders.l, borders.t), MaximumInteger(borders.t, borders.r),
		MaximumInteger(borders.l, borders.b), MaximumInteger(borders.b, borders.r) };

	if (isBlurred) {
		borderGradient.dx = -(GRADIENT_CACHE_COUNT << GRADIENT_COORD_BASE) / borders.l;
		borderGradient.ox = rect.l + borders.l;
		borderGradient.dy = borderGradient.oy = 0;
	}

	ThemeFillRectangle(painter, THEME_RECT_4(rect.l, rect.l + borders.l, rect.t + MaximumInteger(borders.t, corners.tl), 
		rect.b - MaximumInteger(borders.b, corners.bl)), borderPaint, &borderGradient);

	if (isBlurred) {
		borderGradient.dx = (GRADIENT_CACHE_COUNT << GRADIENT_COORD_BASE) / borders.r;
		borderGradient.ox = rect.r - borders.r - 1;
	}

	ThemeFillRectangle(painter, THEME_RECT_4(rect.r - borders.r, rect.r, rect.t + MaximumInteger(borders.t, corners.tr), 
		rect.b - MaximumInteger(borders.b, corners.br)), borderPaint, &borderGradient);

	if (isBlurred) {
		borderGradient.dy = -(GRADIENT_CACHE_COUNT << GRADIENT_COORD_BASE) / borders.t;
		borderGradient.oy = rect.t + borders.t;
		borderGradient.dx = borderGradient.ox = 0;
	}

	ThemeFillRectangle(painter, THEME_RECT_4(rect.l + corners.tl, rect.r - corners.tr, rect.t, rect.t + borders.t), borderPaint, &borderGradient);

	if (isBlurred) {
		borderGradient.dy = (GRADIENT_CACHE_COUNT << GRADIENT_COORD_BASE) / borders.b;
		borderGradient.oy = rect.b - borders.b - 1;
	}

	ThemeFillRectangle(painter, THEME_RECT_4(rect.l + corners.bl, rect.r - corners.br, rect.b - borders.b, rect.b), borderPaint, &borderGradient);

	ThemeFillRectangle(painter, THEME_RECT_4(rect.l + corners.tl, rect.r - corners.tr, 
		rect.t + borders.t, rect.t + MaximumInteger(MinimumInteger(corners.tl, corners.tr), borders.t)), mainPaint, &mainGradient);
	ThemeFillRectangle(painter, THEME_RECT_4(rect.l + (corners.tl > corners.tr ? corners.tl : borders.l),
		rect.r - (corners.tl > corners.tr ? borders.r : corners.tr), 
		rect.t + MaximumInteger(MinimumInteger(corners.tl, corners.tr), borders.t),
		rect.t + MaximumInteger3(corners.tl, corners.tr, borders.t)), mainPaint, &mainGradient);

	EsRectangle mainBlock = THEME_RECT_4(rect.l + borders.l, rect.r - borders.r,
		rect.t + MaximumInteger3(corners.tl, corners.tr, borders.t),
		rect.b - MaximumInteger3(corners.bl, corners.br, borders.b));

	if ((box->flags & THEME_LAYER_BOX_SHADOW_HIDING) && THEME_RECT_VALID(opaqueRegion)) {
		EsRectangle neededBorders = THEME_RECT_4(MaximumInteger(opaqueRegion.l - mainBlock.l, 0), MaximumInteger(mainBlock.r - opaqueRegion.r, 0),
			MaximumInteger(opaqueRegion.t - mainBlock.t, 0), MaximumInteger(mainBlock.b - opaqueRegion.b, 0));
		ThemeFillRectangle(painter, THEME_RECT_4(mainBlock.l, mainBlock.r, mainBlock.t, mainBlock.t + neededBorders.t), mainPaint, &mainGradient);
		ThemeFillRectangle(painter, THEME_RECT_4(mainBlock.l, mainBlock.l + neededBorders.l, 
			mainBlock.t + neededBorders.t, mainBlock.b - neededBorders.b), mainPaint, &mainGradient);
		ThemeFillRectangle(painter, THEME_RECT_4(mainBlock.r - neededBorders.r, mainBlock.r, 
			mainBlock.t + neededBorders.t, mainBlock.b - neededBorders.b), mainPaint, &mainGradient);
		ThemeFillRectangle(painter, THEME_RECT_4(mainBlock.l, mainBlock.r, mainBlock.b - neededBorders.b, mainBlock.b), mainPaint, &mainGradient);
	} else {
		ThemeFillRectangle(painter, mainBlock, mainPaint, &mainGradient);
	}

	ThemeFillRectangle(painter, THEME_RECT_4(rect.l + (corners.bl > corners.br ? corners.bl : borders.l),
		rect.r - (corners.bl > corners.br ? borders.r : corners.br), 
		rect.b - MaximumInteger3(corners.bl, corners.br, borders.b),
		rect.b - MaximumInteger(MinimumInteger(corners.bl, corners.br), borders.b)), mainPaint, &mainGradient);
	ThemeFillRectangle(painter, THEME_RECT_4(rect.l + corners.bl, rect.r - corners.br, 
		rect.b - MaximumInteger(MinimumInteger(corners.bl, corners.br), borders.b), 
		rect.b - borders.b), mainPaint, &mainGradient);

	if (cornerBorders.tl >= corners.tl) {
		ThemeFillRectangle(painter, THEME_RECT_4(rect.l, rect.l + corners.tl, rect.t + corners.tl, rect.t + cornerBorders.tl), borderPaint, &borderGradient);
	}

	if (cornerBorders.tr >= corners.tr) {
		ThemeFillRectangle(painter, THEME_RECT_4(rect.r - corners.tr, rect.r, rect.t + corners.tr, rect.t + cornerBorders.tr), borderPaint, &borderGradient);
	}

	if (cornerBorders.bl >= corners.bl) {
		ThemeFillRectangle(painter, THEME_RECT_4(rect.l, rect.l + corners.bl, rect.b - cornerBorders.bl, rect.b - corners.bl), borderPaint, &borderGradient);
	}

	if (cornerBorders.br >= corners.br) {
		ThemeFillRectangle(painter, THEME_RECT_4(rect.r - corners.br, rect.r, rect.b - cornerBorders.br, rect.b - corners.br), borderPaint, &borderGradient);
	}

	if (isBlurred) {
		ThemeFillBlurCorner(painter, THEME_RECT_4(rect.l, rect.l + corners.tl, rect.t, rect.t + corners.tl),  
			corners.tl, corners.tl, borders.l, corners.tl, &borderGradient);
		ThemeFillBlurCorner(painter, THEME_RECT_4(rect.r - corners.tr, rect.r, rect.t, rect.t + corners.tr),  
			-1, corners.tr, borders.l, corners.tr, &borderGradient);
		ThemeFillBlurCorner(painter, THEME_RECT_4(rect.l, rect.l + corners.bl, rect.b - corners.bl, rect.b),  
			corners.bl, -1, borders.l, corners.bl, &borderGradient);
		ThemeFillBlurCorner(painter, THEME_RECT_4(rect.r - corners.br, rect.r, rect.b - corners.br, rect.b),  
			-1, -1, borders.l, corners.br, &borderGradient);
	} else {
		ThemeFillCorner(painter, THEME_RECT_4(rect.l, rect.l + corners.tl, rect.t, rect.t + corners.tl),  
			corners.tl, corners.tl, cornerBorders.tl, corners.tl, mainPaint, borderPaint, &mainGradient, &borderGradient);
		ThemeFillCorner(painter, THEME_RECT_4(rect.r - corners.tr, rect.r, rect.t, rect.t + corners.tr),  
			0, corners.tr, cornerBorders.tr, corners.tr, mainPaint, borderPaint, &mainGradient, &borderGradient);
		ThemeFillCorner(painter, THEME_RECT_4(rect.l, rect.l + corners.bl, rect.b - corners.bl, rect.b),  
			corners.bl, 0, cornerBorders.bl, corners.bl, mainPaint, borderPaint, &mainGradient, &borderGradient);
		ThemeFillCorner(painter, THEME_RECT_4(rect.r - corners.br, rect.r, rect.b - corners.br, rect.b),  
			0, 0, cornerBorders.br, corners.br, mainPaint, borderPaint, &mainGradient, &borderGradient);
	}
}

void ThemeDrawPath(EsPainter *painter, EsRectangle rect, EsBuffer *data, float scale) {
	int width = THEME_RECT_WIDTH(rect), height = THEME_RECT_HEIGHT(rect);
	if (width <= 0 || height <= 0) return;

	const ThemeLayerPath *layer = (const ThemeLayerPath *) EsBufferRead(data, sizeof(ThemeLayerPath));
	if (!layer) return;
	if (!layer->pointCount) return;
	if (!layer->alpha) return;
	const float *points = (const float *) EsBufferRead(data, layer->pointCount * 6 * sizeof(float));
	if (!points) return;

	EsRectangle bounds;
	EsRectangleClip(rect, painter->clip, &bounds);

	if (!THEME_RECT_VALID(bounds)) {
		return;
	}

	RastVertex scale2 = { width / 100.0f, height / 100.0f };

	RastPath path = {};

	for (uintptr_t i = 0, j = 0; i < layer->pointCount * 3; i += 3) {
		if ((int) i == layer->pointCount * 3 - 3 || points[i * 2 + 2] == -1e6) {
			RastPathAppendBezier(&path, (RastVertex *) points + j, i - j + 1, scale2);
			RastPathCloseSegment(&path);
			j = i + 3;
		}
	}

	RastPathTranslate(&path, rect.l - painter->clip.l, rect.t - painter->clip.t);

	RastSurface surface = {};

	float xOffset = rect.l - painter->clip.l;
	float yOffset = rect.t - painter->clip.t;

	if (!RastSurfaceInitialise(&surface, THEME_RECT_WIDTH(painter->clip), THEME_RECT_HEIGHT(painter->clip), false)) {
		goto error;
	}

	for (uintptr_t i = 0; i < layer->fillCount; i++) {
		const ThemeLayerPathFill *fill = (const ThemeLayerPathFill *) EsBufferRead(data, sizeof(ThemeLayerPathFill));
		if (!fill) return;

		RastPaint paint = {};
		RastShape shape = {};

		if ((fill->paintAndFillType & 0x0F) == THEME_PAINT_SOLID) {
			const ThemePaintSolid *p = (const ThemePaintSolid *) EsBufferRead(data, sizeof(ThemePaintSolid));
			if (!p) return;
			paint.type = RAST_PAINT_SOLID;
			paint.solid.color = p->color & 0xFFFFFF;
			paint.solid.alpha = (p->color >> 24) / 255.0f;
		} else if ((fill->paintAndFillType & 0x0F) == THEME_PAINT_LINEAR_GRADIENT) {
			RastGradientStop stops[16];
			size_t stopCount = 0;

			const ThemePaintLinearGradient *p = (const ThemePaintLinearGradient *) EsBufferRead(data, sizeof(ThemePaintLinearGradient));
			if (!p) return;

			for (uintptr_t i = 0; i < p->stopCount && i < 16; i++, stopCount++) {
				const ThemeGradientStop *stop = (const ThemeGradientStop *) EsBufferRead(data, sizeof(ThemeGradientStop));
				if (!stop) return;
				stops[i].color = stop->color;
				stops[i].position = stop->position / 100.0f;
			}

			paint.type = RAST_PAINT_LINEAR_GRADIENT;
			paint.gradient.repeatMode = (RastRepeatMode) p->repeatMode;
			paint.gradient.transform[0] = p->transform[0] / THEME_RECT_WIDTH(rect);
			paint.gradient.transform[1] = p->transform[1] / THEME_RECT_HEIGHT(rect);
			paint.gradient.transform[2] = p->transform[2] - (paint.gradient.transform[0] * xOffset + paint.gradient.transform[1] * yOffset);

			RastGradientInitialise(&paint, stops, stopCount, p->useGammaInterpolation);
		} else if ((fill->paintAndFillType & 0x0F) == THEME_PAINT_RADIAL_GRADIENT) {
			RastGradientStop stops[16];
			size_t stopCount = 0;

			const ThemePaintRadialGradient *p = (const ThemePaintRadialGradient *) EsBufferRead(data, sizeof(ThemePaintRadialGradient));
			if (!p) return;

			for (uintptr_t i = 0; i < p->stopCount && i < 16; i++, stopCount++) {
				const ThemeGradientStop *stop = (const ThemeGradientStop *) EsBufferRead(data, sizeof(ThemeGradientStop));
				if (!stop) return;
				stops[i].color = stop->color;
				stops[i].position = stop->position / 100.0f;
			}

			paint.type = RAST_PAINT_RADIAL_GRADIENT;
			paint.gradient.repeatMode = (RastRepeatMode) p->repeatMode;
			paint.gradient.transform[0] = p->transform[0] / THEME_RECT_WIDTH(rect);
			paint.gradient.transform[1] = p->transform[1] / THEME_RECT_HEIGHT(rect);
			paint.gradient.transform[2] = p->transform[2] - (paint.gradient.transform[0] * xOffset + paint.gradient.transform[1] * yOffset);
			paint.gradient.transform[3] = p->transform[3] / THEME_RECT_WIDTH(rect);
			paint.gradient.transform[4] = p->transform[4] / THEME_RECT_HEIGHT(rect);
			paint.gradient.transform[5] = p->transform[5] - (paint.gradient.transform[3] * xOffset + paint.gradient.transform[4] * yOffset);

			RastGradientInitialise(&paint, stops, stopCount, p->useGammaInterpolation);
		} else {
			// TODO Checkboards, angular gradients and noise.
		}

		if ((fill->paintAndFillType & 0xF0) == THEME_PATH_FILL_SOLID) {
			shape = RastShapeCreateSolid(&path);
		} else if ((fill->paintAndFillType & 0xF0) == THEME_PATH_FILL_CONTOUR) {
			const ThemeLayerPathFillContour *contour = (const ThemeLayerPathFillContour *) EsBufferRead(data, sizeof(ThemeLayerPathFillContour));
			if (!contour) return;
			RastContourStyle style = {};
			style.internalWidth = contour->internalWidth * scale;
			style.externalWidth = contour->externalWidth * scale;
			style.joinMode = (RastLineJoinMode) ((contour->mode >> 0) & 3);
			style.capMode = (RastLineCapMode) ((contour->mode >> 2) & 3);
			style.miterLimit = contour->miterLimit * scale;

			if (contour->mode & 0x80) {
				style.internalWidth = EsCRTfloorf(style.internalWidth);
				style.externalWidth = EsCRTfloorf(style.externalWidth);
			}

			shape = RastShapeCreateContour(&path, style, ~layer->flags & THEME_LAYER_PATH_CLOSED);
		} else if ((fill->paintAndFillType & 0xF0) == THEME_PATH_FILL_DASHED) {
			RastDash dashes[16];
			RastContourStyle styles[16];
			size_t styleCount = 0;

			for (uintptr_t i = 0; i < fill->dashCount && i < 16; i++, styleCount++) {
				const ThemeLayerPathFillDash *dash = (const ThemeLayerPathFillDash *) EsBufferRead(data, sizeof(ThemeLayerPathFillDash));
				if (!dash) return;
				dashes[i].gap = dash->gap * scale;
				dashes[i].length = dash->length * scale;
				dashes[i].style = styles + i;
				styles[i].internalWidth = dash->contour.internalWidth * scale;
				styles[i].externalWidth = dash->contour.externalWidth * scale;
				styles[i].joinMode = (RastLineJoinMode) ((dash->contour.mode >> 0) & 3);
				styles[i].capMode = (RastLineCapMode) ((dash->contour.mode >> 2) & 3);
				styles[i].miterLimit = dash->contour.miterLimit * scale;

				if (dash->contour.mode & 0x80) {
					styles[i].internalWidth = EsCRTfloorf(styles[i].internalWidth);
					styles[i].externalWidth = EsCRTfloorf(styles[i].externalWidth);
				}
			}

			shape = RastShapeCreateDashed(&path, dashes, styleCount, ~layer->flags & THEME_LAYER_PATH_CLOSED);
		}

		RastSurfaceFill(surface, shape, paint, layer->flags & THEME_LAYER_PATH_FILL_EVEN_ODD);

		RastGradientDestroy(&paint);
	}

	// TODO Use drawn bounding box.

	for (int32_t j = painter->clip.t; j < painter->clip.b; j++) {
		uint32_t *source = surface.buffer + (j - painter->clip.t) * surface.stride / 4;
		uint32_t *destination = (uint32_t *) painter->target->bits + j * painter->target->stride / 4 + painter->clip.l;

		for (int32_t i = painter->clip.l; i < painter->clip.r; i++) {
			uint32_t s = *source;

			if (layer->alpha != 0xFF) {
				uint32_t alpha = s >> 24;

				if (alpha) {
					alpha *= layer->alpha;
					alpha >>= 8;
					s = (s & 0xFFFFFF) | (alpha << 24);
					BlendPixel(destination, s, painter->target->fullAlpha);
				}
			} else {
				BlendPixel(destination, s, painter->target->fullAlpha);
			}

			destination++, source++;
		}
	}

	error:;
	RastSurfaceDestroy(&surface);
	RastPathDestroy(&path);
}

void ThemeDrawLayer(EsPainter *painter, EsRectangle _bounds, EsBuffer *data, float scale, EsRectangle opaqueRegion) {
	const ThemeLayer *layer = (const ThemeLayer *) EsBufferRead(data, sizeof(ThemeLayer));

	if (!layer) {
		return;
	}

	EsRectangle bounds;
	bounds.l = _bounds.l + (int) (scale * layer->offset.l) + THEME_RECT_WIDTH(_bounds)  * layer->position.l / 100;
	bounds.r = _bounds.l + (int) (scale * layer->offset.r) + THEME_RECT_WIDTH(_bounds)  * layer->position.r / 100;
	bounds.t = _bounds.t + (int) (scale * layer->offset.t) + THEME_RECT_HEIGHT(_bounds) * layer->position.t / 100;
	bounds.b = _bounds.t + (int) (scale * layer->offset.b) + THEME_RECT_HEIGHT(_bounds) * layer->position.b / 100;

	if (THEME_RECT_WIDTH(bounds) <= 0 || THEME_RECT_HEIGHT(bounds) <= 0) {
		if (layer->dataByteCount) {
			EsBufferRead(data, layer->dataByteCount - sizeof(ThemeLayer));
		}
	} else {
		if (layer->type == THEME_LAYER_BOX) {
			ThemeDrawBox(painter, bounds, data, scale, layer, opaqueRegion, 0);
		} else if (layer->type == THEME_LAYER_PATH) {
			ThemeDrawPath(painter, bounds, data, scale);
		} else if (layer->type == THEME_LAYER_METRICS) {
			EsBufferRead(data, sizeof(ThemeMetrics));
		}
	}
}

//////////////////////////////////////////

#ifndef IN_DESIGNER

typedef struct ThemeAnimatingProperty {
	uint16_t offset; // Offset into the theme data.
	uint8_t type : 4, beforeEnter : 1, _unused1 : 3; // Interpolation type.
	uint8_t _unused0;
	uint16_t duration, elapsed; // Milliseconds.
	ThemeVariant from; // Value to interpolate from.
} ThemeAnimatingProperty;

typedef struct ThemeAnimation {
	Array<ThemeAnimatingProperty> properties;
} ThemeAnimation;

struct UIStyle {
	intptr_t referenceCount;

	// General information.

	uint8_t textAlign;
	EsFont font;
	EsRectangle insets, borders;
	uint16_t preferredWidth, preferredHeight;
	int16_t gapMajor, gapMinor, gapWrap;
	uint32_t observedStyleStateMask;
	EsRectangle paintOutsets, opaqueInsets;
	float scale;
	EsThemeAppearance *appearance; // An optional, custom appearance provided by the application.

	// Data.

	uint16_t layerDataByteCount;
	const ThemeStyle *style;
	ThemeMetrics *metrics; // Points to correct position in layer data.
	// Followed by overrides, then layer data.
	// The overrides store the base value, and the layer data contains the overriden values.

	// Loaded styles management.

	void CloseReference();

	// Painting.

	void PaintText(EsPainter *painter, EsElement *element, EsRectangle rectangle, const char *text, size_t textBytes, 
			uint32_t iconID, uint32_t flags, EsTextSelection *selectionProperties = nullptr);
	void PaintLayers(EsPainter *painter, EsRectangle rectangle, int childType, int whichLayers);
	void PaintTextLayers(EsPainter *painter, EsTextPlan *plan, EsRectangle textBounds, EsTextSelection *selectionProperties);

	// Misc.

	bool IsRegionCompletelyOpaque(EsRectangle region, int width, int height);
	bool IsStateChangeObserved(uint16_t state1, uint16_t state2);
	inline void GetTextStyle(EsTextStyle *style);
};

void ThemeInitialise() {
	if (theming.initialised) return;
	theming.initialised = true;

	EsBuffer data = {};
	data.in = (const uint8_t *) EsBundleFind(&bundleDesktop, EsLiteral("Theme.dat"), &data.bytes);

	const ThemeHeader *header = (const ThemeHeader *) EsBufferRead(&data, sizeof(ThemeHeader));
	EsAssert(header && header->signature == THEME_HEADER_SIGNATURE && header->styleCount && EsBufferRead(&data, sizeof(ThemeStyle)));
	theming.system.in = (const uint8_t *) data.in;
	theming.system.bytes = data.bytes;
	theming.header = header;

	theming.scale = api.global->uiScale;

	if (!theming.cursorData) {
		size_t cursorsBitmapBytes;
		const void *cursorsBitmap = EsBundleFind(&bundleDesktop, EsLiteral("Cursors.png"), &cursorsBitmapBytes);
		theming.cursorData = EsMemoryCreateShareableRegion(ES_THEME_CURSORS_WIDTH * ES_THEME_CURSORS_HEIGHT * 4);
		void *destination = EsMemoryMapObject(theming.cursorData, 0, ES_THEME_CURSORS_WIDTH * ES_THEME_CURSORS_HEIGHT * 4, ES_MEMORY_MAP_OBJECT_READ_WRITE);
		LoadImage(cursorsBitmap, cursorsBitmapBytes, destination, ES_THEME_CURSORS_WIDTH, ES_THEME_CURSORS_HEIGHT, true);
		EsObjectUnmap(destination);
	}

	theming.cursors.width = ES_THEME_CURSORS_WIDTH;
	theming.cursors.height = ES_THEME_CURSORS_HEIGHT;
	theming.cursors.stride = ES_THEME_CURSORS_WIDTH * 4;
	theming.cursors.bits = EsMemoryMapObject(theming.cursorData, 0, ES_MEMORY_MAP_OBJECT_ALL, ES_MEMORY_MAP_OBJECT_READ_ONLY);
	theming.cursors.fullAlpha = true;
	theming.cursors.readOnly = true;
}

const void *GetConstant(const char *cKey, size_t *byteCount, bool *scale) {
	ThemeInitialise();

	EsBuffer data = theming.system;
	const ThemeHeader *header = (const ThemeHeader *) EsBufferRead(&data, sizeof(ThemeHeader));
	EsBufferRead(&data, sizeof(ThemeStyle) * header->styleCount);

	uint64_t hash = CalculateCRC64(EsLiteral(cKey), 0);

	for (uintptr_t i = 0; i < header->constantCount; i++) {
		const ThemeConstant *constant = (const ThemeConstant *) EsBufferRead(&data, sizeof(ThemeConstant));

		if (!constant) {
			EsPrint("Broken theme constants list.\n");
			break;
		}

		if (constant->hash == hash) {
			size_t _byteCount = 0;

			for (uintptr_t i = 0; i < sizeof(constant->cValue); i++) {
				if (constant->cValue[i] == 0) {
					break;
				} else {
					_byteCount++;
				}
			}

			*byteCount = _byteCount;
			*scale = constant->scale;
			return constant->cValue;
		}
	}

	EsPrint("Could not find theme constant with key \"%z\".\n", cKey);
	return nullptr;
}

int GetConstantNumber(const char *cKey) {
	size_t byteCount;
	bool scale = false;
	const void *value = GetConstant(cKey, &byteCount, &scale);
	int integer = value ? EsIntegerParse((const char *) value, byteCount) : 0; 
	if (scale) integer *= theming.scale;
	return integer;
}

const char *GetConstantString(const char *cKey) {
	size_t byteCount;
	bool scale;
	const char *value = (const char *) GetConstant(cKey, &byteCount, &scale);
	return !value || !byteCount || value[byteCount - 1] ? nullptr : value; 
}

void ThemeStyleCopyInlineMetrics(UIStyle *style) {
	style->font.family = style->metrics->fontFamily;
	style->font.weight = style->metrics->fontWeight;
	style->font.italic = style->metrics->isItalic;
	style->preferredWidth = style->metrics->preferredWidth;
	style->preferredHeight = style->metrics->preferredHeight;
	style->gapMajor = style->metrics->gapMajor;
	style->gapMinor = style->metrics->gapMinor;
	style->gapWrap = style->metrics->gapWrap;
	style->insets.l = style->metrics->insets.l;
	style->insets.r = style->metrics->insets.r;
	style->insets.t = style->metrics->insets.t;
	style->insets.b = style->metrics->insets.b;
	style->textAlign = style->metrics->textAlign;
}

bool ThemeAnimationComplete(ThemeAnimation *animation) {
	return !animation->properties.Length();
}

bool ThemeAnimationStep(ThemeAnimation *animation, int delta) {
	bool repaint = false;

	for (uintptr_t i = 0; i < animation->properties.Length(); i++) {
		ThemeAnimatingProperty *property = &animation->properties[i];

		repaint = true;
		property->elapsed += delta;

		if (property->duration < property->elapsed) {
			animation->properties.Delete(i);
			i--; 
		}
	}

	return repaint;
}

void ThemeAnimationDestroy(ThemeAnimation *animation) {
	animation->properties.Free();
}

ThemeVariant ThemeAnimatingPropertyInterpolate(ThemeAnimatingProperty *property, UIStyle *destination, uint8_t *layerData) {
	uint32_t dataOffset = property->offset;
	float position = (float) property->elapsed / property->duration;
	position = SmoothAnimationTime(position);

	if (property->type == THEME_OVERRIDE_I8) {
		EsAssert(dataOffset <= destination->layerDataByteCount - sizeof(uint8_t));
		int8_t to = *(int8_t *) (layerData + dataOffset);
		return (ThemeVariant) { .i8 = (int8_t ) LinearInterpolate(property->from.i8, to, position) };
	} else if (property->type == THEME_OVERRIDE_I16) {
		EsAssert(dataOffset <= destination->layerDataByteCount - sizeof(uint16_t));
		int16_t to = *(int16_t *) (layerData + dataOffset);
		return (ThemeVariant) { .i16 = (int16_t) LinearInterpolate(property->from.i16, to, position) };
	} else if (property->type == THEME_OVERRIDE_F32) {
		EsAssert(dataOffset <= destination->layerDataByteCount - sizeof(float));
		float to = *(float *) (layerData + dataOffset);
		return (ThemeVariant) { .f32 = (float) LinearInterpolate(property->from.f32, to, position) };
	} else if (property->type == THEME_OVERRIDE_COLOR) {
		EsAssert(dataOffset <= destination->layerDataByteCount - sizeof(uint32_t));
		uint32_t to = *(uint32_t *) (layerData + dataOffset);
		return (ThemeVariant) { .u32 = EsColorInterpolate(property->from.u32, to, position) };
	} else {
		EsAssert(false);
		return {};
	}
}

UIStyle *ThemeStyleInterpolate(UIStyle *source, ThemeAnimation *animation) {
	size_t byteCount = sizeof(UIStyle) + source->layerDataByteCount;
	UIStyle *destination = (UIStyle *) EsHeapAllocate(byteCount, false);
	EsMemoryCopy(destination, source, byteCount);
	uint8_t *layerData = (uint8_t *) (destination + 1);
	destination->metrics = (ThemeMetrics *) (layerData + sizeof(ThemeLayer));

	for (uintptr_t i = 0; i < animation->properties.Length(); i++) {
		ThemeAnimatingProperty *property = &animation->properties[i];
		ThemeVariant result = ThemeAnimatingPropertyInterpolate(property, destination, layerData);

		if (property->type == THEME_OVERRIDE_I8) {
			*(int8_t *) (layerData + property->offset) = result.i8;
		} else if (property->type == THEME_OVERRIDE_I16) {
			*(int16_t *) (layerData + property->offset) = result.i16;
		} else if (property->type == THEME_OVERRIDE_F32) {
			*(float *) (layerData + property->offset) = result.f32;
		} else if (property->type == THEME_OVERRIDE_COLOR) {
			*(uint32_t *) (layerData + property->offset) = result.u32;
		}
	}

	ThemeStyleCopyInlineMetrics(destination);
	return destination;
}

void _ThemeAnimationBuildAddProperties(ThemeAnimation *animation, UIStyle *style, uint16_t stateFlags) {
	const uint32_t *layerList = (const uint32_t *) (theming.system.in + style->style->layerListOffset); 
	// (Layer list was validated during ThemeStyleInitialise.)
	uintptr_t layerCumulativeDataOffset = 0;
	uint8_t *oldLayerData = (uint8_t *) (style + 1);

	for (uintptr_t i = 0; i < style->style->layerCount; i++) {
		const ThemeLayer *layer = (const ThemeLayer *) (theming.system.in + layerList[i]);

		EsBuffer layerData = theming.system;
		layerData.position = layer->overrideListOffset;

		for (uintptr_t i = 0; i < layer->overrideCount; i++) {
			const ThemeOverride *themeOverride = (const ThemeOverride *) EsBufferRead(&layerData, sizeof(ThemeOverride));

			if (!THEME_STATE_CHECK(themeOverride->state, stateFlags) || !themeOverride->duration) {
				continue;
			}

			uintptr_t key = themeOverride->offset + layerCumulativeDataOffset;

			if (themeOverride->type == THEME_OVERRIDE_I8)    EsAssert(key <= (uintptr_t) style->layerDataByteCount - sizeof(uint8_t));
			if (themeOverride->type == THEME_OVERRIDE_I16)   EsAssert(key <= (uintptr_t) style->layerDataByteCount - sizeof(uint16_t));
			if (themeOverride->type == THEME_OVERRIDE_F32)   EsAssert(key <= (uintptr_t) style->layerDataByteCount - sizeof(float));
			if (themeOverride->type == THEME_OVERRIDE_COLOR) EsAssert(key <= (uintptr_t) style->layerDataByteCount - sizeof(uint32_t));

			uintptr_t point;
			bool alreadyInList;

			// Find where the property is/should be in the animation list.

			ES_MACRO_SEARCH(animation->properties.Length(), { 
				uintptr_t item = animation->properties[index].offset;
				result = key < item ? -1 : key > item ? 1 : 0;
			}, point, alreadyInList);

			bool beforeEnter = (themeOverride->state & THEME_STATE_BEFORE_ENTER) != 0;

			if (alreadyInList) {
				// Update the duration, if the property is already in the list.
				// Prioritise before enter sequence durations.

				if (!animation->properties[point].beforeEnter || beforeEnter) {
					animation->properties[point].duration = themeOverride->duration * api.global->animationTimeMultiplier;
					animation->properties[point].beforeEnter = beforeEnter;
				}
			} else {
				// Add the property to the list.

				if (point < animation->properties.Length()) EsAssert(key < animation->properties[point].offset);
				if (point > 0) EsAssert(key > animation->properties[point - 1].offset);

				ThemeAnimatingProperty property = {};
				property.offset = key;
				property.type = themeOverride->type;
				property.duration = themeOverride->duration * api.global->animationTimeMultiplier;
				property.beforeEnter = beforeEnter;

				if (themeOverride->type == THEME_OVERRIDE_I8) {
					EsAssert(themeOverride->offset <= (uintptr_t) layer->dataByteCount - 1);
					property.from.i8 = *(int8_t *) (oldLayerData + key);
				} else if (themeOverride->type == THEME_OVERRIDE_I16) {
					EsAssert(themeOverride->offset <= (uintptr_t) layer->dataByteCount - 2);
					property.from.i16 = *(int16_t *) (oldLayerData + key);
				} else if (themeOverride->type == THEME_OVERRIDE_F32) {
					EsAssert(themeOverride->offset <= (uintptr_t) layer->dataByteCount - 4);
					property.from.f32 = *(float *) (oldLayerData + key);
				} else if (themeOverride->type == THEME_OVERRIDE_COLOR) {
					EsAssert(themeOverride->offset <= (uintptr_t) layer->dataByteCount - 4);
					property.from.u32 = *(uint32_t *) (oldLayerData + key);
				}

				animation->properties.Insert(property, point);
			}
		}

		layerCumulativeDataOffset += layer->dataByteCount;
	}
}

void ThemeAnimationBuild(ThemeAnimation *animation, UIStyle *oldStyle, uint16_t oldStateFlags, uint16_t newStateFlags) {
	// Interpolate all the animating properties using the old style as the target.

	uint8_t *oldLayerData = (uint8_t *) (oldStyle + 1);

	for (uintptr_t i = 0; i < animation->properties.Length(); i++) {
		ThemeAnimatingProperty *property = &animation->properties[i];
		property->from = ThemeAnimatingPropertyInterpolate(property, oldStyle, oldLayerData);
		property->elapsed = 0;
	}

	// Look for the all the sequences that match the old state flags,
	// and add properties to return values to base.

	_ThemeAnimationBuildAddProperties(animation, oldStyle, oldStateFlags);

	// Look for the all the sequences that match the new state flags,
	// and add properties to interpolate them to the target sequencs.

	_ThemeAnimationBuildAddProperties(animation, oldStyle, newStateFlags);
}

void ThemeStylePrepare(UIStyle *style, UIStyleKey key) {
	EsStyle *esStyle = (key.part & 1) || (!key.part) ? nullptr : (EsStyle *) (key.part);
	EsThemeMetrics *customMetrics = esStyle ? &esStyle->metrics : nullptr;
	const ThemeStyle *themeStyle = style->style;

	// Apply custom metrics and appearance.

	if (customMetrics) {
#define ES_RECTANGLE_TO_RECTANGLE_8(x) { (int8_t) (x).l, (int8_t) (x).r, (int8_t) (x).t, (int8_t) (x).b }
		if (customMetrics->mask & ES_THEME_METRICS_INSETS) style->metrics->insets = ES_RECTANGLE_TO_RECTANGLE_8(customMetrics->insets);
		if (customMetrics->mask & ES_THEME_METRICS_CLIP_INSETS) style->metrics->clipInsets = ES_RECTANGLE_TO_RECTANGLE_8(customMetrics->clipInsets);
		if (customMetrics->mask & ES_THEME_METRICS_CLIP_ENABLED) style->metrics->clipEnabled = customMetrics->clipEnabled;
		if (customMetrics->mask & ES_THEME_METRICS_CURSOR) style->metrics->cursor = customMetrics->cursor;
		if (customMetrics->mask & ES_THEME_METRICS_PREFERRED_WIDTH) style->metrics->preferredWidth = customMetrics->preferredWidth;
		if (customMetrics->mask & ES_THEME_METRICS_PREFERRED_HEIGHT) style->metrics->preferredHeight = customMetrics->preferredHeight;
		if (customMetrics->mask & ES_THEME_METRICS_MINIMUM_WIDTH) style->metrics->minimumWidth = customMetrics->minimumWidth;
		if (customMetrics->mask & ES_THEME_METRICS_MINIMUM_HEIGHT) style->metrics->minimumHeight = customMetrics->minimumHeight;
		if (customMetrics->mask & ES_THEME_METRICS_MAXIMUM_WIDTH) style->metrics->maximumWidth = customMetrics->maximumWidth;
		if (customMetrics->mask & ES_THEME_METRICS_MAXIMUM_HEIGHT) style->metrics->maximumHeight = customMetrics->maximumHeight;
		if (customMetrics->mask & ES_THEME_METRICS_GAP_MAJOR) style->metrics->gapMajor = customMetrics->gapMajor;
		if (customMetrics->mask & ES_THEME_METRICS_GAP_MINOR) style->metrics->gapMinor = customMetrics->gapMinor;
		if (customMetrics->mask & ES_THEME_METRICS_GAP_WRAP) style->metrics->gapWrap = customMetrics->gapWrap;
		if (customMetrics->mask & ES_THEME_METRICS_TEXT_COLOR) style->metrics->textColor = customMetrics->textColor;
		if (customMetrics->mask & ES_THEME_METRICS_TEXT_FIGURES) style->metrics->textFigures = customMetrics->textFigures;
		if (customMetrics->mask & ES_THEME_METRICS_SELECTED_BACKGROUND) style->metrics->selectedBackground = customMetrics->selectedBackground;
		if (customMetrics->mask & ES_THEME_METRICS_SELECTED_TEXT) style->metrics->selectedText = customMetrics->selectedText;
		if (customMetrics->mask & ES_THEME_METRICS_ICON_COLOR) style->metrics->iconColor = customMetrics->iconColor;
		if (customMetrics->mask & ES_THEME_METRICS_TEXT_ALIGN) style->metrics->textAlign = customMetrics->textAlign;
		if (customMetrics->mask & ES_THEME_METRICS_TEXT_SIZE) style->metrics->textSize = customMetrics->textSize;
		if (customMetrics->mask & ES_THEME_METRICS_FONT_FAMILY) style->metrics->fontFamily = customMetrics->fontFamily;
		if (customMetrics->mask & ES_THEME_METRICS_FONT_WEIGHT) style->metrics->fontWeight = customMetrics->fontWeight;
		if (customMetrics->mask & ES_THEME_METRICS_ICON_SIZE) style->metrics->iconSize = customMetrics->iconSize;
		if (customMetrics->mask & ES_THEME_METRICS_IS_ITALIC) style->metrics->isItalic = customMetrics->isItalic;
		if (customMetrics->mask & ES_THEME_METRICS_LAYOUT_VERTICAL) style->metrics->layoutVertical = customMetrics->layoutVertical;
	}

	if (esStyle && esStyle->appearance.enabled) {
		style->appearance = &esStyle->appearance;
	}

	// Apply scaling to the metrics.

	int16_t *scale16[] = {
		&style->metrics->insets.l, &style->metrics->insets.r, &style->metrics->insets.t, &style->metrics->insets.b,
		&style->metrics->clipInsets.l, &style->metrics->clipInsets.r, &style->metrics->clipInsets.t, &style->metrics->clipInsets.b,
		&style->metrics->gapMajor, &style->metrics->gapMinor, &style->metrics->gapWrap,
		&style->metrics->preferredWidth, &style->metrics->preferredHeight,
		&style->metrics->minimumWidth, &style->metrics->minimumHeight,
		&style->metrics->maximumWidth, &style->metrics->maximumHeight,
		&style->metrics->iconSize,
	};

	for (uintptr_t i = 0; i < sizeof(scale16) / sizeof(scale16[0]); i++) {
		*(scale16[i]) = *(scale16[i]) * key.scale;
	}

	style->scale = key.scale;

	// Copy inline metrics.

	style->borders.l = themeStyle->approximateBorders.l * key.scale;
	style->borders.r = themeStyle->approximateBorders.r * key.scale;
	style->borders.t = themeStyle->approximateBorders.t * key.scale;
	style->borders.b = themeStyle->approximateBorders.b * key.scale;

	style->paintOutsets.l = EsCRTceilf(themeStyle->paintOutsets.l * key.scale);
	style->paintOutsets.r = EsCRTceilf(themeStyle->paintOutsets.r * key.scale);
	style->paintOutsets.t = EsCRTceilf(themeStyle->paintOutsets.t * key.scale);
	style->paintOutsets.b = EsCRTceilf(themeStyle->paintOutsets.b * key.scale);

	if (style->opaqueInsets.l != 0x7F) {
		style->opaqueInsets.l = themeStyle->opaqueInsets.l * key.scale;
		style->opaqueInsets.r = themeStyle->opaqueInsets.r * key.scale;
		style->opaqueInsets.t = themeStyle->opaqueInsets.t * key.scale;
		style->opaqueInsets.b = themeStyle->opaqueInsets.b * key.scale;
	}

	if (style->appearance) {
		if ((style->appearance->backgroundColor & 0xFF000000) == 0xFF000000) {
			style->opaqueInsets = ES_RECT_1(0);
		} else {
			style->opaqueInsets = ES_RECT_1(0x7F);
		}
	}

	ThemeStyleCopyInlineMetrics(style);
}

UIStyle *ThemeStyleInitialise(UIStyleKey key) {
	ThemeInitialise();

	// Find the ThemeStyle entry.

	EsStyle *esStyle = (key.part & 1) || (!key.part) ? nullptr : (EsStyle *) (key.part);
	uint16_t id = esStyle ? (uint16_t) (uintptr_t) esStyle->inherit : key.part;
	if (!id) id = 1;

	EsBuffer data = theming.system;
	const ThemeHeader *header = (const ThemeHeader *) EsBufferRead(&data, sizeof(ThemeHeader));
	const ThemeStyle *themeStyle = nullptr;
	bool found = false;

	for (uintptr_t i = 0; i < header->styleCount; i++) {
		themeStyle = (const ThemeStyle *) EsBufferRead(&data, sizeof(ThemeStyle));

		if (!themeStyle) {
			EsPrint("Broken theme styles list.\n");
			break;
		}

		if (themeStyle->id == id) {
			found = true;
			break;
		}
	}

	if (!found) {
		EsPrint("Could not find theme style with ID %d.\n", id);
		data.position = sizeof(ThemeHeader);
		themeStyle = (const ThemeStyle *) EsBufferRead(&data, sizeof(ThemeStyle));
	}

	if (!themeStyle->layerCount) {
		EsPrint("Style has no layers (must have a metrics layer).\n");
		return nullptr;
	}

	// Get information about the layers.

	size_t layerDataByteCount = 0;
	data.position = themeStyle->layerListOffset;

	for (uintptr_t i = 0; i < themeStyle->layerCount; i++) {
		const uint32_t *offset = (const uint32_t *) EsBufferRead(&data, sizeof(uint32_t));

		if (!offset) {
			EsPrint("Broken style layer list.\n");
			return nullptr;
		}

		EsBuffer layerData = data;
		layerData.position = *offset;
		const ThemeLayer *layer = (const ThemeLayer *) EsBufferRead(&layerData, sizeof(ThemeLayer));

		if (!layer) {
			EsPrint("Broken style layer list.\n");
			return nullptr;
		}

		if (layer->dataByteCount < sizeof(ThemeLayer)) {
			EsPrint("Broken layer data byte count (%d; %d).\n", layer->dataByteCount, *offset);
			return nullptr;
		}

		layerDataByteCount += layer->dataByteCount;

		if (i == 0) {
			if (layer->type != THEME_LAYER_METRICS) {
				EsPrint("Style does not have metrics layer.\n");
				return nullptr;
			}

			if (layer->dataByteCount != sizeof(ThemeMetrics) + sizeof(ThemeLayer)) {
				EsPrint("Broken metrics layer.\n");
				return nullptr;
			}

			const ThemeMetrics *metrics = (const ThemeMetrics *) EsBufferRead(&layerData, sizeof(ThemeMetrics));

			if (!metrics) {
				EsPrint("Broken metrics layer.\n");
				return nullptr;
			}
		} else {
			const uint8_t *data = (const uint8_t *) EsBufferRead(&layerData, layer->dataByteCount);

			if (!data) {
				EsPrint("Broken layer.\n");
				return nullptr;
			}
		}

		layerData.position = layer->overrideListOffset;

		for (uintptr_t i = 0; i < layer->overrideCount; i++) {
			const ThemeOverride *themeOverride = (const ThemeOverride *) EsBufferRead(&layerData, sizeof(ThemeOverride));

			if (!themeOverride) {
				EsPrint("Broken override list.\n");
				return nullptr;
			}

			if (!THEME_STATE_CHECK(themeOverride->state, key.stateFlags)) {
				continue;
			}

			if (themeOverride->offset >= layer->dataByteCount) {
				EsPrint("Broken override list.\n");
				return nullptr;
			}

			bool valid;

			if (themeOverride->type == THEME_OVERRIDE_I8) {
				valid = themeOverride->offset + 1 <= layer->dataByteCount;
			} else if (themeOverride->type == THEME_OVERRIDE_I16) {
				valid = themeOverride->offset + 2 <= layer->dataByteCount;
			} else if (themeOverride->type == THEME_OVERRIDE_F32) {
				valid = themeOverride->offset + 4 <= layer->dataByteCount;
			} else if (themeOverride->type == THEME_OVERRIDE_COLOR) {
				valid = themeOverride->offset + 4 <= layer->dataByteCount;
			} else {
				EsPrint("Unsupported override type.\n");
				return nullptr;
			}

			if (!valid) {
				EsPrint("Broken override list.\n");
				return nullptr;
			}
		}
	}

	if (layerDataByteCount > 0xFFFF) {
		EsPrint("Layer data too large.\n");
		return nullptr;
	}

	// Allocate the style.

	UIStyle *style = (UIStyle *) EsHeapAllocate(sizeof(UIStyle) + layerDataByteCount, true);

	style->referenceCount = 1;
	style->layerDataByteCount = layerDataByteCount;
	style->style = themeStyle;

	layerDataByteCount = 0;
	data.position = themeStyle->layerListOffset;
	uint8_t *baseData = (uint8_t *) (style + 1);
	style->metrics = (ThemeMetrics *) (baseData + sizeof(ThemeLayer));

	// Copy overrides and base layer data into the style, and apply overrides.

	for (uintptr_t i = 0; i < themeStyle->layerCount; i++) {
		const uint32_t *offset = (const uint32_t *) EsBufferRead(&data, sizeof(uint32_t));
		EsBuffer layerData = data;
		layerData.position = *offset;
		const ThemeLayer *layer = (const ThemeLayer *) EsBufferRead(&layerData, sizeof(ThemeLayer));
		layerData.position = *offset;
		const uint8_t *data = (const uint8_t *) EsBufferRead(&layerData, layer->dataByteCount);
		EsMemoryCopy(baseData + layerDataByteCount, data, layer->dataByteCount);
		layerData.position = layer->overrideListOffset;

		for (uintptr_t i = 0; i < layer->overrideCount; i++) {
			const ThemeOverride *themeOverride = (const ThemeOverride *) EsBufferRead(&layerData, sizeof(ThemeOverride));

			style->observedStyleStateMask |= 0x10000 << (themeOverride->state & THEME_PRIMARY_STATE_MASK);
			style->observedStyleStateMask |= themeOverride->state & ~THEME_PRIMARY_STATE_MASK;

			if (!THEME_STATE_CHECK(themeOverride->state, key.stateFlags)) {
				continue;
			}

			ThemeVariant overrideValue = themeOverride->data;

			if (themeOverride->type == THEME_OVERRIDE_I8) {
				*(int8_t *) (baseData + layerDataByteCount + themeOverride->offset) = overrideValue.i8;
			} else if (themeOverride->type == THEME_OVERRIDE_I16) {
				*(int16_t *) (baseData + layerDataByteCount + themeOverride->offset) = overrideValue.i16;
			} else if (themeOverride->type == THEME_OVERRIDE_F32) {
				*(float *) (baseData + layerDataByteCount + themeOverride->offset) = overrideValue.f32;
			} else if (themeOverride->type == THEME_OVERRIDE_COLOR) {
				*(uint32_t *) (baseData + layerDataByteCount + themeOverride->offset) = overrideValue.u32;
			} else {
				EsAssert(false);
			}
		}

		layerDataByteCount += layer->dataByteCount;
	}

	ThemeStylePrepare(style, key);
	return style;
}

UIStyleKey MakeStyleKey(const EsStyle *style, uint16_t stateFlags) {
	return { .part = (uintptr_t) style, .scale = theming.scale, .stateFlags = stateFlags };
}

void FreeUnusedStyles(bool includePermanentStyles) {
	for (uintptr_t i = 0; i < theming.loadedStyles.Count(); i++) {
		UIStyle *style = theming.loadedStyles[i];

		if (style->referenceCount == 0 || (style->referenceCount == -1 && includePermanentStyles)) {
			UIStyleKey key = theming.loadedStyles.KeyAtIndex(i);
			theming.loadedStyles.Delete(&key);
			EsHeapFree(style);
			i--;
		}
	}
}

UIStyle *GetStyle(UIStyleKey key, bool keepAround) {
	UIStyle **style = theming.loadedStyles.Get(&key);

	if (!style) {
		style = theming.loadedStyles.Put(&key);
		*style = ThemeStyleInitialise(key);
		EsAssert(style);
	} else if ((*style)->referenceCount != -1) {
		(*style)->referenceCount++;
	}

	if (keepAround || !key.part) {
		(*style)->referenceCount = -1;
	}

	return *style;
}

void GetPreferredSizeFromStylePart(const EsStyle *esStyle, int64_t *width, int64_t *height) {
	UIStyle *style = GetStyle(MakeStyleKey(esStyle, ES_FLAGS_DEFAULT), true);
	if (width) *width = style->preferredWidth;
	if (height) *height = style->preferredHeight;
	style->CloseReference();
}

void UIStyle::CloseReference() {
	if (referenceCount == -1) {
		return;
	}

	referenceCount--;
}

void UIStyle::PaintTextLayers(EsPainter *painter, EsTextPlan *plan, EsRectangle textBounds, EsTextSelection *selectionProperties) {
	EsBuffer data = {};
	data.in = (uint8_t *) (this + 1);
	data.bytes = layerDataByteCount;

	EsTextStyle primaryStyle = TextPlanGetPrimaryStyle(plan);
	bool restore = false;

	for (uintptr_t i = 0; i < style->layerCount; i++) {
		const ThemeLayer *layer = (const ThemeLayer *) EsBufferRead(&data, sizeof(ThemeLayer));
		if (!layer) break;

		if (layer->mode == THEME_LAYER_MODE_CONTENT && layer->type == THEME_LAYER_TEXT) {
			EsBuffer data2 = data;
			const ThemeLayerText *textLayer = (const ThemeLayerText *) EsBufferRead(&data2, sizeof(ThemeLayerText));
			if (!textLayer) break;

			EsTextStyle textStyle = primaryStyle;
			textStyle.color = textLayer->color;
			textStyle.blur = textLayer->blur;
			EsTextPlanReplaceStyleRenderProperties(plan, &textStyle);
			restore = true;

			EsDrawText(painter, plan, Translate(textBounds, layer->offset.l, layer->offset.t));
		}

		EsBufferRead(&data, layer->dataByteCount - sizeof(ThemeLayer));
	}

	if (restore) EsTextPlanReplaceStyleRenderProperties(plan, &primaryStyle);
	EsDrawText(painter, plan, textBounds, nullptr, selectionProperties);
}

void UIStyle::PaintText(EsPainter *painter, EsElement *element, EsRectangle rectangle, 
		const char *text, size_t textBytes, uint32_t iconID, uint32_t flags, EsTextSelection *selectionProperties) {
	EsRectangle bounds = Translate(EsRectangleAddBorder(rectangle, insets), painter->offsetX, painter->offsetY);
	EsRectangle textBounds = bounds;
	EsRectangle oldClip = painter->clip;
	EsRectangleClip(painter->clip, Translate(rectangle, painter->offsetX, painter->offsetY), &painter->clip);

	EsRectangle iconBounds = EsRectangleSplit(&textBounds, metrics->iconSize, metrics->layoutVertical ? 't' : 'l', gapMinor);
	EsPainter iconPainter = *painter;
	iconPainter.width = Width(iconBounds), iconPainter.height = Height(iconBounds);
	iconPainter.offsetX = iconBounds.l, iconPainter.offsetY = iconBounds.t;
	EsMessage m = { ES_MSG_PAINT_ICON };
	m.painter = &iconPainter;
	
	if (ES_HANDLED == EsMessageSend(element, &m)) {
		// Icon painted by the application.
	} else if (iconID) {
		EsDrawStandardIcon(painter, iconID, metrics->iconSize, iconBounds, metrics->iconColor);
	} else {
		// Restore the previous bounds.
		textBounds = bounds;
	}

	if (flags & (ES_DRAW_CONTENT_MARKER_DOWN_ARROW | ES_DRAW_CONTENT_MARKER_UP_ARROW)) {
		EsStyle *part = (flags & ES_DRAW_CONTENT_MARKER_DOWN_ARROW) ? ES_STYLE_MARKER_DOWN_ARROW : ES_STYLE_MARKER_UP_ARROW;
		UIStyle *style = GetStyle(MakeStyleKey(part, 0), true);
		textBounds.r -= style->preferredWidth + gapMinor;
		EsRectangle location = ES_RECT_4PD(bounds.r - style->preferredWidth - painter->offsetX, 
				bounds.t + Height(bounds) / 2 - style->preferredHeight / 2 - painter->offsetY, 
				style->preferredWidth, style->preferredHeight);
		style->PaintLayers(painter, location, THEME_CHILD_TYPE_ONLY, THEME_LAYER_MODE_BACKGROUND);
	}

	if (textBytes == (size_t) -1) {
		textBytes = EsCStringLength(text);
	}

	if (selectionProperties) {
		selectionProperties->foreground = metrics->selectedText;
		selectionProperties->background = metrics->selectedBackground;
	}

	if (textBytes) {
		EsTextPlanProperties properties = {};
		properties.flags = textAlign;

		if (flags & ES_TEXT_H_LEFT)   properties.flags = (properties.flags & ~(ES_TEXT_H_CENTER | ES_TEXT_H_RIGHT)) | ES_TEXT_H_LEFT;
		if (flags & ES_TEXT_H_CENTER) properties.flags = (properties.flags & ~(ES_TEXT_H_LEFT | ES_TEXT_H_RIGHT)) | ES_TEXT_H_CENTER;
		if (flags & ES_TEXT_H_RIGHT)  properties.flags = (properties.flags & ~(ES_TEXT_H_LEFT | ES_TEXT_H_CENTER)) | ES_TEXT_H_RIGHT;
		if (flags & ES_TEXT_V_TOP)    properties.flags = (properties.flags & ~(ES_TEXT_V_CENTER | ES_TEXT_V_BOTTOM)) | ES_TEXT_V_TOP;
		if (flags & ES_TEXT_V_CENTER) properties.flags = (properties.flags & ~(ES_TEXT_V_TOP | ES_TEXT_V_BOTTOM)) | ES_TEXT_V_CENTER;
		if (flags & ES_TEXT_V_BOTTOM) properties.flags = (properties.flags & ~(ES_TEXT_V_TOP | ES_TEXT_V_CENTER)) | ES_TEXT_V_BOTTOM;

		EsTextRun textRun[2] = {};
		textRun[1].offset = textBytes;
		GetTextStyle(&textRun[0].style);

		if (flags & ES_DRAW_CONTENT_TABULAR) {
			textRun[0].style.figures = ES_TEXT_FIGURE_TABULAR;
		}

		if (flags & ES_DRAW_CONTENT_RICH_TEXT) {
			char *string;
			EsTextRun *textRuns;
			size_t textRunCount;
			EsRichTextParse(text, textBytes, &string, &textRuns, &textRunCount, &textRun[0].style);
			EsTextPlan *plan = EsTextPlanCreate(element, &properties, textBounds, string, textRuns, textRunCount);

			if (plan) {
				EsDrawText(painter, plan, textBounds, nullptr, selectionProperties);
				EsTextPlanDestroy(plan);
			}

			EsHeapFree(textRuns);
			EsHeapFree(string);
		} else {
			EsTextPlan *plan = EsTextPlanCreate(element, &properties, textBounds, text, textRun, 1);

			if (plan) {
				PaintTextLayers(painter, plan, textBounds, selectionProperties);
				EsTextPlanDestroy(plan);
			}
		}
	}

	painter->clip = oldClip;
}

void EsDrawContent(EsPainter *painter, EsElement *element, EsRectangle rectangle, 
		const char *text, ptrdiff_t textBytes, uint32_t iconID, uint32_t flags, EsTextSelection *selectionProperties) {
	if (textBytes == -1) textBytes = EsCStringLength(text);
	((UIStyle *) painter->style)->PaintText(painter, element, rectangle, text, textBytes, iconID, flags, selectionProperties);
}

void EsDrawTextLayers(EsPainter *painter, EsTextPlan *plan, EsRectangle bounds, EsTextSelection *selectionProperties) {
	((UIStyle *) painter->style)->PaintTextLayers(painter, plan, bounds, selectionProperties);
}

void UIStyle::PaintLayers(EsPainter *painter, EsRectangle location, int childType, int whichLayers) {
	EsBuffer data = {};
	data.in = (uint8_t *) (this + 1);
	data.bytes = layerDataByteCount;

	if (!THEME_RECT_VALID(painter->clip)) {
		return;
	}

	EsRectangle opaqueRegion = {};
	EsRectangle _bounds = Translate(location, painter->offsetX, painter->offsetY);

	if (opaqueInsets.l != 0x7F && opaqueInsets.r != 0x7F
			&& opaqueInsets.t != 0x7F && opaqueInsets.b != 0x7F) {
		opaqueRegion = THEME_RECT_4(_bounds.l + opaqueInsets.l, _bounds.r - opaqueInsets.r, 
			_bounds.t + opaqueInsets.t, _bounds.b - opaqueInsets.b);
	}

	if (appearance && whichLayers == 0) {
		EsDrawRectangle(painter, _bounds, appearance->backgroundColor, appearance->borderColor, appearance->borderSize);
		return;
	}

	for (uintptr_t i = 0; i < style->layerCount; i++) {
		const ThemeLayer *layer = (const ThemeLayer *) EsBufferRead(&data, sizeof(ThemeLayer));

		if (!layer) {
			return;
		}

		EsRectangle bounds;
		bounds.l = _bounds.l + (int) (scale * layer->offset.l) + THEME_RECT_WIDTH(_bounds)  * layer->position.l / 100;
		bounds.r = _bounds.l + (int) (scale * layer->offset.r) + THEME_RECT_WIDTH(_bounds)  * layer->position.r / 100;
		bounds.t = _bounds.t + (int) (scale * layer->offset.t) + THEME_RECT_HEIGHT(_bounds) * layer->position.t / 100;
		bounds.b = _bounds.t + (int) (scale * layer->offset.b) + THEME_RECT_HEIGHT(_bounds) * layer->position.b / 100;

		if (layer->mode == whichLayers) {
			EsBuffer data2 = data;

			if (layer->type == THEME_LAYER_BOX) {
				ThemeDrawBox(painter, bounds, &data2, scale, layer, opaqueRegion, childType);
			} else if (layer->type == THEME_LAYER_PATH) {
				ThemeDrawPath(painter, bounds, &data2, scale);
			}
		}

		EsBufferRead(&data, layer->dataByteCount - sizeof(ThemeLayer));
	}
}

inline void UIStyle::GetTextStyle(EsTextStyle *style) {
	// Also need to update PaintText.
	EsMemoryZero(style, sizeof(EsTextStyle));
	style->font = font;
	style->size = metrics->textSize;
	style->color = metrics->textColor;
	style->figures = metrics->textFigures;
} 

bool UIStyle::IsStateChangeObserved(uint16_t state1, uint16_t state2) {
	if (((state1 & ~THEME_PRIMARY_STATE_MASK) ^ (state2 & ~THEME_PRIMARY_STATE_MASK)) & observedStyleStateMask) {
		return true;
	}

	if (((0x10000 << (state1 & THEME_PRIMARY_STATE_MASK)) ^ (0x10000 << (state2 & THEME_PRIMARY_STATE_MASK))) & observedStyleStateMask) {
		return true;
	}

	return false;
}

bool UIStyle::IsRegionCompletelyOpaque(EsRectangle region, int width, int height) {
	return region.l >= opaqueInsets.l && region.r < width - opaqueInsets.r
		&& region.t >= opaqueInsets.t && region.b < height - opaqueInsets.b;
}

void EsDrawRoundedRectangle(EsPainter *painter, EsRectangle bounds, EsDeviceColor mainColor, EsDeviceColor borderColor, EsRectangle borderSize, const uint32_t *cornerRadii) {
	ThemeLayer layer = {};
	uint8_t info[sizeof(ThemeLayerBox) + sizeof(ThemePaintSolid) * 2] = {};

	ThemeLayerBox *infoBox = (ThemeLayerBox *) info;
	infoBox->borders = { (int8_t) borderSize.l, (int8_t) borderSize.r, (int8_t) borderSize.t, (int8_t) borderSize.b };
	infoBox->corners = { (int8_t) cornerRadii[0], (int8_t) cornerRadii[1], (int8_t) cornerRadii[2], (int8_t) cornerRadii[3] };
	infoBox->mainPaintType = THEME_PAINT_SOLID;
	infoBox->borderPaintType = THEME_PAINT_SOLID;

	ThemePaintSolid *infoMain = (ThemePaintSolid *) (infoBox + 1);
	infoMain->color = mainColor;

	ThemePaintSolid *infoBorder = (ThemePaintSolid *) (infoMain + 1);
	infoBorder->color = borderColor;

	EsBuffer data = { .in = (const uint8_t *) &info, .bytes = sizeof(info) };
	ThemeDrawBox(painter, bounds, &data, 1, &layer, {}, THEME_CHILD_TYPE_ONLY);
}

#endif
