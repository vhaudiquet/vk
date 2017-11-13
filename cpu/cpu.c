/*  
    This file is part of VK.

    VK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    VK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with VK.  If not, see <http://www.gnu.org/licenses/>.
*/

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
    if(CPU_MAX_CPUID == 0)
    {
        return;
    }

    asm("cpuid":"=c"(cpu_f_ecx),"=d"(cpu_f_edx):"a"(1));

    //Special
    cpu_pse = (bool) (cpu_f_edx << 28 >> 31);

    if(CPU_MAX_CPUID >= 7)
    {
        asm("cpuid":"=b"(cpu_ef_ebx),"=c"(cpu_ef_ecx):"a"(7),"c"(0));
    }

    asm("cpuid":"=a"(cpu_max_ecpuid):"a"(0x80000000):);
    if(cpu_max_ecpuid > 0x80000000)
    {
        cpu_e_support = true;
    }
}

void cpu_detect(void)
{
    cpu_vendor_id();
    cpu_features();
}