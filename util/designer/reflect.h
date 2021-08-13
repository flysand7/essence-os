// TODO Bitsets.
// TODO Versioning support for unions and enums.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef RF_ASSERT
#include <assert.h>
#define RF_ASSERT assert
#endif

#ifndef RF_MEMZERO
#include <string.h>
#define RF_MEMZERO(pointer, byteCount) memset((pointer), 0, (byteCount))
#endif

#ifndef RF_MEMCPY
#include <string.h>
#define RF_MEMCPY(destination, source, byteCount) memcpy((destination), (source), (byteCount))
#endif

#ifndef RF_REALLOC
#include <stdlib.h>
#define RF_REALLOC(previous, byteCount) realloc((previous), (byteCount))
#endif

#define RF_SIZE_OF(containerType, field) sizeof(((containerType *) NULL)->field)
#define RF_FIELD(containerType, field, fieldRfType, ...) \
	{ \
		.item.type = &fieldRfType, \
		.item.byteCount = RF_SIZE_OF(containerType, field), \
		.cName = #field, \
		.offset = offsetof(containerType, field), \
		__VA_ARGS__ \
	}

#define RF_OP_SAVE    (-1) // Set access in RfState.
#define RF_OP_LOAD    (-2) // Set access and allocate in RfState.
#define RF_OP_FREE    (-3) // Set allocate in RfState.
#define RF_OP_ITERATE (-4) // Pass RfIterator; set index.
#define RF_OP_COUNT   (-5) // Pass RfIterator; result saved in index.
// User-defined operations use positive integers.

typedef struct RfState {
	bool error;
	int16_t op;
	uint32_t version, flags;
	void *(*allocate)(struct RfState *state, void *previous, size_t byteCount);
	void (*access)(struct RfState *state, void *buffer, size_t byteCount);
} RfState;

typedef struct RfItem {
	struct RfType *type;
	size_t byteCount;
	void *options;
} RfItem;

typedef struct RfField {
	RfItem item;
	const char *cName;
	ptrdiff_t offset;
	uint32_t firstVersion, lastVersion;
	uint32_t flagsInclude, flagsExclude;
} RfField;

typedef struct RfType {
	void (*op)(RfState *state, RfItem *field, void *pointer);
	const char *cName;
	size_t fieldCount;
	RfField *fields; 
} RfType;

typedef struct RfIterator {
	RfState s;
	void *pointer;
	RfItem item;
	uint32_t index;
	bool includeRemovedFields;
	bool isRemoved;
} RfIterator;

typedef struct RfPath {
#define RF_PATH_TERMINATOR (0xFFFFFFFF)
	uint32_t indices[1]; 
} RfPath;

typedef struct RfUnionHeader {
	uint32_t tag;
} RfUnionHeader;

typedef struct RfArrayHeader {
	size_t length, capacity;
	
	// For compatability with stb_ds.h.
	void *hashTable;
	ptrdiff_t temporary;
} RfArrayHeader;

typedef struct RfData {
	void *buffer;
	size_t byteCount;
} RfData;

typedef struct RfGrowableBuffer {
	RfState s;
	RfData data;

	// When writing - allocated space in data.
	// When reading - position in data.
	size_t position; 
} RfGrowableBuffer;

extern RfType rfI8, rfI16, rfI32, rfI64,
	rfU8, rfU16, rfU32, rfU64,
	rfF32, rfF64,
	rfChar, rfBool, 
	rfData, rfNone,
	rfObject /* options - RfItem */, 
	rfArray /* options - RfItem */;

bool RfPathResolve(RfPath *path, RfItem *item, void **pointer); // Returns true if successful.
void RfBroadcast(RfState *state, RfItem *item, void *pointer, bool recurse);
void RfUnionSelect(RfState *state, RfItem *item, RfUnionHeader *header, uint32_t tag);

void RfReadGrowableBuffer(RfState /* RfGrowableBuffer */ *state, void *buffer, size_t byteCount);
void RfWriteGrowableBuffer(RfState /* RfGrowableBuffer */ *state, void *buffer, size_t byteCount);
void *RfRealloc(RfState *state, void *previous, size_t byteCount);

void RfStructOp(RfState *state, RfItem *item, void *pointer);
void RfUnionOp(RfState *state, RfItem *item, void *pointer);
void RfEnumOp(RfState *state, RfItem *item, void *pointer);
void RfBitSetOp(RfState *state, RfItem *item, void *pointer);
void RfEndianOp(RfState *state, RfItem *item, void *pointer);
void RfIntegerOp(RfState *state, RfItem *item, void *pointer);
void RfNoneOp(RfState *state, RfItem *item, void *pointer);

