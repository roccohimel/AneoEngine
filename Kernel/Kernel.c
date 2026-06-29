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
extern void startupSeq(void);
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
extern int as_get_file_data(const char *path, char **data, int *size);
extern void pred(const char *s);
extern void beep(u32 freq);
extern void as_edit_save_screen(unsigned int *oldcx, unsigned int *oldcy, u8 *oldcolor);
extern void as_edit_restore_screen(unsigned int oldcx, unsigned int oldcy, u8 oldcolor);
extern int as_save_to_disk(void);
extern int as_load_from_disk(void);
extern void as_rm(char *name);
extern void as_rm_recursive(int node);
extern unsigned int saveit;
extern int as_cp(const char *src_path, const char *dst_path);
extern int as_mv(const char *src_path, const char *dst_path);
extern void tune(const char *song);
extern void triple_fault();

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
const char *VERSION = "V0.2.2";

unsigned int cx = 0;
unsigned int cy = 0;
unsigned int INPUT_MAX = 128;
unsigned int raw = 0;
unsigned int screen_saved = 0;

u8 color = 0x0F;
u8 defcolor = 0x1F;

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

void putc(char c);
void printx(uint32_t x);
void print(const char *s);
void printint(unsigned int n);

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

void run_script(const char *path);
void clear(void);

