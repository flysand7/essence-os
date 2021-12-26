// TODO Caret blinking.
// TODO Wrapped lines.
// TODO Unicode grapheme/word boundaries.
// TODO Selecting lines with the margin.

#define GET_BUFFER(line) TextboxGetDocumentLineBuffer(textbox, line)

struct DocumentLine {
	int32_t lengthBytes,
		lengthWidth, 
		height,
		yPosition,
		offset;
};

struct TextboxVisibleLine {
	int32_t yPosition;
};

struct TextboxCaret {
	int32_t byte, // Relative to the start of the line.
		line;
};

struct EsTextbox : EsElement {
	ScrollPane scroll;

	char *data; // Call TextboxSetActiveLine(textbox, -1) to access this.
	uintptr_t dataAllocated;
	int32_t dataBytes;

	bool editing;
	char *editStartContent;
	int32_t editStartContentBytes;

	bool ensureCaretVisibleQueued;

	EsElementCallback overlayCallback;
	EsGeneric overlayData;

	char *activeLine;
	uintptr_t activeLineAllocated;
	int32_t activeLineIndex, activeLineStart, activeLineOldBytes, activeLineBytes;

	int32_t longestLine, longestLineWidth; // To set the horizontal scroll bar's size.

	TextboxCaret carets[2]; // carets[1] is the actual caret; carets[0] is the selection anchor.
	TextboxCaret wordSelectionAnchor, wordSelectionAnchor2;

	Array<DocumentLine> lines;
	Array<TextboxVisibleLine> visibleLines;
	int32_t firstVisibleLine;

	int verticalMotionHorizontalDepth;
	int oldHorizontalScroll;

	EsUndoManager *undo;
	EsUndoManager localUndo;

	EsElement *margin;
	
	EsRectangle borders, insets;
	EsTextStyle textStyle;
	EsFont overrideFont;
	uint16_t overrideTextSize;

	uint32_t syntaxHighlightingLanguage;
	uint32_t syntaxHighlightingColors[8];

	bool smartQuotes;

	bool inRightClickDrag;

	// For smart context menus:
	bool colorUppercase;
};

#define MOVE_CARET_SINGLE (2)
#define MOVE_CARET_WORD (3)
#define MOVE_CARET_LINE (4)
#define MOVE_CARET_VERTICAL (5)
#define MOVE_CARET_ALL (6)

#define MOVE_CARET_BACKWARDS (false)
#define MOVE_CARET_FORWARDS (true)

int TextGetLineHeight(EsElement *element, const EsTextStyle *textStyle) {
	EsAssert(element);
	EsMessageMutexCheck();
	Font font = FontGet(textStyle->font);
	FontSetSize(&font, textStyle->size * theming.scale);
	return (FontGetAscent(&font) - FontGetDescent(&font) + FREETYPE_UNIT_SCALE / 2) / FREETYPE_UNIT_SCALE;
}

void TextboxBufferResize(void **array, uintptr_t *allocated, uintptr_t needed, uintptr_t itemSize) {
	if (*allocated >= needed) {
		return;
	}

	uintptr_t oldAllocated = *allocated;
	void *oldArray = *array;

	uintptr_t newAllocated = oldAllocated * 2;
	if (newAllocated < needed) newAllocated = needed + 16;
	void *newArray = EsHeapAllocate(newAllocated * itemSize, false);

	EsMemoryCopy(newArray, oldArray, oldAllocated * itemSize);
	EsHeapFree(oldArray);

	*allocated = newAllocated;
	*array = newArray;
}

void KeyboardLayoutLoad() {
	if (api.keyboardLayoutIdentifier != api.global->keyboardLayout) {
		char buffer[64];
		api.keyboardLayoutIdentifier = api.global->keyboardLayout;
		api.keyboardLayout = (const uint16_t *) EsBundleFind(&bundleDesktop, buffer, EsStringFormat(buffer, sizeof(buffer), "Keyboard Layouts/%c%c.dat", 
					(uint8_t) api.keyboardLayoutIdentifier, (uint8_t) (api.keyboardLayoutIdentifier >> 8)));

		if (!api.keyboardLayout) {
			// Fallback to the US layout if the specifier layout was not found.
			api.keyboardLayout = (const uint16_t *) EsBundleFind(&bundleDesktop, buffer, EsStringFormat(buffer, sizeof(buffer), "Keyboard Layouts/us.dat"));
		}
	}
}

const char *KeyboardLayoutLookup(uint32_t scancode, bool isShiftHeld, bool isAltGrHeld, bool enableTabs, bool enableNewline) {
	KeyboardLayoutLoad();
	if (scancode >= 0x200) return nullptr;
	if (scancode == ES_SCANCODE_ENTER || scancode == ES_SCANCODE_NUM_ENTER) return enableNewline ? "\n" : nullptr;
	if (scancode == ES_SCANCODE_TAB) return enableTabs ? "\t" : nullptr;
	if (scancode == ES_SCANCODE_BACKSPACE || scancode == ES_SCANCODE_DELETE) return nullptr;
	uint16_t offset = api.keyboardLayout[scancode + (isShiftHeld ? 0x200 : 0) + (isAltGrHeld ? 0x400 : 0)];
	return offset ? ((char *) api.keyboardLayout + 0x1000 + offset) : nullptr;
}

uint32_t ScancodeMapToLabel(uint32_t scancode) {
	KeyboardLayoutLoad();
	const char *string = KeyboardLayoutLookup(scancode, false, false, false, false);

	if (string && string[0] && !string[1]) {
		char c = string[0];
		if (c >= 'a' && c <= 'z') return ES_SCANCODE_A + c - 'a';
		if (c >= 'A' && c <= 'Z') return ES_SCANCODE_A + c - 'A';
		if (c >= '0' && c <= '9') return ES_SCANCODE_0 + c - '0';
		if (c == '/')  return ES_SCANCODE_SLASH;
		if (c == '[')  return ES_SCANCODE_LEFT_BRACE;
		if (c == ']')  return ES_SCANCODE_RIGHT_BRACE;
		if (c == '=')  return ES_SCANCODE_EQUALS;
		if (c == '-')  return ES_SCANCODE_HYPHEN;
		if (c == ',')  return ES_SCANCODE_COMMA;
		if (c == '.')  return ES_SCANCODE_PERIOD;
		if (c == '\\') return ES_SCANCODE_PUNCTUATION_1;
		if (c == ';')  return ES_SCANCODE_PUNCTUATION_3;
		if (c == '\'') return ES_SCANCODE_PUNCTUATION_4;
		if (c == '`')  return ES_SCANCODE_PUNCTUATION_5;
	}

	return scancode;
}

bool ScancodeIsNonTypeable(uint32_t scancode) {
	switch (scancode) {
		case ES_SCANCODE_CAPS_LOCK:
		case ES_SCANCODE_SCROLL_LOCK:
		case ES_SCANCODE_NUM_LOCK:
		case ES_SCANCODE_LEFT_SHIFT:
		case ES_SCANCODE_LEFT_CTRL:
		case ES_SCANCODE_LEFT_ALT:
		case ES_SCANCODE_LEFT_FLAG:
		case ES_SCANCODE_RIGHT_SHIFT:
		case ES_SCANCODE_RIGHT_CTRL:
		case ES_SCANCODE_RIGHT_ALT:
		case ES_SCANCODE_PAUSE:
		case ES_SCANCODE_CONTEXT_MENU:
		case ES_SCANCODE_PRINT_SCREEN:
		case ES_SCANCODE_F1:
		case ES_SCANCODE_F2:
		case ES_SCANCODE_F3:
		case ES_SCANCODE_F4:
		case ES_SCANCODE_F5:
		case ES_SCANCODE_F6:
		case ES_SCANCODE_F7:
		case ES_SCANCODE_F8:
		case ES_SCANCODE_F9:
		case ES_SCANCODE_F10:
		case ES_SCANCODE_F11:
		case ES_SCANCODE_F12:
		case ES_SCANCODE_ACPI_POWER:
		case ES_SCANCODE_ACPI_SLEEP:
		case ES_SCANCODE_ACPI_WAKE:
		case ES_SCANCODE_MM_NEXT:
		case ES_SCANCODE_MM_PREVIOUS:
		case ES_SCANCODE_MM_STOP:
		case ES_SCANCODE_MM_PAUSE:
		case ES_SCANCODE_MM_MUTE:
		case ES_SCANCODE_MM_QUIETER:
		case ES_SCANCODE_MM_LOUDER:
		case ES_SCANCODE_MM_SELECT:
		case ES_SCANCODE_MM_EMAIL:
		case ES_SCANCODE_MM_CALC:
		case ES_SCANCODE_MM_FILES:
		case ES_SCANCODE_WWW_SEARCH:
		case ES_SCANCODE_WWW_HOME:
		case ES_SCANCODE_WWW_BACK:
		case ES_SCANCODE_WWW_FORWARD:
		case ES_SCANCODE_WWW_STOP:
		case ES_SCANCODE_WWW_REFRESH:
		case ES_SCANCODE_WWW_STARRED:
			return true;

		default:
			return false;
	}
}

size_t EsMessageGetInputText(EsMessage *message, char *buffer) {
	const char *string = KeyboardLayoutLookup(message->keyboard.scancode, 
			message->keyboard.modifiers & ES_MODIFIER_SHIFT, message->keyboard.modifiers & ES_MODIFIER_ALT_GR, 
			true, true);
	size_t bytes = string ? EsCStringLength(string) : 0;
	EsAssert(bytes < 64);
	EsMemoryCopy(buffer, string, bytes);
	return bytes;
}

enum CharacterType {
	CHARACTER_INVALID,
	CHARACTER_IDENTIFIER, // A-Z, a-z, 0-9, _, >= 0x7F
	CHARACTER_WHITESPACE, // space, tab, newline
	CHARACTER_OTHER,
};

static CharacterType GetCharacterType(int character) {
	if ((character >= '0' && character <= '9') 
			|| (character >= 'a' && character <= 'z')
			|| (character >= 'A' && character <= 'Z')
			|| (character == '_')
			|| (character >= 0x80)) {
		return CHARACTER_IDENTIFIER;
	}

	if (character == '\n' || character == '\t' || character == ' ') {
		return CHARACTER_WHITESPACE;
	}

	return CHARACTER_OTHER;
}

int TextboxCompareCarets(const TextboxCaret *left, const TextboxCaret *right) {
	if (left->line < right->line) return -1;
	if (left->line > right->line) return  1;
	if (left->byte < right->byte) return -1;
	if (left->byte > right->byte) return  1;
	return 0;
}

void TextboxSetActiveLine(EsTextbox *textbox, int lineIndex) {
	if (textbox->activeLineIndex == lineIndex) {
		return;
	}

	if (lineIndex == -1) {
		int32_t lineBytesDelta = textbox->activeLineBytes - textbox->activeLineOldBytes;

		// Step 1: Resize the data buffer to fit the new contents of the line.

		TextboxBufferResize((void **) &textbox->data, &textbox->dataAllocated, textbox->dataBytes + lineBytesDelta, 1);

		// Step 2: Move everything after the old end of the active line to its new position.

		EsMemoryMove(textbox->data + textbox->activeLineStart + textbox->activeLineOldBytes,
				textbox->data + textbox->dataBytes,
				lineBytesDelta,
				false);
		textbox->dataBytes += lineBytesDelta;

		// Step 3: Copy the active line back into the data buffer.

		EsMemoryCopy(textbox->data + textbox->activeLineStart,
				textbox->activeLine,
				textbox->activeLineBytes);

		// Step 4: Update the line byte offsets.

		for (uintptr_t i = textbox->activeLineIndex + 1; i < textbox->lines.Length(); i++) {
			textbox->lines[i].offset += lineBytesDelta;
		}
	} else {
		TextboxSetActiveLine(textbox, -1);

		DocumentLine *line = &textbox->lines[lineIndex];

		TextboxBufferResize((void **) &textbox->activeLine, &textbox->activeLineAllocated, (textbox->activeLineBytes = line->lengthBytes), 1);
		EsMemoryCopy(textbox->activeLine, textbox->data + line->offset, textbox->activeLineBytes);

		textbox->activeLineStart = line->offset;
		textbox->activeLineOldBytes = textbox->activeLineBytes;
	}

	textbox->activeLineIndex = lineIndex;
}

