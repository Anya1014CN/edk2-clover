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
  gArmTokenSpaceGuid.PcdCpuVectorBaseAddress|0x80C40000
  gArmTokenSpaceGuid.PcdArmArchTimerFreqInHz|19200000
  # msm8996.dtsi uses GIC_PPI 13/14/11/10. EDK2 expects absolute INTIDs,
  # so add the 16-SGI offset (irq-gic-v3.c), not the PPI-relative value.
  # CNTP at non-secure EL1 drives INTID 30; GUI time reads alone do not
  # prove that the UEFI timer interrupt/event callbacks are working.
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|29
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|30
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|27
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|26
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x09BC0000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x09C00000

  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x00008996
  gEmbeddedTokenSpaceGuid.PcdPrePiStackBase|0x80C00000
  gEmbeddedTokenSpaceGuid.PcdPrePiStackSize|0x00040000
  gEmbeddedTokenSpaceGuid.PcdPrePiCpuIoSize|44

  # Display is discovered by FujisanDisplayDxe, never guessed by SerialPort.
  gQcomTokenSpaceGuid.PcdUefiMemPoolBase|0xA0000000
  gQcomTokenSpaceGuid.PcdUefiMemPoolSize|0x10000000
  # First ramoops dmesg record. A host-side collector reads it immediately
  # after adb reconnects, before Android userspace can consume it.
  gQcomTokenSpaceGuid.PcdFrameBufferSerialLogOnly|TRUE
  # Keep the non-wrapping trace compact while retaining ASSERT/exception output.
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000001
  gQcomTokenSpaceGuid.PcdUefiPstoreLogAddress|0xBFC00000
  gQcomTokenSpaceGuid.PcdUefiPstoreLogSize|0x00008000
  gQcomTokenSpaceGuid.PcdMipiFrameBufferAddress|0x83401000
  gQcomTokenSpaceGuid.PcdMipiFrameBufferWidth|1080
  gQcomTokenSpaceGuid.PcdMipiFrameBufferHeight|1920
  gQcomTokenSpaceGuid.PcdMipiFrameBufferVisibleWidth|1080
  gQcomTokenSpaceGuid.PcdMipiFrameBufferVisibleHeight|1920
  gQcomTokenSpaceGuid.PcdMipiFrameBufferStride|1088

  gArmPlatformTokenSpaceGuid.PcdCoreCount|4
  gArmPlatformTokenSpaceGuid.PcdClusterCount|2

  # Reserved for the later FDT/Android milestones.
  gSimpleInitTokenSpaceGuid.PcdDeviceTreeStore|0x83300000
  gSimpleInitTokenSpaceGuid.PcdLoggerdUseConsole|FALSE
  gSimpleInitTokenSpaceGuid.PcdLoggerdMinLevel|0xAE02

[LibraryClasses.common]
  PlatformBootManagerLib|Platform/ZTE/msm8996/Library/FujisanBootManagerLib/FujisanBootManagerLib.inf
  PlatformMemoryMapLib|Silicon/Qualcomm/msm8996/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.inf
  PlatformPeiLib|Silicon/Qualcomm/msm8996/Library/PlatformPeiLib/PlatformPeiLib.inf
  PlatformPrePiLib|Silicon/Qualcomm/msm8996/Library/PlatformPrePiLib/PlatformPrePiLib.inf
  AslUpdateLib|Silicon/Qualcomm/QcomPkg/Library/DxeAslUpdateLib/DxeAslUpdateLib.inf
  MsPlatformDevicesLib|Silicon/Qualcomm/msm8996/Library/MsPlatformDevicesLib/MsPlatformDevicesLib.inf
  SOCSmbiosInfoLib|Silicon/Qualcomm/msm8996/Library/SOCSmbiosInfoLib/SOCSmbiosInfoLib.inf

[Components.common]
  Platform/ZTE/msm8996/Drivers/FujisanDisplayDxe/FujisanDisplayDxe.inf

# Architecture-specific component overrides the COMMON declaration from
# SimpleInit.inc. Keep GUI INFO logs without DXE core protocol-install spam.
[Components.AARCH64]
  src/main/SimpleInitMain.inf {
    <PcdsFixedAtBuild>
      gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000041
  }
