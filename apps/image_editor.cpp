// TODO Saving.
// TODO Don't use an EsPaintTarget for the bitmap?
// TODO Show brush preview.
// TODO Other tools: text, selection.
// TODO Resize and crop image.
// TODO Clipboard.
// TODO Clearing textbox undo from EsTextboxInsert?
// TODO Handling out of memory.
// TODO Color palette.
// TODO More brushes?
// TODO Grid.
// TODO Zoom and pan with EsCanvasPane.
// TODO Status bar.
// TODO Clearing undo if too much memory is used.

#define ES_INSTANCE_TYPE Instance
#include <essence.h>
#include <shared/array.cpp>
#include <shared/strings.cpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#define STBIW_MEMMOVE EsCRTmemmove
#define STBIW_MALLOC(sz) EsCRTmalloc(sz)
#define STBIW_REALLOC(p,newsz) EsCRTrealloc(p,newsz)
#define STBIW_FREE(p) EsCRTfree(p)
#define STBIW_ASSERT EsAssert
#include <shared/stb_image_write.h>

#define TILE_SIZE (128)

struct Tile {
	size_t referenceCount;
	uint64_t ownerID;
	uint32_t bits[TILE_SIZE * TILE_SIZE];
};

struct Image {
	Tile **tiles;
	size_t tileCountX, tileCountY;
	uint64_t id;
	uint32_t width, height;
};

struct Instance : EsInstance {
	EsElement *canvas;
	EsColorWell *colorWell;
	EsTextbox *brushSize;

	EsPanel *toolPanel;
	EsButton *toolDropdown;

	EsPaintTarget *bitmap;
	uint32_t bitmapWidth, bitmapHeight;

	Image image;
	uint64_t nextImageID;

	EsCommand commandBrush;
	EsCommand commandFill;
	EsCommand commandRectangle;
	EsCommand commandSelect;
	EsCommand commandText;

	// Data while drawing:
	EsRectangle modifiedBounds;
	float previousPointX, previousPointY;
	bool dragged;
};

const EsInstanceClassEditorSettings editorSettings = {
	INTERFACE_STRING(ImageEditorNewFileName),
	INTERFACE_STRING(ImageEditorNewDocument),
	ES_ICON_IMAGE_X_GENERIC,
};

const EsStyle styleBitmapSizeTextbox = {
	.inherit = ES_STYLE_TEXTBOX_BORDERED_SINGLE_COMPACT,

	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH,
		.preferredWidth = 70,
	},
};

const EsStyle styleImageMenuTable = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_ALL | ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_4(20, 20, 5, 8),
		.gapMajor = 6,
		.gapMinor = 6,
	},
};

Image ImageFork(Instance *instance, Image image, uint32_t newWidth = 0, uint32_t newHeight = 0) {
	Image copy = image;

	copy.width = newWidth ?: image.width;
	copy.height = newHeight ?: image.height;
	copy.tileCountX = newWidth ? (newWidth + TILE_SIZE - 1) / TILE_SIZE : image.tileCountX;
	copy.tileCountY = newHeight ? (newHeight + TILE_SIZE - 1) / TILE_SIZE : image.tileCountY;
	copy.id = instance->nextImageID++;
	copy.tiles = (Tile **) EsHeapAllocate(copy.tileCountX * copy.tileCountY * sizeof(Tile *), true);

	for (uintptr_t y = 0; y < copy.tileCountY; y++) {
		for (uintptr_t x = 0; x < copy.tileCountX; x++) {
			uintptr_t source = y * image.tileCountX + x;
			uintptr_t destination = y * copy.tileCountX + x;

			if (y < image.tileCountY && x < image.tileCountX && image.tiles[source]) {
				image.tiles[source]->referenceCount++;
				copy.tiles[destination] = image.tiles[source];
			}
		}
	}

	return copy;
}

void ImageDelete(Image image) {
	for (uintptr_t i = 0; i < image.tileCountX * image.tileCountY; i++) {
		image.tiles[i]->referenceCount--;

		if (!image.tiles[i]->referenceCount) {
			// EsPrint("free tile %d, %d from image %d\n", i % image.tileCountX, i / image.tileCountX, image.tiles[i]->ownerID);
			EsHeapFree(image.tiles[i]);
		}
	}

	EsHeapFree(image.tiles);
}

void ImageCopyToPaintTarget(Instance *instance, const Image *image) {
	uint32_t *bits;
	size_t width, height, stride;
	EsPaintTargetStartDirectAccess(instance->bitmap, &bits, &width, &height, &stride);

	for (int32_t i = 0; i < (int32_t) image->tileCountY; i++) {
		for (int32_t j = 0; j < (int32_t) image->tileCountX; j++) {
			Tile *tile = image->tiles[i * image->tileCountX + j];
			int32_t copyWidth = TILE_SIZE, copyHeight = TILE_SIZE;

			if (j * TILE_SIZE + copyWidth > (int32_t) width) {
				copyWidth = width - j * TILE_SIZE;
			}

			if (i * TILE_SIZE + copyHeight > (int32_t) height) {
				copyHeight = height - i * TILE_SIZE;
			}

			if (tile) {
				for (int32_t y = 0; y < copyHeight; y++) {
					for (int32_t x = 0; x < copyWidth; x++) {
						bits[stride / 4 * (y + i * TILE_SIZE) + (x + j * TILE_SIZE)] = tile->bits[y * TILE_SIZE + x];
					}
				}
			} else {
				for (int32_t y = 0; y < copyHeight; y++) {
					for (int32_t x = 0; x < copyWidth; x++) {
						bits[stride / 4 * (y + i * TILE_SIZE) + (x + j * TILE_SIZE)] = 0;
					}
				}
			}
		}
	}

	EsPaintTargetEndDirectAccess(instance->bitmap);
}

