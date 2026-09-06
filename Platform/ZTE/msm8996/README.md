# ZTE Axon M / Fujisan MSM8996Pro platform

This is the Milestone 1 platform skeleton described in `EDK2-UEFI适配.md`.

- Device: ZTE Axon M (`fujisan`, P996A26)
- SoC: MSM8996Pro v1.1 / PMI8996
- Board ID: `102/0`
- Android baseline: non-A/B legacy boot image, `boot`=`mmcblk0p18`, `recovery`=`mmcblk0p19`
- Kernel reference: `/Volumes/KernelRebase-CS/android_kernel_zte_msm8996`

## Deliberate limitations

This milestone does **not** include an Android boot loader, eMMC access, buttons,
SimpleInit/GOP, or unverified Qualcomm binary DXEs. It is a platform/configuration
skeleton for the first build pass.

`PcdMipiFrameBufferAddress` is `0` by design until the original bootloader
framebuffer is measured. Do not flash or boot this image on hardware yet.

The memory map reserves the production ramoops interval `0xbfc00000-0xc0000000`
from the kernel DTS, but the firmware/heap/display placements remain provisional
and must be checked against bootloader logs before hardware bring-up.
