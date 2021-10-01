// TODO I think this needs to be rewritten...
// TODO Converting linear gradients where all stops are the same color to a solid paint.
// TODO Why does cap mode on dashes not show selected?
// TODO Undoing creating layer, styles, constants.
// TODO Prevent deleting metrics layer.
// TODO Overrides on reused layers (additionally on non-interpolables; but not structural values).
// TODO Global layer list.
// TODO Animations with multiple keyframes.
// TODO More paint types.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>

#define ES_TEXT_H_LEFT 	 (1 << 0)
#define ES_TEXT_H_CENTER (1 << 1)
#define ES_TEXT_H_RIGHT  (1 << 2)
#define ES_TEXT_V_TOP 	 (1 << 3)
#define ES_TEXT_V_CENTER (1 << 4)
#define ES_TEXT_V_BOTTOM (1 << 5)
#define ES_TEXT_ELLIPSIS (1 << 6)
#define ES_TEXT_WRAP 	 (1 << 7)
#define EsCRTsqrtf sqrtf
#define EsAssert assert
#define EsHeapAllocate(x, y) ((y) ? calloc(1, (x)) : malloc((x)))
#define EsHeapFree free
#define EsCRTfabsf fabsf
#define EsCRTfmodf fmodf
#define EsCRTisnanf isnan
#define EsCRTfloorf floorf
#define AbsoluteFloat fabsf
#define EsCRTatan2f atan2f
#define EsCRTsinf sinf
#define EsCRTcosf cosf
#define EsCRTacosf acosf
#define EsCRTceilf ceilf
#define EsMemoryCopy memcpy
#define EsMemoryZero(a, b) memset((a), 0, (b))
#define ES_FUNCTION_OPTIMISE_O2 __attribute__((optimize("-O2")))
#define ES_INFINITY INFINITY
#define IN_DESIGNER

typedef struct EsBuffer {
	union { const uint8_t *in; uint8_t *out; };
	size_t position, bytes;
	bool error;
	void *context;
} EsBuffer;

#include "../luigi.h"
#define NANOSVG_IMPLEMENTATION
#include "../nanosvg.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../stb_truetype.h"
#define STB_DS_IMPLEMENTATION
#include "../stb_ds.h"
#include "../../shared/hash.cpp"
#define SHARED_COMMON_WANT_BUFFERS
#include "../../shared/common.cpp"

#define OP_DO_MOD           (1)
#define OP_MAKE_UI          (2)
#define OP_EXPORT           (3)
#define OP_GET_PALETTE      (4)
#define OP_REPLACE_COLOR    (5)
#define OP_FIND_COLOR_USERS (6)

#define PATH_IN_KEYFRAME (0xFFFFFFFE)
#define PATH_ANY (0xFFFFFFFD)

typedef struct StringOption {
	const char *string;
} StringOption;

typedef struct ModContext {
	struct Style *style;
	struct Layer *layer;
	struct Sequence *sequence;
	struct Keyframe *keyframe;
} ModContext;

const uint32_t saveFormatVersion = 22;

void SetSelectedItems(ModContext context);
void ColorListRefresh();

#define MOD_CONTEXT(style, layer, sequence, keyframe) ((ModContext) { (style), (layer), (sequence), (keyframe) })

#define REFLECT_IMPLEMENTATION
#include "reflect.h"
#include "../../bin/designer.h"

typedef struct PaletteItem {
	uint32_t key;
	int value;
} PaletteItem;

typedef struct ColorUsersItem {
	uint32_t key;
	Style **value;
} ColorUsersItem;

PaletteItem *palette;
ColorUsersItem *colorUsers;
uint32_t replaceColorFrom, replaceColorTo;
void *currentPaletteOpLayer;

void PropertyOp(RfState *state, RfItem *item, void *pointer) {
	Property *property = (Property *) pointer;
	
	if (state->op == RF_OP_FREE) {
		free(property->path);
		RfStructOp(state, item, pointer);
	} else if (state->op == RF_OP_SAVE) {
		uint8_t count = 0;

		if (property->path) {
			for (; property->path[count] != RF_PATH_TERMINATOR; count++);
			state->access(state, &count, sizeof(count));

			for (uintptr_t i = 0; property->path[i] != RF_PATH_TERMINATOR; i++) {
				RfIntegerSave(state, property->path + i, sizeof(uint32_t));
			}
		} else {
			count = 0xFF;
			state->access(state, &count, sizeof(count));
		}

		RfStructOp(state, item, pointer);
	} else if (state->op == RF_OP_LOAD) {
		uint8_t count = 0;
		state->access(state, &count, sizeof(count));

		if (count != 0xFF) {
			property->path = malloc(sizeof(uint32_t) * (count + 1));
			property->path[count] = RF_PATH_TERMINATOR;

			for (uintptr_t i = 0; i < count; i++) {
				RfIntegerLoad(state, property->path + i, sizeof(uint32_t));
			}
		} else {
			property->path = NULL;
		}

		RfStructOp(state, item, pointer);
	} else if (state->op == OP_GET_PALETTE || state->op == OP_REPLACE_COLOR || state->op == OP_FIND_COLOR_USERS) {
		void *pointer = currentPaletteOpLayer;
		RfItem item = {};
		item.type = &Layer_Type;
		item.byteCount = sizeof(Layer);
		bool success = RfPathResolve((RfPath *) (property->path + 1), &item, &pointer);
		assert(success);

		if (item.type->op == StyleColorOp) {
			item.type->op(state, &item, property->data.buffer);
		}
	} else {
		RfStructOp(state, item, pointer);
	}
}

#define MSG_PROPERTY_CHANGED ((UIMessage) (UI_MSG_USER + 1))

typedef struct MakeUIState {
	RfState s;
	uint32_t index;
	struct MakeUIState *parent;
	uint32_t *basePath;
	bool recurse, inKeyframe;
} MakeUIState;

UIWindow *window;
UITable *tableLayers, *tableSequences, *tableKeyframes;
UIButton *buttonAddLayer, *buttonAddExistingLayer, *buttonPublicStyle;

typedef struct AnimatingValue {
	uint16_t offset; 
#define ANIMATING_VALUE_TYPE_I8 (1)
#define ANIMATING_VALUE_TYPE_I16 (2)
#define ANIMATING_VALUE_TYPE_COLOR (3)
#define ANIMATING_VALUE_TYPE_FLOAT (4)
#define ANIMATING_VALUE_TYPE_UNUSED (1 << 7)
	uint8_t type;
	uint8_t layer;
	uint16_t duration, elapsed; // Milliseconds.
	union { int8_t i8; int16_t i16; uint32_t u32; float f32; } from, to;
} AnimatingValue;

typedef struct SequenceStateSelector {
	uint32_t primary;
	bool focused, checked, indeterminate, _default, itemFocus, listFocus, selected, enter, exit;
} SequenceStateSelector;

UIElement *elementCanvas;
UISlider *previewWidth, *previewHeight, *previewScale;
UIButton *previewTransition, *previewShowGuides, *previewShowComputed, *previewFixAspectRatio;
uint64_t previewTransitionLastTime;
AnimatingValue *animatingValues;
UIPanel *previewPrimaryStatePanel;
UIButton *previewPrimaryStateIdle;
UIButton *previewPrimaryStateHovered;
UIButton *previewPrimaryStatePressed;
UIButton *previewPrimaryStateDisabled;
UIButton *previewPrimaryStateInactive;
UIButton *previewStateFocused;
UIButton *previewStateChecked;
UIButton *previewStateIndeterminate;
UIButton *previewStateDefault;
UIButton *previewStateItemFocus;
UIButton *previewStateListFocus;
UIButton *previewStateSelected;
UIButton *previewStateBeforeEnter;
UIButton *previewStateAfterExit;
UIButton *editPoints;
UIColorPicker *previewBackgroundColor;
uint32_t previewPrimaryState;
SequenceStateSelector currentStateSelector;

UIPanel *panelInspector;
UIElement **inspectorSubscriptions;

UIWindow *importDialog;
UITextbox *importPathTextbox;
UILabel *importPathMessage;

Mod *undoStack, *redoStack;
bool modApplyUndo;

uint32_t *menuPath;

StyleSet styleSet;
ModContext selected = MOD_CONTEXT(NULL, NULL, NULL, NULL);

char temporaryOverride[4096];

char *filePath, *exportPath, *embedBitmapPath, *stylesPath;

void ModApply(ModData *mod);

char *LoadFile(const char *inputFileName, size_t *byteCount);

void MakeUI(MakeUIState *state, RfItem *item, void *pointer);
void MakeHeaderAndIndentUI(const char *format, RfState *state, RfItem *item, void *pointer);

#define MAKE_UI(c, p, k) \
	do { \
		for (uintptr_t i = 0; i < (c ## _Type).fieldCount; i++) { \
			MakeUIState state = { 0 }; \
			state.s.op = OP_MAKE_UI; \
			state.index = i; \
			state.recurse = true; \
			state.inKeyframe = k; \
			RfItem item = (c ## _Type).fields[i].item; \
			void *pointer = (uint8_t *) p + (c ## _Type).fields[i].offset; \
			item.type->op(&state.s, &item, pointer); \
			if (state.recurse) MakeUI(&state, &item, pointer); \
		} \
	} while (0)

// ------------------- Renderer -------------------

int ClampInteger(int from, int to, int value) {
	if (value < from) return from;
	if (value > to) return to;
	return value;
}

float LinearInterpolate(float from, float to, float progress) {
	return from + progress * (to - from);
}

float GammaInterpolate(float from, float to, float progress) {
	from = from * from;
	to = to * to;
	return sqrtf(from + progress * (to - from));
}

int MaximumInteger3(int a, int b, int c) {
	if (a >= b && a >= c) {
		return a;
	} else if (b >= c) {
		return b;
	} else {
		return c;
	}
}

int MinimumInteger3(int a, int b, int c) {
	if (a <= b && a <= c) {
		return a;
	} else if (b <= c) {
		return b;
	} else {
		return c;
	}
}

int MaximumInteger(int a, int b) {
	if (a >= b) {
		return a;
	} else {
		return b;
	}
}

int MinimumInteger(int a, int b) {
	if (a <= b) {
		return a;
	} else {
		return b;
	}
}

typedef struct Corners32 { int32_t tl, tr, bl, br; } Corners32;
typedef struct Rectangle32 { int32_t l, r, t, b; } Rectangle32;
typedef struct EsRectangle { int32_t l, r, t, b; } EsRectangle;

bool EsRectangleClip(EsRectangle parent, EsRectangle rectangle, EsRectangle *output) {
	EsRectangle current = parent;
	EsRectangle intersection = { 0 };

	if (!((current.l > rectangle.r && current.r > rectangle.l)
			|| (current.t > rectangle.b && current.b > rectangle.t))) {
		intersection.l = current.l > rectangle.l ? current.l : rectangle.l;
		intersection.t = current.t > rectangle.t ? current.t : rectangle.t;
		intersection.r = current.r < rectangle.r ? current.r : rectangle.r;
		intersection.b = current.b < rectangle.b ? current.b : rectangle.b;
	}

	if (output) {
		*output = intersection;
	}

	return intersection.l < intersection.r && intersection.t < intersection.b;
}

void BlendPixel(uint32_t *destinationPixel, uint32_t modified, bool fullAlpha) {
	if ((modified & 0xFF000000) == 0xFF000000) {
		*destinationPixel = modified;
	} else if ((modified & 0xFF000000) == 0x00000000) {
	} else if ((*destinationPixel & 0xFF000000) != 0xFF000000 && fullAlpha) {
		uint32_t original = *destinationPixel;
		uint32_t alpha1 = (modified & 0xFF000000) >> 24;
		uint32_t alpha2 = 255 - alpha1;
		uint32_t alphaD = (original & 0xFF000000) >> 24;
		uint32_t alphaD2 = alphaD * alpha2;
		uint32_t alphaOut = alpha1 + (alphaD2 >> 8);

		if (alphaOut) {
			uint32_t m2 = alphaD2 / alphaOut;
			uint32_t m1 = (alpha1 << 8) / alphaOut;
			if (m2 == 0x100) m2--;
			if (m1 == 0x100) m1--;
			uint32_t r2 = m2 * ((original & 0x000000FF) >> 0);
			uint32_t g2 = m2 * ((original & 0x0000FF00) >> 8);
			uint32_t b2 = m2 * ((original & 0x00FF0000) >> 16);
			uint32_t r1 = m1 * ((modified & 0x000000FF) >> 0);
			uint32_t g1 = m1 * ((modified & 0x0000FF00) >> 8);
			uint32_t b1 = m1 * ((modified & 0x00FF0000) >> 16);
			uint32_t result = (alphaOut << 24) 
				| (0x00FF0000 & ((b1 + b2) << 8)) 
				| (0x0000FF00 & ((g1 + g2) << 0)) 
				| (0x000000FF & ((r1 + r2) >> 8));
			*destinationPixel = result;
		}
	} else {
		uint32_t original = *destinationPixel;
		uint32_t alpha1 = (modified & 0xFF000000) >> 24;
		uint32_t alpha2 = 255 - alpha1;
		uint32_t r2 = alpha2 * ((original & 0x000000FF) >> 0);
		uint32_t g2 = alpha2 * ((original & 0x0000FF00) >> 8);
		uint32_t b2 = alpha2 * ((original & 0x00FF0000) >> 16);
		uint32_t r1 = alpha1 * ((modified & 0x000000FF) >> 0);
		uint32_t g1 = alpha1 * ((modified & 0x0000FF00) >> 8);
		uint32_t b1 = alpha1 * ((modified & 0x00FF0000) >> 16);
		uint32_t result = 0xFF000000 | (0x00FF0000 & ((b1 + b2) << 8)) 
			| (0x0000FF00 & ((g1 + g2) << 0)) 
			| (0x000000FF & ((r1 + r2) >> 8));
		*destinationPixel = result;
	}
}

typedef struct EsPaintTarget {
	void *bits;
	uint32_t width, height, stride;
	bool fullAlpha, readOnly;
} EsPaintTarget;

typedef struct EsPainter {
	EsRectangle clip;
	EsPaintTarget *target;
} EsPainter;

#include "../../desktop/renderer.cpp"
#include "../../desktop/theme.cpp"

// ------------------- Reflect utilities -------------------

int StringCompareRaw(const char *s1, ptrdiff_t length1, const char *s2, ptrdiff_t length2) {
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

RfData SaveToGrowableBuffer(RfType *type, size_t byteCount, void *options, void *pointer) {
	RfGrowableBuffer state = { 0 };
	state.s.op = RF_OP_SAVE;
	state.s.allocate = RfRealloc;
	state.s.access = RfWriteGrowableBuffer;
	RfItem item = { 0 };
	item.type = type;
	item.byteCount = byteCount;
	item.options = options;
	type->op(&state.s, &item, pointer);
	state.data.buffer = realloc(state.data.buffer, state.data.byteCount);
	return state.data;
}

bool ArePathsEqual(uint32_t *path1, uint32_t *path2) {
	if (!path1 && !path2) return true;
	if (!path1 || !path2) return false;
	
	while (true) {
		if (*path1 != *path2) {
			return false;
		}

		if (*path1 == RF_PATH_TERMINATOR) {
			return true;
		}

		path1++;
		path2++;
	}
}

uint32_t *DuplicatePath(uint32_t *indices) {
	uintptr_t count = 0;
	for (; indices[count] != RF_PATH_TERMINATOR; count++);
	uint32_t *path = (uint32_t *) malloc((count + 1) * sizeof(uint32_t));
	memcpy(path, indices, (count + 1) * sizeof(uint32_t));
	return path;
}

void PrintPath(uint32_t *indices) {
	printf("{ ");

	for (uintptr_t i = 0; indices[i] != RF_PATH_TERMINATOR; i++) {
		printf("%d, ", (int32_t) indices[i]);
	}

	printf(" }\n");
}

void *ResolveDataObject(RfPath *path, RfItem *item) {
	RfItem _item;
	if (!item) item = &_item;

	void *pointer;
	item->options = NULL;

	bool inKeyframe = false;

	if (path->indices[0] == PATH_IN_KEYFRAME) {
		inKeyframe = true;

		// PrintPath(path->indices);

		// Has the property been overridden in the keyframe?
		for (uintptr_t i = 0; i < arrlenu(selected.keyframe->properties); i++) {
			// PrintPath(selected.keyframe->properties[i].path);

			if (ArePathsEqual(path->indices, selected.keyframe->properties[i].path)) {
				// Get the RfItem.
				pointer = selected.layer;
				item->type = &Layer_Type;
				item->byteCount = sizeof(Layer);
				bool success = RfPathResolve(path + 1, item, &pointer);
				assert(success);
				assert(sizeof(temporaryOverride) >= sizeof(item->byteCount));

				// Load the override.
				RfGrowableBuffer state = { 0 };
				state.s.allocate = RfRealloc;
				state.s.access = RfReadGrowableBuffer;
				state.s.op = RF_OP_LOAD;
				state.data = selected.keyframe->properties[i].data;
				item->type->op(&state.s, item, temporaryOverride);

				// Return the temporary buffer.
				return temporaryOverride;
			}
		}

		path++;
	}

	if (selected.layer && selected.sequence && selected.keyframe && !inKeyframe) {
		pointer = selected.keyframe;
		item->type = &Keyframe_Type;
		item->byteCount = sizeof(Keyframe);
	} else if (selected.layer && selected.sequence && !inKeyframe) {
		pointer = selected.sequence;
		item->type = &Sequence_Type;
		item->byteCount = sizeof(Sequence);
	} else if (selected.layer) {
		pointer = selected.layer;
		item->type = &Layer_Type;
		item->byteCount = sizeof(Layer);
	} else {
		pointer = selected.style;
		item->type = &Style_Type;
		item->byteCount = sizeof(Style);
	}

	bool success = RfPathResolve(path, item, &pointer);
	assert(success);
	return pointer;
}

// ------------------- Exporting -------------------

typedef struct PathToOffset {
	uint32_t *path;
	uintptr_t offset;
} PathToOffset;

typedef struct ExportState {
	RfGrowableBuffer buffer;
	uint32_t *pathStack;
	PathToOffset *pathToOffsetList;
} ExportState;

void ExportAddPathToOffset(ExportState *export, uint32_t last, ptrdiff_t offset) {
	uintptr_t stackPosition = arrlenu(export->pathStack);
	arrput(export->pathStack, last);
	arrput(export->pathStack, RF_PATH_TERMINATOR);
	PathToOffset pathToOffset = { 0 };
	pathToOffset.path = DuplicatePath(export->pathStack);
	pathToOffset.offset = export->buffer.data.byteCount + offset;
	arrsetlen(export->pathStack, stackPosition);
	arrput(export->pathToOffsetList, pathToOffset);
}

void ExportAddPathToOffset2(ExportState *export, uint32_t last1, uint32_t last2, ptrdiff_t offset) {
	arrput(export->pathStack, last1);
	ExportAddPathToOffset(export, last2, offset);
	(void) arrpop(export->pathStack);
}

void ExportAddPathToOffsetForRectangle(ExportState *export, uint32_t last, ptrdiff_t offset) {
	uintptr_t stackPosition = arrlenu(export->pathStack);
	arrput(export->pathStack, last);
	arrput(export->pathStack, RF_PATH_TERMINATOR);
	PathToOffset pathToOffset = { 0 };
	pathToOffset.path = DuplicatePath(export->pathStack);
	pathToOffset.offset = export->buffer.data.byteCount + offset;
	arrsetlen(export->pathStack, stackPosition);
	arrput(export->pathToOffsetList, pathToOffset);
}

void ExportFreePathToOffsetList(PathToOffset *pathToOffsetList) {
	for (uintptr_t i = 0; i < arrlenu(pathToOffsetList); i++) {
		free(pathToOffsetList[i].path);
	}

	arrfree(pathToOffsetList);
}

RfData ExportToGrowableBuffer(RfType *type, size_t byteCount, void *options, void *pointer, PathToOffset **pathToOffsetList) {
	ExportState state = { 0 };
	state.buffer.s.op = OP_EXPORT;
	state.buffer.s.allocate = RfRealloc;
	state.buffer.s.access = RfWriteGrowableBuffer;

	RfItem item = { 0 };
	item.type = type;
	item.byteCount = byteCount;
	item.options = options;

	type->op(&state.buffer.s, &item, pointer);
	state.buffer.data.buffer = realloc(state.buffer.data.buffer, state.buffer.data.byteCount);

	arrfree(state.pathStack);

	*pathToOffsetList = state.pathToOffsetList;
	return state.buffer.data;
}

uint32_t ColorLookup(uint32_t id) {
	for (uintptr_t i = 0; i < arrlenu(styleSet.colors); i++) {
		if (styleSet.colors[i]->id == id) {
			return styleSet.colors[i]->value;
		}
	}

	assert(false);
	return 0;
}

Color *ColorLookupPointer(uint32_t id) {
	for (uintptr_t i = 0; i < arrlenu(styleSet.colors); i++) {
		if (styleSet.colors[i]->id == id) {
			return styleSet.colors[i];
		}
	}

	assert(false);
	return NULL;
}

#define EXPORT_FIELD(fromType, fromVariable, toType, toVariable, fromField, toField) \
	ExportAddPathToOffset(export, fromType ## _ ## fromField, offsetof(toType, toField)); \
	toVariable.toField = fromVariable->fromField;
#define EXPORT_FIELD_COLOR(fromType, fromVariable, toType, toVariable, fromField, toField) \
	ExportAddPathToOffset(export, fromType ## _ ## fromField, offsetof(toType, toField)); \
	toVariable.toField = ColorLookup(fromVariable->fromField);
#define EXPORT_FIELD_ALIGN(fromType, fromVariable, toType, toVariable, fromHField, fromVField, toField) \
	toVariable.toField = ((fromVariable->fromHField == ALIGN_START)  ? ES_TEXT_H_LEFT   : 0) \
			   | ((fromVariable->fromHField == ALIGN_CENTER) ? ES_TEXT_H_CENTER : 0) \
			   | ((fromVariable->fromHField == ALIGN_END)    ? ES_TEXT_H_RIGHT  : 0) \
			   | ((fromVariable->fromVField == ALIGN_START)  ? ES_TEXT_V_TOP    : 0) \
			   | ((fromVariable->fromVField == ALIGN_CENTER) ? ES_TEXT_V_CENTER : 0) \
			   | ((fromVariable->fromVField == ALIGN_END)    ? ES_TEXT_V_BOTTOM : 0);
#define EXPORT_RECTANGLE8_FIELD(fromType, fromVariable, toType, toVariable, fromField, toField) \
	ExportAddPathToOffset2(export, fromType ## _ ## fromField, Rectangle8_l, offsetof(toType, toField.l)); \
	ExportAddPathToOffset2(export, fromType ## _ ## fromField, Rectangle8_r, offsetof(toType, toField.r)); \
	ExportAddPathToOffset2(export, fromType ## _ ## fromField, Rectangle8_t, offsetof(toType, toField.t)); \
	ExportAddPathToOffset2(export, fromType ## _ ## fromField, Rectangle8_b, offsetof(toType, toField.b)); \
	toVariable.toField = fromVariable->fromField;
#define EXPORT_RECTANGLE16_FIELD(fromType, fromVariable, toType, toVariable, fromField, toField) \
	ExportAddPathToOffset2(export, fromType ## _ ## fromField, Rectangle16_l, offsetof(toType, toField.l)); \
	ExportAddPathToOffset2(export, fromType ## _ ## fromField, Rectangle16_r, offsetof(toType, toField.r)); \
	ExportAddPathToOffset2(export, fromType ## _ ## fromField, Rectangle16_t, offsetof(toType, toField.t)); \
	ExportAddPathToOffset2(export, fromType ## _ ## fromField, Rectangle16_b, offsetof(toType, toField.b)); \
	toVariable.toField.l = fromVariable->fromField.l; \
	toVariable.toField.r = fromVariable->fromField.r; \
	toVariable.toField.t = fromVariable->fromField.t; \
	toVariable.toField.b = fromVariable->fromField.b;

void PaintSolidOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		PaintSolid *solid = (PaintSolid *) pointer;
		ThemePaintSolid themeSolid = { 0 };
		ExportAddPathToOffset((ExportState *) state, PaintSolid_color, offsetof(ThemePaintSolid, color));
		themeSolid.color = ColorLookup(solid->color);
		state->access(state, &themeSolid, sizeof(themeSolid));
	} else {
		RfStructOp(state, item, pointer);
	}
}

void PaintOverwriteOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		PaintOverwrite *overwrite = (PaintOverwrite *) pointer;
		ThemePaintSolid themeSolid = { 0 };
		ExportAddPathToOffset((ExportState *) state, PaintOverwrite_color, offsetof(ThemePaintSolid, color));
		themeSolid.color = ColorLookup(overwrite->color);
		state->access(state, &themeSolid, sizeof(themeSolid));
	} else {
		RfStructOp(state, item, pointer);
	}
}

void GradientStopOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		GradientStop *stop = (GradientStop *) pointer;
		ThemeGradientStop themeStop = { 0 };
		ExportAddPathToOffset((ExportState *) state, GradientStop_color, offsetof(ThemeGradientStop, color));
		themeStop.color = ColorLookup(stop->color);
		ExportAddPathToOffset((ExportState *) state, GradientStop_position, offsetof(ThemeGradientStop, position));
		themeStop.position = stop->position;
		state->access(state, &themeStop, sizeof(themeStop));
	} else {
		RfStructOp(state, item, pointer);
	}
}

void PaintLinearGradientOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		ExportState *export = (ExportState *) state;
		PaintLinearGradient *gradient = (PaintLinearGradient *) pointer;
		ThemePaintLinearGradient themeGradient = { 0 };
		EXPORT_FIELD(PaintLinearGradient, gradient, ThemePaintLinearGradient, themeGradient, transformX, transform[0]);
		EXPORT_FIELD(PaintLinearGradient, gradient, ThemePaintLinearGradient, themeGradient, transformY, transform[1]);
		EXPORT_FIELD(PaintLinearGradient, gradient, ThemePaintLinearGradient, themeGradient, transformStart, transform[2]);
		themeGradient.useGammaInterpolation = gradient->useGammaInterpolation;
		themeGradient.useDithering = gradient->useDithering;
		themeGradient.useSystemColor = gradient->useSystemHue;
		themeGradient.stopCount = arrlenu(gradient->stops);
		themeGradient.repeatMode = gradient->repeat == GRADIENT_REPEAT_CLAMP ? RAST_REPEAT_CLAMP
			: gradient->repeat == GRADIENT_REPEAT_NORMAL ? RAST_REPEAT_NORMAL
			: gradient->repeat == GRADIENT_REPEAT_MIRROR ? RAST_REPEAT_MIRROR : 0;
		state->access(state, &themeGradient, sizeof(themeGradient));
		uintptr_t stackPosition = arrlenu(export->pathStack);

		for (uintptr_t i = 0; i < arrlenu(gradient->stops); i++) {
			arrput(export->pathStack, PaintLinearGradient_stops);
			arrput(export->pathStack, i);
			GradientStopOp(state, NULL, gradient->stops + i);
			arrsetlen(export->pathStack, stackPosition);
		}
	} else {
		RfStructOp(state, item, pointer);
	}
}

void PaintRadialGradientOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		ExportState *export = (ExportState *) state;
		PaintRadialGradient *gradient = (PaintRadialGradient *) pointer;
		ThemePaintRadialGradient themeGradient = { 0 };
		EXPORT_FIELD(PaintRadialGradient, gradient, ThemePaintRadialGradient, themeGradient, transform0, transform[0]);
		EXPORT_FIELD(PaintRadialGradient, gradient, ThemePaintRadialGradient, themeGradient, transform1, transform[1]);
		EXPORT_FIELD(PaintRadialGradient, gradient, ThemePaintRadialGradient, themeGradient, transform2, transform[2]);
		EXPORT_FIELD(PaintRadialGradient, gradient, ThemePaintRadialGradient, themeGradient, transform3, transform[3]);
		EXPORT_FIELD(PaintRadialGradient, gradient, ThemePaintRadialGradient, themeGradient, transform4, transform[4]);
		EXPORT_FIELD(PaintRadialGradient, gradient, ThemePaintRadialGradient, themeGradient, transform5, transform[5]);
		themeGradient.useGammaInterpolation = gradient->useGammaInterpolation;
		themeGradient.stopCount = arrlenu(gradient->stops);
		themeGradient.repeatMode = gradient->repeat == GRADIENT_REPEAT_CLAMP ? RAST_REPEAT_CLAMP
			: gradient->repeat == GRADIENT_REPEAT_NORMAL ? RAST_REPEAT_NORMAL
			: gradient->repeat == GRADIENT_REPEAT_MIRROR ? RAST_REPEAT_MIRROR : 0;
		state->access(state, &themeGradient, sizeof(themeGradient));
		uintptr_t stackPosition = arrlenu(export->pathStack);

		for (uintptr_t i = 0; i < arrlenu(gradient->stops); i++) {
			arrput(export->pathStack, PaintRadialGradient_stops);
			arrput(export->pathStack, i);
			GradientStopOp(state, NULL, gradient->stops + i);
			arrsetlen(export->pathStack, stackPosition);
		}
	} else {
		RfStructOp(state, item, pointer);
	}
}

Layer *LayerLookup(uint64_t id) {
	for (uintptr_t i = 0; i < arrlenu(styleSet.layers); i++) {
		if (styleSet.layers[i]->id == id) {
			return styleSet.layers[i];
		}
	}

	return NULL;
}

void LayerBoxOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		ExportState *export = (ExportState *) state;
		LayerBox *box = (LayerBox *) pointer;

#if 0
#define EsContainerOf(type, member, pointer) ((type *) ((uint8_t *) pointer - offsetof(type, member)))
		uint64_t layerID = EsContainerOf(Layer, base.box, box)->id;
#endif

		ThemeLayerBox themeBox = { 0 };
		ExportAddPathToOffset2(export, LayerBox_borders, Rectangle8_l, offsetof(ThemeLayerBox, borders.l));
		ExportAddPathToOffset2(export, LayerBox_borders, Rectangle8_r, offsetof(ThemeLayerBox, borders.r));
		ExportAddPathToOffset2(export, LayerBox_borders, Rectangle8_t, offsetof(ThemeLayerBox, borders.t));
		ExportAddPathToOffset2(export, LayerBox_borders, Rectangle8_b, offsetof(ThemeLayerBox, borders.b));
		themeBox.borders = box->borders;
		ExportAddPathToOffset2(export, LayerBox_corners, Corners8_tl, offsetof(ThemeLayerBox, corners.tl));
		ExportAddPathToOffset2(export, LayerBox_corners, Corners8_tr, offsetof(ThemeLayerBox, corners.tr));
		ExportAddPathToOffset2(export, LayerBox_corners, Corners8_bl, offsetof(ThemeLayerBox, corners.bl));
		ExportAddPathToOffset2(export, LayerBox_corners, Corners8_br, offsetof(ThemeLayerBox, corners.br));
		themeBox.corners = box->corners;
		if (box->blurred) themeBox.flags |= THEME_LAYER_BOX_IS_BLURRED;
		if (box->autoCorners) themeBox.flags |= THEME_LAYER_BOX_AUTO_CORNERS;
		if (box->autoBorders) themeBox.flags |= THEME_LAYER_BOX_AUTO_BORDERS;
		if (box->shadowHiding) themeBox.flags |= THEME_LAYER_BOX_SHADOW_HIDING;
		themeBox.mainPaintType = box->mainPaint.tag == Paint_solid + 1 ? THEME_PAINT_SOLID 
			: box->mainPaint.tag == Paint_linearGradient + 1 ? THEME_PAINT_LINEAR_GRADIENT 
			: box->mainPaint.tag == Paint_overwrite + 1 ? THEME_PAINT_OVERWRITE : 0;
		themeBox.borderPaintType = box->borderPaint.tag == Paint_solid + 1 ? THEME_PAINT_SOLID
			: box->borderPaint.tag == Paint_linearGradient + 1 ? THEME_PAINT_LINEAR_GRADIENT 
			: box->borderPaint.tag == Paint_overwrite + 1 ? THEME_PAINT_OVERWRITE : 0;
		state->access(state, &themeBox, sizeof(themeBox));

		uintptr_t stackPosition = arrlenu(export->pathStack);

		if (box->mainPaint.tag) {
			RfField *mainPaintField = Paint_Type.fields + box->mainPaint.tag - 1;
			arrput(export->pathStack, LayerBox_mainPaint);
			arrput(export->pathStack, box->mainPaint.tag - 1);
			mainPaintField->item.type->op(state, &mainPaintField->item, (uint8_t *) pointer 
					+ mainPaintField->offset + LayerBox_Type.fields[LayerBox_mainPaint].offset);
			arrsetlen(export->pathStack, stackPosition);
		}

		if (box->borderPaint.tag) {
			RfField *borderPaintField = Paint_Type.fields + box->borderPaint.tag - 1;
			arrput(export->pathStack, LayerBox_borderPaint);
			arrput(export->pathStack, box->borderPaint.tag - 1);
			borderPaintField->item.type->op(state, &borderPaintField->item, (uint8_t *) pointer 
					+ borderPaintField->offset + LayerBox_Type.fields[LayerBox_borderPaint].offset);
			arrsetlen(export->pathStack, stackPosition);
		}
	} else {
		RfStructOp(state, item, pointer);
	}
}

void LayerTextOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		ExportState *export = (ExportState *) state;
		LayerText *layer = (LayerText *) pointer;

		ThemeLayerText themeLayer = { 0 };

		EXPORT_FIELD_COLOR(LayerText, layer, ThemeLayerText, themeLayer, color, color);
		EXPORT_FIELD(LayerText, layer, ThemeLayerText, themeLayer, blur, blur);

		state->access(state, &themeLayer, sizeof(themeLayer));
	} else {
		RfStructOp(state, item, pointer);
	}
}

void PathFillSolidOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
	} else {
		RfStructOp(state, item, pointer);
	}
}

void PathFillContourOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		ExportState *export = (ExportState *) state;
		PathFillContour *fill = (PathFillContour *) pointer;

		ThemeLayerPathFillContour themeFill = { 0 };

		EXPORT_FIELD(PathFillContour, fill, ThemeLayerPathFillContour, themeFill, internalWidth, internalWidth);
		EXPORT_FIELD(PathFillContour, fill, ThemeLayerPathFillContour, themeFill, externalWidth, externalWidth);

		if (fill->joinMode != JOIN_MODE_BEVEL) {
			EXPORT_FIELD(PathFillContour, fill, ThemeLayerPathFillContour, themeFill, miterLimit, miterLimit);
		}

		themeFill.mode = (fill->joinMode == JOIN_MODE_MITER ? RAST_LINE_JOIN_MITER 
				: fill->joinMode == JOIN_MODE_ROUND ? RAST_LINE_JOIN_ROUND
				: fill->joinMode == JOIN_MODE_BEVEL ? RAST_LINE_JOIN_MITER : 0)
				| ((fill->capMode == CAP_MODE_FLAT ? RAST_LINE_CAP_FLAT
				: fill->capMode == CAP_MODE_ROUND ? RAST_LINE_CAP_ROUND
				: fill->capMode == CAP_MODE_SQUARE ? RAST_LINE_CAP_SQUARE : 0) << 2)
				| (fill->integerWidthsOnly ? 0x80 : 0);

		state->access(state, &themeFill, sizeof(themeFill));
	} else {
		RfStructOp(state, item, pointer);
	}
}

void PathFillDashOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		ExportState *export = (ExportState *) state;
		PathFillDash *fill = (PathFillDash *) pointer;

		ThemeLayerPathFillDash themeFill = { 0 };

		EXPORT_FIELD(PathFillDash, fill, ThemeLayerPathFillDash, themeFill, gap, gap);
		EXPORT_FIELD(PathFillDash, fill, ThemeLayerPathFillDash, themeFill, length, length);

		arrput(export->pathStack, PathFillDash_contour);

		EXPORT_FIELD(PathFillContour, (&fill->contour), ThemeLayerPathFillContour, themeFill.contour, internalWidth, internalWidth);
		EXPORT_FIELD(PathFillContour, (&fill->contour), ThemeLayerPathFillContour, themeFill.contour, externalWidth, externalWidth);

		if (fill->contour.joinMode != JOIN_MODE_BEVEL) {
			EXPORT_FIELD(PathFillContour, (&fill->contour), ThemeLayerPathFillContour, themeFill.contour, miterLimit, miterLimit);
		}

		(void) arrpop(export->pathStack);

		themeFill.contour.mode = (fill->contour.joinMode == JOIN_MODE_MITER ? RAST_LINE_JOIN_MITER 
				: fill->contour.joinMode == JOIN_MODE_ROUND ? RAST_LINE_JOIN_ROUND
				: fill->contour.joinMode == JOIN_MODE_BEVEL ? RAST_LINE_JOIN_MITER : 0)
				| ((fill->contour.capMode == CAP_MODE_FLAT ? RAST_LINE_CAP_FLAT
				: fill->contour.capMode == CAP_MODE_ROUND ? RAST_LINE_CAP_ROUND
				: fill->contour.capMode == CAP_MODE_SQUARE ? RAST_LINE_CAP_SQUARE : 0) << 2)
				| (fill->contour.integerWidthsOnly ? 0x80 : 0);

		state->access(state, &themeFill, sizeof(themeFill));
	} else if (state->op == OP_MAKE_UI) {
		MakeHeaderAndIndentUI("Dash %d", state, item, pointer);
	} else {
		RfStructOp(state, item, pointer);
	}
}

void PathFillDashedOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		ExportState *export = (ExportState *) state;
		PathFillDashed *fill = (PathFillDashed *) pointer;
		uintptr_t stackPosition = arrlenu(export->pathStack);

		for (uintptr_t i = 0; i < arrlenu(fill->dashes); i++) {
			arrput(export->pathStack, PathFillDashed_dashes);
			arrput(export->pathStack, i);
			PathFillDashOp(state, NULL, fill->dashes + i);
			arrsetlen(export->pathStack, stackPosition);
		}
	} else {
		RfStructOp(state, item, pointer);
	}
}

void PathFillOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		ExportState *export = (ExportState *) state;
		PathFill *fill = (PathFill *) pointer;

		ThemeLayerPathFill themeFill = { 0 };
		themeFill.paintAndFillType |= fill->paint.tag == Paint_solid + 1 ? THEME_PAINT_SOLID 
			: fill->paint.tag == Paint_linearGradient + 1 ? THEME_PAINT_LINEAR_GRADIENT 
			: fill->paint.tag == Paint_radialGradient + 1 ? THEME_PAINT_RADIAL_GRADIENT 
			: fill->paint.tag == Paint_overwrite + 1 ? THEME_PAINT_OVERWRITE : 0;
		themeFill.paintAndFillType |= fill->mode.tag == PathFillMode_solid + 1 ? THEME_PATH_FILL_SOLID
			: fill->mode.tag == PathFillMode_contour + 1 ? THEME_PATH_FILL_CONTOUR
			: fill->mode.tag == PathFillMode_dashed + 1 ? THEME_PATH_FILL_DASHED : 0;
		themeFill.dashCount = fill->mode.tag == PathFillMode_dashed + 1 ? arrlen(fill->mode.dashed.dashes) : 0;
		state->access(state, &themeFill, sizeof(themeFill));

		uintptr_t stackPosition = arrlenu(export->pathStack);

		if (fill->paint.tag) {
			RfField *paintField = Paint_Type.fields + fill->paint.tag - 1;
			arrput(export->pathStack, PathFill_paint);
			arrput(export->pathStack, fill->paint.tag - 1);
			paintField->item.type->op(state, &paintField->item, (uint8_t *) pointer 
					+ paintField->offset + PathFill_Type.fields[PathFill_paint].offset);
			arrsetlen(export->pathStack, stackPosition);
		}

		if (fill->mode.tag) {
			RfField *modeField = PathFillMode_Type.fields + fill->mode.tag - 1;
			arrput(export->pathStack, PathFill_mode);
			arrput(export->pathStack, fill->mode.tag - 1);
			modeField->item.type->op(state, &modeField->item, (uint8_t *) pointer 
					+ modeField->offset + PathFill_Type.fields[PathFill_mode].offset);
			arrsetlen(export->pathStack, stackPosition);
		}
	} else if (state->op == OP_MAKE_UI) {
		MakeHeaderAndIndentUI("Fill %d", state, item, pointer);
	} else {
		RfStructOp(state, item, pointer);
	}
}

void LayerPathOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		ExportState *export = (ExportState *) state;
		LayerPath *layer = (LayerPath *) pointer;

		ThemeLayerPath themeLayer = { 0 };
		if (layer->evenOdd) themeLayer.flags |= THEME_LAYER_PATH_FILL_EVEN_ODD;
		if (layer->closed) themeLayer.flags |= THEME_LAYER_PATH_CLOSED;
		themeLayer.pointCount = arrlen(layer->points);
		themeLayer.fillCount = arrlen(layer->fills);
		EXPORT_FIELD(LayerPath, layer, ThemeLayerPath, themeLayer, alpha, alpha);
		state->access(state, &themeLayer, sizeof(themeLayer));

		uintptr_t stackPosition = arrlenu(export->pathStack);

		for (uintptr_t i = 0; i < arrlenu(layer->points); i++) {
			arrput(export->pathStack, LayerPath_points);
			arrput(export->pathStack, i);
			PathPointOp(state, NULL, layer->points + i);
			arrsetlen(export->pathStack, stackPosition);
		}

		for (uintptr_t i = 0; i < arrlenu(layer->fills); i++) {
			arrput(export->pathStack, LayerPath_fills);
			arrput(export->pathStack, i);
			PathFillOp(state, NULL, layer->fills + i);
			arrsetlen(export->pathStack, stackPosition);
		}
	} else {
		RfStructOp(state, item, pointer);
	}
}

Rectangle8 StyleCalculateMaximumGlobalOutsets(uint64_t *styleLayers) {
	Rectangle8 globalOutsets = { 0 };

	for (uintptr_t i = 0; i < arrlenu(styleLayers); i++) {
		Layer *layer = LayerLookup(styleLayers[i]);
		
		if (layer->base.tag != LayerBase_metrics + 1) {
			continue;
		}

		globalOutsets.l = MaximumInteger(0, -layer->base.metrics.globalOffset.l);
		globalOutsets.r = MaximumInteger(0,  layer->base.metrics.globalOffset.r);
		globalOutsets.t = MaximumInteger(0, -layer->base.metrics.globalOffset.t);
		globalOutsets.b = MaximumInteger(0,  layer->base.metrics.globalOffset.b);

		for (uintptr_t j = 0; j < arrlenu(layer->sequences); j++) {
			for (uintptr_t k = 0; k < arrlenu(layer->sequences[j]->keyframes); k++) {
				for (uintptr_t l = 0; l < arrlenu(layer->sequences[j]->keyframes[k]->properties); l++) {
					Property property = layer->sequences[j]->keyframes[k]->properties[l];

					if (property.path[0] != PATH_IN_KEYFRAME || property.path[1] != Layer_base 
							|| property.path[2] != 0 || property.path[3] != LayerMetrics_globalOffset) {
						continue;
					}

					int8_t value = 0;
					RfGrowableBuffer state = { 0 };
					state.s.access = RfReadGrowableBuffer;
					state.data = property.data;
					state.s.access(&state.s, &value, sizeof(int8_t));

					if (layer->position.l == 0 && -value > globalOutsets.l && property.path[4] == Rectangle8_l) {
						globalOutsets.l = -value;
					}

					if (layer->position.r == 100 && value > globalOutsets.r && property.path[4] == Rectangle8_r) {
						globalOutsets.r = value;
					}

					if (layer->position.t == 0 && -value > globalOutsets.t && property.path[4] == Rectangle8_t) {
						globalOutsets.t = -value;
					}

					if (layer->position.b == 100 && value > globalOutsets.b && property.path[4] == Rectangle8_b) {
						globalOutsets.b = value;
					} 
				}
			}
		}

		break;
	}

	return globalOutsets;
}

Rectangle8 Rectangle8Add(Rectangle8 a, Rectangle8 b) {
	a.l += b.l;
	a.t += b.t;
	a.r += b.r;
	a.b += b.b;
	return a;
}

Rectangle8 StyleCalculatePaintOutsets(uint64_t *styleLayers) {
	Rectangle8 paintOutsets = { 0 };

	for (uintptr_t i = 0; i < arrlenu(styleLayers); i++) {
		Layer *layer = LayerLookup(styleLayers[i]);

		if (layer->position.l == 0 && -layer->offset.l > paintOutsets.l) {
			paintOutsets.l = -layer->offset.l;
		}

		if (layer->position.r == 100 && layer->offset.r > paintOutsets.r) {
			paintOutsets.r = layer->offset.r;
		}

		if (layer->position.t == 0 && -layer->offset.t > paintOutsets.t) {
			paintOutsets.t = -layer->offset.t;
		}

		if (layer->position.b == 100 && layer->offset.b > paintOutsets.b) {
			paintOutsets.b = layer->offset.b;
		}

		for (uintptr_t j = 0; j < arrlenu(layer->sequences); j++) {
			for (uintptr_t k = 0; k < arrlenu(layer->sequences[j]->keyframes); k++) {
				for (uintptr_t l = 0; l < arrlenu(layer->sequences[j]->keyframes[k]->properties); l++) {
					Property property = layer->sequences[j]->keyframes[k]->properties[l];

					if (property.path[0] != PATH_IN_KEYFRAME || property.path[1] != Layer_offset) {
						continue;
					}

					int8_t value = 0;
					RfGrowableBuffer state = { 0 };
					state.s.access = RfReadGrowableBuffer;
					state.data = property.data;
					state.s.access(&state.s, &value, sizeof(int8_t));

					if (layer->position.l == 0 && -value > paintOutsets.l && property.path[2] == Rectangle8_l) {
						paintOutsets.l = -value;
					}

					if (layer->position.r == 100 && value > paintOutsets.r && property.path[2] == Rectangle8_r) {
						paintOutsets.r = value;
					}

					if (layer->position.t == 0 && -value > paintOutsets.t && property.path[2] == Rectangle8_t) {
						paintOutsets.t = -value;
					}

					if (layer->position.b == 100 && value > paintOutsets.b && property.path[2] == Rectangle8_b) {
						paintOutsets.b = value;
					} 
				}
			}
		}
	}

	return Rectangle8Add(paintOutsets, StyleCalculateMaximumGlobalOutsets(styleLayers));
}

Rectangle8 StyleCalculateOpaqueInsets(uint64_t *styleLayers) {
	Rectangle8 opaqueInsets = (Rectangle8) { 0x7F, 0x7F, 0x7F, 0x7F };

	for (uintptr_t i = 0; i < arrlenu(styleLayers); i++) {
		Layer *layer = LayerLookup(styleLayers[i]);

		if (layer->position.l != 0 || layer->position.r != 100 || layer->position.t != 0 || layer->position.b != 100
				|| layer->base.tag != LayerBase_box + 1 || layer->base.box.shadowHiding || layer->base.box.blurred) {
			continue;
		}
		
		Paint *paint = &layer->base.box.mainPaint;

		bool isOpaque = false;

		if (paint->tag == Paint_solid + 1) {
			isOpaque = (paint->solid.color & 0xFF000000) == 0xFF000000;
		} else if (paint->tag == Paint_linearGradient + 1) {
			isOpaque = true;

			for (uintptr_t j = 0; j < arrlenu(paint->linearGradient.stops); j++) {
				if ((paint->linearGradient.stops[j].color & 0xFF000000) != 0xFF000000) {
					isOpaque = false;
				}
			}
		} else if (paint->tag == Paint_overwrite + 1) {
			isOpaque = true;
		}

		Rectangle8 largestBorders = layer->base.box.borders;
		Corners8 largestCorners = layer->base.box.corners;

		for (uintptr_t j = 0; j < arrlenu(layer->sequences); j++) {
			for (uintptr_t k = 0; k < arrlenu(layer->sequences[j]->keyframes); k++) {
				for (uintptr_t l = 0; l < arrlenu(layer->sequences[j]->keyframes[k]->properties); l++) {
					Property property = layer->sequences[j]->keyframes[k]->properties[l];

					if (property.path[0] == PATH_IN_KEYFRAME && property.path[1] == Layer_base
							&& property.path[2] == LayerBase_box && property.path[3] == LayerBox_borders) {
						int8_t value = 0;
						RfGrowableBuffer state = { 0 };
						state.s.access = RfReadGrowableBuffer;
						state.data = property.data;
						state.s.access(&state.s, &value, sizeof(int8_t));

						if (property.path[4] == Rectangle8_l && largestBorders.l < value) largestBorders.l = value;
						if (property.path[4] == Rectangle8_r && largestBorders.r < value) largestBorders.r = value;
						if (property.path[4] == Rectangle8_t && largestBorders.t < value) largestBorders.t = value;
						if (property.path[4] == Rectangle8_b && largestBorders.b < value) largestBorders.b = value;
					} else if (property.path[0] == PATH_IN_KEYFRAME && property.path[1] == Layer_base
							&& property.path[2] == LayerBase_box && property.path[3] == LayerBox_corners) {
						int8_t value = 0;
						RfGrowableBuffer state = { 0 };
						state.s.access = RfReadGrowableBuffer;
						state.data = property.data;
						state.s.access(&state.s, &value, sizeof(int8_t));

						if (property.path[4] == Corners8_tl && largestCorners.tl < value) largestCorners.tl = value;
						if (property.path[4] == Corners8_tr && largestCorners.tr < value) largestCorners.tr = value;
						if (property.path[4] == Corners8_bl && largestCorners.bl < value) largestCorners.bl = value;
						if (property.path[4] == Corners8_br && largestCorners.br < value) largestCorners.br = value;
					}

					if (property.path[0] != PATH_IN_KEYFRAME || property.path[1] != Layer_base
							|| property.path[2] != LayerBase_box || property.path[3] != LayerBox_mainPaint
							|| property.path[4] != paint->tag - 1) {
						continue;
					}

					if (paint->tag == Paint_solid + 1) {
						if (property.path[5] != PaintSolid_color) {
							continue;
						}
					} else if (paint->tag == Paint_linearGradient + 1) {
						if (property.path[5] != PaintLinearGradient_stops
								|| property.path[7] != GradientStop_color) {
							continue;
						}
					}

					uint32_t value = 0;
					RfGrowableBuffer state = { 0 };
					state.s.access = RfReadGrowableBuffer;
					state.data = property.data;
					state.s.access(&state.s, &value, sizeof(uint32_t));

					if ((value & 0xFF000000) != 0xFF000000) {
						isOpaque = false;
					}
				}
			}
		}

		if (isOpaque) {
			opaqueInsets.l = MinimumInteger(opaqueInsets.l, MaximumInteger3(largestCorners.tl, largestCorners.bl, largestBorders.l));
			opaqueInsets.r = MinimumInteger(opaqueInsets.r, MaximumInteger3(largestCorners.tr, largestCorners.br, largestBorders.r));
			opaqueInsets.t = MinimumInteger(opaqueInsets.t, MaximumInteger3(largestCorners.tl, largestCorners.tr, largestBorders.t));
			opaqueInsets.b = MinimumInteger(opaqueInsets.b, MaximumInteger3(largestCorners.bl, largestCorners.br, largestBorders.b));
		}
	}

	return Rectangle8Add(opaqueInsets, StyleCalculateMaximumGlobalOutsets(styleLayers));
}

Rectangle8 StyleCalculateApproximateBorders(uint64_t *styleLayers) {
	for (uintptr_t i = 0; i < arrlenu(styleLayers); i++) {
		Layer *layer = LayerLookup(styleLayers[i]);

		if (layer->position.l != 0 || layer->position.r != 100 || layer->position.t != 0 || layer->position.b != 100
				|| layer->base.tag != LayerBase_box + 1 || layer->base.box.shadowHiding 
				|| layer->base.box.blurred || !layer->base.box.borderPaint.tag
				|| layer->mode != LAYER_MODE_BACKGROUND) {
			continue;
		}

		return layer->base.box.borders;
	}

	return (Rectangle8) { 0 };
}

void LayerMetricsOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		ThemeMetrics metrics = { 0 };
		ExportState *export = (ExportState *) state;
		LayerMetrics *layer = (LayerMetrics *) pointer;
		LayerMetrics *inherit = NULL;

		if (layer->inheritText.byteCount) {
			for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
				Style *style = styleSet.styles[i];

				if (layer->inheritText.byteCount == style->name.byteCount 
						&& 0 == memcmp(layer->inheritText.buffer, style->name.buffer, style->name.byteCount)) {
					inherit = &LayerLookup(style->layers[0])->base.metrics;
					break;
				}
			}
		}

		metrics.insets.l = layer->insets.l;
		metrics.insets.r = layer->insets.r;
		metrics.insets.t = layer->insets.t;
		metrics.insets.b = layer->insets.b;
		metrics.clipEnabled = layer->clipEnabled == CLIP_MODE_ENABLED;
		metrics.clipInsets.l = layer->clipInsets.l;
		metrics.clipInsets.r = layer->clipInsets.r;
		metrics.clipInsets.t = layer->clipInsets.t;
		metrics.clipInsets.b = layer->clipInsets.b;
		metrics.cursor = layer->cursor;
		metrics.preferredWidth = layer->preferredSize.width;
		metrics.preferredHeight = layer->preferredSize.height;
		metrics.minimumWidth = layer->minimumSize.width;
		metrics.minimumHeight = layer->minimumSize.height;
		metrics.maximumWidth = layer->maximumSize.width;
		metrics.maximumHeight = layer->maximumSize.height;
		metrics.gapMajor = layer->gaps.major;
		metrics.gapMinor = layer->gaps.minor;
		metrics.gapWrap = layer->gaps.wrap;
		EXPORT_RECTANGLE16_FIELD(LayerMetrics, layer, ThemeMetrics, metrics, globalOffset, globalOffset);

		int fontFamily = inherit ? inherit->fontFamily : layer->fontFamily;
		metrics.fontFamily = fontFamily == FONT_FAMILY_SANS ? 0xFFFF : fontFamily == FONT_FAMILY_SERIF ? 0xFFFE : 0xFFFD;

		if (inherit) {
			EXPORT_FIELD_COLOR(LayerMetrics, inherit, ThemeMetrics, metrics, textColor, textColor);
			EXPORT_FIELD_COLOR(LayerMetrics, inherit, ThemeMetrics, metrics, selectedBackground, selectedBackground);
			EXPORT_FIELD_COLOR(LayerMetrics, inherit, ThemeMetrics, metrics, selectedText, selectedText);
			EXPORT_FIELD(LayerMetrics, inherit, ThemeMetrics, metrics, textSize, textSize);
			EXPORT_FIELD(LayerMetrics, inherit, ThemeMetrics, metrics, fontWeight, fontWeight);
			EXPORT_FIELD(LayerMetrics, inherit, ThemeMetrics, metrics, italic, isItalic);
		} else {
			EXPORT_FIELD_COLOR(LayerMetrics, layer, ThemeMetrics, metrics, textColor, textColor);
			EXPORT_FIELD_COLOR(LayerMetrics, layer, ThemeMetrics, metrics, selectedBackground, selectedBackground);
			EXPORT_FIELD_COLOR(LayerMetrics, layer, ThemeMetrics, metrics, selectedText, selectedText);
			EXPORT_FIELD(LayerMetrics, layer, ThemeMetrics, metrics, textSize, textSize);
			EXPORT_FIELD(LayerMetrics, layer, ThemeMetrics, metrics, fontWeight, fontWeight);
			EXPORT_FIELD(LayerMetrics, layer, ThemeMetrics, metrics, italic, isItalic);
		}

		EXPORT_FIELD(LayerMetrics, layer, ThemeMetrics, metrics, iconSize, iconSize);
		EXPORT_FIELD_COLOR(LayerMetrics, layer, ThemeMetrics, metrics, iconColor, iconColor);
		EXPORT_FIELD_ALIGN(LayerMetrics, layer, ThemeMetrics, metrics, textHorizontalAlign, textVerticalAlign, textAlign);
		if (layer->ellipsis) metrics.textAlign |= ES_TEXT_ELLIPSIS;
		if (layer->wrapText) metrics.textAlign |= ES_TEXT_WRAP;
		state->access(state, &metrics, sizeof(metrics));
	} else {
		RfStructOp(state, item, pointer);
	}
}

void LayerOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		ExportState *export = (ExportState *) state;
		Layer *layer = (Layer *) pointer;
		ThemeLayer themeLayer = { 0 };
		ExportAddPathToOffset2(export, Layer_offset, Rectangle8_l, offsetof(ThemeLayer, offset.l));
		ExportAddPathToOffset2(export, Layer_offset, Rectangle8_r, offsetof(ThemeLayer, offset.r));
		ExportAddPathToOffset2(export, Layer_offset, Rectangle8_t, offsetof(ThemeLayer, offset.t));
		ExportAddPathToOffset2(export, Layer_offset, Rectangle8_b, offsetof(ThemeLayer, offset.b));
		themeLayer.offset = layer->offset;
		ExportAddPathToOffset2(export, Layer_position, Rectangle8_l, offsetof(ThemeLayer, position.l));
		ExportAddPathToOffset2(export, Layer_position, Rectangle8_r, offsetof(ThemeLayer, position.r));
		ExportAddPathToOffset2(export, Layer_position, Rectangle8_t, offsetof(ThemeLayer, position.t));
		ExportAddPathToOffset2(export, Layer_position, Rectangle8_b, offsetof(ThemeLayer, position.b));
		themeLayer.position = layer->position;
		themeLayer.mode = layer->mode;
		if (layer->base.tag == LayerBase_box + 1) themeLayer.type = THEME_LAYER_BOX;
		if (layer->base.tag == LayerBase_metrics + 1) themeLayer.type = THEME_LAYER_METRICS;
		if (layer->base.tag == LayerBase_text + 1) themeLayer.type = THEME_LAYER_TEXT;
		if (layer->base.tag == LayerBase_path + 1) themeLayer.type = THEME_LAYER_PATH;
		assert(themeLayer.type);
		state->access(state, &themeLayer, sizeof(themeLayer));
		uintptr_t stackPosition = arrlenu(export->pathStack);
		RfField *baseField = LayerBase_Type.fields + layer->base.tag - 1;
		arrput(export->pathStack, Layer_base);
		arrput(export->pathStack, 0);
		baseField->item.type->op(state, &baseField->item, (uint8_t *) pointer 
			+ baseField->offset + Layer_Type.fields[Layer_base].offset);
		arrsetlen(export->pathStack, stackPosition);
	} else if (state->op == OP_GET_PALETTE || state->op == OP_REPLACE_COLOR || state->op == OP_FIND_COLOR_USERS) {
		currentPaletteOpLayer = pointer;
	} else {
		RfStructOp(state, item, pointer);
	}
}

void StyleSetOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		ExportState *export = (ExportState *) state;
		StyleSet *styleSet = (StyleSet *) pointer;

		// Load the cursors image.

		size_t bitmapBytes = 0;
		char *bitmap = NULL;

		if (embedBitmapPath) {
			bitmap = LoadFile(embedBitmapPath, &bitmapBytes);

			if (!bitmap) {
				printf("Error: Could not load the embedded bitmap!\n");
				return;
			}
		}

		// Write the header.

		ThemeHeader header = { 0 };
		header.signature = THEME_HEADER_SIGNATURE;
		header.styleCount = arrlenu(styleSet->styles);
		header.constantCount = arrlenu(styleSet->constants);
		header.bitmapBytes = bitmapBytes;
		state->access(state, &header, sizeof(header));
		assert((export->buffer.data.byteCount & 3) == 0);

		// Write the list of styles.

		uint32_t styleListOffset = export->buffer.data.byteCount;

		FILE *f = stylesPath ? fopen(stylesPath, "wb") : NULL;

		for (uintptr_t i = 0; i < header.styleCount; i++) {
			Style *style = styleSet->styles[i];
			ThemeStyle entry = { 0 };
			entry.id = (style->id << 1) | 1;
			entry.layerCount = arrlenu(style->layers);
			entry.paintOutsets = StyleCalculatePaintOutsets(style->layers);
			entry.opaqueInsets = StyleCalculateOpaqueInsets(style->layers);
			entry.approximateBorders = StyleCalculateApproximateBorders(style->layers);
			state->access(state, &entry, sizeof(entry));
			assert((export->buffer.data.byteCount & 3) == 0);

			printf("exporting '%.*s' (id: %ld)\n", (int) style->name.byteCount, (char *) style->name.buffer, (style->id << 1) | 1);

			for (uintptr_t i = 0; i < arrlenu(style->layers); i++) {
				printf("\thas layer %ld\n", style->layers[i]);
			}

			if (style->id && stylesPath) {
				fprintf(f, "%s ES_STYLE_", style->publicStyle ? "define" : "private define");

				bool dot = false;

				for (uintptr_t j = 0; j < style->name.byteCount; j++) {
					char c = ((const char *) style->name.buffer)[j];

					if (c == '.') {
						fprintf(f, "_");
						dot = true;
					} else if (c >= 'A' && c <= 'Z' && j && !dot) {
						fprintf(f, "_%c", c);
					} else {
						fprintf(f, "%c", toupper(c));
						dot = false;
					}
				}

				fprintf(f, " (ES_STYLE_CAST(%ld))\n", (style->id << 1) | 1);
			}
		}

		if (stylesPath) fclose(f);

		// Write the list of constants.

		uint32_t constantListOffset = export->buffer.data.byteCount;

		for (uintptr_t i = 0; i < header.constantCount; i++) {
			Constant *constant = styleSet->constants[i];
			ThemeConstant entry = { 0 };
			entry.hash = CalculateCRC64(constant->key.buffer, constant->key.byteCount, 0);
			state->access(state, &entry, sizeof(entry));
			assert((export->buffer.data.byteCount & 3) == 0);
		}

		for (uintptr_t i = 0; i < header.constantCount; i++) {
			Constant *constant = styleSet->constants[i];
			ThemeConstant *entry = (ThemeConstant *) ((uint8_t *) export->buffer.data.buffer + constantListOffset) + i;
			entry->valueOffset = export->buffer.data.byteCount;
			entry->valueByteCount = constant->value.byteCount + 1;
			entry->scale = constant->scale;
			state->access(state, constant->value.buffer, constant->value.byteCount);
			uint8_t terminate = 0;
			state->access(state, &terminate, 1);
			uint32_t pad = 0;
			state->access(state, &pad, 4 - ((constant->value.byteCount + 1) & 3));
			assert((export->buffer.data.byteCount & 3) == 0);
		}

		// Write out all layers.

		for (uintptr_t i = 0; i < arrlenu(styleSet->layers); i++) {
			Layer *layer = styleSet->layers[i];
			layer->exportOffset = export->buffer.data.byteCount;
			assert((layer->exportOffset & 3) == 0);
			RfItem item = { 0 };
			item.type = &Layer_Type;
			item.byteCount = sizeof(Layer);
			LayerOp(state, &item, layer);
			ThemeLayer *entry = (ThemeLayer *) ((uint8_t *) export->buffer.data.buffer + layer->exportOffset);
			entry->dataByteCount = export->buffer.data.byteCount - layer->exportOffset;
			entry->sequenceDataOffset = arrlenu(layer->sequences) ? export->buffer.data.byteCount : 0;

			Layer *previousLayer = selected.layer;
			selected.layer = layer; // HACK!

			// Write out the sequences.

			for (uintptr_t j = 0; j < arrlenu(layer->sequences); j++) {
				Sequence *sequence = layer->sequences[j];
				uint32_t headerOffset = export->buffer.data.byteCount;

				{
					ThemeSequenceHeader header = { 0 };
					header.state = sequence->primaryState
						| (sequence->flagFocused ? THEME_STATE_FOCUSED : 0)
						| (sequence->flagChecked ? THEME_STATE_CHECKED : 0)
						| (sequence->flagIndeterminate ? THEME_STATE_INDETERMINATE : 0)
						| (sequence->flagDefault ? THEME_STATE_DEFAULT_BUTTON : 0)
						| (sequence->flagItemFocus ? THEME_STATE_FOCUSED_ITEM : 0)
						| (sequence->flagListFocus ? THEME_STATE_LIST_FOCUSED : 0)
						| (sequence->flagBeforeEnter ? THEME_STATE_BEFORE_ENTER : 0)
						| (sequence->flagAfterExit ? THEME_STATE_AFTER_EXIT : 0)
						| (sequence->flagSelected ? THEME_STATE_SELECTED : 0);
					header.duration = sequence->duration;
					header.isLastSequence = j == arrlenu(layer->sequences) - 1;
					state->access(state, &header, sizeof(header));

					if (sequence->flagBeforeEnter) printf("before enter %ld\n", layer->id);
					if (sequence->flagAfterExit) printf("after exit %ld\n", layer->id);
				}

				uint32_t overrideCount = 0;

				for (uintptr_t k = 0; k < arrlenu(sequence->keyframes); k++) {
					for (uintptr_t l = 0; l < arrlenu(sequence->keyframes[k]->properties); l++) {
						Property *property = sequence->keyframes[k]->properties + l;

						for (uintptr_t m = 0; m < arrlenu(export->pathToOffsetList); m++) {
							const PathToOffset *pathToOffset = export->pathToOffsetList + m;

							if (!ArePathsEqual(pathToOffset->path, property->path + 1)) {
								continue;
							}

							RfItem item;
							Keyframe *previousKeyframe = selected.keyframe;
							selected.keyframe = (Keyframe *) sequence->keyframes[k]; // HACK!
							void *source = ResolveDataObject((RfPath *) property->path, &item);
							selected.keyframe = previousKeyframe;

							ThemeOverride override = { 0 };
							override.offset = pathToOffset->offset - layer->exportOffset;

							if (item.type == &StyleI8_Type) {
								override.type = THEME_OVERRIDE_I8;
								override.data.i8 = *(int8_t *) source;
							} else if (item.type == &StyleI16_Type) {
								override.type = THEME_OVERRIDE_I16;
								override.data.i16 = *(int16_t *) source;
							} else if (item.type == &StyleFloat_Type) {
								override.type = THEME_OVERRIDE_F32;
								override.data.f32 = *(float *) source;
							} else if (item.type == &StyleColor_Type) {
								override.type = THEME_OVERRIDE_COLOR;
								override.data.u32 = ColorLookup(*(uint32_t *) source);
							} else {
								assert(false);
							}

							overrideCount++;
							state->access(state, &override, sizeof(override));

							break;
						}
					}
				}

				ThemeSequenceHeader *header = (ThemeSequenceHeader *) ((uint8_t *) export->buffer.data.buffer + headerOffset);
				header->overrideCount = overrideCount;
			}

			selected.layer = previousLayer;

			ExportFreePathToOffsetList(export->pathToOffsetList);
			export->pathToOffsetList = NULL;
		}

		// Write out layer lists for styles, and update the style list to point to them.

		for (uintptr_t i = 0; i < header.styleCount; i++) {
			Style *style = styleSet->styles[i];
			uint32_t layerListOffset = export->buffer.data.byteCount;
			assert((export->buffer.data.byteCount & 3) == 0);

			for (uintptr_t j = 0; j < arrlenu(style->layers); j++) {
				uint32_t layerOffset = LayerLookup(style->layers[j])->exportOffset;
				state->access(state, &layerOffset, sizeof(layerOffset));
			}

			ThemeStyle *entry = (ThemeStyle *) ((uint8_t *) export->buffer.data.buffer + styleListOffset) + i;
			entry->layerListOffset = layerListOffset;
		}

		// Write out the bitmap.
		state->access(state, bitmap, bitmapBytes);

		free(bitmap);
	} else {
		RfStructOp(state, item, pointer);
	}
}

void ActionExport(void *_unused) {
	PathToOffset *pathToOffsetList;
	RfData data = ExportToGrowableBuffer(&StyleSet_Type, sizeof(StyleSet), NULL, &styleSet, &pathToOffsetList);
	ExportFreePathToOffsetList(pathToOffsetList);
	FILE *f = fopen(exportPath, "wb");
	fwrite(data.buffer, 1, data.byteCount, f);
	fclose(f);
	free(data.buffer);

	{
		RfState state = { 0 };
		state.op = OP_FIND_COLOR_USERS;
		RfItem item = { 0 };
		item.type = &StyleSet_Type;
		item.byteCount = sizeof(StyleSet);
		item.options = NULL;
		RfBroadcast(&state, &item, &styleSet, true);

		for (uintptr_t i = 0; i < hmlenu(colorUsers); i++) {
			Color *color = ColorLookupPointer(colorUsers[i].key);

			printf("%.*s - ", (int) color->key.byteCount, (char *) color->key.buffer);

			for (uintptr_t j = 0; j < arrlenu(colorUsers[i].value); j++) {
				Style *style = colorUsers[i].value[j];
				printf("%.*s, ", (int) style->name.byteCount, (char *) style->name.buffer);
			}

			printf("\n");
			arrfree(colorUsers[i].value);
		}

		for (uintptr_t i = 0; i < arrlenu(styleSet.colors); i++) {
			printf("%.*s\t%.8X\n", (int) styleSet.colors[i]->key.byteCount, (char *) styleSet.colors[i]->key.buffer, styleSet.colors[i]->value);
		}

		for (uintptr_t i = 0; i < arrlenu(styleSet.colors); i++) {
			for (uintptr_t j = 0; j < hmlenu(colorUsers); j++) {
				if (styleSet.colors[i]->id == colorUsers[j].key) {
					goto used;
				}
			}

			printf("Color '%.*s' is unused.\n", (int) styleSet.colors[i]->key.byteCount, (char *) styleSet.colors[i]->key.buffer);
			used:;
		}

		hmfree(colorUsers);
	}
}

// ------------------- Exporting to Designer2 -------------------

enum PropertyType {
	PROP_NONE,
	PROP_COLOR,
	PROP_INT,
	PROP_OBJECT,
	PROP_FLOAT,
};

typedef struct Property2 {
	uint8_t type;
#define PROPERTY_NAME_SIZE (31)
	char cName[PROPERTY_NAME_SIZE];

	union {
		int32_t integer;
		uint64_t object;
		float floating;
	};
} Property2;

enum ObjectType {
	OBJ_NONE,

	OBJ_STYLE,
	OBJ_COMMENT,
	OBJ_INSTANCE,

	OBJ_VAR_COLOR = 0x40,
	OBJ_VAR_INT,
	OBJ_VAR_TEXT_STYLE,
	OBJ_VAR_ICON_STYLE,
	OBJ_VAR_CONTOUR_STYLE,
	
	OBJ_PAINT_OVERWRITE = 0x60,
	OBJ_PAINT_LINEAR_GRADIENT,
	OBJ_PAINT_RADIAL_GRADIENT,

	OBJ_LAYER_BOX = 0x80,
	OBJ_LAYER_METRICS,
	OBJ_LAYER_TEXT,
	OBJ_LAYER_GROUP,
	OBJ_LAYER_PATH,

	OBJ_MOD_COLOR = 0xC0,
	OBJ_MOD_MULTIPLY,
};

typedef struct Object2 {
	uint8_t type;
#define OBJECT_NAME_SIZE (46)
	char cName[OBJECT_NAME_SIZE];
#define OBJECT_IS_SELECTED (1 << 0)
#define OBJECT_IN_PROTOTYPE (1 << 1)
	uint8_t flags;
	uint64_t id;
	Property2 *properties;
} Object2;

void ObjectAddIntegerProperty(Object2 *object, const char *cName, int32_t value) {
	Property2 property = { 0 };
	property.type = PROP_INT;
	strcpy(property.cName, cName);
	property.integer = value;
	arrput(object->properties, property);
}

void ObjectAddColorProperty(Object2 *object, const char *cName, uint32_t value) {
	Property2 property = { 0 };
	property.type = PROP_COLOR;
	strcpy(property.cName, cName);
	property.integer = value;
	arrput(object->properties, property);
}

void ObjectAddFloatProperty(Object2 *object, const char *cName, float value) {
	Property2 property = { 0 };
	property.type = PROP_FLOAT;
	strcpy(property.cName, cName);
	property.floating = value;
	arrput(object->properties, property);
}

void ObjectAddObjectProperty(Object2 *object, const char *cName, uint64_t value) {
	Property2 property = { 0 };
	property.type = PROP_OBJECT;
	strcpy(property.cName, cName);
	property.object = value;
	arrput(object->properties, property);
}

uint64_t ExportPaint2(Paint *paint, int *x, int y, Object2 **objects, uint64_t *idAllocator) {
	char cPropertyName[PROPERTY_NAME_SIZE];

	if (!paint->tag) {
		return 0;
	} else if (paint->tag == Paint_solid + 1) {
		return ColorLookupPointer(paint->solid.color)->object2ID;
	} else if (paint->tag == Paint_linearGradient + 1) {
		bool constantColor = true;

		for (uintptr_t i = 1; i < arrlenu(paint->linearGradient.stops); i++) {
			if (paint->linearGradient.stops[i].color != paint->linearGradient.stops[0].color) {
				constantColor = false;
				break;
			}
		}

		if (constantColor) {
			return ColorLookupPointer(paint->linearGradient.stops[0].color)->object2ID;
		}

		Object2 object = { .type = OBJ_PAINT_LINEAR_GRADIENT, .id = ++(*idAllocator) };

		ObjectAddIntegerProperty(&object, "_graphX", *x);
		ObjectAddIntegerProperty(&object, "_graphY", y);
		ObjectAddIntegerProperty(&object, "_graphW", 80);
		ObjectAddIntegerProperty(&object, "_graphH", 60);
		ObjectAddFloatProperty(&object, "transformX", paint->linearGradient.transformX);
		ObjectAddFloatProperty(&object, "transformY", paint->linearGradient.transformY);
		ObjectAddFloatProperty(&object, "transformStart", paint->linearGradient.transformStart);
		ObjectAddIntegerProperty(&object, "repeatMode", paint->linearGradient.repeat);
		ObjectAddIntegerProperty(&object, "useGammaInterpolation", paint->linearGradient.useGammaInterpolation);
		ObjectAddIntegerProperty(&object, "useSystemColor", paint->linearGradient.useSystemHue);
		ObjectAddIntegerProperty(&object, "stops_count", arrlenu(paint->linearGradient.stops));

		for (uintptr_t i = 0; i < arrlenu(paint->linearGradient.stops); i++) {
			sprintf(cPropertyName, "stops_%d_position", (int) i);
			ObjectAddIntegerProperty(&object, cPropertyName, paint->linearGradient.stops[i].position);
			sprintf(cPropertyName, "stops_%d_color", (int) i);
			ObjectAddObjectProperty(&object, cPropertyName, ColorLookupPointer(paint->linearGradient.stops[i].color)->object2ID);
		}

		*x += 100;
		arrput(*objects, object);
		return object.id;
	} else if (paint->tag == Paint_radialGradient + 1) {
		Object2 object = { .type = OBJ_PAINT_RADIAL_GRADIENT, .id = ++(*idAllocator) };

		ObjectAddIntegerProperty(&object, "_graphX", *x);
		ObjectAddIntegerProperty(&object, "_graphY", y);
		ObjectAddIntegerProperty(&object, "_graphW", 80);
		ObjectAddIntegerProperty(&object, "_graphH", 60);
		ObjectAddFloatProperty(&object, "transform0", paint->radialGradient.transform0);
		ObjectAddFloatProperty(&object, "transform1", paint->radialGradient.transform1);
		ObjectAddFloatProperty(&object, "transform2", paint->radialGradient.transform2);
		ObjectAddFloatProperty(&object, "transform3", paint->radialGradient.transform3);
		ObjectAddFloatProperty(&object, "transform4", paint->radialGradient.transform4);
		ObjectAddFloatProperty(&object, "transform5", paint->radialGradient.transform5);
		ObjectAddIntegerProperty(&object, "repeatMode", paint->radialGradient.repeat);
		ObjectAddIntegerProperty(&object, "useGammaInterpolation", paint->radialGradient.useGammaInterpolation);
		ObjectAddIntegerProperty(&object, "stops_count", arrlenu(paint->radialGradient.stops));

		for (uintptr_t i = 0; i < arrlenu(paint->radialGradient.stops); i++) {
			sprintf(cPropertyName, "stops_%d_position", (int) i);
			ObjectAddIntegerProperty(&object, cPropertyName, paint->radialGradient.stops[i].position);
			sprintf(cPropertyName, "stops_%d_color", (int) i);
			ObjectAddObjectProperty(&object, cPropertyName, ColorLookupPointer(paint->radialGradient.stops[i].color)->object2ID);
		}

		*x += 100;
		arrput(*objects, object);
		return object.id;
	} else if (paint->tag == Paint_overwrite + 1) {
		Object2 object = { .type = OBJ_PAINT_OVERWRITE, .id = ++(*idAllocator) };
		ObjectAddIntegerProperty(&object, "_graphX", *x);
		ObjectAddIntegerProperty(&object, "_graphY", y);
		ObjectAddIntegerProperty(&object, "_graphW", 80);
		ObjectAddIntegerProperty(&object, "_graphH", 60);
		ObjectAddObjectProperty(&object, "color", ColorLookupPointer(paint->overwrite.color)->object2ID);
		*x += 100;
		arrput(*objects, object);
		return object.id;
	} else {
		assert(false);
		return 0;
	}
}

uint64_t ExportFillMode2(PathFillMode *fill, int *x, int y, Object2 **objects, uint64_t *idAllocator) {
	if (fill->tag == PathFillMode_solid + 1) {
		return 0;
	} else if (fill->tag == PathFillMode_contour + 1) {
		Object2 object = { .type = OBJ_VAR_CONTOUR_STYLE, .id = ++(*idAllocator) };
		ObjectAddIntegerProperty(&object, "_graphX", *x);
		ObjectAddIntegerProperty(&object, "_graphY", y);
		ObjectAddIntegerProperty(&object, "_graphW", 80);
		ObjectAddIntegerProperty(&object, "_graphH", 60);
		ObjectAddIntegerProperty(&object, "internalWidth", fill->contour.internalWidth);
		ObjectAddIntegerProperty(&object, "externalWidth", fill->contour.externalWidth);
		ObjectAddIntegerProperty(&object, "integerWidthsOnly", fill->contour.integerWidthsOnly);
		ObjectAddIntegerProperty(&object, "joinMode", fill->contour.joinMode == JOIN_MODE_ROUND ? RAST_LINE_JOIN_ROUND : RAST_LINE_JOIN_MITER);
		ObjectAddIntegerProperty(&object, "capMode", fill->contour.capMode == CAP_MODE_FLAT ? RAST_LINE_CAP_FLAT 
				: fill->contour.capMode == CAP_MODE_ROUND ? RAST_LINE_CAP_ROUND : RAST_LINE_CAP_SQUARE);
		ObjectAddFloatProperty(&object, "miterLimit", fill->contour.joinMode == JOIN_MODE_BEVEL ? 0.0f : fill->contour.miterLimit);
		*x += 100;
		arrput(*objects, object);
		return object.id;
	} else {
		assert(false);
		return 0;
	}
}

void ActionExportDesigner2(void *cp) {
	// TODO Exporting sequences.
	// TODO Inherited text styles.
	// TODO Merging identical layers and styles.

	Object2 *objects = NULL;
	uint64_t objectIDAllocator = 0;
	char cPropertyName[PROPERTY_NAME_SIZE];

	int y = 0;

	// Colors.

	for (uintptr_t i = 0; i < arrlenu(styleSet.colors); i++) {
		Object2 object = { .type = OBJ_VAR_COLOR, .id = ++objectIDAllocator };
		snprintf(object.cName, sizeof(object.cName), "%.*s", (int) styleSet.colors[i]->key.byteCount, (const char *) styleSet.colors[i]->key.buffer);
		ObjectAddIntegerProperty(&object, "_graphX", (i % 10) * 180);
		ObjectAddIntegerProperty(&object, "_graphY", (i / 10) * 100 + y);
		ObjectAddIntegerProperty(&object, "_graphW", 80);
		ObjectAddIntegerProperty(&object, "_graphH", 60);
		ObjectAddColorProperty(&object, "color", styleSet.colors[i]->value);
		ObjectAddIntegerProperty(&object, "isExported", 0);
		arrput(objects, object);
		styleSet.colors[i]->object2ID = object.id;
	}

	y += (arrlenu(styleSet.colors) / 10) * 100 + 200;

	// Constants.

	for (uintptr_t i = 0; i < arrlenu(styleSet.constants); i++) {
		char value[64];
		snprintf(value, sizeof(value), "%.*s", (int) styleSet.constants[i]->value.byteCount, (const char *) styleSet.constants[i]->value.buffer);
		bool isColor = value[0] == '0' && value[1] == 'x';
		Object2 object = { .type = isColor ? OBJ_VAR_COLOR : OBJ_VAR_INT, .id = ++objectIDAllocator };
		snprintf(object.cName, sizeof(object.cName), "%.*s", (int) styleSet.constants[i]->key.byteCount, (const char *) styleSet.constants[i]->key.buffer);
		ObjectAddIntegerProperty(&object, "_graphX", (i % 5) * 360);
		ObjectAddIntegerProperty(&object, "_graphY", (i / 5) * 100 + y);
		ObjectAddIntegerProperty(&object, "_graphW", 80);
		ObjectAddIntegerProperty(&object, "_graphH", 60);
		if (isColor) ObjectAddColorProperty(&object, "color", strtol(value, NULL, 0));
		else ObjectAddIntegerProperty(&object, "value", strtol(value, NULL, 0));
		ObjectAddIntegerProperty(&object, "isScaled", styleSet.constants[i]->scale);
		ObjectAddIntegerProperty(&object, "isExported", 1);
		arrput(objects, object);
	}

	y += (arrlenu(styleSet.constants) / 5) * 100 + 200;

	// Styles.

	int x0 = 180 * 10 + 200;
	y = 0;

	for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
		int x = x0;
		Style *style = styleSet.styles[i];
		
		Object2 layerGroup = { .type = OBJ_LAYER_GROUP, .id = ++objectIDAllocator };
		Object2 metrics = { 0 }, textStyle = { 0 }, iconStyle = { 0 };
		int32_t layerCount = 0;

		for (uintptr_t i = 0; i < arrlenu(style->layers); i++) {
			Layer *layer = LayerLookup(style->layers[i]);
			bool addToLayerGroup = false, addToObjects = false;
			Object2 object = { 0 };

			if (layer->base.tag == LayerBase_box + 1) {
				object.type = OBJ_LAYER_BOX, object.id = ++objectIDAllocator;
				LayerBox *box = &layer->base.box;
				ObjectAddIntegerProperty(&object, "borders0", box->borders.l);
				ObjectAddIntegerProperty(&object, "borders1", box->borders.r);
				ObjectAddIntegerProperty(&object, "borders2", box->borders.t);
				ObjectAddIntegerProperty(&object, "borders3", box->borders.b);
				ObjectAddIntegerProperty(&object, "corners0", box->corners.tl);
				ObjectAddIntegerProperty(&object, "corners1", box->corners.tr);
				ObjectAddIntegerProperty(&object, "corners2", box->corners.bl);
				ObjectAddIntegerProperty(&object, "corners3", box->corners.br);
				ObjectAddIntegerProperty(&object, "isBlurred", box->blurred);
				ObjectAddIntegerProperty(&object, "autoCorners", box->autoCorners);
				ObjectAddIntegerProperty(&object, "autoBorders", box->autoBorders);
				ObjectAddIntegerProperty(&object, "shadowHiding", box->shadowHiding);
				ObjectAddObjectProperty(&object, "mainPaint", ExportPaint2(&box->mainPaint, &x, y, &objects, &objectIDAllocator));
				ObjectAddObjectProperty(&object, "borderPaint", ExportPaint2(&box->borderPaint, &x, y, &objects, &objectIDAllocator));
				addToLayerGroup = true;
				addToObjects = true;
			} else if (layer->base.tag == LayerBase_text + 1) {
				object.type = OBJ_LAYER_TEXT, object.id = ++objectIDAllocator;
				ObjectAddObjectProperty(&object, "color", ColorLookupPointer(layer->base.text.color)->object2ID);
				ObjectAddIntegerProperty(&object, "blur", layer->base.text.blur);
				addToLayerGroup = true;
				addToObjects = true;
			} else if (layer->base.tag == LayerBase_path + 1) {
				object.type = OBJ_LAYER_PATH, object.id = ++objectIDAllocator;
				LayerPath *path = &layer->base.path;
				ObjectAddIntegerProperty(&object, "pathFillEvenOdd", path->evenOdd);
				ObjectAddIntegerProperty(&object, "pathClosed", path->closed);
				ObjectAddIntegerProperty(&object, "alpha", path->alpha);
				ObjectAddIntegerProperty(&object, "points_count", arrlenu(path->points));
				ObjectAddIntegerProperty(&object, "fills_count", arrlenu(path->fills));

				for (uintptr_t i = 0; i < arrlenu(path->points); i++) {
					sprintf(cPropertyName, "points_%d_x0", (int) i);
					ObjectAddFloatProperty(&object, cPropertyName, path->points[i].x0);
					sprintf(cPropertyName, "points_%d_y0", (int) i);
					ObjectAddFloatProperty(&object, cPropertyName, path->points[i].y0);
					sprintf(cPropertyName, "points_%d_x1", (int) i);
					ObjectAddFloatProperty(&object, cPropertyName, path->points[i].x1);
					sprintf(cPropertyName, "points_%d_y1", (int) i);
					ObjectAddFloatProperty(&object, cPropertyName, path->points[i].y1);
					sprintf(cPropertyName, "points_%d_x2", (int) i);
					ObjectAddFloatProperty(&object, cPropertyName, path->points[i].x2);
					sprintf(cPropertyName, "points_%d_y2", (int) i);
					ObjectAddFloatProperty(&object, cPropertyName, path->points[i].y2);
				}

				for (uintptr_t i = 0; i < arrlenu(path->fills); i++) {
					sprintf(cPropertyName, "fills_%d_paint", (int) i);
					ObjectAddObjectProperty(&object, cPropertyName, ExportPaint2(&path->fills[i].paint, &x, y, &objects, &objectIDAllocator));
					sprintf(cPropertyName, "fills_%d_mode", (int) i);
					ObjectAddObjectProperty(&object, cPropertyName, ExportFillMode2(&path->fills[i].mode, &x, y, &objects, &objectIDAllocator));
				}

				addToLayerGroup = true;
				addToObjects = true;
			} else {
				object.type = OBJ_LAYER_METRICS, object.id = ++objectIDAllocator;
				LayerMetrics *m = &layer->base.metrics;
				ObjectAddIntegerProperty(&object, "clipEnabled", m->clipEnabled);
				ObjectAddIntegerProperty(&object, "wrapText", m->wrapText);
				ObjectAddIntegerProperty(&object, "ellipsis", m->ellipsis);
				ObjectAddIntegerProperty(&object, "insets0", m->insets.l);
				ObjectAddIntegerProperty(&object, "insets1", m->insets.r);
				ObjectAddIntegerProperty(&object, "insets2", m->insets.t);
				ObjectAddIntegerProperty(&object, "insets3", m->insets.b);
				ObjectAddIntegerProperty(&object, "clipInsets0", m->clipInsets.l);
				ObjectAddIntegerProperty(&object, "clipInsets1", m->clipInsets.r);
				ObjectAddIntegerProperty(&object, "clipInsets2", m->clipInsets.t);
				ObjectAddIntegerProperty(&object, "clipInsets3", m->clipInsets.b);
				ObjectAddIntegerProperty(&object, "preferredWidth", m->preferredSize.width);
				ObjectAddIntegerProperty(&object, "preferredHeight", m->preferredSize.height);
				ObjectAddIntegerProperty(&object, "minimumWidth", m->minimumSize.width);
				ObjectAddIntegerProperty(&object, "minimumHeight", m->minimumSize.height);
				ObjectAddIntegerProperty(&object, "maximumWidth", m->maximumSize.width);
				ObjectAddIntegerProperty(&object, "maximumHeight", m->maximumSize.height);
				ObjectAddIntegerProperty(&object, "gapMajor", m->gaps.major);
				ObjectAddIntegerProperty(&object, "gapMinor", m->gaps.minor);
				ObjectAddIntegerProperty(&object, "gapWrap", m->gaps.wrap);
				ObjectAddIntegerProperty(&object, "cursor", m->cursor);
				ObjectAddIntegerProperty(&object, "horizontalTextAlign", m->textHorizontalAlign + 1);
				ObjectAddIntegerProperty(&object, "verticalTextAlign", m->textVerticalAlign + 1);
				assert(!m->globalOffset.l && !m->globalOffset.r && !m->globalOffset.t && !m->globalOffset.b);
				addToObjects = true;
				metrics = object;

				textStyle.type = OBJ_VAR_TEXT_STYLE, textStyle.id = ++objectIDAllocator;
				ObjectAddIntegerProperty(&textStyle, "_graphX", x);
				ObjectAddIntegerProperty(&textStyle, "_graphY", y);
				ObjectAddIntegerProperty(&textStyle, "_graphW", 80);
				ObjectAddIntegerProperty(&textStyle, "_graphH", 60);
				ObjectAddObjectProperty(&textStyle, "textColor", ColorLookupPointer(m->textColor)->object2ID);
				ObjectAddObjectProperty(&textStyle, "selectedBackground", ColorLookupPointer(m->selectedBackground)->object2ID);
				ObjectAddObjectProperty(&textStyle, "selectedText", ColorLookupPointer(m->selectedText)->object2ID);
				ObjectAddIntegerProperty(&textStyle, "textSize", m->textSize);
				ObjectAddIntegerProperty(&textStyle, "fontWeight", m->fontWeight);
				ObjectAddIntegerProperty(&textStyle, "isItalic", m->italic);
				ObjectAddIntegerProperty(&textStyle, "fontFamily", m->fontFamily == FONT_FAMILY_MONO ? 0xFFFD : 0xFFFF);
				arrput(objects, textStyle);
				x += 100;

				iconStyle.type = OBJ_VAR_ICON_STYLE, iconStyle.id = ++objectIDAllocator;
				ObjectAddIntegerProperty(&iconStyle, "_graphX", x);
				ObjectAddIntegerProperty(&iconStyle, "_graphY", y);
				ObjectAddIntegerProperty(&iconStyle, "_graphW", 80);
				ObjectAddIntegerProperty(&iconStyle, "_graphH", 60);
				ObjectAddIntegerProperty(&iconStyle, "iconSize", m->iconSize);
				ObjectAddObjectProperty(&iconStyle, "iconColor", ColorLookupPointer(m->iconColor)->object2ID);
				arrput(objects, iconStyle);
				x += 100;
			}

			if (addToLayerGroup) {
				sprintf(cPropertyName, "layers_%d_layer", layerCount);
				ObjectAddObjectProperty(&layerGroup, cPropertyName, object.id);
				sprintf(cPropertyName, "layers_%d_offset0", layerCount);
				ObjectAddIntegerProperty(&layerGroup, cPropertyName, layer->offset.l);
				sprintf(cPropertyName, "layers_%d_offset1", layerCount);
				ObjectAddIntegerProperty(&layerGroup, cPropertyName, layer->offset.r);
				sprintf(cPropertyName, "layers_%d_offset2", layerCount);
				ObjectAddIntegerProperty(&layerGroup, cPropertyName, layer->offset.t);
				sprintf(cPropertyName, "layers_%d_offset3", layerCount);
				ObjectAddIntegerProperty(&layerGroup, cPropertyName, layer->offset.b);
				sprintf(cPropertyName, "layers_%d_position0", layerCount);
				ObjectAddIntegerProperty(&layerGroup, cPropertyName, layer->position.l);
				sprintf(cPropertyName, "layers_%d_position1", layerCount);
				ObjectAddIntegerProperty(&layerGroup, cPropertyName, layer->position.r);
				sprintf(cPropertyName, "layers_%d_position2", layerCount);
				ObjectAddIntegerProperty(&layerGroup, cPropertyName, layer->position.t);
				sprintf(cPropertyName, "layers_%d_position3", layerCount);
				ObjectAddIntegerProperty(&layerGroup, cPropertyName, layer->position.b);
				sprintf(cPropertyName, "layers_%d_mode", layerCount);
				ObjectAddIntegerProperty(&layerGroup, cPropertyName, layer->mode);
				layerCount++;
			}

			if (addToObjects) {
				ObjectAddIntegerProperty(&object, "_graphX", x);
				ObjectAddIntegerProperty(&object, "_graphY", y);
				ObjectAddIntegerProperty(&object, "_graphW", 80);
				ObjectAddIntegerProperty(&object, "_graphH", 60);
				arrput(objects, object);
				x += 100;
			}
		}

		{
			Object2 object = layerGroup;
			ObjectAddIntegerProperty(&object, "_graphX", x);
			ObjectAddIntegerProperty(&object, "_graphY", y);
			ObjectAddIntegerProperty(&object, "_graphW", 80);
			ObjectAddIntegerProperty(&object, "_graphH", 60);
			ObjectAddIntegerProperty(&object, "layers_count", layerCount);
			arrput(objects, object);
			x += 100;
		}

		{
			Object2 object = { .type = OBJ_STYLE, .id = ++objectIDAllocator };
			snprintf(object.cName, sizeof(object.cName), "%.*s", (int) style->name.byteCount, (const char *) style->name.buffer);
			ObjectAddIntegerProperty(&object, "_graphX", x);
			ObjectAddIntegerProperty(&object, "_graphY", y);
			ObjectAddIntegerProperty(&object, "_graphW", 80);
			ObjectAddIntegerProperty(&object, "_graphH", 60);
			ObjectAddIntegerProperty(&object, "isPublic", style->publicStyle);
			ObjectAddObjectProperty(&object, "appearance", layerGroup.id);
			ObjectAddObjectProperty(&object, "metrics", metrics.id);
			ObjectAddObjectProperty(&object, "textStyle", textStyle.id);
			ObjectAddObjectProperty(&object, "iconStyle", iconStyle.id);
			arrput(objects, object);
			x += 100;
		}

		y += 200;
	}

	// Saving.

	FILE *f = fopen("bin/designer2.dat", "wb");
	uint32_t version = 1;
	fwrite(&version, 1, sizeof(uint32_t), f);
	uint32_t objectCount = arrlenu(objects);
	fwrite(&objectCount, 1, sizeof(uint32_t), f);
	fwrite(&objectIDAllocator, 1, sizeof(uint64_t), f);

	for (uintptr_t i = 0; i < arrlenu(objects); i++) {
		Object2 copy = objects[i];
		uint32_t propertyCount = arrlenu(copy.properties);
		copy.properties = NULL;
		fwrite(&copy, 1, sizeof(Object2), f);
		fwrite(&propertyCount, 1, sizeof(uint32_t), f);
		fwrite(objects[i].properties, 1, sizeof(Property2) * propertyCount, f);
		arrfree(objects[i].properties);
		assert(objects[i].id);
	}

	fclose(f);
	arrfree(objects);
}

