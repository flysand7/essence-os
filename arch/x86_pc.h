#ifndef ARCH_X86_PC_HEADER
#define ARCH_X86_PC_HEADER

#if !defined(ES_ARCH_X86_64) && !defined(ES_ARCH_X86_32)
#error Included x86_pc.h but not targeting x86_32 or x86_64.
#endif

// --------------------------------- Standardised IO ports.

#define IO_PIC_1_COMMAND		(0x0020)
#define IO_PIC_1_DATA			(0x0021)
#define IO_PIT_DATA			(0x0040)
#define IO_PIT_COMMAND			(0x0043)
#define IO_PS2_DATA			(0x0060)
#define IO_PC_SPEAKER			(0x0061)
#define IO_PS2_STATUS			(0x0064)
#define IO_PS2_COMMAND			(0x0064)
#define IO_RTC_INDEX 			(0x0070)
#define IO_RTC_DATA 			(0x0071)
#define IO_PIC_2_COMMAND		(0x00A0)
#define IO_PIC_2_DATA			(0x00A1)
#define IO_BGA_INDEX			(0x01CE)
#define IO_BGA_DATA			(0x01CF)
#define IO_ATA_1			(0x0170) // To 0x0177.
#define IO_ATA_2			(0x01F0) // To 0x01F7.
#define IO_COM_4			(0x02E8) // To 0x02EF.
#define IO_COM_2			(0x02F8) // To 0x02FF.
#define IO_ATA_3			(0x0376)
#define IO_VGA_AC_INDEX 		(0x03C0)
#define IO_VGA_AC_WRITE 		(0x03C0)
#define IO_VGA_AC_READ  		(0x03C1)
#define IO_VGA_MISC_WRITE 		(0x03C2)
#define IO_VGA_MISC_READ  		(0x03CC)
#define IO_VGA_SEQ_INDEX 		(0x03C4)
#define IO_VGA_SEQ_DATA  		(0x03C5)
#define IO_VGA_DAC_READ_INDEX  		(0x03C7)
#define IO_VGA_DAC_WRITE_INDEX 		(0x03C8)
#define IO_VGA_DAC_DATA        		(0x03C9)
#define IO_VGA_GC_INDEX 		(0x03CE)
#define IO_VGA_GC_DATA  		(0x03CF)
#define IO_VGA_CRTC_INDEX 		(0x03D4)
#define IO_VGA_CRTC_DATA  		(0x03D5)
#define IO_VGA_INSTAT_READ 		(0x03DA)
#define IO_COM_3			(0x03E8) // To 0x03EF.
#define IO_ATA_4			(0x03F6)
#define IO_COM_1			(0x03F8) // To 0x03FF.
#define IO_PCI_CONFIG 			(0x0CF8)
#define IO_PCI_DATA   			(0x0CFC)

// --------------------------------- Forward declarations.

uint8_t ACPIGetCenturyRegisterIndex();
uintptr_t GetBootloaderInformationOffset();

#endif
