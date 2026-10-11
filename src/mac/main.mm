// The settings window app (Mouse.app/Contents/MacOS/Mouse). The background part is a separate
// executable, MouseAgent (src/mac/agent_main.cpp), so that opening Mouse.app always shows the
// window even while the agent runs.
int runSettingsWindow(int argc, char** argv);  // src/ui/app.mm

int main(int argc, char** argv) { return runSettingsWindow(argc, argv); }
