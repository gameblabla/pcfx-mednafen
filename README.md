SmallPCFX
=========

SmallPCFX is a PC-FX emulator based on a stripped down version of Mednafen.
The current aim is to have a decently fast PC-FX emulator for platforms like the GCW Zero and the RS-97.

Currently though, while it runs faster than stock Mednafen, it's still not fast enough.
This needs some frameskip before it might be playable (to some extent) on low-end hardware.

Installation
============

Like Mednafen, a bios/firmware is required.
Put it in .mednafen/ (Should be found in your HOME directory or /mnt/int_sd/.mednafen for the RS-97) and rename it to pcfx.rom.
A bad dump of the BIOS is floating around and won't work with Mednafen or this emulator,
here's the MD5 hash of the working one : 08e36edbea28a017f79f8d4f7ff9b6d7

Controls
=========

Exit is mapped to SDLK_END, which is the power button on the RS-97.