void EsTextboxStartEdit(EsTextbox *textbox) {
	textbox->state &= ~UI_STATE_LOST_STRONG_FOCUS;

	if ((textbox->flags & ES_TEXTBOX_EDIT_BASED) && !textbox->editing) {
		EsMessage m = { ES_MSG_TEXTBOX_EDIT_START };

		if (0 == EsMessageSend(textbox, &m)) {
			EsTextboxSelectAll(textbox);
		}

		if (textbox->state & UI_STATE_DESTROYING) {
			return;
		}

		textbox->editing = true; // Update this after sending the message so overlays can receive it.
		TextboxSetActiveLine(textbox, -1);
		textbox->editStartContent = (char *) EsHeapAllocate(textbox->dataBytes, false);
		textbox->editStartContentBytes = textbox->dataBytes;
		EsMemoryCopy(textbox->editStartContent, textbox->data, textbox->editStartContentBytes);
		textbox->Repaint(true);
	}
}

void TextboxEndEdit(EsTextbox *textbox, bool reject) {
	if ((textbox->flags & ES_TEXTBOX_EDIT_BASED) && textbox->editing) {
		TextboxSetActiveLine(textbox, -1);
		textbox->editing = false;
		EsMessage m = { ES_MSG_TEXTBOX_EDIT_END };
		m.endEdit.rejected = reject;
		m.endEdit.unchanged = textbox->dataBytes == textbox->editStartContentBytes 
			&& 0 == EsMemoryCompare(textbox->data, textbox->editStartContent, textbox->dataBytes);

		if (reject || ES_REJECTED == EsMessageSend(textbox, &m)) {
			EsTextboxSelectAll(textbox);
			EsTextboxInsert(textbox, textbox->editStartContent, textbox->editStartContentBytes);
			TextboxSetActiveLine(textbox, -1);
			if (reject) EsMessageSend(textbox, &m);
		}

		if (textbox->state & UI_STATE_DESTROYING) {
			return;
		}

		EsTextboxSetSelection(textbox, 0, 0, 0, 0);
		EsHeapFree(textbox->editStartContent);
		textbox->editStartContent = nullptr;
		textbox->scroll.SetX(0);
		textbox->Repaint(true);
	}
}

void TextboxUpdateCommands(EsTextbox *textbox, bool noClipboard) {
	if (~textbox->state & UI_STATE_FOCUSED) {
		return;
	}

	EsCommand *command;

	bool selectionEmpty = !TextboxCompareCarets(textbox->carets + 0, textbox->carets + 1) && textbox->editing;

	command = EsCommandByID(textbox->instance, ES_COMMAND_DELETE);
	command->data = textbox;
	EsCommandSetDisabled(command, selectionEmpty);

	EsCommandSetCallback(command, [] (EsInstance *, EsElement *, EsCommand *command) {
		EsTextbox *textbox = (EsTextbox *) command->data.p;
		EsTextboxInsert(textbox, "", 0, true);
	});

	command = EsCommandByID(textbox->instance, ES_COMMAND_COPY);
	command->data = textbox;
	EsCommandSetDisabled(command, selectionEmpty);

	EsCommandSetCallback(command, [] (EsInstance *, EsElement *, EsCommand *command) {
		EsTextbox *textbox = (EsTextbox *) command->data.p;
		size_t textBytes;
		char *text = EsTextboxGetContents(textbox, &textBytes, textbox->editing ? ES_TEXTBOX_GET_CONTENTS_SELECTED_ONLY : ES_FLAGS_DEFAULT);
		EsError error = EsClipboardAddText(ES_CLIPBOARD_PRIMARY, text, textBytes);
		EsHeapFree(text);

		EsRectangle bounds = EsElementGetWindowBounds(textbox);
		int32_t x = (bounds.l + bounds.r) / 2;
		int32_t y = (bounds.t + bounds.b) / 2; // TODO Position this in the middle of the selection.

		if (error == ES_SUCCESS) {
			EsAnnouncementShow(textbox->window, ES_FLAGS_DEFAULT, x, y, INTERFACE_STRING(CommonAnnouncementTextCopied));
		} else if (error == ES_ERROR_INSUFFICIENT_RESOURCES || error == ES_ERROR_DRIVE_FULL) {
			EsAnnouncementShow(textbox->window, ES_FLAGS_DEFAULT, x, y, INTERFACE_STRING(CommonAnnouncementCopyErrorResources));
		} else {
			EsAnnouncementShow(textbox->window, ES_FLAGS_DEFAULT, x, y, INTERFACE_STRING(CommonAnnouncementCopyErrorOther));
		}
	});

	command = EsCommandByID(textbox->instance, ES_COMMAND_CUT);
	command->data = textbox;
	EsCommandSetDisabled(command, selectionEmpty);

	EsCommandSetCallback(command, [] (EsInstance *, EsElement *, EsCommand *command) {
		EsTextbox *textbox = (EsTextbox *) command->data.p;
		size_t textBytes;
		char *text = EsTextboxGetContents(textbox, &textBytes, textbox->editing ? ES_TEXTBOX_GET_CONTENTS_SELECTED_ONLY : ES_FLAGS_DEFAULT);
		EsClipboardAddText(ES_CLIPBOARD_PRIMARY, text, textBytes);
		EsHeapFree(text);
		EsTextboxStartEdit(textbox);
		EsTextboxInsert(textbox, "", 0, true);
	});

	EsInstanceSetActiveUndoManager(textbox->instance, textbox->undo);

	command = EsCommandByID(textbox->instance, ES_COMMAND_SELECT_ALL);
	command->data = textbox;
	EsCommandSetDisabled(command, !(textbox->lines.Length() > 1 || textbox->lines[0].lengthBytes));

	EsCommandSetCallback(command, [] (EsInstance *, EsElement *, EsCommand *command) {
		EsTextboxSelectAll((EsTextbox *) command->data.p);
	});

	if (!noClipboard) {
		command = EsCommandByID(textbox->instance, ES_COMMAND_PASTE);
		command->data = textbox;
		EsCommandSetDisabled(command, !EsClipboardHasText(ES_CLIPBOARD_PRIMARY));

		EsCommandSetCallback(command, [] (EsInstance *, EsElement *, EsCommand *command) {
			EsTextbox *textbox = (EsTextbox *) command->data.p;

			size_t textBytes = 0;
			char *text = EsClipboardReadText(ES_CLIPBOARD_PRIMARY, &textBytes);
			EsTextboxInsert(textbox, text, textBytes, true);
			EsTextboxEnsureCaretVisible(textbox);
			EsHeapFree(text);
		});
	}
}

char *TextboxGetDocumentLineBuffer(EsTextbox *textbox, DocumentLine *line) {
	if (textbox->activeLineIndex == line - textbox->lines.array) {
		return textbox->activeLine;
	} else {
		return textbox->data + line->offset;
	}
}

void TextboxFindLongestLine(EsTextbox *textbox) {
	if (textbox->longestLine == -1) {
		textbox->longestLine = 0;
		textbox->longestLineWidth = textbox->lines[0].lengthWidth;

		for (uintptr_t i = 1; i < textbox->lines.Length(); i++) {
			int32_t width = textbox->lines[i].lengthWidth;

			if (width > textbox->longestLineWidth) {
				textbox->longestLine = i, textbox->longestLineWidth = width;
			}
		}
	}
}

TextboxVisibleLine *TextboxGetVisibleLine(EsTextbox *textbox, int32_t documentLineIndex) {
	return textbox->firstVisibleLine > documentLineIndex 
		|| textbox->firstVisibleLine + (int32_t) textbox->visibleLines.Length() <= documentLineIndex 
		? nullptr : &textbox->visibleLines[documentLineIndex - textbox->firstVisibleLine];
}

void TextboxEnsureCaretVisibleActionCallback(EsElement *element, EsGeneric context) {
	EsTextbox *textbox = (EsTextbox *) element;
	bool verticallyCenter = context.u;
	TextboxCaret caret = textbox->carets[1];

	for (uintptr_t i = 0; i < 3; i++) {
		// ScrollPane::SetY causes ES_MSG_SCROLL_Y to get sent to the textbox.
		// This causes a TextboxRefreshVisibleLines, which may cause new lines to added.
		// If these lines had not been previously horizontally measured, this will then occur.
		// This then causes a ScrollPane::Refresh for the new horizontal width.
		// If this causes the horizontal scroll bar to appear, then the caret may no longer be fully visible.
		// Therefore, we repeat up to 3 times to ensure that the caret is definitely fully visible.
		
		EsRectangle bounds = textbox->GetBounds();
		DocumentLine *line = &textbox->lines[caret.line];
		int caretY = line->yPosition + textbox->insets.t;

		int scrollY = textbox->scroll.position[1];
		int viewportHeight = bounds.b;
		caretY -= scrollY;

		if (viewportHeight > 0) {
			if (verticallyCenter) {
				scrollY += caretY - viewportHeight / 2;
			} else {
				if (caretY < textbox->insets.t) {
					scrollY += caretY - textbox->insets.t;
				} else if (caretY + line->height > viewportHeight - textbox->insets.b) {
					scrollY += caretY + line->height - viewportHeight + textbox->insets.b;
				}
			}

			if (textbox->scroll.position[1] != scrollY) {
				textbox->scroll.SetY(scrollY);
			} else {
				break;
			}
		} else {
			break;
		}
	}

	TextboxVisibleLine *visibleLine = TextboxGetVisibleLine(textbox, caret.line);

	if (visibleLine) {
		EsRectangle bounds = textbox->GetBounds();
		DocumentLine *line = &textbox->lines[caret.line];
		int scrollX = textbox->scroll.position[0];
		int viewportWidth = bounds.r;
		int caretX = TextGetPartialStringWidth(textbox, &textbox->textStyle,
				GET_BUFFER(line), line->lengthBytes, caret.byte) - scrollX + textbox->insets.l;

		if (caretX < textbox->insets.l) {
			scrollX += caretX - textbox->insets.l;
		} else if (caretX + 1 > viewportWidth - textbox->insets.r) {
			scrollX += caretX + 1 - viewportWidth + textbox->insets.r;
		}

		textbox->scroll.SetX(scrollX);
	}

	UIQueueEnsureVisibleMessage(textbox, false);
	textbox->ensureCaretVisibleQueued = false;
}

void EsTextboxEnsureCaretVisible(EsTextbox *textbox, bool verticallyCenter) {
	if (!textbox->ensureCaretVisibleQueued) {
		UpdateAction action = {};
		action.element = textbox;
		action.callback = TextboxEnsureCaretVisibleActionCallback;
		action.context.u = verticallyCenter;
		textbox->window->updateActions.Add(action);
		textbox->ensureCaretVisibleQueued = true;
	}
}

