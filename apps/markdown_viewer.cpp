// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#define ES_INSTANCE_TYPE Instance
#include <essence.h>
#include <shared/strings.cpp>
#include <shared/array.cpp>
#include <ports/md4c/md4c.c>

// TODO Inline code background?

// TODO When resizing the window, maintain the scroll position.
// TODO Table of contents/navigation pane.
// TODO Searching.

// TODO Images.
// TODO Proper link support.

// TODO Embedding markdown viewers into other applications.

// #define DEBUG_PARSER_OUTPUT

struct Span {
	bool em, strong, monospaced, link;
	uintptr_t offset;
};

struct Instance : EsInstance {
	EsElement *root;

	EsElement *active;
	Array<Span> spans;
	Array<char> text;
	int32_t paddingBefore;
	bool inBlockQuote;
	int inListDepth;
	int tableColumnCount;

	int debugNestDepth;
};

#define MAKE_TEXT_STYLE(_textColor, _textSize, _fontFamily, _fontWeight, _isItalic) \
	{ \
		.metrics = { \
			.mask = ES_THEME_METRICS_TEXT_COLOR | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_SIZE \
				| ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT  \
				| ES_THEME_METRICS_IS_ITALIC, \
			.textColor = _textColor, \
			.textAlign = ES_TEXT_H_LEFT | ES_TEXT_V_TOP | ES_TEXT_WRAP, \
			.textSize = (int) (_textSize * 0.64 + 0.5), \
			.fontFamily = _fontFamily, \
			.fontWeight = _fontWeight, \
			.isItalic = _isItalic, \
		}, \
	}

#define PARAGRAPH_PADDING_BEFORE (0)
#define PARAGRAPH_PADDING_AFTER  (16)
#define H1_PADDING_BEFORE        (16)
#define H1_PADDING_AFTER         (16)
#define H2_PADDING_BEFORE        (24)
#define H2_PADDING_AFTER         (16)
#define H3_PADDING_BEFORE        (24)
#define H3_PADDING_AFTER         (16)
#define HEADING_UNDERLINE_GAP    (7)
#define HR_PADDING_BEFORE        (24)
#define HR_PADDING_AFTER         (24)
#define QUOTE_PADDING_BEFORE     (16)
#define QUOTE_PADDING_AFTER      (0)
#define TABLE_PADDING_BEFORE     (16)
#define TABLE_PADDING_AFTER      (16)

#define COLOR_BACKGROUND (0xFFFDFDFD)
#define COLOR_GRAY1      (0xFFE1E4E8)
#define COLOR_GRAY2      (0xFFEBEDEF)
#define COLOR_GRAY3      (0xFFF6F8FA)
#define COLOR_TEXT1      (0xFF24292E)
#define COLOR_TEXT2      (0xFF5A636D)
#define COLOR_TEXT_LINK  (0xFF0366D6)

EsStyle styleBackground = {
	.appearance = {
		.enabled = true,
		.backgroundColor = COLOR_BACKGROUND,
	},
};

EsStyle styleRoot = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_PREFERRED_WIDTH,
		.insets = ES_RECT_4(32, 32, 16, 32),
		.preferredWidth = 800,
	},
};

EsStyle styleQuote = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_4(20, 16, 0, 0),
	},

	.appearance = {
		.enabled = true,
		.borderColor = COLOR_GRAY1,
		.borderSize = ES_RECT_4(4, 0, 0, 0),
	},
};

EsStyle styleList = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MAJOR,
		.gapMajor = 5,
	},
};

EsStyle styleHeadingUnderline = {
	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_HEIGHT,
		.preferredHeight = 1,
	},

	.appearance = {
		.enabled = true,
		.backgroundColor = COLOR_GRAY2,
	},
};

EsStyle styleCodeBlock = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_CLIP_ENABLED,
		.insets = ES_RECT_4(16, 16, 10, 10),
		.clipEnabled = true,
	},

	.appearance = {
		.enabled = true,
		.backgroundColor = COLOR_GRAY3,
	},
};

EsStyle styleHorizontalRule = {
	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_HEIGHT,
		.preferredHeight = 4,
	},

	.appearance = {
		.enabled = true,
		.backgroundColor = COLOR_GRAY1,
	},
};

EsStyle styleTable = {
};

