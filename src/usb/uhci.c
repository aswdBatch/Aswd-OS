#include "usb/uhci.h"

#include <stdint.h>

#include "cpu/ports.h"
#include "drivers/mouse.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include "usb/hid.h"

/* ---- UHCI I/O register offsets ---- */
#define USBCMD    0x00u
#define USBSTS    0x02u
#define USBINTR   0x04u
#define USBFRNUM  0x06u
#define USBFLBASE 0x08u
#define USBSOFMOD 0x0Cu
#define USBPORT0  0x10u
#define USBPORT1  0x12u

/* USBCMD */
#define CMD_RS    0x0001u
#define CMD_HCRST 0x0002u
#define CMD_GRST  0x0004u
#define CMD_MAXP  0x0080u

/* USBSTS */
#define STS_HCH   0x0020u

/* PORTSC */
#define PORT_CONN   0x0001u
#define PORT_CSCNG  0x0002u
#define PORT_PEN    0x0004u
#define PORT_PENCNG 0x0008u
#define PORT_LSDA   0x0100u
#define PORT_RST    0x0200u

/* TD link pointer bits */
#define TD_LINK_TERM 0x00000001u
#define TD_LINK_QH   0x00000002u
#define TD_LINK_VF   0x00000004u

/* TD Control/Status bit 23 = Active */
#define TD_STS_ACTIVE  (1u << 23)
#define TD_STS_LS      (1u << 26)
#define TD_STS_CERR(n) ((uint32_t)(n) << 27)
#define TD_STS_ERR     ((1u<<17)|(1u<<18)|(1u<<20)|(1u<<21)|(1u<<22))

/* PIDs */
#define PID_SETUP 0x2Du
#define PID_IN    0x69u
#define PID_OUT   0xE1u

/* USB standard requests */
#define REQ_GET_DESC   0x06u
#define REQ_SET_ADDR   0x05u
#define REQ_SET_CFG    0x09u
#define REQ_SET_PROTO  0x0Bu   /* HID class */

/* Descriptor types */
#define DESC_DEVICE 0x01u
#define DESC_CONFIG 0x02u

/* ---- Data structures ---- */

typedef struct __attribute__((aligned(16))) {
    uint32_t link;
    uint32_t ctrl_sts;
    uint32_t token;
    uint32_t buf_ptr;
} uhci_td_t;

typedef struct __attribute__((aligned(16))) {
    uint32_t horiz;
    uint32_t vert;
} uhci_qh_t;

#define MAX_CTRL   4u
#define TD_POOL_SZ 32u
#define QH_POOL_SZ 4u

static uint32_t  g_frame_list[MAX_CTRL][1024] __attribute__((aligned(4096)));
static uhci_td_t g_td[MAX_CTRL][TD_POOL_SZ]   __attribute__((aligned(16)));
static uhci_qh_t g_qh[MAX_CTRL][QH_POOL_SZ]   __attribute__((aligned(16)));
static uint8_t   g_setup[MAX_CTRL][8]          __attribute__((aligned(8)));
static uint8_t   g_data[MAX_CTRL][64]          __attribute__((aligned(64)));
static uint8_t   g_rpt[MAX_CTRL][8]            __attribute__((aligned(8)));
static uint8_t   g_kb_rpt[MAX_CTRL][8]         __attribute__((aligned(8)));

