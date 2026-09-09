# Fujisan: UEFI -> primary-panel GOP -> SimpleInit

## Pending keys bring-up (2026-09-09, K1 — not compiled or committed)

The accepted display baseline is committed/pushed as `611424035f9fe2ce90959d36696b5f72d9319e94`
on `Anya1014CN/edk2-msm:msm8996pro`. Its SimpleInit gitlink is
`c852dd05971d7f278b5ca315dfde107d71e80f55`, already pushed to the owner's fork.
This separate working-tree change adds physical keys without changing that
published baseline or modifying the now-clean SimpleInit submodule.

`FujisanKeysDxe` implements SimpleTextIn, which SimpleInit already enumerates.
The platform BDS adds physical input device paths to ConIn without assuming an
input handle exists. Polling uses the E-validated architectural timer (10 ms).
There is no USB keyboard, touch, storage or PMIC long-press reset modification.

### Hardware evidence and read-only scope

The running Android FDT and kernel sources agree:

* SID0 PM8994 GPIO2 (`0xC108`, STATUS1 bit0, active-low) emits Linux key **114**
  (volume down), despite its node label `vol_up`.
* SID0 GPIO3 (`0xC208`, STATUS1 bit0, active-low) emits Linux key **115**
  (volume up), despite its node label `vol_down`.
* SID0 PON (`0x0810`, RT_STS bit0 asserted on press) emits key **116** (power).
* Android dmesg reports SPMI arbiter **v2, 0x20010000**; DT uses EE0,
  core `0x0400F000` size `0xC00`, observer `0x04C00000` size `0x800000`.

References in the local kernel are `msm8996.dtsi`, `msm-pm8994.dtsi`,
`msm8996-mtp-fujisan.dtsi`, `drivers/spmi/spmi-pmic-arb.c`,
`drivers/gpio/qpnp-pin.c`, and `drivers/input/misc/qpnp-power-on.c`.
Runtime evidence is saved at
`/Users/anya1014/Documents/ZTE_Z999/_uefi_diagnosis_20260908_keys/`.

The driver finds APIDs using the v2 mapping table, not hard-coded channel IDs.
It supports exactly the observed v2 version, uses EE0 observer channels, and
writes **only read-command descriptors to the controller's CMD register**.
It never issues a PMIC register-write opcode, changes GPIO direction/pull/enable,
or touches PON long-press/reset settings. Each transaction waits at most 100 us;
a read failure stops polling without synthesizing a key. GPIO type, input mode
and enabled status must match the inherited configuration, otherwise the driver
logs and declines installation. If LK left the GPIO disabled this read-only
version needs another evidence-based iteration, not an assumed initialization.

Three stable samples debounce a key. Boot/reset-held keys must be released first;
there is one event per press, no autorepeat, a 16-entry queue, and simultaneous
key chords are ignored until release. GPIO3 maps to SCAN_UP, GPIO2 to SCAN_DOWN;
power maps to carriage return. The SimpleInit UI translates those into navigation
and confirmation. Physical direction/response still needs owner verification.

### Build/test handoff

Seventeen source/model tests and EDK2 metadata/library resolution pass; these do
not compile or execute the driver. No new image was built or flashed, and these
key changes are intentionally **not committed/pushed** pending hardware testing.
The E BootShim/PrePi identity is unchanged; K1 is identified by
`FJ KEYS 20260909-K1` and `FJ KEYS SimpleTextIn installed` in pstore.

Build from `/home/anya1014/edk2-msm` with the existing command:

```sh
./build.sh -d fujisan -r DEBUG
```

Keep the validated recovery-target signing/entry method. Release the boot-entry
keys before menu testing. Within the known 10-second interval, briefly press
volume up/down and check focus movement; check whether moving focus cancels the
countdown. Use short power presses to confirm a harmless UI action. Long power
press still belongs to the unchanged PMIC reset behavior. No Android loader is
implemented by this patch; Continue Boot is not an Android handoff guarantee.

If input is absent, capture the complete pstore after returning to Android:

```sh
adb exec-out su -c 'cat /sys/fs/pstore/dmesg-ramoops-0' > fujisan-uefi-next-pstore.txt
```

Look for arbiter/version, APID mapping, GPIO mode and initial state, protocol
installation, and bounded `FJ KEYS press` traces. Do not commit this K1 patch
until the owner confirms actual menu navigation and power confirmation.

## Validated display baseline (2026-09-08, revision E)

The device owner confirms that revision E **visibly displays the UI** on the
physical screen and selects Continue Boot automatically after 10 seconds.
This is the accepted display baseline, not just a successful drawing log.
The owner authorizes committing/pushing all current bring-up changes.