EsStyle styleTD = {
	.inherit = ES_STYLE_TEXT_PARAGRAPH,

	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_COLOR,
		.insets = ES_RECT_4(8, 8, 8, 0),
		.textColor = COLOR_TEXT1,
		.textSize = (int) (16 * 0.64 + 0.5),
	},
};

EsStyle styleTH = {
	.inherit = ES_STYLE_TEXT_PARAGRAPH,

	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_COLOR | ES_THEME_METRICS_FONT_WEIGHT,
		.insets = ES_RECT_4(8, 8, 0, 4),
		.textColor = COLOR_TEXT1,
		.textSize = (int) (16 * 0.64 + 0.5),
		.fontWeight = 5,
	},

	.appearance = {
		.enabled = true,
		.borderColor = COLOR_GRAY1,
		.borderSize = ES_RECT_4(0, 0, 0, 1),
	},
};

EsStyle styleHeading1       = MAKE_TEXT_STYLE(COLOR_TEXT1, 32, ES_FONT_SANS,       6, false);
EsStyle styleHeading2       = MAKE_TEXT_STYLE(COLOR_TEXT1, 24, ES_FONT_SANS,       6, false);
EsStyle styleHeading3       = MAKE_TEXT_STYLE(COLOR_TEXT1, 20, ES_FONT_SANS,       6, false);
EsStyle styleParagraph      = MAKE_TEXT_STYLE(COLOR_TEXT1, 16, ES_FONT_SANS,       4, false);
EsStyle styleQuoteParagraph = MAKE_TEXT_STYLE(COLOR_TEXT2, 16, ES_FONT_SANS,       4, false);
EsStyle styleCode           = MAKE_TEXT_STYLE(COLOR_TEXT1, 16, ES_FONT_MONOSPACED, 4, false);

const char *blockTypes[] = {
	"MD_BLOCK_DOC",
	"MD_BLOCK_QUOTE",
	"MD_BLOCK_UL",
	"MD_BLOCK_OL",
	"MD_BLOCK_LI",
	"MD_BLOCK_HR",
	"MD_BLOCK_H",
	"MD_BLOCK_CODE",
	"MD_BLOCK_HTML",
	"MD_BLOCK_P",
	"MD_BLOCK_TABLE",
	"MD_BLOCK_THEAD",
	"MD_BLOCK_TBODY",
	"MD_BLOCK_TR",
	"MD_BLOCK_TH",
	"MD_BLOCK_TD",
};

const char *spanTypes[] = {
	"MD_SPAN_EM",
	"MD_SPAN_STRONG",
	"MD_SPAN_A",
	"MD_SPAN_IMG",
	"MD_SPAN_CODE",
	"MD_SPAN_DEL",
	"MD_SPAN_LATEXMATH",
	"MD_SPAN_LATEXMATH_DISPLAY",
	"MD_SPAN_WIKILINK",
	"MD_SPAN_U",
};

const char *textTypes[] = {
	"MD_TEXT_NORMAL",
	"MD_TEXT_NULLCHAR",
	"MD_TEXT_BR",
	"MD_TEXT_SOFTBR",
	"MD_TEXT_ENTITY",
	"MD_TEXT_CODE",
	"MD_TEXT_HTML",
	"MD_TEXT_LATEXMATH",
};

void AddPadding(Instance *instance, int32_t before, int32_t after) {
	if (instance->inListDepth) return;
	if (before < instance->paddingBefore) before = instance->paddingBefore;
	EsSpacerCreate(instance->active, ES_FLAGS_DEFAULT, nullptr, 0, before);
	instance->paddingBefore = after;
}

void CreateStyledTextDisplay(Instance *instance, EsStyle *style, uint64_t flags = ES_CELL_H_FILL) {
	EsTextDisplay *display = EsTextDisplayCreate(instance->active, flags, style);
	EsTextRun *runs = (EsTextRun *) EsHeapAllocate(sizeof(EsTextRun) * (instance->spans.Length() + 1), false);

	for (uintptr_t i = 0; i < instance->spans.Length(); i++) {
		runs[i].offset = instance->spans[i].offset;
		EsElementGetTextStyle(display, &runs[i].style);
		if (instance->spans[i].link) { runs[i].style.decorations |= ES_TEXT_DECORATION_UNDERLINE; runs[i].style.color = COLOR_TEXT_LINK;  }
		if (instance->spans[i].em) runs[i].style.font.flags |= ES_FONT_ITALIC;
		if (instance->spans[i].strong) runs[i].style.font.weight = 7;
		if (instance->spans[i].monospaced) runs[i].style.font.family = ES_FONT_MONOSPACED;
		runs[i].style.decorationsColor = runs[i].style.color;
	}

	runs[instance->spans.Length()].offset = instance->text.Length();
	EsTextDisplaySetStyledContents(display, instance->text.array, runs, instance->spans.Length());
	EsHeapFree(runs);
}

