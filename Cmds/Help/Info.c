extern void print(const char *s);
extern const char *VERSION;
extern const char *BUILD;

void info(void)
{//print AneoEngine info

	print("AneoEngine x86 Operating System\n");
	print("\n");
	print("Version: ");
	print(VERSION);
	print("\n\n");
	print("Build: ");
	print(BUILD);
	print("\n\n");
	print("Creator: Rocco Himel\n\n");
	print("Official Website: https://roccohimel.github.io/AneoEngine\n");
}
