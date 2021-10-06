// added by nakst.

/*
TODO
clipboard, headerbar, new graphics layer
scroll, 
timing/signals

mouse grab, 
move mouse, 
mouse buttons, 
*/

#define BX_PLUGGABLE

#include "bochs.h"
#include "plugin.h"
#include "param_names.h"
#include "iodev.h"

#if BX_WITH_ESSENCE
#include "icon_bochs.h"

// For the VGA font.
#include "sdl.h"

#define LOG_THIS theGui->

#define ES_INSTANCE_TYPE Instance
#include <essence.h>

class bx_essence_gui_c : public bx_gui_c {
public:
  bx_essence_gui_c (void) {}
  DECLARE_GUI_VIRTUAL_METHODS()
};

// declare one instance of the gui object and call macro to insert the
// plugin code
static bx_essence_gui_c *theGui = NULL;
IMPLEMENT_GUI_PLUGIN_CODE(essence)

struct Instance : EsInstance {
	EsElement *display;
	uint32_t *vmem;
	int vmemWidth, vmemHeight;
};

extern bx_startup_flags_t bx_startup_flags;
EsHandle openEvent;
Instance *instance;
char *configurationFile;
void *configurationFileData;
size_t configurationFileBytes;

#define MAX_VGA_COLORS 256
unsigned long col_vals[MAX_VGA_COLORS]; // 256 VGA colors
int text_cols, text_rows;

volatile bool repaintQueued;

void QueueRepaint() {
	EsMessageMutexAcquire();

	if (!repaintQueued) {
		repaintQueued = true;
		EsElementRepaint(instance->display);
	}

	EsMessageMutexRelease();
}