/* Per-controller state */
static unsigned  g_nctrl     = 0;
static uint16_t  g_iobase[MAX_CTRL];
static int       g_active[MAX_CTRL];    /* 1 = mouse found on this ctrl */
static int       g_kb_active[MAX_CTRL]; /* 1 = keyboard found on this ctrl */
static int       g_ls[MAX_CTRL];        /* 1 = low-speed device */
static uint8_t   g_devaddr[MAX_CTRL];
static uint8_t   g_endpt[MAX_CTRL];
static uint8_t   g_maxpkt[MAX_CTRL];
static int       g_toggle[MAX_CTRL];
static unsigned  g_irq_td[MAX_CTRL];    /* interrupt TD index for mouse */
static uint8_t   g_prev_btns[MAX_CTRL];
/* keyboard-specific per-controller state */
static uint8_t   g_kb_endpt[MAX_CTRL];
static uint8_t   g_kb_maxpkt[MAX_CTRL];
static int       g_kb_toggle[MAX_CTRL];
static unsigned  g_kb_irq_td[MAX_CTRL]; /* interrupt TD index for keyboard */
static uint8_t   g_kb_prev[MAX_CTRL][6]; /* previous key codes */

/* ---- Register I/O helpers ---- */
static inline uint16_t reg_r16(uint16_t base, uint16_t off) { return inw((uint16_t)(base+off)); }
static inline void      reg_w16(uint16_t base, uint16_t off, uint16_t v) { outw((uint16_t)(base+off), v); }
static inline void      reg_w32(uint16_t base, uint16_t off, uint32_t v) { outl((uint16_t)(base+off), v); }

/* ---- TD/QH address helpers ---- */
static uint32_t td_phys(unsigned ci, unsigned ti) { return (uint32_t)(uintptr_t)&g_td[ci][ti]; }
static uint32_t qh_phys(unsigned ci, unsigned qi) { return (uint32_t)(uintptr_t)&g_qh[ci][qi]; }

/* TD status bits 0-10: actual length; 0 = full maxlen packet for successful IN. */
static unsigned uhci_td_in_length(uint32_t ctrl_sts, unsigned maxlen) {
    if (ctrl_sts & TD_STS_ERR) {
        return 0;
    }
    unsigned act = (unsigned)(ctrl_sts & 0x7FFu);
    if (act == 0) {
        return maxlen > 8u ? 8u : maxlen;
    }
    return act > 8u ? 8u : act;
}

/* Build TD token word */
static uint32_t make_token(uint8_t pid, uint8_t addr, uint8_t ep,
                            int toggle, int maxlen) {
    uint32_t ml = (maxlen == 0) ? 0x7FFu : (uint32_t)(maxlen - 1);
    return (uint32_t)pid
         | ((uint32_t)(addr & 0x7Fu) << 8)
         | ((uint32_t)(ep   & 0x0Fu) << 15)
         | ((uint32_t)(toggle & 1)   << 19)
         | (ml << 21);
}

/* ---- Simple spin-delay ---- */
static void uhci_delay(unsigned loops) {
    volatile unsigned i;
    for (i = 0; i < loops; i++) (void)reg_r16(0x80, 0);
}

/* ---- Controller reset ---- */
static int uhci_reset(uint16_t base) {
    int i;
    reg_w16(base, USBCMD, CMD_HCRST);
    for (i = 0; i < 50000; i++) {
        if (!(reg_r16(base, USBCMD) & CMD_HCRST)) break;
        uhci_delay(100);
    }
    if (reg_r16(base, USBCMD) & CMD_HCRST) return 0;
    reg_w16(base, USBCMD,  0);
    reg_w16(base, USBSTS,  0x3Fu);  /* clear all status bits */
    reg_w16(base, USBINTR, 0);
    return 1;
}

/* ---- Port reset ---- */
static void port_reset(uint16_t base, uint16_t port_off) {
    reg_w16(base, port_off, PORT_RST);
    uhci_delay(500000);   /* ~50 ms */
    reg_w16(base, port_off, 0);
    uhci_delay(50000);
    /* Clear any change bits */
    reg_w16(base, port_off, PORT_CSCNG | PORT_PENCNG);
    uhci_delay(10000);
    /* Enable the port */
    reg_w16(base, port_off, PORT_PEN);
    uhci_delay(50000);
}

/* ---- Synchronous control transfer ---- *
 * Sets up TDs from index 0 in pool, runs them, waits, returns 0 on success. */

