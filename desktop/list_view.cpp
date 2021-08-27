// TODO RMB click/drag.
// TODO Consistent int64_t/intptr_t.
// TODO Drag and drop.
// TODO GetFirstIndex/GetLastIndex assume that every group is non-empty.

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
	ListViewFixedString firstColumn;
	Array<ListViewFixedString> otherColumns;
	EsGeneric data;
	uint32_t iconID;
};

int ListViewProcessItemMessage(EsElement *element, EsMessage *message);

struct EsListView : EsElement {
	ScrollPane scroll;

	uint64_t totalItemCount;
	uint64_t totalSize;
	Array<ListViewGroup> groups;
	Array<ListViewItem> visibleItems;

	const EsStyle *itemStyle, *headerItemStyle, *footerItemStyle;
	int64_t fixedWidth, fixedHeight;
	int64_t fixedHeaderSize, fixedFooterSize;

	// TODO Updating these when the style changes.
	UIStyle *primaryCellStyle;
	UIStyle *secondaryCellStyle;
	UIStyle *selectedCellStyle;

	bool hasFocusedItem;
	EsListViewIndex focusedItemGroup;
	EsListViewIndex focusedItemIndex;

	bool hasAnchorItem;
	EsListViewIndex anchorItemGroup;
	EsListViewIndex anchorItemIndex;

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
	EsListViewColumn *columns;
	size_t columnCount;
	int columnResizingOriginalWidth;
	// TODO Updating this when the style changes.
	int64_t totalColumnWidth;

	EsTextbox *inlineTextbox;
	EsListViewIndex inlineTextboxGroup;
	EsListViewIndex inlineTextboxIndex;

	int maximumItemsPerBand;

	// Fixed item storage:
	Array<ListViewFixedItem> fixedItems;
	ptrdiff_t fixedItemSelection;

