# FC_Plugins — cross-compile build for Far Cry 5 / New Dawn / 6
# MinGW-w64 cross compiler (gcc/g++), mirroring Disrupt's LoaderWD pattern.
#
# Notes:
#   - nmd (.c) is compiled with CC  — its header declares C-style const globals.
#   - SDK/PluginLoader/plugins (.cpp) use CXX.
#   - The SDK's Macros.hpp requires `__clang__` (upstream builds with clang/msvc);
#     we define it explicitly so the code compiles under gcc.
#   - Outputs to build/ (no hardcoded deploy path; deploy is per-game).
#
# Targets:
#   make            -> dbdata.dll + PluginLoader.dll + FileHook.dll
#   make dbdata     -> proxy dll only
#   make loader     -> PluginLoader.dll only
#   make filehook   -> FileHook.dll only
#   make clean

CC        = x86_64-w64-mingw32-gcc
CXX       = x86_64-w64-mingw32-g++
LDFLAGS   = -shared -static -O2
CFLAGS    = -O2
CXXFLAGS  = -O2 -D__clang__ -Wno-inline-new-delete -Wno-implicit-exception-spec-mismatch \
            -Wno-deprecated-declarations -Wno-pragma-pack -Wno-macro-redefined \
            -Wno-format-security -Wno-return-type-c-linkage -Wno-writable-strings \
            -Wno-format -Wno-gnu-string-literal-operator-template
INCLUDES  = -I./SDK -I.

BUILD     = build

# Per-machine install location. Override in Makefile.local (gitignored):
#   GAME_DIR   = /path/to/game            e.g. .../Far Cry 6
#   GAME_BIN   = $(GAME_DIR)/bin          where dbdata.dll/PluginLoader.dll go
#   GAME_PLUGINS = $(GAME_DIR)/plugins    where FileHook.dll goes
# Defaults (harmless; make install errors until overridden):
GAME_DIR     = /nonexistent
GAME_BIN     = $(GAME_DIR)/bin
GAME_PLUGINS = $(GAME_DIR)/plugins

-include Makefile.local

NMD_SRCS  = nmd/assembly/nmd_common.c \
            nmd/assembly/nmd_x86_assembler.c \
            nmd/assembly/nmd_x86_decoder.c \
            nmd/assembly/nmd_x86_formatter.c \
            nmd/assembly/nmd_x86_ldisasm.c
NMD_OBJS  = $(patsubst nmd/assembly/%.c,$(BUILD)/nmd_%.o,$(NMD_SRCS))

LOADER_SRCS = PluginLoader/DllMain.cpp \
              PluginLoader/Plugins.cpp \
              PluginLoader/Game.cpp \
              PluginLoader/Util/Log.cpp \
              PluginLoader/Util/Signatures.cpp \
              PluginLoader/Util/Disasm.cpp \
              PluginLoader/Util/Bytepatch.cpp \
              PluginLoader/Util/Stubs.cpp \
              PluginLoader/Util/Detours.cpp \
              PluginLoader/Util/Offset.cpp \
              PluginLoader/Game/Command.cpp \
              PluginLoader/Game/Entity/EntityList.cpp \
              PluginLoader/Game/Entity/CEntity.cpp
LOADER_OBJS = $(patsubst %.cpp,$(BUILD)/%.o,$(LOADER_SRCS))

DLLS = $(BUILD)/dbdata.dll $(BUILD)/PluginLoader.dll $(BUILD)/FileHook.dll $(BUILD)/DumpModule.dll

.PHONY: all dbdata loader filehook dumpmodule clean
all: $(DLLS)

# ---- nmd (C) objects -------------------------------------------------------
$(BUILD)/nmd_%.o: nmd/assembly/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# ---- PluginLoader (C++) objects -------------------------------------------
$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# ---- proxy -----------------------------------------------------------------
$(BUILD)/dbdata.dll: Proxy/DBData.cpp Proxy/dbdata.def
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -D_USRDLL -D_WINDLL $(LDFLAGS) -Wl,Proxy/dbdata.def -o $@ $<

# ---- PluginLoader.dll ------------------------------------------------------
$(BUILD)/PluginLoader.dll: $(LOADER_OBJS) $(NMD_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -D_USRDLL -D_WINDLL $(LDFLAGS) -o $@ $(LOADER_OBJS) $(NMD_OBJS)

# ---- FileHook.dll (the file-open logger) ----------------------------------
$(BUILD)/FileHook.dll: Plugins/Universal/FileHook/Entry.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -D_USRDLL -D_WINDLL $(LDFLAGS) -o $@ $<

# ---- DumpModule.dll (runtime module capture for Denuvo/VMProtect) ---------
$(BUILD)/DumpModule.dll: Plugins/Universal/DumpModule/Entry.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -D_USRDLL -D_WINDLL $(LDFLAGS) -o $@ $<

dbdata:   $(BUILD)/dbdata.dll
loader:   $(BUILD)/PluginLoader.dll
filehook: $(BUILD)/FileHook.dll
dumpmodule: $(BUILD)/DumpModule.dll

# ---- install (uses Makefile.local overrides) ------------------------------
install: $(DLLS)
	@test -d "$(GAME_BIN)" || { echo "ERROR: GAME_BIN '$(GAME_BIN)' not found (set it in Makefile.local)"; exit 1; }
	@test -d "$(GAME_PLUGINS)" || { echo "ERROR: GAME_PLUGINS '$(GAME_PLUGINS)' not found (set it in Makefile.local)"; exit 1; }
	@echo "== installing to $(GAME_BIN) =="
	@if [ -f "$(GAME_BIN)/dbdata.dll" ] && [ ! -f "$(GAME_BIN)/dbdata.old.dll" ]; then \
		echo "  backing up dbdata.dll -> dbdata.old.dll"; \
		mv "$(GAME_BIN)/dbdata.dll" "$(GAME_BIN)/dbdata.old.dll"; \
	fi
	@cp "$(BUILD)/dbdata.dll" "$(BUILD)/PluginLoader.dll" "$(GAME_BIN)/"
	@cp "$(BUILD)/FileHook.dll" "$(BUILD)/DumpModule.dll" "$(GAME_PLUGINS)/"
	@echo "  installed dbdata.dll + PluginLoader.dll -> bin/, FileHook.dll + DumpModule.dll -> plugins/"
	@echo "  done. launch the game to capture FC6FileHook.log"

clean:
	rm -rf $(BUILD)