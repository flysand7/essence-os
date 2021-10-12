#include <X11/Xlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// It looks like ES_SCANCODE_PUNCTUATION_1 and ES_SCANCODE_PUNCTUATION_2 are mutually exclusive 
// (_1 only on US keyboards, and _2 on everything else),
// and X11 merges them into key code 0x33.

#define ES_SCANCODE_A				(0x04)
#define ES_SCANCODE_B				(0x05)
#define ES_SCANCODE_C				(0x06)
#define ES_SCANCODE_D				(0x07)
#define ES_SCANCODE_E				(0x08)
#define ES_SCANCODE_F				(0x09)
#define ES_SCANCODE_G				(0x0A)
#define ES_SCANCODE_H				(0x0B)
#define ES_SCANCODE_I				(0x0C)
#define ES_SCANCODE_J				(0x0D)
#define ES_SCANCODE_K				(0x0E)
#define ES_SCANCODE_L				(0x0F)
#define ES_SCANCODE_M				(0x10)
#define ES_SCANCODE_N				(0x11)
#define ES_SCANCODE_O				(0x12)
#define ES_SCANCODE_P				(0x13)
#define ES_SCANCODE_Q				(0x14)
#define ES_SCANCODE_R				(0x15)
#define ES_SCANCODE_S				(0x16)
#define ES_SCANCODE_T				(0x17)
#define ES_SCANCODE_U				(0x18)
#define ES_SCANCODE_V				(0x19)
#define ES_SCANCODE_W				(0x1A)
#define ES_SCANCODE_X				(0x1B)
#define ES_SCANCODE_Y				(0x1C)
#define ES_SCANCODE_Z				(0x1D)

#define ES_SCANCODE_1				(0x1E)
#define ES_SCANCODE_2				(0x1F)
#define ES_SCANCODE_3				(0x20)
#define ES_SCANCODE_4				(0x21)
#define ES_SCANCODE_5				(0x22)
#define ES_SCANCODE_6				(0x23)
#define ES_SCANCODE_7				(0x24)
#define ES_SCANCODE_8				(0x25)
#define ES_SCANCODE_9				(0x26)
#define ES_SCANCODE_0				(0x27)

#define ES_SCANCODE_ENTER 			(0x28)
#define ES_SCANCODE_ESCAPE			(0x29)
#define ES_SCANCODE_BACKSPACE			(0x2A)
#define ES_SCANCODE_TAB				(0x2B)
#define ES_SCANCODE_SPACE			(0x2C)

#define ES_SCANCODE_HYPHEN			(0x2D)
#define ES_SCANCODE_EQUALS			(0x2E)
#define ES_SCANCODE_LEFT_BRACE			(0x2F)
#define ES_SCANCODE_RIGHT_BRACE			(0x30)
#define ES_SCANCODE_COMMA			(0x36)
#define ES_SCANCODE_PERIOD			(0x37)
#define ES_SCANCODE_SLASH			(0x38)
#define ES_SCANCODE_PUNCTUATION_1		(0x31) // On US keyboard, \|
#define ES_SCANCODE_PUNCTUATION_2		(0x32) // Not on US keyboard
#define ES_SCANCODE_PUNCTUATION_3		(0x33) // On US keyboard, ;:
#define ES_SCANCODE_PUNCTUATION_4		(0x34) // On US keyboard, '"
#define ES_SCANCODE_PUNCTUATION_5		(0x35) // On US keyboard, `~
#define ES_SCANCODE_PUNCTUATION_6		(0x64) // Not on US keyboard

#define ES_SCANCODE_F1				(0x3A)
#define ES_SCANCODE_F2				(0x3B)
#define ES_SCANCODE_F3				(0x3C)
#define ES_SCANCODE_F4				(0x3D)
#define ES_SCANCODE_F5				(0x3E)
#define ES_SCANCODE_F6				(0x3F)
#define ES_SCANCODE_F7				(0x40)
#define ES_SCANCODE_F8				(0x41)
#define ES_SCANCODE_F9				(0x42)
#define ES_SCANCODE_F10				(0x43)
#define ES_SCANCODE_F11				(0x44)
#define ES_SCANCODE_F12				(0x45)
#define ES_SCANCODE_F13				(0x68)
#define ES_SCANCODE_F14				(0x69)
#define ES_SCANCODE_F15				(0x6A)
#define ES_SCANCODE_F16				(0x6B)
#define ES_SCANCODE_F17				(0x6C)
#define ES_SCANCODE_F18				(0x6D)
#define ES_SCANCODE_F19				(0x6E)
#define ES_SCANCODE_F20				(0x6F)
#define ES_SCANCODE_F21				(0x70)
#define ES_SCANCODE_F22				(0x71)
#define ES_SCANCODE_F23				(0x72)
#define ES_SCANCODE_F24				(0x73)

