struct InspectorElementEntry {
	EsElement *element;
	EsRectangle takenBounds, givenBounds;
	int depth;
};

struct InspectorWindow : EsInstance {
	EsInstance *instance; // The instance being inspected.

	EsListView *elementList;
	Array<InspectorElementEntry> elements; // TODO This is being leaked.
	InspectorElementEntry hoveredElement;
	char *cCategoryFilter;

	intptr_t selectedElement;
	EsButton *alignH[6];
	EsButton *alignV[6];
	EsButton *direction[4];
	EsTextbox *contentTextbox;
	EsButton *addChildButton;
	EsButton *addSiblingButton;
	EsButton *visualizeRepaints;
	EsButton *visualizeLayoutBounds;
	EsButton *visualizePaintSteps;
	EsListView *listEvents;
	EsTextbox *textboxCategoryFilter;
};

int InspectorElementItemCallback(EsElement *element, EsMessage *message) {
	InspectorWindow *inspector = (InspectorWindow *) element->instance;

	if (message->type == ES_MSG_HOVERED_START) {
		InspectorElementEntry *entry = &inspector->elements[EsListViewGetIndexFromItem(element)];
		if (entry->element->parent) entry->element->parent->Repaint(true);
		else entry->element->Repaint(true);
		inspector->hoveredElement = *entry;
	} else if (message->type == ES_MSG_HOVERED_END || message->type == ES_MSG_DESTROY) {
		EsListViewIndex index = EsListViewGetIndexFromItem(element);
		InspectorElementEntry *entry = &inspector->elements[index];
		if (entry->element->parent) entry->element->parent->Repaint(true);
		else entry->element->Repaint(true);
		inspector->hoveredElement = {};
	}

	return 0;
}

