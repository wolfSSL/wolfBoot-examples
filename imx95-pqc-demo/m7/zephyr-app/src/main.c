/*
 * wolfBoot-aware Zephyr RPMsg payload for the i.MX95 Cortex-M7.
 *
 * Brings up the M7 side of the RPMsg link that wolfBoot's resource table
 * declared to Linux, and relays the M7's shared-memory console - wolfBoot's own
 * verify log included - to the A55 as a virtual TTY.
 *
 *
 * Why this is not samples/subsys/ipc/openamp_rsc_table
 * ---------------------------------------------------
 * That sample assumes Linux loaded *its* ELF. The standard remoteproc contract
 * is that the remote declares vrings with da = FW_RSC_ADDR_ANY, Linux allocates
 * them, and Linux writes the resolved addresses and the virtio status back into
 * the resource table it loaded. The remote then reads its own table.
 *
 * Here Linux loads wolfBoot, not this payload. So Linux writes the resolved
 * values into wolfBoot's .resource_table, while the sample polls the table
 * linked into its own image - two different structures. The sample gets as far
 * as rproc_virtio_wait_remote_ready() and waits forever for a status nobody
 * will ever write.
 *
 * CONFIG_OPENAMP_COPY_RSC_TABLE looks like the bridge and is not: it memcpy's
 * the payload's own unresolved table over the shared location before reading it
 * back.
 *
 * The fix is to stop relying on host writeback. Both sides use FIXED addresses:
 * wolfBoot's table hardcodes them, so Linux honours them, and this payload uses
 * the same constants. They come from the board's own reserved-memory nodes, so
 * the two sides agree by construction rather than by convention:
 *
 *   vdev0vring0   0x88000000   32 KiB
 *   vdev0vring1   0x88008000   32 KiB
 *   vdevbuffer    0x88020000    1 MiB
 *
 * Because the addresses are fixed there is nothing to wait for, and the host is
 * ready by construction: Linux registers virtio0 during rproc_start, before it
 * releases the core that eventually runs this code.
 *
 * KEEP IN SYNC with the resource table in wolfBoot's hal/imx95_m7.c. If the
 * vring geometry there changes, change it here too - a mismatch shows up as
 * silence, not as an error.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>

#include <openamp/open_amp.h>
#include <metal/sys.h>
#include <metal/io.h>
#include <metal/device.h>

#include <zephyr/cache.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wolfboot_openamp, LOG_LEVEL_INF);

/* --- Must match wolfBoot's resource table (hal/imx95_m7.c) --- */
#define VRING0_DA        0x88000000UL   /* host -> remote */
#define VRING1_DA        0x88008000UL   /* remote -> host */
#define VRING_ALIGN      0x1000U
#define VRING_NUM        256U

/* vrings plus the 1 MiB vdevbuffer at 0x88020000 */
#define SHM_BASE         0x88000000UL
#define SHM_SIZE         0x00120000UL

#define VDEV_ID          0xFFU
#define VRING0_ID        0U
#define VRING1_ID        1U

/* --- wolfBoot's shared-memory console (hal/imx95_m7.h) --- */
#define CONSOLE_BASE     0x80F00000UL
#define CONSOLE_HDR_SIZE 16U
#define CONSOLE_MAGIC    0x4E4F4357UL   /* "WCON" */

/* The Linux imx_rpmsg_tty driver binds this name and creates /dev/ttyRPMSG*. */
#define TTY_CHANNEL_NAME "rpmsg-virtual-tty-channel"

/* Progress published to wolfBoot's app status block, read from Linux with
 * "memtool r 0x80F10010 4". The RAM console proved unreliable for this - a
 * fault can halt the core before anything is flushed - so the state goes to a
 * plain shared word instead:
 *
 *   +0x00  magic 'ZOAM'
 *   +0x04  progress: 1 platform_init, 2 vdev up, 3 endpoint created
 *   +0x08  heartbeat, increments every loop pass
 *   +0x0C  1 once the endpoint has a destination (host has replied)
 */
