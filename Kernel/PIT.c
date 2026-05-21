#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern void poutb(u16 port, u8 val);
extern void outb(u16 port, u8 val);
extern u8 inb(u16 port);

void pit_init_1000hz(void)
{
        unsigned int divisor = 1120000 / 1000;
        poutb(0x43, 0x36);
        poutb(0x40, divisor & 0xFF);
        poutb(0x40, (divisor >> 8) & 0xFF);
}

unsigned short pit_read_counter(void)
{
        unsigned char lo;
        unsigned char hi;

        outb(0x43, 0x00);

        lo = inb(0x40);
        hi = inb(0x40);

        return (hi << 8) | lo;
}

void sleep(unsigned int ms)
{
        unsigned int i;
        unsigned short last;
        unsigned short now;

        for (i = 0; i < ms; i++)
        {
                last = pit_read_counter();

                while(1)
                {
                        now = pit_read_counter();

                        if(now > last)
                                break;
                        last = now;
                }
        }
}

void beep(u32 freq)
{
	u32 divisor;
	u8 tmp;

	divisor = 1193180 / freq;

	outb(0x43, 0xB6);

	outb(0x42, (u8)(divisor & 0xFF));
	outb(0x42, (u8)((divisor >> 8) & 0xFF));

	tmp = inb(0x61);

	if (tmp != (tmp | 3))
		outb(0x61, tmp | 3);
}

void nosound(void)
{
	u8 tmp;

	tmp = inb(0x61) & 0xFC;
	outb(0x61, tmp);
}
