#include "usb/ehci.h"

#include <stdint.h>

#include "cpu/timer.h"
#include "drivers/pci.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include "usb/hid.h"
#include "usb/usb.h"

#define CAP_CAPLENGTH 0u
#define CAP_HCSPARAMS 4u

#define OP_USBCMD   0x00u
#define OP_USBSTS   0x04u
#define OP_USBINTR  0x08u
#define OP_DSSEG    0x10u
#define OP_PERIODIC 0x14u
#define OP_ASYNC    0x18u
#define OP_CONFIG   0x40u
#define OP_PORTSC(N) (0x44u + (uint32_t)(N)*4u)

#define CMD_RS      (1u << 0)
#define CMD_HCRESET (1u << 1)
#define CMD_PSE     (1u << 4)
#define CMD_ASE     (1u << 5)

#define STS_HALTED (1u << 12)

#define PORT_CONN     (1u << 0)
#define PORT_ENABLE   (1u << 2)
#define PORT_RESET    (1u << 8)
#define PORT_POWER    (1u << 12)
#define PORT_LINE_K   (1u << 11)

#define LINK_TERM   1u
#define LINK_QH     (2u << 1)

#define QTD_ACTIVE  (1u << 7)
#define QTD_IOC     (1u << 15)
#define QTD_PID_OUT (0u << 8)
#define QTD_PID_IN  (1u << 8)
#define QTD_PID_SETUP (2u << 8)
#define QTD_HALTED  (1u << 6)

#define QH_EP0_CTL (1u << 27)
#define QH_EPS_FS  (1u << 12)
#define QH_EPS_LS  (2u << 12)

#define FRAMES 1024u

typedef struct {
    uint32_t next;
    uint32_t alt;
    uint32_t token;
    uint32_t buf[5];
} ehci_qtd;

typedef struct {
    uint32_t hlink;
    uint32_t ep;
    uint32_t ep_cap;
    uint32_t curr;
    uint32_t overlay_next;
    uint32_t overlay_alt;
    uint32_t overlay_token;
    uint32_t overlay_buf[5];
} ehci_qh;

typedef struct {
    usb_controller_t *pci;
    volatile uint8_t  *mmio;
    unsigned             caplen;
    unsigned             n_ports;
    unsigned             port_used;
    uint32_t             periodic[FRAMES] __attribute__((aligned(4096)));
    ehci_qh              qh_dummy __attribute__((aligned(64)));
    ehci_qh              qh_xfer __attribute__((aligned(64)));
    ehci_qh              qh_intr __attribute__((aligned(64)));
    ehci_qtd             td_setup __attribute__((aligned(32)));
    ehci_qtd             td_data __attribute__((aligned(32)));
    ehci_qtd             td_stat __attribute__((aligned(32)));
    ehci_qtd             td_intr __attribute__((aligned(32)));
    uint8_t              setup_pkt[8] __attribute__((aligned(64)));
    uint8_t              data_buf[512] __attribute__((aligned(64)));
    uint8_t              rpt[8] __attribute__((aligned(64)));
    uint8_t              kb_prev[6];
    uint8_t              dev_addr;
    uint8_t              cfg_val;
    uint8_t              kb_ep;
    uint8_t              kb_iface;
    uint8_t              kb_mps;
    uint8_t              low_speed;
    uint8_t              kb_ready;
} ehci_host_t;

static ehci_host_t H;

static volatile uint32_t *op32(unsigned off) {
    return (volatile uint32_t *)(H.mmio + H.caplen + off);
}

static uint32_t dma_phys(void *p) {
    return (uint32_t)(uintptr_t)p;
}

static uint32_t qh_link(void *p) {
    uint32_t a = dma_phys(p);
    return (a & ~0x1Fu) | LINK_QH;
}

static void ehci_wait_ms(unsigned ms) {
    uint32_t t0 = timer_get_ticks();
    uint32_t wt = ms / 10u;
    if (wt < 1u) {
        wt = 1u;
    }
    while (timer_get_ticks() - t0 < wt) {
    }
}

