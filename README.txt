The goal is to have a portable PCFX emulator that is fast and low on resources.
Mednafen was pretty much our only alternative and is GPLv2+ licensed so i took that.

Alright, i want to strip this down as much as possible.
TODO :

- Add 16-bits output support
Doesn't work properly on Mednafen sadly. Forcing it results in a crash.
There's a lot of junk in the rendering code so we would need to greatly simplify it.
Once that's done, i need to fix it. Should be fairly trivial to figure out hopefully.

- Remove some useless accurate emulation
Most notably the dot clock emulation mode making everything blurry.
It was done in order to mimic the real hardware but not only does it make it slower, it's uglier too.

We also need to seperate the accurate & fast versions, for speed reasons.

- Remove more junk.
Things like the debugger have to constantly run and can steal some cycles.
I already removed cheats & netplay. I'll need to remove the resamplers too because they are also