void InspectorUpdateEditor(InspectorWindow *inspector) {
	EsElement *e = inspector->selectedElement == -1 ? nullptr : inspector->elements[inspector->selectedElement].element;

	bool isStack = e && e->messageClass == ProcessPanelMessage && !(e->flags & (ES_PANEL_Z_STACK | ES_PANEL_TABLE | ES_PANEL_SWITCHER));
	bool alignHLeft = e ? (e->flags & ES_CELL_H_LEFT) : false, alignHRight = e ? (e->flags & ES_CELL_H_RIGHT) : false;
	bool alignHExpand = e ? (e->flags & ES_CELL_H_EXPAND) : false, alignHShrink = e ? (e->flags & ES_CELL_H_SHRINK) : false; 
	bool alignHPush = e ? (e->flags & ES_CELL_H_PUSH) : false;
	bool alignVTop = e ? (e->flags & ES_CELL_V_TOP) : false, alignVBottom = e ? (e->flags & ES_CELL_V_BOTTOM) : false;
	bool alignVExpand = e ? (e->flags & ES_CELL_V_EXPAND) : false, alignVShrink = e ? (e->flags & ES_CELL_V_SHRINK) : false; 
	bool alignVPush = e ? (e->flags & ES_CELL_V_PUSH) : false;
	bool stackHorizontal = isStack && (e->flags & ES_PANEL_HORIZONTAL);
	bool stackReverse = isStack && (e->flags & ES_PANEL_REVERSE);

	EsButtonSetCheck(inspector->alignH[0], (EsCheckState) (e && alignHLeft && !alignHRight), false);
	EsButtonSetCheck(inspector->alignH[1], (EsCheckState) (e &&  alignHLeft == alignHRight), false);
	EsButtonSetCheck(inspector->alignH[2], (EsCheckState) (e && !alignHLeft && alignHRight), false);
	EsButtonSetCheck(inspector->alignH[3], (EsCheckState) (e && alignHExpand), false);
	EsButtonSetCheck(inspector->alignH[4], (EsCheckState) (e && alignHShrink), false);
	EsButtonSetCheck(inspector->alignH[5], (EsCheckState) (e && alignHPush), false);

	EsButtonSetCheck(inspector->alignV[0], (EsCheckState) (e && alignVTop && !alignVBottom), false);
	EsButtonSetCheck(inspector->alignV[1], (EsCheckState) (e &&  alignVTop == alignVBottom), false);
	EsButtonSetCheck(inspector->alignV[2], (EsCheckState) (e && !alignVTop && alignVBottom), false);
	EsButtonSetCheck(inspector->alignV[3], (EsCheckState) (e && alignVExpand), false);
	EsButtonSetCheck(inspector->alignV[4], (EsCheckState) (e && alignVShrink), false);
	EsButtonSetCheck(inspector->alignV[5], (EsCheckState) (e && alignVPush), false);

	EsButtonSetCheck(inspector->direction[0], (EsCheckState) (isStack &&  stackHorizontal &&  stackReverse), false);
	EsButtonSetCheck(inspector->direction[1], (EsCheckState) (isStack &&  stackHorizontal && !stackReverse), false);
	EsButtonSetCheck(inspector->direction[2], (EsCheckState) (isStack && !stackHorizontal &&  stackReverse), false);
	EsButtonSetCheck(inspector->direction[3], (EsCheckState) (isStack && !stackHorizontal && !stackReverse), false);

	EsElementSetDisabled(inspector->alignH[0], !e);
	EsElementSetDisabled(inspector->alignH[1], !e);
	EsElementSetDisabled(inspector->alignH[2], !e);
	EsElementSetDisabled(inspector->alignH[3], !e);
	EsElementSetDisabled(inspector->alignH[4], !e);
	EsElementSetDisabled(inspector->alignH[5], !e);
	EsElementSetDisabled(inspector->alignV[0], !e);
	EsElementSetDisabled(inspector->alignV[1], !e);
	EsElementSetDisabled(inspector->alignV[2], !e);
	EsElementSetDisabled(inspector->alignV[3], !e);
	EsElementSetDisabled(inspector->alignV[4], !e);
	EsElementSetDisabled(inspector->alignV[5], !e);
	EsElementSetDisabled(inspector->direction[0], !isStack);
	EsElementSetDisabled(inspector->direction[1], !isStack);
	EsElementSetDisabled(inspector->direction[2], !isStack);
	EsElementSetDisabled(inspector->direction[3], !isStack);
	EsElementSetDisabled(inspector->addChildButton, !isStack);
	EsElementSetDisabled(inspector->addSiblingButton, !e || !e->parent);

	EsElementSetDisabled(inspector->textboxCategoryFilter, !e);

	EsTextboxSelectAll(inspector->contentTextbox);
	EsTextboxInsert(inspector->contentTextbox, "", 0, false);

	if (e) {
#if 0
		for (uintptr_t i = 0; i < sizeof(builtinStyles) / sizeof(builtinStyles[0]); i++) {
			if (e->currentStyleKey.partHash == CalculateCRC64(EsLiteral(builtinStyles[i]))) {
				EsTextboxInsert(inspector->styleTextbox, builtinStyles[i], -1, false);
				break;
			}
		}
#endif

		if (e->messageClass == ProcessButtonMessage) {
			EsButton *button = (EsButton *) e;
			EsElementSetDisabled(inspector->contentTextbox, false);
			EsTextboxInsert(inspector->contentTextbox, button->label, button->labelBytes, false);
		} else if (e->messageClass == ProcessTextDisplayMessage) {
			EsTextDisplay *display = (EsTextDisplay *) e;
			EsElementSetDisabled(inspector->contentTextbox, false);
			EsTextboxInsert(inspector->contentTextbox, display->contents, display->textRuns[display->textRunCount].offset, false);
		} else {
			EsElementSetDisabled(inspector->contentTextbox, true);
		}
	} else {
		EsElementSetDisabled(inspector->contentTextbox, true);
	}
}

