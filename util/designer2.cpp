#define UI_IMPLEMENTATION
#define ES_CRT_WITHOUT_PREFIX
#define EsPainter _EsPainter
#define EsPaintTarget _EsPaintTarget
#include "luigi.h"
#undef EsPaintTarget
#undef EsPainter
#ifndef OS_ESSENCE
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <assert.h>
#endif
#include "hsluv.h"

// x86_64-w64-mingw32-gcc -O3 -o bin/designer2.exe -D UI_WINDOWS util/designer2.cpp -DUNICODE -lgdi32 -luser32 -lkernel32 -Wl,--subsystem,windows -fno-exceptions -fno-rtti

// TODO Needed to replace the old designer:
// 	Calculating opaqueInsets.
// 	Prototyping display: previewing state transitions.

// TODO Additional features:
// 	Undoing a delete does not preserve an instance's layer.
//	Having to link to the end of a conditional object chain is a bit strange.
// 	Automatically cleaning up unused objects.
// 	Show error if property linked to object of incorrect type.
// 	Icons for different object types (especially color overwrite objects).
// 	Path layers: dashed contours.
// 	Picking objects: only highlight objects with an applicable type.
// 	Displaying radial gradients.
// 	Find object in graph by name.
// 	Auto-layout in the prototype display.
// 	Importing SVGs and TTFs.
// 	Schema-based verification of the document.
// 	Get text rendering on non-Essence platforms.
// 	Proper bezier path editor.
// 	Path boolean operations.
// 	Timeline editor for applying a given state change, with rows for possibly many different layers.
// 	Metrics: layoutVertical.

// TODO Reorganize old theming data!
		
//////////////////////////////////////////////////////////////

#ifndef OS_ESSENCE

#define ES_TEXT_H_LEFT 	 (1 << 0)
#define ES_TEXT_H_CENTER (1 << 1)
#define ES_TEXT_H_RIGHT  (1 << 2)
#define ES_TEXT_V_TOP 	 (1 << 3)
#define ES_TEXT_V_CENTER (1 << 4)
#define ES_TEXT_V_BOTTOM (1 << 5)
#define ES_TEXT_ELLIPSIS (1 << 6)
#define ES_TEXT_WRAP 	 (1 << 7)

#define ES_FONT_SANS 	   (0xFFFF)
#define ES_FONT_SERIF 	   (0xFFFE)
#define ES_FONT_MONOSPACED (0xFFFD)

#define ES_RECT_4(a, b, c, d) UI_RECT_4((int32_t) (a), (int32_t) (b), (int32_t) (c), (int32_t) (d))

#define ES_FUNCTION_OPTIMISE_O2 __attribute__((optimize("-O2")))
#define ES_FUNCTION_OPTIMISE_O3 __attribute__((optimize("-O3")))

#define ES_INFINITY INFINITY

#define EsAssert assert
#define EsCRTacosf acosf
#define EsCRTatan2f atan2f
#define EsCRTceilf ceilf
#define EsCRTcosf cosf
#define EsCRTfabsf fabsf
#define EsCRTfloorf floorf
#define EsCRTfmodf fmodf
#define EsCRTisnanf isnan
#define EsCRTlog2f log2f
#define EsCRTsinf sinf
#define EsCRTsqrtf sqrtf
#define EsHeap void
#define EsMemoryCopy memcpy
#define EsMemoryCopyReverse memmove
#define EsMemoryCompare memcmp
#define EsMemoryZero(a, b) memset((a), 0, (b))
#define EsPanic(...) UI_ASSERT(false)
#define EsRectangle UIRectangle

#define ES_MEMORY_MOVE_BACKWARDS -

#define SHARED_COMMON_WANT_BUFFERS

struct EsBuffer {
	union { const uint8_t *in; uint8_t *out; };
	size_t position, bytes;
	bool error;
	void *context;
};

void *EsHeapAllocate(size_t bytes, bool zero, EsHeap *heap = nullptr) {
	(void) heap;
	return zero ? calloc(1, bytes) : malloc(bytes);
}

void EsHeapFree(void *pointer, size_t bytes = 0, EsHeap *heap = nullptr) {
	(void) heap;
	(void) bytes;
	free(pointer);
}

void *EsHeapReallocate(void *oldAddress, size_t newAllocationSize, bool zeroNewSpace, EsHeap *) {
	UI_ASSERT(!zeroNewSpace);
	return realloc(oldAddress, newAllocationSize);
}

void EsMemoryMove(void *_start, void *_end, intptr_t amount, bool zeroEmptySpace) {
	uint8_t *start = (uint8_t *) _start;
	uint8_t *end = (uint8_t *) _end;
	if (end < start) return;

	if (amount > 0) {
		EsMemoryCopyReverse(start + amount, start, end - start);
		if (zeroEmptySpace) EsMemoryZero(start, amount);
	} else if (amount < 0) {
		EsMemoryCopy(start + amount, start, end - start);
		if (zeroEmptySpace) EsMemoryZero(end + amount, -amount);
	}
}

bool EsColorIsLight(uint32_t color) {
	float r = (color & 0xFF0000) >> 16;
	float g = (color & 0x00FF00) >>  8;
	float b = (color & 0x0000FF) >>  0;
	float brightness = r * r * 0.241f + g * g * 0.691f + b * b * 0.068f;
	return brightness >= 180.0f * 180.0f;
}

#endif

struct EsPaintTarget {
	void *bits;
	uint32_t width, height, stride;
	bool fullAlpha, readOnly, fromBitmap, forWindowManager;
};

struct EsPainter {
	EsRectangle clip;
	EsPaintTarget *target;
};

void SetBit(uint32_t *value, uint32_t bit, bool on) {
	if (on) *value = *value | bit;
	else *value = *value & ~bit;
}

UIRectangle ScaleRectangle(UIRectangle r, float by) {
	return UI_RECT_4((int32_t) (r.l * by), (int32_t) (r.r * by), (int32_t) (r.t * by), (int32_t) (r.b * by));
}

#define IN_DESIGNER
#define DESIGNER2

#define SHARED_COMMON_WANT_RECTANGLES
#define SHARED_COMMON_WANT_RENDERING
#include "../shared/common.cpp"
#define SHARED_MATH_WANT_BASIC_UTILITIES
#define SHARED_MATH_WANT_INTERPOLATION
#include "../shared/math.cpp"

#include "../shared/array.cpp"
#include "../shared/hash.cpp"
#include "../shared/hash_table.cpp"
#include "../desktop/renderer.cpp"
#include "../desktop/theme.cpp"

//////////////////////////////////////////////////////////////

const char *cPrimaryStateStrings[] = {
	"Any", "Idle", "Hovered", "Pressed", "Disabled", "Inactive",
};

const char *cStateBitStrings[] = {
	"Focus", "Check", "Indtm", "DefBtn", "Sel", "FcItem", "ListFc", "BfEnt", "AfExt",
};

const char *cCursorStrings[] = {
	"Normal", "Text",
	"Resize vertical", "Resize horizontal",
	"Diagonal 1", "Diagonal 2",
	"Split vertical", "Split horizontal",
	"Hand hover", "Hand drag", "Hand point",
	"Scroll up-left", "Scroll up", "Scroll up-right", "Scroll left",
	"Scroll center", "Scroll right", "Scroll down-left", "Scroll down", "Scroll down-right",
	"Select lines", "Drop text",
	"Cross hair pick", "Cross hair resize",
	"Move hover", "Move drag",
	"Rotate hover", "Rotate drag",
	"Blank",
};

//////////////////////////////////////////////////////////////

#ifdef OS_ESSENCE
EsFileStore *fileStore;

const EsInstanceClassEditorSettings instanceClassEditorSettings = {
	"untitled.designer", -1,
	"New design", -1,
	ES_ICON_TEXT_CSS,
};
#endif

struct Canvas : UIElement {
#define ARROW_MODE_NONE        (0)
#define ARROW_MODE_ALL         (1)
#define ARROW_MODE_FROM        (2)
#define ARROW_MODE_TO          (3)
#define ARROW_MODE_FROM_OR_TO  (4)
#define ARROW_MODE_FROM_AND_TO (5)
	uint8_t arrowMode;

	bool showPrototype;

	float zoom;
	float panX, panY;

	float swapZoom;
	float swapPanX, swapPanY;

	float lastPanPointX, lastPanPointY;
	bool dragging, canDrag, selecting;
	int32_t dragDeltaX, dragDeltaY;
	int32_t selectX, selectY;
	int32_t dragOffsetX, dragOffsetY;
	int32_t leftDownX, leftDownY;

	UIRectangle originalBounds;
	UIRectangle resizeOffsets;
	bool resizing;
	UIElement *resizeHandles[4];

	int32_t previewPrimaryState;
	int32_t previewStateBits;
	bool previewStateActive;
};

struct Prototype : UIElement {
};

UIWindow *window;
UIElement *inspector;
UIPanel *graphControls;
UIPanel *prototypeControls;
Canvas *canvas;

void InspectorAnnouncePropertyChanged(uint64_t objectID, const char *cPropertyName);
void InspectorPopulate();
void InspectorPickTargetEnd();
void CanvasSelectObject(struct Object *object);
void CanvasSwitchView(void *cp);
Rectangle8 ExportCalculatePaintOutsets(Object *object);
void ObjectAddCommand(void *cp);
void ObjectChangeTypeInternal(void *cp);

//////////////////////////////////////////////////////////////

enum PropertyType : uint8_t {
	PROP_NONE,
	PROP_COLOR,
	PROP_INT,
	PROP_OBJECT,
	PROP_FLOAT,
};

struct Property {
	PropertyType type;
#define PROPERTY_NAME_SIZE (31)
	char cName[PROPERTY_NAME_SIZE];

	union {
		int32_t integer;
		uint64_t object;
		float floating;
	};
};

enum ObjectType : uint8_t {
	OBJ_NONE,

	OBJ_STYLE,
	OBJ_COMMENT,
	OBJ_INSTANCE,
	OBJ_SELECTOR,

	OBJ_VAR_COLOR = 0x40,
	OBJ_VAR_INT,
	OBJ_VAR_TEXT_STYLE,
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

struct Object {
	ObjectType type;
#define OBJECT_NAME_SIZE (46)
	char cName[OBJECT_NAME_SIZE];
#define OBJECT_IS_SELECTED (1 << 0)
#define OBJECT_IN_PROTOTYPE (1 << 1)
	uint8_t flags;
	uint64_t id;
	Array<Property> properties;
};

enum StepType : uint8_t {
	STEP_GROUP_MARKER,
	STEP_MODIFY_PROPERTY,
	STEP_RENAME_OBJECT,
	STEP_ADD_OBJECT,
	STEP_DELETE_OBJECT,
	STEP_CHANGE_OBJECT_TYPE,
	STEP_SET_OBJECT_DEPTH,
};

enum StepApplyMode {
	STEP_APPLY_NORMAL,
	STEP_APPLY_GROUPED,
	STEP_APPLY_UNDO,
	STEP_APPLY_REDO,
};

struct Step {
	StepType type;
#define STEP_UPDATE_INSPECTOR (1 << 0)
	uint32_t flags;
	uint64_t objectID;

	union {
		Property property;
		char cName[OBJECT_NAME_SIZE];
		Object object;
		uintptr_t depth;
	};
};

struct ExportOffset {
	uint64_t objectID;
	uint64_t styleObjectID;
	uintptr_t offset;
	char cPropertyName[PROPERTY_NAME_SIZE];
	uint8_t overrideType;
};

struct ObjectTypeString {
	ObjectType type;
	const char *string;
};

ObjectTypeString cObjectTypeStrings[] = {
#define ADD_STRING(x) { x, #x }
	ADD_STRING(OBJ_NONE),
	ADD_STRING(OBJ_STYLE),
	ADD_STRING(OBJ_COMMENT),
	ADD_STRING(OBJ_INSTANCE),
	ADD_STRING(OBJ_SELECTOR),
	ADD_STRING(OBJ_VAR_COLOR),
	ADD_STRING(OBJ_VAR_INT),
	ADD_STRING(OBJ_VAR_TEXT_STYLE),
	ADD_STRING(OBJ_VAR_CONTOUR_STYLE),
	ADD_STRING(OBJ_PAINT_OVERWRITE),
	ADD_STRING(OBJ_PAINT_LINEAR_GRADIENT),
	ADD_STRING(OBJ_PAINT_RADIAL_GRADIENT),
	ADD_STRING(OBJ_LAYER_BOX),
	ADD_STRING(OBJ_LAYER_METRICS),
	ADD_STRING(OBJ_LAYER_TEXT),
	ADD_STRING(OBJ_LAYER_GROUP),
	ADD_STRING(OBJ_LAYER_PATH),
	ADD_STRING(OBJ_MOD_COLOR),
	ADD_STRING(OBJ_MOD_MULTIPLY),
#undef ADD_STRING
};

Array<Step> undoStack;
Array<Step> redoStack;
bool documentModified;
uint64_t selectedObjectID;
HashStore<uint64_t, uintptr_t> objectLookup;
Array<ExportOffset> exportOffsets;
uint8_t activeSelectorIndex;

// Document state:
Array<Object> objects;
uint64_t objectIDAllocator;

Property *PropertyFind(Object *object, const char *cName, uint8_t type = 0) {
	if (object) {
		for (uintptr_t i = 0; i < object->properties.Length(); i++) {
			if (0 == strcmp(object->properties[i].cName, cName)) {
				if (type && object->properties[i].type != type) {
					return nullptr;
				} else {
					return &object->properties[i];
				}
			}
		}
	}

	return nullptr;
}

Object *ObjectFind(uint64_t id, bool resolveSelectors, int depth = 0) {
#if 0
	for (uintptr_t i = 0; i < objects.Length(); i++) {
		if (objects[i].id == id) {
			return &objects[i];
		}
	}

	return nullptr;
#else
	if (!id) return nullptr;
	uint64_t *index = objectLookup.Get(&id);
	Object *object = index ? &objects[*index] : nullptr;
	if (object) assert(object->id == id);

	if (resolveSelectors && depth < 10 && object && object->type == OBJ_SELECTOR) {
		char cPropertyName[PROPERTY_NAME_SIZE];
		snprintf(cPropertyName, sizeof(cPropertyName), "options_%d_link", activeSelectorIndex);
		Property *property = PropertyFind(object, cPropertyName);
		return property ? ObjectFind(property->object, true, depth + 1) : nullptr;
	}

	return object;
#endif
}

ExportOffset *ExportOffsetFindObject(uint64_t id) {
	for (uintptr_t i = 0; i < exportOffsets.Length(); i++) {
		if (exportOffsets[i].objectID == id) {
			return &exportOffsets[i];
		}
	}

	return nullptr;
}

ExportOffset *ExportOffsetFindObjectForStyle(uint64_t id, uint64_t style) {
	for (uintptr_t i = 0; i < exportOffsets.Length(); i++) {
		if (exportOffsets[i].objectID == id && exportOffsets[i].styleObjectID == style) {
			return &exportOffsets[i];
		}
	}

	return nullptr;
}

ExportOffset *ExportOffsetFindProperty(uint64_t objectID, const char *cPropertyName) {
	for (uintptr_t i = 0; i < exportOffsets.Length(); i++) {
		if (exportOffsets[i].objectID == objectID
				&& 0 == strcmp(exportOffsets[i].cPropertyName, cPropertyName)) {
			return &exportOffsets[i];
		}
	}

	return nullptr;
}

void ObjectSetSelected(uint64_t id, bool removeSelectedFlagFromPreviousSelection = true) {
	if (selectedObjectID && removeSelectedFlagFromPreviousSelection) {
		Object *object = ObjectFind(selectedObjectID, false);
		if (object) object->flags &= ~OBJECT_IS_SELECTED;
	}

	selectedObjectID = id;

	if (selectedObjectID) {
		Object *object = ObjectFind(selectedObjectID, false);
		if (object) object->flags |= OBJECT_IS_SELECTED;
	}

	if (!selectedObjectID) {
		canvas->previewPrimaryState = THEME_PRIMARY_STATE_IDLE;
		canvas->previewStateBits = 0;
	}
}

int32_t PropertyReadInt32(Object *object, const char *cName, int32_t defaultValue = 0) {
	Property *property = PropertyFind(object, cName);
	return !property || property->type == PROP_OBJECT ? defaultValue : property->integer;
}

bool ObjectIsConditional(Object *object) {
	return PropertyReadInt32(object, "_primaryState") || PropertyReadInt32(object, "_stateBits");
}

bool ObjectMatchesPreviewState(Object *object) {
	int32_t primaryState = PropertyReadInt32(object, "_primaryState");
	int32_t stateBits = PropertyReadInt32(object, "_stateBits");

	return (primaryState == canvas->previewPrimaryState || !primaryState)
		&& ((stateBits & canvas->previewStateBits) == stateBits);
}

Property *PropertyFindOrInherit(Object *object, const char *cName, uint8_t type = 0) {
	uintptr_t depth = 0;

	while (object && (depth++ < 100)) {
		if (!ObjectIsConditional(object) || (canvas && canvas->previewStateActive && ObjectMatchesPreviewState(object))) {
			// Return the value if the object has this property.
			Property *property = PropertyFind(object, cName);
			if (property) return type && property->type != type ? nullptr : property;
		} else {
			// Skip the object if it has a condition and it's not the original object.
		}

		// Go to the inheritance parent object.
		Property *property = PropertyFind(object, "_parent", PROP_OBJECT);
		object = ObjectFind(property ? property->object : 0, true);
	}

	return nullptr;
}

int32_t PropertyFindOrInheritReadInt32(Object *object, const char *cName, int32_t defaultValue = 0) {
	Property *property = PropertyFindOrInherit(object, cName, PROP_INT);
	return property ? property->integer : defaultValue;
}

float PropertyFindOrInheritReadFloat(Object *object, const char *cName, float defaultValue = 0) {
	Property *property = PropertyFindOrInherit(object, cName, PROP_FLOAT);
	return property ? property->floating : defaultValue;
}

Object *PropertyFindOrInheritReadObject(Object *object, const char *cName) {
	Property *property = PropertyFindOrInherit(object, cName, PROP_OBJECT);
	return property ? ObjectFind(property->object, true) : nullptr;
}

void ObjectLookupRebuild() {
	objectLookup.Free();

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		*objectLookup.Put(&objects[i].id) = i;
	}
}

void DocumentSave(void *) {
#ifdef OS_ESSENCE
	EsBuffer buffer = { .canGrow = 1 };
#define fwrite(a, b, c, d) EsBufferWrite(&buffer, (a), (b) * (c))
#else
	FILE *f = fopen("res/Theme Source.dat", "wb");
#endif

	uint32_t version = 1;
	fwrite(&version, 1, sizeof(uint32_t), f);
	uint32_t objectCount = objects.Length();
	fwrite(&objectCount, 1, sizeof(uint32_t), f);
	fwrite(&objectIDAllocator, 1, sizeof(uint64_t), f);

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		Object copy = objects[i];
		uint32_t propertyCount = copy.properties.Length();
		copy.properties.array = nullptr;
		fwrite(&copy, 1, sizeof(Object), f);
		fwrite(&propertyCount, 1, sizeof(uint32_t), f);
		fwrite(objects[i].properties.array, 1, sizeof(Property) * propertyCount, f);
	}

#ifdef OS_ESSENCE
	EsFileStoreWriteAll(fileStore, buffer.out, buffer.position); 
	EsHeapFree(buffer.out);
#else
	fclose(f);
#endif
	documentModified = false;
}

void DocumentLoad() {
#ifdef OS_ESSENCE
	EsBuffer buffer = {};
	buffer.out = (uint8_t *) EsFileStoreReadAll(fileStore, &buffer.bytes);
#define fread(a, b, c, d) EsBufferReadInto(&buffer, (a), (b) * (c))
#else
	FILE *f = fopen("res/Theme Source.dat", "rb");
	if (!f) return;
#endif

	uint32_t version = 1;
	fread(&version, 1, sizeof(uint32_t), f);
	uint32_t objectCount = 0;
	fread(&objectCount, 1, sizeof(uint32_t), f);
	fread(&objectIDAllocator, 1, sizeof(uint64_t), f);

	for (uintptr_t i = 0; i < objectCount; i++) {
		Object object = {};
		fread(&object, 1, sizeof(Object), f);
		object.cName[OBJECT_NAME_SIZE - 1] = 0;
		uint32_t propertyCount = 0;
		fread(&propertyCount, 1, sizeof(uint32_t), f);
		object.properties.InsertMany(0, propertyCount);
		fread(object.properties.array, 1, sizeof(Property) * propertyCount, f);
		object.flags &= ~OBJECT_IS_SELECTED;

		for (uintptr_t i = 0; i < propertyCount; i++) {
			object.properties[i].cName[PROPERTY_NAME_SIZE - 1] = 0;
		}

		objects.Add(object);
	}

	ObjectLookupRebuild();

#ifdef OS_ESSENCE
	EsHeapFree(buffer.out);
#else
	fclose(f);
#endif
}

void DocumentFree() {
	for (uintptr_t i = 0; i < objects.Length(); i++) {
		objects[i].properties.Free();
	}

	objects.Free();
	undoStack.Free();
	redoStack.Free();
	ObjectLookupRebuild();
}