Tile *ImageUpdateTile(Image *image, uint32_t x, uint32_t y, bool copyOldBits) {
	EsAssert(x < image->tileCountX && y < image->tileCountY);
	Tile **tileReference = image->tiles + y * image->tileCountX + x;
	Tile *tile = *tileReference;

	if (!tile || tile->ownerID != image->id) {
		if (tile && tile->referenceCount == 1) {
			tile->ownerID = image->id;

			// EsPrint("reuse tile %d, %d for image %d\n", x, y, image->id);
		} else {
			Tile *old = tile;
			if (old) old->referenceCount--;

			*tileReference = tile = (Tile *) EsHeapAllocate(sizeof(Tile), false);
			tile->referenceCount = 1;
			tile->ownerID = image->id;

			if (copyOldBits && old) {
				EsMemoryCopy(tile->bits, old->bits, sizeof(old->bits));
			}

			// EsPrint("allocate new tile %d, %d for image %d\n", x, y, image->id);
		}
	}

	return tile;
}

void ImageCopyFromPaintTarget(Instance *instance, Image *image, EsRectangle modifiedBounds) {
	uint32_t *bits;
	size_t width, height, stride;
	EsPaintTargetStartDirectAccess(instance->bitmap, &bits, &width, &height, &stride);

	modifiedBounds = EsRectangleIntersection(modifiedBounds, ES_RECT_4(0, width, 0, height));

	for (int32_t i = modifiedBounds.t / TILE_SIZE; i <= modifiedBounds.b / TILE_SIZE; i++) {
		for (int32_t j = modifiedBounds.l / TILE_SIZE; j <= modifiedBounds.r / TILE_SIZE; j++) {
			if ((uint32_t) j >= image->tileCountX || (uint32_t) i >= image->tileCountY) {
				continue;
			}

			Tile *tile = ImageUpdateTile(image, j, i, false);

			int32_t copyWidth = TILE_SIZE, copyHeight = TILE_SIZE;

			if (j * TILE_SIZE + copyWidth > (int32_t) width) {
				copyWidth = width - j * TILE_SIZE;
			}

			if (i * TILE_SIZE + copyHeight > (int32_t) height) {
				copyHeight = height - i * TILE_SIZE;
			}

			for (int32_t y = 0; y < copyHeight; y++) {
				for (int32_t x = 0; x < copyWidth; x++) {
					tile->bits[y * TILE_SIZE + x] = bits[stride / 4 * (y + i * TILE_SIZE) + (x + j * TILE_SIZE)];
				}
			}
		}
	}

	EsPaintTargetEndDirectAccess(instance->bitmap);
}

void ImageUndoMessage(const void *item, EsUndoManager *manager, EsMessage *message) {
	const Image *image = (const Image *) item;
	Instance *instance = EsUndoGetInstance(manager);

	if (message->type == ES_MSG_UNDO_INVOKE) {
		EsUndoPush(manager, ImageUndoMessage, &instance->image, sizeof(Image));
		instance->image = *image;

		if (instance->bitmapWidth != image->width || instance->bitmapHeight != image->height) {
			instance->bitmapWidth = image->width;
			instance->bitmapHeight = image->height;
			EsPaintTargetDestroy(instance->bitmap);
			instance->bitmap = EsPaintTargetCreate(instance->bitmapWidth, instance->bitmapHeight, false);
			EsElementRelayout(EsElementGetLayoutParent(instance->canvas));
		}

		ImageCopyToPaintTarget(instance, image);
		EsElementRepaint(instance->canvas);
	} else if (message->type == ES_MSG_UNDO_CANCEL) {
		ImageDelete(*image);
	}
}

int BrushSizeMessage(EsElement *element, EsMessage *message) {
	EsTextbox *textbox = (EsTextbox *) element;

	if (message->type == ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA) {
		double oldValue = EsTextboxGetContentsAsDouble(textbox); 
		double newValue = oldValue + message->numberDragDelta.delta * (message->numberDragDelta.fast ? 1 : 0.1);

		if (newValue < 1) {
			newValue = 1;
		} else if (newValue > 200) {
			newValue = 200;
		}

		char result[64];
		size_t resultBytes = EsStringFormat(result, sizeof(result), "%d.%d", (int) newValue, (int) (newValue * 10) % 10);
		EsTextboxSelectAll(textbox);
		EsTextboxInsert(textbox, result, resultBytes);
	}

	return 0;
}

