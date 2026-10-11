// The settings window app (Mouse.app/Contents/MacOS/Mouse). The background part is a separate
// executable, MouseAgent (src/mac/agent_main.cpp), so that opening Mouse.app always shows the
// window even while the agent runs.
#include <cstring>

int runSettingsWindow(int argc, char** argv);  // src/ui/app.mm
int registerAgentHeadless(bool force);         // src/ui/app.mm
int unregisterAgentHeadless();                 // src/ui/app.mm

int main(int argc, char** argv) {
    bool reg = false, force = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--register-agent") == 0) reg = true;
        if (std::strcmp(argv[i], "--force") == 0) force = true;
        if (std::strcmp(argv[i], "--unregister-agent") == 0) return unregisterAgentHeadless();
    }
    if (reg) return registerAgentHeadless(force);
    return runSettingsWindow(argc, argv);
}