static void build_setup_td(unsigned ci, unsigned ti_start,
                            uint8_t addr, uint8_t *setup8) {
    uhci_td_t *td = &g_td[ci][ti_start];
    mem_copy(g_setup[ci], setup8, 8);
    td->link     = TD_LINK_TERM;   /* updated below if chain */
    td->ctrl_sts = TD_STS_ACTIVE
                 | TD_STS_CERR(3)
                 | (g_ls[ci] ? TD_STS_LS : 0u);
    td->token    = make_token(PID_SETUP, addr, 0, 0, 8);
    td->buf_ptr  = (uint32_t)(uintptr_t)g_setup[ci];
}

static void build_in_td(unsigned ci, unsigned ti, uint8_t addr,
                         int toggle, int maxlen, void *buf) {
    uhci_td_t *td = &g_td[ci][ti];
    td->link     = TD_LINK_TERM;
    td->ctrl_sts = TD_STS_ACTIVE
                 | TD_STS_CERR(3)
                 | (g_ls[ci] ? TD_STS_LS : 0u);
    td->token    = make_token(PID_IN, addr, 0, toggle, maxlen);
    td->buf_ptr  = (uint32_t)(uintptr_t)buf;
}

static void build_out_td(unsigned ci, unsigned ti, uint8_t addr,
                          int toggle, int maxlen, void *buf) {
    uhci_td_t *td = &g_td[ci][ti];
    td->link     = TD_LINK_TERM;
    td->ctrl_sts = TD_STS_ACTIVE
                 | TD_STS_CERR(3)
                 | (g_ls[ci] ? TD_STS_LS : 0u);
    td->token    = make_token(PID_OUT, addr, 0, toggle, maxlen);
    td->buf_ptr  = (uint32_t)(uintptr_t)buf;
}

/* Link TDs into a chain: td[0]->td[1]->...->td[n-1] (terminate) */
static void chain_tds(unsigned ci, unsigned ti_start, unsigned count) {
    unsigned i;
    for (i = 0; i < count - 1; i++) {
        g_td[ci][ti_start + i].link = td_phys(ci, ti_start + i + 1);
    }
}

/* Wait for all TDs in range [ti_start, ti_start+count) to go inactive */
static int wait_tds(unsigned ci, unsigned ti_start, unsigned count) {
    unsigned i;
    int loops = 500000;
    while (loops-- > 0) {
        int all_done = 1;
        for (i = 0; i < count; i++) {
            if (g_td[ci][ti_start + i].ctrl_sts & TD_STS_ACTIVE) {
                all_done = 0;
                break;
            }
        }
        if (all_done) break;
        uhci_delay(10);
    }
    /* Check for errors */
    for (i = 0; i < count; i++) {
        if (g_td[ci][ti_start + i].ctrl_sts & TD_STS_ERR) return -1;
    }
    return 0;
}

/* Insert TD chain via skeleton QH 0 into frame list, run, wait, remove */
static int run_control(unsigned ci, unsigned ti_start, unsigned n_tds) {
    uint16_t base = g_iobase[ci];
    unsigned f;

    /* Chain TDs */
    chain_tds(ci, ti_start, n_tds);

    /* QH 0: horizontal=terminate, vertical=first TD */
    g_qh[ci][0].horiz = TD_LINK_TERM;
    g_qh[ci][0].vert  = td_phys(ci, ti_start);

    /* Insert QH into every frame for reliable delivery */
    for (f = 0; f < 1024u; f++) {
        g_frame_list[ci][f] = qh_phys(ci, 0) | TD_LINK_QH;
    }

    /* Start controller */
    reg_w16(base, USBCMD, CMD_RS | CMD_MAXP);
    uhci_delay(200);

    int rc = wait_tds(ci, ti_start, n_tds);

    /* Stop and restore empty frame list */
    reg_w16(base, USBCMD, 0);
    for (f = 0; f < 1024u; f++) g_frame_list[ci][f] = TD_LINK_TERM;

    return rc;
}

