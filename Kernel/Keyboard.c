#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern u8 inb(u16 port);

static const char keymap[128] =
{
	0, 27, '1', '2', '3', '4', '5', '6',
	'7', '8', '9', '0', '-', '=', '\b', '\t',
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
	'o', 'p', '[', ']', '\n', 0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
	'\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '/', 0, '*',
	0, ' ', 0
};

static const char shiftmap[128] =
{
	0, 27, '!', '@', '#', '$', '%', '^',
	'&', '*', '(', ')', '_', '+', '\b', '\t',
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
	'O', 'P', '{', '}', '\n', 0, 'A', 'S',
	'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
	'"', '~', 0, '|', 'Z', 'X', 'C', 'V',
	'B', 'N', 'M', '<', '>', '?', 0, '*',
	0, ' ', 0
};

int shift = 0;

char getkey(void)
{
        u8 sc;

        if(!(inb(0x64) & 1))
                return 0;

        sc = inb(0x60);

        if(sc == 42 || sc == 54)
        {
                shift = 1;
                return 0;
        }

        if(sc == 170 || sc == 182)
        {
                shift = 0;
                return 0;
        }

        if(sc & 0x80)
                return 0;

        if(shift)
                return shiftmap[sc];

        return keymap[sc];
}
