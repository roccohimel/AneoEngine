//File with the main AneoEngine loop and all core
//funtions
#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

//import all AneoEngine funtions
extern void pit_init_1000hz(void);
extern void sleep(unsigned int ms);
extern char getkey(void);
extern void vmoff(void);
extern void halt(void);
extern void startupBanner(void);
extern int helpMenu(void);
extern int utilsMenu(void);
extern void addr(void);
extern void nosound(void);
extern void as_init();
extern int as_mkdir(const char *name);
extern int as_touch(const char *name);
extern int as_write(const char *name, const char *text);
extern void as_cat(const char *name);
extern void as_ls();
extern int as_cd(const char *name);
extern int shift;
extern void as_pwd();

#define VGA ((u16*)0xB8000) //VGA buffer address
#define W 80 //screen width
#define H 50 //screen hight
#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71
#define RTC_SECONDS 0x00
#define RTC_MINUTES 0x02
#define RTC_HOURS   0x04
#define RTC_DAY     0x07
#define RTC_MONTH   0x08
#define RTC_YEAR    0x09

const char *BAR1 = "====";
const char *BAR2 = "===========================================================";

//build information
const char *VERSION = "V0.1";
const char *BUILD = "V0U1-270526B6";

unsigned int cx = 0;
unsigned int cy = 0;
unsigned int INPUT_MAX = 128;
u8 color = 0x0F;

volatile u64 idle_ticks=0;
volatile u64 total_ticks=1;

u32 mem_used=0;

u32 total_mem=
(
	512U*
	1024U*
	1024U
);

extern u32 heap_ptr;

