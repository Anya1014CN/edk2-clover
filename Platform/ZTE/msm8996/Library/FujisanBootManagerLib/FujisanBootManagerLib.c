/** @file
  Display-only BDS: connect GOP and start the FV-resident SimpleInit directly.
  No keys, physical storage, Android loader, ACPI patching or clock protocols.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Library/ArmLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PlatformBootManagerLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/GraphicsOutput.h>

// SimpleInit/src/main/SimpleInitMain.inf (do not change the submodule).
STATIC EFI_GUID mSimpleInitGuid =
  {0x6d77b2bb, 0x69eb, 0x42ab, {0xbe, 0xcf, 0x4f, 0x40, 0xc8, 0x95, 0x68, 0xc3}};
STATIC BOOLEAN mAttempted;

VOID EFIAPI PlatformBootManagerBeforeConsole(VOID)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  UINTN Count;
  UINTN Index;
  DEBUG((EFI_D_ERROR, "FJ BDS before-console\n"));
  EfiEventGroupSignal(&gEfiEndOfDxeEventGroupGuid);
  EfiBootManagerDispatchDeferredImages();
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid,
                                 NULL, &Count, &Handles);
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "FJ BDS no GOP: %r\n", Status));
    return;
  }
  for (Index = 0; Index < Count; Index++) {
    EFI_DEVICE_PATH_PROTOCOL *Path = DevicePathFromHandle(Handles[Index]);
    if (Path != NULL) {
      EfiBootManagerUpdateConsoleVariable(ConOut, Path, NULL);
      EfiBootManagerUpdateConsoleVariable(ErrOut, Path, NULL);
      Status = gBS->ConnectController(Handles[Index], NULL, NULL, TRUE);
      DEBUG((EFI_D_ERROR, "FJ BDS connect GOP: %r\n", Status));
    }
  }
  FreePool(Handles);
  // In particular, never index handles[1] for a nonexistent keypad protocol.
}

STATIC VOID StartSimpleInit(VOID)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  EFI_HANDLE Image;
  EFI_FIRMWARE_VOLUME2_PROTOCOL *Fv;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_DEVICE_PATH_PROTOCOL *FvPath;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH FileNode;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  UINTN Count;
  UINTN Index;
  VOID *Section;
  UINTN Size;
  UINT32 Authentication;

  if (mAttempted)
    return;
  mAttempted = TRUE;
  Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "FJ BDS SimpleInit not started: no validated GOP\n"));
    return;
  }
  gBS->SetWatchdogTimer(0, 0, 0, NULL);
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiFirmwareVolume2ProtocolGuid,
                                 NULL, &Count, &Handles);
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "FJ BDS no FV: %r\n", Status));
    return;
  }
  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol(Handles[Index], &gEfiFirmwareVolume2ProtocolGuid, (VOID **)&Fv);
    if (EFI_ERROR(Status))
      continue;
    Section = NULL;
    Size = 0;
    Status = Fv->ReadSection(Fv, &mSimpleInitGuid, EFI_SECTION_PE32, 0,
                            &Section, &Size, &Authentication);
    if (EFI_ERROR(Status))
      continue;
    FreePool(Section);
    FvPath = DevicePathFromHandle(Handles[Index]);
    if (FvPath == NULL) {
      Status = EFI_NOT_FOUND;
      continue;
    }
    EfiInitializeFwVolDevicepathNode(&FileNode, &mSimpleInitGuid);
    Path = AppendDevicePathNode(FvPath, (EFI_DEVICE_PATH_PROTOCOL *)&FileNode);
    if (Path == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      break;
    }
    DEBUG((EFI_D_ERROR, "FJ SIMPLEINIT LoadImage\n"));
    Status = gBS->LoadImage(FALSE, gImageHandle, Path, NULL, 0, &Image);
    FreePool(Path);
    if (EFI_ERROR(Status))
      break;
    DEBUG((EFI_D_ERROR, "FJ SIMPLEINIT StartImage\n"));
    Status = gBS->StartImage(Image, NULL, NULL);
    DEBUG((EFI_D_ERROR, "FJ SIMPLEINIT returned: %r\n", Status));
    gBS->UnloadImage(Image);
    FreePool(Handles);
    return;
  }
  FreePool(Handles);
  DEBUG((EFI_D_ERROR, "FJ SIMPLEINIT absent or load failed: %r\n", Status));
}

VOID EFIAPI PlatformBootManagerAfterConsole(VOID)
{
  DEBUG((EFI_D_ERROR, "FJ BDS after-console\n"));
  StartSimpleInit();
}

VOID EFIAPI PlatformBootManagerWaitCallback(IN UINT16 TimeoutRemain) {}

VOID EFIAPI PlatformBootManagerUnableToBoot(VOID)
{
  DEBUG((EFI_D_ERROR, "FJ BDS stopped; retain display/trace, no fallback boot or reset\n"));
  // Keep interrupts/timer running, but never retry applications or touch disks.
  for (;;)
    ArmCallWFI();
}
