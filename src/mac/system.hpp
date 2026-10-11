// What the agent asks macOS: its permissions and the system's scrolling direction.
#pragma once

namespace mouse::mac {

bool canPostEvents();  // Accessibility: posting keys and clicks
bool canListen();      // Input Monitoring: opening the mouse
bool systemNaturalScrolling();

// The system prompts (they can block until answered: never call them on the main thread).
void askForAccessibility();
void askForInputMonitoring();

// A symbol of the Accessibility framework (HIServices). It costs about a third of a megabyte, so
// it is loaded only when something needs it: a permission check or prompt while access is
// missing, or a button set to Mission Control, App Exposé or Show Desktop.
void* hiServices(const char* symbol);

}  // namespace mouse::mac