void outb(u16 port, u8 val)
{//write 8-bit value to IO port
	asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

u8 inb(u16 port)
{//read 8-bit value from IO port
	u8 ret;
	asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void outw(u16 port, u16 val)
{//write 16-bit value to IO port
	asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

u8 cmos_read(u8 reg)
{//read CMOS data from 0x70
        outb(CMOS_ADDR, reg);
        return inb(CMOS_DATA);
}

u8 bcd_to_bin(u8 val)
{//BIN to decimal conversion
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
{//write certain RTC values to CMOS to get
//the date and time from RTC

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
{//common cursor updater
	u16 pos = cy * W + cx;

	outb(0x3D4, 0x0F);
	outb(0x3D5, pos & 0xFF);
	outb(0x3D4, 0x0E);
	outb(0x3D5, (pos >> 8) & 0xFF);
}

int strlen(const char *s)
{//return string length
	int i = 0;

	while(s[i])
		i++;

	return i;
}

int strcmp(const char *a, const char *b)
{//compare two strings
	int i = 0;

	while(a[i] && b[i] && a[i] == b[i])
		i++;

	return a[i] - b[i];
}

void strcpy(char *d, const char *s)
{//copy a string
	int i = 0;

	while(s[i])
	{
		d[i] = s[i];
		i++;
	}

	d[i] = 0;
}

void memset(void *p, int v, int n)
{//set a block value to one byte value
	u8 *b = (u8*)p;
	int i = 0;

	while(i < n)
	{
		b[i] = v;
		i++;
	}
}

void scroll(void)
{//scroll the screen
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
{//place character
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
{//simplified putc funtion that prints strings
	int i = 0;

	while(s[i])
	{
		putc(s[i]);
		i++;
	}
}

void print2(u8 n)
{//print an int with two digits
        putc('0' + (n / 10));
        putc('0' + (n % 10));
}

void rtc_print_datetime(void)
{//print RTC information
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
{//shell error funtion
	const u8 oldcolor = color;
	color = 0x4E;

	print(s);

	color = oldcolor;
}


void clear(void)
{//clear the screen
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
{//print a hexadecimal value
	char hex[] = "0123456789ABCDEF";

	print("0x");

	for (int i = 28; i >= 0; i -= 4)
	{
		uint8_t digit = (x >> i) &  0xF;
		putc(hex[digit]);
	}
}

void printint(unsigned int n)
{//print an integer
	char buf[16];
	int i = 0;

	if (n == 0)
	{
		print("0");
		return;
	}

	while (n)
	{
		buf[i++] = '0' + (n % 10);
		n /= 10;
	}

	while (i--)
		putc(buf[i]);
}

void printad(const char *s, uint32_t x)
{//print a memory address
	print(s);
	print(":");
	printx(x);
	putc('\n');
}

void comment(const char *s)
{//comment something
	u8 oldcolor = color;
	color = 0x1A;
	print("//");
	print(s);
	putc('\n');
	color = oldcolor;
}

void printadocu(const char *s, uint32_t x1, uint32_t x2)
{//print a memory address that occupies from one address
//to another one
        print(s);
	print(":&OCU:");
        printx(x1);
	putc('-');
	printx(x2);
	putc('\n');
}

void indprintad(const char *s, uint32_t x)
{//same as printad but with an indentation
        print("        ");
	print(s);
        print(":");
        printx(x);
        putc('\n');
}

void indprintadocu(const char *s, uint32_t x1, uint32_t x2)
{//same as printadocu but with an indentation
        print("        ");
	print(s);
        print(":&OCU:");
        printx(x1);
        putc('-');
        printx(x2);
        putc('\n');
}

void poutw(u16 port, u16 val)
{//outw with verbose
	outw(port, val);
	print("VAL:");
	printx(val);
	print("->IOP:");
	printx(port);
	putc('\n');
}


void poutb(u16 port, u8 val)
{//outb with verbose
        outb(port, val);
        print("VAL:");
        printx(val);
        print("->IOP:");
        printx(port);
        putc('\n');
}

void poutwfail(u16 port, u16 val)
{//outw failure message
	print("ERR: Failed to write value to IO port\n");
	print("        VAL:");
        printx(val);
        print("->IOP:");
        printx(port);
	putc('\n');
}

void poutbfail(u16 port, u8 val)
{//outb failure message
        print("ERR: Failed to write value to IO port\n");
        print("        VAL:");
        printx(val);
        print("->IOP:");
        printx(port);
        putc('\n');
}

int rtc_get_second(void)
{//get the current second from the RTC
        outb(0x70, 0x00);
        return inb(0x71);
}

#define RTC_X  4
#define RTC_Y  0


void update_rtc_only(void)
{//update RTC line
        u8 oldcolor = color;
	color = 0x0F;
	int oldcx = cx;
        int oldcy = cy;

        cx = RTC_X;
        cy = RTC_Y;
        rtc_print_datetime();

        cx = oldcx;
        cy = oldcy;
	color = oldcolor;
}

void draw_tb(void)
{//draw top bar
	u8 oldcolor = color;
	color = 0x01;

	int oldcx = cx;
	int oldcy = cy;

	cy = 0;
	cx = 0;
	print(BAR1);
	cx = 20;
	print(BAR2);

	cy = oldcy;
	cx = oldcx;
	color = oldcolor;
}

void readline(char *buf, int max)
{//read input
        int i = 0;
        char c;
        int last_sec = -1;
        int sec;
        int oldcx;
        int oldcy;

        for(;;)
        {//while waiting, update the RTC from the top bar
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

u32 cpuid_threads()
{
	u32 ebx;

	__asm__ volatile(
		"mov $1, %%eax\n"
		"cpuid"
		: "=b"(ebx)
		:
		: "eax",
		  "ecx",
		  "edx"
	);

	return
	(
		ebx
		>>
		16
	)
	&
	255U;
}


void cpustat(void)
{
	u32 threads;
	u32 used_mb;
	u32 total_mb;
	u32 i;
	u32 t;

	while(1)
	{
		color=0x0F;

		clear();

		threads=cpuid_threads();

		if(threads==0)
			threads=1;

		used_mb=mem_used>>20;

		total_mb=total_mem>>20;

		print(
			"USAGE                                       MEMORY:\n\n"
		);

		for(i=0;i<threads;i++)
		{
			print("CPU Thread ");

			printint(i);

			print(":                 ");

			print("0");

			print("%    ");

			printx(0);

			print("\n");
		}

		print("\nMemory:     ");

		printint(used_mb);

		print("MB/");

		printint(total_mb);

		print("MB    ");

		printx(mem_used);

		print("\n");

		for(t=0;t<100;t++)
		{
			if(getkey()==27)
			{
				color = 0x1F;
				clear();
				return;
			}

			sleep(10);
		}
	}
}

void trim_end(char *s)
{
	int i;

	i = 0;
	while(s[i])
		i++;

	while(i > 0 &&
	      (s[i - 1] == ' ' ||
	       s[i - 1] == '\n' ||
	       s[i - 1] == '\r' ||
	       s[i - 1] == '\t'))
	{
		s[i - 1] = 0;
		i--;
	}
}

void run_commands(void)
{//put your run commands here:
	as_cd("Misc");

	comment("`cd Misc, cat Logo.TXT`");
	as_cat("Logo.TXT");
	as_cd("/");
	comment("`ls`");
	as_ls("`ls`");
}

void shell(void)
{//shell loop
	char line[INPUT_MAX];
	char *a;
	char *b;

	color = 0x1F;
	clear();
	draw_tb();
	sleep(500);
        nosound();

	draw_tb();
	run_commands();
	for(;;)
	{
		shift = 0;
		draw_tb();
		as_pwd();
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
		else if(strcmp(line, "utils") == 0)
                        utilsMenu();
		else if(strcmp(line, "cpustat") == 0)
			cpustat();
		else if(strcmp(line, "ls") == 0)
			as_ls();
		else if(starts(line, "mkdir "))
		{
			char *dir;

			dir = line + 6;
			trim_end(dir);

			as_mkdir(dir);
		}
		else if(starts(line, "touch "))
			as_touch(line + 6);
		else if(starts(line, "cat "))
			as_cat(line + 4);
		else if(starts(line, "cd "))
		{
			char *dir;

			dir = line + 3;
			trim_end(dir);

			if(as_cd(dir) != 0)
				print("directory not found\n");
		}
		else if(starts(line, "write "))
		{
			char *file;
			char *text;
			int i;

			file = line + 6;

			for(i = 0; file[i]; i++)
			{
				if(file[i] == ' ')
				{
					file[i] = 0;
					text = file + i + 1;

					as_write(file, text);
					break;
				}
			}
		}
		else if(line[0])
                        perror("ERR: Unknown command\n");
	}
}

void kmain(void)
{//main
	clear();
	startupBanner();
	/* ANCHORSAND SEED START */
	as_cd("/");
	as_mkdir("Misc");
	as_cd("Misc");
	as_touch("Logo.TXT");
	as_write("Logo.TXT", "---------------------        AneoEngine V0.1\n---------------------        x86 Operating System\n---------------------\n---------------------        Creator: Rocco Himel\n--------------@@-----\n-------------@-@@----\n------------@--@@----\n-----------@---@@----\n----------@@@@@@@@---\n---------@------@@---\n-------@@@-----@@@@@-\n---------------------");
	as_cd("..");
	as_touch("CHANGELOG");
	as_write("CHANGELOG", "there");
	as_touch("LICENSE");
	as_write("LICENSE", "AneoEngine License v1.0\n\nCopyright (c) 2026 Rocco Himel\nProject: https://roccohimel.github.io/AneoEngine/\n\nPermission is hereby granted to any person obtaining a copy\nof AneoEngine and its source code files (the \"Software\"),\nto use, study, modify, and distribute the Software,\nsubject to the following conditions:\n\n1. The original copyright notice and this license text\n   must remain included in all copies or substantial\n   portions of the Software.\n\n2. Modified versions of the Software must clearly state\n   that changes were made.\n\n3. Any redistributed version of the Software, modified\n   or unmodified, must include accessible source code.\n\n4. The name \"AneoEngine\" may not be used to falsely\n   represent modified versions as official releases.\n\n5. The Software is provided for educational,\n   experimental, and operating system development\n   purposes.\n\n6. THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY\n   OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT\n   LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n   FITNESS FOR A PARTICULAR PURPOSE, AND\n   NON-INFRINGEMENT.\n\n7. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS\n   BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER\n   LIABILITY ARISING FROM THE SOFTWARE OR THE USE OF\n   THE SOFTWARE.\n\n8. You shall NOT publish or modify ANY Software in the\n   /docs folder of this project.");
	as_touch("README");
	as_write("README", "test\ntest");
	/* ANCHORSAND SEED END */
	shell();
}



