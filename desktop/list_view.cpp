// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO RMB click/drag.
// TODO Consistent int64_t/intptr_t.
// TODO Drag and drop.
// TODO GetFirstIndex/GetLastIndex assume that every group is non-empty.
// TODO Sticking to top/bottom scroll when inserting/removing space.
// TODO Audit usage of MeasureItems -- it doesn't take into account the gap between items!

struct ListViewItemElement : EsElement {
	uintptr_t index; // Index into the visible items array.
};

struct ListViewItem {
	ListViewItemElement *element;
	EsListViewIndex group;
	int32_t size;
	EsListViewIndex index;
	uint8_t indent;
	bool startAtSecondColumn;
	bool isHeader, isFooter;
	bool showSearchHighlight;
};

struct ListViewGroup {
	// TODO Empty groups.
	EsListViewIndex itemCount;
	int64_t totalSize;
	uint32_t flags;
	bool initialised;
};

struct ListViewFixedString {
	char *string;
	size_t bytes;
};

struct ListViewFixedItem {
	EsGeneric data;
	uint32_t iconID;
};

struct ListViewFixedItemData {
	union {
		ListViewFixedString s;
		double d;
		int64_t i;
	};
};

struct ListViewColumn {
	char *title;
	size_t titleBytes;
	uint32_t id;
	uint32_t flags;
	double width;
	Array<ListViewFixedItemData> items;
	const EsListViewEnumString *enumStrings;
	size_t enumStringCount;
};

typedef void (*ListViewSortFunction)(EsListViewIndex *, size_t, ListViewColumn *);

int ListViewProcessItemMessage(EsElement *element, EsMessage *message);
void ListViewSetSortAscending(EsMenu *menu, EsGeneric context);
void ListViewSetSortDescending(EsMenu *menu, EsGeneric context);
void ListViewPopulateActionCallback(EsElement *element, EsGeneric);
void ListViewEnsureVisibleActionCallback(EsElement *element, EsGeneric);

struct EsListView : EsElement {
	ScrollPane scroll;

	uint64_t totalItemCount;
	uint64_t totalSize;
	Array<ListViewGroup> groups;
	Array<ListViewItem> visibleItems;

	const EsStyle *itemStyle, *headerItemStyle, *footerItemStyle;
	int64_t fixedWidth, fixedHeight;
	int64_t fixedHeaderSize, fixedFooterSize;

	UIStyle *primaryCellStyle;
	UIStyle *secondaryCellStyle;
	UIStyle *selectedCellStyle;

	bool hasFocusedItem;
	EsListViewIndex focusedItemGroup;
	EsListViewIndex focusedItemIndex;

	bool hasAnchorItem;
	EsListViewIndex anchorItemGroup;
	EsListViewIndex anchorItemIndex;

	bool hasScrollItem; // Used to preserve the scroll position when resizing a wrapped list view.
	bool useScrollItem;
	int64_t scrollItemOffset;
	EsListViewIndex scrollItemGroup;
	EsListViewIndex scrollItemIndex;

#define ENSURE_VISIBLE_ALIGN_TOP             (1 << 0)
#define ENSURE_VISIBLE_ALIGN_CENTER          (1 << 1)
#define ENSURE_VISIBLE_ALIGN_FOR_SCROLL_ITEM (1 << 2)
	EsListViewIndex ensureVisibleGroupIndex;
	EsListViewIndex ensureVisibleIndex;
	uint8_t ensureVisibleFlags;
	bool ensureVisibleQueued;
	bool populateQueued;

	// Valid only during Z-order messages.
	Array<EsElement *> zOrderItems;

	EsElement *selectionBox;
	bool hasSelectionBoxAnchor;
	int64_t selectionBoxAnchorX, selectionBoxAnchorY,
		selectionBoxPositionX, selectionBoxPositionY;

	bool firstLayout;

	char searchBuffer[64];
	size_t searchBufferBytes;
	uint64_t searchBufferLastKeyTime;

	char *emptyMessage;
	size_t emptyMessageBytes;

	EsElement *columnHeader;
	Array<ListViewColumn> registeredColumns;
	Array<uint32_t> activeColumns; // Indices into registeredColumns.
	int columnResizingOriginalWidth;
	int64_t totalColumnWidth;

	EsTextbox *inlineTextbox;
	EsListViewIndex inlineTextboxGroup;
	EsListViewIndex inlineTextboxIndex;

	int maximumItemsPerBand;

	// Fixed item storage:
	Array<ListViewFixedItem> fixedItems;
	Array<EsListViewIndex> fixedItemIndices; // For sorting. Converts the actual list index into an index for fixedItems.
	ptrdiff_t fixedItemSelection;
	uint32_t fixedItemSortColumnID;
#define LIST_SORT_DIRECTION_ASCENDING (1)
#define LIST_SORT_DIRECTION_DESCENDING (2)
	uint8_t fixedItemSortDirection;

	inline EsRectangle GetListBounds() {
		EsRectangle bounds = GetBounds();

		if (columnHeader) {
			bounds.t += columnHeader->style->preferredHeight;
		}

		return bounds;
	}

	inline void GetFirstIndex(EsMessage *message) {
		EsAssert(message->iterateIndex.group < (EsListViewIndex) groups.Length()); // Invalid group index.
		EsAssert(groups[message->iterateIndex.group].itemCount); // No items in the group.
		message->iterateIndex.index = 0;
	}

	inline void GetLastIndex(EsMessage *message) {
		EsAssert(message->iterateIndex.group < (EsListViewIndex) groups.Length()); // Invalid group index.
		EsAssert(groups[message->iterateIndex.group].itemCount); // No items in the group.
		message->iterateIndex.index = groups[message->iterateIndex.group].itemCount - 1;
	}

	inline bool IterateForwards(EsMessage *message) {
		if (message->iterateIndex.index == groups[message->iterateIndex.group].itemCount - 1) {
			if (message->iterateIndex.group == (EsListViewIndex) groups.Length() - 1) {
				return false;
			}

			message->iterateIndex.group++;
			message->iterateIndex.index = 0;
		} else {
			message->iterateIndex.index++;
		}

		return true;
	}

	inline bool IterateBackwards(EsMessage *message) {
		if (message->iterateIndex.index == 0) {
			if (message->iterateIndex.group == 0) {
				return false;
			}

			message->iterateIndex.group--;
			message->iterateIndex.index = groups[message->iterateIndex.group].itemCount - 1;
		} else {
			message->iterateIndex.index--;
		}

		return true;
	}

	int64_t MeasureItems(EsListViewIndex groupIndex, EsListViewIndex firstIndex, EsListViewIndex count) {
		if (count == 0) return 0;
		EsAssert(count > 0);

		bool variableSize = flags & ES_LIST_VIEW_VARIABLE_SIZE;

		if (!variableSize) {
			ListViewGroup *group = &groups[groupIndex];
			int64_t normalCount = count;
			int64_t additionalSize = 0;

			if ((group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) && firstIndex == 0) {
				normalCount--;
				additionalSize += fixedHeaderSize;
			}

			if ((group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) && firstIndex + count == group->itemCount) {
				normalCount--;
				additionalSize += fixedFooterSize;
			}

			return additionalSize + normalCount * (flags & ES_LIST_VIEW_HORIZONTAL ? fixedWidth : fixedHeight);
		}

		if (count > 1) {
			EsMessage m = { ES_MSG_LIST_VIEW_MEASURE_RANGE };
			m.itemRange.group = groupIndex;
			m.itemRange.firstIndex = firstIndex;
			m.itemRange.count = count;

			if (ES_HANDLED == EsMessageSend(this, &m)) {
				return m.itemRange.result;
			}
		}

		EsMessage m = {};
		m.iterateIndex.group = groupIndex;
		m.iterateIndex.index = firstIndex;
		int64_t total = 0;
		int64_t _count = 0;

		while (true) {
			EsMessage m2 = { ES_MSG_LIST_VIEW_MEASURE_ITEM };
			m2.measureItem.group = groupIndex;
			m2.measureItem.index = m.iterateIndex.index;
			EsAssert(ES_HANDLED == EsMessageSend(this, &m2)); // Variable height list view must be able to measure items.
			total += m2.measureItem.result;
			_count++;

			if (count == _count) {
				return total;
			}

			IterateForwards(&m);
			EsAssert(groupIndex == m.iterateIndex.group); // Index range did not exist in group.
		}
	}

	void GetItemPosition(EsListViewIndex groupIndex, EsListViewIndex index, int64_t *_position, int64_t *_itemSize) {
		int64_t gapBetweenGroup = style->gapMajor,
			gapBetweenItems = (flags & ES_LIST_VIEW_TILED) ? style->gapWrap : style->gapMinor,
			fixedSize       = (flags & ES_LIST_VIEW_VARIABLE_SIZE) ? 0 : (flags & ES_LIST_VIEW_HORIZONTAL ? fixedWidth : fixedHeight),
			startInset 	= flags & ES_LIST_VIEW_HORIZONTAL ? style->insets.l : style->insets.t;

		int64_t position = (flags & ES_LIST_VIEW_HORIZONTAL ? -scroll.position[0] : -scroll.position[1]) + startInset, 
			itemSize = 0;

		EsListViewIndex targetIndex = index;

		for (EsListViewIndex i = 0; i < groupIndex; i++) {
			position += groups[i].totalSize + gapBetweenGroup;
		}

		ListViewGroup *group = &groups[groupIndex];

		if ((group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) && index == 0) {
			itemSize = fixedHeaderSize;
		} else if ((group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) && index == group->itemCount - 1) {
			position += group->totalSize - fixedFooterSize;
			itemSize = fixedFooterSize;
		} else if (~flags & ES_LIST_VIEW_VARIABLE_SIZE) {
			intptr_t linearIndex = index;

			if (group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) {
				linearIndex--;
				position += fixedHeaderSize + gapBetweenItems;
			}

			linearIndex /= GetItemsPerBand();
			position += (fixedSize + gapBetweenItems) * linearIndex;
			itemSize = fixedSize;
		} else {
			EsAssert(~flags & ES_LIST_VIEW_TILED); // Tiled list views must be fixed-size.

			EsMessage index = {};
			index.type = ES_MSG_LIST_VIEW_FIND_POSITION;
			index.iterateIndex.group = groupIndex;
			index.iterateIndex.index = targetIndex;

			if (ES_HANDLED == EsMessageSend(this, &index)) {
				position += index.iterateIndex.position;
			} else {
				if (group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) {
					position += fixedHeaderSize + gapBetweenItems;
				}

				bool forwards;
				ListViewItem *reference = visibleItems.Length() ? visibleItems.array : nullptr;

				bool closerToStartThanReference = reference && targetIndex < reference->index / 2;
				bool closerToEndThanReference   = reference && targetIndex > reference->index / 2 + (intptr_t) group->itemCount / 2;

				if (reference && reference->group == groupIndex && !closerToStartThanReference && !closerToEndThanReference) {
					index.iterateIndex.index = reference->index;
					position = (flags & ES_LIST_VIEW_HORIZONTAL) ? reference->element->offsetX : reference->element->offsetY;
					forwards = reference->index < targetIndex;

					EsMessage firstIndex = {};
					firstIndex.iterateIndex.group = groupIndex;
					GetFirstIndex(&firstIndex);

					if (index.iterateIndex.index == firstIndex.iterateIndex.index) {
						forwards = true;
					}
				} else if (targetIndex > group->itemCount / 2) {
					GetLastIndex(&index); 
					position += group->totalSize;
					position -= MeasureItems(index.iterateIndex.group, index.iterateIndex.index, 1);
					forwards = false;
				} else {
					GetFirstIndex(&index); 
					forwards = true;
				}

				while (index.iterateIndex.index != targetIndex) {
					int64_t size = MeasureItems(index.iterateIndex.group, index.iterateIndex.index, 1);
					position += forwards ? (size + gapBetweenItems) : -(size + gapBetweenItems);
					EsAssert((forwards ? IterateForwards(&index) : IterateBackwards(&index)) && index.iterateIndex.group == groupIndex);
							// Could not find the item in the group.
				}

				itemSize = MeasureItems(index.iterateIndex.group, index.iterateIndex.index, 1);
			}
		}

		*_position = position;
		*_itemSize = itemSize;
	}

	void EnsureItemVisible(EsListViewIndex groupIndex, EsListViewIndex index, uint8_t visibleFlags) {
		ensureVisibleGroupIndex = groupIndex;
		ensureVisibleIndex = index;
		ensureVisibleFlags = visibleFlags;

		if (!ensureVisibleQueued) {
			UpdateAction action = {};
			action.element = this;
			action.callback = ListViewEnsureVisibleActionCallback;
			window->updateActions.Add(action);
			ensureVisibleQueued = true;
		}
	}

	void _EnsureItemVisible(EsListViewIndex groupIndex, EsListViewIndex index, uint8_t visibleFlags) {
		EsRectangle contentBounds = GetListBounds();

		int64_t startInset = flags & ES_LIST_VIEW_HORIZONTAL ? style->insets.l : style->insets.t,
			endInset = flags & ES_LIST_VIEW_HORIZONTAL ? style->insets.r : style->insets.b,
			contentSize = flags & ES_LIST_VIEW_HORIZONTAL ? Width(contentBounds) : Height(contentBounds);

		int64_t position, itemSize;
		GetItemPosition(groupIndex, index, &position, &itemSize);

		if (visibleFlags & ENSURE_VISIBLE_ALIGN_FOR_SCROLL_ITEM) {
			if (flags & ES_LIST_VIEW_HORIZONTAL) {
				scroll.SetX(scroll.position[0] + position - scrollItemOffset);
			} else {
				scroll.SetY(scroll.position[1] + position - scrollItemOffset);
			}

			useScrollItem = true;
			return;
		}

		if (position >= 0 && position + itemSize <= contentSize - endInset) {
			return;
		}

		if (visibleFlags & ENSURE_VISIBLE_ALIGN_TOP) {
			if (flags & ES_LIST_VIEW_HORIZONTAL) {
				scroll.SetX(scroll.position[0] + position - startInset);
			} else {
				scroll.SetY(scroll.position[1] + position - startInset);
			}
		} else if (visibleFlags & ENSURE_VISIBLE_ALIGN_CENTER) {
			if (flags & ES_LIST_VIEW_HORIZONTAL) {
				scroll.SetX(scroll.position[0] + position + itemSize / 2 - contentSize / 2);
			} else {
				scroll.SetY(scroll.position[1] + position + itemSize / 2 - contentSize / 2);
			}
		} else {
			if (flags & ES_LIST_VIEW_HORIZONTAL) {
				scroll.SetX(scroll.position[0] + position + itemSize - contentSize + endInset);
			} else {
				scroll.SetY(scroll.position[1] + position + itemSize - contentSize + endInset);
			}
		}
	}