#define ES_SCANCODE_CAPS_LOCK			(0x39)
#define ES_SCANCODE_PRINT_SCREEN			(0x46)
#define ES_SCANCODE_SCROLL_LOCK			(0x47)
#define ES_SCANCODE_PAUSE			(0x48)
#define ES_SCANCODE_INSERT			(0x49)
#define ES_SCANCODE_HOME				(0x4A)
#define ES_SCANCODE_PAGE_UP			(0x4B)
#define ES_SCANCODE_DELETE			(0x4C)
#define ES_SCANCODE_END				(0x4D)
#define ES_SCANCODE_PAGE_DOWN			(0x4E)
#define ES_SCANCODE_RIGHT_ARROW			(0x4F)
#define ES_SCANCODE_LEFT_ARROW			(0x50)
#define ES_SCANCODE_DOWN_ARROW			(0x51)
#define ES_SCANCODE_UP_ARROW			(0x52)
#define ES_SCANCODE_NUM_LOCK			(0x53)
#define ES_SCANCODE_CONTEXT_MENU 		(0x65)
#define ES_SCANCODE_SYSTEM_REQUEST		(0x9A)

#define ES_SCANCODE_ACTION_EXECUTE		(0x74)
#define ES_SCANCODE_ACTION_HELP			(0x75)
#define ES_SCANCODE_ACTION_MENU			(0x76)
#define ES_SCANCODE_ACTION_SELECT		(0x77)
#define ES_SCANCODE_ACTION_STOP			(0x78)
#define ES_SCANCODE_ACTION_AGAIN			(0x79)
#define ES_SCANCODE_ACTION_UNDO			(0x7A)
#define ES_SCANCODE_ACTION_CUT			(0x7B)
#define ES_SCANCODE_ACTION_COPY			(0x7C)
#define ES_SCANCODE_ACTION_PASTE			(0x7D)
#define ES_SCANCODE_ACTION_FIND			(0x7E)
#define ES_SCANCODE_ACTION_CANCEL		(0x9B)
#define ES_SCANCODE_ACTION_CLEAR			(0x9C)
#define ES_SCANCODE_ACTION_PRIOR			(0x9D)
#define ES_SCANCODE_ACTION_RETURN		(0x9E)
#define ES_SCANCODE_ACTION_SEPARATOR		(0x9F)

#define ES_SCANCODE_MM_MUTE			(0x7F)
#define ES_SCANCODE_MM_LOUDER			(0x80)
#define ES_SCANCODE_MM_QUIETER			(0x81)
#define ES_SCANCODE_MM_NEXT			(0x103)
#define ES_SCANCODE_MM_PREVIOUS			(0x104)
#define ES_SCANCODE_MM_STOP			(0x105)
#define ES_SCANCODE_MM_PAUSE			(0x106)
#define ES_SCANCODE_MM_SELECT			(0x107)
#define ES_SCANCODE_MM_EMAIL			(0x108)
#define ES_SCANCODE_MM_CALC			(0x109)
#define ES_SCANCODE_MM_FILES			(0x10A)

#define ES_SCANCODE_INTERNATIONAL_1		(0x87)
#define ES_SCANCODE_INTERNATIONAL_2		(0x88)
#define ES_SCANCODE_INTERNATIONAL_3		(0x89)
#define ES_SCANCODE_INTERNATIONAL_4		(0x8A)
#define ES_SCANCODE_INTERNATIONAL_5		(0x8B)
#define ES_SCANCODE_INTERNATIONAL_6		(0x8C)
#define ES_SCANCODE_INTERNATIONAL_7		(0x8D)
#define ES_SCANCODE_INTERNATIONAL_8		(0x8E)
#define ES_SCANCODE_INTERNATIONAL_9		(0x8F)

#define ES_SCANCODE_HANGUL_ENGLISH_TOGGLE	(0x90)
#define ES_SCANCODE_HANJA_CONVERSION		(0x91)
#define ES_SCANCODE_KATAKANA			(0x92)
#define ES_SCANCODE_HIRAGANA			(0x93)
#define ES_SCANCODE_HANKAKU_ZENKAKU_TOGGLE	(0x94)
#define ES_SCANCODE_ALTERNATE_ERASE		(0x99)

