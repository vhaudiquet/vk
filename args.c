#include "system.h"

bool alive = false;
char aroot_dir[5] = {0};
u8 aboot_hint_present = 0;
bool asilent = false;

void args_parse(char* cmdline)
{
    if(*cmdline) return;

    char* ndash = strchr(cmdline, '-');
    while(ndash)
    {
        if(strcfirst("-live ", ndash) == 6)
        {alive = true; aboot_hint_present = KERNEL_MODE_LIVE;}
        if(strcfirst("-silent ", ndash) == 8)
        {asilent = true;}
        if(strcfirst("-root=", ndash) == 6)
        {strncpy(aroot_dir, ndash+6, 4); if(!aboot_hint_present) aboot_hint_present = KERNEL_MODE_INSTALLED;}
        
        ndash = strchr(ndash+1, '-');
    }
}