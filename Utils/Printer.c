//repeats after you, basically
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern unsigned int INPUT_MAX;
extern u8 color;
extern void print(const char *s);
extern void perror(const char *s);
extern void clear(void);
extern void readline(char *buf, int max);
extern int strcmp(const char *a, const char *b);
extern void rtc_print_datetime(void);
extern void draw_tb(void);

int printer(void)
{

	char line[INPUT_MAX];
	char *a;
	char *b;

	print("Kernel loaded Utils/Printer\n");
	for(;;)
	{
		draw_tb();
		print("Printer> ");

		readline(line, INPUT_MAX);

		if(strcmp(line, "cls") == 0)
			clear();
		else if(strcmp(line, "exit") == 0)
		{
			print("Exiting...\n");
			return 0;
		} else {
			print(line);
			print("\n");
		}
	}
}