The SimpleInit fixes are published in the owner's fork
`Anya1014CN/simple-init`, branch `fujisan-display-bringup`, commit
`c852dd05971d7f278b5ca315dfde107d71e80f55`. The parent `.gitmodules` URL points
to that fork and its gitlink pins exactly that commit. No floating submodule
branch or uncommitted submodule patch is required to reproduce this baseline.

Volume and power keys do **not** work yet. They are the next independent
bring-up step and are not part of the validated display commit. No Android
loader, physical storage, USB or second-panel work is implied by this result.
The visible logo bitmap is not independently certified by this confirmation;
the acceptance is visible UI and the observed 10-second Continue Boot behavior.

The sections below record earlier test revisions, including their then-current
uncompiled/uncommitted status; those statements are historical, not the status
of this accepted E baseline.

## Revision E investigation history


The owner tested D: the NULL-logo exception is gone, splash task handling and
boot-menu draw returned, then SimpleInit executed Continue Boot. Per the owner,
treat this as timeout selection for now; do not investigate or change it in E.
The physical screen stayed on the bootloader picture. No `first present
submitted` message appeared. Rendering return codes do not establish visibility.

E corrects a concrete display-refresh dependency: the platform overrides
`PcdArmArchTimerSecIntrNum=17` and `PcdArmArchTimerIntrNum=18` were wrong. The
**tested D TimerDxe AutoGen.h** confirms those values reached the binary.
`msm8996.dtsi` specifies GIC_PPI 13/14/11/10; `irq-gic-v3.c` converts PPI to
absolute INTID with +16. This EDK2 TimerDxe passes its PCD directly to the GIC
registration routine (no offset added). With the selected physical counter
library using CNTP at non-secure EL1, the required interrupt is **30**, not 18.
E explicitly sets secure/physical/virtual/hyp IDs to **29/30/27/26**.

This is consistent with a GUI whose software timers advance via
`custom_tick_get()` -> `GetPerformanceCounter()` while the UEFI event callback
that submits command-mode frames never runs. D did not instrument timer entry
or BLT, so the missing log alone was not proof. E adds bounded first-call logs
at each relevant point instead of declaring the screen fixed:

* SimpleInit reports the selected GOP mode and first accepted/failed GOP flush.
* FujisanDisplayDxe reports the first BLT copied to the inherited scanout RAM.
* Its first timer callback is logged even when no frame is dirty.
* Existing first-present logging and bounded busy diagnostics distinguish a
  submitted kickoff from a pending CTL. None proves panel visibility by itself.

The periodic 30 Hz refresh method, MDP writes, physical framebuffer, pixel
conversion, Logo handling, FDF payload set, memory placement and signing are
unchanged. No fallback guessed framebuffer, panel reset, second display or
Continue Boot/config-store work is added. E targets the shared output path for
all UI drawing; it does not add a platform-logo provider or promise a logo
bitmap. The previously fixed optional-logo path still allows text/menu drawing.

**E is uncompiled and uncommitted.** Fourteen source/model pre-build checks pass;
EDK2 metadata resolution additionally verifies TimerDxe uses the physical timer
library and the corrected INTIDs. Existing D image/FD/BootShim hashes are
unchanged. New trace identity: **`FJ-UEFI-20260908-E`**. Keep the dirty SimpleInit
submodule (now also `src/gui/drivers/uefi_gop.c`) when the owner builds/tests.
Pre-edit backup: `/home/anya1014/uefi-backup-20260908-round4/`.
Host evidence: `/Users/anya1014/Documents/ZTE_Z999/_uefi_diagnosis_20260908_round4/`.

## Revision D history


The owner tested revision C (`Sep 8 2026 22:29:43`). The captured trace confirms:

* DSI0 CTL0 selection and all current display handoff guards passed.
* Active VIG0: address `0x83401000`, stride **3240 bytes**, 1080x1920,
  **24-bpp**, format `0x0002243F`, unpack `0x00020001` (B,G,R bytes),
  command mode, no inherited autorefresh.
* GOP installed, BDS connected it, and `SimpleInitMain` was started.
* SimpleInit crashed before the first splash task handler, not in GOP discovery.

Using the matching existing `SimpleInitMain.debug`, LLVM addr2line maps image
RVA `0x21AC` to `draw_edk2_logo()` at `src/gui/interface/core/splash.c:47`,
inlined into `UefiMain`. The instruction is `ldr x9, [x8]`, at the optional
PlatformLogo protocol method lookup. `LocateProtocol` returns an error/null
when no provider is installed; the existing guard `EFI_ERROR(st) && plogo`
fails to catch that case and dereferences NULL.