bool TextboxMoveCaret(EsTextbox *textbox, TextboxCaret *caret, bool right, int moveType, bool strongWhitespace = false) {
	TextboxCaret old = *caret;
	EsDefer(TextboxUpdateCommands(textbox, true));

	if (moveType == MOVE_CARET_LINE) {
		caret->byte = right ? textbox->lines[caret->line].lengthBytes : 0;
	} else if (moveType == MOVE_CARET_ALL) {
		caret->line = right ? textbox->lines.Length() - 1 : 0;
		caret->byte = right ? textbox->lines[caret->line].lengthBytes : 0;
	} else if (moveType == MOVE_CARET_VERTICAL) {
		if ((right && caret->line + 1 == (int32_t) textbox->lines.Length()) || (!right && !caret->line)) {
			return false;
		}

		if (textbox->verticalMotionHorizontalDepth == -1) {
			textbox->verticalMotionHorizontalDepth = TextGetPartialStringWidth(textbox, &textbox->textStyle,
					GET_BUFFER(&textbox->lines[caret->line]), textbox->lines[caret->line].lengthBytes, caret->byte);
		}

		if (right) caret->line++; else caret->line--;
		caret->byte = 0;

		DocumentLine *line = &textbox->lines[caret->line];
		int pointX = textbox->verticalMotionHorizontalDepth ? textbox->verticalMotionHorizontalDepth - 1 : 0;
		ptrdiff_t result = TextGetCharacterAtPoint(textbox, &textbox->textStyle,
				GET_BUFFER(line), line->lengthBytes, &pointX, ES_TEXT_GET_CHARACTER_AT_POINT_MIDDLE);
		caret->byte = result == -1 ? line->lengthBytes : result;
	} else {
		CharacterType type = CHARACTER_INVALID;
		char *currentLineBuffer = GET_BUFFER(&textbox->lines[caret->line]);
		if (moveType == MOVE_CARET_WORD && right) goto checkCharacterType;

		while (true) {
			if (!right) {
				if (caret->byte || caret->line) {
					if (caret->byte) {
						caret->byte = utf8_retreat(currentLineBuffer + caret->byte) - currentLineBuffer;
					} else {
						caret->byte = textbox->lines[--caret->line].lengthBytes;
						currentLineBuffer = GET_BUFFER(&textbox->lines[caret->line]);
					}
				} else {
					break; // We cannot move any further left.
				}
			} else {
				if (caret->line < (int32_t) textbox->lines.Length() - 1 || caret->byte < textbox->lines[caret->line].lengthBytes) {
					if (caret->byte < textbox->lines[caret->line].lengthBytes) {
						caret->byte = utf8_advance(currentLineBuffer + caret->byte) - currentLineBuffer;
					} else {
						caret->line++;
						caret->byte = 0;
						currentLineBuffer = GET_BUFFER(&textbox->lines[caret->line]);
					}
				} else {
					break; // We cannot move any further right.
				}
			}

			if (moveType == MOVE_CARET_SINGLE) {
				break;
			}

			checkCharacterType:;

			int character;

			if (caret->byte == textbox->lines[caret->line].lengthBytes) {
				character = '\n';
			} else {
				character = utf8_value(currentLineBuffer + caret->byte);
			}

			CharacterType newType = GetCharacterType(character);

			if (type == CHARACTER_INVALID) {
				if (newType != CHARACTER_WHITESPACE || strongWhitespace) {
					type = newType;
				}
			} else {
				if (newType != type) {
					if (!right) {
						// We've gone too far.
						TextboxMoveCaret(textbox, caret, true, MOVE_CARET_SINGLE);
					}

					break;
				}
			}
		}
	}

	return caret->line != old.line;
}

void EsTextboxMoveCaretRelative(EsTextbox *textbox, uint32_t flags) {
	if (~flags & ES_TEXTBOX_MOVE_CARET_SECOND_ONLY) {
		TextboxMoveCaret(textbox, &textbox->carets[0], ~flags & ES_TEXTBOX_MOVE_CARET_BACKWARDS, 
				flags & 0xFF, flags & ES_TEXTBOX_MOVE_CARET_STRONG_WHITESPACE);
	}

	if (~flags & ES_TEXTBOX_MOVE_CARET_FIRST_ONLY) {
		TextboxMoveCaret(textbox, &textbox->carets[1], ~flags & ES_TEXTBOX_MOVE_CARET_BACKWARDS, 
				flags & 0xFF, flags & ES_TEXTBOX_MOVE_CARET_STRONG_WHITESPACE);
	}
}

void TextboxRepaintLine(EsTextbox *textbox, int line) {
	if (line == -1 || (~textbox->flags & ES_TEXTBOX_MULTILINE)) {
		textbox->Repaint(true);
	} else {
		EsRectangle borders = textbox->borders;
		int topInset = textbox->insets.t;

		TextboxVisibleLine *visibleLine = TextboxGetVisibleLine(textbox, line);

		if (visibleLine) {
			EsRectangle bounds = textbox->GetBounds();
			EsRectangle lineBounds = ES_RECT_4(bounds.l + borders.l, bounds.r - borders.r, 
					visibleLine->yPosition + topInset - 1 - textbox->scroll.position[1], 
					visibleLine->yPosition + topInset + textbox->lines[line].height - textbox->scroll.position[1]);
			// EsPrint("textbox bounds %R; line bounds %R\n", bounds);
			textbox->Repaint(false, lineBounds);
		}
	}
}

void TextboxSetHorizontalScroll(EsTextbox *textbox, int scroll) {
	textbox->Repaint(true);
	textbox->oldHorizontalScroll = scroll;
}

void TextboxRefreshVisibleLines(EsTextbox *textbox, bool repaint = true) {
	if (textbox->visibleLines.Length()) {
		textbox->visibleLines.SetLength(0);
	}

	int scrollX = textbox->scroll.position[0], scrollY = textbox->scroll.position[1];
	EsRectangle bounds = textbox->GetBounds();

	int32_t low = 0, high = textbox->lines.Length() - 1, target = scrollY - textbox->insets.t;

	while (low != high) {
		int32_t middle = (low + high) / 2;
		int32_t position = textbox->lines[middle].yPosition;

		if (position < target && low != middle) low = middle;
		else if (position > target && high != middle) high = middle;
		else break;
	}

	textbox->firstVisibleLine = (low + high) / 2;
	if (textbox->firstVisibleLine) textbox->firstVisibleLine--;

	for (int32_t i = textbox->firstVisibleLine; i < (int32_t) textbox->lines.Length(); i++) {
		TextboxVisibleLine line = {};
		line.yPosition = textbox->lines[i].yPosition;
		textbox->visibleLines.Add(line);

		if (line.yPosition - scrollY > bounds.b) {
			break;
		}
	}

	bool refreshXLimit = false;

	for (uintptr_t i = 0; i < textbox->visibleLines.Length(); i++) {
		DocumentLine *line = &textbox->lines[textbox->firstVisibleLine + i];

		if (line->lengthWidth != -1) {
			continue;
		}

		line->lengthWidth = TextGetStringWidth(textbox, &textbox->textStyle,
				GET_BUFFER(line), line->lengthBytes);

		if (textbox->longestLine != -1 && line->lengthWidth > textbox->longestLineWidth) {
			textbox->longestLine = textbox->firstVisibleLine + i;
			textbox->longestLineWidth = line->lengthWidth;
			refreshXLimit = true;
		}
	}

	if (refreshXLimit) {
		textbox->scroll.Refresh();
	}

	textbox->scroll.SetX(scrollX);

	if (repaint) {
		textbox->Repaint(true);
	}
}

void TextboxLineCountChangeCleanup(EsTextbox *textbox, int32_t offsetDelta, int32_t startLine) {
	for (int32_t i = startLine; i < (int32_t) textbox->lines.Length(); i++) {
		DocumentLine *line = &textbox->lines[i], *previous = &textbox->lines[i - 1];
		line->yPosition = previous->yPosition + previous->height;
		line->offset += offsetDelta;
	}

	TextboxRefreshVisibleLines(textbox);
}

void EsTextboxMoveCaret(EsTextbox *textbox, int32_t line, int32_t byte) {
	EsMessageMutexCheck();

	textbox->carets[0].line = line;
	textbox->carets[0].byte = byte;
	textbox->carets[1].line = line;
	textbox->carets[1].byte = byte;
	textbox->Repaint(true);
	TextboxUpdateCommands(textbox, true);
}

void EsTextboxGetSelection(EsTextbox *textbox, int32_t *fromLine, int32_t *fromByte, int32_t *toLine, int32_t *toByte) {
	EsMessageMutexCheck();

	*fromLine = textbox->carets[0].line;
	*fromByte = textbox->carets[0].byte;
	*toLine   = textbox->carets[1].line;
	*toByte   = textbox->carets[1].byte;
}

void EsTextboxSetSelection(EsTextbox *textbox, int32_t fromLine, int32_t fromByte, int32_t toLine, int32_t toByte) {
	EsMessageMutexCheck();

	if (fromByte == -1) fromByte = textbox->lines[fromLine].lengthBytes;
	if (toByte == -1) toByte = textbox->lines[toLine].lengthBytes;
	if (fromByte < 0 || toByte < 0 || fromByte > textbox->lines[fromLine].lengthBytes || toByte > textbox->lines[toLine].lengthBytes) return;
	textbox->carets[0].line = fromLine;
	textbox->carets[0].byte = fromByte;
	textbox->carets[1].line = toLine;
	textbox->carets[1].byte = toByte;
	textbox->Repaint(true);
	TextboxUpdateCommands(textbox, true);
	EsTextboxEnsureCaretVisible(textbox);
}

void EsTextboxSelectAll(EsTextbox *textbox) {
	EsMessageMutexCheck();

	TextboxMoveCaret(textbox, &textbox->carets[0], false, MOVE_CARET_ALL);
	TextboxMoveCaret(textbox, &textbox->carets[1], true, MOVE_CARET_ALL);
	EsTextboxEnsureCaretVisible(textbox);
	textbox->Repaint(true);
}

void EsTextboxClear(EsTextbox *textbox, bool sendUpdatedMessage) {
	EsMessageMutexCheck();

	EsTextboxSelectAll(textbox);
	EsTextboxInsert(textbox, "", 0, sendUpdatedMessage);
}

size_t EsTextboxGetLineLength(EsTextbox *textbox, uintptr_t line) {
	EsMessageMutexCheck();

	return textbox->lines[line].lengthBytes;
}

struct TextboxUndoItemHeader {
	EsTextbox *textbox;
	TextboxCaret caretsBefore[2];
	size_t insertBytes;
	double timeStampMs;
	// Followed by insert string.
};

void TextboxUndoItemCallback(const void *item, EsUndoManager *manager, EsMessage *message) {
	if (message->type == ES_MSG_UNDO_INVOKE) {
		TextboxUndoItemHeader *header = (TextboxUndoItemHeader *) item;
		EsTextbox *textbox = header->textbox;
		EsAssert(textbox->undo == manager);
		TextboxRepaintLine(textbox, textbox->carets[0].line);
		TextboxRepaintLine(textbox, textbox->carets[0].line);
		textbox->carets[0] = header->caretsBefore[0];
		textbox->carets[1] = header->caretsBefore[1];
		EsTextboxInsert(textbox, (const char *) (header + 1), header->insertBytes, true);
		EsTextboxEnsureCaretVisible(textbox);
	} else if (message->type == ES_MSG_UNDO_CANCEL) {
		// Nothing to do.
	}
}

