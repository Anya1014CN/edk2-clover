# clover ACPI tables

`DSDT.asl` is the clover-specific (Mi Pad 4 / 4 Plus, SDM660) ACPI DSDT used by
`Platform/Xiaomi/sdm660/clover.fdf.inc`.

It is based on the SDM660 Windows bring-up layout (eMMC `QCOMF294`, BAM3,
I2C3, TLMM/GIO0, URS0/USB, Goodix `GDGT9XXT`), with values taken from the
device tree of a real Mi Pad 4 Plus:

- eMMC: `0x0C0C4000`, IRQ `0x8E`
- I2C3: `0x0C177000`, IRQ `0x81`, BAM pipes 9/8
- BAM3: `0x0C144000`, IRQ `0x10E`
- TLMM: `0x03000000`, IRQ `0xF0`
- USB: `0x0A800000`, IRQ `0xA3/0x17B/0x113`, `0xA3/0xD4`
- Touch: Goodix `GDGT9XXT`, I2C3 `0x5D`, GPIO `0x43`

## Regenerate

```sh
iasl -f -ve DSDT.asl
```

`-f` is required because `GDGT9XXT` is intentionally a non-standard ACPI HID
matches the Windows Goodix driver (`ACPI\GDGT9XXT`); iasl otherwise rejects the
non-hex suffix. The generated `DSDT.aml` is committed so the firmware build
does not need iasl.
