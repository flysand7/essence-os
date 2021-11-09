// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#include <module.h>
#include <arch/x86_pc.h>

struct VideoModeInformation {
	uint8_t valid : 1, edidValid : 1;
	uint8_t bitsPerPixel;
	uint16_t widthPixels, heightPixels;
	uint16_t bytesPerScanlineLinear;
	uint64_t bufferPhysical;
	uint8_t edid[128];
};

VideoModeInformation *vbeMode;
uint32_t screenWidth, screenHeight, strideX, strideY;
volatile uint8_t *linearBuffer; 

#if 0
float colorBlindnessMatrix[3][9] = {
	{
		// Protanopia.
		 0.171, 0.829, 0,
		 0.171, 0.829, 0,
		-0.005, 0.005, 1, 
	},

	{
		// Deuteranopia.
		 0.330, 0.670, 0,
		 0.330, 0.670, 0,
		-0.028, 0.028, 1,
	},

	{
		// Tritanopia.
		1, 0.127, -0.127,
		0, 0.874,  0.126,
		0, 0.874,  0.126,
	},
};

// #define SIMULATE_COLOR_BLINDNESS (1)
#endif

void UpdateScreen_32_XRGB(K_USER_BUFFER const uint8_t *source, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t sourceStride,
		uint32_t destinationX, uint32_t destinationY) {
	GraphicsUpdateScreen32(source, sourceWidth, sourceHeight, sourceStride, 
			destinationX, destinationY, screenWidth, screenHeight, strideY, linearBuffer);
}

void DebugPutBlock_32_XRGB(uintptr_t x, uintptr_t y, bool toggle) {
	GraphicsDebugPutBlock32(x, y, toggle, screenWidth, screenHeight, strideY, linearBuffer);
}

void DebugClearScreen_32_XRGB() {
	GraphicsDebugClearScreen32(screenWidth, screenHeight, strideY, linearBuffer);
}

void UpdateScreen_24_RGB(K_USER_BUFFER const uint8_t *source, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t sourceStride,
		uint32_t destinationX, uint32_t destinationY) {
	GraphicsUpdateScreen24(source, sourceWidth, sourceHeight, sourceStride, 
			destinationX, destinationY, screenWidth, screenHeight, strideY, linearBuffer);
}

void DebugPutBlock_24_RGB(uintptr_t x, uintptr_t y, bool toggle) {
	if (toggle) {
		linearBuffer[y * strideY + x * 3 + 0] += 0x4C;
		linearBuffer[y * strideY + x * 3 + 1] += 0x4C;
		linearBuffer[y * strideY + x * 3 + 2] += 0x4C;
	} else {
		linearBuffer[y * strideY + x * 3 + 0] = 0xFF;
		linearBuffer[y * strideY + x * 3 + 1] = 0xFF;
		linearBuffer[y * strideY + x * 3 + 2] = 0xFF;
	}

	linearBuffer[(y + 1) * strideY + (x + 1) * 3 + 0] = 0;
	linearBuffer[(y + 1) * strideY + (x + 1) * 3 + 1] = 0;
	linearBuffer[(y + 1) * strideY + (x + 1) * 3 + 2] = 0;
}

void DebugClearScreen_24_RGB() {
	for (uintptr_t i = 0; i < screenWidth * screenHeight * 3; i += 3) {
		linearBuffer[i + 2] = 0x18;
		linearBuffer[i + 1] = 0x7E;
		linearBuffer[i + 0] = 0xCF;
	}
}

void InitialiseVBE(KDevice *parent) {
	if (KGraphicsIsTargetRegistered()) {
		return;
	}

	vbeMode = (VideoModeInformation *) MMMapPhysical(MMGetKernelSpace(), 0x7000 + GetBootloaderInformationOffset(), 
			sizeof(VideoModeInformation), ES_FLAGS_DEFAULT);

	if (!vbeMode->valid) {
		return;
	}

	if (vbeMode->edidValid) {
		for (uintptr_t i = 0; i < 128; i++) {
			EsPrint("EDID byte %d: %X.\n", i, vbeMode->edid[i]);
		}
	}

	KGraphicsTarget *target = (KGraphicsTarget *) KDeviceCreate("VBE", parent, sizeof(KGraphicsTarget));

	linearBuffer = (uint8_t *) MMMapPhysical(MMGetKernelSpace(), vbeMode->bufferPhysical, 
			vbeMode->bytesPerScanlineLinear * vbeMode->heightPixels, MM_REGION_WRITE_COMBINING);
	screenWidth = target->screenWidth = vbeMode->widthPixels;
	screenHeight = target->screenHeight = vbeMode->heightPixels;
	strideX = vbeMode->bitsPerPixel >> 3;
	strideY = vbeMode->bytesPerScanlineLinear;

	if (vbeMode->bitsPerPixel == 32) {
		target->updateScreen = UpdateScreen_32_XRGB;
		target->debugPutBlock = DebugPutBlock_32_XRGB;
		target->debugClearScreen = DebugClearScreen_32_XRGB;
	} else {
		target->updateScreen = UpdateScreen_24_RGB; 
		target->debugPutBlock = DebugPutBlock_24_RGB;
		target->debugClearScreen = DebugClearScreen_24_RGB;
	}

	// TODO Other color modes.

	KRegisterGraphicsTarget(target);
}

#if 0

uint8_t vgaMode18[] = {
	0xE3, 0x03, 0x01, 0x08, 0x00, 0x06, 0x5F, 0x4F,
	0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E, 0x00, 0x40,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x0C,
	0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3, 0xFF, 0x00,
	0x00, 0x00, 0x00, 0x03, 0x00, 0x05, 0x0F, 0xFF,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
	0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
	0x01, 0x00, 0x0F, 0x00, 0x00,
};

