/** @file
  Fujisan PM8994 keys, polled through the MSM8996 SPMI v2 observer window.
  Only SPMI read commands are issued: no PMIC configuration/reset writes.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Library/ArmLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleTextIn.h>

#define ARB_CORE        0x0400F000U
#define ARB_OBSERVER    0x04C00000U
#define ARB_MAP         0x800U
#define ARB_CHANNELS    256U  // (DT core_size 0xC00 - map_offset 0x800) / 4
#define ARB_STRIDE      0x8000U
#define ARB_STATUS      0x08U
#define ARB_RDATA       0x18U
#define EXT_READL       (1U << 27)
#define READ_TIMEOUT_US 100U
#define POLL_PERIOD     100000ULL  // 10 ms in 100 ns units
#define DEBOUNCE_SAMPLES 3U
#define QUEUE_SIZE      16U

typedef struct {
  UINT16 Address;
  UINT16 ScanCode;
  CHAR16 UnicodeChar;
  BOOLEAN ActiveLow;
  UINT8 EnableMask;
  UINTN Channel;
  BOOLEAN Candidate;
  BOOLEAN Stable;
  BOOLEAN Armed;
  UINTN Samples;
} BUTTON;

// Follow linux,code, NOT the swapped vol_up/vol_down node labels:
// GPIO2 -> KEY_VOLUMEDOWN(114); GPIO3 -> KEY_VOLUMEUP(115).
// PON KPDPWR status bit0 is asserted on press (not active-low GPIO).
STATIC BUTTON mButtons[] = {
  {0xC108, SCAN_DOWN, CHAR_NULL, TRUE, 0, 0, FALSE, FALSE, FALSE, 0},
  {0xC208, SCAN_UP, CHAR_NULL, TRUE, 0, 0, FALSE, FALSE, FALSE, 0},
  {0x0810, SCAN_NULL, CHAR_CARRIAGE_RETURN, FALSE, 0, 0, FALSE, FALSE, FALSE, 0}
};
STATIC EFI_INPUT_KEY mQueue[QUEUE_SIZE];
STATIC UINTN mRead;
STATIC UINTN mCount;
STATIC BOOLEAN mStopped;
STATIC UINTN mLoggedKeys;
STATIC EFI_EVENT mPollEvent;
STATIC EFI_EVENT mExitEvent;
STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL mInput;
STATIC struct {
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} mPath = {
  {{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof(VENDOR_DEVICE_PATH), 0}},
   {0x7a52b4da, 0xee0d, 0x4f7a, {0x9b, 0x9e, 0x01, 0x8f, 0x37, 0x29, 0x26, 0x01}}},
  {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE,
   {sizeof(EFI_DEVICE_PATH_PROTOCOL), 0}}
};

STATIC EFI_STATUS FindChannel(IN UINT16 Address, OUT UINTN *Channel)
{
  UINTN Apid;
  // SID0 is fixed by the running DT; PPID = (SID << 8) | (addr >> 8).
  for (Apid = 0; Apid < ARB_CHANNELS; Apid++) {
    UINT32 Map = MmioRead32(ARB_CORE + ARB_MAP + 4 * Apid);
    if (Map != 0 && ((Map >> 8) & 0xFFF) == (Address >> 8)) {
      *Channel = ARB_OBSERVER + ARB_STRIDE * Apid;  // EE0, from DT
      DEBUG((EFI_D_ERROR, "FJ KEYS SID0 addr=%04x APID=%u observer=%08x\n",
             Address, (UINT32)Apid, (UINT32)*Channel));
      return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}

STATIC EFI_STATUS ReadByte(IN BUTTON *Button, IN UINT8 Offset, OUT UINT8 *Value)
{
  UINTN Timeout;
  UINT32 Status = 0;
  BOOLEAN Interrupts = ArmGetInterruptState();
  EFI_STATUS Result = EFI_TIMEOUT;
  // Same command/status sequence as spmi-pmic-arb.c v2. Write ONLY an
  // EXT_READL descriptor to the observer CMD register; never PMIC data.
  ArmDisableInterrupts();
  MmioWrite32(Button->Channel, EXT_READL | ((UINT32)Offset << 4));
  for (Timeout = 0; Timeout < READ_TIMEOUT_US; Timeout++) {
    Status = MmioRead32(Button->Channel + ARB_STATUS);
    if (Status & BIT0) {
      if (Status & (BIT1 | BIT2 | BIT3)) {
        Result = (Status & BIT2) ? EFI_ACCESS_DENIED : EFI_DEVICE_ERROR;
      } else {
        *Value = (UINT8)MmioRead32(Button->Channel + ARB_RDATA);
        Result = EFI_SUCCESS;
      }
      break;
    }
    MicroSecondDelay(1);
  }
  if (Interrupts)
    ArmEnableInterrupts();
  return Result;
}

STATIC EFI_STATUS ReadPressed(IN BUTTON *Button, OUT BOOLEAN *Pressed)
{
  UINT8 Value;
  EFI_STATUS Status = ReadByte(Button, (UINT8)Button->Address, &Value);
  if (EFI_ERROR(Status))
    return Status;
  if (Button->EnableMask != 0 && !(Value & Button->EnableMask))
    return EFI_NOT_READY;
  *Pressed = Button->ActiveLow ? !(Value & BIT0) : !!(Value & BIT0);
  return EFI_SUCCESS;
}

STATIC VOID Enqueue(IN BUTTON *Button)
{
  UINTN Slot;
  if (mCount == QUEUE_SIZE)
    return;  // Never turn an overflow into a phantom confirmation.
  Slot = (mRead + mCount) % QUEUE_SIZE;
  mQueue[Slot].ScanCode = Button->ScanCode;
  mQueue[Slot].UnicodeChar = Button->UnicodeChar;
  mCount++;
  if (mLoggedKeys++ < 32)
    DEBUG((EFI_D_ERROR, "FJ KEYS press addr=%04x scan=%u unicode=%u\n",
           Button->Address, Button->ScanCode, Button->UnicodeChar));
  gBS->SignalEvent(mInput.WaitForKey);
}

STATIC VOID EFIAPI Poll(IN EFI_EVENT Event, IN VOID *Context)
{
  BOOLEAN Pressed[ARRAY_SIZE(mButtons)];
  EFI_STATUS Status;
  UINTN Index;
  UINTN PressedCount = 0;
  EFI_TPL OldTpl;
  if (mStopped)
    return;
  OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  // Read the complete sample before changing any debouncer/queue state.
  for (Index = 0; Index < ARRAY_SIZE(mButtons); Index++) {
    Status = ReadPressed(&mButtons[Index], &Pressed[Index]);
    if (EFI_ERROR(Status)) {
      mStopped = TRUE;
      mCount = 0;
      DEBUG((EFI_D_ERROR, "FJ KEYS stopped: addr=%04x read=%r; no synthetic key\n",
             mButtons[Index].Address, Status));
      gBS->RestoreTPL(OldTpl);
      return;
    }
  }
  for (Index = 0; Index < ARRAY_SIZE(mButtons); Index++)
    if (Pressed[Index]) PressedCount++;
  if (PressedCount > 1) {
    // Chords are not menu commands. Require release rather than accidentally
    // navigating and confirming two queued events from one simultaneous press.
    mCount = 0;
    for (Index = 0; Index < ARRAY_SIZE(mButtons); Index++) {
      mButtons[Index].Armed = FALSE;
      mButtons[Index].Stable = mButtons[Index].Candidate = Pressed[Index];
      mButtons[Index].Samples = 0;
    }
    gBS->RestoreTPL(OldTpl);
    return;
  }
  for (Index = 0; Index < ARRAY_SIZE(mButtons); Index++) {
    BUTTON *Button = &mButtons[Index];
    if (Pressed[Index] != Button->Candidate) {
      Button->Candidate = Pressed[Index];
      Button->Samples = 1;
    } else if (Button->Samples < DEBOUNCE_SAMPLES) {
      Button->Samples++;
    }
    if (Button->Samples == DEBOUNCE_SAMPLES) {
      if (!Button->Candidate) {
        Button->Armed = TRUE;
      } else if (!Button->Stable && Button->Armed) {
        Enqueue(Button);
        Button->Armed = FALSE;
      }
      Button->Stable = Button->Candidate;
    }
  }
  gBS->RestoreTPL(OldTpl);
}

STATIC VOID EFIAPI WaitForKey(IN EFI_EVENT Event, IN VOID *Context)
{
  if (!mStopped && mCount != 0)
    gBS->SignalEvent(Event);
}

STATIC EFI_STATUS EFIAPI Reset(IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, IN BOOLEAN ExtendedVerification)
{
  EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  UINTN Index;
  mRead = mCount = 0;
  // Wait for a debounced release before accepting a held key after reset.
  for (Index = 0; Index < ARRAY_SIZE(mButtons); Index++) {
    mButtons[Index].Armed = FALSE;
    mButtons[Index].Samples = 0;
  }
  gBS->RestoreTPL(OldTpl);
  return mStopped ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI ReadKey(IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, OUT EFI_INPUT_KEY *Key)
{
  EFI_TPL OldTpl;
  EFI_STATUS Status = EFI_NOT_READY;
  if (Key == NULL)
    return EFI_INVALID_PARAMETER;
  OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  if (mStopped) {
    Status = EFI_DEVICE_ERROR;
  } else if (mCount != 0) {
    *Key = mQueue[mRead];
    mRead = (mRead + 1) % QUEUE_SIZE;
    mCount--;
    Status = EFI_SUCCESS;
  }
  gBS->RestoreTPL(OldTpl);
  return Status;
}

STATIC VOID EFIAPI Stop(IN EFI_EVENT Event, IN VOID *Context)
{
  mStopped = TRUE;
}

EFI_STATUS EFIAPI FujisanKeysInitialize(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_HANDLE Handle = NULL;
  UINTN Index;
  UINT32 Version = MmioRead32(ARB_CORE);
  UINT8 Type, Revision, Mode;
  DEBUG((EFI_D_ERROR, "FJ KEYS 20260909-K1 arbiter=%08x\n", Version));
  // The connected device reports v2=0x20010000. Never guess v1/v3/v5 layouts.
  if (Version != 0x20010000)
    return EFI_UNSUPPORTED;
  for (Index = 0; Index < ARRAY_SIZE(mButtons); Index++) {
    BUTTON *Button = &mButtons[Index];
    Status = FindChannel(Button->Address, &Button->Channel);
    if (EFI_ERROR(Status))
      goto Fail;
    if (Button->ActiveLow) {
      Status = ReadByte(Button, 0x04, &Type);
      if (EFI_ERROR(Status)) goto Fail;
      Status = ReadByte(Button, 0x01, &Revision);
      if (EFI_ERROR(Status)) goto Fail;
      Status = ReadByte(Button, 0x40, &Mode);
      if (EFI_ERROR(Status)) goto Fail;
      DEBUG((EFI_D_ERROR, "FJ KEYS GPIO addr=%04x type=%02x rev=%u mode=%02x\n",
             Button->Address, Type, Revision, Mode));
      if (Type != 0x10 || (Mode & 0x70) != 0) {
        Status = EFI_UNSUPPORTED;
        goto Fail;
      }
      Button->EnableMask = (Revision == 0) ? BIT1 : BIT7;
    }
    Status = ReadPressed(Button, &Button->Stable);
    if (EFI_ERROR(Status)) goto Fail;
    Button->Candidate = Button->Stable;
    Button->Armed = FALSE; // Even idle keys must first settle for 3 samples.
    DEBUG((EFI_D_ERROR, "FJ KEYS initial addr=%04x pressed=%u\n",
           Button->Address, Button->Stable));
  }
  mInput.Reset = Reset;
  mInput.ReadKeyStroke = ReadKey;
  Status = gBS->CreateEvent(EVT_NOTIFY_WAIT, TPL_NOTIFY, WaitForKey, NULL, &mInput.WaitForKey);
  if (EFI_ERROR(Status)) goto Fail;
  Status = gBS->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Poll, NULL, &mPollEvent);
  if (EFI_ERROR(Status)) goto Fail;
  Status = gBS->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, Stop, NULL,
                            &gEfiEventExitBootServicesGuid, &mExitEvent);
  if (EFI_ERROR(Status)) goto Fail;
  Status = gBS->SetTimer(mPollEvent, TimerPeriodic, POLL_PERIOD);
  if (EFI_ERROR(Status)) goto Fail;
  Status = gBS->InstallMultipleProtocolInterfaces(&Handle,
    &gEfiDevicePathProtocolGuid, &mPath, &gEfiSimpleTextInProtocolGuid, &mInput, NULL);
  if (EFI_ERROR(Status)) goto Fail;
  DEBUG((EFI_D_ERROR, "FJ KEYS SimpleTextIn installed; volume navigation / power Enter\n"));
  return EFI_SUCCESS;
Fail:
  DEBUG((EFI_D_ERROR, "FJ KEYS init failed: %r; PMIC configuration left unchanged\n", Status));
  mStopped = TRUE;
  if (mPollEvent != NULL) gBS->CloseEvent(mPollEvent);
  if (mExitEvent != NULL) gBS->CloseEvent(mExitEvent);
  if (mInput.WaitForKey != NULL) gBS->CloseEvent(mInput.WaitForKey);
  return Status;
}