void EsTextboxInsert(EsTextbox *textbox, const char *string, ptrdiff_t stringBytes, bool sendUpdatedMessage) {
	EsMessageMutexCheck();

	// EsPerformanceTimerPush();
	// double measureLineTime = 0;

	if (stringBytes == -1) {
		stringBytes = EsCStringLength(string);
	}

	TextboxUndoItemHeader *undoItem = nullptr;
	size_t undoItemBytes = 0;

	textbox->wordSelectionAnchor  = textbox->carets[0];
	textbox->wordSelectionAnchor2 = textbox->carets[1];

	textbox->verticalMotionHorizontalDepth = -1;

	// ::: Delete the selected text.

	// Step 1: Get the range of text we're deleting.

	TextboxCaret deleteFrom, deleteTo;
	int comparison = TextboxCompareCarets(textbox->carets + 0, textbox->carets + 1);

	if      (comparison < 0) deleteFrom = textbox->carets[0], deleteTo = textbox->carets[1];
	else if (comparison > 0) deleteFrom = textbox->carets[1], deleteTo = textbox->carets[0];

	if (comparison) {
		textbox->carets[0] = textbox->carets[1] = deleteFrom;

		// Step 2: Calculate the number of bytes we are deleting.

		int32_t deltaBytes;

		if (deleteFrom.line == deleteTo.line) {
			deltaBytes = deleteFrom.byte - deleteTo.byte;
		} else {
			TextboxSetActiveLine(textbox, -1);

			deltaBytes = deleteFrom.byte - deleteTo.byte;

			for (int32_t i = deleteFrom.line; i < deleteTo.line; i++) {
				deltaBytes -= textbox->lines[i].lengthBytes;
			}
		}

		if (textbox->undo) {
			// Step 3: Allocate space for an undo item.

			undoItemBytes = sizeof(TextboxUndoItemHeader) - deltaBytes + deleteTo.line - deleteFrom.line;
			undoItem = (TextboxUndoItemHeader *) EsHeapAllocate(undoItemBytes, false);
			EsMemoryZero(undoItem, sizeof(TextboxUndoItemHeader));
			undoItem->insertBytes = undoItemBytes - sizeof(TextboxUndoItemHeader);
		}

		if (deleteFrom.line == deleteTo.line) {
			EsAssert(deltaBytes < 0); // Expected deleteTo > deleteFrom.
			DocumentLine *line = &textbox->lines[deleteFrom.line];
			TextboxSetActiveLine(textbox, deleteFrom.line);

			// Step 4: Update the width of the line and repaint it.

			line->lengthWidth = TextGetStringWidth(textbox, &textbox->textStyle, textbox->activeLine, textbox->activeLineBytes);
			TextboxRepaintLine(textbox, deleteFrom.line);

			// Step 5: Update the active line buffer.

			if (undoItem) EsMemoryCopy(undoItem + 1, textbox->activeLine + deleteFrom.byte, -deltaBytes);
			EsMemoryMove(textbox->activeLine + deleteTo.byte, textbox->activeLine + line->lengthBytes, deltaBytes, false);
			textbox->activeLineBytes += deltaBytes;
			line->lengthBytes += deltaBytes;

			// Step 6: Update the longest line.

			if (textbox->longestLine == deleteFrom.line && line->lengthWidth < textbox->longestLineWidth) {
				textbox->longestLine = -1;
			}
		} else {
			if (undoItem) {
				// Step 4: Copy into the undo item.

				char *position = (char *) (undoItem + 1);
				
				for (int32_t i = deleteFrom.line; i <= deleteTo.line; i++) {
					char *from = textbox->data + textbox->lines[i].offset;
					char *to = textbox->data + textbox->lines[i].offset + textbox->lines[i].lengthBytes;	
					if (i == deleteFrom.line) from += deleteFrom.byte;
					if (i == deleteTo.line) to += deleteTo.byte - textbox->lines[i].lengthBytes;
					EsMemoryCopy(position, from, to - from);
					position += to - from;
					if (i != deleteTo.line) *position++ = '\n';
				}
			}

			// Step 5: Remove the text from the buffer.

			EsMemoryMove(textbox->data + deleteTo.byte + textbox->lines[deleteTo.line].offset, textbox->data + textbox->dataBytes, deltaBytes, false);
			textbox->dataBytes += deltaBytes;

			// Step 6: Merged the joined lines.

			DocumentLine *firstLine = &textbox->lines[deleteFrom.line];
			firstLine->lengthBytes = textbox->lines[deleteTo.line].lengthBytes - deleteTo.byte + deleteFrom.byte;
			firstLine->lengthWidth = TextGetStringWidth(textbox, &textbox->textStyle, textbox->data + firstLine->offset, firstLine->lengthBytes);

			// Step 7: Remove the deleted lines and update the textbox.

			textbox->lines.DeleteMany(deleteFrom.line + 1, deleteTo.line - deleteFrom.line);
			textbox->longestLine = -1;
			TextboxLineCountChangeCleanup(textbox, deltaBytes, deleteFrom.line + 1);
		}
	} else {
		if (textbox->undo) {
			undoItemBytes = sizeof(TextboxUndoItemHeader);
			undoItem = (TextboxUndoItemHeader *) EsHeapAllocate(undoItemBytes, false);
			EsMemoryZero(undoItem, sizeof(TextboxUndoItemHeader));
		}
	}

	if (undoItem) {
		undoItem->caretsBefore[0] = undoItem->caretsBefore[1] = textbox->carets[0];
	}

	// ::: Insert the new text.

	if (!stringBytes) goto done;

	{
		TextboxCaret insertionPoint = textbox->carets[0];

		DocumentLine *line = &textbox->lines[insertionPoint.line];
		int32_t lineByteOffset = line->offset,
			offsetIntoLine = insertionPoint.byte,
			byteOffset = offsetIntoLine + lineByteOffset;

		// Step 1: Count the number of newlines in the input string.

		uintptr_t position = 0,
			  newlines = 0,
			  carriageReturns = 0;

		while (position < (size_t) stringBytes) {
			int length;
			UTF8_LENGTH_CHAR(string + position, length);
			if (length == 0) length = 1;

			if (position + length > (size_t) stringBytes) {
				break;
			} else if (string[position] == '\n') {
				newlines++;
			} else if (string[position] == '\r' && position != (size_t) stringBytes - 1 && string[position + 1] == '\n') {
				carriageReturns++;
			}

			position += length;
		}

		size_t bytesToInsert = stringBytes - newlines - carriageReturns;

		if (!newlines || (~textbox->flags & ES_TEXTBOX_MULTILINE)) {
			// Step 2: Update the active line buffer.

			TextboxSetActiveLine(textbox, insertionPoint.line);
			TextboxBufferResize((void **) &textbox->activeLine, &textbox->activeLineAllocated, (textbox->activeLineBytes += bytesToInsert), 1);
			EsMemoryMove(textbox->activeLine + offsetIntoLine, textbox->activeLine + line->lengthBytes, bytesToInsert, false);

			const char *dataToInsert = string;
			size_t added = 0;

			for (uintptr_t i = 0; i < newlines + 1; i++) {
				const char *end = (const char *) EsCRTmemchr(dataToInsert, '\n', stringBytes - (dataToInsert - string)) ?: string + stringBytes;
				bool carriageReturn = end != string && end[-1] == '\r';
				if (carriageReturn) end--;
				EsMemoryCopy(textbox->activeLine + offsetIntoLine + added, dataToInsert, end - dataToInsert);
				added += end - dataToInsert;
				dataToInsert = end + (carriageReturn ? 2 : 1);
			}

			EsAssert(added == bytesToInsert); // Added incorrect number of bytes in EsTextboxInsert.

			line->lengthBytes += bytesToInsert;

			// Step 3: Update the carets, line width, and repaint it.

			textbox->carets[0].byte += bytesToInsert;
			textbox->carets[1].byte += bytesToInsert;
			line->lengthWidth = TextGetStringWidth(textbox, &textbox->textStyle, textbox->activeLine, line->lengthBytes);
			TextboxRepaintLine(textbox, insertionPoint.line);

			// Step 4: Update the longest line.

			if (textbox->longestLine != -1 && line->lengthWidth > textbox->longestLineWidth) {
				textbox->longestLine = insertionPoint.line;
				textbox->longestLineWidth = line->lengthWidth;
			}
		} else {
			// Step 2: Make room in the buffer for the contents of the string.

			TextboxSetActiveLine(textbox, -1);
			TextboxBufferResize((void **) &textbox->data, &textbox->dataAllocated, textbox->dataBytes + bytesToInsert, 1);
			EsMemoryMove(textbox->data + byteOffset, textbox->data + textbox->dataBytes, bytesToInsert, false);
			textbox->dataBytes += bytesToInsert;

			// Step 3: Truncate the insertion line.

			int32_t truncation = line->lengthBytes - insertionPoint.byte;
			line->lengthBytes = insertionPoint.byte;

			// Step 4: Add the new lines.

			textbox->lines.InsertMany(insertionPoint.line + 1, newlines);
			const char *dataToInsert = string;
			uintptr_t insertedBytes = 0;

			for (uintptr_t i = 0; i < newlines + 1; i++) {
				DocumentLine *line = &textbox->lines[insertionPoint.line + i], *previous = line - 1;

				// Step 4a: Initialise the line.

				if (i) {
					EsMemoryZero(line, sizeof(*line));
					line->height = TextGetLineHeight(textbox, &textbox->textStyle);
					line->yPosition = previous->yPosition + previous->height;
					line->offset = lineByteOffset + insertedBytes;
				}

				// Step 4b: Copy the string data into the line.

				const char *end = (const char *) EsCRTmemchr(dataToInsert, '\n', stringBytes - (dataToInsert - string)) ?: string + stringBytes;
				bool carriageReturn = end != string && end[-1] == '\r';
				if (carriageReturn) end--;
				EsMemoryCopy(textbox->data + line->offset + line->lengthBytes, dataToInsert, end - dataToInsert);
				line->lengthBytes += end - dataToInsert;
				insertedBytes += line->lengthBytes;
				dataToInsert = end + (carriageReturn ? 2 : 1);

				if (i == newlines) {
					line->lengthBytes += truncation;
				}

				// Step 4c: Update the line's width.

				// EsPerformanceTimerPush(); 
#if 0
				line->lengthWidth = EsTextGetPartialStringWidth(&textbox->textStyle, textbox->data + line->offset, line->lengthBytes, 0, line->lengthBytes);
#else
				line->lengthWidth = -1;
#endif
				// double time = EsPerformanceTimerPop();
				// measureLineTime += time;
				// EsPrint("Measured the length of line %d in %Fms.\n", insertionPoint.line + i, time * 1000);
			}

			// Step 5: Update the carets.

			textbox->carets[0].line = insertionPoint.line + newlines;
			textbox->carets[1].line = insertionPoint.line + newlines;
			textbox->carets[0].byte = textbox->lines[insertionPoint.line + newlines].lengthBytes - truncation;
			textbox->carets[1].byte = textbox->lines[insertionPoint.line + newlines].lengthBytes - truncation;

			// Step 6: Update the textbox.

			textbox->longestLine = -1;
			TextboxLineCountChangeCleanup(textbox, bytesToInsert, insertionPoint.line + 1 + newlines);
		}

		if (undoItem) undoItem->caretsBefore[1] = textbox->carets[0];
	}

	done:;

	if (sendUpdatedMessage) {
		EsMessage m = { ES_MSG_TEXTBOX_UPDATED };
		EsMessageSend(textbox, &m);
	} else if (textbox->overlayCallback) {
		EsMessage m = { ES_MSG_TEXTBOX_UPDATED };
		textbox->overlayCallback(textbox, &m);
	}

	if (textbox->state & UI_STATE_DESTROYING) {
		return;
	}

	TextboxFindLongestLine(textbox);
	InspectorNotifyElementContentChanged(textbox);

	if (undoItem && (undoItem->insertBytes || TextboxCompareCarets(undoItem->caretsBefore + 0, undoItem->caretsBefore + 1))) {
		undoItem->timeStampMs = EsTimeStampMs();

		EsUndoCallback previousCallback;
		const void *previousItem;

		if (!EsUndoInUndo(textbox->undo) 
				&& EsUndoPeek(textbox->undo, &previousCallback, &previousItem) 
				&& previousCallback == TextboxUndoItemCallback) {
			TextboxUndoItemHeader *header = (TextboxUndoItemHeader *) previousItem;

#define TEXTBOX_UNDO_TIMEOUT (500) // TODO Make this configurable.
			if (undoItem->timeStampMs - header->timeStampMs < TEXTBOX_UNDO_TIMEOUT) {
				if (!undoItem->insertBytes && !header->insertBytes 
						&& undoItem->caretsBefore[0].line == header->caretsBefore[1].line
						&& undoItem->caretsBefore[0].byte == header->caretsBefore[1].byte) {
					// Merge the items.
					undoItem->caretsBefore[0] = header->caretsBefore[0];
					EsUndoPop(textbox->undo);
				} else {
					// Add the new item to the same group as the previous.
					EsUndoContinueGroup(textbox->undo); 
				}
			}
		}

		undoItem->textbox = textbox;
		EsUndoPush(textbox->undo, TextboxUndoItemCallback, undoItem, undoItemBytes, false /* do not set instance's undo manager */);
	}

	EsHeapFree(undoItem);

	// double time = EsPerformanceTimerPop();
	// EsPrint("EsTextboxInsert in %Fms (%Fms measuring new lines).\n", time * 1000, measureLineTime * 1000);

	textbox->scroll.Refresh();
	TextboxUpdateCommands(textbox, true);
}

