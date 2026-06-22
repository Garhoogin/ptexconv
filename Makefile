# SPDX-License-Identifier: CC0-1.0
#
# SPDX-FileContributor: Antonio Niño Díaz, 2023; Jon Ko, 2026

# Source code paths
# -----------------

SOURCEDIRS	:= src
INCLUDEDIRS	:= src

# Libraries
# ---------

LIBS		:= -lm
LIBDIRS		:=

# Define target (if not specified on cmd)
ifeq ($(OS),Windows_NT)
	ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
		TARGET		?= x86_64-windows-gnu
	else ifeq ($(PROCESSOR_ARCHITECTURE),x86)
		TARGET		?= x86-windows-gnu
	else
		TARGET		?= aarch64-windows-gnu
	endif
else
	UNAME_S := $(shell uname -s)
    UNAME_P := $(shell uname -p)
	ifeq ($(UNAME_S),Linux)
		ifeq ($(UNAME_P),x86_64)
			TARGET	?= x86_64-linux-gnu
		else ifneq ($(filter %86,$(UNAME_P)),)
			TARGET	?= x86-linux-gnu
		else ifneq ($(filter arm%,$(UNAME_P)),)
			TARGET	?= aarch64-linux-gnu
		else
			TARGET	?= x86_64-linux-gnu
		endif
	else ifeq ($(UNAME_S),Darwin)
		ifeq ($(UNAME_P),x86_64)
			TARGET	?= x86_64-macos
		else
			TARGET	?= aarch64-macos
		endif
	endif
endif

# Build artifacts
# ---------------

NAME		:= ptexconv
BUILDDIR	:= build
ifneq (,$(findstring windows,$(TARGET)))
	ELF		:= $(NAME).exe
	DLL		:= $(NAME).dll
else ifneq (,$(or $(findstring macos,$(TARGET)),$(findstring ios,$(TARGET))))
	ELF		:= $(NAME)
	DLL		:= $(NAME).dylib
else
	ELF		:= $(NAME)
	DLL		:= $(NAME).so
endif

# Tools
# -----

STRIP		:= -s
BINMODE		:= 755
LIBMODE		:= 644

CC			:= zig cc -target $(TARGET)
MKDIR		:= mkdir
RM			:= rm -rf
MAKE		:= make
INSTALL		:= install

ifneq (,$(findstring android,$(TARGET)))
	CC		:= $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang
else ifneq (,$(findstring ios,$(TARGET)))
	CC		:= xcrun clang -target aarch64-apple-ios
endif

# Verbose flag
# ------------

ifeq ($(VERBOSE),1)
V		:=
else
V		:= @
endif

# Source files
# ------------

SOURCES_C	:= $(shell find -L $(SOURCEDIRS) -name "*.c")

# Compiler and linker flags
# -------------------------

DEFINES	:= -DNDEBUG
ifneq (,$(findstring ios,$(TARGET)))
	DEFINES += -DSTBI_NO_THREAD_LOCALS
endif

ifneq (,$(findstring windows,$(TARGET)))
	DEFINES += -D_WIN32 -DUNICODE
endif

WARNFLAGS_C	:= -Wall -Wextra -Wpedantic \
		-Wno-pointer-sign -Wno-unused-variable -Wno-unused-result \
		-Wno-unused-parameter -Wno-unused-but-set-variable

LD	:= $(CC)

INCLUDEFLAGS	:= $(foreach path,$(INCLUDEDIRS),-I$(path)) \
		   $(foreach path,$(LIBDIRS),-isystem$(path)/include)
ifneq (,$(findstring ios,$(TARGET)))
	INCLUDEFLAGS += -isysroot $(shell xcrun --sdk iphoneos --show-sdk-path)
endif

LIBDIRSFLAGS	:= $(foreach path,$(LIBDIRS),-L$(path)/lib)

CFLAGS		:= -std=gnu11 $(WARNFLAGS_C) $(DEFINES) $(INCLUDEFLAGS) -O3 -g0

ifneq (,$(findstring windows,$(TARGET)))
	ifneq (,$(findstring aarch64,$(TARGET)))
		CFLAGS	+= -m64 -municode -fno-pie -static -flto
	else ifneq (,$(findstring x86_64,$(TARGET)))
		CFLAGS	+= -m64 -msse2 -municode -fno-pie -static -flto
	else
		CFLAGS	+= -municode -msse2 -mfpmath=sse -ffast-math -fno-math-errno -ftree-vectorize -fno-pie -static -flto -fdata-sections -ffunction-sections
	endif
endif

lib: CFLAGS += -fPIC

LDFLAGS		+= $(LIBDIRSFLAGS) $(LIBS) -s
ifneq (,$(findstring windows,$(TARGET)))
	LDFLAGS	+= -Wl,--subsystem,console
endif

# Intermediate build files
# ------------------------

OBJS		:= $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_C)))

DEPS		:= $(OBJS:.o=.d)

# Targets
# -------

.PHONY: all clean install

all: $(ELF)

lib: $(DLL)

$(ELF): $(OBJS)
	@echo "  LD      $@"
	$(V)$(LD) -o $@ $(OBJS) $(LDFLAGS)

$(DLL): $(OBJS)
	@echo "  LD      $@"
	$(V)$(LD) -shared -o $@ $(OBJS) $(LDFLAGS)

clean:
	@echo "  CLEAN  "
	$(V)$(RM) $(ELF) $(DLL) $(BUILDDIR)

INSTALLDIR	?= /opt/blocksds/external/ptexconv
INSTALLDIR_ABS	:= $(abspath $(INSTALLDIR))

install: all
	@echo "  INSTALL $(INSTALLDIR_ABS)"
	@test $(INSTALLDIR_ABS)
	$(V)$(RM) $(INSTALLDIR_ABS)
	$(V)$(INSTALL) -d $(INSTALLDIR_ABS)
	$(V)$(INSTALL) $(STRIP) -m $(BINMODE) $(NAME) $(INSTALLDIR_ABS)
	$(V)$(CP) ./LICENSE $(INSTALLDIR_ABS)

# Rules
# -----

$(BUILDDIR)/%.c.o : %.c
	@echo "  CC      $<"
	@$(MKDIR) -p $(@D)
	$(V)$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# Include dependency files if they exist
# --------------------------------------

-include $(DEPS)