int InspectorElementListCallback(EsElement *element, EsMessage *message) {
	InspectorWindow *inspector = (InspectorWindow *) element->instance;

	if (message->type == ES_MSG_LIST_VIEW_GET_CONTENT) {
		int column = message->getContent.columnID, index = message->getContent.index;
		EsAssert(index >= 0 && index < (int) inspector->elements.Length());
		InspectorElementEntry *entry = &inspector->elements[index];

		if (column == 0) {
			EsBufferFormat(message->getContent.buffer, "%z", entry->element->cName);
		} else if (column == 1) {
			EsBufferFormat(message->getContent.buffer, "%R", entry->element->GetWindowBounds(false));
		} else if (column == 2) {
			EsMessage m = *message;
			m.type = ES_MSG_GET_INSPECTOR_INFORMATION;
			EsMessageSend(entry->element, &m);
		}

		return ES_HANDLED;
	} else if (message->type == ES_MSG_LIST_VIEW_GET_INDENT) {
		message->getIndent.indent = inspector->elements[message->getIndent.index].depth;
		return ES_HANDLED;
	} else if (message->type == ES_MSG_LIST_VIEW_CREATE_ITEM) {
		message->createItem.item->messageUser = InspectorElementItemCallback;
		return ES_HANDLED;
	} else if (message->type == ES_MSG_LIST_VIEW_SELECT) {
		if (inspector->selectedElement != -1) {
			inspector->elements[inspector->selectedElement].element->state &= ~UI_STATE_INSPECTING;
		}
		
		inspector->selectedElement = message->selectItem.isSelected ? message->selectItem.index : -1;

		if (inspector->selectedElement != -1) {
			EsElement *e = inspector->elements[inspector->selectedElement].element;
			e->state |= UI_STATE_INSPECTING;
			InspectorNotifyElementEvent(e, nullptr, "Viewing events from '%z'.\n", e->cName);
		}

		InspectorUpdateEditor(inspector);
		return ES_HANDLED;
	} else if (message->type == ES_MSG_LIST_VIEW_IS_SELECTED) {
		message->selectItem.isSelected = message->selectItem.index == inspector->selectedElement;
		return ES_HANDLED;
	}

	return 0;
}

int InspectorContentTextboxCallback(EsElement *element, EsMessage *message) {
	InspectorWindow *inspector = (InspectorWindow *) element->instance;

	if (message->type == ES_MSG_TEXTBOX_EDIT_END) {
		size_t newContentBytes;
		char *newContent = EsTextboxGetContents(inspector->contentTextbox, &newContentBytes);
		EsElement *e = inspector->elements[inspector->selectedElement].element;

		if (e->messageClass == ProcessButtonMessage) {
			EsButton *button = (EsButton *) e;
			HeapDuplicate((void **) &button->label, &button->labelBytes, newContent, newContentBytes);
		} else if (e->messageClass == ProcessTextDisplayMessage) {
			EsTextDisplay *display = (EsTextDisplay *) e;
			EsTextDisplaySetContents(display, newContent, newContentBytes);
		} else {
			EsAssert(false);
		}

		EsElementUpdateContentSize(e);
		if (e->parent) EsElementUpdateContentSize(e->parent);
		EsHeapFree(newContent);
		return ES_HANDLED;
	}

	return 0;
}

int InspectorTextboxCategoryFilterCallback(EsElement *element, EsMessage *message) {
	InspectorWindow *inspector = (InspectorWindow *) element->instance;
	
	if (message->type == ES_MSG_TEXTBOX_UPDATED) {
		EsHeapFree(inspector->cCategoryFilter);
		inspector->cCategoryFilter = EsTextboxGetContents((EsTextbox *) element);
	}

	return 0;
}

InspectorWindow *InspectorGet(EsElement *element) {
	if (!element->window || !element->instance) return nullptr;
	APIInstance *instance = (APIInstance *) element->instance->_private;
	InspectorWindow *inspector = instance->attachedInspector;
	if (!inspector || inspector->instance->window != element->window) return nullptr;
	return inspector;
}