static void ehci_stop(void) {
    volatile uint32_t *cmd = op32(OP_USBCMD);
    volatile uint32_t *sts = op32(OP_USBSTS);
    uint32_t c;

    c = *cmd;
    *cmd = c & ~(CMD_RS | CMD_PSE | CMD_ASE);
    ehci_wait_ms(2);
    (void)*sts;
}

static int ehci_reset_controller(void) {
    volatile uint32_t *cmd = op32(OP_USBCMD);
    volatile uint32_t *sts = op32(OP_USBSTS);
    unsigned spin;

    *cmd |= CMD_HCRESET;
    for (spin = 0; spin < 100u; spin++) {
        if ((*cmd & CMD_HCRESET) == 0) {
            break;
        }
        ehci_wait_ms(1);
    }
    if (*cmd & CMD_HCRESET) {
        return -1;
    }
    *sts = 0x3Fu;
    return 0;
}

static uint32_t mk_token(uint32_t len, uint32_t pid, int ioc) {
    uint32_t t;

    t = (len << 16) | pid | QTD_ACTIVE | (3u << 10);
    if (ioc) {
        t |= QTD_IOC;
    }
    return t;
}

static int qtd_done(ehci_qtd *t) {
    unsigned i;

    for (i = 0; i < 400000u; i++) {
        if (!(t->token & QTD_ACTIVE)) {
            if (t->token & QTD_HALTED) {
                return -1;
            }
            return 0;
        }
    }
    return -1;
}

static void qh_make_dummy_ring(void) {
    uint32_t p;

    mem_set(&H.qh_dummy, 0, sizeof(H.qh_dummy));
    p = qh_link(&H.qh_dummy);
    H.qh_dummy.hlink = p;
}

static int ehci_async_submit(ehci_qh *qh) {
    volatile uint32_t *cmd = op32(OP_USBCMD);
    volatile uint32_t *async = op32(OP_ASYNC);
    uint32_t c;

    qh_make_dummy_ring();
    H.qh_dummy.hlink = qh_link(qh);
    qh->hlink = qh_link(&H.qh_dummy);

    *async = dma_phys(&H.qh_dummy);

    c = *cmd;
    *cmd = c | CMD_ASE | CMD_RS;
    ehci_wait_ms(1);
    return 0;
}

static void ehci_async_off(void) {
    volatile uint32_t *cmd = op32(OP_USBCMD);

    *cmd &= ~(CMD_ASE);
    ehci_wait_ms(2);
}

static int run_td(uint32_t ep_char, uint32_t ep_cap, ehci_qtd *td) {
    mem_set(&H.qh_xfer, 0, sizeof(H.qh_xfer));
    H.qh_xfer.hlink = LINK_TERM;
    H.qh_xfer.ep = ep_char;
    H.qh_xfer.ep_cap = ep_cap;
    H.qh_xfer.curr = dma_phys(td);
    td->next = LINK_TERM;
    td->alt = LINK_TERM;

    if (ehci_async_submit(&H.qh_xfer) != 0) {
        return -1;
    }
    if (qtd_done(td) != 0) {
        ehci_async_off();
        return -1;
    }
    ehci_async_off();
    return 0;
}

