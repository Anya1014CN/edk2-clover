/** @file
  Primary-panel Synaptics RMI4 touch for Fujisan, over MSM8996 QUP I2C v2.

  The controller is inherited as configured by LK. This driver only resets and
  configures the QUP transport, then uses standard I2C write/read transactions
  to read RMI registers. It does not reset/power/configure the touch IC, TLMM
  pins, PMIC rails, controller interrupt, or the second panel.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/DevicePath.h>

#define GCC_BASE               0x00300000U
#define GCC_AHB_VOTE           0x052004U
#define GCC_AHB_BLSP2_BIT      BIT15
#define GCC_QUP6_I2C_CMD       0x030020U
#define GCC_QUP6_I2C_CFG       0x030024U
#define GCC_QUP6_I2C_CBCR      0x030008U
#define GCC_BRANCH_ENABLE      BIT0
#define GCC_ROOT_UPDATE        BIT0
#define GCC_ROOT_ENABLE        BIT1
#define GCC_ROOT_STATUS        BIT31
#define TLMM_BASE               0x01010000U
#define TLMM_GPIO_CTL(gpio)     (TLMM_BASE + ((UINT32)(gpio) * 0x1000U))
#define TLMM_GPIO_MUX_MASK      (0x7U << 2)
#define TLMM_GPIO_PULL_MASK     0x3U
#define TLMM_GPIO_DRV_MASK      (0x7U << 6)
#define TLMM_GPIO_OE            BIT9
#define TLMM_I2C12_FUNCTION     3U
#define TLMM_I2C12_SDA          87U
#define TLMM_I2C12_SCL          88U
#define QUP_BASE               0x075BA000U
#define QUP_CONFIG             0x000U
#define QUP_STATE              0x004U
#define QUP_IO_MODE            0x008U
#define QUP_SW_RESET           0x00CU
#define QUP_OPERATIONAL        0x018U
#define QUP_ERROR_FLAGS        0x01CU
#define QUP_ERROR_FLAGS_EN     0x020U
#define QUP_OPERATIONAL_MASK   0x028U
#define QUP_HW_VERSION         0x030U
#define QUP_MX_WRITE_CNT       0x150U
#define QUP_MX_READ_CNT        0x208U
#define QUP_OUT_FIFO_BASE      0x110U
#define QUP_IN_FIFO_BASE       0x218U
#define QUP_I2C_CLK_CTL        0x400U
#define QUP_I2C_STATUS         0x404U
#define QUP_I2C_MASTER_CONFIG  0x408U

#define QUP_RESET_STATE        0U
#define QUP_RUN_STATE          1U
#define QUP_PAUSE_STATE        3U
#define QUP_STATE_MASK         3U
#define QUP_STATE_VALID        BIT2
#define QUP_I2C_MASTER_GEN     BIT4
#define QUP_NO_OUTPUT          BIT6
#define QUP_NO_INPUT           BIT7
#define QUP_IN_NOT_EMPTY       BIT5
#define QUP_OUT_SVC_FLAG       BIT8
#define QUP_IN_SVC_FLAG        BIT9
#define QUP_MAX_OUTPUT_DONE    BIT10
#define QUP_MAX_INPUT_DONE     BIT11
#define QUP_I2C_BUS_ACTIVE     BIT8
#define QUP_ERROR_MASK          0x3CU
#define QUP_I2C_ERROR_MASK      0x0380003CU
#define QUP_REPACK_EN           (BIT14 | BIT15)
#define I2C_MINI_CORE           0x200U
#define I2C_N_VAL               7U
#define QUP_TAG2_START          0x81U
#define QUP_TAG2_DATA_WRITE     0x82U
#define QUP_TAG2_DATA_WRITE_END 0x83U
#define QUP_TAG2_DATA_READ_END  0x87U
#define QUP_TAG2_INPUT_EOT_SIZE 2U
#define QUP_CLK_CTL_400KHZ      0x00150015U // DT high/fs div: 0x15 / 0x15
#define QUP_TIMEOUT_US          10000U

#define RMI_I2C_ADDRESS         0x20U
#define RMI_PAGE_SELECT          0xFFU
#define RMI_PDT_START            0xE9U
#define RMI_PDT_ENTRY_SIZE       6U
#define RMI_FUNCTION_F12         0x12U
#define RMI_MAX_PAGES            4U
#define RMI_MAX_PDT_ENTRIES      40U
#define TOUCH_BYTES              8U
#define TOUCH_POLL_100NS         100000ULL // 10 ms; RMI controller already debounces
#define TOUCH_FAILURE_LIMIT      5U
#define SCREEN_X                 1080U
#define SCREEN_Y                 1920U

typedef struct {
  EFI_ABSOLUTE_POINTER_PROTOCOL Protocol;
  EFI_ABSOLUTE_POINTER_MODE Mode;
  EFI_EVENT PollEvent;
  EFI_EVENT ExitEvent;
  EFI_EVENT WaitEvent;
  EFI_ABSOLUTE_POINTER_STATE State;
  BOOLEAN StateChanged;
  BOOLEAN Pressed;
  UINTN FailureCount;
  UINTN LoggedEvents;
  UINT8 CurrentPage;
  UINT8 F12Page;
  UINT8 F12Data;
  BOOLEAN Stopped;
} FUJISAN_TOUCH;

STATIC FUJISAN_TOUCH mTouch;
STATIC struct {
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} mPath = {
  {{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof(VENDOR_DEVICE_PATH), 0}},
   {0x3104b71d, 0x1dbe, 0x4e93, {0x80, 0xfe, 0xd1, 0x17, 0x20, 0x51, 0x35, 0x02}}},
  {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE,
   {sizeof(EFI_DEVICE_PATH_PROTOCOL), 0}}
};

STATIC EFI_STATUS ConfigureI2c12Pins(VOID)
{
  UINTN Index;
  CONST UINT32 Pins[] = {TLMM_I2C12_SDA, TLMM_I2C12_SCL};
  for (Index = 0; Index < ARRAY_SIZE(Pins); Index++) {
    UINT32 Address = TLMM_GPIO_CTL(Pins[Index]);
    UINT32 Before = MmioRead32(Address);
    UINT32 Value = Before;
    // msm8996-pinctrl.dtsi i2c_12_active: mux=blsp_i2c12 (index 3),
    // drive-strength=2mA (encoding 0), bias-disable (encoding 0).
    Value &= ~(TLMM_GPIO_MUX_MASK | TLMM_GPIO_PULL_MASK |
               TLMM_GPIO_DRV_MASK | TLMM_GPIO_OE);
    Value |= TLMM_I2C12_FUNCTION << 2;
    MmioWrite32(Address, Value);
    DEBUG((EFI_D_ERROR, "FJ TOUCH TLMM gpio=%u before=%08x after=%08x\n",
           Pins[Index], Before, MmioRead32(Address)));
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EnableQupClocks(VOID)
{
  UINT32 Value;
  UINTN Index;
  // MSM8996 GCC clock-local2 sequence for BLSP2/QUP6 I2C:
  // vote BLSP2 AHB, select CXO 19.2 MHz, update/force the RCG root,
  // then enable the QUP6 I2C branch. No reset/BCR write is issued.
  Value = MmioRead32(GCC_BASE + GCC_AHB_VOTE) | GCC_AHB_BLSP2_BIT;
  MmioWrite32(GCC_BASE + GCC_AHB_VOTE, Value);
  MmioWrite32(GCC_BASE + GCC_QUP6_I2C_CFG, 0); // CXO, divider 1
  Value = MmioRead32(GCC_BASE + GCC_QUP6_I2C_CMD) | GCC_ROOT_UPDATE;
  MmioWrite32(GCC_BASE + GCC_QUP6_I2C_CMD, Value);
  for (Index = 0; Index < 100; Index++) {
    if (!(MmioRead32(GCC_BASE + GCC_QUP6_I2C_CMD) & GCC_ROOT_UPDATE))
      break;
    MicroSecondDelay(1);
  }
  if (Index == 100) {
    DEBUG((EFI_D_ERROR, "FJ TOUCH GCC RCG update timeout cmd=%08x cfg=%08x\n",
           MmioRead32(GCC_BASE + GCC_QUP6_I2C_CMD), MmioRead32(GCC_BASE + GCC_QUP6_I2C_CFG)));
    return EFI_TIMEOUT;
  }
  Value = MmioRead32(GCC_BASE + GCC_QUP6_I2C_CMD) | GCC_ROOT_ENABLE;
  MmioWrite32(GCC_BASE + GCC_QUP6_I2C_CMD, Value);
  MmioWrite32(GCC_BASE + GCC_QUP6_I2C_CBCR,
              MmioRead32(GCC_BASE + GCC_QUP6_I2C_CBCR) | GCC_BRANCH_ENABLE);
  for (Index = 0; Index < 100; Index++) {
    UINT32 Cbcr = MmioRead32(GCC_BASE + GCC_QUP6_I2C_CBCR);
    if (!(Cbcr & GCC_ROOT_STATUS)) {
      DEBUG((EFI_D_ERROR, "FJ TOUCH GCC clocks enabled ahb=%08x cmd=%08x cfg=%08x cbcr=%08x\n",
             MmioRead32(GCC_BASE + GCC_AHB_VOTE), MmioRead32(GCC_BASE + GCC_QUP6_I2C_CMD),
             MmioRead32(GCC_BASE + GCC_QUP6_I2C_CFG), Cbcr));
      return EFI_SUCCESS;
    }
    MicroSecondDelay(1);
  }
  DEBUG((EFI_D_ERROR, "FJ TOUCH GCC branch timeout ahb=%08x cmd=%08x cfg=%08x cbcr=%08x\n",
         MmioRead32(GCC_BASE + GCC_AHB_VOTE), MmioRead32(GCC_BASE + GCC_QUP6_I2C_CMD),
         MmioRead32(GCC_BASE + GCC_QUP6_I2C_CFG), MmioRead32(GCC_BASE + GCC_QUP6_I2C_CBCR)));
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS QupWaitState(IN UINT32 State)
{
  UINTN Index;
  for (Index = 0; Index < 100; Index++) {
    UINT32 Value = MmioRead32(QUP_BASE + QUP_STATE);
    if ((Value & QUP_STATE_VALID) && (Value & QUP_STATE_MASK) == State)
      return EFI_SUCCESS;
    MicroSecondDelay(1);
  }
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS QupSetState(IN UINT32 State)
{
  EFI_STATUS Status;
  Status = QupWaitState(MmioRead32(QUP_BASE + QUP_STATE) & QUP_STATE_MASK);
  if (EFI_ERROR(Status))
    return Status;
  MmioWrite32(QUP_BASE + QUP_STATE, State);
  return QupWaitState(State);
}

STATIC EFI_STATUS QupPrepare(IN UINTN TxCount, IN UINTN RxCount)
{
  EFI_STATUS Status;
  UINT32 Config;
  if (TxCount == 0 || TxCount > 32 || RxCount > 34)
    return EFI_INVALID_PARAMETER;
  MmioWrite32(QUP_BASE + QUP_SW_RESET, 1);
  Status = QupWaitState(QUP_RESET_STATE);
  if (EFI_ERROR(Status))
    return Status;
  // This controller is qcom,i2c-msm-v2, not the older QUP tag format.
  Config = I2C_MINI_CORE | I2C_N_VAL;
  if (RxCount == 0) Config |= QUP_NO_INPUT;
  MmioWrite32(QUP_BASE + QUP_CONFIG, Config);
  MmioWrite32(QUP_BASE + QUP_IO_MODE, QUP_REPACK_EN);
  MmioWrite32(QUP_BASE + QUP_MX_WRITE_CNT, (UINT32)TxCount);
  MmioWrite32(QUP_BASE + QUP_MX_READ_CNT, (UINT32)RxCount);
  MmioWrite32(QUP_BASE + QUP_ERROR_FLAGS_EN, QUP_ERROR_MASK);
  MmioWrite32(QUP_BASE + QUP_ERROR_FLAGS, QUP_ERROR_MASK);
  MmioWrite32(QUP_BASE + QUP_OPERATIONAL_MASK, 0);
  MmioWrite32(QUP_BASE + QUP_I2C_MASTER_CONFIG, 1); // QUP v2 tags
  Status = QupSetState(QUP_RUN_STATE);
  if (EFI_ERROR(Status)) return Status;
  MmioWrite32(QUP_BASE + QUP_I2C_CLK_CTL, QUP_CLK_CTL_400KHZ);
  return QupSetState(QUP_PAUSE_STATE);
}

STATIC EFI_STATUS QupCheckErrors(VOID)
{
  UINT32 Error = MmioRead32(QUP_BASE + QUP_ERROR_FLAGS) & QUP_ERROR_MASK;
  UINT32 I2c = MmioRead32(QUP_BASE + QUP_I2C_STATUS) & QUP_I2C_ERROR_MASK;
  if (Error != 0) {
    MmioWrite32(QUP_BASE + QUP_ERROR_FLAGS, Error);
    return EFI_DEVICE_ERROR;
  }
  if (I2c != 0) {
    MmioWrite32(QUP_BASE + QUP_I2C_STATUS, I2c);
    return (I2c & BIT3) ? EFI_NO_RESPONSE : EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}

STATIC VOID QupAcknowledgeService(VOID)
{
  UINT32 Operational = MmioRead32(QUP_BASE + QUP_OPERATIONAL);
  UINT32 Clear = Operational & (QUP_IN_SVC_FLAG | QUP_OUT_SVC_FLAG);
  if (Clear != 0)
    MmioWrite32(QUP_BASE + QUP_OPERATIONAL, Clear);
}

STATIC VOID QupWriteBytes(IN CONST UINT8 *Bytes, IN UINTN Length)
{
  UINTN Index;
  UINT32 Word = 0;
  for (Index = 0; Index < Length; Index++) {
    Word |= (UINT32)Bytes[Index] << ((Index & 3) * 8);
    if ((Index & 3) == 3 || Index + 1 == Length) {
      MmioWrite32(QUP_BASE + QUP_OUT_FIFO_BASE, Word);
      Word = 0;
    }
  }
}

STATIC VOID QupLogFailure(IN CONST CHAR8 *Stage, IN EFI_STATUS Status)
{
  DEBUG((EFI_D_ERROR,
    "FJ TOUCH QUP %a=%r state=%08x cfg=%08x op=%08x i2c=%08x err=%08x mcfg=%08x\n",
    Stage, Status, MmioRead32(QUP_BASE + QUP_STATE),
    MmioRead32(QUP_BASE + QUP_CONFIG), MmioRead32(QUP_BASE + QUP_OPERATIONAL),
    MmioRead32(QUP_BASE + QUP_I2C_STATUS), MmioRead32(QUP_BASE + QUP_ERROR_FLAGS),
    MmioRead32(QUP_BASE + QUP_I2C_MASTER_CONFIG)));
}

STATIC EFI_STATUS QupCompleteWrite(VOID)
{
  UINTN Index;
  for (Index = 0; Index < QUP_TIMEOUT_US; Index++) {
    EFI_STATUS Status = QupCheckErrors();
    UINT32 Operational;
    UINT32 I2c;
    if (EFI_ERROR(Status)) return Status;
    Operational = MmioRead32(QUP_BASE + QUP_OPERATIONAL);
    I2c = MmioRead32(QUP_BASE + QUP_I2C_STATUS);
    QupAcknowledgeService();
    if ((Operational & (QUP_OUT_SVC_FLAG | QUP_MAX_OUTPUT_DONE)) &&
        !(I2c & QUP_I2C_BUS_ACTIVE))
      return EFI_SUCCESS;
    MicroSecondDelay(1);
  }
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS QupWrite(IN CONST UINT8 *Data, IN UINTN Length)
{
  // v2 FIFO sequence: START, address, WRITE_N_STOP, length, payload.
  UINT8 Bytes[12];
  EFI_STATUS Status;
  if (Data == NULL || Length == 0 || Length > 8) return EFI_INVALID_PARAMETER;
  Bytes[0] = QUP_TAG2_START;
  Bytes[1] = RMI_I2C_ADDRESS << 1;
  Bytes[2] = QUP_TAG2_DATA_WRITE_END;
  Bytes[3] = (UINT8)Length;
  CopyMem(&Bytes[4], Data, Length);
  Status = QupPrepare(4 + Length, 0);
  if (EFI_ERROR(Status)) { QupLogFailure("prepare-write", Status); return Status; }
  QupWriteBytes(Bytes, 4 + Length);
  Status = QupSetState(QUP_RUN_STATE);
  if (EFI_ERROR(Status)) { QupLogFailure("run-write", Status); return Status; }
  Status = QupCompleteWrite();
  if (EFI_ERROR(Status)) QupLogFailure("write", Status);
  return Status;
}

STATIC EFI_STATUS QupWriteRead(IN UINT8 Register, OUT UINT8 *Data, IN UINTN Length)
{
  // Combined v2 transfer: write RMI register then repeated-start final read.
  UINT8 Bytes[9];
  EFI_STATUS Status;
  UINTN Index;
  UINTN Skip = QUP_TAG2_INPUT_EOT_SIZE;
  UINTN Position = 0;
  if (Data == NULL || Length == 0 || Length > 32) return EFI_INVALID_PARAMETER;
  Bytes[0] = QUP_TAG2_START;
  Bytes[1] = RMI_I2C_ADDRESS << 1;
  Bytes[2] = QUP_TAG2_DATA_WRITE;
  Bytes[3] = 1;
  Bytes[4] = Register;
  Bytes[5] = QUP_TAG2_START;
  Bytes[6] = (RMI_I2C_ADDRESS << 1) | 1;
  Bytes[7] = QUP_TAG2_DATA_READ_END;
  Bytes[8] = (UINT8)Length;
  Status = QupPrepare(sizeof(Bytes), Length + QUP_TAG2_INPUT_EOT_SIZE);
  if (EFI_ERROR(Status)) { QupLogFailure("prepare-read", Status); return Status; }
  QupWriteBytes(Bytes, sizeof(Bytes));
  Status = QupSetState(QUP_RUN_STATE);
  if (EFI_ERROR(Status)) { QupLogFailure("run-read", Status); return Status; }
  for (Index = 0; Index < QUP_TIMEOUT_US; Index++) {
    UINT32 Operational;
    UINT32 I2c;
    Status = QupCheckErrors();
    if (EFI_ERROR(Status)) { QupLogFailure("read-error", Status); return Status; }
    Operational = MmioRead32(QUP_BASE + QUP_OPERATIONAL);
    I2c = MmioRead32(QUP_BASE + QUP_I2C_STATUS);
    if (Operational & QUP_IN_NOT_EMPTY) {
      UINT32 Word = MmioRead32(QUP_BASE + QUP_IN_FIFO_BASE);
      UINTN Byte;
      for (Byte = 0; Byte < 4; Byte++) {
        UINT8 Value = (UINT8)(Word >> (Byte * 8));
        if (Skip != 0) Skip--;
        else if (Position < Length) Data[Position++] = Value;
      }
    }
    QupAcknowledgeService();
    if (Position == Length && !(I2c & QUP_I2C_BUS_ACTIVE))
      return EFI_SUCCESS;
    MicroSecondDelay(1);
  }
  Status = EFI_TIMEOUT;
  QupLogFailure("read-timeout", Status);
  return Status;
}

STATIC EFI_STATUS RmiWriteRegister(IN UINT8 Register, IN CONST UINT8 *Data, IN UINTN Length)
{
  UINT8 Buffer[8];
  if (Length > sizeof(Buffer) - 1) return EFI_INVALID_PARAMETER;
  Buffer[0] = Register;
  CopyMem(&Buffer[1], Data, Length);
  return QupWrite(Buffer, Length + 1);
}

STATIC EFI_STATUS RmiReadRegister(IN UINT8 Register, OUT UINT8 *Data, IN UINTN Length)
{
  return QupWriteRead(Register, Data, Length);
}

STATIC EFI_STATUS RmiSelectPage(IN UINT8 Page)
{
  EFI_STATUS Status;
  if (mTouch.CurrentPage == Page)
    return EFI_SUCCESS;
  Status = RmiWriteRegister(RMI_PAGE_SELECT, &Page, 1);
  if (!EFI_ERROR(Status))
    mTouch.CurrentPage = Page;
  return Status;
}

STATIC EFI_STATUS FindF12(VOID)
{
  UINT8 Page;
  UINT8 Address;
  UINTN Entry;
  EFI_STATUS Status;
  mTouch.CurrentPage = 0xFF;
  for (Page = 0; Page < RMI_MAX_PAGES; Page++) {
    Status = RmiSelectPage(Page);
    if (EFI_ERROR(Status)) return Status;
    for (Address = RMI_PDT_START, Entry = 0; Entry < RMI_MAX_PDT_ENTRIES && Address >= RMI_PDT_ENTRY_SIZE; Address -= RMI_PDT_ENTRY_SIZE, Entry++) {
      UINT8 Descriptor[RMI_PDT_ENTRY_SIZE];
      Status = RmiReadRegister(Address, Descriptor, sizeof(Descriptor));
      if (EFI_ERROR(Status)) return Status;
      if (Descriptor[5] == 0)
        break;
      if (Descriptor[5] == RMI_FUNCTION_F12) {
        mTouch.F12Page = Page;
        mTouch.F12Data = Descriptor[3];
        DEBUG((EFI_D_ERROR, "FJ TOUCH F12 page=%u data=%02x query=%02x ctrl=%02x\n",
          Page, Descriptor[3], Descriptor[0], Descriptor[2]));
        return EFI_SUCCESS;
      }
    }
  }
  return EFI_NOT_FOUND;
}

STATIC VOID EFIAPI WaitForInput(IN EFI_EVENT Event, IN VOID *Context)
{
  if (!mTouch.Stopped && mTouch.StateChanged)
    gBS->SignalEvent(Event);
}

STATIC EFI_STATUS EFIAPI Reset(IN EFI_ABSOLUTE_POINTER_PROTOCOL *This, IN BOOLEAN ExtendedVerification)
{
  EFI_TPL Tpl = gBS->RaiseTPL(TPL_NOTIFY);
  mTouch.StateChanged = FALSE;
  mTouch.Pressed = FALSE;
  gBS->RestoreTPL(Tpl);
  return mTouch.Stopped ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI GetState(IN EFI_ABSOLUTE_POINTER_PROTOCOL *This,
                                   OUT EFI_ABSOLUTE_POINTER_STATE *State)
{
  EFI_TPL Tpl;
  if (State == NULL) return EFI_INVALID_PARAMETER;
  Tpl = gBS->RaiseTPL(TPL_NOTIFY);
  if (mTouch.Stopped) {
    gBS->RestoreTPL(Tpl);
    return EFI_DEVICE_ERROR;
  }
  if (!mTouch.StateChanged) {
    gBS->RestoreTPL(Tpl);
    return EFI_NOT_READY;
  }
  *State = mTouch.State;
  mTouch.StateChanged = FALSE;
  gBS->RestoreTPL(Tpl);
  return EFI_SUCCESS;
}

STATIC VOID EFIAPI Poll(IN EFI_EVENT Event, IN VOID *Context)
{
  UINT8 Data[TOUCH_BYTES];
  EFI_STATUS Status;
  BOOLEAN Pressed;
  if (mTouch.Stopped) return;
  Status = RmiSelectPage(mTouch.F12Page);
  if (!EFI_ERROR(Status))
    Status = RmiReadRegister(mTouch.F12Data, Data, sizeof(Data));
  if (EFI_ERROR(Status)) {
    if (++mTouch.FailureCount == TOUCH_FAILURE_LIMIT) {
      mTouch.Stopped = TRUE;
      DEBUG((EFI_D_ERROR, "FJ TOUCH stopped after %u I2C/RMI read failures: %r\n",
             TOUCH_FAILURE_LIMIT, Status));
    }
    return;
  }
  mTouch.FailureCount = 0;
  // F12 object classification is already debounced by the controller. Extra
  // multi-sample filtering made short taps require 40-60 ms and lose events.
  Pressed = Data[0] != 0;
  if (Pressed) {
    UINT32 X = (UINT32)Data[1] | ((UINT32)Data[2] << 8);
    UINT32 Y = (UINT32)Data[3] | ((UINT32)Data[4] << 8);
    if (X >= SCREEN_X) X = SCREEN_X - 1;
    if (Y >= SCREEN_Y) Y = SCREEN_Y - 1;
    if (!mTouch.Pressed || X != mTouch.State.CurrentX || Y != mTouch.State.CurrentY) {
      mTouch.State.CurrentX = X;
      mTouch.State.CurrentY = Y;
      mTouch.State.CurrentZ = 0;
      mTouch.State.ActiveButtons = EFI_ABSP_TouchActive;
      mTouch.StateChanged = TRUE;
      if (mTouch.LoggedEvents++ < 24)
        DEBUG((EFI_D_ERROR, "FJ TOUCH event down x=%u y=%u status=%02x\n", X, Y, Data[0]));
    }
  } else if (mTouch.Pressed) {
    mTouch.State.ActiveButtons = 0;
    mTouch.StateChanged = TRUE;
    if (mTouch.LoggedEvents++ < 24)
      DEBUG((EFI_D_ERROR, "FJ TOUCH event up x=%Lu y=%Lu\n",
             mTouch.State.CurrentX, mTouch.State.CurrentY));
  }
  mTouch.Pressed = Pressed;
}

STATIC VOID EFIAPI ExitBootServices(IN EFI_EVENT Event, IN VOID *Context)
{
  mTouch.Stopped = TRUE;
}

EFI_STATUS EFIAPI FujisanTouchInitialize(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_HANDLE Handle = NULL;
  UINT32 Version = MmioRead32(QUP_BASE + QUP_HW_VERSION);
  ZeroMem(&mTouch, sizeof(mTouch));
  DEBUG((EFI_D_ERROR, "FJ TOUCH 20260909-T7 QUP=%08x\n", Version));
  // The HW version aperture is readable before the branch clock is enabled,
  // but state/config accesses require active TLMM and GCC clocks.
  if (Version == 0 || Version == MAX_UINT32)
    return EFI_NOT_READY;
  Status = ConfigureI2c12Pins();
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "FJ TOUCH pinmux setup failed: %r\n", Status));
    return Status;
  }
  Status = EnableQupClocks();
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "FJ TOUCH clock setup failed: %r\n", Status));
    return Status;
  }
  Status = FindF12();
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "FJ TOUCH F12 probe failed: %r; no controller/pin/PMIC writes\n", Status));
    return Status;
  }
  mTouch.Mode.AbsoluteMinX = 0;
  mTouch.Mode.AbsoluteMaxX = SCREEN_X - 1;
  mTouch.Mode.AbsoluteMinY = 0;
  mTouch.Mode.AbsoluteMaxY = SCREEN_Y - 1;
  mTouch.Mode.AbsoluteMinZ = 0;
  mTouch.Mode.AbsoluteMaxZ = 0;
  mTouch.Mode.Attributes = EFI_ABSP_SupportsAltActive;
  // EFI_ABSOLUTE_POINTER_PROTOCOL carries a pointer to the mode record;
  // SimpleInit dereferences this immediately when the protocol notification
  // fires. Leaving Mode NULL causes the observed exception in _touch_register.
  mTouch.Protocol.Mode = &mTouch.Mode;
  mTouch.Protocol.Reset = Reset;
  mTouch.Protocol.GetState = GetState;
  Status = gBS->CreateEvent(EVT_NOTIFY_WAIT, TPL_NOTIFY, WaitForInput, NULL, &mTouch.WaitEvent);
  if (EFI_ERROR(Status)) goto Fail;
  mTouch.Protocol.WaitForInput = mTouch.WaitEvent;
  Status = gBS->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Poll, NULL, &mTouch.PollEvent);
  if (EFI_ERROR(Status)) goto Fail;
  Status = gBS->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, ExitBootServices, NULL,
                            &gEfiEventExitBootServicesGuid, &mTouch.ExitEvent);
  if (EFI_ERROR(Status)) goto Fail;
  Status = gBS->SetTimer(mTouch.PollEvent, TimerPeriodic, TOUCH_POLL_100NS);
  if (EFI_ERROR(Status)) goto Fail;
  Status = gBS->InstallMultipleProtocolInterfaces(&Handle,
    &gEfiDevicePathProtocolGuid, &mPath,
    &gEfiAbsolutePointerProtocolGuid, &mTouch.Protocol, NULL);
  if (EFI_ERROR(Status)) goto Fail;
  DEBUG((EFI_D_ERROR, "FJ TOUCH AbsolutePointer installed; primary Synaptics only\n"));
  return EFI_SUCCESS;
Fail:
  mTouch.Stopped = TRUE;
  if (mTouch.PollEvent != NULL) gBS->CloseEvent(mTouch.PollEvent);
  if (mTouch.ExitEvent != NULL) gBS->CloseEvent(mTouch.ExitEvent);
  if (mTouch.WaitEvent != NULL) gBS->CloseEvent(mTouch.WaitEvent);
  DEBUG((EFI_D_ERROR, "FJ TOUCH init failed: %r\n", Status));
  return Status;
}