#define ES_SCANCODE_THOUSANDS_SEPARATOR		(0xB2)
#define ES_SCANCODE_DECIMAL_SEPARATOR		(0xB3)
#define ES_SCANCODE_CURRENCY_UNIT		(0xB4)
#define ES_SCANCODE_CURRENCY_SUBUNIT		(0xB5)

#define ES_SCANCODE_NUM_DIVIDE			(0x54)
#define ES_SCANCODE_NUM_MULTIPLY			(0x55)
#define ES_SCANCODE_NUM_SUBTRACT			(0x56)
#define ES_SCANCODE_NUM_ADD			(0x57)
#define ES_SCANCODE_NUM_ENTER			(0x58)
#define ES_SCANCODE_NUM_1			(0x59)
#define ES_SCANCODE_NUM_2			(0x5A)
#define ES_SCANCODE_NUM_3			(0x5B)
#define ES_SCANCODE_NUM_4			(0x5C)
#define ES_SCANCODE_NUM_5			(0x5D)
#define ES_SCANCODE_NUM_6			(0x5E)
#define ES_SCANCODE_NUM_7			(0x5F)
#define ES_SCANCODE_NUM_8			(0x60)
#define ES_SCANCODE_NUM_9			(0x61)
#define ES_SCANCODE_NUM_0			(0x62)
#define ES_SCANCODE_NUM_POINT			(0x63)
#define ES_SCANCODE_NUM_EQUALS			(0x67)
#define ES_SCANCODE_NUM_COMMA			(0x82)
#define ES_SCANCODE_NUM_00			(0xB0)
#define ES_SCANCODE_NUM_000			(0xB1)
#define ES_SCANCODE_NUM_LEFT_PAREN		(0xB6)
#define ES_SCANCODE_NUM_RIGHT_PAREN		(0xB7)
#define ES_SCANCODE_NUM_LEFT_BRACE		(0xB8)
#define ES_SCANCODE_NUM_RIGHT_BRACE		(0xB9)
#define ES_SCANCODE_NUM_TAB			(0xBA)
#define ES_SCANCODE_NUM_BACKSPACE		(0xBB)
#define ES_SCANCODE_NUM_A			(0xBC)
#define ES_SCANCODE_NUM_B			(0xBD)
#define ES_SCANCODE_NUM_C			(0xBE)
#define ES_SCANCODE_NUM_D			(0xBF)
#define ES_SCANCODE_NUM_E			(0xC0)
#define ES_SCANCODE_NUM_F			(0xC1)
#define ES_SCANCODE_NUM_XOR			(0xC2)
#define ES_SCANCODE_NUM_CARET			(0xC3)
#define ES_SCANCODE_NUM_PERCENT			(0xC4)
#define ES_SCANCODE_NUM_LESS_THAN		(0xC5)
#define ES_SCANCODE_NUM_GREATER_THAN		(0xC6)
#define ES_SCANCODE_NUM_AMPERSAND		(0xC7)
#define ES_SCANCODE_NUM_DOUBLE_AMPERSAND		(0xC8)
#define ES_SCANCODE_NUM_BAR			(0xC9)
#define ES_SCANCODE_NUM_DOUBLE_BAR		(0xCA)
#define ES_SCANCODE_NUM_COLON			(0xCB)
#define ES_SCANCODE_NUM_HASH			(0xCC)
#define ES_SCANCODE_NUM_SPACE			(0xCD)
#define ES_SCANCODE_NUM_AT			(0xCE)
#define ES_SCANCODE_NUM_EXCLAMATION_MARK		(0xCF)
#define ES_SCANCODE_NUM_MEMORY_STORE		(0xD0)
#define ES_SCANCODE_NUM_MEMORY_RECALL		(0xD1)
#define ES_SCANCODE_NUM_MEMORY_CLEAR		(0xD2)
#define ES_SCANCODE_NUM_MEMORY_ADD		(0xD3)
#define ES_SCANCODE_NUM_MEMORY_SUBTRACT		(0xD4)
#define ES_SCANCODE_NUM_MEMORY_MULTIPLY		(0xD5)
#define ES_SCANCODE_NUM_MEMORY_DIVIDE		(0xD6)
#define ES_SCANCODE_NUM_NEGATE			(0xD7)
#define ES_SCANCODE_NUM_CLEAR_ALL		(0xD8)
#define ES_SCANCODE_NUM_CLEAR			(0xD9)
#define ES_SCANCODE_NUM_BINARY			(0xDA)
#define ES_SCANCODE_NUM_OCTAL			(0xDB)
#define ES_SCANCODE_NUM_DECIMAL			(0xDC)
#define ES_SCANCODE_NUM_HEXADECIMAL		(0xDD)