void InspectorNotifyElementEvent(EsElement *element, const char *cCategory, const char *cFormat, ...) {
	if (~element->state & UI_STATE_INSPECTING) return;
	InspectorWindow *inspector = InspectorGet(element);
	if (!inspector) return;
	if (inspector->cCategoryFilter && inspector->cCategoryFilter[0] && cCategory && EsCRTstrcmp(cCategory, inspector->cCategoryFilter)) return;
	va_list arguments;
	va_start(arguments, cFormat);
	char _buffer[256];
	EsBuffer buffer = { .out = (uint8_t *) _buffer, .bytes = sizeof(_buffer) };
	if (cCategory) EsBufferFormat(&buffer, "%z: ", cCategory);
	EsBufferFormatV(&buffer, cFormat, arguments); 
	va_end(arguments);
	EsListViewIndex index = EsListViewFixedItemInsert(inspector->listEvents);
	EsListViewFixedItemSetString(inspector->listEvents, index, 0, _buffer, buffer.position);
	EsListViewScrollToEnd(inspector->listEvents);
}

void InspectorNotifyElementContentChanged(EsElement *element) {
	InspectorWindow *inspector = InspectorGet(element);
	if (!inspector) return;

	for (uintptr_t i = 0; i < inspector->elements.Length(); i++) {
		if (inspector->elements[i].element == element) {
			EsListViewInvalidateContent(inspector->elementList, 0, i);
			return;
		}
	}

	EsAssert(false);
}

void InspectorNotifyElementMoved(EsElement *element, EsRectangle takenBounds) {
	InspectorWindow *inspector = InspectorGet(element);
	if (!inspector) return;

	for (uintptr_t i = 0; i < inspector->elements.Length(); i++) {
		if (inspector->elements[i].element == element) {
			inspector->elements[i].takenBounds = takenBounds;
			inspector->elements[i].givenBounds = takenBounds; // TODO.
			EsListViewInvalidateContent(inspector->elementList, 0, i);
			return;
		}
	}

	EsAssert(false);
}

void InspectorNotifyElementDestroyed(EsElement *element) {
	InspectorWindow *inspector = InspectorGet(element);
	if (!inspector) return;

	for (uintptr_t i = 0; i < inspector->elements.Length(); i++) {
		if (inspector->elements[i].element == element) {
			if (inspector->selectedElement == (intptr_t) i) {
				inspector->selectedElement = -1;
				InspectorUpdateEditor(inspector);
			} else if (inspector->selectedElement > (intptr_t) i) {
				inspector->selectedElement--;
			}

			EsListViewRemove(inspector->elementList, 0, i, 1);
			inspector->elements.Delete(i);
			return;
		}
	}

	EsAssert(false);
}

void InspectorNotifyElementCreated(EsElement *element) {
	InspectorWindow *inspector = InspectorGet(element);
	if (!inspector) return;

	ptrdiff_t indexInParent = -1;

	for (uintptr_t i = 0; i < element->parent->children.Length(); i++) {
		if (element->parent->children[i] == element) {
			indexInParent = i;
			break;
		}
	}

	EsAssert(indexInParent != -1);

	ptrdiff_t insertAfterIndex = -1;

	for (uintptr_t i = 0; i < inspector->elements.Length(); i++) {
		if (indexInParent == 0) {
			if (inspector->elements[i].element == element->parent) {
				insertAfterIndex = i;
				break;
			}
		} else {
			if (inspector->elements[i].element == element->parent->children[indexInParent - 1]) {
				insertAfterIndex = i;
				int baseDepth = inspector->elements[i++].depth;

				for (; i < inspector->elements.Length(); i++) {
					if (inspector->elements[i].depth > baseDepth) {
						insertAfterIndex++;
					} else {
						break;
					}
				}

				break;
			}
		}
	}

	EsAssert(insertAfterIndex != -1);

	int depth = 0;
	EsElement *ancestor = element->parent;

	while (ancestor) {
		depth++;
		ancestor = ancestor->parent;
	}

	if (inspector->selectedElement > insertAfterIndex) {
		inspector->selectedElement++;
	}

	InspectorElementEntry entry;
	entry.element = element;
	entry.depth = depth;
	inspector->elements.Insert(entry, insertAfterIndex + 1);
	EsListViewInsert(inspector->elementList, 0, insertAfterIndex + 1, 1);
}