EsRectangle DrawFill(Instance *instance, EsPoint point) {
	uint32_t color = 0xFF000000 | EsColorWellGetRGB(instance->colorWell);
	EsRectangle modifiedBounds = ES_RECT_4(point.x, point.x, point.y, point.y);

	if ((uint32_t) point.x >= instance->bitmapWidth || (uint32_t) point.y >= instance->bitmapHeight) {
		return {};
	}

	uint32_t *bits;
	size_t width, height, stride;
	EsPaintTargetStartDirectAccess(instance->bitmap, &bits, &width, &height, &stride);
	stride /= 4;

	Array<EsPoint> pointsToVisit = {};

	uint32_t replaceColor = bits[point.y * stride + point.x];
	if (replaceColor != color) pointsToVisit.Add(point);

	while (pointsToVisit.Length()) {
		EsPoint startPoint = pointsToVisit.Pop();

		if (startPoint.y < modifiedBounds.t) {
			modifiedBounds.t = startPoint.y;
		}

		if (startPoint.y > modifiedBounds.b) {
			modifiedBounds.b = startPoint.y;
		}

		for (ptrdiff_t delta = -1; delta <= 1; delta += 2) {
			EsPoint point = startPoint;
			uint32_t *pointer = bits + point.y * stride + point.x;

			bool spaceAbove = false;
			bool spaceBelow = false;

			if (delta == 1) {
				point.x += delta;
				pointer += delta;

				if (point.x == (int32_t) width) {
					break;
				}
			}

			while (true) {
				if (*pointer != replaceColor) {
					break;
				}

				*pointer = color;

				if (point.x < modifiedBounds.l) {
					modifiedBounds.l = point.x;
				}

				if (point.x > modifiedBounds.r) {
					modifiedBounds.r = point.x;
				}

				if (point.y) {
					if (!spaceAbove && pointer[-stride] == replaceColor) {
						spaceAbove = true;
						pointsToVisit.Add({ point.x, point.y - 1 });
					} else if (spaceAbove && pointer[-stride] != replaceColor) {
						spaceAbove = false;
					}
				}

				if (point.y != (int32_t) height - 1) {
					if (!spaceBelow && pointer[stride] == replaceColor) {
						spaceBelow = true;
						pointsToVisit.Add({ point.x, point.y + 1 });
					} else if (spaceBelow && pointer[stride] != replaceColor) {
						spaceBelow = false;
					}
				}

				point.x += delta;
				pointer += delta;

				if (point.x == (int32_t) width || point.x < 0) {
					break;
				}
			}
		}
	}

	modifiedBounds.r++, modifiedBounds.b += 2;
	pointsToVisit.Free();
	EsPaintTargetEndDirectAccess(instance->bitmap);
	EsElementRepaint(instance->canvas, &modifiedBounds); 
	return modifiedBounds;
}

EsRectangle DrawLine(Instance *instance, bool force = false) {
	EsPoint point = EsMouseGetPosition(instance->canvas); 
	float brushSize = EsTextboxGetContentsAsDouble(instance->brushSize);
	float spacing = brushSize * 0.1f;
	uint32_t color = 0xFF000000 | EsColorWellGetRGB(instance->colorWell);
	EsRectangle modifiedBounds = ES_RECT_4(instance->bitmapWidth, 0, instance->bitmapHeight, 0);

	uint32_t *bits;
	size_t width, height, stride;
	EsPaintTargetStartDirectAccess(instance->bitmap, &bits, &width, &height, &stride);
	stride /= 4;

	// Draw the line.

	while (true) {
		float dx = point.x - instance->previousPointX;
		float dy = point.y - instance->previousPointY;
		float distance = EsCRTsqrtf(dx * dx + dy * dy);

		if (distance < spacing && !force) {
			break;
		}

		int32_t x0 = instance->previousPointX;
		int32_t y0 = instance->previousPointY;

		EsRectangle bounds = ES_RECT_4(x0 - brushSize / 2 - 1, x0 + brushSize / 2 + 1, 
				y0 - brushSize / 2 - 1, y0 + brushSize / 2 + 1);
		bounds = EsRectangleIntersection(bounds, ES_RECT_4(0, instance->bitmapWidth, 0, instance->bitmapHeight));
		modifiedBounds = EsRectangleBounding(modifiedBounds, bounds);

		for (int32_t y = bounds.t; y < bounds.b; y++) {
			for (int32_t x = bounds.l; x < bounds.r; x++) {
				float distance = (x - x0) * (x - x0) + (y - y0) * (y - y0);

				if (distance < brushSize * brushSize * 0.25f) {
					bits[y * stride + x] = color;
				}
			}
		}

		if (force) {
			break;
		}

		instance->previousPointX += dx / distance * spacing;
		instance->previousPointY += dy / distance * spacing;
	}

	// Repaint the canvas.

	modifiedBounds.r++, modifiedBounds.b++; 
	EsPaintTargetEndDirectAccess(instance->bitmap);
	EsElementRepaint(instance->canvas, &modifiedBounds); 
	return modifiedBounds;
}

