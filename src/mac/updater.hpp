#pragma once

#include <string>

namespace mouse::mac {

constexpr int kUpdateInstalled = 10;  // exit code of `Mouse --update` after installing a new version

std::string currentBundlePath();  // "/Applications/Mouse.app", empty when not running from a bundle
bool installedForUpdates();       // in /Applications or ~/Applications (never a build folder)
int runUpdate();                  // 0 nothing to do, 1 failed, kUpdateInstalled

}  // namespace mouse::mac