void InspectorFindElementsRecursively(InspectorWindow *inspector, EsElement *element, int depth) {
	InspectorElementEntry entry = {};
	entry.element = element;
	entry.depth = depth;
	inspector->elements.Add(entry);

	for (uintptr_t i = 0; i < element->children.Length(); i++) {
		InspectorFindElementsRecursively(inspector, element->children[i], depth + 1);
	}
}

void InspectorRefreshElementList(InspectorWindow *inspector) {
	EsListViewRemoveAll(inspector->elementList, 0);
	inspector->elements.Free();
	InspectorFindElementsRecursively(inspector, inspector->instance->window, 0);
	EsListViewInsert(inspector->elementList, 0, 0, inspector->elements.Length());
}

void InspectorNotifyElementPainted(EsElement *element, EsPainter *painter) {
	InspectorWindow *inspector = InspectorGet(element);
	if (!inspector) return;

	InspectorElementEntry *entry = inspector->hoveredElement.element ? &inspector->hoveredElement : nullptr;
	if (!entry) return;

	EsRectangle bounds = ES_RECT_4(painter->offsetX, painter->offsetX + painter->width, 
			painter->offsetY, painter->offsetY + painter->height);

	if (entry->element == element) {
		EsDrawRectangle(painter, bounds, 0x607F7FFF, 0x60FFFF7F, element->style->insets);
	} else if (entry->element->parent == element) {
		if ((element->flags & ES_CELL_FILL) != ES_CELL_FILL) {
			EsRectangle rectangle = entry->givenBounds;
			rectangle.l += bounds.l, rectangle.r += bounds.l;
			rectangle.t += bounds.t, rectangle.b += bounds.t;
			// EsDrawBlock(painter, rectangle, 0x20FF7FFF);
		}
	}
}

#define INSPECTOR_ALIGN_COMMAND(name, clear, set, toggle) \
void name (EsInstance *instance, EsElement *, EsCommand *) { \
	InspectorWindow *inspector = (InspectorWindow *) instance; \
	EsElement *e = inspector->elements[inspector->selectedElement].element; \
	if (toggle) e->flags ^= set; \
	else { e->flags &= ~(clear); e->flags |= set; } \
	EsElementUpdateContentSize(e); \
	if (e->parent) EsElementUpdateContentSize(e->parent); \
	inspector->elementList->Repaint(true); \
	InspectorUpdateEditor(inspector); \
}

INSPECTOR_ALIGN_COMMAND(InspectorHAlignLeft, ES_CELL_H_LEFT | ES_CELL_H_RIGHT, ES_CELL_H_LEFT, false);
INSPECTOR_ALIGN_COMMAND(InspectorHAlignCenter, ES_CELL_H_LEFT | ES_CELL_H_RIGHT, ES_CELL_H_LEFT | ES_CELL_H_RIGHT, false);
INSPECTOR_ALIGN_COMMAND(InspectorHAlignRight, ES_CELL_H_LEFT | ES_CELL_H_RIGHT, ES_CELL_H_RIGHT, false);
INSPECTOR_ALIGN_COMMAND(InspectorHAlignExpand, 0, ES_CELL_H_EXPAND, true);
INSPECTOR_ALIGN_COMMAND(InspectorHAlignShrink, 0, ES_CELL_H_SHRINK, true);
INSPECTOR_ALIGN_COMMAND(InspectorHAlignPush, 0, ES_CELL_H_PUSH, true);
INSPECTOR_ALIGN_COMMAND(InspectorVAlignTop, ES_CELL_V_TOP | ES_CELL_V_BOTTOM, ES_CELL_V_TOP, false);
INSPECTOR_ALIGN_COMMAND(InspectorVAlignCenter, ES_CELL_V_TOP | ES_CELL_V_BOTTOM, ES_CELL_V_TOP | ES_CELL_V_BOTTOM, false);
INSPECTOR_ALIGN_COMMAND(InspectorVAlignBottom, ES_CELL_V_TOP | ES_CELL_V_BOTTOM, ES_CELL_V_BOTTOM, false);
INSPECTOR_ALIGN_COMMAND(InspectorVAlignExpand, 0, ES_CELL_V_EXPAND, true);
INSPECTOR_ALIGN_COMMAND(InspectorVAlignShrink, 0, ES_CELL_V_SHRINK, true);
INSPECTOR_ALIGN_COMMAND(InspectorVAlignPush, 0, ES_CELL_V_PUSH, true);
INSPECTOR_ALIGN_COMMAND(InspectorDirectionLeft, ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE | ES_ELEMENT_LAYOUT_HINT_HORIZONTAL | ES_ELEMENT_LAYOUT_HINT_REVERSE, 
		ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE | ES_ELEMENT_LAYOUT_HINT_HORIZONTAL | ES_ELEMENT_LAYOUT_HINT_REVERSE, false);
