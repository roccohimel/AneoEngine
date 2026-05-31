#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern void print(const char *s);
extern void printint(int n);
extern u8 color;
extern int getkey(void);
extern unsigned int cx;
extern unsigned int cy;
extern int shift;
extern void cursor_update(void);
extern void clear(void);

#define AS_MAX_NODES	64
#define AS_NAME_MAX	32
#define AS_DATA_MAX	4096

#define AS_FILE		1
#define AS_DIR		2

void pred(char *s)
{//shell error funtion
        const u8 oldcolor = color;
        color = 0xCF;
        print("ERROR:");
	color = oldcolor;
        print(" ");
	print(s);
}

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

	print("AS_MAX_NODES=");
	printint(AS_MAX_NODES);
	print("\nAS_NAME_MAX=");
	printint(AS_NAME_MAX);
	print("\nAS_DATA_MAX=");
	printint(AS_DATA_MAX);
	print("\nIf you have file listing issues, ajust these settings.\n\n");
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
		pred("File not found\n");
		return;
	}

	if(as_nodes[n].type != AS_FILE)
	{
		pred("Not a readable file\n");
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
		pred("Directory not found\n");
		return;
	}

	if(as_nodes[n].type != AS_DIR)
	{
		pred("Not a directory\n");
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

#define EDIT_KEY_UP    1
#define EDIT_KEY_DOWN  2
#define EDIT_KEY_LEFT  3
#define EDIT_KEY_RIGHT 4

#define EDIT_TOP 3
#define EDIT_W	 80
#define EDIT_H   50

#define VGA_TEXT ((u16*)0xB8000)

u16 edit_saved_vga[EDIT_W * EDIT_H];

void as_edit_save_screen(unsigned int *oldcx, unsigned int *oldcy, u8 *oldcolor)
{
	int i;

	*oldcx = cx;
	*oldcy = cy;
	*oldcolor = color;

	for(i = 0; i < EDIT_W * EDIT_H; i++)
		edit_saved_vga[i] = VGA_TEXT[i];
}

void as_edit_restore_screen(unsigned int oldcx, unsigned int oldcy, u8 oldcolor)
{
	int i;

	for(i = 0; i < EDIT_W * EDIT_H; i++)
		VGA_TEXT[i] = edit_saved_vga[i];

	cx = oldcx;
	cy = oldcy;
	color = oldcolor;
	cursor_update();
}

void as_edit_put_at(int x, int y, char c, u8 col)
{
	if(x < 0 || x >= EDIT_W)
		return;

	if(y < 0 || y >= EDIT_H)
		return;

	VGA_TEXT[y * EDIT_W + x] = (col << 8) | c;
}

void as_edit_clear_editor_area(void)
{
	int x;
	int y;

	for(y = EDIT_TOP; y < EDIT_H; y++)
	{
		for(x = 0; x < EDIT_W; x++)
			as_edit_put_at(x, y, ' ', color);
	}
}

int as_edit_line_start(int n, int pos)
{
	while(pos > 0 && as_nodes[n].data[pos - 1] != '\n')
		pos--;

	return pos;
}

int as_edit_line_end(int n, int pos)
{
	while(pos < as_nodes[n].size && as_nodes[n].data[pos] != '\n')
		pos++;

	return pos;
}

int as_edit_col(int n, int pos)
{
	return pos - as_edit_line_start(n, pos);
}

int as_edit_prev_line(int n, int pos)
{
	int start;
	int col;
	int prev_end;
	int prev_start;
	int prev_len;

	start = as_edit_line_start(n, pos);

	if(start == 0)
		return pos;

	col = pos - start;
	prev_end = start - 1;
	prev_start = as_edit_line_start(n, prev_end);
	prev_len = prev_end - prev_start;

	if(col > prev_len)
		col = prev_len;

	return prev_start + col;
}

int as_edit_next_line(int n, int pos)
{
	int col;
	int end;
	int next_start;
	int next_end;
	int next_len;

	col = as_edit_col(n, pos);
	end = as_edit_line_end(n, pos);

	if(end >= as_nodes[n].size)
		return pos;

	next_start = end + 1;
	next_end = as_edit_line_end(n, next_start);
	next_len = next_end - next_start;

	if(col > next_len)
		col = next_len;

	return next_start + col;
}

void as_edit_cursor(int n, int pos)
{
	int i;

	cx = 0;
	cy = EDIT_TOP;

	for(i = 0; i < pos; i++)
	{
		if(as_nodes[n].data[i] == '\n')
		{
			cx = 0;
			cy++;
		}
		else
		{
			cx++;

			if(cx >= EDIT_W)
			{
				cx = 0;
				cy++;
			}
		}

		if(cy >= EDIT_H)
		{
			cy = EDIT_H - 1;
			cx = EDIT_W - 1;
			break;
		}
	}

	cursor_update();
}

void as_edit_draw_header(const char *path)
{
	clear();

	cx = 0;
	cy = 1;

	print("EDITOR: ");
	print(path);
	print("\n\n");
}

void as_edit_redraw(int n, const char *path, int pos)
{
	int i;
	int x;
	int y;

	as_edit_draw_header(path);
	as_edit_clear_editor_area();

	x = 0;
	y = EDIT_TOP;

	for(i = 0; i < as_nodes[n].size; i++)
	{
		if(as_nodes[n].data[i] == '\n')
		{
			x = 0;
			y++;
		}
		else
		{
			as_edit_put_at(x, y, as_nodes[n].data[i], color);
			x++;

			if(x >= EDIT_W)
			{
				x = 0;
				y++;
			}
		}

		if(y >= EDIT_H)
			break;
	}

	as_edit_cursor(n, pos);
}

void as_edit_insert(int n, int *pos, int c)
{
	int i;

	if(as_nodes[n].size >= AS_DATA_MAX - 1)
		return;

	i = as_nodes[n].size;

	while(i >= *pos)
	{
		as_nodes[n].data[i + 1] = as_nodes[n].data[i];

		if(i == 0)
			break;

		i--;
	}

	as_nodes[n].data[*pos] = c;
	(*pos)++;
	as_nodes[n].size++;
	as_nodes[n].data[as_nodes[n].size] = 0;
}

void as_edit_backspace(int n, int *pos)
{
	int i;

	if(*pos <= 0)
		return;

	for(i = *pos - 1; i < as_nodes[n].size; i++)
		as_nodes[n].data[i] = as_nodes[n].data[i + 1];

	(*pos)--;
	as_nodes[n].size--;

	if(as_nodes[n].size < 0)
		as_nodes[n].size = 0;

	as_nodes[n].data[as_nodes[n].size] = 0;
}

void as_edit_restore_file(int n, char *olddata, int oldsize)
{
	int i;

	for(i = 0; i <= oldsize && i < AS_DATA_MAX; i++)
		as_nodes[n].data[i] = olddata[i];

	as_nodes[n].size = oldsize;
	as_nodes[n].data[as_nodes[n].size] = 0;
}

void as_edit(const char *path)
{
	char parent_path[AS_NAME_MAX * 4];
	char name[AS_NAME_MAX];

	char olddata[AS_DATA_MAX];
	int oldsize;

	int parent;
	int n;
	int i;
	int pos;
	int c;

	unsigned int oldcx;
	unsigned int oldcy;
	u8 oldcolor;

	as_edit_save_screen(&oldcx, &oldcy, &oldcolor);

	as_split_path(path, parent_path, name);
	parent = as_resolve(parent_path);

	if(parent == -1)
	{
		as_edit_restore_screen(oldcx, oldcy, oldcolor);
		pred("Directory not found\n");
		return;
	}

	if(as_nodes[parent].type != AS_DIR)
	{
		as_edit_restore_screen(oldcx, oldcy, oldcolor);
		pred("Not a directory\n");
		return;
	}

	n = as_find_child(parent, name);

	if(n == -1)
	{
		if(as_touch_at(parent, name) != 0)
		{
			as_edit_restore_screen(oldcx, oldcy, oldcolor);
			pred("Could not create file\n");
			return;
		}

		n = as_find_child(parent, name);
	}

	if(as_nodes[n].type != AS_FILE)
	{
		as_edit_restore_screen(oldcx, oldcy, oldcolor);
		pred("Not a readable file\n");
		return;
	}

	oldsize = as_nodes[n].size;

	for(i = 0; i <= oldsize && i < AS_DATA_MAX; i++)
		olddata[i] = as_nodes[n].data[i];

	pos = as_nodes[n].size;

	as_edit_redraw(n, path, pos);

	for(;;)
	{
		c = getkey();

		if(!c)
			continue;

		if(c == 19)
		{
			as_edit_restore_screen(oldcx, oldcy, oldcolor);
			return;
		}

		if(c == 16 && shift)
		{
			as_edit_restore_file(n, olddata, oldsize);
			as_edit_restore_screen(oldcx, oldcy, oldcolor);
			return;
		}

		if(c == EDIT_KEY_LEFT)
		{
			if(pos > 0)
				pos--;

			as_edit_cursor(n, pos);
			continue;
		}

		if(c == EDIT_KEY_RIGHT)
		{
			if(pos < as_nodes[n].size)
				pos++;

			as_edit_cursor(n, pos);
			continue;
		}

		if(c == EDIT_KEY_UP)
		{
			pos = as_edit_prev_line(n, pos);
			as_edit_cursor(n, pos);
			continue;
		}

		if(c == EDIT_KEY_DOWN)
		{
			pos = as_edit_next_line(n, pos);
			as_edit_cursor(n, pos);
			continue;
		}

		if(c == '\b')
		{
			as_edit_backspace(n, &pos);
			as_edit_redraw(n, path, pos);
			continue;
		}

		as_edit_insert(n, &pos, c);
		as_edit_redraw(n, path, pos);
	}
}