#define APP_STATUS_ADDR  0x80F10010UL
#define APP_STATUS_MAGIC 0x5A4F414DUL   /* "ZOAM" */

/* The M7 runs with D-cache enabled (wolfBoot turns it on), so a plain store
 * lands in cache and the A55 never sees it. wolfBoot's own console code cleans
 * the cache after every write for exactly this reason; do the same here.
 * Without it the status words read back as zero from Linux - or worse, appear
 * intermittently as lines happen to get evicted. */
static void app_status(uint32_t idx, uint32_t val)
{
    volatile uint32_t *st = (volatile uint32_t *)APP_STATUS_ADDR;

    st[0] = APP_STATUS_MAGIC;
    st[idx] = val;
    sys_cache_data_flush_range((void *)APP_STATUS_ADDR, 32);
}

struct console_hdr {
    volatile uint32_t magic;
    volatile uint32_t wr;    /* total bytes ever written, monotonic */
    volatile uint32_t size;
    volatile uint32_t rsvd;
};

/* rproc_virtio_create_vdev() only reads the header fields of this structure;
 * the vrings are supplied separately to rproc_virtio_init_vring(). They are
 * declared anyway so the layout matches a real resource table entry.
 *
 * status is pre-set to DRIVER_OK rather than waiting for the host to write it:
 * see the file header for why there is no writeback to wait for. */
struct local_vdev_rsc {
    struct fw_rsc_vdev vdev;
    struct fw_rsc_vdev_vring vring[2];
};

static struct local_vdev_rsc vdev_rsc = {
    .vdev = {
        .type = RSC_VDEV,
        .id = VIRTIO_ID_RPMSG,
        .notifyid = 0,
        .dfeatures = 1U << VIRTIO_RPMSG_F_NS,
        .gfeatures = 1U << VIRTIO_RPMSG_F_NS,
        .config_len = 0,
        .status = VIRTIO_CONFIG_STATUS_DRIVER_OK,
        .num_of_vrings = 2,
    },
    .vring = {
        { VRING0_DA, VRING_ALIGN, VRING_NUM, VRING0_ID, 0 },
        { VRING1_DA, VRING_ALIGN, VRING_NUM, VRING1_ID, 0 },
    },
};

static const struct device *const ipm_handle = DEVICE_DT_GET(DT_CHOSEN(zephyr_ipc));

/* A fault here halts the core with nothing useful in the RAM console - the log
 * backend does not get a chance to flush. Publish the reason and PC to the
 * status block instead, which is a plain store and always works:
 *
 *   +0x10  fault reason (Zephyr K_ERR_*)
 *   +0x14  faulting PC
 *   +0x18  0xDEADBEEF marker that a fault happened at all
 */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    volatile uint32_t *st = (volatile uint32_t *)APP_STATUS_ADDR;

    st[4] = (uint32_t)reason;
    st[5] = (esf != NULL) ? (uint32_t)esf->basic.pc : 0xFFFFFFFFU;
    st[6] = 0xDEADBEEFU;
    sys_cache_data_flush_range((void *)APP_STATUS_ADDR, 32);

    for (;;) {
        /* halt - leave the state readable from the A55 */
    }
}

static metal_phys_addr_t shm_physmap[] = { SHM_BASE };
static metal_phys_addr_t rsc_physmap[] = { (metal_phys_addr_t)(uintptr_t)&vdev_rsc };

static struct metal_io_region shm_io_data;
static struct metal_io_region rsc_io_data;
static struct metal_io_region *shm_io = &shm_io_data;
static struct metal_io_region *rsc_io = &rsc_io_data;

static struct rpmsg_virtio_device rvdev;
static struct rpmsg_endpoint tty_ept;

static K_SEM_DEFINE(kick_sem, 0, 1);

static void ipm_callback(const struct device *dev, void *context,
                         uint32_t id, volatile void *data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(context);
    ARG_UNUSED(id);
    ARG_UNUSED(data);
    k_sem_give(&kick_sem);
}

static int mailbox_notify(void *priv, uint32_t id)
{
    ARG_UNUSED(priv);
    ipm_send(ipm_handle, 0, id, &id, sizeof(id));
    return 0;
}