char *EsTextboxGetContents(EsTextbox *textbox, size_t *_bytes, uint32_t flags) {
	EsMessageMutexCheck();

	TextboxSetActiveLine(textbox, -1);

	bool includeNewline = textbox->flags & ES_TEXTBOX_MULTILINE;
	size_t bytes = textbox->dataBytes + (includeNewline ? textbox->lines.Length() : 0);
	char *buffer = (char *) EsHeapAllocate(bytes + 1, false);
	buffer[bytes] = 0;

	uintptr_t position = 0;
	uintptr_t lineFrom = 0, lineTo = textbox->lines.Length() - 1;

	if (flags & ES_TEXTBOX_GET_CONTENTS_SELECTED_ONLY) {
		lineFrom = textbox->carets[0].line;
		lineTo = textbox->carets[1].line;

		if (lineFrom > lineTo) { 
			uintptr_t swap = lineFrom; 
			lineFrom = lineTo, lineTo = swap; 
		}
	}

	for (uintptr_t i = lineFrom; i <= lineTo; i++) {
		DocumentLine *line = &textbox->lines[i];

		uintptr_t offsetFrom = 0;
		uintptr_t offsetTo = line->lengthBytes;

		if (flags & ES_TEXTBOX_GET_CONTENTS_SELECTED_ONLY) {
			if (i == lineFrom) {
				offsetFrom = TextboxCompareCarets(textbox->carets + 0, textbox->carets + 1) < 0 ? textbox->carets[0].byte : textbox->carets[1].byte; 
			}

			if (i == lineTo) {
				offsetTo = TextboxCompareCarets(textbox->carets + 0, textbox->carets + 1) > 0 ? textbox->carets[0].byte : textbox->carets[1].byte; 
			}
		}

		EsMemoryCopy(buffer + position, GET_BUFFER(line) + offsetFrom, offsetTo - offsetFrom);
		position += offsetTo - offsetFrom;

		if (includeNewline && i != lineTo) {
			buffer[position++] = '\n';
		}
	}

	buffer[position] = 0;
	EsAssert(position <= bytes); 
	if (_bytes) *_bytes = position;

	char *result = (char *) EsHeapReallocate(buffer, position + 1, false);

	if (!result) {
		EsHeapFree(buffer);
	}

	return result;
}

double EsTextboxGetContentsAsDouble(EsTextbox *textbox, uint32_t flags) {
	size_t bytes;
	char *text = EsTextboxGetContents(textbox, &bytes, flags);
	double result = EsDoubleParse(text, bytes, nullptr); 
	EsHeapFree(text);
	return result;
}

bool EsTextboxFind(EsTextbox *textbox, const char *needle, intptr_t _needleBytes, int32_t *_line, int32_t *_byte, uint32_t flags) {
	EsMessageMutexCheck();

	if (_needleBytes == 0) {
		return false;
	}

	uintptr_t needleBytes = _needleBytes == -1 ? EsCStringLength(needle) : _needleBytes;
	uint32_t lineIndex = *_line, byteIndex = *_byte;
	bool firstLoop = true;

	while (true) {
		DocumentLine *line = &textbox->lines[lineIndex];
		const char *buffer = GET_BUFFER(line);
		size_t bufferBytes = line->lengthBytes;
		EsAssert(byteIndex <= bufferBytes); // Invalid find byte offset.

		// TODO Case-insensitive search.
		// TODO Ignore quotation mark type.

		if (flags & ES_TEXTBOX_FIND_BACKWARDS) {
			if (bufferBytes >= needleBytes) {
				for (uintptr_t i = byteIndex; i >= needleBytes; i--) {
					for (uintptr_t j = 0; j < needleBytes; j++) {
						if (buffer[i - needleBytes + j] != needle[j]) {
							goto previousPosition;
						}
					}

					*_line = lineIndex;
					*_byte = i - needleBytes;
					return true;

					previousPosition:;
				}
			}

			if ((int32_t) lineIndex <= *_line && !firstLoop) {
				return false;
			}

			if (lineIndex == 0) {
				firstLoop = false;
				lineIndex = textbox->lines.Length() - 1;
			} else {
				lineIndex--;
			}

			byteIndex = textbox->lines[lineIndex].lengthBytes;
		} else {
			if (bufferBytes >= needleBytes) {
				for (uintptr_t i = byteIndex; i <= bufferBytes - needleBytes; i++) {
					for (uintptr_t j = 0; j < needleBytes; j++) {
						if (buffer[i + j] != needle[j]) {
							goto nextPosition;
						}
					}

					*_line = lineIndex;
					*_byte = i;
					return true;

					nextPosition:;
				}
			}

			lineIndex++;

			if ((int32_t) lineIndex > *_line && !firstLoop) {
				return false;
			}

			if (lineIndex == textbox->lines.Length()) {
				firstLoop = false;
				lineIndex = 0;
			}

			byteIndex = 0;
		}
	}

	return false;
}

bool TextboxFindCaret(EsTextbox *textbox, int positionX, int positionY, bool secondCaret, int clickChainCount) {
	int startLine0 = textbox->carets[0].line, startLine1 = textbox->carets[1].line;
	EsRectangle bounds = textbox->GetBounds();

	if (positionX < 0) {
		positionX = 0;
	} else if (positionX >= bounds.r) {
		positionX = bounds.r - 1;
	}

	if (positionY < 0) {
		positionY = 0;
	} else if (positionY >= bounds.b) {
		positionY = bounds.b - 1;
	}

	if (clickChainCount >= 4) {
		textbox->carets[0].line = 0;
		textbox->carets[0].byte = 0;
		textbox->carets[1].line = textbox->lines.Length() - 1;
		textbox->carets[1].byte = textbox->lines[textbox->lines.Length() - 1].lengthBytes;
	} else {
		for (uintptr_t i = 0; i < textbox->visibleLines.Length(); i++) {
			TextboxVisibleLine *visibleLine = &textbox->visibleLines[i];
			DocumentLine *line = &textbox->lines[textbox->firstVisibleLine + i];

			EsRectangle lineBounds = ES_RECT_4(textbox->insets.l, bounds.r, 
					textbox->insets.t + visibleLine->yPosition, 
					textbox->insets.t + visibleLine->yPosition + line->height);
			lineBounds.l -= textbox->scroll.position[0];
			lineBounds.t -= textbox->scroll.position[1];
			lineBounds.b -= textbox->scroll.position[1];

			if (!((positionY >= lineBounds.t || i + textbox->firstVisibleLine == 0) && (positionY < lineBounds.b 
							|| i + textbox->firstVisibleLine == textbox->lines.Length() - 1))) {
				continue;
			}

			if (!line->lengthBytes) {
				textbox->carets[1].byte = 0;
			} else {
				DocumentLine *line = &textbox->lines[i + textbox->firstVisibleLine];
				int pointX = positionX + textbox->scroll.position[0] - textbox->insets.l;
				if (pointX < 0) pointX = 0;
				ptrdiff_t result = TextGetCharacterAtPoint(textbox, &textbox->textStyle,
						GET_BUFFER(line), line->lengthBytes, 
						&pointX, ES_TEXT_GET_CHARACTER_AT_POINT_MIDDLE);
				textbox->carets[1].byte = result == -1 ? line->lengthBytes : result;
			}

			textbox->carets[1].line = i + textbox->firstVisibleLine;

			break;
		}

		if (!secondCaret) {
			textbox->carets[0] = textbox->carets[1];

			if (clickChainCount == 2) {
				TextboxMoveCaret(textbox, textbox->carets + 0, MOVE_CARET_BACKWARDS, MOVE_CARET_WORD, true);
				TextboxMoveCaret(textbox, textbox->carets + 1, MOVE_CARET_FORWARDS,  MOVE_CARET_WORD, true);
				textbox->wordSelectionAnchor  = textbox->carets[0];
				textbox->wordSelectionAnchor2 = textbox->carets[1];
			} else if (clickChainCount == 3) {
				TextboxMoveCaret(textbox, textbox->carets + 0, MOVE_CARET_BACKWARDS, MOVE_CARET_LINE, true);
				TextboxMoveCaret(textbox, textbox->carets + 1, MOVE_CARET_FORWARDS,  MOVE_CARET_LINE, true);
				textbox->wordSelectionAnchor  = textbox->carets[0];
				textbox->wordSelectionAnchor2 = textbox->carets[1];
			}
		} else {
			if (clickChainCount == 2) {
				if (TextboxCompareCarets(textbox->carets + 1, textbox->carets + 0) < 0) {
					TextboxMoveCaret(textbox, textbox->carets + 1, MOVE_CARET_BACKWARDS, MOVE_CARET_WORD);
					textbox->carets[0] = textbox->wordSelectionAnchor2;
				} else {
					TextboxMoveCaret(textbox, textbox->carets + 1, MOVE_CARET_FORWARDS, MOVE_CARET_WORD);
					textbox->carets[0] = textbox->wordSelectionAnchor;
				}
			} else if (clickChainCount == 3) {
				if (TextboxCompareCarets(textbox->carets + 1, textbox->carets + 0) < 0) {
					TextboxMoveCaret(textbox, textbox->carets + 1, MOVE_CARET_BACKWARDS, MOVE_CARET_LINE);
					textbox->carets[0] = textbox->wordSelectionAnchor2;
				} else {
					TextboxMoveCaret(textbox, textbox->carets + 1, MOVE_CARET_FORWARDS, MOVE_CARET_LINE);
					textbox->carets[0] = textbox->wordSelectionAnchor;
				}
			}
		}
	}

	TextboxUpdateCommands(textbox, true);
	return textbox->carets[0].line != startLine0 || textbox->carets[1].line != startLine1;
}

void TextboxMoveCaretToCursor(EsTextbox *textbox, int x, int y, bool doNotMoveIfNoSelection) {
	int oldCompare = TextboxCompareCarets(textbox->carets + 0, textbox->carets + 1);
	bool hasSelection = oldCompare != 0;
	TextboxCaret old[2] = { textbox->carets[0], textbox->carets[1] };
	bool lineChanged = TextboxFindCaret(textbox, x, y, gui.clickChainCount == 1, gui.clickChainCount);

	if (doNotMoveIfNoSelection && TextboxCompareCarets(&old[0], &old[1]) != 0) {
		textbox->carets[0] = old[0];
		textbox->carets[1] = old[1];
	} else if (gui.clickChainCount == 1 && !EsKeyboardIsShiftHeld()) {
		textbox->carets[0] = textbox->carets[1];
	}

	TextboxUpdateCommands(textbox, true);
	textbox->verticalMotionHorizontalDepth = -1;
	TextboxRepaintLine(textbox, lineChanged || hasSelection ? -1 : textbox->carets[0].line);
	EsTextboxEnsureCaretVisible(textbox);
}

int ProcessTextboxMarginMessage(EsElement *element, EsMessage *message) {
	EsTextbox *textbox = (EsTextbox *) element->parent;

	if (message->type == ES_MSG_PAINT) {
		EsPainter *painter = message->painter;

		for (int32_t i = 0; i < (int32_t) textbox->visibleLines.Length(); i++) {
			TextboxVisibleLine *visibleLine = &textbox->visibleLines[i];
			DocumentLine *line = &textbox->lines[i + textbox->firstVisibleLine];

			EsRectangle bounds;
			bounds.l = painter->offsetX + element->style->insets.l;
			bounds.r = painter->offsetX + painter->width - element->style->insets.r;
			bounds.t = painter->offsetY + textbox->insets.t + visibleLine->yPosition - textbox->scroll.position[1];
			bounds.b = bounds.t + line->height;

			char label[64];
			EsTextRun textRun[2] = {};
			element->style->GetTextStyle(&textRun[0].style);
			textRun[0].style.figures = ES_TEXT_FIGURE_TABULAR;
			textRun[1].offset = EsStringFormat(label, sizeof(label), "%d", i + textbox->firstVisibleLine + 1);
			EsTextPlanProperties properties = {};
			properties.flags = ES_TEXT_V_CENTER | ES_TEXT_H_RIGHT | ES_TEXT_ELLIPSIS | ES_TEXT_PLAN_SINGLE_USE;
			EsTextPlan *plan = EsTextPlanCreate(element, &properties, bounds, label, textRun, 1);
			if (plan) EsDrawText(painter, plan, bounds, nullptr, nullptr);
		}
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
		return ES_HANDLED;
	}

	return 0;
}

