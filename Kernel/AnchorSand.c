#include <stdint.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
extern void print(const char *s);
extern void printint(int n);

#define AS_MAX_NODES	64
#define AS_NAME_MAX	32
#define AS_DATA_MAX	4096

#define AS_FILE		1
#define AS_DIR		2

typedef struct
{
	char name[AS_NAME_MAX];
	char data[AS_DATA_MAX];
	int size;
	int type;
	int used;
	int parent;
} ASNode;

ASNode as_nodes[AS_MAX_NODES];
int as_cwd = 0;

void as_strcpy(char *d, const char *s)
{
	while(*s)
		*d++ = *s++;
	*d = 0;
}

int as_streq(const char *a, const char *b)
{
	while(*a && *b)
	{
		if(*a != *b)
			return 0;
		a++;
		b++;
	}
	return *a == *b;
}

void as_init()
{
	int i;

	for(i = 0; i < AS_MAX_NODES; i++)
		as_nodes[i].used = 0;

	as_nodes[0].used = 1;
	as_nodes[0].type = AS_DIR;
	as_nodes[0].parent = 0;
	as_strcpy(as_nodes[0].name, "/");
}

int as_alloc()
{
	int i;

	for(i = 1; i < AS_MAX_NODES; i++)
	{
		if(!as_nodes[i].used)
			return i;
	}

	return -1;
}

int as_find_child(int parent, const char *name)
{
	int i;

	for(i = 0; i < AS_MAX_NODES; i++)
	{
		if(as_nodes[i].used == 1 &&
		   as_nodes[i].parent == parent &&
		   as_streq(as_nodes[i].name, name))
			return i;
	}

	return -1;
}

int as_mkdir(const char *name) {
	int n;

	if(as_find_child(as_cwd, name) != -1)
		return -1;

	n = as_alloc();
	if(n == -1)
		return -1;

	as_nodes[n].used = 1;
	as_nodes[n].type = AS_DIR;
	as_nodes[n].size = 0;
	as_nodes[n].parent = as_cwd;
	as_strcpy(as_nodes[n].name, name);
	return 0;
}

int as_touch(const char *name)
{
	int n;

	if(as_find_child(as_cwd, name) != -1)
		return -1;

	n = as_alloc();
	if(n == -1)
		return -1;

	as_nodes[n].used = 1;
	as_nodes[n].type = AS_FILE;
	as_nodes[n].size = 0;
	as_nodes[n].parent = as_cwd;
	as_strcpy(as_nodes[n].name, name);
	as_nodes[n].data[0] = 0;

	return 0;
}

int as_write(const char *name, const char *text)
{
	int n;
	int i;

	n = as_find_child(as_cwd, name);

	if(n == -1)
	{
		if(as_touch(name) != 0)
			return -1;

		n = as_find_child(as_cwd, name);
	}

	if(as_nodes[n].type != AS_FILE)
		return -1;

	for(i = 0; text[i] && i < AS_DATA_MAX - 1; i++)
		as_nodes[n].data[i] = text[i];

	as_nodes[n].data[i] = 0;
	as_nodes[n].size = i;

	return 0;
}

void as_cat(const char *name)
{
	int n;

	n = as_find_child(as_cwd, name);

	if(n == -1)
	{
		print("file not found\n");
		return;
	}

	if(as_nodes[n].type != AS_FILE)
	{
		print("not a file\n");
		return;
	}

	print(as_nodes[n].data);
	print("\n");
}

void as_ls()
{
	int i;

	for(i = 0; i < AS_MAX_NODES; i++)
	{
		if(as_nodes[i].used && as_nodes[i].parent == as_cwd && i != as_cwd)
		{
			if(as_nodes[i].type == AS_DIR)
				print("Directory      ");
			else
				print("Regular File   ");

			print(as_nodes[i].name);
			print("\n");
		}
	}
}

int as_cd(const char *name)
{
	int n;

	print("cd arg=[");
	print(name);
	print("]\n");

	if(as_streq(name, "/"))
	{
		as_cwd = 0;
		return 0;
	}

	if(as_streq(name, ".."))
	{
		if(as_cwd != 0)
			as_cwd = as_nodes[as_cwd].parent;

		return 0;
	}

	n = as_find_child(as_cwd, name);

	print("find=");
	printint(n);
	print("\n");

	if(n == -1)
		return -1;

	print("type=");
	printint(as_nodes[n].type);
	print("\n");

	if(as_nodes[n].type != AS_DIR)
		return -1;

	as_cwd = n;
	return 0;
}

void as_pwd_rec(int n)
{
	if(n == 0)
	{
		print("/");
		return;
	}

	as_pwd_rec(as_nodes[n].parent);

	if(as_nodes[n].parent != 0)
		print("/");

	print(as_nodes[n].name);
}

void as_pwd()
{
	as_pwd_rec(as_cwd);
	print("\n");
}
