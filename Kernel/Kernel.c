//File with the main AneoEngine loop and all core
//funtions
#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

//import all AneoEngine funtions
extern void entropy(void);
extern void pit_init_1000hz(void);
extern u16 pit_read_counter(void);
extern void sleep(unsigned int ms);
extern int getkey(void);
extern void vmoff(void);
extern void halt(void);
extern void startupSeq(void);
extern int helpMenu(void);
extern void addr(void);
extern void nosound(void);
extern int hda_init(void);
extern int hda_ready(void);
extern void as_init();
extern int as_mkdir(const char *name);
extern int as_touch(const char *name);
extern int as_write(const char *name, const char *text);
extern void as_cat(const char *name);
extern void as_ls();
extern int as_cd(const char *name);
extern int as_cwd;
extern int shift;
extern void as_pwd();
extern void as_ls_path(const char *path);
extern void idt_init();
extern void as_edit(const char *path);
extern int as_edit_open_win(int id, const char *path);
extern int as_edit_key_win(int id, int c);
extern int as_edit_win_open(int id);
extern void as_edit_win_refresh(int id);
extern void as_edit_win_close(int id);
extern int as_edit_win_mem(int id);
extern const char *as_edit_win_path(int id);
extern int ctrl;
extern int ext;
extern int as_get_file_data(const char *path, char **data, int *size);
extern void pred(const char *s);
extern void beep(u32 freq);
extern void as_edit_save_screen(unsigned int *oldcx, unsigned int *oldcy, u8 *oldcolor);
extern void as_edit_restore_screen(unsigned int oldcx, unsigned int oldcy, u8 oldcolor);
extern int as_save_to_disk(void);
extern int as_load_from_disk(void);
extern void as_rm(char *name);
extern void as_rm_recursive(int node);
extern unsigned int saveit;
extern int as_cp(const char *src_path, const char *dst_path);
extern int as_mv(const char *src_path, const char *dst_path);
extern void tune(const char *song);
extern void triple_fault();
extern void wm_open_help_window(void);

#define GFX ((volatile u8*)0xA0000) //VGA 640x480 graphics buffer
#define FONT8X8 ((u8*)0x7000) //8x8 font copied by the bootloader
#define VGA_COPY40(dst, src) \
	do { \
		volatile u32 *_d40 = (volatile u32*)(dst); \
		volatile u32 *_s40 = (volatile u32*)(src); \
		_d40[0] = _s40[0]; \
		_d40[1] = _s40[1]; \
		_d40[2] = _s40[2]; \
		_d40[3] = _s40[3]; \
		_d40[4] = _s40[4]; \
		_d40[5] = _s40[5]; \
		_d40[6] = _s40[6]; \
		_d40[7] = _s40[7]; \
		_d40[8] = _s40[8]; \
		_d40[9] = _s40[9]; \
	} while(0)
#define W 80 //screen width
#define H 60 //screen hight
#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71
#define RTC_SECONDS 0x00
#define RTC_MINUTES 0x02
#define RTC_HOURS   0x04
#define RTC_DAY     0x07
#define RTC_MONTH   0x08
#define RTC_YEAR    0x09
#define GVN(var) #var

const char *BAR1 = "====";
const char *BAR2 = "===========================================================";

//build information
const char *VERSION = "V0.2.2";

unsigned int cx = 0;
unsigned int cy = 0;
unsigned int INPUT_MAX = 128;
unsigned int raw = 0;
unsigned int screen_saved = 0;

char screen_chars[W * H];
u8 screen_attrs[W * H];
unsigned int vga_start = 0;
int cursor_drawn = 0;
unsigned int cursor_x = 0;
unsigned int cursor_y = 0;

#define CON_FULL 0
#define CON_WINDOW 1
#define WM_WINDOWS 6
#define WM_INPUT_MAX 128
#define WM_TITLE_MAX 64
#define WM_CONTENT_W 78
#define WM_CONTENT_H 57
#define WM_BG_ATTR 0xF0
#define WM_ADD_NONE 0
#define WM_ADD_CARD 1
#define WM_ADD_SIDE 2
#define WM_CARD_W 40
#define WM_CARD_H 30
#define WM_SIDE_W 40
#define WM_SIDE_H 59
#define WM_PLUS_X 78
#define WM_MENU_X 50
#define WM_MENU_Y 1
#define WM_MENU_W 29
#define WM_MENU_H 5
#define WM_CLOSE_MARGIN 3
#define WM_TASK_STACK_MAX 8

typedef struct {
	int bx;
	int by;
	int bw;
	int bh;
	int x;
	int y;
	int w;
	int h;
	unsigned int cx;
	unsigned int cy;
	int cwd;
	int alive;
	int fullscreen;
	int carded;
	int card_old_bx;
	int card_old_by;
	int card_old_bw;
	int card_old_bh;
	int old_bx;
	int old_by;
	int old_bw;
	int old_bh;
	char title[WM_TITLE_MAX];
	char input[WM_INPUT_MAX];
	int input_len;
	int input_x;
	int input_y;
	char task_cmd[WM_INPUT_MAX];
	int task_pending;
	char *task_data;
	int task_size;
	int task_pos;
	int task_running;
	char *task_stack_data[WM_TASK_STACK_MAX];
	int task_stack_size[WM_TASK_STACK_MAX];
	int task_stack_pos[WM_TASK_STACK_MAX];
	int task_sp;
	u64 task_wake;
	char tune_song[WM_INPUT_MAX];
	int tune_pos;
	int tune_tempo;
	int tune_running;
	u64 tune_until;
	int tune_sounding;
} WMWindow;

WMWindow wm_windows[WM_WINDOWS];
int console_mode = CON_FULL;
int con_x = 0;
int con_y = 0;
int con_w = W;
int con_h = H;
int wm_on = 0;
int wm_active = 0;
int wm_redrawing = 0;
char wm_buf_chars[WM_WINDOWS][WM_CONTENT_W * WM_CONTENT_H];
u8 wm_buf_attrs[WM_WINDOWS][WM_CONTENT_W * WM_CONTENT_H];
int wm_selected_win = -1;
int wm_selected_row = -1;
int wm_selected_type = 0;
int wm_sel_x0 = 0;
int wm_sel_y0 = 0;
int wm_sel_x1 = 0;
int wm_sel_y1 = 0;
char wm_selected_cmd[WM_INPUT_MAX];
int wm_drag_win = -1;
int wm_drag_dx = 0;
int wm_drag_dy = 0;
int wm_add_menu = 0;
int wm_next_side = 0;
int wm_next_card_x = 0;
int wm_next_card_y = 1;
int wm_mouse_select_win = -1;
int wm_z_order[WM_WINDOWS];
int wm_z_count = 0;
int wm_shift_selecting = 0;
int wm_shift_ax = 0;
int wm_shift_ay = 0;
int wm_task_exec_win = -1;
int wm_in_task_step = 0;
int wm_help_win = -1;
int wm_task_focus_win = -1;

#define MEMSTAT_OVERLAY_X 0
#define MEMSTAT_OVERLAY_W W
#define MEMSTAT_OVERLAY_H (H - 1)

char memstat_overlay_chars[MEMSTAT_OVERLAY_H][MEMSTAT_OVERLAY_W];
u8 memstat_overlay_attrs[MEMSTAT_OVERLAY_H][MEMSTAT_OVERLAY_W];
int memstat_overlay_ready = 0;
int memstat_overlay_y = H;
int memstat_overlay_rows = 0;

int mouse_ready = 0;
int mouse_x = 320;
int mouse_y = 240;
int mouse_drawn = 0;
int mouse_old_x = 0;
int mouse_old_y = 0;
int mouse_left = 0;
int mouse_prev_left = 0;
int mouse_packet_index = 0;
u8 mouse_packet[3];
#define MOUSE_SPEED 1
#define MOUSE_W 11
#define MOUSE_H 16

u8 color = 0x0F;
u8 defcolor = 0x1F;

volatile u64 idle_ticks=0;
volatile u64 total_ticks=1;

u32 mem_used=0;

u32 total_mem=
(
	512U*
	1024U*
	1024U
);

extern u32 heap_ptr;

void putc(char c);
void clear(void);
void printx(uint32_t x);
void print(const char *s);
void printint(unsigned int n);
void screen_draw_cell(int x, int y, char c, u8 col, int cursor);
void screen_redraw(void);
void cursor_hide(void);
void cursor_update(void);
void draw_tb(void);
void update_rtc_only(void);
void screen_put_at(int x, int y, char c, u8 col);
void screen_write_at(int x, int y, const char *s, u8 col);
void screen_set_cell(int i, char c, u8 col);
void screen_get_cell(int i, char *c, u8 *col);
int con_abs_x(void);
int con_abs_y(void);
void con_set_full(void);
void con_set_window(int x, int y, int w, int h);
void con_save(WMWindow *win);
void con_load(WMWindow *win);
void wm(void);
void wm_draw(void);
void wm_prompt(void);
void wm_switch(void);
void wm_set_title(int id, const char *s);
void gfx_clear_text_rect(int x, int y, int w, int h, u8 col);
void gfx_scroll_text_rect_up_8(int x, int y, int w, int h, u8 col);
void gfx_scroll_wm_text_rect_up_8(int x, int y, int w, int h, u8 col);
void wm_draw_one(int id);
void wm_clear_background(void);
void wm_redraw_all(void);
void wm_capture(int id);
void wm_capture_window(WMWindow *win);
void wm_store_cell(int x, int y, char c, u8 col);
void wm_draw_content(int id);
void wm_draw_content_region(int id, int rx0, int ry0, int rx1, int ry1);
void wm_draw_one_region(int id, int rx0, int ry0, int rx1, int ry1);
void wm_redraw_region(int rx0, int ry0, int rx1, int ry1);
void wm_redraw_window_move(int id, int obx, int oby);
void wm_redraw_selection_change(int oldwin, int oldtype, int oldrow, int oldx0, int oldy0, int oldx1, int oldy1, int newwin, int newtype, int newrow, int newx0, int newy0, int newx1, int newy1);
int wm_cell_in_selection(int type, int row, int x0, int y0, int x1, int y1, int x, int y);
void wm_finish_selected_cmd(int id);
void wm_extract_line_command(int id, int row, char *out);
void wm_init_buffers(void);
void wm_activate(int id);
int wm_hit_window(int tx, int ty);
void wm_select_line(int id, int row);
void wm_select_range(int id, int x0, int y0, int x1, int y1);
void wm_clear_highlight(void);
int wm_cell_selected(int id, int x, int y);
void wm_extract_range_command(int id, char *out);
int wm_alive_count(void);
int wm_next_alive_from(int from);
void wm_apply_geometry(int id, int bx, int by, int bw, int bh);
void wm_blank_window_buffer(int id);
void wm_init_new_window(int id, int bx, int by, int bw, int bh);
int wm_create_window(int kind);
void wm_start_window(int id);
void wm_start_shell_window(int id);
void wm_close_window(int id);
void wm_toggle_fullscreen(int id);
void wm_toggle_card(int id);
void wm_z_remove(int id);
void wm_z_add_top(int id);
void wm_bring_front(int id);
int wm_active_ok(void);
void wm_draw_top_controls(void);
void wm_draw_add_menu(void);
void wm_hide_add_menu(void);
int wm_handle_add_menu_click(int tx, int ty);
void wm_prepare_selected_run_line(const char *cmd);
void wm_sync_input_from_screen(int id);
void wm_insert_char_here(int id, char c);
void wm_backspace_here(int id);
int wm_cursor_on_prompt_line(int id);
void wm_open_help_window(void);
void wm_compose_cell(int sx, int sy, char *outc, u8 *outcol);
int wm_cell_visible_for(int id, int sx, int sy);
void wm_enqueue_cmd(int id, const char *cmd);
void wm_task_start_script(int id, const char *path);
void wm_task_step(int id);
int wm_task_run_line(int id, char *line);
void wm_task_clean_path(const char *src, char *dst);
int wm_task_try_file(int id, const char *line);
void wm_extract_quoted_arg(const char *line, char *out);
void wm_tasks_step(void);
void ticks_poll(void);
void memstat(void);
void memstat_overlay_refresh(int force);
int memstat_overlay_cell(int x, int y);
void memstat_overlay_compose(int sx, int sy, char *outc, u8 *outcol);
u32 memstat_window_bytes(int id);
void wm_tune_start(int id, const char *song);
int wm_tune_step(int id);
int wm_tune_freq(char note, int accidental, int octave);
int wm_tune_duration_ms(int tempo, char dur);
int wm_is_wm_command(const char *line);
void wm_handle_key(int c);
void wm_mouse_poll(void);
void mouse_init(void);
void ps2_flush_output(void);
void kb_drain_mouse(void);
void mouse_show(void);
void mouse_hide(void);
int mouse_cell_hit(int x, int y);
void gfx_pixel(int px, int py, u8 col);
u8 screen_pixel_color(int px, int py);
void gfx_set_start(unsigned int off);
void wm_setup(void);
void wm_start_window(int id);
int rtc_get_second(void);
void wm_switch(void);
void wm_redraw_region(int rx0, int ry0, int rx1, int ry1);
void mouse_hide(void);
void mouse_show(void);
int mouse_cell_hit(int x, int y);