#ifdef DEBUG_PARSER_OUTPUT
void ParserOutputPrintIndentation(Instance *instance) {
	for (int i = 0; i < instance->debugNestDepth; i++) {
		EsPrint("    ");
	}
}
#endif

int ParserEnterBlock(MD_BLOCKTYPE type, void *detail, void *_instance) {
	Instance *instance = (Instance *) _instance;
#ifdef DEBUG_PARSER_OUTPUT
	ParserOutputPrintIndentation(instance);
	EsPrint(">> Enter block %z\n", blockTypes[type]);
	instance->debugNestDepth++;
#endif
	(void) detail;

	if (instance->inListDepth && instance->text.Length()) {
		CreateStyledTextDisplay(instance, &styleParagraph);
	}

	instance->text.SetLength(0);
	instance->spans.SetLength(0);

	Span span = {};
	instance->spans.Add(span);

	if (type == MD_BLOCK_UL) {
		AddPadding(instance, PARAGRAPH_PADDING_BEFORE, PARAGRAPH_PADDING_AFTER);
		instance->active = EsListDisplayCreate(instance->active, ES_CELL_H_FILL | ES_LIST_DISPLAY_BULLETED, ES_STYLE_LIST_DISPLAY_DEFAULT);
	} else if (type == MD_BLOCK_OL) {
		AddPadding(instance, PARAGRAPH_PADDING_BEFORE, PARAGRAPH_PADDING_AFTER);
		EsListDisplay *display = EsListDisplayCreate(instance->active, ES_CELL_H_FILL | ES_LIST_DISPLAY_NUMBERED, ES_STYLE_LIST_DISPLAY_DEFAULT);
		instance->active = display;
		EsListDisplaySetCounterStart(display, ((MD_BLOCK_OL_DETAIL *) detail)->start - 1);
	} else if (type == MD_BLOCK_QUOTE) {
		AddPadding(instance, QUOTE_PADDING_BEFORE, QUOTE_PADDING_AFTER);
		instance->active = EsPanelCreate(instance->active, ES_CELL_H_FILL, &styleQuote);
		instance->inBlockQuote = true;
	} else if (type == MD_BLOCK_LI) {
		instance->active = EsPanelCreate(instance->active, ES_CELL_H_FILL, &styleList);
		instance->inListDepth++;
	} else if (type == MD_BLOCK_TABLE) {
		AddPadding(instance, TABLE_PADDING_BEFORE, TABLE_PADDING_AFTER);
		EsPanel *table = EsPanelCreate(instance->active, ES_PANEL_TABLE | ES_PANEL_HORIZONTAL | ES_CELL_H_SHRINK, &styleTable);
		instance->active = table;
		instance->tableColumnCount = 0;
	}

	return 0;
}

