ENTRY(_start)

SECTIONS
{
	. = 0x100000;
	.text : ALIGN(512B)
	{
		*(.text)
	}
	.rodata : ALIGN(512B)
	{
		*(.rodata)
	}
	.data : ALIGN(512B)
	{
		*(.data)
	}
	.bss : ALIGN(512B)
	{
		*(COMMON)
		*(.bss)
	}
}
