#pragma once

namespace bronco::log {

/// Initialize the logger (called automatically on first use).
/// Opens bronco_log.txt in the same directory as the proxy DLL.
void init();

/// Log an informational message with timestamp.
/// Also forwards to OutputDebugStringA for DebugView compatibility.
void info(const char* msg);

/// Log an error message with timestamp.
/// Also forwards to OutputDebugStringA for DebugView compatibility.
void error(const char* msg);

} // namespace bronco::log