INSPECTOR_ALIGN_COMMAND(InspectorDirectionRight, ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE | ES_ELEMENT_LAYOUT_HINT_HORIZONTAL | ES_ELEMENT_LAYOUT_HINT_REVERSE, 
		ES_PANEL_HORIZONTAL | ES_ELEMENT_LAYOUT_HINT_HORIZONTAL, false);
INSPECTOR_ALIGN_COMMAND(InspectorDirectionUp, ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE | ES_ELEMENT_LAYOUT_HINT_HORIZONTAL | ES_ELEMENT_LAYOUT_HINT_REVERSE, 
		ES_PANEL_REVERSE | ES_ELEMENT_LAYOUT_HINT_REVERSE, false);
INSPECTOR_ALIGN_COMMAND(InspectorDirectionDown, ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE | ES_ELEMENT_LAYOUT_HINT_HORIZONTAL | ES_ELEMENT_LAYOUT_HINT_REVERSE, 0, false);

void InspectorVisualizeRepaints(EsInstance *instance, EsElement *, EsCommand *) {
	InspectorWindow *inspector = (InspectorWindow *) instance;
	EsWindow *window = inspector->instance->window;
	window->visualizeRepaints = !window->visualizeRepaints;
	EsButtonSetCheck(inspector->visualizeRepaints, window->visualizeRepaints ? ES_CHECK_CHECKED : ES_CHECK_UNCHECKED, false);
}

void InspectorVisualizePaintSteps(EsInstance *instance, EsElement *, EsCommand *) {
	InspectorWindow *inspector = (InspectorWindow *) instance;
	EsWindow *window = inspector->instance->window;
	window->visualizePaintSteps = !window->visualizePaintSteps;
	EsButtonSetCheck(inspector->visualizePaintSteps, window->visualizePaintSteps ? ES_CHECK_CHECKED : ES_CHECK_UNCHECKED, false);
}

void InspectorVisualizeLayoutBounds(EsInstance *instance, EsElement *, EsCommand *) {
	InspectorWindow *inspector = (InspectorWindow *) instance;
	EsWindow *window = inspector->instance->window;
	window->visualizeLayoutBounds = !window->visualizeLayoutBounds;
	EsButtonSetCheck(inspector->visualizeLayoutBounds, window->visualizeLayoutBounds ? ES_CHECK_CHECKED : ES_CHECK_UNCHECKED, false);
	EsElementRepaint(window);
}

void InspectorAddElement2(EsMenu *menu, EsGeneric context) {
	InspectorWindow *inspector = (InspectorWindow *) menu->instance; 
	if (inspector->selectedElement == -1) return;
	EsElement *e = inspector->elements[inspector->selectedElement].element; 
	int asSibling = context.u & 0x80;
	context.u &= ~0x80;

	if (asSibling) {
		EsElementInsertAfter(e); 
		e = e->parent;
	}

	if (context.u == 1) {
		EsButtonCreate(e);
	} else if (context.u == 2) {
		EsPanelCreate(e);
	} else if (context.u == 3) {
		EsSpacerCreate(e);
	} else if (context.u == 4) {
		EsTextboxCreate(e);
	} else if (context.u == 5) {
		EsTextDisplayCreate(e);
	}
}

