/* SimpleFbDxe: Simple FrameBuffer */
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/FrameBufferBltLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <PiDxe.h>
#include <Protocol/GraphicsOutput.h>
#include <Uefi.h>

/// Defines
/*
 * Convert enum video_log2_bpp to bytes and bits. Note we omit the outer
 * brackets to allow multiplication by fractional pixels.
 */
#define VNBYTES(bpix) (1 << (bpix)) / 8
#define VNBITS(bpix) (1 << (bpix))

#define FB_BITS_PER_PIXEL (32)
#define FB_BYTES_PER_PIXEL (FB_BITS_PER_PIXEL / 8)

/*
 * Bits per pixel selector. Each value n is such that the bits-per-pixel is
 * 2 ^ n
 */
enum video_log2_bpp {
  VIDEO_BPP1 = 0,
  VIDEO_BPP2,
  VIDEO_BPP4,
  VIDEO_BPP8,
  VIDEO_BPP16,
  VIDEO_BPP32,
};

typedef struct {
  VENDOR_DEVICE_PATH DisplayDevicePath;
  EFI_DEVICE_PATH    EndDevicePath;
} DISPLAY_DEVICE_PATH;

DISPLAY_DEVICE_PATH mDisplayDevicePath = {
    {{HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
          (UINT8)(sizeof(VENDOR_DEVICE_PATH)),
          (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8),
      }},
     EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID},
    {END_DEVICE_PATH_TYPE,
     END_ENTIRE_DEVICE_PATH_SUBTYPE,
     {sizeof(EFI_DEVICE_PATH_PROTOCOL), 0}}};

/// Declares

STATIC FRAME_BUFFER_CONFIGURE *mFrameBufferBltLibConfigure;
STATIC UINTN                   mFrameBufferBltLibConfigureSize;

STATIC
EFI_STATUS
EFIAPI
DisplayQueryMode(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber,
    OUT UINTN *SizeOfInfo, OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);

STATIC
EFI_STATUS
EFIAPI
DisplaySetMode(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber);

STATIC
EFI_STATUS
EFIAPI
DisplayBlt(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *            BltBuffer,
    OPTIONAL IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
    IN UINTN SourceX, IN UINTN SourceY, IN UINTN DestinationX,
    IN UINTN DestinationY, IN UINTN Width, IN UINTN Height,
    IN UINTN Delta OPTIONAL);

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL mDisplay = {
    DisplayQueryMode, DisplaySetMode, DisplayBlt, NULL};

