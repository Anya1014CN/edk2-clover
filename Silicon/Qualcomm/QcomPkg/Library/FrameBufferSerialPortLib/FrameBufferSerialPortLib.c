#include <PiDxe.h>

#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/HobLib.h>
#include <Library/SerialPortLib.h>

#include <Resources/FbColor.h>
#include <Resources/font5x12.h>

#include "Library/FrameBufferSerialPortLib.h"
#include <Library/TimerLib.h>


FBCON_POSITION m_Position;
FBCON_POSITION m_MaxPosition;
FBCON_COLOR    m_Color;
BOOLEAN        m_Initialized = FALSE;

UINTN gWidth = FixedPcdGet32(PcdMipiFrameBufferWidth);
// Physical scanline width, in pixels. It may exceed the visible width.
UINTN gStride = FixedPcdGet32(PcdMipiFrameBufferStride);
UINTN gHeight = FixedPcdGet32(PcdMipiFrameBufferHeight);
UINTN gBpp    = FixedPcdGet32(PcdMipiFrameBufferPixelBpp);
UINTN delay = FixedPcdGet32(PcdMipiFrameBufferDelay);

/*
 * The Fujisan kernel exposes ramoops through /sys/fs/pstore.  This trace
 * uses the first dmesg record and must be captured immediately after adb
 * reconnects, before Android userspace consumes or rotates it.
 *
 * persistent_ram_buffer is deliberately reproduced here: sig, start and
 * size are three little-endian 32-bit fields followed by the circular data
 * area.  The kernel expects sig == 0x43474244 when the ramoops record has no
 * ECC and no alternate signature.
 */
#define UEFI_PSTORE_SIGNATURE 0x43474244U

typedef struct {
  volatile UINT32 Signature;
  volatile INT32  Start;
  volatile INT32  Size;
} UEFI_PSTORE_HEADER;

STATIC volatile UEFI_PSTORE_HEADER *mUefiPstoreHeader;
STATIC volatile UINT8               *mUefiPstoreData;
STATIC UINTN                         mUefiPstoreDataSize;
STATIC BOOLEAN                       mUefiPstoreReady;

STATIC
VOID
UefiPstoreInitialize(VOID)
{
  UINT32 Address = FixedPcdGet32(PcdUefiPstoreLogAddress);
  UINT32 Size    = FixedPcdGet32(PcdUefiPstoreLogSize);

  if (mUefiPstoreReady || Address == 0 || Size <= sizeof(UEFI_PSTORE_HEADER))
    return;

  mUefiPstoreHeader = (volatile UEFI_PSTORE_HEADER *)(UINTN)Address;
  mUefiPstoreData = (volatile UINT8 *)(UINTN)(Address + sizeof(UEFI_PSTORE_HEADER));
  mUefiPstoreDataSize = Size - sizeof(UEFI_PSTORE_HEADER);
  // SerialPortLib is statically linked into every DXE: module-local state
  // must not clear the shared trace. BootShim starts a new boot record.
  if (mUefiPstoreHeader->Signature != UEFI_PSTORE_SIGNATURE ||
      mUefiPstoreHeader->Size < 0 ||
      (UINTN)mUefiPstoreHeader->Size > mUefiPstoreDataSize ||
      mUefiPstoreHeader->Start !=
        mUefiPstoreHeader->Size % (INT32)mUefiPstoreDataSize) {
    CONST CHAR8 Header[] = "====0.0\nFJ-UEFI-20260908-E NO-SHIM-RECORD\n";
    CopyMem((VOID *)mUefiPstoreData, Header, sizeof(Header) - 1);
    mUefiPstoreHeader->Start = sizeof(Header) - 1;
    mUefiPstoreHeader->Size = sizeof(Header) - 1;
    mUefiPstoreHeader->Signature = UEFI_PSTORE_SIGNATURE;
  }
  ArmDataSynchronizationBarrier();
  mUefiPstoreReady = TRUE;
}

