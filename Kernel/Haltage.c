#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern void print(const char *s);
extern void sleep(unsigned int ms);
extern unsigned int cy;
extern u8 color;
extern void outw(u16 port, u16 val);

void vmoff(void)
{
	color = 0x0F;
	cy = 0;
	print("VAL:0x00002000->IOP:0x00000604\n");
	outw(0x604, 0x2000);
	print("VAL:0x00002000->IOP:0x0000B004\n");
	outw(0xB004, 0x2000);
	print("VAL:0x00003400->IOP:0x00004004\n");
	outw(0x4004, 0x3400);
	print("Halted: VAL->IOP writing error\n");
	print("ERR: Failed to write values:\n");
	print("        VAL:0x00002000->IOP:0x00000604\n");
	print("        VAL:0x00002000->IOP:0x0000B004\n");
	print("        VAL:0x00003400->IOP:0x00004004\n");


	for(;;)
		asm volatile("hlt");
}

void halt(void)
{
	color = 0x0F;
	cy = 0;
	print("Halted...");
	sleep(1000);
	for (;;)
		asm volatile("hlt");
}