#define ES_SCANCODE_LEFT_CTRL			(0xE0)
#define ES_SCANCODE_LEFT_SHIFT			(0xE1)
#define ES_SCANCODE_LEFT_ALT			(0xE2)
#define ES_SCANCODE_LEFT_FLAG			(0xE3)
#define ES_SCANCODE_RIGHT_CTRL			(0xE4)
#define ES_SCANCODE_RIGHT_SHIFT			(0xE5)
#define ES_SCANCODE_RIGHT_ALT			(0xE6)
#define ES_SCANCODE_RIGHT_FLAG			(0xE7)

#define ES_SCANCODE_ACPI_POWER 			(0x100)
#define ES_SCANCODE_ACPI_SLEEP 			(0x101)
#define ES_SCANCODE_ACPI_WAKE  			(0x102)

#define ES_SCANCODE_WWW_SEARCH			(0x10B)
#define ES_SCANCODE_WWW_HOME			(0x10C)
#define ES_SCANCODE_WWW_BACK			(0x10D)
#define ES_SCANCODE_WWW_FORWARD			(0x10E)
#define ES_SCANCODE_WWW_STOP			(0x10F)
#define ES_SCANCODE_WWW_REFRESH			(0x110)
#define ES_SCANCODE_WWW_STARRED			(0x111)