	inline EsRectangle GetListBounds() {
		EsRectangle bounds = GetBounds();

		if (columnHeader) {
			bounds.t += columnHeader->currentStyle->preferredHeight;
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
		int64_t gapBetweenGroup = currentStyle->gapMajor,
			gapBetweenItems = (flags & ES_LIST_VIEW_TILED) ? currentStyle->gapWrap : currentStyle->gapMinor,
			fixedSize       = (flags & ES_LIST_VIEW_VARIABLE_SIZE) ? 0 : (flags & ES_LIST_VIEW_HORIZONTAL ? fixedWidth : fixedHeight),
			startInset 	= flags & ES_LIST_VIEW_HORIZONTAL ? currentStyle->insets.l : currentStyle->insets.t;

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

	void EnsureItemVisible(EsListViewIndex groupIndex, EsListViewIndex index, bool alignTop) {
		EsRectangle contentBounds = GetListBounds();

		int64_t startInset = flags & ES_LIST_VIEW_HORIZONTAL ? currentStyle->insets.l : currentStyle->insets.t,
			endInset = flags & ES_LIST_VIEW_HORIZONTAL ? currentStyle->insets.r : currentStyle->insets.b,
			contentSize = flags & ES_LIST_VIEW_HORIZONTAL ? Width(contentBounds) : Height(contentBounds);

		int64_t position, itemSize;
		GetItemPosition(groupIndex, index, &position, &itemSize);

		if (position >= 0 && position + itemSize <= contentSize - endInset) {
			return;
		}

		if (alignTop) {
			if (flags & ES_LIST_VIEW_HORIZONTAL) {
				scroll.SetX(scroll.position[0] + position - startInset);
			} else {
				scroll.SetY(scroll.position[1] + position - startInset);
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
		int64_t gapBetweenGroup = currentStyle->gapMajor,
			gapBetweenItems = (flags & ES_LIST_VIEW_TILED) ? currentStyle->gapWrap : currentStyle->gapMinor,
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

			intptr_t band = -position / (fixedSize + gapBetweenItems);
			if (band < 0) band = 0;
			position += band * (fixedSize + gapBetweenItems);

			if (flags & ES_LIST_VIEW_TILED) {
				band *= GetItemsPerBand();
			}

			index.iterateIndex.index = band + addHeader;

			if (index.iterateIndex.index >= (intptr_t) group->itemCount) {
				index.iterateIndex.index = group->itemCount - 1;
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

	void Populate() {
		// TODO Keep one item before and after the viewport, so tab traversal on custom elements works.
		// TODO Always keep an item if it has FOCUS_WITHIN.
		// 	- But maybe we shouldn't allow focusable elements in a list view.

		if (!totalItemCount) {
			return;
		}

		EsRectangle contentBounds = GetListBounds();
		int64_t contentSize = flags & ES_LIST_VIEW_HORIZONTAL ? Width(contentBounds) : Height(contentBounds);
		int64_t scroll = EsCRTfloor(flags & ES_LIST_VIEW_HORIZONTAL ? (this->scroll.position[0] - currentStyle->insets.l) 
				: (this->scroll.position[1] - currentStyle->insets.t));

		int64_t position = 0;
		bool noItems = false;
		EsMessage currentItem = FindFirstVisibleItem(&position, -scroll, visibleItems.Length() ? visibleItems.array : nullptr, &noItems);
		uintptr_t visibleIndex = 0;

		int64_t wrapLimit = GetWrapLimit();
		int64_t fixedMinorSize = (flags & ES_LIST_VIEW_HORIZONTAL) ? fixedHeight : fixedWidth;
		intptr_t itemsPerBand = GetItemsPerBand();
		intptr_t itemInBand = 0;
		int64_t computedMinorGap = currentStyle->gapMinor;
		int64_t minorPosition = 0;
		int64_t centerOffset = (flags & ES_LIST_VIEW_CENTER_TILES) 
			? (wrapLimit - itemsPerBand * (fixedMinorSize + currentStyle->gapMinor) + currentStyle->gapMinor) / 2 : 0;

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
						EsPrint("Item in unexpected position: expected %d, got %d; index %d, scroll %d.\n", 
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
							position + contentBounds.l, minorPosition + currentStyle->insets.t + contentBounds.t + centerOffset);
				} else {
					visibleItem->element->InternalMove(fixedWidth, fixedHeight, 
							minorPosition + currentStyle->insets.l + contentBounds.l + centerOffset, position + contentBounds.t);
				}

				minorPosition += computedMinorGap + fixedMinorSize;
				itemInBand++;

				bool endOfGroup = ((group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) && currentItem.iterateIndex.index == group->itemCount - 2)
					|| (currentItem.iterateIndex.index == group->itemCount - 1);

				if (itemInBand == itemsPerBand || endOfGroup) {
					minorPosition = 0;
					itemInBand = 0;
					position += (flags & ES_LIST_VIEW_HORIZONTAL) ? visibleItem->element->width : visibleItem->element->height;
					if (!endOfGroup || (group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER)) position += currentStyle->gapWrap;
				}
			} else {
				if (flags & ES_LIST_VIEW_HORIZONTAL) {
					visibleItem->element->InternalMove(
						visibleItem->size, 
						Height(contentBounds) - currentStyle->insets.t - currentStyle->insets.b - visibleItem->indent * currentStyle->gapWrap,
						position + contentBounds.l, 
						currentStyle->insets.t - this->scroll.position[1] + visibleItem->indent * currentStyle->gapWrap + contentBounds.t);
					position += visibleItem->element->width;
				} else if ((flags & ES_LIST_VIEW_COLUMNS) && ((~flags & ES_LIST_VIEW_CHOICE_SELECT) || (this->scroll.autoScrollbars[0]))) {
					int indent = visibleItem->indent * currentStyle->gapWrap;
					int firstColumn = columns[0].width + secondaryCellStyle->gapMajor;
					visibleItem->startAtSecondColumn = indent > firstColumn;
					if (indent > firstColumn) indent = firstColumn;
					visibleItem->element->InternalMove(totalColumnWidth - indent, visibleItem->size, 
						indent - this->scroll.position[0] + contentBounds.l + currentStyle->insets.l, position + contentBounds.t);
					position += visibleItem->element->height;
				} else {
					int indent = visibleItem->indent * currentStyle->gapWrap + currentStyle->insets.l;
					visibleItem->element->InternalMove(Width(contentBounds) - indent - currentStyle->insets.r, visibleItem->size, 
						indent + contentBounds.l - this->scroll.position[0], position + contentBounds.t);
					position += visibleItem->element->height;
				}

				if ((flags & ES_LIST_VIEW_TILED) && (group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) && currentItem.iterateIndex.index == 0) {
					position += currentStyle->gapWrap;
				}
			}

			// Go to the next item.

			visibleIndex++;
			EsListViewIndex previousGroup = currentItem.iterateIndex.group;
			if (!IterateForwards(&currentItem)) break;
			position += previousGroup == currentItem.iterateIndex.group ? (flags & ES_LIST_VIEW_TILED ? 0 : currentStyle->gapMinor) : currentStyle->gapMajor;
		}

		while (visibleIndex < visibleItems.Length()) {
			// Remove visible items no longer visible, after the viewport.

			ListViewItem *visibleItem = &visibleItems[visibleIndex];
			visibleItem->element->index = visibleIndex;
			visibleItem->element->Destroy();
			visibleItems.Delete(visibleIndex);
		}
	}

	void Wrap(bool autoScroll) {
		if (~flags & ES_LIST_VIEW_TILED) return;

		totalSize = 0;

		intptr_t itemsPerBand = GetItemsPerBand();

		for (uintptr_t i = 0; i < groups.Length(); i++) {
			ListViewGroup *group = &groups[i];
			int64_t groupSize = 0;

			intptr_t itemCount = group->itemCount;

			if (group->flags & ES_LIST_VIEW_GROUP_HAS_HEADER) {
				groupSize += fixedHeaderSize + currentStyle->gapWrap;
				itemCount--;
			}

			if (group->flags & ES_LIST_VIEW_GROUP_HAS_FOOTER) {
				groupSize += fixedFooterSize + currentStyle->gapWrap;
				itemCount--;
			}

			intptr_t bandsInGroup = (itemCount + itemsPerBand - 1) / itemsPerBand;
			groupSize += (((flags & ES_LIST_VIEW_HORIZONTAL) ? fixedWidth : fixedHeight) + currentStyle->gapWrap) * bandsInGroup;
			groupSize -= currentStyle->gapWrap;
			group->totalSize = groupSize;

			totalSize += groupSize + (group == &groups.Last() ? 0 : currentStyle->gapMajor);
		}

		scroll.Refresh();

		if (visibleItems.Length() && autoScroll) {
			EnsureItemVisible(visibleItems[0].group, visibleItems[0].index, true);
		}
	}

	void InsertSpace(int64_t space, uintptr_t beforeItem) {
		if (!space) return;

		if (flags & ES_LIST_VIEW_TILED) {
			EsElementUpdateContentSize(this);
			return;
		}

		int64_t currentScroll = (flags & ES_LIST_VIEW_HORIZONTAL) ? scroll.position[0] : scroll.position[1];
		int64_t scrollLimit = (flags & ES_LIST_VIEW_HORIZONTAL) ? scroll.limit[0] : scroll.limit[1];

		totalSize += space;

		if (((beforeItem == 0 && currentScroll) || currentScroll == scrollLimit) && firstLayout && space > 0 && scrollLimit) {
			scroll.Refresh();

			if (flags & ES_LIST_VIEW_HORIZONTAL) {
				scroll.SetX(scroll.position[0] + space, false);
			} else {
				scroll.SetY(scroll.position[1] + space, false);
			}
		} else {
			for (uintptr_t i = beforeItem; i < visibleItems.Length(); i++) {
				ListViewItem *item = &visibleItems[i];

				if (flags & ES_LIST_VIEW_HORIZONTAL) {
					item->element->offsetX += space;
				} else {
					item->element->offsetY += space;
				}
			}

			scroll.Refresh();
		}

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
				fixedItemSelection = m.selectItem.index;
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
			intptr_t itemsPerBand = fixedMinorSize && ((fixedMinorSize + currentStyle->gapMinor) < wrapLimit) 
				? (wrapLimit / (fixedMinorSize + currentStyle->gapMinor)) : 1;
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
			if (y1 >= contentBounds.b - currentStyle->insets.b || y2 < contentBounds.t + currentStyle->insets.t) {
				return;
			}
		} else if (flags & ES_LIST_VIEW_COLUMNS) {
			if (x1 >= contentBounds.l + currentStyle->insets.l + totalColumnWidth || x2 < contentBounds.l + currentStyle->insets.l) {
				return;
			}
		} else {
			if (x1 >= contentBounds.r - currentStyle->insets.r || x2 < contentBounds.l + currentStyle->insets.l) {
				return;
			}
		}

		// TODO Use reference for FindFirstVisibleItem.

		bool adjustStart = false, adjustEnd = false;
		int r1 = (flags & ES_LIST_VIEW_HORIZONTAL) ? currentStyle->insets.l - x1 : currentStyle->insets.t - y1 + scroll.fixedViewport[1];
		int r2 = (flags & ES_LIST_VIEW_HORIZONTAL) ? currentStyle->insets.l - x2 : currentStyle->insets.t - y2 + scroll.fixedViewport[1];
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
			int64_t computedMinorGap = (wrapLimit - itemsPerBand * fixedMinorSize) / (itemsPerBand + 1);
			int64_t minorStartOffset = computedMinorGap + ((flags & ES_LIST_VIEW_HORIZONTAL) ? currentStyle->insets.t : currentStyle->insets.l);
			intptr_t startInBand = (((flags & ES_LIST_VIEW_HORIZONTAL) ? y1 : x1) - minorStartOffset) / (fixedMinorSize + computedMinorGap);
			intptr_t endInBand = (((flags & ES_LIST_VIEW_HORIZONTAL) ? y2 : x2) - minorStartOffset) / (fixedMinorSize + computedMinorGap);

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
				EsRectangle bounds = EsRectangleAddBorder(element->GetBounds(), element->currentStyle->insets);

				for (uintptr_t i = item->startAtSecondColumn ? 1 : 0; i < columnCount; i++) {
					m.getContent.column = i;
					m.getContent.icon = 0;
					buffer.position = 0;

					bounds.r = bounds.l + columns[i].width 
						- element->currentStyle->insets.r - element->currentStyle->insets.l;

					if (i == 0) {
						bounds.r -= item->indent * currentStyle->gapWrap;
					}

					EsRectangle drawBounds = { bounds.l + message->painter->offsetX, bounds.r + message->painter->offsetX,
						bounds.t + message->painter->offsetY, bounds.b + message->painter->offsetY };

					if (EsRectangleClip(drawBounds, message->painter->clip, nullptr) 
							&& ES_HANDLED == EsMessageSend(this, &m)) {
						bool useSelectedCellStyle = (item->element->customStyleState & THEME_STATE_SELECTED) && (flags & ES_LIST_VIEW_CHOICE_SELECT);
						UIStyle *style = useSelectedCellStyle ? selectedCellStyle : i ? secondaryCellStyle : primaryCellStyle;

						uint8_t previousTextAlign = style->textAlign;

						if (columns[i].flags & ES_LIST_VIEW_COLUMN_RIGHT_ALIGNED) {
							style->textAlign ^= ES_TEXT_H_RIGHT | ES_TEXT_H_LEFT;
						}

						style->PaintText(message->painter, element, bounds, 
								(char *) _buffer, buffer.position, m.getContent.icon,
								(columns[i].flags & ES_LIST_VIEW_COLUMN_TABULAR) ? ES_DRAW_CONTENT_TABULAR : ES_FLAGS_DEFAULT, 
								i ? nullptr : &selection);
						style->textAlign = previousTextAlign;
					}

					bounds.l += columns[i].width + secondaryCellStyle->gapMajor;

					if (i == 0) {
						bounds.l -= item->indent * currentStyle->gapWrap;
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
					m.getContent.richText ? ES_DRAW_CONTENT_RICH_TEXT : ES_FLAGS_DEFAULT,
					&selection);
			}
		} else if (message->type == ES_MSG_LAYOUT) {
			if (element->GetChildCount()) {
				EsElement *child = element->GetChild(0);
				EsRectangle bounds = element->GetBounds();
				child->InternalMove(bounds.r - bounds.l, bounds.b - bounds.t, bounds.l, bounds.t);
			}
		} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
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

			Select(item->group, item->index, EsKeyboardIsShiftHeld(), EsKeyboardIsCtrlHeld(), false);

			if (message->mouseDown.clickChainCount == 2 && !EsKeyboardIsShiftHeld() && !EsKeyboardIsCtrlHeld()) {
				EsMessage m = { ES_MSG_LIST_VIEW_CHOOSE_ITEM };
				m.chooseItem.group = item->group;
				m.chooseItem.index = item->index;
				EsMessageSend(this, &m);
			}
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
			? bounds.b - bounds.t - currentStyle->insets.b - currentStyle->insets.t 
			: bounds.r - bounds.l - currentStyle->insets.r - currentStyle->insets.l;
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

				if (EsStringStartsWith((char *) _buffer, buffer.position, searchBuffer, searchBufferBytes, true)) {
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
			EnsureItemVisible(focusedItemGroup, focusedItemIndex, false);
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
				bool shouldShowSearchHighlight = EsStringStartsWith((char *) _buffer, buffer.position, searchBuffer, searchBufferBytes, true);

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
			EnsureItemVisible(focusedItemGroup, focusedItemIndex, isPrevious || isHome || isPageUp || isPreviousBand);
			Select(focusedItemGroup, focusedItemIndex, shift, ctrl, ctrl && !shift);
			return true;
		} else if (isSpace && ctrl && !shift && hasFocusedItem) {
			Select(focusedItemGroup, focusedItemIndex, false, true, false);
			return true;
		} else if (isEnter && hasFocusedItem && !shift && !ctrl && !alt) {
			if (searchBufferBytes) {
				searchBufferLastKeyTime = 0;
				searchBufferBytes = 0;
				EsListViewInvalidateAll(this);
			}

			EsMessage m = { ES_MSG_LIST_VIEW_CHOOSE_ITEM };
			m.chooseItem.group = focusedItemGroup;
			m.chooseItem.index = focusedItemIndex;
			EsMessageSend(this, &m);
			return true;
		} else if (!ctrl && !alt) {
			uint64_t currentTime = EsTimeStamp() / (api.systemConstants[ES_SYSTEM_CONSTANT_TIME_STAMP_UNITS_PER_MICROSECOND] * 1000);

			if (searchBufferLastKeyTime + GetConstantNumber("listViewSearchBufferTimeout") < currentTime) {
				searchBufferBytes = 0;
			}

			StartAnimating();
			searchBufferLastKeyTime = currentTime;
			int ic, isc;
			ConvertScancodeToCharacter(scancode, &ic, &isc, false, false);
			int character = shift ? isc : ic;

			if (character != -1 && searchBufferBytes + 4 < sizeof(searchBuffer)) {
				utf8_encode(character, searchBuffer + searchBufferBytes);
				size_t previousSearchBufferBytes = searchBufferBytes;
				searchBufferBytes += utf8_length_char(searchBuffer + searchBufferBytes);
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
		UIStyle *style = item->element->currentStyle;

		if (flags & ES_LIST_VIEW_COLUMNS) {
			int offset = primaryCellStyle->metrics->iconSize + primaryCellStyle->gapMinor 
				+ style->insets.l - inlineTextbox->currentStyle->insets.l;
			inlineTextbox->InternalMove(columns[0].width - offset, item->element->height, 
					item->element->offsetX + offset, item->element->offsetY);
		} else if (flags & ES_LIST_VIEW_TILED) {
			if (style->metrics->layoutVertical) {
				int height = inlineTextbox->currentStyle->preferredHeight;
				int textStart = style->metrics->iconSize + style->gapMinor + style->insets.t;
				int textEnd = item->element->height - style->insets.b;
				int offset = (textStart + textEnd - height) / 2;
				inlineTextbox->InternalMove(item->element->width - style->insets.r - style->insets.l, height, 
						item->element->offsetX + style->insets.l, item->element->offsetY + offset);
			} else {
				int textboxInset = inlineTextbox->currentStyle->insets.l;
				int offset = style->metrics->iconSize + style->gapMinor 
					+ style->insets.l - textboxInset;
				int height = inlineTextbox->currentStyle->preferredHeight;
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
		scroll.ReceivedMessage(message);

		if (message->type == ES_MSG_GET_WIDTH || message->type == ES_MSG_GET_HEIGHT) {
			if (flags & ES_LIST_VIEW_HORIZONTAL) {
				message->measure.width  = totalSize + currentStyle->insets.l + currentStyle->insets.r;
			} else {
				message->measure.height = totalSize + currentStyle->insets.t + currentStyle->insets.b;

				if (flags & ES_LIST_VIEW_COLUMNS) {
					message->measure.width = totalColumnWidth + currentStyle->insets.l + currentStyle->insets.r;
					message->measure.height += columnHeader->currentStyle->preferredHeight;
				}
			}
		} else if (message->type == ES_MSG_LAYOUT) {
			firstLayout = true;
			Wrap(message->layout.sizeChanged);
			Populate();

			if (columnHeader) {
				EsRectangle bounds = GetBounds();
				columnHeader->InternalMove(Width(bounds), columnHeader->currentStyle->preferredHeight, 0, 0);
			}

			if (inlineTextbox) {
				ListViewItem *item = FindVisibleItem(inlineTextboxGroup, inlineTextboxIndex);
				if (item) MoveInlineTextbox(item);
			}
		} else if (message->type == ES_MSG_SCROLL_X || message->type == ES_MSG_SCROLL_Y) {
			int64_t delta = message->scrollbarMoved.scroll - message->scrollbarMoved.previous;

			if ((message->type == ES_MSG_SCROLL_X) == ((flags & ES_LIST_VIEW_HORIZONTAL) ? true : false)) {
				for (uintptr_t i = 0; i < visibleItems.Length(); i++) {
					if (flags & ES_LIST_VIEW_HORIZONTAL) visibleItems[i].element->offsetX -= delta;
					else				     visibleItems[i].element->offsetY -= delta;
				}
			}

			Populate();
			Repaint(true);

			if (columnHeader) {
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

			for (uintptr_t i = 0; i < fixedItems.Length(); i++) {
				for (uintptr_t j = 0; j < fixedItems[i].otherColumns.Length(); j++) {
					EsHeapFree(fixedItems[i].otherColumns[j].string);
				}

				EsHeapFree(fixedItems[i].firstColumn.string);
				fixedItems[i].otherColumns.Free();
			}

			primaryCellStyle->CloseReference();
			secondaryCellStyle->CloseReference();
			selectedCellStyle->CloseReference();
			fixedItems.Free();
			visibleItems.Free();
			groups.Free();
		} else if (message->type == ES_MSG_KEY_UP) {
			if (message->keyboard.scancode == ES_SCANCODE_LEFT_CTRL || message->keyboard.scancode == ES_SCANCODE_RIGHT_CTRL) {
				SelectPreview();
			}
		} else if (message->type == ES_MSG_KEY_DOWN) {
			if (message->keyboard.scancode == ES_SCANCODE_LEFT_CTRL || message->keyboard.scancode == ES_SCANCODE_RIGHT_CTRL) {
				SelectPreview();
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

			EsCommandSetCallback(EsCommandByID(instance, ES_COMMAND_SELECT_ALL), nullptr);
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
			UIStyle *style = GetStyle(MakeStyleKey(ES_STYLE_TEXT_LABEL_SECONDARY, 0), true);
			EsTextPlanProperties properties = {};
			properties.flags = ES_TEXT_H_CENTER | ES_TEXT_V_CENTER | ES_TEXT_WRAP | ES_TEXT_PLAN_SINGLE_USE;
			EsTextRun textRun[2] = {};
			style->GetTextStyle(&textRun[0].style);
			textRun[1].offset = emptyMessageBytes;
			EsRectangle bounds = EsPainterBoundsInset(message->painter); 
			EsTextPlan *plan = EsTextPlanCreate(&properties, bounds, emptyMessage, textRun, 1);
			EsDrawText(message->painter, plan, bounds); 
		} else if (message->type == ES_MSG_ANIMATE) {
			if (scroll.dragScrolling && (flags & ES_LIST_VIEW_CHOICE_SELECT)) {
				DragSelect();
			}

			uint64_t currentTime = EsTimeStamp() / (api.systemConstants[ES_SYSTEM_CONSTANT_TIME_STAMP_UNITS_PER_MICROSECOND] * 1000);
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
				} else if (visibleItems[i].element->state & UI_STATE_HOVERED) {
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
		} else if (message->type == ES_MSG_LIST_VIEW_GET_CONTENT && (flags & ES_LIST_VIEW_FIXED_ITEMS)) {
			uintptr_t index = message->getContent.index;
			EsAssert(index < fixedItems.Length());
			ListViewFixedString emptyString = {};
			ListViewFixedItem *item = &fixedItems[index];
			ListViewFixedString *string = message->getContent.column == 0 ? &item->firstColumn 
				: message->getContent.column <= item->otherColumns.Length() ? &item->otherColumns[message->getContent.column - 1] : &emptyString;
			EsBufferFormat(message->getContent.buffer, "%s", string->bytes, string->string);
			if (message->getContent.column == 0) message->getContent.icon = item->iconID;
		} else if (message->type == ES_MSG_LIST_VIEW_IS_SELECTED && (flags & ES_LIST_VIEW_FIXED_ITEMS)) {
			message->selectItem.isSelected = message->selectItem.index == fixedItemSelection;
		} else {
			return 0;
		}

		return ES_HANDLED;
	}
};

int ListViewProcessMessage(EsElement *element, EsMessage *message) {
	return ((EsListView *) element)->ProcessMessage(message);
}

int ListViewProcessItemMessage(EsElement *_element, EsMessage *message) {
	ListViewItemElement *element = (ListViewItemElement *) _element;
	return ((EsListView *) element->parent)->ProcessItemMessage(element->index, message, element);
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

		view->columnHeader->messageUser = [] (EsElement *element, EsMessage *message) {
			EsListView *view = (EsListView *) element->userData.p;

			if (message->type == ES_MSG_LAYOUT) {
				int x = view->currentStyle->insets.l - view->scroll.position[0];

				for (uintptr_t i = 0; i < element->children.Length(); i += 2) {
					EsElement *item = element->children[i], *splitter = element->children[i + 1];
					EsListViewColumn *column = view->columns + item->userData.u;
					int splitterLeft = splitter->currentStyle->preferredWidth - view->secondaryCellStyle->gapMajor;
					item->InternalMove(column->width - splitterLeft, element->height, x, 0);
					splitter->InternalMove(splitter->currentStyle->preferredWidth, element->height, x + column->width - splitterLeft, 0);
					x += column->width + view->secondaryCellStyle->gapMajor;
				}
			}

			return 0;
		};

		view->scroll.fixedViewport[1] = view->columnHeader->currentStyle->preferredHeight;
	} else if ((~view->flags & ES_LIST_VIEW_COLUMNS) && view->columnHeader) {
		EsElementDestroy(view->columnHeader);
		view->columnHeader = nullptr;
		view->scroll.fixedViewport[1] = 0;
	}

	// It's safe to use SCROLL_MODE_AUTO even in tiled mode,
	// because decreasing the secondary axis can only increase the primary axis.

	uint8_t scrollXMode = 0, scrollYMode = 0;

	if (view->flags & ES_LIST_VIEW_COLUMNS) {
		scrollXMode = SCROLL_MODE_AUTO;
		scrollYMode = SCROLL_MODE_AUTO;
	} else if (view->flags & ES_LIST_VIEW_HORIZONTAL) {
		scrollXMode = SCROLL_MODE_AUTO;
	} else {
		scrollYMode = SCROLL_MODE_AUTO;
	}

	view->scroll.Setup(view, scrollXMode, scrollYMode, SCROLL_X_DRAG | SCROLL_Y_DRAG);

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
		spaceDelta += view->groups[i].totalSize;
	}

	view->InsertSpace(spaceDelta, 0);

	EsElementRelayout(view);
}

EsListView *EsListViewCreate(EsElement *parent, uint64_t flags, const EsStyle *style, 
		const EsStyle *itemStyle, const EsStyle *headerItemStyle, const EsStyle *footerItemStyle) {
	EsListView *view = (EsListView *) EsHeapAllocate(sizeof(EsListView), true);

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
	view->groups.Insert(empty, group);

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

	view->InsertSpace(view->groups.Length() > 1 ? view->currentStyle->gapMajor : 0, firstVisibleItemToMove);

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
	int64_t sizeToAdd = (count - (addedFirstItemInGroup ? 1 : 0)) * view->currentStyle->gapMinor + totalSizeOfItems;
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
		: (count * view->currentStyle->gapMinor + totalSizeOfItems);
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
	EsListViewColumn *column = view->columns + element->userData.u;

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
		m.columnMenu.index = element->userData.u;
		EsMessageSend(view, &m);
	} else {
		return 0;
	}

	return ES_HANDLED;
}

void EsListViewSetColumns(EsListView *view, EsListViewColumn *columns, size_t columnCount) {
	EsMessageMutexCheck();

	EsAssert(view->flags & ES_LIST_VIEW_COLUMNS); // List view does not have columns flag set.

	EsElementDestroyContents(view->columnHeader);

	view->columns = columns;
	view->columnCount = columnCount;

	view->totalColumnWidth = -view->secondaryCellStyle->gapMajor;

	for (uintptr_t i = 0; i < columnCount; i++) {
		EsElement *columnHeaderItem = EsCustomElementCreate(view->columnHeader, ES_CELL_FILL, 
				(columns[i].flags & ES_LIST_VIEW_COLUMN_HAS_MENU) ? ES_STYLE_LIST_COLUMN_HEADER_ITEM_HAS_MENU : ES_STYLE_LIST_COLUMN_HEADER_ITEM);

		columnHeaderItem->messageUser = ListViewColumnHeaderItemMessage;
		columnHeaderItem->cName = "column header item";
		columnHeaderItem->userData = i;

		if (!columns[i].width) {
			columns[i].width = (i ? view->secondaryCellStyle : view->primaryCellStyle)->preferredWidth;
		}

		EsElement *splitter = EsCustomElementCreate(view->columnHeader, ES_CELL_FILL, ES_STYLE_LIST_COLUMN_HEADER_SPLITTER);

		splitter->messageUser = [] (EsElement *element, EsMessage *message) {
			EsListViewColumn *column = (EsListViewColumn *) element->userData.p;
			EsListView *view = (EsListView *) element->parent->parent;

			if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
				view->columnResizingOriginalWidth = column->width;
			} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
				int width = message->mouseDragged.newPositionX - message->mouseDragged.originalPositionX + view->columnResizingOriginalWidth;
				int minimumWidth = element->currentStyle->metrics->minimumWidth;
				if (width < minimumWidth) width = minimumWidth;

				view->totalColumnWidth += width - column->width;
				column->width = width;
				EsElementRelayout(element->parent);
				EsElementRelayout(view);
			} else {
				return 0;
			}

			return ES_HANDLED;
		},

		splitter->cName = "column header splitter";
		splitter->userData = columns + i;

		view->totalColumnWidth += columns[i].width + view->secondaryCellStyle->gapMajor;
	}

	view->scroll.Refresh();
}

void EsListViewContentChanged(EsListView *view) {
	EsMessageMutexCheck();

	view->searchBufferLastKeyTime = 0;
	view->searchBufferBytes = 0;

	view->scroll.SetX(0);
	view->scroll.SetY(0);

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

	view->EnsureItemVisible(group, index, false);
}

bool EsListViewGetFocusedItem(EsListView *view, EsListViewIndex *group, EsListViewIndex *index) {
	if (view->hasFocusedItem) {
		if (group) *group = view->focusedItemGroup;
		if (index) *index = view->focusedItemIndex;
	}

	return view->hasFocusedItem;
}

void EsListViewSelect(EsListView *view, EsListViewIndex group, EsListViewIndex index) {
	EsMessageMutexCheck();

	view->Select(group, index, false, false, false);
}

void EsListViewSetEmptyMessage(EsListView *view, const char *message, ptrdiff_t messageBytes) {
	EsMessageMutexCheck();
	if (messageBytes == -1) messageBytes = EsCStringLength(message);
	HeapDuplicate((void **) &view->emptyMessage, message, messageBytes);
	view->emptyMessageBytes = messageBytes;

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

EsListViewIndex EsListViewFixedItemInsert(EsListView *view, const char *string, ptrdiff_t stringBytes, EsGeneric data, EsListViewIndex index, uint32_t iconID) {
	EsAssert(view->flags & ES_LIST_VIEW_FIXED_ITEMS);

	if (stringBytes == -1) {
		stringBytes = EsCStringLength(string);
	}

	if (!view->groups.Length()) {
		EsListViewInsertGroup(view, 0, ES_FLAGS_DEFAULT);
	}

	if (index == -1) {
		index = view->fixedItems.Length();
	}

	EsAssert(index >= 0 && index <= (intptr_t) view->fixedItems.Length());
	ListViewFixedItem item = {};
	item.data = data;
	item.iconID = iconID;
	HeapDuplicate((void **) &item.firstColumn.string, string, stringBytes);
	item.firstColumn.bytes = stringBytes;
	view->fixedItems.Insert(item, index);

	EsListViewInsert(view, 0, index, 1);
	return index;
}

void EsListViewFixedItemAddString(EsListView *view, EsListViewIndex index, const char *string, ptrdiff_t stringBytes) {
	EsAssert(view->flags & ES_LIST_VIEW_FIXED_ITEMS);
	EsMessageMutexCheck();
	EsAssert(index >= 0 && index < (intptr_t) view->fixedItems.Length());
	ListViewFixedString fixedString = {};
	fixedString.bytes = stringBytes == -1 ? EsCStringLength(string) : stringBytes;
	HeapDuplicate((void **) &fixedString, string, fixedString.bytes);
	view->fixedItems[index].otherColumns.Add(fixedString);
	EsListViewInvalidateContent(view, 0, index);
}

bool EsListViewFixedItemFindIndex(EsListView *view, EsGeneric data, EsListViewIndex *index) {
	EsAssert(view->flags & ES_LIST_VIEW_FIXED_ITEMS);
	EsMessageMutexCheck();

	for (uintptr_t i = 0; i < view->fixedItems.Length(); i++) {
		if (view->fixedItems[i].data == data) {
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
	if (found) EsListViewSelect(view, 0, index);
	return found;
}

bool EsListViewFixedItemGetSelected(EsListView *view, EsGeneric *data) {
	EsAssert(view->flags & ES_LIST_VIEW_FIXED_ITEMS);
	EsMessageMutexCheck();

	if (view->fixedItemSelection == -1 || view->fixedItemSelection >= (intptr_t) view->fixedItems.Length()) {
		return false;
	} else {
		*data = view->fixedItems[view->fixedItemSelection].data;
		return true;
	}
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
	view->EnsureItemVisible(group, index, true);

	uint64_t textboxFlags = ES_CELL_FILL | ES_TEXTBOX_EDIT_BASED | ES_TEXTBOX_ALLOW_TABS;
	
	if (flags & ES_LIST_VIEW_INLINE_TEXTBOX_REJECT_EDIT_IF_FOCUS_LOST) {
		textboxFlags |= ES_TEXTBOX_REJECT_EDIT_IF_LOST_FOCUS;
	}

	view->inlineTextbox = EsTextboxCreate(view, textboxFlags, ES_STYLE_TEXTBOX_INLINE);
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

void EsListViewEnumerateVisibleItems(EsListView *view, EsListViewEnumerateVisibleItemsCallbackFunction callback) {
	for (uintptr_t i = 0; i < view->visibleItems.Length(); i++) {
		callback(view, view->visibleItems[i].element, view->visibleItems[i].group, view->visibleItems[i].index);
	}
}

void EsListViewSetMaximumItemsPerBand(EsListView *view, int maximumItemsPerBand) {
	view->maximumItemsPerBand = maximumItemsPerBand;
}
