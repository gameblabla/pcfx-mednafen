PRGNAME     = mednafen.elf

# define regarding OS, which compiler to use
CC        	= gcc
CXX         = g++
LD          = g++

# change compilation / linking flag options
F_OPTS		=  -I./include -I./src/thread -I. -Iconfigurations/PC
F_OPTS 		+= -DHAVE_CONFIG_H -DUSE_ALSA_DEFAULT
DEFINES    += -Isrc/pcfx/input -Isrc/pcfx -Isrc/sound -Isrc/mpcdec
DEFINES    += -DLOCALEDIR=\"/usr/local/share/locale\" -D_GNU_SOURCE=1 -D_REENTRANT

CC_OPTS		= -O2 -pg -DHAVE_COMPUTED_GOTO $(F_OPTS) $(DEFINES) -march=native -mtune=native
CC_OPTS    += -fsigned-char -fno-fast-math -fno-unsafe-math-optimizations -fno-aggressive-loop-optimizations -fno-ipa-icf 
CC_OPTS    += -fno-printf-return-value -fno-omit-frame-pointer -fstrict-aliasing
CC_OPTS    += -Wall -Wshadow -Wempty-body -Wignored-qualifiers -Wvla -Wvariadic-macros -Wdisabled-optimization
CC_OPTS    += -fno-pic -fno-pie -fno-PIC -fno-PIE -no-pie -fwrapv -fjump-tables -mcmodel=small -fexceptions  -fno-stack-protector


CFLAGS		= $(CC_OPTS) -std=gnu99
CXXFLAGS	= $(CC_OPTS) -std=gnu++11
LDFLAGS     = -lSDL -lm -lz -lstdc++ -pthread -lasound

# Files to be compiled
SRCDIR    = src src/video src/cputest src/string src/demo src/resampler src/hash src/compress src/drivers 
SRCDIR    += src/hw_cpu src/hw_misc src/hw_video src/mpcdec 
SRCDIR    += src/quicklz src/sound_ src/sexyal src/cdrom 
SRCDIR    += src/sexyal/drivers src/hw_sound/pce_psg src/hw_cpu/v810 src/thread
SRCDIR    += src/pcfx/input src/pcfx
SRCDIR    += src/sound src/tremor src/hw_video/huc6270 src/sexyal/drivers_alsa

VPATH     = $(SRCDIR)
SRC_C   = $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.c))
SRC_CP   = $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.cpp))
OBJ_C   = $(notdir $(patsubst %.c, %.o, $(SRC_C)))
OBJ_CP   = $(notdir $(patsubst %.cpp, %.o, $(SRC_CP)))
OBJS     = $(OBJ_C) $(OBJ_CP)

# Rules to make executable
all: $(PRGNAME) 

$(PRGNAME): $(OBJS)  
	$(LD) $(CFLAGS) -o $(PRGNAME) $^ $(LDFLAGS)

$(OBJ_C) : %.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_CP) : %.o : %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(PRGNAME) *.o