int ParserLeaveBlock(MD_BLOCKTYPE type, void *detail, void *_instance) {
	Instance *instance = (Instance *) _instance;
#ifdef DEBUG_PARSER_OUTPUT
	instance->debugNestDepth--;
	ParserOutputPrintIndentation(instance);
	EsPrint(">> Leave block %z\n", blockTypes[type]);
#endif

	if (type == MD_BLOCK_P) {
		if (instance->text.Length()) {
			if (type == MD_BLOCK_P) {
				AddPadding(instance, PARAGRAPH_PADDING_BEFORE, PARAGRAPH_PADDING_AFTER);
			}

			CreateStyledTextDisplay(instance, instance->inBlockQuote ? &styleQuoteParagraph : &styleParagraph);
		}
	} else if (type == MD_BLOCK_LI) {
		if (instance->text.Length()) CreateStyledTextDisplay(instance, &styleParagraph);
		instance->text.SetLength(0);
		instance->spans.SetLength(0);
		instance->active = EsElementGetLayoutParent(instance->active);
		instance->inListDepth--;
	} else if (type == MD_BLOCK_TD || type == MD_BLOCK_TH) {
		if (type == MD_BLOCK_TH) instance->tableColumnCount++;
		CreateStyledTextDisplay(instance, type == MD_BLOCK_TH ? &styleTH : &styleTD, ES_CELL_H_EXPAND | ES_CELL_V_EXPAND | ES_CELL_H_SHRINK | ES_CELL_V_SHRINK);
		instance->text.SetLength(0);
		instance->spans.SetLength(0);
	} else if (type == MD_BLOCK_H) {
		unsigned level = ((MD_BLOCK_H_DETAIL *) detail)->level;

		if      (level == 1) AddPadding(instance, H1_PADDING_BEFORE, H1_PADDING_AFTER);
		else if (level == 2) AddPadding(instance, H2_PADDING_BEFORE, H2_PADDING_AFTER);
		else                 AddPadding(instance, H3_PADDING_BEFORE, H3_PADDING_AFTER);

		CreateStyledTextDisplay(instance, level == 1 ? &styleHeading1 : level == 2 ? &styleHeading2 : &styleHeading3);

		if (level <= 2) {
			EsSpacerCreate(instance->active, ES_FLAGS_DEFAULT, nullptr, 0, HEADING_UNDERLINE_GAP);
			EsSpacerCreate(instance->active, ES_CELL_H_FILL, &styleHeadingUnderline, 0, 0);
		}
	} else if (type == MD_BLOCK_CODE) {
		MD_BLOCK_CODE_DETAIL *code = (MD_BLOCK_CODE_DETAIL *) detail;
		AddPadding(instance, PARAGRAPH_PADDING_BEFORE, PARAGRAPH_PADDING_AFTER);
		EsElement *wrapper = EsPanelCreate(instance->active, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL | ES_PANEL_H_SCROLL_AUTO, &styleCodeBlock);
		EsTextDisplay *display = EsTextDisplayCreate(wrapper, ES_TEXT_DISPLAY_PREFORMATTED, &styleCode, instance->text.array, instance->text.Length());

		if (0 == EsStringCompare(code->lang.text, code->lang.size, EsLiteral("ini"))) {
			EsTextDisplaySetupSyntaxHighlighting(display, ES_SYNTAX_HIGHLIGHTING_LANGUAGE_INI);
		} else if (0 == EsStringCompare(code->lang.text, code->lang.size, EsLiteral("c"))
				|| 0 == EsStringCompare(code->lang.text, code->lang.size, EsLiteral("cpp"))
				|| 0 == EsStringCompare(code->lang.text, code->lang.size, EsLiteral("c++"))) {
			EsTextDisplaySetupSyntaxHighlighting(display, ES_SYNTAX_HIGHLIGHTING_LANGUAGE_C);
		}
	} else if (type == MD_BLOCK_UL || type == MD_BLOCK_OL || type == MD_BLOCK_QUOTE) {
		instance->active = EsElementGetLayoutParent(instance->active);
		instance->inBlockQuote = false;
	} else if (type == MD_BLOCK_HR) {
		AddPadding(instance, HR_PADDING_BEFORE, HR_PADDING_AFTER);
		EsSpacerCreate(instance->active, ES_CELL_H_FILL, &styleHorizontalRule, 0, 0);
	} else if (type == MD_BLOCK_TABLE) {
		EsPanel *panel = (EsPanel *) instance->active;

		EsPanelSetBands(panel, instance->tableColumnCount);
		EsPanelTableSetChildCells(panel);

		EsPanelBand column = {};
		column.preferredSize = column.minimumSize = column.maximumSize = ES_PANEL_BAND_SIZE_DEFAULT;
		column.pull = 1; // Shrink all columns with equal weight.
		EsPanelSetBandsAll(panel, &column);

		instance->active = EsElementGetLayoutParent(instance->active);
	} else {
		// EsPrint("Unhandled block of type %z.\n", blockTypes[type]);
	}

	return 0;
}