Revision D changes that guard to `EFI_ERROR(st) || !plogo`, allowing the existing
text splash to continue without a logo. It adds INFO/NOTICE checkpoints for
missing optional logo, splash task completion and boot-menu draw completion.
No LogoDxe is added. The display driver, framebuffer conversion/kickoff logic,
FDF, FD/memory layout and image signing are unchanged from C. The display DXE's
internal `20260908-C, DSI0 selector=2` label is intentionally unchanged; the new
BootShim/PrePi trace identity is **`FJ-UEFI-20260908-D`**.

**D has not been compiled or tested.** Twelve pre-build checks and metadata
resolution pass, including a source-bound truth-table model for the NULL guard.
The checked-in code is not executed by these model checks. Existing C image,
FD, BootShim binary and symbol-file hashes remain unchanged. Stop before build;
commit only after the owner confirms visible SimpleInit on hardware.

Two directly edited files are inside the SimpleInit Git submodule:
`src/gui/interface/core/splash.c` and `src/main/uefimain.c`. They remain dirty
and uncommitted there; do not reset/reinitialize the submodule before testing.
A parent-repository diff alone does not capture these changes. The original C
symbol file and pre-edit snapshot are preserved outside the repository under
`/home/anya1014/uefi-backup-20260908-round3/`. Host evidence and symbolization:
`/Users/anya1014/Documents/ZTE_Z999/_uefi_diagnosis_20260908_round3/`.

## Revision C history


The owner compiled and tested revision B (`FJ-UEFI-20260908-B`), firmware build
stamp `Sep 8 2026 22:15:16`. Its pstore confirms BootShim -> PrePi -> display DXE
-> BDS. The picture remained unchanged because GOP validation rejected DSI0;
BDS therefore deliberately did not launch SimpleInit.

Revision C is the next **uncompiled, uncommitted** source update. Stop before
compilation; the owner builds and tests. Commit only after visible SimpleInit
is confirmed on hardware. Revision B's execution does not prove C or GOP works.

### Revision B failure and targeted C fix

The captured primary controller is `CTL0 top=00021F20 lm0=00000001`;
other CTLs have `top=00001F00`. `mdss.h` starts its interface enum with
`MDSS_MDP_NO_INTF=0`, so `MDSS_MDP_INTF1` (DSI0) is encoded as **2**, not 1.
The previous comparison incorrectly accepted eDP/INTF0's encoding and rejected
the actual primary. C compares the field against named constant
`CTL_INTF_DSI0=2`; DSI1 remains excluded. No topology/format/address safety
checks are relaxed. B rejected before SSPP capture, so the actual surface
format and address still need the next test; C includes a bounded read-only
pipe snapshot before later topology guards.

B also logged `Capsule Arch Protocol not present!!` and a DXE-core assertion.
The FDF now restores `CapsuleRuntimeDxe`, required by this checkout's DXE
architecture-protocol checklist. It uses the existing `DxeCapsuleLibNull`
backend; this is not a capsule firmware-update feature or a storage driver.

The former `PREPI EL4` text was a logging error: `ArmReadCurrentEL()` returns
raw `CurrentEL` (`4` means EL1). C shifts it by two before formatting.

C leaves FD/heap placement, boot image signing, and the tested relocation path
unchanged (apart from the trace version string). Eleven pre-build checks now
include the captured CTL value and negative eDP/DSI1/WB cases, Capsule presence,
and EL log decoding. No compilation or device reboot was performed this round.

No Android/recovery loader, physical storage controller, USB bring-up, buttons,
ACPI/Windows, CPU performance programming, or second-panel initialization is
included in this FV. The existing recovery-target boot image signing/packaging
hook is unchanged. Its output is still named `boot-fujisan.img`; the name does
not make it an Android boot-partition image.

## Evidence and baseline problems

The Android device connected on 2026-09-08, with working `su`, exposed a
237-byte `dmesg-ramoops-0` beginning with `UEFI-PSTORE-BEGIN` and three
`InstallProtocolInterface` messages. This proves that **a previous UEFI build
reached DXE**, not that the currently edited BootShim/FV ran. That record has no
build identity. It does not prove GOP installation or visible UI.

Before this change the uncommitted BootShim `_DrawEarlyMarkers` called other
functions using `bl` without saving its incoming `x30`; its final `ret` returned
inside the marker function instead of resuming FV relocation. The shared file
also wrote guessed framebuffer addresses on every platform and destroyed x0.
The markers are removed, rather than patched to write more candidate buffers.

Other directly relevant issues found:

