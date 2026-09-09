#!/usr/bin/env python3
"""Pre-build checks only: metadata, layout and Python models, never a compiler.
These checks do not prove C/assembly compilation or physical display operation.
Run from any directory: python3 tools/tests/test_fujisan_bringup.py
"""
import os
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
DISPLAY = ROOT / 'Platform/ZTE/msm8996/Drivers/FujisanDisplayDxe/FujisanDisplayDxe.c'
MAP = ROOT / 'Silicon/Qualcomm/msm8996/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.c'


def regions():
    return [(name, int(base, 16), int(size, 16), mem)
            for name, base, size, mem in re.findall(
                r'\{"([^"]+)",\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),'
                r'\s*\w+,\s*\w+,\s*\w+,\s*(\w+)', MAP.read_text())]


def reserved_display(address, size):
    return any(name in ('UEFI Display Reserved', 'Display Reserved') and
               address >= base and address + size <= base + length
               for name, base, length, _ in regions())


class PreBuildTests(unittest.TestCase):
    def test_map_nonoverlap(self):
        ordered = sorted(regions(), key=lambda r: r[1])
        for left, right in zip(ordered, ordered[1:]):
            self.assertLessEqual(left[1] + left[2], right[1], (left, right))

    def test_fd_consistent(self):
        config = (ROOT / 'configs/msm8996.conf').read_text()
        base = int(re.search(r'^FD_BASE=(.*)$', config, re.M)[1], 16)
        size = int(re.search(r'^FD_SIZE=(.*)$', config, re.M)[1], 16)
        fd = next(r for r in regions() if r[0] == 'UEFI FD')
        self.assertEqual(fd[1:3], (base, size))
        self.assertEqual(fd[3], 'BsData')
        fdf = (ROOT / 'Platform/Qualcomm/msm8996/msm8996.fdf').read_text()
        self.assertIn(f'0x00000000|0x{size:08X}', fdf)
        blocks = int(re.search(r'NumBlocks\s*=\s*(0x[0-9A-Fa-f]+)', fdf)[1], 16)
        self.assertEqual(blocks * 4096, size)
        self.assertEqual(size % 16, 0)

    def test_heap_and_diagnostics(self):
        heap = next(r for r in regions() if r[0] == 'DXE Heap')
        dsc = (ROOT / 'Platform/Qualcomm/msm8996/msm8996.dsc').read_text()
        for pcd, value in [('PcdUefiMemPoolBase', heap[1]), ('PcdUefiMemPoolSize', heap[2])]:
            self.assertEqual(int(re.search(pcd+r'\|(0x[0-9A-Fa-f]+)', dsc)[1], 16), value)
        for address in (0x80400000, 0x83401000, 0xB1C00000, 0xBDD00000, 0xBFC00000, 0xBFFFFFFF):
            row = next(r for r in regions() if r[1] <= address < r[1] + r[2])
            self.assertEqual(row[3], 'Reserv', row)

    def test_bootshim_stackless_and_scoped(self):
        shim = (ROOT / 'tools/BootShim/BootShim.S').read_text()
        self.assertNotRegex(shim, r'(?m)^\s*(bl|blr|ret)\s')
        self.assertNotIn('_DrawEarlyMarkers', shim)
        body = shim[shim.index('_Start:'):shim.index('_Dead:')]
        self.assertNotRegex(body, r'(?m)^\s*(mov|ldr|ldp|add|adr)\s+x0\b')
        self.assertIn('#ifdef FUJISAN_BRINGUP', body)
        script = (ROOT / 'build.sh').read_text()
        self.assertIn('local BOOTSHIM_CPPFLAGS=""', script)
        self.assertIn('EXTRA_CPPFLAGS="${BOOTSHIM_CPPFLAGS:-}"', script)

    def test_no_unsupported_fv_payloads(self):
        fdf = (ROOT / 'Platform/Qualcomm/msm8996/msm8996.fdf').read_text()
        active = '\n'.join(l for l in fdf.splitlines() if not l.lstrip().startswith('#'))
        self.assertIn('INF src/main/SimpleInitMain.inf', active)
        self.assertIn('FujisanDisplayDxe.inf', active)
        for forbidden in ('SimpleFbDxe.inf', 'AcpiPlatformDxe', 'PlatformSmbios',
                          'UsbBusDxe', 'PartitionDxe', 'Shell.inf', 'KernelFdtDxe', 'Reboot2Payload'):
            self.assertNotIn(forbidden, active)
        apriori = (ROOT / 'Platform/Qualcomm/msm8996/Apriori.fdf.inc').read_text()
        for inf in re.findall(r'^\s*INF\s+(\S+)', apriori, re.M):
            self.assertIn('INF '+inf, active)
        self.assertIn('timeout = -1', (ROOT / 'Platform/ZTE/msm8996/simpleinit.static.uefi.cfg').read_text())

    def test_captured_primary_ctl_encoding(self):
        # Actual B-image pstore: CTL0=0x00021F20; CTL1..4=0x00001F00.
        # Kernel mdss.h has NO_INTF=0, INTF0=1, INTF1=2, INTF2=3.
        source = DISPLAY.read_text()
        code = int(re.search(r'#define CTL_INTF_DSI0\s+(\d+)U', source)[1])
        self.assertEqual(code, 2)
        self.assertIn('((Top >> 4) & 0xF) == CTL_INTF_DSI0', source)
        def matches(top):
            return ((top >> 4) & 15) == code and (top & 15) == 0
        captured = [0x00021F20] + [0x00001F00] * 4
        self.assertEqual([i for i, top in enumerate(captured) if matches(top)], [0])
        self.assertTrue(matches(0x00001F20))  # same primary, video mode
        for rejected in (0x00021F10, 0x00021F30, 0x00021F40, 0x00021F23, 0):
            self.assertFalse(matches(rejected), hex(rejected))
        # Wrong selector from revision B must reject the captured primary.
        self.assertNotEqual((captured[0] >> 4) & 15, 1)

    def test_required_capsule_arch_protocol(self):
        fdf = (ROOT / 'Platform/Qualcomm/msm8996/msm8996.fdf').read_text()
        self.assertIn('INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf', fdf)
        common = (ROOT / 'Silicon/Qualcomm/QcomPkg/QcomCommonDsc.inc').read_text()
        self.assertIn('CapsuleLib|MdeModulePkg/Library/DxeCapsuleLibNull/DxeCapsuleLibNull.inf', common)
        core = (ROOT / 'Common/edk2/MdeModulePkg/Core/Dxe/DxeMain/DxeProtocolNotify.c').read_text()
        self.assertIn('&gEfiCapsuleArchProtocolGuid', core)

    def test_current_el_log_encoding(self):
        source = (ROOT / 'Silicon/Qualcomm/msm8996/Library/PlatformPrePiLib/PlatformUtils.c').read_text()
        self.assertIn('(UINT32)(ArmReadCurrentEL() >> 2)', source)
        self.assertEqual(4 >> 2, 1)

    def test_optional_logo_null_guard(self):
        source = (ROOT / 'GPLDrivers/Library/SimpleInit/src/gui/interface/core/splash.c').read_text()
        # Bind the truth-table model to the actual source guard, then ensure
        # the failure branch returns before the protocol method is called.
        guard = re.search(r'if\((EFI_ERROR\(st\)\|\|!plogo)\)\{(.*?)\n\t\}', source, re.S)
        self.assertIsNotNone(guard)
        self.assertIn('return false;', guard[2])
        self.assertLess(guard.end(), source.index('st=plogo->GetImage'))
        self.assertNotIn('EFI_ERROR(st)&&plogo', source)
        for error, pointer, should_call in ((True, False, False),
                                            (True, True, False),
                                            (False, False, False),
                                            (False, True, True)):
            self.assertEqual(not (error or not pointer), should_call)
        # If logo loading declines, gui_splash_draw still constructs text.
        self.assertLess(source.index('load_splash(img);'), source.index('text=lv_label_create(scr);'))

    def test_arch_timer_interrupt_ids(self):
        dsc = (ROOT / 'Platform/Qualcomm/msm8996/msm8996.dsc').read_text()
        self.assertIn('USE_PHYSICAL_TIMER      = TRUE', dsc)
        # Captured local msm8996.dtsi: PPI 13,14,11,10; irq-gic-v3 adds 16.
        for name, ppi in (('Sec', 13), ('', 14), ('Virt', 11), ('Hyp', 10)):
            pcd = 'PcdArmArchTimer'+name+'IntrNum'
            value = int(re.search(pcd+r'\|(\d+)', dsc)[1])
            self.assertEqual(value, 16 + ppi, pcd)
        # A counter advancing without INTID 30 enabled is insufficient for
        # the callback-only display path; D compiled the physical IRQ as 18.
        driver = DISPLAY.read_text()
        self.assertIn('EVT_TIMER | EVT_NOTIFY_SIGNAL', driver)
        self.assertIn('first timer callback', driver)
        self.assertIn('first BLT copied', driver)
        self.assertIn('first present submitted', driver)

    def test_gui_flush_status_observable(self):
        source = (ROOT / 'GPLDrivers/Library/SimpleInit/src/gui/drivers/uefi_gop.c').read_text()
        flush = source[source.index('static void uefigop_flush'):source.index('static int uefigop_get_modes')]
        self.assertIn('EFI_STATUS st=gop->Blt(', flush)
        self.assertIn('if(EFI_ERROR(st))', flush)
        self.assertIn('GOP flush failed:', flush)
        self.assertIn('first GOP flush accepted:', flush)
        self.assertIn('lv_disp_flush_ready(disp_drv);', flush)

    def test_keys_mapping_and_observer_bounds(self):
        keys = (ROOT / 'Platform/ZTE/msm8996/Drivers/FujisanKeysDxe/FujisanKeysDxe.c').read_text()
        self.assertIn('{0xC108, SCAN_DOWN, CHAR_NULL, TRUE', keys)
        self.assertIn('{0xC208, SCAN_UP, CHAR_NULL, TRUE', keys)
        self.assertIn('{0x0810, SCAN_NULL, CHAR_CARRIAGE_RETURN, FALSE', keys)
        # Bounded v2 channel table, SID0 PPIDs. No assumed APID numbers.
        for apid in range(256):
            self.assertLess(0x800 + apid*4, 0xC00)
            self.assertLess(0x04C00000 + apid*0x8000 + 0x1C, 0x05400000)
        for address in (0xC108, 0xC208, 0x0810):
            ppid = address >> 8
            map_word = (ppid << 8) | 1
            self.assertEqual((map_word >> 8) & 0xFFF, ppid)
            command = (1 << 27) | ((address & 255) << 4)
            self.assertEqual(command >> 27, 1)  # EXT_READL, never PMIC write
            self.assertEqual(command & 7, 0)     # one byte
        writes = re.findall(r'MmioWrite32\(([^;]+);', keys)
        self.assertEqual(writes, ['Button->Channel, EXT_READL | ((UINT32)Offset << 4))'])
        self.assertIn('Timeout < READ_TIMEOUT_US', keys)
        self.assertIn('Version != 0x20010000', keys)
        self.assertIn('PressedCount > 1', keys)

    def test_keys_debounce_model(self):
        # Python model of Poll's debounce, not execution of the C driver.
        def events(initial, samples):
            stable = candidate = initial
            armed = False
            count = 0
            out = 0
            for pressed in samples:
                if pressed != candidate:
                    candidate, count = pressed, 1
                elif count < 3:
                    count += 1
                if count == 3:
                    if not candidate:
                        armed = True
                    elif not stable and armed:
                        out += 1
                        armed = False
                    stable = candidate
            return out
        self.assertEqual(events(False, [False]*3 + [True]*30), 1)
        self.assertEqual(events(True, [True]*30), 0)  # boot key held
        self.assertEqual(events(True, [False]*3 + [True]*3), 1)
        self.assertEqual(events(False, [False]*3 + [True,False,True,False]), 0)
        self.assertEqual(events(False, [False]*3 + [True]*3 + [False]*3 + [True]*3), 2)

    def test_keys_integration(self):
        path = 'Platform/ZTE/msm8996/Drivers/FujisanKeysDxe/FujisanKeysDxe.inf'
        for name in ('msm8996.dsc','msm8996.fdf','Apriori.fdf.inc'):
            self.assertIn(path, (ROOT/'Platform/Qualcomm/msm8996'/name).read_text())
        bds = (ROOT/'Platform/ZTE/msm8996/Library/FujisanBootManagerLib/FujisanBootManagerLib.c').read_text()
        self.assertIn('EfiBootManagerUpdateConsoleVariable(ConIn, Path, NULL)', bds)
        keys = (ROOT/path).with_suffix('.c').read_text()
        self.assertIn('&gEfiSimpleTextInProtocolGuid, &mInput', keys)
        self.assertIn('mInput.ReadKeyStroke = ReadKey', keys)
        self.assertIn('mStopped = TRUE;', keys)

    def test_fujisan_dpi_matches_android(self):
        dsc = (ROOT / "Platform/Qualcomm/msm8996/msm8996.dsc").read_text()
        self.assertIn("gSimpleInitTokenSpaceGuid.PcdGuiDefaultDPI|440", dsc)
        # SimpleInit's threshold table selects 64 px for 451..550 DPI.
        dpi = 440
        font_sizes = [8, 16, 24, 32, 48, 64, 72]
        index = sum(dpi > threshold for threshold in (50, 150, 250, 350, 450, 550))
        self.assertEqual(font_sizes[index], 48)
        self.assertNotIn("PcdGuiDefaultDPI|480", dsc)
        self.assertNotIn("PcdGuiDefaultDPI|200", dsc)

    def test_touch_qup_and_rmi4_scope(self):
        touch = (ROOT / "Platform/ZTE/msm8996/Drivers/FujisanTouchDxe/FujisanTouchDxe.c").read_text()
        self.assertIn("#define QUP_BASE               0x075BA000U", touch)
        self.assertIn("#define RMI_I2C_ADDRESS         0x20U", touch)
        self.assertIn("#define RMI_FUNCTION_F12         0x12U", touch)
        self.assertIn("RMI_MAX_PAGES            4U", touch)
        self.assertIn("QUP_CLK_CTL_400KHZ      0x00150015U", touch)
        # The only transport writes target QUP registers. No touch reset,
        # PMIC, TLMM or second-panel address appears in the driver.
        for forbidden in ("0x075B9000", "0x075B5000", "0x09830000",
                          "ControllerReset", "ConfigGpio", "GpioOut", "SPMI"):
            self.assertNotIn(forbidden, touch)
        writes = re.findall(r"MmioWrite32\(QUP_BASE \+ ([^,]+),", touch)
        self.assertTrue(writes)
        allowed = {"QUP_STATE", "QUP_SW_RESET", "QUP_CONFIG", "QUP_ERROR_FLAGS",
                   "QUP_ERROR_FLAGS_EN", "QUP_OPERATIONAL", "QUP_OPERATIONAL_MASK",
                   "QUP_IO_MODE", "QUP_MX_WRITE_CNT", "QUP_MX_READ_CNT",
                   "QUP_I2C_CLK_CTL", "QUP_I2C_MASTER_CONFIG", "QUP_I2C_STATUS",
                   "QUP_OUT_FIFO_BASE"}
        self.assertTrue(set(writes).issubset(allowed), set(writes) - allowed)
        self.assertIn("FindF12()", touch)
        self.assertIn("&gEfiAbsolutePointerProtocolGuid, &mTouch.Protocol", touch)
        self.assertIn("TOUCH_FAILURE_LIMIT", touch)
        self.assertIn("FJ TOUCH 20260909-T7", touch)
        self.assertIn("QUP_TAG2_DATA_READ_END", touch)
        self.assertIn("QUP_I2C_MASTER_CONFIG, 1", touch)
        self.assertIn("GCC_AHB_BLSP2_BIT      BIT15", touch)
        self.assertIn("GCC_QUP6_I2C_CMD       0x030020U", touch)
        self.assertIn("GCC_QUP6_I2C_CBCR      0x030008U", touch)
        self.assertIn("TLMM_I2C12_SDA          87U", touch)
        self.assertIn("TLMM_I2C12_SCL          88U", touch)
        self.assertIn("TLMM_I2C12_FUNCTION     3U", touch)
        self.assertIn("ConfigureI2c12Pins()", touch)
        self.assertEqual(touch.count("Status = ConfigureI2c12Pins();"), 1)
        self.assertEqual(touch.count("Status = EnableQupClocks();"), 1)
        mmap = (ROOT / "Silicon/Qualcomm/msm8996/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.c").read_text()
        self.assertIn('{"GCC Clock Control", 0x00300000, 0x00090000', mmap)
        self.assertIn("EnableQupClocks()", touch)

    def test_touch_latency_policy(self):
        driver = (ROOT / "Platform/ZTE/msm8996/Drivers/FujisanTouchDxe/FujisanTouchDxe.c").read_text()
        self.assertIn("TOUCH_POLL_100NS         100000ULL", driver)
        self.assertNotIn("TOUCH_DEBOUNCE_SAMPLES", driver)
        self.assertNotIn("CandidateSamples", driver)
        self.assertIn("Pressed = Data[0] != 0;", driver)
        self.assertIn("EFI_ABSP_TouchActive", driver)
        self.assertIn("FJ TOUCH event down", driver)
        self.assertIn("FJ TOUCH event up", driver)

    def test_touch_i2c_packet_models(self):
        # Fujisan uses qcom,i2c-msm-v2, whose FIFO uses byte tags, not the
        # older QUP word tags. A RMI read is one combined write/read transfer.
        addr = 0x20
        register = 0xE9
        read_len = 6
        tx = bytes([0x81, addr << 1, 0x82, 1, register,
                    0x81, (addr << 1) | 1, 0x87, read_len])
        self.assertEqual(tx, bytes([0x81, 0x40, 0x82, 1, 0xE9,
                                    0x81, 0x41, 0x87, 6]))
        self.assertEqual(len(tx), 9)
        self.assertEqual(read_len + 2, 8)  # v2 INPUT_EOT overhead + payload
        # DT high/fs divider fields occupy bits 16..23 and 0..7.
        self.assertEqual(0x00150015, (0x15 << 16) | 0x15)
        descriptor = bytes([0x10, 0x20, 0x30, 0x40, 0x01, 0x12])
        self.assertEqual(descriptor[5], 0x12)
        self.assertEqual(descriptor[3], 0x40)

    def test_touch_absolute_pointer_mode_pointer(self):
        source = (ROOT / "Platform/ZTE/msm8996/Drivers/FujisanTouchDxe/FujisanTouchDxe.c").read_text()
        self.assertIn("mTouch.Protocol.Mode = &mTouch.Mode;", source)
        self.assertLess(source.index("mTouch.Protocol.Mode = &mTouch.Mode;"), source.index("&gEfiAbsolutePointerProtocolGuid, &mTouch.Protocol"))
        # The consumer dereferences Mode before its first GetState call.
        consumer = (ROOT / "GPLDrivers/Library/SimpleInit/src/gui/drivers/uefi_touch.c").read_text()
        self.assertIn("data->touch->Mode->AbsoluteMaxX", consumer)
        self.assertIn("data->touch->Mode->AbsoluteMaxY", consumer)
        self.assertIn("if(!touch||!touch->Mode||", consumer)

    def test_touch_simpleinit_release_consumption(self):
        source = (ROOT / "GPLDrivers/Library/SimpleInit/src/gui/drivers/uefi_touch.c").read_text()
        self.assertIn("data->state=touch_down?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;", source)
        self.assertIn("touch_down=(p.ActiveButtons&EFI_ABSP_TouchActive)!=0;", source)
        self.assertIn("data->continue_reading=false;", source)
        self.assertNotIn("data->continue_reading=(p.ActiveButtons!=0);", source)
        self.assertNotIn("if(p.CurrentX==d->lx&&p.CurrentY==d->ly)continue;", source)

    def test_touch_integration(self):
        path = "Platform/ZTE/msm8996/Drivers/FujisanTouchDxe/FujisanTouchDxe.inf"
        for name in ("msm8996.dsc", "msm8996.fdf", "Apriori.fdf.inc"):
            self.assertIn(path, (ROOT / "Platform/Qualcomm/msm8996" / name).read_text())
        mmap = (ROOT / "Silicon/Qualcomm/msm8996/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.c").read_text()
        self.assertIn("{\"QUP I2C12\",        0x075BA000, 0x00001000", mmap)

    def test_display_bounds(self):
        self.assertTrue(reserved_display(0x80400000, 4352 * 1920))
        self.assertTrue(reserved_display(0x83401000, 3240 * 1920))
        self.assertTrue(reserved_display(0x80404000, 4352 * 1920))
        self.assertFalse(reserved_display(0x803FFFFF, 1))
        self.assertFalse(reserved_display(0x80BFFFFF, 2))
        self.assertFalse(reserved_display(0xBFC00000, 4096))
        self.assertFalse(reserved_display(0xCE000000, 4096))
        self.assertFalse(reserved_display(0xFFFFF000, 0x2000))

    def test_unpack_model(self):
        # MDP C0=G, C1=B, C2=R, C3=A. Convert canonical GOP BGRA.
        channels = [0x22, 0x33, 0x11, 0xFF]
        for pattern, bpp, expected in ((0x00010002, 3, b'\x11\x22\x33'),
                                       (0x03010002, 4, b'\x11\x22\x33\xff'),
                                       (0x03020001, 4, b'\x33\x22\x11\xff')):
            got = bytes(channels[(pattern >> (8*i)) & 255] for i in range(bpp))
            self.assertEqual(got, expected)

    def test_pstore_append_model(self):
        capacity = 0x8000 - 12
        data = bytearray(capacity)
        trace = b'====0.0\nFJ-UEFI-20260908-E BOOTSHIM\n'
        data[:len(trace)] = trace
        size = len(trace)
        # Simulate many independent statically-linked module instances.
        for _ in range(80):
            start = size % capacity
            self.assertEqual(start, size)
            text = b'FJ module\n'
            data[size:size+len(text)] = text
            size += len(text)
        self.assertEqual(bytes(data[:size]), trace + b'FJ module\n' * 80)
        full = b'\nFJ-TRACE-FULL (later messages dropped)\n'
        while size < capacity - (len(full) + 1):
            data[size] = 0x58
            size += 1
        data[size:size+len(full)] = full
        size += len(full)
        self.assertLess(size, capacity)
        self.assertTrue(data[:size].startswith(trace))
        self.assertTrue(data[:size].endswith(full))
        source = (ROOT / 'Silicon/Qualcomm/QcomPkg/Library/FrameBufferSerialPortLib/FrameBufferSerialPortLib.c').read_text()
        self.assertNotIn('SetMem((VOID *)(UINTN)Address, Size, 0)', source)
        self.assertIn('mUefiPstoreHeader->Start !=', source)
        self.assertIn('FJ-TRACE-FULL', source)