	EsMessage FindFirstVisibleItem(int64_t *_position, int64_t position, ListViewItem *reference, bool *noItems) {
		int64_t gapBetweenGroup = style->gapMajor,
			gapBetweenItems = (flags & ES_LIST_VIEW_TILED) ? style->gapWrap : style->gapMinor,
			fixedSize       = (flags & ES_LIST_VIEW_VARIABLE_SIZE) ? 0 : (flags & ES_LIST_VIEW_HORIZONTAL ? fixedWidth : fixedHeight);
		
		// Find the group.
		// TODO Faster searching when there are many groups.

		EsListViewIndex groupIndex = 0;
		bool foundGroup = false;

		for (; groupIndex < (EsListViewIndex) groups.Length(); groupIndex++) {
			ListViewGroup *group = &groups[groupIndex];
			int64_t totalSize = group->totalSize;

			if (position + totalSize > 0) {
				foundGroup = true;
				break;
			}

			position += totalSize + gapBetweenGroup;
		}

		if (!foundGroup) {
			if (noItems) {
				*noItems = true;
				return {};
			} else {
				EsAssert(false); // Could not find the first visible item with the given scroll.
			}
		}

		EsMessage index = {};
		index.iterateIndex.group = groupIndex;

		// Can we go directly to the item?

		if (~flags & ES_LIST_VIEW_VARIABLE_SIZE) {
			index.iterateIndex.index = 0;
			intptr_t addHeader = 0;

			ListViewGroup *group = &groups[groupIndex];

			if (group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) {
				if (position + fixedHeaderSize > 0) {
					*_position = position;
					return index;
				}

				position += fixedHeaderSize + gapBetweenItems;
				addHeader = 1;
			}

			EsListViewIndex band = -position / (fixedSize + gapBetweenItems);
			if (band < 0) band = 0;
			position += band * (fixedSize + gapBetweenItems);

			EsListViewIndex itemsPerBand = (flags & ES_LIST_VIEW_TILED) ? GetItemsPerBand() : 1;
			index.iterateIndex.index = band * itemsPerBand + addHeader;

			if (index.iterateIndex.index >= group->itemCount) {
				index.iterateIndex.index = group->itemCount - 1;
				position += (index.iterateIndex.index / itemsPerBand - band) * (fixedSize + gapBetweenItems);
			}

			*_position = position;
			return index;
		}

		EsAssert(~flags & ES_LIST_VIEW_TILED); // Trying to use TILED mode with VARIABLE_SIZE mode.

		// Try asking the application to find the item.

		index.type = ES_MSG_LIST_VIEW_FIND_INDEX;
		index.iterateIndex.position = -position;

		if (ES_HANDLED == EsMessageSend(this, &index)) {
			*_position = -index.iterateIndex.position;
			return index;
		}

		// Find the item within the group, manually.

		bool forwards;

		if (reference && reference->group == groupIndex) {
			int64_t referencePosition = (flags & ES_LIST_VIEW_HORIZONTAL) ? reference->element->offsetX : reference->element->offsetY;

			if (AbsoluteInteger64(referencePosition) < AbsoluteInteger64(position)
					&& AbsoluteInteger64(referencePosition) < AbsoluteInteger64(position + groups[groupIndex].totalSize)) {
				index.iterateIndex.index = reference->index;
				position = referencePosition; // Use previous first visible item as reference.
				forwards = position < 0;

				EsMessage firstIndex = {};
				firstIndex.iterateIndex.group = groupIndex;
				GetFirstIndex(&firstIndex);

				if (index.iterateIndex.index == firstIndex.iterateIndex.index) {
					forwards = true;
				}

				goto gotReference;
			}
		} 
		
		if (position + groups[groupIndex].totalSize / 2 >= 0) {
			GetFirstIndex(&index); // Use start of group as reference.
			forwards = true;
		} else {
			GetLastIndex(&index); // Use end of group as reference
			position += groups[groupIndex].totalSize;
			position -= MeasureItems(index.iterateIndex.group, index.iterateIndex.index, 1);
			forwards = false;
		}

		gotReference:;

		if (forwards) {
			// Iterate forwards from reference point.

			while (true) {
				int64_t size = fixedSize ?: MeasureItems(index.iterateIndex.group, index.iterateIndex.index, 1);

				if (position + size > 0) {
					*_position = position;
					return index;
				}

				EsAssert(IterateForwards(&index) && index.iterateIndex.group == groupIndex);
						// No items in the group are visible. Maybe invalid scroll position?
				position += size + gapBetweenItems;
			}
		} else {
			// Iterate backwards from reference point.

			while (true) {
				if (position <= 0 || !IterateBackwards(&index)) {
					*_position = position;
					return index;
				}

				int64_t size = fixedSize ?: MeasureItems(index.iterateIndex.group, index.iterateIndex.index, 1);
				EsAssert(index.iterateIndex.group == groupIndex);
						// No items in the group are visible. Maybe invalid scroll position?
				position -= size + gapBetweenItems;
			}
		}
	}

	void _Populate() {
		// TODO Keep one item before and after the viewport, so tab traversal on custom elements works.
		// TODO Always keep an item if it has FOCUS_WITHIN.
		// 	- But maybe we shouldn't allow focusable elements in a list view.

		if (!totalItemCount) {
			return;
		}

		EsRectangle contentBounds = GetListBounds();
		int64_t contentSize = flags & ES_LIST_VIEW_HORIZONTAL ? Width(contentBounds) : Height(contentBounds);
		int64_t scroll = EsCRTfloor(flags & ES_LIST_VIEW_HORIZONTAL ? (this->scroll.position[0] - style->insets.l) 
				: (this->scroll.position[1] - style->insets.t));

		int64_t position = 0;
		bool noItems = false;
		EsMessage currentItem = FindFirstVisibleItem(&position, -scroll, visibleItems.Length() ? visibleItems.array : nullptr, &noItems);
		uintptr_t visibleIndex = 0;

		int64_t wrapLimit = GetWrapLimit();
		int64_t fixedMinorSize = (flags & ES_LIST_VIEW_HORIZONTAL) ? fixedHeight : fixedWidth;
		intptr_t itemsPerBand = GetItemsPerBand();
		intptr_t itemInBand = 0;
		int64_t computedMinorGap = style->gapMinor;
		int64_t minorPosition = 0;
		int64_t centerOffset = (flags & ES_LIST_VIEW_CENTER_TILES) 
			? (wrapLimit - itemsPerBand * (fixedMinorSize + style->gapMinor) + style->gapMinor) / 2 : 0;

		while (visibleIndex < visibleItems.Length()) {
			// Remove visible items no longer visible, before the viewport.

			ListViewItem *visibleItem = &visibleItems[visibleIndex];
			int64_t visibleItemPosition = flags & ES_LIST_VIEW_HORIZONTAL ? visibleItem->element->offsetX : visibleItem->element->offsetY;
			if (visibleItemPosition >= position) break;
			visibleItem->element->index = visibleIndex;
			visibleItem->element->Destroy();
			visibleItems.Delete(visibleIndex);
		}

		while (position < contentSize && !noItems) {
			ListViewItem *visibleItem = visibleIndex == visibleItems.Length() ? nullptr : &visibleItems[visibleIndex];

			if (visibleItem && visibleItem->index == currentItem.iterateIndex.index
					&& visibleItem->group == currentItem.iterateIndex.group) {
				// This is already a visible item.

				if (~flags & ES_LIST_VIEW_TILED) {
					int64_t expectedPosition = (flags & ES_LIST_VIEW_HORIZONTAL)
							? visibleItem->element->offsetX - contentBounds.l 
							: visibleItem->element->offsetY - contentBounds.t;

					if (position < expectedPosition - 1 || position > expectedPosition + 1) {
						EsPrint("Item in unexpected position: got %d, should have been %d; index %d, scroll %d.\n", 
								expectedPosition, position, visibleItem->index, scroll);
						EsAssert(false);
					}
				}
			} else {
				// Add a new visible item.

				ListViewItem empty = {};
				visibleItems.Insert(empty, visibleIndex);
				visibleItem = &visibleItems[visibleIndex];

				visibleItem->group = currentItem.iterateIndex.group;
				visibleItem->index = currentItem.iterateIndex.index;
				visibleItem->size = MeasureItems(visibleItem->group, visibleItem->index, 1);

				ListViewGroup *group = &groups[visibleItem->group];
				const EsStyle *style = nullptr;

				if ((group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) && visibleItem->index == 0) {
					style = headerItemStyle;
					visibleItem->isHeader = true;
				} else if ((group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) && visibleItem->index == (intptr_t) group->itemCount - 1) {
					style = footerItemStyle;
					visibleItem->isFooter = true;
				} else {
					if (group->flags & ES_LIST_VIEW_GROUP_INDENT) {
						visibleItem->indent++;
					}

					style = itemStyle;
				}

				visibleItem->element = (ListViewItemElement *) EsHeapAllocate(sizeof(ListViewItemElement), true);
				visibleItem->element->Initialise(this, ES_CELL_FILL, nullptr, style);
				visibleItem->element->index = visibleIndex;
				visibleItem->element->cName = "list view item";

				visibleItem->element->messageClass = ListViewProcessItemMessage;

				if (hasFocusedItem && visibleItem->group == focusedItemGroup && visibleItem->index == focusedItemIndex) {
					visibleItem->element->customStyleState |= THEME_STATE_FOCUSED_ITEM;
				}

				if (state & UI_STATE_FOCUSED) {
					visibleItem->element->customStyleState |= THEME_STATE_LIST_FOCUSED;
				}

				EsMessage m = {};

				m.type = ES_MSG_LIST_VIEW_IS_SELECTED;
				m.selectItem.group = visibleItem->group;
				m.selectItem.index = visibleItem->index;
				EsMessageSend(this, &m);
				if (m.selectItem.isSelected) visibleItem->element->customStyleState |= THEME_STATE_SELECTED;

				m.type = ES_MSG_LIST_VIEW_CREATE_ITEM;
				m.createItem.group = visibleItem->group;
				m.createItem.index = visibleItem->index;
				m.createItem.item = visibleItem->element;
				EsMessageSend(this, &m);

				m.type = ES_MSG_LIST_VIEW_GET_INDENT;
				m.getIndent.group = visibleItem->group;
				m.getIndent.index = visibleItem->index;
				m.getIndent.indent = 0;
				EsMessageSend(this, &m);
				visibleItem->indent += m.getIndent.indent;

				SelectPreview(visibleItems.Length() - 1);
				
				visibleItem->element->MaybeRefreshStyle();
			}

			visibleItem->element->index = visibleIndex;

			// Update the item's position.

			ListViewGroup *group = &groups[visibleItem->group];

			if ((flags & ES_LIST_VIEW_TILED) && !visibleItem->isHeader && !visibleItem->isFooter) {
				if (flags & ES_LIST_VIEW_HORIZONTAL) {
					visibleItem->element->InternalMove(fixedWidth, fixedHeight, 
							position + contentBounds.l, minorPosition + style->insets.t + contentBounds.t + centerOffset);
				} else {
					visibleItem->element->InternalMove(fixedWidth, fixedHeight, 
							minorPosition + style->insets.l + contentBounds.l + centerOffset, position + contentBounds.t);
				}

				minorPosition += computedMinorGap + fixedMinorSize;
				itemInBand++;

				bool endOfGroup = ((group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) && currentItem.iterateIndex.index == group->itemCount - 2)
					|| (currentItem.iterateIndex.index == group->itemCount - 1);

				if (itemInBand == itemsPerBand || endOfGroup) {
					minorPosition = 0;
					itemInBand = 0;
					position += (flags & ES_LIST_VIEW_HORIZONTAL) ? visibleItem->element->width : visibleItem->element->height;
					if (!endOfGroup || (group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER)) position += style->gapWrap;
				}
			} else {
				if (flags & ES_LIST_VIEW_HORIZONTAL) {
					visibleItem->element->InternalMove(
						visibleItem->size, 
						Height(contentBounds) - style->insets.t - style->insets.b - visibleItem->indent * style->gapWrap,
						position + contentBounds.l, 
						style->insets.t - this->scroll.position[1] + visibleItem->indent * style->gapWrap + contentBounds.t);
					position += visibleItem->element->width;
				} else if ((flags & ES_LIST_VIEW_COLUMNS) && ((~flags & ES_LIST_VIEW_CHOICE_SELECT) || (this->scroll.enabled[0]))) {
					int indent = visibleItem->indent * style->gapWrap;
					int firstColumn = activeColumns.Length() ? (registeredColumns[activeColumns[0]].width * theming.scale + secondaryCellStyle->gapMajor) : 0;
					visibleItem->startAtSecondColumn = indent > firstColumn;
					if (indent > firstColumn) indent = firstColumn;
					visibleItem->element->InternalMove(totalColumnWidth - indent, visibleItem->size, 
						indent - this->scroll.position[0] + contentBounds.l + style->insets.l, position + contentBounds.t);
					position += visibleItem->element->height;
				} else {
					int indent = visibleItem->indent * style->gapWrap + style->insets.l;
					visibleItem->element->InternalMove(Width(contentBounds) - indent - style->insets.r, visibleItem->size, 
						indent + contentBounds.l - this->scroll.position[0], position + contentBounds.t);
					position += visibleItem->element->height;
				}

				if ((flags & ES_LIST_VIEW_TILED) && (group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) && currentItem.iterateIndex.index == 0) {
					position += style->gapWrap;
				}
			}

			// Go to the next item.

			visibleIndex++;
			EsListViewIndex previousGroup = currentItem.iterateIndex.group;
			if (!IterateForwards(&currentItem)) break;
			position += previousGroup == currentItem.iterateIndex.group ? (flags & ES_LIST_VIEW_TILED ? 0 : style->gapMinor) : style->gapMajor;
		}

		while (visibleIndex < visibleItems.Length()) {
			// Remove visible items no longer visible, after the viewport.

			ListViewItem *visibleItem = &visibleItems[visibleIndex];
			visibleItem->element->index = visibleIndex;
			visibleItem->element->Destroy();
			visibleItems.Delete(visibleIndex);
		}

		if (inlineTextbox) {
			ListViewItem *item = FindVisibleItem(inlineTextboxGroup, inlineTextboxIndex);
			if (item) MoveInlineTextbox(item);
		}

		if (visibleItems.Length() && (!useScrollItem || !hasScrollItem)) {
			scrollItemGroup = visibleItems.First().group;
			scrollItemIndex = visibleItems.First().index;
			scrollItemOffset = visibleItems.First().element->offsetY;
			hasScrollItem = true;
		}
	}

	void Populate() {
		if (!populateQueued) {
			UpdateAction action = {};
			action.element = this;
			action.callback = ListViewPopulateActionCallback;
			window->updateActions.Add(action);
			populateQueued = true;
		}
	}

	void Wrap(bool sizeChanged) {
		if (~flags & ES_LIST_VIEW_TILED) return;

		totalSize = 0;

		intptr_t itemsPerBand = GetItemsPerBand();

		for (uintptr_t i = 0; i < groups.Length(); i++) {
			ListViewGroup *group = &groups[i];
			int64_t groupSize = 0;

			intptr_t itemCount = group->itemCount;

			if (group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) {
				groupSize += fixedHeaderSize + style->gapWrap;
				itemCount--;
			}

			if (group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) {
				groupSize += fixedFooterSize + style->gapWrap;
				itemCount--;
			}

			intptr_t bandsInGroup = (itemCount + itemsPerBand - 1) / itemsPerBand;
			groupSize += (((flags & ES_LIST_VIEW_HORIZONTAL) ? fixedWidth : fixedHeight) + style->gapWrap) * bandsInGroup;
			groupSize -= style->gapWrap;
			group->totalSize = groupSize;

			totalSize += groupSize + (group == &groups.Last() ? 0 : style->gapMajor);
		}

		scroll.Refresh();

		if (sizeChanged) {
			useScrollItem = true;
		}

		if (useScrollItem && hasScrollItem) {
			EnsureItemVisible(scrollItemGroup, scrollItemIndex, ENSURE_VISIBLE_ALIGN_FOR_SCROLL_ITEM);
		}
	}

	void InsertSpace(int64_t space, uintptr_t beforeItem) {
		if (!space) return;

		if (flags & ES_LIST_VIEW_TILED) {
			EsElementUpdateContentSize(this);
			return;
		}

		totalSize += space;

		for (uintptr_t i = beforeItem; i < visibleItems.Length(); i++) {
			ListViewItem *item = &visibleItems[i];

			if (flags & ES_LIST_VIEW_HORIZONTAL) {
				item->element->offsetX += space;
			} else {
				item->element->offsetY += space;
			}
		}

		useScrollItem = false;
		scroll.Refresh();
		EsElementUpdateContentSize(this);
	}