* The FDF did not include SimpleInit, so that image could not show its UI.
* Common BDS indexes `handles[1]` without validating the input handle array, then
  calls `PlatformSetup()`, whose copied MSM8998-style implementation dereferences
  absent Qualcomm protocols. This platform now has its own minimal BDS library.
* Each module's statically linked serial library cleared the same pstore record;
  the ring used a different meaning of `start` from Linux persistent RAM. Wrapping
  a dmesg record also destroys the required `====` timestamp header.
* The kernel's `cont_splash_mem` **reservation does not identify the active SSPP
  source buffer**. Command-mode panels can retain a bootloader picture without
  fetching new pixels until an MDP kickoff. A framebuffer write/cache flush alone
  is not a reliable execution indicator.

The original pre-edit diff/status is saved outside the repository:
`/home/anya1014/uefi-backup-20260908/before.patch` and `before.status`.
Revision C's pre-edit snapshot is under
`/home/anya1014/uefi-backup-20260908-round2/`, including revision B image hashes
and the untracked display/BDS/test files. The revision B runtime capture is in
`/Users/anya1014/Documents/ZTE_Z999/_uefi_diagnosis_20260908_round2/`.
Pre-existing generic `SimpleFbDxe` edits are retained, but that driver is no
longer included in the Fujisan FV.

## Source and hardware references

Host evidence directory:
`/Users/anya1014/Documents/ZTE_Z999/_uefi_diagnosis_20260908`.
The capture is read-only: pstore, dmesg, `/proc/iomem`, and the running FDT.
No device partitions were written and Android was not rebooted by this task.

The supplied `/Volumes/KernelRebase-CS/android/kernel/zte/msm8996` path was not
present on the host. The kernel tree actually available at
`/Volumes/KernelRebase-CS/android_kernel_zte_msm8996` matches the reference path
in the workspace adaptation document and was used read-only. Key files:

* `arch/arm/boot/dts/qcom/msm8996-mdss.dtsi`: MDSS `0x00900000`, block size
  `0x90000`, CTL, pipe and pingpong offsets.
* `drivers/video/fbdev/msm/mdss_mdp_hwio.h`: register offsets, CMD bit 17,
  unpack/channel definitions, security and decimation registers.
* `drivers/video/fbdev/msm/mdss.h`: interface enum with NO_INTF=0, INTF1=2.
* `drivers/video/fbdev/msm/mdss_mdp_ctl.c`: CTL INTF number at bits 4+, and
  pipe-to-layer shift mapping.
* `drivers/video/fbdev/msm/mdss_mdp_pipe.c`: SRC_FORMAT pixel size at bits 9:10,
  channel/unpack fields, linear/interleaved constraints.
* `drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c`: command kickoff via CTL_START,
  and existing pingpong autorefresh behavior.
* `fs/pstore/ram.c`, `ram_core.c`: persistent-RAM header and dmesg timestamp.

The running Android fb0 reports 2160x3840 virtual pixels / 8704-byte stride;
those Android dual-panel values are **not** used as bootloader scanout settings.

## Execution and memory

* BootShim writes `FJ-UEFI-20260908-D BOOTSHIM` without a stack or subroutine calls,
  preserves bootloader x0, and relocates the FV. Its trace is enabled only by the
  MSM8996 build configuration. `BOOTSHIM_CPPFLAGS` is local to each build call.
* FV/FD is now `0xCE000000..0xCE800000` (8 MiB). The previous 1 MiB XBL window
  was inadequate for assuming SimpleInit would fit, and cannot be expanded over
  the XBL framebuffer/stack. The new FD is explicitly mapped as BootServicesData
  with executable attributes, not free memory. `/proc/iomem` reports the selected
  interval as ordinary RAM. Its suitability still requires the next boot test.
* PrePi's allocation pool is limited to `0xA0000000..0xB0000000` (256 MiB),
  avoiding the observed `0xB1C00000` HDMI/display reservation. The larger high
  display interval through `0xBFC00000` and the entire ramoops interval through
  `0xC0000000` remain reserved. No second-screen buffer is allocated or painted.
* The MDSS aperture is mapped as device memory, not a misleading QTIMER alias.
  Timers continue to use the architectural counter.

## Primary-panel handoff GOP

`Drivers/FujisanDisplayDxe` is a platform-local replacement, not a change to
other devices' display policy. It reads the existing MDP state before writes.
It accepts only one DSI0/INTF1 controller using LM0 and a single full-screen
base-layer pipe, with a 1080x1920 unscaled linear interleaved RGB888/RGBA8888
surface. No shared pipe, extra mixers/layers, crop, rotation/flip, secure source,
decimation, tile/UBWC or wide-channel formats are guessed.