void TextboxStyleChanged(EsTextbox *textbox) {
	textbox->borders = textbox->style->borders;
	textbox->insets = textbox->style->insets;

	if (textbox->flags & ES_TEXTBOX_MARGIN) {
		int marginWidth = textbox->margin->style->preferredWidth;
		textbox->borders.l += marginWidth;
		textbox->insets.l += marginWidth + textbox->margin->style->gapMajor;
	}

	int lineHeight = TextGetLineHeight(textbox, &textbox->textStyle);

	for (int32_t i = 0; i < (int32_t) textbox->lines.Length(); i++) {
		DocumentLine *line = &textbox->lines[i];
		DocumentLine *previous = i ? (&textbox->lines[i - 1]) : nullptr;
		line->height = lineHeight;
		line->yPosition = previous ? (previous->yPosition + previous->height) : 0;
		line->lengthWidth = -1;
		textbox->longestLine = -1;
	}

	TextboxRefreshVisibleLines(textbox);
	TextboxFindLongestLine(textbox);
	textbox->scroll.Refresh();
	EsElementRepaint(textbox);
}

int ProcessTextboxMessage(EsElement *element, EsMessage *message) {
	EsTextbox *textbox = (EsTextbox *) element;

	if (!textbox->editing && textbox->overlayCallback) {
		int response = textbox->overlayCallback(element, message);
		if (response != 0 && message->type != ES_MSG_DESTROY) return response;
	}

	int response = textbox->scroll.ReceivedMessage(message);
	if (response) return response;
	response = ES_HANDLED;

	if (message->type == ES_MSG_PAINT) {
		EsPainter *painter = message->painter;

		EsTextSelection selectionProperties = {};
		selectionProperties.hideCaret = (~textbox->state & UI_STATE_FOCUSED) || (textbox->flags & ES_ELEMENT_DISABLED) || !textbox->editing;
		selectionProperties.snapCaretToInsets = true;
		selectionProperties.background = textbox->style->metrics->selectedBackground;
		selectionProperties.foreground = textbox->style->metrics->selectedText;

		EsRectangle clip;
		EsRectangleClip(painter->clip, ES_RECT_4(painter->offsetX + textbox->borders.l, 
					painter->offsetX + painter->width - textbox->borders.r, 
					painter->offsetY + textbox->borders.t, 
					painter->offsetY + painter->height - textbox->borders.b), &clip);

		Array<EsTextRun> textRuns = {};

		for (int32_t i = 0; i < (int32_t) textbox->visibleLines.Length(); i++) {
			TextboxVisibleLine *visibleLine = &textbox->visibleLines[i];
			DocumentLine *line = &textbox->lines[i + textbox->firstVisibleLine];

			EsRectangle lineBounds = ES_RECT_4(painter->offsetX + textbox->insets.l, 
					painter->offsetX + painter->width, 
					painter->offsetY + textbox->insets.t + visibleLine->yPosition, 
					painter->offsetY + textbox->insets.t + visibleLine->yPosition + line->height);
			lineBounds.l -= textbox->scroll.position[0];
			lineBounds.t -= textbox->scroll.position[1];
			lineBounds.b -= textbox->scroll.position[1];

			if (~textbox->flags & ES_TEXTBOX_MULTILINE) {
				lineBounds.b = painter->offsetY + painter->height - textbox->insets.b;
			}

			int32_t caret0 = textbox->carets[0].byte, caret1 = textbox->carets[1].byte;
			if (textbox->carets[0].line < i + textbox->firstVisibleLine) caret0 = -2;
			if (textbox->carets[0].line > i + textbox->firstVisibleLine) caret0 = line->lengthBytes + 2;
			if (textbox->carets[1].line < i + textbox->firstVisibleLine) caret1 = -2;
			if (textbox->carets[1].line > i + textbox->firstVisibleLine) caret1 = line->lengthBytes + 2;

			if (textbox->carets[1].line == i + textbox->firstVisibleLine && textbox->syntaxHighlightingLanguage) {
				EsRectangle line = ES_RECT_4(painter->offsetX, painter->offsetX + painter->width, lineBounds.t, lineBounds.b);
				EsDrawBlock(painter, line, textbox->syntaxHighlightingColors[0]);
			}

			if (textbox->syntaxHighlightingLanguage && line->lengthBytes) {
				if (textRuns.Length()) textRuns.SetLength(0);
				textRuns = TextApplySyntaxHighlighting(&textbox->textStyle, textbox->syntaxHighlightingLanguage, 
						textbox->syntaxHighlightingColors, textRuns, GET_BUFFER(line), line->lengthBytes);
			} else {
				textRuns.SetLength(2);
				textRuns[0].style = textbox->textStyle;
				textRuns[0].offset = 0;
				textRuns[1].offset = line->lengthBytes;
			}

			EsTextPlanProperties properties = {};
			properties.flags = ES_TEXT_V_CENTER | ES_TEXT_H_LEFT | ES_TEXT_PLAN_SINGLE_USE;
			selectionProperties.caret0 = caret0; 
			selectionProperties.caret1 = caret1;
			EsTextPlan *plan;

			if (!textRuns.Length()) {
				plan = nullptr;
			} else if (textRuns[1].offset) {
				plan = EsTextPlanCreate(element, &properties, lineBounds, GET_BUFFER(line), textRuns.array, textRuns.Length() - 1);
			} else {
				textRuns[1].offset = 1; // Make sure that the caret and selection is draw correctly, even on empty lines.
				plan = EsTextPlanCreate(element, &properties, lineBounds, " ", textRuns.array, textRuns.Length() - 1);
			}

			if (plan) {
				EsDrawText(painter, plan, lineBounds, &clip, &selectionProperties);
			}
		}

		textRuns.Free();
	} else if (message->type == ES_MSG_LAYOUT) {
		EsRectangle bounds = textbox->GetBounds();

		if (textbox->margin) {
			int marginWidth = textbox->margin->style->preferredWidth;
			textbox->margin->InternalMove(marginWidth, Height(bounds), bounds.l, bounds.t);
		}

		TextboxRefreshVisibleLines(textbox);
	} else if (message->type == ES_MSG_DESTROY) {
		textbox->visibleLines.Free();
		textbox->lines.Free();
		UndoManagerDestroy(&textbox->localUndo);
		EsHeapFree(textbox->activeLine);
		EsHeapFree(textbox->data);
		EsHeapFree(textbox->editStartContent);
	} else if (message->type == ES_MSG_KEY_TYPED && !ScancodeIsNonTypeable(message->keyboard.scancode)) {
		bool verticalMotion = false;
		bool ctrl = message->keyboard.modifiers & ES_MODIFIER_CTRL;

		if (message->keyboard.modifiers & ~(ES_MODIFIER_CTRL | ES_MODIFIER_ALT | ES_MODIFIER_SHIFT | ES_MODIFIER_ALT_GR)) {
			// Unused modifier.
			return 0;
		}

		if (message->keyboard.scancode == ES_SCANCODE_LEFT_ARROW || message->keyboard.scancode == ES_SCANCODE_RIGHT_ARROW
				|| message->keyboard.scancode == ES_SCANCODE_HOME || message->keyboard.scancode == ES_SCANCODE_END
				|| message->keyboard.scancode == ES_SCANCODE_UP_ARROW || message->keyboard.scancode == ES_SCANCODE_DOWN_ARROW) {
			bool direction = (message->keyboard.scancode == ES_SCANCODE_LEFT_ARROW || message->keyboard.scancode == ES_SCANCODE_HOME 
					|| message->keyboard.scancode == ES_SCANCODE_UP_ARROW)
				? MOVE_CARET_BACKWARDS : MOVE_CARET_FORWARDS;
			int moveType = (message->keyboard.scancode == ES_SCANCODE_HOME || message->keyboard.scancode == ES_SCANCODE_END) 
				? (ctrl ? MOVE_CARET_ALL : MOVE_CARET_LINE)
				: ((message->keyboard.scancode == ES_SCANCODE_UP_ARROW || message->keyboard.scancode == ES_SCANCODE_DOWN_ARROW) 
						? MOVE_CARET_VERTICAL : (ctrl ? MOVE_CARET_WORD : MOVE_CARET_SINGLE));
			if (moveType == MOVE_CARET_VERTICAL) verticalMotion = true;

			int32_t lineFrom = textbox->carets[1].line;

			if (message->keyboard.modifiers & ES_MODIFIER_SHIFT) {
				TextboxMoveCaret(textbox, &textbox->carets[1], direction, moveType);
			} else {
				int caretCompare = TextboxCompareCarets(textbox->carets + 1, textbox->carets + 0);

				if ((caretCompare < 0 && direction == MOVE_CARET_BACKWARDS) || (caretCompare > 0 && direction == MOVE_CARET_FORWARDS)) {
					textbox->carets[0] = textbox->carets[1];
					TextboxUpdateCommands(textbox, true);
				} else if ((caretCompare > 0 && direction == MOVE_CARET_BACKWARDS) || (caretCompare < 0 && direction == MOVE_CARET_FORWARDS)) {
					textbox->carets[1] = textbox->carets[0];
					TextboxUpdateCommands(textbox, true);
				} else {
					TextboxMoveCaret(textbox, &textbox->carets[1], direction, moveType);
					textbox->carets[0] = textbox->carets[1];
					TextboxUpdateCommands(textbox, true);
				}
			}

			int32_t lineTo = textbox->carets[1].line;
			if (lineFrom > lineTo) { int32_t t = lineTo; lineTo = lineFrom; lineFrom = t; }
			for (int32_t i = lineFrom; i <= lineTo; i++) TextboxRepaintLine(textbox, i);
		} else if (message->keyboard.scancode == ES_SCANCODE_PAGE_UP || message->keyboard.scancode == ES_SCANCODE_PAGE_DOWN) {
			for (uintptr_t i = 0; i < 10; i++) {
				TextboxMoveCaret(textbox, textbox->carets + 1, 
						message->keyboard.scancode == ES_SCANCODE_PAGE_UP ? MOVE_CARET_BACKWARDS : MOVE_CARET_FORWARDS, 
						MOVE_CARET_VERTICAL);
			}

			if (~message->keyboard.modifiers & ES_MODIFIER_SHIFT) {
				textbox->carets[0] = textbox->carets[1];
				TextboxUpdateCommands(textbox, true);
			}
			
			textbox->Repaint(true);
			verticalMotion = true;
		} else if (message->keyboard.scancode == ES_SCANCODE_BACKSPACE || message->keyboard.scancode == ES_SCANCODE_DELETE) {
			if (!textbox->editing) {
				EsTextboxStartEdit(textbox);
			}

			if (!TextboxCompareCarets(textbox->carets + 0, textbox->carets + 1)) {
				TextboxMoveCaret(textbox, textbox->carets + 1, message->keyboard.scancode == ES_SCANCODE_BACKSPACE ? MOVE_CARET_BACKWARDS : MOVE_CARET_FORWARDS, 
						ctrl ? MOVE_CARET_WORD : MOVE_CARET_SINGLE);
			}

			EsTextboxInsert(textbox, EsLiteral(""));
		} else if (message->keyboard.scancode == ES_SCANCODE_ENTER && (textbox->flags & ES_TEXTBOX_EDIT_BASED)) {
			if (textbox->editing) {
				TextboxEndEdit(textbox, false);
			} else {
				EsTextboxStartEdit(textbox);
			}
		} else if (message->keyboard.scancode == ES_SCANCODE_ESCAPE && (textbox->flags & ES_TEXTBOX_EDIT_BASED)) {
			TextboxEndEdit(textbox, true);
		} else if (message->keyboard.scancode == ES_SCANCODE_TAB && (~textbox->flags & ES_TEXTBOX_ALLOW_TABS)) {
			response = 0;
		} else {
			if (!textbox->editing) {
				EsTextboxStartEdit(textbox);
			}

			const char *inputString = KeyboardLayoutLookup(message->keyboard.scancode, 
					message->keyboard.modifiers & ES_MODIFIER_SHIFT, message->keyboard.modifiers & ES_MODIFIER_ALT_GR, 
					true, textbox->flags & ES_TEXTBOX_MULTILINE);

			if (inputString && (message->keyboard.modifiers & ~(ES_MODIFIER_SHIFT | ES_MODIFIER_ALT_GR)) == 0) {
				if (textbox->smartQuotes && api.global->useSmartQuotes) {
					DocumentLine *currentLine = &textbox->lines[textbox->carets[0].line];
					const char *buffer = GET_BUFFER(currentLine);
					bool left = !textbox->carets[0].byte || buffer[textbox->carets[0].byte - 1] == ' ';

					if (inputString[0] == '"' && inputString[1] == 0) {
						inputString = left ? "\u201C" : "\u201D";
					} else if (inputString[0] == '\'' && inputString[1] == 0) {
						inputString = left ? "\u2018" : "\u2019";
					}
				}

				EsTextboxInsert(textbox, inputString, -1);

				if (inputString[0] == '\n' && inputString[1] == 0 && textbox->carets[0].line) {
					// Copy the indentation from the previous line.

					DocumentLine *previousLine = &textbox->lines[textbox->carets[0].line - 1];
					const char *buffer = GET_BUFFER(previousLine);
					int32_t i = 0;
					
					for (; i < previousLine->lengthBytes; i++) {
						if (buffer[i] != '\t') {
							break;
						}
					}

					EsTextboxInsert(textbox, buffer, i);
				}
			} else {
				response = 0;
			}
		}

		if (!verticalMotion) {
			textbox->verticalMotionHorizontalDepth = -1;
		}

		if (response != 0 && (~textbox->state & UI_STATE_DESTROYING)) {
			TextboxFindLongestLine(textbox);
			textbox->scroll.Refresh();
			EsTextboxEnsureCaretVisible(textbox);
		}
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN || message->type == ES_MSG_MOUSE_RIGHT_DOWN) {
		TextboxMoveCaretToCursor(textbox, message->mouseDown.positionX, message->mouseDown.positionY, message->type == ES_MSG_MOUSE_RIGHT_DOWN);
	} else if (message->type == ES_MSG_MOUSE_LEFT_CLICK) {
		EsTextboxStartEdit(textbox);
	} else if (message->type == ES_MSG_FOCUSED_START || message->type == ES_MSG_PRIMARY_CLIPBOARD_UPDATED) {
		TextboxUpdateCommands(textbox, false);
		EsInstanceSetActiveUndoManager(textbox->instance, textbox->undo);
		textbox->Repaint(true);
	} else if (message->type == ES_MSG_FOCUSED_END) {
		EsCommandSetCallback(EsCommandByID(textbox->instance, ES_COMMAND_SELECT_ALL), nullptr);
		EsCommandSetCallback(EsCommandByID(textbox->instance, ES_COMMAND_DELETE), nullptr);
		EsCommandSetCallback(EsCommandByID(textbox->instance, ES_COMMAND_COPY), nullptr);
		EsCommandSetCallback(EsCommandByID(textbox->instance, ES_COMMAND_CUT), nullptr);
		EsCommandSetCallback(EsCommandByID(textbox->instance, ES_COMMAND_PASTE), nullptr);
		EsInstanceSetActiveUndoManager(textbox->instance, textbox->instance->undoManager);
		textbox->Repaint(true);
	} else if (message->type == ES_MSG_STRONG_FOCUS_END) {
		TextboxEndEdit(textbox, textbox->flags & ES_TEXTBOX_REJECT_EDIT_IF_LOST_FOCUS);
	} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG || message->type == ES_MSG_MOUSE_RIGHT_DRAG || (message->type == ES_MSG_ANIMATE && textbox->scroll.dragScrolling)) {
		int32_t lineFrom = textbox->carets[1].line;

		if (gui.lastClickButton == ES_MSG_MOUSE_RIGHT_DOWN && !textbox->inRightClickDrag) {
			TextboxMoveCaretToCursor(textbox, message->mouseDragged.originalPositionX, message->mouseDragged.originalPositionY, false);
			textbox->inRightClickDrag = true;
		}

		EsPoint position = EsMouseGetPosition(textbox); 
		TextboxFindCaret(textbox, position.x, position.y, true, gui.clickChainCount);

		int32_t lineTo = textbox->carets[1].line;
		if (lineFrom > lineTo) { int32_t t = lineTo; lineTo = lineFrom; lineFrom = t; }
		for (int32_t i = lineFrom; i <= lineTo; i++) TextboxRepaintLine(textbox, i);
	} else if (message->type == ES_MSG_GET_CURSOR) {
		if (!textbox->editing || (textbox->flags & ES_ELEMENT_DISABLED)) {
			message->cursorStyle = ES_CURSOR_NORMAL;
		} else {
			return 0;
		}
	} else if (message->type == ES_MSG_MOUSE_RIGHT_UP) {
		textbox->inRightClickDrag = false;
		EsMenu *menu = EsMenuCreate(textbox, ES_MENU_AT_CURSOR);
		if (!menu) return ES_HANDLED;

		// TODO User customisation of menus.

		if (textbox->editing) {
			EsMenuAddCommand(menu, 0, INTERFACE_STRING(CommonUndo), EsCommandByID(textbox->instance, ES_COMMAND_UNDO));
			EsMenuAddSeparator(menu);
		}

		EsMenuAddCommand(menu, 0, INTERFACE_STRING(CommonClipboardCut), EsCommandByID(textbox->instance, ES_COMMAND_CUT));
		EsMenuAddCommand(menu, 0, INTERFACE_STRING(CommonClipboardCopy), EsCommandByID(textbox->instance, ES_COMMAND_COPY));
		EsMenuAddCommand(menu, 0, INTERFACE_STRING(CommonClipboardPaste), EsCommandByID(textbox->instance, ES_COMMAND_PASTE));

		if (textbox->editing) {
			EsMenuAddSeparator(menu);
			EsMenuAddCommand(menu, 0, INTERFACE_STRING(CommonSelectionSelectAll), EsCommandByID(textbox->instance, ES_COMMAND_SELECT_ALL));
			EsMenuAddCommand(menu, 0, INTERFACE_STRING(CommonSelectionDelete), EsCommandByID(textbox->instance, ES_COMMAND_DELETE));

			// Add the smart context menu, if necessary.

			if ((~textbox->flags & ES_TEXTBOX_NO_SMART_CONTEXT_MENUS) && textbox->carets[0].line == textbox->carets[1].line) {
				int32_t selectionFrom = textbox->carets[0].byte, selectionTo = textbox->carets[1].byte;

				if (selectionTo < selectionFrom) {
					int32_t temporary = selectionFrom;
					selectionFrom = selectionTo;
					selectionTo = temporary;
				}

				if (selectionTo - selectionFrom == 7) {
					char buffer[7];
					EsMemoryCopy(buffer, GET_BUFFER(&textbox->lines[textbox->carets[0].line]) + selectionFrom, 7);

					if (buffer[0] == '#' && EsCRTisxdigit(buffer[1]) && EsCRTisxdigit(buffer[2]) && EsCRTisxdigit(buffer[3])
							&& EsCRTisxdigit(buffer[4]) && EsCRTisxdigit(buffer[5]) && EsCRTisxdigit(buffer[6])) {
						// It's a color hex-code!
						// TODO Versions with alpha.
						EsMenuNextColumn(menu);
						ColorPickerCreate(menu, { textbox }, EsColorParse(buffer, 7), false);

						textbox->colorUppercase = true;

						for (uintptr_t i = 1; i <= 6; i++) {
							if (buffer[i] >= 'a' && buffer[i] <= 'f') {
								textbox->colorUppercase = false;
								break;
							}
						}
					}
				}
			}
		}

		EsMenuShow(menu);
	} else if (message->type == ES_MSG_COLOR_CHANGED) {
		EsAssert(~textbox->flags & ES_TEXTBOX_NO_SMART_CONTEXT_MENUS); // Textbox sent color changed message, but it cannot have smart context menus?
		uint32_t color = message->colorChanged.newColor;

		if (message->colorChanged.pickerClosed) {
			int32_t selectionFrom = textbox->carets[0].byte, selectionTo = textbox->carets[1].byte;

			if (textbox->carets[0].line == textbox->carets[1].line && AbsoluteInteger(selectionFrom - selectionTo) == 7) {
				char buffer[7];
				const char *hexChars = textbox->colorUppercase ? "0123456789ABCDEF" : "0123456789abcedf";
				size_t length = EsStringFormat(buffer, 7, "#%c%c%c%c%c%c", 
						hexChars[(color >> 20) & 0xF], hexChars[(color >> 16) & 0xF], hexChars[(color >> 12) & 0xF], 
						hexChars[(color >> 8) & 0xF], hexChars[(color >> 4) & 0xF], hexChars[(color >> 0) & 0xF]);
				EsTextboxInsert(textbox, buffer, length, true);
				EsTextboxSetSelection(textbox, textbox->carets[1].line, textbox->carets[1].byte - 7, 
						textbox->carets[1].line, textbox->carets[1].byte);
			}
		}
	} else if (message->type == ES_MSG_GET_WIDTH) {
		message->measure.width = textbox->longestLineWidth + textbox->insets.l + textbox->insets.r;
	} else if (message->type == ES_MSG_GET_HEIGHT) {
		DocumentLine *lastLine = &textbox->lines.Last();
		message->measure.height = lastLine->yPosition + lastLine->height + textbox->insets.t + textbox->insets.b;
	} else if (message->type == ES_MSG_SCROLL_X) {
		TextboxSetHorizontalScroll(textbox, message->scroll.scroll);
	} else if (message->type == ES_MSG_SCROLL_Y) {
		TextboxRefreshVisibleLines(textbox, false);
		EsElementRepaintForScroll(textbox, message, EsRectangleAdd(element->GetInternalOffset(), element->style->borders));
	} else if (message->type == ES_MSG_GET_INSPECTOR_INFORMATION) {
		DocumentLine *firstLine = &textbox->lines.First();
		EsBufferFormat(message->getContent.buffer, "'%s'", firstLine->lengthBytes, GET_BUFFER(firstLine));
	} else if (message->type == ES_MSG_UI_SCALE_CHANGED) {
		if (textbox->margin) {
			// Force the margin to update its style now, so that its width can be read correctly by TextboxStyleChanged.
			textbox->margin->RefreshStyle(nullptr, false, true);
		}

		textbox->style->GetTextStyle(&textbox->textStyle);

		if (textbox->overrideTextSize) {
			textbox->textStyle.size = textbox->overrideTextSize;
		}

		if (textbox->overrideFont.family) {
			textbox->textStyle.font = textbox->overrideFont;
		}

		TextboxStyleChanged(textbox);
	} else {
		response = 0;
	}

	return response;
}

