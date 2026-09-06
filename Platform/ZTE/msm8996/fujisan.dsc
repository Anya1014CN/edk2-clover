[Defines]
  VENDOR_NAME                    = ZTE
  PLATFORM_NAME                  = fujisan
  PLATFORM_GUID                  = 3c0de4cf-f9a9-4b3a-9e34-f0e66aa861f4
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Platform/Qualcomm/msm8996/msm8996.fdf
  DEVICE_DXE_FV_COMPONENTS       = Platform/ZTE/msm8996/fujisan.fdf.inc

!include Platform/Qualcomm/msm8996/msm8996.dsc

[BuildOptions.common]
  # SimpleInit and Android handoff are enabled in later milestones only.

[PcdsFixedAtBuild.common]
  gRenegadePkgTokenSpaceGuid.PcdDeviceVendor|"ZTE"
  gRenegadePkgTokenSpaceGuid.PcdDeviceProduct|"Axon M"
  gRenegadePkgTokenSpaceGuid.PcdDeviceCodeName|"fujisan"