int ParserEnterSpan(MD_SPANTYPE type, void *detail, void *_instance) {
	Instance *instance = (Instance *) _instance;
#ifdef DEBUG_PARSER_OUTPUT
	ParserOutputPrintIndentation(instance);
	EsPrint(">> Enter span %z\n", spanTypes[type]);
	instance->debugNestDepth++;
#endif
	(void) detail;

	if (type == MD_SPAN_EM || type == MD_SPAN_STRONG || type == MD_SPAN_CODE || type == MD_SPAN_A) {
		Span span = instance->spans.Last();
		if (type == MD_SPAN_EM) span.em = true;
		if (type == MD_SPAN_STRONG) span.strong = true;
		if (type == MD_SPAN_CODE) span.monospaced = true;
		if (type == MD_SPAN_A) span.link = true;
		span.offset = instance->text.Length();
		instance->spans.Add(span);
	}

	return 0;
}

int ParserLeaveSpan(MD_SPANTYPE type, void *detail, void *_instance) {
	Instance *instance = (Instance *) _instance;
#ifdef DEBUG_PARSER_OUTPUT
	instance->debugNestDepth--;
	ParserOutputPrintIndentation(instance);
	EsPrint(">> Leave span %z\n", spanTypes[type]);
#endif
	(void) detail;

	if (type == MD_SPAN_EM || type == MD_SPAN_STRONG || type == MD_SPAN_CODE || type == MD_SPAN_A) {
		Span span = instance->spans.Last();
		if (type == MD_SPAN_EM) span.em = false;
		if (type == MD_SPAN_STRONG) span.strong = false;
		if (type == MD_SPAN_CODE) span.monospaced = false;
		if (type == MD_SPAN_A) span.link = false;
		span.offset = instance->text.Length();
		instance->spans.Add(span);
	}

	return 0;
}

int ParserText(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *_instance) {
	(void) type;
	Instance *instance = (Instance *) _instance;
#ifdef DEBUG_PARSER_OUTPUT
	ParserOutputPrintIndentation(instance);
	EsPrint(">> Text %z, %x: %s\n", textTypes[type], text, size, text);
#endif
	char *buffer = instance->text.InsertMany(instance->text.Length(), size);
	EsMemoryCopy(buffer, text, size);
	return 0;
}

int InstanceCallback(Instance *instance, EsMessage *message) {
	if (message->type == ES_MSG_INSTANCE_OPEN) {
		if (message->instanceOpen.update) {
			EsElementStartTransition(instance->root, ES_TRANSITION_ZOOM_IN);
		}

		EsElementDestroyContents(instance->root);

		size_t fileSize;
		char *file = (char *) EsFileStoreReadAll(message->instanceOpen.file, &fileSize);

		if (!file || !EsUTF8IsValid(file, fileSize)) {
			EsInstanceOpenComplete(instance, message->instanceOpen.file, false);
			return ES_HANDLED;
		} 

		MD_PARSER parser = {};
		parser.flags = MD_DIALECT_GITHUB | MD_FLAG_NOHTML;
		parser.enter_block = ParserEnterBlock;
		parser.leave_block = ParserLeaveBlock;
		parser.enter_span = ParserEnterSpan;
		parser.leave_span = ParserLeaveSpan;
		parser.text = ParserText;
		instance->active = instance->root;
		int result = md_parse(file, fileSize, &parser, instance);
		if (result) EsElementDestroyContents(instance->root); // An error occurred.
		EsInstanceOpenComplete(instance, message->instanceOpen.file, result == 0);
		EsHeapFree(file);

		EsElementRelayout(instance->root);
		instance->spans.Free();
		instance->text.Free();
	} else {
		return 0;
	}

	return ES_HANDLED;
}

void ProcessApplicationMessage(EsMessage *message) {
	if (message->type == ES_MSG_INSTANCE_CREATE) {
		Instance *instance = EsInstanceCreate(message, INTERFACE_STRING(MarkdownViewerTitle));
		instance->callback = InstanceCallback;
		EsInstanceSetClassViewer(instance, nullptr);
		EsWindow *window = instance->window;
		EsPanel *wrapper = EsPanelCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);
		EsPanel *background = EsPanelCreate(wrapper, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleBackground);
		instance->root = EsPanelCreate(background, ES_CELL_H_SHRINK, &styleRoot);
		EsWindowSetIcon(window, ES_ICON_TEXT_MARKDOWN);
	}
}

void _start() {
	_init();

	while (true) {
		ProcessApplicationMessage(EsMessageReceive());
	}
}