STATIC
VOID
UefiPstoreWrite(IN CONST UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  UINTN Index;

  UefiPstoreInitialize();
  if (!mUefiPstoreReady)
    return;

  // Append-only: Linux requires the ==== timestamp at the beginning of a
  // dmesg record. A wrapping ring would erase that header and be discarded.
  // The Fujisan DEBUG mask deliberately excludes per-protocol INFO spam.
  for (Index = 0; Index < NumberOfBytes; Index++) {
    UINTN Size = (UINTN)mUefiPstoreHeader->Size;
    CONST CHAR8 Full[] = "\nFJ-TRACE-FULL (later messages dropped)\n";
    if (Size >= mUefiPstoreDataSize - sizeof(Full))
      break;
    mUefiPstoreData[Size++] = Buffer[Index];
    if (Size == mUefiPstoreDataSize - sizeof(Full)) {
      CopyMem((VOID *)(mUefiPstoreData + Size), Full, sizeof(Full) - 1);
      Size += sizeof(Full) - 1;
    }
    mUefiPstoreHeader->Size = (INT32)Size;
    mUefiPstoreHeader->Start = (INT32)(Size % mUefiPstoreDataSize);
  }
  ArmDataSynchronizationBarrier();
  WriteBackDataCacheRange((VOID *)mUefiPstoreHeader,
                         sizeof(*mUefiPstoreHeader) + mUefiPstoreHeader->Size);

}

// Module-used internal routine
void FbConPutCharWithFactor(char c, int type, unsigned scale_factor);

void FbConDrawglyph(
    char *pixels, unsigned stride, unsigned bpp, unsigned *glyph,
    unsigned scale_factor);

void FbConReset(void);
void FbConScrollUp(void);
void FbConFlush(void);

RETURN_STATUS
EFIAPI
SerialPortInitialize(VOID)
{
  UINTN InterruptState = 0;

  // Prevent dup initialization
  if (m_Initialized)
    return RETURN_SUCCESS;

  UefiPstoreInitialize();
  if (FixedPcdGetBool(PcdFrameBufferSerialLogOnly)) {
    m_Initialized = TRUE;
    return RETURN_SUCCESS;
  }

  // Interrupt Disable
  InterruptState = ArmGetInterruptState();
  ArmDisableInterrupts();

  // Reset console
  FbConReset();

  // Set flag
  m_Initialized = TRUE;

  if (InterruptState)
    ArmEnableInterrupts();
  return RETURN_SUCCESS;
}

void ResetFb(void)
{
  // Clear current screen.
  char *Pixels  = (void *)FixedPcdGet32(PcdMipiFrameBufferAddress);
  UINTN BgColor = FB_BGRA8888_BLACK;

  // Set to black color.
  for (UINTN i = 0; i < gStride; i++) {
    for (UINTN j = 0; j < gHeight; j++) {
      BgColor = FB_BGRA8888_BLACK;
      // Set pixel bit
      for (UINTN p = 0; p < (gBpp / 8); p++) {
        *Pixels = (unsigned char)BgColor;
        BgColor = BgColor >> 8;
        Pixels++;
      }
    }
  }
}

void FbConReset(void)
{
  // Reset position.
  m_Position.x = 0;
  m_Position.y = 0;

  // Calc max position.
  m_MaxPosition.x = gWidth / (FONT_WIDTH + 1);
  m_MaxPosition.y = (gHeight - 1) / FONT_HEIGHT;

  // Reset color.
  m_Color.Foreground = FB_BGRA8888_WHITE;
  m_Color.Background = FB_BGRA8888_BLACK;
}

