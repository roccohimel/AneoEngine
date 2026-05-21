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
extern void nosound(void);

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;


#define VGA ((u16*)0xB8000)
#define W 80
#define H 50
#define MAX_FILES 64
#define MAX_NAME 32
#define MAX_DATA 512
#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71
#define RTC_SECONDS 0x00
#define RTC_MINUTES 0x02
#define RTC_HOURS   0x04
#define RTC_DAY     0x07
#define RTC_MONTH   0x08
#define RTC_YEAR    0x09

const char *BAR1 = "====";
const char *BAR2 = "========================================================";
const char *VERSION = "V0.1";
const char *BUILD = "V0U1-210526B2";
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

u8 cmos_read(u8 reg)
{
        outb(CMOS_ADDR, reg);
        return inb(CMOS_DATA);
}

u8 bcd_to_bin(u8 val)
{
        return (val & 0x0F) + ((val >> 4) * 10);
}

typedef unsigned char u8;

typedef struct {
        u8 second;
        u8 minute;
        u8 hour;
        u8 day;
        u8 month;
        u8 year;
} RTCDateTime;

RTCDateTime rtc_get_datetime(void)
{
        RTCDateTime t;

        t.second = bcd_to_bin(cmos_read(RTC_SECONDS));
        t.minute = bcd_to_bin(cmos_read(RTC_MINUTES));
        t.hour   = bcd_to_bin(cmos_read(RTC_HOURS));
        t.day    = bcd_to_bin(cmos_read(RTC_DAY));
        t.month  = bcd_to_bin(cmos_read(RTC_MONTH));
        t.year   = bcd_to_bin(cmos_read(RTC_YEAR));

        return t;
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

void print2(u8 n)
{
        putc('0' + (n / 10));
        putc('0' + (n % 10));
}

void rtc_print_datetime(void)
{
        RTCDateTime t = rtc_get_datetime();

        print2(t.month);
        putc('/');
        print2(t.day);
        putc('/');
        print("20");
        print2(t.year);

        putc(' ');

        print2(t.hour);
        putc(':');
        print2(t.minute);
        putc(':');
        print2(t.second);
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

void printx(uint32_t x)
{
	char hex[] = "0123456789ABCDEF";

	print("0x");

	for (int i = 28; i >= 0; i -= 4)
	{
		uint8_t digit = (x >> i) &  0xF;
		putc(hex[digit]);
	}
}

void printad(const char *s, uint32_t x)
{
	print(s);
	print(":");
	printx(x);
	putc('\n');
}

void printadocu(const char *s, uint32_t x1, uint32_t x2)
{
        print(s);
	print(":&OCU:");
        printx(x1);
	putc('-');
	printx(x2);
	putc('\n');
}

void indprintad(const char *s, uint32_t x)
{
        print("        ");
	print(s);
        print(":");
        printx(x);
        putc('\n');
}

void indprintadocu(const char *s, uint32_t x1, uint32_t x2)
{
        print("        ");
	print(s);
        print(":&OCU:");
        printx(x1);
        putc('-');
        printx(x2);
        putc('\n');
}

void poutw(u16 port, u16 val)
{
	outw(port, val);
	print("VAL:");
	printx(val);
	print("->IOP:");
	printx(port);
	putc('\n');
}


void poutb(u16 port, u8 val)
{
        outb(port, val);
        print("VAL:");
        printx(val);
        print("->IOP:");
        printx(port);
        putc('\n');
}

void poutwfail(u16 port, u16 val)
{
	print("ERR: Failed to write value to IO port\n");
	print("        VAL:");
        printx(val);
        print("->IOP:");
        printx(port);
	putc('\n');
}

int rtc_get_second(void)
{
        outb(0x70, 0x00);
        return inb(0x71);
}

#define RTC_X  4
#define RTC_Y  0

void draw_topbar_once(void)
{
        u8 oldcolor = color;
	color = 0xF1;
	cy = 0;
        cx = 0;
        print(BAR1);

        cx = RTC_X;
        cy = RTC_Y;
        rtc_print_datetime();

        print(BAR2);
	color = oldcolor;
}

void update_rtc_only(void)
{
        u8 oldcolor = color;
	color = 0xF1;
	int oldcx = cx;
        int oldcy = cy;

        cx = RTC_X;
        cy = RTC_Y;
        rtc_print_datetime();

        cx = oldcx;
        cy = oldcy;
	color = oldcolor;
}

void readline(char *buf, int max)
{
        int i = 0;
        char c;
        int last_sec = -1;
        int sec;
        int oldcx;
        int oldcy;

        for(;;)
        {
                sec = rtc_get_second();

		if(sec != last_sec)
		{
        		update_rtc_only();
        		last_sec = sec;
		}
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
	sleep(500);
        nosound();
	cy = 0;
	draw_topbar_once();
	cy = 1;
	cx = 0;

	print(logo);
	for(;;)
	{
		print("> ");
		color = 0x1A;

		readline(line, INPUT_MAX);

		color = 0x1F;
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
	shell();
}



