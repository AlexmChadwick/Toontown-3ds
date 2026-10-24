#---------------------------------------------------------------------------------
# Toontown 3DS - native homebrew client
# Based on the standard devkitPro 3DS template (devkitARM + libctru + citro3d/citro2d).
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(MAKECMDGOALS),assets)
# Asset baking needs only Python; skip the toolchain.
assets:
	@tools/pipeline/bake_phase1.sh
else

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET      := toontown3ds
BUILD       := build
SOURCES     := source source/core source/render source/world source/net source/audio source/ui
DATA        := data
INCLUDES    := source
GRAPHICS    := gfx
ROMFS       := romfs
GFXBUILD    := $(ROMFS)/gfx

APP_TITLE       := Toontown 3DS
APP_DESCRIPTION := Unofficial Open Toontown client
APP_AUTHOR      := toontown-3ds contributors
ICON            := meta/icon.png

# CIA metadata
CIA_RSF         := meta/app.rsf
CIA_BANNER_IMG  := meta/banner.png
CIA_BANNER_SND  := meta/banner.wav

#---------------------------------------------------------------------------------
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS   := -g -Wall -Wextra -O2 -mword-relocations -ffunction-sections \
            $(ARCH) $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS     := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS  := $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
                   $(CURDIR)/shaders
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
PICAFILES   := $(notdir $(wildcard shaders/*.v.pica))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD   := $(CXX)
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export OFILES_BIN     := $(addsuffix .o,$(BINFILES)) $(PICAFILES:.v.pica=.shbin.o)
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES         := $(PICAFILES:.v.pica=_shbin.h) $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON := $(TOPDIR)/$(ICON)
export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS += --smdh=$(OUTPUT).smdh --romfs=$(CURDIR)/$(ROMFS)

.PHONY: all clean cia assets

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

# .cia needs makerom + bannertool on PATH (see README.md).
cia: all
	@bannertool makebanner -i $(CIA_BANNER_IMG) -a $(CIA_BANNER_SND) -o $(BUILD)/banner.bnr
	@bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" \
		-i $(ICON) -o $(BUILD)/icon.icn
	@makerom -f cia -o $(OUTPUT).cia -elf $(OUTPUT).elf -rsf $(CIA_RSF) \
		-icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr -exefslogo -target t
	@echo built ... $(notdir $(OUTPUT).cia)

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cia

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)
$(OUTPUT).elf: $(OFILES)
$(OFILES_SOURCES): $(HFILES)

%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)

# Shaders: shaders/foo.v.pica -> foo.shbin embedded via bin2o
%.shbin.o %_shbin.h: %.v.pica
	@echo $(notdir $<)
	@picasso -o $*.shbin $<
	@bin2s -a 4 -H $*_shbin.h $*.shbin | $(AS) -o $*.shbin.o

-include $(DEPSDIR)/*.d

endif

endif # assets