#ifdef REFLECT_IMPLEMENTATION

void RfIntegerSave(RfState *state, void *pointer, size_t inByteCount) {
	uint8_t in[16];
	RF_ASSERT(inByteCount < 16);
	RF_MEMCPY(in, pointer, inByteCount);
	
	bool negative = in[inByteCount - 1] & 0x80;
	size_t inBitCount = 1;
	
	for (int i = inByteCount - 1; i >= 0; i--) {
		for (int j = 7; j >= 0; j--) {
			if (((in[i] >> j) & 1) != negative) {
				inBitCount = i * 8 + j + 2;
				goto gotBitCount;
			}
		}
	}
	
	gotBitCount:;
	
	size_t outByteCount = (inBitCount + 6) / 7;
	uint8_t out[16];	
	RF_ASSERT(outByteCount < 16);

	for (uintptr_t i = 0; i < outByteCount; i++) {
		uint8_t b = 0;
		
		for (uintptr_t j = 0; j < 7; j++) {
			uintptr_t bitIndex = i * 7 + j;
			bool inBit = negative;
			if (bitIndex < 8 * inByteCount) inBit = in[bitIndex >> 3] & (1 << (bitIndex & 7));
			if (inBit) b |= 1 << j;
		}
		
		out[i] = b;
	}
	
	out[outByteCount - 1] |= 0x80;
	state->access(state, out, outByteCount);
}

void RfIntegerLoad(RfState *state, void *pointer, size_t byteCount) {
	uint8_t out[16];
	uintptr_t outIndex = 0;
	RF_ASSERT(byteCount < 16);
	RF_MEMZERO(out, byteCount);
	
	while (!state->error) {
		uint8_t b;
		state->access(state, &b, 1); 
		
		for (uintptr_t i = 0; i < 7; i++) {
			if (outIndex == byteCount * 8) break;
			if (b & (1 << i)) out[outIndex >> 3] |= 1 << (outIndex & 7);
			outIndex++;
		}
		
		if (b & 0x80) {
			if (b & 0x40) {
				for (uintptr_t i = outIndex; i < byteCount * 8; i++) {
					out[i >> 3] |= 1 << (i & 7);
				}
			}
			
			break;
		}
	}
	
	if (!state->error && pointer) {
		RF_MEMCPY(pointer, out, byteCount);
	}
}

void RfStructOp(RfState *state, RfItem *item, void *pointer) {
	RfType *type = item->type;

	if (state->op == RF_OP_SAVE || state->op == RF_OP_LOAD || state->op == RF_OP_FREE) {
		for (uintptr_t i = 0; i < type->fieldCount && !state->error; i++) {
			RfField *field = type->fields + i;

			if (state->flags & field->flagsExclude) continue;
			if ((state->flags & field->flagsInclude) != field->flagsInclude) continue;

			void *fieldPointer = pointer ? ((uint8_t *) pointer + field->offset) : NULL;

			if (state->op == RF_OP_LOAD) {
				if (state->version < field->firstVersion) {
					// The field exists, but we're loading from a version where it did not exist.
					// Ignore it.
					continue;
				} else if (field->lastVersion && state->version > field->lastVersion) {
					// The field no longer exists, and we're from loading a version where it did not exist.
					// Ignore it.
					continue;
				} else if (field->lastVersion) {
					// The field no longer exists, but we're loading a version where it did. 
					// Skip over it.
					fieldPointer = NULL;
				} else {
					// The field exists, and we're loading from a version where it exists.
				}
			} else {
				if (field->lastVersion) {
					continue;
				}
			}

			field->item.type->op(state, &field->item, fieldPointer);

			if (state->op == RF_OP_FREE) {
				RF_MEMZERO(fieldPointer, field->item.byteCount);
			}
		}
	} else if (state->op == RF_OP_COUNT) {
		RfIterator *iterator = (RfIterator *) state;
		uint32_t count = 0;

		for (uintptr_t i = 0; i < type->fieldCount; i++) {
			if (!type->fields[i].lastVersion) {
				count++;
			}
		}

		iterator->index = count;
	} else if (state->op == RF_OP_ITERATE) {
		RfIterator *iterator = (RfIterator *) state;
		uint32_t count = 0;

		for (uintptr_t i = 0; i < type->fieldCount; i++) {
			iterator->isRemoved = type->fields[i].lastVersion;

			if (!iterator->isRemoved || iterator->includeRemovedFields) {
				if (iterator->index == count) {
					iterator->pointer = (uint8_t *) pointer + type->fields[i].offset;
					iterator->item = type->fields[i].item;
					return;
				}

				count++;
			}
		}

		state->error = true;
	}
}

