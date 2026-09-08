#include <Library/PcdLib.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/HobLib.h>
#include <Library/SerialPortLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/PlatformPrePiLib.h>

#include "PlatformUtils.h"

VOID UartInit(VOID)
{
  SerialPortInitialize();
  DEBUG((EFI_D_ERROR, "FJ-UEFI-20260908-E PREPI EL%u\n", (UINT32)(ArmReadCurrentEL() >> 2)));
  DEBUG((EFI_D_ERROR, "FJ firmware %s built %a %a\n",
         (CHAR16 *)PcdGetPtr(PcdFirmwareVersionString), __DATE__, __TIME__));
}

VOID SetWatchdogState(BOOLEAN Enable)
{
  MmioWrite32(APSS_WDT_BASE + APSS_WDT_ENABLE_OFFSET, Enable);
}

VOID PlatformInitialize()
{
  UartInit();
  SetWatchdogState(FALSE);
  DEBUG((EFI_D_ERROR, "FJ PREPI platform ready; entering memory/FV setup\n"));
}