int CanvasMessage(EsElement *element, EsMessage *message) {
	Instance *instance = element->instance;

	if (message->type == ES_MSG_PAINT) {
		EsPainter *painter = message->painter;
		EsRectangle bounds = EsPainterBoundsInset(painter); 
		EsRectangle area = ES_RECT_4(bounds.l, bounds.l + instance->bitmapWidth, bounds.t, bounds.t + instance->bitmapHeight);
		EsDrawPaintTarget(painter, instance->bitmap, area, ES_RECT_4(0, instance->bitmapWidth, 0, instance->bitmapHeight), 0xFF); 

		if (instance->commandRectangle.check == ES_CHECK_CHECKED && instance->dragged) {
			EsRectangle rectangle = instance->modifiedBounds;
			rectangle.l += painter->offsetX, rectangle.r += painter->offsetX;
			rectangle.t += painter->offsetY, rectangle.b += painter->offsetY;
			EsDrawBlock(painter, EsRectangleIntersection(rectangle, area), 0xFF000000 | EsColorWellGetRGB(instance->colorWell));
		}
	} else if ((message->type == ES_MSG_MOUSE_LEFT_DRAG || message->type == ES_MSG_MOUSE_MOVED) 
			&& EsMouseIsLeftHeld() && instance->commandBrush.check == ES_CHECK_CHECKED) {
		instance->modifiedBounds = EsRectangleBounding(DrawLine(instance), instance->modifiedBounds);
	} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG && instance->commandRectangle.check == ES_CHECK_CHECKED) {
		EsRectangle previous = instance->modifiedBounds;
		EsPoint point = EsMouseGetPosition(element);
		EsRectangle rectangle;
		if (point.x < instance->previousPointX) rectangle.l = point.x, rectangle.r = instance->previousPointX + 1;
		else rectangle.l = instance->previousPointX, rectangle.r = point.x + 1;
		if (point.y < instance->previousPointY) rectangle.t = point.y, rectangle.b = instance->previousPointY + 1;
		else rectangle.t = instance->previousPointY, rectangle.b = point.y + 1;
		instance->modifiedBounds = rectangle;
		EsRectangle bounding = EsRectangleBounding(rectangle, previous);
		EsElementRepaint(element, &bounding);
		instance->dragged = true;
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN && instance->commandBrush.check == ES_CHECK_CHECKED) {
		EsUndoPush(instance->undoManager, ImageUndoMessage, &instance->image, sizeof(Image));
		instance->image = ImageFork(instance, instance->image);
		EsPoint point = EsMouseGetPosition(element); 
		instance->previousPointX = point.x, instance->previousPointY = point.y;
		instance->modifiedBounds = ES_RECT_4(instance->bitmapWidth, 0, instance->bitmapHeight, 0);
		DrawLine(instance, true);
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN && instance->commandRectangle.check == ES_CHECK_CHECKED) {
		EsPoint point = EsMouseGetPosition(element); 
		instance->previousPointX = point.x, instance->previousPointY = point.y;
		instance->dragged = false;
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP && instance->commandBrush.check == ES_CHECK_CHECKED) {
		ImageCopyFromPaintTarget(instance, &instance->image, instance->modifiedBounds);
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP && instance->commandRectangle.check == ES_CHECK_CHECKED && instance->dragged) {
		instance->dragged = false;
		EsUndoPush(instance->undoManager, ImageUndoMessage, &instance->image, sizeof(Image));
		instance->image = ImageFork(instance, instance->image);
		EsPainter painter = {};
		painter.clip = ES_RECT_4(0, instance->bitmapWidth, 0, instance->bitmapHeight);
		painter.target = instance->bitmap;
		EsDrawBlock(&painter, instance->modifiedBounds, 0xFF000000 | EsColorWellGetRGB(instance->colorWell));
		ImageCopyFromPaintTarget(instance, &instance->image, instance->modifiedBounds);
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP && instance->commandFill.check == ES_CHECK_CHECKED) {
		EsUndoPush(instance->undoManager, ImageUndoMessage, &instance->image, sizeof(Image));
		instance->image = ImageFork(instance, instance->image);

		EsRectangle modifiedBounds = DrawFill(instance, EsMouseGetPosition(element));
		ImageCopyFromPaintTarget(instance, &instance->image, modifiedBounds);
	} else if (message->type == ES_MSG_GET_CURSOR) {
		message->cursorStyle = ES_CURSOR_CROSS_HAIR_PICK;
	} else if (message->type == ES_MSG_GET_WIDTH) {
		message->measure.width = instance->bitmapWidth;
	} else if (message->type == ES_MSG_GET_HEIGHT) {
		message->measure.height = instance->bitmapHeight;
	} else {
		return 0;
	}

	return ES_HANDLED;
}

