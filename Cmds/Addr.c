extern void print(const char *s);

void addr(void)
{
	print("IVT:0x00000000-0x000003FF\n");
	print("BIOS DATA:0x00000400-0x000004FF\n");
	print("AEBOOT:0x00007C00-0x00007DFF\n");
	print("BOOT SIG:0x00007DFE-0x00007DFF\n");
	print("GDT_CODE:gtd_start+0x00000008\n");
	print("GTD_DATA:gtd_start+0x00000010\n");
	print("KERNEL:0x00001000-0x00004000\n");
	print("KERNEL BIN:0x00001000-0x00003FFF\n");
	print("STACK TOP:0x00090000\n");
	print("VGA MEM:0x000A0000-0x000BFFF\n");
	print("VGA TEXT BUF:0x000B8000\n");
	print("BIOS ROM:0x000F0000-0x000FFFFF\n");
	print("32-BIT Protected mode MEM ADDR:\n");
	print("        CS=0x00000008\n");
	print("        DS=0x00000010\n");
	print("        ES=0x00000010\n");
	print("        FS=0x00000010\n");
	print("        GS=0x00000010\n");
	print("        SS=0x00000010\n");
	print("        ESP=0x00090000\n");
	print("        JMP 0x00000008:INIT_PM\n");
	print("        JMP/CALL 0x00001000\n");
}
