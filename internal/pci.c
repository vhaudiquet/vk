//PCI : 8 BUSs, each bus can have up to 32 devices, each device can have 8 functions 
#include "system.h"
#include "error/error.h"
#include "memory/mem.h"
#include "internal.h"

#define PCI_DATA_PORT 0xCFC
#define PCI_COMMAND_PORT 0xCF8

pci_device_t* pci_first = 0;
pci_device_t* pci_last = 0;
u8 pci_devices_count = 0;

static void select_pci(u8 bus, u8 device, u8 function, u32 reg);
static u32 pci_read(u8 bus, u8 device, u8 function, u32 reg);
//unused : static void pci_write(u8 bus, u8 device, u8 function, u32 reg, u32 value);
static bool pci_has_functions(u8 bus, u8 device);
static void get_device_desc(u8 bus, u8 device, u8 function, pci_device_t* tr);
static void pci_check_bus(u32 bus);

void pci_install()
{
    kprintf("[PCI] Installing PCI devices...");
    pci_check_bus(0);
    kfree(pci_last);
    vga_text_okmsg();
}

static void pci_check_bus(u32 bus)
{
    u32 device; u32 function;
    for(device = 0; device < 32;device++)
    {
        u8 functions = (pci_has_functions((u8) bus, (u8) device) ? 8 : 1);
        for(function = 0;function < functions;function++)
        {
            if(pci_first == 0) {pci_first = pci_last = 
                #ifdef MEMLEAK_DBG
                kmalloc(sizeof(pci_device_t), "PCI Device struct");
                #else
                kmalloc(sizeof(pci_device_t));
                #endif
            }

            get_device_desc((u8) bus, (u8) device, (u8) function, pci_last);
            if(pci_last->vendor_id == 0xFFFF) {continue;}

            //if the device is a pci_to_pci
            if(pci_last->class_id == 0x06 && pci_last->subclass_id == 0x04)
            {
                u32 secondary_bus = (u8) pci_read((u8) bus, (u8) device, (u8) function, 0x19);
                pci_check_bus(secondary_bus);
            }

            pci_devices_count++;
            #ifdef MEMLEAK_DBG
            pci_last->next = kmalloc(sizeof(pci_device_t), "PCI Device struct");
            #else
            pci_last->next = kmalloc(sizeof(pci_device_t));
            #endif
            pci_last = pci_last->next;
        }
    }
}

u32 pci_read_device(pci_device_t* dev, u32 reg)
{
    return pci_read(dev->bus, dev->device, dev->function, reg);
}

static void select_pci(u8 bus, u8 device, u8 function, u32 reg)
{
    if(device > 32 || function > 8) fatal_kernel_error("Trying to select invalid PCI address", "SELECT_PCI");
    //select bus, device, function, register
    u32 id = ((u32) ((0x1 << 31) | ((bus & 0xFF) << 16) | ((device & 0x1F) << 11) | ((function & 0x07) << 8) | ((u8) (reg & 0xFC))));
    outl(PCI_COMMAND_PORT, id);
}

static u32 pci_read(u8 bus, u8 device, u8 function, u32 reg)
{
    select_pci(bus, device, function, reg);

    u32 result = inl(PCI_DATA_PORT);
    return result >> (8 * (reg % 4));
}

//static void pci_write(u8 bus, u8 device, u8 function, u32 reg, u32 value)
//{
//    select_pci(bus, device, function, reg);
//    outl(PCI_DATA_PORT, value);
//}

static bool pci_has_functions(u8 bus, u8 device)
{
    return (pci_read(bus, device, 0, 0x0E) & (1<<7)); //conversion warning
}

static void get_device_desc(u8 bus, u8 device, u8 function, pci_device_t* tr)
{
    tr->bus = bus;
    tr->device = device;
    tr->function = function;

    tr->vendor_id = (u16) pci_read(bus, device, function, 0x00);

    if(tr->vendor_id == 0xFFFF) return;

    tr->device_id = (u16) pci_read(bus, device, function, 0x02);

    tr->class_id = (u8) pci_read(bus, device, function, 0x0B);
    tr->subclass_id = (u8) pci_read(bus, device, function, 0x0A);
    tr->interface_id = (u8) pci_read(bus, device, function, 0x09);
    
    tr->revision = (u8) pci_read(bus, device, function, 0x08);
    tr->interrupt = pci_read(bus, device, function, 0x3C);
}