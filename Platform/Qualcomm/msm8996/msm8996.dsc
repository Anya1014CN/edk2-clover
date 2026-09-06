## @file
#
#  Initial Qualcomm MSM8996Pro platform description for ZTE Axon M/Fujisan.
#  This milestone provides the platform skeleton only; binary BSP DXEs and
#  Android boot handoff are intentionally not enabled yet.
#

[Defines]
  SOC_PLATFORM            = MSM8996
  USE_PHYSICAL_TIMER      = TRUE

!include Silicon/Qualcomm/QcomPkg/QcomCommonDsc.inc

[PcdsFixedAtBuild.common]
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000
  # Approximately 4 GiB installed; top 256 MiB is kept outside the initial map.
  gArmTokenSpaceGuid.PcdSystemMemorySize|0xF0000000

  # Confirmed/derived from msm8996.dtsi where noted; vector placement remains
  # a bootloader-layout assumption and must be validated before boot testing.
  gArmTokenSpaceGuid.PcdCpuVectorBaseAddress|0x9FF8C000
  gArmTokenSpaceGuid.PcdArmArchTimerFreqInHz|19200000
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|17
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|18
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x09BC0000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x09C00000

  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x00008996
  gEmbeddedTokenSpaceGuid.PcdPrePiStackBase|0x9FF90000
  gEmbeddedTokenSpaceGuid.PcdPrePiStackSize|0x00040000
  gEmbeddedTokenSpaceGuid.PcdPrePiCpuIoSize|44

  # The DXE pool and framebuffer address are provisional.  Framebuffer address
  # is deliberately zero until the original bootloader framebuffer is measured;
  # this prevents accidental writes to an unverified physical address.
  gQcomTokenSpaceGuid.PcdUefiMemPoolBase|0xA0000000
  gQcomTokenSpaceGuid.PcdUefiMemPoolSize|0x1FC00000
  gQcomTokenSpaceGuid.PcdMipiFrameBufferAddress|0x00000000
  gQcomTokenSpaceGuid.PcdMipiFrameBufferWidth|1080
  gQcomTokenSpaceGuid.PcdMipiFrameBufferHeight|1920

  gArmPlatformTokenSpaceGuid.PcdCoreCount|4
  gArmPlatformTokenSpaceGuid.PcdClusterCount|2

  # Reserved for the later FDT/Android milestones.
  gSimpleInitTokenSpaceGuid.PcdDeviceTreeStore|0x83300000
  gSimpleInitTokenSpaceGuid.PcdLoggerdUseConsole|FALSE

[LibraryClasses.common]
  PlatformMemoryMapLib|Silicon/Qualcomm/msm8996/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.inf
  PlatformPeiLib|Silicon/Qualcomm/msm8996/Library/PlatformPeiLib/PlatformPeiLib.inf
  PlatformPrePiLib|Silicon/Qualcomm/msm8996/Library/PlatformPrePiLib/PlatformPrePiLib.inf
  AslUpdateLib|Silicon/Qualcomm/QcomPkg/Library/DxeAslUpdateLib/DxeAslUpdateLib.inf
  MsPlatformDevicesLib|Silicon/Qualcomm/msm8996/Library/MsPlatformDevicesLib/MsPlatformDevicesLib.inf
  SOCSmbiosInfoLib|Silicon/Qualcomm/msm8996/Library/SOCSmbiosInfoLib/SOCSmbiosInfoLib.inf
