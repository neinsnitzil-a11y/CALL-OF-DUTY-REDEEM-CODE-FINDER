// Start with Windows / at sign-in (minimised to the tray, scanning). Per-user, no admin rights.
#pragma once
#include <QString>

namespace autostart {
bool enabled();
bool setEnabled(bool on, QString* error = nullptr);
}