void FbConPutCharWithFactor(char c, int type, unsigned scale_factor)
{
  char *Pixels;

  if (!m_Initialized)
    return;

paint:

  if ((unsigned char)c > 127)
    return;

  if ((unsigned char)c < 32) {
    if (c == '\n') {
      goto newline;
    }
    else if (c == '\r') {
      m_Position.x = 0;
      return;
    }
    else {
      return;
    }
  }

  // Save some space
  if (m_Position.x == 0 && (unsigned char)c == ' ' &&
      type != FBCON_SUBTITLE_MSG && type != FBCON_TITLE_MSG)
    return;

  BOOLEAN intstate = ArmGetInterruptState();
  ArmDisableInterrupts();

  Pixels = (void *)FixedPcdGet32(PcdMipiFrameBufferAddress);
  Pixels += m_Position.y * ((gBpp / 8) * FONT_HEIGHT * gStride);
  Pixels += m_Position.x * scale_factor * ((gBpp / 8) * (FONT_WIDTH + 1));

  // Glyph row advancement must use the physical scanline stride.  The
  // Fujisan XBL surface has 1080 visible pixels but an aligned 1088-pixel
  // stride; using the visible width progressively writes each console row
  // into the wrong memory location.
  FbConDrawglyph(
      Pixels, gStride, (gBpp / 8), font5x12 + (c - 32) * 2, scale_factor);

  m_Position.x++;

  if (m_Position.x >= (int)(m_MaxPosition.x / scale_factor))
    goto newline;

  if (intstate)
    ArmEnableInterrupts();
  return;

newline:
  MicroSecondDelay( delay ); 
  m_Position.y += scale_factor;
  m_Position.x = 0;
  if (m_Position.y >= m_MaxPosition.y - scale_factor) {
    FbConFlush();
    m_Position.y = 0;

    if (intstate)
      ArmEnableInterrupts();
    goto paint;
  }
  else {
    Pixels = (void *)FixedPcdGet32(PcdMipiFrameBufferAddress);
    Pixels += m_Position.y * ((gBpp / 8) * FONT_HEIGHT * gStride);
    ZeroMem(Pixels, ((gBpp / 8) * FONT_HEIGHT * gStride) * scale_factor);
    FbConFlush();
    if (intstate)
      ArmEnableInterrupts();
  }
}

void FbConDrawglyph(
    char *pixels, unsigned stride, unsigned bpp, unsigned *glyph,
    unsigned scale_factor)
{
  char *       bg_pixels = pixels;
  unsigned     x, y, i, j, k;
  unsigned     data, temp;
  unsigned int fg_color = m_Color.Foreground;
  unsigned int bg_color = m_Color.Background;
  stride -= FONT_WIDTH * scale_factor;

  for (y = 0; y < FONT_HEIGHT / 2; ++y) {
    for (i = 0; i < scale_factor; i++) {
      for (x = 0; x < FONT_WIDTH; ++x) {
        for (j = 0; j < scale_factor; j++) {
          bg_color = m_Color.Background;
          for (k = 0; k < bpp; k++) {
            *bg_pixels = (unsigned char)bg_color;
            bg_color   = bg_color >> 8;
            bg_pixels++;
          }
        }
      }
      bg_pixels += (stride * bpp);
    }
  }

  for (y = 0; y < FONT_HEIGHT / 2; ++y) {
    for (i = 0; i < scale_factor; i++) {
      for (x = 0; x < FONT_WIDTH; ++x) {
        for (j = 0; j < scale_factor; j++) {
          bg_color = m_Color.Background;
          for (k = 0; k < bpp; k++) {
            *bg_pixels = (unsigned char)bg_color;
            bg_color   = bg_color >> 8;
            bg_pixels++;
          }
        }
      }
      bg_pixels += (stride * bpp);
    }
  }

  data = glyph[0];
  for (y = 0; y < FONT_HEIGHT / 2; ++y) {
    temp = data;
    for (i = 0; i < scale_factor; i++) {
      data = temp;
      for (x = 0; x < FONT_WIDTH; ++x) {
        if (data & 1) {
          for (j = 0; j < scale_factor; j++) {
            fg_color = m_Color.Foreground;
            for (k = 0; k < bpp; k++) {
              *pixels  = (unsigned char)fg_color;
              fg_color = fg_color >> 8;
              pixels++;
            }
          }
        }
        else {
          for (j = 0; j < scale_factor; j++) {
            pixels = pixels + bpp;
          }
        }
        data >>= 1;
      }
      pixels += (stride * bpp);
    }
  }

  data = glyph[1];
  for (y = 0; y < FONT_HEIGHT / 2; ++y) {
    temp = data;
    for (i = 0; i < scale_factor; i++) {
      data = temp;
      for (x = 0; x < FONT_WIDTH; ++x) {
        if (data & 1) {
          for (j = 0; j < scale_factor; j++) {
            fg_color = m_Color.Foreground;
            for (k = 0; k < bpp; k++) {
              *pixels  = (unsigned char)fg_color;
              fg_color = fg_color >> 8;
              pixels++;
            }
          }
        }
        else {
          for (j = 0; j < scale_factor; j++) {
            pixels = pixels + bpp;
          }
        }
        data >>= 1;
      }
      pixels += (stride * bpp);
    }
  }
}