void DocumentApplyStep(Step step, StepApplyMode mode = STEP_APPLY_NORMAL) {
	bool allowMerge = false;

	if (step.type == STEP_GROUP_MARKER) {
	} else if (step.type == STEP_MODIFY_PROPERTY) {
		Object *object = ObjectFind(step.objectID, false);
		UI_ASSERT(object);

		Property *property = PropertyFind(object, step.property.cName);

		if (property) {
			Property oldValue = *property;

			if (step.property.type != PROP_NONE) {
				// Update the property.
				*property = step.property;
				allowMerge = true;
			} else {
				// Remove the property.
				for (uintptr_t i = 0; i < object->properties.Length(); i++) {
					if (0 == strcmp(object->properties[i].cName, step.property.cName)) {
						object->properties.DeleteSwap(i);
						break;
					}
				}
			}

			step.property = oldValue;
		} else {
			if (step.property.type != PROP_NONE) {
				// Add the property.
				object->properties.Add(step.property);
				step.property.type = PROP_NONE;
			} else {
				// Asking to remove a property that does not exist.
				// Probably from a remove broadcast.
			}
		}

		UIElementRefresh(canvas);

		if (step.flags & STEP_UPDATE_INSPECTOR) {
			InspectorPopulate();
		} else {
			InspectorAnnouncePropertyChanged(step.objectID, step.property.cName);
		}
	} else if (step.type == STEP_RENAME_OBJECT) {
		Object *object = ObjectFind(step.objectID, false);
		UI_ASSERT(object);

		char oldName[OBJECT_NAME_SIZE];
		strcpy(oldName, object->cName);
		strcpy(object->cName, step.cName);
		strcpy(step.cName, oldName);

		UIElementRepaint(canvas, nullptr);
		InspectorPopulate();
	} else if (step.type == STEP_ADD_OBJECT) {
		objects.Add(step.object);
		ObjectLookupRebuild();
		UIElementRepaint(canvas, nullptr);
		step.objectID = step.object.id;
		step.type = STEP_DELETE_OBJECT;
	} else if (step.type == STEP_CHANGE_OBJECT_TYPE) {
		Object *object = ObjectFind(step.object.id, false);
		ObjectType oldType = object->type;
		object->type = step.object.type;
		step.object.type = oldType;
		InspectorPopulate();
		UIElementRefresh(canvas);
	} else if (step.type == STEP_DELETE_OBJECT) {
		if (selectedObjectID == step.objectID) {
			ObjectSetSelected(0);
		}

		step.type = STEP_ADD_OBJECT;

		for (uintptr_t i = 0; i < objects.Length(); i++) {
			if (objects[i].id == step.objectID) {
				step.object = objects[i];
				objects.Delete(i);
				break;
			}
		}

		ObjectLookupRebuild();
		step.object.flags = 0;
		UIElementRefresh(canvas);
		InspectorPopulate();
	} else if (step.type == STEP_SET_OBJECT_DEPTH) {
		uintptr_t newDepth = step.depth;
		bool found = false;

		for (uintptr_t i = 0; i < objects.Length(); i++) {
			if (objects[i].id == step.objectID) {
				step.depth = i;
				Object object = objects[i];
				objects.Delete(i);
				objects.Insert(object, newDepth);
				found = true;
				break;
			}
		}

		UI_ASSERT(found);
		ObjectLookupRebuild();
		UIElementRefresh(canvas);
	} else {
		UI_ASSERT(false);
	}

	if (mode == STEP_APPLY_NORMAL || mode == STEP_APPLY_GROUPED) {
		bool merge = false;

		if (allowMerge && undoStack.Length() > 2 && !redoStack.Length() && (~step.flags & STEP_UPDATE_INSPECTOR)) {
			Step last = undoStack[undoStack.Length() - (undoStack.Last().type == STEP_GROUP_MARKER ? 2 : 1)];

			if (step.type == STEP_MODIFY_PROPERTY && last.type == STEP_MODIFY_PROPERTY 
					&& last.objectID == step.objectID && 0 == strcmp(last.property.cName, step.property.cName)) {
				merge = true;
			}
		}

		if (!merge) {
			undoStack.Add(step);
			
			for (uintptr_t i = 0; i < redoStack.Length(); i++) {
				if (redoStack[i].type == STEP_ADD_OBJECT) {
					redoStack[i].object.properties.Free();
				}
			}

			redoStack.Free();

			if (mode != STEP_APPLY_GROUPED) {
				Step step = {};
				step.type = STEP_GROUP_MARKER;
				undoStack.Add(step);
			}
		}
	} else if (mode == STEP_APPLY_UNDO) {
		redoStack.Add(step);
	} else if (mode == STEP_APPLY_REDO) {
		undoStack.Add(step);
	}

	documentModified = true;
	InspectorPickTargetEnd();

#ifdef UI_ESSENCE
	EsInstanceSetModified(ui.instance, true);
#endif
}

void DocumentUndoStep(void *) {
	if (!undoStack.Length()) return;

	Step marker = undoStack.Pop();
	UI_ASSERT(marker.type == STEP_GROUP_MARKER);

	do {
		DocumentApplyStep(undoStack.Pop(), STEP_APPLY_UNDO);
	} while (undoStack.Length() && undoStack.Last().type != STEP_GROUP_MARKER);

	redoStack.Add(marker);
}

void DocumentRedoStep(void *) {
	if (!redoStack.Length()) return;

	Step marker = redoStack.Pop();
	UI_ASSERT(marker.type == STEP_GROUP_MARKER);

	do {
		DocumentApplyStep(redoStack.Pop(), STEP_APPLY_REDO);
	} while (redoStack.Length() && redoStack.Last().type != STEP_GROUP_MARKER);

	undoStack.Add(marker);
}

void DocumentSwapPropertyPrefixes(Object *object, Step step, const char *cPrefix0, const char *cPrefix1, bool last, bool moveOnly) {
	char cNewName[PROPERTY_NAME_SIZE];
	Array<Step> steps = {};

	for (uintptr_t i = 0; i < object->properties.Length(); i++) {
		if (0 == memcmp(object->properties[i].cName, cPrefix0, strlen(cPrefix0))
				|| 0 == memcmp(object->properties[i].cName, cPrefix1, strlen(cPrefix1))) {
			strcpy(step.property.cName, object->properties[i].cName);
			step.property.type = PROP_NONE;
			steps.Add(step);
		}
	}

	for (uintptr_t i = 0; i < object->properties.Length(); i++) {
		if (!moveOnly && 0 == memcmp(object->properties[i].cName, cPrefix0, strlen(cPrefix0))) {
			strcpy(cNewName, cPrefix1);
			strcat(cNewName, object->properties[i].cName + strlen(cPrefix0));
			step.property = object->properties[i];
			strcpy(step.property.cName, cNewName);
			steps.Add(step);
		} else if (0 == memcmp(object->properties[i].cName, cPrefix1, strlen(cPrefix1))) {
			strcpy(cNewName, cPrefix0);
			strcat(cNewName, object->properties[i].cName + strlen(cPrefix1));
			step.property = object->properties[i];
			strcpy(step.property.cName, cNewName);
			steps.Add(step);
		}
	}

	for (uintptr_t i = 0; i < steps.Length(); i++) {
		DocumentApplyStep(steps[i], (i == steps.Length() - 1 && last) ? STEP_APPLY_NORMAL : STEP_APPLY_GROUPED);
	}

	steps.Free();
}

int PropertyCompareNames(const void *left, const void *right) {
	return strcmp(((*(Property *) left)).cName, ((*(Property *) right)).cName);
}

void TextStyleRemoveDuplicates(void *cp) {
	// Remove all duplicates of this text style,
	// and redirect objects that linked to them to this object.

	Object *object = ObjectFind((uintptr_t) cp, false);
	if (!object) return;

	// TODO Support undo.
	if (strcmp("OK", UIDialogShow(window, 0, "Warning: You cannot undo this action.\n%f%b%b", "OK", "Cancel"))) return;

	// Start by sorting the property list so we can detect duplicates easier.
	qsort(object->properties.array, object->properties.Length(), sizeof(Property), PropertyCompareNames);

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		Object *other = &objects[i];
		if (other->type != OBJ_VAR_TEXT_STYLE || other == object) continue;
		qsort(other->properties.array, other->properties.Length(), sizeof(Property), PropertyCompareNames);

		uintptr_t pi = 0, pj = 0;

		while (pi != object->properties.Length() && pj != other->properties.Length()) {
			// Ignore the graph position when checking for duplicates.
			if (0 == strcmp(object->properties[pi].cName, "_graphX")) { pi++; continue; }
			if (0 == strcmp(object->properties[pi].cName, "_graphY")) { pi++; continue; }
			if (0 == strcmp(object->properties[pi].cName, "_graphW")) { pi++; continue; }
			if (0 == strcmp(object->properties[pi].cName, "_graphH")) { pi++; continue; }
			if (0 == strcmp(other ->properties[pj].cName, "_graphX")) { pj++; continue; }
			if (0 == strcmp(other ->properties[pj].cName, "_graphY")) { pj++; continue; }
			if (0 == strcmp(other ->properties[pj].cName, "_graphW")) { pj++; continue; }
			if (0 == strcmp(other ->properties[pj].cName, "_graphH")) { pj++; continue; }

			// Check if the properties are identical.
			if (strcmp(object->properties[pi].cName, other->properties[pj].cName))                                               break;
			if (object->properties[pi].type != other->properties[pj].type)                                                       break;
			if (object->properties[pi].type == PROP_INT    && object->properties[pi].integer  != other->properties[pj].integer ) break;
			if (object->properties[pi].type == PROP_COLOR  && object->properties[pi].integer  != other->properties[pj].integer ) break;
			if (object->properties[pi].type == PROP_OBJECT && object->properties[pi].object   != other->properties[pj].object  ) break;
			if (object->properties[pi].type == PROP_FLOAT  && object->properties[pi].floating != other->properties[pj].floating) break;
			
			pi++, pj++;
		}

		if (pi != object->properties.Length() || pj != other->properties.Length()) {
			continue;
		}

		// Duplicate found. Delete and relink.
		uint64_t replaceID = other->id;
		objects.Delete(i);
		ObjectLookupRebuild();
		object = ObjectFind((uintptr_t) cp, false);
		assert(object);
		i--;

		for (uintptr_t j = 0; j < objects.Length(); j++) {
			Object *source = &objects[j];

			for (uintptr_t k = 0; k < source->properties.Length(); k++) {
				if (source->properties[k].type == PROP_OBJECT && source->properties[k].object == replaceID) {
					source->properties[k].object = object->id;
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////

enum InspectorElementType {
	INSPECTOR_INVALID_ELEMENT,
	INSPECTOR_REMOVE_BUTTON,
	INSPECTOR_REMOVE_BUTTON_BROADCAST,
	INSPECTOR_COLOR_PICKER,
	INSPECTOR_COLOR_TEXTBOX,
	INSPECTOR_INTEGER_TEXTBOX,
	INSPECTOR_INTEGER_TEXTBOX_BROADCAST,
	INSPECTOR_FLOAT_TEXTBOX,
	INSPECTOR_LINK,
	INSPECTOR_LINK_BROADCAST,
	INSPECTOR_BOOLEAN_TOGGLE,
	INSPECTOR_MASK_BIT_TOGGLE,
	INSPECTOR_RADIO_SWITCH,
	INSPECTOR_CURSOR_DROP_DOWN,
	INSPECTOR_ADD_ARRAY_ITEM,
	INSPECTOR_SWAP_ARRAY_ITEMS,
	INSPECTOR_DELETE_ARRAY_ITEM,
};

struct InspectorBindingData {
	UIElement *element; 
	uint64_t objectID; 
	char cPropertyName[PROPERTY_NAME_SIZE];
	const char *cEnablePropertyName;
	InspectorElementType elementType;
	int32_t choiceValue;
};

Array<UIElement *> inspectorBoundElements;
UIElement *inspectorActivePropertyStepElement;
InspectorBindingData *inspectorMenuData;
InspectorBindingData *inspectorPickData;

void InspectorPickTargetEnd() {
	if (inspectorPickData) {
		inspectorPickData = nullptr;
		UIElementRepaint(canvas, nullptr);
	}
}

void InspectorUpdateSingleElementEnable(InspectorBindingData *data) {
	UI_ASSERT(data->cEnablePropertyName);
	bool enabled = PropertyReadInt32(ObjectFind(data->objectID, false), data->cEnablePropertyName);
	SetBit(&data->element->flags, UI_ELEMENT_DISABLED, !enabled);
	UIElementRefresh(data->element);
}

void InspectorUpdateSingleElement(InspectorBindingData *data) {
	if (data->element->flags & UI_ELEMENT_DESTROY) {
		return;
	}

	if (data->elementType == INSPECTOR_REMOVE_BUTTON || data->elementType == INSPECTOR_REMOVE_BUTTON_BROADCAST) {
		UIButton *button = (UIButton *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID, false), data->cPropertyName);
		SetBit(&data->element->flags, UI_ELEMENT_DISABLED, !property);
		button->label[0] = property ? 'X' : '-';
		UIElementRefresh(&button->e);
	} else if (data->elementType == INSPECTOR_BOOLEAN_TOGGLE) {
		UICheckbox *box = (UICheckbox *) data->element;
		box->check = PropertyReadInt32(ObjectFind(data->objectID, false), data->cPropertyName, 2);
		if ((~box->e.flags & UI_CHECKBOX_ALLOW_INDETERMINATE)) box->check &= 1;
		UIElementRefresh(&box->e);
	} else if (data->elementType == INSPECTOR_MASK_BIT_TOGGLE) {
		UICheckbox *box = (UICheckbox *) data->element;
		box->check = (PropertyReadInt32(ObjectFind(data->objectID, false), data->cPropertyName) & data->choiceValue) ? UI_CHECK_CHECKED : UI_CHECK_UNCHECKED;
		UIElementRefresh(&box->e);
	} else if (data->elementType == INSPECTOR_RADIO_SWITCH) {
		UIButton *button = (UIButton *) data->element;
		int32_t value = PropertyReadInt32(ObjectFind(data->objectID, false), data->cPropertyName);
		SetBit(&button->e.flags, UI_BUTTON_CHECKED, value == data->choiceValue);
		UIElementRefresh(&button->e);
	} else if (data->elementType == INSPECTOR_CURSOR_DROP_DOWN) {
		UIButton *button = (UIButton *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID, false), data->cPropertyName, PROP_INT);
		UI_FREE(button->label);
		button->label = UIStringCopy(property ? cCursorStrings[property->integer] : "---", (button->labelBytes = -1));
		UIElementRefresh(&button->e);
	} else if (data->elementType == INSPECTOR_COLOR_PICKER) {
		UIColorPicker *colorPicker = (UIColorPicker *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID, false), data->cPropertyName, PROP_COLOR);
		uint32_t color = property ? property->integer : 0xFFFFFFFF;
		colorPicker->opacity = (color >> 24) / 255.0f;
		UIColorToHSV(color, &colorPicker->hue, &colorPicker->saturation, &colorPicker->value);
		UIElementRefresh(&colorPicker->e);
	} else if (data->elementType == INSPECTOR_COLOR_TEXTBOX) {
		UITextbox *textbox = (UITextbox *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID, false), data->cPropertyName, PROP_COLOR);
		char buffer[32] = "";
		if (property) snprintf(buffer, sizeof(buffer), "%.8X", (uint32_t) property->integer);
		UITextboxClear(textbox, false);
		UITextboxReplace(textbox, buffer, -1, false);
		UIElementRefresh(&textbox->e);
	} else if (data->elementType == INSPECTOR_INTEGER_TEXTBOX || data->elementType == INSPECTOR_INTEGER_TEXTBOX_BROADCAST) {
		UITextbox *textbox = (UITextbox *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID, false), data->cPropertyName, PROP_INT);
		char buffer[32] = "";
		if (property) snprintf(buffer, sizeof(buffer), "%d", property->integer);
		UITextboxClear(textbox, false);
		UITextboxReplace(textbox, buffer, -1, false);
		UIElementRefresh(&textbox->e);
	} else if (data->elementType == INSPECTOR_FLOAT_TEXTBOX) {
		UITextbox *textbox = (UITextbox *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID, false), data->cPropertyName, PROP_FLOAT);
		char buffer[32] = "";
		if (property) snprintf(buffer, sizeof(buffer), "%.2f", property->floating);
		UITextboxClear(textbox, false);
		UITextboxReplace(textbox, buffer, -1, false);
		UIElementRefresh(&textbox->e);
	} else if (data->elementType == INSPECTOR_LINK || data->elementType == INSPECTOR_LINK_BROADCAST) {
		UIButton *button = (UIButton *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID, false), data->cPropertyName, PROP_OBJECT);
		Object *target = ObjectFind(property ? property->object : 0, false);
		const char *string = target ? (target->cName[0] ? target->cName : "(untitled)") : "---";
		UI_FREE(button->label);
		button->label = UIStringCopy(string, (button->labelBytes = -1));
		UIElementRefresh(&button->e);
	} else if (data->elementType == INSPECTOR_ADD_ARRAY_ITEM || data->elementType == INSPECTOR_SWAP_ARRAY_ITEMS 
			|| data->elementType == INSPECTOR_DELETE_ARRAY_ITEM) {
	} else {
		UI_ASSERT(false);
	}
}

void InspectorAnnouncePropertyChanged(uint64_t objectID, const char *cPropertyName) {
	for (uintptr_t i = 0; i < inspectorBoundElements.Length(); i++) {
		InspectorBindingData *data = (InspectorBindingData *) inspectorBoundElements[i]->cp;
		if (data->element == inspectorActivePropertyStepElement) continue;
		if (data->objectID != objectID) continue; 

		if (data->cEnablePropertyName && 0 == strcmp(data->cEnablePropertyName, cPropertyName)) {
			InspectorUpdateSingleElementEnable(data);
		}

		if (0 == strcmp(data->cPropertyName, cPropertyName)) {
			InspectorUpdateSingleElement(data);
		}
	}
}

void InspectorBroadcastStep(Step step, InspectorBindingData *data) {
	if (data->elementType == INSPECTOR_INTEGER_TEXTBOX_BROADCAST || data->elementType == INSPECTOR_LINK_BROADCAST
			|| data->elementType == INSPECTOR_REMOVE_BUTTON_BROADCAST) {
		for (char i = '1'; i <= '3'; i++) {
			step.property.cName[strlen(step.property.cName) - 1] = i;
			DocumentApplyStep(step, STEP_APPLY_GROUPED);
		}

		strcpy(step.property.cName, data->cPropertyName);
	}
}

void InspectorCursorDropDownMenuInvoke(void *cp) {
	intptr_t index = (intptr_t) cp;
	Step step = {};
	step.type = STEP_MODIFY_PROPERTY;
	step.objectID = inspectorMenuData->objectID;
	strcpy(step.property.cName, inspectorMenuData->cPropertyName);
	step.property.type = index == -1 ? PROP_NONE : PROP_INT;
	step.property.integer = index;
	DocumentApplyStep(step);
	inspectorMenuData = nullptr;
}