def metadata_check():
    """Use EDK2's parsers/library resolver; no AutoGen/build/assembler calls."""
    sys.path.insert(0, str(ROOT / 'Common/edk2/BaseTools/Source/Python'))
    from Common import GlobalData, EdkLogger
    from Common.MultipleWorkspace import MultipleWorkspace
    from Common.Misc import PathClass
    from Workspace.WorkspaceDatabase import WorkspaceDatabase
    from Workspace.WorkspaceCommon import GetModuleLibInstances
    from GenFds.FdfParser import FdfParser
    EdkLogger.Initialize()
    GlobalData.gWorkspace = str(ROOT / 'workspace')
    GlobalData.gGlobalDefines.update(WORKSPACE=GlobalData.gWorkspace, TARGET='DEBUG',
                                     TOOL_CHAIN_TAG='CLANG38', ARCH='AARCH64')
    GlobalData.gCommandLineDefines.update(FIRMWARE_VER='STATIC-CHECK', USE_UART='0',
        FIX_CLANG='1', NO_EXCEPTION_DISPLAY='FALSE', FD_BASE='0xCE000000',
        FD_SIZE='0x00800000', ENABLE_LINUX_UTILS='0')
    pkgs = [ROOT/'Common/edk2', ROOT/'Common/edk2-platforms',
            ROOT/'GPLDrivers/Library/SimpleInit', ROOT, ROOT/'GPLDrivers']
    os.environ['WORKSPACE'] = GlobalData.gWorkspace
    os.environ['PACKAGES_PATH'] = ':'.join(map(str, pkgs))
    MultipleWorkspace.setWs(GlobalData.gWorkspace, os.environ['PACKAGES_PATH'])
    db = WorkspaceDatabase()
    platform = db.BuildObject[PathClass(str(ROOT/'Platform/ZTE/msm8996/fujisan.dsc')),
                              'AARCH64', 'DEBUG', 'CLANG38']
    platform.OverrideDuplicateModule()
    modules = platform.Modules
    for path, override in modules.items():
        if any(x in str(path) for x in ('FujisanDisplay', 'FujisanKeys', 'FujisanTouch', 'SimpleInitMain', '/BdsDxe/', '/PrePi/', '/CapsuleRuntimeDxe/', '/TimerDxe/')):
            module = db.BuildObject[path, 'AARCH64', 'DEBUG', 'CLANG38']
            libs = GetModuleLibInstances(module, platform, db.BuildObject,
                                         'AARCH64', 'DEBUG', 'CLANG38', str(path), EdkLogger)
            if '/TimerDxe/' in str(path):
                assert any('ArmGenericTimerPhyCounterLib' in str(lib.MetaFile) for lib in libs)
                for suffix, expected in (('Sec', '29'), ('', '30'), ('Virt', '27'), ('Hyp', '26')):
                    value = platform.Pcds['PcdArmArchTimer'+suffix+'IntrNum', 'gArmTokenSpaceGuid'].DefaultValue
                    assert int(value, 0) == int(expected), (suffix, value)
            if 'SimpleInitMain' in str(path):
                assert override.Pcds['PcdDebugPrintErrorLevel', 'gEfiMdePkgTokenSpaceGuid'].DefaultValue == '0x80000041'
            if '/BdsDxe/' in str(path):
                assert any('FujisanBootManagerLib' in str(lib.MetaFile) for lib in libs)
                assert not any('MsPlatformDevicesLib' in str(lib.MetaFile) for lib in libs)
            if '/CapsuleRuntimeDxe/' in str(path):
                assert any('DxeCapsuleLibNull' in str(lib.MetaFile) for lib in libs)
            print(f'Metadata OK: {module.BaseName}, {len(libs)} resolved libraries')
    fdf = FdfParser(str(ROOT/'Platform/Qualcomm/msm8996/msm8996.fdf'))
    fdf.ParseFile()
    print('FDF parsed; no AutoGen/compiler/assembler/image generation invoked.')


if __name__ == '__main__':
    sys.dont_write_bytecode = True
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(PreBuildTests))
    if not result.wasSuccessful():
        sys.exit(1)
    metadata_check()