void FbConScrollUp(void)
{
  unsigned short *dst   = (void *)FixedPcdGet32(PcdMipiFrameBufferAddress);
  unsigned short *src   = dst + (gStride * FONT_HEIGHT);
  unsigned        count = gStride * (gHeight - FONT_HEIGHT);

  while (count--) {
    *dst++ = *src++;
  }

  count = gStride * FONT_HEIGHT;
  while (count--) {
    *dst++ = m_Color.Background;
  }

  FbConFlush();
}

void FbConFlush(void)
{
  unsigned total_x, total_y;
  unsigned bytes_per_bpp;

  total_x       = gWidth;
  total_y       = gHeight;
  bytes_per_bpp = (gBpp / 8);

  WriteBackInvalidateDataCacheRange(
      (void *)FixedPcdGet32(PcdMipiFrameBufferAddress),
      (gStride * total_y * bytes_per_bpp));
}

UINTN
EFIAPI
SerialPortWrite(IN UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  UINT8 *CONST Final          = &Buffer[NumberOfBytes];
  UINTN        InterruptState = ArmGetInterruptState();
  ArmDisableInterrupts();
  UefiPstoreWrite(Buffer, NumberOfBytes);

  while (!FixedPcdGetBool(PcdFrameBufferSerialLogOnly) && Buffer < Final) {
    FbConPutCharWithFactor(*Buffer++, FBCON_COMMON_MSG, SCALE_FACTOR);
  }

  if (InterruptState)
    ArmEnableInterrupts();
  return NumberOfBytes;
}

UINTN
EFIAPI
SerialPortWriteCritical(IN UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  UINT8 *CONST Final             = &Buffer[NumberOfBytes];
  UINTN        CurrentForeground = m_Color.Foreground;
  UINTN        InterruptState    = ArmGetInterruptState();

  ArmDisableInterrupts();
  UefiPstoreWrite(Buffer, NumberOfBytes);
  m_Color.Foreground = FB_BGRA8888_YELLOW;

  while (!FixedPcdGetBool(PcdFrameBufferSerialLogOnly) && Buffer < Final) {
    FbConPutCharWithFactor(*Buffer++, FBCON_COMMON_MSG, SCALE_FACTOR);
  }

  m_Color.Foreground = CurrentForeground;

  if (InterruptState)
    ArmEnableInterrupts();
  return NumberOfBytes;
}

UINTN
EFIAPI
SerialPortRead(OUT UINT8 *Buffer, IN UINTN NumberOfBytes) { return 0; }

BOOLEAN
EFIAPI
SerialPortPoll(VOID) { return FALSE; }

RETURN_STATUS
EFIAPI
SerialPortSetControl(IN UINT32 Control) { return RETURN_UNSUPPORTED; }

RETURN_STATUS
EFIAPI
SerialPortGetControl(OUT UINT32 *Control) { return RETURN_UNSUPPORTED; }

RETURN_STATUS
EFIAPI
SerialPortSetAttributes(
    IN OUT UINT64 *BaudRate, IN OUT UINT32 *ReceiveFifoDepth,
    IN OUT UINT32 *Timeout, IN OUT EFI_PARITY_TYPE *Parity,
    IN OUT UINT8 *DataBits, IN OUT EFI_STOP_BITS_TYPE *StopBits)
{
  return RETURN_UNSUPPORTED;
}

UINTN SerialPortFlush(VOID) { return 0; }

VOID EnableSynchronousSerialPortIO(VOID)
{
  // Already synchronous
}