uint32_t remap[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, ES_SCANCODE_ESCAPE, ES_SCANCODE_1, ES_SCANCODE_2, ES_SCANCODE_3, ES_SCANCODE_4, ES_SCANCODE_5, ES_SCANCODE_6,
	ES_SCANCODE_7, ES_SCANCODE_8, ES_SCANCODE_9, ES_SCANCODE_0, ES_SCANCODE_HYPHEN, ES_SCANCODE_EQUALS, ES_SCANCODE_BACKSPACE, ES_SCANCODE_TAB,
	ES_SCANCODE_Q, ES_SCANCODE_W, ES_SCANCODE_E, ES_SCANCODE_R, ES_SCANCODE_T, ES_SCANCODE_Y, ES_SCANCODE_U, ES_SCANCODE_I,
	ES_SCANCODE_O, ES_SCANCODE_P, ES_SCANCODE_LEFT_BRACE, ES_SCANCODE_RIGHT_BRACE, ES_SCANCODE_ENTER, ES_SCANCODE_LEFT_CTRL, ES_SCANCODE_A, ES_SCANCODE_S,
	ES_SCANCODE_D, ES_SCANCODE_F, ES_SCANCODE_G, ES_SCANCODE_H, ES_SCANCODE_J, ES_SCANCODE_K, ES_SCANCODE_L, ES_SCANCODE_PUNCTUATION_3, 
	ES_SCANCODE_PUNCTUATION_4, ES_SCANCODE_PUNCTUATION_5, ES_SCANCODE_LEFT_SHIFT, ES_SCANCODE_PUNCTUATION_1, ES_SCANCODE_Z, ES_SCANCODE_X, ES_SCANCODE_C, ES_SCANCODE_V,
	ES_SCANCODE_B, ES_SCANCODE_N, ES_SCANCODE_M, ES_SCANCODE_COMMA, ES_SCANCODE_PERIOD, ES_SCANCODE_SLASH, ES_SCANCODE_RIGHT_SHIFT, ES_SCANCODE_NUM_MULTIPLY,
	ES_SCANCODE_LEFT_ALT, ES_SCANCODE_SPACE, ES_SCANCODE_CAPS_LOCK, ES_SCANCODE_F1, ES_SCANCODE_F2, ES_SCANCODE_F3, ES_SCANCODE_F4, ES_SCANCODE_F5,
	ES_SCANCODE_F6, ES_SCANCODE_F7, ES_SCANCODE_F8, ES_SCANCODE_F9, ES_SCANCODE_F10, ES_SCANCODE_NUM_LOCK, ES_SCANCODE_SCROLL_LOCK, ES_SCANCODE_NUM_7,
	ES_SCANCODE_NUM_8, ES_SCANCODE_NUM_9, ES_SCANCODE_NUM_SUBTRACT, ES_SCANCODE_NUM_4, ES_SCANCODE_NUM_5, ES_SCANCODE_NUM_6, ES_SCANCODE_NUM_ADD, ES_SCANCODE_NUM_1, 
	ES_SCANCODE_NUM_2, ES_SCANCODE_NUM_3, ES_SCANCODE_NUM_0, ES_SCANCODE_NUM_POINT, 0, 0, ES_SCANCODE_PUNCTUATION_6, ES_SCANCODE_F11,
	ES_SCANCODE_F12, 0, ES_SCANCODE_KATAKANA, ES_SCANCODE_HIRAGANA, 0, 0, 0, 0,
	ES_SCANCODE_NUM_ENTER, ES_SCANCODE_RIGHT_CTRL, ES_SCANCODE_NUM_DIVIDE, ES_SCANCODE_PRINT_SCREEN, ES_SCANCODE_RIGHT_ALT, 0, ES_SCANCODE_HOME, ES_SCANCODE_UP_ARROW,
	ES_SCANCODE_PAGE_UP, ES_SCANCODE_LEFT_ARROW, ES_SCANCODE_RIGHT_ARROW, ES_SCANCODE_END, ES_SCANCODE_DOWN_ARROW, ES_SCANCODE_PAGE_DOWN, ES_SCANCODE_INSERT, ES_SCANCODE_DELETE,
	0, ES_SCANCODE_MM_MUTE, ES_SCANCODE_MM_QUIETER, ES_SCANCODE_MM_LOUDER, ES_SCANCODE_ACPI_POWER, ES_SCANCODE_NUM_EQUALS, 0, ES_SCANCODE_PAUSE,
};

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <layout>\n", argv[0]);
		return 1;
	}

	FILE *f = popen("setxkbmap -query | grep layout", "r");
	char oldLayout[64] = {};
	fread(oldLayout, 1, sizeof(oldLayout) - 1, f);
	pclose(f);

	char setLayout[128];
	snprintf(setLayout, sizeof(setLayout), "setxkbmap %s", argv[1]);
	system(setLayout);

	Display *display = XOpenDisplay(NULL);
	XIM xim = XOpenIM(display, 0, 0, 0);
	XSetLocaleModifiers("");
	XSetWindowAttributes attributes = {};
	Window window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, 800, 600, 0, 0, 
			InputOutput, CopyFromParent, CWOverrideRedirect, &attributes);
	XIC xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, window, XNFocusWindow, window, NULL);

	uint16_t table[512 * 4] = {};
	char stringBuffer[65536] = {};
	size_t stringBufferPosition = 0;

	for (uintptr_t state = 0; state < 4; state++) {
		for (uintptr_t i = 0; i < 0x80; i++) {
			XEvent event = {};
			event.xkey.type = KeyPress;
			event.xkey.display = display;
			event.xkey.window = window;
			event.xkey.state = state == 0 ? 0x00 : state == 1 ? 0x01 /* shift */ : state == 2 ? 0x80 /* alt gr */ : state == 3 ? 0x81 : 0;
			event.xkey.keycode = i;
			char text[32];
			KeySym symbol = NoSymbol;
			Status status;
			size_t textBytes = Xutf8LookupString(xic, &event.xkey, text, sizeof(text) - 1, &symbol, &status); 
			uint16_t offset = 0;

			if (textBytes) {
				offset = stringBufferPosition;
				assert(stringBufferPosition + textBytes + 1 < sizeof(stringBuffer));
				memcpy(stringBuffer + stringBufferPosition, text, textBytes);
				stringBuffer[stringBufferPosition + textBytes] = 0;
				stringBufferPosition += textBytes + 1;
			}

			table[remap[i] + state * 512] = offset;

			if (remap[i] == ES_SCANCODE_PUNCTUATION_1) {
				table[ES_SCANCODE_PUNCTUATION_2 + state * 512] = offset;
			}
		}
	}

	snprintf(setLayout, sizeof(setLayout), "setxkbmap %s", oldLayout + 7);
	system(setLayout);

	fwrite(table, 1, sizeof(table), stdout);
	fwrite(stringBuffer, 1, stringBufferPosition, stdout);

	return 0;
}
