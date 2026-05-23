typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern unsigned int INPUT_MAX;
extern u8 color;
extern const char *BAR1;
extern const char *BAR2;
extern unsigned int cy;
extern unsigned int cx;
extern void print(const char *s);
extern void perror(const char *s);
extern void clear(void);
extern void readline(char *buf, int max);
extern int strcmp(const char *a, const char *b);
extern void printer(void);
extern void rtc_print_datetime(void);
extern void draw_tb(void);

int programsMenu(void)
{

	char line[INPUT_MAX];
	char *a;
	char *b;

	print("Programs:\n");
	print("00001576  /Programs/Printer\n");

	for(;;)
	{

		draw_tb();
		print("Programs> ");

		readline(line, INPUT_MAX);

		if(strcmp(line, "cls") == 0)
			clear();
		else if(strcmp(line, "exit") == 0)
		{
			print("Exiting...\n");
			return 0;
		}
		else if(strcmp(line, "Printer") == 0)
			printer();
		else if(line[0])
			perror("ERR: Unknown command\n");
	}
}