void outb(u16 port, u8 val)
{//write 8-bit value to IO port
	asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

u8 inb(u16 port)
{//read 8-bit value from IO port
	u8 ret;
	asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void outw(u16 port, u16 val)
{//write 16-bit value to IO port
	asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

void outl(u16 port, u32 val)
{//write 32-bit value to IO port
	asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

u32 inl(u16 port)
{//read 32-bit value from IO port
	u32 ret;
	asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void vga_stosl(void *dst, u32 val, unsigned int count)
{//fast VGA fill using 32-bit repeat stores
	asm volatile("cld; rep stosl"
		: "+D"(dst), "+c"(count)
		: "a"(val)
		: "memory");
}

void vga_movsl(void *dst, const void *src, unsigned int count)
{//fast VGA copy using 32-bit repeat moves
	asm volatile("cld; rep movsl"
		: "+D"(dst), "+S"(src), "+c"(count)
		:
		: "memory");
}

void fast_memset8(void *dst, u8 val, unsigned int bytes)
{
	u32 fill;
	u8 *tail;
	unsigned int rem;

	fill = (u32)val;
	fill |= fill << 8;
	fill |= fill << 16;

	vga_stosl(dst, fill, bytes / 4);
	tail = (u8*)dst + (bytes & ~3U);
	rem = bytes & 3U;
	while(rem--)
		*tail++ = val;
}

void fast_memcpy8(void *dst, const void *src, unsigned int bytes)
{
	u8 *d;
	const u8 *s;
	unsigned int rem;

	vga_movsl(dst, src, bytes / 4);
	d = (u8*)dst + (bytes & ~3U);
	s = (const u8*)src + (bytes & ~3U);
	rem = bytes & 3U;
	while(rem--)
		*d++ = *s++;
}

unsigned int gfx_off(unsigned int off)
{
	return off & 0xFFFF;
}

void gfx_set_start(unsigned int off)
{
	vga_start = off & 0xFFFF;
	outb(0x3D4, 0x0C);
	outb(0x3D5, (u8)(vga_start >> 8));
	outb(0x3D4, 0x0D);
	outb(0x3D5, (u8)vga_start);
}

void gfx_ready(void)
{
	outb(0x3C4, 0x02);
	outb(0x3C5, 0x0F);
	outb(0x3C4, 0x04);
	outb(0x3C5, 0x06);
	outb(0x3CE, 0x00);
	outb(0x3CF, 0x00);
	outb(0x3CE, 0x01);
	outb(0x3CF, 0x00);
	outb(0x3CE, 0x03);
	outb(0x3CF, 0x00);
	outb(0x3CE, 0x05);
	outb(0x3CF, 0x02);
	outb(0x3CE, 0x06);
	outb(0x3CF, 0x05);
	outb(0x3CE, 0x07);
	outb(0x3CF, 0x0F);
	outb(0x3CE, 0x08);
	outb(0x3CF, 0xFF);
}

void gfx_write_masked(unsigned int off, u8 mask, u8 col)
{
	volatile u8 junk;

	if(mask == 0)
		return;

	outb(0x3CE, 0x08);
	outb(0x3CF, mask);
	junk = GFX[off];
	(void)junk;
	GFX[off] = col & 0x0F;
}

void gfx_direct_plane(unsigned int plane)
{
	outb(0x3C4, 0x02);
	outb(0x3C5, 1 << plane);
	outb(0x3C4, 0x04);
	outb(0x3C5, 0x06);
	outb(0x3CE, 0x00);
	outb(0x3CF, 0x00);
	outb(0x3CE, 0x01);
	outb(0x3CF, 0x00);
	outb(0x3CE, 0x03);
	outb(0x3CF, 0x00);
	outb(0x3CE, 0x04);
	outb(0x3CF, plane);
	outb(0x3CE, 0x05);
	outb(0x3CF, 0x00);
	outb(0x3CE, 0x06);
	outb(0x3CF, 0x05);
	outb(0x3CE, 0x08);
	outb(0x3CF, 0xFF);
}

void gfx_mode2(void)
{
	outb(0x3C4, 0x02);
	outb(0x3C5, 0x0F);
	outb(0x3C4, 0x04);
	outb(0x3C5, 0x06);
	outb(0x3CE, 0x00);
	outb(0x3CF, 0x00);
	outb(0x3CE, 0x01);
	outb(0x3CF, 0x00);
	outb(0x3CE, 0x03);
	outb(0x3CF, 0x00);
	outb(0x3CE, 0x05);
	outb(0x3CF, 0x02);
	outb(0x3CE, 0x06);
	outb(0x3CF, 0x05);
	outb(0x3CE, 0x07);
	outb(0x3CF, 0x0F);
	outb(0x3CE, 0x08);
	outb(0x3CF, 0xFF);
}

void gfx_fill_linear(unsigned int off, unsigned int bytes, u8 col)
{
	u32 val;
	volatile u8 *tail;
	unsigned int rem;

	if(bytes == 0)
		return;

	off &= 0xFFFF;
	gfx_mode2();
	val = (u32)(col & 0x0F);
	val |= val << 8;
	val |= val << 16;
	vga_stosl((void*)(GFX + off), val, bytes / 4);
	tail = GFX + off + (bytes & ~3U);
	rem = bytes & 3U;
	while(rem--)
		*tail++ = col & 0x0F;
}

void gfx_fill_span(unsigned int off, unsigned int bytes, u8 col)
{
	unsigned int first;

	off &= 0xFFFF;
	while(bytes)
	{
		first = 0x10000 - off;
		if(first > bytes)
			first = bytes;
		gfx_fill_linear(off, first, col);
		bytes -= first;
		off = 0;
	}
}

void gfx_fill(u8 col)
{
	gfx_set_start(0);
	gfx_fill_span(0, 80 * 480, col);
}

void gfx_clear_text_row(int row, u8 col)
{
	unsigned int line;

	if(row < 0 || row >= H)
		return;

	for(line = 0; line < 8; line++)
		gfx_fill_span(vga_start + row * 8 * 80 + line * 80, 80, col);
}

void gfx_clear_text_rect(int x, int y, int w, int h, u8 col)
{
	int py;
	int lines;
	u32 val;
	volatile u8 *tail;
	unsigned int off;
	unsigned int rem;

	if(w <= 0 || h <= 0)
		return;

	if(x < 0 || y < 0 || x + w > W || y + h > H)
		return;

	gfx_mode2();
	val = (u32)(col & 0x0F);
	val |= val << 8;
	val |= val << 16;

	lines = h * 8;
	for(py = 0; py < lines; py++)
	{
		off = gfx_off(vga_start + ((y * 8 + py) * 80) + x);
		if(off + (unsigned int)w > 0x10000)
		{
			gfx_fill_span(vga_start + ((y * 8 + py) * 80) + x, w, col);
			gfx_mode2();
			continue;
		}

		vga_stosl((void*)(GFX + off), val, ((unsigned int)w) / 4);
		tail = GFX + off + (((unsigned int)w) & ~3U);
		rem = ((unsigned int)w) & 3U;
		while(rem--)
			*tail++ = col & 0x0F;
	}
}

void gfx_scroll_text_rect_up_8(int x, int y, int w, int h, u8 col)
{
	int plane;
	int py;
	int lines;
	unsigned int dst;
	unsigned int src;

	if(w <= 0 || h <= 1)
		return;

	if(x < 0 || y < 0 || x + w > W || y + h > H)
		return;

	lines = (h - 1) * 8;
	for(plane = 0; plane < 4; plane++)
	{
		gfx_direct_plane(plane);
		for(py = 0; py < lines; py++)
		{
			dst = gfx_off(vga_start + ((y * 8 + py) * 80) + x);
			src = gfx_off(dst + 8 * 80);
			fast_memcpy8((void*)(GFX + dst), (const void*)(GFX + src), w);
		}
	}

	gfx_clear_text_rect(x, y + h - 1, w, 1, col);
	gfx_ready();
}

void gfx_scroll_wm_text_rect_up_8(int x, int y, int w, int h, u8 col)
{
	int plane;
	int py;
	int lines;
	unsigned int dst;
	unsigned int src;

	if(w != 40 || (x & 3))
	{
		gfx_scroll_text_rect_up_8(x, y, w, h, col);
		return;
	}

	if(h <= 1)
		return;

	if(x < 0 || y < 0 || x + w > W || y + h > H)
		return;

	lines = (h - 1) * 8;
	for(plane = 0; plane < 4; plane++)
	{
		gfx_direct_plane(plane);
		for(py = 0; py < lines; py++)
		{
			dst = gfx_off(vga_start + ((y * 8 + py) * 80) + x);
			src = gfx_off(vga_start + (((y + 1) * 8 + py) * 80) + x);
			VGA_COPY40(GFX + dst, GFX + src);
		}
	}

	gfx_clear_text_rect(x + 1, y + h - 1, w - 2, 1, col);
	gfx_ready();
}

void gfx_scroll_up_8(u8 col)
{
	unsigned int next;

	next = vga_start + 8 * 80;

	if(next + 80 * 480 > 0x10000)
	{
		gfx_set_start(0);
		screen_redraw();
		return;
	}

	gfx_set_start(next);
	gfx_clear_text_row(H - 1, col);
}

void cursor_hide(void)
{
	int old;

	if(cursor_drawn && cursor_x < W && cursor_y < H)
	{
		old = cursor_y * W + cursor_x;
		cursor_drawn = 0;
		screen_draw_cell(cursor_x, cursor_y, screen_chars[old], screen_attrs[old], 0);
	}
	else
		cursor_drawn = 0;
}

void screen_draw_cell(int x, int y, char c, u8 col, int cursor)
{
	int row;
	u8 fg;
	u8 bg;
	u8 bits;
	u8 *font;
	unsigned int off;

	if(x < 0 || x >= W || y < 0 || y >= H)
		return;

	fg = col & 0x0F;
	bg = (col >> 4) & 0x0F;

	if(cursor)
	{
		u8 t = fg;
		fg = bg;
		bg = t;
	}

	font = FONT8X8 + ((u8)c * 8);
	gfx_ready();

	for(row = 0; row < 8; row++)
	{
		bits = (c == ' ') ? 0 : font[row];
		off = gfx_off(vga_start + ((y * 8 + row) * 80) + x);
		gfx_write_masked(off, 0xFF, bg);
		gfx_write_masked(off, bits, fg);
	}
}

void screen_put_at(int x, int y, char c, u8 col)
{
	int i;
	int redraw_mouse;

	if(wm_on && !wm_redrawing && console_mode == CON_WINDOW)
	{
		/* Store into the window buffer before clipping to the
		   physical screen. This keeps offscreen/half-offscreen
		   windows from losing output while they are outside view. */
		wm_store_cell(x, y, c, col);
		if(x < 0 || x >= W || y < 0 || y >= H)
			return;
		if(!wm_cell_visible_for(wm_active, x, y))
			return;
	}
	else if(x < 0 || x >= W || y < 0 || y >= H)
		return;

	redraw_mouse = mouse_cell_hit(x, y);
	if(redraw_mouse)
		mouse_hide();

	i = y * W + x;
	screen_chars[i] = c;
	screen_attrs[i] = col;
	screen_draw_cell(x, y, c, col, cursor_drawn && cursor_x == (unsigned int)x && cursor_y == (unsigned int)y);

	if(redraw_mouse)
		mouse_show();
}


void screen_set_cell(int i, char c, u8 col)
{
	if(i < 0 || i >= W * H)
		return;

	screen_put_at(i % W, i / W, c, col);
}

void screen_get_cell(int i, char *c, u8 *col)
{
	if(i < 0 || i >= W * H)
	{
		*c = ' ';
		*col = color;
		return;
	}

	*c = screen_chars[i];
	*col = screen_attrs[i];
}

int con_abs_x(void)
{
	if(console_mode == CON_WINDOW)
		return con_x + cx;

	return cx;
}

int con_abs_y(void)
{
	if(console_mode == CON_WINDOW)
		return con_y + cy;

	return cy;
}

void con_set_full(void)
{
	console_mode = CON_FULL;
	con_x = 0;
	con_y = 0;
	con_w = W;
	con_h = H;
}

void con_set_window(int x, int y, int w, int h)
{
	console_mode = CON_WINDOW;
	con_x = x;
	con_y = y;
	con_w = w;
	con_h = h;
}

void con_save(WMWindow *win)
{
	win->cx = cx;
	win->cy = cy;
	win->cwd = as_cwd;
}


void con_load(WMWindow *win)
{
	con_set_window(win->x, win->y, win->w, win->h);
	cx = win->cx;
	cy = win->cy;
	as_cwd = win->cwd;
}


int wm_index_of(WMWindow *win)
{
	int i;

	for(i = 0; i < WM_WINDOWS; i++)
		if(win == &wm_windows[i])
			return i;

	return -1;
}

void wm_init_buffers(void)
{
	int id;
	int i;

	for(id = 0; id < WM_WINDOWS; id++)
	{
		for(i = 0; i < WM_CONTENT_W * WM_CONTENT_H; i++)
		{
			wm_buf_chars[id][i] = ' ';
			wm_buf_attrs[id][i] = 0x1F;
		}
	}
}

int wm_alive_count(void)
{
	int id;
	int n;

	n = 0;
	for(id = 0; id < WM_WINDOWS; id++)
		if(wm_windows[id].alive)
			n++;

	return n;
}

int wm_next_alive_from(int from)
{
	int i;
	int id;

	for(i = 1; i <= WM_WINDOWS; i++)
	{
		id = (from + i) % WM_WINDOWS;
		if(wm_windows[id].alive)
			return id;
	}

	return -1;
}


int wm_active_ok(void)
{
	return wm_active >= 0 && wm_active < WM_WINDOWS && wm_windows[wm_active].alive;
}

void wm_z_remove(int id)
{
	int i;
	int j;

	for(i = 0; i < wm_z_count; i++)
	{
		if(wm_z_order[i] == id)
		{
			for(j = i; j < wm_z_count - 1; j++)
				wm_z_order[j] = wm_z_order[j + 1];
			wm_z_count--;
			return;
		}
	}
}

void wm_z_add_top(int id)
{
	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;
	wm_z_remove(id);
	if(wm_z_count < WM_WINDOWS)
		wm_z_order[wm_z_count++] = id;
}

void wm_bring_front(int id)
{
	wm_z_add_top(id);
}

void wm_apply_geometry(int id, int bx, int by, int bw, int bh)
{
	WMWindow *win;

	if(id < 0 || id >= WM_WINDOWS)
		return;

	win = &wm_windows[id];
	if(bw < 4)
		bw = 4;
	if(bh < 4)
		bh = 4;
	if(bw > W)
		bw = W;
	if(bh > H - 1)
		bh = H - 1;
	/* Position is intentionally NOT clamped.
	   Windows may be dragged/spawned past the screen borders;
	   the drawing code clips whatever is actually visible. */

	win->bx = bx;
	win->by = by;
	win->bw = bw;
	win->bh = bh;
	win->x = bx + 1;
	win->y = by + 1;
	win->w = bw - 2;
	win->h = bh - 2;
	if(win->w > WM_CONTENT_W)
		win->w = WM_CONTENT_W;
	if(win->h > WM_CONTENT_H)
		win->h = WM_CONTENT_H;
	if(win->cx >= (unsigned int)win->w)
		win->cx = 0;
	if(win->cy >= (unsigned int)win->h)
		win->cy = win->h ? win->h - 1 : 0;
}

void wm_blank_window_buffer(int id)
{
	int i;

	if(id < 0 || id >= WM_WINDOWS)
		return;

	for(i = 0; i < WM_CONTENT_W * WM_CONTENT_H; i++)
	{
		wm_buf_chars[id][i] = ' ';
		wm_buf_attrs[id][i] = 0x1F;
	}
}

void wm_init_new_window(int id, int bx, int by, int bw, int bh)
{
	if(id < 0 || id >= WM_WINDOWS)
		return;

	wm_windows[id].alive = 1;
	wm_windows[id].fullscreen = 0;
	wm_windows[id].carded = 0;
	wm_windows[id].card_old_bx = bx;
	wm_windows[id].card_old_by = by;
	wm_windows[id].card_old_bw = bw;
	wm_windows[id].card_old_bh = bh;
	wm_windows[id].cx = 0;
	wm_windows[id].cy = 0;
	wm_windows[id].cwd = as_cwd;
	wm_windows[id].old_bx = bx;
	wm_windows[id].old_by = by;
	wm_windows[id].old_bw = bw;
	wm_windows[id].old_bh = bh;
	wm_windows[id].input_len = 0;
	wm_windows[id].input_x = 0;
	wm_windows[id].input_y = 0;
	wm_windows[id].input[0] = 0;
	wm_windows[id].task_cmd[0] = 0;
	wm_windows[id].task_pending = 0;
	wm_windows[id].task_data = 0;
	wm_windows[id].task_size = 0;
	wm_windows[id].task_pos = 0;
	wm_windows[id].task_running = 0;
	wm_windows[id].task_sp = 0;
	wm_windows[id].task_wake = 0;
	wm_windows[id].tune_song[0] = 0;
	wm_windows[id].tune_pos = 0;
	wm_windows[id].tune_tempo = 120;
	wm_windows[id].tune_running = 0;
	wm_windows[id].tune_until = 0;
	wm_windows[id].tune_sounding = 0;
	wm_blank_window_buffer(id);
	wm_apply_geometry(id, bx, by, bw, bh);
	wm_set_title(id, "Shell");
}

int wm_create_window(int kind)
{
	int id;
	int bx;
	int by;
	int bw;
	int bh;
	int old;

	for(id = 0; id < WM_WINDOWS; id++)
		if(!wm_windows[id].alive)
			break;

	if(id >= WM_WINDOWS)
		return -1;

	if(kind == WM_ADD_CARD)
	{
		bw = WM_CARD_W;
		bh = WM_CARD_H;
		bx = wm_next_card_x;
		by = wm_next_card_y;
		/* Start cards at the top-left edge, then keep cascading
		   down/right forever. No wrap and no border clamp. */
		wm_next_card_x += 4;
		wm_next_card_y += 3;
	}
	else
	{
		bw = WM_SIDE_W;
		bh = WM_SIDE_H;
		bx = wm_next_side ? W - WM_SIDE_W : 0;
		by = 1;
		wm_next_side = !wm_next_side;
	}

	old = wm_active;
	if(old >= 0 && old < WM_WINDOWS && wm_windows[old].alive)
		con_save(&wm_windows[old]);

	wm_init_new_window(id, bx, by, bw, bh);
	wm_active = id;
	wm_bring_front(id);
	con_load(&wm_windows[id]);
	wm_redraw_region(bx, by, bx + bw - 1, by + bh - 1);
	wm_start_shell_window(id);
	wm_redraw_region(bx, by, bx + bw - 1, by + bh - 1);
	return id;
}

void wm_close_window(int id)
{
	int next;

	if(id < 0 || id >= WM_WINDOWS)
		return;
	if(!wm_windows[id].alive)
		return;

	if(id == wm_selected_win)
		wm_clear_highlight();

	if(wm_windows[id].tune_sounding)
		nosound();

	as_edit_win_close(id);

	wm_windows[id].alive = 0;
	if(id == wm_help_win)
		wm_help_win = -1;
	wm_windows[id].fullscreen = 0;
	wm_windows[id].carded = 0;
	wm_windows[id].task_running = 0;
	wm_windows[id].task_pending = 0;
	wm_windows[id].task_sp = 0;
	wm_windows[id].task_wake = 0;
	wm_windows[id].tune_running = 0;
	wm_windows[id].tune_until = 0;
	wm_windows[id].tune_sounding = 0;
	wm_z_remove(id);
	wm_drag_win = -1;
	wm_mouse_select_win = -1;

	if(wm_active == id)
	{
		next = (wm_z_count > 0) ? wm_z_order[wm_z_count - 1] : -1;
		wm_active = next;
	}

	if(wm_active_ok())
		con_load(&wm_windows[wm_active]);
	else
		con_set_full();

	wm_redraw_region(wm_windows[id].bx, wm_windows[id].by, wm_windows[id].bx + wm_windows[id].bw - 1, wm_windows[id].by + wm_windows[id].bh - 1);
}

void wm_toggle_fullscreen(int id)
{
	WMWindow *win;

	if(id < 0 || id >= WM_WINDOWS)
		return;
	if(!wm_windows[id].alive)
		return;

	con_save(&wm_windows[id]);
	win = &wm_windows[id];
	if(!win->fullscreen)
	{
		win->carded = 0;
		win->old_bx = win->bx;
		win->old_by = win->by;
		win->old_bw = win->bw;
		win->old_bh = win->bh;
		win->fullscreen = 1;
		wm_apply_geometry(id, 0, 1, 80, 59);
	}
	else
	{
		win->fullscreen = 0;
		wm_apply_geometry(id, win->old_bx, win->old_by, win->old_bw, win->old_bh);
	}
	con_load(&wm_windows[id]);
	wm_redraw_all();
}

void wm_toggle_card(int id)
{
	WMWindow *win;
	int bx;
	int by;

	if(id < 0 || id >= WM_WINDOWS)
		return;
	if(!wm_windows[id].alive)
		return;

	con_save(&wm_windows[id]);
	win = &wm_windows[id];

	if(win->fullscreen)
	{
		win->fullscreen = 0;
		wm_apply_geometry(id, win->old_bx, win->old_by, win->old_bw, win->old_bh);
	}

	if(!win->carded)
	{
		win->card_old_bx = win->bx;
		win->card_old_by = win->by;
		win->card_old_bw = win->bw;
		win->card_old_bh = win->bh;
		win->carded = 1;
		bx = win->bx;
		by = win->by;
		if(bx + WM_CARD_W > W)
			bx = W - WM_CARD_W;
		if(by + WM_CARD_H > H)
			by = H - WM_CARD_H;
		wm_apply_geometry(id, bx, by, WM_CARD_W, WM_CARD_H);
	}
	else
	{
		win->carded = 0;
		wm_apply_geometry(id, win->card_old_bx, win->card_old_by, win->card_old_bw, win->card_old_bh);
	}

	wm_bring_front(id);
	wm_active = id;
	con_load(&wm_windows[id]);
	wm_redraw_all();
}

void wm_draw_top_controls(void)
{
	if(!wm_on)
		return;

	screen_put_at(WM_PLUS_X - 1, 0, '[', 0xF1);
	screen_put_at(WM_PLUS_X, 0, '+', 0xF1);
	screen_put_at(WM_PLUS_X + 1, 0, ']', 0xF1);
}

void wm_draw_add_menu(void)
{
	int x;
	int y;
	u8 col;

	col = 0x1F;
	for(x = 0; x < WM_MENU_W; x++)
	{
		screen_put_at(WM_MENU_X + x, WM_MENU_Y, (char)196, col);
		screen_put_at(WM_MENU_X + x, WM_MENU_Y + WM_MENU_H - 1, (char)196, col);
	}
	for(y = 0; y < WM_MENU_H; y++)
	{
		screen_put_at(WM_MENU_X, WM_MENU_Y + y, (char)179, col);
		screen_put_at(WM_MENU_X + WM_MENU_W - 1, WM_MENU_Y + y, (char)179, col);
	}
	screen_put_at(WM_MENU_X, WM_MENU_Y, (char)218, col);
	screen_put_at(WM_MENU_X + WM_MENU_W - 1, WM_MENU_Y, (char)191, col);
	screen_put_at(WM_MENU_X, WM_MENU_Y + WM_MENU_H - 1, (char)192, col);
	screen_put_at(WM_MENU_X + WM_MENU_W - 1, WM_MENU_Y + WM_MENU_H - 1, (char)217, col);

	screen_write_at(WM_MENU_X + 2, WM_MENU_Y + 1, "+ Add window", col);
	screen_write_at(WM_MENU_X + 2, WM_MENU_Y + 2, "Card window", 0x1F);
	screen_write_at(WM_MENU_X + 2, WM_MENU_Y + 3, "Side window", 0x1F);
}

void wm_hide_add_menu(void)
{
	if(!wm_add_menu)
		return;

	wm_add_menu = 0;
	wm_redraw_region(WM_MENU_X, WM_MENU_Y, WM_MENU_X + WM_MENU_W - 1, WM_MENU_Y + WM_MENU_H - 1);
	wm_draw_top_controls();
}

int wm_handle_add_menu_click(int tx, int ty)
{
	if(!wm_add_menu)
		return 0;

	if(tx >= WM_MENU_X && tx < WM_MENU_X + WM_MENU_W &&
	   ty >= WM_MENU_Y && ty < WM_MENU_Y + WM_MENU_H)
	{
		if(ty == WM_MENU_Y + 2)
		{
			wm_hide_add_menu();
			wm_create_window(WM_ADD_CARD);
			return 1;
		}
		if(ty == WM_MENU_Y + 3)
		{
			wm_hide_add_menu();
			wm_create_window(WM_ADD_SIDE);
			return 1;
		}
		return 1;
	}

	wm_hide_add_menu();
	return 0;
}

void wm_prepare_selected_run_line(const char *cmd)
{
	int x;
	int oldcolor;

	if(!cmd || !cmd[0])
		return;

	cursor_hide();
	oldcolor = color;
	color = 0x1A;
	cy = con_h - 1;
	cx = 0;
	for(x = 0; x < con_w; x++)
		screen_put_at(con_x + x, con_y + cy, ' ', color);
	cx = 0;
	print(cmd);
	putc('\n');
	color = oldcolor;
}

void wm_sync_input_from_screen(int id)
{
	WMWindow *win;
	int pos;
	int len;
	char c;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;

	win = &wm_windows[id];
	win->input_len = 0;
	win->input[0] = 0;
	if(win->input_y < 0 || win->input_y >= win->h)
		return;
	if(win->input_x < 0)
		win->input_x = 0;
	if(win->input_x >= win->w)
		return;

	len = 0;
	for(pos = win->input_x; pos < win->w && len < WM_INPUT_MAX - 1; pos++)
	{
		c = wm_buf_chars[id][win->input_y * WM_CONTENT_W + pos];
		win->input[len++] = c;
	}
	while(len > 0 && win->input[len - 1] == ' ')
		len--;
	win->input[len] = 0;
	win->input_len = len;
}

int wm_cursor_on_prompt_line(int id)
{
	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return 0;
	return cy == (unsigned int)wm_windows[id].input_y && cx >= (unsigned int)wm_windows[id].input_x;
}

void wm_insert_char_here(int id, char c)
{
	WMWindow *win;
	int row;
	int col;
	int x;
	int i;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;

	win = &wm_windows[id];
	row = cy;
	col = cx;
	if(row < 0 || row >= win->h || col < 0 || col >= win->w)
		return;

	cursor_hide();
	for(x = win->w - 1; x > col; x--)
	{
		wm_buf_chars[id][row * WM_CONTENT_W + x] = wm_buf_chars[id][row * WM_CONTENT_W + x - 1];
		wm_buf_attrs[id][row * WM_CONTENT_W + x] = wm_buf_attrs[id][row * WM_CONTENT_W + x - 1];
	}
	i = row * WM_CONTENT_W + col;
	wm_buf_chars[id][i] = c;
	wm_buf_attrs[id][i] = color;
	if(wm_selected_win == id)
		wm_clear_highlight();
	wm_draw_content_region(id, col, row, win->w - 1, row);
	if(cx + 1 < (unsigned int)win->w)
		cx++;
	if(wm_cursor_on_prompt_line(id))
		wm_sync_input_from_screen(id);
	else
		wm_select_line(id, row);
	cursor_update();
}

void wm_backspace_here(int id)
{
	WMWindow *win;
	int row;
	int col;
	int x;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;

	win = &wm_windows[id];
	row = cy;
	col = cx;
	if(row < 0 || row >= win->h || col <= 0 || col > win->w)
		return;

	if(row == win->input_y && col <= win->input_x)
		return;

	cursor_hide();
	col--;
	cx = col;
	for(x = col; x < win->w - 1; x++)
	{
		wm_buf_chars[id][row * WM_CONTENT_W + x] = wm_buf_chars[id][row * WM_CONTENT_W + x + 1];
		wm_buf_attrs[id][row * WM_CONTENT_W + x] = wm_buf_attrs[id][row * WM_CONTENT_W + x + 1];
	}
	wm_buf_chars[id][row * WM_CONTENT_W + win->w - 1] = ' ';
	wm_buf_attrs[id][row * WM_CONTENT_W + win->w - 1] = color;
	if(wm_selected_win == id)
		wm_clear_highlight();
	wm_draw_content_region(id, col, row, win->w - 1, row);
	if(wm_cursor_on_prompt_line(id))
		wm_sync_input_from_screen(id);
	else
		wm_select_line(id, row);
	cursor_update();
}

int wm_create_plain_window(int kind)
{
	int id;
	int bx;
	int by;
	int bw;
	int bh;
	int old;

	for(id = 0; id < WM_WINDOWS; id++)
		if(!wm_windows[id].alive)
			break;

	if(id >= WM_WINDOWS)
		return -1;

	if(kind == WM_ADD_CARD)
	{
		bw = WM_CARD_W;
		bh = WM_CARD_H;
		bx = wm_next_card_x;
		by = wm_next_card_y;
		/* Start cards at the top-left edge, then keep cascading
		   down/right forever. No wrap and no border clamp. */
		wm_next_card_x += 4;
		wm_next_card_y += 3;
	}
	else
	{
		bw = WM_SIDE_W;
		bh = WM_SIDE_H;
		bx = wm_next_side ? W - WM_SIDE_W : 0;
		by = 1;
		wm_next_side = !wm_next_side;
	}

	old = wm_active;
	if(old >= 0 && old < WM_WINDOWS && wm_windows[old].alive)
		con_save(&wm_windows[old]);

	wm_init_new_window(id, bx, by, bw, bh);
	wm_active = id;
	wm_bring_front(id);
	con_load(&wm_windows[id]);
	wm_redraw_region(bx, by, bx + bw - 1, by + bh - 1);
	return id;
}

void wm_draw_help_card(int id)
{
	int old;
	u8 oldcolor;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;

	old = wm_active;
	oldcolor = color;
	if(old >= 0 && old < WM_WINDOWS && wm_windows[old].alive)
		con_save(&wm_windows[old]);

	wm_active = id;
	con_load(&wm_windows[id]);
	color = 0x1F;
	clear();
	wm_set_title(id, "Help");
	print("AneoEngine Help\n");
	print("Useful files:\n");
	print("\ncommand help - /Help/CommandHelp.TXT\n");
	print("controls - /Help/Controls\n");
	print("about - /Help/AboutAneoEngine.TXT\n");
	print("FAQ - /Help/FAQ.TXT\n");
	print("Normal shell commands work here too.\n");
	wm_prompt();
	con_save(&wm_windows[id]);
	color = oldcolor;
	wm_active = old;
	if(wm_active_ok())
		con_load(&wm_windows[wm_active]);
}

void wm_open_help_window(void)
{
	int id;

	if(!wm_on)
	{
		helpMenu();
		return;
	}

	if(wm_help_win >= 0 && wm_help_win < WM_WINDOWS && wm_windows[wm_help_win].alive)
	{
		wm_activate(wm_help_win);
		wm_task_focus_win = wm_help_win;
		return;
	}

	id = wm_create_plain_window(WM_ADD_CARD);
	if(id < 0)
		return;

	wm_help_win = id;
	wm_windows[id].task_cmd[0] = 0;
	wm_windows[id].task_pending = 0;
	wm_windows[id].task_running = 0;
	wm_windows[id].task_sp = 0;
	wm_windows[id].task_wake = 0;
	wm_windows[id].tune_running = 0;
	wm_windows[id].tune_until = 0;
	wm_windows[id].tune_sounding = 0;
	wm_draw_help_card(id);
	wm_activate(id);
	wm_task_focus_win = id;
}

void wm_store_cell(int x, int y, char c, u8 col)
{
	WMWindow *win;
	int rx;
	int ry;
	int i;

	if(!wm_on || wm_redrawing)
		return;

	if(console_mode != CON_WINDOW)
		return;

	if(wm_active < 0 || wm_active >= WM_WINDOWS)
		return;

	win = &wm_windows[wm_active];
	if(x < win->x || y < win->y || x >= win->x + win->w || y >= win->y + win->h)
		return;

	rx = x - win->x;
	ry = y - win->y;
	if(rx < 0 || ry < 0 || rx >= WM_CONTENT_W || ry >= WM_CONTENT_H)
		return;

	i = ry * WM_CONTENT_W + rx;
	wm_buf_chars[wm_active][i] = c;
	wm_buf_attrs[wm_active][i] = col;
}

void wm_capture(int id)
{
	WMWindow *win;
	int x;
	int y;
	int si;
	int di;

	if(!wm_on || id < 0 || id >= WM_WINDOWS)
		return;
	if(!wm_windows[id].alive)
		return;

	win = &wm_windows[id];
	if(win->w > WM_CONTENT_W || win->h > WM_CONTENT_H)
		return;

	for(y = 0; y < win->h; y++)
	{
		for(x = 0; x < win->w; x++)
		{
			int sx = win->x + x;
			int sy = win->y + y;
			if(sx < 0 || sx >= W || sy < 0 || sy >= H)
				continue;
			si = sy * W + sx;
			di = y * WM_CONTENT_W + x;
			wm_buf_chars[id][di] = screen_chars[si];
			if(!wm_cell_selected(id, x, y))
				wm_buf_attrs[id][di] = screen_attrs[si];
		}
	}
}

void wm_capture_window(WMWindow *win)
{
	int id;

	id = wm_index_of(win);
	if(id >= 0)
		wm_capture(id);
}

void wm_draw_content(int id)
{
	WMWindow *win;
	int x;
	int y;
	int si;
	int di;
	u8 col;

	if(id < 0 || id >= WM_WINDOWS)
		return;
	if(!wm_windows[id].alive)
		return;

	win = &wm_windows[id];
	wm_redrawing = 1;
	for(y = 0; y < win->h; y++)
	{
		for(x = 0; x < win->w; x++)
		{
			int sx = win->x + x;
			int sy = win->y + y;
			if(sx < 0 || sx >= W || sy < 0 || sy >= H)
				continue;
			di = y * WM_CONTENT_W + x;
			si = sy * W + sx;
			col = wm_buf_attrs[id][di];
			if(wm_cell_selected(id, x, y))
				col = 0x70;
			screen_chars[si] = wm_buf_chars[id][di];
			screen_attrs[si] = col;
			screen_draw_cell(sx, sy, screen_chars[si], col, 0);
		}
	}
	wm_redrawing = 0;
}


void wm_draw_content_region(int id, int rx0, int ry0, int rx1, int ry1)
{
	WMWindow *win;
	int x;
	int y;
	int sx;
	int sy;
	int si;
	int di;
	u8 col;

	if(id < 0 || id >= WM_WINDOWS)
		return;
	if(!wm_windows[id].alive)
		return;

	win = &wm_windows[id];

	if(rx0 < 0)
		rx0 = 0;
	if(ry0 < 0)
		ry0 = 0;
	if(rx1 >= win->w)
		rx1 = win->w - 1;
	if(ry1 >= win->h)
		ry1 = win->h - 1;

	if(rx0 > rx1 || ry0 > ry1)
		return;

	wm_redrawing = 1;
	for(y = ry0; y <= ry1; y++)
	{
		for(x = rx0; x <= rx1; x++)
		{
			sx = win->x + x;
			sy = win->y + y;
			if(sx < 0 || sx >= W || sy < 0 || sy >= H)
				continue;
			if(!wm_cell_visible_for(id, sx, sy))
				continue;
			di = y * WM_CONTENT_W + x;
			si = sy * W + sx;
			col = wm_buf_attrs[id][di];
			if(wm_cell_selected(id, x, y))
				col = 0x70;
			screen_chars[si] = wm_buf_chars[id][di];
			screen_attrs[si] = col;
			screen_draw_cell(sx, sy, screen_chars[si], col, 0);
		}
	}
	wm_redrawing = 0;
}

void wm_draw_one_region(int id, int rx0, int ry0, int rx1, int ry1)
{
	WMWindow *win;
	int x;
	int y;
	int i;
	int max;
	u8 col;

	if(id < 0 || id >= WM_WINDOWS)
		return;
	if(!wm_windows[id].alive)
		return;

	win = &wm_windows[id];
	col = (id == wm_active) ? 0x1F : 0x17;
	wm_redrawing = 1;

	for(x = 0; x < win->bw; x++)
	{
		int sx = win->bx + x;
		if(sx >= rx0 && sx <= rx1)
		{
			if(win->by >= ry0 && win->by <= ry1)
				screen_put_at(sx, win->by, (char)196, col);
			if(win->by + win->bh - 1 >= ry0 && win->by + win->bh - 1 <= ry1)
				screen_put_at(sx, win->by + win->bh - 1, (char)196, col);
		}
	}

	for(y = 0; y < win->bh; y++)
	{
		int sy = win->by + y;
		if(sy >= ry0 && sy <= ry1)
		{
			if(win->bx >= rx0 && win->bx <= rx1)
				screen_put_at(win->bx, sy, (char)179, col);
			if(win->bx + win->bw - 1 >= rx0 && win->bx + win->bw - 1 <= rx1)
				screen_put_at(win->bx + win->bw - 1, sy, (char)179, col);
		}
	}

	if(win->by >= ry0 && win->by <= ry1)
	{
		if(win->bx >= rx0 && win->bx <= rx1)
			screen_put_at(win->bx, win->by, (char)218, col);
		if(win->bx + win->bw - 1 >= rx0 && win->bx + win->bw - 1 <= rx1)
			screen_put_at(win->bx + win->bw - 1, win->by, (char)191, col);
	}

	if(win->by + win->bh - 1 >= ry0 && win->by + win->bh - 1 <= ry1)
	{
		if(win->bx >= rx0 && win->bx <= rx1)
			screen_put_at(win->bx, win->by + win->bh - 1, (char)192, col);
		if(win->bx + win->bw - 1 >= rx0 && win->bx + win->bw - 1 <= rx1)
			screen_put_at(win->bx + win->bw - 1, win->by + win->bh - 1, (char)217, col);
	}

	max = win->bw - 8;
	if(max > WM_TITLE_MAX - 1)
		max = WM_TITLE_MAX - 1;
	if(max < 0)
		max = 0;

	if(win->by >= ry0 && win->by <= ry1)
	{
		for(i = 1; i < win->bw - 1; i++)
		{
			if(win->bx + i >= rx0 && win->bx + i <= rx1)
				screen_put_at(win->bx + i, win->by, ' ', col);
		}
		i = 0;
		while(win->title[i] && i < max)
		{
			if(win->bx + 2 + i >= rx0 && win->bx + 2 + i <= rx1)
				screen_put_at(win->bx + 2 + i, win->by, win->title[i], col);
			i++;
		}
		if(win->bx + win->bw - WM_CLOSE_MARGIN >= rx0 && win->bx + win->bw - WM_CLOSE_MARGIN <= rx1)
			screen_put_at(win->bx + win->bw - WM_CLOSE_MARGIN, win->by, 'X', col);
	}

	wm_redrawing = 0;
}


void wm_compose_window_cell(int id, int sx, int sy, char *outc, u8 *outcol)
{
	WMWindow *win;
	int rx;
	int ry;
	int max;
	int ti;
	u8 col;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;

	win = &wm_windows[id];
	if(sx < win->bx || sy < win->by || sx >= win->bx + win->bw || sy >= win->by + win->bh)
		return;

	col = (id == wm_active) ? 0x1F : 0x17;
	rx = sx - win->bx;
	ry = sy - win->by;

	if(ry == 0 || ry == win->bh - 1 || rx == 0 || rx == win->bw - 1)
	{
		*outcol = col;
		*outc = ' ';
		if(ry == 0 && rx == 0)
			*outc = (char)218;
		else if(ry == 0 && rx == win->bw - 1)
			*outc = (char)191;
		else if(ry == win->bh - 1 && rx == 0)
			*outc = (char)192;
		else if(ry == win->bh - 1 && rx == win->bw - 1)
			*outc = (char)217;
		else if(ry == 0 || ry == win->bh - 1)
			*outc = (char)196;
		else
			*outc = (char)179;

		if(ry == 0)
		{
			max = win->bw - 8;
			if(max > WM_TITLE_MAX - 1)
				max = WM_TITLE_MAX - 1;
			if(max < 0)
				max = 0;

			if(rx > 0 && rx < win->bw - 1)
				*outc = ' ';
			if(rx >= 2 && rx < 2 + max)
			{
				ti = rx - 2;
				if(win->title[ti])
					*outc = win->title[ti];
			}
			if(rx == win->bw - WM_CLOSE_MARGIN)
				*outc = 'X';
		}
		return;
	}

	if(sx >= win->x && sy >= win->y && sx < win->x + win->w && sy < win->y + win->h)
	{
		int bi;
		int cx0;
		int cy0;

		cx0 = sx - win->x;
		cy0 = sy - win->y;
		bi = cy0 * WM_CONTENT_W + cx0;
		*outc = wm_buf_chars[id][bi];
		*outcol = wm_cell_selected(id, cx0, cy0) ? 0x70 : wm_buf_attrs[id][bi];
	}
}

void wm_compose_menu_cell(int sx, int sy, char *outc, u8 *outcol)
{
	const char *s;
	int idx;

	if(!wm_add_menu)
		return;
	if(sx < WM_MENU_X || sx >= WM_MENU_X + WM_MENU_W || sy < WM_MENU_Y || sy >= WM_MENU_Y + WM_MENU_H)
		return;

	*outc = ' ';
	*outcol = 0x1F;
	if(sy == WM_MENU_Y || sy == WM_MENU_Y + WM_MENU_H - 1)
		*outc = (char)196;
	if(sx == WM_MENU_X || sx == WM_MENU_X + WM_MENU_W - 1)
		*outc = (char)179;
	if(sx == WM_MENU_X && sy == WM_MENU_Y)
		*outc = (char)218;
	else if(sx == WM_MENU_X + WM_MENU_W - 1 && sy == WM_MENU_Y)
		*outc = (char)191;
	else if(sx == WM_MENU_X && sy == WM_MENU_Y + WM_MENU_H - 1)
		*outc = (char)192;
	else if(sx == WM_MENU_X + WM_MENU_W - 1 && sy == WM_MENU_Y + WM_MENU_H - 1)
		*outc = (char)217;

	s = 0;
	if(sy == WM_MENU_Y + 1)
		s = "+ Add window";
	else if(sy == WM_MENU_Y + 2)
		s = "Card window";
	else if(sy == WM_MENU_Y + 3)
		s = "Side window";
	if(s)
	{
		idx = sx - (WM_MENU_X + 2);
		if(idx >= 0 && s[idx])
			*outc = s[idx];
	}
}

void wm_compose_cell(int sx, int sy, char *outc, u8 *outcol)
{
	int zi;
	int id;

	*outc = ' ';
	*outcol = WM_BG_ATTR;

	memstat_overlay_compose(sx, sy, outc, outcol);

	for(zi = 0; zi < wm_z_count; zi++)
	{
		id = wm_z_order[zi];
		wm_compose_window_cell(id, sx, sy, outc, outcol);
	}

	wm_compose_menu_cell(sx, sy, outc, outcol);
}

int wm_cell_visible_for(int id, int sx, int sy)
{
	int zi;
	int pos;
	int top;
	WMWindow *win;

	if(!wm_on || wm_redrawing)
		return 1;
	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return 1;
	if(wm_add_menu && sx >= WM_MENU_X && sx < WM_MENU_X + WM_MENU_W && sy >= WM_MENU_Y && sy < WM_MENU_Y + WM_MENU_H)
		return 0;

	pos = -1;
	for(zi = 0; zi < wm_z_count; zi++)
		if(wm_z_order[zi] == id)
			pos = zi;
	if(pos < 0)
		return 1;

	for(zi = pos + 1; zi < wm_z_count; zi++)
	{
		top = wm_z_order[zi];
		if(top < 0 || top >= WM_WINDOWS || !wm_windows[top].alive)
			continue;
		win = &wm_windows[top];
		if(sx >= win->bx && sx < win->bx + win->bw && sy >= win->by && sy < win->by + win->bh)
			return 0;
	}

	return 1;
}

void wm_redraw_region(int rx0, int ry0, int rx1, int ry1)
{
	int x;
	int y;
	int i;
	char c;
	u8 col;
	int redraw_mouse;

	if(rx0 < 0)
		rx0 = 0;
	if(ry0 < 1)
		ry0 = 1;
	if(rx1 >= W)
		rx1 = W - 1;
	if(ry1 >= H)
		ry1 = H - 1;
	if(rx0 > rx1 || ry0 > ry1)
		return;

	cursor_hide();
	redraw_mouse = mouse_drawn;
	if(redraw_mouse)
		mouse_hide();

	wm_redrawing = 1;
	for(y = ry0; y <= ry1; y++)
	{
		for(x = rx0; x <= rx1; x++)
		{
			wm_compose_cell(x, y, &c, &col);
			i = y * W + x;
			if(screen_chars[i] != c || screen_attrs[i] != col)
			{
				screen_chars[i] = c;
				screen_attrs[i] = col;
				screen_draw_cell(x, y, c, col, 0);
			}
		}
	}
	wm_redrawing = 0;

	cursor_update();
	if(redraw_mouse)
		mouse_show();
}


void wm_redraw_window_move(int id, int obx, int oby)
{
	WMWindow *win;
	int rx0;
	int ry0;
	int rx1;
	int ry1;

	if(id < 0 || id >= WM_WINDOWS)
		return;

	win = &wm_windows[id];
	rx0 = obx;
	ry0 = oby;
	rx1 = obx + win->bw - 1;
	ry1 = oby + win->bh - 1;

	if(win->bx < rx0)
		rx0 = win->bx;
	if(win->by < ry0)
		ry0 = win->by;
	if(win->bx + win->bw - 1 > rx1)
		rx1 = win->bx + win->bw - 1;
	if(win->by + win->bh - 1 > ry1)
		ry1 = win->by + win->bh - 1;

	wm_redraw_region(rx0, ry0, rx1, ry1);
}

void wm_clear_background(void)
{
	int x;
	int y;
	int i;

	cursor_hide();
	mouse_hide();
	gfx_clear_text_rect(0, 1, W, H - 1, 0x0F);

	for(y = 1; y < H; y++)
	{
		for(x = 0; x < W; x++)
		{
			i = y * W + x;
			screen_chars[i] = ' ';
			screen_attrs[i] = WM_BG_ATTR;
		}
	}
}

void wm_redraw_all(void)
{
	int oldmode;
	int oldx;
	int oldy;
	int oldw;
	int oldh;
	unsigned int oldcx;
	unsigned int oldcy;

	oldmode = console_mode;
	oldx = con_x;
	oldy = con_y;
	oldw = con_w;
	oldh = con_h;
	oldcx = cx;
	oldcy = cy;

	con_set_full();
	draw_tb();
	update_rtc_only();
	wm_redraw_region(0, 1, W - 1, H - 1);
	wm_draw_top_controls();

	console_mode = oldmode;
	con_x = oldx;
	con_y = oldy;
	con_w = oldw;
	con_h = oldh;
	cx = oldcx;
	cy = oldcy;
}


void wm_activate(int id)
{
	int old;
	int rx0;
	int ry0;
	int rx1;
	int ry1;
	WMWindow *win;
	WMWindow *oldwin;

	if(id < 0 || id >= WM_WINDOWS)
		return;
	if(!wm_windows[id].alive)
		return;

	old = wm_active;
	if(old >= 0 && old < WM_WINDOWS && wm_windows[old].alive)
		con_save(&wm_windows[old]);

	cursor_hide();
	wm_active = id;
	wm_bring_front(id);
	con_load(&wm_windows[wm_active]);

	win = &wm_windows[id];
	rx0 = win->bx;
	ry0 = win->by;
	rx1 = win->bx + win->bw - 1;
	ry1 = win->by + win->bh - 1;
	if(old >= 0 && old < WM_WINDOWS && wm_windows[old].alive)
	{
		oldwin = &wm_windows[old];
		if(oldwin->bx < rx0) rx0 = oldwin->bx;
		if(oldwin->by < ry0) ry0 = oldwin->by;
		if(oldwin->bx + oldwin->bw - 1 > rx1) rx1 = oldwin->bx + oldwin->bw - 1;
		if(oldwin->by + oldwin->bh - 1 > ry1) ry1 = oldwin->by + oldwin->bh - 1;
	}
	wm_redraw_region(rx0, ry0, rx1, ry1);
	cursor_update();
}


int wm_hit_window(int tx, int ty)
{
	int zi;
	int id;
	WMWindow *win;

	for(zi = wm_z_count - 1; zi >= 0; zi--)
	{
		id = wm_z_order[zi];
		if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
			continue;
		win = &wm_windows[id];
		if(tx >= win->bx && tx < win->bx + win->bw &&
		   ty >= win->by && ty < win->by + win->bh)
			return id;
	}

	return -1;
}

void wm_extract_line_command(int id, int row, char *out)
{
	int i;
	int end;
	int start;
	WMWindow *win;
	char line[WM_CONTENT_W + 1];

	out[0] = 0;
	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;
	win = &wm_windows[id];
	if(row < 0 || row >= win->h)
		return;

	for(i = 0; i < win->w; i++)
		line[i] = wm_buf_chars[id][row * WM_CONTENT_W + i];
	line[win->w] = 0;

	end = win->w;
	while(end > 0 && line[end - 1] == ' ')
		end--;
	line[end] = 0;

	start = 0;
	for(i = 0; i < end; i++)
		if(line[i] == '>')
			start = i + 1;

	while(line[start] == ' ')
		start++;

	i = 0;
	while(line[start] && i < WM_INPUT_MAX - 1)
		out[i++] = line[start++];
	out[i] = 0;
}

int wm_cell_in_selection(int type, int row, int x0, int y0, int x1, int y1, int x, int y)
{
	int a;
	int b;
	int p;

	if(type == 1)
		return y == row;

	if(type != 2)
		return 0;

	a = y0 * WM_CONTENT_W + x0;
	b = y1 * WM_CONTENT_W + x1;
	p = y * WM_CONTENT_W + x;
	if(a > b)
	{
		int t = a;
		a = b;
		b = t;
	}

	return p >= a && p <= b;
}

int wm_cell_selected(int id, int x, int y)
{
	if(id != wm_selected_win)
		return 0;

	return wm_cell_in_selection(wm_selected_type, wm_selected_row, wm_sel_x0, wm_sel_y0, wm_sel_x1, wm_sel_y1, x, y);
}

void wm_draw_selected_cell(int id, int x, int y, int selected)
{
	WMWindow *win;
	int si;
	int bi;
	u8 col;

	if(id < 0 || id >= WM_WINDOWS || x < 0 || y < 0 || x >= WM_CONTENT_W || y >= WM_CONTENT_H)
		return;

	win = &wm_windows[id];
	if(!win->alive || x >= win->w || y >= win->h)
		return;
	{
		int sx = win->x + x;
		int sy = win->y + y;
		if(sx < 0 || sx >= W || sy < 0 || sy >= H)
			return;
		bi = y * WM_CONTENT_W + x;
		si = sy * W + sx;
		col = selected ? 0x70 : wm_buf_attrs[id][bi];
		screen_chars[si] = wm_buf_chars[id][bi];
		screen_attrs[si] = col;
		screen_draw_cell(sx, sy, screen_chars[si], col, 0);
	}
}

void wm_redraw_selection_change(int oldwin, int oldtype, int oldrow, int oldx0, int oldy0, int oldx1, int oldy1, int newwin, int newtype, int newrow, int newx0, int newy0, int newx1, int newy1)
{
	int id;
	int x;
	int y;
	int oldsel;
	int newsel;
	int miny;
	int maxy;
	WMWindow *win;

	if(oldwin < 0 && newwin < 0)
		return;

	cursor_hide();
	mouse_hide();
	wm_redrawing = 1;

	for(id = 0; id < WM_WINDOWS; id++)
	{
		if(id != oldwin && id != newwin)
			continue;
		if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
			continue;
		win = &wm_windows[id];

		miny = 0;
		maxy = win->h - 1;
		if(id == oldwin && id == newwin)
		{
			int omin;
			int omax;
			int nmin;
			int nmax;

			omin = 0;
			omax = WM_CONTENT_H - 1;
			nmin = 0;
			nmax = WM_CONTENT_H - 1;

			if(oldtype == 1)
				omin = omax = oldrow;
			else if(oldtype == 2)
			{
				omin = oldy0 < oldy1 ? oldy0 : oldy1;
				omax = oldy0 > oldy1 ? oldy0 : oldy1;
			}

			if(newtype == 1)
				nmin = nmax = newrow;
			else if(newtype == 2)
			{
				nmin = newy0 < newy1 ? newy0 : newy1;
				nmax = newy0 > newy1 ? newy0 : newy1;
			}

			miny = omin < nmin ? omin : nmin;
			maxy = omax > nmax ? omax : nmax;
		}
		else if(id == oldwin)
		{
			if(oldtype == 1)
				miny = maxy = oldrow;
			else if(oldtype == 2)
			{
				miny = oldy0 < oldy1 ? oldy0 : oldy1;
				maxy = oldy0 > oldy1 ? oldy0 : oldy1;
			}
		}
		else if(id == newwin)
		{
			if(newtype == 1)
				miny = maxy = newrow;
			else if(newtype == 2)
			{
				miny = newy0 < newy1 ? newy0 : newy1;
				maxy = newy0 > newy1 ? newy0 : newy1;
			}
		}
		if(miny < 0)
			miny = 0;
		if(maxy >= win->h)
			maxy = win->h - 1;

		for(y = miny; y <= maxy; y++)
		{
			for(x = 0; x < win->w; x++)
			{
				oldsel = (id == oldwin) ? wm_cell_in_selection(oldtype, oldrow, oldx0, oldy0, oldx1, oldy1, x, y) : 0;
				newsel = (id == newwin) ? wm_cell_in_selection(newtype, newrow, newx0, newy0, newx1, newy1, x, y) : 0;
				if(oldsel != newsel)
					wm_draw_selected_cell(id, x, y, newsel);
			}
		}
	}

	wm_redrawing = 0;
	cursor_update();
	mouse_show();
}

void wm_extract_range_command(int id, char *out)
{
	int a;
	int b;
	int p;
	int i;
	int len;

	out[0] = 0;
	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;

	a = wm_sel_y0 * WM_CONTENT_W + wm_sel_x0;
	b = wm_sel_y1 * WM_CONTENT_W + wm_sel_x1;
	if(a > b)
	{
		int t = a;
		a = b;
		b = t;
	}

	len = 0;
	for(p = a; p <= b && len < WM_INPUT_MAX - 1; p++)
	{
		if((p % WM_CONTENT_W) == 0 && p != a && len < WM_INPUT_MAX - 1)
			out[len++] = ' ';
		out[len++] = wm_buf_chars[id][p];
	}
	out[len] = 0;

	while(out[0] == ' ')
	{
		for(i = 0; out[i]; i++)
			out[i] = out[i + 1];
	}

	while(len > 0 && out[len - 1] == ' ')
	{
		out[len - 1] = 0;
		len--;
	}
}

void wm_finish_selected_cmd(int id)
{
	int i;
	int start;
	int len;

	if(id < 0 || id >= WM_WINDOWS)
		return;

	if(wm_selected_type == 1)
		wm_extract_line_command(id, wm_selected_row, wm_selected_cmd);
	else if(wm_selected_type == 2)
		wm_extract_range_command(id, wm_selected_cmd);
	else
		wm_selected_cmd[0] = 0;

	start = 0;
	for(i = 0; wm_selected_cmd[i]; i++)
		if(wm_selected_cmd[i] == '>')
			start = i + 1;

	while(wm_selected_cmd[start] == ' ')
		start++;

	if(start)
	{
		i = 0;
		while(wm_selected_cmd[start])
			wm_selected_cmd[i++] = wm_selected_cmd[start++];
		wm_selected_cmd[i] = 0;
	}

	len = 0;
	while(wm_selected_cmd[len])
		len++;
	while(len > 0 && wm_selected_cmd[len - 1] == ' ')
		wm_selected_cmd[--len] = 0;
}

void wm_clear_highlight(void)
{
	int oldwin;
	int oldtype;
	int oldrow;
	int oldx0;
	int oldy0;
	int oldx1;
	int oldy1;

	oldwin = wm_selected_win;
	oldtype = wm_selected_type;
	oldrow = wm_selected_row;
	oldx0 = wm_sel_x0;
	oldy0 = wm_sel_y0;
	oldx1 = wm_sel_x1;
	oldy1 = wm_sel_y1;

	wm_selected_win = -1;
	wm_selected_row = -1;
	wm_selected_type = 0;
	wm_selected_cmd[0] = 0;

	wm_redraw_selection_change(oldwin, oldtype, oldrow, oldx0, oldy0, oldx1, oldy1, -1, 0, -1, 0, 0, 0, 0);
}

void wm_select_line(int id, int row)
{
	int oldwin;
	int oldtype;
	int oldrow;
	int oldx0;
	int oldy0;
	int oldx1;
	int oldy1;
	int lastx;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;
	if(row < 0 || row >= wm_windows[id].h)
		return;

	oldwin = wm_selected_win;
	oldtype = wm_selected_type;
	oldrow = wm_selected_row;
	oldx0 = wm_sel_x0;
	oldy0 = wm_sel_y0;
	oldx1 = wm_sel_x1;
	oldy1 = wm_sel_y1;

	lastx = wm_windows[id].w - 1;
	if(lastx < 0)
		lastx = 0;

	wm_selected_win = id;
	wm_selected_row = row;
	wm_selected_type = 1;
	wm_sel_x0 = 0;
	wm_sel_y0 = row;
	wm_sel_x1 = lastx;
	wm_sel_y1 = row;
	wm_finish_selected_cmd(id);

	wm_redraw_selection_change(oldwin, oldtype, oldrow, oldx0, oldy0, oldx1, oldy1, id, 1, row, 0, row, lastx, row);
}

void wm_select_range(int id, int x0, int y0, int x1, int y1)
{
	int oldwin;
	int oldtype;
	int oldrow;
	int oldx0;
	int oldy0;
	int oldx1;
	int oldy1;
	WMWindow *win;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;
	win = &wm_windows[id];
	if(x0 < 0)
		x0 = 0;
	if(y0 < 0)
		y0 = 0;
	if(x1 < 0)
		x1 = 0;
	if(y1 < 0)
		y1 = 0;
	if(x0 >= win->w)
		x0 = win->w - 1;
	if(x1 >= win->w)
		x1 = win->w - 1;
	if(y0 >= win->h)
		y0 = win->h - 1;
	if(y1 >= win->h)
		y1 = win->h - 1;

	oldwin = wm_selected_win;
	oldtype = wm_selected_type;
	oldrow = wm_selected_row;
	oldx0 = wm_sel_x0;
	oldy0 = wm_sel_y0;
	oldx1 = wm_sel_x1;
	oldy1 = wm_sel_y1;

	wm_selected_win = id;
	wm_selected_row = -1;
	wm_selected_type = 2;
	wm_sel_x0 = x0;
	wm_sel_y0 = y0;
	wm_sel_x1 = x1;
	wm_sel_y1 = y1;
	wm_finish_selected_cmd(id);

	wm_redraw_selection_change(oldwin, oldtype, oldrow, oldx0, oldy0, oldx1, oldy1, id, 2, -1, x0, y0, x1, y1);
}

void screen_write_at(int x, int y, const char *s, u8 col)
{
	int i;

	i = 0;
	while(s[i] && x + i < W)
	{
		screen_put_at(x + i, y, s[i], col);
		i++;
	}
}

void screen_put2_at(int x, int y, u8 n, u8 col)
{
	screen_put_at(x, y, '0' + (n / 10), col);
	screen_put_at(x + 1, y, '0' + (n % 10), col);
}

void screen_redraw(void)
{
	int i;

	gfx_fill((color >> 4) & 0x0F);

	for(i = 0; i < W * H; i++)
		screen_draw_cell(i % W, i / W, screen_chars[i], screen_attrs[i], cursor_drawn && cursor_x == (unsigned int)(i % W) && cursor_y == (unsigned int)(i / W));
}


void gfx_pixel(int px, int py, u8 col)
{
	unsigned int off;
	u8 mask;

	if(px < 0 || px >= 640 || py < 0 || py >= 480)
		return;

	off = gfx_off(vga_start + py * 80 + (px >> 3));
	mask = 0x80 >> (px & 7);
	gfx_write_masked(off, mask, col & 0x0F);
}

int mouse_cell_hit(int x, int y)
{
	int x0;
	int y0;
	int x1;
	int y1;

	if(!mouse_drawn)
		return 0;

	x0 = mouse_old_x / 8;
	y0 = mouse_old_y / 8;
	x1 = (mouse_old_x + MOUSE_W - 1) / 8;
	y1 = (mouse_old_y + MOUSE_H - 1) / 8;

	return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

void mouse_redraw_area(int px, int py)
{
	int x;
	int y;
	int x0;
	int y0;
	int x1;
	int y1;
	int i;

	x0 = px / 8;
	y0 = py / 8;
	x1 = (px + MOUSE_W - 1) / 8;
	y1 = (py + MOUSE_H - 1) / 8;

	if(x0 < 0)
		x0 = 0;
	if(y0 < 0)
		y0 = 0;
	if(x1 >= W)
		x1 = W - 1;
	if(y1 >= H)
		y1 = H - 1;

	for(y = y0; y <= y1; y++)
	{
		for(x = x0; x <= x1; x++)
		{
			i = y * W + x;
			screen_draw_cell(x, y, screen_chars[i], screen_attrs[i], cursor_drawn && cursor_x == (unsigned int)x && cursor_y == (unsigned int)y);
		}
	}
}

void mouse_hide(void)
{
	if(!mouse_drawn)
		return;

	mouse_redraw_area(mouse_old_x, mouse_old_y);
	mouse_drawn = 0;
}

u8 screen_pixel_color(int px, int py)
{
	int tx;
	int ty;
	int rx;
	int ry;
	int i;
	u8 col;
	u8 fg;
	u8 bg;
	u8 bits;
	char c;

	if(px < 0 || px >= 640 || py < 0 || py >= 480)
		return 0;

	tx = px >> 3;
	ty = py >> 3;
	rx = px & 7;
	ry = py & 7;
	i = ty * W + tx;
	c = screen_chars[i];
	col = screen_attrs[i];

	fg = col & 0x0F;
	bg = (col >> 4) & 0x0F;

	if(cursor_drawn && cursor_x == (unsigned int)tx && cursor_y == (unsigned int)ty)
	{
		u8 t = fg;
		fg = bg;
		bg = t;
	}

	bits = (c == ' ') ? 0 : FONT8X8[((u8)c * 8) + ry];
	if(bits & (0x80 >> rx))
		return fg;

	return bg;
}

void mouse_draw_shape(int x, int y)
{
	int row;
	int col;
	static const u16 arrow[MOUSE_H] = {
		0x400, 0x600, 0x700, 0x780,
		0x7C0, 0x7E0, 0x7F0, 0x7F8,
		0x7C0, 0x6E0, 0x4E0, 0x070,
		0x070, 0x038, 0x038, 0x010
	};

	gfx_ready();
	for(row = 0; row < MOUSE_H; row++)
	{
		for(col = 0; col < MOUSE_W; col++)
		{
			if(arrow[row] & (0x400 >> col))
				gfx_pixel(x + col, y + row, screen_pixel_color(x + col, y + row) ^ 0x0F);
		}
	}
}

void mouse_show(void)
{
	if(!mouse_ready || mouse_drawn)
		return;

	mouse_draw_shape(mouse_x, mouse_y);
	mouse_old_x = mouse_x;
	mouse_old_y = mouse_y;
	mouse_drawn = 1;
}

int ps2_wait_input_clear(void)
{
	int t;

	for(t = 0; t < 100000; t++)
		if((inb(0x64) & 2) == 0)
			return 1;

	return 0;
}

int ps2_wait_output_full(void)
{
	int t;

	for(t = 0; t < 100000; t++)
		if(inb(0x64) & 1)
			return 1;

	return 0;
}

void mouse_write(u8 val)
{
	ps2_wait_input_clear();
	outb(0x64, 0xD4);
	ps2_wait_input_clear();
	outb(0x60, val);
	ps2_wait_output_full();
	inb(0x60);
}

void mouse_flush_aux(void)
{
	u8 st;
	int t;

	for(t = 0; t < 64; t++)
	{
		st = inb(0x64);
		if(!(st & 1))
			return;
		if(!(st & 0x20))
			return;
		inb(0x60);
	}
}

void ps2_flush_output(void)
{//discard anything sitting in the PS/2 output buffer,
//keyboard or mouse, so command replies aren't misread
	int t;

	for(t = 0; t < 64; t++)
	{
		if(!(inb(0x64) & 1))
			return;
		inb(0x60);
	}
}

void kb_drain_mouse(void)
{//throw away pending mouse bytes so modal getkey()
//loops (like the editor) never jam behind mouse data
	u8 st;
	int t;

	for(t = 0; t < 64; t++)
	{
		st = inb(0x64);
		if(!(st & 1))
			return;
		if(!(st & 0x20))
			return;
		inb(0x60);
		mouse_packet_index = 0;
	}
}

void mouse_init(void)
{
	u8 status;

	mouse_ready = 0;
	ps2_wait_input_clear();
	outb(0x64, 0xA8);

	ps2_flush_output();
	ps2_wait_input_clear();
	outb(0x64, 0x20);
	if(ps2_wait_output_full())
		status = inb(0x60);
	else
		status = 0x44;
	status &= ~2;
	status &= ~0x20;
	status &= ~0x10; //keep the keyboard clock enabled
	status |= 0x40;  //keep scancode translation ON (set 1),
	                 //QEMU scrambles every key without this
	ps2_wait_input_clear();
	outb(0x64, 0x60);
	ps2_wait_input_clear();
	outb(0x60, status);

	mouse_flush_aux();
	mouse_write(0xF6);
	mouse_write(0xF4);
	mouse_flush_aux();
	mouse_packet_index = 0;
	mouse_ready = 1;
	mouse_show();
}

void wm_move_window(int id, int nbx, int nby)
{
	WMWindow *win;
	int obx;
	int oby;

	if(id < 0 || id >= WM_WINDOWS)
		return;
	if(!wm_windows[id].alive)
		return;

	win = &wm_windows[id];
	if(win->fullscreen)
		return;
	/* No border clamp: dragging can place a window partly or fully
	   outside the screen. Visible cells are clipped by redraw code. */

	if(nbx == win->bx && nby == win->by)
		return;

	obx = win->bx;
	oby = win->by;
	win->bx = nbx;
	win->by = nby;
	win->x = nbx + 1;
	win->y = nby + 1;
	if(wm_active_ok())
		con_load(&wm_windows[wm_active]);
	wm_redraw_window_move(id, obx, oby);
}

void wm_mouse_down(int tx, int ty)
{
	int id;
	WMWindow *win;

	if(ty == 0 && tx >= WM_PLUS_X - 1 && tx <= WM_PLUS_X + 1)
	{
		if(wm_add_menu)
			wm_hide_add_menu();
		else
		{
			wm_add_menu = 1;
			wm_draw_add_menu();
		}
		return;
	}

	if(wm_handle_add_menu_click(tx, ty))
		return;

	id = wm_hit_window(tx, ty);
	if(id < 0)
		return;

	win = &wm_windows[id];
	if(ty == win->by && tx == win->bx + win->bw - WM_CLOSE_MARGIN)
	{
		wm_close_window(id);
		return;
	}

	wm_activate(id);
	win = &wm_windows[id];
	if((tx == win->bx || tx == win->bx + win->bw - 1 ||
	    ty == win->by || ty == win->by + win->bh - 1) &&
	   !win->fullscreen)
	{
		wm_mouse_select_win = -1;
		wm_drag_win = id;
		wm_drag_dx = tx - win->bx;
		wm_drag_dy = ty - win->by;
		return;
	}

	if(tx >= win->x && tx < win->x + win->w &&
	   ty >= win->y && ty < win->y + win->h)
	{
		wm_mouse_select_win = -1;
		cursor_update();
	}
}

void wm_mouse_up(void)
{
	wm_drag_win = -1;
	wm_mouse_select_win = -1;
}

void wm_mouse_drag(int tx, int ty)
{
	if(wm_drag_win >= 0)
	{
		wm_move_window(wm_drag_win, tx - wm_drag_dx, ty - wm_drag_dy);
		return;
	}
}

void wm_mouse_poll(void)
{
	u8 st;
	u8 b;
	int dx;
	int dy;
	int move_x;
	int move_y;
	int tx;
	int ty;
	int old_left;
	int new_left;
	int got_packet;
	int packets;
	int old_tx;
	int old_ty;

	if(!mouse_ready)
		return;

	move_x = 0;
	move_y = 0;
	got_packet = 0;
	packets = 0;
	old_left = mouse_left;
	new_left = mouse_left;
	old_tx = mouse_x / 8;
	old_ty = mouse_y / 8;

	while((inb(0x64) & 1) && packets < 16)
	{
		st = inb(0x64);
		if(!(st & 0x20))
			break;

		b = inb(0x60);

		if(mouse_packet_index == 0)
		{
			if(!(b & 0x08))
				continue;
			if(b & 0xC0)
				continue;
		}

		mouse_packet[mouse_packet_index++] = b;
		if(mouse_packet_index < 3)
			continue;

		mouse_packet_index = 0;
		packets++;

		if(!(mouse_packet[0] & 0x08))
			continue;
		if(mouse_packet[0] & 0xC0)
			continue;

		new_left = mouse_packet[0] & 1;
		dx = (int)(signed char)mouse_packet[1];
		dy = (int)(signed char)mouse_packet[2];

		move_x += dx;
		move_y -= dy;
		got_packet = 1;
	}

	if(!got_packet)
		return;

	mouse_hide();

	mouse_x += move_x;
	mouse_y += move_y;

	if(mouse_x < 0)
		mouse_x = 0;
	if(mouse_y < 0)
		mouse_y = 0;
	if(mouse_x > 639)
		mouse_x = 639;
	if(mouse_y > 479)
		mouse_y = 479;

	mouse_prev_left = old_left;
	mouse_left = new_left;
	tx = mouse_x / 8;
	ty = mouse_y / 8;

	if(wm_on)
	{
		if(mouse_left && !mouse_prev_left)
			wm_mouse_down(tx, ty);
		else if(mouse_left && mouse_prev_left)
		{
			if(tx != old_tx || ty != old_ty)
				wm_mouse_drag(tx, ty);
		}
		else if(!mouse_left && mouse_prev_left)
			wm_mouse_up();
	}

	mouse_show();
}


u8 cmos_read(u8 reg)
{//read CMOS data from 0x70
        outb(CMOS_ADDR, reg);
        return inb(CMOS_DATA);
}

u8 bcd_to_bin(u8 val)
{//BIN to decimal conversion
        return (val & 0x0F) + ((val >> 4) * 10);
}

typedef unsigned char u8;

typedef struct {
        u8 second;
        u8 minute;
        u8 hour;
        u8 day;
        u8 month;
        u8 year;
} RTCDateTime;

RTCDateTime rtc_get_datetime(void)
{//write certain RTC values to CMOS to get
//the date and time from RTC

        RTCDateTime t;

        t.second = bcd_to_bin(cmos_read(RTC_SECONDS));
        t.minute = bcd_to_bin(cmos_read(RTC_MINUTES));
        t.hour   = bcd_to_bin(cmos_read(RTC_HOURS));
        t.day    = bcd_to_bin(cmos_read(RTC_DAY));
        t.month  = bcd_to_bin(cmos_read(RTC_MONTH));
        t.year   = bcd_to_bin(cmos_read(RTC_YEAR));

        return t;
}

void cursor_update(void)
{//common cursor updater
	int old;
	int now;
	int ax;
	int ay;

	if(cursor_drawn && cursor_x < W && cursor_y < H)
	{
		old = cursor_y * W + cursor_x;
		screen_draw_cell(cursor_x, cursor_y, screen_chars[old], screen_attrs[old], 0);
	}

	ax = con_abs_x();
	ay = con_abs_y();
	cursor_x = ax;
	cursor_y = ay;

	if(wm_on && memstat_overlay_cell(ax, ay) && wm_hit_window(ax, ay) < 0)
	{
		cursor_drawn = 0;
		return;
	}

	cursor_drawn = 1;

	if(cursor_x < W && cursor_y < H)
	{
		now = cursor_y * W + cursor_x;
		screen_draw_cell(cursor_x, cursor_y, screen_chars[now], screen_attrs[now], 1);
	}
}

int strlen(const char *s)
{//return string length
	int i = 0;

	while(s[i])
		i++;

	return i;
}

int strcmp(const char *a, const char *b)
{//compare two strings
	int i = 0;

	while(a[i] && b[i] && a[i] == b[i])
		i++;

	return a[i] - b[i];
}

void strcpy(char *d, const char *s)
{//copy a string
	int i = 0;

	while(s[i])
	{
		d[i] = s[i];
		i++;
	}

	d[i] = 0;
}

void memset(void *p, int v, int n)
{//set a block value to one byte value
	u8 *b = (u8*)p;
	int i = 0;

	while(i < n)
	{
		b[i] = v;
		i++;
	}
}

void scroll(void)
{//scroll the screen
	int x;
	int y;
	int src;
	int dst;
	int limit;

	limit = (console_mode == CON_WINDOW) ? con_h : H;
	if(cy < (unsigned int)limit)
		return;

	cursor_hide();

	if(console_mode == CON_WINDOW)
	{
		if(wm_on && wm_active >= 0 && wm_active < WM_WINDOWS && wm_windows[wm_active].alive)
		{
			int id;
			id = wm_active;
			for(y = 0; y < con_h - 1; y++)
			{
				src = (y + 1) * WM_CONTENT_W;
				dst = y * WM_CONTENT_W;
				fast_memcpy8(wm_buf_chars[id] + dst, wm_buf_chars[id] + src, con_w);
				fast_memcpy8(wm_buf_attrs[id] + dst, wm_buf_attrs[id] + src, con_w);
			}
			dst = (con_h - 1) * WM_CONTENT_W;
			fast_memset8(wm_buf_chars[id] + dst, ' ', con_w);
			fast_memset8(wm_buf_attrs[id] + dst, color, con_w);
			cy = con_h - 1;
			wm_redraw_region(wm_windows[id].x, wm_windows[id].y, wm_windows[id].x + wm_windows[id].w - 1, wm_windows[id].y + wm_windows[id].h - 1);
			return;
		}

		mouse_hide();
		for(y = 0; y < con_h - 1; y++)
		{
			src = (con_y + y + 1) * W + con_x;
			dst = (con_y + y) * W + con_x;
			fast_memcpy8(screen_chars + dst, screen_chars + src, con_w);
			fast_memcpy8(screen_attrs + dst, screen_attrs + src, con_w);
		}

		dst = (con_y + con_h - 1) * W + con_x;
		fast_memset8(screen_chars + dst, ' ', con_w);
		fast_memset8(screen_attrs + dst, color, con_w);
		gfx_scroll_text_rect_up_8(con_x, con_y, con_w, con_h, (color >> 4) & 0x0F);
		cy = con_h - 1;
		mouse_show();
		return;
	}

	vga_movsl((void*)screen_chars, (const void*)(screen_chars + W), ((H - 1) * W) / 4);
	vga_movsl((void*)screen_attrs, (const void*)(screen_attrs + W), ((H - 1) * W) / 4);

	for(x = 0; x < W; x++)
	{
		dst = (H - 1) * W + x;
		screen_chars[dst] = ' ';
		screen_attrs[dst] = color;
	}

	gfx_scroll_up_8((color >> 4) & 0x0F);

	cy = H - 1;
}

void run_script(const char *path);
void clear(void);

void putc(char c)
{//place character
	int limit;
	int ax;
	int ay;

	limit = (console_mode == CON_WINDOW) ? con_w : W;

	if(c == '\n')
	{
		cx = 0;
		cy++;
		scroll();
		cursor_update();
		return;
	}

	if(c == '\b')
	{
		if(cx > 0)
			cx--;
		ax = con_abs_x();
		ay = con_abs_y();
		screen_put_at(ax, ay, ' ', color);
		cursor_update();
		return;
	}

	ax = con_abs_x();
	ay = con_abs_y();
	screen_put_at(ax, ay, c, color);
	cx++;

	if(cx >= (unsigned int)limit)
	{
		cx = 0;
		cy++;
	}

	scroll();
	cursor_update();
}

void printx(uint32_t x)
{//print a hexadecimal value
        char hex[] = "0123456789ABCDEF";

        putc('0');
	putc('x');

        for (int i = 28; i >= 0; i -= 4)
        {
                uint8_t digit = (x >> i) &  0xF;
                putc(hex[digit]);
        }
}


void print(const char *s)
{//simplified putc funtion that prints strings
	int i = 0;

	while(s[i])
	{
		putc(s[i]);
		i++;
	}

}

void print2(u8 n)
{//print an int with two digits
        putc('0' + (n / 10));
        putc('0' + (n % 10));
}

void rtc_print_datetime(void)
{//print RTC information
        RTCDateTime t = rtc_get_datetime();

        print2(t.month);
        putc('/');
        print2(t.day);
        putc('/');
        print("20");
        print2(t.year);

        putc(' ');

        print2(t.hour);
        putc(':');
        print2(t.minute);
        putc(':');
        print2(t.second);
}

void perror(char *line)
{//shell error funtion
	const u8 oldcolor = color;
	color = 0xCF;
	print("ERROR:");
	color = oldcolor;
	print(" Undefined refrence to \"");
	print(line);
	print("\" in line.\n");
	print("You can see ");
	color = 0x1C;
	print("/Help/CommandHelp.TXT");
	color = oldcolor;

	print(" for help on AneoEngine shell commands or you\ncan press ");
	color = 0x1C;
	print("F1");
	color = oldcolor;
	print(" to easily get to the help menu.\n");
}


void clear(void)
{//clear the screen
	int i;
	int x;
	int y;

	cursor_hide();

	if(console_mode == CON_WINDOW)
	{
		if(wm_on && wm_active >= 0 && wm_active < WM_WINDOWS && wm_windows[wm_active].alive)
		{
			int id;
			id = wm_active;
			for(y = 0; y < con_h; y++)
			{
				i = y * WM_CONTENT_W;
				fast_memset8(wm_buf_chars[id] + i, ' ', con_w);
				fast_memset8(wm_buf_attrs[id] + i, color, con_w);
			}
			cx = 0;
			cy = 0;
			wm_redraw_region(wm_windows[id].x, wm_windows[id].y, wm_windows[id].x + wm_windows[id].w - 1, wm_windows[id].y + wm_windows[id].h - 1);
			cursor_update();
			return;
		}

		mouse_hide();
		for(y = 0; y < con_h; y++)
		{
			i = (con_y + y) * W + con_x;
			fast_memset8(screen_chars + i, ' ', con_w);
			fast_memset8(screen_attrs + i, color, con_w);
		}

		gfx_clear_text_rect(con_x, con_y, con_w, con_h, (color >> 4) & 0x0F);
		cx = 0;
		cy = 0;
		cursor_update();
		mouse_show();
		return;
	}

	i = 0;
	gfx_fill((color >> 4) & 0x0F);

	while(i < W * H)
	{
		screen_chars[i] = ' ';
		screen_attrs[i] = color;
		i++;
	}

	cx = 0;
	cy = 1;
	cursor_update();
}

void printint(unsigned int n)
{//print an integer
	char buf[16];
	int i = 0;

	if (n == 0)
	{
		print("0");
		return;
	}

	while (n)
	{
		buf[i++] = '0' + (n % 10);
		n /= 10;
	}

	while (i--)
		putc(buf[i]);
}

void printad(const char *s, uint32_t x)
{//print a memory address
	print(s);
	print(":");
	printx(x);
	putc('\n');
}

void printadl(const char *s, uint32_t x)
{//print a memory address on the same line
        print(s);
        print(":");
        printx(x);
	print(" ");
}


void comment(const char *s)
{//comment something
	u8 oldcolor = color;
	color = 0x1A;
	print("//");
	print(s);
	putc('\n');
	color = oldcolor;
}

void printadocu(const char *s, uint32_t x1, uint32_t x2)
{//print a memory address that occupies from one address
//to another one
        print(s);
	print(":&OCU:");
        printx(x1);
	putc('-');
	printx(x2);
	putc('\n');
}

void indprintad(const char *s, uint32_t x)
{//same as printad but with an indentation
        print("        ");
	print(s);
        print(":");
        printx(x);
        putc('\n');
}

void indprintadocu(const char *s, uint32_t x1, uint32_t x2)
{//same as printadocu but with an indentation
        print("        ");
	print(s);
        print(":&OCU:");
        printx(x1);
        putc('-');
        printx(x2);
        putc('\n');
}

void poutw(u16 port, u16 val)
{//outw with verbose
	outw(port, val);
	print("VAL:");
	printx(val);
	print("->IOP:");
	printx(port);
	putc('\n');
}


void poutb(u16 port, u8 val)
{//outb with verbose
        outb(port, val);
        print("VAL:");
        printx(val);
        print("->IOP:");
        printx(port);
        putc('\n');
}

void poutwfail(u16 port, u16 val)
{//outw failure message
	print("ERR: Failed to write value to IO port\n");
	print("        VAL:");
        printx(val);
        print("->IOP:");
        printx(port);
	putc('\n');
}

void poutbfail(u16 port, u8 val)
{//outb failure message
        print("ERR: Failed to write value to IO port\n");
        print("        VAL:");
        printx(val);
        print("->IOP:");
        printx(port);
        putc('\n');
}

void ata_debug(void) {
        print("ATA status: \n");
        u16 port = 0x1F7;
	u8 s = inb(port);
        printx(port);
	print(" = ");
	printx(s);
	putc('\n');
	if (s == 0xFF)
                print("        Unknown ATA bus\n");
        else if (s == 0x00)
                print("        No ATA drive ready\n");
        else
                print("        ATA drive ready\n");
}


int rtc_get_second(void)
{//get the current second from the RTC
        outb(0x70, 0x00);
        return inb(0x71);
}


#define RTC_X  0
#define RTC_Y  0


void update_rtc_only(void)
{//update RTC line
	RTCDateTime t;
	u8 col;
	int x;
	int y;

	t = rtc_get_datetime();
	col = 0xF1;
	x = RTC_X;
	y = RTC_Y;

	screen_put2_at(x, y, t.month, col);
	screen_put_at(x + 2, y, '/', col);
	screen_put2_at(x + 3, y, t.day, col);
	screen_put_at(x + 5, y, '/', col);
	screen_put_at(x + 6, y, '2', col);
	screen_put_at(x + 7, y, '0', col);
	screen_put2_at(x + 8, y, t.year, col);
	screen_put_at(x + 10, y, ' ', col);
	screen_put2_at(x + 11, y, t.hour, col);
	screen_put_at(x + 13, y, ':', col);
	screen_put2_at(x + 14, y, t.minute, col);
	screen_put_at(x + 16, y, ':', col);
	screen_put2_at(x + 17, y, t.second, col);
}

void draw_tb(void)
{//draw top bar
	int x;

	for(x = 0; x < W; x++)
		screen_put_at(x, 0, ' ', 0xF1);

	update_rtc_only();
	if(wm_on)
		wm_draw_top_controls();
}

void redraw_shell_input(char *buf, int len)
{//Ctrl-L: redraw the full-screen shell prompt and keep the typed command.
	int n;
	u8 oldcolor;

	oldcolor = color;

	if(!wm_on)
		draw_tb();

	update_rtc_only();

	if(raw == 0)
	{
		color = 0x1F;
		as_pwd();
		print(">");
	}

	color = 0x1A;
	for(n = 0; n < len && buf[n]; n++)
		putc(buf[n]);

	color = oldcolor;
	cursor_update();
}

#define KEY_UP    0x101
#define KEY_DOWN  0x102
#define KEY_LEFT  0x103
#define KEY_RIGHT 0x104

int readline(char *buf, int max)
{//read input
        int i = 0;
        int c;
        int last_sec = -1;
        int sec;
        int maxx;
        int maxy;

        for(;;)
        {//while waiting, update the RTC from the top bar
                sec = rtc_get_second();

		if(sec != last_sec)
		{
        		update_rtc_only();
        		last_sec = sec;
		}
		if(wm_on)
			wm_mouse_poll();
                c = getkey();

                if(!c)
                        continue;

		if(wm_on && c == '\t')
		{
			buf[i] = 0;
			return 1;
		}

		if(wm_on && c == 6)
		{
			buf[i] = 0;
			if(wm_active_ok())
			{
				con_save(&wm_windows[wm_active]);
				wm_toggle_fullscreen(wm_active);
			}
			i = 0;
			continue;
		}

		if(wm_on && c == 3)
		{
			buf[i] = 0;
			if(wm_active_ok())
			{
				con_save(&wm_windows[wm_active]);
				wm_toggle_card(wm_active);
			}
			i = 0;
			continue;
		}

                if(c == '\n')
                {
			if(wm_on && wm_selected_win == wm_active && wm_selected_cmd[0])
			{
				strcpy(buf, wm_selected_cmd);
				wm_clear_highlight();
				wm_prepare_selected_run_line(buf);
				return 0;
			}
                        buf[i] = 0;
                        putc('\n');
                        return 0;
                }

                if(c == '\b')
                {
                        if(i > 0)
                        {
                                i--;
                                putc('\b');
                        }
                        continue;
                }

		maxx = (console_mode == CON_WINDOW) ? con_w : W;
		maxy = (console_mode == CON_WINDOW) ? con_h : H;

		if(c == KEY_UP || c == KEY_DOWN || c == KEY_LEFT || c == KEY_RIGHT)
		{
			unsigned int oldcx;
			unsigned int oldcy;

			oldcx = cx;
			oldcy = cy;

			if(wm_on && console_mode == CON_WINDOW && shift && !wm_shift_selecting)
			{
				wm_shift_ax = cx;
				wm_shift_ay = cy;
				wm_shift_selecting = 1;
			}

			if(c == KEY_UP)
			{
				if(cy > 0)
					cy--;
			}
			else if(c == KEY_DOWN)
			{
				if(cy + 1 < (unsigned int)maxy)
					cy++;
			}
			else if(c == KEY_LEFT)
			{
				if(cx > 0)
					cx--;
			}
			else if(c == KEY_RIGHT)
			{
				if(cx + 1 < (unsigned int)maxx)
					cx++;
			}

			if(wm_on && console_mode == CON_WINDOW)
			{
				if(shift)
					wm_select_range(wm_active, wm_shift_ax, wm_shift_ay, cx, cy);
				else
				{
					wm_shift_selecting = 0;
					if(wm_selected_type != 2 || wm_selected_win != wm_active)
						wm_select_line(wm_active, cy);
				}
			}
			else
			{
				(void)oldcx;
				(void)oldcy;
			}

			cursor_update();
			continue;
		}

		if(c == 12)
		{
			buf[i] = 0;
			clear();
			redraw_shell_input(buf, i);
			continue;
		}

		if(i < max - 1)
                {
			if(wm_on)
			{
				wm_shift_selecting = 0;
				wm_clear_highlight();
			}
                        buf[i] = c;
                        i++;
                        putc(c);
                }
        }
	return 0;
}

int starts(const char *s, const char *p)
{
	int i = 0;

	while(p[i])
	{
		if(s[i] != p[i])
			return 0;

		i++;
	}

	return 1;
}

int ends(const char *s, const char *p)
{
	if (!s || !p)
		return 0;
	int s_len = strlen(s);
    	int p_len = strlen(p);
    	if (p_len > s_len)
		return 0;
    	int i = p_len - 1;
    	while (i >= 0)
	{
        	if (s[s_len - p_len + i] != p[i])
            		return 0;
        	i--;
    	}
    	return 1;
}


char *skip(char *s)
{
	while(*s == ' ')
		s++;

	return s;
}


int atoi(const char *s)
{
	int n = 0;

	while(*s >= '0' && *s <= '9')
	{
		n = n * 10 + (*s - '0');
		s++;
	}

	return n;
}

u32 cpuid_threads()
{
	u32 ebx;

	__asm__ volatile(
		"mov $1, %%eax\n"
		"cpuid"
		: "=b"(ebx)
		:
		: "eax",
		  "ecx",
		  "edx"
	);

	return
	(
		ebx
		>>
		16
	)
	&
	255U;
}


void cpustat(void)
{
	u32 threads;
	u32 used_mb;
	u32 total_mb;
	u32 i;
	u32 t;

	while(1)
	{
		color=0x0F;

		clear();

		threads=cpuid_threads();

		if(threads==0)
			threads=1;

		used_mb=mem_used>>20;

		total_mb=total_mem>>20;

		print(
			"USAGE                                       MEMORY:\n\n"
		);

		for(i=0;i<threads;i++)
		{
			print("CPU Thread ");

			printint(i);

			print(":                 ");

			print("0");

			print("%    ");

			printx(0);

			print("\n");
		}

		print("\nMemory:     ");

		printint(used_mb);

		print("MB/");

		printint(total_mb);

		print("MB    ");

		printx(mem_used);

		print("\n");

		for(t=0;t<100;t++)
		{
			if(getkey()==27)
			{
				color = 0x1F;
				clear();
				return;
			}

			sleep(10);
		}
	}
}

void trim_end(char *s)
{
	int i;

	i = 0;
	while(s[i])
		i++;

	while(i > 0 &&
	      (s[i - 1] == ' ' ||
	       s[i - 1] == '\n' ||
	       s[i - 1] == '\r' ||
	       s[i - 1] == '\t'))
	{
		s[i - 1] = 0;
		i--;
	}
}

void as_two_args(const char *s, char *a, char *b)
{
	int i = 0;
	int j = 0;

	while(*s == ' ')
		s++;

	while(*s && *s != ' ')
		a[i++] = *s++;

	a[i] = 0;

	while(*s == ' ')
		s++;

	while(*s && *s != ' ')
		b[j++] = *s++;

	b[j] = 0;
}

void rmsemiv(char *line)
{
        if (!line)
                return;

        int i = 0;
        int j = 0;

        while (line[i])
        {
                if (line[i] == ';')
                {
                        i += 3;
                }
                else
                {
                        line[j] = line[i];
                        j++;
                        i++;
                }
        }
        line[j] = '\0';
}


void rmsemi(char *line)
{
        if (!line)
                return;

        int i = 0;
        int j = 0;

        while (line[i])
        {
                if (line[i] == '\"' && line[i + 1] == ')' && line[i + 2] == ';')
                {
                        i += 3;
                }
                else
                {
                        line[j] = line[i];
                        j++;
                        i++;
                }
        }
        line[j] = '\0';
}

void rmsemia(char *line)
{
        if (!line)
                return;

        int i = 0;
        int j = 0;

        while (line[i])
        {
                if (line[i] == ')' && line[i + 1] == ';')
                {
                        i += 3;
                }
                else
                {
                        line[j] = line[i];
                        j++;
                        i++;
                }
        }
        line[j] = '\0';
}

u8 hexval(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';

	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;

	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	return 0;
}

u32 atox(const char *s)
{
	u32 value = 0;

	while(*s)
	{
		char c = *s++;

		if(c >= '0' && c <= '9')
			value = (value << 4) | (c - '0');
		else if(c >= 'a' && c <= 'f')
			value = (value << 4) | (c - 'a' + 10);
		else if(c >= 'A' && c <= 'F')
			value = (value << 4) | (c - 'A' + 10);
		else
			break;
	}

	return value;
}

u32 parsex(const char *s)
{
	if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		return atox(s + 2);

	return atoi(s);
}

#define CP_MAX_DRIVES 4
#define CP_NONE 0
#define CP_ATA 1
#define CP_ATAPI 2

typedef unsigned char CP_U8;
typedef unsigned short CP_U16;
typedef unsigned int CP_U32;
typedef unsigned long long CP_U64;

typedef struct {
	CP_U16 io;
	CP_U16 ctrl;
	CP_U8 slave;
	CP_U8 type;
	CP_U8 lba48;
	CP_U64 sectors;
	CP_U32 block_size;
	char model[41];
} CPDrive;

static CPDrive cp_drives[CP_MAX_DRIVES];
static CP_U16 cp_buffer[1024];

static inline CP_U8 cp_inb(CP_U16 port)
{
	CP_U8 value;

	asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static inline CP_U16 cp_inw(CP_U16 port)
{
	CP_U16 value;

	asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static inline void cp_outb(CP_U16 port, CP_U8 value)
{
	asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void cp_outw(CP_U16 port, CP_U16 value)
{
	asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void cp_delay(CPDrive *drive)
{
	cp_inb(drive->ctrl);
	cp_inb(drive->ctrl);
	cp_inb(drive->ctrl);
	cp_inb(drive->ctrl);
}

static int cp_wait_not_busy(CPDrive *drive)
{
	CP_U32 timeout = 10000000;

	while(timeout--)
		if(!(cp_inb(drive->io + 7) & 0x80))
			return 1;

	return 0;
}

static int cp_wait_drq(CPDrive *drive)
{
	CP_U32 timeout = 10000000;
	CP_U8 status;

	while(timeout--) {
		status = cp_inb(drive->io + 7);

		if(status & 0x21)
			return 0;

		if(!(status & 0x80) && (status & 0x08))
			return 1;
	}

	return 0;
}

static int cp_wait_done(CPDrive *drive)
{
	CP_U8 status;

	if(!cp_wait_not_busy(drive))
		return 0;

	status = cp_inb(drive->io + 7);

	if(status & 0x21)
		return 0;

	return 1;
}

static void cp_set_name(CPDrive *drive, const char *name)
{
	CP_U32 i = 0;

	while(name[i] && i < 40) {
		drive->model[i] = name[i];
		i++;
	}

	drive->model[i] = 0;
}

static void cp_model_from_identify(CPDrive *drive, CP_U16 *identify)
{
	CP_U32 i;
	CP_U32 end;

	for(i = 0; i < 20; i++) {
		drive->model[i * 2] = (char)(identify[27 + i] >> 8);
		drive->model[i * 2 + 1] =
			(char)(identify[27 + i] & 0xFF);
	}

	drive->model[40] = 0;
	end = 40;

	while(end &&
	      (drive->model[end - 1] == ' ' ||
	       drive->model[end - 1] == 0))
		end--;

	drive->model[end] = 0;
}

static int cp_atapi_packet_read(CPDrive *drive,
				CP_U8 *packet,
				CP_U32 bytes,
				CP_U8 *buffer)
{
	CP_U32 i;
	CP_U32 done = 0;
	CP_U32 count;
	CP_U32 words;
	CP_U16 value;

	if(!cp_wait_not_busy(drive))
		return 0;

	cp_outb(drive->io + 6,
		0xA0 | (drive->slave << 4));

	cp_delay(drive);

	cp_outb(drive->io + 1, 0);
	cp_outb(drive->io + 2, 0);
	cp_outb(drive->io + 3, 0);
	cp_outb(drive->io + 4,
		(CP_U8)(bytes & 0xFF));
	cp_outb(drive->io + 5,
		(CP_U8)((bytes >> 8) & 0xFF));
	cp_outb(drive->io + 7, 0xA0);

	if(!cp_wait_drq(drive))
		return 0;

	for(i = 0; i < 6; i++) {
		cp_outw(drive->io,
			(CP_U16)packet[i * 2] |
			((CP_U16)packet[i * 2 + 1] << 8));
	}

	while(done < bytes) {
		if(!cp_wait_drq(drive))
			return 0;

		count = (CP_U32)cp_inb(drive->io + 4);
		count |= (CP_U32)cp_inb(drive->io + 5) << 8;

		if(!count)
			return 0;

		words = (count + 1) >> 1;

		for(i = 0; i < words; i++) {
			value = cp_inw(drive->io);

			if(done + i * 2 < bytes) {
				buffer[done + i * 2] =
					(CP_U8)(value & 0xFF);
			}

			if(done + i * 2 + 1 < bytes) {
				buffer[done + i * 2 + 1] =
					(CP_U8)(value >> 8);
			}
		}

		done += count;
	}

	return cp_wait_done(drive);
}

static int cp_atapi_capacity(CPDrive *drive)
{
	CP_U8 packet[12];
	CP_U8 data[8];
	CP_U32 i;
	CP_U32 last_lba;
	CP_U32 block_size;

	for(i = 0; i < 12; i++)
		packet[i] = 0;

	packet[0] = 0x25;

	if(!cp_atapi_packet_read(drive,
				 packet,
				 8,
				 data))
		return 0;

	last_lba =
		((CP_U32)data[0] << 24) |
		((CP_U32)data[1] << 16) |
		((CP_U32)data[2] << 8) |
		(CP_U32)data[3];

	block_size =
		((CP_U32)data[4] << 24) |
		((CP_U32)data[5] << 16) |
		((CP_U32)data[6] << 8) |
		(CP_U32)data[7];

	drive->sectors = (CP_U64)last_lba + 1;
	drive->block_size = block_size;

	return 1;
}

static int cp_identify(CPDrive *drive)
{
	CP_U16 identify[256];
	CP_U32 i;
	CP_U8 mid;
	CP_U8 high;
	CP_U8 status;

	drive->type = CP_NONE;
	drive->lba48 = 0;
	drive->sectors = 0;
	drive->block_size = 0;
	drive->model[0] = 0;

	cp_outb(drive->io + 6,
		0xA0 | (drive->slave << 4));

	cp_delay(drive);

	cp_outb(drive->io + 2, 0);
	cp_outb(drive->io + 3, 0);
	cp_outb(drive->io + 4, 0);
	cp_outb(drive->io + 5, 0);
	cp_outb(drive->io + 7, 0xEC);

	cp_delay(drive);

	status = cp_inb(drive->io + 7);

	if(!status)
		return 0;

	if(!cp_wait_not_busy(drive))
		return 0;

	mid = cp_inb(drive->io + 4);
	high = cp_inb(drive->io + 5);

	if((mid == 0x14 && high == 0xEB) ||
	   (mid == 0x69 && high == 0x96)) {
		drive->type = CP_ATAPI;

		cp_outb(drive->io + 7, 0xA1);

		if(!cp_wait_drq(drive))
			return 0;
	} else {
		drive->type = CP_ATA;

		if(!cp_wait_drq(drive))
			return 0;
	}

	for(i = 0; i < 256; i++)
		identify[i] = cp_inw(drive->io);

	cp_model_from_identify(drive, identify);

	if(drive->type == CP_ATA) {
		drive->block_size = 512;

		if((identify[83] & (1 << 10)) &&
		   (identify[100] ||
		    identify[101] ||
		    identify[102] ||
		    identify[103])) {
			drive->lba48 = 1;

			drive->sectors =
				(CP_U64)identify[100] |
				((CP_U64)identify[101] << 16) |
				((CP_U64)identify[102] << 32) |
				((CP_U64)identify[103] << 48);
		} else {
			drive->sectors =
				(CP_U64)identify[60] |
				((CP_U64)identify[61] << 16);
		}

		if(!drive->model[0])
			cp_set_name(drive, "ATA Disk");

		return drive->sectors != 0;
	}

	if(!drive->model[0])
		cp_set_name(drive, "ATAPI CD-ROM");

	return cp_atapi_capacity(drive);
}

static int cp_scan_drives(void)
{
	CP_U32 i;
	CP_U16 ios[4] = {
		0x1F0,
		0x1F0,
		0x170,
		0x170
	};
	CP_U16 ctrls[4] = {
		0x3F6,
		0x3F6,
		0x376,
		0x376
	};
	CP_U8 slaves[4] = {
		0,
		1,
		0,
		1
	};
	int count = 0;

	for(i = 0; i < CP_MAX_DRIVES; i++) {
		cp_drives[i].io = ios[i];
		cp_drives[i].ctrl = ctrls[i];
		cp_drives[i].slave = slaves[i];

		if(cp_identify(&cp_drives[i]))
			count++;
	}

	return count;
}

static int cp_find_source(void)
{
	CP_U8 bios_drive =
		*((volatile CP_U8 *)0x0500);

	CP_U32 wanted;
	CP_U32 found;
	CP_U32 i;
	int first_ata = -1;
	int first_atapi = -1;

	for(i = 0; i < CP_MAX_DRIVES; i++) {
		if(cp_drives[i].type == CP_ATA &&
		   first_ata < 0)
			first_ata = (int)i;

		if(cp_drives[i].type == CP_ATAPI &&
		   first_atapi < 0)
			first_atapi = (int)i;
	}

	if(bios_drive < 0x80)
		return first_atapi;

	if(bios_drive >= 0xE0) {
		wanted = bios_drive - 0xE0;
		found = 0;

		for(i = 0; i < CP_MAX_DRIVES; i++) {
			if(cp_drives[i].type != CP_ATAPI)
				continue;

			if(found == wanted)
				return (int)i;

			found++;
		}

		return first_atapi;
	}

	wanted = bios_drive - 0x80;
	found = 0;

	for(i = 0; i < CP_MAX_DRIVES; i++) {
		if(cp_drives[i].type != CP_ATA)
			continue;

		if(found == wanted)
			return (int)i;

		found++;
	}

	return first_ata;
}

static int cp_ata_read(CPDrive *drive,
		       CP_U64 lba,
		       CP_U16 *buffer)
{
	CP_U32 i;

	if(!cp_wait_not_busy(drive))
		return 0;

	if(drive->lba48) {
		cp_outb(drive->io + 6,
			0x40 | (drive->slave << 4));

		cp_delay(drive);

		cp_outb(drive->io + 2, 0);
		cp_outb(drive->io + 3,
			(CP_U8)(lba >> 24));
		cp_outb(drive->io + 4,
			(CP_U8)(lba >> 32));
		cp_outb(drive->io + 5,
			(CP_U8)(lba >> 40));

		cp_outb(drive->io + 2, 1);
		cp_outb(drive->io + 3,
			(CP_U8)lba);
		cp_outb(drive->io + 4,
			(CP_U8)(lba >> 8));
		cp_outb(drive->io + 5,
			(CP_U8)(lba >> 16));

		cp_outb(drive->io + 7, 0x24);
	} else {
		if(lba > 0x0FFFFFFFULL)
			return 0;

		cp_outb(drive->io + 6,
			0xE0 |
			(drive->slave << 4) |
			((CP_U8)(lba >> 24) & 0x0F));

		cp_delay(drive);

		cp_outb(drive->io + 2, 1);
		cp_outb(drive->io + 3,
			(CP_U8)lba);
		cp_outb(drive->io + 4,
			(CP_U8)(lba >> 8));
		cp_outb(drive->io + 5,
			(CP_U8)(lba >> 16));

		cp_outb(drive->io + 7, 0x20);
	}

	if(!cp_wait_drq(drive))
		return 0;

	for(i = 0; i < 256; i++)
		buffer[i] = cp_inw(drive->io);

	return cp_wait_done(drive);
}

static int cp_ata_write(CPDrive *drive,
			CP_U64 lba,
			CP_U16 *buffer)
{
	CP_U32 i;

	if(!cp_wait_not_busy(drive))
		return 0;

	if(drive->lba48) {
		cp_outb(drive->io + 6,
			0x40 | (drive->slave << 4));

		cp_delay(drive);

		cp_outb(drive->io + 2, 0);
		cp_outb(drive->io + 3,
			(CP_U8)(lba >> 24));
		cp_outb(drive->io + 4,
			(CP_U8)(lba >> 32));
		cp_outb(drive->io + 5,
			(CP_U8)(lba >> 40));

		cp_outb(drive->io + 2, 1);
		cp_outb(drive->io + 3,
			(CP_U8)lba);
		cp_outb(drive->io + 4,
			(CP_U8)(lba >> 8));
		cp_outb(drive->io + 5,
			(CP_U8)(lba >> 16));

		cp_outb(drive->io + 7, 0x34);
	} else {
		if(lba > 0x0FFFFFFFULL)
			return 0;

		cp_outb(drive->io + 6,
			0xE0 |
			(drive->slave << 4) |
			((CP_U8)(lba >> 24) & 0x0F));

		cp_delay(drive);

		cp_outb(drive->io + 2, 1);
		cp_outb(drive->io + 3,
			(CP_U8)lba);
		cp_outb(drive->io + 4,
			(CP_U8)(lba >> 8));
		cp_outb(drive->io + 5,
			(CP_U8)(lba >> 16));

		cp_outb(drive->io + 7, 0x30);
	}

	if(!cp_wait_drq(drive))
		return 0;

	for(i = 0; i < 256; i++)
		cp_outw(drive->io, buffer[i]);

	return cp_wait_done(drive);
}

static int cp_ata_flush(CPDrive *drive)
{
	if(!cp_wait_not_busy(drive))
		return 0;

	cp_outb(drive->io + 6,
		0xE0 | (drive->slave << 4));

	cp_outb(drive->io + 7,
		drive->lba48 ? 0xEA : 0xE7);

	return cp_wait_done(drive);
}

static int cp_atapi_read(CPDrive *drive,
			 CP_U32 lba,
			 CP_U16 *buffer)
{
	CP_U8 packet[12];
	CP_U32 i;

	for(i = 0; i < 12; i++)
		packet[i] = 0;

	packet[0] = 0xA8;
	packet[2] = (CP_U8)(lba >> 24);
	packet[3] = (CP_U8)(lba >> 16);
	packet[4] = (CP_U8)(lba >> 8);
	packet[5] = (CP_U8)lba;
	packet[9] = 1;

	return cp_atapi_packet_read(drive,
				     packet,
				     2048,
				     (CP_U8 *)buffer);
}

static void cp_print_u32(CP_U32 value)
{
	char buffer[11];
	CP_U32 count = 0;

	if(!value) {
		putc('0');
		return;
	}

	while(value) {
		buffer[count++] =
			'0' + value % 10;
		value /= 10;
	}

	while(count)
		putc(buffer[--count]);
}

static CP_U32 cp_mib(CPDrive *drive)
{
	if(drive->block_size == 2048)
		return (CP_U32)(drive->sectors >> 9);

	return (CP_U32)(drive->sectors >> 11);
}

static void cp_print_location(CPDrive *drive)
{
	if(drive->io == 0x1F0)
		print("Primary ");
	else
		print("Secondary ");

	if(drive->slave)
		print("Slave");
	else
		print("Master");
}

static CP_U32 cp_read_line(char *buffer,
			   CP_U32 max)
{
	CP_U32 length = 0;
	int key;

	while(1) {
		key = getkey();

		if(key == '\r' || key == '\n') {
			putc('\n');
			buffer[length] = 0;
			return length;
		}

		if((key == '\b' || key == 127) &&
		   length) {
			length--;

			putc('\b');
			putc(' ');
			putc('\b');

			continue;
		}

		if(key >= 32 &&
		   key <= 126 &&
		   length + 1 < max) {
			buffer[length++] = (char)key;
			putc((char)key);
		}
	}
}

static CP_U32 cp_parse_number(char *buffer)
{
	CP_U32 value = 0;
	CP_U32 i = 0;

	while(buffer[i] >= '0' &&
	      buffer[i] <= '9') {
		value =
			value * 10 +
			(CP_U32)(buffer[i] - '0');

		i++;
	}

	if(buffer[i])
		return 0;

	return value;
}

static int cp_is_yes(char *buffer)
{
	return
		(buffer[0] == 'Y' ||
		 buffer[0] == 'y') &&
		(buffer[1] == 'E' ||
		 buffer[1] == 'e') &&
		(buffer[2] == 'S' ||
		 buffer[2] == 's') &&
		buffer[3] == 0;
}

static void cp_progress(CP_U32 mib)
{
	print("Copied ");
	cp_print_u32(mib);
	print(" MiB\n");
}

static int cp_copy_ata(CPDrive *source,
		       CPDrive *target)
{
	CP_U64 lba;

	for(lba = 0;
	    lba < source->sectors;
	    lba++) {
		if(!cp_ata_read(source,
				lba,
				cp_buffer)) {
			print("\nRead failed at sector ");
			cp_print_u32((CP_U32)lba);
			putc('\n');

			return 0;
		}

		if(!cp_ata_write(target,
				 lba,
				 cp_buffer)) {
			print("\nWrite failed at sector ");
			cp_print_u32((CP_U32)lba);
			putc('\n');

			return 0;
		}

		if(!(lba & 0x7FFF))
			cp_progress(
				(CP_U32)(lba >> 11));
	}

	return cp_ata_flush(target);
}

static int cp_copy_atapi(CPDrive *source,
			 CPDrive *target)
{
	CP_U64 lba;
	CP_U64 target_lba;
	CP_U32 part;

	for(lba = 0;
	    lba < source->sectors;
	    lba++) {
		if(!cp_atapi_read(source,
				  (CP_U32)lba,
				  cp_buffer)) {
			print("\nRead failed at CD sector ");
			cp_print_u32((CP_U32)lba);
			putc('\n');

			return 0;
		}

		target_lba = lba << 2;

		for(part = 0; part < 4; part++) {
			if(!cp_ata_write(
				   target,
				   target_lba + part,
				   cp_buffer + part * 256)) {
				print(
					"\nWrite failed at disk sector ");

				cp_print_u32(
					(CP_U32)(
						target_lba +
						part));

				putc('\n');

				return 0;
			}
		}

		if(!(lba & 0x1FFF))
			cp_progress(
				(CP_U32)(lba >> 9));
	}

	return cp_ata_flush(target);
}

void cpdrive(void)
{
	int source_index;
	int targets[CP_MAX_DRIVES];
	int target_count = 0;
	int target_index;
	CP_U32 i;
	CP_U32 choice;
	CP_U64 required_sectors;
	char input[16];
	CPDrive *source;
	CPDrive *target;

	print("\nScanning drives...\n");

	if(!cp_scan_drives()) {
		print("No IDE devices found.\n");
		return;
	}

	source_index = cp_find_source();

	if(source_index < 0) {
		print(
			"Could not map the BIOS boot drive "
			"to an IDE device.\n");

		return;
	}

	source = &cp_drives[source_index];

	if(source->type == CP_ATAPI &&
	   source->block_size != 2048) {
		print(
			"The boot CD does not use "
			"2048-byte logical sectors.\n");

		return;
	}

	print("\nSource: Current disk - ");
	print(source->model);
	print(" ");
	cp_print_u32(cp_mib(source));
	print(" MiB (");
	cp_print_location(source);
	print(")\n\n");

	for(i = 0; i < CP_MAX_DRIVES; i++) {
		if((int)i == source_index ||
		   cp_drives[i].type != CP_ATA)
			continue;

		targets[target_count] = (int)i;

		print("Device ");
		cp_print_u32(
			(CP_U32)target_count + 1);
		print(": ");
		print(cp_drives[i].model);
		print(" ");
		cp_print_u32(
			cp_mib(&cp_drives[i]));
		print(" MiB (");
		cp_print_location(&cp_drives[i]);
		print(")\n");

		target_count++;
	}

	if(!target_count) {
		print(
			"No writable destination "
			"disks found.\n");

		return;
	}

	print("\n>");
	cp_read_line(input, sizeof(input));

	choice = cp_parse_number(input);

	if(!choice ||
	   choice > (CP_U32)target_count) {
		print("Invalid device.\n");
		return;
	}

	target_index = targets[choice - 1];
	target = &cp_drives[target_index];

	if(source->type == CP_ATAPI)
		required_sectors =
			source->sectors << 2;
	else
		required_sectors =
			source->sectors;

	if(target->sectors < required_sectors) {
		print("Destination is too small.\n");
		return;
	}

	print("\nTHIS ERASES DEVICE ");
	cp_print_u32(choice);
	print(" COMPLETELY.\n");
	print("Type YES to continue: ");

	cp_read_line(input, sizeof(input));

	if(!cp_is_yes(input)) {
		print("Cancelled.\n");
		return;
	}

	print("\nInstalling AneoEngine to Device ");
	cp_print_u32(choice);
	print("\n");

	if(source->type == CP_ATAPI) {
		if(!cp_copy_atapi(source, target)) {
			print("Copy failed.\n");
			return;
		}
	} else {
		if(!cp_copy_ata(source, target)) {
			print("Copy failed.\n");
			return;
		}
	}

	cp_progress(cp_mib(source));
	print("\nAneoEngine media copy complete.\n");
}

u16 u16port = 0x0;
u16 u16val = 0x0;
u8 u8val = 0x0;

int wm_shell_target_window(void)
{
	if(wm_task_exec_win >= 0 && wm_task_exec_win < WM_WINDOWS && wm_windows[wm_task_exec_win].alive)
		return wm_task_exec_win;

	if(wm_on && wm_active_ok())
		return wm_active;

	return -1;
}


void shell_exec(char *line)
{
	char arg1[INPUT_MAX];
	char arg2[INPUT_MAX];
	int wid;
	int len;

	line = skip(line);

	len = strlen(line);
	while(len > 0 &&
		(line[len - 1] == '\r' ||
		line[len - 1] == '\n' ||
		line[len - 1] == ' ' ||
		line[len - 1] == '\t'))
	{
		line[len - 1] = 0;
		len--;
	}

	if(line[0] == 0)
		return;

	if(line[0] == '/' && line[1] == '/')
		return;

	color = 0x1F;

	if(strcmp(line, "fault;") == 0)
	{
		asm volatile("mov $0x1234, %ax");
		asm volatile("mov %ax, %ds");
	}
	else if(strcmp(line, "cls;") == 0)
		clear();
	else if(strcmp(line, "reboot;") == 0)
		triple_fault();
	else if(strcmp(line, "n;") == 0)
		entropy();
	else if(strcmp(line, "cpdrive;") == 0)
	{
		clear();
		cpdrive();
	}
	else if(strcmp(line, "help;") == 0)
		wm_open_help_window();
	else if(strcmp(line, "__help;") == 0)
		helpMenu();
	else if(starts(line, "color(") && ends(line, ");"))
	{
		rmsemia(line);
		color = atoi(skip(line + 5));
	}
	else if(starts(line, "u16port = ") && ends(line, ";"))
	{
		rmsemiv(line);
		u16port = parsex(line + 10);
	}
	else if(starts(line, "u16val = ") && ends(line, ";"))
	{
		rmsemiv(line);
		u16val = parsex(line + 9);
	}
	else if(starts(line, "u8val = ") && ends(line, ";"))
	{
		rmsemiv(line);
		u8val = parsex(line + 8);
	}
	else if(strcmp(line, "outb;") == 0)
		poutb(u16port, u8val);
	else if(strcmp(line, "outw;") == 0)
		poutw(u16port, u16val);
	else if(strcmp(line, "vmoff;") == 0)
		vmoff();
	else if(strcmp(line, "halt;") == 0)
		halt();
	else if(strcmp(line, "addr;") == 0)
		addr();
	else if(strcmp(line, "memstat;") == 0 || strcmp(line, "MemStat;") == 0)
		memstat();
	else if(strcmp(line, "ls;") == 0)
		as_ls();
	else if(starts(line, "ls(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		as_ls_path(line + 4);
	}
	else if(starts(line, "mkdir(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		as_mkdir(line + 7);
	}
	else if(starts(line, "touch(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		as_touch(line + 7);
	}
	else if(starts(line, "cat(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		as_cat(line + 5);
	}
	else if(starts(line, "cd(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		if(as_cd(line + 4) != 0)
			pred("Directory not found\n");
	}
	else if(starts(line, "cd")) {
		if(as_cd(line + 2) != 0)
			pred("Directory not found\n");
	}
	else if(starts(line, "comment(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		comment(line + 9);
	}
	else if(starts(line, "cp(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		as_two_args(line + 4, arg1, arg2);
		as_cp(arg1, arg2);
	}
	else if(starts(line, "c"))
		as_cat(line + 1);
	else if(starts(line, "edit(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		wid = wm_create_window(1);

		if(wid >= 0)
			as_edit_open_win(wid, line + 6);
		else
			as_edit(line + 6);


		if(wm_on && wm_active_ok())
		{
			con_save(&wm_windows[wm_active]);
			wm_redraw_region(wm_windows[wm_active].bx, wm_windows[wm_active].by, wm_windows[wm_active].bx + wm_windows[wm_active].bw - 1, wm_windows[wm_active].by + wm_windows[wm_active].bh - 1);
			cursor_update();
		}
	}
	else if(starts(line, "e"))
	{
		wid = wm_create_window(1);

		if(wid >= 0)
			as_edit_open_win(wid, line + 1);
		else
			as_edit(line + 1);

		if(wm_on && wm_active_ok())
		{
			con_save(&wm_windows[wm_active]);
			wm_redraw_region(wm_windows[wm_active].bx, wm_windows[wm_active].by, wm_windows[wm_active].bx + wm_windows[wm_active].bw - 1, wm_windows[wm_active].by + wm_windows[wm_active].bh - 1);
			cursor_update();
		}
	}
	else if(starts(line, "run(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		wid = wm_shell_target_window();

		if(wid >= 0)
			wm_task_start_script(wid, line + 5);
		else
			run_script(line + 5);
	}
	else if(starts(line, "rm(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		as_rm(line + 4);
	}
	else if(starts(line, "r"))
	{
		wid = wm_shell_target_window();

		if(wid >= 0)
			wm_task_start_script(wid, line + 1);
		else
			run_script(line + 1);
	}
	else if(starts(line, "tune(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		wid = wm_shell_target_window();

		if(wid >= 0)
			wm_tune_start(wid, line + 6);
		else
			tune(line + 6);
	}
	else if(starts(line, "beep(") && ends(line, ");"))
	{
		rmsemia(line);
		beep(atoi(line + 5));
	}
	else if(starts(line, "sleep(") && ends(line, ");"))
	{
		rmsemia(line);
		wid = wm_shell_target_window();

		if(wid >= 0)
			wm_windows[wid].task_wake = total_ticks + atoi(line + 6);
		else
			sleep(atoi(line + 6));
	}
	else if(strcmp(line, "nosound;") == 0)
		nosound();
	else if(strcmp(line, "save;") == 0)
		as_save_to_disk();
	else if(strcmp(line, "load;") == 0)
		as_load_from_disk();
	else if(strcmp(line, "atadbg;") == 0)
		ata_debug();
	else if(strcmp(line, "saveity;") == 0)
		saveit = 1;
	else if(strcmp(line, "saveitn;") == 0)
		saveit = 0;
	else if(starts(line, "mv(\"") && ends(line, "\");"))
	{
		rmsemi(line);
		as_two_args(line + 4, arg1, arg2);
		as_mv(arg1, arg2);
	}
	else if(starts(line, "print(\"") && ends(line, "\");"))
	{
		u8 oldcolor = color;
		char *text;

		color = defcolor;
		rmsemi(line);
		text = line + 7;

		while(*text && *text != '"')
		{
			if(text[0] == '\\' && text[1] == 'n')
			{
				putc('\n');
				text += 2;
			}
			else if(text[0] == '\\' &&
				text[1] == 'c' &&
				text[2] == '[' &&
				text[3] == 'd' &&
				text[4] == 'c')
			{
				color = defcolor;
				text += 5;
			}
			else if(text[0] == '\\' &&
				text[1] == 'c' &&
				text[2] == '[' &&
				text[3] == '0' &&
				text[4] == 'x')
			{
				color = (hexval(text[5]) << 4) |
					hexval(text[6]);

				text += 7;
			}
			else
			{
				putc(*text);
				text++;
			}
		}

		color = oldcolor;
	}
	else if(line[0])
	{
		wid = wm_shell_target_window();

		if(wid >= 0 && wm_task_try_file(wid, line))
			return;

		perror(line);
	}
}
void run_script(const char *path)
{
	char *data;
	int size;
	int i;
	int j;
	char line[128];

	if(as_get_file_data(path, &data, &size) != 0)
	{
		pred("File not found\n");
		return;
	}

	i = 0;
	while(i < size)
	{
		j = 0;

		while(i < size && data[i] != '\n' && j < 127)
			line[j++] = data[i++];

		line[j] = 0;

		if(data[i] == '\n')
			i++;

		trim_end(line);

		if(line[0] == 0)
			continue;

		if(line[0] == '/' && line[1] == '/')
			continue;

		shell_exec(line);
	}
}

void wm_setup(void)
{
	int id;

	wm_init_buffers();
	for(id = 0; id < WM_WINDOWS; id++)
	{
		wm_windows[id].alive = 0;
		wm_windows[id].fullscreen = 0;
	}

	wm_z_count = 0;
	wm_next_side = 0;
	wm_next_card_x = 0;
	wm_next_card_y = 1;
	wm_init_new_window(0, 0, 1, 40, 59);
	wm_z_add_top(0);
	wm_init_new_window(1, 40, 1, 40, 59);
	wm_z_add_top(1);
	wm_next_side = 0;
}

void wm_set_title(int id, const char *s)
{
	int i;
	int max;

	if(id < 0 || id >= WM_WINDOWS)
		return;

	for(i = 0; i < WM_TITLE_MAX; i++)
		wm_windows[id].title[i] = 0;

	max = wm_windows[id].bw - 8;
	if(max > WM_TITLE_MAX - 1)
		max = WM_TITLE_MAX - 1;
	if(max < 0)
		max = 0;

	i = 0;
	if(!s)
		return;
	while(s[i] && i < max)
	{
		wm_windows[id].title[i] = s[i];
		i++;
	}
	wm_windows[id].title[i] = 0;
}

void wm_draw_one(int id)
{
	WMWindow *win;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;

	win = &wm_windows[id];
	wm_redraw_region(win->bx, win->by, win->bx + win->bw - 1, win->by + win->bh - 1);
}


void wm_draw(void)
{
	draw_tb();
	update_rtc_only();
	wm_redraw_region(0, 1, W - 1, H - 1);
	wm_draw_top_controls();
}


void wm_prompt(void)
{
	if(raw == 0)
	{
		color = 0x1F;
		as_pwd();
		print(">");
	}

	color = 0x1A;
	if(wm_on && wm_active_ok() && console_mode == CON_WINDOW)
	{
		wm_windows[wm_active].input_x = cx;
		wm_windows[wm_active].input_y = cy;
		wm_windows[wm_active].input_len = 0;
		wm_windows[wm_active].input[0] = 0;
	}
}



int wm_tune_freq(char note, int accidental, int octave)
{
	int idx;
	int f;
	static int tbl[12] = {262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494};

	idx = 0;
	if(note == 'C') idx = 0;
	else if(note == 'D') idx = 2;
	else if(note == 'E') idx = 4;
	else if(note == 'F') idx = 5;
	else if(note == 'G') idx = 7;
	else if(note == 'A') idx = 9;
	else if(note == 'B') idx = 11;

	idx += accidental;
	while(idx < 0)
	{
		idx += 12;
		octave--;
	}
	while(idx >= 12)
	{
		idx -= 12;
		octave++;
	}

	f = tbl[idx];
	while(octave > 4)
	{
		f <<= 1;
		octave--;
	}
	while(octave < 4)
	{
		f >>= 1;
		octave++;
	}
	if(f < 1)
		f = 1;
	return f;
}

int wm_tune_duration_ms(int tempo, char dur)
{
	int q;
	int ms;

	if(tempo <= 0)
		tempo = 120;
	q = 60000 / tempo;
	if(dur == 'w' || dur == 'W')
		ms = q * 4;
	else if(dur == 'h' || dur == 'H')
		ms = q * 2;
	else if(dur == 'e' || dur == 'E')
		ms = q / 2;
	else if(dur == 's' || dur == 'S')
		ms = q / 4;
	else
		ms = q;
	if(ms < 10)
		ms = 10;
	return ms;
}

void wm_tune_start(int id, const char *song)
{
	int i;
	WMWindow *win;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive || !song)
		return;

	win = &wm_windows[id];
	i = 0;
	while(song[i] && i < WM_INPUT_MAX - 1)
	{
		win->tune_song[i] = song[i];
		i++;
	}
	win->tune_song[i] = 0;
	win->tune_pos = 0;
	if(win->tune_tempo <= 0)
		win->tune_tempo = 120;
	win->tune_running = 1;
	win->tune_until = 0;
	win->tune_sounding = 0;
}

int wm_tune_step(int id)
{
	WMWindow *win;
	char *s;
	char note;
	char dur;
	int accidental;
	int octave;
	int freq;
	int ms;
	int tempo;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return 0;

	win = &wm_windows[id];
	if(!win->tune_running)
		return 0;

	if(win->tune_until)
	{
		if(total_ticks < win->tune_until)
			return 1;
		if(win->tune_sounding)
			nosound();
		win->tune_sounding = 0;
		win->tune_until = 0;
	}

	s = win->tune_song;
	for(;;)
	{
		while(s[win->tune_pos] == ' ' || s[win->tune_pos] == '\t' || s[win->tune_pos] == ',')
			win->tune_pos++;

		if(!s[win->tune_pos])
		{
			win->tune_running = 0;
			nosound();
			return 0;
		}

		if(s[win->tune_pos] == 'T' || s[win->tune_pos] == 't')
		{
			win->tune_pos++;
			tempo = 0;
			while(s[win->tune_pos] >= '0' && s[win->tune_pos] <= '9')
			{
				tempo = tempo * 10 + (s[win->tune_pos] - '0');
				win->tune_pos++;
			}
			if(tempo > 0)
				win->tune_tempo = tempo;
			continue;
		}

		note = s[win->tune_pos++];
		if(note >= 'a' && note <= 'z')
			note -= 32;

		accidental = 0;
		if(s[win->tune_pos] == '#' || s[win->tune_pos] == '+')
		{
			accidental = 1;
			win->tune_pos++;
		}
		else if(s[win->tune_pos] == 'b' || s[win->tune_pos] == '-')
		{
			accidental = -1;
			win->tune_pos++;
		}

		octave = 4;
		if(s[win->tune_pos] >= '0' && s[win->tune_pos] <= '9')
			octave = s[win->tune_pos++] - '0';

		dur = 'q';
		if(s[win->tune_pos] == 'w' || s[win->tune_pos] == 'W' ||
		   s[win->tune_pos] == 'h' || s[win->tune_pos] == 'H' ||
		   s[win->tune_pos] == 'q' || s[win->tune_pos] == 'Q' ||
		   s[win->tune_pos] == 'e' || s[win->tune_pos] == 'E' ||
		   s[win->tune_pos] == 's' || s[win->tune_pos] == 'S')
			dur = s[win->tune_pos++];

		ms = wm_tune_duration_ms(win->tune_tempo, dur);
		if(note == 'R')
		{
			nosound();
			win->tune_sounding = 0;
		}
		else if(note == 'A' || note == 'B' || note == 'C' || note == 'D' || note == 'E' || note == 'F' || note == 'G')
		{
			freq = wm_tune_freq(note, accidental, octave);
			beep((u32)freq);
			win->tune_sounding = 1;
		}
		else
		{
			continue;
		}

		win->tune_until = total_ticks + (u64)ms;
		return 1;
	}
}

int wm_is_wm_command(const char *line)
{
	if(!line)
		return 0;
	if(starts(line, "run(\"") && ends(line, "\");"))
		return 1;
	if(starts(line, "sleep(") && ends(line, ");"))
		return 1;
	if(starts(line, "tune(\"") && ends(line, "\");"))
		return 1;
	return 0;
}


u8 memstat_overlay_attr(u8 src)
{//make text draw over the white desktop background.
//Default white text becomes black so it stays readable.
	u8 fg;

	fg = src & 0x0F;
	if(fg == 0x0F)
		fg = 0x00;

	return 0xF0 | fg;
}

int memstat_overlay_cell(int x, int y)
{
	if(!wm_on || !memstat_overlay_ready || memstat_overlay_rows <= 0)
		return 0;
	if(x < MEMSTAT_OVERLAY_X || x >= MEMSTAT_OVERLAY_X + MEMSTAT_OVERLAY_W)
		return 0;
	if(y < memstat_overlay_y || y >= memstat_overlay_y + memstat_overlay_rows)
		return 0;

	return 1;
}

void memstat_overlay_clear_buf(void)
{
	int x;
	int y;

	for(y = 0; y < MEMSTAT_OVERLAY_H; y++)
	{
		for(x = 0; x < MEMSTAT_OVERLAY_W; x++)
		{
			memstat_overlay_chars[y][x] = ' ';
			memstat_overlay_attrs[y][x] = 0xF0;
		}
	}
}

void memstat_overlay_putc(int row, int *x, char c, u8 col)
{
	if(row < 0 || row >= MEMSTAT_OVERLAY_H)
		return;
	if(*x < 0 || *x >= MEMSTAT_OVERLAY_W)
		return;

	memstat_overlay_chars[row][*x] = c;
	memstat_overlay_attrs[row][*x] = memstat_overlay_attr(col);
	(*x)++;
}

void memstat_overlay_puts(int row, int *x, const char *s, u8 col)
{
	int i;

	if(!s)
		return;

	i = 0;
	while(s[i] && *x < MEMSTAT_OVERLAY_W)
	{
		memstat_overlay_putc(row, x, s[i], col);
		i++;
	}
}

void memstat_overlay_put_uint(int row, int *x, unsigned int n, u8 col)
{
	char buf[11];
	int i;

	if(n == 0)
	{
		memstat_overlay_putc(row, x, '0', col);
		return;
	}

	i = 0;
	while(n && i < 10)
	{
		buf[i++] = '0' + (n % 10U);
		n /= 10U;
	}

	while(i--)
		memstat_overlay_putc(row, x, buf[i], col);
}

u32 memstat_window_bytes(int id)
{
	u32 bytes;
	WMWindow *win;

	if(id < 0 || id >= WM_WINDOWS)
		return 0;

	win = &wm_windows[id];
	if(!win->alive)
		return 0;

	bytes = (u32)sizeof(WMWindow);
	bytes += (u32)(WM_CONTENT_W * WM_CONTENT_H * 2);

	if(win->task_running)
		bytes += (u32)win->task_size;

	bytes += (u32)as_edit_win_mem(id);
	return bytes;
}

void memstat_window_state(int id, const char **name, u8 *col)
{
	WMWindow *win;

	*name = "IDLE";
	*col = 0x1F;

	if(id < 0 || id >= WM_WINDOWS)
		return;

	win = &wm_windows[id];
	if(!win->alive)
		return;

	if(as_edit_win_open(id))
	{
		*name = "EDITOR";
		*col = 0x1E;
	}
	else if(win->tune_running)
	{
		*name = "TUNE";
		*col = 0x1D;
	}
	else if(win->task_wake)
	{
		*name = "SLEEP";
		*col = 0x1D;
	}
	else if(win->task_running)
	{
		*name = "SCRIPT";
		*col = 0x1A;
	}
	else if(win->task_pending)
	{
		*name = "QUEUED";
		*col = 0x1A;
	}
}

void memstat_overlay_put_window_detail(int row, int *x, int id)
{
	WMWindow *win;

	if(id < 0 || id >= WM_WINDOWS)
		return;

	win = &wm_windows[id];

	if(as_edit_win_open(id))
	{
		memstat_overlay_puts(row, x, "[EDITOR] ", 0x1E);
		memstat_overlay_puts(row, x, as_edit_win_path(id), 0x1F);
	}
	else if(win->tune_running)
	{
		memstat_overlay_puts(row, x, "[TUNE  ] ", 0x1D);
		memstat_overlay_puts(row, x, win->tune_song, 0x1F);
	}
	else if(win->task_wake)
	{
		memstat_overlay_puts(row, x, "[SLEEP ] ", 0x1D);
		memstat_overlay_puts(row, x, win->title, 0x1F);
	}
	else if(win->task_running)
	{
		memstat_overlay_puts(row, x, "[SCRIPT] ", 0x1A);
		memstat_overlay_puts(row, x, win->title, 0x1F);
	}
	else if(win->task_pending)
	{
		memstat_overlay_puts(row, x, "[QUEUED] ", 0x1A);
		memstat_overlay_puts(row, x, win->task_cmd, 0x1F);
	}
	else
		memstat_overlay_puts(row, x, "[IDLE  ] shell prompt", 0x1F);
}

void memstat_overlay_build(void)
{
	int id;
	int x;
	int row;
	int count;
	int used;
	u32 bytes;
	u32 total;
	WMWindow *win;

	memstat_overlay_clear_buf();

	row = 0;
	x = 0;
	memstat_overlay_puts(row, &x, "MemStat -- window tasks", 0x1F);

	row = 2;
	total = 0;
	count = 0;

	for(id = 0; id < WM_WINDOWS && row + 1 < MEMSTAT_OVERLAY_H; id++)
	{
		win = &wm_windows[id];
		if(!win->alive)
			continue;

		bytes = memstat_window_bytes(id);

		x = 0;
		memstat_overlay_puts(row, &x, "Window ", 0x1F);
		memstat_overlay_put_uint(row, &x, (unsigned int)id, 0x1F);
		memstat_overlay_puts(row, &x, ": ", 0x1F);
		memstat_overlay_put_window_detail(row, &x, id);
		row++;

		x = 0;
		memstat_overlay_puts(row, &x, "  memory: ", 0x1F);
		memstat_overlay_put_uint(row, &x, bytes / 1024U, 0x1F);
		memstat_overlay_puts(row, &x, " kB (", 0x1F);
		memstat_overlay_put_uint(row, &x, bytes, 0x1F);
		memstat_overlay_puts(row, &x, " bytes)", 0x1F);
		row++;

		total += bytes;
		count++;
	}

	if(row < MEMSTAT_OVERLAY_H)
		row++;

	if(row < MEMSTAT_OVERLAY_H)
	{
		x = 0;
		memstat_overlay_put_uint(row, &x, (unsigned int)count, 0x1F);
		memstat_overlay_puts(row, &x, " window(s), ", 0x1F);
		memstat_overlay_put_uint(row, &x, total / 1024U, 0x1F);
		memstat_overlay_puts(row, &x, " kB total (", 0x1F);
		memstat_overlay_put_uint(row, &x, total, 0x1F);
		memstat_overlay_puts(row, &x, " bytes)", 0x1F);
		row++;
	}

	used = row;
	if(used < 1)
		used = 1;
	if(used > MEMSTAT_OVERLAY_H)
		used = MEMSTAT_OVERLAY_H;

	memstat_overlay_rows = used;
	memstat_overlay_y = H - memstat_overlay_rows;
	if(memstat_overlay_y < 1)
		memstat_overlay_y = 1;

	memstat_overlay_ready = 1;
}

void memstat_overlay_compose(int sx, int sy, char *outc, u8 *outcol)
{
	int ox;
	int oy;

	if(!memstat_overlay_cell(sx, sy))
		return;

	if(!memstat_overlay_ready)
		memstat_overlay_build();

	ox = sx - MEMSTAT_OVERLAY_X;
	oy = sy - memstat_overlay_y;
	*outc = memstat_overlay_chars[oy][ox];
	*outcol = memstat_overlay_attrs[oy][ox];
}

void memstat_overlay_refresh(int force)
{
	static int last_sec = -1;
	int sec;
	int old_y;
	int old_rows;
	int y0;
	int y1;
	int old_bottom;
	int new_bottom;

	if(!wm_on)
		return;

	sec = rtc_get_second();
	if(!force && sec == last_sec)
		return;

	old_y = memstat_overlay_y;
	old_rows = memstat_overlay_rows;

	last_sec = sec;
	memstat_overlay_build();

	if(old_rows <= 0)
	{
		y0 = memstat_overlay_y;
		y1 = memstat_overlay_y + memstat_overlay_rows - 1;
	}
	else
	{
		old_bottom = old_y + old_rows - 1;
		new_bottom = memstat_overlay_y + memstat_overlay_rows - 1;
		y0 = old_y < memstat_overlay_y ? old_y : memstat_overlay_y;
		y1 = old_bottom > new_bottom ? old_bottom : new_bottom;
	}

	if(y0 < 1)
		y0 = 1;
	if(y1 >= H)
		y1 = H - 1;
	if(y0 <= y1)
		wm_redraw_region(MEMSTAT_OVERLAY_X, y0, MEMSTAT_OVERLAY_X + MEMSTAT_OVERLAY_W - 1, y1);
}

void memstat(void)
{//MemStat: report every running window task and how much
//memory it is holding, in kB and bytes
	int id;
	int count;
	u32 bytes;
	u32 total;
	WMWindow *win;
	u8 oldcolor;

	oldcolor = color;
	color = 0x1F;
	print("MemStat -- window tasks\n\n");

	total = 0;
	count = 0;

	for(id = 0; id < WM_WINDOWS; id++)
	{
		win = &wm_windows[id];

		if(!win->alive)
			continue;

		bytes = memstat_window_bytes(id);

		color = 0x1F;
		print("Window ");
		printint((unsigned int)id);
		print(": ");

		if(as_edit_win_open(id))
		{
			color = 0x1E;
			print("[EDITOR] ");
			color = 0x1F;
			print(as_edit_win_path(id));
		}
		else if(win->tune_running)
		{
			color = 0x1D;
			print("[TUNE  ] ");
			color = 0x1F;
			print(win->tune_song);
		}
		else if(win->task_wake)
		{
			color = 0x1D;
			print("[SLEEP ] ");
			color = 0x1F;
			print(win->title);
		}
		else if(win->task_running)
		{
			color = 0x1A;
			print("[SCRIPT] ");
			color = 0x1F;
			print(win->title);
		}
		else if(win->task_pending)
		{
			color = 0x1A;
			print("[QUEUED] ");
			color = 0x1F;
			print(win->task_cmd);
		}
		else
			print("[IDLE  ] shell prompt");

		print("\n  memory: ");
		printint(bytes / 1024U);
		print(" kB (");
		printint(bytes);
		print(" bytes)\n");

		total += bytes;
		count++;
	}

	print("\n");
	printint((unsigned int)count);
	print(" window(s), ");
	printint(total / 1024U);
	print(" kB total (");
	printint(total);
	print(" bytes)\n");
	color = oldcolor;
}

void wm_enqueue_cmd(int id, const char *cmd)
{
	int i;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive || !cmd)
		return;

	i = 0;
	while(cmd[i] && i < WM_INPUT_MAX - 1)
	{
		wm_windows[id].task_cmd[i] = cmd[i];
		i++;
	}
	wm_windows[id].task_cmd[i] = 0;
	wm_windows[id].task_pending = 1;
	wm_set_title(id, wm_windows[id].task_cmd);
	wm_redraw_region(wm_windows[id].bx, wm_windows[id].by, wm_windows[id].bx + wm_windows[id].bw - 1, wm_windows[id].by);
}

void wm_task_begin_script_data(int id, char *data, int size)
{
	wm_windows[id].task_data = data;
	wm_windows[id].task_size = size;
	wm_windows[id].task_pos = 0;
	wm_windows[id].task_running = 1;
	wm_windows[id].task_pending = 0;
	wm_windows[id].task_wake = 0;
	wm_windows[id].tune_running = 0;
	wm_windows[id].tune_until = 0;
	wm_windows[id].tune_sounding = 0;
}

void wm_task_start_script(int id, const char *path)
{
	char *data;
	int size;
	char clean[WM_INPUT_MAX];
	int old_active;
	int old_exec;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;

	old_active = wm_active;
	old_exec = wm_task_exec_win;
	wm_task_exec_win = id;
	wm_active = id;
	con_load(&wm_windows[id]);
	wm_task_clean_path(path, clean);

	if(as_get_file_data(clean, &data, &size) != 0)
	{
		pred("File not found\n");
		con_save(&wm_windows[id]);
		wm_task_exec_win = old_exec;
		wm_active = old_active;
		if(wm_active_ok())
			con_load(&wm_windows[wm_active]);
		wm_windows[id].task_running = 0;
		wm_windows[id].task_sp = 0;
		return;
	}

	con_save(&wm_windows[id]);
	wm_task_exec_win = old_exec;
	wm_active = old_active;
	if(wm_active_ok())
		con_load(&wm_windows[wm_active]);

	wm_windows[id].task_sp = 0;
	wm_task_begin_script_data(id, data, size);
}

void wm_task_chain_script(int id, const char *path)
{
	WMWindow *win;
	char *data;
	int size;
	char clean[WM_INPUT_MAX];

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;

	win = &wm_windows[id];
	wm_task_clean_path(path, clean);

	if(as_get_file_data(clean, &data, &size) != 0)
	{
		pred("File not found\n");
		return;
	}

	if(win->task_running)
	{
		if(win->task_sp >= WM_TASK_STACK_MAX)
		{
			pred("Script stack full\n");
			return;
		}

		win->task_stack_data[win->task_sp] = win->task_data;
		win->task_stack_size[win->task_sp] = win->task_size;
		win->task_stack_pos[win->task_sp] = win->task_pos;
		win->task_sp++;
	}

	wm_task_begin_script_data(id, data, size);
}

void wm_extract_quoted_arg(const char *line, char *out)
{
	int i;
	int j;

	out[0] = 0;
	if(!line)
		return;

	i = 0;
	while(line[i] && line[i] != '"')
		i++;
	if(!line[i])
		return;
	i++;

	j = 0;
	while(line[i] && line[i] != '"' && j < WM_INPUT_MAX - 1)
		out[j++] = line[i++];
	out[j] = 0;
}

void wm_task_clean_path(const char *src, char *dst)
{
	int i;
	int j;

	dst[0] = 0;
	if(!src)
		return;

	i = 0;
	while(src[i] == ' ' || src[i] == '\t')
		i++;

	j = 0;
	if(src[i] == 'R' && src[i + 1] == 'o' && src[i + 2] == 'o' && src[i + 3] == 't' && src[i + 4] == '/')
	{
		dst[j++] = '/';
		i += 5;
	}

	while(src[i] && src[i] != ';' && j < WM_INPUT_MAX - 1)
		dst[j++] = src[i++];
	dst[j] = 0;
	trim_end(dst);
}

int wm_task_try_file(int id, const char *line)
{
	char path[WM_INPUT_MAX];
	char *data;
	int size;
	int old_active;
	int old_exec;

	if(!line || !line[0])
		return 0;

	if(strcmp(line, "__help;") == 0 || starts(line, "print(") || starts(line, "tune(") || starts(line, "sleep(") ||
	   starts(line, "run(") || starts(line, "cd(") || starts(line, "ls") ||
	   starts(line, "cat(") || starts(line, "edit(") || starts(line, "mkdir(") ||
	   starts(line, "touch(") || starts(line, "rm(") || starts(line, "cp(") ||
	   starts(line, "mv(") || starts(line, "comment(") || starts(line, "color(") ||
	   starts(line, "beep(") || strcmp(line, "save;") == 0 || strcmp(line, "load;") == 0 ||
	   strcmp(line, "ls;") == 0 || strcmp(line, "cls;") == 0)
		return 0;

	old_active = wm_active;
	old_exec = wm_task_exec_win;
	wm_task_exec_win = id;
	wm_active = id;
	con_load(&wm_windows[id]);

	wm_task_clean_path(line, path);
	if(as_get_file_data(path, &data, &size) == 0)
	{
		con_save(&wm_windows[id]);
		wm_task_exec_win = old_exec;
		wm_active = old_active;
		if(wm_active_ok())
			con_load(&wm_windows[wm_active]);
		if(wm_windows[id].task_running)
			wm_task_chain_script(id, path);
		else
			wm_task_start_script(id, path);
		return 1;
	}

	con_save(&wm_windows[id]);
	wm_task_exec_win = old_exec;
	wm_active = old_active;
	if(wm_active_ok())
		con_load(&wm_windows[wm_active]);
	return 0;
}

int wm_task_run_line(int id, char *line)
{
	int old_active;
	int old_exec;
	int wait;
	u8 oldcolor;
	char arg[WM_INPUT_MAX];

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return 0;

	if(wm_task_try_file(id, line))
		return 0;

	old_active = wm_active;
	old_exec = wm_task_exec_win;
	oldcolor = color;
	wait = 0;

	wm_task_exec_win = id;
	wm_in_task_step = 1;
	wm_active = id;
	con_load(&wm_windows[id]);
	color = 0x1F;
	cursor_hide();

	if(starts(line, "tune(\"") && ends(line, "\");"))
	{
		wm_extract_quoted_arg(line, arg);
		wm_tune_start(id, arg);
		wm_tune_step(id);
		wait = wm_windows[id].tune_running || wm_windows[id].tune_until;
	}
	else if(starts(line, "sleep(") && ends(line, ");"))
	{
		wm_windows[id].task_wake = total_ticks + atoi(line + 6);
		wait = 1;
	}
	else if(starts(line, "run(\"") && ends(line, "\");"))
	{
		wm_extract_quoted_arg(line, arg);
		wm_task_chain_script(id, arg);
	}
	else
	{
		shell_exec(line);
		if(wm_windows[id].task_wake || wm_windows[id].tune_running)
			wait = 1;
	}

	if(wm_windows[id].alive)
		con_save(&wm_windows[id]);
	wm_in_task_step = 0;
	wm_task_exec_win = old_exec;
	if(wm_task_focus_win >= 0 && wm_task_focus_win < WM_WINDOWS && wm_windows[wm_task_focus_win].alive)
		old_active = wm_task_focus_win;
	wm_task_focus_win = -1;
	wm_active = old_active;
	color = oldcolor;
	if(wm_active_ok())
		con_load(&wm_windows[wm_active]);
	cursor_update();
	return wait;
}

void wm_task_prompt_if_idle(int id)
{
	int old_active;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;
	if(as_edit_win_open(id))
		return;
	if(wm_windows[id].task_running || wm_windows[id].task_pending || wm_windows[id].task_wake || wm_windows[id].tune_running)
		return;

	old_active = wm_active;
	wm_active = id;
	con_load(&wm_windows[id]);
	wm_prompt();
	con_save(&wm_windows[id]);
	wm_active = old_active;
	if(wm_active_ok())
		con_load(&wm_windows[wm_active]);
}

void wm_task_step(int id)
{
	WMWindow *win;
	char line[WM_INPUT_MAX];
	int j;
	int had_wait;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;

	win = &wm_windows[id];
	had_wait = win->tune_running || win->task_wake;
	if(wm_tune_step(id))
		return;
	if(win->task_wake)
	{
		if(total_ticks < win->task_wake)
			return;
		win->task_wake = 0;
	}
	if(had_wait && !win->task_running && !win->task_pending && !win->task_wake && !win->tune_running)
	{
		wm_task_prompt_if_idle(id);
		return;
	}

	if(win->task_pending)
	{
		strcpy(line, win->task_cmd);
		win->task_pending = 0;
		if(wm_task_run_line(id, line))
			return;
		if(wm_windows[id].alive && !wm_windows[id].task_running && !wm_windows[id].task_pending && !wm_windows[id].task_wake && !wm_windows[id].tune_running)
		{
			wm_task_prompt_if_idle(id);
			return;
		}
	}

	if(!win->task_running)
		return;

	for(j = 0; j < 8 && win->task_running; j++)
	{
		int k;
		k = 0;
		while(win->task_pos < win->task_size && win->task_data[win->task_pos] != '\n' && k < WM_INPUT_MAX - 1)
			line[k++] = win->task_data[win->task_pos++];
		line[k] = 0;
		if(win->task_pos < win->task_size && win->task_data[win->task_pos] == '\n')
			win->task_pos++;
		trim_end(line);
		if(line[0] == 0)
			continue;
		if(line[0] == '/' && line[1] == '/')
			continue;
		if(wm_task_run_line(id, line))
			return;
		if(wm_windows[id].task_wake || wm_windows[id].tune_running || wm_windows[id].task_pending)
			return;
		if(win->task_pos >= win->task_size)
			break;
	}

	if(win->task_pos >= win->task_size && win->task_running)
	{
		if(win->task_sp > 0)
		{
			win->task_sp--;
			win->task_data = win->task_stack_data[win->task_sp];
			win->task_size = win->task_stack_size[win->task_sp];
			win->task_pos = win->task_stack_pos[win->task_sp];
			return;
		}

		win->task_running = 0;
		wm_task_prompt_if_idle(id);
	}
}

void ticks_poll(void)
{//advance total_ticks by polling PIT channel 0
//(same wraparound trick as sleep(), 1 wrap = ~1ms)
	static u16 tick_last = 0;
	static int tick_init = 0;
	u16 now;

	now = pit_read_counter();

	if(!tick_init)
	{
		tick_last = now;
		tick_init = 1;
		return;
	}

	if(now > tick_last)
		total_ticks++;

	tick_last = now;
}

void wm_tasks_step(void)
{
	static int rr = 0;
	int n;
	int id;
	int stepped;

	ticks_poll();

	stepped = 0;
	for(n = 0; n < WM_WINDOWS; n++)
	{
		id = (rr + n) % WM_WINDOWS;
		if(!wm_windows[id].alive)
			continue;
		if(!wm_windows[id].task_pending &&
		   !wm_windows[id].task_running &&
		   !wm_windows[id].task_wake &&
		   !wm_windows[id].tune_running)
			continue;

		wm_task_step(id);
		stepped = 1;
	}

	if(stepped)
		rr = (rr + 1) % WM_WINDOWS;
}

void wm_submit_line(int id, const char *line)
{
	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;
	if(line && line[0])
		wm_enqueue_cmd(id, line);
}

void wm_handle_key(int c)
{
	WMWindow *win;
	int maxx;
	int maxy;
	char line[WM_INPUT_MAX];

	if(c == '\t')
	{
		wm_switch();
		return;
	}

	if(shift && c == 3)
	{
		wm_create_window(WM_ADD_CARD);
		return;
	}
	if(shift && c == 14)
	{
		wm_create_window(WM_ADD_SIDE);
		return;
	}

	if(shift && c == 17)
	{
		if(wm_active_ok())
		{
			wm_close_window(wm_active);
			cursor_update();
		}
		return;
	}

	if(!wm_active_ok())
		return;

	win = &wm_windows[wm_active];
	con_load(win);

	if(as_edit_win_open(wm_active))
	{
		color = 0x1F;

		if(c == 6 || c == 3)
		{
			con_save(win);
			if(c == 6)
				wm_toggle_fullscreen(wm_active);
			else
				wm_toggle_card(wm_active);
			if(wm_active_ok() && as_edit_win_open(wm_active))
			{
				con_load(&wm_windows[wm_active]);
				as_edit_win_refresh(wm_active);
				con_save(&wm_windows[wm_active]);
				cursor_update();
			}
			return;
		}

		if(as_edit_key_win(wm_active, c))
		{//editor closed, give the window its shell back
			clear();
			wm_prompt();
		}

		con_save(win);
		return;
	}

	color = 0x1A;

	if(c == 6)
	{
		con_save(win);
		wm_toggle_fullscreen(wm_active);
		return;
	}
	if(c == 3)
	{
		con_save(win);
		wm_toggle_card(wm_active);
		return;
	}
	if(c == 12)
	{
		if(wm_cursor_on_prompt_line(wm_active))
			wm_sync_input_from_screen(wm_active);

		strcpy(line, win->input);
		wm_clear_highlight();
		wm_shift_selecting = 0;
		clear();
		wm_prompt();
		print(line);
		strcpy(win->input, line);
		win->input_len = strlen(line);
		cursor_update();
		con_save(win);
		return;
	}

	if(c == '\n')
	{
		if(wm_selected_win == wm_active && wm_selected_cmd[0])
		{
			strcpy(line, wm_selected_cmd);
			wm_clear_highlight();
			wm_prepare_selected_run_line(line);
			con_save(win);
			wm_submit_line(wm_active, line);
			return;
		}
		if(wm_cursor_on_prompt_line(wm_active))
			wm_sync_input_from_screen(wm_active);
		else
		{
			wm_select_line(wm_active, cy);
			strcpy(line, wm_selected_cmd);
			wm_clear_highlight();
			wm_prepare_selected_run_line(line);
			con_save(win);
			wm_submit_line(wm_active, line);
			return;
		}
		win->input[win->input_len] = 0;
		strcpy(line, win->input);
		win->input_len = 0;
		win->input[0] = 0;
		putc('\n');
		if(line[0] == 0)
		{
			wm_prompt();
			con_save(win);
			return;
		}
		con_save(win);
		wm_submit_line(wm_active, line);
		return;
	}

	if(c == '\b')
	{
		wm_backspace_here(wm_active);
		con_save(win);
		return;
	}

	maxx = win->w;
	maxy = win->h;
	if(c == KEY_UP || c == KEY_DOWN || c == KEY_LEFT || c == KEY_RIGHT)
	{
		if(shift && !wm_shift_selecting)
		{
			wm_shift_ax = cx;
			wm_shift_ay = cy;
			wm_shift_selecting = 1;
		}
		if(c == KEY_UP)
		{
			if(cy > 0)
				cy--;
		}
		else if(c == KEY_DOWN)
		{
			if(cy + 1 < (unsigned int)maxy)
				cy++;
		}
		else if(c == KEY_LEFT)
		{
			if(cx > 0)
				cx--;
		}
		else if(c == KEY_RIGHT)
		{
			if(cx + 1 < (unsigned int)maxx)
				cx++;
		}
		if(shift)
			wm_select_range(wm_active, wm_shift_ax, wm_shift_ay, cx, cy);
		else
			wm_shift_selecting = 0;
		cursor_update();
		con_save(win);
		return;
	}

	if(c >= 32 && c < 127)
	{
		wm_shift_selecting = 0;
		wm_insert_char_here(wm_active, (char)c);
		con_save(win);
	}
}

void wm_start_shell_window(int id)
{
	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;

	wm_active = id;
	con_load(&wm_windows[id]);
	color = 0x1F;
	wm_set_title(id, "Shell");
	clear();
	wm_prompt();
	con_save(&wm_windows[id]);
}


void wm_start_window(int id)
{
	const char *path;
	char cmd[WM_INPUT_MAX];
	int i;
	int j;

	if(id < 0 || id >= WM_WINDOWS || !wm_windows[id].alive)
		return;

	path = "/RunCmds.AC";
	if(id == 1)
		path = "/Everist/Index/Main.AC";

	wm_active = id;
	con_load(&wm_windows[id]);
	color = 0x1F;

	j = 0;
	cmd[j++] = 'r';
	cmd[j++] = 'u';
	cmd[j++] = 'n';
	cmd[j++] = '(';
	cmd[j++] = '\"';
	i = 0;
	while(path[i] && j < WM_INPUT_MAX - 4)
		cmd[j++] = path[i++];
	cmd[j++] = '\"';
	cmd[j++] = ')';
	cmd[j++] = ';';
	cmd[j] = 0;

	wm_set_title(id, cmd);
	clear();
	print(cmd);
	print("\n");
	wm_enqueue_cmd(id, cmd);
	con_save(&wm_windows[id]);
}


void wm_switch(void)
{
	int next;

	wm_clear_highlight();
	next = wm_next_alive_from(wm_active);
	if(next >= 0)
		wm_activate(next);
}

void wm(void)
{
	int c;
	int last_sec;
	int sec;

	wm_on = 1;
	wm_active = 0;
	con_set_full();
	color = 0x1F;
	gfx_set_start(0);
	nosound();
	wm_setup();
	wm_clear_background();
	memstat_overlay_refresh(1);
	draw_tb();
	update_rtc_only();
	wm_draw();
	mouse_init();

	wm_start_window(0);
	wm_start_window(1);
	memstat_overlay_refresh(1);

	wm_active = 0;
	con_load(&wm_windows[0]);
	wm_redraw_all();
	cursor_update();
	last_sec = -1;

	for(;;)
	{
		sec = rtc_get_second();
		if(sec != last_sec)
		{
			update_rtc_only();
			memstat_overlay_refresh(1);
			last_sec = sec;
		}

		ticks_poll();
		wm_mouse_poll();
		wm_tasks_step();
		c = getkey();
		if(c)
			wm_handle_key(c);
		wm_tasks_step();
	}
}


int shell(void)
{//shell loop
	wm();
	return 0;
}

void kmain(void)
{//main
	clear();
	startupSeq();
}