// ------------------- Preview canvas -------------------

float Smooth(float x) {
	return x * x * (3 - 2 * x);
}

void ApplyKeyframeOverrides(const Keyframe *keyframe, const PathToOffset *pathToOffsetList, const RfData data) {
	for (uintptr_t j = 0; j < arrlenu(keyframe->properties); j++) {
		const Property *property = keyframe->properties + j;
		bool found = false;

		for (uintptr_t k = 0; k < arrlenu(pathToOffsetList); k++) {
			const PathToOffset *pathToOffset = pathToOffsetList + k;

			if (!ArePathsEqual(pathToOffset->path, property->path + 1)) {
				continue;
			}

			found = true;

			RfItem item;
			Keyframe *previousKeyframe = selected.keyframe;
			selected.keyframe = (Keyframe *) keyframe; // HACK!
			void *source = ResolveDataObject((RfPath *) property->path, &item);
			selected.keyframe = previousKeyframe;
			void *destination = (uint8_t *) data.buffer + pathToOffset->offset;

			if (pathToOffset->offset + item.byteCount > data.byteCount) {
				break;
			}

			if (item.type->op == StyleColorOp) {
				*(uint32_t *) destination = ColorLookup(*(uint32_t *) source);
			} else {
				memcpy(destination, source, item.byteCount);
			}

			break;
		}

		// If it wasn't found, that's fine.
		// e.g. deleting gradient stop, but a keyframe still has an override.
		(void) found;
	}
}

void AnimatingValueCalculate(AnimatingValue *value) {
	float progress = value->duration ? Smooth((float) value->elapsed / value->duration) : 1;

	if (value->type == ANIMATING_VALUE_TYPE_COLOR) {
		uint32_t from = value->from.u32;
		float fr = UI_COLOR_RED_F(from);
		float fg = UI_COLOR_GREEN_F(from);
		float fb = UI_COLOR_BLUE_F(from);
		float fa = UI_COLOR_ALPHA_F(from);
		uint32_t to = value->to.u32;
		float tr = UI_COLOR_RED_F(to);
		float tg = UI_COLOR_GREEN_F(to);
		float tb = UI_COLOR_BLUE_F(to);
		float ta = UI_COLOR_ALPHA_F(to);
		if (!fa) fr = tr, fg = tg, fb = tb;
		if (!ta) tr = fr, tg = fg, tb = fb;
		float dr = (tr - fr) * progress + fr;
		float dg = (tg - fg) * progress + fg;
		float db = (tb - fb) * progress + fb;
		float da = (ta - fa) * progress + fa;
		value->from.u32 = UI_COLOR_FROM_RGBA_F(dr, dg, db, da);
	} else if (value->type == ANIMATING_VALUE_TYPE_I8) {
		value->from.i8 = (value->to.i8 - value->from.i8) * progress + value->from.i8;
	} else if (value->type == ANIMATING_VALUE_TYPE_I16) {
		value->from.i16 = (value->to.i16 - value->from.i16) * progress + value->from.i16;
	} else if (value->type == ANIMATING_VALUE_TYPE_FLOAT) {
		value->from.f32 = (value->to.f32 - value->from.f32) * progress + value->from.f32;
	} else {
		assert(false);
	}
}

SequenceStateSelector GetCurrentSequenceStateSelector() {
	SequenceStateSelector s = { 0 };
	s.primary = previewPrimaryState;
	s.focused = previewStateFocused->e.flags & UI_BUTTON_CHECKED;
	s.checked = previewStateChecked->e.flags & UI_BUTTON_CHECKED;
	s.indeterminate = previewStateIndeterminate->e.flags & UI_BUTTON_CHECKED;
	s._default = previewStateDefault->e.flags & UI_BUTTON_CHECKED;
	s.itemFocus = previewStateItemFocus->e.flags & UI_BUTTON_CHECKED;
	s.listFocus = previewStateListFocus->e.flags & UI_BUTTON_CHECKED;
	s.selected = previewStateSelected->e.flags & UI_BUTTON_CHECKED;
	s.enter = previewStateBeforeEnter->e.flags & UI_BUTTON_CHECKED;
	s.exit = previewStateAfterExit->e.flags & UI_BUTTON_CHECKED;
	return s;
}

bool SequenceMatchesPreviewState(Sequence *sequence, SequenceStateSelector selector) {
	return (sequence->primaryState == selector.primary || sequence->primaryState == PRIMARY_STATE_ANY)
		&& (!sequence->flagFocused || selector.focused)
		&& (!sequence->flagChecked || selector.checked)
		&& (!sequence->flagIndeterminate || selector.indeterminate)
		&& (!sequence->flagDefault || selector._default)
		&& (!sequence->flagItemFocus || selector.itemFocus)
		&& (!sequence->flagListFocus || selector.listFocus)
		&& (!sequence->flagBeforeEnter || selector.enter)
		&& (!sequence->flagAfterExit || selector.exit)
		&& (!sequence->flagSelected || selector.selected);
}

void ApplySequenceOverrides(Layer *layer, PathToOffset *pathToOffsetList, RfData data, SequenceStateSelector selector) {
	for (uintptr_t j = 0; j < arrlenu(layer->sequences); j++) {
		if (SequenceMatchesPreviewState(layer->sequences[j], selector)) {
			for (uintptr_t k = 0; k < arrlenu(layer->sequences[j]->keyframes); k++) {
				Layer *previousLayer = selected.layer;
				selected.layer = layer; // HACK!
				ApplyKeyframeOverrides(layer->sequences[j]->keyframes[k], pathToOffsetList, data);
				selected.layer = previousLayer;
			}
		}
	}
}

void *PrepareThemeDataForLayer(EsBuffer *themeData, EsPainter *themePainter, Layer *layer, bool applyOverrides, uintptr_t index, UIPainter *painter) {
	PathToOffset *pathToOffsetList = NULL;
	RfData data = ExportToGrowableBuffer(&Layer_Type, sizeof(Layer), NULL, layer, &pathToOffsetList);

	if (applyOverrides) {
		Keyframe *keyframe = NULL;

		if (layer == selected.layer && selected.keyframe) {
			keyframe = selected.keyframe;
		}

		if (keyframe) {
			ApplyKeyframeOverrides(keyframe, pathToOffsetList, data);
		} else {
			ApplySequenceOverrides(layer, pathToOffsetList, data, currentStateSelector);

			for (uintptr_t j = 0; j < arrlenu(animatingValues); j++) {
				if (animatingValues[j].layer != index) {
					continue;
				}

				AnimatingValue value = animatingValues[j];
				uint8_t *destination = (uint8_t *) data.buffer + value.offset;
				AnimatingValueCalculate(&value);

				if (value.type == ANIMATING_VALUE_TYPE_COLOR) {
					*(uint32_t *) destination = value.from.u32;
				} else if (value.type == ANIMATING_VALUE_TYPE_I8) {
					*(int8_t *) destination = value.from.i8;
				} else if (value.type == ANIMATING_VALUE_TYPE_I16) {
					*(int16_t *) destination = value.from.i16;
				} else if (value.type == ANIMATING_VALUE_TYPE_FLOAT) {
					*(float *) destination = value.from.f32;
				} else {
					assert(false);
				}
			}
		}
	}

	ExportFreePathToOffsetList(pathToOffsetList);

	themeData->in = data.buffer;
	themeData->bytes = data.byteCount;
	themePainter->clip.l = painter->clip.l;
	themePainter->clip.r = painter->clip.r;
	themePainter->clip.t = painter->clip.t;
	themePainter->clip.b = painter->clip.b;
	themePainter->target->bits = painter->bits;
	themePainter->target->width = painter->width;
	themePainter->target->height = painter->height;
	themePainter->target->stride = painter->width * 4;
	return data.buffer;
}

void DrawStyle(UIPainter *painter, UIRectangle generalBounds, UIRectangle *globalOffset, float scale, UIRectangle *opaqueRegion, bool applyOverrides, Style *style) {
	for (uintptr_t i = 0; i < arrlenu(style->layers); i++) {
		Layer *layer = LayerLookup(style->layers[i]);
		EsPaintTarget paintTarget = { 0 };
		EsPainter themePainter = { 0 };
		themePainter.target = &paintTarget;
		EsBuffer themeData = { 0 };
		void *dataBuffer = PrepareThemeDataForLayer(&themeData, &themePainter, layer, applyOverrides, i, painter);

		if (i) {
			UIRectangle bounds = UIRectangleAdd(generalBounds, *globalOffset);
			EsRectangle bounds2 = { bounds.l, bounds.r, bounds.t, bounds.b };
			ThemeDrawLayer(&themePainter, bounds2, &themeData, scale, *(EsRectangle *) opaqueRegion);
		} else {
			EsBufferRead(&themeData, sizeof(ThemeLayer));
			const ThemeMetrics *metrics = (const ThemeMetrics *) EsBufferRead(&themeData, sizeof(ThemeMetrics));
			globalOffset->l = metrics->globalOffset.l * scale;
			globalOffset->r = metrics->globalOffset.r * scale;
			globalOffset->t = metrics->globalOffset.t * scale;
			globalOffset->b = metrics->globalOffset.b * scale;
		}

		free(dataBuffer);
	}

	if (editPoints->e.flags & UI_BUTTON_CHECKED) {
		for (uintptr_t i = 0; i < arrlenu(style->layers); i++) {
			Layer *_layer = LayerLookup(style->layers[i]);

			if (_layer->base.tag != LayerBase_path + 1 || _layer != selected.layer) {
				continue;
			}

			EsPaintTarget paintTarget = { 0 };
			EsPainter themePainter = { 0 };
			themePainter.target = &paintTarget;
			EsBuffer themeData = { 0 };
			void *dataBuffer = PrepareThemeDataForLayer(&themeData, &themePainter, _layer, applyOverrides, i, painter);

			const ThemeLayer *layer = (const ThemeLayer *) EsBufferRead(&themeData, sizeof(ThemeLayer));
			UIRectangle _bounds = UIRectangleAdd(generalBounds, *globalOffset), bounds;
			bounds.l = _bounds.l + (int) (scale * layer->offset.l) + THEME_RECT_WIDTH(_bounds)  * layer->position.l / 100;
			bounds.r = _bounds.l + (int) (scale * layer->offset.r) + THEME_RECT_WIDTH(_bounds)  * layer->position.r / 100;
			bounds.t = _bounds.t + (int) (scale * layer->offset.t) + THEME_RECT_HEIGHT(_bounds) * layer->position.t / 100;
			bounds.b = _bounds.t + (int) (scale * layer->offset.b) + THEME_RECT_HEIGHT(_bounds) * layer->position.b / 100;

			const ThemeLayerPath *path = (const ThemeLayerPath *) EsBufferRead(&themeData, sizeof(ThemeLayerPath));
			const float *points = (const float *) EsBufferRead(&themeData, sizeof(float) * 6 * path->pointCount);

#if 0
			for (uintptr_t i = 0; i < path->pointCount * 6; i += 2) {
				intptr_t k = (i / 2) % 3;
				if (k == 0) continue;
				float x = points[i + 0], y = points[i + 1];
				intptr_t m = k == 2 ? (i + 2) : (i - 2);
				if (m < 0) m += path->pointCount * 6;
				if (m == path->pointCount * 6) m = 0;
				float mx = points[m + 0], my = points[m + 1];

				x = UI_RECT_WIDTH(bounds) * x / 100.0f + bounds.l;
				y = UI_RECT_HEIGHT(bounds) * y / 100.0f + bounds.t;
				mx = UI_RECT_WIDTH(bounds) * mx / 100.0f + bounds.l;
				my = UI_RECT_HEIGHT(bounds) * my / 100.0f + bounds.t;

				UIDrawLine(painter, x, y, mx, my, 0xFF000000);
			}

			for (uintptr_t i = 0; i < path->pointCount * 6; i += 2) {
				intptr_t k = (i / 2) % 3;
				if (k == 0) continue;
				float x = points[i + 0], y = points[i + 1];

				x = UI_RECT_WIDTH(bounds) * x / 100.0f + bounds.l;
				y = UI_RECT_HEIGHT(bounds) * y / 100.0f + bounds.t;

				UIDrawRectangle(painter, UI_RECT_4(x - 6, x + 6, y - 6, y + 6), 
						0xFFE0E0E0, 0xFFA8A8A8, UI_RECT_1(1));
			}
#endif

			for (uintptr_t i = 0; i < path->pointCount * 6; i += 2) {
				intptr_t k = (i / 2) % 3;
				if (k != 0) continue;
				float x = points[i + 0], y = points[i + 1];

				x = UI_RECT_WIDTH(bounds) * x / 100.0f + bounds.l;
				y = UI_RECT_HEIGHT(bounds) * y / 100.0f + bounds.t;

				UIDrawRectangle(painter, UI_RECT_4(x - 6, x + 6, y - 6, y + 6), 
						0xFFFFFFFF, 0xFFA8A8A8, UI_RECT_1(1));
			}

			free(dataBuffer);
		}
	}
}

int CanvasMessage(UIElement *element, UIMessage message, int di, void *dp) {
#define CANVAS_DRAW_BOUNDS() \
	float scale = previewScale->position * 4 + 1; \
	int drawX = 100 + elementCanvas->bounds.l; \
	int drawY = 100 + elementCanvas->bounds.t; \
	int drawWidth = 1000 * previewWidth->position * scale; \
	int drawHeight = 1000 * previewHeight->position * scale

	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		uint32_t background;
		UIColorToRGB(previewBackgroundColor->hue, previewBackgroundColor->saturation, previewBackgroundColor->value, &background);
		UIDrawBlock(painter, element->bounds, background | 0xFF000000);

		CANVAS_DRAW_BOUNDS();

		UIRectangle generalBounds = UI_RECT_4(drawX, drawX + drawWidth, drawY, drawY + drawHeight);

		if (!selected.style) {
			for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
				UIRectangle iBounds = UIRectangleAdd(generalBounds, UI_RECT_1I(-10));

				if (UIRectangleContains(iBounds, element->window->cursorX, element->window->cursorY)) {
					UIDrawString(painter, UI_RECT_4(element->bounds.l, element->bounds.r, element->bounds.t, element->bounds.t + 25), 
						styleSet.styles[i]->name.buffer, styleSet.styles[i]->name.byteCount, 0x000000, UI_ALIGN_LEFT, NULL);
					UIDrawBlock(painter, iBounds, 0xFFA2A0A4);
				}

				UIRectangle opaqueRegion = { 0 };
				UIRectangle globalOffset = { 0 };
				DrawStyle(painter, generalBounds, &globalOffset, scale, &opaqueRegion, false, styleSet.styles[i]);

				if (generalBounds.r + drawWidth > elementCanvas->bounds.r - 100) {
					generalBounds.l = 100 + elementCanvas->bounds.l;
					generalBounds.r = generalBounds.l + drawWidth;
					generalBounds.t += drawHeight + 20;
					generalBounds.b += drawHeight + 20;
				} else {
					generalBounds.l += drawWidth + 20;
					generalBounds.r += drawWidth + 20;
				}
			}

			// UIDrawString(painter, element->bounds, "(select a style to preview it)", -1, 0x000000, UI_ALIGN_CENTER, NULL);
			return 0;
		}

		if (!arrlenu(selected.style->layers)) {
			UIDrawString(painter, element->bounds, "(selected style has no layers)", -1, 0x000000, UI_ALIGN_CENTER, NULL);
			return 0;
		}

		if (previewShowGuides->e.flags & UI_BUTTON_CHECKED) {
			UIDrawBlock(painter, UIRectangleAdd(generalBounds, UI_RECT_1I(-2)), 0xFFA2A0A4);
			UIDrawBlock(painter, UIRectangleAdd(generalBounds, UI_RECT_1I(0)), 0xFFC2C0C4);
		}

		Rectangle8 opaqueInsets = StyleCalculateOpaqueInsets(selected.style->layers);
		UIRectangle opaqueRegion = { 0 };
		
		if (opaqueInsets.l != 0x7F && opaqueInsets.r != 0x7F
				&& opaqueInsets.t != 0x7F && opaqueInsets.b != 0x7F) {
			opaqueRegion = UIRectangleAdd(generalBounds, UI_RECT_4(opaqueInsets.l * scale, -opaqueInsets.r * scale, 
					opaqueInsets.t * scale, -opaqueInsets.b * scale));
		}

		UIRectangle globalOffset = { 0 };

		DrawStyle(painter, generalBounds, &globalOffset, scale, &opaqueRegion, true, selected.style);

		if (previewShowComputed->e.flags & UI_BUTTON_CHECKED) {
			Rectangle8 paintOutsets8 = StyleCalculatePaintOutsets(selected.style->layers);
			UIRectangle paintOutsets = UI_RECT_4(-paintOutsets8.l * scale, paintOutsets8.r * scale, -paintOutsets8.t * scale, paintOutsets8.b * scale);
			UIDrawBlock(painter, UIRectangleAdd(generalBounds, paintOutsets), 0xFF0000);

			Rectangle8 opaqueInsets8 = StyleCalculateOpaqueInsets(selected.style->layers);

			if (opaqueInsets8.l != 0x7F && opaqueInsets8.r != 0x7F && opaqueInsets8.t != 0x7F && opaqueInsets8.b != 0x7F) {
				UIRectangle opaqueInsets = UI_RECT_4(opaqueInsets8.l * scale, -opaqueInsets8.r * scale, opaqueInsets8.t * scale, -opaqueInsets8.b * scale);
				UIDrawBlock(painter, UIRectangleAdd(generalBounds, opaqueInsets), 0x00FF00);
			}
		}

		{
			char buffer[128];
			snprintf(buffer, 128, "%dx%dpx at %d%% scale", (int) (1000 * previewWidth->position), (int) (1000 * previewHeight->position), (int) (100 * scale));
			UIRectangle bounds = element->bounds;
			bounds.b = bounds.t + 16;
			UIDrawString(painter, bounds, buffer, -1, 0x000000, UI_ALIGN_LEFT, NULL);
		}
	} else if (message == UI_MSG_ANIMATE) {
		uint64_t current = UIAnimateClock();
		uint64_t delta = current - previewTransitionLastTime;

		for (uintptr_t i = 0; i < arrlenu(animatingValues); i++) {
			animatingValues[i].elapsed += delta;

			if (animatingValues[i].elapsed >= animatingValues[i].duration) {
				animatingValues[i].elapsed = animatingValues[i].duration;
			}
		}

		previewTransitionLastTime = current;
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_MOUSE_MOVE) {
		if (!selected.style) {
			UIElementRepaint(element, NULL);
		}
	}

	return 0;
}

void PreviewTransitionInvoke(void *_unused) {
	previewTransition->e.flags ^= UI_BUTTON_CHECKED;

	if (previewTransition->e.flags & UI_BUTTON_CHECKED) {
		UIElementAnimate(elementCanvas, false);
		previewTransitionLastTime = UIAnimateClock();
	} else {
		UIElementAnimate(elementCanvas, true);
		arrfree(animatingValues);
	}

	UIElementRepaint(&previewTransition->e, NULL);
	UIElementRepaint(elementCanvas, NULL);
}

void PreviewPreferredSizeInvoke(void *_unused) {
	previewWidth->position = selected.style ? LayerLookup(selected.style->layers[0])->base.metrics.preferredSize.width / 1000.0f : 0.1f;
	previewHeight->position = selected.style ? LayerLookup(selected.style->layers[0])->base.metrics.preferredSize.height / 1000.0f : 0.1f;

	UIElementRefresh(&previewWidth->e);
	UIElementRefresh(&previewHeight->e);
	UIElementRepaint(elementCanvas, NULL);
}

void PreviewFixAspectRatioInvoke(void *_unused) {
	previewHeight->position = previewWidth->position;
	previewFixAspectRatio->e.flags ^= UI_BUTTON_CHECKED;
	previewHeight->e.flags ^= UI_ELEMENT_HIDE;
	UIElementRefresh(&previewFixAspectRatio->e);
	UIElementRefresh(&previewHeight->e);
	UIElementRepaint(elementCanvas, NULL);
}

void PreviewShowGuidesInvoke(void *_unused) {
	previewShowGuides->e.flags ^= UI_BUTTON_CHECKED;
	UIElementRefresh(&previewShowGuides->e);
	UIElementRepaint(elementCanvas, NULL);
}

void PreviewShowComputedInvoke(void *_unused) {
	previewShowComputed->e.flags ^= UI_BUTTON_CHECKED;
	UIElementRefresh(&previewShowComputed->e);
	UIElementRepaint(elementCanvas, NULL);
}

void EditPointsInvoke(void *_unused) {
	editPoints->e.flags ^= UI_BUTTON_CHECKED;
	UIElementRefresh(&editPoints->e);
	UIElementRepaint(elementCanvas, NULL);
}

int PreviewSliderMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_VALUE_CHANGED) {
		if (previewFixAspectRatio->e.flags & UI_BUTTON_CHECKED) {
			previewHeight->position = previewWidth->position;
		}
		UIElementRepaint(elementCanvas, NULL);
	}

	return 0;
}

void UpdateAnimationListWithSequence(Sequence *sequence, int layer, PathToOffset *pathToOffsetList, uint8_t *data) {
	for (uintptr_t i = 0; i < arrlenu(sequence->keyframes); i++) {
		Keyframe *keyframe = sequence->keyframes[i];

		for (uintptr_t j = 0; j < arrlenu(keyframe->properties); j++) {
			Property *property = keyframe->properties + j;

			uint16_t offset = 0xFFFF;

			for (uintptr_t k = 0; k < arrlenu(pathToOffsetList); k++) {
				const PathToOffset *pathToOffset = pathToOffsetList + k;

				if (!ArePathsEqual(pathToOffset->path, property->path + 1)) {
					continue;
				}

				offset = pathToOffset->offset;
				assert(pathToOffset->offset < 0xFFFF);
				break;
			}

			if (offset == 0xFFFF) {
				continue;
			}

			// TODO Binary search.

			uintptr_t point = 0;
			bool found = false;

			for (uintptr_t k = 0; k < arrlenu(animatingValues); k++) {
				if (animatingValues[k].layer < layer || animatingValues[k].offset < offset) {
					point = k + 1;
				} else if (animatingValues[k].layer == layer && animatingValues[k].offset == offset) {
					found = true;
					point = k;
					break;
				}
			}

			AnimatingValue *value;

			if (found) {
				value = animatingValues + point;
				value->elapsed = 0;
				value->duration = sequence->duration;
			} else {
				AnimatingValue _value = { 0 };
				_value.offset = offset;
				_value.layer = layer;
				_value.duration = sequence->duration;
				arrins(animatingValues, point, _value);
				value = animatingValues + point;
			}

			RfItem item;
			void *source;

			{
				Keyframe *previousKeyframe = selected.keyframe;
				selected.keyframe = keyframe; // HACK!
				source = ResolveDataObject((RfPath *) property->path, &item);
				selected.keyframe = previousKeyframe;
			}

			if (item.type == &StyleI8_Type) {
				value->type = ANIMATING_VALUE_TYPE_I8;
				if (!found) value->from.i8 = *(int8_t *) (data + offset);
				value->to.i8 = *(int8_t *) source;
			} else if (item.type == &StyleI16_Type) {
				value->type = ANIMATING_VALUE_TYPE_I16;
				if (!found) value->from.i16 = *(int16_t *) (data + offset);
				value->to.i16 = *(int16_t *) source;
			} else if (item.type == &StyleColor_Type) {
				value->type = ANIMATING_VALUE_TYPE_COLOR;
				if (!found) value->from.u32 = *(uint32_t *) (data + offset);
				value->to.u32 = ColorLookup(*(uint32_t *) source);
			} else if (item.type == &StyleFloat_Type) {
				value->type = ANIMATING_VALUE_TYPE_FLOAT;
				if (!found) value->from.f32 = *(float *) (data + offset);
				value->to.f32 = *(float *) source;
			} else {
				assert(false);
			}
		}
	}
}

void UpdateAnimationList() {
	if (!selected.style) return;

	SequenceStateSelector oldStateSelector = currentStateSelector;
	currentStateSelector = GetCurrentSequenceStateSelector();

	for (uintptr_t i = 0; i < arrlenu(animatingValues); i++) {
		AnimatingValueCalculate(animatingValues + i);
		animatingValues[i].type |= ANIMATING_VALUE_TYPE_UNUSED;
	}

	for (uintptr_t i = 0; i < arrlenu(selected.style->layers); i++) {
		Layer *layer = LayerLookup(selected.style->layers[i]);

		PathToOffset *pathToOffsetList = NULL;
		RfData data = ExportToGrowableBuffer(&Layer_Type, sizeof(Layer), NULL, layer, &pathToOffsetList);
		ApplySequenceOverrides(layer, pathToOffsetList, data, oldStateSelector); 

		PathToOffset *pathToOffsetList2 = NULL;
		RfData data2 = ExportToGrowableBuffer(&Layer_Type, sizeof(Layer), NULL, layer, &pathToOffsetList2);
		ApplySequenceOverrides(layer, pathToOffsetList2, data2, currentStateSelector); 

		for (uintptr_t j = 0; j < arrlenu(layer->sequences); j++) {
			Sequence *sequence = layer->sequences[j];

			if (SequenceMatchesPreviewState(sequence, currentStateSelector)) {
				Layer *previousLayer = selected.layer;
				selected.layer = layer; // HACK!
				UpdateAnimationListWithSequence(sequence, i, pathToOffsetList, (uint8_t *) data.buffer);
				selected.layer = previousLayer;
			}
		}

		for (uintptr_t j = 0; j < arrlenu(animatingValues); j++) {
			AnimatingValue *value = animatingValues + j;

			if ((value->type & ANIMATING_VALUE_TYPE_UNUSED) && value->layer == i) {
				// Return values to base.

				value->type &= ~ANIMATING_VALUE_TYPE_UNUSED;
				value->elapsed = 0;
				uint8_t *source = ((uint8_t *) data2.buffer + value->offset);

				if (value->type == ANIMATING_VALUE_TYPE_I8) {
					value->to.i8 = *(int8_t *) source;
				} else if (value->type == ANIMATING_VALUE_TYPE_I16) {
					value->to.i16 = *(int16_t *) source;
				} else if (value->type == ANIMATING_VALUE_TYPE_COLOR) {
					value->to.u32 = *(uint32_t *) source;
				} else if (value->type == ANIMATING_VALUE_TYPE_FLOAT) {
					value->to.f32 = *(float *) source;
				} else {
					assert(false);
				}
			}
		}

		ExportFreePathToOffsetList(pathToOffsetList);
		ExportFreePathToOffsetList(pathToOffsetList2);
		free(data.buffer);
		free(data2.buffer);
	}
}

void PreviewSetPrimaryState(void *cp) {
	previewPrimaryState = (uintptr_t) cp;

	UIElement *child = previewPrimaryStatePanel->e.children;

	while (child) {
		if (child->cp == cp) child->flags |= UI_BUTTON_CHECKED;
		else child->flags &= ~UI_BUTTON_CHECKED;
		child = child->next;
	}

	UIElementRefresh(&previewPrimaryStatePanel->e);
	UpdateAnimationList();
}

int PreviewToggleState(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		element->flags ^= UI_BUTTON_CHECKED;
		UIElementRefresh(element);
		UpdateAnimationList();
	}

	return 0;
}

int PreviewChangeBackgroundColor(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_VALUE_CHANGED) {
		UIElementRefresh(elementCanvas);
	}

	return 0;
}

// ------------------- Paths -------------------

void MakeUI(MakeUIState *state, RfItem *item, void *pointer) {
	RfIterator iterator = { 0 };
	iterator.includeRemovedFields = true;
	iterator.s.op = RF_OP_COUNT;
	item->type->op(&iterator.s, item, pointer);
	iterator.s.op = RF_OP_ITERATE;
	uint32_t count = iterator.index;

	for (uint32_t i = 0; i < count; i++) {
		iterator.index = i;
		item->type->op(&iterator.s, item, pointer);
		if (iterator.s.error) return;
		MakeUIState s = { 0 };
		s.s = state->s;
		s.index = i;
		s.parent = state;
		s.inKeyframe = state->inKeyframe;
		s.recurse = true;
		if (iterator.isRemoved) continue;
		iterator.item.type->op(&s.s, &iterator.item, iterator.pointer);
		if (s.recurse) MakeUI(&s, &iterator.item, iterator.pointer);
	}
}

void MakeHeaderAndIndentUI(const char *format, RfState *state, RfItem *item, void *pointer) {
	char buffer[64];
	snprintf(buffer, sizeof(buffer), format, ((MakeUIState *) state)->index + 1);

#if 0
	UILabelCreate(0, 0, buffer, -1);
	UIPanel *subPanel = UIPanelCreate(0, UI_PANEL_EXPAND | UI_ELEMENT_PARENT_PUSH);
	subPanel->gap = 5;
	subPanel->border.l = 20;
	subPanel->border.b = 20;
#endif

	UIExpandPane *pane = UIExpandPaneCreate(0, UI_ELEMENT_PARENT_PUSH, buffer, -1, UI_PANEL_EXPAND);
	pane->panel->gap = 5;
	pane->panel->border.l = 20;
	pane->panel->border.t = 5;
	pane->panel->border.r = 5;
	pane->panel->border.b = 20;

	MakeUI((MakeUIState *) state, item, pointer);
	UIParentPop();

	((MakeUIState *) state)->recurse = false;
}

void InspectorUnsubscribe(UIElement *element) {
	for (uintptr_t i = 0; i < arrlenu(inspectorSubscriptions); i++) {
		if (inspectorSubscriptions[i] == element) {
			arrdel(inspectorSubscriptions, i);
			free(element->cp);
			return;
		}
	}

	assert(false);
}