void RfUnionOp(RfState *state, RfItem *item, void *pointer) {
	RfType *type = item->type;
	RfUnionHeader *header = (RfUnionHeader *) pointer;

	if (state->op == RF_OP_SAVE) {
		state->access(state, &header->tag, sizeof(uint32_t));

		if (header->tag) {
			RfField *field = type->fields + header->tag - 1;
			field->item.type->op(state, &field->item, (uint8_t *) pointer + field->offset);
		}
	} else if (state->op == RF_OP_LOAD) {
		uint32_t tag = 0;
		state->access(state, &tag, sizeof(uint32_t));

		if (tag > type->fieldCount) {
			tag = 0;
			state->error = true;
		} else if (tag) {
			RfField *field = type->fields + tag - 1;
			if (field->lastVersion) tag = 0;
			field->item.type->op(state, &field->item, pointer && !field->lastVersion ? ((uint8_t *) pointer + field->offset) : NULL);
		}

		if (header) {
			header->tag = tag;
		}
	} else if (state->op == RF_OP_FREE) {
		if (header->tag) {
			RfField *field = type->fields + header->tag - 1;
			field->item.type->op(state, &field->item, (uint8_t *) pointer + field->offset);
		}

		header->tag = 0;
	} else if (state->op == RF_OP_COUNT) {
		RfIterator *iterator = (RfIterator *) state;
		iterator->index = header->tag ? 1 : 0;
	} else if (state->op == RF_OP_ITERATE) {
		RfIterator *iterator = (RfIterator *) state;

		if (!header->tag || iterator->index > 1) {
			state->error = true;
		} else {
			RfField *field = type->fields + header->tag - 1;
			iterator->pointer = (uint8_t *) pointer + field->offset;
			iterator->item = field->item;
		}
	}
}

void RfUnionSelect(RfState *state, RfItem *item, RfUnionHeader *header, uint32_t tag) {
	RF_ASSERT(header->tag < item->type->fieldCount && !item->type->fields[header->tag].lastVersion);
	RF_ASSERT(tag < item->type->fieldCount && !item->type->fields[tag].lastVersion);

	if (header->tag) {
		RfField *field = item->type->fields + header->tag - 1;
		field->item.type->op(state, &field->item, (uint8_t *) header + field->offset);
	}

	header->tag = tag + 1;
}

void RfEnumOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == RF_OP_SAVE) {
		RfIntegerSave(state, pointer, item->byteCount);
	} else if (state->op == RF_OP_LOAD) {
		RfIntegerLoad(state, pointer, item->byteCount);

		uint32_t value = 0;

		if (item->byteCount == 1) {
			value = *(uint8_t *) pointer;
		} else if (item->byteCount == 2) {
			value = *(uint16_t *) pointer;
		} else if (item->byteCount == 4) {
			value = *(uint32_t *) pointer;
		} else {
			RF_ASSERT(false);
		}

		if (value >= item->type->fieldCount) {
			state->error = true;
		}
	}
}

void RfEndianOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == RF_OP_SAVE || state->op == RF_OP_LOAD) {
		state->access(state, pointer, item->byteCount);
	}
}

void RfBoolOp(RfState *state, RfItem *item, void *pointer) {
	RfEndianOp(state, item, pointer);

	if (state->op == RF_OP_LOAD) {
		if (pointer && *(uint8_t *) pointer > 1) {
			state->error = true;
		}
	}
}

void RfIntegerOp(RfState *state, RfItem *item, void *pointer) {
	if (state->op == RF_OP_SAVE) {
		RfIntegerSave(state, pointer, item->byteCount);
	} else if (state->op == RF_OP_LOAD) {
		RfIntegerLoad(state, pointer, item->byteCount);
	}
}

