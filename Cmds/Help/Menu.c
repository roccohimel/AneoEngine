//Help menu
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
extern void info(void);
extern void rtc_print_datetime(void);
extern void draw_tb(void);
extern void comment(const char *s);
extern void as_cat(const char *name);
extern void addr(void);

const char *CONTROLS = "/Help/Controls.TXT";
const char *ABT = "/Help/AboutAneoEngine.TXT";
const char *CMDS = "/Help/CommandHelp.TXT";
const char *KM = "/Misc/Keymap.c";
const char *FAQ = "/Docs/FAQ.TXT";
int helpMenu(void)
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
		print("Help> ");
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
		else if(strcmp(line, "Controls") == 0)
		{
			comment(CONTROLS);
			as_cat(CONTROLS);
		}
		else if(strcmp(line, "Abt") == 0)
                {
                        comment(ABT);
                        as_cat(ABT);
                }
		else if(strcmp(line, "Cmds") == 0)
                {
                        comment(CMDS);
                        as_cat(CMDS);
                }
		else if(strcmp(line, "KM") == 0)
                {
                        comment(KM);
			comment("This is a snippet from the AneoEngine");
			comment("source code, in Kernel/Keyboard.c. This");
			comment("cannot be found on this machine.");
                        as_cat(KM);
                }
		else if(strcmp(line, "Addr") == 0)
                {
			addr();
                }
		else if(strcmp(line, "FAQ") == 0)
                {
                        comment(FAQ);
			comment("Frequently Asked Questions");
                	as_cat(FAQ);
		}
		else if(line[0])
			perror(line);

	}

}