/* ---- USB GET_DEVICE_DESCRIPTOR (first 8 bytes) ---- */
static int usb_get_desc8(unsigned ci, uint8_t addr) {
    uint8_t setup[8] = { 0x80, REQ_GET_DESC, 0x00, DESC_DEVICE,
                         0x00, 0x00, 8, 0x00 };
    build_setup_td(ci, 0, addr, setup);
    build_in_td(ci, 1, addr, 1, 8, g_data[ci]);
    build_out_td(ci, 2, addr, 1, 0, 0);
    return run_control(ci, 0, 3);
}

/* ---- USB GET_FULL_DEVICE_DESCRIPTOR (18 bytes) ---- */
static int usb_get_desc18(unsigned ci, uint8_t addr) {
    uint8_t setup[8] = { 0x80, REQ_GET_DESC, 0x00, DESC_DEVICE,
                         0x00, 0x00, 18, 0x00 };
    /* Use maxpkt for first data packet */
    int mp = (int)g_data[ci][7];  /* bMaxPacketSize0 from previous 8-byte read */
    if (mp < 8) mp = 8;
    if (mp > 64) mp = 64;
    build_setup_td(ci, 0, addr, setup);
    build_in_td(ci, 1, addr, 1, mp, g_data[ci]);
    build_out_td(ci, 2, addr, 1, 0, 0);
    return run_control(ci, 0, 3);
}

/* ---- USB SET_ADDRESS ---- */
static int usb_set_address(unsigned ci, uint8_t old_addr, uint8_t new_addr) {
    uint8_t setup[8] = { 0x00, REQ_SET_ADDR, new_addr, 0x00,
                         0x00, 0x00, 0x00, 0x00 };
    build_setup_td(ci, 0, old_addr, setup);
    build_in_td(ci, 1, old_addr, 1, 0, 0);   /* status: IN DATA1 zero-length */
    return run_control(ci, 0, 2);
}

/* ---- USB GET_CONFIGURATION_DESCRIPTOR ---- */
static int usb_get_config_desc(unsigned ci, uint8_t addr) {
    uint8_t setup[8] = { 0x80, REQ_GET_DESC, 0x00, DESC_CONFIG,
                         0x00, 0x00, 64, 0x00 };
    build_setup_td(ci, 0, addr, setup);
    build_in_td(ci, 1, addr, 1, 64, g_data[ci]);
    build_out_td(ci, 2, addr, 1, 0, 0);
    return run_control(ci, 0, 3);
}

/* ---- USB SET_CONFIGURATION ---- */
static int usb_set_config(unsigned ci, uint8_t addr, uint8_t cfg_val) {
    uint8_t setup[8] = { 0x00, REQ_SET_CFG, cfg_val, 0x00,
                         0x00, 0x00, 0x00, 0x00 };
    build_setup_td(ci, 0, addr, setup);
    build_in_td(ci, 1, addr, 1, 0, 0);
    return run_control(ci, 0, 2);
}

/* ---- USB HID SET_PROTOCOL (boot protocol = 0) ---- */
static int usb_set_boot_protocol(unsigned ci, uint8_t addr, uint8_t iface) {
    uint8_t setup[8] = { 0x21, REQ_SET_PROTO, 0x00, 0x00,
                         iface, 0x00, 0x00, 0x00 };
    build_setup_td(ci, 0, addr, setup);
    build_in_td(ci, 1, addr, 1, 0, 0);
    return run_control(ci, 0, 2);
}

/* ---- Parse config descriptor for HID mouse endpoint ---- *
 * Looks for: Interface class=0x03 (HID), subclass=0x01 (boot), protocol=0x02 (mouse)
 * Returns 1 if found, and fills ep/maxpkt/iface_idx. */
