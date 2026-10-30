# J7 Prime GPU, performance, and audio status

## Performance candidate

`local-build/j7-ios16.7.16-20H392-d21ap/boot-performance.img` is derived from
the local `boot.img` bundle with `emmcfast=1` and without `-enable_kprintf_spam`. It
retains `cpus=8`, `j7gpu=7`, and all root/display settings. The J7 eMMC driver's
opt-in CMD18 multiblock path reads up to 32 sectors per request and falls back
to single-sector reads if it fails (`xnu/patches/0305`). Removing kprintf spam
reduces console/log overhead; it also makes boot output less detailed. This is
a candidate, not a measured speedup. `tools/j7-bootargs.py` makes the change
reproducible and recalculates the Android image digest. Keep `boot.img` as the
rollback image and compare boot/UI behavior on-device.

## GPU status

The J7 kernel already has a Mali-T830 path: identification and power stages,
WRITE_VALUE jobs, a job manager, and `J7MaliUserClient` (`0302`, `0306`–`0311`).
The current boot uses `j7gpu=7`, which exposes that client and runs its kernel
self-test. `tools/j7gpu/j7gputest.c` is a client/API exerciser, not an OpenGL or
Metal renderer. The root's `/usr/lib/sweetgl.dylib` is therefore not a graphics
driver, and the GPU is not yet connected to CoreAnimation/IOSurface presentation.
Real acceleration still requires a user-space rendering implementation and a
presentation/fence path, followed by on-device rendered-pixel validation.

## Audio hardware findings

The local Exynos 7870 Linux source identifies the J7 Prime codec as Samsung
COD3026X: `arch/arm64/boot/dts/exynos7870-on7xelte_swa_open_01.dts` describes an
I2C codec at address `0x04` on the S3C2440-compatible I2C controller at
`0x13870000` (IRQ 428), its LDO25 supply, GPA0.4 interrupt, and jack ADC input.
Playback/capture routes through I2S0 at `0x148a0000` (IRQ 69) and Exynos ADMA
at `0x14890000` (IRQ 73); Linux's clock IDs and DMA request numbers are also
described in `arch/arm64/boot/dts/exynos7870.dtsi`. The corresponding Linux
codec/machine drivers are in `sound/soc/codecs/cod3026*` and
`sound/soc/samsung/universal7870-cod3026*`. Those drivers provide register
sequences and routing references, but are Linux code and cannot be loaded by
XNU as-is.

There is no J7 XNU audio device/engine or iOS audio HAL in this tree yet. The
separate `~/hackios` tree has a CoreAudio server plug-in and an audio queue, but
its hardware backend targets a Qualcomm DSP and Awinic amplifiers, so that
backend cannot drive this Samsung codec. A real J7 port needs the I2C/regulator/
GPIO setup, I2S clocks and ADMA, then an XNU audio-facing device and a HAL that
exposes playback/capture. Until those layers are present, audio remains
unsupported; a silent placeholder would not make sound work.