/* Set when the host writes to the TTY, asking for the console log again.
 *
 * The relay flushes the whole ring the moment the endpoint has a destination
 * address, which is before any reader can realistically be attached: the
 * /dev/ttyRPMSG node does not exist until the host has bound the channel, and
 * by the time a reader opens it the one-shot replay has already been sent and
 * dropped. Treating any inbound byte as a replay request makes the log
 * retrievable on demand, so "attach a reader, then poke the TTY" works every
 * time instead of racing the bind. */
static volatile bool replay_req;
static uint32_t rx_count;

static int tty_recv(struct rpmsg_endpoint *ept, void *data, size_t len,
                    uint32_t src, void *priv)
{
    ARG_UNUSED(ept);
    ARG_UNUSED(data);
    ARG_UNUSED(src);
    ARG_UNUSED(priv);
    LOG_DBG("rx %u bytes from the host, replaying console",
            (unsigned int)len);
    rx_count++;
    app_status(10, rx_count);
    replay_req = true;
    return RPMSG_SUCCESS;
}

static void ns_bind_cb(struct rpmsg_device *rdev, const char *name, uint32_t src)
{
    ARG_UNUSED(rdev);
    ARG_UNUSED(src);
    LOG_INF("host announced service '%s'", name);
}

static int platform_init(void)
{
    struct metal_init_params params = METAL_INIT_DEFAULTS;
    int ret;

    ret = metal_init(&params);
    if (ret != 0) {
        LOG_ERR("metal_init failed: %d", ret);
        return ret;
    }

    metal_io_init(shm_io, (void *)SHM_BASE, shm_physmap, SHM_SIZE, -1, 0, NULL);
    metal_io_init(rsc_io, &vdev_rsc, rsc_physmap, sizeof(vdev_rsc), -1, 0, NULL);

    if (!device_is_ready(ipm_handle)) {
        LOG_ERR("IPM device not ready");
        return -ENODEV;
    }

    ipm_register_callback(ipm_handle, ipm_callback, NULL);

    ret = ipm_set_enabled(ipm_handle, 1);
    if (ret != 0) {
        LOG_ERR("ipm_set_enabled failed: %d", ret);
        return ret;
    }

    return 0;
}

static struct rpmsg_device *rpmsg_start(void)
{
    struct virtio_device *vdev;
    int ret;

    /* VIRTIO_DEV_DEVICE: this core is the device/remote, Linux is the driver. */
    vdev = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE, VDEV_ID, &vdev_rsc.vdev,
                                    rsc_io, NULL, mailbox_notify, NULL);
    if (vdev == NULL) {
        LOG_ERR("rproc_virtio_create_vdev failed");
        return NULL;
    }

    /* No rproc_virtio_wait_remote_ready() here on purpose - the status it polls
     * is written into wolfBoot's table, not this one, and the host is already
     * up by the time this code runs. */

    ret = rproc_virtio_init_vring(vdev, 0, VRING0_ID, (void *)VRING0_DA,
                                  shm_io, VRING_NUM, VRING_ALIGN);
    if (ret != 0) {
        LOG_ERR("init vring 0 failed: %d", ret);
        return NULL;
    }

    ret = rproc_virtio_init_vring(vdev, 1, VRING1_ID, (void *)VRING1_DA,
                                  shm_io, VRING_NUM, VRING_ALIGN);
    if (ret != 0) {
        LOG_ERR("init vring 1 failed: %d", ret);
        return NULL;
    }

    ret = rpmsg_init_vdev(&rvdev, vdev, ns_bind_cb, shm_io, NULL);
    if (ret != 0) {
        LOG_ERR("rpmsg_init_vdev failed: %d", ret);
        return NULL;
    }

    return rpmsg_virtio_get_rpmsg_device(&rvdev);
}

/* Copy whatever is new in wolfBoot's console ring out to the endpoint.
 *
 * The ring publishes `wr` as a monotonic count of bytes ever written, so the
 * reader keeps its own position and copies (wr - pos) from data[pos % size].
 * Starting pos at 0 means the first pass replays everything already in the
 * buffer, which is the point: wolfBoot's verify log is written long before any
 * RPMsg endpoint exists, and replaying it is the only way the A55 ever sees it.
 */