void BuildPathForUI(MakeUIState *state, uint32_t last, UIElement *element) {
	int count = 1;

	if (state->inKeyframe) {
		count++;
	}

	MakeUIState *s = state;

	while (s) {
		count++;

		if (!s->parent && s->basePath) {
			for (uintptr_t i = 0; s->basePath[i] != RF_PATH_TERMINATOR; i++) {
				count++;
			}
		}

		s = s->parent;
	}

	RfPath *path = (RfPath *) malloc((count + 1) * sizeof(uint32_t));

	path->indices[count - 1] = last;
	path->indices[count] = RF_PATH_TERMINATOR;

	s = state;
	count--;

	while (s) {
		path->indices[--count] = s->index;

		if (!s->parent && s->basePath) {
			for (uintptr_t i = 0; s->basePath[i] != RF_PATH_TERMINATOR; i++) {
				path->indices[state->inKeyframe ? (i + 1) : i] = s->basePath[i];
				count--;
			}
		}

		s = s->parent;
	}

	if (state->inKeyframe) {
		path->indices[--count] = PATH_IN_KEYFRAME;
	}

	assert(!count);
	element->cp = path;
	arrput(inspectorSubscriptions, element);
	UIElementMessage(element, MSG_PROPERTY_CHANGED, 0, ResolveDataObject(path, NULL));
}

// ------------------- Inspector -------------------

int StyleI8Message(UIElement *element, UIMessage message, int di, void *dp) {
	UITextbox *textbox = (UITextbox *) element;

	if (message == UI_MSG_DESTROY) {
		InspectorUnsubscribe(element);
	} else if (message == UI_MSG_VALUE_CHANGED) {
		textbox->string = realloc(textbox->string, textbox->bytes + 1);
		textbox->string[textbox->bytes] = 0;
		int8_t newValue = atoi(textbox->string);

		ModData mod = { 0 };
		mod.tag = ModData_changeProperty + 1;
		mod.changeProperty.property.path = DuplicatePath((uint32_t *) element->cp);
		mod.changeProperty.property.data = SaveToGrowableBuffer(&StyleI8_Type, sizeof(newValue), NULL, &newValue);
		mod.changeProperty.source = element;
		ModApply(&mod);
	} else if (message == MSG_PROPERTY_CHANGED) {
		char buffer[16];
		textbox->carets[0] = 0;
		textbox->carets[1] = textbox->bytes;
		UITextboxReplace(textbox, buffer, snprintf(buffer, 16, "%d", *(int8_t *) dp), false);
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_UPDATE && di == UI_UPDATE_FOCUSED) {
		if (element->window->focused == element) {
			textbox->carets[0] = 0;
			textbox->carets[1] = textbox->bytes;
		} else {
			StyleI8Message(element, MSG_PROPERTY_CHANGED, 0, ResolveDataObject((RfPath *) element->cp, NULL));
		}
	}

	return 0;
}

int StyleI16Message(UIElement *element, UIMessage message, int di, void *dp) {
	UITextbox *textbox = (UITextbox *) element;

	if (message == UI_MSG_DESTROY) {
		InspectorUnsubscribe(element);
	} else if (message == UI_MSG_VALUE_CHANGED) {
		textbox->string = realloc(textbox->string, textbox->bytes + 1);
		textbox->string[textbox->bytes] = 0;
		int16_t newValue = atoi(textbox->string);

		ModData mod = { 0 };
		mod.tag = ModData_changeProperty + 1;
		mod.changeProperty.property.path = DuplicatePath((uint32_t *) element->cp);
		mod.changeProperty.property.data = SaveToGrowableBuffer(&StyleI16_Type, sizeof(newValue), NULL, &newValue);
		mod.changeProperty.source = element;
		ModApply(&mod);
	} else if (message == MSG_PROPERTY_CHANGED) {
		char buffer[16];
		textbox->carets[0] = 0;
		textbox->carets[1] = textbox->bytes;
		UITextboxReplace(textbox, buffer, snprintf(buffer, 16, "%d", *(int16_t *) dp), false);
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_UPDATE && di == UI_UPDATE_FOCUSED) {
		if (element->window->focused == element) {
			textbox->carets[0] = 0;
			textbox->carets[1] = textbox->bytes;
		} else {
			StyleI16Message(element, MSG_PROPERTY_CHANGED, 0, ResolveDataObject((RfPath *) element->cp, NULL));
		}
	}

	return 0;
}

int StyleFloatMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITextbox *textbox = (UITextbox *) element;

	if (message == UI_MSG_DESTROY) {
		InspectorUnsubscribe(element);
	} else if (message == UI_MSG_VALUE_CHANGED) {
		textbox->string = realloc(textbox->string, textbox->bytes + 1);
		textbox->string[textbox->bytes] = 0;
		float newValue = strtof(textbox->string, NULL);

		ModData mod = { 0 };
		mod.tag = ModData_changeProperty + 1;
		mod.changeProperty.property.path = DuplicatePath((uint32_t *) element->cp);
		mod.changeProperty.property.data = SaveToGrowableBuffer(&StyleFloat_Type, sizeof(newValue), NULL, &newValue);
		mod.changeProperty.source = element;
		ModApply(&mod);
	} else if (message == MSG_PROPERTY_CHANGED) {
		char buffer[16];
		textbox->carets[0] = 0;
		textbox->carets[1] = textbox->bytes;
		UITextboxReplace(textbox, buffer, snprintf(buffer, 16, "%.2f", *(float *) dp), false);
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_UPDATE && di == UI_UPDATE_FOCUSED) {
		if (element->window->focused == element) {
			textbox->carets[0] = 0;
			textbox->carets[1] = textbox->bytes;
		} else {
			StyleFloatMessage(element, MSG_PROPERTY_CHANGED, 0, ResolveDataObject((RfPath *) element->cp, NULL));
		}
	}

	return 0;
}

int StyleStringMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITextbox *textbox = (UITextbox *) element;

	if (message == UI_MSG_DESTROY) {
		InspectorUnsubscribe(element);
	} else if (message == UI_MSG_VALUE_CHANGED) {
		RfData data = { 0 };
		data.buffer = textbox->string;
		data.byteCount = textbox->bytes;

		ModData mod = { 0 };
		mod.tag = ModData_changeProperty + 1;
		mod.changeProperty.property.path = DuplicatePath((uint32_t *) element->cp);
		mod.changeProperty.property.data = SaveToGrowableBuffer(&StyleString_Type, sizeof(data), NULL, &data);
		mod.changeProperty.source = element;
		ModApply(&mod);
	} else if (message == MSG_PROPERTY_CHANGED) {
		RfData *data = (RfData *) dp;
		textbox->carets[0] = 0;
		textbox->carets[1] = textbox->bytes;
		UITextboxReplace(textbox, data->buffer, data->byteCount, false);
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_UPDATE && di == UI_UPDATE_FOCUSED && element->window->focused == element) {
		textbox->carets[0] = 0;
		textbox->carets[1] = textbox->bytes;
	}

	return 0;
}

int StyleBoolMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		InspectorUnsubscribe(element);
	} else if (message == UI_MSG_CLICKED) {
		element->flags ^= UI_BUTTON_CHECKED;
		bool newValue = element->flags & UI_BUTTON_CHECKED;

		ModData mod = { 0 };
		mod.tag = ModData_changeProperty + 1;
		mod.changeProperty.property.path = DuplicatePath((uint32_t *) element->cp);
		mod.changeProperty.property.data = SaveToGrowableBuffer(&StyleBool_Type, sizeof(newValue), NULL, &newValue);
		mod.changeProperty.source = element;
		ModApply(&mod);
	} else if (message == MSG_PROPERTY_CHANGED) {
		if (*(bool *) dp) element->flags |= UI_BUTTON_CHECKED;
		else element->flags &= ~UI_BUTTON_CHECKED;
		UIElementRepaint(element, NULL);
	}

	return 0;
}

int StyleUnionButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	// Also used for enums.

	if (message == UI_MSG_CLICKED && (~element->flags & UI_BUTTON_CHECKED)) {
		uint32_t newValue = (uint32_t) (uintptr_t) element->cp;

		ModData mod = { 0 };
		mod.tag = ModData_changeProperty + 1;
		mod.changeProperty.property.path = DuplicatePath((uint32_t *) element->parent->cp);
		mod.changeProperty.property.data = SaveToGrowableBuffer(&rfU32, sizeof(newValue), NULL, &newValue);
		ModApply(&mod);
	}

	return 0;
}

void StyleEnumMenuItemInvoke(void *cp) {
	uint32_t newValue = (uint32_t) (uintptr_t) cp;

	ModData mod = { 0 };
	mod.tag = ModData_changeProperty + 1;
	mod.changeProperty.property.path = DuplicatePath(menuPath);
	mod.changeProperty.property.data = SaveToGrowableBuffer(&rfU32, sizeof(newValue), NULL, &newValue);
	ModApply(&mod);
}

int StyleUnionButtonPanelMessage(UIElement *element, UIMessage message, int di, void *dp) {
	// Also used for enums.

	if (message == UI_MSG_DESTROY) {
		InspectorUnsubscribe(element);
	} else if (message == MSG_PROPERTY_CHANGED) {
		UIElement *child = element->children;

		while (child) {
			if ((uint32_t) (uintptr_t) child->cp == *(uint32_t *) dp) {
				child->flags |= UI_BUTTON_CHECKED;
			} else {
				child->flags &= ~UI_BUTTON_CHECKED;
			}

			UIElementRepaint(child, NULL);
			child = child->next;
		}
	}

	return 0;
}

void RefreshAncestorsUntilInspector(UIElement *element) {
	while (element != &panelInspector->e) {
		element->clip = UI_RECT_1(0);
		element = element->parent;
	}

	UIElementRefresh(&panelInspector->e);
}

int StyleChoiceButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		InspectorUnsubscribe(element);
	} else if (message == UI_MSG_GET_WIDTH) {
		return 240 * element->window->scale;
	} else if (message == MSG_PROPERTY_CHANGED) {
		RfItem item;
		void *_dp = ResolveDataObject((RfPath *) element->cp, &item);
		assert(dp == _dp);
		UIButton *button = (UIButton *) element;
		free(button->label);
		button->label = UIStringCopy(((StringOption *) item.type->fields[*(uint32_t *) dp].item.options)->string, -1);
		UIElementRefresh(element);
	} else if (message == UI_MSG_CLICKED) {
		RfItem item;
		ResolveDataObject((RfPath *) element->cp, &item);
		UIMenu *menu = UIMenuCreate(element, 0);
		menuPath = (uint32_t *) element->cp;

		for (uintptr_t i = 0; i < item.type->fieldCount; i++) {
			UIMenuAddItem(menu, 0, ((StringOption *) item.type->fields[i].item.options)->string, -1, StyleEnumMenuItemInvoke, (void *) i);
		}

		UIMenuShow(menu);
	}

	return 0;
}

int StyleUnionPanelMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		InspectorUnsubscribe(element);
	} else if (message == MSG_PROPERTY_CHANGED) {
		UIElementDestroyDescendents(element);

		uint32_t *path = (uint32_t *) element->cp;
		uintptr_t last = 0;
		for (; path[last] != RF_PATH_TERMINATOR; last++);
		assert(last && path[last - 1] == 0);
		path[last - 1] = RF_PATH_TERMINATOR;
		RfItem item;
		void *_dp = ResolveDataObject((RfPath *) path, &item);
		assert(dp == _dp);

		MakeUIState state = { 0 }; 
		state.s.op = OP_MAKE_UI; 
		state.index = *(uint32_t *) dp; 
		state.basePath = element->cp;
		state.inKeyframe = state.basePath[0] == PATH_IN_KEYFRAME;
		if (state.inKeyframe) state.basePath++;
		if (state.index) state.index--;
		RfField *field = item.type->fields + state.index;
		RfItem fieldItem = field->item; 
		uint8_t *pointer = (uint8_t *) dp + field->offset;
		UIParentPush(element);
		fieldItem.type->op(&state.s, &fieldItem, pointer); 
		MakeUI(&state, &fieldItem, pointer); 
		UIParentPop();

		path[last - 1] = 0; // Restore previous path.
		RefreshAncestorsUntilInspector(element);
	}

	return 0;
}

int StyleArrayPanelMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		InspectorUnsubscribe(element);
	} else if (message == MSG_PROPERTY_CHANGED) {
		UIElementDestroyDescendents(element);

		uint32_t *path = (uint32_t *) element->cp;
		RfItem item;
		void *_dp = ResolveDataObject((RfPath *) path, &item);
		assert(dp == _dp);

		RfArrayHeader *header = *(RfArrayHeader **) dp;
		UIParentPush(element);

		for (uintptr_t i = 0; header && i < header[-1].length; i++) {
			MakeUIState state = { 0 }; 
			state.s.op = OP_MAKE_UI; 
			state.index = i; 
			state.basePath = element->cp;
			state.inKeyframe = state.basePath[0] == PATH_IN_KEYFRAME;
			if (state.inKeyframe) state.basePath++;
			RfItem *fieldItem = (RfItem *) item.options; 
			uint8_t *pointer = (*(uint8_t **) dp) + i * fieldItem->byteCount;
			state.recurse = true;
			fieldItem->type->op(&state.s, fieldItem, pointer); 
			if (state.recurse) MakeUI(&state, fieldItem, pointer); 
		}

		UIParentPop();
		RefreshAncestorsUntilInspector(element);
	}

	return 0;
}

int StyleArrayAddMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		ModData mod = { 0 };
		mod.tag = ModData_array + 1;
		mod.array.property.path = DuplicatePath((uint32_t *) ((UIElement *) element->cp)->cp);
		RfItem item;
		ResolveDataObject((RfPath *) mod.array.property.path, &item);
		RfItem *elementItem = (RfItem *) item.options;
		void *temporary = malloc(elementItem->byteCount);
		memset(temporary, 0, elementItem->byteCount);
		mod.changeProperty.property.data = SaveToGrowableBuffer(elementItem->type, elementItem->byteCount, elementItem->options, temporary);
		free(temporary);
		ModApply(&mod);
	}

	return 0;
}

int StyleArrayDeleteMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		uint32_t *path = (uint32_t *) ((UIElement *) element->cp)->cp;
		RfItem item;
		RfArrayHeader *pointer = *(RfArrayHeader **) ResolveDataObject((RfPath *) path, &item);

		if (pointer && pointer[-1].length) {
			ModData mod = { 0 };
			mod.tag = ModData_array + 1;
			mod.array.property.path = DuplicatePath(path);
			mod.array.isDelete = true;
			ModApply(&mod);
		}
	}

	return 0;
}

int RemoveOverrideButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		ModData mod = { 0 };
		mod.tag = ModData_deleteOverride + 1;
		mod.deleteOverride.property.path = DuplicatePath(element->cp);
		ModApply(&mod);
	} else if (message == MSG_PROPERTY_CHANGED) {
		if (dp == temporaryOverride) {
			element->flags &= ~UI_ELEMENT_DISABLED;
		} else {
			element->flags |= UI_ELEMENT_DISABLED;
		}

		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_DESTROY) {
		InspectorUnsubscribe(element);
	}

	return 0;
}

void MakeOverrideButton(UIElement *parent, RfState *state, uint32_t last) {
	if (!((MakeUIState *) state)->inKeyframe) return;
	UIButton *removeOverride = UIButtonCreate(parent, UI_BUTTON_SMALL, "X", -1);
	removeOverride->e.messageUser = RemoveOverrideButtonMessage;
	BuildPathForUI((MakeUIState *) state, last, &removeOverride->e);
}

void StyleColorMenuItemInvoke(void *cp) {
	uint32_t newValue = (uint32_t) (uintptr_t) cp;

	ModData mod = { 0 };
	mod.tag = ModData_changeProperty + 1;
	mod.changeProperty.property.path = DuplicatePath((uint32_t *) menuPath);
	mod.changeProperty.property.data = SaveToGrowableBuffer(&StyleColor_Type, sizeof(newValue), NULL, &newValue);
	ModApply(&mod);
}

int StyleColorButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		InspectorUnsubscribe(element);
	} else if (message == UI_MSG_GET_WIDTH) {
		return 240 * element->window->scale;
	} else if (message == MSG_PROPERTY_CHANGED) {
		RfItem item;
		void *_dp = ResolveDataObject((RfPath *) element->cp, &item);
		assert(dp == _dp);
		UIButton *button = (UIButton *) element;
		free(button->label);

		RfData key = ColorLookupPointer(*(uint32_t *) dp)->key;
		button->label = UIStringCopy(key.buffer, key.byteCount);

		UIElementRefresh(element);
	} else if (message == UI_MSG_CLICKED) {
		RfItem item;
		ResolveDataObject((RfPath *) element->cp, &item);
		UIMenu *menu = UIMenuCreate(element, 0);
		menuPath = (uint32_t *) element->cp;

		for (uintptr_t i = 0; i < arrlenu(styleSet.colors); i++) {
			UIMenuAddItem(menu, 0, styleSet.colors[i]->key.buffer, styleSet.colors[i]->key.byteCount, 
					StyleColorMenuItemInvoke, (void *) (uintptr_t) styleSet.colors[i]->id);
		}

		UIMenuShow(menu);
	}

	return 0;
}

void StyleColorRenameButtonCommand(void *cp) {
	UIButton *button = (UIButton *) cp;
	RfItem item;
	void *dp = ResolveDataObject((RfPath *) button->e.cp, &item);
	uint32_t id = *(uint32_t *) dp;
	Color *color = ColorLookupPointer(id);
	char buffer[128];
	snprintf(buffer, sizeof(buffer), "%.*s", (int) color->key.byteCount, (char *) color->key.buffer);
	char *key = strdup(buffer);
	UIDialogShow(window, 0, "Rename color             \n%t\n%f%b", &key, "Rename");
	free(color->key.buffer);
	color->key.buffer = key;
	color->key.byteCount = strlen(key);
	free(button->label);
	button->label = UIStringCopy(key, -1);
	UIElementRefresh(&button->e);
	ColorListRefresh();
}

void StyleColorOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		UIPanel *labelPanel = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
		UILabelCreate(&labelPanel->e, UI_ELEMENT_H_FILL, ((StringOption *) item->options)->string, -1);
		MakeOverrideButton(&labelPanel->e, state, RF_PATH_TERMINATOR);
		UIPanel *row = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
		UIButton *button = UIButtonCreate(&row->e, UI_BUTTON_DROP_DOWN, "", -1);
		button->e.messageUser = StyleColorButtonMessage;
		UIButton *rename = UIButtonCreate(&row->e, 0, "Rename", -1);
		rename->e.cp = button;
		rename->invoke = StyleColorRenameButtonCommand;
		BuildPathForUI((MakeUIState *) state, RF_PATH_TERMINATOR, &button->e);
	} else if (state->op == OP_GET_PALETTE) {
		uint32_t color = *(uint32_t *) pointer;
		int count = hmget(palette, color);
		count++;
		hmput(palette, color, count);
	} else if (state->op == OP_REPLACE_COLOR) {
		assert(*(uint32_t *) pointer != replaceColorTo);

		if (*(uint32_t *) pointer == replaceColorFrom) {
			*(uint32_t *) pointer = replaceColorTo;
		}
	} else if (state->op == OP_FIND_COLOR_USERS) {
		uint32_t id = *(uint32_t *) pointer;
		Style **list = hmget(colorUsers, id);
		Layer *layer = (Layer *) currentPaletteOpLayer;

		for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
			for (uintptr_t j = 0; j < arrlenu(styleSet.styles[i]->layers); j++) {
				if (styleSet.styles[i]->layers[j] == layer->id) {
					for (uintptr_t k = 0; k < arrlenu(list); k++) {
						if (list[k] == styleSet.styles[i]) {
							goto found;
						}
					}

					arrput(list, styleSet.styles[i]);
					goto found;
				}
			}
		}

		found:;
		hmput(colorUsers, id, list);
	} else {
		RfEndianOp(state, item, pointer);
	}
}

void StyleBoolOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		if (((MakeUIState *) state)->inKeyframe) return;
		UIButton *button = UIButtonCreate(&UIPanelCreate(0, UI_PANEL_HORIZONTAL)->e, 
			0, ((StringOption *) item->options)->string, -1);
		button->e.messageUser = StyleBoolMessage;
		BuildPathForUI((MakeUIState *) state, RF_PATH_TERMINATOR, &button->e);
	} else {
		RfEndianOp(state, item, pointer);
	}
}

void StyleStringOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		if (((MakeUIState *) state)->inKeyframe) return;
		UILabelCreate(0, 0, ((StringOption *) item->options)->string, -1);
		UITextbox *textbox = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		textbox->e.messageUser = StyleStringMessage;
		BuildPathForUI((MakeUIState *) state, RF_PATH_TERMINATOR, &textbox->e);
	} else {
		RfDataOp(state, item, pointer);
	}
}

void Rectangle8Op(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		UILabelCreate(0, 0, ((StringOption *) item->options)->string, -1);
		UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);

		MakeOverrideButton(0, state, Rectangle8_l);
		UITextbox *l = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		l->e.messageUser = StyleI8Message;
		BuildPathForUI((MakeUIState *) state, Rectangle8_l, &l->e);
		MakeOverrideButton(0, state, Rectangle8_r);
		UITextbox *r = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		r->e.messageUser = StyleI8Message;
		BuildPathForUI((MakeUIState *) state, Rectangle8_r, &r->e);
		MakeOverrideButton(0, state, Rectangle8_t);
		UITextbox *t = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		t->e.messageUser = StyleI8Message;
		BuildPathForUI((MakeUIState *) state, Rectangle8_t, &t->e);
		MakeOverrideButton(0, state, Rectangle8_b);
		UITextbox *b = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		b->e.messageUser = StyleI8Message;
		BuildPathForUI((MakeUIState *) state, Rectangle8_b, &b->e);

		UIParentPop();
		((MakeUIState *) state)->recurse = false;
	} else {
		RfStructOp(state, item, pointer);
	}
}

void Rectangle16Op(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		UILabelCreate(0, 0, ((StringOption *) item->options)->string, -1);
		UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);

		MakeOverrideButton(0, state, Rectangle16_l);
		UITextbox *l = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		l->e.messageUser = StyleI16Message;
		BuildPathForUI((MakeUIState *) state, Rectangle16_l, &l->e);
		MakeOverrideButton(0, state, Rectangle16_r);
		UITextbox *r = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		r->e.messageUser = StyleI16Message;
		BuildPathForUI((MakeUIState *) state, Rectangle16_r, &r->e);
		MakeOverrideButton(0, state, Rectangle16_t);
		UITextbox *t = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		t->e.messageUser = StyleI16Message;
		BuildPathForUI((MakeUIState *) state, Rectangle16_t, &t->e);
		MakeOverrideButton(0, state, Rectangle16_b);
		UITextbox *b = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		b->e.messageUser = StyleI16Message;
		BuildPathForUI((MakeUIState *) state, Rectangle16_b, &b->e);

		UIParentPop();
		((MakeUIState *) state)->recurse = false;
	} else {
		RfStructOp(state, item, pointer);
	}
}

void PathPointOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_EXPORT) {
		PathPoint *point = (PathPoint *) pointer;
		float themePoint[6];
		themePoint[0] = point->x0; ExportAddPathToOffset((ExportState *) state, PathPoint_x0, 0 * sizeof(float));
		themePoint[1] = point->y0; ExportAddPathToOffset((ExportState *) state, PathPoint_y0, 1 * sizeof(float));
		themePoint[2] = point->x1; ExportAddPathToOffset((ExportState *) state, PathPoint_x1, 2 * sizeof(float));
		themePoint[3] = point->y1; ExportAddPathToOffset((ExportState *) state, PathPoint_y1, 3 * sizeof(float));
		themePoint[4] = point->x2; ExportAddPathToOffset((ExportState *) state, PathPoint_x2, 4 * sizeof(float));
		themePoint[5] = point->y2; ExportAddPathToOffset((ExportState *) state, PathPoint_y2, 5 * sizeof(float));
		state->access(state, themePoint, sizeof(themePoint));
	} else if (state->op == OP_MAKE_UI) {
		UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);

		for (int i = 0; i < 6; i++) {
			MakeOverrideButton(0, state, PathPoint_x0 + i);
			UITextbox *textbox = UITextboxCreate(0, UI_ELEMENT_H_FILL);
			textbox->e.messageUser = StyleFloatMessage;
			BuildPathForUI((MakeUIState *) state, PathPoint_x0 + i, &textbox->e);
			if (i == 1 || i == 3) UISpacerCreate(0, 0, 5, 0);
		}

		UIParentPop();
		((MakeUIState *) state)->recurse = false;
	} else {
		RfStructOp(state, item, pointer);
	}
}

void Gaps8Op(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		UILabelCreate(0, 0, ((StringOption *) item->options)->string, -1);
		UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);

		MakeOverrideButton(0, state, Gaps8_major);
		UITextbox *major = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		major->e.messageUser = StyleI8Message;
		BuildPathForUI((MakeUIState *) state, Gaps8_major, &major->e);
		MakeOverrideButton(0, state, Gaps8_minor);
		UITextbox *minor = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		minor->e.messageUser = StyleI8Message;
		BuildPathForUI((MakeUIState *) state, Gaps8_minor, &minor->e);
		MakeOverrideButton(0, state, Gaps8_wrap);
		UITextbox *wrap = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		wrap->e.messageUser = StyleI8Message;
		BuildPathForUI((MakeUIState *) state, Gaps8_wrap, &wrap->e);

		UIParentPop();
		((MakeUIState *) state)->recurse = false;
	} else {
		RfStructOp(state, item, pointer);
	}
}

void Size16Op(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		UILabelCreate(0, 0, ((StringOption *) item->options)->string, -1);
		UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);

		MakeOverrideButton(0, state, Size16_width);
		UITextbox *width = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		width->e.messageUser = StyleI16Message;
		BuildPathForUI((MakeUIState *) state, Size16_width, &width->e);
		MakeOverrideButton(0, state, Size16_height);
		UITextbox *height = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		height->e.messageUser = StyleI16Message;
		BuildPathForUI((MakeUIState *) state, Size16_height, &height->e);

		UIParentPop();
		((MakeUIState *) state)->recurse = false;
	} else {
		RfStructOp(state, item, pointer);
	}
}

void Corners8Op(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		UILabelCreate(0, 0, ((StringOption *) item->options)->string, -1);
		UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
		MakeOverrideButton(0, state, Corners8_tl);
		UITextbox *l = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		l->e.messageUser = StyleI8Message;
		BuildPathForUI((MakeUIState *) state, Corners8_tl, &l->e);
		MakeOverrideButton(0, state, Corners8_tr);
		UITextbox *r = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		r->e.messageUser = StyleI8Message;
		BuildPathForUI((MakeUIState *) state, Corners8_tr, &r->e);
		MakeOverrideButton(0, state, Corners8_bl);
		UITextbox *t = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		t->e.messageUser = StyleI8Message;
		BuildPathForUI((MakeUIState *) state, Corners8_bl, &t->e);
		MakeOverrideButton(0, state, Corners8_br);
		UITextbox *b = UITextboxCreate(0, UI_ELEMENT_H_FILL);
		b->e.messageUser = StyleI8Message;
		BuildPathForUI((MakeUIState *) state, Corners8_br, &b->e);
		UIParentPop();
		((MakeUIState *) state)->recurse = false;
	} else {
		RfStructOp(state, item, pointer);
	}
}

void StyleI8Op(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		UIPanel *labelPanel = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
		UILabelCreate(&labelPanel->e, UI_ELEMENT_H_FILL, ((StringOption *) item->options)->string, -1);
		MakeOverrideButton(&labelPanel->e, state, RF_PATH_TERMINATOR);
		UITextbox *textbox = UITextboxCreate(0, 0);
		textbox->e.messageUser = StyleI8Message;
		BuildPathForUI((MakeUIState *) state, RF_PATH_TERMINATOR, &textbox->e);
	} else {
		rfI8.op(state, item, pointer);
	}
}

void StyleI16Op(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		UIPanel *labelPanel = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
		UILabelCreate(&labelPanel->e, UI_ELEMENT_H_FILL, ((StringOption *) item->options)->string, -1);
		MakeOverrideButton(&labelPanel->e, state, RF_PATH_TERMINATOR);
		UITextbox *textbox = UITextboxCreate(0, 0);
		textbox->e.messageUser = StyleI16Message;
		BuildPathForUI((MakeUIState *) state, RF_PATH_TERMINATOR, &textbox->e);
	} else {
		rfI16.op(state, item, pointer);
	}
}

void StyleFloatOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		UIPanel *labelPanel = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
		UILabelCreate(&labelPanel->e, UI_ELEMENT_H_FILL, ((StringOption *) item->options)->string, -1);
		MakeOverrideButton(&labelPanel->e, state, RF_PATH_TERMINATOR);
		UITextbox *textbox = UITextboxCreate(0, 0);
		textbox->e.messageUser = StyleFloatMessage;
		BuildPathForUI((MakeUIState *) state, RF_PATH_TERMINATOR, &textbox->e);
	} else {
		rfF32.op(state, item, pointer);
	}
}

void StyleUnionOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		MakeUIState *makeUI = (MakeUIState *) state;

		UILabelCreate(0, 0, ((StringOption *) item->options)->string, -1);

		if (!makeUI->inKeyframe) {
			UIPanel *buttonPanel = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
			buttonPanel->e.messageUser = StyleUnionButtonPanelMessage;

			for (uintptr_t i = 0; i < item->type->fieldCount; i++) {
				uint32_t tag = i ? i + 1 : 0;
				const char *name = i ? ((StringOption *) item->type->fields[i].item.options)->string : "(none)";
				UIButton *button = UIButtonCreate(&buttonPanel->e, UI_BUTTON_SMALL, name, -1);
				button->e.cp = (void *) (uintptr_t) tag;
				button->e.messageUser = StyleUnionButtonMessage;
			}

			BuildPathForUI(makeUI, 0, &buttonPanel->e);
		}

		UIPanel *subPanel = UIPanelCreate(0, UI_PANEL_WHITE | UI_PANEL_EXPAND);
		subPanel->gap = 5;
		subPanel->e.messageUser = StyleUnionPanelMessage;
		BuildPathForUI(makeUI, 0, &subPanel->e);
		makeUI->recurse = false;
	} else {
		RfStructOp(state, item, pointer);
	}
}

void StyleEnumOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		MakeUIState *makeUI = (MakeUIState *) state;

		if (makeUI->inKeyframe) {
			return;
		}

		if (item->type->fieldCount > 5) {
			UIPanel *panel = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
			UILabelCreate(&panel->e, UI_ELEMENT_H_FILL, ((StringOption *) item->options)->string, -1);
			UIButton *button = UIButtonCreate(&panel->e, UI_BUTTON_DROP_DOWN, "", -1);
			button->e.messageUser = StyleChoiceButtonMessage;
			BuildPathForUI(makeUI, RF_PATH_TERMINATOR, &button->e);
		} else {
			UIPanel *buttonPanel = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
			UILabelCreate(&buttonPanel->e, 0, ((StringOption *) item->options)->string, -1);
			buttonPanel->e.messageUser = StyleUnionButtonPanelMessage;

			for (uintptr_t i = 0; i < item->type->fieldCount; i++) {
				uint32_t tag = i;
				const char *name = ((StringOption *) item->type->fields[i].item.options)->string;
				UIButton *button = UIButtonCreate(&buttonPanel->e, UI_BUTTON_SMALL, name, -1);
				button->e.cp = (void *) (uintptr_t) tag;
				button->e.messageUser = StyleUnionButtonMessage;
			}

			BuildPathForUI(makeUI, RF_PATH_TERMINATOR, &buttonPanel->e);
		}
	} else {
		RfEnumOp(state, item, pointer);
	}
}

void StyleArrayOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		MakeUIState *makeUI = (MakeUIState *) state;

		UIPanel *buttonPanel = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
		UILabelCreate(&buttonPanel->e, 0, ((StringOption *) ((RfItem *) item->options)->options)->string, -1);

		UIPanel *subPanel = UIPanelCreate(0, UI_PANEL_WHITE | UI_PANEL_EXPAND);
		subPanel->gap = 5;
		subPanel->e.messageUser = StyleArrayPanelMessage;
		BuildPathForUI(makeUI, RF_PATH_TERMINATOR, &subPanel->e);
		makeUI->recurse = false;

		if (!makeUI->inKeyframe) {
			UIButton *addButton = UIButtonCreate(&buttonPanel->e, UI_BUTTON_SMALL, "Add", -1);
			addButton->e.cp = subPanel;
			addButton->e.messageUser = StyleArrayAddMessage;
			UIButton *deleteButton = UIButtonCreate(&buttonPanel->e, UI_BUTTON_SMALL, "Delete", -1);
			deleteButton->e.cp = subPanel;
			deleteButton->e.messageUser = StyleArrayDeleteMessage;
		}
	} else {
		RfArrayOp(state, item, pointer);
	}
}

void ButtonMoveLayerUp(void *_unused) {
	uintptr_t index = 0;

	for (uintptr_t i = 0; i < arrlenu(selected.style->layers); i++) {
		if (selected.style->layers[i] == selected.layer->id) {
			index = i;
			break;
		}
	}

	if (index <= 1) {
		return;
	}

	ModData mod = { 0 };
	mod.tag = ModData_swapLayers + 1;
	mod.swapLayers.index = index - 1;
	ModApply(&mod);
}

void ButtonMoveLayerDown(void *_unused) {
	uintptr_t index = 0;

	for (uintptr_t i = 0; i < arrlenu(selected.style->layers); i++) {
		if (selected.style->layers[i] == selected.layer->id) {
			index = i;
			break;
		}
	}

	if (index == arrlenu(selected.style->layers) - 1) {
		return;
	}

	ModData mod = { 0 };
	mod.tag = ModData_swapLayers + 1;
	mod.swapLayers.index = index;
	ModApply(&mod);
}

void ForkLayer(void *_unused) {
	// TODO Undo.
	// TODO Memory leak of state.data?

	if (!selected.layer) return;

	RfGrowableBuffer state = { 0 };
	state.data = SaveToGrowableBuffer(&Layer_Type, sizeof(Layer), NULL, selected.layer);

	ModData mod = { 0 };
	mod.tag = ModData_deleteLayer + 1;
	bool found = false;

	for (uintptr_t i = 0; i < arrlenu(selected.style->layers); i++) {
		if (selected.style->layers[i] == selected.layer->id) {
			mod.deleteLayer.index = i;
			found = true;
			break;
		}
	}

	assert(found);
	ModApply(&mod);

	state.s.version = saveFormatVersion;
	state.data.byteCount -= sizeof(uint32_t);
	state.s.allocate = RfRealloc;
	state.s.access = RfReadGrowableBuffer;

	RfItem item = { 0 };
	item.type = &Layer_Type;
	item.byteCount = sizeof(Layer);
	state.s.op = RF_OP_LOAD;

	Layer *layer = calloc(1, sizeof(Layer));
	item.type->op(&state.s, &item, layer);
	layer->id = ++styleSet.lastID;

	mod.tag = ModData_addLayer + 1;
	mod.addLayer.index = mod.deleteLayer.index;
	mod.addLayer.layer = layer;
	ModApply(&mod);
}

void RebuildInspector() {
	UIElementDestroyDescendents(&panelInspector->e);
	UIParentPush(&panelInspector->e);

	if (selected.keyframe) {
		MAKE_UI(Keyframe, selected.keyframe, false);
		MAKE_UI(Layer, selected.layer, true);
	} else if (selected.sequence) {
		MAKE_UI(Sequence, selected.sequence, false);
	} else if (selected.layer) {
		char buffer[256];
		snprintf(buffer, 256, "Layer ID: %ld", selected.layer->id);
		UILabelCreate(0, 0, buffer, -1);

		uintptr_t layerUsageCount = 0;

		for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
			Style *style = styleSet.styles[i];

			for (uintptr_t j = 0; j < arrlenu(style->layers); j++) {
				if (style->layers[j] == selected.layer->id) {
					layerUsageCount++;
				}
			}
		}

		if (layerUsageCount > 1) {
			snprintf(buffer, 256, "This layer is used %ld times.", layerUsageCount);
			UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH)->gap = 10;
			UILabelCreate(0, 0, buffer, -1);
			UIButtonCreate(0, 0, "Fork", -1)->invoke = ForkLayer;
			UIParentPop();
		}

		if (selected.layer->isMetricsLayer) {
			assert(selected.layer->base.tag == LayerBase_metrics + 1);
			MakeUIState state = { 0 };
			state.s.op = OP_MAKE_UI;
			state.index = Layer_base;
			state.recurse = true;
			RfItem item = Layer_Type.fields[Layer_base].item;
			void *pointer = (uint8_t *) selected.layer + Layer_Type.fields[Layer_base].offset;
			MakeUI(&state, &item, pointer);
		} else {
			UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
				UIButtonCreate(0, UI_BUTTON_SMALL, "Move up", -1)->invoke = ButtonMoveLayerUp;
				UIButtonCreate(0, UI_BUTTON_SMALL, "Move down", -1)->invoke = ButtonMoveLayerDown;
			UIParentPop();

			MAKE_UI(Layer, selected.layer, false);
		}
	}

	UIParentPop();
	UIElementRefresh(&panelInspector->e);
	UIElementRepaint(elementCanvas, NULL);
}

void StyleListRefresh();

void SetSelectedItems(ModContext context) {
	if (0 == memcmp(&selected, &context, sizeof(context))) {
		return;
	}

	arrfree(animatingValues);

	selected = context;

	tableLayers->itemCount = selected.style ? arrlen(selected.style->layers) : 0;
	UITableResizeColumns(tableLayers);
	UIElementRefresh(&tableLayers->e);

	tableSequences->itemCount = selected.layer ? arrlen(selected.layer->sequences) : 0;
	UITableResizeColumns(tableSequences);
	UIElementRefresh(&tableSequences->e);

	tableKeyframes->itemCount = selected.sequence ? arrlen(selected.sequence->keyframes) : 0;
	UITableResizeColumns(tableKeyframes);
	UIElementRefresh(&tableKeyframes->e);

	static uint32_t layerNamePath[] = { Layer_name, RF_PATH_TERMINATOR };
	if (selected.layer && !selected.sequence) tableLayers->e.cp = layerNamePath;
	else tableLayers->e.cp = NULL;

	static uint32_t sequencePath[] = { PATH_ANY, RF_PATH_TERMINATOR };
	if (selected.sequence && !selected.keyframe) tableSequences->e.cp = sequencePath;
	else tableSequences->e.cp = NULL;

	static uint32_t keyframeProgressPath[] = { Keyframe_progress, RF_PATH_TERMINATOR };
	if (selected.keyframe) tableSequences->e.cp = keyframeProgressPath;
	else tableKeyframes->e.cp = NULL;

	StyleListRefresh();
	RebuildInspector();
}

// ------------------- Modifications -------------------

void ModPushUndo(ModData *data) {
	Mod mod = { 0 };
	mod.context = selected;
	mod.data = *data;

	if (modApplyUndo) {
		arrput(redoStack, mod);
	} else {
		arrput(undoStack, mod);
	}
}

void _ModApply(Mod *mod) {
	if (memcmp(&mod->context, &selected, sizeof(ModContext))) {
		SetSelectedItems(mod->context);
	}

	RfIterator iterator = { 0 };
	iterator.s.op = RF_OP_ITERATE;
	RfItem item = { 0 };
	item.type = &ModData_Type;
	item.byteCount = sizeof(Mod);
	item.type->op(&iterator.s, &item, &mod->data);

	RfState state = { 0 };
	state.op = OP_DO_MOD;
	iterator.item.type->op(&state, &iterator.item, iterator.pointer);

	UIElementRepaint(elementCanvas, NULL);
}

void ClearUndoRedo() {
	RfState state = { 0 };
	state.op = RF_OP_FREE;
	state.allocate = RfRealloc;
	RfItem item = { 0 };
	item.type = &Mod_Type;
	item.byteCount = sizeof(Mod);

	for (uintptr_t i = 0; i < arrlenu(redoStack); i++) {
		item.type->op(&state, &item, redoStack + i);
	}

	for (uintptr_t i = 0; i < arrlenu(undoStack); i++) {
		item.type->op(&state, &item, undoStack + i);
	}

	arrfree(redoStack);
	arrfree(undoStack);
}

void ModApply(ModData *data) {
	arrfree(animatingValues);

	Mod mod = { 0 };
	mod.context = selected;
	mod.data = *data;

	modApplyUndo = false;
	_ModApply(&mod);

	RfState state = { 0 };
	state.op = RF_OP_FREE;
	state.allocate = RfRealloc;
	RfItem item = { 0 };
	item.type = &Mod_Type;
	item.byteCount = sizeof(Mod);

	for (uintptr_t i = 0; i < arrlenu(redoStack); i++) {
		item.type->op(&state, &item, redoStack + i);
	}

	arrfree(redoStack);

	if (arrlenu(undoStack) >= 2 && data->tag != ModData_deleteOverride + 1) {
		Mod *undo1 = undoStack + arrlen(undoStack) - 1;
		Mod *undo2 = undoStack + arrlen(undoStack) - 2;

		if (0 == memcmp(&undo1->context, &undo2->context, sizeof(ModContext))) {
			if ((undo1->data.tag == ModData_changeProperty + 1 && undo2->data.tag == ModData_changeProperty + 1
						&& ArePathsEqual(undo1->data.changeProperty.property.path, undo2->data.changeProperty.property.path))
					|| (undo1->data.tag == ModData_changeProperty + 1 && undo2->data.tag == ModData_deleteOverride + 1
						&& ArePathsEqual(undo1->data.changeProperty.property.path, undo2->data.deleteOverride.property.path))) {
				item.type->op(&state, &item, undo1);
				(void) arrpop(undoStack);
			}
		}
	}
}

void NotifySubscriptions(UIElement *source, uint32_t *path, void *pointer) {
	for (uintptr_t i = 0; i < arrlenu(inspectorSubscriptions); i++) {
		if (inspectorSubscriptions[i] == source) {
			continue;
		}

		uint32_t *subscription = (uint32_t *) inspectorSubscriptions[i]->cp;

		if (!subscription) {
			continue;
		}

		if (subscription[0] == PATH_ANY || ArePathsEqual(subscription, path)) {
			UIElementMessage(inspectorSubscriptions[i], MSG_PROPERTY_CHANGED, 0, pointer);
		}
	}
}

void ModChangePropertyOp(RfState *state, RfItem *item, void *pointer) {
	ModChangeProperty *mod = (ModChangeProperty *) pointer;

	if (state->op == OP_DO_MOD) {
		// Do we need to use an override?
		bool isOverride = mod->property.path[0] == PATH_IN_KEYFRAME;
		void *pointer = NULL;

		if (isOverride) {
			bool foundOverride = false;

			// Does an override exist?
			for (uintptr_t i = 0; i < arrlenu(selected.keyframe->properties); i++) {
				if (ArePathsEqual(mod->property.path, selected.keyframe->properties[i].path)) {
					// Save the old value for undo.
					ModData undo = { 0 };
					undo.tag = ModData_changeProperty + 1;
					undo.changeProperty.property.path = mod->property.path;
					undo.changeProperty.property.data = selected.keyframe->properties[i].data;
					ModPushUndo(&undo);

					// Load the new value.
					selected.keyframe->properties[i].data = mod->property.data;
					foundOverride = true;
					break;
				}
			}

			if (!foundOverride) {
				// Create the undo mod.
				ModData undo = { 0 };
				undo.tag = ModData_deleteOverride + 1;
				undo.deleteOverride.property.path = mod->property.path;
				ModPushUndo(&undo);

				// Add the override.
				Property property = mod->property;
				property.path = DuplicatePath(property.path); 
				arrput(selected.keyframe->properties, property);
			}

			// Load the object so we can notify subscribers.
			RfItem item;
			pointer = ResolveDataObject((RfPath *) mod->property.path, &item);
		} else {
			// Resolve the pointer.
			RfItem item;
			pointer = ResolveDataObject((RfPath *) mod->property.path, &item);

			// Save the old value for undo.
			ModData undo = { 0 };
			undo.tag = ModData_changeProperty + 1;
			undo.changeProperty.property.path = mod->property.path;
			undo.changeProperty.property.data = SaveToGrowableBuffer(item.type, item.byteCount, item.options, pointer);
			ModPushUndo(&undo);

			// Free the old value.
			RfGrowableBuffer state = { 0 };
			state.s.allocate = RfRealloc;
			state.s.op = RF_OP_FREE;
			item.type->op(&state.s, &item, pointer);

			// Load the new value.
			state.s.op = RF_OP_LOAD;
			state.s.access = RfReadGrowableBuffer;
			state.data = mod->property.data;
			state.position = 0;
			item.type->op(&state.s, &item, pointer);
			free(state.data.buffer);
		}

		// Notify subscribed elements in the inspector.
		NotifySubscriptions(mod->source, mod->property.path, pointer);
	} else {
		RfStructOp(state, item, pointer);
	}
}

void ModDeleteOverrideOp(RfState *state, RfItem *item, void *pointer) {
	ModDeleteOverride *mod = (ModDeleteOverride *) pointer;

	if (state->op == OP_DO_MOD) {
		// Find the override.
		for (uintptr_t i = 0; i < arrlenu(selected.keyframe->properties); i++) {
			if (ArePathsEqual(mod->property.path, selected.keyframe->properties[i].path)) {
				// Save the old value for undo.
				ModData undo = { 0 };
				undo.tag = ModData_changeProperty + 1;
				undo.changeProperty.property.path = mod->property.path;
				undo.changeProperty.property.data = selected.keyframe->properties[i].data;
				ModPushUndo(&undo);

				// Delete the override.
				free(selected.keyframe->properties[i].path);
				arrdel(selected.keyframe->properties, i);

				// Notify subscribed elements in the inspector.
				RfItem item;
				NotifySubscriptions(NULL, mod->property.path, 
					ResolveDataObject((RfPath *) mod->property.path, &item));

				return;
			}
		}

		assert(false);
	} else {
		RfStructOp(state, item, pointer);
	}
}

void ModArrayOp(RfState *state, RfItem *item, void *pointer) {
	ModArray *mod = (ModArray *) pointer;

	if (state->op == OP_DO_MOD) {
		// Resolve the pointer.
		RfItem item;
		void **pointer = (void **) ResolveDataObject((RfPath *) mod->property.path, &item);
		RfItem *elementItem = (RfItem *) item.options;

		if (mod->isDelete) {
			size_t length = --((RfArrayHeader *) *pointer)[-1].length;

			ModData undo = { 0 };
			undo.tag = ModData_array + 1;
			undo.array.property.path = mod->property.path;
			undo.array.property.data = SaveToGrowableBuffer(elementItem->type, elementItem->byteCount, elementItem->options, 
				(uint8_t *) *pointer + elementItem->byteCount * length);
			ModPushUndo(&undo);
		} else {
			*pointer = stbds_arrgrowf(*pointer, elementItem->byteCount, 1, 0);

			size_t length = ++((RfArrayHeader *) *pointer)[-1].length;
			RfGrowableBuffer state = { 0 };
			state.s.allocate = RfRealloc;
			state.s.op = RF_OP_LOAD;
			state.s.access = RfReadGrowableBuffer;
			state.data = mod->property.data;
			state.position = 0;
			uint8_t *p = (uint8_t *) *pointer + elementItem->byteCount * (length - 1);
			memset(p, 0, elementItem->byteCount);
			elementItem->type->op(&state.s, elementItem, p);
			free(state.data.buffer);

			ModData undo = { 0 };
			undo.tag = ModData_array + 1;
			undo.array.property.path = mod->property.path;
			undo.array.isDelete = true;
			ModPushUndo(&undo);
		}

		// Notify subscribed elements in the inspector.
		NotifySubscriptions(NULL, mod->property.path, pointer);
	} else {
		RfStructOp(state, item, pointer);
	}
}

// ------------------- Styles -------------------

UITextbox *stylesTextbox;
UITable *stylesTable;
UIButton *stylesShowWithSelectedLayer;
Style **stylesInList;

int StylesTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		Style *style = stylesInList[m->index];
		m->isSelected = style == selected.style;

		if (!style) {
			return snprintf(m->buffer, m->bufferBytes, "View all");
		} else if (style->name.byteCount) {
			return snprintf(m->buffer, m->bufferBytes, "%.*s", (int) style->name.byteCount, (char *) style->name.buffer);
		} else {
			return snprintf(m->buffer, m->bufferBytes, "(default)");
		}
	} else if (message == UI_MSG_CLICKED || message == UI_MSG_MOUSE_DRAG) {
		int index = UITableHitTest(stylesTable, element->window->cursorX, element->window->cursorY);
		assert(index <= arrlen(stylesInList));

		if (index >= 0) {
			bool keepExistingLayer = false;

			if (selected.layer && stylesInList[index]) {
				for (uintptr_t i = 0; i < arrlenu(stylesInList[index]->layers); i++) {
					if (stylesInList[index]->layers[i] == selected.layer->id) {
						keepExistingLayer = true;
						break;
					}
				}
			}

			SetSelectedItems(MOD_CONTEXT(stylesInList[index], keepExistingLayer ? selected.layer 
						: LayerLookup(stylesInList[index]->layers[0]), NULL, NULL));
			UIElementRefresh(&stylesTable->e);
		}
	}

	return 0;
}

int StyleCompare(const void *_left, const void *_right) {
	Style *left = *(Style **) _left; 
	Style *right = *(Style **) _right; 
	return StringCompareRaw(left->name.buffer, left->name.byteCount, right->name.buffer, right->name.byteCount);
}

void StyleListRefresh() {
	qsort(styleSet.styles, arrlenu(styleSet.styles), sizeof(Style *), StyleCompare);

	arrfree(stylesInList);

	bool showWithSelectedLayer = stylesShowWithSelectedLayer->e.flags & UI_BUTTON_CHECKED;

	if (!showWithSelectedLayer) {
		arrput(stylesInList, NULL);
	}

	for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
		bool include = false;

		if (showWithSelectedLayer && selected.layer) {
			for (uintptr_t j = 0; j < arrlenu(styleSet.styles[i]->layers); j++) {
				if (styleSet.styles[i]->layers[j] == selected.layer->id) {
					include = true;
					break;
				}
			}
		} else {
			include = true;
		}

		if (include) {
			arrput(stylesInList, styleSet.styles[i]);
		}
	}

	stylesTable->itemCount = arrlenu(stylesInList);
	UITableResizeColumns(stylesTable);
	UIElementRefresh(&stylesTable->e);

	UITextboxClear(stylesTextbox, false);
	UIElementRefresh(&stylesTextbox->e);

	if (selected.style && selected.style->publicStyle) buttonPublicStyle->e.flags |= UI_BUTTON_CHECKED;
	else buttonPublicStyle->e.flags &= ~UI_BUTTON_CHECKED;
}

void ButtonCreateStyle(void *_unused) {
	if (!stylesTextbox->bytes) {
		return;
	}

	for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
		if (styleSet.styles[i]->name.byteCount == (size_t) stylesTextbox->bytes
				&& 0 == memcmp(styleSet.styles[i]->name.buffer, stylesTextbox->string, stylesTextbox->bytes)) {
			return;
		}
	}

	Style *style = calloc(1, sizeof(Style));
	style->name.buffer = malloc((style->name.byteCount = stylesTextbox->bytes));
	style->id = ++styleSet.lastID;
	memcpy(style->name.buffer, stylesTextbox->string, style->name.byteCount);
	Layer *metrics = calloc(1, sizeof(Layer));
	metrics->id = ++styleSet.lastID;
	metrics->name.buffer = malloc(16);
	metrics->name.byteCount = snprintf(metrics->name.buffer, 16, "Metrics");
	metrics->base.tag = LayerBase_metrics + 1;
	metrics->isMetricsLayer = true;
	arrput(style->layers, metrics->id);
	arrput(styleSet.layers, metrics);
	arrput(styleSet.styles, style);
	SetSelectedItems(MOD_CONTEXT(style, NULL, NULL, NULL));

	ClearUndoRedo();
	StyleListRefresh();
}

void ButtonDeleteStyle(void *_unused) {
	if (!selected.style) return;
	Style *style = selected.style;
	SetSelectedItems(MOD_CONTEXT(NULL, NULL, NULL, NULL));

	bool found = false;

	for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
		if (styleSet.styles[i] == style) {
			RfState state = { 0 };
			state.op = RF_OP_FREE;
			state.allocate = RfRealloc;
			RfItem item = { 0 };
			item.type = &Style_Type;
			item.byteCount = sizeof(Style);
			item.type->op(&state, &item, style);
			arrdel(styleSet.styles, i);
			found = true;
			break;
		}
	}

	assert(found);

	ClearUndoRedo();
	StyleListRefresh();
}

void ButtonTogglePublicStyle(void *_unused) {
	if (!selected.style) {
		return;
	}

	selected.style->publicStyle = !selected.style->publicStyle;

	if (selected.style->publicStyle) buttonPublicStyle->e.flags |= UI_BUTTON_CHECKED;
	else buttonPublicStyle->e.flags &= ~UI_BUTTON_CHECKED;
}

void ButtonRenameStyle(void *_unused) {
	if (!stylesTextbox->bytes || !selected.style) {
		return;
	}

	selected.style->name.buffer = realloc(selected.style->name.buffer, (selected.style->name.byteCount = stylesTextbox->bytes));
	memcpy(selected.style->name.buffer, stylesTextbox->string, selected.style->name.byteCount);

	ClearUndoRedo();
	StyleListRefresh();
}

void ButtonShowStylesWithSelectedLayer(void *_unused) {
	stylesShowWithSelectedLayer->e.flags ^= UI_BUTTON_CHECKED;
	StyleListRefresh();
}

// ------------------- Constants -------------------

UITextbox *constantsTextbox;
UITextbox *constantsValue;
UIButton *constantsScale;
UITable *constantsTable;
Constant *selectedConstant;

void ConstantsDialogSetSelected(Constant *constant) {
	selectedConstant = constant;
	UIElementRefresh(&constantsTable->e);

	UITextboxClear(constantsValue, false);

	if (constant) {
		UITextboxReplace(constantsValue, constant->value.buffer, constant->value.byteCount, false);
		constantsValue->e.flags &= ~UI_ELEMENT_DISABLED;
		UIElementRefresh(&constantsValue->e);

		if (constant->scale) constantsScale->e.flags |= UI_BUTTON_CHECKED;
		else constantsScale->e.flags &= ~UI_BUTTON_CHECKED;
		UIElementRefresh(&constantsScale->e);
	} else {
		constantsValue->e.flags |= UI_ELEMENT_DISABLED;
		UIElementRefresh(&constantsValue->e);

		constantsScale->e.flags &= ~UI_BUTTON_CHECKED;
		UIElementRefresh(&constantsScale->e);
	}
}

int ConstantsTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		Constant *constant = styleSet.constants[m->index];
		m->isSelected = constant == selectedConstant;
		return snprintf(m->buffer, m->bufferBytes, "%.*s", (int) constant->key.byteCount, (char *) constant->key.buffer);
	} else if (message == UI_MSG_CLICKED || message == UI_MSG_MOUSE_DRAG) {
		int index = UITableHitTest(constantsTable, element->window->cursorX, element->window->cursorY);

		if (index != -1) {
			ConstantsDialogSetSelected(styleSet.constants[index]);
		}
	}

	return 0;
}

int ConstantsValueMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_VALUE_CHANGED) {
		if (selectedConstant) {
			free(selectedConstant->value.buffer);
			selectedConstant->value.buffer = malloc(constantsValue->bytes);
			selectedConstant->value.byteCount = constantsValue->bytes;
			memcpy(selectedConstant->value.buffer, constantsValue->string, constantsValue->bytes);
		}
	}

	return 0;
}

int ConstantsScaleMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		if (selectedConstant) {
			element->flags ^= UI_BUTTON_CHECKED;
			selectedConstant->scale = element->flags & UI_BUTTON_CHECKED;
		}
	}

	return 0;
}

int ConstantCompare(const void *_left, const void *_right) {
	Constant *left = *(Constant **) _left; 
	Constant *right = *(Constant **) _right; 
	return StringCompareRaw(left->key.buffer, left->key.byteCount, right->key.buffer, right->key.byteCount);
}

void ConstantListRefresh() {
	constantsTable->itemCount = arrlenu(styleSet.constants);
	qsort(styleSet.constants, constantsTable->itemCount, sizeof(Constant *), ConstantCompare);
	UITableResizeColumns(constantsTable);
	UIElementRefresh(&constantsTable->e);

	UITextboxClear(constantsTextbox, false);
	UIElementRefresh(&constantsTextbox->e);
}

void ButtonAddConstant(void *_unused) {
	if (!constantsTextbox->bytes) {
		return;
	}

	for (uintptr_t i = 0; i < arrlenu(styleSet.constants); i++) {
		if (styleSet.constants[i]->key.byteCount == (size_t) constantsTextbox->bytes
				&& 0 == memcmp(styleSet.constants[i]->key.buffer, constantsTextbox->string, constantsTextbox->bytes)) {
			return;
		}
	}

	Constant *constant = calloc(1, sizeof(Constant));
	constant->key.buffer = malloc((constant->key.byteCount = constantsTextbox->bytes));
	memcpy(constant->key.buffer, constantsTextbox->string, constant->key.byteCount);
	arrput(styleSet.constants, constant);

	ConstantsDialogSetSelected(constant);
	ConstantListRefresh();
}

void ButtonDeleteConstant(void *_unused) {
	if (!selectedConstant) {
		return;
	}

	for (uintptr_t i = 0; i < arrlenu(styleSet.constants); i++) {
		if (styleSet.constants[i] == selectedConstant) {
			arrdel(styleSet.constants, i);
			ConstantsDialogSetSelected(NULL);
			ConstantListRefresh();
		}
	}
}

// ------------------- Colors -------------------

UITextbox *colorsTextbox;
UITextbox *colorsValue;
UIColorPicker *colorsValue2;
UITable *colorsTable;
UIElement *colorsPreview;
Color *selectedColor;

int ColorsPreviewMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_PAINT) {
		UIDrawRectangle((UIPainter *) dp, element->bounds, selectedColor ? selectedColor->value : 0, 0xFF000000, UI_RECT_1(1));
	} else if (message == UI_MSG_GET_WIDTH || message == UI_MSG_GET_HEIGHT) {
		return 20;
	}

	return 0;
}

void ColorsDialogSetSelected(Color *color) {
	selectedColor = color;
	UIElementRefresh(&colorsTable->e);

	UITextboxClear(colorsValue, false);

	if (color) {
		char buffer[16];
		sprintf(buffer, "%.8X", color->value);
		UITextboxReplace(colorsValue, buffer, -1, false);
		colorsValue->e.flags &= ~UI_ELEMENT_DISABLED;
		UIColorToHSV(color->value, &colorsValue2->hue, &colorsValue2->saturation, &colorsValue2->value);
		colorsValue2->opacity = (selectedColor->value >> 24) / 255.0f;
		UIElementRefresh(&colorsValue->e);
		UIElementRefresh(&colorsValue2->e);
		UIElementRefresh(colorsPreview);
	} else {
		colorsValue->e.flags |= UI_ELEMENT_DISABLED;
		UIElementRefresh(&colorsValue->e);
	}
}

int ColorsTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		Color *color = styleSet.colors[m->index];
		m->isSelected = color == selectedColor;

		if (m->column == 1) {
			return snprintf(m->buffer, m->bufferBytes, "%.8X", color->value);
		} else {
			return snprintf(m->buffer, m->bufferBytes, "%.*s", (int) color->key.byteCount, (char *) color->key.buffer);
		}
	} else if (message == UI_MSG_CLICKED || message == UI_MSG_MOUSE_DRAG) {
		int index = UITableHitTest(colorsTable, element->window->cursorX, element->window->cursorY);

		if (index != -1) {
			ColorsDialogSetSelected(styleSet.colors[index]);
		}
	}

	return 0;
}

int ColorsValue2Message(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_VALUE_CHANGED) {
		if (selectedColor) {
			uint32_t newValue;
			UIColorToRGB(colorsValue2->hue, colorsValue2->saturation, colorsValue2->value, &newValue);
			newValue |= (uint32_t) (colorsValue2->opacity * 255.0f) << 24;
			selectedColor->value = newValue;
			char buffer[16];
			sprintf(buffer, "%.8X", selectedColor->value);
			UITextboxClear(colorsValue, false);
			UITextboxReplace(colorsValue, buffer, -1, false);
			UIElementRefresh(&colorsValue->e);
			UIElementRefresh(colorsPreview);
			UIElementRepaint(elementCanvas, NULL);
		}
	}

	return 0;
}

int ColorsValueMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_VALUE_CHANGED) {
		if (selectedColor) {
			char buffer[16];
			int length = 15 > colorsValue->bytes ? colorsValue->bytes : 15;
			memcpy(buffer, colorsValue->string, length);
			buffer[length] = 0;
			selectedColor->value = strtol(buffer, NULL, 16);
			UITableResizeColumns(colorsTable);
			UIElementRefresh(&colorsTable->e);
			UIColorToHSV(selectedColor->value, &colorsValue2->hue, &colorsValue2->saturation, &colorsValue2->value);
			colorsValue2->opacity = (selectedColor->value >> 24) / 255.0f;
			UIElementRefresh(&colorsValue2->e);
			UIElementRefresh(colorsPreview);
			UIElementRepaint(elementCanvas, NULL);
		}
	}

	return 0;
}

int ColorCompare(const void *_left, const void *_right) {
	Color *left = *(Color **) _left; 
	Color *right = *(Color **) _right; 
	return StringCompareRaw(left->key.buffer, left->key.byteCount, right->key.buffer, right->key.byteCount);
}

void ColorListRefresh() {
	colorsTable->itemCount = arrlenu(styleSet.colors);
	qsort(styleSet.colors, colorsTable->itemCount, sizeof(Color *), ColorCompare);
	UITableResizeColumns(colorsTable);
	UIElementRefresh(&colorsTable->e);

	UITextboxClear(colorsTextbox, false);
	UIElementRefresh(&colorsTextbox->e);
}

void ButtonAddColor(void *_unused) {
	if (!colorsTextbox->bytes) {
		return;
	}

	for (uintptr_t i = 0; i < arrlenu(styleSet.colors); i++) {
		if (styleSet.colors[i]->key.byteCount == (size_t) colorsTextbox->bytes
				&& 0 == memcmp(styleSet.colors[i]->key.buffer, colorsTextbox->string, colorsTextbox->bytes)) {
			return;
		}
	}

	Color *color = calloc(1, sizeof(Color));
	color->id = ++styleSet.lastID;
	color->key.buffer = malloc((color->key.byteCount = colorsTextbox->bytes));
	memcpy(color->key.buffer, colorsTextbox->string, color->key.byteCount);
	arrput(styleSet.colors, color);

	ColorsDialogSetSelected(color);
	ColorListRefresh();
}

