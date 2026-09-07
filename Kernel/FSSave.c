// FSSave.c
// no header files
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
extern void print(const char *s);
extern void outb(u16 port, u8 value);
extern u8 inb(u16 port);
extern void outw(u16 port, u16 value);
extern u16 inw(u16 port);
extern void print_red(const char *string);
extern int bios_disk_op(u8 drive, u32 lba, u32 write);
typedef struct {
	char name[32];
	char data[4096];
	u32 size;
	u8 type;
	u8 used;
	int parent;
} ASNode;
extern ASNode as_nodes[64];
extern int as_cwd;
#define AS_SAVE_LBA         4096
#define AS_SAVE_MAX_SECTORS 768
#define AS_MAGIC            0x41534653
static u8 secbuf[512];
/* DiskThunk.ASM writes BIOS sectors to this real-mode buffer.
   Do NOT put it at 0x7000: Kernel.c uses 0x7000 for FONT8X8,
   and disk load/save would corrupt glyphs like / and ; after install. */
#define AS_DISK_BOUNCE_ADDR 0x6000
static u8 *bounce = (u8 *)AS_DISK_BOUNCE_ADDR;

static void zero(u8 *pointer, u32 count)
{
	u32 index;
	for (index = 0; index < count; index++)
		pointer[index] = 0;
}

static void copy(u8 *destination, u8 *source_value, u32 count)
{
	u32 index;
	for (index = 0; index < count; index++)
		destination[index] = source_value[index];
}

static void put32(u8 *pointer, u32 value)
{
	pointer[0] = value;
	pointer[1] = value >> 8;
	pointer[2] = value >> 16;
	pointer[3] = value >> 24;
}

static u32 get32(u8 *pointer)
{
	return ((u32)pointer[0]) |
	       ((u32)pointer[1] << 8) |
	       ((u32)pointer[2] << 16) |
	       ((u32)pointer[3] << 24);
}

static u8 get_boot_drive(void)
{
	return *(u8 *)0x0500;
}

static int bad_drive(u8 value)
{
	if (value == 0x00) return 0;
	if (value >= 0x80 && value < 0xE0) return 0;
	return 1;
}

static int disk_read(u32 lba, u8 *buf)
{
	u8 drive = get_boot_drive();
	if (bad_drive(drive)) {
		print_red("Bad boot drive\n");
		return 0;
	}
	if (drive >= 0xE0) {
		print_red("CD-ROM is read only\n");
		return 0;
	}
	if (!bios_disk_op(drive, lba, 0)) {
		print_red("BIOS read failed\n");
		return 0;
	}
	copy(buf, bounce, 512);
	return 1;
}

static int disk_write(u32 lba, u8 *buf)
{
	u8 drive = get_boot_drive();
	if (bad_drive(drive)) {
		print_red("Bad boot drive\n");
		return 0;
	}
	if (drive >= 0xE0) {
		print_red("CD-ROM is read only\n");
		return 0;
	}
	copy(bounce, buf, 512);
	if (!bios_disk_op(drive, lba, 1)) {
		print_red("BIOS write failed\n");
		return 0;
	}
	return 1;
}
//save stream
static u32 sw_lba;
static u32 sw_pos;
static u32 sw_secs;
static u32 sw_sum;

static int sw_flush(void)
{
	if (sw_secs >= AS_SAVE_MAX_SECTORS) {
		print_red("Failed to save, AnchorSand is full!\n");
		return 0;
	}
	if (!disk_write(sw_lba, secbuf)) {
		print_red("Failed to flush write\n");
		return 0;
	}
	sw_lba++;
	sw_secs++;
	sw_pos = 0;
	zero(secbuf, 512);
	return 1;
}

static int sw_byte(u8 byte_value)
{
	secbuf[sw_pos++] = byte_value;
	sw_sum += byte_value;
	if (sw_pos == 512)
		return sw_flush();
	return 1;
}

static int sw_buf(u8 *pointer, u32 count)
{
	u32 index;
	for (index = 0; index < count; index++) {
		if (!sw_byte(pointer[index]))
			return 0;
	}
	return 1;
}