static int control_xfer(uint32_t ep_char, uint32_t ep_cap,
                        const uint8_t setup[8], uint8_t *data,
                        uint32_t direction_in, uint32_t datalen) {
    mem_copy(H.setup_pkt, setup, 8);

    mem_set(&H.td_setup, 0, sizeof(H.td_setup));
    H.td_setup.buf[0] = dma_phys(H.setup_pkt);
    H.td_setup.token = mk_token(8u, QTD_PID_SETUP, 1u);
    if (run_td(ep_char, ep_cap, &H.td_setup) != 0) {
        return -1;
    }

    if (datalen > 0u) {
        mem_set(&H.td_data, 0, sizeof(H.td_data));
        H.td_data.buf[0] = dma_phys(data);
        H.td_data.token =
            mk_token(datalen, direction_in ? QTD_PID_IN : QTD_PID_OUT, 1u);
        if (run_td(ep_char, ep_cap, &H.td_data) != 0) {
            return -1;
        }
    }

    mem_set(&H.td_stat, 0, sizeof(H.td_stat));
    H.td_stat.token =
        mk_token(0u, direction_in ? QTD_PID_OUT : QTD_PID_IN, 1u);
    if (run_td(ep_char, ep_cap, &H.td_stat) != 0) {
        return -1;
    }

    return 0;
}

static uint32_t qh_ep0_char(uint32_t addr, uint32_t mps, int ls) {
    uint32_t spd = ls ? QH_EPS_LS : QH_EPS_FS;
    return (addr & 0x7Fu) | spd | ((mps & 0x7FFu) << 16) | QH_EP0_CTL;
}

static uint32_t qh_intr_char(uint32_t addr, uint32_t ep, uint32_t mps, int ls) {
    uint32_t spd = ls ? QH_EPS_LS : QH_EPS_FS;
    return (addr & 0x7Fu) | ((ep & 0xFu) << 8) | spd | ((mps & 0x7FFu) << 16);
}

static int parse_kb_endpoint(const uint8_t *cfg, int total,
                             uint8_t *out_ep, uint8_t *out_mps, uint8_t *out_iface) {
    int i = 0;
    int in_kbd_iface = 0;
    uint8_t iface_num = 0;

    if (total > 256) {
        total = 256;
    }
    while (i < total) {
        uint8_t len = cfg[i];
        uint8_t type;

        if (len < 2) {
            break;
        }
        type = cfg[i + 1];
        if (type == 4u) {
            in_kbd_iface =
                (cfg[i + 5] == 3u && (cfg[i + 6] == 1u || cfg[i + 6] == 0u) &&
                 cfg[i + 7] == 1u);
            iface_num = cfg[i + 2];
        } else if (type == 5u && in_kbd_iface) {
            uint8_t epaddr = cfg[i + 2];
            uint8_t attr = cfg[i + 3];
            uint16_t mps =
                (uint16_t)cfg[i + 4] | ((uint16_t)cfg[i + 5] << 8);
            if ((epaddr & 0x80u) && ((attr & 3u) == 3u)) {
                *out_ep = epaddr & 0x0Fu;
                *out_mps = (uint8_t)(mps > 8 ? 8 : mps);
                *out_iface = iface_num;
                return 1;
            }
        }
        i += len;
    }
    return 0;
}

