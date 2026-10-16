APP       := Mouse
VERSION   := 1.0.4
BUILD     := build
BUNDLE    := $(BUILD)/$(APP).app
BINARY    := $(BUNDLE)/Contents/MacOS/$(APP)
HELPER    := $(BUNDLE)/Contents/Helpers/MouseAgent.app
AGENT     := $(HELPER)/Contents/MacOS/MouseAgent
PREFIX    ?= /Applications

CXX       := clang++
# Apple silicon and macOS 27 or later only.
ARCH      := arm64
MIN_MACOS := 27.0
# Code signing identity: "-" (ad hoc) by default. Releases are signed with the project's own
# certificate (SHA-1 below) so every version satisfies the same designated requirement and
# macOS keeps its Accessibility / Input Monitoring permission across updates.
RELEASE_ID := 36C5521F087DACFB0AE26D2332DC3331BADFB13A
SIGN_ID   ?= -
SIGN_KEYCHAIN ?=

ARCHFLAGS := -arch $(ARCH) -mmacosx-version-min=$(MIN_MACOS)
WARN      := -Wall -Wextra -Wshadow -Wno-missing-field-initializers
OPT       ?= -Oz -flto
LTOFLAGS  ?= -flto
CXXFLAGS  += $(ARCHFLAGS) -std=c++20 $(OPT) $(WARN) -fvisibility=hidden -MMD -MP -DMOUSE_VERSION=\"$(VERSION)\" -Isrc
OBJCFLAGS := -fobjc-arc
# The C++ code neither catches exceptions nor asks for types at run time (a throw ends the process
# either way); without both, the agent fits a 16 KB page less. AppKit's Objective-C++ keeps them.
CPPFLAGS_ONLY := -fno-exceptions -fno-rtti
LDFLAGS   += $(ARCHFLAGS) $(LTOFLAGS) -dead_strip