int InspectorBoundMessage(UIElement *element, UIMessage message, int di, void *dp) {
	InspectorBindingData *data = (InspectorBindingData *) element->cp;

	if (message == UI_MSG_DESTROY) {
		inspectorBoundElements.FindAndDeleteSwap(element, true);
		free(data);
		data = nullptr;
	} else if (message == UI_MSG_VALUE_CHANGED) {
		Step step = {};
		step.type = STEP_MODIFY_PROPERTY;
		step.objectID = data->objectID;
		strcpy(step.property.cName, data->cPropertyName);

		if (data->elementType == INSPECTOR_COLOR_PICKER) {
			UIColorPicker *colorPicker = (UIColorPicker *) element;
			uint32_t color;
			UIColorToRGB(colorPicker->hue, colorPicker->saturation, colorPicker->value, &color);
			color |= (uint32_t) (colorPicker->opacity * 255.0f) << 24;
			step.property.type = PROP_COLOR;
			step.property.integer = (int32_t) color;
		} else if (data->elementType == INSPECTOR_COLOR_TEXTBOX) {
			UITextbox *textbox = (UITextbox *) element;
			char buffer[32];
			int length = 31 > textbox->bytes ? textbox->bytes : 31;
			memcpy(buffer, textbox->string, length);
			buffer[length] = 0;
			step.property.type = PROP_COLOR;
			step.property.integer = (int32_t) strtoul(buffer, nullptr, 16);
		} else if (data->elementType == INSPECTOR_INTEGER_TEXTBOX || data->elementType == INSPECTOR_INTEGER_TEXTBOX_BROADCAST) {
			UITextbox *textbox = (UITextbox *) element;
			char buffer[32];
			int length = 31 > textbox->bytes ? textbox->bytes : 31;
			memcpy(buffer, textbox->string, length);
			buffer[length] = 0;
			step.property.type = PROP_INT;
			step.property.integer = (int32_t) strtol(buffer, nullptr, 10);
			InspectorBroadcastStep(step, data);
		} else if (data->elementType == INSPECTOR_FLOAT_TEXTBOX) {
			UITextbox *textbox = (UITextbox *) element;
			char buffer[32];
			int length = 31 > textbox->bytes ? textbox->bytes : 31;
			memcpy(buffer, textbox->string, length);
			buffer[length] = 0;
			step.property.type = PROP_FLOAT;
			step.property.floating = strtof(buffer, nullptr);
			InspectorBroadcastStep(step, data);
		} else {
			UI_ASSERT(false);
		}

		inspectorActivePropertyStepElement = element; // Don't tell this element about the step.
		DocumentApplyStep(step);
		inspectorActivePropertyStepElement = nullptr;
	} else if (message == UI_MSG_CLICKED) {
		Step step = {};
		step.type = STEP_MODIFY_PROPERTY;
		step.objectID = data->objectID;
		strcpy(step.property.cName, data->cPropertyName);

		if (data->elementType == INSPECTOR_REMOVE_BUTTON || data->elementType == INSPECTOR_REMOVE_BUTTON_BROADCAST) {
			step.property.type = PROP_NONE; // Remove the property.
			InspectorBroadcastStep(step, data);
			DocumentApplyStep(step);
		} else if (data->elementType == INSPECTOR_LINK || data->elementType == INSPECTOR_LINK_BROADCAST) {
			char *name = nullptr;
			const char *dialog = "Enter the name of the new link target object:\n%t\n\n%l\n\n%f%b%b";
			const char *result = UIDialogShow(window, 0, dialog, &name, "OK", "Cancel");

			if (0 == strcmp(result, "OK")) {
				uint64_t id = 0;

				if (name && name[0]) {
					for (uintptr_t i = 0; i < objects.Length(); i++) {
						if (0 == strcmp(objects[i].cName, name)) {
							id = objects[i].id;
							break;
						}
					}
				}

				if (!id) {
					UIDialogShow(window, 0, "Error: The object was not found.\n%f%b", "OK");
				} else {
					step.property.type = PROP_OBJECT;
					step.property.object = id;
					InspectorBroadcastStep(step, data);
					DocumentApplyStep(step);
				}
			}

			free(name);
		} else if (data->elementType == INSPECTOR_RADIO_SWITCH) {
			step.property.type = PROP_INT;
			step.property.integer = data->choiceValue;
			DocumentApplyStep(step);
		} else if (data->elementType == INSPECTOR_BOOLEAN_TOGGLE) {
			UICheckbox *box = (UICheckbox *) element;
			step.property.integer = (box->check + 1) % ((box->e.flags & UI_CHECKBOX_ALLOW_INDETERMINATE) ? 3 : 2);
			step.property.type = step.property.integer == UI_CHECK_INDETERMINATE ? PROP_NONE : PROP_INT;
			DocumentApplyStep(step);
			return 1; // InspectorUpdateSingleElement will update the check.
		} else if (data->elementType == INSPECTOR_MASK_BIT_TOGGLE) {
			UICheckbox *box = (UICheckbox *) element;
			step.property.type = PROP_INT;
			step.property.integer = PropertyReadInt32(ObjectFind(data->objectID, false), data->cPropertyName);
			SetBit((uint32_t *) &step.property.integer, data->choiceValue, box->check == UI_CHECK_UNCHECKED);
			DocumentApplyStep(step);
			return 1; // InspectorUpdateSingleElement will update the check.
		} else if (data->elementType == INSPECTOR_CURSOR_DROP_DOWN) {
			UIMenu *menu = UIMenuCreate(window->pressed, UI_MENU_NO_SCROLL);
			UIMenuAddItem(menu, 0, "Inherit", -1, InspectorCursorDropDownMenuInvoke, (void *) (intptr_t) -1);

			for (uintptr_t i = 0; i < sizeof(cCursorStrings) / sizeof(cCursorStrings[0]); i++) {
				UIMenuAddItem(menu, 0, cCursorStrings[i], -1, InspectorCursorDropDownMenuInvoke, (void *) (intptr_t) i);
			}

			inspectorMenuData = data;
			UIMenuShow(menu);
		} else if (data->elementType == INSPECTOR_ADD_ARRAY_ITEM) {
			step.property.type = PROP_INT;
			step.property.integer = 1 + PropertyReadInt32(ObjectFind(data->objectID, false), data->cPropertyName);
			step.flags |= STEP_UPDATE_INSPECTOR;
			DocumentApplyStep(step);
		} else if (data->elementType == INSPECTOR_SWAP_ARRAY_ITEMS) {
			char cPrefix0[PROPERTY_NAME_SIZE];
			char cPrefix1[PROPERTY_NAME_SIZE];
			Object *object = ObjectFind(data->objectID, false);

			strcpy(cPrefix0, data->cPropertyName);
			strcpy(cPrefix1, data->cPropertyName);

			for (intptr_t i = strlen(cPrefix0) - 2; i >= 0; i--) {
				if (cPrefix0[i] == '_') {
					int32_t index = atoi(cPrefix0 + i + 1);
					sprintf(cPrefix1 + i + 1, "%d_", index + 1);
					break;
				}
			}

			DocumentSwapPropertyPrefixes(object, step, cPrefix0, cPrefix1, true /* last */, false);
		} else if (data->elementType == INSPECTOR_DELETE_ARRAY_ITEM) {
			char cPrefix0[PROPERTY_NAME_SIZE];
			char cPrefix1[PROPERTY_NAME_SIZE];
			int32_t index = -1;
			int32_t count = -1;
			intptr_t offset = strlen(data->cPropertyName) - 2;
			Object *object = ObjectFind(data->objectID, false);

			for (; offset >= 0; offset--) {
				if (data->cPropertyName[offset] == '_') {
					index = atoi(data->cPropertyName + offset + 1);
					break;
				}
			}

			strcpy(cPrefix0, data->cPropertyName);
			strcpy(cPrefix0 + offset + 1, "count");
			count = PropertyReadInt32(ObjectFind(data->objectID, false), cPrefix0);

			for (int32_t i = index; i < count - 1; i++) {
				strcpy(cPrefix0, data->cPropertyName);
				strcpy(cPrefix1, data->cPropertyName);
				sprintf(cPrefix0 + offset + 1, "%d_", i + 0);
				sprintf(cPrefix1 + offset + 1, "%d_", i + 1);
				DocumentSwapPropertyPrefixes(object, step, cPrefix0, cPrefix1, false, true /* moveOnly */);
			}

			strcpy(cPrefix0, data->cPropertyName);
			sprintf(cPrefix0 + offset + 1, "%d_", count - 1);

			Array<Step> steps = {};

			for (uintptr_t i = 0; i < object->properties.Length(); i++) {
				if (0 == memcmp(object->properties[i].cName, cPrefix0, strlen(cPrefix0))) {
					strcpy(step.property.cName, object->properties[i].cName);
					step.property.type = PROP_NONE;
					steps.Add(step);
				}
			}

			for (uintptr_t i = 0; i < steps.Length(); i++) {
				DocumentApplyStep(steps[i], STEP_APPLY_GROUPED);
			}

			steps.Free();

			strcpy(step.property.cName, data->cPropertyName);
			strcpy(step.property.cName + offset + 1, "count");
			step.property.type = PROP_INT;
			step.property.integer = count - 1;
			step.flags |= STEP_UPDATE_INSPECTOR;
			DocumentApplyStep(step);
		}
	} else if (message == UI_MSG_UPDATE) {
		if (di == UI_UPDATE_FOCUSED && element->window->focused == element 
				&& (data->elementType == INSPECTOR_COLOR_TEXTBOX || data->elementType == INSPECTOR_INTEGER_TEXTBOX
					|| data->elementType == INSPECTOR_INTEGER_TEXTBOX_BROADCAST || data->elementType == INSPECTOR_FLOAT_TEXTBOX)) {
			UITextbox *textbox = (UITextbox *) element;
			textbox->carets[0] = 0;
			textbox->carets[1] = textbox->bytes;
		}
	}

	return 0;
}

InspectorBindingData *InspectorBind(UIElement *element, uint64_t objectID, const char *cPropertyName, InspectorElementType elementType, 
		int32_t choiceValue = 0, const char *cEnablePropertyName = nullptr) {
	InspectorBindingData *data = (InspectorBindingData *) calloc(1, sizeof(InspectorBindingData));
	data->element = element;
	data->objectID = objectID;
	strcpy(data->cPropertyName, cPropertyName);
	data->elementType = elementType;
	data->choiceValue = choiceValue;
	data->cEnablePropertyName = cEnablePropertyName;
	element->cp = data;
	element->messageUser = InspectorBoundMessage;
	inspectorBoundElements.Add(element);
	InspectorUpdateSingleElement(data);
	if (cEnablePropertyName) InspectorUpdateSingleElementEnable(data);
	return data;
}

void InspectorRenameObject(void *) {
	Step step = {};
	step.type = STEP_RENAME_OBJECT;
	step.objectID = selectedObjectID;

	char *name = nullptr;
	const char *result = UIDialogShow(window, 0, "Enter the new name for the object:\n%t\n\n%l\n\n%f%b%b", &name, "OK", "Cancel");

	if (0 == strcmp(result, "OK")) {
		if (name && strlen(name) >= sizeof(step.cName) - 1) {
			UIDialogShow(window, 0, "Error: Name cannot have more than 46 characters.\n%f%b", "OK");
		} else {
			strcpy(step.cName, name ?: "");
			DocumentApplyStep(step);
		}
	}

	free(name);
}

void InspectorAutoNameObject(void *) {
	Object *object = ObjectFind(selectedObjectID, false);

	if (!ObjectIsConditional(object)) {
		UIDialogShow(window, 0, "Error: The object needs to be conditional to use auto-name.\n%f%b", "OK");
		return;
	}

	int32_t primaryState = PropertyReadInt32(object, "_primaryState");
	int32_t stateBits = PropertyReadInt32(object, "_stateBits");

	Step step = {};
	step.type = STEP_RENAME_OBJECT;
	step.objectID = selectedObjectID;

	snprintf(step.cName, sizeof(step.cName), "?%s", primaryState ? cPrimaryStateStrings[primaryState] : "");

	for (uintptr_t i = 0; i < 16; i++) {
		if (stateBits & (1 << (15 - i))) {
			snprintf(step.cName + strlen(step.cName), sizeof(step.cName) - strlen(step.cName), "%s%s", i || primaryState ? "&" : "", cStateBitStrings[i]);
		}
	}

	DocumentApplyStep(step);
}

void InspectorPickTargetCommand(void *cp) {
	if (inspectorPickData) {
		InspectorPickTargetEnd();
		return;
	}

	inspectorPickData = (InspectorBindingData *) cp;
	UIElementRepaint(canvas, nullptr);
}

void InspectorFindTargetCommand(void *cp) {
	InspectorBindingData *data = (InspectorBindingData *) cp;
	Property *property = PropertyFind(ObjectFind(data->objectID, false), data->cPropertyName, PROP_OBJECT);
	Object *target = ObjectFind(property ? property->object : 0, false);

	if (target) {
		CanvasSelectObject(target);
	} else {
		UIDialogShow(window, 0, "Error: The object does not exist.\n%f%b", "OK");
	}
}

void InspectorGoToInstanceStyle(void *) {
	Property *property = PropertyFind(ObjectFind(selectedObjectID, false), "style", PROP_OBJECT);
	Object *target = ObjectFind(property ? property->object : 0, false);

	if (target) {
		CanvasSelectObject(target);
	} else {
		UIDialogShow(window, 0, "Error: The object does not exist.\n%f%b", "OK");
	}
}

void InspectorToFrontCommand(void *) {
	Step step = {};
	step.type = STEP_SET_OBJECT_DEPTH;
	step.depth = objects.Length() - 1;
	step.objectID = selectedObjectID;
	DocumentApplyStep(step);
}

void InspectorToBackCommand(void *) {
	Step step = {};
	step.type = STEP_SET_OBJECT_DEPTH;
	step.depth = 0;
	step.objectID = selectedObjectID;
	DocumentApplyStep(step);
}

int InspectorTabPaneMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_LEFT_DOWN) {
		element->messageClass(element, message, di, dp);
		UIElementRefresh(inspector);
		return 1;
	}

	return 0;
}

void InspectorAddLink(Object *object, const char *cLabel, const char *cPropertyName, 
		bool broadcast = false, const char *cEnablePropertyName = nullptr) {
	if (cLabel) UILabelCreate(0, 0, cLabel, -1);
	UIElement *row = &UIPanelCreate(0, UI_PANEL_HORIZONTAL)->e;
	InspectorBindingData *data = InspectorBind(&UIButtonCreate(row, UI_ELEMENT_H_FILL, 0, 0)->e, 
			object->id, cPropertyName, broadcast ? INSPECTOR_LINK_BROADCAST : INSPECTOR_LINK, 0, cEnablePropertyName);
	UIButton *pickTarget = UIButtonCreate(row, UI_BUTTON_SMALL, "Pick", -1);
	pickTarget->e.cp = data;
	pickTarget->invoke = InspectorPickTargetCommand;
	UIButton *findTarget = UIButtonCreate(row, UI_BUTTON_SMALL, "Find", -1);
	findTarget->e.cp = data;
	findTarget->invoke = InspectorFindTargetCommand;
	if (!broadcast) InspectorBind(&UIButtonCreate(row, UI_BUTTON_SMALL, "X", 1)->e, object->id, cPropertyName, INSPECTOR_REMOVE_BUTTON);
}

void InspectorAddIntegerTextbox(Object *object, const char *cLabel, const char *cPropertyName, bool broadcast = false, const char *cEnablePropertyName = nullptr) {
	if (cLabel) UILabelCreate(0, 0, cLabel, -1);
	InspectorBind(&UITextboxCreate(0, UI_ELEMENT_H_FILL)->e, object->id, cPropertyName, 
			broadcast ? INSPECTOR_INTEGER_TEXTBOX_BROADCAST : INSPECTOR_INTEGER_TEXTBOX, 0, cEnablePropertyName);
}

void InspectorAddFloat(Object *object, const char *cLabel, const char *cPropertyName, const char *cEnablePropertyName = nullptr) {
	if (cLabel) UILabelCreate(0, 0, cLabel, -1);
	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_H_FILL | UI_PANEL_HORIZONTAL);
	InspectorBind(&UITextboxCreate(0, UI_ELEMENT_H_FILL)->e, object->id, cPropertyName, INSPECTOR_FLOAT_TEXTBOX, 0, cEnablePropertyName);
	InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, cPropertyName, INSPECTOR_REMOVE_BUTTON);
	UIParentPop();
}

void InspectorAddInteger(Object *object, const char *cLabel, const char *cPropertyName) {
	if (cLabel) UILabelCreate(0, 0, cLabel, -1);
	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_H_FILL | UI_PANEL_HORIZONTAL);
	UITabPane *tabPane = UITabPaneCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_H_FILL, "Direct\tLink");
	tabPane->e.messageUser = InspectorTabPaneMessage;
	InspectorAddIntegerTextbox(object, nullptr, cPropertyName);
	InspectorAddLink(object, nullptr, cPropertyName);
	UIParentPop();
	Property *property = PropertyFind(object, cPropertyName);
	if (property && property->type == PROP_OBJECT) tabPane->active = 1;
	InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, cPropertyName, INSPECTOR_REMOVE_BUTTON);
	UIParentPop();
}

void InspectorAddFourGroup(Object *object, const char *cLabel, const char *cPropertyNamePrefix, 
		const char *cEnablePropertyName = nullptr, bool defaultToIndividualTab = false) {
	char cPropertyName0[PROPERTY_NAME_SIZE]; strcpy(cPropertyName0, cPropertyNamePrefix); strcat(cPropertyName0, "0");
	char cPropertyName1[PROPERTY_NAME_SIZE]; strcpy(cPropertyName1, cPropertyNamePrefix); strcat(cPropertyName1, "1");
	char cPropertyName2[PROPERTY_NAME_SIZE]; strcpy(cPropertyName2, cPropertyNamePrefix); strcat(cPropertyName2, "2");
	char cPropertyName3[PROPERTY_NAME_SIZE]; strcpy(cPropertyName3, cPropertyNamePrefix); strcat(cPropertyName3, "3");
	if (cLabel) UILabelCreate(0, 0, cLabel, -1);
	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_H_FILL | UI_PANEL_HORIZONTAL);
	UITabPane *tabPane = UITabPaneCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_H_FILL, "Single\tIndividual\tLink");
	tabPane->e.messageUser = InspectorTabPaneMessage;
	InspectorAddIntegerTextbox(object, nullptr, cPropertyName0, true /* broadcast */, cEnablePropertyName);
	UIElement *row = &UIPanelCreate(0, UI_PANEL_HORIZONTAL)->e;
	InspectorBind(&UITextboxCreate(row, UI_ELEMENT_H_FILL)->e, object->id, cPropertyName0, INSPECTOR_INTEGER_TEXTBOX, 0, cEnablePropertyName);
	InspectorBind(&UITextboxCreate(row, UI_ELEMENT_H_FILL)->e, object->id, cPropertyName1, INSPECTOR_INTEGER_TEXTBOX, 0, cEnablePropertyName);
	InspectorBind(&UITextboxCreate(row, UI_ELEMENT_H_FILL)->e, object->id, cPropertyName2, INSPECTOR_INTEGER_TEXTBOX, 0, cEnablePropertyName);
	InspectorBind(&UITextboxCreate(row, UI_ELEMENT_H_FILL)->e, object->id, cPropertyName3, INSPECTOR_INTEGER_TEXTBOX, 0, cEnablePropertyName);
	InspectorAddLink(object, nullptr, cPropertyName0, true /* broadcast */, cEnablePropertyName);
	UIParentPop();
	Property *property = PropertyFind(object, cPropertyName0);
	int32_t b0 = PropertyReadInt32(object, cPropertyName0);
	int32_t b1 = PropertyReadInt32(object, cPropertyName1);
	int32_t b2 = PropertyReadInt32(object, cPropertyName2);
	int32_t b3 = PropertyReadInt32(object, cPropertyName3);
	if (property && property->type == PROP_OBJECT) tabPane->active = 2;
	else if (defaultToIndividualTab || b0 != b1 || b1 != b2 || b2 != b3) tabPane->active = 1;
	InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, cPropertyName0, INSPECTOR_REMOVE_BUTTON_BROADCAST);
	UIParentPop();
}

void InspectorAddBooleanToggle(Object *object, const char *cLabel, const char *cPropertyName) {
	InspectorBind(&UICheckboxCreate(0, UI_CHECKBOX_ALLOW_INDETERMINATE, cLabel, -1)->e, object->id, cPropertyName, INSPECTOR_BOOLEAN_TOGGLE);
}

void InspectorAddRadioSwitch(Object *object, const char *cLabel, const char *cPropertyName, int32_t choiceValue) {
	InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, cLabel, -1)->e, object->id, cPropertyName, INSPECTOR_RADIO_SWITCH, choiceValue);
}

void InspectorAddMaskBitToggle(Object *object, const char *cLabel, const char *cPropertyName, int32_t bit) {
	InspectorBind(&UICheckboxCreate(0, 0, cLabel, -1)->e, object->id, cPropertyName, INSPECTOR_MASK_BIT_TOGGLE, bit);
}

int InspectorPreviewPrimaryStateButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		UIElement *sibling = element->parent->children;
		canvas->previewPrimaryState = (uintptr_t) element->cp;
		UIElementRefresh(canvas);

		while (sibling) {
			SetBit(&sibling->flags, UI_BUTTON_CHECKED, sibling == element);
			UIElementRefresh(sibling);
			sibling = sibling->next;
		}
	}

	return 0;
}

void InspectorAddPreviewPrimaryStateButton(const char *cLabel, int32_t value) {
	UIButton *button = UIButtonCreate(0, UI_BUTTON_SMALL, cLabel, -1);
	button->e.cp = (void *) (uintptr_t) value;
	button->e.messageUser = InspectorPreviewPrimaryStateButtonMessage;
	if (canvas->previewPrimaryState == value) button->e.flags |= UI_BUTTON_CHECKED;
}

void InspectorPreviewStateBitsCheckboxInvoke(void *cp) {
	canvas->previewStateBits ^= (uintptr_t) cp;
	UIElementRefresh(canvas);
}

void InspectorAddPreviewStateBitsCheckbox(const char *cLabel, int32_t bit) {
	UICheckbox *box = UICheckboxCreate(0, 0, cLabel, -1);
	box->e.cp = (void *) (uintptr_t) bit;
	box->invoke = InspectorPreviewStateBitsCheckboxInvoke;
	if (canvas->previewStateBits & bit) box->check = UI_CHECK_CHECKED;
}