STATIC
EFI_STATUS
EFIAPI
DisplayQueryMode(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber,
    OUT UINTN *SizeOfInfo, OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info)
{
  EFI_STATUS Status;
  Status = gBS->AllocatePool(
      EfiBootServicesData, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
      (VOID **)Info);

  ASSERT_EFI_ERROR(Status);

  *SizeOfInfo                   = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  (*Info)->Version              = This->Mode->Info->Version;
  (*Info)->HorizontalResolution = This->Mode->Info->HorizontalResolution;
  (*Info)->VerticalResolution   = This->Mode->Info->VerticalResolution;
  (*Info)->PixelFormat          = This->Mode->Info->PixelFormat;
  (*Info)->PixelsPerScanLine    = This->Mode->Info->PixelsPerScanLine;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DisplaySetMode(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber)
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DisplayBlt(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *            BltBuffer,
    OPTIONAL IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
    IN UINTN SourceX, IN UINTN SourceY, IN UINTN DestinationX,
    IN UINTN DestinationY, IN UINTN Width, IN UINTN Height,
    IN UINTN Delta OPTIONAL)
{

  RETURN_STATUS Status;
  EFI_TPL       Tpl;
  //
  // We have to raise to TPL_NOTIFY, so we make an atomic write to the frame
  // buffer. We would not want a timer based event (Cursor, ...) to come in
  // while we are doing this operation.
  //
  Tpl    = gBS->RaiseTPL(TPL_NOTIFY);
  Status = FrameBufferBlt(
      mFrameBufferBltLibConfigure, BltBuffer, BltOperation, SourceX, SourceY,
      DestinationX, DestinationY, Width, Height, Delta);
  gBS->RestoreTPL(Tpl);

  // zhuowei: hack: flush the cache manually since my memory maps are still
  // broken
  WriteBackInvalidateDataCacheRange(
      (void *)mDisplay.Mode->FrameBufferBase, mDisplay.Mode->FrameBufferSize);
  // zhuowei: end hack

  return RETURN_ERROR(Status) ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SimpleFbDxeInitialize(
    IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{

  EFI_STATUS Status             = EFI_SUCCESS;
  EFI_HANDLE hUEFIDisplayHandle = NULL;

  /* Retrieve simple frame buffer from the pre-SEC bootloader. */
  DEBUG(
      (EFI_D_ERROR,
       "SimpleFbDxe: ENTER\n"));
  DEBUG(
      (EFI_D_ERROR,
       "SimpleFbDxe: Retrieve MIPI FrameBuffer parameters from PCD\n"));
  ARM_MEMORY_REGION_DESCRIPTOR_EX DisplayMemoryRegion;
  UINT32 MipiFrameBufferAddr = FixedPcdGet32(PcdMipiFrameBufferAddress);
  UINT32 MipiFrameBufferWidth = FixedPcdGet32(PcdMipiFrameBufferWidth);
  UINT32 MipiFrameBufferHeight = FixedPcdGet32(PcdMipiFrameBufferHeight);
  UINT32 MipiFrameBufferVisibleWidth = FixedPcdGet32(PcdMipiFrameBufferVisibleWidth);
  UINT32 MipiFrameBufferVisibleHeight = FixedPcdGet32(PcdMipiFrameBufferVisibleHeight);
  UINT32 MipiFrameBufferStride = FixedPcdGet32(PcdMipiFrameBufferStride);
  UINT64 FrameBufferSize = (UINT64)MipiFrameBufferStride *
                           FB_BYTES_PER_PIXEL * MipiFrameBufferHeight;
  EFI_STATUS DisplayStatus = LocateMemoryMapAreaByAddress(
      MipiFrameBufferAddr, &DisplayMemoryRegion);

  DEBUG((EFI_D_ERROR,
         "SimpleFbDxe: fb=0x%08x mode=%ux%u visible=%ux%u stride=%u size=0x%llx map=%r mapaddr=0x%llx maplen=0x%llx\n",
         MipiFrameBufferAddr, MipiFrameBufferWidth, MipiFrameBufferHeight,
         MipiFrameBufferVisibleWidth, MipiFrameBufferVisibleHeight,
         MipiFrameBufferStride, FrameBufferSize, DisplayStatus,
         DisplayStatus == EFI_SUCCESS ? DisplayMemoryRegion.Address : 0,
         DisplayStatus == EFI_SUCCESS ? DisplayMemoryRegion.Length : 0));

  /*
   * The bootloader still scans out this region.  Keep the PCD explicit and
   * verify it against the platform map, rather than accidentally selecting
   * Android's different cont_splash reservation by a generic label.
   */
  if ((DisplayStatus == EFI_SUCCESS &&
       (MipiFrameBufferAddr != DisplayMemoryRegion.Address ||
        FrameBufferSize > DisplayMemoryRegion.Length)) ||
      MipiFrameBufferAddr == 0 || MipiFrameBufferWidth == 0 ||
      MipiFrameBufferHeight == 0 || MipiFrameBufferVisibleWidth == 0 ||
      MipiFrameBufferVisibleHeight == 0 || MipiFrameBufferStride == 0 ||
      MipiFrameBufferVisibleWidth > MipiFrameBufferWidth ||
      MipiFrameBufferWidth > MipiFrameBufferStride ||
      MipiFrameBufferVisibleHeight > MipiFrameBufferHeight) {
    DEBUG((EFI_D_ERROR, "SimpleFbDxe: Invalid fixed FrameBuffer parameters\n"));
    return EFI_DEVICE_ERROR;
  }

  if (DisplayStatus != EFI_SUCCESS) {
    DEBUG((EFI_D_ERROR,
           "SimpleFbDxe: platform map has no entry for fixed XBL framebuffer; continuing\n"));
  }

  DEBUG((EFI_D_ERROR, "SimpleFbDxe: S1 validation passed\n"));

  /* Prepare struct */
  if (mDisplay.Mode == NULL) {
    Status = gBS->AllocatePool(
        EfiBootServicesData, sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE),
        (VOID **)&mDisplay.Mode);

    ASSERT_EFI_ERROR(Status);
    if (EFI_ERROR(Status))
      return Status;

    ZeroMem(mDisplay.Mode, sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE));
  }
  DEBUG((EFI_D_ERROR, "SimpleFbDxe: S2 mode allocated\n"));

  if (mDisplay.Mode->Info == NULL) {
    Status = gBS->AllocatePool(
        EfiBootServicesData, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
        (VOID **)&mDisplay.Mode->Info);

    ASSERT_EFI_ERROR(Status);
    if (EFI_ERROR(Status))
      return Status;

    ZeroMem(mDisplay.Mode->Info, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION));
  }
  DEBUG((EFI_D_ERROR, "SimpleFbDxe: S3 info allocated\n"));

  /* Set information */
  mDisplay.Mode->MaxMode       = 1;
  mDisplay.Mode->Mode          = 0;
  mDisplay.Mode->Info->Version = 0;

  mDisplay.Mode->Info->HorizontalResolution = MipiFrameBufferVisibleWidth;
  mDisplay.Mode->Info->VerticalResolution   = MipiFrameBufferVisibleHeight;

  /* SimpleFB runs on a8r8g8b8 (VIDEO_BPP32) for DB410c */
  EFI_PHYSICAL_ADDRESS FrameBufferAddress = MipiFrameBufferAddr;

  mDisplay.Mode->Info->PixelsPerScanLine = MipiFrameBufferStride;
  mDisplay.Mode->Info->PixelFormat = PixelRedGreenBlueReserved8BitPerColor;
  mDisplay.Mode->SizeOfInfo      = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  mDisplay.Mode->FrameBufferBase = FrameBufferAddress;
  mDisplay.Mode->FrameBufferSize = FrameBufferSize;

  //
  // Create the FrameBufferBltLib configuration.
  //
  Status = FrameBufferBltConfigure(
      (VOID *)(UINTN)mDisplay.Mode->FrameBufferBase, mDisplay.Mode->Info,
      mFrameBufferBltLibConfigure, &mFrameBufferBltLibConfigureSize);
  if (Status == RETURN_BUFFER_TOO_SMALL) {
    mFrameBufferBltLibConfigure = AllocatePool(mFrameBufferBltLibConfigureSize);
    if (mFrameBufferBltLibConfigure != NULL) {
      Status = FrameBufferBltConfigure(
          (VOID *)(UINTN)mDisplay.Mode->FrameBufferBase, mDisplay.Mode->Info,
          mFrameBufferBltLibConfigure, &mFrameBufferBltLibConfigureSize);
    }
  }
  DEBUG((EFI_D_ERROR, "SimpleFbDxe: S4 blt configure=%r\n", Status));
  ASSERT_EFI_ERROR(Status);
  if (EFI_ERROR(Status))
    return Status;

  DEBUG((EFI_D_ERROR, "SimpleFbDxe: S5 clear\n"));
  // zhuowei: clear the screen to black
  // UEFI standard requires this, since text is white - see
  // OvmfPkg/QemuVideoDxe/Gop.c
  ZeroMem((void *)FrameBufferAddress, FrameBufferSize);
  // hack: clear cache
  WriteBackInvalidateDataCacheRange(
      (void *)FrameBufferAddress, FrameBufferSize);
  // zhuowei: end
  DEBUG((EFI_D_ERROR, "SimpleFbDxe: S6 flush complete\n"));

  /* Register handle */
  Status = gBS->InstallMultipleProtocolInterfaces(
      &hUEFIDisplayHandle, &gEfiDevicePathProtocolGuid, &mDisplayDevicePath,
      &gEfiGraphicsOutputProtocolGuid, &mDisplay, NULL);

  DEBUG((EFI_D_ERROR, "SimpleFbDxe: S7 GOP install=%r\n", Status));
  ASSERT_EFI_ERROR(Status);

  return Status;
}
