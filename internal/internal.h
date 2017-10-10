//Internal devices
#ifndef INTERNAL_HEAD
#define INTERNAL_HEAD

//PIC (Programmable Interrupt Controller)
void pic_install();

//PCI Controller
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
void pci_print_devices();
u32 pci_read_device(pci_device_t* dev, u32 reg);

#endif