void InspectorPopulate() {
	UIElementDestroyDescendents(inspector);
	UIParentPush(inspector);

	Object *object = ObjectFind(selectedObjectID, false);
	size_t referenceCount = 0;

	if (object) {
		for (uintptr_t i = 0; i < objects.Length(); i++) {
			for (uintptr_t j = 0; j < objects[i].properties.Length(); j++) {
				if (objects[i].properties[j].type == PROP_OBJECT 
						&& objects[i].properties[j].object == object->id) {
					referenceCount++;
				}
			}
		}
	}

	if (object && object->type != OBJ_INSTANCE) {
		UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_BORDER | UI_PANEL_MEDIUM_SPACING | UI_PANEL_EXPAND);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			char buffer[256];
			snprintf(buffer, sizeof(buffer), "%lu: %s", object->id, object->cName[0] ? object->cName : "(untitled)");
			UILabelCreate(0, 0, buffer, -1);
			UISpacerCreate(0, UI_ELEMENT_H_FILL, 0, 0);
			UIButtonCreate(0, UI_BUTTON_SMALL, "Auto", -1)->invoke = InspectorAutoNameObject;
			UIButtonCreate(0, UI_BUTTON_SMALL, "Rename", -1)->invoke = InspectorRenameObject;
			UIParentPop();
			snprintf(buffer, sizeof(buffer), "%ld references", referenceCount);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			UILabelCreate(0, 0, buffer, -1);
			UISpacerCreate(0, UI_ELEMENT_H_FILL, 0, 0);
			UIButton *button = UIButtonCreate(0, UI_BUTTON_SMALL, "Change type \x19", -1);
			button->invoke = ObjectAddCommand;
			button->e.cp = (void *) ObjectChangeTypeInternal;
			UIParentPop();

			bool inheritWithAnimation = object->type == OBJ_VAR_TEXT_STYLE
				|| object->type == OBJ_LAYER_BOX || object->type == OBJ_LAYER_TEXT || object->type == OBJ_LAYER_PATH
				|| object->type == OBJ_PAINT_OVERWRITE || object->type == OBJ_PAINT_LINEAR_GRADIENT || object->type == OBJ_PAINT_RADIAL_GRADIENT
				|| object->type == OBJ_VAR_CONTOUR_STYLE;
			bool inheritWithoutAnimation = object->type == OBJ_STYLE || object->type == OBJ_LAYER_METRICS;

			if (inheritWithAnimation || inheritWithoutAnimation) {
				InspectorAddLink(object, "Inherit from:", "_parent");

				if (inheritWithAnimation) {
					UILabelCreate(0, 0, "Primary state:", -1);
					UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
					InspectorAddRadioSwitch(object, "Idle", "_primaryState", THEME_PRIMARY_STATE_IDLE);
					InspectorAddRadioSwitch(object, "Hovered", "_primaryState", THEME_PRIMARY_STATE_HOVERED);
					InspectorAddRadioSwitch(object, "Pressed", "_primaryState", THEME_PRIMARY_STATE_PRESSED);
					InspectorAddRadioSwitch(object, "Disabled", "_primaryState", THEME_PRIMARY_STATE_DISABLED);
					InspectorAddRadioSwitch(object, "Inactive", "_primaryState", THEME_PRIMARY_STATE_INACTIVE);
					InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, "_primaryState", INSPECTOR_REMOVE_BUTTON);
					UIParentPop();

					UILabelCreate(0, 0, "State bits:", -1);
					UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_EXPAND)->gap = -5;
					UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL)->gap = 8;
					InspectorAddMaskBitToggle(object, cStateBitStrings[0], "_stateBits", THEME_STATE_FOCUSED);
					InspectorAddMaskBitToggle(object, cStateBitStrings[1], "_stateBits", THEME_STATE_CHECKED);
					InspectorAddMaskBitToggle(object, cStateBitStrings[2], "_stateBits", THEME_STATE_INDETERMINATE);
					InspectorAddMaskBitToggle(object, cStateBitStrings[3], "_stateBits", THEME_STATE_DEFAULT_BUTTON);
					InspectorAddMaskBitToggle(object, cStateBitStrings[4], "_stateBits", THEME_STATE_SELECTED);
					UIParentPop();
					UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL)->gap = 8;
					InspectorAddMaskBitToggle(object, cStateBitStrings[5], "_stateBits", THEME_STATE_FOCUSED_ITEM);
					InspectorAddMaskBitToggle(object, cStateBitStrings[6], "_stateBits", THEME_STATE_LIST_FOCUSED);
					InspectorAddMaskBitToggle(object, cStateBitStrings[7], "_stateBits", THEME_STATE_BEFORE_ENTER);
					InspectorAddMaskBitToggle(object, cStateBitStrings[8], "_stateBits", THEME_STATE_AFTER_EXIT);
					UIParentPop();
					UIParentPop();

					UILabelCreate(0, 0, "Transition duration:", -1);
					InspectorAddInteger(object, nullptr, "_duration");
				}
			}
		UIParentPop();

		UISpacerCreate(0, 0, 0, 10);

		if (object->type == OBJ_STYLE) {
			InspectorAddLink(object, "Appearance:", "appearance");
			InspectorAddLink(object, "Metrics:", "metrics");
			InspectorAddLink(object, "Text style:", "textStyle");
			InspectorAddBooleanToggle(object, "Public style", "isPublic");

			char buffer[128];
			snprintf(buffer, sizeof(buffer), "Header ID: %d", PropertyReadInt32(object, "headerID"));
			UILabelCreate(0, 0, buffer, -1);
		} else if (object->type == OBJ_SELECTOR) {
			int32_t optionCount = PropertyReadInt32(object, "options_count");
			if (optionCount < 0) optionCount = 0;
			if (optionCount > 100) optionCount = 100;

			for (int32_t i = 0; i < optionCount; i++) {
				char cPropertyName[PROPERTY_NAME_SIZE];
				UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_BORDER | UI_PANEL_MEDIUM_SPACING | UI_PANEL_EXPAND);
				sprintf(cPropertyName, "options_%d_", i);
				UIPanel *row = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
				UISpacerCreate(&row->e, UI_ELEMENT_H_FILL, 0, 0);
				InspectorBind(&UIButtonCreate(&row->e, UI_BUTTON_SMALL, "Delete", -1)->e, object->id, cPropertyName, INSPECTOR_DELETE_ARRAY_ITEM);
				sprintf(cPropertyName, "options_%d_link", i);
				InspectorAddLink(object, "Link:", cPropertyName);
				UIParentPop();

				if (i != optionCount - 1) {
					sprintf(cPropertyName, "options_%d_", i);
					InspectorBind(&UIButtonCreate(&UIPanelCreate(0, 0)->e, UI_BUTTON_SMALL, "Swap", -1)->e, 
							object->id, cPropertyName, INSPECTOR_SWAP_ARRAY_ITEMS);
				}
			}

			InspectorBind(&UIButtonCreate(0, 0, "Add option", -1)->e, object->id, "options_count", INSPECTOR_ADD_ARRAY_ITEM);
		} else if (object->type == OBJ_VAR_COLOR) {
			InspectorBind(&UIColorPickerCreate(&UIPanelCreate(0, 0)->e, UI_COLOR_PICKER_HAS_OPACITY)->e, object->id, "color", INSPECTOR_COLOR_PICKER);
			InspectorBind(&UITextboxCreate(0, 0)->e, object->id, "color", INSPECTOR_COLOR_TEXTBOX);
			InspectorAddBooleanToggle(object, "Export to theme file", "isExported");
		} else if (object->type == OBJ_VAR_INT) {
			InspectorBind(&UITextboxCreate(0, 0)->e, object->id, "value", INSPECTOR_INTEGER_TEXTBOX);
			InspectorAddBooleanToggle(object, "Export to theme file", "isExported");
			InspectorAddBooleanToggle(object, "Apply UI scaling factor", "isScaled");
		} else if (object->type == OBJ_LAYER_METRICS) {
			UILabelCreate(0, 0, "General options:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddBooleanToggle(object, "Enable clipping", "clipEnabled");
			InspectorAddBooleanToggle(object, "Wrap text", "wrapText");
			InspectorAddBooleanToggle(object, "Ellipsis", "ellipsis");
			UIParentPop();

			InspectorAddFourGroup(object, "Insets:", "insets");
			InspectorAddFourGroup(object, "Clip insets:", "clipInsets", "clipEnabled");

			UILabelCreate(0, 0, "Preferred size:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddInteger(object, nullptr, "preferredWidth");
			InspectorAddInteger(object, nullptr, "preferredHeight");
			UIParentPop();

			UILabelCreate(0, 0, "Minimum size:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddInteger(object, nullptr, "minimumWidth");
			InspectorAddInteger(object, nullptr, "minimumHeight");
			UIParentPop();

			UILabelCreate(0, 0, "Maximum size:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddInteger(object, nullptr, "maximumWidth");
			InspectorAddInteger(object, nullptr, "maximumHeight");
			UIParentPop();

			UILabelCreate(0, 0, "Gaps:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddInteger(object, nullptr, "gapMajor");
			InspectorAddInteger(object, nullptr, "gapMinor");
			InspectorAddInteger(object, nullptr, "gapWrap");
			UIParentPop();

			UILabelCreate(0, 0, "Horizontal text alignment:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddRadioSwitch(object, "Left", "horizontalTextAlign", 1);
			InspectorAddRadioSwitch(object, "Center", "horizontalTextAlign", 2);
			InspectorAddRadioSwitch(object, "Right", "horizontalTextAlign", 3);
			InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, "horizontalTextAlign", INSPECTOR_REMOVE_BUTTON);
			UIParentPop();

			UILabelCreate(0, 0, "Vertical text alignment:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddRadioSwitch(object, "Top", "verticalTextAlign", 1);
			InspectorAddRadioSwitch(object, "Center", "verticalTextAlign", 2);
			InspectorAddRadioSwitch(object, "Bottom", "verticalTextAlign", 3);
			InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, "verticalTextAlign", INSPECTOR_REMOVE_BUTTON);
			UIParentPop();

			UILabelCreate(0, 0, "Cursor style:", -1);
			InspectorBind(&UIButtonCreate(0, UI_BUTTON_DROP_DOWN, 0, 0)->e, object->id, "cursor", INSPECTOR_CURSOR_DROP_DOWN);
		} else if (object->type == OBJ_VAR_TEXT_STYLE) {
			InspectorAddLink(object, "Text color:", "textColor");
			InspectorAddLink(object, "Selection background color:", "selectedBackground");
			InspectorAddLink(object, "Selection text color:", "selectedText");
			InspectorAddInteger(object, "Text size:", "textSize");
			InspectorAddInteger(object, "Font weight:", "fontWeight");

			UILabelCreate(0, 0, "Font options:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddBooleanToggle(object, "Italic", "isItalic");
			UIParentPop();

			UILabelCreate(0, 0, "Font family:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddRadioSwitch(object, "Sans-serif", "fontFamily", ES_FONT_SANS);
			InspectorAddRadioSwitch(object, "Serif", "fontFamily", ES_FONT_SERIF);
			InspectorAddRadioSwitch(object, "Monospaced", "fontFamily", ES_FONT_MONOSPACED);
			InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, "fontFamily", INSPECTOR_REMOVE_BUTTON);
			UIParentPop();

			InspectorAddLink(object, "Icon color:", "iconColor");
			InspectorAddInteger(object, "Icon size:", "iconSize");

#if 0
			UIButton *removeDuplicates = UIButtonCreate(0, 0, "Remove duplicates", -1);
			removeDuplicates->e.cp = (void *) (uintptr_t) object->id;
			removeDuplicates->invoke = TextStyleRemoveDuplicates;
#endif
		} else if (object->type == OBJ_VAR_CONTOUR_STYLE) {
			InspectorAddInteger(object, "Internal width:", "internalWidth");
			InspectorAddInteger(object, "External width:", "externalWidth");
			InspectorAddBooleanToggle(object, "When scaling, snap to integer widths", "integerWidthsOnly");
			InspectorAddFloat(object, "Miter limit:", "miterLimit");

			UILabelCreate(0, 0, "Join mode:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddRadioSwitch(object, "Miter", "joinMode", RAST_LINE_JOIN_MITER);
			InspectorAddRadioSwitch(object, "Round", "joinMode", RAST_LINE_JOIN_ROUND);
			InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, "joinMode", INSPECTOR_REMOVE_BUTTON);
			UIParentPop();

			UILabelCreate(0, 0, "Cap mode:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddRadioSwitch(object, "Flat", "capMode", RAST_LINE_CAP_FLAT);
			InspectorAddRadioSwitch(object, "Square", "capMode", RAST_LINE_CAP_SQUARE);
			InspectorAddRadioSwitch(object, "Round", "capMode", RAST_LINE_CAP_ROUND);
			InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, "capMode", INSPECTOR_REMOVE_BUTTON);
			UIParentPop();
		} else if (object->type == OBJ_LAYER_BOX) {
			InspectorAddFourGroup(object, "Border sizes:", "borders");
			InspectorAddFourGroup(object, "Corner radii:", "corners");
			InspectorAddLink(object, "Fill paint:", "mainPaint");
			InspectorAddLink(object, "Border paint:", "borderPaint");

			InspectorAddBooleanToggle(object, "Blurred", "isBlurred");
			InspectorAddBooleanToggle(object, "Auto-corners", "autoCorners");
			InspectorAddBooleanToggle(object, "Auto-borders", "autoBorders");
			InspectorAddBooleanToggle(object, "Shadow hiding", "shadowHiding");

			InspectorAddFourGroup(object, "Offset (dpx):", "offset", nullptr, true);
		} else if (object->type == OBJ_LAYER_PATH) {
			InspectorAddBooleanToggle(object, "Use even-odd fill rule", "pathFillEvenOdd");
			InspectorAddBooleanToggle(object, "Path is closed", "pathClosed");
			InspectorAddIntegerTextbox(object, "Alpha", "alpha");

			int32_t pointCount = PropertyReadInt32(object, "points_count");
			if (pointCount < 0) pointCount = 0;
			if (pointCount > 100) pointCount = 100;

			for (int32_t i = 0; i < pointCount; i++) {
				char cPropertyName[PROPERTY_NAME_SIZE];
				UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
				sprintf(cPropertyName, "points_%d_x0", i);
				InspectorAddFloat(object, nullptr, cPropertyName);
				sprintf(cPropertyName, "points_%d_y0", i);
				InspectorAddFloat(object, nullptr, cPropertyName);
				sprintf(cPropertyName, "points_%d_x1", i);
				InspectorAddFloat(object, nullptr, cPropertyName);
				sprintf(cPropertyName, "points_%d_y1", i);
				InspectorAddFloat(object, nullptr, cPropertyName);
				sprintf(cPropertyName, "points_%d_x2", i);
				InspectorAddFloat(object, nullptr, cPropertyName);
				sprintf(cPropertyName, "points_%d_y2", i);
				InspectorAddFloat(object, nullptr, cPropertyName);
				sprintf(cPropertyName, "points_%d_", i);
				InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "Delete", -1)->e, object->id, cPropertyName, INSPECTOR_DELETE_ARRAY_ITEM);
				UIParentPop();
			}

			InspectorBind(&UIButtonCreate(0, 0, "Add point", -1)->e, object->id, "points_count", INSPECTOR_ADD_ARRAY_ITEM);

			int32_t fillCount = PropertyReadInt32(object, "fills_count");
			if (fillCount < 0) fillCount = 0;
			if (fillCount > 100) fillCount = 100;

			for (int32_t i = 0; i < fillCount; i++) {
				char cPropertyName[PROPERTY_NAME_SIZE];
				UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_BORDER | UI_PANEL_MEDIUM_SPACING | UI_PANEL_EXPAND);
				sprintf(cPropertyName, "fills_%d_", i);
				UIPanel *row = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
				UISpacerCreate(&row->e, UI_ELEMENT_H_FILL, 0, 0);
				InspectorBind(&UIButtonCreate(&row->e, UI_BUTTON_SMALL, "Delete", -1)->e, object->id, cPropertyName, INSPECTOR_DELETE_ARRAY_ITEM);
				sprintf(cPropertyName, "fills_%d_paint", i);
				InspectorAddLink(object, "Paint:", cPropertyName);
				sprintf(cPropertyName, "fills_%d_mode", i);
				InspectorAddLink(object, "Mode:", cPropertyName);
				UIParentPop();

				if (i != fillCount - 1) {
					sprintf(cPropertyName, "fills_%d_", i);
					InspectorBind(&UIButtonCreate(&UIPanelCreate(0, 0)->e, UI_BUTTON_SMALL, "Swap", -1)->e, 
							object->id, cPropertyName, INSPECTOR_SWAP_ARRAY_ITEMS);
				}
			}

			InspectorBind(&UIButtonCreate(0, 0, "Add fill", -1)->e, object->id, "fills_count", INSPECTOR_ADD_ARRAY_ITEM);
		} else if (object->type == OBJ_LAYER_TEXT) {
			InspectorAddLink(object, "Text color:", "color");
			InspectorAddInteger(object, "Blur radius:", "blur");
		} else if (object->type == OBJ_LAYER_GROUP) {
			int32_t layerCount = PropertyReadInt32(object, "layers_count");
			if (layerCount < 0) layerCount = 0;
			if (layerCount > 100) layerCount = 100;

			for (int32_t i = 0; i < layerCount; i++) {
				char cPropertyName[PROPERTY_NAME_SIZE];
				UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_BORDER | UI_PANEL_MEDIUM_SPACING | UI_PANEL_EXPAND);
				sprintf(cPropertyName, "layers_%d_", i);
				UIPanel *row = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
				UISpacerCreate(&row->e, UI_ELEMENT_H_FILL, 0, 0);
				InspectorBind(&UIButtonCreate(&row->e, UI_BUTTON_SMALL, "Delete", -1)->e, object->id, cPropertyName, INSPECTOR_DELETE_ARRAY_ITEM);
				sprintf(cPropertyName, "layers_%d_layer", i);
				InspectorAddLink(object, "Layer:", cPropertyName);
				sprintf(cPropertyName, "layers_%d_offset", i);
				InspectorAddFourGroup(object, "Offset (dpx):", cPropertyName, nullptr, true /* defaultToIndividualTab */);
				sprintf(cPropertyName, "layers_%d_position", i);
				InspectorAddFourGroup(object, "Position (%):", cPropertyName, nullptr, true /* defaultToIndividualTab */);
				sprintf(cPropertyName, "layers_%d_mode", i);
				UILabelCreate(0, 0, "Mode:", -1);
				UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
				InspectorAddRadioSwitch(object, "Background", cPropertyName, THEME_LAYER_MODE_BACKGROUND);
				InspectorAddRadioSwitch(object, "Shadow", cPropertyName, THEME_LAYER_MODE_SHADOW);
				InspectorAddRadioSwitch(object, "Content", cPropertyName, THEME_LAYER_MODE_CONTENT);
				InspectorAddRadioSwitch(object, "Overlay", cPropertyName, THEME_LAYER_MODE_OVERLAY);
				InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, cPropertyName, INSPECTOR_REMOVE_BUTTON);
				UIParentPop();
				UIParentPop();

				if (i != layerCount - 1) {
					sprintf(cPropertyName, "layers_%d_", i);
					InspectorBind(&UIButtonCreate(&UIPanelCreate(0, 0)->e, UI_BUTTON_SMALL, "Swap", -1)->e, 
							object->id, cPropertyName, INSPECTOR_SWAP_ARRAY_ITEMS);
				}
			}

			InspectorBind(&UIButtonCreate(0, 0, "Add layer", -1)->e, object->id, "layers_count", INSPECTOR_ADD_ARRAY_ITEM);
		} else if (object->type == OBJ_PAINT_OVERWRITE) {
			InspectorAddLink(object, "Color:", "color");
		} else if (object->type == OBJ_PAINT_LINEAR_GRADIENT || object->type == OBJ_PAINT_RADIAL_GRADIENT) {
			if (object->type == OBJ_PAINT_LINEAR_GRADIENT) {
				InspectorAddFloat(object, "Transform X:", "transformX");
				InspectorAddFloat(object, "Transform Y:", "transformY");
				InspectorAddFloat(object, "Transform start:", "transformStart");
			} else {
				InspectorAddFloat(object, "Transform X scale:",  "transform0");
				InspectorAddFloat(object, "Transform X offset:", "transform2");
				InspectorAddFloat(object, "Transform Y scale:",  "transform4");
				InspectorAddFloat(object, "Transform Y offset:", "transform5");
				InspectorAddFloat(object, "Transform X skew:",   "transform1");
				InspectorAddFloat(object, "Transform Y skew:",   "transform3");
			}

			UILabelCreate(0, 0, "Repeat mode:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddRadioSwitch(object, "Clamp", "repeatMode", RAST_REPEAT_CLAMP);
			InspectorAddRadioSwitch(object, "Normal", "repeatMode", RAST_REPEAT_NORMAL);
			InspectorAddRadioSwitch(object, "Mirror", "repeatMode", RAST_REPEAT_MIRROR);
			InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, "repeatMode", INSPECTOR_REMOVE_BUTTON);
			UIParentPop();

			InspectorAddBooleanToggle(object, "Use gamma interpolation", "useGammaInterpolation");

			int32_t stopCount = PropertyReadInt32(object, "stops_count");
			if (stopCount < 0) stopCount = 0;
			if (stopCount > 100) stopCount = 100;

			UILabelCreate(0, 0, "Gradient stops:", -1);

			for (int32_t i = 0; i < stopCount; i++) {
				char cPropertyName[PROPERTY_NAME_SIZE];
				UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_BORDER | UI_PANEL_MEDIUM_SPACING | UI_PANEL_EXPAND);
				sprintf(cPropertyName, "stops_%d_", i);
				UIPanel *row = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
				UISpacerCreate(&row->e, UI_ELEMENT_H_FILL, 0, 0);
				InspectorBind(&UIButtonCreate(&row->e, UI_BUTTON_SMALL, "Delete", -1)->e, object->id, cPropertyName, INSPECTOR_DELETE_ARRAY_ITEM);
				sprintf(cPropertyName, "stops_%d_color", i);
				InspectorAddLink(object, "Color:", cPropertyName);
				sprintf(cPropertyName, "stops_%d_position", i);
				InspectorAddInteger(object, "Position (%):", cPropertyName);
				sprintf(cPropertyName, "stops_%d_wci", i);
				InspectorAddInteger(object, "Window color index:", cPropertyName);
				UIParentPop();

				if (i != stopCount - 1) {
					sprintf(cPropertyName, "stops_%d_", i);
					InspectorBind(&UIButtonCreate(&UIPanelCreate(0, 0)->e, UI_BUTTON_SMALL, "Swap", -1)->e, 
							object->id, cPropertyName, INSPECTOR_SWAP_ARRAY_ITEMS);
				}
			}

			InspectorBind(&UIButtonCreate(0, 0, "Add stop", -1)->e, object->id, "stops_count", INSPECTOR_ADD_ARRAY_ITEM);
		} else if (object->type == OBJ_MOD_COLOR) {
			InspectorAddLink(object, "Base color:", "base");
			UILabelCreate(0, 0, "Brightness (%):", -1);
			InspectorBind(&UITextboxCreate(0, UI_ELEMENT_H_FILL)->e, object->id, "brightness", INSPECTOR_INTEGER_TEXTBOX);
			UILabelCreate(0, 0, "Hue shift (deg):", -1);
			InspectorBind(&UITextboxCreate(0, UI_ELEMENT_H_FILL)->e, object->id, "hueShift", INSPECTOR_INTEGER_TEXTBOX);
		} else if (object->type == OBJ_MOD_MULTIPLY) {
			InspectorAddLink(object, "Base integer:", "base");
			UILabelCreate(0, 0, "Factor (%):", -1);
			InspectorBind(&UITextboxCreate(0, UI_ELEMENT_H_FILL)->e, object->id, "factor", INSPECTOR_INTEGER_TEXTBOX);
		}
	} else if (object && object->type == OBJ_INSTANCE) {
		Property *property = PropertyFind(object, "style", PROP_OBJECT);
		Object *style = ObjectFind(property ? property->object : 0, false);
		char buffer[128];

		if (style) {
			snprintf(buffer, sizeof(buffer), "Instance of style '%s'.", style->cName);
		} else {
			snprintf(buffer, sizeof(buffer), "Instance of deleted style.");
		}

		UILabelCreate(0, 0, buffer, -1);

		if (style) {
			UIButtonCreate(&UIPanelCreate(0, UI_PANEL_HORIZONTAL)->e, 0, "View in graph \x1A", -1)->invoke = InspectorGoToInstanceStyle;
		}

		UISpacerCreate(0, 0, 0, 10);

		UILabelCreate(0, 0, "Depth:", -1);
		UIPanelCreate(0, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
		UIButtonCreate(0, 0, "To front", -1)->invoke = InspectorToFrontCommand;
		UIButtonCreate(0, 0, "To back", -1)->invoke = InspectorToBackCommand;
		UIParentPop();

		UISpacerCreate(0, 0, 0, 10);

		UILabelCreate(0, 0, "Preview state:", -1);
		UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
		InspectorAddPreviewPrimaryStateButton(cPrimaryStateStrings[1], THEME_PRIMARY_STATE_IDLE);
		InspectorAddPreviewPrimaryStateButton(cPrimaryStateStrings[2], THEME_PRIMARY_STATE_HOVERED);
		InspectorAddPreviewPrimaryStateButton(cPrimaryStateStrings[3], THEME_PRIMARY_STATE_PRESSED);
		InspectorAddPreviewPrimaryStateButton(cPrimaryStateStrings[4], THEME_PRIMARY_STATE_DISABLED);
		InspectorAddPreviewPrimaryStateButton(cPrimaryStateStrings[5], THEME_PRIMARY_STATE_INACTIVE);
		UIParentPop();
		UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_EXPAND)->gap = -5;
		UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL)->gap = 8;
		InspectorAddPreviewStateBitsCheckbox(cStateBitStrings[0], 1 << 15);
		InspectorAddPreviewStateBitsCheckbox(cStateBitStrings[1], 1 << 14);
		InspectorAddPreviewStateBitsCheckbox(cStateBitStrings[2], 1 << 13);
		InspectorAddPreviewStateBitsCheckbox(cStateBitStrings[3], 1 << 12);
		InspectorAddPreviewStateBitsCheckbox(cStateBitStrings[4], 1 << 11);
		UIParentPop();
		UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL)->gap = 8;
		InspectorAddPreviewStateBitsCheckbox(cStateBitStrings[5], 1 << 10);
		InspectorAddPreviewStateBitsCheckbox(cStateBitStrings[6], 1 <<  9);
		InspectorAddPreviewStateBitsCheckbox(cStateBitStrings[7], 1 <<  8);
		InspectorAddPreviewStateBitsCheckbox(cStateBitStrings[8], 1 <<  7);
		UIParentPop();
		UIParentPop();

		Rectangle8 paintOutsets = ExportCalculatePaintOutsets(PropertyFindOrInheritReadObject(style, "appearance"));
		char paintOutsetsText[256];
		snprintf(paintOutsetsText, sizeof(paintOutsetsText), "Paint outsets: %d, %d, %d, %d.", UI_RECT_ALL(paintOutsets));
		UILabelCreate(0, 0, paintOutsetsText, -1);
	} else {
		UILabelCreate(0, 0, "Select an object to inspect.", -1);
	}

	UIParentPop();
	UIElementRefresh(inspector);
}