static int find_mouse_endpoint(unsigned ci, uint8_t *out_ep,
                                uint8_t *out_maxpkt, uint8_t *out_iface) {
    uint8_t *buf = g_data[ci];
    int total = (int)buf[2] | ((int)buf[3] << 8);
    int i = 0;
    int in_mouse_iface = 0;
    uint8_t iface_num = 0;

    if (total > 64) total = 64;
    while (i < total) {
        uint8_t len  = buf[i];
        uint8_t type = (len > 1) ? buf[i + 1] : 0;
        if (len < 2) break;

        if (type == 0x04u) {   /* Interface descriptor */
            in_mouse_iface = (buf[i+5] == 0x03u &&                        /* HID */
                              (buf[i+6] == 0x01u || buf[i+6] == 0x00u) && /* boot or generic */
                              buf[i+7] == 0x02u);                          /* mouse */
            iface_num = buf[i + 2];
        } else if (type == 0x05u && in_mouse_iface) {  /* Endpoint */
            uint8_t addr   = buf[i + 2];
            uint8_t attrs  = buf[i + 3];
            uint16_t mps   = (uint16_t)buf[i + 4] | ((uint16_t)buf[i + 5] << 8);
            if ((addr & 0x80u) && (attrs & 0x03u) == 0x03u) { /* IN + interrupt */
                *out_ep     = addr & 0x0Fu;
                *out_maxpkt = (uint8_t)(mps > 8 ? 8 : mps);
                *out_iface  = iface_num;
                return 1;
            }
        }
        i += len;
    }
    return 0;
}

/* ---- Parse config descriptor for HID keyboard endpoint ---- *
 * Looks for: Interface class=0x03, subclass=0x01, protocol=0x01 (keyboard) */
static int find_keyboard_endpoint(unsigned ci, uint8_t *out_ep,
                                  uint8_t *out_maxpkt, uint8_t *out_iface) {
    uint8_t *buf = g_data[ci];
    int total = (int)buf[2] | ((int)buf[3] << 8);
    int i = 0;
    int in_kbd_iface = 0;
    uint8_t iface_num = 0;

    if (total > 64) total = 64;
    while (i < total) {
        uint8_t len  = buf[i];
        uint8_t type = (len > 1) ? buf[i + 1] : 0;
        if (len < 2) break;

        if (type == 0x04u) {   /* Interface descriptor */
            in_kbd_iface = (buf[i+5] == 0x03u &&                         /* HID */
                            (buf[i+6] == 0x01u || buf[i+6] == 0x00u) && /* boot or generic */
                            buf[i+7] == 0x01u);                          /* keyboard */
            iface_num = buf[i + 2];
        } else if (type == 0x05u && in_kbd_iface) {  /* Endpoint */
            uint8_t addr   = buf[i + 2];
            uint8_t attrs  = buf[i + 3];
            uint16_t mps   = (uint16_t)buf[i + 4] | ((uint16_t)buf[i + 5] << 8);
            if ((addr & 0x80u) && (attrs & 0x03u) == 0x03u) { /* IN + interrupt */
                *out_ep     = addr & 0x0Fu;
                *out_maxpkt = (uint8_t)(mps > 8 ? 8 : mps);
                *out_iface  = iface_num;
                return 1;
            }
        }
        i += len;
    }
    return 0;
}

static void parse_primary_interface(unsigned ci, uint8_t *out_class,
                                    uint8_t *out_subclass, uint8_t *out_protocol) {
    uint8_t *buf = g_data[ci];
    int total = (int)buf[2] | ((int)buf[3] << 8);
    int i = 0;

    *out_class = 0;
    *out_subclass = 0;
    *out_protocol = 0;
    if (total > 64) total = 64;

    while (i + 8 < total) {
        uint8_t len  = buf[i];
        uint8_t type = (len > 1) ? buf[i + 1] : 0;
        if (len < 2) break;
        if (type == 0x04u) {
            *out_class = buf[i + 5];
            *out_subclass = buf[i + 6];
            *out_protocol = buf[i + 7];
            return;
        }
        i += len;
    }
}