	void SetSelected(EsListViewIndex fromGroup, EsListViewIndex fromIndex, EsListViewIndex toGroup, EsListViewIndex toIndex, 
			bool select, bool toggle,
			intptr_t period = 0, intptr_t periodBegin = 0, intptr_t periodEnd = 0) {
		if (!select && (flags & ES_LIST_VIEW_CHOICE_SELECT)) {
			return;
		}

		if (!select && (flags & ES_LIST_VIEW_SINGLE_SELECT)) {
			EsMessage m = {};
			m.type = ES_MSG_LIST_VIEW_SELECT;
			m.selectItem.isSelected = false;
			fixedItemSelection = -1;
			EsMessageSend(this, &m);
			return;
		}

		if (fromGroup == toGroup && fromIndex > toIndex) {
			EsListViewIndex temp = fromIndex;
			fromIndex = toIndex;
			toIndex = temp;
		} else if (fromGroup > toGroup) {
			EsListViewIndex temp1 = fromGroup;
			fromGroup = toGroup;
			toGroup = temp1;
			EsListViewIndex temp2 = fromIndex;
			fromIndex = toIndex;
			toIndex = temp2;
		}

		EsMessage start = {}, end = {};
		start.iterateIndex.group = fromGroup;
		end.iterateIndex.group = fromGroup;

		for (; start.iterateIndex.group <= toGroup; start.iterateIndex.group++, end.iterateIndex.group++) {
			if (start.iterateIndex.group == fromGroup) {
				start.iterateIndex.index = fromIndex;
			} else {
				GetFirstIndex(&start);
			}

			if (end.iterateIndex.group == toGroup) {
				end.iterateIndex.index = toIndex;
			} else {
				GetLastIndex(&end);
			}

			EsMessage m = { ES_MSG_LIST_VIEW_SELECT_RANGE };
			m.selectRange.group = start.iterateIndex.group;
			m.selectRange.fromIndex = start.iterateIndex.index;
			m.selectRange.toIndex = end.iterateIndex.index;
			m.selectRange.select = select;
			m.selectRange.toggle = toggle;

			if (!period && 0 != EsMessageSend(this, &m)) {
				continue;
			}

			intptr_t linearIndex = 0;

			while (true) {
				m.selectItem.group = start.iterateIndex.group;
				m.selectItem.index = start.iterateIndex.index;
				m.selectItem.isSelected = select;

				if (period) {
					ListViewGroup *group = &groups[m.selectItem.group];
					intptr_t i = linearIndex;

					if (group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) {
						if (linearIndex == 0) {
							goto process;
						} else {
							i--;
						}
					}

					if (group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) {
						if (linearIndex == (intptr_t) group->itemCount - 1) {
							goto process;
						}
					}

					i %= period;

					if (i < periodBegin || i > periodEnd) {
						goto ignore;
					}
				}

				process:;

				if (toggle) {
					m.type = ES_MSG_LIST_VIEW_IS_SELECTED;
					EsMessageSend(this, &m);
					m.selectItem.isSelected = !m.selectItem.isSelected;
				}

				m.type = ES_MSG_LIST_VIEW_SELECT;

				if (flags & ES_LIST_VIEW_FIXED_ITEMS) {
					fixedItemSelection = m.selectItem.index;
					EsAssert((uintptr_t) m.selectItem.index < fixedItems.Length());
					m.selectItem.index = fixedItemIndices[m.selectItem.index];
				}

				EsMessageSend(this, &m);

				ignore:;

				if (start.iterateIndex.index == end.iterateIndex.index) {
					break;
				}

				IterateForwards(&start);
				linearIndex++;
				EsAssert(start.iterateIndex.group == end.iterateIndex.group); // The from and to selection indices in the group were incorrectly ordered.
			}
		}
	}

	void SelectPreview(intptr_t singleItem = -1) {
		if (!hasSelectionBoxAnchor) {
			return;
		}
		
		int64_t x1 = selectionBoxPositionX, x2 = selectionBoxAnchorX,
			y1 = selectionBoxPositionY, y2 = selectionBoxAnchorY;

		if (x1 > x2) { int64_t temp = x1; x1 = x2; x2 = temp; }
		if (y1 > y2) { int64_t temp = y1; y1 = y2; y2 = temp; }

		x1 -= scroll.position[0], x2 -= scroll.position[0];
		y1 -= scroll.position[1], y2 -= scroll.position[1];

		EsRectangle bounds = GetListBounds();

		if (x1 < -1000) x1 = -1000; 
		if (x2 < -1000) x2 = -1000; 
		if (y1 < -1000) y1 = -1000; 
		if (y2 < -1000) y2 = -1000; 

		if (x1 > bounds.r + 1000) x1 = bounds.r + 1000;
		if (x2 > bounds.r + 1000) x2 = bounds.r + 1000;
		if (y1 > bounds.b + 1000) y1 = bounds.b + 1000;
		if (y2 > bounds.b + 1000) y2 = bounds.b + 1000;

		selectionBox->InternalMove(x2 - x1, y2 - y1, x1, y1);

		for (uintptr_t i = 0; i < visibleItems.Length(); i++) {
			if (singleItem != -1) {
				i = singleItem;
			}

			EsMessage m = { ES_MSG_LIST_VIEW_IS_SELECTED };
			m.selectItem.index = visibleItems[i].index;
			m.selectItem.group = visibleItems[i].group;
			EsMessageSend(this, &m);

			EsElement *item = visibleItems[i].element;

			if (x1 < item->offsetX + item->width && x2 >= item->offsetX && y1 < item->offsetY + item->height && y2 >= item->offsetY) {
				if (EsKeyboardIsCtrlHeld()) {
					m.selectItem.isSelected = !m.selectItem.isSelected;
				} else {
					m.selectItem.isSelected = true;
				}
			}

			if (m.selectItem.isSelected) {
				item->customStyleState |= THEME_STATE_SELECTED;
			} else {
				item->customStyleState &= ~THEME_STATE_SELECTED;
			}

			item->MaybeRefreshStyle();

			if (singleItem != -1) {
				break;
			}
		}
	}

	intptr_t GetItemsPerBand() {
		if (~flags & ES_LIST_VIEW_TILED) {
			return 1;
		} else {
			int64_t wrapLimit = GetWrapLimit();
			int64_t fixedMinorSize = (flags & ES_LIST_VIEW_HORIZONTAL) ? fixedHeight : fixedWidth;
			intptr_t itemsPerBand = fixedMinorSize && ((fixedMinorSize + style->gapMinor) < wrapLimit) 
				? (wrapLimit / (fixedMinorSize + style->gapMinor)) : 1;
			return MinimumInteger(itemsPerBand, maximumItemsPerBand);
		}
	}

	void SelectBox(int64_t x1, int64_t x2, int64_t y1, int64_t y2, bool toggle) {
		if (!totalItemCount) {
			return;
		}

		EsRectangle contentBounds = GetListBounds();
		int64_t offset = 0;
		EsMessage start, end;
		bool noItems = false;

		if (flags & ES_LIST_VIEW_HORIZONTAL) {
			if (y1 >= contentBounds.b - style->insets.b || y2 < contentBounds.t + style->insets.t) {
				return;
			}
		} else if (flags & ES_LIST_VIEW_COLUMNS) {
			if (x1 >= contentBounds.l + style->insets.l + totalColumnWidth || x2 < contentBounds.l + style->insets.l) {
				return;
			}
		} else {
			if (x1 >= contentBounds.r - style->insets.r || x2 < contentBounds.l + style->insets.l) {
				return;
			}
		}

		// TODO Use reference for FindFirstVisibleItem.

		bool adjustStart = false, adjustEnd = false;
		int r1 = (flags & ES_LIST_VIEW_HORIZONTAL) ? style->insets.l - x1 : style->insets.t - y1 + scroll.fixedViewport[1];
		int r2 = (flags & ES_LIST_VIEW_HORIZONTAL) ? style->insets.l - x2 : style->insets.t - y2 + scroll.fixedViewport[1];
		start = FindFirstVisibleItem(&offset, r1, nullptr, &noItems);
		if (noItems) return;
		adjustStart = -offset >= MeasureItems(start.iterateIndex.group, start.iterateIndex.index, 1);
		end = FindFirstVisibleItem(&offset, r2, nullptr, &noItems);
		adjustEnd = !noItems;
		if (noItems) { end.iterateIndex.group = groups.Length() - 1; GetLastIndex(&end); }

		if (flags & ES_LIST_VIEW_TILED) {
			int64_t wrapLimit = GetWrapLimit();
			int64_t fixedMinorSize = (flags & ES_LIST_VIEW_HORIZONTAL) ? fixedHeight : fixedWidth;
			intptr_t itemsPerBand = GetItemsPerBand();
			int64_t centerOffset = (flags & ES_LIST_VIEW_CENTER_TILES) ? (wrapLimit - itemsPerBand * (fixedMinorSize + style->gapMinor) + style->gapMinor) / 2 : 0;
			int64_t minorStartOffset = centerOffset + ((flags & ES_LIST_VIEW_HORIZONTAL) ? style->insets.t : style->insets.l);

			int64_t s0 = (flags & ES_LIST_VIEW_HORIZONTAL) ? y1 : x1;
			int64_t s1 = (flags & ES_LIST_VIEW_HORIZONTAL) ? y2 : x2;

			int64_t startEdge = minorStartOffset;
			int64_t endEdge = minorStartOffset + (fixedMinorSize + style->gapMinor) * itemsPerBand - style->gapMinor;

			if (s1 < startEdge || s0 >= endEdge) return;
			if (s0 < startEdge) s0 = startEdge;
			if (s1 >= endEdge) s1 = endEdge - 1;

			intptr_t startInBand = (s0 - startEdge + style->gapMinor /* round up if in gap */) / (fixedMinorSize + style->gapMinor);
			intptr_t endInBand = (s1 - startEdge) / (fixedMinorSize + style->gapMinor);

			if (startInBand > endInBand) return;

			if (adjustStart) {
				ListViewGroup *group = &groups[start.iterateIndex.group];

				if (((group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) && start.iterateIndex.index == 0)
						|| ((group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) && start.iterateIndex.index == group->itemCount - 1)) {
					IterateForwards(&start);
				} else {
					for (intptr_t i = 0; i < itemsPerBand; i++) {
						if ((group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) && start.iterateIndex.index == group->itemCount - 1) {
							break;
						}

						IterateForwards(&start);
					}
				}
			}

			if (adjustEnd) {
				ListViewGroup *group = &groups[end.iterateIndex.group];

				if (((group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) && end.iterateIndex.index == 0)
						|| ((group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) && end.iterateIndex.index == group->itemCount - 1)) {
				} else {
					for (intptr_t i = 0; i < itemsPerBand - 1; i++) {
						if ((group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) && end.iterateIndex.index == group->itemCount - 1) {
							IterateBackwards(&end);
							break;
						}

						IterateForwards(&end);
					}
				}
			}

			if ((start.iterateIndex.group == end.iterateIndex.group && start.iterateIndex.index > end.iterateIndex.index) 
					|| (start.iterateIndex.group > end.iterateIndex.group)) {
				return;
			}

			SetSelected(start.iterateIndex.group, start.iterateIndex.index, end.iterateIndex.group, end.iterateIndex.index, true, toggle,
					itemsPerBand, startInBand, endInBand);
		} else {
			if (adjustStart) {
				IterateForwards(&start);
			}

			SetSelected(start.iterateIndex.group, start.iterateIndex.index, end.iterateIndex.group, end.iterateIndex.index, true, toggle);
		}
	}

	void UpdateVisibleItemSelectionState(uintptr_t i) {
		EsMessage m = {};
		m.type = ES_MSG_LIST_VIEW_IS_SELECTED;
		m.selectItem.group = visibleItems[i].group;
		m.selectItem.index = visibleItems[i].index;
		EsMessageSend(this, &m);

		if (m.selectItem.isSelected) {
			visibleItems[i].element->customStyleState |=  THEME_STATE_SELECTED;
		} else {
			visibleItems[i].element->customStyleState &= ~THEME_STATE_SELECTED;
		}

		visibleItems[i].element->MaybeRefreshStyle();
	}

	void UpdateVisibleItemsSelectionState() {
		for (uintptr_t i = 0; i < visibleItems.Length(); i++) {
			UpdateVisibleItemSelectionState(i);
		}
	}

	void Select(EsListViewIndex group, EsListViewIndex index, bool range, bool toggle, bool moveAnchorOnly) {
		if ((~flags & ES_LIST_VIEW_SINGLE_SELECT) && (~flags & ES_LIST_VIEW_MULTI_SELECT) && (~flags & ES_LIST_VIEW_CHOICE_SELECT)) {
			return;
		}

		if (!totalItemCount) {
			return;
		}

		if (!hasAnchorItem || (~flags & ES_LIST_VIEW_MULTI_SELECT)) {
			range = false;
		}

		bool emptySpace = false;

		if (group == -1) {
			// Clicked on empty space.
			if (range || toggle) return;
			emptySpace = true;
		}

		if (!range && !emptySpace) {
			hasAnchorItem = true;
			anchorItemGroup = group;
			anchorItemIndex = index;
		}

		if (moveAnchorOnly) {
			return;
		}

		if (!toggle) {
			// Clear existing selection.
			SetSelected(0, 0, groups.Length() - 1, groups.Last().itemCount - 1, false, false);
		}

		if (range) {
			// Select range.
			SetSelected(anchorItemGroup, anchorItemIndex, group, index, true, false);
		} else if (toggle) {
			// Toggle single item.
			SetSelected(group, index, group, index, false, true);
		} else if (!emptySpace) {
			// Select single item.
			SetSelected(group, index, group, index, true, false);
		}

		UpdateVisibleItemsSelectionState();
	}

