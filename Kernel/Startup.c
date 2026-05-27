//Main startup file that contains memory
//addresses and starts the shell loop
#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern void putc(char c);
extern void print(const char *s);
extern void sleep(unsigned int ms);
extern const char *VERSION;
extern const char *BUILD;
extern void pit_init_1000hz(void);
extern void printad(const char *s, uint32_t x);
extern void printadocu(const char *s, uint32_t x1, uint32_t x2);
extern void beep(u32 freq);
extern void nosound(void);
extern void indprintad(const char *s, uint32_t x);
extern void indprintadocu(const char *s, uint32_t x1, uint32_t x2);
extern u8 inb(u16 port);

//Addresses not involved in boot
uint32_t IVT_START = 0x0;
uint32_t IVT_END = 0x3FF;
uint32_t FREE_LOW_MEM_START = 0x500;
uint32_t FREE_LOW_MEM_END = 0x7BFF;
uint32_t STACK = 0x90000;
uint32_t VGA_MEM_START = 0xA0000;
uint32_t VGA_MEM_END = 0xAFFFF;
uint32_t VGA_TEXT_BUF_START = 0xB0000;
uint32_t VGA_TEXT_BUF_END = 0xBFFFF;

//addresses involved in boot
uint32_t BIOS_DATA_START = 0x400;
uint32_t BIOS_DATA_END = 0x4FF;
uint32_t BOOTLOADER_START = 0x7C00;
uint32_t BOOTLOADER_END = 0x7DFF;
uint32_t GTD_START = 0x7CDA;
uint32_t GTD_END = 0x7CF2;
uint32_t GTD_CODE = 0x7CE2;
uint32_t GTD_DATA = 0x7CEA;
uint32_t KERNEL_ENTRY = 0x10000;
uint32_t KERNEL_DATA_START = 0x10000;
uint32_t KERNEL_DATA_END = 0x20000;

void waitkey(void)
{//wait till key is pressed; continue
        u8 sc;

        while (inb(0x64) & 1)
                inb(0x60);

        for (;;) {
                while (!(inb(0x64) & 1));

                sc = inb(0x60);

                if (!(sc & 0x80))
                        break;
        }
}

void startupBanner(void)
{//startup banner
	print("AneoEngine ");
	print(VERSION);
	print(" Build ");
        print(BUILD);
        print("\n\n");
	print("Initializing PIT...\n");
	pit_init_1000hz();
        sleep(500);
	print("Data addresses listed\n");
        putc('\n');
        printadocu("IVT", IVT_START, IVT_END);
	printadocu("Free low memory", FREE_LOW_MEM_START, FREE_LOW_MEM_END);
	printad("Stack", STACK);
	printadocu("VGA memory", VGA_MEM_START, VGA_MEM_END);
	printadocu("VGA text buffer", VGA_TEXT_BUF_START, VGA_TEXT_BUF_END);
	putc('\n');
        sleep(500);
        print("Boot sequence:\n");
	printadocu("BIOS data", BIOS_DATA_START, BIOS_DATA_END);
	printadocu("AEBoot bootloader", BOOTLOADER_START, BOOTLOADER_END);
	indprintadocu("GTD", GTD_START, GTD_END);
	indprintad("gtd_code", GTD_CODE);
	indprintad("gtd_data", GTD_DATA);
	indprintad("Kernel entry", KERNEL_ENTRY);
	printadocu("Kernel data", KERNEL_DATA_START, KERNEL_DATA_END);
	putc('\n');
	print("Press any key to continue...\n");
	waitkey();
	beep(1000);
}