static int sw_u32(u32 value)
{
	u8 byte_value[4];
	byte_value[0] = value;
	byte_value[1] = value >> 8;
	byte_value[2] = value >> 16;
	byte_value[3] = value >> 24;
	return sw_buf(byte_value, 4);
}
/* ================= LOAD STREAM ================= */
static u32 sr_lba;
static u32 sr_pos;
static u32 sr_left;
static u32 sr_sum;

static int sr_fill(void)
{
	if (!disk_read(sr_lba, secbuf)) {
		print_red("Failed to read disk\n");
		return 0;
	}
	sr_lba++;
	sr_pos = 0;
	return 1;
}

static int sr_byte(u8 *byte_value)
{
	if (sr_left == 0) {
		print_red("Read past EOF\n");
		return 0;
	}
	if (sr_pos == 512) {
		if (!sr_fill())
			return 0;
	}
	*byte_value = secbuf[sr_pos++];
	sr_sum += *byte_value;
	sr_left--;
	return 1;
}

static int sr_buf(u8 *pointer, u32 count)
{
	u32 index;
	for (index = 0; index < count; index++) {
		if (!sr_byte(&pointer[index]))
			return 0;
	}
	return 1;
}

static int sr_u32(u32 *value)
{
	u8 byte_value[4];
	if (!sr_buf(byte_value, 4))
		return 0;
	*value = get32(byte_value);
	return 1;
}
/* ================= PUBLIC ================= */

int as_save_to_disk(void)
{
	u32 index;
	u32 sum = 0;
	u8 *position = (u8 *)as_nodes;
	u32 total = sizeof(ASNode) * 64;
	u32 pos = 0;
	u32 lba = AS_SAVE_LBA + 1;
	u32 sectors = 0;
	while (pos < total) {
		zero(secbuf, 512);
		for (index = 0; index < 512 && pos < total; index++) {
			secbuf[index] = position[pos];
			sum += position[pos];
			pos++;
		}
		if (!disk_write(lba, secbuf)) {
			print_red("Failed to write node\n");
			print_red("Failed to write to filesystem\n");
			return 0;
		}
		lba++;
		sectors++;
	}
	zero(secbuf, 512);
	put32(secbuf + 0, AS_MAGIC);
	put32(secbuf + 4, 2);
	put32(secbuf + 8, sectors);
	put32(secbuf + 12, sum);
	put32(secbuf + 16, (u32)as_cwd);
	put32(secbuf + 20, total);
	if (!disk_write(AS_SAVE_LBA, secbuf)) {
		print_red("Failed to write to header\n");
		print_red("Failed to write to filesystem\n");
		return 0;
	}
	print("Files saved\n");
	return 1;
}

int as_load_from_disk(void)
{
	u32 magic;
	u32 version;
	u32 sectors;
	u32 wanted_sum;
	u32 cwd;
	u32 total;
	u32 sum = 0;
	u32 pos = 0;
	u32 index;
	u32 lba = AS_SAVE_LBA + 1;
	u8 *position = (u8 *)as_nodes;
	if (!disk_read(AS_SAVE_LBA, secbuf)) {
		print_red("Failed to write to header\n");
                print_red("Failed to write to filesystem\n");
		return 0;
	}
	magic = get32(secbuf + 0);
	version = get32(secbuf + 4);
	sectors = get32(secbuf + 8);
	wanted_sum = get32(secbuf + 12);
	cwd = get32(secbuf + 16);
	total = get32(secbuf + 20);
	if (magic != AS_MAGIC) {
		print_red("No files!\n");
		return 0;
	}
	if (version != 2) {
		print_red("Bad AnchorSand save version\n");
		return 0;
	}
	if (total != sizeof(ASNode) * 64) {
		print_red("Bad AnchorSand save total\n");
		return 0;
	}
	if (sectors == 0 || sectors > AS_SAVE_MAX_SECTORS) {
		print_red("Bad AnchorSand save size\n");
		return 0;
	}
	while (pos < total) {
		if (!disk_read(lba, secbuf)) {
			print_red("Failed to read node\n");
			print_red("Failed to load filesystem\n");
			return 0;
		}
		for (index = 0; index < 512 && pos < total; index++) {
			position[pos] = secbuf[index];
			sum += secbuf[index];
			pos++;
		}
		lba++;
	}
	if (sum != wanted_sum) {
		print_red("AnchorSand checksum bad\n");
		return 0;
	}
	as_cwd = (int)cwd;
	print("AnchorSand loaded\n");
	return 1;
}
