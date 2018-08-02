The goal is to have a portable PCFX emulator that is fast and low on resources.
Mednafen was pretty much our only alternative and is GPLv2+ licensed so i took that.

Alright, i want to strip this down as much as possible.

TODO
====

# Add 16-bits output support
Doesn't work properly on Mednafen sadly. Forcing it results in a crash.
There's a lot of junk in the rendering code so we would need to greatly simplify it.
Once that's done, i need to fix it. Should be fairly trivial to figure out hopefully.

Removal/Cleanup
==============

We also need to seperate the accurate & fast versions, for speed reasons (there are a lot of conditions for switching between the 2)

We need to remove the FX-SCSI stuff. Do you really need this just to run games ?

Remove FPU exceptions & CPU exceptions to the full extent possible

n my experiments, removing those has no ill or intended effects. (Plus it would crash anyway if it would encounter those)

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

# Remove exceptions and/or eventually switch to C
Mednafen extensively uses C++ exceptions so it might be difficult but it would be the first step towards a C codebase. 
We could not care about handling exceptions and assume everything goes fine but that wouldn't be the proper way to do it.
I guess that's the last thing i would want to work on given how spread out it is.