void putc(char c)
{//place character
	int oldcx;
	int oldcy;
	u8 oldcolor;

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

void printx(uint32_t x)
{//print a hexadecimal value
        char hex[] = "0123456789ABCDEF";

        putc('0');
	putc('x');

        for (int i = 28; i >= 0; i -= 4)
        {
                uint8_t digit = (x >> i) &  0xF;
                putc(hex[digit]);
        }
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
	print(" to easily get to the help menu.\n");
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

void ata_debug(void) {
        print("ATA status: \n");
        u16 port = 0x1F7;
	u8 s = inb(port);
        printx(port);
	print(" = ");
	printx(s);
	putc('\n');
	if (s == 0xFF)
                print("        Unknown ATA bus\n");
        else if (s == 0x00)
                print("        No ATA drive ready\n");
        else
                print("        ATA drive ready\n");
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

#define KEY_UP    1
#define KEY_DOWN  2
#define KEY_LEFT  3
#define KEY_RIGHT 4

int readline(char *buf, int max)
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
                        return 0;
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

		if(c == KEY_UP)
		{
			cy--;
			continue;
		}

		if(c == KEY_DOWN)
		{
			cy++;
			continue;
		}

		if(c == KEY_LEFT)
		{
			cx--;
			continue;
		}

		if(c == KEY_RIGHT)
		{
			cx++;
			continue;
		}

		if(c == 12)
		{
			clear();
			draw_tb();
			update_rtc_only();
			continue;
		}

		if(i < max - 1)
                {
                        buf[i] = c;
                        i++;
                        putc(c);
                }
        }
	return 0;
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

int ends(const char *s, const char *p)
{
	if (!s || !p)
		return 0;
	int s_len = strlen(s);
    	int p_len = strlen(p);
    	if (p_len > s_len)
		return 0;
    	int i = p_len - 1;
    	while (i >= 0)
	{
        	if (s[s_len - p_len + i] != p[i])
            		return 0;
        	i--;
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

void as_two_args(const char *s, char *a, char *b)
{
	int i = 0;
	int j = 0;

	while(*s == ' ')
		s++;

	while(*s && *s != ' ')
		a[i++] = *s++;

	a[i] = 0;

	while(*s == ' ')
		s++;

	while(*s && *s != ' ')
		b[j++] = *s++;

	b[j] = 0;
}

void rmsemiv(char *line)
{
        if (!line)
                return;

        int i = 0;
        int j = 0;

        while (line[i])
        {
                if (line[i] == ';')
                {
                        i += 3;
                }
                else
                {
                        line[j] = line[i];
                        j++;
                        i++;
                }
        }
        line[j] = '\0';
}


void rmsemi(char *line)
{
        if (!line)
                return;

        int i = 0;
        int j = 0;

        while (line[i])
        {
                if (line[i] == '\"' && line[i + 1] == ')' && line[i + 2] == ';')
                {
                        i += 3;
                }
                else
                {
                        line[j] = line[i];
                        j++;
                        i++;
                }
        }
        line[j] = '\0';
}

void rmsemia(char *line)
{
        if (!line)
                return;

        int i = 0;
        int j = 0;

        while (line[i])
        {
                if (line[i] == ')' && line[i + 1] == ';')
                {
                        i += 3;
                }
                else
                {
                        line[j] = line[i];
                        j++;
                        i++;
                }
        }
        line[j] = '\0';
}

u8 hexval(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';

	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;

	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	return 0;
}

u32 atox(const char *s)
{
	u32 value = 0;

	while(*s)
	{
		char c = *s++;

		if(c >= '0' && c <= '9')
			value = (value << 4) | (c - '0');
		else if(c >= 'a' && c <= 'f')
			value = (value << 4) | (c - 'a' + 10);
		else if(c >= 'A' && c <= 'F')
			value = (value << 4) | (c - 'A' + 10);
		else
			break;
	}

	return value;
}

u32 parsex(const char *s)
{
	if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		return atox(s + 2);

	return atoi(s);
}

#define CP_MAX_DRIVES 4
#define CP_NONE 0
#define CP_ATA 1
#define CP_ATAPI 2

typedef unsigned char CP_U8;
typedef unsigned short CP_U16;
typedef unsigned int CP_U32;
typedef unsigned long long CP_U64;

typedef struct {
	CP_U16 io;
	CP_U16 ctrl;
	CP_U8 slave;
	CP_U8 type;
	CP_U8 lba48;
	CP_U64 sectors;
	CP_U32 block_size;
	char model[41];
} CPDrive;

static CPDrive cp_drives[CP_MAX_DRIVES];
static CP_U16 cp_buffer[1024];

static inline CP_U8 cp_inb(CP_U16 port)
{
	CP_U8 value;

	asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static inline CP_U16 cp_inw(CP_U16 port)
{
	CP_U16 value;

	asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static inline void cp_outb(CP_U16 port, CP_U8 value)
{
	asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void cp_outw(CP_U16 port, CP_U16 value)
{
	asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void cp_delay(CPDrive *drive)
{
	cp_inb(drive->ctrl);
	cp_inb(drive->ctrl);
	cp_inb(drive->ctrl);
	cp_inb(drive->ctrl);
}

static int cp_wait_not_busy(CPDrive *drive)
{
	CP_U32 timeout = 10000000;

	while(timeout--)
		if(!(cp_inb(drive->io + 7) & 0x80))
			return 1;

	return 0;
}

static int cp_wait_drq(CPDrive *drive)
{
	CP_U32 timeout = 10000000;
	CP_U8 status;

	while(timeout--) {
		status = cp_inb(drive->io + 7);

		if(status & 0x21)
			return 0;

		if(!(status & 0x80) && (status & 0x08))
			return 1;
	}

	return 0;
}

static int cp_wait_done(CPDrive *drive)
{
	CP_U8 status;

	if(!cp_wait_not_busy(drive))
		return 0;

	status = cp_inb(drive->io + 7);

	if(status & 0x21)
		return 0;

	return 1;
}

static void cp_set_name(CPDrive *drive, const char *name)
{
	CP_U32 i = 0;

	while(name[i] && i < 40) {
		drive->model[i] = name[i];
		i++;
	}

	drive->model[i] = 0;
}

static void cp_model_from_identify(CPDrive *drive, CP_U16 *identify)
{
	CP_U32 i;
	CP_U32 end;

	for(i = 0; i < 20; i++) {
		drive->model[i * 2] = (char)(identify[27 + i] >> 8);
		drive->model[i * 2 + 1] =
			(char)(identify[27 + i] & 0xFF);
	}

	drive->model[40] = 0;
	end = 40;

	while(end &&
	      (drive->model[end - 1] == ' ' ||
	       drive->model[end - 1] == 0))
		end--;

	drive->model[end] = 0;
}

static int cp_atapi_packet_read(CPDrive *drive,
				CP_U8 *packet,
				CP_U32 bytes,
				CP_U8 *buffer)
{
	CP_U32 i;
	CP_U32 done = 0;
	CP_U32 count;
	CP_U32 words;
	CP_U16 value;

	if(!cp_wait_not_busy(drive))
		return 0;

	cp_outb(drive->io + 6,
		0xA0 | (drive->slave << 4));

	cp_delay(drive);

	cp_outb(drive->io + 1, 0);
	cp_outb(drive->io + 2, 0);
	cp_outb(drive->io + 3, 0);
	cp_outb(drive->io + 4,
		(CP_U8)(bytes & 0xFF));
	cp_outb(drive->io + 5,
		(CP_U8)((bytes >> 8) & 0xFF));
	cp_outb(drive->io + 7, 0xA0);

	if(!cp_wait_drq(drive))
		return 0;

	for(i = 0; i < 6; i++) {
		cp_outw(drive->io,
			(CP_U16)packet[i * 2] |
			((CP_U16)packet[i * 2 + 1] << 8));
	}

	while(done < bytes) {
		if(!cp_wait_drq(drive))
			return 0;

		count = (CP_U32)cp_inb(drive->io + 4);
		count |= (CP_U32)cp_inb(drive->io + 5) << 8;

		if(!count)
			return 0;

		words = (count + 1) >> 1;

		for(i = 0; i < words; i++) {
			value = cp_inw(drive->io);

			if(done + i * 2 < bytes) {
				buffer[done + i * 2] =
					(CP_U8)(value & 0xFF);
			}

			if(done + i * 2 + 1 < bytes) {
				buffer[done + i * 2 + 1] =
					(CP_U8)(value >> 8);
			}
		}

		done += count;
	}

	return cp_wait_done(drive);
}

static int cp_atapi_capacity(CPDrive *drive)
{
	CP_U8 packet[12];
	CP_U8 data[8];
	CP_U32 i;
	CP_U32 last_lba;
	CP_U32 block_size;

	for(i = 0; i < 12; i++)
		packet[i] = 0;

	packet[0] = 0x25;

	if(!cp_atapi_packet_read(drive,
				 packet,
				 8,
				 data))
		return 0;

	last_lba =
		((CP_U32)data[0] << 24) |
		((CP_U32)data[1] << 16) |
		((CP_U32)data[2] << 8) |
		(CP_U32)data[3];

	block_size =
		((CP_U32)data[4] << 24) |
		((CP_U32)data[5] << 16) |
		((CP_U32)data[6] << 8) |
		(CP_U32)data[7];

	drive->sectors = (CP_U64)last_lba + 1;
	drive->block_size = block_size;

	return 1;
}

static int cp_identify(CPDrive *drive)
{
	CP_U16 identify[256];
	CP_U32 i;
	CP_U8 mid;
	CP_U8 high;
	CP_U8 status;

	drive->type = CP_NONE;
	drive->lba48 = 0;
	drive->sectors = 0;
	drive->block_size = 0;
	drive->model[0] = 0;

	cp_outb(drive->io + 6,
		0xA0 | (drive->slave << 4));

	cp_delay(drive);

	cp_outb(drive->io + 2, 0);
	cp_outb(drive->io + 3, 0);
	cp_outb(drive->io + 4, 0);
	cp_outb(drive->io + 5, 0);
	cp_outb(drive->io + 7, 0xEC);

	cp_delay(drive);

	status = cp_inb(drive->io + 7);

	if(!status)
		return 0;

	if(!cp_wait_not_busy(drive))
		return 0;

	mid = cp_inb(drive->io + 4);
	high = cp_inb(drive->io + 5);

	if((mid == 0x14 && high == 0xEB) ||
	   (mid == 0x69 && high == 0x96)) {
		drive->type = CP_ATAPI;

		cp_outb(drive->io + 7, 0xA1);

		if(!cp_wait_drq(drive))
			return 0;
	} else {
		drive->type = CP_ATA;

		if(!cp_wait_drq(drive))
			return 0;
	}

	for(i = 0; i < 256; i++)
		identify[i] = cp_inw(drive->io);

	cp_model_from_identify(drive, identify);

	if(drive->type == CP_ATA) {
		drive->block_size = 512;

		if((identify[83] & (1 << 10)) &&
		   (identify[100] ||
		    identify[101] ||
		    identify[102] ||
		    identify[103])) {
			drive->lba48 = 1;

			drive->sectors =
				(CP_U64)identify[100] |
				((CP_U64)identify[101] << 16) |
				((CP_U64)identify[102] << 32) |
				((CP_U64)identify[103] << 48);
		} else {
			drive->sectors =
				(CP_U64)identify[60] |
				((CP_U64)identify[61] << 16);
		}

		if(!drive->model[0])
			cp_set_name(drive, "ATA Disk");

		return drive->sectors != 0;
	}

	if(!drive->model[0])
		cp_set_name(drive, "ATAPI CD-ROM");

	return cp_atapi_capacity(drive);
}

static int cp_scan_drives(void)
{
	CP_U32 i;
	CP_U16 ios[4] = {
		0x1F0,
		0x1F0,
		0x170,
		0x170
	};
	CP_U16 ctrls[4] = {
		0x3F6,
		0x3F6,
		0x376,
		0x376
	};
	CP_U8 slaves[4] = {
		0,
		1,
		0,
		1
	};
	int count = 0;

	for(i = 0; i < CP_MAX_DRIVES; i++) {
		cp_drives[i].io = ios[i];
		cp_drives[i].ctrl = ctrls[i];
		cp_drives[i].slave = slaves[i];

		if(cp_identify(&cp_drives[i]))
			count++;
	}

	return count;
}

static int cp_find_source(void)
{
	CP_U8 bios_drive =
		*((volatile CP_U8 *)0x0500);

	CP_U32 wanted;
	CP_U32 found;
	CP_U32 i;
	int first_ata = -1;
	int first_atapi = -1;

	for(i = 0; i < CP_MAX_DRIVES; i++) {
		if(cp_drives[i].type == CP_ATA &&
		   first_ata < 0)
			first_ata = (int)i;

		if(cp_drives[i].type == CP_ATAPI &&
		   first_atapi < 0)
			first_atapi = (int)i;
	}

	if(bios_drive < 0x80)
		return first_atapi;

	if(bios_drive >= 0xE0) {
		wanted = bios_drive - 0xE0;
		found = 0;

		for(i = 0; i < CP_MAX_DRIVES; i++) {
			if(cp_drives[i].type != CP_ATAPI)
				continue;

			if(found == wanted)
				return (int)i;

			found++;
		}

		return first_atapi;
	}

	wanted = bios_drive - 0x80;
	found = 0;

	for(i = 0; i < CP_MAX_DRIVES; i++) {
		if(cp_drives[i].type != CP_ATA)
			continue;

		if(found == wanted)
			return (int)i;

		found++;
	}

	return first_ata;
}

static int cp_ata_read(CPDrive *drive,
		       CP_U64 lba,
		       CP_U16 *buffer)
{
	CP_U32 i;

	if(!cp_wait_not_busy(drive))
		return 0;

	if(drive->lba48) {
		cp_outb(drive->io + 6,
			0x40 | (drive->slave << 4));

		cp_delay(drive);

		cp_outb(drive->io + 2, 0);
		cp_outb(drive->io + 3,
			(CP_U8)(lba >> 24));
		cp_outb(drive->io + 4,
			(CP_U8)(lba >> 32));
		cp_outb(drive->io + 5,
			(CP_U8)(lba >> 40));

		cp_outb(drive->io + 2, 1);
		cp_outb(drive->io + 3,
			(CP_U8)lba);
		cp_outb(drive->io + 4,
			(CP_U8)(lba >> 8));
		cp_outb(drive->io + 5,
			(CP_U8)(lba >> 16));

		cp_outb(drive->io + 7, 0x24);
	} else {
		if(lba > 0x0FFFFFFFULL)
			return 0;

		cp_outb(drive->io + 6,
			0xE0 |
			(drive->slave << 4) |
			((CP_U8)(lba >> 24) & 0x0F));

		cp_delay(drive);

		cp_outb(drive->io + 2, 1);
		cp_outb(drive->io + 3,
			(CP_U8)lba);
		cp_outb(drive->io + 4,
			(CP_U8)(lba >> 8));
		cp_outb(drive->io + 5,
			(CP_U8)(lba >> 16));

		cp_outb(drive->io + 7, 0x20);
	}

	if(!cp_wait_drq(drive))
		return 0;

	for(i = 0; i < 256; i++)
		buffer[i] = cp_inw(drive->io);

	return cp_wait_done(drive);
}

static int cp_ata_write(CPDrive *drive,
			CP_U64 lba,
			CP_U16 *buffer)
{
	CP_U32 i;

	if(!cp_wait_not_busy(drive))
		return 0;

	if(drive->lba48) {
		cp_outb(drive->io + 6,
			0x40 | (drive->slave << 4));

		cp_delay(drive);

		cp_outb(drive->io + 2, 0);
		cp_outb(drive->io + 3,
			(CP_U8)(lba >> 24));
		cp_outb(drive->io + 4,
			(CP_U8)(lba >> 32));
		cp_outb(drive->io + 5,
			(CP_U8)(lba >> 40));

		cp_outb(drive->io + 2, 1);
		cp_outb(drive->io + 3,
			(CP_U8)lba);
		cp_outb(drive->io + 4,
			(CP_U8)(lba >> 8));
		cp_outb(drive->io + 5,
			(CP_U8)(lba >> 16));

		cp_outb(drive->io + 7, 0x34);
	} else {
		if(lba > 0x0FFFFFFFULL)
			return 0;

		cp_outb(drive->io + 6,
			0xE0 |
			(drive->slave << 4) |
			((CP_U8)(lba >> 24) & 0x0F));

		cp_delay(drive);

		cp_outb(drive->io + 2, 1);
		cp_outb(drive->io + 3,
			(CP_U8)lba);
		cp_outb(drive->io + 4,
			(CP_U8)(lba >> 8));
		cp_outb(drive->io + 5,
			(CP_U8)(lba >> 16));

		cp_outb(drive->io + 7, 0x30);
	}

	if(!cp_wait_drq(drive))
		return 0;

	for(i = 0; i < 256; i++)
		cp_outw(drive->io, buffer[i]);

	return cp_wait_done(drive);
}

static int cp_ata_flush(CPDrive *drive)
{
	if(!cp_wait_not_busy(drive))
		return 0;

	cp_outb(drive->io + 6,
		0xE0 | (drive->slave << 4));

	cp_outb(drive->io + 7,
		drive->lba48 ? 0xEA : 0xE7);

	return cp_wait_done(drive);
}

static int cp_atapi_read(CPDrive *drive,
			 CP_U32 lba,
			 CP_U16 *buffer)
{
	CP_U8 packet[12];
	CP_U32 i;

	for(i = 0; i < 12; i++)
		packet[i] = 0;

	packet[0] = 0xA8;
	packet[2] = (CP_U8)(lba >> 24);
	packet[3] = (CP_U8)(lba >> 16);
	packet[4] = (CP_U8)(lba >> 8);
	packet[5] = (CP_U8)lba;
	packet[9] = 1;

	return cp_atapi_packet_read(drive,
				     packet,
				     2048,
				     (CP_U8 *)buffer);
}

static void cp_print_u32(CP_U32 value)
{
	char buffer[11];
	CP_U32 count = 0;

	if(!value) {
		putc('0');
		return;
	}

	while(value) {
		buffer[count++] =
			'0' + value % 10;
		value /= 10;
	}

	while(count)
		putc(buffer[--count]);
}

static CP_U32 cp_mib(CPDrive *drive)
{
	if(drive->block_size == 2048)
		return (CP_U32)(drive->sectors >> 9);

	return (CP_U32)(drive->sectors >> 11);
}

static void cp_print_location(CPDrive *drive)
{
	if(drive->io == 0x1F0)
		print("Primary ");
	else
		print("Secondary ");

	if(drive->slave)
		print("Slave");
	else
		print("Master");
}

static CP_U32 cp_read_line(char *buffer,
			   CP_U32 max)
{
	CP_U32 length = 0;
	int key;

	while(1) {
		key = getkey();

		if(key == '\r' || key == '\n') {
			putc('\n');
			buffer[length] = 0;
			return length;
		}

		if((key == '\b' || key == 127) &&
		   length) {
			length--;

			putc('\b');
			putc(' ');
			putc('\b');

			continue;
		}

		if(key >= 32 &&
		   key <= 126 &&
		   length + 1 < max) {
			buffer[length++] = (char)key;
			putc((char)key);
		}
	}
}

static CP_U32 cp_parse_number(char *buffer)
{
	CP_U32 value = 0;
	CP_U32 i = 0;

	while(buffer[i] >= '0' &&
	      buffer[i] <= '9') {
		value =
			value * 10 +
			(CP_U32)(buffer[i] - '0');

		i++;
	}

	if(buffer[i])
		return 0;

	return value;
}

static int cp_is_yes(char *buffer)
{
	return
		(buffer[0] == 'Y' ||
		 buffer[0] == 'y') &&
		(buffer[1] == 'E' ||
		 buffer[1] == 'e') &&
		(buffer[2] == 'S' ||
		 buffer[2] == 's') &&
		buffer[3] == 0;
}

static void cp_progress(CP_U32 mib)
{
	print("Copied ");
	cp_print_u32(mib);
	print(" MiB\n");
}

static int cp_copy_ata(CPDrive *source,
		       CPDrive *target)
{
	CP_U64 lba;

	for(lba = 0;
	    lba < source->sectors;
	    lba++) {
		if(!cp_ata_read(source,
				lba,
				cp_buffer)) {
			print("\nRead failed at sector ");
			cp_print_u32((CP_U32)lba);
			putc('\n');

			return 0;
		}

		if(!cp_ata_write(target,
				 lba,
				 cp_buffer)) {
			print("\nWrite failed at sector ");
			cp_print_u32((CP_U32)lba);
			putc('\n');

			return 0;
		}

		if(!(lba & 0x7FFF))
			cp_progress(
				(CP_U32)(lba >> 11));
	}

	return cp_ata_flush(target);
}

static int cp_copy_atapi(CPDrive *source,
			 CPDrive *target)
{
	CP_U64 lba;
	CP_U64 target_lba;
	CP_U32 part;

	for(lba = 0;
	    lba < source->sectors;
	    lba++) {
		if(!cp_atapi_read(source,
				  (CP_U32)lba,
				  cp_buffer)) {
			print("\nRead failed at CD sector ");
			cp_print_u32((CP_U32)lba);
			putc('\n');

			return 0;
		}

		target_lba = lba << 2;

		for(part = 0; part < 4; part++) {
			if(!cp_ata_write(
				   target,
				   target_lba + part,
				   cp_buffer + part * 256)) {
				print(
					"\nWrite failed at disk sector ");

				cp_print_u32(
					(CP_U32)(
						target_lba +
						part));

				putc('\n');

				return 0;
			}
		}

		if(!(lba & 0x1FFF))
			cp_progress(
				(CP_U32)(lba >> 9));
	}

	return cp_ata_flush(target);
}

void cpdrive(void)
{
	int source_index;
	int targets[CP_MAX_DRIVES];
	int target_count = 0;
	int target_index;
	CP_U32 i;
	CP_U32 choice;
	CP_U64 required_sectors;
	char input[16];
	CPDrive *source;
	CPDrive *target;

	print("\nScanning drives...\n");

	if(!cp_scan_drives()) {
		print("No IDE devices found.\n");
		return;
	}

	source_index = cp_find_source();

	if(source_index < 0) {
		print(
			"Could not map the BIOS boot drive "
			"to an IDE device.\n");

		return;
	}

	source = &cp_drives[source_index];

	if(source->type == CP_ATAPI &&
	   source->block_size != 2048) {
		print(
			"The boot CD does not use "
			"2048-byte logical sectors.\n");

		return;
	}

	print("\nSource: Current disk - ");
	print(source->model);
	print(" ");
	cp_print_u32(cp_mib(source));
	print(" MiB (");
	cp_print_location(source);
	print(")\n\n");

	for(i = 0; i < CP_MAX_DRIVES; i++) {
		if((int)i == source_index ||
		   cp_drives[i].type != CP_ATA)
			continue;

		targets[target_count] = (int)i;

		print("Device ");
		cp_print_u32(
			(CP_U32)target_count + 1);
		print(": ");
		print(cp_drives[i].model);
		print(" ");
		cp_print_u32(
			cp_mib(&cp_drives[i]));
		print(" MiB (");
		cp_print_location(&cp_drives[i]);
		print(")\n");

		target_count++;
	}

	if(!target_count) {
		print(
			"No writable destination "
			"disks found.\n");

		return;
	}

	print("\n>");
	cp_read_line(input, sizeof(input));

	choice = cp_parse_number(input);

	if(!choice ||
	   choice > (CP_U32)target_count) {
		print("Invalid device.\n");
		return;
	}

	target_index = targets[choice - 1];
	target = &cp_drives[target_index];

	if(source->type == CP_ATAPI)
		required_sectors =
			source->sectors << 2;
	else
		required_sectors =
			source->sectors;

	if(target->sectors < required_sectors) {
		print("Destination is too small.\n");
		return;
	}

	print("\nTHIS ERASES DEVICE ");
	cp_print_u32(choice);
	print(" COMPLETELY.\n");
	print("Type YES to continue: ");

	cp_read_line(input, sizeof(input));

	if(!cp_is_yes(input)) {
		print("Cancelled.\n");
		return;
	}

	print("\nInstalling AneoEngine to Device ");
	cp_print_u32(choice);
	print("\n");

	if(source->type == CP_ATAPI) {
		if(!cp_copy_atapi(source, target)) {
			print("Copy failed.\n");
			return;
		}
	} else {
		if(!cp_copy_ata(source, target)) {
			print("Copy failed.\n");
			return;
		}
	}

	cp_progress(cp_mib(source));
	print("\nAneoEngine media copy complete.\n");
}

u16 u16port = 0x0;
u16 u16val = 0x0;
u8 u8val = 0x0;


void shell_exec(char *line)
{
	char arg1[INPUT_MAX];
	char arg2[INPUT_MAX];

	color = 0x1F;
	if(strcmp(line, "fault;") == 0)
	{
		asm volatile("mov $0x1234, %ax");
		asm volatile("mov %ax, %ds");
	}
	else if(strcmp(line, "cls;") == 0)
		clear();
	else if(strcmp(line, "reboot;") == 0)
		triple_fault();
	else if(strcmp(line, "cpdrive;") == 0) {
                clear();
		cpdrive();
	}
	else if(strcmp(line, "help;") == 0)
		helpMenu();
	else if(starts(line, "color(") && ends(line, ");")) {
                rmsemia(line);
                color = atoi(skip(line + 5));
        }
	else if(starts(line, "u16port = ") && ends(line, ";")) {
                rmsemiv(line);
                u16port = parsex(line + 10);
        }
	else if(starts(line, "u16val = ") && ends(line, ";")) {
                rmsemiv(line);
                u16val = parsex(line + 9);
        }
	else if(starts(line, "u8val = ") && ends(line, ";")) {
                rmsemiv(line);
                u8val = parsex(line + 8);
        }
	else if(strcmp(line, "outb;") == 0)
                poutb(u16port, u8val);
	else if(strcmp(line, "outw;") == 0)
                poutw(u16port, u16val);
	else if(strcmp(line, "vmoff;") == 0)
		vmoff();
	else if(strcmp(line, "halt;") == 0)
		halt();
	else if(strcmp(line, "addr;") == 0)
		addr();
	else if(strcmp(line, "utils;") == 0)
		utilsMenu();
	else if(strcmp(line, "ls;") == 0)
		as_ls();
	else if(starts(line, "ls(\"") && ends(line, "\");")) {
		rmsemi(line);
		as_ls_path(line + 4);
	}
	else if(starts(line, "mkdir(\"") && ends(line, "\");")) {
		rmsemi(line);
		as_mkdir(line + 7);
	}
	else if(starts(line, "touch(\"") && ends(line, "\");")) {
		rmsemi(line);
		as_touch(line + 7);
	}
	else if(starts(line, "cat(\"") && ends(line, "\");")) {
		rmsemi(line);
		as_cat(line + 5);
	}
	else if(starts(line, "edit(\"") && ends(line, "\");")) {
		rmsemi(line);
		as_edit(line + 6);
	}
	else if(starts(line, "run(\"") && ends(line, "\");")) {
		rmsemi(line);
		run_script(line + 5);
	}
	else if(starts(line, "comment(\"") && ends(line, "\");")) {
                rmsemi(line);
		comment(line + 9);
	}
	else if(starts(line, "tune(\"") && ends(line, "\");")) {
                rmsemi(line);
		tune(line + 6);
	}
	else if(starts(line, "beep(") && ends(line, ");")) {
		rmsemia(line);
                beep(atoi(line + 5));
	}
	else if(starts(line, "sleep(") && ends(line, ");")) {
                rmsemia(line);
		sleep(atoi(line + 6));
	}
	else if(strcmp(line, "nosound;") == 0)
                nosound();
	else if(strcmp(line, "save;") == 0)
		as_save_to_disk();
	else if(strcmp(line, "load;") == 0)
		as_load_from_disk();
	else if(strcmp(line, "atadbg;") == 0)
		ata_debug();
	else if(starts(line, "rm(\"") && ends(line, "\");")) {
		rmsemi(line);
		as_rm(line + 4);
	}
	else if(strcmp(line, "saveity;") == 0)
		saveit = 1;
	else if(strcmp(line, "saveitn;") == 0)
                saveit = 0;
	else if(starts(line, "cp(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		as_two_args(line + 4, arg1, arg2);
		as_cp(arg1, arg2);
	}

	else if(starts(line, "mv(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		as_two_args(line + 4, arg1, arg2);
		as_mv(arg1, arg2);
	}
	else if(starts(line, "print(\"") && ends(line, "\");"))
	{
		u8 oldcolor = color;
		char *text;

		color = defcolor;
		rmsemi(line);
		text = line + 7;

		while(*text && *text != '"')
		{
			if(text[0] == '\\' && text[1] == 'n')
			{
				putc('\n');
				text += 2;
			}
			else if(text[0] == '\\' &&
				text[1] == 'c' &&
				text[2] == '[' &&
				text[3] == 'd' &&
				text[4] == 'c')
			{
				color = defcolor;
				text += 5;
			}
			else if(text[0] == '\\' &&
				text[1] == 'c' &&
				text[2] == '[' &&
				text[3] == '0' &&
				text[4] == 'x')
			{
				color = (hexval(text[5]) << 4) |
					hexval(text[6]);

				text += 7;
			}
			else
			{
				putc(*text);
				text++;
			}
		}

		color = oldcolor;
	}

	else if(starts(line, "cd(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		if(as_cd(line + 4) != 0)
			pred("Directory not found\n");
	}
	else if (line[0])
	{
                perror(line);
        }
}

void run_script(const char *path)
{
	char *data;
	int size;
	int i;
	int j;
	char line[128];

	if(as_get_file_data(path, &data, &size) != 0)
	{
		pred("File not found\n");
		return;
	}

	i = 0;
	while(i < size)
	{
		j = 0;

		while(i < size && data[i] != '\n' && j < 127)
			line[j++] = data[i++];

		line[j] = 0;

		if(data[i] == '\n')
			i++;

		trim_end(line);

		if(line[0] == 0)
			continue;

		if(line[0] == '/' && line[1] == '/')
			continue;

		shell_exec(line);
	}
}

int shell(void)
{//shell loop
	char line[INPUT_MAX];
	char *a;
	char *b;

	color = 0x1F;
	clear();
	draw_tb();
	nosound();

	draw_tb();
	update_rtc_only();
	print("run(\"/RunCmds.AC\");\n");
	run_script("/RunCmds.AC");
	shift = 0;
	ctrl = 0;
	ext = 0;
	for(;;)
	{
		shift = 0;
		ctrl = 0;
		ext = 0;
		draw_tb();
		if(raw == 0)
		{
			color = 0x1F;
			as_pwd();
			print(">");
			color = 0x1A;

		}
		color = 0x1A;
		readline(line, INPUT_MAX);
		color = 0x1F;
		if(raw == 0)
		{
			shell_exec(line);
		}
	}
}

void kmain(void)
{//main
	clear();
	startupSeq();
	/* ANCHORSAND SEED START */
/*	as_cd("/");
	as_mkdir("Docs");
	as_cd("Docs");
	as_touch("FAQ.TXT");
	as_write("FAQ.TXT", ">Why do you use your own license? Why not use the MIT License, Apache License,\n        or even the GNU GPL? Why would I use someone else's license when I\n        can build my own? After all, a project that is hand-crafted from\n        scratch should also have a license that is also hand-crafted from\n        scratch. I also think that yes, the GPL does already have what mine\n        does, but it feels better comming from my words, because they're mine.\n\n>Is it really built from scratch? You used GNU compilers, linkers, and\n        assemblers, that arent yours. Technically, you would be incorrect. The\n        operating system, including the boot loader and kernel, is built from\n        scratch. I used standard C and standard ASM. However, I never said I\n        made the build tools from scratch.\n\n>What operating system do you use daily?\n        Currently, I use Debian GNU/Linux 13.4 with the XFCE 4 desktop\n        enviornment. I run it both on my Intel Xeon E3-1230 v6 desktop, and my\n        AMD Ryzen 5 4650U laptop (Lenovo Thinkpad E14 Gen2)\n\n>Why do you use capital letters for your source file names?\n        Because thats what they start with, I give my kernel respect.");
	as_touch("LICENSE");
	as_write("LICENSE", "AneoEngine License V1.1\n\n28 May, 2026\nCopyright (C) 2026 Rocco Himel, All Rights Reserved.\nProject: https://roccohimel.github.io/AneoEngine/\n\nPermission is hereby granted to any person obtaining a copy\nof AneoEngine and its source code files (the \"Software\"),\nto use, study, modify, and distribute the Software,\nsubject to the following conditions:\n\n1. The original copyright notice and this license text\n   must remain included in all copies or substantial\n   portions of the Software.\n\n2. Modified versions of the Software must clearly state\n   that changes were made.\n\n3. Any redistributed version of the Software, modified\n   or unmodified, must include accessible source code.\n\n4. The name \"AneoEngine\" may not be used to falsely\n   represent modified versions as official releases.\n\n5. The Software is provided for educational,\n   experimental, and operating system development\n   purposes.\n\n6. THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY\n   OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT\n   LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n   FITNESS FOR A PARTICULAR PURPOSE, AND\n   NON-INFRINGEMENT.\n\n7. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS\n   BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER\n   LIABILITY ARISING FROM THE SOFTWARE OR THE USE OF\n   THE SOFTWARE.\n\n8. The contents of the /docs directory are excluded\n   from this license. No permission is granted to\n   copy, modify, distribute, mirror, republish, or\n   otherwise redistribute any files contained within\n   the /docs directory without prior written\n   permission from the copyright holder.\n\n9. You shall NOT call or classify your copy of the \n   Software as official.\n\n10. Any redistributed version of the Software must\n    remain licensed under the AneoEngine License\n    version under which it was received, or a later\n    official version of the AneoEngine License.\n\n11. You shall not intentionally submit, promote,\n    request indexing of, or otherwise cause any\n    redistributed or modified copy of the Software\n    to be indexed by search engines. Only the\n    official AneoEngine project may be intentionally\n    indexed.\n\n12. You shall not redistribute the Software under\n    any license other than the AneoEngine License\n    V1.1.");
	as_cd("..");
	as_mkdir("Everist");
	as_cd("Everist");
	as_mkdir("Install");
	as_cd("Install");
	as_touch("Main.AC");
	as_write("Main.AC", "cd(\"/\");\nprint(\"[*] Removing /RunCmds.AC\\n\");\nrm(\"RunCmds.AC\");\nprint(\"[*] Copying /Everist/Install/RunCmds.AC to /RunCmds.AC\\n\");\ncp(\"/Everist/Install/RunCmds.AC /RunCmds.AC\");\nsave;\nprint(\"[!] Done! Please reboot the machine.\\n\");\n");
	as_touch("RunCmds.AC");
	as_write("RunCmds.AC", "cd(\"/Home\");\nls;\nsaveity;");
	as_cd("..");
	as_mkdir("Music");
	as_cd("Music");
	as_touch("0_Note.TXT");
	as_write("0_Note.TXT", "All of these files can be played!\nJust press F4 followed by the song or tune\nyou want to play.");
	as_touch("1_Spark");
	as_write("1_Spark", "print(\"\\nDo \");\ntune(\"T120 C4e\");\nprint(\"I \");\ntune(\"T120 D4e\");\nprint(\"ha\");\ntune(\"T120 E4h\");\nprint(\"ve an \");\ntune(\"T120 D4e\"); \nprint(\"ide\");\ntune(\"T120 E4e G4q\");\nprint(\"a?\\n\");\ntune(\"T120 A4h\");\ntune(\"T120 Rq\");\nprint(\"I\");\ntune(\"T120 G4h\");\nprint(\"s i\");\ntune(\"T120 F4h\");\nprint(\"t a \");\ntune(\"T120 E4q\");\nprint(\"spa\");\ntune(\"T120 C4w\");\nprint(\"rk?\\n\");\n\ntune(\"T120 Rq\");\n\nprint(\"Do \");\ntune(\"T120 C4e\");\nprint(\"I \");\ntune(\"T120 D4e\");\nprint(\"ha\");\ntune(\"T120 E4h\");\nprint(\"ve an \");\ntune(\"T120 D4e\"); \nprint(\"ide\");\ntune(\"T120 E4e G4q\");\nprint(\"a?\\n\");\ntune(\"T120 A4h\");\ntune(\"T120 Rq\");\nprint(\"I\");\ntune(\"T120 E4h\");\nprint(\"t i\");\ntune(\"T120 G4h\");\nprint(\"s a \");\ntune(\"T120 A4q\");\nprint(\"spa\");\ntune(\"T120 C5w\");\nprint(\"rk!\\n\");\n\ntune(\"T120 Rq\");\n\nprint(\"Is \");\ntune(\"T120 E4e\");\nprint(\"it \");\ntune(\"T120 F4e\");\nprint(\"re\");\ntune(\"T120 G4q\"); \nprint(\"ady \");\ntune(\"T120 A4h\");\nprint(\"for \");\ntune(\"T120 G4e\");\nprint(\"cre\");\ntune(\"T120 F4e\");\nprint(\"a\");\ntune(\"T120 E4q\");\nprint(\"tion?\\n\");\ntune(\"T120 Rq G4q\");\nprint(\"Le\");\ntune(\"T120 A4h\");\nprint(\"t it \");\ntune(\"T120 C5w Rq\");\nprint(\"be \");\ntune(\"T120 C5h\");\nprint(\"do\");\ntune(\"T120 B4h\");\nprint(\"ne!\\n\");\ntune(\"T120 A4w\");\n\ntune(\"T120 Rq\");\n\nprint(\"Is \");\ntune(\"T120 E4e\");\nprint(\"it \");\ntune(\"T120 F4e\");\nprint(\"re\");\ntune(\"T120 G4q\"); \nprint(\"ady \");\ntune(\"T120 A4h\");\nprint(\"for \");\ntune(\"T120 G4e\");\nprint(\"cre\");\ntune(\"T120 Rq F4e\");\nprint(\"a\");\ntune(\"T120 E4q\");\nprint(\"tion?\\n\");\ntune(\"T120 G4q\");\nprint(\"Le\");\ntune(\"T120 A4h\");\nprint(\"t it \");\ntune(\"T120 C5w Rq\");\nprint(\"be \");\ntune(\"T120 C5h\");\nprint(\"do\");\ntune(\"T120 B4h\");\nprint(\"ne!\\n\");\ntune(\"T120 A4w\");\n\ntune(\"T120 Rq\");\n\nprint(\"Make \");\ntune(\"T120 E4e\");\nprint(\"it \");\ntune(\"T120 F4e\");\nprint(\"re\");\ntune(\"T120 G4q\"); \nprint(\"ady \");\ntune(\"T120 A4h\");\nprint(\"for \");\ntune(\"T120 G4e\");\nprint(\"cre\");\ntune(\"T120 F4e\");\nprint(\"a\");\ntune(\"T120 E4q\");\nprint(\"tion.\\n\");\ntune(\"T120 G4q Rq\");\nprint(\"It \");\ntune(\"T120 A4h\");\nprint(\"is \");\ntune(\"T120 C5w Rq\");\nprint(\"a \");\ntune(\"T120 A4h\");\nprint(\"spa\");\ntune(\"T120 B4h\");\nprint(\"r\");\ntune(\"T120 C5h\");\nprint(\"k!\\n\\n\");\ntune(\"T120 C5w\");\n\n\n\n\n");
	as_cd("..");
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
	as_write("Logo.TXT", "---------------------        AneoEngine V0.2.2\n---------------------        x86 Operating System\n---------------------\n---------------------        Creator: Rocco Himel\n--------------@@-----\n-------------@-@@----\n------------@--@@----\n-----------@---@@----\n----------@@@@@@@@---\n---------@------@@---\n-------@@@-----@@@@@-\n---------------------");
	as_cd("..");
	as_touch("RunCmds.AC");
	as_write("RunCmds.AC", "//AneoEngine run commands (kind of like .bashrc on Linux)\n\ncomment(\"cat /Misc/Logo.TXT\");\ncat(\"/Misc/Logo.TXT\");\ncd(\"/Home\");\ncomment(\"ls /Home\");\nls;\ncomment(\"Run '/Everist/Install/Main.AC' to install to current disk.\");");
	/* ANCHORSAND SEED END */
}