static int ehci_enumerate_keyboard(unsigned port_idx) {
    uint32_t portsc_off = OP_PORTSC(port_idx);
    volatile uint32_t *portsc = op32(portsc_off);
    uint32_t ps;
    uint32_t ep_cap = (((uint32_t)port_idx + 1u) << 16) | (3u << 30);
    uint8_t mps0 = 8;
    int ls;

    ps = *portsc;
    if (!(ps & PORT_CONN)) {
        return -1;
    }

    ls = ((ps & PORT_LINE_K) != 0) ? 1 : 0;
    H.low_speed = (uint8_t)ls;

    *portsc |= PORT_POWER;
    ehci_wait_ms(10);

    *portsc |= PORT_RESET;
    ehci_wait_ms(50);
    *portsc &= ~PORT_RESET;
    ehci_wait_ms(20);

    ps = *portsc;
    if (!(ps & PORT_CONN) || !(ps & PORT_ENABLE)) {
        return -1;
    }

    {
        uint8_t getdev[8] = {
            0x80u, 6u, 0, 0, 0, 0, 8, 0,
        };
        uint32_t ec = qh_ep0_char(0, (uint32_t)mps0, ls);

        if (control_xfer(ec, ep_cap, getdev, H.data_buf, 1u, 8u) != 0) {
            return -1;
        }
        mps0 = H.data_buf[7];
        if (mps0 == 0) {
            mps0 = 8;
        }
    }

    {
        uint8_t setaddr[8] = {
            0u, 5u, 1, 0, 0, 0, 0, 0,
        };
        uint32_t ec = qh_ep0_char(0, (uint32_t)mps0, ls);

        if (control_xfer(ec, ep_cap, setaddr, H.data_buf, 0u, 0u) != 0) {
            return -1;
        }
    }

    H.dev_addr = 1;
    ehci_wait_ms(10);

    {
        uint8_t getcfg[8] = {
            0x80u, 6u, 0, 1, 0, 0, 64, 0,
        };
        uint32_t ec = qh_ep0_char((uint32_t)H.dev_addr, (uint32_t)mps0, ls);

        mem_set(H.data_buf, 0, sizeof(H.data_buf));
        if (control_xfer(ec, ep_cap, getcfg, H.data_buf, 1u, 64u) != 0) {
            return -1;
        }
        H.cfg_val = H.data_buf[5];
    }

    if (!parse_kb_endpoint(H.data_buf, 64, &H.kb_ep, &H.kb_mps, &H.kb_iface)) {
        uint16_t tot_le =
            (uint16_t)H.data_buf[2] | ((uint16_t)H.data_buf[3] << 8);
        uint32_t want = (uint32_t)sizeof(H.data_buf);
        uint8_t getbig[8] = {
            0x80u, 6u, 0, 1, 0, 0, 0, 0,
        };

        if (tot_le > 0 && tot_le < want) {
            want = tot_le;
        }
        getbig[6] = (uint8_t)(want & 0xFFu);
        getbig[7] = (uint8_t)((want >> 8) & 0xFFu);

        {
            uint32_t ec = qh_ep0_char((uint32_t)H.dev_addr, (uint32_t)mps0, ls);

            mem_set(H.data_buf, 0, sizeof(H.data_buf));
            if (control_xfer(ec, ep_cap, getbig, H.data_buf, 1u, want) != 0) {
                return -1;
            }
            if (!parse_kb_endpoint(H.data_buf, (int)want, &H.kb_ep, &H.kb_mps,
                                   &H.kb_iface)) {
                return -1;
            }
        }
    }

    {
        uint8_t setcfg[8] = {
            0u, 9u, H.cfg_val, 0, 0, 0, 0, 0,
        };
        uint32_t ec = qh_ep0_char((uint32_t)H.dev_addr, (uint32_t)mps0, ls);

        if (control_xfer(ec, ep_cap, setcfg, H.data_buf, 0u, 0u) != 0) {
            return -1;
        }
    }

    {
        uint8_t setprot[8] = {
            0x21u, 0x0Bu, 0, 0, H.kb_iface, 0, 0, 0,
        };
        uint32_t ec = qh_ep0_char((uint32_t)H.dev_addr, (uint32_t)mps0, ls);

        if (control_xfer(ec, ep_cap, setprot, H.data_buf, 0u, 0u) != 0) {
            return -1;
        }
    }

    mem_set(H.kb_prev, 0, sizeof(H.kb_prev));

    mem_set(&H.qh_intr, 0, sizeof(H.qh_intr));
    H.qh_intr.hlink = LINK_TERM;
    H.qh_intr.ep = qh_intr_char((uint32_t)H.dev_addr, (uint32_t)H.kb_ep,
                                (uint32_t)H.kb_mps, ls);
    H.qh_intr.ep_cap = ep_cap;

    mem_set(&H.td_intr, 0, sizeof(H.td_intr));
    H.td_intr.next = LINK_TERM;
    H.td_intr.alt = LINK_TERM;
    H.td_intr.token = mk_token(8u, QTD_PID_IN, 1);
    H.td_intr.buf[0] = dma_phys(H.rpt);

    H.qh_intr.curr = dma_phys(&H.td_intr);

    {
        unsigned f;

        for (f = 0; f < FRAMES; f++) {
            H.periodic[f] =
                (f % 8u == 0u) ? qh_link(&H.qh_intr) : LINK_TERM;
        }
    }

    {
        volatile uint32_t *perbase = op32(OP_PERIODIC);
        volatile uint32_t *cmd = op32(OP_USBCMD);

        *perbase = dma_phys(H.periodic);
        *cmd |= CMD_PSE | CMD_RS;
    }

    H.kb_ready = 1;
    H.port_used = port_idx;
    return 0;
}