/* ---- Set up interrupt-polling TD and insert into frame list ---- */
static void setup_interrupt_td(unsigned ci) {
    unsigned ti = TD_POOL_SZ - 1;   /* last slot reserved for interrupt */
    uint32_t f;

    g_irq_td[ci] = ti;

    uhci_td_t *td = &g_td[ci][ti];
    td->link     = TD_LINK_TERM;
    td->ctrl_sts = TD_STS_ACTIVE
                 | TD_STS_CERR(3)
                 | (g_ls[ci] ? TD_STS_LS : 0u);
    td->token    = make_token(PID_IN, g_devaddr[ci], g_endpt[ci],
                              g_toggle[ci], g_maxpkt[ci]);
    td->buf_ptr  = (uint32_t)(uintptr_t)g_rpt[ci];

    /* Insert into QH 1 */
    g_qh[ci][1].horiz = TD_LINK_TERM;
    g_qh[ci][1].vert  = td_phys(ci, ti);

    /* Link QH 1 into every 8th frame for ~8 ms polling interval */
    for (f = 0; f < 1024u; f++) {
        if (f % 8u == 0u)
            g_frame_list[ci][f] = qh_phys(ci, 1) | TD_LINK_QH;
        else
            g_frame_list[ci][f] = TD_LINK_TERM;
    }
}

/* ---- Set up keyboard interrupt-polling TD (slot TD_POOL_SZ-2, QH 2) ---- */
static void setup_kb_interrupt_td(unsigned ci) {
    unsigned ti = TD_POOL_SZ - 2;
    uint32_t f;

    g_kb_irq_td[ci] = ti;

    uhci_td_t *td = &g_td[ci][ti];
    td->link     = TD_LINK_TERM;
    td->ctrl_sts = TD_STS_ACTIVE
                 | TD_STS_CERR(3)
                 | (g_ls[ci] ? TD_STS_LS : 0u);
    td->token    = make_token(PID_IN, g_devaddr[ci], g_kb_endpt[ci],
                              g_kb_toggle[ci], g_kb_maxpkt[ci]);
    td->buf_ptr  = (uint32_t)(uintptr_t)g_kb_rpt[ci];

    /* Insert into QH 2 */
    g_qh[ci][2].horiz = TD_LINK_TERM;
    g_qh[ci][2].vert  = td_phys(ci, ti);

    /* Link QH 2 into every 8th frame (offset by 4 to interleave with mouse) */
    for (f = 0; f < 1024u; f++) {
        if (f % 8u == 4u)
            g_frame_list[ci][f] = qh_phys(ci, 2) | TD_LINK_QH;
    }
}

static void process_kb_report(unsigned ci) {
    usb_hid_boot_keyboard_process(g_kb_rpt[ci], g_kb_prev[ci]);
}

/* ---- Enumerate one port ---- *
 * Returns 1 if a HID mouse was successfully enumerated, 0 otherwise. */