uint32_t ConvertScancode(uint32_t scancode) {
	switch (scancode) {
		case ES_SCANCODE_0:                           scancode = BX_KEY_0; break;
		case ES_SCANCODE_1:                           scancode = BX_KEY_1; break;
		case ES_SCANCODE_2:                           scancode = BX_KEY_2; break;
		case ES_SCANCODE_3:                           scancode = BX_KEY_3; break;
		case ES_SCANCODE_4:                           scancode = BX_KEY_4; break;
		case ES_SCANCODE_5:                           scancode = BX_KEY_5; break;
		case ES_SCANCODE_6:                           scancode = BX_KEY_6; break;
		case ES_SCANCODE_7:                           scancode = BX_KEY_7; break;
		case ES_SCANCODE_8:                           scancode = BX_KEY_8; break;
		case ES_SCANCODE_9:                           scancode = BX_KEY_9; break;
		case ES_SCANCODE_A:                           scancode = BX_KEY_A; break;
		case ES_SCANCODE_B:                           scancode = BX_KEY_B; break;
		case ES_SCANCODE_PUNCTUATION_1:               scancode = BX_KEY_BACKSLASH; break;
		case ES_SCANCODE_BACKSPACE:                   scancode = BX_KEY_BACKSPACE; break;
		case ES_SCANCODE_C:                           scancode = BX_KEY_C; break;
		case ES_SCANCODE_CAPS_LOCK:                   scancode = BX_KEY_CAPS_LOCK; break;
		case ES_SCANCODE_COMMA:                       scancode = BX_KEY_COMMA; break;
		case ES_SCANCODE_D:                           scancode = BX_KEY_D; break;
		case ES_SCANCODE_DELETE:                      scancode = BX_KEY_DELETE; break;
		case ES_SCANCODE_DOWN_ARROW:                  scancode = BX_KEY_DOWN; break;
		case ES_SCANCODE_E:                           scancode = BX_KEY_E; break;
		case ES_SCANCODE_END:                         scancode = BX_KEY_END; break;
		case ES_SCANCODE_ENTER:                       scancode = BX_KEY_ENTER; break;
		case ES_SCANCODE_EQUALS:                      scancode = BX_KEY_EQUALS; break;
		case ES_SCANCODE_ESCAPE:                      scancode = BX_KEY_ESC; break;
		case ES_SCANCODE_F:                           scancode = BX_KEY_F; break;
		case ES_SCANCODE_F1:                          scancode = BX_KEY_F1; break;
		case ES_SCANCODE_F10:                         scancode = BX_KEY_F10; break;
		case ES_SCANCODE_F11:                         scancode = BX_KEY_F11; break;
		case ES_SCANCODE_F12:                         scancode = BX_KEY_F12; break;
		case ES_SCANCODE_F2:                          scancode = BX_KEY_F2; break;
		case ES_SCANCODE_F3:                          scancode = BX_KEY_F3; break;
		case ES_SCANCODE_F4:                          scancode = BX_KEY_F4; break;
		case ES_SCANCODE_F5:                          scancode = BX_KEY_F5; break;
		case ES_SCANCODE_F6:                          scancode = BX_KEY_F6; break;
		case ES_SCANCODE_F7:                          scancode = BX_KEY_F7; break;
		case ES_SCANCODE_F8:                          scancode = BX_KEY_F8; break;
		case ES_SCANCODE_F9:                          scancode = BX_KEY_F9; break;
		case ES_SCANCODE_G:                           scancode = BX_KEY_G; break;
		case ES_SCANCODE_H:                           scancode = BX_KEY_H; break;
		case ES_SCANCODE_HOME:                        scancode = BX_KEY_HOME; break;
		case ES_SCANCODE_I:                           scancode = BX_KEY_I; break;
		case ES_SCANCODE_INSERT:                      scancode = BX_KEY_INSERT; break;
		case ES_SCANCODE_J:                           scancode = BX_KEY_J; break;
		case ES_SCANCODE_K:                           scancode = BX_KEY_K; break;
		case ES_SCANCODE_L:                           scancode = BX_KEY_L; break;
		case ES_SCANCODE_M:                           scancode = BX_KEY_M; break;
		case ES_SCANCODE_N:                           scancode = BX_KEY_N; break;
		case ES_SCANCODE_O:                           scancode = BX_KEY_O; break;
		case ES_SCANCODE_P:                           scancode = BX_KEY_P; break;
		case ES_SCANCODE_PAGE_DOWN:                   scancode = BX_KEY_PAGE_DOWN; break;
		case ES_SCANCODE_PAGE_UP:                     scancode = BX_KEY_PAGE_UP; break;
		case ES_SCANCODE_PAUSE:                       scancode = BX_KEY_PAUSE; break;
		case ES_SCANCODE_PERIOD:                      scancode = BX_KEY_PERIOD; break;
		case ES_SCANCODE_PRINT_SCREEN:                scancode = BX_KEY_PRINT; break;
		case ES_SCANCODE_Q:                           scancode = BX_KEY_Q; break;
		case ES_SCANCODE_R:                           scancode = BX_KEY_R; break;
		case ES_SCANCODE_S:                           scancode = BX_KEY_S; break;
		case ES_SCANCODE_SCROLL_LOCK:                 scancode = BX_KEY_SCRL_LOCK; break;
		case ES_SCANCODE_PUNCTUATION_3:               scancode = BX_KEY_SEMICOLON; break;
		case ES_SCANCODE_SLASH:                       scancode = BX_KEY_SLASH; break;
		case ES_SCANCODE_SPACE:                       scancode = BX_KEY_SPACE; break;
		case ES_SCANCODE_T:                           scancode = BX_KEY_T; break;
		case ES_SCANCODE_TAB:                         scancode = BX_KEY_TAB; break;
		case ES_SCANCODE_U:                           scancode = BX_KEY_U; break;
		case ES_SCANCODE_UP_ARROW:                    scancode = BX_KEY_UP; break;
		case ES_SCANCODE_V:                           scancode = BX_KEY_V; break;
		case ES_SCANCODE_W:                           scancode = BX_KEY_W; break;
		case ES_SCANCODE_X:                           scancode = BX_KEY_X; break;
		case ES_SCANCODE_Y:                           scancode = BX_KEY_Y; break;
		case ES_SCANCODE_Z:                           scancode = BX_KEY_Z; break;
		case ES_SCANCODE_NUM_0:                       scancode = BX_KEY_KP_INSERT; break;
		case ES_SCANCODE_NUM_1:                       scancode = BX_KEY_KP_END; break;
		case ES_SCANCODE_NUM_2:                       scancode = BX_KEY_KP_DOWN; break;
		case ES_SCANCODE_NUM_3:                       scancode = BX_KEY_KP_PAGE_DOWN; break;
		case ES_SCANCODE_NUM_4:                       scancode = BX_KEY_KP_LEFT; break;
		case ES_SCANCODE_NUM_5:                       scancode = BX_KEY_KP_5; break;
		case ES_SCANCODE_NUM_6:                       scancode = BX_KEY_KP_RIGHT; break;
		case ES_SCANCODE_NUM_7:                       scancode = BX_KEY_KP_HOME; break;
		case ES_SCANCODE_NUM_8:                       scancode = BX_KEY_KP_UP; break;
		case ES_SCANCODE_NUM_9:                       scancode = BX_KEY_KP_PAGE_UP; break;
		case ES_SCANCODE_NUM_ADD:                     scancode = BX_KEY_KP_ADD; break;
		case ES_SCANCODE_NUM_DIVIDE:                  scancode = BX_KEY_KP_DIVIDE; break;
		case ES_SCANCODE_NUM_ENTER:                   scancode = BX_KEY_KP_ENTER; break;
		case ES_SCANCODE_NUM_LOCK:                    scancode = BX_KEY_NUM_LOCK; break;
		case ES_SCANCODE_NUM_MULTIPLY:                scancode = BX_KEY_KP_MULTIPLY; break;
		case ES_SCANCODE_NUM_POINT:                   scancode = BX_KEY_KP_DELETE; break;
		case ES_SCANCODE_NUM_SUBTRACT:                scancode = BX_KEY_KP_SUBTRACT; break;
		case ES_SCANCODE_WWW_BACK:                    scancode = BX_KEY_INT_BACK; break;
		case ES_SCANCODE_WWW_STARRED:                 scancode = BX_KEY_INT_FAV; break;
		case ES_SCANCODE_WWW_FORWARD:                 scancode = BX_KEY_INT_FORWARD; break;
		case ES_SCANCODE_WWW_HOME:                    scancode = BX_KEY_INT_HOME; break;
		case ES_SCANCODE_MM_EMAIL:                    scancode = BX_KEY_INT_MAIL; break;
		case ES_SCANCODE_WWW_SEARCH:                  scancode = BX_KEY_INT_SEARCH; break;
		case ES_SCANCODE_WWW_STOP:                    scancode = BX_KEY_INT_STOP; break;
		case ES_SCANCODE_MM_CALC:                     scancode = BX_KEY_POWER_CALC; break;
		case ES_SCANCODE_MM_FILES:                    scancode = BX_KEY_POWER_MYCOMP; break;
		case ES_SCANCODE_ACPI_POWER:                  scancode = BX_KEY_POWER_POWER; break;
		case ES_SCANCODE_ACPI_SLEEP:                  scancode = BX_KEY_POWER_SLEEP; break;
		case ES_SCANCODE_ACPI_WAKE:                   scancode = BX_KEY_POWER_WAKE; break;
		case ES_SCANCODE_LEFT_ALT:                    scancode = BX_KEY_ALT_L; break;
		case ES_SCANCODE_LEFT_ARROW:                  scancode = BX_KEY_LEFT; break;
		case ES_SCANCODE_LEFT_BRACE:                  scancode = BX_KEY_LEFT_BRACKET; break;
		case ES_SCANCODE_LEFT_CTRL:                   scancode = BX_KEY_CTRL_L; break;
		case ES_SCANCODE_LEFT_FLAG:                   scancode = BX_KEY_WIN_L; break;
		case ES_SCANCODE_LEFT_SHIFT:                  scancode = BX_KEY_SHIFT_L; break;
		case ES_SCANCODE_RIGHT_ALT:                   scancode = BX_KEY_ALT_R; break;
		case ES_SCANCODE_RIGHT_ARROW:                 scancode = BX_KEY_RIGHT; break;
		case ES_SCANCODE_RIGHT_BRACE:                 scancode = BX_KEY_RIGHT_BRACKET; break;
		case ES_SCANCODE_RIGHT_CTRL:                  scancode = BX_KEY_CTRL_R; break;
		case ES_SCANCODE_RIGHT_SHIFT:                 scancode = BX_KEY_SHIFT_R; break;
		case ES_SCANCODE_CONTEXT_MENU:                scancode = BX_KEY_MENU; break;
		case ES_SCANCODE_PUNCTUATION_5:               scancode = BX_KEY_GRAVE; break;
		case ES_SCANCODE_HYPHEN:                      scancode = BX_KEY_MINUS; break;
		case ES_SCANCODE_PUNCTUATION_4:               scancode = BX_KEY_SINGLE_QUOTE; break;
	}

	return scancode;
}

