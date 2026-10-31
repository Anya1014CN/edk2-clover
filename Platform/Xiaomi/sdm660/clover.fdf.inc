// per-device BSP DXEs

// ACPI Tables
FILE FREEFORM = 7E374E25-8E01-4FEE-87F2-390C23C606CD {
  SECTION RAW = Platform/Xiaomi/sdm660/AcpiTables/clover/DSDT.aml
  SECTION RAW = Silicon/Qualcomm/sdm660/AcpiTables/BuiltIn/CSRT.aml
  SECTION RAW = Silicon/Qualcomm/sdm660/AcpiTables/BuiltIn/DBG2.aml
  SECTION RAW = Silicon/Qualcomm/sdm660/AcpiTables/BuiltIn/FACS.aml
  SECTION RAW = Silicon/Qualcomm/sdm660/AcpiTables/BuiltIn/FADT.aml
  SECTION RAW = Silicon/Qualcomm/sdm660/AcpiTables/BuiltIn/GTDT.aml
  SECTION RAW = Silicon/Qualcomm/sdm660/AcpiTables/BuiltIn/MADT.aml
  SECTION RAW = Silicon/Qualcomm/sdm660/AcpiTables/BuiltIn/MCFG.aml
  SECTION UI = "AcpiTables"
}

// per-device ButtonsDxe (disabled for now; no per-device binary)
#FILE DRIVER = 5bd181db-0487-4f1a-ae73-820e165611b3 {
#  SECTION DXE_DEPEX = Platform/EFI_Binaries/Drivers/Devices/clover/ButtonsDxe/ButtonsDxe.depex
#  SECTION PE32 = Platform/EFI_Binaries/Drivers/Devices/clover/ButtonsDxe/ButtonsDxe.efi
#  SECTION UI = "ButtonsDxe"
#}
