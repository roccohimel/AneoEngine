#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;


extern void print(const char *s);
extern void outb(u16 port, u8 val);
extern u8 inb(u16 port);

void pit_init_1000hz(void)
{
        unsigned int divisor = 1120000 / 1000;
        outb(0x43, 0x36);
        outb(0x40, divisor & 0xFF);
        outb(0x40, (divisor >> 8) & 0xFF);
        print("PIT:IOP:0x40-0x43\n");
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
