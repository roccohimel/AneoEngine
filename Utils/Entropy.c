#include <stdint.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned int u32;
typedef unsigned long long u64;

void printint(unsigned int n);
extern u8 color;
extern void print(const char *s);

static inline u64 rdtsc()
{
	u32 lo, hi;
	__asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
	return ((u64)hi << 32) | lo;
}
u32 rand5()
{
	u64 t = rdtsc();
	t ^= t >> 33;
	t *= 0xff51afd7ed558ccdULL;
	t ^= t >> 33;
	t *= 0xc4ceb9fe1a85ec53ULL;
	t ^= t >> 33;
	return ((u32)t % 90000) + 10000;
}

void entropy(void)
{
	print("Kernel loaded Utils/Printer\n");
	u32 n = rand5();
	print("Entropy value: ");
	u8 oldcolor = color;
	color = 0x1A;
	printint(n);
	color = oldcolor;
	print("\n");
}
