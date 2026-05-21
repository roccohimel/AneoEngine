#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern void print(const char *s);
extern void sleep(unsigned int ms);
extern unsigned int cy;
extern unsigned int cx;
extern u8 color;
extern void outw(u16 port, u16 val);
extern void poutw(u16 port, u16 val);
extern void poutwfail(u16 port, u16 val);

#define VAL1 0x2000
#define VAL2 0x3400

#define IOP1 0x604
#define IOP2 0xB004
#define IOP3 0x4004

void vmoff(void)
{
	color = 0x0F;
	unsigned int oldcy = cy;
	unsigned int oldcx = cx;
	cy = 0;
	poutw(IOP1, VAL1);
	poutw(IOP2, VAL1);
	poutw(IOP3, VAL2);
	poutwfail(IOP1, VAL1);
	poutwfail(IOP2, VAL1);
        poutwfail(IOP3, VAL2);
	cy = oldcy;
	cx = oldcx;


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
