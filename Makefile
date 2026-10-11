APP       := Mouse
VERSION   := 1.0.1
BUILD     := build
BUNDLE    := $(BUILD)/$(APP).app
BINARY    := $(BUNDLE)/Contents/MacOS/$(APP)
HELPER    := $(BUNDLE)/Contents/Helpers/MouseAgent.app
AGENT     := $(HELPER)/Contents/MacOS/MouseAgent
PREFIX    ?= /Applications

CXX       := clang++
ARCHS     ?= $(shell uname -m)
MIN_MACOS := 13.0
# Code signing identity: "-" (ad hoc) by default. Releases are signed with the project's own
# certificate (SHA-1 below) so every version satisfies the same designated requirement and
# macOS keeps its Accessibility / Input Monitoring permission across updates.
RELEASE_ID := 36C5521F087DACFB0AE26D2332DC3331BADFB13A
SIGN_ID   ?= -
SIGN_KEYCHAIN ?=

ARCHFLAGS := $(foreach a,$(ARCHS),-arch $(a)) -mmacosx-version-min=$(MIN_MACOS)
WARN      := -Wall -Wextra -Wshadow -Wno-missing-field-initializers
CXXFLAGS  += $(ARCHFLAGS) -std=c++20 -Os $(WARN) -fvisibility=hidden -MMD -MP -DMOUSE_VERSION=\"$(VERSION)\" -Isrc
OBJCFLAGS := -fobjc-arc
LDFLAGS   += $(ARCHFLAGS) -dead_strip

CORE_SRC  := $(wildcard src/core/*.cpp)
SIM_SRC   := $(wildcard src/sim/*.cpp)
AGENT_SRC := $(CORE_SRC) $(SIM_SRC) $(wildcard src/mac/*.cpp)
UI_SRC    := $(CORE_SRC)
UI_MM     := $(wildcard src/mac/*.mm src/ui/*.mm)
AGENT_OBJ := $(AGENT_SRC:src/%.cpp=$(BUILD)/obj/%.o)
UI_OBJ    := $(UI_SRC:src/%.cpp=$(BUILD)/obj/%.o) $(UI_MM:src/%.mm=$(BUILD)/obj/%.o)
# The agent never loads AppKit: it stays at a couple of megabytes.
AGENT_LIBS := -framework IOKit -framework ApplicationServices -framework Security -framework CoreFoundation
UI_LIBS    := -framework AppKit -framework ServiceManagement -framework IOKit -framework ApplicationServices \
              -framework CoreFoundation

TEST_SRC  := $(wildcard tests/*.cpp)
TEST_OBJ  := $(TEST_SRC:tests/%.cpp=$(BUILD)/test/%.o) $(patsubst src/%.cpp,$(BUILD)/test/src/%.o,$(CORE_SRC) $(SIM_SRC))
TEST_BIN  := $(BUILD)/mouse-tests

ICON      := resources/$(APP).icns
RESOURCES := $(BUNDLE)/Contents/Info.plist $(HELPER)/Contents/Info.plist \
             $(if $(wildcard $(ICON)),$(BUNDLE)/Contents/Resources/$(APP).icns $(HELPER)/Contents/Resources/$(APP).icns)
SIGN      = codesign --force --sign "$(SIGN_ID)" $(if $(SIGN_KEYCHAIN),--keychain "$(SIGN_KEYCHAIN)") --timestamp=none

.PHONY: all app run install uninstall test snapshot icon zip release clean

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

$(BUNDLE)/Contents/Info.plist: resources/Info.plist Makefile
	@mkdir -p $(@D)
	@sed 's/__VERSION__/$(VERSION)/g' $< > $@

$(HELPER)/Contents/Info.plist: resources/AgentInfo.plist Makefile
	@mkdir -p $(@D)
	@sed 's/__VERSION__/$(VERSION)/g' $< > $@

$(BUNDLE)/Contents/Resources/$(APP).icns $(HELPER)/Contents/Resources/$(APP).icns: $(ICON)
	@mkdir -p $(@D)
	@cp $< $@

$(BUILD)/obj/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

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

# Universal, certificate-signed zip for GitHub releases (see .github/workflows/release.yml).
release:
	@$(MAKE) clean
	@$(MAKE) zip ARCHS="arm64 x86_64" SIGN_ID="$(RELEASE_ID)" SIGN_KEYCHAIN="$(SIGN_KEYCHAIN)" VERSION="$(VERSION)"
	@codesign --verify --strict $(BUNDLE)
	@codesign -d -r- $(BUNDLE) 2>&1 | grep -q 'certificate leaf' || (echo "release is not certificate-signed" && false)
	@lipo -info $(BINARY)
	@lipo -info $(AGENT)

icon: tools/icon.cpp
	@mkdir -p $(BUILD)/icon.iconset
	$(CXX) -std=c++20 -O2 $< -o $(BUILD)/icon -framework CoreGraphics -framework ImageIO -framework CoreFoundation
	@$(BUILD)/icon $(BUILD)/icon.iconset
	@iconutil -c icns $(BUILD)/icon.iconset -o $(ICON)
	@echo "Wrote $(ICON)"

clean:
	rm -rf $(BUILD)

-include $(AGENT_OBJ:.o=.d) $(UI_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