void RfDataOp(RfState *state, RfItem *item, void *pointer) {
	(void) item;
	RfData *data = (RfData *) pointer;

	if (state->op == RF_OP_SAVE) {
		uint32_t byteCount = data->byteCount;
		RfIntegerSave(state, &byteCount, sizeof(uint32_t));
		state->access(state, data->buffer, data->byteCount);
	} else if (state->op == RF_OP_LOAD) {
		uint32_t byteCount = 0;
		RfIntegerLoad(state, &byteCount, sizeof(uint32_t));

		if (data) {
			RF_ASSERT(!data->buffer);
			data->buffer = state->allocate(state, NULL, byteCount);
			if (!data->buffer) { state->error = true; return; }
			data->byteCount = byteCount;
		}

		state->access(state, data ? data->buffer : NULL, data->byteCount);
	} else if (state->op == RF_OP_FREE) {
		state->allocate(state, data->buffer, 0);
		data->buffer = NULL;
	}
}

void RfObjectOp(RfState *state, RfItem *item, void *pointer) {
	RfItem *objectItem = (RfItem *) item->options;
	void **object = (void **) pointer;

	if (state->op == RF_OP_SAVE) {
		uint8_t present = *object != NULL;
		state->access(state, &present, sizeof(uint8_t));
		if (present) objectItem->type->op(state, objectItem, *object);
	} else if (state->op == RF_OP_LOAD) {
		uint8_t present = 0;
		state->access(state, &present, sizeof(uint8_t));

		if (object) {
			RF_ASSERT(!(*object));

			if (present) {
				*object = state->allocate(state, NULL, objectItem->byteCount);
				if (!(*object)) { state->error = true; return; }
				RF_MEMZERO(*object, objectItem->byteCount);
				objectItem->type->op(state, objectItem, *object);
			}
		} else if (present) {
			objectItem->type->op(state, objectItem, NULL);
		}
	} else if (state->op == RF_OP_FREE) {
		if (*object) {
			objectItem->type->op(state, objectItem, *object);
			state->allocate(state, *object, 0);
			*object = NULL;
		}
	} else if (state->op == RF_OP_COUNT) {
		RfIterator *iterator = (RfIterator *) state;
		iterator->index = *object ? 1 : 0;
	} else if (state->op == RF_OP_ITERATE) {
		RfIterator *iterator = (RfIterator *) state;

		if (!(*object) || iterator->index > 1) {
			state->error = true;
		} else {
			iterator->pointer = *object;
			iterator->item = *objectItem;
		}
	}
}

void RfNoneOp(RfState *state, RfItem *item, void *pointer) {
	(void) state;
	(void) item;
	(void) pointer;
}

void RfArrayOp(RfState *state, RfItem *item, void *_pointer) {
	RfArrayHeader **pointer = (RfArrayHeader **) _pointer;
	RfItem *objectItem = (RfItem *) item->options;

	if (state->op == RF_OP_SAVE) {
		uint32_t length = 0;

		if (*pointer) {
			length = (*pointer)[-1].length;
		}

		RfIntegerSave(state, &length, sizeof(uint32_t));

		for (uint32_t i = 0; i < length; i++) {
			objectItem->type->op(state, objectItem, (uint8_t *) (*pointer) + i * objectItem->byteCount);
		}
	} else if (state->op == RF_OP_LOAD) {
		RF_ASSERT(!pointer || !(*pointer));

		uint32_t length = 0;
		RfIntegerLoad(state, &length, sizeof(uint32_t));

		if (length >= 0xFFFFFFFF / objectItem->byteCount) {
			state->error = true;
			return;
		}

		if (!length) {
			return;
		}

		if (pointer) {
			void *allocation = state->allocate(state, NULL, length * objectItem->byteCount + sizeof(RfArrayHeader));

			if (!allocation) {
				state->error = true;
				return;
			}

			*pointer = (RfArrayHeader *) allocation + 1;

			(*pointer)[-1].length = 0;
			(*pointer)[-1].capacity = length;
			(*pointer)[-1].hashTable = NULL;
			(*pointer)[-1].temporary = 0;
		}

		for (uint32_t i = 0; i < length && !state->error; i++) {
			if (pointer) {
				uint32_t index = (*pointer)[-1].length;
				(*pointer)[-1].length++;
				uint8_t *p = (uint8_t *) (*pointer) + index * objectItem->byteCount;
				RF_MEMZERO(p, objectItem->byteCount);
				objectItem->type->op(state, objectItem, p);
			} else {
				objectItem->type->op(state, objectItem, NULL);
			}
		}
	} else if (state->op == RF_OP_FREE) {
		if (*pointer) {
			state->allocate(state, (*pointer) - 1, 0);
			(*pointer) = NULL;
		}
	} else if (state->op == RF_OP_COUNT) {
		RfIterator *iterator = (RfIterator *) state;
		iterator->index = *pointer ? (*pointer)[-1].length : 0;
	} else if (state->op == RF_OP_ITERATE) {
		RfIterator *iterator = (RfIterator *) state;

		if (!(*pointer) || iterator->index > (*pointer)[-1].length) {
			state->error = true;
		} else {
			iterator->pointer = (uint8_t *) (*pointer) + iterator->index * objectItem->byteCount;
			iterator->item = *objectItem;
		}
	}
}

