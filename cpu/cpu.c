/*
* CPU Informations
*/
#include "cpu.h"

char CPU_VENDOR[13]; //CPU Vendor : GenuineIntel for intel (as example)
u32 CPU_MAX_CPUID; //MAX standard CPUID instruction supported by processor
u32 cpu_f_edx, cpu_f_ecx; //Registers content after CPUID with EAX=1 (CPU_FEATURES)
u32 cpu_ef_ebx, cpu_ef_ecx; //Registers content after CPUID with EAX=7,ECX=0 (CPU_EXTENDED_FEATURES)
bool cpu_e_support; //Does CPU support EXTENDED CPUID ?
u32 cpu_max_ecpuid; //MAX extended CPUID instruction supported by processor

bool cpu_pse = false;

void cpu_vendor_id()
{
    int* where = (int*) CPU_VENDOR;
    __asm__ volatile ("cpuid":"=a"(CPU_MAX_CPUID),"=b"(*(where+0)),
               "=d"(*(where+1)),"=c"(*(where+2)):"a"(0));
    CPU_VENDOR[12] = '\0';

    //kprintf("[CPU] CPU Vendor : %s\n", CPU_VENDOR);
}

void cpu_features()
{
    kprintf("[CPU] Detecting CPU features...");
    if(CPU_MAX_CPUID == 0)
    {
        vga_text_failmsg();
        //kprintf("\n[CPU] %lCPUID not supported. Could not get CPU features...\n", 2);
        return;
    }

    asm("cpuid":"=c"(cpu_f_ecx),"=d"(cpu_f_edx):"a"(1));
    vga_text_okmsg();

    //Special
    cpu_pse = (bool) (cpu_f_edx << 28 >> 31);

    kprintf("[CPU] Detecting extended features...");
    if(CPU_MAX_CPUID >= 7)
    {
        asm("cpuid":"=b"(cpu_ef_ebx),"=c"(cpu_ef_ecx):"a"(7),"c"(0));
        vga_text_okmsg();
    }
    else vga_text_skipmsg();//kprintf("[CPU] CPUID extended features not supported.\n");

    asm("cpuid":"=a"(cpu_max_ecpuid):"a"(0x80000000):);
    if(cpu_max_ecpuid > 0x80000000)
    {
        cpu_e_support = true;
        //kprintf("[CPU] %lCPUID extended instructions supported.\n", 1);
    }
    //else kprintf("[CPU] CPUID extended instructions not supported.\n");
}

void cpu_detect(void)
{
    cpu_vendor_id();
    cpu_features();
}