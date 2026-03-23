#include "common/changelog.h"

static const changelog_entry_t g_entries[] = {
    {
        "v0.7",
        "2026-03-22",
        "AX Studio: visual app designer with drag-and-drop controls and wire-based logic.",
        {
            "Added AX Studio: design forms with Button, Label, TextBox, Checkbox controls.",
            "Multi-scene support: add/remove scenes and switch between them at design time.",
            "Logic tab: place trigger/action/condition blocks and wire them together.",
            "Triggers: OnClick, OnStart, OnSubmit, OnToggle. Actions: SetText, Show/Hide,",
            "  SwitchScene, Enable/Disable. Conditions: IfTextEq, IfChecked.",
            "Save/load .ax projects to /ROOT; Run opens live window; axapp shell command.",
        },
        6,
    },
    {
        "v0.6",
        "2026-03-22",
        "AX Code IDE, window resize/minimize, start menu redesign, smaller UI chrome.",
        {
            "Editor renamed to AX Code: always-Ax mode, default /ROOT/HELLO.AX.",
            "AX Code toolbar: New, Open, Save, Run, Docs buttons; Ln/Col in status bar.",
            "New Ax Language Reference window (Docs button or axdocs_gui_launch).",
            "Windows can now be minimized (- button) and restored via taskbar.",
            "Windows can be resized by dragging the bottom-right corner handle.",
            "Title bar slimmer (22px), taskbar slimmer (28px).",
            "Start menu redesigned: greeting header, 3-col app grid, user/power footer.",
            "Power button shows Shutdown/Reboot/Reboot AswdOS; user area shows Log Out.",
        },
        8,
    },
    {
        "v0.5",
        "2026-03-22",
        "Ax scripting language, VS Code-like editor, browser demoted to admin-only.",
        {
            "Added Ax: a custom interpreted language for AswdOS (.ax scripts).",
            "Ax supports variables, arithmetic, strings, if/else, while, functions, print/sys/input.",
            "New shell commands: ax <file.ax> runs a script; run also dispatches .ax files.",
            "Editor upgraded: .ax files get syntax highlighting (keywords, strings, numbers, comments).",
            "Editor: Tab inserts 4 spaces; Enter auto-indents; status bar shows [Ax] badge.",
            "Browser moved to admin-only in App Store (hidden for regular users).",
        },
        6,
    },
    {
        "v0.4",
        "2026-03-22",
        "Networking, calculator, browser, USB keyboard, more desktop colors.",
        {
            "Added TCP/IP stack: RTL8139, RTL8168, e1000 NIC drivers, DHCP, DNS, HTTP.",
            "Added Browser app: fetch any URL, strips HTML, scrollable text view.",
            "Added Calculator app with integer arithmetic and expression display.",
            "Added PC Speaker with boot chime and error beep.",
            "Fixed USB HID keyboard on real hardware via UHCI second interrupt TD.",
            "Added shell commands: ping, fetch, nslookup, ifconfig, date.",
            "Added Network tab in Control Panel showing IP, MAC, gateway.",
            "Expanded desktop color presets from 6 to 12.",
        },
        8,
    },
    {
        "v0.3",
        "2026-03-22",
        "Filesystem-on-hardware release with safer storage and new desktop tools.",
        {
            "Stabilized FAT32 on real BIOS USB with the FS Lab test path.",
            "Locked user writes to /ROOT so system files stay protected.",
            "Refreshed Files with a Windows-style sidebar and details view.",
            "Added a simple GUI Editor for notes and text files in /ROOT.",
            "Added the OS Info desktop app with built-in release notes.",
        },
        5,
    },
    {
        "v0.2",
        "",
        "Desktop baseline with the core shell, tools, and windowed apps.",
        {
            "Brought up the desktop shell with Terminal, Files, and Control Panel.",
            "Added Task Manager, Snake, shell commands, and diagnostics.",
            "Shipped the original full-screen text editor workflow.",
        },
        3,
    },
};

int changelog_count(void) {
    return (int)(sizeof(g_entries) / sizeof(g_entries[0]));
}

const changelog_entry_t *changelog_entry_at(int index) {
    if (index < 0 || index >= changelog_count()) {
        return 0;
    }
    return &g_entries[index];
}

const changelog_entry_t *changelog_latest(void) {
    return changelog_entry_at(0);
}
