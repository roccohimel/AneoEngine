#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern void print(const char *s);
extern void printint(int n);
extern u8 color;

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

int as_strlen(const char *s)
{
	int i = 0;

	while(s[i])
		i++;

	return i;
}

int as_namecmp(const char *a, const char *b)
{
	char ca;
	char cb;

	while(*a && *b)
	{
		ca = *a;
		cb = *b;

		if(ca >= 'A' && ca <= 'Z')
			ca += 32;

		if(cb >= 'A' && cb <= 'Z')
			cb += 32;

		if(ca < cb)
			return -1;

		if(ca > cb)
			return 1;

		a++;
		b++;
	}

	if(!*a && *b)
		return -1;

	if(*a && !*b)
		return 1;

	return 0;
}

void as_init()
{
	int i;

	print("AnchorSand init...\n\n");

	for(i = 0; i < AS_MAX_NODES; i++)
		as_nodes[i].used = 0;

	as_nodes[0].used = 1;
	as_nodes[0].type = AS_DIR;
	as_nodes[0].parent = 0;
	as_nodes[0].size = 0;
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
		if(
			as_nodes[i].used &&
			as_nodes[i].parent == parent &&
			as_streq(as_nodes[i].name, name)
		)
			return i;
	}

	return -1;
}

void as_split_path(const char *path, char *parent, char *name)
{
	int len;
	int i;
	int slash;

	parent[0] = 0;
	name[0] = 0;

	len = as_strlen(path);
	slash = -1;

	for(i = 0; i < len; i++)
	{
		if(path[i] == '/')
			slash = i;
	}

	if(slash == -1)
	{
		as_strcpy(parent, ".");
		as_strcpy(name, path);
		return;
	}

	if(slash == 0)
	{
		as_strcpy(parent, "/");
		as_strcpy(name, path + 1);
		return;
	}

	for(i = 0; i < slash; i++)
		parent[i] = path[i];

	parent[slash] = 0;
	as_strcpy(name, path + slash + 1);
}

int as_resolve(const char *path)
{
	int cur;
	int i;
	int j;
	int n;
	char part[AS_NAME_MAX];

	if(!path || !path[0])
		return as_cwd;

	if(path[0] == '/')
	{
		cur = 0;
		i = 1;
	}
	else
	{
		cur = as_cwd;
		i = 0;
	}

	while(1)
	{
		while(path[i] == '/')
			i++;

		if(!path[i])
			return cur;

		j = 0;

		while(path[i] && path[i] != '/' && j < AS_NAME_MAX - 1)
			part[j++] = path[i++];

		part[j] = 0;

		if(as_streq(part, "."))
			continue;

		if(as_streq(part, ".."))
		{
			if(cur != 0)
				cur = as_nodes[cur].parent;

			continue;
		}

		n = as_find_child(cur, part);

		if(n == -1)
			return -1;

		cur = n;
	}
}

int as_mkdir_at(int parent, const char *name)
{
	int n;

	if(!name[0])
		return -1;

	if(as_find_child(parent, name) != -1)
		return -1;

	n = as_alloc();

	if(n == -1)
		return -1;

	as_nodes[n].used = 1;
	as_nodes[n].type = AS_DIR;
	as_nodes[n].size = 0;
	as_nodes[n].parent = parent;
	as_strcpy(as_nodes[n].name, name);

	return 0;
}

int as_touch_at(int parent, const char *name)
{
	int n;

	if(!name[0])
		return -1;

	if(as_find_child(parent, name) != -1)
		return -1;

	n = as_alloc();

	if(n == -1)
		return -1;

	as_nodes[n].used = 1;
	as_nodes[n].type = AS_FILE;
	as_nodes[n].size = 0;
	as_nodes[n].parent = parent;
	as_strcpy(as_nodes[n].name, name);
	as_nodes[n].data[0] = 0;

	return 0;
}

int as_mkdir(const char *path)
{
	char parent_path[AS_NAME_MAX * 4];
	char name[AS_NAME_MAX];
	int parent;

	as_split_path(path, parent_path, name);

	parent = as_resolve(parent_path);

	if(parent == -1)
		return -1;

	if(as_nodes[parent].type != AS_DIR)
		return -1;

	return as_mkdir_at(parent, name);
}

int as_touch(const char *path)
{
	char parent_path[AS_NAME_MAX * 4];
	char name[AS_NAME_MAX];
	int parent;

	as_split_path(path, parent_path, name);

	parent = as_resolve(parent_path);

	if(parent == -1)
		return -1;

	if(as_nodes[parent].type != AS_DIR)
		return -1;

	return as_touch_at(parent, name);
}

int as_write(const char *path, const char *text)
{
	char parent_path[AS_NAME_MAX * 4];
	char name[AS_NAME_MAX];
	int parent;
	int n;
	int i;

	as_split_path(path, parent_path, name);

	parent = as_resolve(parent_path);

	if(parent == -1)
		return -1;

	if(as_nodes[parent].type != AS_DIR)
		return -1;

	n = as_find_child(parent, name);

	if(n == -1)
	{
		if(as_touch_at(parent, name) != 0)
			return -1;

		n = as_find_child(parent, name);
	}

	if(as_nodes[n].type != AS_FILE)
		return -1;

	for(i = 0; text[i] && i < AS_DATA_MAX - 1; i++)
		as_nodes[n].data[i] = text[i];

	as_nodes[n].data[i] = 0;
	as_nodes[n].size = i;

	return 0;
}

void as_cat(const char *path)
{
	int n;

	n = as_resolve(path);

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

void as_ls_node(int dir)
{
	int i;
	int j;
	int count = 0;
	int entries[AS_MAX_NODES];
	int temp;

	print("DIR..........: .\n");
	print("DIR..........: ..\n");

	for(i = 0; i < AS_MAX_NODES; i++)
	{
		if(
			as_nodes[i].used &&
			as_nodes[i].parent == dir &&
			i != dir
		)
		{
			entries[count++] = i;
		}
	}

	for(i = 0; i < count - 1; i++)
	{
		for(j = i + 1; j < count; j++)
		{
			int a = entries[i];
			int b = entries[j];
			int swap = 0;

			if(as_nodes[a].type != as_nodes[b].type)
			{
				if(as_nodes[a].type != AS_DIR)
					swap = 1;
			}
			else
			{
				if(as_namecmp(as_nodes[a].name, as_nodes[b].name) > 0)
					swap = 1;
			}

			if(swap)
			{
				temp = entries[i];
				entries[i] = entries[j];
				entries[j] = temp;
			}
		}
	}

	for(i = 0; i < count; i++)
	{
		int idx = entries[i];

		if(as_nodes[idx].type == AS_DIR)
		{
			print("DIR..........: ");
			print(as_nodes[idx].name);
		}
		else
		{
			u8 oldcolor;

			print("FILE.........: ");

			oldcolor = color;
			color = 0x1C;
			print(as_nodes[idx].name);
			color = oldcolor;
		}

		print("\n");
	}
}

void as_ls()
{
	as_ls_node(as_cwd);
}

void as_ls_path(const char *path)
{
	int n;

	n = as_resolve(path);

	if(n == -1)
	{
		print("path not found\n");
		return;
	}

	if(as_nodes[n].type != AS_DIR)
	{
		print("not a directory\n");
		return;
	}

	as_ls_node(n);
}

int as_cd(const char *path)
{
	int n;

	n = as_resolve(path);

	if(n == -1)
		return -1;

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
}