	int ProcessItemMessage(uintptr_t visibleIndex, EsMessage *message, ListViewItemElement *element) {
		ListViewItem *item = &visibleItems[visibleIndex];

		if (message->type == ES_MSG_PAINT) {
			EsMessage m = { ES_MSG_LIST_VIEW_GET_CONTENT };
			uint8_t _buffer[512];
			EsBuffer buffer = { .out = _buffer, .bytes = sizeof(_buffer) };
			m.getContent.buffer = &buffer;
			m.getContent.index = item->index;
			m.getContent.group = item->group;

			EsTextSelection selection = {};
			selection.hideCaret = true;

			if (searchBufferBytes && item->showSearchHighlight) {
				// TODO We might need to store the matched bytes per item, because of case insensitivity.
				selection.caret1 = searchBufferBytes;
				selection.hideCaret = false;
			}

			if (flags & ES_LIST_VIEW_COLUMNS) {
				EsRectangle bounds = EsRectangleAddBorder(element->GetBounds(), element->style->insets);

				for (uintptr_t i = item->startAtSecondColumn ? 1 : 0; i < activeColumns.Length(); i++) {
					m.getContent.activeColumnIndex = i;
					m.getContent.columnID = registeredColumns[activeColumns[i]].id;
					m.getContent.icon = 0;
					buffer.position = 0;

					bounds.r = bounds.l + registeredColumns[activeColumns[i]].width * theming.scale
						- element->style->insets.r - element->style->insets.l;

					if (i == 0) {
						bounds.r -= item->indent * style->gapWrap;
					}

					EsRectangle drawBounds = { bounds.l + message->painter->offsetX, bounds.r + message->painter->offsetX,
						bounds.t + message->painter->offsetY, bounds.b + message->painter->offsetY };

					if (EsRectangleClip(drawBounds, message->painter->clip, nullptr) 
							&& ES_HANDLED == EsMessageSend(this, &m)) {
						bool useSelectedCellStyle = (item->element->customStyleState & THEME_STATE_SELECTED) && (flags & ES_LIST_VIEW_CHOICE_SELECT);
						UIStyle *style = useSelectedCellStyle ? selectedCellStyle : i ? secondaryCellStyle : primaryCellStyle;
						style->PaintText(message->painter, element, bounds, 
								(char *) _buffer, buffer.position, m.getContent.icon,
								registeredColumns[activeColumns[i]].flags, i ? nullptr : &selection);
					}

					bounds.l += registeredColumns[activeColumns[i]].width * theming.scale + secondaryCellStyle->gapMajor;

					if (i == 0) {
						bounds.l -= item->indent * style->gapWrap;
					}
				}
			} else {
				if (flags & ES_LIST_VIEW_TILED) {
					m.type = ES_MSG_LIST_VIEW_GET_SUMMARY;
					if (ES_HANDLED == EsMessageSend(this, &m)) goto standardPaint;
					m.type = ES_MSG_LIST_VIEW_GET_CONTENT;
				}

				if (ES_HANDLED == EsMessageSend(this, &m)) goto standardPaint;

				return 0;

				standardPaint:;

				if (inlineTextbox && inlineTextboxGroup == item->group && inlineTextboxIndex == item->index) {
					buffer.position = 0;
				}

				EsDrawContent(message->painter, element, element->GetBounds(), 
					(char *) _buffer, buffer.position, m.getContent.icon,
					m.getContent.drawContentFlags, &selection);
			}
		} else if (message->type == ES_MSG_LAYOUT) {
			if (element->GetChildCount()) {
				EsElement *child = element->GetChild(0);
				EsRectangle bounds = element->GetBounds();
				child->InternalMove(bounds.r - bounds.l, bounds.b - bounds.t, bounds.l, bounds.t);
			}
		} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN || message->type == ES_MSG_MOUSE_MIDDLE_CLICK) {
			EsElementFocus(this);

			if (hasFocusedItem) {
				ListViewItem *oldFocus = FindVisibleItem(focusedItemGroup, focusedItemIndex);

				if (oldFocus) {
					oldFocus->element->customStyleState &= ~THEME_STATE_FOCUSED_ITEM;
					oldFocus->element->MaybeRefreshStyle();
				}
			}

			hasFocusedItem = true;
			focusedItemGroup = item->group;
			focusedItemIndex = item->index;
			element->customStyleState |= THEME_STATE_FOCUSED_ITEM;

			if (message->type == ES_MSG_MOUSE_MIDDLE_CLICK) {
				Select(item->group, item->index, false, false, false);
				EsMessage m = { ES_MSG_LIST_VIEW_CHOOSE_ITEM };
				m.chooseItem.group = item->group;
				m.chooseItem.index = item->index;
				m.chooseItem.source = ES_LIST_VIEW_CHOOSE_ITEM_MIDDLE_CLICK;
				EsMessageSend(this, &m);
			} else if (message->mouseDown.clickChainCount == 1 || (~element->customStyleState & THEME_STATE_SELECTED)) {
				Select(item->group, item->index, EsKeyboardIsShiftHeld(), EsKeyboardIsCtrlHeld(), false);
			} else if (message->mouseDown.clickChainCount == 2) {
				EsMessage m = { ES_MSG_LIST_VIEW_CHOOSE_ITEM };
				m.chooseItem.group = item->group;
				m.chooseItem.index = item->index;
				m.chooseItem.source = ES_LIST_VIEW_CHOOSE_ITEM_DOUBLE_CLICK;
				EsMessageSend(this, &m);
			}
		} else if (message->type == ES_MSG_MOUSE_MIDDLE_DOWN) {
		} else if (message->type == ES_MSG_MOUSE_RIGHT_DOWN) {
			EsMessage m = { ES_MSG_LIST_VIEW_IS_SELECTED };
			m.selectItem.index = item->index;
			m.selectItem.group = item->group;
			EsMessageSend(this, &m);

			if (!m.selectItem.isSelected) {
				Select(item->group, item->index, EsKeyboardIsShiftHeld(), EsKeyboardIsCtrlHeld(), false);
			}

			m.type = ES_MSG_LIST_VIEW_CONTEXT_MENU;
			EsMessageSend(this, &m);
		} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
			if (flags & ES_LIST_VIEW_CHOICE_SELECT) {
				window->pressed = this;
				ProcessMessage(message);
			}
		} else if (message->type == ES_MSG_GET_INSPECTOR_INFORMATION) {
			EsMessage m = { ES_MSG_LIST_VIEW_GET_CONTENT };
			uint8_t _buffer[256];
			EsBuffer buffer = { .out = _buffer, .bytes = sizeof(_buffer) };
			m.getContent.buffer = &buffer;
			m.getContent.index = item->index;
			m.getContent.group = item->group;
			EsMessageSend(this, &m);
			EsBufferFormat(message->getContent.buffer, "index %d '%s'", item->index, buffer.position, buffer.out);
		} else {
			return 0;
		}

		return ES_HANDLED;
	}

	inline int GetWrapLimit() {
		EsRectangle bounds = GetListBounds();
		return (flags & ES_LIST_VIEW_HORIZONTAL) 
			? bounds.b - bounds.t - style->insets.b - style->insets.t 
			: bounds.r - bounds.l - style->insets.r - style->insets.l;
	}

	ListViewItem *FindVisibleItem(EsListViewIndex group, EsListViewIndex index) {
		for (uintptr_t i = 0; i < visibleItems.Length(); i++) {
			ListViewItem *item = &visibleItems[i];

			if (item->group == group && item->index == index) {
				return item;
			}
		}

		return nullptr;
	}

	bool Search() {
		uint8_t _buffer[64];

		if (!hasFocusedItem) {
			// Select the first item in the list.
			KeyInput(ES_SCANCODE_DOWN_ARROW, false, false, false, true);
			if (!hasFocusedItem) return false;
		}

		EsMessage m = { ES_MSG_LIST_VIEW_SEARCH };
		m.searchItem.index = focusedItemIndex;
		m.searchItem.group = focusedItemGroup;
		m.searchItem.query = searchBuffer;
		m.searchItem.queryBytes = searchBufferBytes;
		int response = EsMessageSend(this, &m);

		if (response == ES_REJECTED) {
			return false;
		} 

		ListViewItem *oldFocus = FindVisibleItem(focusedItemGroup, focusedItemIndex);

		if (oldFocus) {
			oldFocus->element->customStyleState &= ~THEME_STATE_FOCUSED_ITEM;
			oldFocus->element->MaybeRefreshStyle();
		}
		
		bool found = false;

		if (response == ES_HANDLED) {
			focusedItemIndex = m.searchItem.index;
			focusedItemGroup = m.searchItem.group;
			found = true;
		} else {
			EsMessage m = { ES_MSG_LIST_VIEW_GET_CONTENT };
			EsBuffer buffer = { .out = _buffer, .bytes = sizeof(_buffer) };
			m.getContent.buffer = &buffer;
			m.getContent.index = focusedItemIndex;
			m.getContent.group = focusedItemGroup;
			EsMessage m2 = {};
			m2.iterateIndex.index = focusedItemIndex;
			m2.iterateIndex.group = focusedItemGroup;

			do {
				buffer.position = 0;
				EsMessageSend(this, &m);

				if (StringStartsWith((char *) _buffer, buffer.position, searchBuffer, searchBufferBytes, true)) {
					found = true;
					break;
				}

				if (!IterateForwards(&m2)) {
					m2.iterateIndex.group = 0;
					GetFirstIndex(&m2);
				}

				m.getContent.index = m2.iterateIndex.index;
				m.getContent.group = m2.iterateIndex.group;
			} while (m.getContent.index != focusedItemIndex || m.getContent.group != focusedItemGroup);

			focusedItemIndex = m.getContent.index;
			focusedItemGroup = m.getContent.group;
			EnsureItemVisible(focusedItemGroup, focusedItemIndex, ES_FLAGS_DEFAULT);
		}

		ListViewItem *newFocus = FindVisibleItem(focusedItemGroup, focusedItemIndex);

		if (newFocus) {
			newFocus->element->customStyleState |= THEME_STATE_FOCUSED_ITEM;
			newFocus->element->MaybeRefreshStyle();
		}

		{
			EsMessage m = { ES_MSG_LIST_VIEW_GET_CONTENT };
			EsBuffer buffer = { .out = _buffer, .bytes = sizeof(_buffer) };
			m.getContent.buffer = &buffer;

			for (uintptr_t i = 0; i < visibleItems.Length(); i++) {
				ListViewItem *item = &visibleItems[i];
				m.getContent.index = item->index;
				m.getContent.group = item->group;
				buffer.position = 0;
				EsMessageSend(this, &m);
				bool shouldShowSearchHighlight = StringStartsWith((char *) _buffer, buffer.position, searchBuffer, searchBufferBytes, true);

				if (shouldShowSearchHighlight || (!shouldShowSearchHighlight && item->showSearchHighlight)) {
					item->showSearchHighlight = shouldShowSearchHighlight;
					item->element->Repaint(true);
				}
			}
		}

		Select(-1, 0, false, false, false);
		Select(focusedItemGroup, focusedItemIndex, false, false, false);
		return found;
	}

	bool KeyInput(int scancode, bool ctrl, bool alt, bool shift, bool keepSearchBuffer = false) {
		if (!totalItemCount || alt) {
			return false;
		}

		if (scancode == ES_SCANCODE_BACKSPACE && searchBufferBytes) {
			searchBufferBytes = 0;
			Search();
			return true;
		}

		bool isNext = false, 
		     isPrevious = false,
		     isNextBand = false,
		     isPreviousBand = false,
		     isHome = scancode == ES_SCANCODE_HOME,
		     isEnd = scancode == ES_SCANCODE_END,
		     isPageUp = scancode == ES_SCANCODE_PAGE_UP,
		     isPageDown = scancode == ES_SCANCODE_PAGE_DOWN,
		     isSpace = scancode == ES_SCANCODE_SPACE,
		     isEnter = scancode == ES_SCANCODE_ENTER;

		if (flags & ES_LIST_VIEW_HORIZONTAL) {
			isNext = scancode == ES_SCANCODE_DOWN_ARROW;
			isNextBand = scancode == ES_SCANCODE_RIGHT_ARROW;
			isPrevious = scancode == ES_SCANCODE_UP_ARROW;
			isPreviousBand = scancode == ES_SCANCODE_LEFT_ARROW;
		} else {
			isNext = scancode == ES_SCANCODE_RIGHT_ARROW;
			isNextBand = scancode == ES_SCANCODE_DOWN_ARROW;
			isPrevious = scancode == ES_SCANCODE_LEFT_ARROW;
			isPreviousBand = scancode == ES_SCANCODE_UP_ARROW;
		}

		if (hasSelectionBoxAnchor) {
			if (scancode == ES_SCANCODE_UP_ARROW)     scroll.SetY(scroll.position[1] - GetConstantNumber("scrollKeyMovement"));
			if (scancode == ES_SCANCODE_DOWN_ARROW)   scroll.SetY(scroll.position[1] + GetConstantNumber("scrollKeyMovement"));
			if (scancode == ES_SCANCODE_LEFT_ARROW)   scroll.SetX(scroll.position[0] - GetConstantNumber("scrollKeyMovement"));
			if (scancode == ES_SCANCODE_RIGHT_ARROW)  scroll.SetX(scroll.position[0] + GetConstantNumber("scrollKeyMovement"));
			if (scancode == ES_SCANCODE_PAGE_UP)      scroll.SetY(scroll.position[1] - Height(GetBounds()));
			if (scancode == ES_SCANCODE_PAGE_DOWN)    scroll.SetY(scroll.position[1] + Height(GetBounds()));
			
			if (flags & ES_LIST_VIEW_HORIZONTAL) {
				if (scancode == ES_SCANCODE_HOME) scroll.SetX(0);
				if (scancode == ES_SCANCODE_END)  scroll.SetX(scroll.limit[0]);
			} else {
				if (scancode == ES_SCANCODE_HOME) scroll.SetY(0);
				if (scancode == ES_SCANCODE_END)  scroll.SetY(scroll.limit[1]);
			}
		} else if (isPrevious || isNext || isHome || isEnd || isPageUp || isPageDown || isNextBand || isPreviousBand) {
			ListViewItem *oldFocus = FindVisibleItem(focusedItemGroup, focusedItemIndex);

			if (oldFocus) {
				oldFocus->element->customStyleState &= ~THEME_STATE_FOCUSED_ITEM;
				oldFocus->element->MaybeRefreshStyle();
			}

			EsMessage m = {};

			if (hasFocusedItem && (isPrevious || isNext || isPageUp || isPageDown || isNextBand || isPreviousBand)) {
				m.iterateIndex.group = focusedItemGroup;
				m.iterateIndex.index = focusedItemIndex;

				uintptr_t itemsPerBand = GetItemsPerBand();

				for (uintptr_t i = 0; i < ((isPageUp || isPageDown) ? (10 * itemsPerBand) : (isNextBand || isPreviousBand) ? itemsPerBand : 1); i++) {
					if (isNext || isPageDown || isNextBand) IterateForwards(&m);
					else IterateBackwards(&m);
				}
			} else {
				if (isNext || isNextBand || isHome) {
					m.iterateIndex.group = 0;
					GetFirstIndex(&m);
				} else {
					m.iterateIndex.group = groups.Length() - 1;
					GetLastIndex(&m);
				}
			}

			hasFocusedItem = true;
			focusedItemGroup = m.iterateIndex.group;
			focusedItemIndex = m.iterateIndex.index;

			ListViewItem *newFocus = FindVisibleItem(focusedItemGroup, focusedItemIndex);

			if (newFocus) {
				newFocus->element->customStyleState |= THEME_STATE_FOCUSED_ITEM;
				newFocus->element->MaybeRefreshStyle();
			}

			if (!keepSearchBuffer) ClearSearchBuffer();
			EnsureItemVisible(focusedItemGroup, focusedItemIndex, (isPrevious || isHome || isPageUp || isPreviousBand) ? ENSURE_VISIBLE_ALIGN_TOP : ES_FLAGS_DEFAULT);
			Select(focusedItemGroup, focusedItemIndex, shift, ctrl, ctrl && !shift);
			return true;
		} else if (isSpace && ctrl && !shift && hasFocusedItem) {
			Select(focusedItemGroup, focusedItemIndex, false, true, false);
			return true;
		} else if (isEnter && hasFocusedItem) {
			if (searchBufferBytes) {
				searchBufferLastKeyTime = 0;
				searchBufferBytes = 0;
				EsListViewInvalidateAll(this);
			}

			EsMessage m = { ES_MSG_LIST_VIEW_CHOOSE_ITEM };
			m.chooseItem.group = focusedItemGroup;
			m.chooseItem.index = focusedItemIndex;
			m.chooseItem.source = ES_LIST_VIEW_CHOOSE_ITEM_ENTER;
			EsMessageSend(this, &m);
			return true;
		} else if (!ctrl && !alt) {
			uint64_t currentTime = EsTimeStampMs();

			if (searchBufferLastKeyTime + GetConstantNumber("listViewSearchBufferTimeout") < currentTime) {
				searchBufferBytes = 0;
			}

			StartAnimating();

			const char *inputString = KeyboardLayoutLookup(scancode, shift, false, false, false);
			size_t inputStringBytes = EsCStringLength(inputString);

			if (inputString && searchBufferBytes + inputStringBytes < sizeof(searchBuffer)) {
				searchBufferLastKeyTime = currentTime;
				EsMemoryCopy(searchBuffer + searchBufferBytes, inputString, inputStringBytes);
				size_t previousSearchBufferBytes = searchBufferBytes;
				searchBufferBytes += inputStringBytes;
				if (!Search()) searchBufferBytes = previousSearchBufferBytes;
				return true;
			}
		}

		return false;
	}

	void ClearSearchBuffer() {
		searchBufferBytes = 0;

		for (uintptr_t i = 0; i < visibleItems.Length(); i++) {
			if (visibleItems[i].showSearchHighlight) {
				visibleItems[i].showSearchHighlight = false;
				visibleItems[i].element->Repaint(true);
			}
		}
	}

	void DragSelect() {
		EsRectangle bounds = GetWindowBounds();
		EsPoint mouse = EsMouseGetPosition(window);

		if (mouse.x < bounds.l) mouse.x = bounds.l;
		if (mouse.x >= bounds.r) mouse.x = bounds.r - 1;

		if (visibleItems.Length()) {
			int32_t start = visibleItems[0].element->GetWindowBounds().t;
			int32_t end = visibleItems.Last().element->GetWindowBounds().b;
			if (mouse.y < start) mouse.y = start;
			if (mouse.y >= end) mouse.y = end - 1;
		}

		EsElement *hoverItem = UIFindHoverElementRecursively(this, bounds.l - offsetX, bounds.t - offsetY, mouse);

		if (hoverItem && hoverItem->messageClass == ListViewProcessItemMessage) {
			EsMessage m = {};
			m.type = ES_MSG_MOUSE_LEFT_DOWN;
			EsMessageSend(hoverItem, &m);
		}
	}

	void MoveInlineTextbox(ListViewItem *item) {
		UIStyle *style = item->element->style;

		if (flags & ES_LIST_VIEW_COLUMNS) {
			int offset = primaryCellStyle->metrics->iconSize + primaryCellStyle->gapMinor 
				+ style->insets.l - inlineTextbox->style->insets.l;
			inlineTextbox->InternalMove(registeredColumns[activeColumns[0]].width * theming.scale - offset, item->element->height, 
					item->element->offsetX + offset, item->element->offsetY);
		} else if (flags & ES_LIST_VIEW_TILED) {
			if (style->metrics->layoutVertical) {
				int height = inlineTextbox->style->preferredHeight;
				int textStart = style->metrics->iconSize + style->gapMinor + style->insets.t;
				int textEnd = item->element->height - style->insets.b;
				int offset = (textStart + textEnd - height) / 2;
				inlineTextbox->InternalMove(item->element->width - style->insets.r - style->insets.l, height, 
						item->element->offsetX + style->insets.l, item->element->offsetY + offset);
			} else {
				int textboxInset = inlineTextbox->style->insets.l;
				int offset = style->metrics->iconSize + style->gapMinor 
					+ style->insets.l - textboxInset;
				int height = inlineTextbox->style->preferredHeight;
				inlineTextbox->InternalMove(item->element->width - offset - style->insets.r + textboxInset, height, 
						item->element->offsetX + offset, 
						item->element->offsetY + (item->element->height - height) / 2);
			}
		} else {
			inlineTextbox->InternalMove(item->element->width, item->element->height, 
					item->element->offsetX, item->element->offsetY);
		}
	}

	int ProcessMessage(EsMessage *message) {
		int response = scroll.ReceivedMessage(message);
		if (response) return response;

		if (message->type == ES_MSG_GET_WIDTH || message->type == ES_MSG_GET_HEIGHT) {
			if (flags & ES_LIST_VIEW_HORIZONTAL) {
				message->measure.width = totalSize + style->insets.l + style->insets.r;
			} else {
				message->measure.height = totalSize + style->insets.t + style->insets.b;

				if (flags & ES_LIST_VIEW_COLUMNS) {
					message->measure.width = totalColumnWidth + style->insets.l + style->insets.r;
					message->measure.height += columnHeader->style->preferredHeight;
				}
			}
		} else if (message->type == ES_MSG_LAYOUT) {
			firstLayout = true;
			Wrap(message->layout.sizeChanged);

			if (columnHeader) {
				columnHeader->InternalMove(Width(GetBounds()), columnHeader->style->preferredHeight, 0, 0);
			}

			Populate();
		} else if (message->type == ES_MSG_SCROLL_X || message->type == ES_MSG_SCROLL_Y) {
			int64_t delta = message->scroll.scroll - message->scroll.previous;

			if ((message->type == ES_MSG_SCROLL_X) == ((flags & ES_LIST_VIEW_HORIZONTAL) ? true : false)) {
				for (uintptr_t i = 0; i < visibleItems.Length(); i++) {
					if (flags & ES_LIST_VIEW_HORIZONTAL) visibleItems[i].element->offsetX -= delta;
					else				     visibleItems[i].element->offsetY -= delta;
				}
			}

			useScrollItem = false;
			Populate();
			Repaint(true);

			if (columnHeader && message->type == ES_MSG_SCROLL_X) {
				EsElementRelayout(columnHeader);
			}

			if (selectionBox) {
				EsPoint position = EsMouseGetPosition(this);
				selectionBoxPositionX = position.x + scroll.position[0];
				selectionBoxPositionY = position.y + scroll.position[1];
				SelectPreview();
			}
		} else if (message->type == ES_MSG_DESTROY) {
			for (uintptr_t i = 0; i < visibleItems.Length(); i++) {
				visibleItems[i].element->Destroy();
			}

			for (uintptr_t i = 0; i < registeredColumns.Length(); i++) {
				if ((registeredColumns[i].flags & ES_LIST_VIEW_COLUMN_DATA_MASK) == ES_LIST_VIEW_COLUMN_DATA_STRINGS) {
					for (uintptr_t j = 0; j < registeredColumns[i].items.Length(); j++) {
						EsHeapFree(registeredColumns[i].items[j].s.string);
					}
				}

				EsHeapFree(registeredColumns[i].title);
				registeredColumns[i].items.Free();
			}

			EsHeapFree(emptyMessage);
			primaryCellStyle->CloseReference();
			secondaryCellStyle->CloseReference();
			selectedCellStyle->CloseReference();
			fixedItems.Free();
			fixedItemIndices.Free();
			visibleItems.Free();
			groups.Free();
			activeColumns.Free();
			registeredColumns.Free();

			if (EsElementIsFocused(this)) {
				EsCommandSetCallback(EsCommandByID(instance, ES_COMMAND_SELECT_ALL), nullptr);
			}
		} else if (message->type == ES_MSG_KEY_UP) {
			if (message->keyboard.scancode == ES_SCANCODE_LEFT_CTRL || message->keyboard.scancode == ES_SCANCODE_RIGHT_CTRL) {
				SelectPreview();
			}
		} else if (message->type == ES_MSG_KEY_DOWN) {
			if (message->keyboard.scancode == ES_SCANCODE_LEFT_CTRL || message->keyboard.scancode == ES_SCANCODE_RIGHT_CTRL) {
				SelectPreview();
			}

			if (message->keyboard.modifiers & ~(ES_MODIFIER_CTRL | ES_MODIFIER_ALT | ES_MODIFIER_SHIFT)) {
				// Unused modifier.
				return 0;
			}

			return KeyInput(message->keyboard.scancode, 
					message->keyboard.modifiers & ES_MODIFIER_CTRL, 
					message->keyboard.modifiers & ES_MODIFIER_ALT, 
					message->keyboard.modifiers & ES_MODIFIER_SHIFT)
				? ES_HANDLED : 0;
		} else if (message->type == ES_MSG_FOCUSED_START) {
			if (!hasFocusedItem && groups.Length() && (message->focus.flags & ES_ELEMENT_FOCUS_FROM_KEYBOARD)) {
				hasFocusedItem = true;
				focusedItemGroup = 0;
				focusedItemIndex = 0;
			}

			for (uintptr_t i = 0; i < visibleItems.Length(); i++) {
				ListViewItem *item = &visibleItems[i];
				item->element->customStyleState |= THEME_STATE_LIST_FOCUSED;

				if (hasFocusedItem && focusedItemGroup == item->group && focusedItemIndex == item->index) {
					item->element->customStyleState |= THEME_STATE_FOCUSED_ITEM;
				}

				item->element->MaybeRefreshStyle();
			}

			EsCommand *command = EsCommandByID(instance, ES_COMMAND_SELECT_ALL);
			command->data = this;

			EsCommandSetCallback(command, [] (EsInstance *, EsElement *, EsCommand *command) {
				EsListView *list = (EsListView *) command->data.p;
				if (!list->groups.Length() || !list->totalItemCount) return;
				list->SetSelected(0, 0, list->groups.Length() - 1, list->groups.Last().itemCount - 1, true, false);
				list->UpdateVisibleItemsSelectionState();
			});

			EsCommandSetDisabled(command, false);
		} else if (message->type == ES_MSG_FOCUSED_END) {
			for (uintptr_t i = 0; i < visibleItems.Length(); i++) {
				ListViewItem *item = &visibleItems[i];
				item->element->customStyleState &= ~(THEME_STATE_LIST_FOCUSED | THEME_STATE_FOCUSED_ITEM);
				item->element->MaybeRefreshStyle();
			}

			// Also done in ES_MSG_DESTROY:
			EsCommandSetCallback(EsCommandByID(instance, ES_COMMAND_SELECT_ALL), nullptr);
		} else if (message->type == ES_MSG_MOUSE_RIGHT_DOWN) {
			// Make sure that right clicking will focus the list.
		} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
			Select(-1, 0, EsKeyboardIsShiftHeld(), EsKeyboardIsCtrlHeld(), false);
		} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
			if (selectionBox) {
				Repaint(false, ES_RECT_4(selectionBox->offsetX, selectionBox->offsetX + selectionBox->width,
							selectionBox->offsetY, selectionBox->offsetY + selectionBox->height));
				
				if (!hasSelectionBoxAnchor) {
					hasSelectionBoxAnchor = true;
					selectionBoxAnchorX = message->mouseDragged.originalPositionX + scroll.position[0];
					selectionBoxAnchorY = message->mouseDragged.originalPositionY + scroll.position[1];

					if (gui.lastClickButton == ES_MSG_MOUSE_LEFT_DOWN) {
						Select(-1, 0, EsKeyboardIsShiftHeld(), EsKeyboardIsCtrlHeld(), false);
					}
				}

				EsElementSetDisabled(selectionBox, false);

				selectionBoxPositionX = message->mouseDragged.newPositionX + scroll.position[0];
				selectionBoxPositionY = message->mouseDragged.newPositionY + scroll.position[1];

				// Inclusive rectangle.
				if (selectionBoxPositionX >= selectionBoxAnchorX) selectionBoxPositionX++;
				if (selectionBoxPositionY >= selectionBoxAnchorY) selectionBoxPositionY++;

				SelectPreview();
			} else if (flags & ES_LIST_VIEW_CHOICE_SELECT) {
				DragSelect();
			}
		} else if (message->type == ES_MSG_MOUSE_LEFT_UP || message->type == ES_MSG_MOUSE_RIGHT_UP) {
			if (selectionBox) {
				EsElementSetDisabled(selectionBox, true);

				if (hasSelectionBoxAnchor) {
					hasSelectionBoxAnchor = false;

					int64_t x1 = selectionBoxPositionX, x2 = selectionBoxAnchorX,
						y1 = selectionBoxPositionY, y2 = selectionBoxAnchorY;
					if (x1 > x2) { int64_t temp = x1; x1 = x2; x2 = temp; }
					if (y1 > y2) { int64_t temp = y1; y1 = y2; y2 = temp; }

					SelectBox(x1, x2, y1, y2, EsKeyboardIsCtrlHeld());
				}

				for (uintptr_t i = 0; i < visibleItems.Length(); i++) {
					EsMessage m = { ES_MSG_LIST_VIEW_IS_SELECTED };
					m.selectItem.index = visibleItems[i].index;
					m.selectItem.group = visibleItems[i].group;
					EsMessageSend(this, &m);

					EsElement *item = visibleItems[i].element;

					if (m.selectItem.isSelected) {
						item->customStyleState |= THEME_STATE_SELECTED;
					} else {
						item->customStyleState &= ~THEME_STATE_SELECTED;
					}

					item->MaybeRefreshStyle();
				}
			}
		} else if (message->type == ES_MSG_Z_ORDER) {
			uintptr_t index = message->zOrder.index;

			if (index < visibleItems.Length()) {
				EsAssert(zOrderItems.Length() == visibleItems.Length());
				message->zOrder.child = zOrderItems[index];
				return ES_HANDLED;
			} else {
				index -= visibleItems.Length();
			}

			if (selectionBox)  { if (index == 0) return message->zOrder.child = selectionBox,  ES_HANDLED; else index--; }
			if (columnHeader)  { if (index == 0) return message->zOrder.child = columnHeader,  ES_HANDLED; else index--; }
			if (inlineTextbox) { if (index == 0) return message->zOrder.child = inlineTextbox, ES_HANDLED; else index--; }

			message->zOrder.child = nullptr;
		} else if (message->type == ES_MSG_PAINT && !totalItemCount && emptyMessageBytes) {
			EsDrawTextThemed(message->painter, this, EsPainterBoundsInset(message->painter), emptyMessage, emptyMessageBytes, 
					ES_STYLE_TEXT_LABEL_SECONDARY, ES_TEXT_H_CENTER | ES_TEXT_V_CENTER | ES_TEXT_WRAP);
		} else if (message->type == ES_MSG_ANIMATE) {
			if (scroll.dragScrolling && (flags & ES_LIST_VIEW_CHOICE_SELECT)) {
				DragSelect();
			}

			uint64_t currentTime = EsTimeStampMs();
			int64_t remainingTime = searchBufferLastKeyTime + GetConstantNumber("listViewSearchBufferTimeout") - currentTime;

			if (remainingTime < 0) {
				ClearSearchBuffer();
			} else {
				message->animate.waitMs = remainingTime;
				message->animate.complete = false;
			}
		} else if (message->type == ES_MSG_BEFORE_Z_ORDER) {
			EsAssert(!zOrderItems.Length());
			intptr_t focused = -1, hovered = -1;

			for (uintptr_t i = 0; i < visibleItems.Length(); i++) {
				if (hasFocusedItem && visibleItems[i].index == focusedItemIndex && visibleItems[i].group == focusedItemGroup) {
					focused = i;
				} else if (window->hovered == visibleItems[i].element) {
					hovered = i;
				} else {
					zOrderItems.Add(visibleItems[i].element);
				}
			}

			if (hovered != -1) {
				zOrderItems.Add(visibleItems[hovered].element);
			}
			
			if (focused != -1) {
				zOrderItems.Add(visibleItems[focused].element);
			}
		} else if (message->type == ES_MSG_AFTER_Z_ORDER) {
			zOrderItems.Free();
		} else if (message->type == ES_MSG_GET_ACCESS_KEY_HINT_BOUNDS) {
			AccessKeysCenterHint(this, message);
		} else if (message->type == ES_MSG_UI_SCALE_CHANGED) {
			primaryCellStyle->CloseReference();
			secondaryCellStyle->CloseReference();
			selectedCellStyle->CloseReference();

			primaryCellStyle = GetStyle(MakeStyleKey(ES_STYLE_LIST_PRIMARY_CELL, 0), false);
			secondaryCellStyle = GetStyle(MakeStyleKey(ES_STYLE_LIST_SECONDARY_CELL, 0), false);
			selectedCellStyle = GetStyle(MakeStyleKey(ES_STYLE_LIST_SELECTED_CHOICE_CELL, 0), false);

			EsListViewChangeStyles(this, nullptr, nullptr, nullptr, nullptr, ES_FLAGS_DEFAULT, ES_FLAGS_DEFAULT);
		} else if (message->type == ES_MSG_LIST_VIEW_GET_CONTENT && (activeColumns.Length() || (flags & ES_LIST_VIEW_FIXED_ITEMS))) {
			uintptr_t index = message->getContent.index;

			ListViewFixedItemData data = {};

			ListViewColumn *column = &registeredColumns[(flags & ES_LIST_VIEW_COLUMNS) ? activeColumns[message->getContent.activeColumnIndex] : 0];
			uint32_t format = column->flags & ES_LIST_VIEW_COLUMN_FORMAT_MASK;
			uint32_t type = column->flags & ES_LIST_VIEW_COLUMN_DATA_MASK;

			if (flags & ES_LIST_VIEW_FIXED_ITEMS) {
				EsAssert(index < fixedItems.Length());
				index = fixedItemIndices[index];
				ListViewFixedItem *item = &fixedItems[index];
				if (index < column->items.Length()) data = column->items[index];

				if (!activeColumns.Length() || message->getContent.columnID == registeredColumns[activeColumns[0]].id) {
					message->getContent.icon = item->iconID;
				}
			} else {
				EsMessage m = { .type = ES_MSG_LIST_VIEW_GET_ITEM_DATA };
				m.getItemData.index = message->getContent.index;
				m.getItemData.group = message->getContent.group;
				m.getItemData.columnID = message->getContent.columnID;
				m.getItemData.activeColumnIndex = message->getContent.activeColumnIndex;
				EsMessageSend(this, &m);

				if (type == ES_LIST_VIEW_COLUMN_DATA_STRINGS) {
					data.s.string = (char *) m.getItemData.s;
					data.s.bytes = m.getItemData.sBytes;
				} else if (type == ES_LIST_VIEW_COLUMN_DATA_DOUBLES) {
					data.d = m.getItemData.d;
				} else if (type == ES_LIST_VIEW_COLUMN_DATA_INTEGERS) {
					data.i = m.getItemData.i;
				}

				if (!activeColumns.Length() || message->getContent.columnID == registeredColumns[activeColumns[0]].id) {
					message->getContent.icon = m.getItemData.icon;
				}
			}

#define BOOLEAN_FORMAT(trueString, falseString) \
	if (type == ES_LIST_VIEW_COLUMN_DATA_INTEGERS) { \
		EsBufferFormat(message->getContent.buffer, "%z", data.i ? interfaceString_ ## trueString : interfaceString_ ## falseString); \
	} else { \
		EsAssert(false); \
	}
#define NUMBER_FORMAT(unitString) \
	if (type == ES_LIST_VIEW_COLUMN_DATA_INTEGERS) { \
		EsBufferFormat(message->getContent.buffer, "%d%z", data.i, interfaceString_ ## unitString); \
	} else if (type == ES_LIST_VIEW_COLUMN_DATA_DOUBLES) { \
		EsBufferFormat(message->getContent.buffer, "%F%z", data.d, interfaceString_ ## unitString); \
	} else { \
		EsAssert(false); \
	}
#define UNIT_FORMAT(unitString1, unitString2, unitString3) \
	double d = type == ES_LIST_VIEW_COLUMN_DATA_INTEGERS ? data.i : type == ES_LIST_VIEW_COLUMN_DATA_DOUBLES ? data.d : 0; \
	if (d < 10000)         EsBufferFormat(message->getContent.buffer, "%F%z",     d,           interfaceString_ ## unitString1); \
	else if (d < 10000000) EsBufferFormat(message->getContent.buffer, "%.F%z", 1, d / 1000,    interfaceString_ ## unitString2); \
	else                   EsBufferFormat(message->getContent.buffer, "%.F%z", 1, d / 1000000, interfaceString_ ## unitString3);

			if (format == ES_LIST_VIEW_COLUMN_FORMAT_DEFAULT) {
				if (type == ES_LIST_VIEW_COLUMN_DATA_STRINGS) {
					EsBufferFormat(message->getContent.buffer, "%s", data.s.bytes, data.s.string);
				} else if (type == ES_LIST_VIEW_COLUMN_DATA_DOUBLES) {
					EsBufferFormat(message->getContent.buffer, "%F", data.d);
				} else if (type == ES_LIST_VIEW_COLUMN_DATA_INTEGERS) {
					EsBufferFormat(message->getContent.buffer, "%d", data.i);
				}
			} else if (format == ES_LIST_VIEW_COLUMN_FORMAT_BYTES) {
				if (type == ES_LIST_VIEW_COLUMN_DATA_INTEGERS) {
					EsBufferFormat(message->getContent.buffer, "%D", data.i);
				} else {
					EsAssert(false);
				}
			} else if (format == ES_LIST_VIEW_COLUMN_FORMAT_ENUM_STRING) {
				if (type == ES_LIST_VIEW_COLUMN_DATA_INTEGERS) {
					EsAssert(data.i >= 0 && (uintptr_t) data.i < column->enumStringCount);
					EsBufferFormat(message->getContent.buffer, "%s", column->enumStrings[data.i].stringBytes, column->enumStrings[data.i].string);
				} else {
					EsAssert(false);
				}
			} else if (format == ES_LIST_VIEW_COLUMN_FORMAT_YES_NO) {
				BOOLEAN_FORMAT(CommonBooleanYes, CommonBooleanNo);
			} else if (format == ES_LIST_VIEW_COLUMN_FORMAT_ON_OFF) {
				BOOLEAN_FORMAT(CommonBooleanOn, CommonBooleanOff);
			} else if (format == ES_LIST_VIEW_COLUMN_FORMAT_PERCENTAGE) {
				NUMBER_FORMAT(CommonUnitPercent);
			} else if (format == ES_LIST_VIEW_COLUMN_FORMAT_BITS) {
				NUMBER_FORMAT(CommonUnitBits);
			} else if (format == ES_LIST_VIEW_COLUMN_FORMAT_PIXELS) {
				NUMBER_FORMAT(CommonUnitPixels);
			} else if (format == ES_LIST_VIEW_COLUMN_FORMAT_DPI) {
				NUMBER_FORMAT(CommonUnitDPI);
			} else if (format == ES_LIST_VIEW_COLUMN_FORMAT_SECONDS) {
				NUMBER_FORMAT(CommonUnitSeconds);
			} else if (format == ES_LIST_VIEW_COLUMN_FORMAT_HERTZ) {
				UNIT_FORMAT(CommonUnitHz, CommonUnitKHz, CommonUnitMHz);
			} else if (format == ES_LIST_VIEW_COLUMN_FORMAT_BYTE_RATE) {
				UNIT_FORMAT(CommonUnitBps, CommonUnitKBps, CommonUnitMBps);
			} else {
				EsAssert(false);
			}

#undef NUMBER_FORMAT
#undef BOOLEAN_FORMAT
		} else if (message->type == ES_MSG_LIST_VIEW_IS_SELECTED && (flags & ES_LIST_VIEW_FIXED_ITEMS)) {
			message->selectItem.isSelected = message->selectItem.index == fixedItemSelection;
		} else if (message->type == ES_MSG_LIST_VIEW_COLUMN_MENU && (flags & ES_LIST_VIEW_FIXED_ITEMS)) {
			EsMenu *menu = EsMenuCreate(message->columnMenu.source);
			menu->userData = this;

			ListViewColumn *column = &registeredColumns[activeColumns[message->columnMenu.activeColumnIndex]];
			uint32_t sortMode = column->flags & ES_LIST_VIEW_COLUMN_SORT_MASK;
			uint64_t checkAscending  = (fixedItemSortDirection == LIST_SORT_DIRECTION_ASCENDING  && column->id == fixedItemSortColumnID) ? ES_MENU_ITEM_CHECKED : 0;
			uint64_t checkDescending = (fixedItemSortDirection == LIST_SORT_DIRECTION_DESCENDING && column->id == fixedItemSortColumnID) ? ES_MENU_ITEM_CHECKED : 0;

			if (sortMode != ES_LIST_VIEW_COLUMN_SORT_NONE) {
				EsMenuAddItem(menu, ES_MENU_ITEM_HEADER, INTERFACE_STRING(CommonSortHeader));

				if (sortMode == ES_LIST_VIEW_COLUMN_SORT_DEFAULT) {
					EsMenuAddItem(menu, checkAscending, INTERFACE_STRING(CommonSortAToZ), ListViewSetSortAscending, column->id);
					EsMenuAddItem(menu, checkDescending, INTERFACE_STRING(CommonSortZToA), ListViewSetSortDescending, column->id);
				} else if (sortMode == ES_LIST_VIEW_COLUMN_SORT_TIME) {
					EsMenuAddItem(menu, checkAscending, INTERFACE_STRING(CommonSortOldToNew), ListViewSetSortAscending, column->id);
					EsMenuAddItem(menu, checkDescending, INTERFACE_STRING(CommonSortNewToOld), ListViewSetSortDescending, column->id);
				} else if (sortMode == ES_LIST_VIEW_COLUMN_SORT_SIZE) {
					EsMenuAddItem(menu, checkAscending, INTERFACE_STRING(CommonSortSmallToLarge), ListViewSetSortAscending, column->id);
					EsMenuAddItem(menu, checkDescending, INTERFACE_STRING(CommonSortLargeToSmall), ListViewSetSortDescending, column->id);
				}
			}

			EsMenuShow(menu);
		} else {
			return 0;
		}

		return ES_HANDLED;
	}
};

void ListViewPopulateActionCallback(EsElement *element, EsGeneric) {
	EsListView *view = (EsListView *) element;
	EsAssert(view->populateQueued);
	view->populateQueued = false;
	view->_Populate();
	EsAssert(!view->populateQueued);

#if 0
	if (element->flags & ES_ELEMENT_DEBUG) {
		EsPrint("Populate complete for list %x with scroll %i:\n", view, view->scroll.position[1]);

		for (uintptr_t i = 0; i < view->visibleItems.Length(); i++) {
			EsMessage m = { ES_MSG_LIST_VIEW_GET_CONTENT };
			uint8_t _buffer[512];
			EsBuffer buffer = { .out = _buffer, .bytes = sizeof(_buffer) };
			m.getContent.buffer = &buffer;
			m.getContent.index = view->visibleItems[i].index;
			m.getContent.group = view->visibleItems[i].group;
			EsMessageSend(view, &m);
			EsPrint("\t%d: %d '%s' at %i\n", i, view->visibleItems[i].index, buffer.position, _buffer, 
					view->visibleItems[i].element->offsetY - view->GetListBounds().t);
		}
	}
#endif
}

void ListViewEnsureVisibleActionCallback(EsElement *element, EsGeneric) {
	EsListView *view = (EsListView *) element;
	EsAssert(view->ensureVisibleQueued);
	view->ensureVisibleQueued = false;
	view->_EnsureItemVisible(view->ensureVisibleGroupIndex, view->ensureVisibleIndex, view->ensureVisibleFlags);
	EsAssert(!view->ensureVisibleQueued);
}

int ListViewProcessMessage(EsElement *element, EsMessage *message) {
	return ((EsListView *) element)->ProcessMessage(message);
}

int ListViewProcessItemMessage(EsElement *_element, EsMessage *message) {
	ListViewItemElement *element = (ListViewItemElement *) _element;
	return ((EsListView *) element->parent)->ProcessItemMessage(element->index, message, element);
}

void ListViewCalculateTotalColumnWidth(EsListView *view) {
	view->totalColumnWidth = -view->secondaryCellStyle->gapMajor;

	for (uintptr_t i = 0; i < view->activeColumns.Length(); i++) {
		view->totalColumnWidth += view->registeredColumns[view->activeColumns[i]].width * theming.scale + view->secondaryCellStyle->gapMajor;
	}
}

int ListViewColumnHeaderMessage(EsElement *element, EsMessage *message) {
	EsListView *view = (EsListView *) element->userData.p;

	if (message->type == ES_MSG_LAYOUT) {
		int x = view->style->insets.l - view->scroll.position[0];

		for (uintptr_t i = 0; i < element->children.Length(); i += 2) {
			EsElement *item = element->children[i], *splitter = element->children[i + 1];
			ListViewColumn *column = &view->registeredColumns[item->userData.u];
			int splitterLeft = splitter->style->preferredWidth - view->secondaryCellStyle->gapMajor;
			item->InternalMove(column->width * theming.scale - splitterLeft, element->height, x, 0);
			splitter->InternalMove(splitter->style->preferredWidth, element->height, x + column->width * theming.scale - splitterLeft, 0);
			x += column->width * theming.scale + view->secondaryCellStyle->gapMajor;
		}
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN || message->type == ES_MSG_MOUSE_RIGHT_DOWN) {
		return ES_HANDLED;
	}

	return 0;
}

void EsListViewChangeStyles(EsListView *view, const EsStyle *style, const EsStyle *itemStyle, 
		const EsStyle *headerItemStyle, const EsStyle *footerItemStyle, uint32_t addFlags, uint32_t removeFlags) {
	// TODO Animating changes.

	bool wasTiledView = view->flags & ES_LIST_VIEW_TILED;

	EsAssert(!(addFlags & removeFlags));
	view->flags |= addFlags;
	view->flags &= ~(uint64_t) removeFlags;

	bool horizontal = view->flags & ES_LIST_VIEW_HORIZONTAL;

	if (style) view->SetStyle(style, true);
	if (itemStyle) view->itemStyle = itemStyle;
	if (headerItemStyle) view->headerItemStyle = headerItemStyle;
	if (footerItemStyle) view->footerItemStyle = footerItemStyle;

	GetPreferredSizeFromStylePart(view->itemStyle, &view->fixedWidth, &view->fixedHeight);
	GetPreferredSizeFromStylePart(view->headerItemStyle, horizontal ? &view->fixedHeaderSize : nullptr, horizontal ? nullptr : &view->fixedHeaderSize);
	GetPreferredSizeFromStylePart(view->footerItemStyle, horizontal ? &view->fixedFooterSize : nullptr, horizontal ? nullptr : &view->fixedFooterSize);

	if ((view->flags & ES_LIST_VIEW_MULTI_SELECT) && !view->selectionBox) {
		view->selectionBox = EsCustomElementCreate(view, ES_CELL_FILL | ES_ELEMENT_DISABLED | ES_ELEMENT_NO_HOVER, ES_STYLE_LIST_SELECTION_BOX);
		view->selectionBox->cName = "selection box";
	} else if ((~view->flags & ES_LIST_VIEW_MULTI_SELECT) && view->selectionBox) {
		EsElementDestroy(view->selectionBox);
		view->selectionBox = nullptr;
	}

	if ((view->flags & ES_LIST_VIEW_COLUMNS) && !view->columnHeader) {
		view->columnHeader = EsCustomElementCreate(view, ES_CELL_FILL, ES_STYLE_LIST_COLUMN_HEADER);
		view->columnHeader->cName = "column header";
		view->columnHeader->userData = view;
		view->columnHeader->messageUser = ListViewColumnHeaderMessage;
		view->scroll.fixedViewport[1] = view->columnHeader->style->preferredHeight;
	} else if ((~view->flags & ES_LIST_VIEW_COLUMNS) && view->columnHeader) {
		EsElementDestroy(view->columnHeader);
		view->activeColumns.Free();
		view->columnHeader = nullptr;
		view->scroll.fixedViewport[1] = 0;
	}

	// It's safe to use ES_SCROLL_MODE_AUTO even in tiled mode,
	// because decreasing the secondary axis can only increase the primary axis.

	uint8_t scrollXMode = 0, scrollYMode = 0;

	if (view->flags & ES_LIST_VIEW_COLUMNS) {
		scrollXMode = ES_SCROLL_MODE_AUTO;
		scrollYMode = ES_SCROLL_MODE_AUTO;
	} else if (view->flags & ES_LIST_VIEW_HORIZONTAL) {
		scrollXMode = ES_SCROLL_MODE_AUTO;
	} else {
		scrollYMode = ES_SCROLL_MODE_AUTO;
	}

	view->scroll.Setup(view, scrollXMode, scrollYMode, ES_SCROLL_X_DRAG | ES_SCROLL_Y_DRAG);
	ListViewCalculateTotalColumnWidth(view);

	// Remove existing visible items; the list will need to be repopulated.

	for (uintptr_t i = view->visibleItems.Length(); i > 0; i--) {
		view->visibleItems[i - 1].element->Destroy();
	}

	view->visibleItems.SetLength(0);

	// Remeasure each group.

	if (wasTiledView) {
		view->totalSize = 0;

		for (uintptr_t i = 0; i < view->groups.Length(); i++) {
			view->groups[i].totalSize = 0;
		}
	}

	int64_t spaceDelta = 0;

	for (uintptr_t i = 0; i < view->groups.Length(); i++) {
		if (!view->groups[i].itemCount) continue;
		spaceDelta -= view->groups[i].totalSize;
		view->groups[i].totalSize = view->MeasureItems(i, 0, view->groups[i].itemCount);
		view->groups[i].totalSize += view->style->gapMinor * (view->groups[i].itemCount - 1);
		spaceDelta += view->groups[i].totalSize;
	}

	view->InsertSpace(spaceDelta, 0);

	EsElementRelayout(view);
}

EsListView *EsListViewCreate(EsElement *parent, uint64_t flags, const EsStyle *style, 
		const EsStyle *itemStyle, const EsStyle *headerItemStyle, const EsStyle *footerItemStyle) {
	EsListView *view = (EsListView *) EsHeapAllocate(sizeof(EsListView), true);
	if (!view) return nullptr;

	view->primaryCellStyle = GetStyle(MakeStyleKey(ES_STYLE_LIST_PRIMARY_CELL, 0), false);
	view->secondaryCellStyle = GetStyle(MakeStyleKey(ES_STYLE_LIST_SECONDARY_CELL, 0), false);
	view->selectedCellStyle = GetStyle(MakeStyleKey(ES_STYLE_LIST_SELECTED_CHOICE_CELL, 0), false); // Only used for choice list views.

	view->Initialise(parent, flags | ES_ELEMENT_FOCUSABLE, ListViewProcessMessage, style ?: ES_STYLE_LIST_VIEW);
	view->cName = "list view";

	view->fixedItemSelection = -1;
	view->maximumItemsPerBand = INT_MAX;

	if (!itemStyle) {
		if (flags & ES_LIST_VIEW_CHOICE_SELECT) itemStyle = ES_STYLE_LIST_CHOICE_ITEM;
		else if (flags & ES_LIST_VIEW_TILED) itemStyle = ES_STYLE_LIST_ITEM_TILE;
		else itemStyle = ES_STYLE_LIST_ITEM;
	}

	EsListViewChangeStyles(view, nullptr, itemStyle, headerItemStyle ?: ES_STYLE_LIST_ITEM_GROUP_HEADER, 
			footerItemStyle ?: ES_STYLE_LIST_ITEM_GROUP_FOOTER, ES_FLAGS_DEFAULT, ES_FLAGS_DEFAULT);

	return view;
}

void EsListViewInsertGroup(EsListView *view, EsListViewIndex group, uint32_t flags) {
	EsMessageMutexCheck();

	// Add the group.

	ListViewGroup empty = { .flags = flags };
	EsAssert(group <= (EsListViewIndex) view->groups.Length()); // Invalid group index.

	if (!view->groups.Insert(empty, group)) {
		return;
	}

	// Update the group index on visible items.

	uintptr_t firstVisibleItemToMove = view->visibleItems.Length();

	for (uintptr_t i = 0; i < view->visibleItems.Length(); i++) {
		ListViewItem *item = &view->visibleItems[i];

		if (item->group >= group) {
			item->group++;

			if (i < firstVisibleItemToMove) {
				firstVisibleItemToMove = i;
			}
		}
	}

	// Insert gap between groups.

	view->InsertSpace(view->groups.Length() > 1 ? view->style->gapMajor : 0, firstVisibleItemToMove);

	// Create header and footer items.

	int64_t additionalItems = ((flags & ES_LIST_VIEW_GROUP_HAS_HEADER) ? 1 : 0) + ((flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) ? 1 : 0);
	EsListViewInsert(view, group, 0, additionalItems);
	view->groups[group].initialised = true;
}

void EsListViewInsert(EsListView *view, EsListViewIndex groupIndex, EsListViewIndex firstIndex, EsListViewIndex count) {
	EsMessageMutexCheck();
	if (!count) return;
	EsAssert(count > 0);

	// Get the group.

	EsAssert(groupIndex < (EsListViewIndex) view->groups.Length()); // Invalid group index.
	ListViewGroup *group = &view->groups[groupIndex];

	if (group->initialised) {
		if (group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) {
			EsAssert(firstIndex > 0); // Cannot insert before group header.
		}

		if (group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) {
			EsAssert(firstIndex < (intptr_t) group->itemCount); // Cannot insert after group footer.
		}
	}

	// Add the items to the group.

	bool addedFirstItemInGroup = !group->itemCount;
	group->itemCount += count;
	int64_t totalSizeOfItems = view->MeasureItems(groupIndex, firstIndex, count);
	int64_t sizeToAdd = (count - (addedFirstItemInGroup ? 1 : 0)) * view->style->gapMinor + totalSizeOfItems;
	group->totalSize += sizeToAdd;
	view->totalItemCount += count;

	// Update indices of visible items.

	uintptr_t firstVisibleItemToMove = view->visibleItems.Length();

	if (view->hasFocusedItem && view->focusedItemGroup == groupIndex) {
		if (view->focusedItemIndex >= firstIndex) {
			view->focusedItemIndex += count;
		}
	}

	if (view->hasAnchorItem && view->anchorItemGroup == groupIndex) {
		if (view->anchorItemIndex >= firstIndex) {
			view->anchorItemIndex += count;
		}
	}

	for (uintptr_t i = 0; i < view->visibleItems.Length(); i++) {
		ListViewItem *item = &view->visibleItems[i];

		if (item->group < groupIndex) {
			continue;
		} else if (item->group > groupIndex) {
			if (i < firstVisibleItemToMove) {
				firstVisibleItemToMove = i;
			}

			break;
		}

		if (item->index >= firstIndex) {
			item->index += count;

			if (i < firstVisibleItemToMove) {
				firstVisibleItemToMove = i;
			}
		}
	}

	// Insert the space for the items.

	view->InsertSpace(sizeToAdd, firstVisibleItemToMove);
}

void EsListViewRemove(EsListView *view, EsListViewIndex groupIndex, EsListViewIndex firstIndex, EsListViewIndex count) {
	EsMessageMutexCheck();
	if (!count) return;
	EsAssert(count > 0);

	// Get the group.

	EsAssert(groupIndex < (EsListViewIndex) view->groups.Length()); // Invalid group index.
	ListViewGroup *group = &view->groups[groupIndex];

	if (group->initialised) {
		if (group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) {
			EsAssert(firstIndex > 0); // Cannot remove the group header.
		}

		if (group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) {
			EsAssert(firstIndex + count < (intptr_t) group->itemCount); // Cannot remove the group footer.
		}
	}

	// Remove the items from the group.

	int64_t totalSizeOfItems = view->MeasureItems(groupIndex, firstIndex, count);
	int64_t sizeToRemove = (int64_t) group->itemCount == count ? group->totalSize 
		: (count * view->style->gapMinor + totalSizeOfItems);
	group->itemCount -= count;
	group->totalSize -= sizeToRemove;
	view->totalItemCount -= count;

	// Update indices of visible items,
	// and remove deleted items.

	uintptr_t firstVisibleItemToMove = view->visibleItems.Length();

	if (view->hasFocusedItem && view->focusedItemGroup == groupIndex) {
		if (view->focusedItemIndex >= firstIndex && view->focusedItemIndex < firstIndex + count) {
			view->hasFocusedItem = false;
		} else {
			view->focusedItemIndex -= count;
		}
	}

	if (view->hasAnchorItem && view->anchorItemGroup == groupIndex) {
		if (view->focusedItemIndex >= firstIndex && view->anchorItemIndex < firstIndex + count) {
			view->hasAnchorItem = false;
		} else {
			view->anchorItemIndex -= count;
		}
	}

	for (uintptr_t i = 0; i < view->visibleItems.Length(); i++) {
		ListViewItem *item = &view->visibleItems[i];

		if (item->group < groupIndex) {
			continue;
		} else if (item->group > groupIndex) {
			if (i < firstVisibleItemToMove) {
				firstVisibleItemToMove = i;
			}

			break;
		}

		if (item->index >= firstIndex + count) {
			item->index -= count;

			if (i < firstVisibleItemToMove) {
				firstVisibleItemToMove = i;
			}
		} else if (item->index >= firstIndex && item->index < firstIndex + count) {
			item->element->index = i;
			item->element->Destroy();
			view->visibleItems.Delete(i);
			i--;
		}
	}

	// Remove the space of the items.

	view->InsertSpace(-sizeToRemove, firstVisibleItemToMove);
}

void EsListViewRemoveAll(EsListView *view, EsListViewIndex group) {
	EsMessageMutexCheck();

	if (view->groups[group].itemCount) {
		EsListViewRemove(view, group, 0, view->groups[group].itemCount);
	}
}

int ListViewColumnHeaderItemMessage(EsElement *element, EsMessage *message) {
	EsListView *view = (EsListView *) element->parent->parent;

	if (message->type == ES_MSG_DESTROY) {
		return 0;
	}

	ListViewColumn *column = &view->registeredColumns[element->userData.u];

	if (message->type == ES_MSG_PAINT) {
		EsMessage m = { ES_MSG_LIST_VIEW_GET_COLUMN_SORT };
		m.getColumnSort.index = element->userData.u;
		int sort = EsMessageSend(view, &m);
		EsDrawContent(message->painter, element, element->GetBounds(), 
				column->title, column->titleBytes, 0, 
				sort == ES_LIST_VIEW_COLUMN_SORT_ASCENDING ? ES_DRAW_CONTENT_MARKER_UP_ARROW
				: sort == ES_LIST_VIEW_COLUMN_SORT_DESCENDING ? ES_DRAW_CONTENT_MARKER_DOWN_ARROW : ES_FLAGS_DEFAULT);
	} else if (message->type == ES_MSG_MOUSE_LEFT_CLICK && (column->flags & ES_LIST_VIEW_COLUMN_HAS_MENU)) {
		EsMessage m = { ES_MSG_LIST_VIEW_COLUMN_MENU };
		m.columnMenu.source = element;
		m.columnMenu.activeColumnIndex = element->userData.u;
		m.columnMenu.columnID = view->registeredColumns[element->userData.u].id;
		EsMessageSend(view, &m);
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
	} else {
		return 0;
	}

	return ES_HANDLED;
}

int ListViewColumnSplitterMessage(EsElement *element, EsMessage *message) {
	EsListView *view = (EsListView *) element->parent->parent;

	if (message->type == ES_MSG_DESTROY) {
		return 0;
	}

	ListViewColumn *column = &view->registeredColumns[element->userData.u];

	if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
		view->columnResizingOriginalWidth = column->width * theming.scale;
	} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
		int width = message->mouseDragged.newPositionX - message->mouseDragged.originalPositionX + view->columnResizingOriginalWidth;
		int minimumWidth = element->style->metrics->minimumWidth;
		if (width < minimumWidth) width = minimumWidth;
		column->width = width / theming.scale;
		ListViewCalculateTotalColumnWidth(view);
		EsElementRelayout(element->parent);
		EsElementRelayout(view);
	} else {
		return 0;
	}

	return ES_HANDLED;
}

void EsListViewAddAllColumns(EsListView *view) {
	EsElementDestroyContents(view->columnHeader);
	EsAssert(view->flags & ES_LIST_VIEW_COLUMNS); // List view does not have columns flag set.
	view->activeColumns.Free();

	for (uintptr_t i = 0; i < view->registeredColumns.Length(); i++) {
		view->activeColumns.Add(i);

		EsStyle *style = (view->registeredColumns[i].flags & ES_LIST_VIEW_COLUMN_HAS_MENU) ? ES_STYLE_LIST_COLUMN_HEADER_ITEM_HAS_MENU : ES_STYLE_LIST_COLUMN_HEADER_ITEM;
		EsElement *columnHeaderItem = EsCustomElementCreate(view->columnHeader, ES_CELL_FILL, style);
		columnHeaderItem->messageUser = ListViewColumnHeaderItemMessage;
		columnHeaderItem->cName = "column header item";
		columnHeaderItem->userData = i;

		EsElement *splitter = EsCustomElementCreate(view->columnHeader, ES_CELL_FILL, ES_STYLE_LIST_COLUMN_HEADER_SPLITTER);
		splitter->messageUser = ListViewColumnSplitterMessage;
		splitter->cName = "column header splitter";
		splitter->userData = i;
	}

	ListViewCalculateTotalColumnWidth(view);
	view->scroll.Refresh();
}

void EsListViewRegisterColumn(EsListView *view, uint32_t id, const char *title, ptrdiff_t titleBytes, uint32_t flags, double initialWidth) {
	EsMessageMutexCheck();

	if (!initialWidth) {
		initialWidth = (view->registeredColumns.Length() ? view->secondaryCellStyle : view->primaryCellStyle)->preferredWidth / theming.scale;
	}

	ListViewColumn column = {};
	column.id = id;
	column.flags = flags;
	column.width = initialWidth;
	if (titleBytes == -1) titleBytes = EsCStringLength(title);
	HeapDuplicate((void **) &column.title, &column.titleBytes, title, titleBytes);
	view->registeredColumns.Add(column);
}

void EsListViewContentChanged(EsListView *view) {
	EsMessageMutexCheck();

	view->searchBufferLastKeyTime = 0;
	view->searchBufferBytes = 0;

	view->scroll.SetX(0);
	view->scroll.SetY(0);

	view->hasScrollItem = false;
	view->useScrollItem = false;

	EsListViewInvalidateAll(view);
}

void EsListViewFocusItem(EsListView *view, EsListViewIndex group, EsListViewIndex index) {
	ListViewItem *oldFocus = view->FindVisibleItem(view->focusedItemGroup, view->focusedItemIndex);

	if (oldFocus) {
		oldFocus->element->customStyleState &= ~THEME_STATE_FOCUSED_ITEM;
		oldFocus->element->MaybeRefreshStyle();
	}

	view->hasFocusedItem = true;
	view->focusedItemGroup = group;
	view->focusedItemIndex = index;

	ListViewItem *newFocus = view->FindVisibleItem(view->focusedItemGroup, view->focusedItemIndex);

	if (newFocus) {
		newFocus->element->customStyleState |= THEME_STATE_FOCUSED_ITEM;
		newFocus->element->MaybeRefreshStyle();
	}

	view->EnsureItemVisible(group, index, ES_FLAGS_DEFAULT);
}

bool EsListViewGetFocusedItem(EsListView *view, EsListViewIndex *group, EsListViewIndex *index) {
	if (view->hasFocusedItem) {
		if (group) *group = view->focusedItemGroup;
		if (index) *index = view->focusedItemIndex;
	}

	return view->hasFocusedItem;
}

void EsListViewSelectNone(EsListView *view) {
	EsMessageMutexCheck();
	view->Select(-1, 0, false, false, false);
}

void EsListViewSelect(EsListView *view, EsListViewIndex group, EsListViewIndex index, bool addToExistingSelection) {
	EsMessageMutexCheck();

	if (addToExistingSelection) {
		view->SetSelected(group, index, group, index, true, false);
		view->UpdateVisibleItemsSelectionState();
	} else {
		view->Select(group, index, false, false, false);
	}
}

void EsListViewSetEmptyMessage(EsListView *view, const char *message, ptrdiff_t messageBytes) {
	EsMessageMutexCheck();
	if (messageBytes == -1) messageBytes = EsCStringLength(message);
	HeapDuplicate((void **) &view->emptyMessage, &view->emptyMessageBytes, message, messageBytes);

	if (!view->totalItemCount) {
		view->Repaint(true);
	}
}

EsListViewIndex EsListViewGetIndexFromItem(EsElement *_element, EsListViewIndex *group) {
	ListViewItemElement *element = (ListViewItemElement *) _element;
	EsListView *view = (EsListView *) element->parent;
	EsAssert(element->index < view->visibleItems.Length());
	if (group) *group = view->visibleItems[element->index].group;
	return view->visibleItems[element->index].index;
}

void EsListViewInvalidateAll(EsListView *view) {
	view->UpdateVisibleItemsSelectionState();
	view->Repaint(true);
}

void EsListViewInvalidateContent(EsListView *view, EsListViewIndex group, EsListViewIndex index) {
	for (uintptr_t i = 0; i < view->visibleItems.Length(); i++) {
		if (view->visibleItems[i].group == group && view->visibleItems[i].index == index) {
			view->UpdateVisibleItemSelectionState(i);
			view->visibleItems[i].element->Repaint(true);
			break;
		}
	}
}

#define LIST_VIEW_SORT_FUNCTION(_name, _line) \
	ES_MACRO_SORT(_name, EsListViewIndex, { \
		ListViewFixedItemData *left = (ListViewFixedItemData *) &context->items[*_left]; \
		ListViewFixedItemData *right = (ListViewFixedItemData *) &context->items[*_right]; \
		result = _line; \
	}, ListViewColumn *)

LIST_VIEW_SORT_FUNCTION(ListViewSortByStringsAscending, EsStringCompare(left->s.string, left->s.bytes, right->s.string, right->s.bytes));
LIST_VIEW_SORT_FUNCTION(ListViewSortByStringsDescending, -EsStringCompare(left->s.string, left->s.bytes, right->s.string, right->s.bytes));
LIST_VIEW_SORT_FUNCTION(ListViewSortByEnumsAscending, EsStringCompare(context->enumStrings[left->i].string, context->enumStrings[left->i].stringBytes, 
			context->enumStrings[right->i].string, context->enumStrings[right->i].stringBytes));
LIST_VIEW_SORT_FUNCTION(ListViewSortByEnumsDescending, -EsStringCompare(context->enumStrings[left->i].string, context->enumStrings[left->i].stringBytes, 
			context->enumStrings[right->i].string, context->enumStrings[right->i].stringBytes));
LIST_VIEW_SORT_FUNCTION(ListViewSortByIntegersAscending, left->i > right->i ? 1 : left->i == right->i ? 0 : -1);
LIST_VIEW_SORT_FUNCTION(ListViewSortByIntegersDescending, left->i < right->i ? 1 : left->i == right->i ? 0 : -1);
LIST_VIEW_SORT_FUNCTION(ListViewSortByDoublesAscending, left->d > right->d ? 1 : left->d == right->d ? 0 : -1);
LIST_VIEW_SORT_FUNCTION(ListViewSortByDoublesDescending, left->d < right->d ? 1 : left->d == right->d ? 0 : -1);

ListViewSortFunction ListViewGetSortFunction(ListViewColumn *column, uint8_t direction) {
	if ((column->flags & ES_LIST_VIEW_COLUMN_DATA_MASK) == ES_LIST_VIEW_COLUMN_DATA_STRINGS) {
		return (direction == LIST_SORT_DIRECTION_DESCENDING ? ListViewSortByStringsDescending : ListViewSortByStringsAscending);
	} else if ((column->flags & ES_LIST_VIEW_COLUMN_DATA_MASK) == ES_LIST_VIEW_COLUMN_DATA_INTEGERS) {
		if ((column->flags & ES_LIST_VIEW_COLUMN_FORMAT_MASK) == ES_LIST_VIEW_COLUMN_FORMAT_ENUM_STRING) {
			return (direction == LIST_SORT_DIRECTION_DESCENDING ? ListViewSortByEnumsDescending : ListViewSortByEnumsAscending);
		} else {
			return (direction == LIST_SORT_DIRECTION_DESCENDING ? ListViewSortByIntegersDescending : ListViewSortByIntegersAscending);
		}
	} else if ((column->flags & ES_LIST_VIEW_COLUMN_DATA_MASK) == ES_LIST_VIEW_COLUMN_DATA_DOUBLES) {
		return (direction == LIST_SORT_DIRECTION_DESCENDING ? ListViewSortByDoublesDescending : ListViewSortByDoublesAscending);
	} else {
		EsAssert(false);
	}

	return nullptr;
}

EsListViewIndex EsListViewFixedItemInsert(EsListView *view, EsGeneric data, EsListViewIndex index, uint32_t iconID) {
	EsAssert(view->flags & ES_LIST_VIEW_FIXED_ITEMS);

	if (!view->groups.Length()) {
		EsListViewInsertGroup(view, 0, ES_FLAGS_DEFAULT);
	}

	if (!view->registeredColumns.Length()) {
		EsListViewRegisterColumn(view, 0, nullptr, 0);
	}

	if (index == -1) {
		index = view->fixedItems.Length();
	}

	EsAssert(index >= 0 && index <= (intptr_t) view->fixedItems.Length());
	ListViewFixedItem item = {};
	item.data = data;
	item.iconID = iconID;
	view->fixedItems.Insert(item, index);
	view->fixedItemIndices.Insert(index, index);

	ListViewFixedItemData emptyData = {};

	for (uintptr_t i = 0; i < view->registeredColumns.Length(); i++) {
		ListViewColumn *column = &view->registeredColumns[i];

		if (column->items.Length() >= (uintptr_t) index) {
			column->items.InsertPointer(&emptyData, index);
		}
	}

	EsListViewInsert(view, 0, index, 1);

	return index;
}

void ListViewFixedItemSetInternal(EsListView *view, EsListViewIndex index, uint32_t columnID, ListViewFixedItemData data, uint32_t dataType) {
	EsAssert(view->flags & ES_LIST_VIEW_FIXED_ITEMS);
	EsMessageMutexCheck();
	EsAssert(index >= 0 && index < (intptr_t) view->fixedItems.Length());

	ListViewColumn *column = nullptr;

	for (uintptr_t i = 0; i < view->registeredColumns.Length(); i++) {
		if (view->registeredColumns[i].id == columnID) {
			column = &view->registeredColumns[i];
			break;
		}
	}

	EsAssert(column);
	EsAssert((column->flags & ES_LIST_VIEW_COLUMN_DATA_MASK) == dataType);

	// Make sure that the column's array of items has been updated to match to the size of fixedItems.
	if (column->items.Length() < view->fixedItems.Length()) {
		uintptr_t oldLength = column->items.Length();
		column->items.SetLength(view->fixedItems.Length());
		EsMemoryZero(&column->items[oldLength], (view->fixedItems.Length() - oldLength) * sizeof(column->items[0]));
	}

	bool changed = false;

	if (dataType == ES_LIST_VIEW_COLUMN_DATA_STRINGS) {
		changed = EsStringCompareRaw(column->items[index].s.string, column->items[index].s.bytes, data.s.string, data.s.bytes);
	} else if (dataType == ES_LIST_VIEW_COLUMN_DATA_DOUBLES) {
		changed = column->items[index].d != data.d;
	} else if (dataType == ES_LIST_VIEW_COLUMN_DATA_INTEGERS) {
		changed = column->items[index].i != data.i;
	} else {
		EsAssert(false);
	}

	if (dataType == ES_LIST_VIEW_COLUMN_DATA_STRINGS) {
		EsHeapFree(column->items[index].s.string);
	}

	column->items[index] = data;

	if (changed) {
		for (uintptr_t i = 0; i < view->fixedItemIndices.Length(); i++) {
			if (view->fixedItemIndices[i] == index) {
				EsListViewInvalidateContent(view, 0, i);
				break;
			}
		}
	}
}

void EsListViewFixedItemSetString(EsListView *view, EsListViewIndex index, uint32_t columnID, const char *string, ptrdiff_t stringBytes) {
	ListViewFixedString fixedString = {};
	fixedString.bytes = stringBytes == -1 ? EsCStringLength(string) : stringBytes;
	size_t outBytes;
	HeapDuplicate((void **) &fixedString, &outBytes, string, fixedString.bytes);

	if (outBytes == fixedString.bytes) {
		ListViewFixedItemSetInternal(view, index, columnID, { .s = fixedString }, ES_LIST_VIEW_COLUMN_DATA_STRINGS);
	}
}

void EsListViewFixedItemSetDouble(EsListView *view, EsListViewIndex index, uint32_t columnID, double number) {
	ListViewFixedItemSetInternal(view, index, columnID, { .d = number }, ES_LIST_VIEW_COLUMN_DATA_DOUBLES);
}

void EsListViewFixedItemSetInteger(EsListView *view, EsListViewIndex index, uint32_t columnID, int64_t number) {
	ListViewFixedItemSetInternal(view, index, columnID, { .i = number }, ES_LIST_VIEW_COLUMN_DATA_INTEGERS);
}

bool EsListViewFixedItemFindIndex(EsListView *view, EsGeneric data, EsListViewIndex *index) {
	EsAssert(view->flags & ES_LIST_VIEW_FIXED_ITEMS);
	EsMessageMutexCheck();

	for (uintptr_t i = 0; i < view->fixedItemIndices.Length(); i++) {
		if (view->fixedItems[view->fixedItemIndices[i]].data == data) {
			*index = i;
			return true;
		}
	}

	return false;
}

bool EsListViewFixedItemSelect(EsListView *view, EsGeneric data) {
	EsAssert(view->flags & ES_LIST_VIEW_FIXED_ITEMS);
	EsMessageMutexCheck();
	EsListViewIndex index;
	bool found = EsListViewFixedItemFindIndex(view, data, &index);

	if (found) {
		EsListViewSelect(view, 0, index);

		// TODO Maybe you should have to separately call EsListViewFocusItem to get this behaviour?
		EsListViewFocusItem(view, 0, index);
		view->EnsureItemVisible(0, index, ENSURE_VISIBLE_ALIGN_CENTER);
	}

	return found;
}

bool EsListViewFixedItemRemove(EsListView *view, EsGeneric data) {
	EsAssert(view->flags & ES_LIST_VIEW_FIXED_ITEMS);
	EsMessageMutexCheck();
	EsListViewIndex index;
	bool found = EsListViewFixedItemFindIndex(view, data, &index);

	if (found) {
		EsListViewRemove(view, 0, index, 1);
		EsListViewIndex fixedIndex = view->fixedItemIndices[index];

		for (uintptr_t i = 0; i < view->registeredColumns.Length(); i++) {
			ListViewColumn *column = &view->registeredColumns[i];

			if ((uintptr_t) fixedIndex < column->items.Length()) {
				if ((column->flags & ES_LIST_VIEW_COLUMN_DATA_MASK) == ES_LIST_VIEW_COLUMN_DATA_STRINGS) {
					EsHeapFree(column->items[fixedIndex].s.string);
				}

				column->items.Delete(fixedIndex);

				if (!column->items.Length()) {
					column->items.Free();
				}
			}
		}

		view->fixedItems.Delete(fixedIndex);
		view->fixedItemIndices.Delete(index);

		for (uintptr_t i = 0; i < view->fixedItemIndices.Length(); i++) {
			if (view->fixedItemIndices[i] > fixedIndex) {
				view->fixedItemIndices[i]--;
			}
		}
	}

	return found;
}
	
bool EsListViewFixedItemGetSelected(EsListView *view, EsGeneric *data) {
	EsAssert(view->flags & ES_LIST_VIEW_FIXED_ITEMS);
	EsMessageMutexCheck();

	if (view->fixedItemSelection == -1 || view->fixedItemSelection >= (intptr_t) view->fixedItems.Length()) {
		return false;
	} else {
		*data = view->fixedItems[view->fixedItemIndices[view->fixedItemSelection]].data;
		return true;
	}
}

void EsListViewFixedItemSetEnumStringsForColumn(EsListView *view, uint32_t columnID, const EsListViewEnumString *strings, size_t stringCount) {
	for (uintptr_t i = 0; i < view->registeredColumns.Length(); i++) {
		if (view->registeredColumns[i].id == columnID) {
			view->registeredColumns[i].enumStrings = strings;
			view->registeredColumns[i].enumStringCount = stringCount;
			EsListViewInvalidateAll(view);
			return;
		}
	}

	EsAssert(false);
}

void EsListViewFixedItemSortAll(EsListView *view) {
	ListViewColumn *column = nullptr;

	for (uintptr_t i = 0; i < view->registeredColumns.Length(); i++) {
		if (view->registeredColumns[i].id == view->fixedItemSortColumnID) {
			column = &view->registeredColumns[i];
			break;
		}
	}

	EsAssert(column);

	EsAssert(view->fixedItems.Length() == view->fixedItemIndices.Length());

	EsListViewIndex previousSelectionIndex = view->fixedItemSelection >= 0 && (uintptr_t) view->fixedItemSelection < view->fixedItemIndices.Length() 
		? view->fixedItemIndices[view->fixedItemSelection] : -1;

	ListViewSortFunction sortFunction = ListViewGetSortFunction(column, view->fixedItemSortDirection);
	sortFunction(view->fixedItemIndices.array, view->fixedItems.Length(), column);
	EsListViewInvalidateAll(view);

	if (previousSelectionIndex != -1) {
		for (uintptr_t i = 0; i < view->fixedItemIndices.Length(); i++) {
			if (view->fixedItemIndices[i] == previousSelectionIndex) {
				EsListViewSelect(view, 0, i);
				EsListViewFocusItem(view, 0, i);
				view->EnsureItemVisible(0, i, ENSURE_VISIBLE_ALIGN_CENTER);
				break;
			}
		}
	}
}

void ListViewSetSortDirection(EsListView *view, uint32_t columnID, uint8_t direction) {
	if (view->fixedItemSortColumnID != columnID || view->fixedItemSortDirection != direction) {
		view->fixedItemSortColumnID = columnID;
		view->fixedItemSortDirection = direction;
		EsListViewFixedItemSortAll(view);
	}
}

void ListViewSetSortAscending(EsMenu *menu, EsGeneric context) {
	ListViewSetSortDirection((EsListView *) menu->userData.p, context.u, LIST_SORT_DIRECTION_ASCENDING);
}

void ListViewSetSortDescending(EsMenu *menu, EsGeneric context) {
	ListViewSetSortDirection((EsListView *) menu->userData.p, context.u, LIST_SORT_DIRECTION_DESCENDING);
}

int ListViewInlineTextboxMessage(EsElement *element, EsMessage *message) {
	int response = ProcessTextboxMessage(element, message);

	if (message->type == ES_MSG_DESTROY) {
		EsListView *view = (EsListView *) EsElementGetLayoutParent(element);	
		view->inlineTextbox = nullptr;
		ListViewItem *item = view->FindVisibleItem(view->inlineTextboxGroup, view->inlineTextboxIndex);
		if (item) EsElementRepaint(item->element);
		EsElementFocus(view);
	}

	return response;
}

EsTextbox *EsListViewCreateInlineTextbox(EsListView *view, EsListViewIndex group, EsListViewIndex index, uint32_t flags) {
	if (view->inlineTextbox) {
		view->inlineTextbox->Destroy();
	}

	view->inlineTextboxGroup = group;
	view->inlineTextboxIndex = index;
	view->EnsureItemVisible(group, index, ENSURE_VISIBLE_ALIGN_TOP);

	uint64_t textboxFlags = ES_CELL_FILL | ES_TEXTBOX_EDIT_BASED | ES_TEXTBOX_ALLOW_TABS;
	
	if (flags & ES_LIST_VIEW_INLINE_TEXTBOX_REJECT_EDIT_IF_FOCUS_LOST) {
		textboxFlags |= ES_TEXTBOX_REJECT_EDIT_IF_LOST_FOCUS;
	}

	view->inlineTextbox = EsTextboxCreate(view, textboxFlags, ES_STYLE_TEXTBOX_INLINE);

	if (!view->inlineTextbox) {
		return nullptr;
	}

	EsAssert(view->inlineTextbox->messageClass == ProcessTextboxMessage);
	view->inlineTextbox->messageClass = ListViewInlineTextboxMessage;

	if (flags & ES_LIST_VIEW_INLINE_TEXTBOX_COPY_EXISTING_TEXT) {
		EsMessage m = { ES_MSG_LIST_VIEW_GET_CONTENT };
		uint8_t _buffer[256];
		EsBuffer buffer = { .out = _buffer, .bytes = sizeof(_buffer) };
		m.getContent.buffer = &buffer;
		m.getContent.index = index;
		m.getContent.group = group;
		EsMessageSend(view, &m);
		EsTextboxInsert(view->inlineTextbox, (char *) _buffer, buffer.position);
		EsTextboxSelectAll(view->inlineTextbox);
	}

	if (view->searchBufferBytes) {
		view->searchBufferBytes = 0;
		EsElementRepaint(view);
	}

	EsElementRelayout(view);
	EsElementFocus(view->inlineTextbox);
	EsTextboxStartEdit(view->inlineTextbox);

	return view->inlineTextbox;
}

void EsListViewScrollToEnd(EsListView *view) {
	if (view->flags & ES_LIST_VIEW_HORIZONTAL) {
		view->scroll.SetX(view->scroll.limit[0]);
	} else {
		view->scroll.SetY(view->scroll.limit[1]);
	}
}

EsListViewEnumeratedVisibleItem *EsListViewEnumerateVisibleItems(EsListView *view, size_t *count) {
	EsMessageMutexCheck();
	*count = view->visibleItems.Length();
	EsListViewEnumeratedVisibleItem *result = (EsListViewEnumeratedVisibleItem *) EsHeapAllocate(sizeof(EsListViewEnumeratedVisibleItem) * *count, true);

	if (!result) {
		*count = 0;
		return nullptr;
	}

	for (uintptr_t i = 0; i < *count; i++) {
		result[i].element = view->visibleItems[i].element;
		result[i].group = view->visibleItems[i].group;
		result[i].index = view->visibleItems[i].index;
	}

	return result;
}

void EsListViewSetMaximumItemsPerBand(EsListView *view, int maximumItemsPerBand) {
	view->maximumItemsPerBand = maximumItemsPerBand;
}

EsPoint EsListViewGetAnnouncementPointForSelection(EsListView *view) {
	EsRectangle viewWindowBounds = EsElementGetWindowBounds(view);
	EsRectangle bounding = viewWindowBounds;
	bool first = true;

	for (uintptr_t i = 0; i < view->visibleItems.Length(); i++) {
		if (~view->visibleItems[i].element->customStyleState & THEME_STATE_SELECTED) continue;
		EsRectangle bounds = EsElementGetWindowBounds(view->visibleItems[i].element);
		if (first) bounding = bounds;
		else bounding = EsRectangleBounding(bounding, bounds);
		first = false;
	}

	bounding = EsRectangleIntersection(bounding, viewWindowBounds);
	return ES_POINT((bounding.l + bounding.r) / 2, (bounding.t + bounding.b) / 2);
}