void InspectorAddElement(EsInstance *, EsElement *element, EsCommand *) {
	EsMenu *menu = EsMenuCreate(element, ES_FLAGS_DEFAULT);
	EsMenuAddItem(menu, 0, "Add button", -1, InspectorAddElement2, element->userData.u | 1);
	EsMenuAddItem(menu, 0, "Add panel", -1, InspectorAddElement2, element->userData.u | 2);
	EsMenuAddItem(menu, 0, "Add spacer", -1, InspectorAddElement2, element->userData.u | 3);
	EsMenuAddItem(menu, 0, "Add textbox", -1, InspectorAddElement2, element->userData.u | 4);
	EsMenuAddItem(menu, 0, "Add text display", -1, InspectorAddElement2, element->userData.u | 5);
	EsMenuShow(menu);
}

void InspectorSetup(EsWindow *window) {
	InspectorWindow *inspector = (InspectorWindow *) EsHeapAllocate(sizeof(InspectorWindow), true); // TODO Freeing this.
	inspector->window = window;
	InstanceSetup(inspector);
	EsInstanceOpenReference(inspector);
	
	inspector->instance = window->instance;
	window->instance = inspector;

	inspector->selectedElement = -1;

	EsSplitter *splitter = EsSplitterCreate(window, ES_CELL_FILL | ES_SPLITTER_VERTICAL);
	EsPanel *panel1 = EsPanelCreate(splitter, ES_CELL_FILL, ES_STYLE_PANEL_FILLED);
	EsPanel *panel2 = EsPanelCreate(splitter, ES_CELL_FILL, ES_STYLE_PANEL_FILLED);

	{
		EsPanel *toolbar = EsPanelCreate(panel1, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_TOOLBAR);
		inspector->visualizeRepaints = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR, 0, "Visualize repaints");
		EsButtonOnCommand(inspector->visualizeRepaints, InspectorVisualizeRepaints);
		inspector->visualizeLayoutBounds = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR, 0, "Visualize layout bounds");
		EsButtonOnCommand(inspector->visualizeLayoutBounds, InspectorVisualizeLayoutBounds);
		inspector->visualizePaintSteps = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR, 0, "Visualize paint steps");
		EsButtonOnCommand(inspector->visualizePaintSteps, InspectorVisualizePaintSteps);
		EsSpacerCreate(toolbar, ES_CELL_H_FILL);
	}

	inspector->elementList = EsListViewCreate(panel1, ES_CELL_FILL | ES_LIST_VIEW_COLUMNS | ES_LIST_VIEW_SINGLE_SELECT);
	inspector->elementList->messageUser = InspectorElementListCallback;
	EsListViewRegisterColumn(inspector->elementList, 0, "Name", -1, 0, 300);
	EsListViewRegisterColumn(inspector->elementList, 1, "Bounds", -1, 0, 200);
	EsListViewRegisterColumn(inspector->elementList, 2, "Information", -1, 0, 200);
	EsListViewAddAllColumns(inspector->elementList);
	EsListViewInsertGroup(inspector->elementList, 0);

	{
		EsPanel *toolbar = EsPanelCreate(panel1, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_TOOLBAR);
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		EsTextDisplayCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, "Horizontal:");
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		inspector->alignH[0] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->alignH[0], ES_ICON_ALIGN_HORIZONTAL_LEFT);
		EsButtonOnCommand(inspector->alignH[0], InspectorHAlignLeft);
		inspector->alignH[1] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->alignH[1], ES_ICON_ALIGN_HORIZONTAL_CENTER);
		EsButtonOnCommand(inspector->alignH[1], InspectorHAlignCenter);
		inspector->alignH[2] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->alignH[2], ES_ICON_ALIGN_HORIZONTAL_RIGHT);
		EsButtonOnCommand(inspector->alignH[2], InspectorHAlignRight);
		inspector->alignH[3] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED, 0, "Expand");
		EsButtonOnCommand(inspector->alignH[3], InspectorHAlignExpand);
		inspector->alignH[4] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED, 0, "Shrink");
		EsButtonOnCommand(inspector->alignH[4], InspectorHAlignShrink);
		inspector->alignH[5] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED, 0, "Push");
		EsButtonOnCommand(inspector->alignH[5], InspectorHAlignPush);
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		EsTextDisplayCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, "Vertical:");
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		inspector->alignV[0] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->alignV[0], ES_ICON_ALIGN_VERTICAL_TOP);
		EsButtonOnCommand(inspector->alignV[0], InspectorVAlignTop);
		inspector->alignV[1] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->alignV[1], ES_ICON_ALIGN_VERTICAL_CENTER);
		EsButtonOnCommand(inspector->alignV[1], InspectorVAlignCenter);
		inspector->alignV[2] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->alignV[2], ES_ICON_ALIGN_VERTICAL_BOTTOM);
		EsButtonOnCommand(inspector->alignV[2], InspectorVAlignBottom);
		inspector->alignV[3] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED, 0, "Expand");
		EsButtonOnCommand(inspector->alignV[3], InspectorVAlignExpand);
		inspector->alignV[4] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED, 0, "Shrink");
		EsButtonOnCommand(inspector->alignV[4], InspectorVAlignShrink);
		inspector->alignV[5] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED, 0, "Push");
		EsButtonOnCommand(inspector->alignV[5], InspectorVAlignPush);
	}

	{
		EsPanel *toolbar = EsPanelCreate(panel1, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_TOOLBAR);
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		EsTextDisplayCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, "Stack:");
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		inspector->direction[0] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->direction[0], ES_ICON_GO_PREVIOUS);
		EsButtonOnCommand(inspector->direction[0], InspectorDirectionLeft);
		inspector->direction[1] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->direction[1], ES_ICON_GO_NEXT);
		EsButtonOnCommand(inspector->direction[1], InspectorDirectionRight);
		inspector->direction[2] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->direction[2], ES_ICON_GO_UP);
		EsButtonOnCommand(inspector->direction[2], InspectorDirectionUp);
		inspector->direction[3] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->direction[3], ES_ICON_GO_DOWN);
		EsButtonOnCommand(inspector->direction[3], InspectorDirectionDown);
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 25, 0);
		inspector->addChildButton = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_BUTTON_DROPDOWN | ES_ELEMENT_DISABLED | ES_BUTTON_COMPACT, nullptr, "Add child... ");
		EsButtonOnCommand(inspector->addChildButton, InspectorAddElement);
		inspector->addSiblingButton = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_BUTTON_DROPDOWN | ES_ELEMENT_DISABLED | ES_BUTTON_COMPACT, nullptr, "Add sibling... ");
		inspector->addSiblingButton->userData.i = 0x80;
		EsButtonOnCommand(inspector->addSiblingButton, InspectorAddElement);
	}

	{
		EsPanel *toolbar = EsPanelCreate(panel1, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_TOOLBAR);
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		EsTextDisplayCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, "Content:");
		inspector->contentTextbox = EsTextboxCreate(toolbar, ES_ELEMENT_DISABLED | ES_TEXTBOX_EDIT_BASED);
		inspector->contentTextbox->messageUser = InspectorContentTextboxCallback;
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 25, 0);
		EsTextDisplayCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, "Event category filter:");
		inspector->textboxCategoryFilter = EsTextboxCreate(toolbar, ES_ELEMENT_DISABLED);
		inspector->textboxCategoryFilter->messageUser = InspectorTextboxCategoryFilterCallback;
	}

	{
		inspector->listEvents = EsListViewCreate(panel2, ES_CELL_FILL | ES_LIST_VIEW_CHOICE_SELECT | ES_LIST_VIEW_FIXED_ITEMS, ES_STYLE_LIST_CHOICE_BORDERED);
	}

	InspectorRefreshElementList(inspector);

	APIInstance *instance = (APIInstance *) inspector->instance->_private;
	instance->attachedInspector = inspector;
}