void ButtonDeleteColor(void *_unused) {
	if (!selectedColor) {
		return;
	}

	for (uintptr_t i = 0; i < arrlenu(styleSet.colors); i++) {
		if (styleSet.colors[i] == selectedColor) {
			arrdel(styleSet.colors, i);
			ColorsDialogSetSelected(NULL);
			ColorListRefresh();
		}
	}
}

// ------------------- Item lists -------------------

void DoAddItemMod(void ***array, void *item, int index, int undoField) {
	ModData undo = { 0 };
	undo.tag = undoField + 1;
	undo.deleteLayer.index = index;
	ModPushUndo(&undo);
	arrins(*array, index, item);
}

void DoDeleteItemMod(void ***array, int index, int undoField) {
	ModData undo = { 0 };
	undo.tag = undoField + 1;
	undo.addLayer.layer = (Layer *) (*array)[index];
	undo.addLayer.index = index;
	ModPushUndo(&undo);
	arrdel(*array, index);
}

void ButtonDeleteItem(void *selected, void **array, int field) {
	if (!selected) return;

	ModData mod = { 0 };
	mod.tag = field + 1;
	bool found = false;

	for (uintptr_t i = 0; i < arrlenu(array); i++) {
		if (array[i] == selected) {
			mod.deleteLayer.index = i;
			found = true;
			break;
		}
	}

	assert(found);
	ModApply(&mod);
}

void DesignerArrayOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == OP_MAKE_UI) {
		((MakeUIState *) state)->recurse = false;
	} else {
		RfArrayOp(state, item, pointer);
	}
}

// ------------------- Fonts -------------------

BasicFontKerningEntry *kerningEntries;

bool ImportFont(char *fileData) {
	arrfree(kerningEntries);
	stbtt_fontinfo font = {};

	if (!stbtt_InitFont(&font, (uint8_t *) fileData, 0)) {
		return false;
	}

	float scale = stbtt_ScaleForPixelHeight(&font, 100);

	const char *charactersToImport = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";

	for (uintptr_t character = 0; charactersToImport[character]; character++) {
		int glyphIndex = stbtt_FindGlyphIndex(&font, charactersToImport[character]);
		stbtt_vertex *vertices;
		int vertexCount = stbtt_GetGlyphShape(&font, glyphIndex, &vertices);
		// printf("importing glyph for character '%c'\n", charactersToImport[character]);

		int x0, y0, x1, y1, advanceWidth, leftSideBearing;
		stbtt_GetGlyphHMetrics(&font, glyphIndex, &advanceWidth, &leftSideBearing);
		stbtt_GetGlyphBox(&font, glyphIndex, &x0, &y0, &x1, &y1);
		// printf("\t%d, %d, %d, %d, %d, %d\n", x0, x1, y0, y1, advanceWidth, leftSideBearing);

		Style *style = calloc(1, sizeof(Style));
		style->name.buffer = malloc(32);
		style->name.byteCount = snprintf(style->name.buffer, 32, "Glyph %c", charactersToImport[character]);
		style->id = ++styleSet.lastID;
		Layer *metrics = calloc(1, sizeof(Layer));
		metrics->id = ++styleSet.lastID;
		metrics->name.buffer = malloc(16);
		metrics->name.byteCount = snprintf(metrics->name.buffer, 16, "Metrics");
		metrics->base.tag = LayerBase_metrics + 1;
		metrics->base.metrics.minimumSize.width = x0; // Put these somewhere with 16 bits of space...
		metrics->base.metrics.maximumSize.width = x1;
		metrics->base.metrics.minimumSize.height = y0;
		metrics->base.metrics.maximumSize.height = y1;
		metrics->base.metrics.preferredSize.width = advanceWidth;
		metrics->isMetricsLayer = true;
		arrput(style->layers, metrics->id);
		arrput(styleSet.layers, metrics);
		arrput(styleSet.styles, style);

		Layer *layer = calloc(1, sizeof(Layer));
		layer->id = ++styleSet.lastID;
		layer->name.buffer = malloc(16);
		layer->name.byteCount = snprintf(layer->name.buffer, 16, "Path");
		layer->base.tag = LayerBase_path + 1;
		layer->mode = LAYER_MODE_BACKGROUND;
		layer->position.r = 100;
		layer->position.b = 100;
		arrput(style->layers, layer->id);
		arrput(styleSet.layers, layer);

		LayerPath *layerPath = &layer->base.path;
		layerPath->closed = true;
		layerPath->alpha = 255;

		PathFill fill = {};
		fill.mode.tag = PathFillMode_solid + 1;
		fill.paint.tag = Paint_solid + 1;
		fill.paint.solid.color = 0xFF000000;
		arrput(layerPath->fills, fill);

		int i = 0;

		while (i < vertexCount) {
			if (vertices[i].type == STBTT_vmove) {
				// printf("move to %d, %d\n", vertices[i].x, vertices[i].y);

				if (arrlen(layerPath->points)) {
					PathPoint *p = &arrlast(layerPath->points);
					p->x1 = -1e6;
				}

				PathPoint point = {};
				point.x0 = vertices[i].x;
				point.y0 = vertices[i].y;
				arrput(layerPath->points, point);
			} else if (vertices[i].type == STBTT_vline) {
				// printf("line to %d, %d\n", vertices[i].x, vertices[i].y);

				if (!arrlen(layerPath->points)) return false;
				PathPoint *p = &arrlast(layerPath->points);
				p->x1 = p->x0;
				p->y1 = p->y0;
				p->x2 = vertices[i].x;
				p->y2 = vertices[i].y;
				PathPoint point = {};
				point.x0 = vertices[i].x;
				point.y0 = vertices[i].y;
				arrput(layerPath->points, point);
			} else if (vertices[i].type == STBTT_vcurve) {
				// printf("curve to %d, %d via %d, %d\n", vertices[i].x, vertices[i].y, vertices[i].cx, vertices[i].cy);

				if (!arrlen(layerPath->points)) return false;
				PathPoint *p = &arrlast(layerPath->points);
				p->x1 = (p->x0 + 0.6667f * (vertices[i].cx - p->x0));
				p->y1 = (p->y0 + 0.6667f * (vertices[i].cy - p->y0));
				p->x2 = (vertices[i].x + 0.6667f * (vertices[i].cx - vertices[i].x));
				p->y2 = (vertices[i].y + 0.6667f * (vertices[i].cy - vertices[i].y));
				PathPoint point = {};
				point.x0 = vertices[i].x;
				point.y0 = vertices[i].y;
				arrput(layerPath->points, point);
			} else if (vertices[i].type == STBTT_vcubic) {
				// printf("cubic to %d, %d\n", vertices[i].x, vertices[i].y);

				if (!arrlen(layerPath->points)) return false;
				PathPoint *p = &arrlast(layerPath->points);
				p->x1 = vertices[i].cx;
				p->y1 = vertices[i].cy;
				p->x2 = vertices[i].cx1;
				p->y2 = vertices[i].cy1;
				PathPoint point = {};
				point.x0 = vertices[i].x;
				point.y0 = vertices[i].y;
				arrput(layerPath->points, point);
			} else {
				return false;
			}

			i++;
		}

		for (uintptr_t i = 0; i < arrlenu(layerPath->points); i++) {
			layerPath->points[i].x0 *= scale;
			if (layerPath->points[i].x1 != -1e6) layerPath->points[i].x1 *= scale;
			layerPath->points[i].x2 *= scale;
			layerPath->points[i].y0 *= scale;
			layerPath->points[i].y1 *= scale;
			layerPath->points[i].y2 *= scale;
		}

		stbtt_FreeShape(&font, vertices);
	}

	int ascent, descent, lineGap;
	stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);

	{
		Constant *constant = calloc(1, sizeof(Constant));
		constant->key.buffer = malloc(16);
		constant->key.byteCount = strlen(strcpy((char *) constant->key.buffer, "ascent"));
		constant->value.buffer = malloc(16);
		constant->value.byteCount = snprintf((char *) constant->value.buffer, 16, "%d", ascent);
		arrput(styleSet.constants, constant);
	}

	{
		Constant *constant = calloc(1, sizeof(Constant));
		constant->key.buffer = malloc(16);
		constant->key.byteCount = strlen(strcpy((char *) constant->key.buffer, "descent"));
		constant->value.buffer = malloc(16);
		constant->value.byteCount = snprintf((char *) constant->value.buffer, 16, "%d", descent);
		arrput(styleSet.constants, constant);
	}

	{
		Constant *constant = calloc(1, sizeof(Constant));
		constant->key.buffer = malloc(16);
		constant->key.byteCount = strlen(strcpy((char *) constant->key.buffer, "lineGap"));
		constant->value.buffer = malloc(16);
		constant->value.byteCount = snprintf((char *) constant->value.buffer, 16, "%d", lineGap);
		arrput(styleSet.constants, constant);
	}

	for (uintptr_t c1 = 0; charactersToImport[c1]; c1++) {
		for (uintptr_t c2 = 0; charactersToImport[c2]; c2++) {
			int xAdvance = stbtt_GetGlyphKernAdvance(&font, 
					stbtt_FindGlyphIndex(&font, charactersToImport[c1]), 
					stbtt_FindGlyphIndex(&font, charactersToImport[c2]));
			if (!xAdvance) continue;

			BasicFontKerningEntry entry = { 0 };
			entry.leftGlyphIndex = charactersToImport[c1];
			entry.rightGlyphIndex = charactersToImport[c2];
			entry.xAdvance = xAdvance;
			arrput(kerningEntries, entry);
		}
	}

	return true;
}

void ExportFont(const char *path) {
	FILE *f = fopen(path, "wb");

	BasicFontHeader header = { BASIC_FONT_SIGNATURE };

	for (uintptr_t i = 0; i < arrlenu(styleSet.constants); i++) {
		if (styleSet.constants[i]->key.byteCount == 6 && 0 == memcmp(styleSet.constants[i]->key.buffer, "ascent", 6)) {
			header.ascender = atoi((char *) styleSet.constants[i]->value.buffer);
		} else if (styleSet.constants[i]->key.byteCount == 7 && 0 == memcmp(styleSet.constants[i]->key.buffer, "descent", 7)) {
			header.descender = atoi((char *) styleSet.constants[i]->value.buffer);
		}
	}

	for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
		Style *style = styleSet.styles[i];

		if (style->name.byteCount > 6 && 0 == memcmp(style->name.buffer, "Glyph ", 6)) {
			header.glyphCount++;
		}
	}

	header.kerningEntries = arrlenu(kerningEntries);

	fwrite(&header, 1, sizeof(BasicFontHeader), f);

	uint32_t offsetToPoints = sizeof(BasicFontHeader) 
		+ sizeof(BasicFontGlyph) * header.glyphCount 
		+ sizeof(BasicFontKerningEntry) * header.kerningEntries;

	uint32_t *characters = NULL;

	for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
		Style *style = styleSet.styles[i];

		if (style->name.byteCount > 6 && 0 == memcmp(style->name.buffer, "Glyph ", 6)) {
			BasicFontGlyph glyph = { 0 };
			assert(arrlenu(style->layers) == 2);
			glyph.codepoint = ((char *) style->name.buffer)[6];
			LayerMetrics *metrics = &LayerLookup(style->layers[0])->base.metrics;
			LayerPath *path = &LayerLookup(style->layers[1])->base.path;
			glyph.xAdvance = metrics->preferredSize.width;
			glyph.xOffset = metrics->minimumSize.width;
			glyph.yOffset = metrics->minimumSize.height;
			glyph.width = metrics->maximumSize.width - metrics->minimumSize.width;
			glyph.height = metrics->maximumSize.height - metrics->minimumSize.height;
			glyph.pointCount = arrlenu(path->points);
			glyph.offsetToPoints = offsetToPoints;
			offsetToPoints += glyph.pointCount * 6 * sizeof(float);
			fwrite(&glyph, 1, sizeof(BasicFontGlyph), f);
			arrput(characters, glyph.codepoint);
		}
	}

	arrfree(characters);

	for (uintptr_t i = 0; i < arrlenu(kerningEntries); i++) {
		BasicFontKerningEntry *entry = kerningEntries + i;
		BasicFontKerningEntry copy = *entry;

		for (uintptr_t i = 0; i < arrlenu(characters); i++) {
			if (entry->leftGlyphIndex == characters[i]) {
				copy.leftGlyphIndex = i;
			}

			if (entry->rightGlyphIndex == characters[i]) {
				copy.rightGlyphIndex = i;
			}
		}

		fwrite(&copy, 1, sizeof(BasicFontKerningEntry), f);
	}

	for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
		Style *style = styleSet.styles[i];

		if (style->name.byteCount > 6 && 0 == memcmp(style->name.buffer, "Glyph ", 6)) {
			LayerPath *path = &LayerLookup(style->layers[1])->base.path;
			// printf("%ld at %ld, %ld\n", i, ftell(f), arrlenu(path->points));
			fwrite(path->points, 1, arrlenu(path->points) * 6 * sizeof(float), f);
		}
	}

	fclose(f);
}

void ButtonImportFontConfirm(void *_unused) {
	if (!importDialog) {
		return;
	}

	char path[4096];

	if (importPathTextbox->bytes > (ptrdiff_t) sizeof(path) - 1) {
		UILabelSetContent(importPathMessage, "Path too long", -1);
		UIElementRefresh(&importPathMessage->e);
		return;
	}

	memcpy(path, importPathTextbox->string, importPathTextbox->bytes);
	path[importPathTextbox->bytes] = 0;

	char *fileData = LoadFile(path, NULL);

	if (fileData && ImportFont(fileData)) {
		ConstantListRefresh();
		ClearUndoRedo();
		StyleListRefresh();
		UIElementDestroy(&importDialog->e);
		importDialog = NULL;
	} else {
		UILabelSetContent(importPathMessage, "Invalid or unsupported font file.", -1);
		UIElementRefresh(&importPathMessage->e);
	}

	free(fileData);
}

void ActionImportFont(void *_unused) {
	if (importDialog) {
		return;
	}

	importDialog = UIWindowCreate(window, UI_WINDOW_CENTER_IN_OWNER, "Import Font", 400, 100);

	UIPanelCreate(&importDialog->e, UI_PANEL_GRAY | UI_ELEMENT_PARENT_PUSH | UI_PANEL_EXPAND | UI_PANEL_MEDIUM_SPACING);
		UILabelCreate(0, 0, "Enter path to font file:", -1);
		importPathTextbox = UITextboxCreate(0, 0);

		UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
			importPathMessage = UILabelCreate(0, UI_ELEMENT_H_FILL, "", -1);
			UIButtonCreate(0, 0, "Import", -1)->invoke = ButtonImportFontConfirm;
		UIParentPop();
	UIParentPop();

	UIElementFocus(&importPathTextbox->e);
}

// ------------------- Importing SVG -------------------

void ImportSVGPath(NSVGimage *image, NSVGshape *shape) {
	// TODO If shape has multiple paths, and has a contour fills,
	// 	then the contours need to use each path separately.

	if ((~shape->flags & NSVG_FLAGS_VISIBLE) || shape->opacity == 0) {
		return;
	}

	ModData mod = { 0 };
	mod.tag = ModData_addLayer + 1;
	mod.addLayer.index = arrlen(selected.style->layers);

	Layer *layer = calloc(1, sizeof(Layer));
	layer->id = ++styleSet.lastID;
	layer->name.buffer = malloc(16);
	layer->name.byteCount = snprintf(layer->name.buffer, 16, "path %d", mod.addLayer.index);
	layer->base.tag = LayerBase_path + 1;
	layer->mode = LAYER_MODE_BACKGROUND;
	layer->position.r = 100;
	layer->position.b = 100;

	LayerPath *layerPath = &layer->base.path;
	layerPath->evenOdd = shape->fillRule == NSVG_FILLRULE_EVENODD;
	layerPath->closed = shape->paths->closed; // TODO See comment about multiple paths above.
	layerPath->alpha = shape->opacity * 255;

	float scale = 100.0f / image->width;

	NSVGpath *path = shape->paths;

	while (path) {
		for (int i = 0; i < path->npts; i += 3) {
			PathPoint point = {};

			point.x0 = path->pts[i * 2 + 0] * scale;
			point.y0 = path->pts[i * 2 + 1] * scale;

			if (i + 1 < path->npts) {
				point.x1 = path->pts[i * 2 + 2] * scale;
				point.y1 = path->pts[i * 2 + 3] * scale;
			} else {
				point.x1 = path->pts[i * 2 + 0] * scale;
				point.y1 = path->pts[i * 2 + 1] * scale;
			}

			if (i + 2 < path->npts) {
				point.x2 = path->pts[i * 2 + 4] * scale;
				point.y2 = path->pts[i * 2 + 5] * scale;
			} else {
				point.x2 = path->pts[i * 2 + 0] * scale;
				point.y2 = path->pts[i * 2 + 1] * scale;
			}

			arrput(layerPath->points, point);
		}

		if (arrlenu(layerPath->points) && (path->closed || path->next)) {
			PathPoint point = layerPath->points[0];
			point.x1 = -1e6;
			point.x2 = point.y1 = point.y2 = 0;
			arrput(layerPath->points, point);
		}

		path = path->next;
	}

	for (uintptr_t i = 0; i < 2; i++) {
		NSVGpaint *paint = i ? &shape->stroke : &shape->fill;

		PathFill fill = {};

		if (i) {
			// TODO Dashes.
			
			fill.mode.tag = PathFillMode_contour + 1;
			fill.mode.contour.internalWidth = shape->strokeWidth * 0.5f + 0.5f; // TODO Floating-point contour widths?
			fill.mode.contour.externalWidth = shape->strokeWidth * 0.5f;
			fill.mode.contour.joinMode = shape->strokeLineJoin == NSVG_JOIN_ROUND ? JOIN_MODE_ROUND 
				: shape->strokeLineJoin == NSVG_JOIN_BEVEL ? JOIN_MODE_BEVEL : JOIN_MODE_MITER;
			fill.mode.contour.capMode = shape->strokeLineCap == NSVG_CAP_BUTT ? CAP_MODE_FLAT 
				: shape->strokeLineCap == NSVG_CAP_ROUND ? CAP_MODE_ROUND : CAP_MODE_SQUARE;
			fill.mode.contour.miterLimit = shape->strokeWidth * shape->miterLimit; 
		} else {
			fill.mode.tag = PathFillMode_solid + 1;
		}

		if (paint->type == NSVG_PAINT_COLOR) {
			fill.paint.tag = Paint_solid + 1;
			fill.paint.solid.color = (paint->color & 0xFF00FF00) | ((paint->color & 0xFF) << 16) | ((paint->color & 0xFF0000) >> 16);
		} else if (paint->type == NSVG_PAINT_LINEAR_GRADIENT) {
			NSVGgradient *gradient = paint->gradient;
			fill.paint.tag = Paint_linearGradient + 1;

			fill.paint.linearGradient.transformX = gradient->xform[1] * image->width;
			fill.paint.linearGradient.transformY = gradient->xform[3] * image->height;
			fill.paint.linearGradient.transformStart = gradient->xform[5];

			size_t stopCount = gradient->nstops;
			if (stopCount > 16) stopCount = 16;

			for (int i = 0; i < gradient->nstops; i++) {
				GradientStop stop = {};
				uint32_t color = gradient->stops[i].color;
				stop.color = (color & 0xFF00FF00) | ((color & 0xFF) << 16) | ((color & 0xFF0000) >> 16);
				stop.position = gradient->stops[i].offset * 100;
				arrput(fill.paint.linearGradient.stops, stop);
			}

			if (gradient->spread == NSVG_SPREAD_PAD) {
				fill.paint.linearGradient.repeat = GRADIENT_REPEAT_CLAMP;
			} else if (gradient->spread == NSVG_SPREAD_REFLECT) {
				fill.paint.linearGradient.repeat = GRADIENT_REPEAT_MIRROR;
			} else if (gradient->spread == NSVG_SPREAD_REPEAT) {
				fill.paint.linearGradient.repeat = GRADIENT_REPEAT_NORMAL;
			}
		} else if (paint->type == NSVG_PAINT_RADIAL_GRADIENT) {
			NSVGgradient *gradient = paint->gradient;
			fill.paint.tag = Paint_radialGradient + 1;

			fill.paint.radialGradient.transform0 = gradient->xform[0] * image->width;
			fill.paint.radialGradient.transform1 = gradient->xform[2] * image->width;
			fill.paint.radialGradient.transform2 = gradient->xform[4];
			fill.paint.radialGradient.transform3 = gradient->xform[1] * image->height;
			fill.paint.radialGradient.transform4 = gradient->xform[3] * image->height;
			fill.paint.radialGradient.transform5 = gradient->xform[5];

			size_t stopCount = gradient->nstops;
			if (stopCount > 16) stopCount = 16;

			for (int i = 0; i < gradient->nstops; i++) {
				GradientStop stop = {};
				uint32_t color = gradient->stops[i].color;
				stop.color = (color & 0xFF00FF00) | ((color & 0xFF) << 16) | ((color & 0xFF0000) >> 16);
				stop.position = gradient->stops[i].offset * 100;
				arrput(fill.paint.radialGradient.stops, stop);
			}

			if (gradient->spread == NSVG_SPREAD_PAD) {
				fill.paint.radialGradient.repeat = GRADIENT_REPEAT_CLAMP;
			} else if (gradient->spread == NSVG_SPREAD_REFLECT) {
				fill.paint.radialGradient.repeat = GRADIENT_REPEAT_MIRROR;
			} else if (gradient->spread == NSVG_SPREAD_REPEAT) {
				fill.paint.radialGradient.repeat = GRADIENT_REPEAT_NORMAL;
			}
		}

		arrput(layerPath->fills, fill);
	}

	mod.addLayer.layer = layer;
	ModApply(&mod);
}

void ImportSVGImage(NSVGimage *image) {
	previewWidth->position = image->width / 1000.0f;
	previewHeight->position = image->height / 1000.0f;

	NSVGshape *shape = image->shapes;

	while (shape) {
		if (shape->paths) {
			ImportSVGPath(image, shape);
		}

		shape = shape->next;
	}
}

void ButtonImportSVGConfirm(void *_unused) {
	if (!importDialog) {
		return;
	}

	char path[4096];

	if (importPathTextbox->bytes > (ptrdiff_t) sizeof(path) - 1) {
		UILabelSetContent(importPathMessage, "Path too long", -1);
		UIElementRefresh(&importPathMessage->e);
		return;
	}

	memcpy(path, importPathTextbox->string, importPathTextbox->bytes);
	path[importPathTextbox->bytes] = 0;

	NSVGimage *image = nsvgParseFromFile(path, "px", 96.0f);

	if (image) {
		UIElementDestroy(&importDialog->e);
		importDialog = NULL;
		ImportSVGImage(image);
		nsvgDelete(image);
	} else {
		UILabelSetContent(importPathMessage, "Invalid or unsupported SVG file.", -1);
		UIElementRefresh(&importPathMessage->e);
	}
}

void ButtonImportSVG(void *_unused) {
	if (importDialog || !selected.style) {
		return;
	}

	importDialog = UIWindowCreate(window, UI_WINDOW_CENTER_IN_OWNER, "Import SVG", 400, 100);

	UIPanelCreate(&importDialog->e, UI_PANEL_GRAY | UI_ELEMENT_PARENT_PUSH | UI_PANEL_EXPAND | UI_PANEL_MEDIUM_SPACING);
		UILabelCreate(0, 0, "Enter path to SVG file:", -1);
		importPathTextbox = UITextboxCreate(0, 0);

		UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
			importPathMessage = UILabelCreate(0, UI_ELEMENT_H_FILL, "", -1);
			UIButtonCreate(0, 0, "Import", -1)->invoke = ButtonImportSVGConfirm;
		UIParentPop();
	UIParentPop();

	UIElementFocus(&importPathTextbox->e);
}

// ------------------- Layers -------------------

void CleanupUnusedLayers(void *_unused) {
	ClearUndoRedo();

	struct {
		uint64_t key;
		bool value;
	} *usedLayers = NULL;

	for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
		for (uintptr_t j = 0; j < arrlenu(styleSet.styles[i]->layers); j++) {
			hmput(usedLayers, styleSet.styles[i]->layers[j], true);
		}
	}

	for (uintptr_t j = 0; j < arrlenu(styleSet.layers); j++) {
		if (hmget(usedLayers, styleSet.layers[j]->id)) {
			continue;
		}

		printf("remove %ld\n", styleSet.layers[j]->id);

		RfState state = { 0 };
		state.op = RF_OP_FREE;
		state.allocate = RfRealloc;
		RfItem item = { 0 };
		item.type = &Layer_Type;
		item.byteCount = sizeof(Layer);
		item.type->op(&state, &item, styleSet.layers[j]);

		arrdel(styleSet.layers, j);
		j--;
	}
}

int TableLayersMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		Layer *l = LayerLookup(selected.style->layers[m->index]);
		m->isSelected = selected.layer == l;
		return snprintf(m->buffer, m->bufferBytes, "%.*s", (int) l->name.byteCount, (char *) l->name.buffer);
	} else if (message == UI_MSG_CLICKED || message == UI_MSG_MOUSE_DRAG) {
		int index = UITableHitTest(tableLayers, element->window->cursorX, element->window->cursorY);

		if (index != -1) {
			SetSelectedItems(MOD_CONTEXT(selected.style, LayerLookup(selected.style->layers[index]), NULL, NULL));
		}
	} else if (message == MSG_PROPERTY_CHANGED) {
		UITableResizeColumns(tableLayers);
		UIElementRefresh(&tableLayers->e);
	}

	return 0;
}

void ModAddLayerOp(RfState *state, RfItem *item, void *pointer) {
	ModAddLayer *mod = (ModAddLayer *) pointer;

	if (state->op == OP_DO_MOD) {
		ModData undo = { 0 };
		undo.tag = ModData_deleteLayer + 1;
		undo.deleteLayer.index = mod->index;
		ModPushUndo(&undo);

		arrins(selected.style->layers, mod->index, mod->layer->id);
		arrput(styleSet.layers, mod->layer);

		SetSelectedItems(MOD_CONTEXT(selected.style, mod->layer, NULL, NULL));
	} else {
		RfStructOp(state, item, pointer);
	}
}

void ButtonAddBoxLayer(void *_unused) {
	if (!selected.style) return;

	ModData mod = { 0 };
	mod.tag = ModData_addLayer + 1;
	mod.addLayer.index = arrlen(selected.style->layers);

	Layer *layer = calloc(1, sizeof(Layer));
	layer->id = ++styleSet.lastID;
	layer->name.buffer = malloc(16);
	layer->name.byteCount = snprintf(layer->name.buffer, 16, "box %d", mod.addLayer.index);
	layer->base.tag = LayerBase_box + 1;
	layer->position.r = 100;
	layer->position.b = 100;

	mod.addLayer.layer = layer;

	ModApply(&mod);
}

void ButtonAddTextLayer(void *_unused) {
	if (!selected.style) return;

	ModData mod = { 0 };
	mod.tag = ModData_addLayer + 1;
	mod.addLayer.index = arrlen(selected.style->layers);

	Layer *layer = calloc(1, sizeof(Layer));
	layer->id = ++styleSet.lastID;
	layer->name.buffer = malloc(16);
	layer->name.byteCount = snprintf(layer->name.buffer, 16, "text %d", mod.addLayer.index);
	layer->base.tag = LayerBase_text + 1;
	layer->mode = LAYER_MODE_CONTENT;

	mod.addLayer.layer = layer;

	ModApply(&mod);
}

void ButtonAddPathLayer(void *_unused) {
	if (!selected.style) return;

	ModData mod = { 0 };
	mod.tag = ModData_addLayer + 1;
	mod.addLayer.index = arrlen(selected.style->layers);

	Layer *layer = calloc(1, sizeof(Layer));
	layer->id = ++styleSet.lastID;
	layer->name.buffer = malloc(16);
	layer->name.byteCount = snprintf(layer->name.buffer, 16, "path %d", mod.addLayer.index);
	layer->base.tag = LayerBase_path + 1;
	layer->mode = LAYER_MODE_BACKGROUND;

	mod.addLayer.layer = layer;

	ModApply(&mod);
}

void ButtonAddLayer(void *_unused) {
	UIMenu *menu = UIMenuCreate(&buttonAddLayer->e, 0);
	UIMenuAddItem(menu, 0, "Add box...", -1, ButtonAddBoxLayer, NULL);
	UIMenuAddItem(menu, 0, "Add text...", -1, ButtonAddTextLayer, NULL);
	UIMenuAddItem(menu, 0, "Add path...", -1, ButtonAddPathLayer, NULL);
	UIMenuShow(menu);
}

void ButtonDuplicateLayer(void *_unused) {
	if (!selected.layer) return;
	RfGrowableBuffer state = { 0 };
	state.data = SaveToGrowableBuffer(&Layer_Type, sizeof(Layer), NULL, selected.layer);
	ModData mod = { 0 };
	mod.tag = ModData_addLayer + 1;
	mod.addLayer.index = arrlen(selected.style->layers);
	Layer *layer = calloc(1, sizeof(Layer));
	state.s.version = saveFormatVersion;
	state.data.byteCount -= sizeof(uint32_t);
	state.s.allocate = RfRealloc;
	state.s.access = RfReadGrowableBuffer;
	RfItem item = { 0 };
	item.type = &Layer_Type;
	item.byteCount = sizeof(Layer);
	state.s.op = RF_OP_LOAD;
	item.type->op(&state.s, &item, layer);
	layer->id = ++styleSet.lastID;
	mod.addLayer.layer = layer;
	ModApply(&mod);
}

void ButtonAddExistingLayer2(void *_layer) {
	if (!selected.style) return;

	ModData mod = { 0 };
	mod.tag = ModData_addLayer + 1;
	mod.addLayer.index = arrlen(selected.style->layers);
	mod.addLayer.layer = (Layer *) _layer;

	ModApply(&mod);
}

void ButtonAddExistingLayer(void *_unused) {
	UIMenu *menu = UIMenuCreate(&buttonAddExistingLayer->e, 0);

	for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
		Style *style = styleSet.styles[i];

		for (uintptr_t j = 0; j < arrlenu(style->layers); j++) {
			Layer *layer = LayerLookup(style->layers[j]);
			if (!layer) continue;
			char name[64];
			snprintf(name, sizeof(name), "%.*s:%.*s", (int) style->name.byteCount, (char *) style->name.buffer, 
					(int) layer->name.byteCount, (char *) layer->name.buffer);
			UIMenuAddItem(menu, 0, name, -1, ButtonAddExistingLayer2, layer);
		}
	}

	UIMenuShow(menu);
}