int CanvasCallback(EsElement *element, EsMessage *message) {
	// TODO Is it safe to pass input to Bochs on this thread?

	if (message->type == ES_MSG_PAINT) {
		int ox = message->painter->width / 2 - instance->vmemWidth / 2;
		int oy = message->painter->height / 2 - instance->vmemHeight / 2;
		EsRectangle bounds = { ox, ox + instance->vmemWidth, oy, oy + instance->vmemHeight };
		EsDrawBitmap(message->painter, bounds, instance->vmem, instance->vmemWidth * 4, 0xFFFF);
		repaintQueued = false;
	} else if (message->type == ES_MSG_KEY_DOWN) {
		if (message->keyboard.scancode == ES_SCANCODE_RIGHT_CTRL) {
			theGui->toggle_mouse_enable();
		} else {
			DEV_kbd_gen_scancode(ConvertScancode(message->keyboard.scancode));
		}
	} else if (message->type == ES_MSG_KEY_UP) {
		if (message->keyboard.scancode == ES_SCANCODE_RIGHT_CTRL) {
		} else {
			DEV_kbd_gen_scancode(ConvertScancode(message->keyboard.scancode) | BX_KEY_RELEASED);
		}
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
	} else {
		return 0;
	}

	return ES_HANDLED;
}

