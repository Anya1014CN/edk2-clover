#include <Library/BaseLib.h>
#include <Library/PlatformMemoryMapLib.h>

/*
 * Initial Fujisan/MSM8996Pro memory map.
 *
 * This is a bring-up map, not a final production memory map.  The 0x9d400000
 * display region and the 0xce000000 firmware placement still need to be
 * confirmed against the original bootloader before any device boot test.
 * Do not use this table as permission to write an image to eMMC.
 */
static ARM_MEMORY_REGION_DESCRIPTOR_EX gDeviceMemoryDescriptorEx[] = {
    /* Name               Address     Length      HobOption  ResourceAttribute  ArmAttributes
                                                        ResourceType  MemoryType */

    /* DDR below the inherited bootloader/display workspace. */
    {"RAM Partition",    0x80000000, 0x1D400000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
    {"Display Reserved", 0x9D400000, 0x02400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},
    {"FV Region",        0x9F800000, 0x00400000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
    {"SEC Heap",         0x9FC00000, 0x00300000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
    {"CPU Vectors",      0x9FF8C000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
    {"MMU PageTables",   0x9FF8D000, 0x00003000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
    {"UEFI Stack",       0x9FF90000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
    {"UEFI Reserved",    0x9FFD0000, 0x00023000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
    {"Log Buffer",       0x9FFF7000, 0x00008000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
    {"Info Blk",         0x9FFFF000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},

    /*
     * The DXE pool is split around the production ramoops reservation from
     * msm8996-mtp-fujisan.dtsi (0xbfc00000..0xc0000000).
     */
    {"DXE Heap",         0xA0000000, 0x1FC00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
    {"ramoops",          0xBFC00000, 0x00400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, UNCACHED_UNBUFFERED_XN},
    {"DXE Heap",         0xC0000000, 0x0E000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
    {"UEFI FD",          0xCE000000, 0x00700000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
    {"RAM Partition",    0xCE700000, 0x21700000, Mem4G,  SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},

    /* MMIO ranges confirmed by the MSM8996 kernel DTS or Qualcomm layout. */
    {"IMEM Base",        0x14680000, 0x0003F000, NoHob,  MMAP_IO, INITIALIZED, Conv, NS_DEVICE},
    {"IMEM Cookie Base", 0x146BF000, 0x00001000, AddDev, MMAP_IO, INITIALIZED, Conv, NS_DEVICE},
    {"SPMI",             0x0400A000, 0x02800000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"TLMM",             0x01010000, 0x00300000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"SDCC1",            0x07464000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"QTIMER",           0x00900000, 0x00030000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"WATCHDOG",         0x09830000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"GIC Distributor",  0x09BC0000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"GIC Redistributor",0x09C00000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},

    {"Terminator",       0,          0,          0,      0,       0,          0,      0}
};

ARM_MEMORY_REGION_DESCRIPTOR_EX *GetPlatformMemoryMap()
{
  return gDeviceMemoryDescriptorEx;
}
