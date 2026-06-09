// AnchorSave.c
// no header files

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

extern void print(const char *s);

extern void outb(u16 port, u8 value);
extern u8 inb(u16 port);
extern void outw(u16 port, u16 value);
extern u16 inw(u16 port);
extern void pred(const char *s);

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
static u8 *bounce = (u8 *)0x7000;

static void zero(u8 *p, u32 n) {
	u32 i;
	for (i = 0; i < n; i++)
		p[i] = 0;
}

static void copy(u8 *d, u8 *s, u32 n) {
	u32 i;
	for (i = 0; i < n; i++)
		d[i] = s[i];
}

static void put32(u8 *p, u32 v) {
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static u32 get32(u8 *p) {
	return ((u32)p[0]) |
	       ((u32)p[1] << 8) |
	       ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static u8 get_boot_drive(void) {
	return *(u8 *)0x0500;
}

static int bad_drive(u8 d) {
	if (d == 0x00) return 0;
	if (d >= 0x80 && d < 0xE0) return 0;
	return 1;
}

static int disk_read(u32 lba, u8 *buf) {
	u8 drive = get_boot_drive();

	if (bad_drive(drive)) {
		pred("Bad boot drive\n");
		return 0;
	}

	if (drive >= 0xE0) {
		pred("CD-ROM is read only\n");
		return 0;
	}

	if (!bios_disk_op(drive, lba, 0)) {
		pred("BIOS read failed\n");
		return 0;
	}

	copy(buf, bounce, 512);
	return 1;
}

static int disk_write(u32 lba, u8 *buf) {
	u8 drive = get_boot_drive();

	if (bad_drive(drive)) {
		pred("Bad boot drive\n");
		return 0;
	}

	if (drive >= 0xE0) {
		pred("CD-ROM is read only\n");
		return 0;
	}

	copy(bounce, buf, 512);

	if (!bios_disk_op(drive, lba, 1)) {
		pred("BIOS write failed\n");
		return 0;
	}

	return 1;
}

/* ================= SAVE STREAM ================= */

static u32 sw_lba;
static u32 sw_pos;
static u32 sw_secs;
static u32 sw_sum;

static int sw_flush(void) {
	if (sw_secs >= AS_SAVE_MAX_SECTORS) {
		pred("Failed to save, AnchorSand is full!\n");
		return 0;
	}

	if (!disk_write(sw_lba, secbuf)) {
		pred("Failed to flush write\n");
		return 0;
	}

	sw_lba++;
	sw_secs++;
	sw_pos = 0;
	zero(secbuf, 512);

	return 1;
}

static int sw_byte(u8 b) {
	secbuf[sw_pos++] = b;
	sw_sum += b;

	if (sw_pos == 512)
		return sw_flush();

	return 1;
}

static int sw_buf(u8 *p, u32 n) {
	u32 i;

	for (i = 0; i < n; i++) {
		if (!sw_byte(p[i]))
			return 0;
	}

	return 1;
}

static int sw_u32(u32 v) {
	u8 b[4];

	b[0] = v;
	b[1] = v >> 8;
	b[2] = v >> 16;
	b[3] = v >> 24;

	return sw_buf(b, 4);
}

/* ================= LOAD STREAM ================= */

static u32 sr_lba;
static u32 sr_pos;
static u32 sr_left;
static u32 sr_sum;

static int sr_fill(void) {
	if (!disk_read(sr_lba, secbuf)) {
		pred("Failed to read disk\n");
		return 0;
	}

	sr_lba++;
	sr_pos = 0;

	return 1;
}

static int sr_byte(u8 *b) {
	if (sr_left == 0) {
		pred("Read past EOF\n");
		return 0;
	}

	if (sr_pos == 512) {
		if (!sr_fill())
			return 0;
	}

	*b = secbuf[sr_pos++];
	sr_sum += *b;
	sr_left--;

	return 1;
}

static int sr_buf(u8 *p, u32 n) {
	u32 i;

	for (i = 0; i < n; i++) {
		if (!sr_byte(&p[i]))
			return 0;
	}

	return 1;
}

static int sr_u32(u32 *v) {
	u8 b[4];

	if (!sr_buf(b, 4))
		return 0;

	*v = get32(b);
	return 1;
}

/* ================= PUBLIC ================= */

int as_save_to_disk(void) {
	u32 i;
	u32 sum = 0;
	u8 *p = (u8 *)as_nodes;
	u32 total = sizeof(ASNode) * 64;
	u32 pos = 0;
	u32 lba = AS_SAVE_LBA + 1;
	u32 sectors = 0;

	while (pos < total) {
		zero(secbuf, 512);

		for (i = 0; i < 512 && pos < total; i++) {
			secbuf[i] = p[pos];
			sum += p[pos];
			pos++;
		}

		if (!disk_write(lba, secbuf)) {
			pred("Failed to write node\n");
			pred("Failed to write to filesystem\n");
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
		pred("Failed to write to header\n");
		pred("Failed to write to filesystem\n");
		return 0;
	}

	print("Files saved\n");
	return 1;
}

int as_load_from_disk(void) {
	u32 magic;
	u32 version;
	u32 sectors;
	u32 wanted_sum;
	u32 cwd;
	u32 total;
	u32 sum = 0;
	u32 pos = 0;
	u32 i;
	u32 lba = AS_SAVE_LBA + 1;
	u8 *p = (u8 *)as_nodes;

	if (!disk_read(AS_SAVE_LBA, secbuf)) {
		pred("Failed to write to header\n");
                pred("Failed to write to filesystem\n");
		return 0;
	}

	magic = get32(secbuf + 0);
	version = get32(secbuf + 4);
	sectors = get32(secbuf + 8);
	wanted_sum = get32(secbuf + 12);
	cwd = get32(secbuf + 16);
	total = get32(secbuf + 20);

	if (magic != AS_MAGIC) {
		pred("No files!\n");
		return 0;
	}

	if (version != 2) {
		pred("Bad AnchorSand save version\n");
		return 0;
	}

	if (total != sizeof(ASNode) * 64) {
		pred("Bad AnchorSand save total\n");
		return 0;
	}

	if (sectors == 0 || sectors > AS_SAVE_MAX_SECTORS) {
		pred("Bad AnchorSand save size\n");
		return 0;
	}

	while (pos < total) {
		if (!disk_read(lba, secbuf)) {
			pred("Failed to read node\n");
			pred("Failed to load filesystem\n");
			return 0;
		}

		for (i = 0; i < 512 && pos < total; i++) {
			p[pos] = secbuf[i];
			sum += secbuf[i];
			pos++;
		}

		lba++;
	}

	if (sum != wanted_sum) {
		pred("AnchorSand checksum bad\n");
		return 0;
	}

	as_cwd = (int)cwd;

	print("AnchorSand loaded\n");
	return 1;
}