void CommandSelectTool(Instance *instance, EsElement *, EsCommand *command) {
	if (command->check == ES_CHECK_CHECKED) {
		return;
	}

	EsCommandSetCheck(&instance->commandBrush, ES_CHECK_UNCHECKED, false);
	EsCommandSetCheck(&instance->commandFill, ES_CHECK_UNCHECKED, false);
	EsCommandSetCheck(&instance->commandRectangle, ES_CHECK_UNCHECKED, false);
	EsCommandSetCheck(&instance->commandSelect, ES_CHECK_UNCHECKED, false);
	EsCommandSetCheck(&instance->commandText, ES_CHECK_UNCHECKED, false);
	EsCommandSetCheck(command, ES_CHECK_CHECKED, false);

	if (command == &instance->commandBrush) EsButtonSetIcon(instance->toolDropdown, ES_ICON_DRAW_FREEHAND);
	if (command == &instance->commandFill) EsButtonSetIcon(instance->toolDropdown, ES_ICON_COLOR_FILL);
	if (command == &instance->commandRectangle) EsButtonSetIcon(instance->toolDropdown, ES_ICON_DRAW_RECTANGLE);
	if (command == &instance->commandSelect) EsButtonSetIcon(instance->toolDropdown, ES_ICON_OBJECT_GROUP);
	if (command == &instance->commandText) EsButtonSetIcon(instance->toolDropdown, ES_ICON_DRAW_TEXT);

	instance->dragged = false;
}

int BitmapSizeTextboxMessage(EsElement *element, EsMessage *message) {
	EsTextbox *textbox = (EsTextbox *) element;
	Instance *instance = textbox->instance;

	if (message->type == ES_MSG_TEXTBOX_EDIT_END || message->type == ES_MSG_TEXTBOX_NUMBER_DRAG_END) {
		char *expression = EsTextboxGetContents(textbox);
		EsCalculationValue value = EsCalculateFromUserExpression(expression); 
		EsHeapFree(expression);

		if (value.error) {
			return ES_REJECTED;
		}

		if (value.number < 1) value.number = 1;
		else if (value.number > 20000) value.number = 20000;
		int newSize = (int) (value.number + 0.5);
		char result[64];
		size_t resultBytes = EsStringFormat(result, sizeof(result), "%d", newSize);
		EsTextboxSelectAll(textbox);
		EsTextboxInsert(textbox, result, resultBytes);

		int oldSize = textbox->userData.i ? instance->bitmapHeight : instance->bitmapWidth;

		if (oldSize == newSize) {
			return ES_HANDLED;
		}

		EsRectangle clearRegion;

		if (textbox->userData.i) {
			instance->bitmapHeight = newSize;
			clearRegion = ES_RECT_4(0, instance->bitmapWidth, oldSize, newSize);
		} else {
			instance->bitmapWidth = newSize;
			clearRegion = ES_RECT_4(oldSize, newSize, 0, instance->bitmapHeight);
		}

		EsUndoPush(instance->undoManager, ImageUndoMessage, &instance->image, sizeof(Image));
		instance->image = ImageFork(instance, instance->image, instance->bitmapWidth, instance->bitmapHeight);
		EsPaintTargetDestroy(instance->bitmap);
		instance->bitmap = EsPaintTargetCreate(instance->bitmapWidth, instance->bitmapHeight, false);
		ImageCopyToPaintTarget(instance, &instance->image);

		EsPainter painter = {};
		painter.clip = ES_RECT_4(0, instance->bitmapWidth, 0, instance->bitmapHeight);
		painter.target = instance->bitmap;
		EsDrawBlock(&painter, clearRegion, 0xFFFFFFFF);
		ImageCopyFromPaintTarget(instance, &instance->image, clearRegion);

		EsElementRelayout(EsElementGetLayoutParent(instance->canvas));

		return ES_HANDLED;
	} else if (message->type == ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA) {
		int oldValue = EsTextboxGetContentsAsDouble(textbox); 
		int newValue = oldValue + message->numberDragDelta.delta * (message->numberDragDelta.fast ? 10 : 1);
		if (newValue < 1) newValue = 1;
		else if (newValue > 20000) newValue = 20000;
		char result[64];
		size_t resultBytes = EsStringFormat(result, sizeof(result), "%d", newValue);
		EsTextboxSelectAll(textbox);
		EsTextboxInsert(textbox, result, resultBytes);
		return ES_HANDLED;
	}

	return 0;
}