static void relay_console(struct rpmsg_endpoint *ept, uint32_t *pos)
{
    const struct console_hdr *h = (const struct console_hdr *)CONSOLE_BASE;
    const volatile uint8_t *data =
        (const volatile uint8_t *)(CONSOLE_BASE + CONSOLE_HDR_SIZE);
    static uint32_t sent_total;
    uint8_t chunk[256];
    uint32_t wr, size, avail, i;
    int ret;

    /* wolfBoot wrote this ring with its own cache cleans, but this core may
     * still hold stale lines for it, so invalidate before every read. */
    sys_cache_data_invd_range((void *)CONSOLE_BASE, CONSOLE_HDR_SIZE);

    if (h->magic != CONSOLE_MAGIC) {
        return;
    }

    size = h->size;
    wr = h->wr;
    if (size == 0U || wr == *pos) {
        return;
    }

    avail = wr - *pos;
    if (avail > size) {
        /* Reader fell behind; report the gap rather than emit corrupt text. */
        static const char msg[] = "\r\n[console overrun - output dropped]\r\n";

        (void)rpmsg_send(ept, msg, sizeof(msg) - 1U);
        *pos = wr - size;
        avail = size;
    }

    sys_cache_data_invd_range((void *)(CONSOLE_BASE + CONSOLE_HDR_SIZE), size);

    while (avail > 0U) {
        uint32_t n = (avail > sizeof(chunk)) ? (uint32_t)sizeof(chunk) : avail;

        for (i = 0; i < n; i++) {
            chunk[i] = data[(*pos + i) % size];
        }

        ret = rpmsg_send(ept, chunk, (int)n);
        app_status(7, (uint32_t)ret);       /* last send result */
        app_status(9, avail);               /* bytes still pending */
        if (ret < 0) {
            /* Host buffer full: leave the rest for the next pass. */
            return;
        }
        sent_total += n;
        app_status(8, sent_total);

        *pos += n;
        avail -= n;
    }
}

int main(void)
{
    struct rpmsg_device *rpdev;
    uint32_t pos = 0;
    uint32_t beat = 0;
    int ret;

    LOG_INF("wolfBoot RPMsg payload starting");

    ret = platform_init();
    if (ret != 0) {
        return ret;
    }
    app_status(1, 1);

    rpdev = rpmsg_start();
    if (rpdev == NULL) {
        return -EIO;
    }
    app_status(1, 2);

    ret = rpmsg_create_ept(&tty_ept, rpdev, TTY_CHANNEL_NAME,
                           RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, tty_recv, NULL);
    if (ret != 0) {
        LOG_ERR("rpmsg_create_ept failed: %d", ret);
        return ret;
    }

    app_status(1, 3);
    LOG_INF("endpoint '%s' announced", TTY_CHANNEL_NAME);

    while (1) {
        /* Service the host's kick if one arrived, then push console output. */
        if (k_sem_take(&kick_sem, K_MSEC(50)) == 0) {
            rproc_virtio_notified(rvdev.vdev, VRING1_ID);
        }

        /* Only relay once the host has bound the endpoint and a destination
         * address is known. Sending earlier fails, and the failed send makes
         * OpenAMP re-announce the name service - which shows up on the Linux
         * side as "creating channel ... already exist" repeating forever and
         * stops the tty driver from ever attaching. */
#ifdef RELAY_DISABLED
        (void)pos;
#else
        app_status(2, ++beat);
        app_status(3, is_rpmsg_ept_ready(&tty_ept) ? 1U : 0U);
        app_status(11, tty_ept.dest_addr);

        if (is_rpmsg_ept_ready(&tty_ept)) {
            if (replay_req) {
                replay_req = false;
                pos = 0;    /* rewind; relay_console reports any lost span */
            }
            relay_console(&tty_ept, &pos);
        }
#endif
    }

    return 0;
}
