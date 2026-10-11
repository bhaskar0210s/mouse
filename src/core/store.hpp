// Files and notifications shared by the agent and the settings window.
#pragma once

#include <string>

#include "config.hpp"

namespace mouse {

constexpr const char* kConfigChanged = "app.mouse.config";  // window -> agent
constexpr const char* kStateChanged = "app.mouse.state";    // agent -> window
constexpr const char* kAskPermission = "app.mouse.permissions";  // window -> agent: show the system prompts
constexpr const char* kReapply = "app.mouse.reapply";  // diagnostics -> agent: send every setting to the mouse again

// ~/Library/Application Support/Mouse (or $MOUSE_DIR, used by tests).
std::string dataDir();
std::string configPath();
std::string statePath();

bool readFile(const std::string& path, std::string& out);
// Write via a temporary file and rename, so readers never see half a file.
bool writeFileAtomic(const std::string& path, const std::string& data);

Config loadConfig();             // defaults when missing or unreadable
bool saveConfig(const Config&);  // also notifies the agent

// What the agent tells the window.
struct State {
    bool running = false;    // agent wrote this file
    bool connected = false;  // MX Master 3S found and set up
    std::string name;        // device name
    int battery = -1;        // percent, -1 unknown
    bool charging = false;
    int dpiMin = 200, dpiMax = 8000, dpiStep = 50;
    bool trusted = false;      // Accessibility granted
    bool inputAccess = false;  // Input Monitoring granted
    std::string version;       // agent version
    std::string update;        // last update note, e.g. "Updated to 1.0.1"
    bool operator==(const State&) const = default;
};

State parseState(const std::string& text);
std::string serializeState(const State& s);
State loadState();
bool saveState(const State& s);  // also notifies the window

void postNotification(const char* name);

}  // namespace mouse