//////////////////////////////////////////////////////////////

int32_t GraphGetInteger(Object *object, int depth = 0) {
	if (!object || depth == 100) {
		return 0;
	}

	if (object->type == OBJ_VAR_INT) {
		return PropertyReadInt32(object, "value");
	} else if (object->type == OBJ_MOD_MULTIPLY) {
		Property *property = PropertyFind(object, "base", PROP_OBJECT);
		int32_t base = GraphGetInteger(ObjectFind(property ? property->object : 0, true), depth + 1);
		int32_t factor = PropertyReadInt32(object, "factor");
		return base * factor / 100;
	} else {
		return 0;
	}
}

int32_t GraphGetIntegerFromProperty(Property *property) {
	if (!property) {
		return 0;
	} else if (property->type == PROP_INT) {
		return property->integer;
	} else if (property->type == PROP_OBJECT) {
		return GraphGetInteger(ObjectFind(property->object, true));
	} else {
		return 0;
	}
}

uint32_t GraphGetColor(Object *object, int depth = 0) {
	if (!object || depth == 100) {
		return 0;
	}

	if (object->type == OBJ_VAR_COLOR) {
		return PropertyReadInt32(object, "color");
	} else if (object->type == OBJ_MOD_COLOR) {
		Property *property = PropertyFind(object, "base", PROP_OBJECT);
		uint32_t base = GraphGetColor(ObjectFind(property ? property->object : 0, true), depth + 1);
		uint32_t alpha = base & 0xFF000000;
		int32_t brightness = PropertyReadInt32(object, "brightness");
		int32_t hueShift = PropertyReadInt32(object, "hueShift");
		double hue, saturation, luminosity, red, green, blue;
		rgb2hsluv(UI_COLOR_RED_F(base), UI_COLOR_GREEN_F(base), UI_COLOR_BLUE_F(base), &hue, &saturation, &luminosity);
		luminosity += luminosity * brightness / 100.0f;
		hue = fmod(hue + hueShift, 360.0);
		if (luminosity < 0.0) luminosity = 0.0;
		if (luminosity > 100.0) luminosity = 100.0;
		hsluv2rgb(hue, saturation, luminosity, &red, &green, &blue);
		return UI_COLOR_FROM_FLOAT(red, green, blue) | alpha;
	} else {
		return 0;
	}
}

uint32_t GraphGetColorFromProperty(Property *property) {
	if (!property) {
		return 0;
	} else if (property->type == PROP_OBJECT) {
		return GraphGetColor(ObjectFind(property->object, true));
	} else {
		return 0;
	}
}

//////////////////////////////////////////////////////////////

Rectangle8 ExportCalculatePaintOutsets(Object *object) {
	Rectangle8 paintOutsets = {};

	int32_t layerCount = PropertyReadInt32(object, "layers_count");
	if (layerCount < 0) layerCount = 0;
	if (layerCount > 100) layerCount = 100;

	for (int32_t i = 0; i < layerCount; i++) {
		char cPropertyName[PROPERTY_NAME_SIZE];
		sprintf(cPropertyName, "layers_%d_layer", i);
		Property *layerProperty = PropertyFind(object, cPropertyName, PROP_OBJECT);
		Object *layerObject = ObjectFind(layerProperty ? layerProperty->object : 0, true);
		if (!layerObject) continue;

#define LAYER_READ_INT32(x) sprintf(cPropertyName, "layers_%d_" #x, i); int8_t x = PropertyReadInt32(object, cPropertyName)
		LAYER_READ_INT32(offset0);
		LAYER_READ_INT32(offset1);
		LAYER_READ_INT32(offset2);
		LAYER_READ_INT32(offset3);
		LAYER_READ_INT32(position0);
		LAYER_READ_INT32(position1);
		LAYER_READ_INT32(position2);
		LAYER_READ_INT32(position3);
#undef LAYER_READ_INT32

		if (layerObject->type == OBJ_LAYER_BOX) {
			Object *object = layerObject;
			int depth = 0;

			while (object && (depth++ < 100)) {
				int32_t boxOffset0 = PropertyReadInt32(object, "offset0");
				int32_t boxOffset1 = PropertyReadInt32(object, "offset1");
				int32_t boxOffset2 = PropertyReadInt32(object, "offset2");
				int32_t boxOffset3 = PropertyReadInt32(object, "offset3");
				if (boxOffset0 < offset0) offset0 = boxOffset0;
				if (boxOffset1 > offset1) offset1 = boxOffset1;
				if (boxOffset2 < offset2) offset2 = boxOffset2;
				if (boxOffset3 > offset3) offset3 = boxOffset3;
				Property *property = PropertyFind(object, "_parent", PROP_OBJECT);
				object = ObjectFind(property ? property->object : 0, true);
			}
		}

		if (position0 ==   0 && -offset0 > paintOutsets.l) paintOutsets.l = -offset0;
		if (position1 == 100 &&  offset1 > paintOutsets.r) paintOutsets.r =  offset1;
		if (position2 ==   0 && -offset2 > paintOutsets.t) paintOutsets.t = -offset2;
		if (position3 == 100 &&  offset3 > paintOutsets.b) paintOutsets.b =  offset3;
	}

	return paintOutsets;
}

Rectangle8 ExportCalculateOpaqueInsets(Object *object) {
	return { 0x7F, 0x7F, 0x7F, 0x7F }; // TODO;
}

Rectangle8 ExportCalculateApproximateBorders(Object *object) {
	int32_t layerCount = PropertyReadInt32(object, "layers_count");
	if (layerCount < 0) layerCount = 0;
	if (layerCount > 100) layerCount = 100;

	for (int32_t i = 0; i < layerCount; i++) {
		char cPropertyName[PROPERTY_NAME_SIZE];
		sprintf(cPropertyName, "layers_%d_layer", i);
		Property *layerProperty = PropertyFind(object, cPropertyName, PROP_OBJECT);
		Object *layerObject = ObjectFind(layerProperty ? layerProperty->object : 0, true);
		if (!layerObject) continue;

#define LAYER_READ_INT32(x) sprintf(cPropertyName, "layers_%d_" #x, i); int8_t x = PropertyReadInt32(object, cPropertyName)
		LAYER_READ_INT32(position0);
		LAYER_READ_INT32(position1);
		LAYER_READ_INT32(position2);
		LAYER_READ_INT32(position3);
		LAYER_READ_INT32(mode);
#undef LAYER_READ_INT32

		if (layerObject->type == OBJ_LAYER_BOX && position0 == 0 && position1 == 100 && position2 == 0 && position3 == 100
				&& !PropertyReadInt32(layerObject, "shadowHiding") && !PropertyReadInt32(layerObject, "isBlurred")
				&& mode == THEME_LAYER_MODE_BACKGROUND && PropertyFind(layerObject, "borderPaint")) {
			return { 
				(int8_t) PropertyReadInt32(layerObject, "borders0"),
				(int8_t) PropertyReadInt32(layerObject, "borders1"),
				(int8_t) PropertyReadInt32(layerObject, "borders2"),
				(int8_t) PropertyReadInt32(layerObject, "borders3"),
			};
		}
	}

	return {};
}

//////////////////////////////////////////////////////////////

struct ExportContext {
	EsBuffer *baseData;
	EsBuffer *overrideData;
	uintptr_t overrideOffset;
};

ThemeVariant PropertyToThemeVariant(Property *property, uint8_t type) {
	ThemeVariant value = {};

	if (type == THEME_OVERRIDE_COLOR) {
		value.u32 = GraphGetColorFromProperty(property);
	} else if (type == THEME_OVERRIDE_I8) {
		value.i8 = GraphGetIntegerFromProperty(property);
	} else if (type == THEME_OVERRIDE_I16) {
		value.i16 = GraphGetIntegerFromProperty(property);
	} else if (type == THEME_OVERRIDE_F32) {
		value.f32 = property && property->type == PROP_FLOAT ? property->floating : 0;
	} else {
		assert(false);
	}

	return value;
}

ThemeVariant ExportValue(ExportContext *data, uintptr_t additionalOffset, Object *object, const char *cPropertyName, uint8_t type) {
	uintptr_t depth = 0;
	ThemeVariant value = {};

	if (data->overrideData) {
		Array<ThemeOverride> themeOverrides = {};

		while (object && (depth++ < 100)) {
			Property *property = PropertyFind(object, cPropertyName);

			if (property) {
				uint16_t state = PropertyReadInt32(object, "_primaryState") | PropertyReadInt32(object, "_stateBits");
				value = PropertyToThemeVariant(property, type);

				if (state) {
					ThemeOverride themeOverride = {};
					themeOverride.state = state;
					themeOverride.duration = GraphGetIntegerFromProperty(PropertyFind(object, "_duration"));
					themeOverride.offset = data->overrideOffset + (data->baseData ? data->baseData->position : 0) + additionalOffset;
					themeOverride.type = type;
					themeOverride.data = value;
					themeOverrides.Insert(themeOverride, 0);
				} else {
					break;
				}
			}

			// Go to the inheritance parent object.
			property = PropertyFind(object, "_parent", PROP_OBJECT);
			object = ObjectFind(property ? property->object : 0, true);
		}

		EsBufferWrite(data->overrideData, &themeOverrides[0], themeOverrides.Length() * sizeof(ThemeOverride));
		themeOverrides.Free();
	} else {
		value = PropertyToThemeVariant(PropertyFindOrInherit(object, cPropertyName), type);
	}

	return value;
}

#define ExportColor(data, structure, field, object, cPropertyName) \
	structure.field = ExportValue(data, ((uint8_t *) &structure.field - (uint8_t *) &structure), object, cPropertyName, THEME_OVERRIDE_COLOR).u32
#define ExportI8(data, structure, field, object, cPropertyName) \
	structure.field = ExportValue(data, ((uint8_t *) &structure.field - (uint8_t *) &structure), object, cPropertyName, THEME_OVERRIDE_I8).i8
#define ExportI16(data, structure, field, object, cPropertyName) \
	structure.field = ExportValue(data, ((uint8_t *) &structure.field - (uint8_t *) &structure), object, cPropertyName, THEME_OVERRIDE_I16).i16
#define ExportF32(data, structure, field, object, cPropertyName) \
	structure.field = ExportValue(data, ((uint8_t *) &structure.field - (uint8_t *) &structure), object, cPropertyName, THEME_OVERRIDE_F32).f32

void ExportWrite(ExportContext *context, const void *buffer, size_t bytes) {
	if (context->baseData) {
		EsBufferWrite(context->baseData, buffer, bytes);
	}
}

void ExportGradientStopArray(Object *object, ExportContext *data, size_t stopCount) {
	for (uintptr_t i = 0; i < stopCount; i++) {
		char cPropertyName[PROPERTY_NAME_SIZE];
		ThemeGradientStop stop = {};
		sprintf(cPropertyName, "stops_%d_color", (int32_t) i);
		ExportColor(data, stop, color, object, cPropertyName);
		sprintf(cPropertyName, "stops_%d_position", (int32_t) i);
		ExportI8(data, stop, position, object, cPropertyName);
		sprintf(cPropertyName, "stops_%d_wci", (int32_t) i);
		ExportI8(data, stop, windowColorIndex, object, cPropertyName);
		ExportWrite(data, &stop, sizeof(stop));
	}
}

int8_t ExportPaint(Object *parentObject, const char *cPropertyNameInParent, ExportContext *data, int depth = 0) {
	Object *object = PropertyFindOrInheritReadObject(parentObject, cPropertyNameInParent);

	if (!object || depth == 100) {
		return 0;
	}

	if (object->type == OBJ_VAR_COLOR || object->type == OBJ_MOD_COLOR) {
		if (data) {
			ThemePaintSolid solid = {};
			ExportColor(data, solid, color, parentObject, cPropertyNameInParent);
			ExportWrite(data, &solid, sizeof(solid));
		}

		return THEME_PAINT_SOLID;
	} else if (object->type == OBJ_PAINT_OVERWRITE) {
		ExportPaint(object, "color", data, depth + 1);
		return THEME_PAINT_OVERWRITE;
	} else if (object->type == OBJ_PAINT_LINEAR_GRADIENT) {
		if (data) {
			ThemePaintLinearGradient paint = {};
			ExportF32(data, paint, transform[0], object, "transformX");
			ExportF32(data, paint, transform[1], object, "transformY");
			ExportF32(data, paint, transform[2], object, "transformStart");
			paint.stopCount = PropertyFindOrInheritReadInt32(object, "stops_count");
			paint.useGammaInterpolation = !!PropertyFindOrInheritReadInt32(object, "useGammaInterpolation");
			paint.repeatMode = PropertyFindOrInheritReadInt32(object, "repeatMode");
			ExportWrite(data, &paint, sizeof(paint));
			ExportGradientStopArray(object, data, paint.stopCount);
		}

		return THEME_PAINT_LINEAR_GRADIENT;
	} else if (object->type == OBJ_PAINT_RADIAL_GRADIENT) {
		if (data) {
			ThemePaintRadialGradient paint = {};
			ExportF32(data, paint, transform[0], object, "transform0");
			ExportF32(data, paint, transform[1], object, "transform1");
			ExportF32(data, paint, transform[2], object, "transform2");
			ExportF32(data, paint, transform[3], object, "transform3");
			ExportF32(data, paint, transform[4], object, "transform4");
			ExportF32(data, paint, transform[5], object, "transform5");
			paint.stopCount = PropertyFindOrInheritReadInt32(object, "stops_count");
			paint.useGammaInterpolation = !!PropertyFindOrInheritReadInt32(object, "useGammaInterpolation");
			paint.repeatMode = PropertyFindOrInheritReadInt32(object, "repeatMode");
			ExportWrite(data, &paint, sizeof(paint));
			ExportGradientStopArray(object, data, paint.stopCount);
		}

		return THEME_PAINT_RADIAL_GRADIENT;
	} else {
		return 0;
	}
}

void ExportLayerBox(Object *object, ExportContext *data) {
	ThemeLayerBox box = {};
	box.mainPaintType = ExportPaint(object, "mainPaint", nullptr);
	box.borderPaintType = ExportPaint(object, "borderPaint", nullptr);
	ExportI8(data, box, borders.l, object, "borders0");
	ExportI8(data, box, borders.r, object, "borders1");
	ExportI8(data, box, borders.t, object, "borders2");
	ExportI8(data, box, borders.b, object, "borders3");
	ExportI8(data, box, corners.tl, object, "corners0");
	ExportI8(data, box, corners.tr, object, "corners1");
	ExportI8(data, box, corners.bl, object, "corners2");
	ExportI8(data, box, corners.br, object, "corners3");
	ExportI8(data, box, offset.l, object, "offset0");
	ExportI8(data, box, offset.r, object, "offset1");
	ExportI8(data, box, offset.t, object, "offset2");
	ExportI8(data, box, offset.b, object, "offset3");
	if (GraphGetIntegerFromProperty(PropertyFindOrInherit(object, "isBlurred"   ))) box.flags |= THEME_LAYER_BOX_IS_BLURRED;
	if (GraphGetIntegerFromProperty(PropertyFindOrInherit(object, "autoCorners" ))) box.flags |= THEME_LAYER_BOX_AUTO_CORNERS;
	if (GraphGetIntegerFromProperty(PropertyFindOrInherit(object, "autoBorders" ))) box.flags |= THEME_LAYER_BOX_AUTO_BORDERS;
	if (GraphGetIntegerFromProperty(PropertyFindOrInherit(object, "shadowHiding"))) box.flags |= THEME_LAYER_BOX_SHADOW_HIDING;
	ExportWrite(data, &box, sizeof(box));
	ExportPaint(object, "mainPaint", data);
	ExportPaint(object, "borderPaint", data);
}

