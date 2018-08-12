The goal is to have a portable PCFX emulator that is fast and low on resources.
Mednafen was pretty much our only alternative and is GPLv2+ licensed so i took that.
Alright, i want to strip this down as much as possible.

# Remove ACCURATE paths - DONE

# Add 16-bits output support - DONE
This should run faster on hardware that runs faster on 16-bits surfaces.

# Remove dot clock emulation mode - DONE

# Remove Virtual Boy specific code
Done for the most part, i need to dive more into the CPU core for that.
Maybe i could look at Red Dragon for reference ?

# Remove more junk that steals cycles
Things like the debugger have to constantly run and can steal some cycles.
I already removed cheats & netplay. Resampling has been removed but OwlResampler will still be kept for the buffers.

# Remove libtrio
Why is this a thing ? It's not even maintained anymore, except by Mednafen of course...
We need to switch to using the C library instead and remove those that we can't switch (like trio_aprintf but we need to be careful about that)

# Remove tests
They are useful for checking Mednafen and whetever it would crash or not.
Maybe put it behind a DEBUG flag ?

# Make it mono-threaded, rather than multi-threaded
Chances are, if it does not run well mono-threaded for something like this then i've failed !
I've managed to remove the GameThread, which now runs inside of the main loop.
The only thread stuff that is left is cdromif.cpp, which uses a thread for CD stuff.

# Fix frameskipping
It doesn't seem to work at all for PC-FX. (but it did for other consoles)

# Remove C++ exceptions and/or eventually switch to C
Mednafen extensively uses C++ exceptions so it might be difficult but it would be the first step towards a C codebase. 
We could not care about handling exceptions and assume everything goes fine but that wouldn't be the proper way to do it.
I guess that's the last thing i would want to work on given how spread out it is.


Other things to do
===================

We need to remove the FX-SCSI stuff. Do you really need this just to run games ?

Remove FPU exceptions & CPU exceptions to the full extent possible.

In my experiments, removing those has no ill or un-intended effects. (Plus it would crash anyway if it would encounter those)

