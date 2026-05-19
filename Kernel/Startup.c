extern void print(const char *s);
extern void sleep(unsigned int ms);
extern const char *VERSION;
extern const char *BUILD;
extern void pit_init_1000hz(void);

void startupBanner(void)
{
        print("AneoEngine ");
	print(VERSION);
	print(" Build ");
        print(BUILD);
        print("\n\n");
        pit_init_1000hz();
        sleep(500);
        print("Data addresses listed\n");
        print("\n");
        print("IVT:0x00000000-0x000003FF\n");
        print("Free low mem:0x00000500-0x00007BFF\n");
        print("Stack:0x00090000-downward\n");
        print("VGA mem:0x000A0000-0x000AFFFF\n");
        print("Text buf:0x000B0000-0x000BFFFF\n");
        print("\n");
        sleep(500);
        print("Boot sequence:\n");
        print("BIOS DATA  &OCU ->BOOTLOADER\n");
        print("        0x00000400-0x000004FF\n");
        print("BOOTLOADER  &OCU ->KERNEL ENTRY\n");
        print("        0x00007C00-0x00007DFF\n");
        print("        GTD &OCU:0x00007CDA-0x00007CF2\n");
        print("        gtd_code:0x00007CE2\n");
        print("        gtd_data:0x00007CEA\n");
        print("KERNEL ENTRY  &OCU ->KERNEL DATA\n");
        print("        0x00001000\n");
        print("KERNEL DATA  &OCU\n");
        print("        0x00001000-0x00004000\n");
        print("\n");
        print("Loading shell...\n");
}