void ExportLayerPath(Object *object, ExportContext *data) {
	Property *pointCount = PropertyFindOrInherit(object, "points_count", PROP_INT);
	Property *fillCount = PropertyFindOrInherit(object, "fills_count", PROP_INT);

	ThemeLayerPath path = {};
	if (GraphGetIntegerFromProperty(PropertyFindOrInherit(object, "pathFillEvenOdd"))) path.flags |= THEME_LAYER_PATH_FILL_EVEN_ODD;
	if (GraphGetIntegerFromProperty(PropertyFindOrInherit(object, "pathClosed"))) path.flags |= THEME_LAYER_PATH_CLOSED;
	ExportI16(data, path, alpha, object, "alpha");
	path.pointCount = pointCount ? pointCount->integer : 0;
	path.fillCount = fillCount ? fillCount->integer : 0;
	ExportWrite(data, &path, sizeof(path));

	for (uintptr_t i = 0; i < path.pointCount; i++) {
		char cPropertyName[PROPERTY_NAME_SIZE];
		ThemeVariant value;
#define LAYER_PATH_WRITE_POINT(x) \
		sprintf(cPropertyName, "points_%d_" #x, (int32_t) i); \
		value = ExportValue(data, 0, object, cPropertyName, THEME_OVERRIDE_F32); \
		ExportWrite(data, &value.f32, sizeof(float));
		LAYER_PATH_WRITE_POINT(x0);
		LAYER_PATH_WRITE_POINT(y0);
		LAYER_PATH_WRITE_POINT(x1);
		LAYER_PATH_WRITE_POINT(y1);
		LAYER_PATH_WRITE_POINT(x2);
		LAYER_PATH_WRITE_POINT(y2);
	}

	for (uintptr_t i = 0; i < path.fillCount; i++) {
		char cPropertyName[PROPERTY_NAME_SIZE];
		ThemeLayerPathFill fill = {};

		sprintf(cPropertyName, "fills_%d_paint", (int32_t) i);
		fill.paintAndFillType |= ExportPaint(object, cPropertyName, nullptr);

		sprintf(cPropertyName, "fills_%d_mode", (int32_t) i);
		Object *mode = PropertyFindOrInheritReadObject(object, cPropertyName);

		// TODO Dashed contours.

		if (mode && mode->type == OBJ_VAR_CONTOUR_STYLE) {
			fill.paintAndFillType |= THEME_PATH_FILL_CONTOUR;
		} else {
			fill.paintAndFillType |= THEME_PATH_FILL_SOLID;
		}

		ExportWrite(data, &fill, sizeof(fill));

		sprintf(cPropertyName, "fills_%d_paint", (int32_t) i);
		ExportPaint(object, cPropertyName, data);

		if (mode && mode->type == OBJ_VAR_CONTOUR_STYLE) {
			ThemeLayerPathFillContour contour = {};
			ExportF32(data, contour, miterLimit, mode, "miterLimit");
			ExportI8(data, contour, internalWidth, mode, "internalWidth");
			ExportI8(data, contour, externalWidth, mode, "externalWidth");
			contour.mode = PropertyFindOrInheritReadInt32(mode, "joinMode") 
				| (PropertyFindOrInheritReadInt32(mode, "capMode") << 2)
				| (PropertyFindOrInheritReadInt32(mode, "integerWidthsOnly") ? 0x80 : 0);
			ExportWrite(data, &contour, sizeof(contour));
		}
	}
}

void ExportPaintAsLayerBox(Object *object, ExportContext *data) {
	Object parentObject = {};
	Property property = {};
	property.type = PROP_OBJECT;
	property.cName[0] = '.';
	property.object = object->id;
	parentObject.properties.Add(property);
	ThemeLayerBox box = {};
	box.mainPaintType = ExportPaint(&parentObject, ".", nullptr);
	ExportWrite(data, &box, sizeof(box));
	ExportPaint(&parentObject, ".", data);
	parentObject.properties.Free();
}

//////////////////////////////////////////////////////////////

#define CANVAS_ALIGN (20)

UIRectangle CanvasGetObjectBounds(Object *object) {
	int32_t x = (PropertyReadInt32(object, "_graphX") - canvas->panX) * canvas->zoom + canvas->bounds.l;
	int32_t y = (PropertyReadInt32(object, "_graphY") - canvas->panY) * canvas->zoom + canvas->bounds.t;
	int32_t w = PropertyReadInt32(object, "_graphW") * canvas->zoom;
	int32_t h = PropertyReadInt32(object, "_graphH") * canvas->zoom;

	if (w < 1) w = 1;
	if (h < 1) h = 1;

	UIRectangle bounds = UI_RECT_4(x, x + w, y, y + h);

	if (object->flags & OBJECT_IS_SELECTED) {
		if (canvas->dragging) {
			UIRectangle offset = ScaleRectangle(UI_RECT_2(canvas->dragDeltaX, canvas->dragDeltaY), canvas->zoom);
			bounds = UIRectangleAdd(bounds, offset);
		}

		if (canvas->resizing) {
			bounds = UIRectangleAdd(bounds, canvas->resizeOffsets);
		}
	}

	return bounds;
}

void CanvasSelectObject(Object *object) {
	for (uintptr_t i = 0; i < objects.Length(); i++) {
		objects[i].flags &= ~OBJECT_IS_SELECTED;
	}

	if (canvas->showPrototype) CanvasSwitchView(nullptr);
	UIRectangle bounds = CanvasGetObjectBounds(object);
	canvas->panX += ((bounds.l + bounds.r) / 2 - UI_RECT_WIDTH(canvas->bounds) / 2) / canvas->zoom;
	canvas->panY += ((bounds.t + bounds.b) / 2 - UI_RECT_HEIGHT(canvas->bounds) / 2) / canvas->zoom;
	ObjectSetSelected(object->id);
	UIElementRefresh(canvas);
	InspectorPopulate();
}

void CanvasDrawArrow(UIPainter *painter, int x0, int y0, int x1, int y1, uint32_t color) {
	if (!UIDrawLine(painter, x0, y0, x1, y1, color)) return;
	float angle = atan2f(y1 - y0, x1 - x0);
	UIDrawLine(painter, x0, y0, x0 + cosf(angle + 0.5f) * 15, y0 + sinf(angle + 0.5f) * 15, color);
	UIDrawLine(painter, x0, y0, x0 + cosf(angle - 0.5f) * 15, y0 + sinf(angle - 0.5f) * 15, color);
}

void CanvasDrawLayerFromData(UIPainter *painter, UIRectangle bounds, EsBuffer data) {
	EsPaintTarget paintTarget = {};
	EsPainter themePainter = {};
	themePainter.target = &paintTarget;
	themePainter.clip.l = painter->clip.l;
	themePainter.clip.r = painter->clip.r;
	themePainter.clip.t = painter->clip.t;
	themePainter.clip.b = painter->clip.b;
	themePainter.target->bits = painter->bits;
	themePainter.target->width = painter->width;
	themePainter.target->height = painter->height;
	themePainter.target->stride = painter->width * 4;

	data.bytes = data.position;
	data.position = 0;

	ThemeDrawLayer(&themePainter, bounds, &data, canvas->zoom, UI_RECT_1(0));
}

void CanvasDrawColorSwatch(Object *object, UIRectangle bounds, UIPainter *painter) {
	for (int32_t y = bounds.t, y0 = 0; y < bounds.b; y += 15, y0++) {
		for (int32_t x = bounds.l, x0 = 0; x < bounds.r; x += 15, x0++) {
			UIDrawBlock(painter, UIRectangleIntersection(UI_RECT_4(x, x + 15, y, y + 15), bounds), ((x0 ^ y0) & 1) ? 0xFF808080 : 0xFFC0C0C0);
		}
	}

	uint8_t buffer[4096];
	EsBuffer data = { .out = buffer, .bytes = sizeof(buffer) };
	ExportContext context = { .baseData = &data };
	ThemeLayer layer = { .position = { .r = 100, .b = 100 }, .type = THEME_LAYER_BOX };
	ExportWrite(&context, &layer, sizeof(layer));
	ExportPaintAsLayerBox(object, &context);
	CanvasDrawLayerFromData(painter, bounds, data);
}

void CanvasDrawLayer(Object *object, UIRectangle bounds, UIPainter *painter, int depth = 0) {
	if (!object || depth == 100) {
		return;
	}

	if (object->type == OBJ_LAYER_BOX) {
		uint8_t buffer[4096];
		EsBuffer data = { .out = buffer, .bytes = sizeof(buffer) };
		ExportContext context = { .baseData = &data };
		ThemeLayer layer = { .position = { .r = 100, .b = 100 }, .type = THEME_LAYER_BOX };
		ExportWrite(&context, &layer, sizeof(layer));
		ExportLayerBox(object, &context);
		CanvasDrawLayerFromData(painter, bounds, data);
	} else if (object->type == OBJ_LAYER_TEXT) {
#ifdef OS_ESSENCE
		EsPaintTarget paintTarget = {};
		_EsPainter themePainter = {};
		themePainter.target = (_EsPaintTarget *) &paintTarget;
		themePainter.clip.l = painter->clip.l;
		themePainter.clip.r = painter->clip.r;
		themePainter.clip.t = painter->clip.t;
		themePainter.clip.b = painter->clip.b;
		paintTarget.bits = painter->bits;
		paintTarget.width = painter->width;
		paintTarget.height = painter->height;
		paintTarget.stride = painter->width * 4;
		EsTextStyle textStyle = {};
		textStyle.font.family = ES_FONT_SANS;
		textStyle.font.weight = 5;
		textStyle.size = 10;
		textStyle.color = GraphGetColorFromProperty(PropertyFindOrInherit(object, "color"));
		textStyle.blur = GraphGetIntegerFromProperty(PropertyFindOrInherit(object, "blur"));
		if (textStyle.blur > 10) textStyle.blur = 10;
		EsDrawTextSimple((_EsPainter *) &themePainter, ui.instance->window, bounds, "Sample", -1, textStyle, ES_TEXT_H_CENTER | ES_TEXT_V_CENTER); 
#endif
	} else if (object->type == OBJ_LAYER_PATH) {
		uint8_t buffer[4096];
		EsBuffer data = { .out = buffer, .bytes = sizeof(buffer) };
		ExportContext context = { .baseData = &data };
		ThemeLayer layer = { .position = { .r = 100, .b = 100 }, .type = THEME_LAYER_PATH };
		ExportWrite(&context, &layer, sizeof(layer));
		ExportLayerPath(object, &context);
		CanvasDrawLayerFromData(painter, bounds, data);
	} else if (object->type == OBJ_LAYER_GROUP) {
		int32_t layerCount = PropertyReadInt32(object, "layers_count");
		if (layerCount < 0) layerCount = 0;
		if (layerCount > 100) layerCount = 100;

		int32_t inWidth = UI_RECT_WIDTH(bounds);
		int32_t inHeight = UI_RECT_HEIGHT(bounds);

		for (int32_t i = 0; i < layerCount; i++) {
			char cPropertyName[PROPERTY_NAME_SIZE];
			sprintf(cPropertyName, "layers_%d_layer", i);
			Property *layerProperty = PropertyFind(object, cPropertyName, PROP_OBJECT);
			Object *layerObject = ObjectFind(layerProperty ? layerProperty->object : 0, true);

#define LAYER_READ_INT32(x) sprintf(cPropertyName, "layers_%d_" #x, i); int32_t x = PropertyReadInt32(object, cPropertyName)
			LAYER_READ_INT32(offset0);
			LAYER_READ_INT32(offset1);
			LAYER_READ_INT32(offset2);
			LAYER_READ_INT32(offset3);
			LAYER_READ_INT32(position0);
			LAYER_READ_INT32(position1);
			LAYER_READ_INT32(position2);
			LAYER_READ_INT32(position3);
#undef LAYER_READ_INT32

			UIRectangle outBounds;
			outBounds.l = bounds.l + offset0 * canvas->zoom + position0 * inWidth  / 100;
			outBounds.r = bounds.l + offset1 * canvas->zoom + position1 * inWidth  / 100;
			outBounds.t = bounds.t + offset2 * canvas->zoom + position2 * inHeight / 100;
			outBounds.b = bounds.t + offset3 * canvas->zoom + position3 * inHeight / 100;

			CanvasDrawLayer(layerObject, outBounds, painter, depth + 1);
		}
	}
}

void CanvasDrawStyle(Object *object, UIRectangle bounds, UIPainter *painter, int depth = 0) {
	if (!object || depth == 100) {
		return;
	}

#ifdef OS_ESSENCE
	EsPaintTarget paintTarget = {};
	_EsPainter themePainter = {};
	themePainter.target = (_EsPaintTarget *) &paintTarget;
	themePainter.clip.l = painter->clip.l;
	themePainter.clip.r = painter->clip.r;
	themePainter.clip.t = painter->clip.t;
	themePainter.clip.b = painter->clip.b;
	paintTarget.bits = painter->bits;
	paintTarget.width = painter->width;
	paintTarget.height = painter->height;
	paintTarget.stride = painter->width * 4;

	if (object->type == OBJ_VAR_TEXT_STYLE) {
		EsTextStyle textStyle = {};
		textStyle.font.family = GraphGetIntegerFromProperty(PropertyFindOrInherit(object, "fontFamily"));
		textStyle.font.weight = GraphGetIntegerFromProperty(PropertyFindOrInherit(object, "fontWeight"));
		textStyle.font.italic = GraphGetIntegerFromProperty(PropertyFindOrInherit(object, "isItalic"));
		textStyle.size = GraphGetIntegerFromProperty(PropertyFindOrInherit(object, "textSize")) * canvas->zoom;
		textStyle.color = GraphGetColorFromProperty(PropertyFindOrInherit(object, "textColor"));
		EsDrawTextSimple((_EsPainter *) &themePainter, ui.instance->window, bounds, "Sample", -1, textStyle, ES_TEXT_H_CENTER | ES_TEXT_V_CENTER); 
#if 0
		EsDrawStandardIcon((_EsPainter *) &themePainter, ES_ICON_GO_NEXT_SYMBOLIC, 
				GraphGetIntegerFromProperty(PropertyFindOrInherit(false, object, "iconSize")), bounds, 
				GraphGetColorFromProperty(PropertyFindOrInherit(false, object, "iconColor")));
#endif
	}
#else
	if (object->type == OBJ_VAR_TEXT_STYLE && depth == 0) {
		UIDrawString(painter, bounds, "TxtStyle", -1, 0xFF000000, UI_ALIGN_CENTER, nullptr);
	}
#endif

	if (object->type == OBJ_STYLE) {
		Property *appearance = PropertyFindOrInherit(object, "appearance");
		Property *textStyle = PropertyFindOrInherit(object, "textStyle");
		if (appearance) CanvasDrawLayer(ObjectFind(appearance->object, true), bounds, painter, depth + 1);
		if (textStyle) CanvasDrawStyle(ObjectFind(textStyle->object, true), bounds, painter, depth + 1);
	}
}

int ResizeHandleMessage(UIElement *element, UIMessage message, int di, void *dp) {
	uintptr_t side = (uintptr_t) element->cp;

	if (message == UI_MSG_PAINT) {
		UIDrawRectangle((UIPainter *) dp, element->bounds, 0xFFF8F8F8, 0xFF404040, UI_RECT_1(1));
	} else if (message == UI_MSG_LEFT_DOWN) {
		canvas->originalBounds = CanvasGetObjectBounds(ObjectFind(selectedObjectID, false));
	} else if (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 1) {
		if (side == 0) canvas->resizeOffsets = UI_RECT_4(element->window->cursorX - canvas->originalBounds.l, 0, 0, 0);
		if (side == 1) canvas->resizeOffsets = UI_RECT_4(0, element->window->cursorX - canvas->originalBounds.r, 0, 0);
		if (side == 2) canvas->resizeOffsets = UI_RECT_4(0, 0, element->window->cursorY - canvas->originalBounds.t, 0);
		if (side == 3) canvas->resizeOffsets = UI_RECT_4(0, 0, 0, element->window->cursorY - canvas->originalBounds.b);
		canvas->resizing = true;
		UIElementRefresh(canvas);
	} else if (message == UI_MSG_LEFT_UP) {
		Object *object = ObjectFind(selectedObjectID, false);
		int32_t x = PropertyReadInt32(object, "_graphX");
		int32_t y = PropertyReadInt32(object, "_graphY");
		int32_t w = PropertyReadInt32(object, "_graphW");
		int32_t h = PropertyReadInt32(object, "_graphH");
		UIRectangle oldBounds = UI_RECT_4(x, x + w, y, y + h);
		UIRectangle newBounds = UIRectangleAdd(ScaleRectangle(canvas->resizeOffsets, 1.0f / canvas->zoom), oldBounds);
		canvas->resizing = false;

		if (object) {
			Step step = {};
			step.type = STEP_MODIFY_PROPERTY;
			step.property.type = PROP_INT;
			step.objectID = selectedObjectID;

			strcpy(step.property.cName, "_graphX");
			step.property.integer = newBounds.l;
			DocumentApplyStep(step, STEP_APPLY_GROUPED);
			strcpy(step.property.cName, "_graphY");
			step.property.integer = newBounds.t;
			DocumentApplyStep(step, STEP_APPLY_GROUPED);
			strcpy(step.property.cName, "_graphW");
			step.property.integer = UI_RECT_WIDTH(newBounds);
			DocumentApplyStep(step, STEP_APPLY_GROUPED);
			strcpy(step.property.cName, "_graphH");
			step.property.integer = UI_RECT_HEIGHT(newBounds);
			DocumentApplyStep(step);
		}
	} else if (message == UI_MSG_GET_CURSOR) {
		if (side == 0) return UI_CURSOR_RESIZE_LEFT;
		if (side == 1) return UI_CURSOR_RESIZE_RIGHT;
		if (side == 2) return UI_CURSOR_RESIZE_UP;
		if (side == 3) return UI_CURSOR_RESIZE_DOWN;
	}

	return 0;
}

int CanvasMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIDrawBlock(painter, element->bounds, 0xFFC0C0C0);
		UIRectangle selectionBounds = UI_RECT_4(MinimumInteger(canvas->leftDownX, canvas->selectX), MaximumInteger(canvas->leftDownX, canvas->selectX),
				MinimumInteger(canvas->leftDownY, canvas->selectY), MaximumInteger(canvas->leftDownY, canvas->selectY));

		if (canvas->selecting) {
			UIDrawBlock(painter, selectionBounds, 0xFF99CCFF);
		}

		for (uintptr_t i = 0; i < objects.Length(); i++) {
			Object *object = &objects[i];
			if (!!(object->flags & OBJECT_IN_PROTOTYPE) != canvas->showPrototype) continue;
			UIRectangle bounds = CanvasGetObjectBounds(object);

			if (bounds.r < element->bounds.l || bounds.l > element->bounds.r
					|| bounds.b < element->bounds.t || bounds.t > element->bounds.b) {
				continue;
			}

			UIRectangle selectionIntersection = UIRectangleIntersection(bounds, selectionBounds);
			bool isSelected = (object->flags & OBJECT_IS_SELECTED) == (inspectorPickData == nullptr)
				|| (canvas->selecting && UI_RECT_VALID(selectionIntersection));
			bool isConditional = ObjectIsConditional(object);

			if (!canvas->showPrototype) {
				if (isSelected) {
					UIDrawBorder(painter, UIRectangleAdd(bounds, UI_RECT_1I(-3)), 0xFF4092FF, UI_RECT_1(3));
				} 

				if (object->type == OBJ_COMMENT || canvas->zoom > 0.1f) {
					UIDrawString(painter, UI_RECT_4(bounds.l, element->bounds.r, bounds.t - ui.glyphHeight, bounds.t), 
							object->cName, -1, 0xFF000000, UI_ALIGN_LEFT, nullptr);
				}

				UIDrawRectangle(painter, bounds, 0xFFE0E0E0, 0xFF404040, UI_RECT_1(1));
				UIDrawBlock(painter, UI_RECT_4(bounds.l + 1, bounds.r + 1, bounds.b, bounds.b + 1), 0xFF404040);
				UIDrawBlock(painter, UI_RECT_4(bounds.r, bounds.r + 1, bounds.t + 1, bounds.b + 1), 0xFF404040);

				if (isConditional && canvas->zoom > 0.1f) {
					UIRectangle indicator = UI_RECT_4(bounds.l - ui.glyphWidth, bounds.l, bounds.t, bounds.t + ui.glyphHeight);
					UIDrawBlock(painter, indicator, 0xFFFFFF00);
					UIDrawString(painter, indicator, "?", -1, 0xFF000000, UI_ALIGN_CENTER, nullptr);
				}

				bounds = UIRectangleAdd(bounds, UI_RECT_1I(5));
			}

			if (selectedObjectID == object->id && canvas->resizing) {
				char buffer[32];
				snprintf(buffer, sizeof(buffer), "%dx%d", UI_RECT_WIDTH(bounds), UI_RECT_HEIGHT(bounds));
				UIDrawString(painter, UI_RECT_4(bounds.l, bounds.r, bounds.t - ui.glyphHeight, bounds.t), 
						buffer, -1, 0xFF000000, UI_ALIGN_CENTER, nullptr);
			}

			if (isConditional) {
				canvas->previewStateActive = true;
				canvas->previewPrimaryState = PropertyReadInt32(object, "_primaryState");
				canvas->previewStateBits = PropertyReadInt32(object, "_stateBits");
			}

			if (object->type == OBJ_SELECTOR) {
				// Resolve the selector before painting.
				Object *link = ObjectFind(object->id, true);
				if (link) object = link;
			}

			if (object->type == OBJ_VAR_INT || object->type == OBJ_MOD_MULTIPLY) {
				int32_t value = GraphGetInteger(object);
				char buffer[32];
				snprintf(buffer, sizeof(buffer), "%d", value);
				UIDrawString(painter, bounds, buffer, -1, 0xFF000000, UI_ALIGN_CENTER, nullptr);
			} else if (object->type == OBJ_LAYER_BOX || object->type == OBJ_LAYER_GROUP 
					|| object->type == OBJ_LAYER_TEXT || object->type == OBJ_LAYER_PATH) {
				CanvasDrawLayer(object, bounds, painter);
			} else if (object->type == OBJ_PAINT_LINEAR_GRADIENT || object->type == OBJ_PAINT_RADIAL_GRADIENT) {
				CanvasDrawColorSwatch(object, bounds, painter);
			} else if (object->type == OBJ_VAR_COLOR || object->type == OBJ_MOD_COLOR) {
				CanvasDrawColorSwatch(object, bounds, painter);
				uint32_t color = GraphGetColor(object);
				bool isLight = EsColorIsLight(color);
				char buffer[32];
				snprintf(buffer, sizeof(buffer), "%.8X", color);
				UIRectangle area = UI_RECT_4(bounds.l, bounds.r, bounds.t, bounds.t + UIMeasureStringHeight());
				UIDrawString(painter, area, buffer, -1, isLight ? 0xFF000000 : 0xFFFFFFFF, UI_ALIGN_CENTER, nullptr);
			} else if (object->type == OBJ_VAR_TEXT_STYLE || object->type == OBJ_STYLE) {
				CanvasDrawStyle(object, bounds, painter);
			} else if (object->type == OBJ_INSTANCE) {
				Property *style = PropertyFind(object, "style", PROP_OBJECT);
				canvas->previewStateActive = object->id == selectedObjectID;
				Object *styleObject = ObjectFind(style ? style->object : 0, false);
				Rectangle8 paintOutsets = ExportCalculatePaintOutsets(PropertyFindOrInheritReadObject(styleObject, "appearance"));
				UIRectangle clip = bounds;
				clip.l -= ceilf(paintOutsets.l * canvas->zoom);
				clip.r += ceilf(paintOutsets.r * canvas->zoom);
				clip.t -= ceilf(paintOutsets.t * canvas->zoom);
				clip.b += ceilf(paintOutsets.b * canvas->zoom);
				UIPainter painter2 = *painter;
				painter2.clip = UIRectangleIntersection(painter2.clip, clip);
				CanvasDrawStyle(styleObject, bounds, &painter2);
			} else if (object->type == OBJ_LAYER_METRICS) {
				// TODO Visually show the preferred size, insets and gaps?
				UIDrawString(painter, bounds, "Metrics", -1, 0xFF000000, UI_ALIGN_CENTER, nullptr);
			} else {
				// TODO Preview for OBJ_VAR_CONTOUR_STYLE.
			}

			canvas->previewStateActive = false;

			if (!canvas->showPrototype) {
				canvas->previewPrimaryState = THEME_PRIMARY_STATE_IDLE;
				canvas->previewStateBits = 0;
			}
		}

		if (canvas->arrowMode && !canvas->showPrototype) {
			// Draw object connections.

			for (uintptr_t i = 0; i < objects.Length(); i++) {
				Object *object = &objects[i];
				if (!!(object->flags & OBJECT_IN_PROTOTYPE) != canvas->showPrototype) continue;
				UIRectangle b1 = CanvasGetObjectBounds(object);

				for (uintptr_t j = 0; j < object->properties.Length(); j++) {
					if (object->properties[j].type == PROP_OBJECT) {
						Object *target = ObjectFind(object->properties[j].object, false);
						if (!target) continue;

						bool sourceIsSelected = object->flags & OBJECT_IS_SELECTED;
						bool targetIsSelected = target->flags & OBJECT_IS_SELECTED;

						if (canvas->arrowMode == ARROW_MODE_ALL) {
						} else if (canvas->arrowMode == ARROW_MODE_FROM) {
							if (!sourceIsSelected) continue;
						} else if (canvas->arrowMode == ARROW_MODE_TO) {
							if (!targetIsSelected) continue;
						} else if (canvas->arrowMode == ARROW_MODE_FROM_OR_TO) {
							if (!targetIsSelected && !sourceIsSelected) continue;
						} else if (canvas->arrowMode == ARROW_MODE_FROM_AND_TO) {
							if (!targetIsSelected || !sourceIsSelected) continue;
						}

						UIRectangle b2 = CanvasGetObjectBounds(target);
						CanvasDrawArrow(painter, (b2.l + b2.r) / 2, (b2.t + b2.b) / 2, (b1.l + b1.r) / 2, (b1.t + b1.b) / 2, 0xFF000000);
					}
				}
			}
		}
	} else if (message == UI_MSG_LEFT_DOWN) {
		canvas->canDrag = true;
		bool foundObjectToSelect = false;

		for (uintptr_t i = objects.Length(); i > 0; i--) {
			Object *object = &objects[i - 1];
			if (!!(object->flags & OBJECT_IN_PROTOTYPE) != canvas->showPrototype) continue;
			UIRectangle bounds = CanvasGetObjectBounds(object);

			if (UIRectangleContains(bounds, element->window->cursorX, element->window->cursorY)) {
				if (inspectorPickData) {
					canvas->canDrag = false;

					Step step = {};
					step.type = STEP_MODIFY_PROPERTY;
					step.objectID = inspectorPickData->objectID;
					strcpy(step.property.cName, inspectorPickData->cPropertyName);
					step.property.type = PROP_OBJECT;
					step.property.object = object->id;
					InspectorBroadcastStep(step, inspectorPickData);
					DocumentApplyStep(step);
				} else {
					if (~object->flags & OBJECT_IS_SELECTED) {
						for (uintptr_t i = 0; i < objects.Length(); i++) {
							objects[i].flags &= ~OBJECT_IS_SELECTED;
						}
					}

					ObjectSetSelected(object->id, false /* do not clear selection flag from previous */);
					canvas->dragOffsetX = bounds.l - element->window->cursorX;
					canvas->dragOffsetY = bounds.t - element->window->cursorY;
				}

				foundObjectToSelect = true;
				break;
			}
		}

		if (!foundObjectToSelect) {
			ObjectSetSelected(0);

			for (uintptr_t i = 0; i < objects.Length(); i++) {
				objects[i].flags &= ~OBJECT_IS_SELECTED;
			}
		}

		canvas->leftDownX = element->window->cursorX;
		canvas->leftDownY = element->window->cursorY;

		UIElementRefresh(element);
		InspectorPopulate();
		InspectorPickTargetEnd();
	} else if (message == UI_MSG_LEFT_UP && canvas->dragging) {
		Object *object = ObjectFind(selectedObjectID, false);
		int32_t oldX = PropertyReadInt32(object, "_graphX");
		int32_t oldY = PropertyReadInt32(object, "_graphY");

		if ((canvas->dragDeltaX || canvas->dragDeltaY) && object) {
			Step step = {};
			step.type = STEP_MODIFY_PROPERTY;
			step.property.type = PROP_INT;

			for (uintptr_t i = 0; i < objects.Length(); i++) {
				Object *object = &objects[i];
				if (!!(object->flags & OBJECT_IN_PROTOTYPE) != canvas->showPrototype) continue;

				if ((object->flags & OBJECT_IS_SELECTED) && object->id != selectedObjectID) {
					step.objectID = object->id;
					strcpy(step.property.cName, "_graphX");
					step.property.integer = PropertyReadInt32(object, "_graphX") + canvas->dragDeltaX;
					DocumentApplyStep(step, STEP_APPLY_GROUPED);
					strcpy(step.property.cName, "_graphY");
					step.property.integer = PropertyReadInt32(object, "_graphY") + canvas->dragDeltaY;
					DocumentApplyStep(step, STEP_APPLY_GROUPED);
				}
			}

			step.objectID = selectedObjectID;
			strcpy(step.property.cName, "_graphX");
			step.property.integer = oldX + canvas->dragDeltaX;
			DocumentApplyStep(step, STEP_APPLY_GROUPED);
			strcpy(step.property.cName, "_graphY");
			step.property.integer = oldY + canvas->dragDeltaY;
			DocumentApplyStep(step);
		}

		canvas->dragging = false;
		UIElementRefresh(canvas);
	} else if (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 1 && selectedObjectID && canvas->canDrag) {
		int32_t dx = canvas->leftDownX - element->window->cursorX;
		int32_t dy = canvas->leftDownY - element->window->cursorY;

		if (canvas->dragging || dx * dx + dy * dy > 200) {
			int32_t canvasDragNewX = (element->window->cursorX + canvas->dragOffsetX - canvas->bounds.l) / canvas->zoom + canvas->panX;
			int32_t canvasDragNewY = (element->window->cursorY + canvas->dragOffsetY - canvas->bounds.t) / canvas->zoom + canvas->panY;
			if (!canvas->showPrototype) canvasDragNewX -= canvasDragNewX % CANVAS_ALIGN, canvasDragNewY -= canvasDragNewY % CANVAS_ALIGN;
			canvas->dragDeltaX = canvasDragNewX - PropertyReadInt32(ObjectFind(selectedObjectID, false), "_graphX");
			canvas->dragDeltaY = canvasDragNewY - PropertyReadInt32(ObjectFind(selectedObjectID, false), "_graphY");
			canvas->dragging = true;
			UIElementRefresh(canvas);
		}
	} else if (message == UI_MSG_LEFT_UP && canvas->selecting) {
		UIRectangle selectionBounds = UI_RECT_4(MinimumInteger(canvas->leftDownX, canvas->selectX), MaximumInteger(canvas->leftDownX, canvas->selectX),
				MinimumInteger(canvas->leftDownY, canvas->selectY), MaximumInteger(canvas->leftDownY, canvas->selectY));

		for (uintptr_t i = 0; i < objects.Length(); i++) {
			Object *object = &objects[i];
			if (!!(object->flags & OBJECT_IN_PROTOTYPE) != canvas->showPrototype) continue;

			UIRectangle bounds = CanvasGetObjectBounds(object);
			UIRectangle selectionIntersection = UIRectangleIntersection(bounds, selectionBounds);

			if (UI_RECT_VALID(selectionIntersection)) {
				object->flags |= OBJECT_IS_SELECTED;
			}
		}

		canvas->selecting = false;
		UIElementRefresh(canvas);
	} else if (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 1 && !selectedObjectID) {
		canvas->selectX = element->window->cursorX;
		canvas->selectY = element->window->cursorY;
		canvas->selecting = true;
		UIElementRefresh(canvas);
	} else if (message == UI_MSG_MIDDLE_DOWN) {
		canvas->lastPanPointX = element->window->cursorX;
		canvas->lastPanPointY = element->window->cursorY;
		_UIWindowSetCursor(element->window, UI_CURSOR_HAND);
	} else if (message == UI_MSG_MIDDLE_UP) {
		_UIWindowSetCursor(element->window, UI_CURSOR_ARROW);
	} else if (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 2) {
		canvas->panX -= (element->window->cursorX - canvas->lastPanPointX) / canvas->zoom;
		canvas->panY -= (element->window->cursorY - canvas->lastPanPointY) / canvas->zoom;
		canvas->lastPanPointX = element->window->cursorX;
		canvas->lastPanPointY = element->window->cursorY;
		UIElementRefresh(canvas);
	} else if (message == UI_MSG_MOUSE_WHEEL && !element->window->ctrl) {
		canvas->panY += di / canvas->zoom;
		UIElementRefresh(canvas);
	} else if (message == UI_MSG_MOUSE_WHEEL && element->window->ctrl) {
		int divisions = -di / 72;
		float factor = 1, perDivision = 1.2f;
		while (divisions > 0) factor *= perDivision, divisions--;
		while (divisions < 0) factor /= perDivision, divisions++;
		if (canvas->zoom * factor > 4) factor = 4 / canvas->zoom;
		if (canvas->zoom * factor < 0.05) factor = 0.05 / canvas->zoom;
		int mx = element->window->cursorX - element->bounds.l;
		int my = element->window->cursorY - element->bounds.t;
		canvas->zoom *= factor;
		canvas->panX -= mx / canvas->zoom * (1 - factor);
		canvas->panY -= my / canvas->zoom * (1 - factor);
		UIElementRefresh(canvas);
	} else if (message == UI_MSG_LAYOUT) {
		UIElement *controls = canvas->showPrototype ? &prototypeControls->e : &graphControls->e;
		(canvas->showPrototype ? &graphControls->e : &prototypeControls->e)->flags |= UI_ELEMENT_HIDE;
		controls->flags &= ~UI_ELEMENT_HIDE;
		int width = UIElementMessage(controls, UI_MSG_GET_WIDTH, 0, 0);
		int height = UIElementMessage(controls, UI_MSG_GET_HEIGHT, 0, 0);
		UIRectangle bounds = UI_RECT_4(element->bounds.l + 10, element->bounds.l + 10 + width, element->bounds.b - 10 - height, element->bounds.b - 10);
		UIElementMove(controls, bounds, false);

		Object *object = ObjectFind(selectedObjectID, false);
		bool showHandles = canvas->showPrototype && selectedObjectID && object && (object->flags & OBJECT_IN_PROTOTYPE);

		if (showHandles) {
			UIRectangle bounds = CanvasGetObjectBounds(object);
			int cx = (bounds.l + bounds.r) / 2, cy = (bounds.t + bounds.b) / 2;
			const int size = 3;
			UIElementMove(canvas->resizeHandles[0], UI_RECT_4(bounds.l - size, bounds.l + size + 1, cy - size, cy + size + 1), false);
			UIElementMove(canvas->resizeHandles[1], UI_RECT_4(bounds.r - size - 1, bounds.r + size, cy - size, cy + size + 1), false);
			UIElementMove(canvas->resizeHandles[2], UI_RECT_4(cx - size, cx + size + 1, bounds.t - size, bounds.t + size + 1), false);
			UIElementMove(canvas->resizeHandles[3], UI_RECT_4(cx - size, cx + size + 1, bounds.b - size - 1, bounds.b + size), false);
		}

		SetBit(&canvas->resizeHandles[0]->flags, UI_ELEMENT_HIDE, !showHandles);
		SetBit(&canvas->resizeHandles[1]->flags, UI_ELEMENT_HIDE, !showHandles);
		SetBit(&canvas->resizeHandles[2]->flags, UI_ELEMENT_HIDE, !showHandles);
		SetBit(&canvas->resizeHandles[3]->flags, UI_ELEMENT_HIDE, !showHandles);
	}

	return 0;
}

