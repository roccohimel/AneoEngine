//keyboard driver
#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define KEY_F1  0x3B
#define KEY_F2  0x3C
#define KEY_F3  0x3D
#define KEY_F4  0x3E
#define KEY_F5  0x3F
#define KEY_F6  0x40
#define KEY_F7  0x41
#define KEY_F8  0x42
#define KEY_F9  0x43
#define KEY_F10 0x44
#define KEY_F11 0x57
#define KEY_F12 0x58

extern u8 inb(u16 port);
extern void print(const char *s);
extern int helpMenu(void);
extern void reset(void);
extern int utilsMenu(void);
extern void cpustat(void);

static const char keymap[128] =
{//allowed chars
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
{//allowed chars when shift is pressed
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

#define KEY_UP    1
#define KEY_DOWN  2
#define KEY_LEFT  3
#define KEY_RIGHT 4

int ctrl = 0;
int ext = 0;

static void HandleFn(u8 sc)
{
	switch(sc)
	{
		case KEY_F1:
			helpMenu();
			break;
		case KEY_F2:
			reset();
                        break;
		case KEY_F3:
			utilsMenu();
                        break;
		case KEY_F4:
			cpustat();
                        break;
		case KEY_F5:
                        print("This is the F5 key\n");
                        break;
		case KEY_F6:
                        print("This is the F6 key\n");
                        break;
		case KEY_F7:
                        print("This is the F7 key\n");
                        break;
		case KEY_F8:
                        print("This is the F8 key\n");
                        break;
		case KEY_F9:
                        print("This is the F9 key\n");
                        break;
		case KEY_F10:
                        print("This is the F10 key\n");
                        break;
	}
}

int getkey(void)
{//get the current key from keyboard IO port
        u8 sc;
	char c;

        if(!(inb(0x64) & 1))
                return 0;

        sc = inb(0x60);

        if(sc == 0xE0)
	{
		ext = 1;
		return 0;
	}

	if(ext)
	{
		if(sc == 0x48)
			return KEY_UP;
		if(sc == 0x50)
			return KEY_DOWN;
		if(sc == 0x4B)
			return KEY_LEFT;
		if(sc == 0x4D)
			return KEY_RIGHT;
		return 0;
	}

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

        if(sc == 29)
	{
		ctrl = 1;
		return 0;
	}

	if(sc == 157)
	{
		ctrl = 0;
		return 0;
	}

	if(sc & 0x80)
                return 0;
	if(
		(sc >= KEY_F1 && sc <= KEY_F10) ||
		sc == KEY_F11 ||
		sc == KEY_F12
	)
	{
		HandleFn(sc);
		return 0;
	}

	if(sc >= 128)
		return 0;

	if(shift)
		c = shiftmap[sc];
	else
		c = keymap[sc];

	return c;
}

