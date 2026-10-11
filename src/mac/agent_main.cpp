// MouseAgent (Mouse.app/Contents/Helpers/MouseAgent.app): started by launchd at login.
//   MouseAgent                        keep the mouse configured and perform its actions
//   MouseAgent --simulate <file>      drive a simulated mouse from a script (real output; --dry prints only)
//   MouseAgent --info                 read the connected mouse without changing anything
//   MouseAgent --probe [seconds]      print what a connected MX Master 3S reports
//   MouseAgent --update               install a newer release if there is one (spawned by the agent)
#include <cstring>

int runAgent(int argc, char** argv);
int runProbe(int argc, char** argv);
int runUpdateCheck();
int runInfo();

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--probe") == 0) return runProbe(argc, argv);
        if (std::strcmp(argv[i], "--update") == 0) return runUpdateCheck();
        if (std::strcmp(argv[i], "--info") == 0) return runInfo();
    }
    return runAgent(argc, argv);
}