void CanvasArrowModeInternal(void *cp) {
	canvas->arrowMode = (uintptr_t) cp;
	UIElementRepaint(canvas, nullptr);
}

void CanvasArrowMode(void *) {
	UIMenu *menu = UIMenuCreate(window->pressed, UI_MENU_NO_SCROLL | UI_MENU_PLACE_ABOVE);
	UIMenuAddItem(menu, 0, "None", -1, CanvasArrowModeInternal, (void *) (uintptr_t) ARROW_MODE_NONE);
	UIMenuAddItem(menu, 0, "All", -1, CanvasArrowModeInternal, (void *) (uintptr_t) ARROW_MODE_ALL);
	UIMenuAddItem(menu, 0, "From selected", -1, CanvasArrowModeInternal, (void *) (uintptr_t) ARROW_MODE_FROM);
	UIMenuAddItem(menu, 0, "To selected", -1, CanvasArrowModeInternal, (void *) (uintptr_t) ARROW_MODE_TO);
	UIMenuAddItem(menu, 0, "From or to selected", -1, CanvasArrowModeInternal, (void *) (uintptr_t) ARROW_MODE_FROM_OR_TO);
	UIMenuAddItem(menu, 0, "From and to selected", -1, CanvasArrowModeInternal, (void *) (uintptr_t) ARROW_MODE_FROM_AND_TO);
	UIMenuShow(menu);
}

void CanvasSwitchView(void *) {
	float z = canvas->swapZoom, x = canvas->swapPanX, y = canvas->swapPanY;
	canvas->swapZoom = canvas->zoom, canvas->swapPanX = canvas->panX, canvas->swapPanY = canvas->panY;
	canvas->zoom = z, canvas->panX = x, canvas->panY = y;
	canvas->showPrototype = !canvas->showPrototype;
	UIElementRefresh(canvas);
}

void CanvasSwitchSelectorIndex(void *) {
	activeSelectorIndex = 1 - activeSelectorIndex;
	UIElementRefresh(canvas);
}

void CanvasZoom100(void *) {
	float factor = 1.0f / canvas->zoom;
	canvas->zoom *= factor;
	canvas->panX -= UI_RECT_WIDTH(canvas->bounds) / 2 / canvas->zoom * (1 - factor);
	canvas->panY -= UI_RECT_HEIGHT(canvas->bounds) / 2 / canvas->zoom * (1 - factor);
	UIElementRefresh(canvas);
}

//////////////////////////////////////////////////////////////

void ObjectChangeTypeInternal(void *cp) {
	Step step = {};
	step.type = STEP_CHANGE_OBJECT_TYPE;
	step.object.id = selectedObjectID;
	step.object.type = (ObjectType) (uintptr_t) cp;
	DocumentApplyStep(step);
}

void ObjectAddInternal(Object object) {
	Step step = {};
	step.type = STEP_ADD_OBJECT;
	step.object = object;
	DocumentApplyStep(step);

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		objects[i].flags &= ~OBJECT_IS_SELECTED;
	}

	ObjectSetSelected(object.id);
	InspectorPopulate();
	UIElementRefresh(canvas);
}

void ObjectAddCommandInternal(void *cp) {
	Object object = {};
	object.type = (ObjectType) (uintptr_t) cp;
	object.id = ++objectIDAllocator;
	Property p;
	int32_t x = canvas->panX + UI_RECT_WIDTH(canvas->bounds) / 2;
	int32_t y = canvas->panY + UI_RECT_HEIGHT(canvas->bounds) / 2;
	x -= x % CANVAS_ALIGN, y -= y % CANVAS_ALIGN;
	int32_t w = object.type == OBJ_COMMENT ? 30 : 80;
	int32_t h = object.type == OBJ_COMMENT ? 10 : 60;
	p = { .type = PROP_INT, .integer = x }; strcpy(p.cName, "_graphX"); object.properties.Add(p);
	p = { .type = PROP_INT, .integer = y }; strcpy(p.cName, "_graphY"); object.properties.Add(p);
	p = { .type = PROP_INT, .integer = w }; strcpy(p.cName, "_graphW"); object.properties.Add(p);
	p = { .type = PROP_INT, .integer = h }; strcpy(p.cName, "_graphH"); object.properties.Add(p);

	if (object.type == OBJ_STYLE) {
		// TODO Prevent taking IDs of style objects in the clipboard?

		uint8_t allocatedIDs[32768 / 8] = {};

		for (uintptr_t i = 0; i < objects.Length(); i++) {
			if (objects[i].type == OBJ_STYLE) {
				int32_t id = PropertyReadInt32(&objects[i], "headerID");

				if (id > 0 && id < 32768) {
					allocatedIDs[id / 8] |= 1 << (id % 8);
				}
			}
		}

		bool foundID = false;

		for (int32_t i = 1; i < 32768; i++) {
			if (!(allocatedIDs[i / 8] & (1 << (i % 8)))) {
				p = { .type = PROP_INT, .integer = i }; strcpy(p.cName, "headerID"); object.properties.Add(p);
				foundID = true;
				break;
			}
		}

		if (!foundID) {
			UIDialogShow(window, 0, "Error: No free header IDs.\n%f%b", "OK");
			object.properties.Free();
			return;
		}
	}

	ObjectAddInternal(object);
}

void ObjectAddInstanceCommandInternal(void *cp) {
	Object *style = (Object *) cp;
	Property *metricsProperty = PropertyFind(style, "metrics", PROP_OBJECT);
	Object *metrics = ObjectFind(metricsProperty ? metricsProperty->object : 0, true);
	int32_t preferredWidth = PropertyReadInt32(metrics, "preferredWidth");
	int32_t preferredHeight = PropertyReadInt32(metrics, "preferredHeight");
	Object object = {};
	object.type = OBJ_INSTANCE;
	object.id = ++objectIDAllocator;
	object.flags |= OBJECT_IN_PROTOTYPE;
	Property p;
	int32_t x = canvas->panX + UI_RECT_WIDTH(canvas->bounds) / 2;
	int32_t y = canvas->panY + UI_RECT_HEIGHT(canvas->bounds) / 2;
	int32_t w = preferredWidth ?: 100;
	int32_t h = preferredHeight ?: 100;
	p = { .type = PROP_INT, .integer = x }; strcpy(p.cName, "_graphX"); object.properties.Add(p);
	p = { .type = PROP_INT, .integer = y }; strcpy(p.cName, "_graphY"); object.properties.Add(p);
	p = { .type = PROP_INT, .integer = w }; strcpy(p.cName, "_graphW"); object.properties.Add(p);
	p = { .type = PROP_INT, .integer = h }; strcpy(p.cName, "_graphH"); object.properties.Add(p);
	p = { .type = PROP_OBJECT, .object = style->id }; strcpy(p.cName, "style"); object.properties.Add(p);
	ObjectAddInternal(object);
}

void ObjectAddCommand(void *cp) {
	void (*invoke)(void *) = (void (*)(void *)) cp;
	UIMenu *menu = UIMenuCreate(window->pressed, UI_MENU_NO_SCROLL | (invoke == ObjectChangeTypeInternal ? 0 : UI_MENU_PLACE_ABOVE));
	UIMenuAddItem(menu, 0, "Style", -1, invoke, (void *) (uintptr_t) OBJ_STYLE);
	UIMenuAddItem(menu, 0, "Comment", -1, invoke, (void *) (uintptr_t) OBJ_COMMENT);
	UIMenuAddItem(menu, 0, "Selector", -1, invoke, (void *) (uintptr_t) OBJ_SELECTOR);
	UIMenuAddItem(menu, 0, "Color variable", -1, invoke, (void *) (uintptr_t) OBJ_VAR_COLOR);
	UIMenuAddItem(menu, 0, "Integer variable", -1, invoke, (void *) (uintptr_t) OBJ_VAR_INT);
	UIMenuAddItem(menu, 0, "Text style", -1, invoke, (void *) (uintptr_t) OBJ_VAR_TEXT_STYLE);
	UIMenuAddItem(menu, 0, "Contour style", -1, invoke, (void *) (uintptr_t) OBJ_VAR_CONTOUR_STYLE);
	UIMenuAddItem(menu, 0, "Metrics", -1, invoke, (void *) (uintptr_t) OBJ_LAYER_METRICS);
	UIMenuAddItem(menu, 0, "Overwrite paint", -1, invoke, (void *) (uintptr_t) OBJ_PAINT_OVERWRITE);
	UIMenuAddItem(menu, 0, "Linear gradient", -1, invoke, (void *) (uintptr_t) OBJ_PAINT_LINEAR_GRADIENT);
	UIMenuAddItem(menu, 0, "Radial gradient", -1, invoke, (void *) (uintptr_t) OBJ_PAINT_RADIAL_GRADIENT);
	UIMenuAddItem(menu, 0, "Box layer", -1, invoke, (void *) (uintptr_t) OBJ_LAYER_BOX);
	UIMenuAddItem(menu, 0, "Path layer", -1, invoke, (void *) (uintptr_t) OBJ_LAYER_PATH);
	UIMenuAddItem(menu, 0, "Text layer", -1, invoke, (void *) (uintptr_t) OBJ_LAYER_TEXT);
	UIMenuAddItem(menu, 0, "Layer group", -1, invoke, (void *) (uintptr_t) OBJ_LAYER_GROUP);
	UIMenuAddItem(menu, 0, "Modify color", -1, invoke, (void *) (uintptr_t) OBJ_MOD_COLOR);
	UIMenuAddItem(menu, 0, "Modify integer", -1, invoke, (void *) (uintptr_t) OBJ_MOD_MULTIPLY);
	UIMenuShow(menu);
}

int ObjectCompareNames(const void *left, const void *right) {
	return strcmp(((*(Object **) left))->cName, ((*(Object **) right))->cName);
}

void ObjectAddInstanceCommand(void *) {
	Array<Object *> styles = {};

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		if (objects[i].type == OBJ_STYLE) {
			styles.Add(&objects[i]);
		}
	}

	qsort(styles.array, styles.Length(), sizeof(Object *), ObjectCompareNames);

	UIMenu *menu = UIMenuCreate(window->pressed, 0);

	for (uintptr_t i = 0; i < styles.Length(); i++) {
		Object *object = styles[i];
		UIMenuAddItem(menu, 0, object->cName, -1, ObjectAddInstanceCommandInternal, (void *) (uintptr_t) object);
	}

	UIMenuShow(menu);
	styles.Free();
}

void ObjectDeleteCommand(void *) {
	Array<uint64_t> list = {};

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		Object *object = &objects[i];
		if (!!(object->flags & OBJECT_IN_PROTOTYPE) != canvas->showPrototype) continue;

		if (object->flags & OBJECT_IS_SELECTED) {
			list.Add(object->id);
		}
	}

	Step step = {};
	step.type = STEP_DELETE_OBJECT;

	for (uintptr_t i = 0; i < list.Length(); i++) {
		step.objectID = list[i];
		DocumentApplyStep(step, i == list.Length() - 1 ? STEP_APPLY_NORMAL : STEP_APPLY_GROUPED);
	}

	list.Free();
}

