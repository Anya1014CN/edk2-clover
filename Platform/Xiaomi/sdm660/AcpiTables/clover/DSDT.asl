DefinitionBlock ("DSDT.aml", "DSDT", 2, "QCOMM ", "SDM660 ", 3)
{
    Scope (\_SB)
    {
        Name (PSUB, "MTP00660")
        Name (SOID, 0xFFFFFFFF)
        Name (SIDS, "899800000000000")
        Name (SIDV, 0xFFFFFFFF)
        Name (SVMJ, 0xFFFF)
        Name (SVMI, 0xFFFF)
        Name (SDFE, 0xFFFF)
        Name (SFES, "899800000000000")
        Name (SIDM, 0xFFFFFFFFF)
        Name (SOSN, 0xAAAAAAAABBBBBBBB)
        Name (RMTB, 0xAAAAAAAA)
        Name (RMTX, 0xBBBBBBBB)
        Name (RFMB, 0xCCCCCCCC)
        Name (RFMS, 0xDDDDDDDD)
        Name (RFAB, 0xEEEEEEEE)
        Name (RFAS, 0x77777777)
        Name (PRP0, 0xFFFFFFFF)

        Device (SDC1)
        {
            Name (_HID, "QCOM24BF")
            Name (_CID, "QCOMF294")
            Name (_UID, 0)
            Name (_CCA, 0)

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0C0C4000, 0x00001000)

                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, ) { 0x8E }
            })

            Method (_DIS, 0, NotSerialized) {}
            Method (_STA, 0, NotSerialized) { Return (0x0F) }

            Device (EMMC)
            {
                Name (_ADR, 8)
                Name (_RMV, 0)
            }
        }

        Device (BAM3)
        {
            Name (_HID, "QCOM6012")
            Name (_UID, 3)
            Name (_CCA, 0)

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0C144000, 0x0001F000)

                Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive, ,, ) { 0x10E }
            })

            Method (_SUB, 0, NotSerialized) { Return (\_SB.PSUB) }
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (I2C3)
        {
            Name (_HID, "QCOM601F")
            Name (_DEP, Package () { \_SB.BAM3 })
            Name (_UID, 3)

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0C177000, 0x00000600)

                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, ) { 0x81 }
            })

            Name (FNOC, Buffer ()
            {
                0x04,
                0x00,
                0x00,
                0x40,
                0x14,
                0x0C,
                0x09,
                0x08,
                0x00,
                0x04,
                0x00,
                0xF8,
                0x24,
                0x01
            })

            Method (_SUB, 0, NotSerialized) { Return (\_SB.PSUB) }
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (GIO0)
        {
            Name (_HID, "QCOM6016")
            Name (_UID, 0)
            Name (GABL, 0)

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x03000000, 0x00C00000)

                Interrupt (ResourceConsumer, Level, ActiveHigh, Shared, ,, ) { 0xF0 }
                Interrupt (ResourceConsumer, Level, ActiveHigh, Shared, ,, ) { 0xF0 }
                Interrupt (ResourceConsumer, Level, ActiveHigh, Shared, ,, ) { 0xF0 }
            })

            Name (OFNI, Buffer ()
            {
                0x72,
                0x00
            })

            Method (_REG, 2, NotSerialized)
            {
                If (Arg0 == 8)
                {
                    GABL = Arg1
                }
            }

            Method (_SUB, 0, NotSerialized) { Return (\_SB.PSUB) }
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU0)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 0)

            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU1)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 1)

            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU2)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 2)

            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU3)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 3)

            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU4)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 4)

            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU5)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 5)

            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU6)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 6)

            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Device (CPU7)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 7)

            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        Name (QUFN, 0)
        Name (HPDB, 0)
        Name (PINA, 0)
        Name (DPPN, 13)

        Name (DPP0, Buffer () { 0x00 })
        Name (DPP1, Buffer () { 0x00 })
        Name (MPP0, Buffer () { 0x00 })
        Name (MPP1, Buffer () { 0x00 })
        Name (HPDS, Buffer () { 0x00 })
        Name (CCST, Buffer () { 0x02 })
        Name (PORT, Buffer () { 0x02 })
        Name (HIRQ, Buffer () { 0x00 })
        Name (HSFL, Buffer () { 0x00 })
        Name (USBC, Buffer () { 0x0B })
        Name (MUXC, Buffer () { 0x00 })



        //
        //SPMI driver.
        //
        Device (SPMI)
        {
            Name (_HID, "QCOM6015")  // _HID: Hardware ID
            Alias (\_SB.PSUB, _SUB)
            Name (_UID, One)  // _UID: Unique ID
            Name (_CCA, Zero)  // _CCA: Cache Coherency Attribute
            Method (_CRS, 0, NotSerialized)  // _CRS: Current Resource Settings
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0x08000000,         // Address Base
                        0x02800000,         // Address Length
                        )
                })
                Return (RBUF) /* \_SB_.SPMI._CRS.RBUF */
            }

            Method (CONF, 0, NotSerialized)
            {
                Name (XBUF, Buffer (0x1A)
                {
                    /* 0000 */  0x00, 0x01, 0x01, 0x01, 0xFF, 0x00, 0x02, 0x00,  // ........
                    /* 0008 */  0x07, 0x06, 0x04, 0x07, 0x01, 0xFF, 0x10, 0x01,  // ........
                    /* 0010 */  0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x02, 0x80,  // ........
                    /* 0018 */  0x00, 0x00                                       // ..
                })
                Return (XBUF) /* \_SB_.SPMI.CONF.XBUF */
            }
        }

        // This file contains common Power Management IC (PMIC) ACPI device definitions

        //
        //PMIC KMDF
        //

        Name (PM3E, One)

        Device (PMIC)
        {
            Name (_DEP, Package (One)  // _DEP: Dependencies
            {
                \_SB.SPMI
            })
            Name (_HID, "QCOM6063")  // _HID: Hardware ID
            Alias (\_SB.PSUB, _SUB)
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (0x0F)
            }

            Method (PMCF, 0, NotSerialized)
            {
                Name (CFG0, Package (0x03)
                {
                    0x02, 
                    Package (0x02)
                    {
                        Zero, 
                        One
                    }, 

                    Package (0x02)
                    {
                        0x04, 
                        0x05
                    }
                })
                Return (CFG0) /* \_SB_.PMIC.PMCF.CFG0 */
            }
        }

        //
        // PMIC
        //
        Device (PM01)
        {
            Name (_HID, "QCOM6066")  // _HID: Hardware ID
            Alias (\_SB.PSUB, _SUB)
            Name (_UID, One)  // _UID: Unique ID
            Name (_DEP, Package (One)  // _DEP: Dependencies
            {
                \_SB.PMIC
            })
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                    Return (0x0F)
            }

            Method (_CRS, 0, NotSerialized)  // _CRS: Current Resource Settings
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Shared, ,, )
                    {
                        0x00000166,
                    }
                })
                Return (RBUF) /* \_SB_.PM01._CRS.RBUF */
            }

            Method (PMIO, 0, NotSerialized)
            {
                Name (CFG0, Package (0x0B)
                {
                    Zero, 
                    Zero, 
                    0x1040, 
                    0x40, 
                    0x1A, 
                    0xC0, 
                    Zero, 
                    0xA0, 
                    Zero, 
                    Zero, 
                    Zero
                })
                Return (CFG0) /* \_SB_.PM01.PMIO.CFG0 */
            }

            // _DSM method to mark PM01's ActiveBoth interrupts
            Method(_DSM, 0x4, NotSerialized) {
                // DSM UUID
                switch(ToBuffer(Arg0))
                {
                    // ACPI DSM UUID for GPIO
                    case(ToUUID("4F248F40-D5E2-499F-834C-27758EA1CD3F"))
                    {
                        // DSM Function
                        switch(ToInteger(Arg2))
                        {
                            // Function 0: Return supported functions, based on revision
                            case(0)
                            {
                                // revision 0: function 0 & 1 are supported.
                                return (Buffer() {0x3})
                            }

                            // Function 1: For emulated ActiveBoth controllers, returns
                            //             a package of controller-relative pin numbers.
                            //             Each corresponding pin will have an initial
                            //             polarity of ActiveHigh.
                            case(1)
                            {
                                // Marks pins to be ActiveHigh.
                                Return (Package() {0x40, 0x41})
                            }

                            default
                            {
                                // Functions 2+: not supported
                            }
                        }
                    }

                    default
                    {
                        // No other GUIDs supported
                        Return(Buffer(One) { 0x00 })
                    }
                }
            }
        }

        Device (URS0)
        {
            Name (_HID, "QCOM2430")
            Name (_CID, "PNP0CA1")
            Name (_UID, 0)
            Name (_CCA, 0)

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0A800000, 0x00100000)
            })

            Method (_SUB, 0, NotSerialized) { Return (\_SB.PSUB) }

            Device (USB0)
            {
                Name (_ADR, 0)
                Name (_S0W, 3)

                Name (_CRS, ResourceTemplate ()
                {
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Shared, ,, )        { 0xA3 }
                    Interrupt (ResourceConsumer, Level, ActiveHigh, SharedAndWake, ,, ) { 0x17B }
                    Interrupt (ResourceConsumer, Level, ActiveHigh, SharedAndWake, ,, ) { 0x113 }
                })

                Name (_PLD, Package ()
                {
                    Buffer ()
                    {
                        0x82,
                        0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00,
                        0x69,
                        0x0C,
                        0x80, 0x00,
                        0x00, 0x00, 0x00, 0x00,
                        0xFF, 0xFF, 0xFF, 0xFF
                    }
                })

                Name (_UPC, Package ()
                {
                    1,
                    9,
                    0,
                    0
                })

                Method (_DSM, 4, Serialized)
                {
                    Switch (ToBuffer (Arg0))
                    {
                        Case (ToUUID ("CE2EE385-00E6-48CB-9F05-2EDB927C4899"))
                        {
                            Switch (ToInteger (Arg2))
                            {
                                Case (0)
                                {
                                    If (ToInteger (Arg1) == 0)
                                    {
                                        Return (Buffer () { 0x1D })
                                    }

                                    Return (Buffer () { 0x01 })
                                }

                                Case (2)
                                {
                                    Return (0x00)
                                }

                                Case (3)
                                {
                                    Return (0x00)
                                }

                                Case (4)
                                {
                                    Return (0x02)
                                }

                                Default
                                {
                                    Return (Buffer () { 0x00 })
                                }
                            }
                        }

                        Default
                        {
                            Return (Buffer () { 0x00 })
                        }
                    }
                }

                Method (DPM0, 1, NotSerialized)
                {
                    DPP0 = Arg0
                }

                Method (PHYC, 0, NotSerialized)
                {
                    Return (Package () {})
                }

                Method (CCVL, 0, NotSerialized) { Return (\_SB.CCST) }
                Method (HSEN, 0, NotSerialized) { Return (\_SB.HSFL) }
                Method (_STA, 0, NotSerialized) { Return (0x0F) }
            }

            Device (UFN0)
            {
                Name (_ADR, 0)
                Name (_S0W, 3)

                Name (_CRS, ResourceTemplate ()
                {
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Shared, ,, )        { 0xA3 }
                    Interrupt (ResourceConsumer, Level, ActiveHigh, SharedAndWake, ,, ) { 0xD4 }
                })

                Name (_PLD, Package ()
                {
                    Buffer ()
                    {
                        0x82,
                        0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00,
                        0x69,
                        0x0C,
                        0x80, 0x00,
                        0x00, 0x00, 0x00, 0x00,
                        0xFF, 0xFF, 0xFF, 0xFF
                    }
                })

                Name (_UPC, Package ()
                {
                    1,
                    9,
                    0,
                    0
                })

                Method (_DSM, 4, Serialized)
                {
                    Switch (ToBuffer (Arg0))
                    {
                        Case (ToUUID ("FE56CFEB-49D5-4378-A8A2-2978DBE54AD2"))
                        {
                            Switch (ToInteger (Arg2))
                            {
                                Case (0)
                                {
                                    If (ToInteger (Arg1) == 0)
                                    {
                                        Return (Buffer () { 0x03 })
                                    }

                                    Return (Buffer () { 0x01 })
                                }

                                Case (1)
                                {
                                    Return (0x20)
                                }

                                Default
                                {
                                    Return (Buffer () { 0x00 })
                                }
                            }
                        }

                        Case (ToUUID ("18DE299F-9476-4FC9-B43B-8AEB713ED751"))
                        {
                            Switch (ToInteger (Arg2))
                            {
                                Case (0)
                                {
                                    If (ToInteger (Arg1) == 0)
                                    {
                                        Return (Buffer () { 0x03 })
                                    }

                                    Return (Buffer () { 0x01 })
                                }

                                Case (1)
                                {
                                    Return (0x39)
                                }

                                Default
                                {
                                    Return (Buffer () { 0x00 })
                                }
                            }
                        }

                        Default
                        {
                            Return (Buffer () { 0x00 })
                        }
                    }
                }

                Method (PHYC, 0, NotSerialized)
                {
                    Return (Package () {})
                }

                Method (CCVL, 0, NotSerialized) { Return (\_SB.CCST) }
                Method (_STA, 0, NotSerialized) { Return (0x0F) }
            }
        }

        Device (TSC1)
        {
            Name (_HID, "GDGT9XXT")
            Name (_DEP, Package () { \_SB.GIO0, \_SB.I2C3 })
            Name (_CRS, ResourceTemplate ()
            {
                I2cSerialBus (0x5D, ControllerInitiated, 0x00061A80, AddressingMode7Bit, "\\_SB.I2C3", 0x00, ResourceConsumer, , )
                GpioInt (Level, ActiveLow, Exclusive, PullNone, 0x0000, "\\_SB.GIO0", 0x00, ResourceConsumer, , ) { 0x43 }
            })
            Method (_SUB, 0, NotSerialized) { Return (\_SB.PSUB) }
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

    }
}