void ehci_attach(usb_controller_t *ctrl) {
    uint32_t bar;
    volatile uint32_t *hcsp;
    unsigned n_port;
    unsigned p;

    if (!ctrl || ctrl->kind != USB_CTRL_EHCI) {
        return;
    }
    if (H.pci != 0) {
        ctrl->ready = 0;
        return;
    }

    pci_enable_busmaster(ctrl->bus, ctrl->dev, ctrl->func);

    bar = ctrl->bar0 & 0xFFFFFFF0u;
    if (bar == 0u) {
        ctrl->ready = 0;
        return;
    }

    H.mmio = (volatile uint8_t *)(uintptr_t)bar;
    H.caplen = (unsigned)(*(volatile uint8_t *)H.mmio);
    hcsp = (volatile uint32_t *)(H.mmio + CAP_HCSPARAMS);
    n_port = (unsigned)(*hcsp & 0xFu);

    H.pci = ctrl;
    H.n_ports = n_port;
    mem_set(H.periodic, 0, sizeof(H.periodic));
    mem_set(H.kb_prev, 0, sizeof(H.kb_prev));
    H.kb_ready = 0;
    H.dev_addr = 0;

    *op32(OP_DSSEG) = 0;

    ehci_stop();
    if (ehci_reset_controller() != 0) {
        ctrl->ready = 0;
        H.pci = 0;
        return;
    }

    mem_set(H.periodic, 0, sizeof(H.periodic));

    *op32(OP_CONFIG) = 1u;
    ehci_wait_ms(10);

    for (p = 0; p < n_port; p++) {
        if (ehci_enumerate_keyboard(p) == 0) {
            break;
        }
    }

    if (!H.kb_ready) {
        ehci_stop();
        ctrl->ready = 0;
        H.pci = 0;
        serial_write("[ehci] no HID keyboard\n");
        return;
    }

    ctrl->ready = 1;
    ctrl->supports_control = 1;
    ctrl->supports_interrupt = 1;
    serial_write("[ehci] keyboard OK\n");

    {
        usb_device_t devinfo;

        mem_set(&devinfo, 0, sizeof(devinfo));
        devinfo.controller_kind = USB_CTRL_EHCI;
        devinfo.bus = ctrl->bus;
        devinfo.dev = ctrl->dev;
        devinfo.func = ctrl->func;
        devinfo.supports_interrupt = 1;
        devinfo.iface_class = 3;
        devinfo.iface_subclass = 1;
        devinfo.iface_protocol = 1;
        usb_register_device(&devinfo);
    }
}

void ehci_poll(usb_controller_t *ctrl) {
    uint32_t tok;

    if (!ctrl || ctrl != H.pci || !H.kb_ready) {
        return;
    }

    tok = H.td_intr.token;
    if (!(tok & QTD_ACTIVE)) {
        if (!(tok & QTD_HALTED)) {
            usb_hid_boot_keyboard_process(H.rpt, H.kb_prev);
        }
        mem_set(H.rpt, 0, sizeof(H.rpt));
        H.td_intr.token = mk_token(8u, QTD_PID_IN, 1);
        H.td_intr.buf[0] = dma_phys(H.rpt);
        H.qh_intr.curr = dma_phys(&H.td_intr);
    }
}
