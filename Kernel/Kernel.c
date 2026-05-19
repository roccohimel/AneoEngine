#include <stdint.h>

extern void pit_init_1000hz(void);
extern void sleep(unsigned int ms);
extern char getkey(void);
extern void vmoff(void);
extern void halt(void);
extern void startupBanner(void);
extern int helpMenu(void);
extern int programsMenu(void);
extern void addr(void);
extern const char *logo;

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;


#define VGA ((u16*)0xB8000)
#define W 80
#define H 50
#define MAX_FILES 64
#define MAX_NAME 32
#define MAX_DATA 512

const char *BAR = "===============================================================================";
const char *VERSION = "V0.1";
const char *BUILD = "V0U1-180526B4";
unsigned int cx = 0;
unsigned int cy = 0;
unsigned int INPUT_MAX = 128;
u8 color = 0x0F;

void outb(u16 port, u8 val)
{
	asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

u8 inb(u16 port)
{
	u8 ret;
	asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void outw(u16 port, u16 val)
{
	asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}



void cursor_update(void)
{
	u16 pos = cy * W + cx;

	outb(0x3D4, 0x0F);
	outb(0x3D5, pos & 0xFF);
	outb(0x3D4, 0x0E);
	outb(0x3D5, (pos >> 8) & 0xFF);
}

int strlen(const char *s)
{
	int i = 0;

	while(s[i])
		i++;

	return i;
}

int strcmp(const char *a, const char *b)
{
	int i = 0;

	while(a[i] && b[i] && a[i] == b[i])
		i++;

	return a[i] - b[i];
}

void strcpy(char *d, const char *s)
{
	int i = 0;

	while(s[i])
	{
		d[i] = s[i];
		i++;
	}

	d[i] = 0;
}

void memset(void *p, int v, int n)
{
	u8 *b = (u8*)p;
	int i = 0;

	while(i < n)
	{
		b[i] = v;
		i++;
	}
}

void scroll(void)
{
	int y;
	int x;

	if(cy < H)
		return;

	for(y = 1; y < H; y++)
	{
		for(x = 0; x < W; x++)
			VGA[(y - 1) * W + x] = VGA[y * W + x];
	}

	for(x = 0; x < W; x++)
		VGA[(H - 1) * W + x] = (color << 8) | ' ';

	cy = H - 1;
}

void putc(char c)
{
	if(c == '\n')
	{
		cx = 0;
		cy++;
		scroll();
		cursor_update();
		return;
	}

	if(c == '\b')
	{
		if(cx > 0)
			cx--;
		VGA[cy * W + cx] = (color << 8) | ' ';
		cursor_update();
		return;
	}

	VGA[cy * W + cx] = (color << 8) | c;
	cx++;

	if(cx >= W)
	{
		cx = 0;
		cy++;
	}

	scroll();
	cursor_update();
}

void print(const char *s)
{
	int i = 0;

	while(s[i])
	{
		putc(s[i]);
		i++;
	}
}

void perror(const char *s)
{
	const u8 oldcolor = color;
	color = 0x4E;

	print(s);

	color = oldcolor;
}


void clear(void)
{
	int i = 0;

	while(i < W * H)
	{
		VGA[i] = (color << 8) | ' ';
		i++;
	}

	cx = 0;
	cy = 1;
	cursor_update();
}

void print_hex(u32 v)
{
	char *h = "0123456789ABCDEF";
	int i;

	print("0x");

	for(i = 28; i >= 0; i -= 4)
		putc(h[(v >> i) & 15]);
}


void readline(char *buf, int max)
{
	int i = 0;
	char c;

	for(;;)
	{
		c = getkey();

		if(!c)
			continue;

		if(c == '\n')
		{
			buf[i] = 0;
			putc('\n');
			return;
		}

		if(c == '\b')
		{
			if(i > 0)
			{
				i--;
				putc('\b');
			}
			continue;
		}

		if(i < max - 1)
		{
			buf[i] = c;
			i++;
			putc(c);
		}
	}
}

int starts(const char *s, const char *p)
{
	int i = 0;

	while(p[i])
	{
		if(s[i] != p[i])
			return 0;

		i++;
	}

	return 1;
}

char *skip(char *s)
{
	while(*s == ' ')
		s++;

	return s;
}


int atoi(const char *s)
{
	int n = 0;

	while(*s >= '0' && *s <= '9')
	{
		n = n * 10 + (*s - '0');
		s++;
	}

	return n;
}

void shell(void)
{

	char line[INPUT_MAX];
	char *a;
	char *b;

	color = 0x1F;
	clear();
	cy = 0;
	print(BAR);
	cy = 1;
	cx = 0;

	print(logo);
	for(;;)
	{

		const int oldcy = cy;
		cy = 0;
		print(BAR);
		cy = oldcy;
		cx = 0;

		print("> ");

		readline(line, INPUT_MAX);

		if(strcmp(line, "cls") == 0)
			clear();
		else if(strcmp(line, "help") == 0)
			helpMenu();
		else if(starts(line, "color "))
			color = atoi(skip(line + 5));
		else if(strcmp(line, "vmoff") == 0)
			vmoff();
		else if(strcmp(line, "halt") == 0)
			halt();
		else if(strcmp(line, "addr") == 0)
			addr();
		else if(strcmp(line, "programs") == 0)
                        programsMenu();
		else if(line[0])
			perror("ERR: Unknown command\n");
	}
}

void kmain(void)
{
	clear();
	startupBanner();
	sleep(1000);
	shell();

}



