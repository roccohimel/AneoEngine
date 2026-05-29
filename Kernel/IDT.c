#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern void idt_load(u32);
extern void printad(const char *s, uint32_t x);
extern void printadl(const char *s, uint32_t x);

extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

extern void clear();
extern void print(char *);
extern unsigned char color;

struct IDTEntry
{
	u16 base_low;
	u16 sel;
	u8 always0;
	u8 flags;
	u16 base_high;
} __attribute__((packed));

struct IDTPtr
{
	u16 limit;
	u32 base;
} __attribute__((packed));

struct Registers
{
	u32 gs;
	u32 fs;
	u32 es;
	u32 ds;

	u32 edi;
	u32 esi;
	u32 ebp;
	u32 esp;
	u32 ebx;
	u32 edx;
	u32 ecx;
	u32 eax;

	u32 int_no;
	u32 err_code;

	u32 eip;
	u32 cs;
	u32 eflags;
};

struct IDTEntry idt[256];
struct IDTPtr idtp;

char *exceptions[] =
{
	"DivideByZero",
	"Debug",
	"NonMaskableInterrupt",
	"Breakpoint",
	"Overflow",
	"BoundRangeExceeded",
	"InvalidOpcode",
	"DeviceNotAvailable",
	"DoubleFault",
	"CoprocessorSegmentOverrun",
	"InvalidTSS",
	"SegmentNotPresent",
	"StackSegmentFault",
	"GeneralProtectionFault",
	"PageFault",
	"Reserved",
	"x87FloatingPoint",
	"AlignmentCheck",
	"MachineCheck",
	"SIMDFloatingPoint",
	"Virtualization",
	"ControlProtection",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"HypervisorInjection",
	"VMMCommunication",
	"SecurityException",
	"Reserved"
};

char *exception_info[] =
{
	"Division by zero.",
	"Debug exception.",
	"Non-maskable interrupt.",
	"Breakpoint hit.",
	"Overflow detected.",
	"BOUND range exceeded.",
	"Invalid CPU instruction.",
	"Device not available.",
	"Double fault.",
	"Coprocessor segment overrun.",
	"Invalid task state segment.",
	"Segment not present.",
	"Stack segment fault.",
	"General protection fault.",
	"Page fault.",
	"Reserved exception.",
	"x87 floating point fault.",
	"Alignment check fault.",
	"Machine check fault.",
	"SIMD floating point fault.",
	"Virtualization fault.",
	"Control protection fault.",
	"Reserved exception.",
	"Reserved exception.",
	"Reserved exception.",
	"Reserved exception.",
	"Reserved exception.",
	"Reserved exception.",
	"Hypervisor injection fault.",
	"VMM communication fault.",
	"Security exception.",
	"Reserved exception."
};

void print_hex_digit(u8 n)
{
	char c[2];

	if(n < 10)
		c[0] = '0' + n;
	else
		c[0] = 'A' + (n - 10);

	c[1] = 0;

	print(c);
}

void print_hex32(u32 n)
{
	print_hex_digit((n >> 28) & 0xF);
	print_hex_digit((n >> 24) & 0xF);
	print_hex_digit((n >> 20) & 0xF);
	print_hex_digit((n >> 16) & 0xF);
	print_hex_digit((n >> 12) & 0xF);
	print_hex_digit((n >> 8) & 0xF);
	print_hex_digit((n >> 4) & 0xF);
	print_hex_digit(n & 0xF);
}

void print_dec(u32 n)
{
	char buf[16];
	char c[2];
	int i;

	if(n == 0)
	{
		print("0");
		return;
	}

	i = 0;

	while(n > 0)
	{
		buf[i] = '0' + (n % 10);
		n = n / 10;
		i++;
	}

	while(i > 0)
	{
		i--;

		c[0] = buf[i];
		c[1] = 0;

		print(c);
	}
}

void idt_set_gate(u8 num, u32 base, u16 sel, u8 flags)
{
	idt[num].base_low = base & 0xFFFF;
	idt[num].base_high = (base >> 16) & 0xFFFF;
	idt[num].sel = sel;
	idt[num].always0 = 0;
	idt[num].flags = flags;
}

void print_exception_name(u32 int_no)
{
	if(int_no < 32)
		print(exceptions[int_no]);
	else
		print("Unknown");
}

void print_exception_info(u32 int_no)
{
	if(int_no < 32)
		print(exception_info[int_no]);
	else
		print("Unknown CPU exception.");
}

