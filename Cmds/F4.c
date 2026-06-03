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

int F4(void)
{

	char line[INPUT_MAX];
	char *a;
	char *b;

	draw_tb();
	print("run ");
	readline(line, INPUT_MAX);
	if(strcmp(line, "exit") == 0)
	{
		print("Exiting...\n");
		return 0;
	}
	else
		run_script(line);
}