void ModDeleteLayerOp(RfState *state, RfItem *item, void *pointer) {
	ModDeleteLayer *mod = (ModDeleteLayer *) pointer;

	if (state->op == OP_DO_MOD) {
		ModData undo = { 0 };
		undo.tag = ModData_addLayer + 1;
		undo.addLayer.layer = LayerLookup(selected.style->layers[mod->index]);
		undo.addLayer.index = mod->index;
		ModPushUndo(&undo);
		arrdel(selected.style->layers, mod->index);

		if ((uintptr_t) mod->index < arrlenu(selected.style->layers)) {
			SetSelectedItems(MOD_CONTEXT(selected.style, LayerLookup(selected.style->layers[mod->index]), NULL, NULL));
		} else if (arrlenu(selected.style->layers)) {
			SetSelectedItems(MOD_CONTEXT(selected.style, LayerLookup(arrlast(selected.style->layers)), NULL, NULL));
		} else {
			SetSelectedItems(MOD_CONTEXT(selected.style, NULL, NULL, NULL));
		}
	} else {
		RfStructOp(state, item, pointer);
	}
}

void ButtonDeleteLayer(void *_unused) {
	if (!selected.layer) return;

	ModData mod = { 0 };
	mod.tag = ModData_deleteLayer + 1;
	bool found = false;

	for (uintptr_t i = 0; i < arrlenu(selected.style->layers); i++) {
		if (selected.style->layers[i] == selected.layer->id) {
			mod.deleteLayer.index = i;
			found = true;
			break;
		}
	}

	assert(found);
	ModApply(&mod);
}

void ButtonDeleteLayerInAllStyles(void *_unused) {
	if (!selected.layer) {
		return;
	}

	ClearUndoRedo();
	
	for (uintptr_t i = 0; i < arrlenu(styleSet.styles); i++) {
		for (uintptr_t j = 0; j < arrlenu(styleSet.styles[i]->layers); j++) {
			if (styleSet.styles[i]->layers[j] == selected.layer->id) {
				arrdel(styleSet.styles[i]->layers, j);
				j--;
			}
		}
	}

	bool found = false;

	for (uintptr_t i = 0; i < arrlenu(styleSet.layers); i++) {
		if (styleSet.layers[i] == selected.layer) {
			arrdel(styleSet.layers, i);
			found = true;
			break;
		}
	}

	assert(found);

	RfState state = { 0 };
	state.op = RF_OP_FREE;
	state.allocate = RfRealloc;
	RfItem item = { 0 };
	item.type = &Layer_Type;
	item.byteCount = sizeof(Layer);
	item.type->op(&state, &item, selected.layer);

	StyleListRefresh();
	SetSelectedItems(MOD_CONTEXT(selected.style, NULL, NULL, NULL));
}

void ModSwapLayersOp(RfState *state, RfItem *item, void *pointer) {
	ModSwapLayers *mod = (ModSwapLayers *) pointer;

	if (state->op == OP_DO_MOD) {
		assert(mod->index >= 0 && mod->index < arrlen(selected.style->layers) - 1);

		ModData undo = { 0 };
		undo.tag = ModData_swapLayers + 1;
		undo.swapLayers.index = mod->index;
		ModPushUndo(&undo);

		uint64_t temporary = selected.style->layers[mod->index];
		selected.style->layers[mod->index] = selected.style->layers[mod->index + 1];
		selected.style->layers[mod->index + 1] = temporary;

		UIElementRefresh(&tableLayers->e);
	} else {
		RfStructOp(state, item, pointer);
	}
}

// ------------------- Sequences -------------------

int TableSequencesMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		Sequence *s = selected.layer->sequences[m->index];
		m->isSelected = selected.sequence == s;
		return snprintf(m->buffer, m->bufferBytes, "%s%s%s%s%s%s%s%s%s%s", 
			((StringOption *) PrimaryState_Type.fields[s->primaryState].item.options)->string,
			s->flagFocused ? " (focused)" : "",
			s->flagChecked ? " (checked)" : "",
			s->flagIndeterminate ? " (indeterminate)" : "",
			s->flagDefault ? " (default)" : "",
			s->flagItemFocus ? " (list item focus)" : "",
			s->flagListFocus ? " (list focus)" : "",
			s->flagBeforeEnter ? " (before enter)" : "",
			s->flagAfterExit ? " (after exit)" : "",
			s->flagSelected ? " (selected)" : "");
	} else if (message == UI_MSG_CLICKED || message == UI_MSG_MOUSE_DRAG) {
		int index = UITableHitTest(tableSequences, element->window->cursorX, element->window->cursorY);

		if (index != -1) {
			SetSelectedItems(MOD_CONTEXT(selected.style, selected.layer, selected.layer->sequences[index], NULL));
		}
	} else if (message == MSG_PROPERTY_CHANGED) {
		UITableResizeColumns(tableSequences);
		UIElementRefresh(&tableSequences->e);
	}

	return 0;
}

void ModAddSequenceOp(RfState *state, RfItem *item, void *pointer) {
	ModAddSequence *mod = (ModAddSequence *) pointer;

	if (state->op == OP_DO_MOD) {
		DoAddItemMod((void ***) &selected.layer->sequences, mod->sequence, mod->index, ModData_deleteSequence);
		SetSelectedItems(MOD_CONTEXT(selected.style, selected.layer, mod->sequence, NULL));
	} else {
		RfStructOp(state, item, pointer);
	}
}

void ButtonAddSequence(void *_unused) {
	if (!selected.layer) return;
	ModData mod = { 0 };
	mod.tag = ModData_addSequence + 1;
	mod.addSequence.sequence = calloc(1, sizeof(Sequence));
	mod.addSequence.index = arrlen(selected.layer->sequences);
	ModApply(&mod);
}

void ModDeleteSequenceOp(RfState *state, RfItem *item, void *pointer) {
	ModDeleteSequence *mod = (ModDeleteSequence *) pointer;

	if (state->op == OP_DO_MOD) {
		DoDeleteItemMod((void ***) &selected.layer->sequences, mod->index, ModData_addSequence);
		SetSelectedItems(MOD_CONTEXT(selected.style, selected.layer, NULL, NULL));
	} else {
		RfStructOp(state, item, pointer);
	}
}

void ButtonDeleteSequence(void *_unused) {
	if (!selected.sequence) return;
	ButtonDeleteItem(selected.sequence, (void **) selected.layer->sequences, ModData_deleteSequence);
}

void ButtonMoveSequenceUp(void *_unused) {
	if (!selected.sequence) {
		return;
	}

	uintptr_t index = 0;

	for (uintptr_t i = 0; i < arrlenu(selected.layer->sequences); i++) {
		if (selected.layer->sequences[i] == selected.sequence) {
			index = i;
			break;
		}
	}

	if (index <= 1) {
		return;
	}

	ModData mod = { 0 };
	mod.tag = ModData_swapSequences + 1;
	mod.swapSequences.index = index - 1;
	ModApply(&mod);
}

void ButtonMoveSequenceDown(void *_unused) {
	if (!selected.sequence) {
		return;
	}

	uintptr_t index = 0;

	for (uintptr_t i = 0; i < arrlenu(selected.layer->sequences); i++) {
		if (selected.layer->sequences[i] == selected.sequence) {
			index = i;
			break;
		}
	}

	if (index == arrlenu(selected.layer->sequences) - 1) {
		return;
	}

	ModData mod = { 0 };
	mod.tag = ModData_swapSequences + 1;
	mod.swapSequences.index = index;
	ModApply(&mod);
}

void ModSwapSequencesOp(RfState *state, RfItem *item, void *pointer) {
	ModSwapSequences *mod = (ModSwapSequences *) pointer;

	if (state->op == OP_DO_MOD) {
		assert(mod->index >= 0 && mod->index < arrlen(selected.layer->sequences) - 1);

		ModData undo = { 0 };
		undo.tag = ModData_swapSequences + 1;
		undo.swapSequences.index = mod->index;
		ModPushUndo(&undo);

		Sequence *temporary = selected.layer->sequences[mod->index];
		selected.layer->sequences[mod->index] = selected.layer->sequences[mod->index + 1];
		selected.layer->sequences[mod->index + 1] = temporary;

		UIElementRefresh(&tableSequences->e);
	} else {
		RfStructOp(state, item, pointer);
	}
}

// ------------------- Keyframes -------------------

int TableKeyframesMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		Keyframe *k = selected.sequence->keyframes[m->index];
		m->isSelected = selected.keyframe == k;
		return snprintf(m->buffer, m->bufferBytes, "%d%%", k->progress);
	} else if (message == UI_MSG_CLICKED || message == UI_MSG_MOUSE_DRAG) {
		int index = UITableHitTest(tableKeyframes, element->window->cursorX, element->window->cursorY);

		if (index != -1) {
			SetSelectedItems(MOD_CONTEXT(selected.style, selected.layer, selected.sequence, selected.sequence->keyframes[index]));
		}
	} else if (message == MSG_PROPERTY_CHANGED) {
		UITableResizeColumns(tableKeyframes);
		UIElementRefresh(&tableKeyframes->e);
	}

	return 0;
}

void ModAddKeyframeOp(RfState *state, RfItem *item, void *pointer) {
	ModAddKeyframe *mod = (ModAddKeyframe *) pointer;

	if (state->op == OP_DO_MOD) {
		DoAddItemMod((void ***) &selected.sequence->keyframes, mod->keyframe, mod->index, ModData_deleteKeyframe);
		SetSelectedItems(MOD_CONTEXT(selected.style, selected.layer, selected.sequence, mod->keyframe));
	} else {
		RfStructOp(state, item, pointer);
	}
}

void ButtonAddKeyframe(void *_unused) {
	if (!selected.sequence) return;
	ModData mod = { 0 };
	mod.tag = ModData_addKeyframe + 1;
	mod.addKeyframe.keyframe = calloc(1, sizeof(Keyframe));
	mod.addKeyframe.index = arrlen(selected.sequence->keyframes);
	mod.addKeyframe.keyframe->progress = 100;
	ModApply(&mod);
}

void ModDeleteKeyframeOp(RfState *state, RfItem *item, void *pointer) {
	ModDeleteKeyframe *mod = (ModDeleteKeyframe *) pointer;

	if (state->op == OP_DO_MOD) {
		DoDeleteItemMod((void ***) &selected.sequence->keyframes, mod->index, ModData_addKeyframe);
		SetSelectedItems(MOD_CONTEXT(selected.style, selected.layer, selected.sequence, NULL));
	} else {
		RfStructOp(state, item, pointer);
	}
}

void ButtonDeleteKeyframe(void *_unused) {
	if (!selected.keyframe) return;
	ButtonDeleteItem(selected.keyframe, (void **) selected.sequence->keyframes, ModData_deleteKeyframe);
}

// ------------------- Actions -------------------

void ActionSave(void *_unused) {
	RfData data = SaveToGrowableBuffer(&StyleSet_Type, sizeof(styleSet), NULL, &styleSet);
	FILE *f = fopen(filePath, "wb");
	fwrite(&saveFormatVersion, 1, sizeof(uint32_t), f);
	fwrite(data.buffer, 1, data.byteCount, f);
	fclose(f);
	free(data.buffer);
}

char *LoadFile(const char *inputFileName, size_t *byteCount) {
	FILE *inputFile = fopen(inputFileName, "rb");
	
	if (!inputFile) {
		return NULL;
	}
	
	fseek(inputFile, 0, SEEK_END);
	size_t inputFileBytes = ftell(inputFile);
	fseek(inputFile, 0, SEEK_SET);
	
	char *inputBuffer = (char *) malloc(inputFileBytes + 1);
	size_t inputBytesRead = fread(inputBuffer, 1, inputFileBytes, inputFile);
	inputBuffer[inputBytesRead] = 0;
	fclose(inputFile);
	
	if (byteCount) *byteCount = inputBytesRead;
	return inputBuffer;
}

void ActionLoad(void *_unused) {
	selectedConstant = NULL;

	RfGrowableBuffer state = { 0 };
	uint32_t *buffer = (uint32_t *) LoadFile(filePath, &state.data.byteCount);

	if (state.data.byteCount > sizeof(uint32_t)) {
		state.s.version = *buffer;
		state.data.buffer = buffer + 1;
		state.data.byteCount -= sizeof(uint32_t);
		state.s.allocate = RfRealloc;
		state.s.access = RfReadGrowableBuffer;

		RfItem item = { 0 };
		item.type = &StyleSet_Type;
		item.byteCount = sizeof(styleSet);
		state.s.op = RF_OP_FREE;
		item.type->op(&state.s, &item, &styleSet);
		state.s.op = RF_OP_LOAD;
		item.type->op(&state.s, &item, &styleSet);

		if (state.s.error) {
			state.s.op = RF_OP_FREE;
			state.s.error = false;
			item.type->op(&state.s, &item, &styleSet);
		} else {
			if (state.s.version <= 17) {
				RfState state = { 0 };
				state.op = OP_GET_PALETTE;
				RfItem item = { 0 };
				item.type = &StyleSet_Type;
				item.byteCount = sizeof(StyleSet);
				item.options = NULL;
				RfBroadcast(&state, &item, &styleSet, true);

				for (uintptr_t i = 0; i < hmlenu(palette); i++) {
					fprintf(stderr, "%.8X (%d)\n", palette[i].key, palette[i].value);

					char name[16];
					snprintf(name, sizeof(name), "Color %d", (int) i + 1);
					Color *color = calloc(1, sizeof(Color));
					color->key.buffer = strdup(name);
					color->key.byteCount = strlen(name);
					color->value = palette[i].key;
					color->id = i + 1;
					arrput(styleSet.colors, color);

					state.op = OP_REPLACE_COLOR;
					replaceColorFrom = color->value;
					replaceColorTo = color->id;
					RfBroadcast(&state, &item, &styleSet, true);
				}

				hmfree(palette);
			} 

			if (state.s.version <= 18) {
				char name[16];
				snprintf(name, sizeof(name), "Uninitialised");
				Color *color = calloc(1, sizeof(Color));
				color->key.buffer = strdup(name);
				color->key.byteCount = strlen(name);
				color->value = 0;
				color->id = 0;
				arrput(styleSet.colors, color);
			}
		}
	}

	free(buffer);
	SetSelectedItems(MOD_CONTEXT(NULL, NULL, NULL, NULL));
	StyleListRefresh();
	ConstantListRefresh();
	ColorListRefresh();
	UIElementRepaint(elementCanvas, NULL);
}

void ActionUndo(void *_unused) {
	if (!arrlen(undoStack)) return;
	modApplyUndo = true;
	Mod mod = arrpop(undoStack);
	_ModApply(&mod);
}

void ActionRedo(void *_unused) {
	if (!arrlen(redoStack)) return;
	modApplyUndo = false;
	Mod mod = arrpop(redoStack);
	_ModApply(&mod);
}

// ------------------- Initialisation -------------------

#ifdef _WIN32
int WinMain(void *, void *, char *, int)
#else
int main(int argc, char **argv)
#endif
{
	if (argc == 4 && 0 == strcmp(argv[1], "--make-font")) {
		bool success = ImportFont(LoadFile(argv[2], NULL));
		if (success) ExportFont(argv[3]);
		return success ? 0 : 1;
	}

	if (argc < 3 || argc > 5) {
		fprintf(stderr, "Usage: %s <source path> <export path> <optional: embed bitmap path> <optional: styles header path>\n", argv[0]);
		exit(1);
	}

	filePath = argv[1];
	exportPath = argv[2];
	embedBitmapPath = argc >= 4 ? argv[3] : NULL;
	stylesPath = argc >= 5 ? argv[4] : NULL;

	UIInitialise();

	window = UIWindowCreate(0, 0, "Designer", 1600, 900);

	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_LETTER('S'), true, false, false, ActionSave, NULL));
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_LETTER('E'), true, false, false, ActionExport, NULL));
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_LETTER('Z'), true, false, false, ActionUndo, NULL));
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_LETTER('Y'), true, false, false, ActionRedo, NULL));

	UISplitPane *splitPane1 = UISplitPaneCreate(&window->e, UI_ELEMENT_V_FILL, 0.25f);

	UIPanel *panel1 = UIPanelCreate(&splitPane1->e, UI_PANEL_EXPAND);

	UIPanelCreate(&panel1->e, UI_PANEL_GRAY | UI_PANEL_HORIZONTAL | UI_PANEL_MEDIUM_SPACING | UI_ELEMENT_PARENT_PUSH);
		UIButtonCreate(0, 0, "Save", -1)->invoke = ActionSave;
		UIButtonCreate(0, 0, "Load", -1)->invoke = ActionLoad;
		UIButtonCreate(0, 0, "Export", -1)->invoke = ActionExport;
		UIButtonCreate(0, 0, "Export for Designer2", -1)->invoke = ActionExportDesigner2;
		UIButtonCreate(0, 0, "Import font", -1)->invoke = ActionImportFont;
	UIParentPop();

	UITabPaneCreate(&panel1->e, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_V_FILL, "Layers\tAnimation\tConstants\tColors");
		UISplitPaneCreate(0, UI_SPLIT_PANE_VERTICAL | UI_ELEMENT_PARENT_PUSH, 0.5f);
			UIPanelCreate(0, UI_PANEL_GRAY | UI_PANEL_EXPAND | UI_PANEL_MEDIUM_SPACING | UI_ELEMENT_PARENT_PUSH);
				UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
					stylesTextbox = UITextboxCreate(0, UI_ELEMENT_H_FILL);
					UIButtonCreate(0, UI_BUTTON_SMALL, "Create", -1)->invoke = ButtonCreateStyle;
					UIButtonCreate(0, UI_BUTTON_SMALL, "Rename", -1)->invoke = ButtonRenameStyle;
					UIButtonCreate(0, UI_BUTTON_SMALL, "Delete", -1)->invoke = ButtonDeleteStyle;
					buttonPublicStyle = UIButtonCreate(0, UI_BUTTON_SMALL, "Public", -1);
					buttonPublicStyle->invoke = ButtonTogglePublicStyle;
				UIParentPop();

				stylesShowWithSelectedLayer = UIButtonCreate(0, 0, "Show styles with selected layer", -1);
				stylesShowWithSelectedLayer->invoke = ButtonShowStylesWithSelectedLayer;

				UIButtonCreate(0, 0, "Cleanup unused layers", -1)->invoke = CleanupUnusedLayers;

				stylesTable = UITableCreate(0, UI_ELEMENT_V_FILL, "Name");
				stylesTable->e.messageUser = StylesTableMessage;
				UITableResizeColumns(stylesTable);
			UIParentPop();

			UIPanelCreate(0, UI_PANEL_GRAY | UI_PANEL_EXPAND | UI_ELEMENT_PARENT_PUSH);
				tableLayers = UITableCreate(0, UI_ELEMENT_V_FILL, "Layers");
				tableLayers->e.messageUser = TableLayersMessage;
				UITableResizeColumns(tableLayers);
				arrput(inspectorSubscriptions, &tableLayers->e);

				UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_PANEL_SMALL_SPACING | UI_ELEMENT_PARENT_PUSH);
					buttonAddLayer = UIButtonCreate(0, UI_BUTTON_SMALL, "Add...", -1);
					buttonAddLayer->invoke = ButtonAddLayer;
					UIButtonCreate(0, UI_BUTTON_SMALL, "Delete", -1)->invoke = ButtonDeleteLayer;
					buttonAddExistingLayer = UIButtonCreate(0, UI_BUTTON_SMALL, "Use existing", -1);
					buttonAddExistingLayer->invoke = ButtonAddExistingLayer;
					UIButtonCreate(0, UI_BUTTON_SMALL, "Duplicate", -1)->invoke = ButtonDuplicateLayer;
					UIButtonCreate(0, UI_BUTTON_SMALL, "Import SVG", -1)->invoke = ButtonImportSVG;
				UIParentPop();

				UIButtonCreate(0, UI_BUTTON_SMALL, "Delete layer in all styles", -1)->invoke = ButtonDeleteLayerInAllStyles;
			UIParentPop();
		UIParentPop();

		UISplitPaneCreate(0, UI_SPLIT_PANE_VERTICAL | UI_ELEMENT_PARENT_PUSH, 0.5f);
			UIPanelCreate(0, UI_PANEL_GRAY | UI_PANEL_EXPAND | UI_ELEMENT_PARENT_PUSH);
				tableSequences = UITableCreate(0, UI_ELEMENT_V_FILL, "Sequences");
				tableSequences->e.messageUser = TableSequencesMessage;
				UITableResizeColumns(tableSequences);
				arrput(inspectorSubscriptions, &tableSequences->e);

				UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_PANEL_SMALL_SPACING | UI_ELEMENT_PARENT_PUSH);
					UIButtonCreate(0, UI_BUTTON_SMALL, "Add", -1)->invoke = ButtonAddSequence;
					UIButtonCreate(0, UI_BUTTON_SMALL, "Delete", -1)->invoke = ButtonDeleteSequence;
					UISpacerCreate(0, 0, 10, 0);
					UIButtonCreate(0, UI_BUTTON_SMALL, "Move up", -1)->invoke = ButtonMoveSequenceUp;
					UIButtonCreate(0, UI_BUTTON_SMALL, "Move down", -1)->invoke = ButtonMoveSequenceDown;
				UIParentPop();
			UIParentPop();

			UIPanelCreate(0, UI_PANEL_GRAY | UI_PANEL_EXPAND | UI_ELEMENT_PARENT_PUSH);
				tableKeyframes = UITableCreate(0, UI_ELEMENT_V_FILL, "Keyframes");
				tableKeyframes->e.messageUser = TableKeyframesMessage;
				UITableResizeColumns(tableKeyframes);
				arrput(inspectorSubscriptions, &tableKeyframes->e);

				UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_PANEL_SMALL_SPACING | UI_ELEMENT_PARENT_PUSH);
					UIButtonCreate(0, UI_BUTTON_SMALL, "Add", -1)->invoke = ButtonAddKeyframe;
					UIButtonCreate(0, UI_BUTTON_SMALL, "Delete", -1)->invoke = ButtonDeleteKeyframe;
				UIParentPop();
			UIParentPop();
		UIParentPop();

		UIPanelCreate(0, UI_PANEL_GRAY | UI_PANEL_MEDIUM_SPACING | UI_ELEMENT_PARENT_PUSH | UI_PANEL_EXPAND);
			UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
				constantsTextbox = UITextboxCreate(0, UI_ELEMENT_H_FILL);
				UIButtonCreate(0, 0, "Add", -1)->invoke = ButtonAddConstant;
				UIButtonCreate(0, 0, "Delete", -1)->invoke = ButtonDeleteConstant;
			UIParentPop();

			constantsTable = UITableCreate(0, UI_ELEMENT_V_FILL, "Name");
			constantsTable->e.messageUser = ConstantsTableMessage;
			constantsTable->itemCount = arrlenu(styleSet.constants);
			UITableResizeColumns(constantsTable);

			UIPanelCreate(0, UI_PANEL_WHITE | UI_PANEL_EXPAND | UI_PANEL_MEDIUM_SPACING | UI_ELEMENT_PARENT_PUSH);
				UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
					UILabelCreate(0, UI_ELEMENT_H_FILL, "Value", -1);
					constantsScale = UIButtonCreate(0, 0, "Scale", -1);
					constantsScale->e.messageUser = ConstantsScaleMessage;
				UIParentPop();

				constantsValue = UITextboxCreate(0, UI_ELEMENT_DISABLED);
				constantsValue->e.messageUser = ConstantsValueMessage;
			UIParentPop();
		UIParentPop();

		UIPanelCreate(0, UI_PANEL_GRAY | UI_PANEL_MEDIUM_SPACING | UI_ELEMENT_PARENT_PUSH | UI_PANEL_EXPAND);
			UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
				colorsTextbox = UITextboxCreate(0, UI_ELEMENT_H_FILL);
				UIButtonCreate(0, 0, "Add", -1)->invoke = ButtonAddColor;
				UIButtonCreate(0, 0, "Delete", -1)->invoke = ButtonDeleteColor;
			UIParentPop();

			colorsTable = UITableCreate(0, UI_ELEMENT_V_FILL, "Name\tValue");
			colorsTable->e.messageUser = ColorsTableMessage;
			colorsTable->itemCount = arrlenu(styleSet.colors);
			UITableResizeColumns(colorsTable);

			UIPanelCreate(0, UI_PANEL_WHITE | UI_PANEL_EXPAND | UI_PANEL_MEDIUM_SPACING | UI_ELEMENT_PARENT_PUSH);
				colorsValue2 = UIColorPickerCreate(&UIPanelCreate(0, 0)->e, UI_COLOR_PICKER_HAS_OPACITY);
				colorsValue2->e.messageUser = ColorsValue2Message;
				colorsValue = UITextboxCreate(0, UI_ELEMENT_DISABLED);
				colorsValue->e.messageUser = ColorsValueMessage;
				colorsPreview = UIElementCreate(sizeof(UIElement), 0, 0, ColorsPreviewMessage, "color preview");
			UIParentPop();
		UIParentPop();
	UIParentPop();

	UISplitPane *splitPane2 = UISplitPaneCreate(&splitPane1->e, 0, 0.7f);
	UIPanel *panel6 = UIPanelCreate(&splitPane2->e, UI_PANEL_EXPAND);
	elementCanvas = UIElementCreate(sizeof(UIElement), &panel6->e, UI_ELEMENT_V_FILL, CanvasMessage, "Canvas");

	UIPanelCreate(&panel6->e, UI_PANEL_GRAY | UI_PANEL_EXPAND | UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL | UI_ELEMENT_H_FILL);
		UIPanelCreate(0, UI_PANEL_EXPAND | UI_PANEL_MEDIUM_SPACING | UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_H_FILL);
			UILabelCreate(0, 0, "Preview options", -1);

			UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
				UISpacerCreate(0, 0, 20, 0);
				UILabelCreate(0, 0, "Width: ", -1);
				UISpacerCreate(0, 0, 5, 0);
				previewWidth = UISliderCreate(0, 0);
				previewWidth->position = 0.1f;
				previewWidth->e.messageUser = PreviewSliderMessage;
				UISpacerCreate(0, 0, 20, 0);
				previewFixAspectRatio = UIButtonCreate(0, UI_BUTTON_SMALL, "=", -1);
				previewFixAspectRatio->invoke = PreviewFixAspectRatioInvoke;
				UISpacerCreate(0, 0, 20, 0);
				UILabelCreate(0, 0, "Height:", -1);
				UISpacerCreate(0, 0, 5, 0);
				previewHeight = UISliderCreate(0, 0);
				previewHeight->position = 0.1f;
				previewHeight->e.messageUser = PreviewSliderMessage;
			UIParentPop();

			UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
				UISpacerCreate(0, 0, 20, 0);
				UILabelCreate(0, 0, "Scale: ", -1);
				UISpacerCreate(0, 0, 5, 0);
				previewScale = UISliderCreate(0, 0);
				previewScale->steps = 17;
				previewScale->e.messageUser = PreviewSliderMessage;
			UIParentPop();

			UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
				previewTransition = UIButtonCreate(0, 0, "Preview transition", -1);
				previewTransition->invoke = PreviewTransitionInvoke;
				UIButtonCreate(0, 0, "Preferred size", -1)->invoke = PreviewPreferredSizeInvoke;
				previewShowGuides = UIButtonCreate(0, 0, "Show guides", -1);
				previewShowGuides->invoke = PreviewShowGuidesInvoke;
				previewShowComputed = UIButtonCreate(0, 0, "Show computed rectangles", -1);
				previewShowComputed->invoke = PreviewShowComputedInvoke;
				editPoints = UIButtonCreate(0, 0, "View points", -1);
				editPoints->invoke = EditPointsInvoke;
			UIParentPop();

			previewPrimaryStatePanel = UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
				previewPrimaryStateIdle = UIButtonCreate(0, UI_BUTTON_SMALL | UI_BUTTON_CHECKED, "Idle", -1);
				previewPrimaryStateIdle->invoke = PreviewSetPrimaryState;
				previewPrimaryStateIdle->e.cp = (void *) PRIMARY_STATE_IDLE;
				previewPrimaryStateHovered = UIButtonCreate(0, UI_BUTTON_SMALL, "Hovered", -1);
				previewPrimaryStateHovered->invoke = PreviewSetPrimaryState;
				previewPrimaryStateHovered->e.cp = (void *) PRIMARY_STATE_HOVERED;
				previewPrimaryStatePressed = UIButtonCreate(0, UI_BUTTON_SMALL, "Pressed", -1);
				previewPrimaryStatePressed->invoke = PreviewSetPrimaryState;
				previewPrimaryStatePressed->e.cp = (void *) PRIMARY_STATE_PRESSED;
				previewPrimaryStateDisabled = UIButtonCreate(0, UI_BUTTON_SMALL, "Disabled", -1);
				previewPrimaryStateDisabled->invoke = PreviewSetPrimaryState;
				previewPrimaryStateDisabled->e.cp = (void *) PRIMARY_STATE_DISABLED;
				previewPrimaryStateInactive = UIButtonCreate(0, UI_BUTTON_SMALL, "Inactive", -1);
				previewPrimaryStateInactive->invoke = PreviewSetPrimaryState;
				previewPrimaryStateInactive->e.cp = (void *) PRIMARY_STATE_INACTIVE;
			UIParentPop();

			UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
				previewStateFocused = UIButtonCreate(0, UI_BUTTON_SMALL, "Focused", -1);
				previewStateFocused->e.messageUser = PreviewToggleState;
				previewStateChecked = UIButtonCreate(0, UI_BUTTON_SMALL, "Checked", -1);
				previewStateChecked->e.messageUser = PreviewToggleState;
				previewStateIndeterminate = UIButtonCreate(0, UI_BUTTON_SMALL, "Indeterminate", -1);
				previewStateIndeterminate->e.messageUser = PreviewToggleState;
				previewStateDefault = UIButtonCreate(0, UI_BUTTON_SMALL, "Default", -1);
				previewStateDefault->e.messageUser = PreviewToggleState;
				previewStateItemFocus = UIButtonCreate(0, UI_BUTTON_SMALL, "Item focus", -1);
				previewStateItemFocus->e.messageUser = PreviewToggleState;
				previewStateListFocus = UIButtonCreate(0, UI_BUTTON_SMALL, "List focus", -1);
				previewStateListFocus->e.messageUser = PreviewToggleState;
				previewStateSelected = UIButtonCreate(0, UI_BUTTON_SMALL, "Selected", -1);
				previewStateSelected->e.messageUser = PreviewToggleState;
				previewStateBeforeEnter = UIButtonCreate(0, UI_BUTTON_SMALL, "Before enter", -1);
				previewStateBeforeEnter->e.messageUser = PreviewToggleState;
				previewStateAfterExit = UIButtonCreate(0, UI_BUTTON_SMALL, "After exit", -1);
				previewStateAfterExit->e.messageUser = PreviewToggleState;
			UIParentPop();
		UIParentPop();

		UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
			previewBackgroundColor = UIColorPickerCreate(0, 0);
			previewBackgroundColor->e.messageUser = PreviewChangeBackgroundColor;
			UIColorToHSV(0xC0C0C0, &previewBackgroundColor->hue, &previewBackgroundColor->saturation, &previewBackgroundColor->value);
		UIParentPop();
	UIParentPop();

	panelInspector = UIPanelCreate(&splitPane2->e, UI_PANEL_GRAY | UI_PANEL_EXPAND | UI_PANEL_SCROLL);
	panelInspector->border = UI_RECT_1(10);
	panelInspector->gap = 5;

	ActionLoad(NULL);

	return UIMessageLoop();
}
