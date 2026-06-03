//Command template
#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern unsigned int INPUT_MAX;
extern u8 color;
extern unsigned int cy;
extern unsigned int cx;
extern void print(const char *s);
extern void perror(const char *s);
extern void clear(void);
extern void readline(char *buf, int max);
extern int strcmp(const char *a, const char *b);
extern void draw_tb(void);
extern void run_script(const char *path);

int temp(void)
{

	char line[INPUT_MAX];
	char *a;
	char *b;

	color = 0x1F;
	u8 oldcolor = color;
	color = 0x1A;
	print("        AneoEngine Help Menu\n");
	color = 0x1D;
	print("\n");
	print("Controls and Shortcuts [Controls]\n");
	print("About AneoEngine [Abt]\n");
	print("Command Help [Cmds]\n");
	print("Keymap [KM]\n");
	print("Memory Addresses [Addr]\n");
	print("F.A.Q. [FAQ]\n");
	color = oldcolor;
	for(;;)
	{

		draw_tb();
		color = oldcolor;
		print("Help>");
		color = 0x1A;
		readline(line, INPUT_MAX);
		color = oldcolor;
		if(strcmp(line, "cls") == 0)
			clear();
		else if(strcmp(line, "exit") == 0)
		{
			print("Exiting...\n");
			return 0;
		}

		else if(line[0])
			perror(line);

	}

}
