# SPU2 provenance

The register-level SPU2 core (`src/emulator/core/spu2.cpp`, `spu2.h`), the initial
SPU2 tests (`../ps2xTest/src/ps2_spu2_tests.cpp`), and the host output adapter are
adapted from [KIexster/god-of-war-recomp](https://github.com/KIexster/god-of-war-recomp),
revision `92ece581c673842c282a9c23819545de5ab92d3c`, licensed under GNU GPL version 3,
as is this repository (see the root `LICENSE`).

The source patches are `patches/anteriores/ps2recomp-spu2.patch`,
`ps2recomp-spu2-output.patch`, and `ps2recomp-spu2-nax.patch`. The performance
instrumentation patch was not imported. Original source comments, including
the upstream attribution for the NAX behavior, are retained.

The integration here uses the existing interpreted IOP and the real LIBSD MMIO
path. It does not import GoW1 generated code, IOP AOT overrides, game data, or
the separate native sound engine used by newer GoW1 runtime patches.

This is an initial implementation: it supports sound RAM, direct DMA, ADPCM,
pitch, ADSR, fixed volume, loops, voice positions, dry mixing and IRQA. Reverb,
hardware Gaussian interpolation, accurate volume sweeps, noise/pitch modulation
and ADMA are not implemented. Audio fidelity beyond the validated cases remains
unverified. Output is stereo signed 16-bit PCM at 48 kHz.

`PS2_SPU2_AUDIO=0` disables host playback without stopping emulation.
For diagnostics, `PS2_SPU2_CAPTURE=/absolute/path.pcm` records produced PCM before
host silence padding. `PS2_SPU2_CAPTURE_FRAMES` bounds the recording (default
480,000 frames, maximum 2,880,000). Captures are game data and must stay outside
the code repository. These captures use the host's native 16-bit byte order
(little endian on the validated Linux x86-64 host).