void ImageTransform(EsMenu *menu, EsGeneric context) {
	Instance *instance = menu->instance;
	EsUndoPush(instance->undoManager, ImageUndoMessage, &instance->image, sizeof(Image));

	uint32_t *bits;
	size_t width, height, stride;
	EsPaintTargetStartDirectAccess(instance->bitmap, &bits, &width, &height, &stride);

	EsPaintTarget *newTarget = nullptr;
	uint32_t *newBits = nullptr;
	size_t newStride = 0;
	
	if (context.i == 1 || context.i == 2) {
		instance->image = ImageFork(instance, instance->image, height, width);
		newTarget = EsPaintTargetCreate(height, width, false);
		EsPaintTargetStartDirectAccess(newTarget, &newBits, &height, &width, &newStride);
	} else {
		instance->image = ImageFork(instance, instance->image);
	}

	if (context.i == 1 /* rotate left */) {
		for (uintptr_t i = 0; i < height; i++) {
			for (uintptr_t j = 0; j < width; j++) {
				newBits[(width - j - 1) * newStride / 4 + i] = bits[i * stride / 4 + j];
			}
		}
	} else if (context.i == 2 /* rotate right */) {
		for (uintptr_t i = 0; i < height; i++) {
			for (uintptr_t j = 0; j < width; j++) {
				newBits[j * newStride / 4 + (height - i - 1)] = bits[i * stride / 4 + j];
			}
		}
	} else if (context.i == 3 /* flip horizontally */) {
		for (uintptr_t i = 0; i < height; i++) {
			for (uintptr_t j = 0; j < width / 2; j++) {
				uint32_t temporary = bits[i * stride / 4 + j];
				bits[i * stride / 4 + j] = bits[i * stride / 4 + (width - j - 1)];
				bits[i * stride / 4 + (width - j - 1)] = temporary;
			}
		}
	} else if (context.i == 4 /* flip vertically */) {
		for (uintptr_t i = 0; i < height / 2; i++) {
			for (uintptr_t j = 0; j < width; j++) {
				uint32_t temporary = bits[i * stride / 4 + j];
				bits[i * stride / 4 + j] = bits[(height - i - 1) * stride / 4 + j];
				bits[(height - i - 1) * stride / 4 + j] = temporary;
			}
		}
	}

	EsPaintTargetEndDirectAccess(instance->bitmap);

	if (newTarget) {
		EsPaintTargetDestroy(instance->bitmap);
		instance->bitmap = newTarget;
		size_t width, height;
		EsPaintTargetGetSize(instance->bitmap, &width, &height);
		instance->bitmapWidth = width;
		instance->bitmapHeight = height;
		EsElementRelayout(EsElementGetLayoutParent(instance->canvas));
	}

	ImageCopyFromPaintTarget(instance, &instance->image, ES_RECT_4(0, instance->bitmapWidth, 0, instance->bitmapHeight));
	EsElementRepaint(instance->canvas);
}

void MenuTools(Instance *instance, EsElement *element, EsCommand *) {
	EsMenu *menu = EsMenuCreate(element);
	EsMenuAddCommandsFromToolbar(menu, instance->toolPanel);
	EsMenuShow(menu);
}

void MenuImage(Instance *instance, EsElement *element, EsCommand *) {
	EsMenu *menu = EsMenuCreate(element);

	EsMenuAddItem(menu, ES_MENU_ITEM_HEADER, INTERFACE_STRING(ImageEditorCanvasSize));
	EsPanel *table = EsPanelCreate(menu, ES_PANEL_HORIZONTAL | ES_PANEL_TABLE, &styleImageMenuTable);
	EsPanelSetBands(table, 2, 2);

	char buffer[64];
	size_t bytes;
	EsTextbox *textbox;

	bytes = EsStringFormat(buffer, sizeof(buffer), "%d", instance->bitmapWidth);
	EsTextDisplayCreate(table, ES_CELL_H_RIGHT, ES_STYLE_TEXT_LABEL, INTERFACE_STRING(ImageEditorPropertyWidth));
	textbox = EsTextboxCreate(table, ES_TEXTBOX_EDIT_BASED, &styleBitmapSizeTextbox);
	EsTextboxInsert(textbox, buffer, bytes, false);
	textbox->userData.i = 0;
	textbox->messageUser = BitmapSizeTextboxMessage;
	EsTextboxUseNumberOverlay(textbox, true);

	bytes = EsStringFormat(buffer, sizeof(buffer), "%d", instance->bitmapHeight);
	EsTextDisplayCreate(table, ES_CELL_H_RIGHT, ES_STYLE_TEXT_LABEL, INTERFACE_STRING(ImageEditorPropertyHeight));
	textbox = EsTextboxCreate(table, ES_TEXTBOX_EDIT_BASED, &styleBitmapSizeTextbox);
	EsTextboxInsert(textbox, buffer, bytes, false);
	textbox->userData.i = 1;
	textbox->messageUser = BitmapSizeTextboxMessage;
	EsTextboxUseNumberOverlay(textbox, true);

	EsMenuAddSeparator(menu);
	EsMenuAddItem(menu, ES_MENU_ITEM_HEADER, INTERFACE_STRING(ImageEditorImageTransformations));
	EsMenuAddItem(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(ImageEditorRotateLeft), ImageTransform, 1);
	EsMenuAddItem(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(ImageEditorRotateRight), ImageTransform, 2);
	EsMenuAddItem(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(ImageEditorFlipHorizontally), ImageTransform, 3);
	EsMenuAddItem(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(ImageEditorFlipVertically), ImageTransform, 4);

	EsMenuShow(menu);
}