EsTextbox *EsTextboxCreate(EsElement *parent, uint64_t flags, const EsStyle *style) {
	EsTextbox *textbox = (EsTextbox *) EsHeapAllocate(sizeof(EsTextbox), true);
	if (!textbox) return nullptr;

	if (!style) {
		if (flags & ES_TEXTBOX_MULTILINE) {
			style = ES_STYLE_TEXTBOX_BORDERED_MULTILINE;
		} else {
			style = ES_STYLE_TEXTBOX_BORDERED_SINGLE;
		}
	}

	textbox->Initialise(parent, ES_ELEMENT_FOCUSABLE | flags, ProcessTextboxMessage, style);
	textbox->cName = "textbox";

	textbox->scroll.Setup(textbox, 
			(flags & ES_TEXTBOX_MULTILINE) ? ES_SCROLL_MODE_AUTO : ES_SCROLL_MODE_HIDDEN, 
			(flags & ES_TEXTBOX_MULTILINE) ? ES_SCROLL_MODE_AUTO : ES_SCROLL_MODE_NONE,
			ES_SCROLL_X_DRAG | ES_SCROLL_Y_DRAG);

	textbox->undo = &textbox->localUndo;
	textbox->undo->instance = textbox->instance;

	textbox->borders = textbox->style->borders;
	textbox->insets = textbox->style->insets;

	textbox->style->GetTextStyle(&textbox->textStyle);

	textbox->smartQuotes = true;

	DocumentLine firstLine = {};
	firstLine.height = TextGetLineHeight(textbox, &textbox->textStyle);
	textbox->lines.Add(firstLine);

	TextboxVisibleLine firstVisibleLine = {};
	textbox->visibleLines.Add(firstVisibleLine);

	textbox->activeLineIndex = textbox->verticalMotionHorizontalDepth = textbox->longestLine = -1;

	if (~flags & ES_TEXTBOX_EDIT_BASED) {
		textbox->editing = true;
	}

	if (textbox->flags & ES_TEXTBOX_MARGIN) {
		textbox->margin = EsCustomElementCreate(textbox, ES_CELL_FILL, ES_STYLE_TEXTBOX_MARGIN);
		textbox->margin->cName = "margin";
		textbox->margin->messageUser = ProcessTextboxMarginMessage;

		int marginWidth = textbox->margin->style->preferredWidth;
		textbox->borders.l += marginWidth;
		textbox->insets.l += marginWidth + textbox->margin->style->gapMajor;
	}

	return textbox;
}