void SetDimensions(int width, int height) {
	instance->vmemWidth = width;
	instance->vmemHeight = height;
	instance->vmem = (uint32_t *) EsHeapReallocate(instance->vmem, width * height * 4, true);
	EsElementRepaint(instance->display);
}

void MessageLoopThread(EsGeneric) {
	EsPrint("Reached message loop thread...\n");
	EsMessageMutexAcquire();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			instance = EsInstanceCreate(message, "Bochs");
			EsWindowSetTitle(instance->window, "Bochs", -1);
			EsWindowSetIcon(instance->window, ES_ICON_APPLICATIONS_DEVELOPMENT);
			instance->display = EsCustomElementCreate(instance->window, ES_CELL_FILL | ES_ELEMENT_FOCUSABLE);
			instance->display->messageUser = CanvasCallback;
			EsElementFocus(instance->display);
			SetDimensions(640, 480);
		} else if (message->type == ES_MSG_INSTANCE_OPEN) {
			configurationFileData = (char *) EsFileStoreReadAll(message->instanceOpen.file, &configurationFileBytes);

			if (!configurationFileData) {
				EsInstanceOpenComplete(message, false);
			} else {
				EsEventSet(openEvent);
				EsInstanceOpenComplete(message, true);
			}
		} else if (message->type == ES_MSG_INSTANCE_DESTROY) {
			// TODO Tell the emulator to stop.
			// unlink(configurationFile); (Do this on the POSIX thread.)
		}
	}
}

