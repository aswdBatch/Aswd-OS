#include "common/changelog.h"

static const changelog_entry_t g_entries[] = {
    {
        "v0.8",
        "2026-03-28",
        "Automated CI test harness with GitHub Actions.",
        {
            "Added a GitHub Actions workflow that builds the OS on every push and pull request, boots it headlessly in QEMU, and runs a suite of unit tests via the serial port.",
            "Kernel now detects a 'test' multiboot command-line argument and enters a headless test mode that skips the GUI entirely.",
            "Test suite covers: string library (str_len, str_copy, str_cat, str_ncmp, mem_copy, mem_set, u32_to_dec, split_args), shell command dispatch, VFS read/write/delete round-trips, the Ax interpreter, the Calculator evaluator, and the 180 Work document/sheet/slide data model.",
            "Tests output [PASS]/[FAIL] lines to COM1 serial and exit QEMU via the ISA debug-exit port so CI can check the process exit code.",
        },
        4,
    },
    {
        "v0.8",
        "2026-03-28",
        "Boot no longer probes Wi-Fi hardware before the desktop is reachable.",
        {
            "Moved the new Wi-Fi manager off the early boot path so real hardware no longer risks crashing or bouncing back to PXE-style fallback before login.",
            "The Control Panel Network tab now brings Wi-Fi hardware detection online on demand instead of doing PCI, USB, and /WIFI.CFG work during startup.",
            "Wi-Fi profile reads and writes also restore IRQ1 after BIOS-backed storage access so opening the Network tab does not reintroduce the old keyboard-masked behavior.",
        },
        3,
    },
    {
        "v0.8",
        "2026-03-28",
        "Control Panel now has an honest Wi-Fi manager with saved profiles and adapter detection.",
        {
            "The Network tab now shows wired and Wi-Fi status from one shared model, including transport, connection state, detected adapter family, and whether the Wi-Fi backend is actually ready.",
            "Added first-wave Wi-Fi manager scaffolding for Intel 2200/2915, Intel 3945, Atheros AR5xxx, Broadcom BCM43xx, and Realtek RTL8187 USB family detection, plus saved-profile storage in /WIFI.CFG for open, WEP, and WPA2-PSK networks.",
            "USB controller reporting is now honest about transport support, so OHCI, EHCI, and xHCI no longer appear ready when the code path only has UHCI control and interrupt transfers today.",
            "This release lays down the manager, UI, and detection path without pretending that unsupported or not-yet-implemented Wi-Fi association backends are already online.",
        },
        4,
    },
    {
        "v0.8",
        "2026-03-28",
        "Shared shell styling is cleaner and more consistent across graphics-mode UI.",
        {
            "Retuned the shared theme palette, shadows, card depth, spacing, tabs, fields, buttons, status strips, and shell sizing so the desktop, taskbar, Control Panel, auth screens, and apps feel more like one system.",
            "Control Panel now uses denser cards and info strips, the shell metrics give menus and taskbar items more consistent hit targets, and Browser network status reflects the newer transport-aware model.",
        },
        2,
    },
    {
        "v0.8",
        "2026-03-28",
        "Closing the start menu now really closes it.",
        {
            "Split the start menu's logical-open state from its visual motion so a close action cannot leave a ghosted menu onscreen anymore.",
            "Taskbar highlighting, shell focus, and attached start-menu child popups now follow the logical closed state immediately instead of acting like the menu is still active.",
        },
        2,
    },
    {
        "v0.8",
        "2026-03-28",
        "Login keeps keyboard input alive through the boot-to-auth handoff.",
        {
            "The GUI boot path now waits until after sign-in before starting the PS/2 mouse, so the login and first-boot setup screens stay on the simpler keyboard-only controller path.",
            "The shared keyboard reader also gained a polled PS/2 fallback that can read raw controller scancodes when real hardware accepts a key or two and then stops delivering IRQ-driven input.",
            "This keeps the login account picker, PIN entry, and first-boot name field responsive on systems where boot-time keyboard input worked but the auth handoff was fragile.",
        },
        3,
    },
    {
        "v0.8",
        "2026-03-28",
        "Login and first-boot input stay responsive on real hardware.",
        {
            "Reworked the login and first-boot setup screens to use a lighter auth-only backdrop instead of the heavier full-screen desktop scene.",
            "Those auth screens now redraw just their centered panel after keys are pressed, which avoids repainting the whole framebuffer for every selection, PIN change, or setup edit.",
            "Added low-volume serial breadcrumbs around auth screen entry, drawing, present, and accepted keys so real-hardware login issues are easier to confirm.",
        },
        3,
    },
    {
        "v0.8",
        "2026-03-27",
        "Login theme loading no longer leaves the keyboard IRQ stuck off.",
        {
            "Fixed the themed login and first-boot setup backdrop so reading the saved desktop theme restores IRQ1 after BIOS-backed VFS access.",
            "This prevents real hardware from reaching the auth screen and then acting like the keyboard stopped working after UI-only theme changes.",
        },
        2,
    },
    {
        "v0.8",
        "2026-03-27",
        "Keyboard reads now keep USB input alive across the whole OS.",
        {
            "Fixed USB keyboards appearing dead after login: the shared keyboard read path now polls USB HID before reporting that no key is available.",
            "This keeps the desktop, shell, explorer, and permission prompts responsive instead of making the UI look hung while input is actually waiting to be serviced.",
        },
        2,
    },
    {
        "v0.8",
        "2026-03-27",
        "USB keyboard and mouse real-hardware input fixes.",
        {
            "Fixed USB keyboard not working on real hardware: the UHCI device enumeration now accepts HID keyboards that advertise generic subclass (0x00) in addition to the boot subclass (0x01), matching the same fix already applied to USB mice.",
            "Fixed USB mouse and keyboard polling stopping after the first USB transaction: UHCI QH.vert is now explicitly re-linked after each completed TD so the hardware can find the reactivated transfer descriptor on subsequent frames.",
            "USB input is no longer dead during the login and first-boot setup screens: both loops now call usb_poll() so keyboards and mice are serviced before the desktop is reached.",
            "A20 enable in the stage-2 bootloader now uses a non-zero sentinel (0xAA55) for the aliasing check, preventing false negatives that caused the keyboard-controller A20 method to run unnecessarily on hardware where INT 15h already succeeded.",
        },
        4,
    },
    {
        "v0.8",
        "2026-03-26",
        "Added stage-2 boot breadcrumbs and more conservative BIOS disk chunks.",
        {
            "The USB BIOS stage-2 loader now shows simple progress markers on screen so early real-hardware hangs can be narrowed to disk load, video setup, or the protected-mode handoff.",
            "Stage-2 kernel reads were reduced from 64-sector chunks to 32-sector chunks to be friendlier to stricter BIOS USB storage implementations.",
        },
        2,
    },
    {
        "v0.8",
        "2026-03-26",
        "USB BIOS boot now streams large kernels safely on real hardware.",
        {
            "Fixed the USB BIOS stage-2 loader so it no longer tries to buffer the whole kernel below 1 MB before jumping to protected mode.",
            "The loader now reads the kernel in chunks, copies each chunk up to 0x00100000 immediately, and then continues with the next read.",
            "This prevents newer larger kernel builds from overrunning the old low-memory bounce buffer and hanging early on real hardware.",
        },
        3,
    },
    {
        "v0.8",
        "2026-03-26",
        "Quick start-menu header spacing fix.",
        {
            "Fixed the start menu so the welcome header keeps its own space and no longer collides with the search box at the top of the panel.",
        },
        1,
    },
    {
        "v0.8",
        "2026-03-26",
        "Responsive GUI cleanup, safer start-menu popups, and lightweight shell motion.",
        {
            "Added shared responsive layout helpers and content-width buckets so graphics-mode apps can reflow from the live window body instead of piling new UI onto old fixed offsets.",
            "Gave the shell short fade and slide motion for start, search, child popups, and window open or restore, while keeping dragging and resizing immediate so the software framebuffer stays responsive.",
            "Fixed the start-menu footer popup flow so power and user menus stay attached as child popups instead of collapsing the whole start menu first.",
            "Cleaned up the login and first-boot setup screens so the title stack, account list, and footer no longer overlap or fade into each other.",
            "Refreshed Control Panel, App Store, OS Info, Browser, Files, Notes, Terminal, and Task Manager around safer minimum sizes and more resize-friendly content regions.",
            "Reworked Control Panel theme cards, Network allowed-sites editing, OS Info release notes, and App Store rows so they stay inside their windows instead of spilling or colliding at wider and narrower sizes.",
        },
        6,
    },
    {
        "v0.8",
        "2026-03-26",
        "Modernized shell chrome, better login visuals, richer Browser UI, and curated background themes.",
        {
            "Reworked the desktop shell with new abstract background themes, sharper window chrome, an updated taskbar, and a hybrid start menu that feels less like painted VGA blocks.",
            "Fixed the start-menu footer popups so the power and user submenus open as child menus instead of collapsing the whole start menu first.",
            "Refreshed Task Manager, Control Panel, and OS Info onto denser shared layouts with page headers, info strips, cards, and less empty white space.",
            "Rebuilt the login and first-boot setup screens around the shared auth-card styling and the same themed backdrop system used by the desktop.",
            "Upgraded Browser with a real navigation shell, history-aware back and forward controls, clearer network status, and a first-step HTML-lite renderer instead of only stripped plain text.",
            "Added background theme selection to Display settings so desktop and login visuals can switch together between Mint, Glass, Studio, Sunset, Ocean, and Geometric variants.",
        },
        6,
    },
    {
        "v0.8",
        "2026-03-26",
        "Persistent allowed-site list for Browser and fetch.",
        {
            "Added a persistent allowed-site list backed by /TLSALLOW.CFG so you can manage approved hosts instead of hardcoding them.",
            "Browser and shell fetch now share the same host gate, so once a site is added it is allowed in both places automatically.",
            "Control Panel now lets you add and remove allowed sites from the Network tab using a host name or full URL.",
            "Adding a base domain like lovable.dev also allows its subdomains, so common site variants work without duplicate entries.",
        },
        4,
    },
    {
        "v0.8",
        "2026-03-26",
        "Adwaita UI fonts, icon assets, and fixed start-menu footer popups.",
        {
            "Added atlas-backed Adwaita Sans and Adwaita Mono rendering for the graphics shell, with committed generated assets and dev-only generators instead of runtime font parsing.",
            "Added a real icon asset system with vendored Papirus app icons plus Fluent and custom symbolic shell icons, and wired it into the desktop, start menu, taskbar, window chrome, search, and context menus.",
            "Fixed the broken start-menu footer menus by making the power and user controls share one geometry source for drawing and hit-testing, then opening their popups on mouse release instead of mouse press.",
            "Terminal window console sizing now follows mono font metrics instead of assuming the old 8x16 VGA cell size, so rows and columns track the actual GUI font.",
            "Refreshed the shell toward a Mint plus Windows style mix with cleaner app icons, more deliberate window controls, and less placeholder-looking menu chrome.",
        },
        5,
    },
    {
        "v0.8",
        "2026-03-26",
        "Shared power control and cleaner composited shell chrome.",
        {
            "Shutdown no longer falls through to a reset path when ACPI power-off is unavailable, so the Shutdown option now halts cleanly instead of behaving like a reboot.",
            "GUI, legacy explorer, and shell reboot paths now use one shared power-control module instead of three separate implementations.",
            "Added framebuffer alpha fills, gradients, and transparent text drawing so the desktop shell can render real shadows, layered panels, and less flat chrome.",
            "Refreshed the desktop background, taskbar, start menu, window frames, and shared widget surfaces to use the new compositing primitives for a cleaner GUI baseline.",
        },
        4,
    },
    {
        "v0.8",
        "2026-03-26",
        "QEMU BGA framebuffer corruption fix for rounded scanline widths.",
        {
            "Fixed the QEMU desktop rendering bug where the screen sheared diagonally after switching to wider modes.",
            "The BGA driver now reads back the actual visible size and virtual scanline width after mode set instead of assuming pitch equals requested width times four.",
            "Graphics init now accepts sane rounded BGA modes and commits the real framebuffer geometry, so odd widths like 1366 no longer corrupt the desktop.",
        },
        3,
    },
    {
        "v0.8",
        "2026-03-26",
        "Responsive BIOS framebuffer UI, shared widgets, and scale-aware desktop chrome.",
        {
            "The GUI now boots through a 32-bit framebuffer mode chain that prefers 1366x768, 1280x800, 1280x720, 1024x768, then 800x600 instead of forcing one fixed size.",
            "Added display profiles with aspect buckets and density tiers so shell sizing adapts cleanly across 4:3, 16:10, and 16:9 displays.",
            "Built a shared widget and theme layer with scalable text, toolbars, dialogs, lists, status bars, fields, tabs, cards, and scrollbars.",
            "Desktop chrome now follows display metrics: title bars, taskbar, start menu, search, desktop icons, hit targets, and minimum window sizes all scale with the framebuffer.",
            "Refreshed core apps onto the shared UI layer, including login/setup, permission prompts, Files, Browser, App Store, Task Manager, OS Info, Terminal, Notes, AX Code, and Control Panel.",
            "AX Studio, AX App, and 180 Work slide surfaces now fit fixed-aspect content inside letterboxed viewports instead of relying on one-off scaling paths.",
            "Expanded terminal window console capacity so larger framebuffers can show more columns and rows without the old 800x600-era cap.",
        },
        7,
    },
    {
        "v0.8",
        "2026-03-26",
        "Permission system, networking improvements, graphics primitives, and power fixes.",
        {
            "Added UAC-style permission prompt: privileged actions now show a darkened overlay dialog asking for the admin PIN (9898 for devacc).",
            "Added screen-darken effect, Bresenham line drawing, and outline-rectangle primitives to the graphics layer.",
            "Fixed Shutdown and Restart: they now show a status message and try multiple reset methods (PS/2, CF9 chipset, triple fault) before giving up.",
            "Fixed ARP to block and retry for up to 1 second instead of silently dropping the first packet to any new IP.",
            "Added TCP retransmission for unACKed segments (3 retries, 2-second timer) and out-of-order segment handling.",
            "Increased TCP RX buffer from 4 KB to 8 KB for more reliable large-page downloads.",
            "Browser now follows HTTP 301/302 redirects (up to 5 hops) and supports chunked transfer-encoding.",
        },
        7,
    },
    {
        "v0.7",
        "2026-03-22",
        "180 Work replaces AX Code, and AX Studio keeps improving for visual app building.",
        {
            "Fixed AX Studio so the toolbar, toolbox, canvas, and logic view respond to clicks and drags again.",
            "Run previews now keep textbox input and Set Text actions in sync, so AX apps update visibly at runtime.",
            "AX Studio and AX App now scale forms down to fit smaller windows, so resizing stays usable.",
            "Logic nodes now point at readable control IDs like button1, and title bars now include maximize.",
            "AX Studio now lets you type control IDs directly, and logic refs can target controls by typed ID too.",
            "Added AX Studio: design forms with Button, Label, TextBox, Checkbox controls.",
            "Multi-scene support: add/remove scenes and switch between them at design time.",
            "Logic tab: place trigger/action/condition blocks and wire them together.",
            "Triggers: OnClick, OnStart, OnSubmit, OnToggle. Actions: SetText, Show/Hide, SwitchScene, Enable/Disable. Conditions: IfTextEq, IfChecked.",
            "Save/load .ax projects to /ROOT; Run opens live window; axapp shell command.",
            "Replaced AX Code with 180 Work: Docs saves text files, while Sheets and Slides live together in one editor app.",
        },
        11,
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