static int enumerate_port(unsigned ci, uint16_t port_off) {
    uint16_t base  = g_iobase[ci];
    uint8_t  iface = 0;
    usb_device_t devinfo;

    /* Check connection */
    uint16_t psc = reg_r16(base, port_off);
    if (!(psc & PORT_CONN)) return 0;

    /* Detect speed */
    g_ls[ci] = (psc & PORT_LSDA) ? 1 : 0;

    /* Reset port */
    port_reset(base, port_off);

    psc = reg_r16(base, port_off);
    if (!(psc & PORT_CONN) || !(psc & PORT_PEN)) return 0;

    /* Assign address 1 */
    if (usb_set_address(ci, 0, 1) != 0) return 0;
    uhci_delay(20000);
    g_devaddr[ci] = 1;

    /* Read first 8 bytes of device descriptor */
    if (usb_get_desc8(ci, 1) != 0) return 0;

    /* Read full device descriptor */
    (void)usb_get_desc18(ci, 1);

    /* Read configuration descriptor */
    if (usb_get_config_desc(ci, 1) != 0) return 0;

    mem_set(&devinfo, 0, sizeof(devinfo));
    devinfo.controller_kind = USB_CTRL_UHCI;
    devinfo.controller_index = (uint8_t)ci;
    devinfo.vendor_id = (uint16_t)g_data[ci][8] | ((uint16_t)g_data[ci][9] << 8);
    devinfo.product_id = (uint16_t)g_data[ci][10] | ((uint16_t)g_data[ci][11] << 8);
    devinfo.device_class = g_data[ci][4];
    devinfo.device_subclass = g_data[ci][5];
    devinfo.device_protocol = g_data[ci][6];
    devinfo.supports_control = 1;
    devinfo.supports_interrupt = 1;
    devinfo.supports_bulk = 0;
    parse_primary_interface(ci, &devinfo.iface_class, &devinfo.iface_subclass, &devinfo.iface_protocol);

    /* Check for HID mouse endpoint */
    uint8_t kb_iface = 0;
    int has_mouse = find_mouse_endpoint(ci, &g_endpt[ci], &g_maxpkt[ci], &iface);
    int has_kbd   = find_keyboard_endpoint(ci, &g_kb_endpt[ci], &g_kb_maxpkt[ci], &kb_iface);

    usb_register_device(&devinfo);

    if (!has_mouse && !has_kbd) return 0;

    /* SET_CONFIGURATION with first config value */
    uint8_t cfg_val = g_data[ci][5];
    if (usb_set_config(ci, 1, cfg_val) != 0) return 0;

    if (has_mouse) {
        (void)usb_set_boot_protocol(ci, 1, iface);
        g_toggle[ci]    = 0;
        g_prev_btns[ci] = 0;
        g_active[ci]    = 1;
    }
    if (has_kbd) {
        (void)usb_set_boot_protocol(ci, 1, kb_iface);
        g_kb_toggle[ci] = 0;
        unsigned j;
        for (j = 0; j < 6; j++) g_kb_prev[ci][j] = 0;
        g_kb_active[ci] = 1;
    }
    return (has_mouse || has_kbd);
}

/* ---- Public: attach ---- */
void uhci_attach(usb_controller_t *ctrl) {
    unsigned ci;
    uint16_t base;

    if (!ctrl) return;
    if (g_nctrl >= MAX_CTRL) { ctrl->ready = 0; return; }

    /* BAR0 for UHCI is an I/O BAR; lower 16 bits = I/O base */
    base = (uint16_t)(ctrl->bar0 & 0xFFFFu);
    if (base < 0x100u) { ctrl->ready = 0; return; }

    ci = g_nctrl;

    if (!uhci_reset(base)) { ctrl->ready = 0; return; }

    g_iobase[ci]    = base;
    g_active[ci]    = 0;
    g_kb_active[ci] = 0;
    ctrl->supports_control = 1;
    ctrl->supports_interrupt = 1;
    ctrl->supports_bulk = 0;

    /* Set up empty frame list */
    unsigned f;
    for (f = 0; f < 1024u; f++) g_frame_list[ci][f] = TD_LINK_TERM;
    reg_w32(base, USBFLBASE, (uint32_t)(uintptr_t)g_frame_list[ci]);
    reg_w16(base, USBFRNUM,  0);
    reg_w16(base, USBSOFMOD, 64);

    /* Try each port (don't stop early — keyboard may be on port 1 even if no mouse on port 0) */
    uint16_t ports[2] = { USBPORT0, USBPORT1 };
    unsigned p;
    int started = 0;
    for (p = 0; p < 2u; p++) {
        if (enumerate_port(ci, ports[p])) {
            if (g_active[ci] && !started) {
                setup_interrupt_td(ci);
                started = 1;
            }
            if (g_kb_active[ci]) {
                setup_kb_interrupt_td(ci);
                started = 1;
            }
        }
    }
    if (g_kb_active[ci]) serial_write("[uhci] keyboard enumerated\n");
    else                  serial_write("[uhci] keyboard NOT found\n");
    if (g_active[ci])    serial_write("[uhci] mouse enumerated\n");
    else                  serial_write("[uhci] mouse NOT found\n");

    if (started) {
        reg_w16(base, USBCMD, CMD_RS | CMD_MAXP);
    }

    g_nctrl++;
    ctrl->ready = 1;
}