The active address, byte stride, bpp and channel order come from SSPP registers.
The entire surface must be contained in either reserved interval:

* XBL display: `0x80400000..0x80C00000`;
* primary continuous splash: `0x83401000..0x85800000`.

The driver does not choose an address based on which reservation name looks
likely. On an unsupported handoff it logs the relevant register values/reason,
returns `EFI_UNSUPPORTED`, and does **not** install GOP or write scanout/CTL.
BDS then reports that no validated GOP exists. Such a result needs the captured
registers for the next iteration, not speculative writes or panel reset code.

For an accepted handoff, GOP exposes `PixelBltOnly` over a private 32-bit shadow.
This avoids pretending a command-mode panel is a directly updated framebuffer.
Blt rectangles are converted into the inherited 24/32-bit pixel order, without
changing the panel/pipe/clock/SMMU configuration. Cache-cleaned writes mark a
frame dirty. A 30 Hz UEFI timer issues primary CTL_START only when needed and
not busy; inherited video mode or enabled autorefresh needs no manual kickoff.
There is no unbounded register polling. ExitBootServices stops this work.

`first present submitted` means a transfer was requested, **not** that the panel
has visibly changed. Only a photo/observation of the actual screen proves that.

## SimpleInit launch and diagnostics

The platform-local BDS connects discovered GOP handles safely, signals EndOfDxe,
loads the FV-resident `SimpleInitMain` GUID, and starts it directly without keys.
It never calls the copied platform ACPI/performance setup. The FV static config
sets `boot.timeout = -1`; no countdown to Android or another payload is intended.

Serial/debug output goes only to pstore on this platform, never to a guessed
framebuffer or over the GUI. BootShim alone starts a fresh first 32 KiB dmesg
record at `0xBFC00000`. Subsequent library instances attach to it. The record is
append-only so Linux can recognize its timestamp, and has an explicit full-log
marker rather than wrapping. Per-protocol DXE INFO spam is disabled; SimpleInit
has a component-specific INFO mask and logger level INFO for useful GUI traces.

Expected checkpoints:

1. `FJ-UEFI-20260908-D BOOTSHIM`
2. `FJ-UEFI-20260908-D PREPI`, then `PREPI platform ready`
3. `FJ DISPLAY DXE entry`, CTL/SSPP snapshot, validation or explicit rejection
4. `FJ GOP installed`, `FJ BDS before-console` / `after-console`
5. `FJ SIMPLEINIT LoadImage` / `StartImage`
6. SimpleInit application logs and a visible SimpleInit screen

An absent record does **not** prove no execution: a cold boot, kernel panic,
record clearing or another reboot can destroy/replace the trace. A
`NO-SHIM-RECORD` marker indicates the serial library could not attach to a valid
BootShim record; do not treat it as evidence of a fresh BootShim entry.

## Pre-build verification (no compilation)

From `/home/anya1014/edk2-msm`:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/tests/test_fujisan_bringup.py
git diff --check
bash -n build.sh configs/msm8996.conf
```

The Python checks validate non-overlap, FV sizing/mapping, heap/display/ramoops
exclusions, platform scoping, active FV contents, framebuffer range boundaries,
and Python models of pixel unpacking and pstore append semantics. They also run
EDK2's own DSC/FDF parsers and resolve the PrePi, display, BDS and SimpleInit
library graphs, plus CapsuleRuntimeDxe (including the component-specific DEBUG override). They do not
run AutoGen, a compiler, assembler, linker or image packager; they do not execute
or prove the C/assembly implementation. Hardware acceptance remains pending.

## Owner's next test

Build a fresh DEBUG image using the existing working toolchain, for example:

```sh
cd /home/anya1014/edk2-msm
./build.sh -d fujisan -r DEBUG
```

Do not use `-u`, `--no-exception-disp`, `-b`, `-A`, `-z` or `--distclean` for this
test. Keep the previously verified recovery-target packaging/entry method; do
not add partition or signature experiments to this display test. Retain the
build log, resulting image hash, and a photo of the screen. The older existing
`.img`, `.fd` and BootShim binary files were not rebuilt by this task.

If it stays at the bootloader picture, return to Android using the already
established recovery procedure and promptly capture the new pstore record:

```sh
adb exec-out su -c 'cat /sys/fs/pstore/dmesg-ramoops-0' > fujisan-uefi-next-pstore.txt
```

Return that **complete** record, even if no SimpleInit text is found. Avoid an
additional reboot before capture. Do not commit until the owner validates
visible SimpleInit on the actual primary panel.
