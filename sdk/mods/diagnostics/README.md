# Runtime Diagnostics

USB-owned system information and runtime-report tool for R114 or newer.
Place `enable_diagnostics.txt` on a mounted USB before software startup to load
this tool. Enable it in the Mod Menu, then choose Save runtime diagnostics to USB.
Scroll the right pane to read the entire system/save status. A writable USB is
required; no revision-specific marker is required to save a report.

This add-on owns the menu and polling policy. The foundation's common collector
still gathers the other services' observations without duplicating their hooks.