bool RfPathResolve(RfPath *path, RfItem *item, void **pointer) {
	RfIterator iterator = { 0 };
	iterator.s.op = RF_OP_ITERATE;
	iterator.item = *item;
	iterator.pointer = *pointer;
	iterator.includeRemovedFields = true;

	for (int i = 0; path->indices[i] != RF_PATH_TERMINATOR && !iterator.s.error; i++) {
		iterator.index = path->indices[i];
		RfItem _item = iterator.item;
		_item.type->op(&iterator.s, &_item, iterator.pointer);
	}

	*item = iterator.item;
	*pointer = iterator.pointer;
	return !iterator.s.error;
}

void RfBroadcast(RfState *state, RfItem *item, void *pointer, bool recurse) {
	RfIterator iterator = { 0 };
	iterator.s.op = RF_OP_COUNT;
	item->type->op(&iterator.s, item, pointer);
	iterator.s.op = RF_OP_ITERATE;
	uint32_t count = iterator.index;

	for (uint32_t i = 0; i < count; i++) {
		iterator.index = i;
		item->type->op(&iterator.s, item, pointer);
		if (iterator.s.error) return;

		iterator.item.type->op(state, &iterator.item, iterator.pointer);

		if (recurse) {
			RfBroadcast(state, &iterator.item, iterator.pointer, true);
		}
	}
}

void RfWriteGrowableBuffer(RfState *state, void *source, size_t byteCount) {
	if (state->error) return;

	RfGrowableBuffer *destination = (RfGrowableBuffer *) state;

	if (destination->data.byteCount + byteCount > destination->position) {
		destination->position = destination->position * 2;

		if (destination->data.byteCount + byteCount > destination->position) {
			destination->position = destination->data.byteCount + byteCount + 64;
		}

		void *old = destination->data.buffer;
		destination->data.buffer = state->allocate(state, destination->data.buffer, destination->position);
		
		if (!destination->data.buffer) {
			state->allocate(state, old, 0);
			state->error = true;
			return;
		}
	}
	
	RF_MEMCPY((uint8_t *) destination->data.buffer + destination->data.byteCount, source, byteCount);
	destination->data.byteCount += byteCount;
}

void RfReadGrowableBuffer(RfState *state, void *destination, size_t byteCount) {
	if (state->error) return;

	RfGrowableBuffer *source = (RfGrowableBuffer *) state;

	if (source->position + byteCount > source->data.byteCount) {
		state->error = true;
	} else {
		if (destination) {
			RF_MEMCPY(destination, (uint8_t *) source->data.buffer + source->position, byteCount);
		}

		source->position += byteCount;
	}
}

void *RfRealloc(RfState *state, void *previous, size_t byteCount) {
	(void) state;
	return RF_REALLOC(previous, byteCount);
}

RfType rfI8     = { .op = RfEndianOp,  .cName = "I8"     };
RfType rfI16    = { .op = RfIntegerOp, .cName = "I16"    };
RfType rfI32    = { .op = RfIntegerOp, .cName = "I32"    };
RfType rfI64    = { .op = RfIntegerOp, .cName = "I64"    };
RfType rfU8     = { .op = RfEndianOp,  .cName = "U8"     };
RfType rfU16    = { .op = RfIntegerOp, .cName = "U16"    };
RfType rfU32    = { .op = RfIntegerOp, .cName = "U32"    };
RfType rfU64    = { .op = RfIntegerOp, .cName = "U64"    };
RfType rfChar   = { .op = RfEndianOp,  .cName = "Char"   };
RfType rfBool   = { .op = RfBoolOp,    .cName = "Bool"   };
RfType rfF32    = { .op = RfEndianOp,  .cName = "F32"    };
RfType rfF64    = { .op = RfEndianOp,  .cName = "F64"    };
RfType rfData   = { .op = RfDataOp,    .cName = "Data"   };
RfType rfObject = { .op = RfObjectOp,  .cName = "Object" };
RfType rfArray  = { .op = RfArrayOp,   .cName = "Array"  };
RfType rfNone   = { .op = RfNoneOp,    .cName = "None"   };

#endif