void InstanceCreate(EsMessage *message) {
	Instance *instance = EsInstanceCreate(message, INTERFACE_STRING(ImageEditorTitle));
	EsElement *toolbar = EsWindowGetToolbar(instance->window);
	EsInstanceSetClassEditor(instance, &editorSettings);

	// Register commands.

	EsCommandRegister(&instance->commandBrush, instance, CommandSelectTool, 1, "N", true);
	EsCommandRegister(&instance->commandFill, instance, CommandSelectTool, 2, "Shift+B", true);
	EsCommandRegister(&instance->commandRectangle, instance, CommandSelectTool, 3, "Shift+R", true);
	EsCommandRegister(&instance->commandSelect, instance, CommandSelectTool, 4, "R", false);
	EsCommandRegister(&instance->commandText, instance, CommandSelectTool, 5, "T", false);

	EsCommandSetCheck(&instance->commandBrush, ES_CHECK_CHECKED, false);

	// Create the toolbar.

	EsButton *button;

	EsToolbarAddFileMenu(toolbar);
	button = EsButtonCreate(toolbar, ES_BUTTON_DROPDOWN, ES_STYLE_PUSH_BUTTON_TOOLBAR_BIG, INTERFACE_STRING(ImageEditorImage));
	EsButtonSetIcon(button, ES_ICON_IMAGE_X_GENERIC);
	button->accessKey = 'I';
	EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT);
	EsButtonOnCommand(button, MenuImage);
	button = EsButtonCreate(toolbar, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_TOOLBAR_MEDIUM);
	EsCommandAddButton(EsCommandByID(instance, ES_COMMAND_UNDO), button);
	EsButtonSetIcon(button, ES_ICON_EDIT_UNDO_SYMBOLIC);
	button->accessKey = 'U';
	button = EsButtonCreate(toolbar, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_TOOLBAR_MEDIUM);
	EsCommandAddButton(EsCommandByID(instance, ES_COMMAND_REDO), button);
	EsButtonSetIcon(button, ES_ICON_EDIT_REDO_SYMBOLIC);
	button->accessKey = 'R';

	EsSpacerCreate(toolbar, ES_CELL_FILL);

	button = instance->toolDropdown = EsButtonCreate(toolbar, ES_BUTTON_DROPDOWN, ES_STYLE_PUSH_BUTTON_TOOLBAR_BIG, INTERFACE_STRING(ImageEditorPickTool));
	EsButtonSetIcon(button, ES_ICON_DRAW_FREEHAND);
	EsButtonOnCommand(button, MenuTools);
	button->accessKey = 'T';

	instance->toolPanel = EsPanelCreate(toolbar, ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_TOOLBAR);
	button = EsButtonCreate(instance->toolPanel, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_TOOLBAR_BIG, INTERFACE_STRING(ImageEditorToolBrush));
	EsCommandAddButton(&instance->commandBrush, button);
	EsButtonSetIcon(button, ES_ICON_DRAW_FREEHAND);
	button->accessKey = 'B';
	button = EsButtonCreate(instance->toolPanel, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_TOOLBAR_BIG, INTERFACE_STRING(ImageEditorToolFill));
	EsCommandAddButton(&instance->commandFill, button);
	EsButtonSetIcon(button, ES_ICON_COLOR_FILL);
	button->accessKey = 'F';
	button = EsButtonCreate(instance->toolPanel, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_TOOLBAR_BIG, INTERFACE_STRING(ImageEditorToolRectangle));
	EsCommandAddButton(&instance->commandRectangle, button);
	EsButtonSetIcon(button, ES_ICON_DRAW_RECTANGLE);
	button->accessKey = 'E';
	button = EsButtonCreate(instance->toolPanel, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_TOOLBAR_BIG, INTERFACE_STRING(ImageEditorToolSelect));
	EsCommandAddButton(&instance->commandSelect, button);
	EsButtonSetIcon(button, ES_ICON_OBJECT_GROUP);
	button->accessKey = 'S';
	button = EsButtonCreate(instance->toolPanel, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_TOOLBAR_BIG, INTERFACE_STRING(ImageEditorToolText));
	EsCommandAddButton(&instance->commandText, button);
	EsButtonSetIcon(button, ES_ICON_DRAW_TEXT);
	button->accessKey = 'T';

	EsWindowAddSizeAlternative(instance->window, instance->toolDropdown, instance->toolPanel, 1100, 0);

	EsSpacerCreate(toolbar, ES_CELL_FILL);

	EsPanel *section = EsPanelCreate(toolbar, ES_PANEL_HORIZONTAL);
	EsTextDisplayCreate(section, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(ImageEditorPropertyColor));
	instance->colorWell = EsColorWellCreate(section, ES_FLAGS_DEFAULT, 0xFFFF0000);
	instance->colorWell->accessKey = 'C';
	EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, 0, 5, 0);

	section = EsPanelCreate(toolbar, ES_PANEL_HORIZONTAL);
	EsTextDisplayCreate(section, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(ImageEditorPropertyBrushSize));
	instance->brushSize = EsTextboxCreate(section, ES_TEXTBOX_EDIT_BASED, ES_STYLE_TEXTBOX_BORDERED_SINGLE_COMPACT);
	instance->brushSize->messageUser = BrushSizeMessage;
	EsTextboxUseNumberOverlay(instance->brushSize, false);
	EsTextboxInsert(instance->brushSize, EsLiteral("5.0"));
	instance->brushSize->accessKey = 'Z';
	EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, 0, 1, 0);

	// Create the user interface.

	EsWindowSetIcon(instance->window, ES_ICON_MULTIMEDIA_PHOTO_MANAGER);

	EsCanvasPane *canvasPane = EsCanvasPaneCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_BACKGROUND);
	instance->canvas = EsCustomElementCreate(canvasPane, ES_CELL_FILL | ES_ELEMENT_FOCUSABLE);
	instance->canvas->messageUser = CanvasMessage;
	EsElementFocus(instance->canvas, false);

	// Setup the paint target and the image.

	instance->bitmapWidth = 500;
	instance->bitmapHeight = 400;
	instance->bitmap = EsPaintTargetCreate(instance->bitmapWidth, instance->bitmapHeight, false);
	EsPainter painter = {};
	painter.clip = ES_RECT_4(0, instance->bitmapWidth, 0, instance->bitmapHeight);
	painter.target = instance->bitmap;
	EsDrawBlock(&painter, painter.clip, 0xFFFFFFFF);
	instance->image = ImageFork(instance, {}, instance->bitmapWidth, instance->bitmapHeight);
	ImageCopyFromPaintTarget(instance, &instance->image, painter.clip);
}

