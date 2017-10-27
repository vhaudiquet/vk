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

//Internal devices
#ifndef INTERNAL_HEAD
#define INTERNAL_HEAD

//PIC (Programmable Interrupt Controller)
void pic_install();

//PCI Controller
#define BAR0 0x10
#define BAR1 0x14
#define BAR2 0x18
#define BAR3 0x1C
#define BAR4 0x20
#define BAR5 0x24
typedef struct pci_device
{
    struct pci_device* next;
    u32 base_port;
    u32 interrupt;
    
    u16 vendor_id;
    u16 device_id;

    u8 bus; u8 device; u8 function;

    u8 class_id;
    u8 subclass_id;
    u8 interface_id;

    u8 revision;
} pci_device_t;
extern pci_device_t* pci_first;
extern pci_device_t* pci_last;
extern u8 pci_devices_count;

void pci_install();
u32 pci_read_device(pci_device_t* dev, u32 reg);
void pci_write_device(pci_device_t* dev, u32 reg, u32 value);

#endif