void print_caller_line(u32 addr, char *name)
{
	print_hex32(addr);
	print(":");
	print(name);
	print("\n");
}

void exception_handler(struct Registers *r)
{
	color = 0x0F;
	clear();

	print("Fault: ");

	if(r->int_no < 32)
		print(exceptions[r->int_no]);
	else
		print("Unknown");

	printadl(" Error Code", r->err_code);
	print("\n");

	printadl("Faulting Instruction", r->eip);
        printadl("&exception_handler", (u32)&exception_handler);
        printadl("&idt", (u32)&idt);
        printadl("&idtp", (u32)&idtp);
        printadl("Fault Addr", r->eip);
        printadl("IDT Base", idtp.base);
        printadl("IDT Limit", idtp.limit);

	print("\n\n");
	print("          AneoEngine ERROR\n");
	print("Kernel halted due to CPU exception\n\n");

	print("Exception Report:\n");

	printadl("EIP", r->eip);
	printadl("EBP", r->ebp);

	print("\n");

	printadl("ESP", r->esp);
	printadl("EAX", r->eax);

        print("\n");

	printadl("EBX", r->ebx);
	printadl("ECX", r->ecx);

        print("\n");

	printadl("EDX", r->edx);
	printadl("ESI", r->esi);

        print("\n");

	printadl("EDI", r->edi);
	printadl("CS", r->cs);

        print("\n");

	printadl("DS", r->ds);
	printadl("ES", r->es);

        print("\n");

	printadl("FS", r->fs);
	printadl("GS", r->gs);

        print("\n");

	printadl("EFLAGS", r->eflags);

	for(;;)
		__asm__ __volatile__("cli; hlt");
}

void idt_init()
{
	int i;

	idtp.limit = sizeof(struct IDTEntry) * 256 - 1;
	idtp.base = (u32)&idt;

	for(i = 0; i < 256; i++)
		idt_set_gate(i, 0, 0, 0);

	idt_set_gate(0, (u32)isr0, 0x08, 0x8E);
	idt_set_gate(1, (u32)isr1, 0x08, 0x8E);
	idt_set_gate(2, (u32)isr2, 0x08, 0x8E);
	idt_set_gate(3, (u32)isr3, 0x08, 0x8E);
	idt_set_gate(4, (u32)isr4, 0x08, 0x8E);
	idt_set_gate(5, (u32)isr5, 0x08, 0x8E);
	idt_set_gate(6, (u32)isr6, 0x08, 0x8E);
	idt_set_gate(7, (u32)isr7, 0x08, 0x8E);
	idt_set_gate(8, (u32)isr8, 0x08, 0x8E);
	idt_set_gate(9, (u32)isr9, 0x08, 0x8E);
	idt_set_gate(10, (u32)isr10, 0x08, 0x8E);
	idt_set_gate(11, (u32)isr11, 0x08, 0x8E);
	idt_set_gate(12, (u32)isr12, 0x08, 0x8E);
	idt_set_gate(13, (u32)isr13, 0x08, 0x8E);
	idt_set_gate(14, (u32)isr14, 0x08, 0x8E);
	idt_set_gate(15, (u32)isr15, 0x08, 0x8E);
	idt_set_gate(16, (u32)isr16, 0x08, 0x8E);
	idt_set_gate(17, (u32)isr17, 0x08, 0x8E);
	idt_set_gate(18, (u32)isr18, 0x08, 0x8E);
	idt_set_gate(19, (u32)isr19, 0x08, 0x8E);
	idt_set_gate(20, (u32)isr20, 0x08, 0x8E);
	idt_set_gate(21, (u32)isr21, 0x08, 0x8E);
	idt_set_gate(22, (u32)isr22, 0x08, 0x8E);
	idt_set_gate(23, (u32)isr23, 0x08, 0x8E);
	idt_set_gate(24, (u32)isr24, 0x08, 0x8E);
	idt_set_gate(25, (u32)isr25, 0x08, 0x8E);
	idt_set_gate(26, (u32)isr26, 0x08, 0x8E);
	idt_set_gate(27, (u32)isr27, 0x08, 0x8E);
	idt_set_gate(28, (u32)isr28, 0x08, 0x8E);
	idt_set_gate(29, (u32)isr29, 0x08, 0x8E);
	idt_set_gate(30, (u32)isr30, 0x08, 0x8E);
	idt_set_gate(31, (u32)isr31, 0x08, 0x8E);

	idt_load((u32)&idtp);
}