volatile uint8_t *vgaAddress;

#define VGA_SCREEN_WIDTH (640)
#define VGA_SCREEN_HEIGHT (480)

uint8_t egaPaletteConverter[4][64] = {
	{ 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1, 
	  0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1, },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 1, 1, 1,
	  0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 1, 1, 1, },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	  1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, },
	{ 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1,
	  0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, },
};

void VGAUpdateScreen(uint8_t *_source, uint8_t *modifiedScanlineBitset, KModifiedScanline *modifiedScanlines) {
	for (int plane = 0; plane < 4; plane++) {
		uint8_t *source = _source;

		ProcessorOut8(IO_VGA_SEQ_INDEX, 2);
		ProcessorOut8(IO_VGA_SEQ_DATA, 1 << plane);

		for (uintptr_t y_ = 0; y_ < VGA_SCREEN_HEIGHT / 8; y_++) {
			if (modifiedScanlineBitset[y_] == 0) {
				source += VGA_SCREEN_WIDTH * 32;
				continue;
			}

			for (uintptr_t y = 0; y < 8; y++) {
				uint8_t *sourceStart = source;

				if ((modifiedScanlineBitset[y_] & (1 << y)) == 0) {
					source += VGA_SCREEN_WIDTH * 4;
					continue;
				}

				KModifiedScanline *scanline = modifiedScanlines + y + (y_ << 3);

				uintptr_t x = scanline->minimumX & ~7;
				source += 4 * x;

				while (x < scanline->maximumX) {
					uint8_t v = 0;

					for (int i = 7; i >= 0; i--) {
						if (egaPaletteConverter[plane][((source[0] >> 6) & 3) | ((source[1] >> 4) & 12) | ((source[2] >> 2) & 48)]) {
							v |= 1 << i;
						}

						source += 4;
					}

					vgaAddress[(y + y_ * 8) * 80 + (x >> 3)] = v;
					x += 8;
				}

				source = sourceStart + VGA_SCREEN_WIDTH * 4;
			}
		}
	}
}

void VGAPutBlock(uintptr_t x, uintptr_t y, bool toggle) {
	for (int plane = 0; plane < 4; plane++) {
		ProcessorOut8(IO_VGA_SEQ_INDEX, 2);
		ProcessorOut8(IO_VGA_SEQ_DATA, 1 << plane);
		
		if (toggle) {
			vgaAddress[y * 80 + x / 8] ^= 1 << (7 - (x & 7));
		} else {
			vgaAddress[y * 80 + x / 8] |= 1 << (7 - (x & 7));
		}
	}
}

void VGAClearScreen() {
	for (int plane = 0; plane < 4; plane++) {
		ProcessorOut8(IO_VGA_SEQ_INDEX, 2);
		ProcessorOut8(IO_VGA_SEQ_DATA, 1 << plane);
		EsMemoryZero((void *) vgaAddress, VGA_SCREEN_WIDTH / 8 * VGA_SCREEN_HEIGHT);
	}
}

void InitialiseVGA(KDevice *parent) {
	if (KGraphicsIsTargetRegistered()) {
		return;
	}

	vgaAddress = (uint8_t *) MMMapPhysical(MMGetKernelSpace(), 0xA0000, 0x10000, MM_REGION_WRITE_COMBINING);
	uint8_t *registers = vgaMode18;
	ProcessorOut8(IO_VGA_MISC_WRITE, *registers++);
	for (int i = 0; i < 5; i++) { ProcessorOut8(IO_VGA_SEQ_INDEX, i); ProcessorOut8(IO_VGA_SEQ_DATA, *registers++); }
	ProcessorOut8(IO_VGA_CRTC_INDEX, 0x03);
	ProcessorOut8(IO_VGA_CRTC_DATA, ProcessorIn8(IO_VGA_CRTC_DATA) | 0x80);
	ProcessorOut8(IO_VGA_CRTC_INDEX, 0x11);
	ProcessorOut8(IO_VGA_CRTC_DATA, ProcessorIn8(IO_VGA_CRTC_DATA) & ~0x80);
	registers[0x03] |= 0x80;
	registers[0x11] &= ~0x80;
	for (int i = 0; i < 25; i++) { ProcessorOut8(IO_VGA_CRTC_INDEX, i); ProcessorOut8(IO_VGA_CRTC_DATA, *registers++); }
	for (int i = 0; i < 9; i++) { ProcessorOut8(IO_VGA_GC_INDEX, i); ProcessorOut8(IO_VGA_GC_DATA, *registers++); }
	for (int i = 0; i < 21; i++) { ProcessorIn8(IO_VGA_INSTAT_READ); ProcessorOut8(IO_VGA_AC_INDEX, i); ProcessorOut8(IO_VGA_AC_WRITE, *registers++); }
	ProcessorIn8(IO_VGA_INSTAT_READ);
	ProcessorOut8(IO_VGA_AC_INDEX, 0x20);

	KGraphicsTarget *target = (KGraphicsTarget *) KDeviceCreate("VGA", parent, sizeof(KGraphicsTarget));
	target->screenWidth = VGA_SCREEN_WIDTH;
	target->screenHeight = VGA_SCREEN_HEIGHT;
	target->updateScreen = VGAUpdateScreen;
	target->debugPutBlock = VGAPutBlock;
	target->debugClearScreen = VGAClearScreen;
	target->reducedColors = true;
	// TODO Debug callbacks.
	KRegisterGraphicsTarget(target);
}

#endif

KDriver driverSVGA = {
	.attach = InitialiseVBE,
};