void ObjectDuplicateCommand(void *) {
	if (!selectedObjectID) return;

	Object *source = ObjectFind(selectedObjectID, false);
	UI_ASSERT(source);

	Object object = {};
	object.type = source->type;
	object.id = ++objectIDAllocator;
	object.properties.InsertMany(0, source->properties.Length());
	memcpy(object.properties.array, source->properties.array, source->properties.Length() * sizeof(Property));

	Property *graphX = PropertyFind(&object, "_graphX", PROP_INT);
	if (graphX) graphX->integer += 60;
	
	Step step = {};
	step.type = STEP_ADD_OBJECT;
	step.object = object;
	DocumentApplyStep(step);
}

//////////////////////////////////////////////////////////////

#ifndef OS_ESSENCE
void Export() {
	DocumentLoad();

	// TODO Exporting modified integers and colors as constants.
	// TODO Recursively exporting nested groups.
	// TODO Handling styles that don't have metrics/textStyle.

	// Create the list of styles.

	FILE *output = fopen("desktop/styles.header", "wb");

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		Object *object = &objects[i];

		if (object->type == OBJ_STYLE) {
			bool isPublic = PropertyReadInt32(object, "isPublic");
			int32_t headerID = PropertyReadInt32(object, "headerID");

			if (!headerID) {
				continue;
			}

			fprintf(output, "%sdefine ES_STYLE_", !isPublic ? "private " : "");

			bool dot = false;

			for (uintptr_t j = 0; object->cName[j]; j++) {
				char c = object->cName[j];

				if (c == '.') {
					fprintf(output, "_");
					dot = true;
				} else if (c >= 'A' && c <= 'Z' && j && !dot) {
					fprintf(output, "_%c", c);
				} else if (c >= 'a' && c <= 'z') {
					fprintf(output, "%c", c - 'a' + 'A');
					dot = false;
				} else {
					fprintf(output, "%c", c);
					dot = false;
				}
			}

			fprintf(output, " (ES_STYLE_CAST(%d))\n", (headerID << 1) | 1);
		}
	}

	fclose(output);

	output = fopen("res/Theme.dat", "wb");

	// Write the header.

	ThemeHeader header = { 0 };
	header.signature = THEME_HEADER_SIGNATURE;

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		if (objects[i].type == OBJ_STYLE) {
			header.styleCount++;
		} else if ((objects[i].type == OBJ_VAR_COLOR || objects[i].type == OBJ_VAR_INT) && PropertyReadInt32(&objects[i], "isExported")) {
			header.constantCount++;
		}
	}

	fwrite(&header, 1, sizeof(header), output);

	// Write the list of styles.

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		Object *object = &objects[i];

		if (object->type != OBJ_STYLE) {
			continue;
		}
		
		int32_t headerID = PropertyReadInt32(object, "headerID");
		ThemeStyle entry = {};
		entry.id = (headerID << 1) | 1;
		entry.layerCount = 1;

		Object *appearance = PropertyFindOrInheritReadObject(object, "appearance");

		if (appearance && appearance->type == OBJ_LAYER_GROUP) {
			entry.layerCount += PropertyReadInt32(appearance, "layers_count");
			entry.paintOutsets = ExportCalculatePaintOutsets(appearance);
			entry.opaqueInsets = ExportCalculateOpaqueInsets(appearance);
			entry.approximateBorders = ExportCalculateApproximateBorders(appearance);
		}

		fwrite(&entry, 1, sizeof(entry), output);
	}

	// Write the list of constants.

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		Object *object = &objects[i];

		if ((object->type != OBJ_VAR_COLOR && object->type != OBJ_VAR_INT) || !PropertyReadInt32(object, "isExported")) {
			continue;
		}

		ThemeConstant constant = {};
		constant.scale = PropertyReadInt32(object, "isScaled");
		constant.hash = CalculateCRC64(object->cName, strlen(object->cName), 0);

		if (object->type == OBJ_VAR_COLOR) {
			snprintf(constant.cValue, sizeof(constant.cValue), "0x%.8X", (uint32_t) PropertyReadInt32(object, "color"));
		} else if (object->type == OBJ_VAR_INT) {
			snprintf(constant.cValue, sizeof(constant.cValue), "%d", (int32_t) PropertyReadInt32(object, "value"));
		}

		fwrite(&constant, 1, sizeof(constant), output);
	}

	// Write out all layers.

	Array<uint32_t> styleMetricsOffsets = {};

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		Object *object = &objects[i];

		if (object->type != OBJ_STYLE) {
			continue;
		}
		
		uint64_t styleObjectID = object->id;

		Object *metrics = PropertyFindOrInheritReadObject(object, "metrics");
		Object *textStyle = PropertyFindOrInheritReadObject(object, "textStyle");
		Object *appearance = PropertyFindOrInheritReadObject(object, "appearance");

		if (metrics && textStyle) {
			styleMetricsOffsets.Add(ftell(output));

			uint8_t overrideDataBuffer[4096];
			EsBuffer overrideData = { .out = overrideDataBuffer, .bytes = sizeof(overrideDataBuffer) };
			ExportContext context = { .overrideData = &overrideData, .overrideOffset = sizeof(ThemeLayer) };

			ThemeMetrics _metrics = {};

			_metrics.insets.l = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "insets0"));
			_metrics.insets.r = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "insets1"));
			_metrics.insets.t = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "insets2"));
			_metrics.insets.b = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "insets3"));
			_metrics.clipInsets.l = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "clipInsets0"));
			_metrics.clipInsets.r = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "clipInsets1"));
			_metrics.clipInsets.t = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "clipInsets2"));
			_metrics.clipInsets.b = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "clipInsets3"));
			_metrics.clipEnabled = PropertyFindOrInheritReadInt32(metrics, "clipEnabled");
			_metrics.cursor = PropertyFindOrInheritReadInt32(metrics, "cursor");
			_metrics.preferredWidth = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "preferredWidth"));
			_metrics.preferredHeight = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "preferredHeight"));
			_metrics.minimumWidth = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "minimumWidth"));
			_metrics.minimumHeight = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "minimumHeight"));
			_metrics.maximumWidth = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "maximumWidth"));
			_metrics.maximumHeight = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "maximumHeight"));
			_metrics.gapMajor = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "gapMajor"));
			_metrics.gapMinor = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "gapMinor"));
			_metrics.gapWrap = GraphGetIntegerFromProperty(PropertyFindOrInherit(metrics, "gapWrap"));

			int32_t horizontalTextAlign = PropertyFindOrInheritReadInt32(metrics, "horizontalTextAlign");
			int32_t verticalTextAlign = PropertyFindOrInheritReadInt32(metrics, "verticalTextAlign");
			int32_t wrapText = PropertyFindOrInheritReadInt32(metrics, "wrapText");
			int32_t ellipsis = PropertyFindOrInheritReadInt32(metrics, "ellipsis");
			_metrics.textAlign = (wrapText ? ES_TEXT_WRAP : 0) | (ellipsis ? ES_TEXT_ELLIPSIS : 0)
				| (horizontalTextAlign == 1 ? ES_TEXT_H_LEFT : horizontalTextAlign == 3 ? ES_TEXT_H_RIGHT : ES_TEXT_H_CENTER)
				| (verticalTextAlign == 1 ? ES_TEXT_V_TOP : verticalTextAlign == 3 ? ES_TEXT_V_BOTTOM : ES_TEXT_V_CENTER);

			_metrics.fontFamily = PropertyFindOrInheritReadInt32(textStyle, "fontFamily");
			_metrics.isItalic = PropertyFindOrInheritReadInt32(textStyle, "isItalic");
			ExportI8(&context, _metrics, fontWeight, textStyle, "fontWeight");
			ExportI16(&context, _metrics, textSize, textStyle, "textSize");
			ExportI16(&context, _metrics, iconSize, textStyle, "iconSize");
			ExportColor(&context, _metrics, textColor, textStyle, "textColor");
			ExportColor(&context, _metrics, selectedBackground, textStyle, "selectedBackground");
			ExportColor(&context, _metrics, selectedText, textStyle, "selectedText");
			ExportColor(&context, _metrics, iconColor, textStyle, "iconColor");

			ThemeLayer layer = {};
			layer.type = THEME_LAYER_METRICS;
			layer.dataByteCount = sizeof(ThemeLayer) + sizeof(ThemeMetrics);
			layer.overrideCount = overrideData.position / sizeof(ThemeOverride);
			layer.overrideListOffset = layer.dataByteCount + ftell(output);
			fwrite(&layer, 1, sizeof(layer), output);

#if 0
			for (uintptr_t i = 0; i < objects.Length(); i++) {
				if (objects[i].type == OBJ_STYLE && PropertyFindOrInheritReadObject(&objects[i], "textStyle") == textStyle) {
					fprintf(stderr, "%s %d %d\n", objects[i].cName, layer.overrideCount, layer.overrideListOffset);
				}
			}
#endif

			fwrite(&_metrics, 1, sizeof(_metrics), output);
			fwrite(overrideData.out, 1, overrideData.position, output);
			assert(!overrideData.error);
		} else {
			assert(false); // TODO.
		}

		if (appearance && appearance->type == OBJ_LAYER_GROUP) {
			int32_t layerCount = PropertyReadInt32(appearance, "layers_count");
			if (layerCount < 0) layerCount = 0;
			if (layerCount > 100) layerCount = 100;

			for (int32_t i = 0; i < layerCount; i++) {
				char cPropertyName[PROPERTY_NAME_SIZE];
				sprintf(cPropertyName, "layers_%d_layer", i);
				Property *layerProperty = PropertyFind(appearance, cPropertyName, PROP_OBJECT);
				Object *layerObject = ObjectFind(layerProperty ? layerProperty->object : 0, true);
				if (!layerObject) continue;

				ExportOffset exportOffset = {};
				exportOffset.styleObjectID = styleObjectID;
				exportOffset.objectID = layerObject->id;
				exportOffset.offset = ftell(output);
				exportOffsets.Add(exportOffset);

#define LAYER_READ_INT8(x) sprintf(cPropertyName, "layers_%d_" #x, i); int8_t x = PropertyReadInt32(appearance, cPropertyName)
				LAYER_READ_INT8(offset0);
				LAYER_READ_INT8(offset1);
				LAYER_READ_INT8(offset2);
				LAYER_READ_INT8(offset3);
				LAYER_READ_INT8(position0);
				LAYER_READ_INT8(position1);
				LAYER_READ_INT8(position2);
				LAYER_READ_INT8(position3);
				LAYER_READ_INT8(mode);
#undef LAYER_READ_INT32

				uint8_t baseDataBuffer[4096];
				uint8_t overrideDataBuffer[4096];
				EsBuffer baseData = { .out = baseDataBuffer, .bytes = sizeof(baseDataBuffer) };
				EsBuffer overrideData = { .out = overrideDataBuffer, .bytes = sizeof(overrideDataBuffer) };
				ExportContext context = { .baseData = &baseData, .overrideData = &overrideData, .overrideOffset = sizeof(ThemeLayer) };
				ThemeLayer layer = {};
				layer.position = { position0, position1, position2, position3 };
				layer.offset = { offset0, offset1, offset2, offset3 };

				if (layerObject->type == OBJ_LAYER_PATH) {
					layer.type = THEME_LAYER_PATH;
					ExportLayerPath(layerObject, &context);
				} else if (layerObject->type == OBJ_LAYER_BOX) {
					layer.type = THEME_LAYER_BOX;
					ExportLayerBox(layerObject, &context);
				} else if (layerObject->type == OBJ_LAYER_TEXT) {
					layer.type = THEME_LAYER_TEXT;
					ThemeLayerText text = {};
					ExportI8(&context, text, blur, layerObject, "blur");
					ExportColor(&context, text, color, layerObject, "color");
					ExportWrite(&context, &text, sizeof(text));
				} else {
					assert(false);
				}

				layer.dataByteCount = baseData.position + sizeof(layer);
				layer.overrideCount = overrideData.position / sizeof(ThemeOverride);
				layer.overrideListOffset = layer.dataByteCount + ftell(output);
				layer.mode = mode;
				fwrite(&layer, 1, sizeof(layer), output);
				fwrite(baseData.out, 1, baseData.position, output);
				fwrite(overrideData.out, 1, overrideData.position, output);
				assert(!baseData.error);
				assert(!overrideData.error);
			}
		}
	}

	// Write out layer lists for styles.

	uintptr_t styleIndex = 0;
	
	for (uintptr_t i = 0; i < objects.Length(); i++) {
		Object *object = &objects[i];

		if (object->type != OBJ_STYLE) {
			continue;
		}

		uint64_t styleObjectID = object->id;

		ExportOffset exportOffset = {};
		exportOffset.objectID = object->id;
		exportOffset.offset = ftell(output);
		exportOffsets.Add(exportOffset);

		uint32_t metricsExportOffset = styleMetricsOffsets[styleIndex++];
		fwrite(&metricsExportOffset, 1, sizeof(metricsExportOffset), output);

		Object *appearance = PropertyFindOrInheritReadObject(object, "appearance");

		if (appearance && appearance->type == OBJ_LAYER_GROUP) {
			int32_t layerCount = PropertyReadInt32(appearance, "layers_count");
			if (layerCount < 0) layerCount = 0;
			if (layerCount > 100) layerCount = 100;

			for (int32_t i = 0; i < layerCount; i++) {
				char cPropertyName[PROPERTY_NAME_SIZE];
				sprintf(cPropertyName, "layers_%d_layer", i);
				Property *layerProperty = PropertyFind(appearance, cPropertyName, PROP_OBJECT);
				Object *layerObject = ObjectFind(layerProperty ? layerProperty->object : 0, true);
				if (!layerObject) continue;
				uint32_t exportOffset = ExportOffsetFindObjectForStyle(layerObject->id, styleObjectID)->offset;
				fwrite(&exportOffset, 1, sizeof(exportOffset), output);
			}
		}
	}

	// Update the style list to point to the layer lists.

	uintptr_t writeOffset = sizeof(ThemeHeader);

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		Object *object = &objects[i];
		if (object->type != OBJ_STYLE) continue;
		uint32_t exportOffset = ExportOffsetFindObject(object->id)->offset;
		fseek(output, writeOffset, SEEK_SET);
		fwrite(&exportOffset, 1, sizeof(exportOffset), output);
		writeOffset += sizeof(ThemeStyle);
	}

	DocumentFree();
	fclose(output);
	exportOffsets.Free();
	styleMetricsOffsets.Free();
}

void ExportJSON() {
	DocumentLoad();
	FILE *f = fopen("bin/designer2.json", "wb");
	fprintf(f, "{\n\t\"version\": 1,\n\t\"objectIDAllocator\": %ld,\n\t\"objects\": [\n", objectIDAllocator);

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		Object *object = &objects[i];
		const char *type = "??";

		for (uintptr_t i = 0; i < sizeof(cObjectTypeStrings) / sizeof(cObjectTypeStrings[0]); i++) {
			if (cObjectTypeStrings[i].type == object->type) {
				type = cObjectTypeStrings[i].string;
				break;
			}
		}

		fprintf(f, "\t\t{\n\t\t\t\"ID\": %ld,\n\t\t\t\"Flags\": %d,\n\t\t\t\"Type\": \"%s\",\n\t\t\t\"Name\": \"%s\",\n", 
				object->id, object->flags, type, object->cName);

		for (uintptr_t i = 0; i < object->properties.Length(); i++) {
			Property *property = &object->properties[i];
			fprintf(f, "\t\t\t\"%s\": ", property->cName);
			if (property->type == PROP_COLOR) fprintf(f, "\"#%.8X\"", (uint32_t) property->integer);
			if (property->type == PROP_INT) fprintf(f, "%d", property->integer);
			if (property->type == PROP_OBJECT) fprintf(f, "\"@%ld\"", property->object);
			if (property->type == PROP_FLOAT) fprintf(f, "%f", property->floating);
			fprintf(f, "%s\n", i < object->properties.Length() - 1 ? "," : "");
		}

		fprintf(f, "\t\t}%s\n", i < objects.Length() - 1 ? "," : "");
	}

	fprintf(f, "\t]\n}\n");
	fclose(f);
	DocumentFree();
}
#endif

//////////////////////////////////////////////////////////////

int WindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_WINDOW_CLOSE) {
#ifndef OS_ESSENCE
		if (documentModified && !window->dialog) {
			const char *dialog = "Document modified. Save changes?\n%f%b%b";
			const char *result = UIDialogShow(window, 0, dialog, "Save", "Discard");

			if (0 == strcmp(result, "Save")) {
				DocumentSave(nullptr);
			}
		}
#endif
	}

	return 0;
}

#ifdef OS_ESSENCE
void DocumentFileMenu(void *) {
	EsFileMenuCreate(ui.instance, ui.instance->window, ES_MENU_AT_CURSOR);
}
#endif

int main(int argc, char **argv) {
#ifndef OS_ESSENCE
	if (argc == 2) {
		if (0 == strcmp(argv[1], "export")) {
			Export();
		} else if (0 == strcmp(argv[1], "json")) {
			ExportJSON();
		} else {
			fprintf(stderr, "Error: Unknown action '%s'.\n", argv[1]);
			return 1;
		}

		return 0;
	}
#endif

	UIInitialise();
	ui.theme = _uiThemeClassic;
	window = UIWindowCreate(0, UI_ELEMENT_PARENT_PUSH | UI_WINDOW_MAXIMIZE, "Designer", 0, 0);
	window->e.messageUser = WindowMessage;

	UISplitPaneCreate(0, UI_ELEMENT_PARENT_PUSH, 0.75f);
	canvas = (Canvas *) UIElementCreate(sizeof(Canvas), 0, 0, CanvasMessage, "Canvas");
	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_EXPAND);
	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL | UI_PANEL_GRAY | UI_PANEL_MEDIUM_SPACING);
#ifdef OS_ESSENCE
		UIButtonCreate(0, UI_BUTTON_DROP_DOWN, "File", -1)->invoke = DocumentFileMenu;
#else
		UIButtonCreate(0, 0, "Save", -1)->invoke = DocumentSave;
#endif
		UIButtonCreate(0, 0, "Switch view", -1)->invoke = CanvasSwitchView;
		UIButtonCreate(0, 0, "Switch selector index", -1)->invoke = CanvasSwitchSelectorIndex;
	UIParentPop();
	UISpacerCreate(0, UI_SPACER_LINE, 0, 1);
	inspector = &UIPanelCreate(0, UI_ELEMENT_V_FILL | UI_PANEL_GRAY | UI_PANEL_MEDIUM_SPACING | UI_PANEL_SCROLL | UI_PANEL_EXPAND)->e;

	InspectorPopulate();

	graphControls = UIPanelCreate(canvas, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
	graphControls->gap = -1;
		UIButton *button = UIButtonCreate(0, UI_BUTTON_SMALL, "Add object \x18", -1);
		button->invoke = ObjectAddCommand;
		button->e.cp = (void *) ObjectAddCommandInternal;
		UIButtonCreate(0, UI_BUTTON_SMALL, "Arrow mode \x18", -1)->invoke = CanvasArrowMode;
	UIParentPop();

	prototypeControls = UIPanelCreate(canvas, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH);
	prototypeControls->gap = -1;
		UIButtonCreate(0, UI_BUTTON_SMALL, "Add instance \x18", -1)->invoke = ObjectAddInstanceCommand;
	UIParentPop();

	for (uintptr_t i = 0; i < 4; i++) {
		canvas->resizeHandles[i] = UIElementCreate(sizeof(UIElement), canvas, 0, ResizeHandleMessage, "Resize handle");
		canvas->resizeHandles[i]->cp = (void *) (uintptr_t) i;
	}

	canvas->zoom = canvas->swapZoom = 1.0f;
	canvas->previewPrimaryState = THEME_PRIMARY_STATE_IDLE;
	canvas->arrowMode = ARROW_MODE_FROM_OR_TO;

	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_LETTER('Z'), 1 /* ctrl */, 0, 0, DocumentUndoStep, 0));
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_LETTER('Y'), 1 /* ctrl */, 0, 0, DocumentRedoStep, 0));
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_LETTER('D'), 1 /* ctrl */, 0, 0, ObjectDuplicateCommand, 0));
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_FKEY(2), 0, 0, 0, CanvasSwitchView, 0));
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_FKEY(3), 0, 0, 0, CanvasSwitchSelectorIndex, 0));
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_DIGIT('1'), 1 /* ctrl */, 0, 0, CanvasZoom100, 0));
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_DELETE, 0, 0, 0, ObjectDeleteCommand, 0));

#ifdef OS_ESSENCE
	EsWindowSetIcon(ui.instance->window, ES_ICON_APPLICATIONS_INTERFACEDESIGN);
	EsInstanceSetClassEditor(ui.instance, &instanceClassEditorSettings);
#else
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_LETTER('S'), 1 /* ctrl */, 0, 0, DocumentSave, 0));
	DocumentLoad();
#endif

	int result = UIMessageLoop();
	DocumentFree();
	UIElementDestroy(&window->e);
	_UIUpdate();
	inspectorBoundElements.Free();
	return result;
}

#ifdef OS_ESSENCE
void _UIMessageProcess(EsMessage *message) {
	if (message->type == ES_MSG_INSTANCE_OPEN) {
		DocumentFree();
		fileStore = message->instanceOpen.file;
		DocumentLoad();
		fileStore = nullptr;
		EsInstanceOpenComplete(message, true);
	} else if (message->type == ES_MSG_INSTANCE_SAVE) {
		fileStore = message->instanceSave.file;
		DocumentSave(nullptr);
		fileStore = nullptr;
		EsInstanceSaveComplete(message, true);
	}
}

void _start() {
	_init();
	main(0, nullptr);
}
#endif
