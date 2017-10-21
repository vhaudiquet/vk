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

#include "storage.h"

#define BYTES_PER_SECTOR 512

//PRDT : u32 aligned, contiguous in physical, cannot cross 64k boundary,  1 per ATA bus
static u8 ata_dma_read_28(u32 sector, u32 offset, u8* data, u32 count, ata_device_t* drive)
{
    if(sector & 0xF0000000) return DISK_FAIL_OUT;

    /*prepare a PRDT (Physical Region Descriptor Table)*/
    //u32 prdt_phys = reserve_block(prdt_size, PHYS_KERNELF_BLOCK_TYPE);
    //map_physical(prdt_phys, virtual, kernel_page_dir);

    /*Send the physical PRDT addr to Bus Master PRDT Register*/

    /*set read bit in the Bus Master Command Register*/

    /*Clear err/interrupt bits in Bus Master Status Register*/

    /*Select drive*/

    /*Send LBA and sector count*/

    //calculate sector count
	u32 scount = count / BYTES_PER_SECTOR;
	if(count % BYTES_PER_SECTOR) scount++;

	//calculate sector offset
	while(offset>=BYTES_PER_SECTOR) {offset-=BYTES_PER_SECTOR; sector++; scount--;}

	if(scount > 255) return DISK_FAIL_INTERNAL;

    /*send DMA transfer command*/

    /*Set the Start/Stop bit in Bus Master Command Register*/

    /*Wait for interrupt*/

    /*Reset Start/Stop bit*/

    /*Read controller and drive status to see if transfer went well*/

}