CORE_SRC  := $(wildcard src/core/*.cpp)
SIM_SRC   := $(wildcard src/sim/*.cpp)
MAC_SRC   := src/mac/hid.cpp src/mac/output.cpp src/mac/system.cpp
AGENT_SRC := $(CORE_SRC) $(MAC_SRC) src/mac/agent.cpp src/mac/agent_main.cpp src/mac/updater.cpp
UI_SRC    := $(CORE_SRC)
UI_MM     := $(wildcard src/mac/*.mm src/ui/*.mm)
AGENT_OBJ := $(AGENT_SRC:src/%.cpp=$(BUILD)/obj/%.o)
UI_OBJ    := $(UI_SRC:src/%.cpp=$(BUILD)/obj/%.o) $(UI_MM:src/%.mm=$(BUILD)/obj/%.o)
SIM_OBJ   := $(patsubst src/%.cpp,$(BUILD)/obj/%.o,$(CORE_SRC) $(SIM_SRC) $(MAC_SRC) src/mac/simulate.cpp)
# The agent never loads AppKit (nor, unless needed, the Accessibility framework): it stays at
# a couple of megabytes.
AGENT_LIBS := -framework IOKit -framework CoreGraphics -framework Security -framework CoreFoundation
UI_LIBS    := -framework AppKit -framework CoreFoundation

TEST_SRC  := $(wildcard tests/*.cpp)
TEST_OBJ  := $(TEST_SRC:tests/%.cpp=$(BUILD)/test/%.o) $(patsubst src/%.cpp,$(BUILD)/test/src/%.o,$(CORE_SRC) $(SIM_SRC))
TEST_BIN  := $(BUILD)/mouse-tests
SIM_BIN   := $(BUILD)/mouse-simulate

ICON      := resources/$(APP).icns
AGENT_ICON := resources/MouseAgent.icns
RESOURCES := $(BUNDLE)/Contents/Info.plist $(HELPER)/Contents/Info.plist \
             $(if $(wildcard $(ICON)),$(BUNDLE)/Contents/Resources/$(APP).icns) \
             $(if $(wildcard $(AGENT_ICON)),$(HELPER)/Contents/Resources/MouseAgent.icns)
SIGN      = codesign --force --sign "$(SIGN_ID)" $(if $(SIGN_KEYCHAIN),--keychain "$(SIGN_KEYCHAIN)") --timestamp=none

.PHONY: all app run install uninstall test simulate snapshot icon zip release clean

all: app

app: $(BUILD)/.signed

# Sign the helper first, then the app that contains it.
$(BUILD)/.signed: $(BINARY) $(AGENT) $(RESOURCES)
	@$(SIGN) $(HELPER)
	@$(SIGN) $(BUNDLE)
	@touch $@
	@echo "Built $(BUNDLE) ($$(du -sh $(BUNDLE) | cut -f1))"

$(BINARY): $(UI_OBJ)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $^ -o $@ $(UI_LIBS)
	@strip -x $@

$(AGENT): $(AGENT_OBJ)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $^ -o $@ $(AGENT_LIBS)
	@strip -x $@

# Development tool (not part of the app): drives a simulated mouse from a script with real output.
simulate: $(SIM_BIN)

$(SIM_BIN): $(SIM_OBJ)
	$(CXX) $(LDFLAGS) $^ -o $@ $(AGENT_LIBS)

$(BUNDLE)/Contents/Info.plist: resources/Info.plist Makefile
	@mkdir -p $(@D)
	@sed 's/__VERSION__/$(VERSION)/g' $< > $@

$(HELPER)/Contents/Info.plist: resources/AgentInfo.plist Makefile
	@mkdir -p $(@D)
	@sed 's/__VERSION__/$(VERSION)/g' $< > $@

$(BUNDLE)/Contents/Resources/$(APP).icns: $(ICON)
	@mkdir -p $(@D)
	@cp $< $@

$(HELPER)/Contents/Resources/MouseAgent.icns: $(AGENT_ICON)
	@mkdir -p $(@D)
	@cp $< $@

$(BUILD)/obj/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS_ONLY) -c $< -o $@

$(BUILD)/obj/%.o: src/%.mm
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(OBJCFLAGS) -c $< -o $@

# Tests build the portable core and the simulator only (no AppKit), with sanitizers.
TESTFLAGS := -std=c++20 -O1 -g $(WARN) -Isrc -fsanitize=address,undefined -fno-omit-frame-pointer -DMOUSE_VERSION=\"$(VERSION)\"

$(BUILD)/test/%.o: tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(TESTFLAGS) -c $< -o $@

$(BUILD)/test/src/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(TESTFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_OBJ)
	$(CXX) $(TESTFLAGS) $^ -o $@ -framework CoreFoundation

test: $(TEST_BIN)
	@$(TEST_BIN)

run: app
	@$(BINARY)

install: app
	@pkill -x $(APP) || true
	@pkill -x MouseAgent || true
	@rm -rf "$(PREFIX)/$(APP).app"
	@cp -R $(BUNDLE) "$(PREFIX)/"
	@open "$(PREFIX)/$(APP).app"
	@echo "Installed to $(PREFIX)/$(APP).app"

uninstall:
	@launchctl bootout gui/$$(id -u)/app.mouse.agent 2>/dev/null || true
	@rm -f ~/Library/LaunchAgents/app.mouse.agent.plist
	@pkill -x $(APP) || true
	@pkill -x MouseAgent || true
	@rm -rf "$(PREFIX)/$(APP).app"

# Renders the settings window (light and dark, demo data) to docs/.
snapshot: app
	@$(BINARY) --snapshot docs

zip: app
	@cd $(BUILD) && rm -f $(APP).zip && ditto -c -k --sequesterRsrc --keepParent $(APP).app $(APP).zip
	@echo "Wrote $(BUILD)/$(APP).zip"

# Certificate-signed zip for GitHub releases (see .github/workflows/release.yml).
release:
	@$(MAKE) clean
	@$(MAKE) zip SIGN_ID="$(RELEASE_ID)" SIGN_KEYCHAIN="$(SIGN_KEYCHAIN)" VERSION="$(VERSION)"
	@codesign --verify --strict $(BUNDLE)
	@codesign -d -r- $(BUNDLE) 2>&1 | grep -q 'certificate leaf' || (echo "release is not certificate-signed" && false)
	@for f in $(BINARY) $(AGENT); do test "$$(lipo -archs $$f)" = $(ARCH) || (echo "$$f is not $(ARCH) only" && false); done
	@for f in $(BINARY) $(AGENT); do vtool -show-build $$f | grep -q "minos $(MIN_MACOS)" || (echo "$$f: wrong minimum macOS" && false); done

# The agent gets a small icon: it only appears in the Privacy & Security lists and prompts.
icon: tools/icon.cpp
	@rm -rf $(BUILD)/icon.iconset $(BUILD)/agent-icon.iconset
	@mkdir -p $(BUILD)/icon.iconset $(BUILD)/agent-icon.iconset
	$(CXX) -std=c++20 -O2 $< -o $(BUILD)/icon -framework CoreGraphics -framework CoreFoundation -lz
	@$(BUILD)/icon $(BUILD)/icon.iconset
	@$(BUILD)/icon --small $(BUILD)/agent-icon.iconset
	@iconutil -c icns $(BUILD)/icon.iconset -o $(ICON)
	@iconutil -c icns $(BUILD)/agent-icon.iconset -o $(AGENT_ICON)
	@$(BUILD)/icon --pack $(BUILD)/icon.iconset $(ICON)
	@$(BUILD)/icon --pack $(BUILD)/agent-icon.iconset $(AGENT_ICON)
	@echo "Wrote $(ICON) ($$(stat -f%z $(ICON)) bytes) and $(AGENT_ICON) ($$(stat -f%z $(AGENT_ICON)) bytes)"

clean:
	rm -rf $(BUILD)

-include $(AGENT_OBJ:.o=.d) $(UI_OBJ:.o=.d) $(SIM_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
