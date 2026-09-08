/** @file
  Fujisan single-panel bootloader handoff GOP. No panel/clock/SMMU programming.
  Register definitions are from the local msm8996-mdss.dtsi/mdss_mdp_hwio.h;
  see README.md for the supported (deliberately narrow) handoff contract.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/FrameBufferBltLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>

#define MDP_BASE       0x00900000U
#define CTL_TOP        0x14
#define CTL_START      0x1C
#define CMD_MODE       BIT17
// msm8996 mdss.h reserves 0 for NO_INTF: INTF0=1, INTF1/DSI0=2,
// INTF2/DSI1=3. The CTL field is not a zero-based interface index.
#define CTL_INTF_DSI0  2U
#define WIDTH          1080U
#define HEIGHT         1920U
#define PACKED_SIZE    ((HEIGHT << 16) | WIDTH)
#define REFRESH_PERIOD 333333ULL  // 30 Hz, in 100 ns units

typedef struct { UINT32 Offset; UINT32 Shift; } PIPE;
STATIC CONST PIPE mPipes[] = {
  {0x05000, 0}, {0x07000, 3}, {0x09000, 6},
  {0x15000, 9}, {0x17000, 12}, {0x19000, 15},
  {0x25000, 18}, {0x27000, 21}, {0x0B000, 26}, {0x1B000, 29}
};
STATIC UINTN mCtl;
STATIC UINTN mScanout;
STATIC UINT32 mStride;
STATIC UINT32 mBytesPerPixel;
STATIC UINT8 mChannels[4];
STATIC BOOLEAN mCommandMode;
STATIC BOOLEAN mAutoRefresh;
STATIC BOOLEAN mDirty;
STATIC BOOLEAN mStopped;
STATIC BOOLEAN mPresented;
STATIC BOOLEAN mTimerObserved;
STATIC BOOLEAN mFirstBlt;
STATIC BOOLEAN mFirstBltError;
STATIC UINTN mBusyTicks;
STATIC EFI_EVENT mRefreshEvent;
STATIC EFI_EVENT mExitEvent;
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL *mShadow;
STATIC FRAME_BUFFER_CONFIGURE *mBltConfig;
STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION mInfo;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mMode;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL mGop;
STATIC struct {
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} mPath = {
  {{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof(VENDOR_DEVICE_PATH), 0}},
   {0x49fd2867, 0x226b, 0x4403, {0x87, 0x97, 0x1d, 0xee, 0x69, 0x49, 0x96, 0x01}}},
  {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE,
   {sizeof(EFI_DEVICE_PATH_PROTOCOL), 0}}
};

STATIC BOOLEAN InDisplayReservation(UINT32 Address, UINT64 Size)
{
  ARM_MEMORY_REGION_DESCRIPTOR_EX Region;
  CONST CHAR8 *Names[] = {"UEFI Display Reserved", "Display Reserved"};
  UINTN Index;
  for (Index = 0; Index < ARRAY_SIZE(Names); Index++) {
    if (!EFI_ERROR(LocateMemoryMapAreaByName((CHAR8 *)Names[Index], &Region)) &&
        Address >= Region.Address && (UINT64)Address + Size <= Region.Address + Region.Length)
      return TRUE;
  }
  return FALSE;
}

STATIC EFI_STATUS UnsupportedHandoff(IN CONST CHAR8 *Reason)
{
  DEBUG((EFI_D_ERROR, "FJ DISPLAY reject: %a\n", Reason));
  return EFI_UNSUPPORTED;
}

STATIC EFI_STATUS DiscoverPrimary(VOID)
{
  UINTN Index;
  UINTN PipeIndex;
  UINTN Selected = 0;
  UINT32 Top;
  UINT32 Layer;
  UINT32 Format;
  UINT32 Unpack;
  UINT32 Address;
  UINT32 Seen = 0;
  UINTN Pipe;

  // Read-only until all topology, format and RAM checks have succeeded.
  DEBUG((EFI_D_ERROR, "FJ DISPLAY probe MDSS HW=0x%08x\n", MmioRead32(MDP_BASE)));
  for (Index = 0; Index < 5; Index++) {
    UINTN Ctl = MDP_BASE + 0x2000 + Index * 0x200;
    Top = MmioRead32(Ctl + CTL_TOP);
    DEBUG((EFI_D_ERROR, "FJ CTL%u top=%08x lm0=%08x ext=%08x start=%08x\n",
           (UINT32)Index, Top, MmioRead32(Ctl), MmioRead32(Ctl + 0x40),
           MmioRead32(Ctl + CTL_START)));
    // INTF1/DSI0 has encoding 2; do not select eDP (1), DSI1 (3), or WB.
    if (((Top >> 4) & 0xF) == CTL_INTF_DSI0 && (Top & 0xF) == 0) {
      mCtl = Ctl;
      Selected++;
    }
  }
  if (Selected != 1)
    return UnsupportedHandoff("expected exactly one DSI0 controller");
  Top = MmioRead32(mCtl + CTL_TOP);
  DEBUG((EFI_D_ERROR, "FJ DISPLAY selected DSI0 ctl=%08x intf-code=%u\n",
         (UINT32)mCtl, (Top >> 4) & 0xF));
  // Capture SSPPs before topology validation. These are reads only; no
  // acceptance rule is relaxed if the next test exposes another mismatch.
  for (Index = 0; Index < ARRAY_SIZE(mPipes); Index++) {
    UINTN Base = MDP_BASE + mPipes[Index].Offset;
    if (MmioRead32(Base) != 0) {
      DEBUG((EFI_D_ERROR,
        "FJ SNAP pipe=%08x src=%08x out=%08x addr=%08x stride=%08x fmt=%08x unpack=%08x\n",
        (UINT32)Base, MmioRead32(Base), MmioRead32(Base + 0x0C),
        MmioRead32(Base + 0x14), MmioRead32(Base + 0x24),
        MmioRead32(Base + 0x30), MmioRead32(Base + 0x34)));
    }
  }
  // Only LM0, no 3D pack, split mixer, extra layer or cursor routing.
  if ((Top & BIT19) || MmioRead32(mCtl + 0x40) || MmioRead32(mCtl + 0x70))
    return UnsupportedHandoff("3D/extended layer routing");
  for (Index = 1; Index < 4; Index++)
    if (MmioRead32(mCtl + Index * 4))
      return UnsupportedHandoff("multiple mixers");
  Layer = MmioRead32(mCtl);
  Pipe = 0;
  PipeIndex = 0;
  for (Index = 0; Index < ARRAY_SIZE(mPipes); Index++) {
    UINT32 Stage = (Layer >> mPipes[Index].Shift) & 7;
    if (Stage != 0) {
      Pipe = MDP_BASE + mPipes[Index].Offset;
      PipeIndex = Index;
      Selected = Stage;
      Seen++;
    }
  }
  if (Seen != 1 || Selected != 1 || Layer != (1U << mPipes[PipeIndex].Shift))
    return UnsupportedHandoff("expected one full-screen base-layer pipe");
  // A pipe must not be shared with another controller (including DSI1).
  for (Index = 0; Index < 5; Index++) {
    UINTN Ctl = MDP_BASE + 0x2000 + Index * 0x200;
    UINTN Mixer;
    if (Ctl == mCtl)
      continue;
    for (Mixer = 0; Mixer < 4; Mixer++)
      if ((MmioRead32(Ctl + Mixer * 4) >> mPipes[PipeIndex].Shift) & 7)
        return UnsupportedHandoff("pipe shared with another controller");
  }
  Address = MmioRead32(Pipe + 0x14);
  mStride = MmioRead32(Pipe + 0x24) & 0xFFFF;
  Format = MmioRead32(Pipe + 0x30);
  Unpack = MmioRead32(Pipe + 0x34);
  mBytesPerPixel = ((Format >> 9) & 3) + 1;
  DEBUG((EFI_D_ERROR,
    "FJ PIPE=%08x addr=%08x stride=%u fmt=%08x unpack=%08x src=%08x out=%08x op=%08x\n",
    (UINT32)Pipe, Address, mStride, Format, Unpack,
    MmioRead32(Pipe), MmioRead32(Pipe + 0x0C), MmioRead32(Pipe + 0x38)));
  DEBUG((EFI_D_ERROR, "FJ PIPE srcxy=%08x outxy=%08x secure=%08x decimation=%08x\n",
         MmioRead32(Pipe + 0x08), MmioRead32(Pipe + 0x10),
         MmioRead32(Pipe + 0x70), MmioRead32(Pipe + 0xB4)));
  // Full-size, unscaled, linear, interleaved RGB888/RGBA8888, tightly packed.
  // No UBWC/tile/YUV/rotation/flip/secure/decimation handoff is guessed.
  if (MmioRead32(Pipe) != PACKED_SIZE || MmioRead32(Pipe + 0x0C) != PACKED_SIZE ||
      MmioRead32(Pipe + 0x08) || MmioRead32(Pipe + 0x10) ||
      (MmioRead32(Pipe + 0x38) & ~BIT31) ||
      MmioRead32(Pipe + 0x70) || MmioRead32(Pipe + 0xB4) ||
      (Format & ~(0xFFU | BIT8 | BIT9 | BIT10 | BIT12 | BIT13 | BIT17)) ||
      (Format & 0x3F) != 0x3F || !(Format & BIT17) ||
      (mBytesPerPixel != 3 && mBytesPerPixel != 4) ||
      (((Format >> 12) & 3) + 1) != mBytesPerPixel ||
      mStride < WIDTH * mBytesPerPixel || mStride > 16384 ||
      !InDisplayReservation(Address, (UINT64)mStride * HEIGHT))
    return UnsupportedHandoff("surface geometry/format/address/stride not supported");
  Seen = 0;
  for (Index = 0; Index < mBytesPerPixel; Index++) {
    mChannels[Index] = (Unpack >> (Index * 8)) & 0xFF;
    if (mChannels[Index] > 3 || (Seen & (1U << mChannels[Index])))
      return UnsupportedHandoff("invalid channel unpack pattern");
    Seen |= 1U << mChannels[Index];
  }
  if ((Seen & 7) != 7)
    return UnsupportedHandoff("missing RGB channels");
  mScanout = Address;
  mCommandMode = (Top & CMD_MODE) != 0;
  mAutoRefresh = (MmioRead32(MDP_BASE + 0x71030) & BIT31) != 0;
  DEBUG((EFI_D_ERROR, "FJ DISPLAY validated single DSI0 %ux%u %u-bpp cmd=%u auto=%u\n",
         WIDTH, HEIGHT, mBytesPerPixel * 8, mCommandMode, mAutoRefresh));
  return EFI_SUCCESS;
}

STATIC VOID EFIAPI Refresh(IN EFI_EVENT Event, IN VOID *Context)
{
  if (!mTimerObserved) {
    mTimerObserved = TRUE;
    DEBUG((EFI_D_ERROR, "FJ DISPLAY first timer callback dirty=%u stopped=%u\n",
           mDirty, mStopped));
  }
  if (mStopped || !mDirty)
    return;
  if (mCommandMode && !mAutoRefresh) {
    if (MmioRead32(mCtl + CTL_START) & BIT0) {
      ++mBusyTicks;
      if (mBusyTicks == 1 || mBusyTicks == 120)
        DEBUG((EFI_D_ERROR, "FJ DISPLAY CTL_START busy, tick=%u; no register reset attempted\n", (UINT32)mBusyTicks));
      return;
    }
    ArmDataSynchronizationBarrier();
    // Memory contents changed, not pipe registers: no CTL_FLUSH needed.
    MmioWrite32(mCtl + CTL_START, 1);
  }
  mDirty = FALSE;
  mBusyTicks = 0;
  if (!mPresented) {
    mPresented = TRUE;
    DEBUG((EFI_D_ERROR, "FJ DISPLAY first present submitted (not proof of panel visibility)\n"));
  }
}

STATIC VOID EFIAPI Stop(IN EFI_EVENT Event, IN VOID *Context)
{
  // No Boot Services calls in ExitBootServices. Leave hardware as inherited.
  mStopped = TRUE;
}

STATIC EFI_STATUS EFIAPI Blt(
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Buffer OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION Operation, IN UINTN SourceX, IN UINTN SourceY,
  IN UINTN DestX, IN UINTN DestY, IN UINTN Width, IN UINTN Height, IN UINTN Delta OPTIONAL)
{
  RETURN_STATUS Status;
  EFI_TPL Tpl;
  UINTN X, Y, Byte;
  if (mStopped)
    return EFI_NOT_READY;
  Tpl = gBS->RaiseTPL(TPL_NOTIFY);
  Status = FrameBufferBlt(mBltConfig, Buffer, Operation, SourceX, SourceY,
                         DestX, DestY, Width, Height, Delta);
  if (!RETURN_ERROR(Status) && Operation != EfiBltVideoToBltBuffer) {
    for (Y = DestY; Y < DestY + Height; Y++) {
      UINT8 *Dest = (UINT8 *)mScanout + Y * mStride + DestX * mBytesPerPixel;
      for (X = DestX; X < DestX + Width; X++) {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Pixel = &mShadow[Y * WIDTH + X];
        UINT8 Channels[4] = {Pixel->Green, Pixel->Blue, Pixel->Red, 0xFF};
        for (Byte = 0; Byte < mBytesPerPixel; Byte++)
          *Dest++ = Channels[mChannels[Byte]];
      }
    }
    WriteBackDataCacheRange((VOID *)(mScanout + DestY * mStride), Height * mStride);
    mDirty = TRUE;
    if (!mFirstBlt) {
      mFirstBlt = TRUE;
      DEBUG((EFI_D_ERROR,
        "FJ DISPLAY first BLT copied: op=%u dst=%u,%u %ux%u scanout=%08x stride=%u timer-seen=%u\n",
        (UINT32)Operation, (UINT32)DestX, (UINT32)DestY,
        (UINT32)Width, (UINT32)Height, (UINT32)mScanout, mStride, mTimerObserved));
    }
  } else if (RETURN_ERROR(Status) && !mFirstBltError) {
    mFirstBltError = TRUE;
    DEBUG((EFI_D_ERROR, "FJ DISPLAY BLT failed: %r op=%u dst=%u,%u %ux%u\n",
           Status, (UINT32)Operation, (UINT32)DestX, (UINT32)DestY,
           (UINT32)Width, (UINT32)Height));
  }
  gBS->RestoreTPL(Tpl);
  return RETURN_ERROR(Status) ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI QueryMode(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  IN UINT32 Mode, OUT UINTN *Size, OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info)
{
  if (Size == NULL || Info == NULL)
    return EFI_INVALID_PARAMETER;
  if (Mode != 0)
    return EFI_UNSUPPORTED;
  *Info = AllocateCopyPool(sizeof(mInfo), &mInfo);
  if (*Info == NULL)
    return EFI_OUT_OF_RESOURCES;
  *Size = sizeof(mInfo);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI SetMode(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 Mode)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black = {0, 0, 0, 0};
  if (Mode != 0)
    return EFI_UNSUPPORTED;
  return Blt(This, &Black, EfiBltVideoFill, 0, 0, 0, 0, WIDTH, HEIGHT, 0);
}

EFI_STATUS EFIAPI FujisanDisplayInitialize(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_HANDLE Handle = NULL;
  UINTN ConfigSize = 0;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION ShadowInfo;
  DEBUG((EFI_D_ERROR, "FJ DISPLAY DXE entry (20260908-E, timer INTID fix)\n"));
  Status = DiscoverPrimary();
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "FJ DISPLAY unsupported handoff: %r; no scanout/register writes\n", Status));
    return Status;
  }
  mShadow = AllocateZeroPool(WIDTH * HEIGHT * sizeof(*mShadow));
  if (mShadow == NULL)
    return EFI_OUT_OF_RESOURCES;
  ZeroMem(&ShadowInfo, sizeof(ShadowInfo));
  ShadowInfo.HorizontalResolution = WIDTH;
  ShadowInfo.VerticalResolution = HEIGHT;
  ShadowInfo.PixelsPerScanLine = WIDTH;
  ShadowInfo.PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  Status = FrameBufferBltConfigure(mShadow, &ShadowInfo, NULL, &ConfigSize);
  if (Status != RETURN_BUFFER_TOO_SMALL)
    goto Fail;
  mBltConfig = AllocatePool(ConfigSize);
  if (mBltConfig == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Fail;
  }
  Status = FrameBufferBltConfigure(mShadow, &ShadowInfo, mBltConfig, &ConfigSize);
  if (EFI_ERROR(Status))
    goto Fail;
  mInfo = ShadowInfo;
  // Command-mode output must go through Blt; no fake directly-mapped GOP.
  mInfo.PixelFormat = PixelBltOnly;
  mMode.MaxMode = 1;
  mMode.Info = &mInfo;
  mMode.SizeOfInfo = sizeof(mInfo);
  mGop.QueryMode = QueryMode;
  mGop.SetMode = SetMode;
  mGop.Blt = Blt;
  mGop.Mode = &mMode;
  Status = gBS->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Refresh, NULL, &mRefreshEvent);
  if (EFI_ERROR(Status))
    goto Fail;
  Status = gBS->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, Stop, NULL,
                            &gEfiEventExitBootServicesGuid, &mExitEvent);
  if (EFI_ERROR(Status))
    goto Fail;
  Status = gBS->SetTimer(mRefreshEvent, TimerPeriodic, REFRESH_PERIOD);
  if (EFI_ERROR(Status))
    goto Fail;
  Status = gBS->InstallMultipleProtocolInterfaces(&Handle,
    &gEfiDevicePathProtocolGuid, &mPath, &gEfiGraphicsOutputProtocolGuid, &mGop, NULL);
  if (EFI_ERROR(Status))
    goto Fail;
  DEBUG((EFI_D_ERROR, "FJ GOP installed (Blt-only); waiting for UI draw\n"));
  return EFI_SUCCESS;
Fail:
  DEBUG((EFI_D_ERROR, "FJ DISPLAY setup failed: %r\n", Status));
  if (mRefreshEvent != NULL) gBS->CloseEvent(mRefreshEvent);
  if (mExitEvent != NULL) gBS->CloseEvent(mExitEvent);
  if (mBltConfig != NULL) FreePool(mBltConfig);
  FreePool(mShadow);
  return Status;
}