/* ---- Public: poll ---- */
void uhci_poll(usb_controller_t *ctrl) {
    unsigned ci;
    uint16_t base;

    if (!ctrl || !ctrl->ready) return;
    base = (uint16_t)(ctrl->bar0 & 0xFFFFu);

    /* Find the matching controller slot */
    for (ci = 0; ci < g_nctrl; ci++) {
        if (g_iobase[ci] == base) break;
    }
    if (ci >= g_nctrl) return;

    /* ---- Mouse TD ---- */
    if (g_active[ci]) {
        uhci_td_t *td = &g_td[ci][g_irq_td[ci]];
        if (!(td->ctrl_sts & TD_STS_ACTIVE)) {
            if (td->ctrl_sts & TD_STS_ERR) {
                g_toggle[ci] = 0;
            } else {
                unsigned rlen = uhci_td_in_length(td->ctrl_sts, (unsigned)g_maxpkt[ci]);
                int dx = 0, dy = 0;
                uint8_t btns = 0;
                int8_t wheel = 0;
                if (rlen < 8u) {
                    mem_set(g_rpt[ci] + rlen, 0, (size_t)(8u - rlen));
                }
                if (rlen >= 3u) {
                    usb_hid_parse_boot_mouse(g_rpt[ci], rlen, &dx, &dy, &btns, &wheel);
                }
                if (dx != 0 || dy != 0 || wheel != 0 || btns != g_prev_btns[ci]) {
                    mouse_push_usb_event(dx, -(int)dy, btns, wheel);
                    g_prev_btns[ci] = btns;
                }
                g_toggle[ci] ^= 1;
            }
            td->ctrl_sts = TD_STS_ACTIVE | TD_STS_CERR(3) | (g_ls[ci] ? TD_STS_LS : 0u);
            td->token    = make_token(PID_IN, g_devaddr[ci], g_endpt[ci],
                                      g_toggle[ci], g_maxpkt[ci]);
            /* Re-link TD into QH — hardware sets QH.vert=TERM on completion */
            g_qh[ci][1].vert = td_phys(ci, g_irq_td[ci]);
        }
    }

    /* ---- Keyboard TD ---- */
    if (g_kb_active[ci]) {
        uhci_td_t *ktd = &g_td[ci][g_kb_irq_td[ci]];
        if (!(ktd->ctrl_sts & TD_STS_ACTIVE)) {
            if (ktd->ctrl_sts & TD_STS_ERR) {
                g_kb_toggle[ci] = 0;
            } else {
                process_kb_report(ci);
                g_kb_toggle[ci] ^= 1;
            }
            ktd->ctrl_sts = TD_STS_ACTIVE | TD_STS_CERR(3) | (g_ls[ci] ? TD_STS_LS : 0u);
            ktd->token    = make_token(PID_IN, g_devaddr[ci], g_kb_endpt[ci],
                                       g_kb_toggle[ci], g_kb_maxpkt[ci]);
            /* Re-link TD into QH — hardware sets QH.vert=TERM on completion */
            g_qh[ci][2].vert = td_phys(ci, g_kb_irq_td[ci]);
        }
    }
}
