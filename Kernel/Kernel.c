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
extern int getkey(void);
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
extern void as_ls_path(const char *path);
extern void idt_init();
extern void as_edit(const char *path);
extern int ctrl;
extern int ext;

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
#define GVN(var) #var

const char *BAR1 = "====";
const char *BAR2 = "===========================================================";

//build information
const char *VERSION = "V0.2.1";

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

void perror(char *line)
{//shell error funtion
	const u8 oldcolor = color;
	color = 0xCF;
	print("ERROR:");
	color = oldcolor;
	print(" Undefined refrence to \"");
	print(line);
	print("\" in line.\n");
	print("You can see ");
	color = 0x1C;
	print("/Help/CommandHelp.TXT");
	color = oldcolor;

	print(" for help on AneoEngine shell commands or you\ncan press ");
	color = 0x1C;
	print("F1");
	color = oldcolor;
	print(" to easily get to the help menu. You can also run ");
	color = 0x1C;
	print("help");
	color = oldcolor;
	print(" to get there as well.\n");
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

void printadl(const char *s, uint32_t x)
{//print a memory address on the same line
        print(s);
        print(":");
        printx(x);
	print(" ");
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
        int c;
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
	comment("cat /Misc/Logo.TXT");
	as_cat("/Misc/Logo.TXT");
	as_cd("/Home");
	comment("ls /Home");
	as_ls("");
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
		ctrl = 0;
		ext = 0;
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
		else if(strcmp(line, "cd") == 0)
                        as_cd("/Home");
		else if(starts(line, "ls "))
		{
			char *dir;

			dir = line + 3;
			trim_end(dir);

			as_ls_path(dir);
		}
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
		else if(starts(line, "edit "))
                        as_edit(line + 5);
		else if(strcmp(line, "fault") == 0)
		{
			__asm__ __volatile__(
				"mov $0x20, %%ax\n"
				"mov %%ax, %%ds"
				:
				:
				: "ax"
			);
		}
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
                        perror(line);
	}
}

