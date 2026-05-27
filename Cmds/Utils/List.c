//utilities lister

#include <stdint.h>
extern void print(const char *s);

void utilsList(void)
{//print list of utilities
	char *utilsMsg =
		"Utilities:\n"
		">Entropy\n"
		">Printer\n";
	print(utilsMsg);
}
