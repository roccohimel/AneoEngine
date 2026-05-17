typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern unsigned int INPUT_MAX;
extern u8 color;
extern const char *BAR;
extern unsigned int cy;
extern unsigned int cx;
extern void print(const char *s);
extern void perror(const char *s);
extern void clear(void);
extern void readline(char *buf, int max);
extern int strcmp(const char *a, const char *b);

int helpMenu(void)
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

	color = 0x12;
	print("        AneoEngine Help Menu\n");
	color = 0x1F;
	print("\n");
	print("[1] AneoEngine information\n");
	print("[2] Shell commands\n");
	print("[3] How AneoEngine works\n");
	print("[4] Tweaking the kernel\n");
	for(;;)
	{

		const int oldcy = cy;
		cy = 0;
		print(BAR);
		cy = oldcy;
		cx = 0;

		print("Help> ");

		readline(line, INPUT_MAX);

		if(strcmp(line, "cls") == 0)
			clear();
		else if(strcmp(line, "exit") == 0)
		{
			print("Exiting...\n");
			return 0;
		}
		else if(line[0])
			perror("ERR: Unknown command\n");
	}
}