void kmain(void)
{//main
	clear();
	startupBanner();
	idt_init();
	/* ANCHORSAND SEED START */
	as_cd("/");
	as_mkdir("Docs");
	as_cd("Docs");
	as_touch("FAQ.TXT");
	as_write("FAQ.TXT", ">Why do you use your own license? Why not use the MIT License, Apache License,\n        or even the GNU GPL? Why would I use someone else's license when I\n        can build my own? After all, a project that is hand-crafted from\n        scratch should also have a license that is also hand-crafted from\n        scratch. I also think that yes, the GPL does already have what mine\n        does, but it feels better comming from my words, because they're mine.\n\n>Is it really built from scratch? You used GNU compilers, linkers, and\n        assemblers, that arent yours. Technically, you would be incorrect. The\n        operating system, including the boot loader and kernel, is built from\n        scratch. I used standard C and standard ASM. However, I never said I\n        made the build tools from scratch.\n\n>What operating system do you use daily?\n        Currently, I use Debian GNU/Linux 13.4 with the XFCE 4 desktop\n        enviornment. I run it both on my Intel Xeon E3-1230 v6 desktop, and my\n        AMD Ryzen 5 4650U laptop (Lenovo Thinkpad E14 Gen2)\n\n>Why do you use capital letters for your source file names?\n        Because thats what they start with, I give my kernel respect.");
	as_touch("LICENSE");
	as_write("LICENSE", "AneoEngine License V1.1\n\n28 May, 2026\nCopyright (C) 2026 Rocco Himel, All Rights Reserved.\nProject: https://roccohimel.github.io/AneoEngine/\n\nPermission is hereby granted to any person obtaining a copy\nof AneoEngine and its source code files (the \"Software\"),\nto use, study, modify, and distribute the Software,\nsubject to the following conditions:\n\n1. The original copyright notice and this license text\n   must remain included in all copies or substantial\n   portions of the Software.\n\n2. Modified versions of the Software must clearly state\n   that changes were made.\n\n3. Any redistributed version of the Software, modified\n   or unmodified, must include accessible source code.\n\n4. The name \"AneoEngine\" may not be used to falsely\n   represent modified versions as official releases.\n\n5. The Software is provided for educational,\n   experimental, and operating system development\n   purposes.\n\n6. THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY\n   OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT\n   LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n   FITNESS FOR A PARTICULAR PURPOSE, AND\n   NON-INFRINGEMENT.\n\n7. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS\n   BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER\n   LIABILITY ARISING FROM THE SOFTWARE OR THE USE OF\n   THE SOFTWARE.\n\n8. The contents of the /docs directory are excluded\n   from this license. No permission is granted to\n   copy, modify, distribute, mirror, republish, or\n   otherwise redistribute any files contained within\n   the /docs directory without prior written\n   permission from the copyright holder.\n\n9. You shall NOT call or classify your copy of the \n   Software as official.\n\n10. Any redistributed version of the Software must\n    remain licensed under the AneoEngine License\n    version under which it was received, or a later\n    official version of the AneoEngine License.\n\n11. You shall not intentionally submit, promote,\n    request indexing of, or otherwise cause any\n    redistributed or modified copy of the Software\n    to be indexed by search engines. Only the\n    official AneoEngine project may be intentionally\n    indexed.\n\n12. You shall not redistribute the Software under\n    any license other than the AneoEngine License\n    V1.1.");
	as_cd("..");
	as_mkdir("Help");
	as_cd("Help");
	as_touch("AboutAneoEngine.TXT");
	as_write("AboutAneoEngine.TXT", "Official Website: https://roccohimel.github.io/AneoEngine\n\nCreator: Rocco Himel\n\nThe first language I ever learned was BASH. I was very young when my mom gave\nme her 2015 MacBook Pro running MacOS Mojave. I was very curious when I\nopened the terminal and I wanted to learn how to use what I didn't know was,\na shell. I then taught my self how to use it.\n\nThe first PROGRAMMING language I ever learned was C++. I was also younger at\nthe time, when I became a small indie developer making random games in Unity.\nI of course, only made them to mess around and just to have fun.\n\nI then of course learned C, standalone C, and x86 Assembly. I taught my self\nwith the OS Dev Wiki and other online forums\n\nI skipped 7th grade math and Pre-Algebra. Right now, I currently study Algebra\n1 and Physical Science. In my free time, I study electronics, more x86\nAssembly, and more C. Soon, I will study Geometry 1 and Biology.");
	as_touch("CommandHelp.TXT");
	as_write("CommandHelp.TXT", "addr:........................List all memory addresses used by AneoEngine\ncat /Directory/File.TXT:.....Prints the contents of a file\ncd:..........................Changes your directory to \"/Home\"\ncd /Directory:...............Manually changes your directory\ncls:.........................Clears your screen\ncpustat:.....................Shows system stats\nhalt:........................Stops the system\nls:..........................Lists the contents of the current directory\nls /Directory:...............Lists the contents of the selected directory\nmkdir /Directory/Name:.......Create a directory\ntouch........................Create a file\nvmoff:.......................Turns off your VM\nwrite........................Write text to a file");
	as_touch("Controls.TXT");
	as_write("Controls.TXT", " F1 -- Open the help menu\n F2 -- Reset the system\n F3 -- Open the utilities menu\n F4 -- Open the CpuStat menu");
	as_cd("..");
	as_mkdir("Home");
	as_cd("Home");
	as_touch("CHANGELOG");
	as_write("CHANGELOG", "AneoEngine V0.2.2 Change log\n- Added exception handling");
	as_touch("README");
	as_write("README", "                   The AneoEngine Operating System\n\nPlease read the LICENSE before software redistribution.\n\nYou can either burn a CD/DVD or flash a USB with the AneoEngine.ISO file.\nYou can also boot it via virtual machine as long as yours supports\nfloppy disc drives, which most of them do. I recommend QEMU since you\nare not installing anything, AneoEngine is a live image.\n\nAneoEngine is supported by any 32-bit CPU, any 64-bit CPU that supports\nLegacy BIOS boot will also run AneoEngine. ARM-64 and RISC-V are not\nsupported.\n\nAneoEngine requires ar least 512 megabytes of RAM.\n\nAneoEngine files are initialized via the official shell script which\nscans files from the \"Root\" folder in the AneoEngine source code,\nand makes AnchorSand commands of them and puts them into kmain. I know\nthis sounds inefficient, but it happens VERY fast. AnchorSand is a\ncustom filesystem that is used exclusively in AneoEngine and doesn't\nsupport FAT12 or FAT32, but is 100% open-source.");
	as_touch("Welcome.TXT");
	as_write("Welcome.TXT", "\nTo get started with AneoEngine, press F1.\n");
	as_cd("..");
	as_mkdir("Misc");
	as_cd("Misc");
	as_touch("Keymap.c");
	as_write("Keymap.c", "static const char keymap[128] =\n{//allowed chars\n        0, 27, '1', '2', '3', '4', '5', '6',\n        '7', '8', '9', '0', '-', '=', '\\b', '\\t',\n        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',\n        'o', 'p', '[', ']', '\\n', 0, 'a', 's',\n        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',\n        '\\'', '`', 0, '\\\\', 'z', 'x', 'c', 'v',\n        'b', 'n', 'm', ',', '.', '/', 0, '*',\n        0, ' ', 0\n};\n\nstatic const char shiftmap[128] =\n{//allowed chars when shift is pressed\n        0, 27, '!', '@', '#', '$', '%', '^',\n        '&', '*', '(', ')', '_', '+', '\\b', '\\t',\n        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',\n        'O', 'P', '{', '}', '\\n', 0, 'A', 'S',\n        'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',\n        '\"', '~', 0, '|', 'Z', 'X', 'C', 'V',\n        'B', 'N', 'M', '<', '>', '?', 0, '*',\n        0, ' ', 0\n};");
	as_touch("Logo.TXT");
	as_write("Logo.TXT", "---------------------        AneoEngine V0.2\n---------------------        x86 Operating System\n---------------------\n---------------------        Creator: Rocco Himel\n--------------@@-----\n-------------@-@@----\n------------@--@@----\n-----------@---@@----\n----------@@@@@@@@---\n---------@------@@---\n-------@@@-----@@@@@-\n---------------------");
	as_cd("..");
	/* ANCHORSAND SEED END */
	shell();
}