void EsTextboxUseNumberOverlay(EsTextbox *textbox, bool defaultBehaviour) {
	EsMessageMutexCheck();

	EsAssert(textbox->flags & ES_TEXTBOX_EDIT_BASED); // Using textbox overlay without edit based mode.
	EsAssert(~textbox->flags & ES_TEXTBOX_MULTILINE); // Using number overlay with multiline mode.

	textbox->overlayData = defaultBehaviour;

	textbox->overlayCallback = [] (EsElement *element, EsMessage *message) {
		EsTextbox *textbox = (EsTextbox *) element;
		bool defaultBehaviour = textbox->overlayData.u;

		if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
			if (!gui.draggingStarted) {
				EsMessage m = { ES_MSG_TEXTBOX_NUMBER_DRAG_START };
				EsMessageSend(textbox, &m);
			}

			TextboxFindCaret(textbox, message->mouseDragged.originalPositionX, message->mouseDragged.originalPositionY, false, 1);
			
			EsMessage m = { ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA };
			m.numberDragDelta.delta = message->mouseDragged.originalPositionY - message->mouseDragged.newPositionY;
			m.numberDragDelta.fast = EsKeyboardIsShiftHeld();
			m.numberDragDelta.hoverCharacter = textbox->carets[1].byte;
			EsMessageSend(textbox, &m);

			EsMouseSetPosition(textbox->window, gui.lastClickX, gui.lastClickY);
		} else if (message->type == ES_MSG_KEY_TYPED && message->keyboard.scancode == ES_SCANCODE_UP_ARROW) {
			EsMessage m = { ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA };
			m.numberDragDelta.delta = 1;
			m.numberDragDelta.fast = EsKeyboardIsShiftHeld();
			m.numberDragDelta.hoverCharacter = 0;
			EsMessageSend(textbox, &m);
		} else if (message->type == ES_MSG_KEY_TYPED && message->keyboard.scancode == ES_SCANCODE_DOWN_ARROW) {
			EsMessage m = { ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA };
			m.numberDragDelta.delta = -1;
			m.numberDragDelta.fast = EsKeyboardIsShiftHeld();
			m.numberDragDelta.hoverCharacter = 0;
			EsMessageSend(textbox, &m);
		} else if (message->type == ES_MSG_MOUSE_LEFT_UP) {
			if (gui.draggingStarted) {
				EsMessage m = { ES_MSG_TEXTBOX_NUMBER_DRAG_END };
				EsMessageSend(textbox, &m);
			}
		} else if (message->type == ES_MSG_GET_CURSOR) {
			if (gui.draggingStarted) {
				message->cursorStyle = ES_CURSOR_BLANK;
			} else if (~textbox->flags & ES_ELEMENT_DISABLED) {
				message->cursorStyle = ES_CURSOR_RESIZE_VERTICAL;
			} else {
				message->cursorStyle = ES_CURSOR_NORMAL;
			}
		} else if (message->type == ES_MSG_TEXTBOX_EDIT_END && defaultBehaviour) {
			double oldValue = EsDoubleParse(textbox->editStartContent, textbox->editStartContentBytes, nullptr);

			char *expression = EsTextboxGetContents(textbox);
			EsCalculationValue value = EsCalculateFromUserExpression(expression); 
			EsHeapFree(expression);

			if (value.error) {
				return ES_REJECTED;
			} else {
				EsMessage m = { ES_MSG_TEXTBOX_NUMBER_UPDATED };
				m.numberUpdated.delta = value.number - oldValue;
				m.numberUpdated.newValue = value.number;
				EsMessageSend(textbox, &m);

				char result[64];
				size_t resultBytes = EsStringFormat(result, sizeof(result), "%F", (double) m.numberUpdated.newValue);
				EsTextboxSelectAll(textbox);
				EsTextboxInsert(textbox, result, resultBytes);
			}
		} else if (message->type == ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA && defaultBehaviour) {
			TextboxSetActiveLine(textbox, -1);
			double oldValue = EsDoubleParse(textbox->data, textbox->lines[0].lengthBytes, nullptr);
			double newValue = oldValue + message->numberDragDelta.delta * (message->numberDragDelta.fast ? 10 : 1);

			EsMessage m = { ES_MSG_TEXTBOX_NUMBER_UPDATED };
			m.numberUpdated.delta = newValue - oldValue;
			m.numberUpdated.newValue = newValue;
			EsMessageSend(textbox, &m);

			char result[64];
			size_t resultBytes = EsStringFormat(result, sizeof(result), "%F", m.numberUpdated.newValue);
			EsTextboxSelectAll(textbox);
			EsTextboxInsert(textbox, result, resultBytes);
		} else {
			return 0;
		}

		return ES_HANDLED;
	};
}

void TextboxBreadcrumbOverlayRecreate(EsTextbox *textbox) {
	if (textbox->overlayData.p) {
		// Remove the old breadcrumb panel.
		((EsElement *) textbox->overlayData.p)->Destroy();
	}

	EsPanel *panel = EsPanelCreate(textbox, ES_PANEL_HORIZONTAL | ES_CELL_FILL | ES_ELEMENT_NO_HOVER, ES_STYLE_BREADCRUMB_BAR_PANEL);
	textbox->overlayData = panel;

	if (!panel) {
		return;
	}

	uint8_t _buffer[256];
	EsBuffer buffer = { .out = _buffer, .bytes = sizeof(_buffer) };
	EsMessage m = { ES_MSG_TEXTBOX_GET_BREADCRUMB };
	m.getBreadcrumb.buffer = &buffer;

	while (true) {
		buffer.position = 0, m.getBreadcrumb.icon = 0;
		int response = EsMessageSend(textbox, &m);
		EsAssert(response != 0); // Must handle ES_MSG_TEXTBOX_GET_BREADCRUMB message for breadcrumb overlay.
		if (response == ES_REJECTED) break;

		EsButton *crumb = EsButtonCreate(panel, ES_BUTTON_NOT_FOCUSABLE | ES_BUTTON_COMPACT | ES_CELL_V_FILL, 
				ES_STYLE_BREADCRUMB_BAR_CRUMB, (char *) buffer.out, buffer.position);

		if (crumb) {
			EsButtonSetIcon(crumb, m.getBreadcrumb.icon);

			crumb->userData = m.getBreadcrumb.index;

			crumb->messageUser = [] (EsElement *element, EsMessage *message) {
				if (message->type == ES_MSG_MOUSE_LEFT_CLICK) {
					EsMessage m = { ES_MSG_TEXTBOX_ACTIVATE_BREADCRUMB };
					m.activateBreadcrumb = element->userData.u;
					EsMessageSend(element->parent->parent, &m);
				} else {
					return 0;
				}

				return ES_HANDLED;
			};
		}
		
		m.getBreadcrumb.index++;
	}
}

void EsTextboxUseBreadcrumbOverlay(EsTextbox *textbox) {
	EsMessageMutexCheck();

	EsAssert(textbox->flags & ES_TEXTBOX_EDIT_BASED); // Using textbox overlay without edit based mode.

	// Use this to store the panel containing the breadcrumb buttons.
	textbox->overlayData = nullptr;

	textbox->overlayCallback = [] (EsElement *element, EsMessage *message) {
		EsTextbox *textbox = (EsTextbox *) element;

		if (message->type == ES_MSG_TEXTBOX_UPDATED) {
			TextboxBreadcrumbOverlayRecreate(textbox);
		} else if (message->type == ES_MSG_TEXTBOX_EDIT_START) {
			((EsElement *) textbox->overlayData.p)->Destroy();
			textbox->overlayData.p = nullptr;
		} else if (message->type == ES_MSG_TEXTBOX_EDIT_END) {
			TextboxBreadcrumbOverlayRecreate(textbox);
		} else if (message->type == ES_MSG_LAYOUT) {
			EsRectangle bounds = textbox->GetBounds();
			((EsElement *) textbox->overlayData.p)->InternalMove(bounds.r, bounds.b, 0, 0);
		} else if (message->type == ES_MSG_PAINT) {
			return ES_HANDLED;
		}

		return 0;
	};

	TextboxBreadcrumbOverlayRecreate(textbox);
}

void EsTextboxSetUndoManager(EsTextbox *textbox, EsUndoManager *undoManager) {
	EsMessageMutexCheck();
	EsAssert(~textbox->state & UI_STATE_FOCUSED); // Can't change undo manager if the textbox is focused.
	EsAssert(textbox->undo == &textbox->localUndo); // This can only be set once.
	textbox->undo = undoManager;
}

void EsTextboxSetTextSize(EsTextbox *textbox, uint16_t size) {
	textbox->overrideTextSize = size;
	textbox->textStyle.size = size;
	TextboxStyleChanged(textbox);
}

void EsTextboxSetFont(EsTextbox *textbox, EsFont font) {
	textbox->overrideFont = font;
	textbox->textStyle.font = font;
	TextboxStyleChanged(textbox);
}

void EsTextboxSetupSyntaxHighlighting(EsTextbox *textbox, uint32_t language, uint32_t *customColors, size_t customColorCount) {
	textbox->syntaxHighlightingLanguage = language;

	// TODO Load these from the theme file.
	textbox->syntaxHighlightingColors[0] = 0x04000000; // Highlighted line.
	textbox->syntaxHighlightingColors[1] = 0xFF000000; // Default.
	textbox->syntaxHighlightingColors[2] = 0xFFA11F20; // Comment.
	textbox->syntaxHighlightingColors[3] = 0xFF037E01; // String.
	textbox->syntaxHighlightingColors[4] = 0xFF213EF1; // Number.
	textbox->syntaxHighlightingColors[5] = 0xFF7F0480; // Operator.
	textbox->syntaxHighlightingColors[6] = 0xFF545D70; // Preprocessor.
	textbox->syntaxHighlightingColors[7] = 0xFF17546D; // Keyword.

	if (customColorCount > sizeof(textbox->syntaxHighlightingColors) / sizeof(uint32_t)) {
		customColorCount = sizeof(textbox->syntaxHighlightingColors) / sizeof(uint32_t);
	}

	EsMemoryCopy(textbox->syntaxHighlightingColors, customColors, customColorCount * sizeof(uint32_t));

	textbox->Repaint(true);
}

void EsTextboxEnableSmartQuotes(EsTextbox *textbox, bool enabled) {
	textbox->smartQuotes = enabled;
}

#undef GET_BUFFER
