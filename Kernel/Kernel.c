#include <stdint.h>

extern void pit_init_1000hz(void);
extern void sleep(unsigned int ms);

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;


#define VGA ((u16*)0xB8000)
#define W 80
#define H 50
#define MAX_FILES 64
#define MAX_NAME 32
#define MAX_DATA 512
#define INPUT_MAX 128
#define BUILD "V0U1-170526B18"
#define BAR "==============================================================================="

unsigned int cx = 0;
unsigned int cy = 0;
static u8 color = 0x0F;

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

	for(;;)
	{
		while(!(inb(0x64) & 1))
			;

		sc = inb(0x60);

		if(sc == 42 || sc == 54)
		{
			shift = 1;
			continue;
		}

		if(sc == 170 || sc == 182)
		{
			shift = 0;
			continue;
		}

		if(sc & 0x80)
			continue;

		if(shift)
			return shiftmap[sc];

		return keymap[sc];
	}
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

void startupBanner(void)
{
	sleep(500);
	print("Data addresses listed\n");
	print("\n");
	print("IVT:0x00000000->0x000003FF\n");
	print("Free low mem:0x00000500->0x00007BFF\n");
	print("Stack:0x00090000->downward\n");
	print("VGA mem:0x000A0000->0x000AFFFF\n");
	print("Text buf:0x000B0000->0x000BFFFF\n");
	print("\n");
	sleep(500);
	print("Boot sequence:\n");
	print("BIOS DATA  &OCU ->BOOTLOADER\n");
	print("        0x00000400->0x000004FF\n");
	print("BOOTLOADER  &OCU ->KERNEL ENTRY\n");
	print("        0x00007C00->0x00007DFF\n");
	print("        GTD &OCU:0x00007CDA->0x00007CF2\n");
	print("        gtd_code:0x00007CE2\n");
	print("        gtd_data:0x00007CEA\n");
	print("KERNEL ENTRY  &OCU ->KERNEL DATA\n");
	print("        0x00001000\n");
	print("KERNEL DATA  &OCU\n");
	print("        0x00001000->0x00004000\n");
	print("\n");
	print("Loading shell...\n");
}


void help(void)
{
	print("help      show commands\n");
	print("info      system info\n");
	print("cls       clear screen\n");
	print("ls        list files\n");
	print("touch     create file\n");
	print("mkdir     create dir\n");
	print("cat       read file\n");
	print("write     write file\n");
	print("cd        change dir\n");
	print("rm        remove file\n");
	print("addr      address map\n");
	print("color     set color\n");
	print("vmoff     power off vm\n");
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

	for(;;)
	{

		const int oldcy = cy;
		cy = 0;
		print(BAR);
		cy = oldcy;
		cx = 0;

		print("> ");

		readline(line, INPUT_MAX);

		if(strcmp(line, "help") == 0)
			help();
		else if(strcmp(line, "cls") == 0)
			clear();
		else if(starts(line, "color "))
			color = atoi(skip(line + 5));
		else if(strcmp(line, "vmoff") == 0)
			vmoff();
		else if(strcmp(line, "halt") == 0)
			halt();
		else if(line[0])
			print("Bad command\n");
	}
}

void kmain(void)
{
	clear();
	print("AneoEngine V0.1 Build ");
        print(BUILD);
        print("\n\n");
	pit_init_1000hz();
	startupBanner();
	sleep(1000);
	shell();

}



