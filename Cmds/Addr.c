//Command that prints memory addresses
#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern void putc(char c);
extern void print(const char *s);
extern void printad(const char *s, uint32_t x);
extern void printadocu(const char *s, uint32_t x1, uint32_t x2);
extern void indprintad(const char *s, uint32_t x);
extern void indprintadocu(const char *s, uint32_t x1, uint32_t x2);


extern uint32_t IVT_START;
extern uint32_t IVT_END;
extern uint32_t FREE_LOW_MEM_START;
extern uint32_t FREE_LOW_MEM_END;
extern uint32_t STACK;
extern uint32_t VGA_MEM_START;
extern uint32_t VGA_MEM_END;
extern uint32_t VGA_TEXT_BUF_START;
extern uint32_t VGA_TEXT_BUF_END;

extern uint32_t BIOS_DATA_START;
extern uint32_t BIOS_DATA_END;
extern uint32_t BOOTLOADER_START;
extern uint32_t BOOTLOADER_END;
extern uint32_t GTD_START;
extern uint32_t GTD_END;
extern uint32_t GTD_CODE;
extern uint32_t GTD_DATA;
extern uint32_t KERNEL_ENTRY;
extern uint32_t KERNEL_DATA_START;
extern uint32_t KERNEL_DATA_END;

void addr(void)
{//print them
	print("Data addresses listed\n");
        putc('\n');
        printadocu("IVT", IVT_START, IVT_END);
	printadocu("Free low memory", FREE_LOW_MEM_START, FREE_LOW_MEM_END);
	printad("Stack", STACK);
	printadocu("VGA memory", VGA_MEM_START, VGA_MEM_END);
	printadocu("VGA text buffer", VGA_TEXT_BUF_START, VGA_TEXT_BUF_END);
	printadocu("BIOS data", BIOS_DATA_START, BIOS_DATA_END);
	printadocu("AEBoot bootloader", BOOTLOADER_START, BOOTLOADER_END);
	indprintadocu("GTD", GTD_START, GTD_END);
	indprintad("gtd_code", GTD_CODE);
	indprintad("gtd_data", GTD_DATA);
	indprintad("Kernel entry", KERNEL_ENTRY);
	printadocu("Kernel data", KERNEL_DATA_START, KERNEL_DATA_END);
}