void WriteCallback(void *context, void *data, int size) {
	EsBufferWrite((EsBuffer *) context, data, size);
}

void SwapRedAndBlueChannels(uint32_t *bits, size_t width, size_t height, size_t stride) {
	for (uintptr_t i = 0; i < height; i++) {
		for (uintptr_t j = 0; j < width; j++) {
			uint32_t *pixel = &bits[i * stride / 4 + j];
			*pixel = (*pixel & 0xFF00FF00) | (((*pixel >> 16) | (*pixel << 16)) & 0x00FF00FF);
		}
	}
}

void _start() {
	_init();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			InstanceCreate(message);
		} else if (message->type == ES_MSG_INSTANCE_OPEN) {
			Instance *instance = message->instanceOpen.instance;
			size_t fileSize;
			uint8_t *file = (uint8_t *) EsFileStoreReadAll(message->instanceOpen.file, &fileSize);

			if (!file) {
				EsInstanceOpenComplete(message, false);
				continue;
			}

			uint32_t width, height;
			uint8_t *bits = EsImageLoad(file, fileSize, &width, &height, 4);
			EsHeapFree(file);

			if (!bits) {
				EsInstanceOpenComplete(message, false, INTERFACE_STRING(ImageEditorUnsupportedFormat));
				continue;
			}

			EsPaintTargetDestroy(instance->bitmap);
			ImageDelete(instance->image);

			instance->bitmapWidth = width;
			instance->bitmapHeight = height;
			instance->bitmap = EsPaintTargetCreate(instance->bitmapWidth, instance->bitmapHeight, false);
			EsPainter painter = {};
			painter.clip = ES_RECT_4(0, instance->bitmapWidth, 0, instance->bitmapHeight);
			painter.target = instance->bitmap;
			EsDrawBitmap(&painter, painter.clip, (uint32_t *) bits, width * 4, 0xFF);
			instance->image = ImageFork(instance, {}, instance->bitmapWidth, instance->bitmapHeight);
			ImageCopyFromPaintTarget(instance, &instance->image, painter.clip);
			EsElementRelayout(EsElementGetLayoutParent(instance->canvas));

			EsHeapFree(bits);
			EsInstanceOpenComplete(message, true);
		} else if (message->type == ES_MSG_INSTANCE_SAVE) {
			Instance *instance = message->instanceSave.instance;

			uintptr_t extensionOffset = message->instanceSave.nameBytes;

			while (extensionOffset) {
				if (message->instanceSave.name[extensionOffset - 1] == '.') {
					break;
				} else {
					extensionOffset--;
				}
			}

			const char *extension = extensionOffset ? message->instanceSave.name + extensionOffset : "png";
			size_t extensionBytes = extensionOffset ? message->instanceSave.nameBytes - extensionOffset : 3;

			uint32_t *bits;
			size_t width, height, stride;
			EsPaintTargetStartDirectAccess(instance->bitmap, &bits, &width, &height, &stride);
			EsAssert(stride == width * 4); // TODO Other strides.
			SwapRedAndBlueChannels(bits, width, height, stride); // stbi_write uses the other order. We swap back below.

			uint8_t _buffer[4096];
			EsBuffer buffer = { .out = _buffer, .bytes = sizeof(_buffer) };
			buffer.fileStore = message->instanceSave.file;

			if (0 == EsStringCompare(extension, extensionBytes, EsLiteral("jpg"))
					|| 0 == EsStringCompare(extension, extensionBytes, EsLiteral("jpeg"))) {
				stbi_write_jpg_to_func(WriteCallback, &buffer, width, height, 4, bits, 90);
			} else if (0 == EsStringCompare(extension, extensionBytes, EsLiteral("bmp"))) {
				stbi_write_bmp_to_func(WriteCallback, &buffer, width, height, 4, bits);
			} else if (0 == EsStringCompare(extension, extensionBytes, EsLiteral("tga"))) {
				stbi_write_tga_to_func(WriteCallback, &buffer, width, height, 4, bits);
			} else {
				stbi_write_png_to_func(WriteCallback, &buffer, width, height, 4, bits, stride);
			}

			SwapRedAndBlueChannels(bits, width, height, stride); // Swap back.
			EsBufferFlushToFileStore(&buffer);
			EsPaintTargetEndDirectAccess(instance->bitmap);
			EsInstanceSaveComplete(message, true);
		} else if (message->type == ES_MSG_INSTANCE_DESTROY) {
			Instance *instance = message->instanceDestroy.instance;
			EsPaintTargetDestroy(instance->bitmap);
			ImageDelete(instance->image);
		}
	}
}