void bx_essence_gui_c::specific_init(int argc, char **argv, unsigned headerbar_y)
{
  put("ESSENCE");
  EsPrint("Starting Essence GUI...\n");

  for(int i=0;i<256;i++) for(int j=0;j<16;j++) vga_charmap[i*32+j] = sdl_font8x16[i][j];
}

void bx_essence_gui_c::handle_events(void)
{
	// We handle events on a separate thread.
}

void bx_essence_gui_c::flush(void)
{
	QueueRepaint();
}

void bx_essence_gui_c::clear_screen(void)
{
	EsMemoryZero(instance->vmem, instance->vmemWidth * 4 * guest_yres);
	QueueRepaint();
}

static unsigned prev_cursor_x=0;
static unsigned prev_cursor_y=0;

void bx_essence_gui_c::text_update(Bit8u *old_text, Bit8u *new_text,
                      unsigned long cursor_x, unsigned long cursor_y,
                      bx_vga_tminfo_t *tm_info)
{
  uint32_t text_palette[16];

  for (int i=0; i<16; i++) {
    text_palette[i] = col_vals[tm_info->actl_palette[i]];
  }

  int left = text_cols, right = 0, top = text_rows, bottom = 0;

  for (int j = 0; j < text_rows; j++) {
	  for (int i = 0; i < text_cols; i++) {
		  if (old_text[0] != new_text[0] || old_text[1] != new_text[1] || (prev_cursor_x == i && prev_cursor_y == j) || (cursor_x == i && cursor_y == j)) {
			  if (i < left) left = i;
			  if (i < top) top = i;
			  if (i > right) right = i;
			  if (i > bottom) bottom = i;

			  bool cursorHere = (cursor_x == i && cursor_y == j);
			  uint32_t foreground, background;

			  if (cursorHere) {
				  foreground = text_palette[new_text[1] >> 4], background = text_palette[new_text[1] & 0x0F];
			  } else {
				  foreground = text_palette[new_text[1] & 0x0F], background = text_palette[new_text[1] >> 4];
			  }

			  for (int y = 0; y < 16; y++) {
				  for (int x = 0; x < 9; x++) {
					instance->vmem[guest_xres * (y + j * 16) + (x + i * 9)] 
					 	  = (x != 9 && (vga_charmap[new_text[0] * 32 + y] & (1 << (7 - x)))) 
					 	  ? foreground : background;
				  }
			  }
		  }

		  old_text += 2;
		  new_text += 2;
	  }
  }

  QueueRepaint();
  prev_cursor_x = cursor_x;
  prev_cursor_y = cursor_y;
}

int bx_essence_gui_c::get_clipboard_text(Bit8u **bytes, Bit32s *nbytes)
{
	// TODO.
  UNUSED(bytes);
  UNUSED(nbytes);
  return 0;
}

int bx_essence_gui_c::set_clipboard_text(char *text_snapshot, Bit32u len)
{
	// TODO.
  UNUSED(text_snapshot);
  UNUSED(len);
  return 0;
}

bx_bool bx_essence_gui_c::palette_change(Bit8u index, Bit8u red, Bit8u green, Bit8u blue)
{
  col_vals[index] = (red << 16) | (green << 8) | blue;
  return 1;
}

