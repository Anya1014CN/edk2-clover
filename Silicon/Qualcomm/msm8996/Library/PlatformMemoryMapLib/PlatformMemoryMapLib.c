#include <Library/BaseLib.h>
#include <Library/PlatformMemoryMapLib.h>

/* Display-only map. See Platform/ZTE/msm8996/README.md for evidence and
 * the deliberate exclusions. Neither XBL nor Linux splash is allocatable. */
static ARM_MEMORY_REGION_DESCRIPTOR_EX gDeviceMemoryDescriptorEx[] = {
    /* Name               Address     Length      HobOption  ResourceAttribute  ArmAttributes
                                                        ResourceType  MemoryType */

    {"RAM Partition",      0x80000000, 0x00100000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
    {"HLOS 0",             0x80100000, 0x00100000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
    {"XBL FD Reserved",    0x80200000, 0x00100000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
    {"MPPark Code",        0x80300000, 0x00080000, AddMem, MEM_RES, UNCACHEABLE, RtCode, UNCACHED_UNBUFFERED_XN},
    {"XBL Reserved",       0x80380000, 0x00080000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, UNCACHED_UNBUFFERED_XN},
    {"UEFI Display Reserved",0x80400000, 0x00800000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},
    {"UEFI Stack",         0x80C00000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
    {"CPU Vectors",        0x80C40000, 0x00010000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
    {"Reserved Cached",    0x80C50000, 0x000B0000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
    {"HLOS 1",             0x80D00000, 0x02700000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
    {"DFPS Data",          0x83400000, 0x00001000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, UNCACHED_UNBUFFERED_XN},
    {"Display Reserved",   0x83401000, 0x023FF000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},
    {"Removed Regions",    0x85800000, 0x03000000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, UNCACHED_UNBUFFERED_XN},
    {"Modem Region",       0x88800000, 0x06200000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, UNCACHED_UNBUFFERED_XN},
    {"Peripheral Region",  0x8EA00000, 0x02D00000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, UNCACHED_UNBUFFERED_XN},
    {"RAM Partition",      0x91700000, 0x0BD00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
    {"FV Region",          0x9F800000, 0x00400000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
    {"SEC Heap",           0x9FC00000, 0x00300000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
    {"CPU Vectors Alias",  0x9FF8C000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK},
    {"MMU PageTables",     0x9FF8D000, 0x00003000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
    {"UEFI Reserved",      0x9FFD0000, 0x00023000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
    {"Log Buffer",         0x9FFF7000, 0x00008000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
    {"Info Blk",           0x9FFFF000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},

    /* UEFI pool and production ramoops interval. */
    {"DXE Heap",           0xA0000000, 0x10000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
    {"High Display Guard", 0xB0000000, 0x0FC00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, UNCACHED_UNBUFFERED_XN},
    {"ramoops",            0xBFC00000, 0x00400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, UNCACHED_UNBUFFERED_XN},
    {"DXE Heap High",      0xC0000000, 0x0E000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
    {"UEFI FD",            0xCE000000, 0x00800000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
    {"RAM Partition High", 0xCE800000, 0x31800000, Mem4G,  SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},

    /* MMIO ranges confirmed by the MSM8996 kernel DTS or Qualcomm layout. */
    {"IMEM Base",        0x14680000, 0x0003F000, NoHob,  MMAP_IO, INITIALIZED, Conv, NS_DEVICE},
    {"IMEM Cookie Base", 0x146BF000, 0x00001000, AddDev, MMAP_IO, INITIALIZED, Conv, NS_DEVICE},
    {"SPMI",             0x0400A000, 0x02800000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"TLMM",             0x01010000, 0x00300000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"GCC Clock Control", 0x00300000, 0x00090000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"QUP I2C12",        0x075BA000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"SDCC1",            0x07464000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"MDSS MDP",         0x00900000, 0x00090000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"WATCHDOG",         0x09830000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"GIC Distributor",  0x09BC0000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
    {"GIC Redistributor",0x09C00000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},

    {"Terminator",       0,          0,          0,      0,       0,          0,      0}
};

ARM_MEMORY_REGION_DESCRIPTOR_EX *GetPlatformMemoryMap()
{
  return gDeviceMemoryDescriptorEx;
}