void bx_essence_gui_c::graphics_tile_update(Bit8u *tile, unsigned x0, unsigned y0)
{
  unsigned x, y, x_size, y_size;
  unsigned color, offset;

  if ((x0 + x_tilesize) > guest_xres) {
    x_size = guest_xres - x0;
  } else {
    x_size = x_tilesize;
  }

  if ((y0 + y_tilesize) > guest_yres) {
    y_size = guest_yres - y0;
  } else {
    y_size = y_tilesize;
  }

  switch (guest_bpp) {
    case 8:  // 8 bits per pixel
      for (y=0; y<y_size; y++) {
        for (x=0; x<x_size; x++) {
          color = col_vals[tile[y*x_tilesize + x]];
	  instance->vmem[guest_xres * (y + y0) + (x + x0)] = color;
        }
      }
      break;
    default:
      BX_PANIC(("X_graphics_tile_update: bits_per_pixel %u handled by new graphics API",
                (unsigned) guest_bpp));
      return;
  }

  QueueRepaint();
}

void bx_essence_gui_c::dimension_update(unsigned x, unsigned y, unsigned fheight, unsigned fwidth, unsigned bpp)
{
  guest_textmode = (fheight > 0);
  guest_xres = x;
  guest_yres = y;
  guest_bpp = bpp;
  if (guest_textmode) {
	  text_cols = guest_xres / fwidth;
	  text_rows = guest_yres / fheight;
  }
  EsPrint("dimension_update: %d, %d, %d, %d, %d\n", x, y, fwidth, fheight, bpp);
  EsMessageMutexAcquire();
  SetDimensions(x, y);
  EsMessageMutexRelease();
}

unsigned bx_essence_gui_c::create_bitmap(const unsigned char *bmap, unsigned xdim, unsigned ydim)
{
	// TODO.
  UNUSED(bmap);
  UNUSED(xdim);
  UNUSED(ydim);
  return(0);
}

unsigned bx_essence_gui_c::headerbar_bitmap(unsigned bmap_id, unsigned alignment, void (*f)(void))
{
	// TODO.
  UNUSED(bmap_id);
  UNUSED(alignment);
  UNUSED(f);
  return(0);
}

void bx_essence_gui_c::show_headerbar(void)
{
	// TODO.
}

void bx_essence_gui_c::replace_bitmap(unsigned hbar_id, unsigned bmap_id)
{
	// TODO.
  UNUSED(hbar_id);
  UNUSED(bmap_id);
}

void bx_essence_gui_c::exit(void)
{
	// We don't need to do anything.
}

void bx_essence_gui_c::mouse_enabled_changed_specific(bx_bool val)
{
	// TODO.
}

int bxmain();

int main() {
#if 1
	int output = open("bochs_output.txt", O_WRONLY | O_CREAT);
	dup2(output, 1);
	dup2(output, 2);
	printf("in main()...\n");
	const char *temporaryFolder = getenv("TMPDIR");
	configurationFile = (char *) malloc(strlen(temporaryFolder) + 32);
	strcpy(configurationFile, temporaryFolder);
	strcat(configurationFile, "/");
	char *_argv[] = { "bochs", "-f", configurationFile, "-q" };
	size_t offset = strlen(configurationFile);
	for (int i = 0; i < 8; i++) configurationFile[i + offset] = (EsRandomU8() % 26) + 'a';
	configurationFile[offset + 8] = 0;
	strcat(configurationFile, ".txt");
	bx_startup_flags.argc = 4;
	bx_startup_flags.argv = _argv;
	openEvent = EsEventCreate(true);
	EsMessageMutexRelease();
	EsThreadCreate(MessageLoopThread, nullptr, 0);
	EsWaitSingle(openEvent);
	FILE *f = fopen(configurationFile, "wb");
	fprintf(stderr, "%s, %p, %d\n", configurationFile, f, (int32_t) configurationFileBytes);
	fwrite(configurationFileData, 1, configurationFileBytes, f);
	fclose(f);
#else
	char *_argv[] = { "bochs" };
	bx_startup_flags.argc = 1;
	bx_startup_flags.argv = _argv;
#endif
	return bxmain();
}

#endif /* if BX_WITH_ESSENCE */
