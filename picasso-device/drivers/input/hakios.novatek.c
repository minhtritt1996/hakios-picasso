/*
 * HakiOS Novatek NT36672C Touch Screen Driver for Xiaomi Redmi K30 5G (picasso / SM7250).
 *
 * Hardware:
 *   - Controller: Novatek NT36672C
 *   - Bus: Qualcomm GENI SE7 SPI @ 0x00984000 (QUP wrapper 1 @ 0x009c0000)
 *   - Clocks: GCC GPLL0 vote @ 0x00052010, QUP branch votes @ 0x00052008, SE7 DFS RCG @ 0x00018140
 *   - Pins:
 *       Reset: GPIO 4 in TLMM EAST tile (0x0F900000 + 0x1000 * 4)
 *       IRQ:   GPIO 5 in TLMM EAST tile (0x0F900000 + 0x1000 * 5)
 *       SPI:   GPIO 6..9 (CLK, MISO, MOSI, CS) in TLMM WEST tile (0x0F500000 + 0x1000 * gpio)
 *   - Panel: 1080 x 2400 pixels
 *   - Interface: posts multi-touch samples directly to /dev/hakios-touch (hakios_touch_post)
 */

#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include <sys/systm.h>

#include "hakios.novatek_fw.h"

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern kern_return_t thread_terminate(thread_t thread);

static void sleep_us(uint32_t us) {
    delay_for_interval(us, NSEC_PER_USEC);
}

static void sleep_ms(uint32_t ms) {
    delay_for_interval(ms, NSEC_PER_MSEC);
}

/* --- Board Constants --- */
#define TB_QUP_WRAPPER_BASE     0x009c0000ULL
#define TB_QUP_WRAPPER_SIZE     0x2000ULL
#define TB_SE_BASE              0x00984000ULL
#define TB_SE_SIZE              0x4000ULL

#define TB_GCC_BASE             0x00100000ULL
#define TB_GCC_SIZE             0x00060000ULL
#define TB_GCC_GPLL0_MODE       0x00000000
#define TB_GCC_BRANCH_M_AHB     0x00018004
#define TB_GCC_BRANCH_S_AHB     0x00018008
#define TB_GCC_BRANCH_S1        0x0001813c
#define TB_GCC_DFS_S1_RCG       0x00018140
#define TB_GCC_BRANCH_CORE      0x00023138
#define TB_GCC_BRANCH_CORE_2X   0x00023140
#define TB_GCC_VOTE_ENABLE      0x00052008
#define TB_GCC_GPLL0_ENABLE     0x00052010

#define TB_VOTE_CORE_2X         (1u << 18)
#define TB_VOTE_CORE            (1u << 19)
#define TB_VOTE_M_AHB           (1u << 20)
#define TB_VOTE_S_AHB           (1u << 21)
#define TB_VOTE_S1              (1u << 23)
#define TB_VOTE_ALL             (TB_VOTE_CORE_2X | TB_VOTE_CORE | TB_VOTE_M_AHB | TB_VOTE_S_AHB | TB_VOTE_S1)
#define TB_CBCR_CLK_OFF         (1u << 31)
#define TB_GPLL0_LOCK           (1u << 31)

#define TB_DFS_LEVELS           8
#define TB_XO_HZ                19200000ULL
#define TB_RCG_CFG              0x4
#define TB_RCG_M                0x8
#define TB_RCG_N                0xc
#define TB_RCG_DFSR_CMD         0x14
#define TB_RCG_PERF_CFG(l)      (0x1c + 4 * (l))
#define TB_RCG_PERF_M(l)        (0x5c + 4 * (l))
#define TB_RCG_PERF_N(l)        (0x9c + 4 * (l))

#define TB_TLMM_EAST_BASE       0x0f900000ULL
#define TB_TLMM_WEST_BASE       0x0f500000ULL
#define TB_TLMM_TILE_SIZE       0x10000ULL

#define TB_GPIO_STRIDE          0x1000
#define TB_TLMM_IO_OFFSET       0x4
#define TB_GPIO_RESET           4
#define TB_GPIO_IRQ             5
#define TB_GPIO_SPI_CLK         6
#define TB_GPIO_SPI_MISO        7
#define TB_GPIO_SPI_MOSI        8
#define TB_GPIO_SPI_CS          9

/* TLMM Pin Config for SM7250 / Lito:
 * bits 1:0: pull (0=none, 1=down, 2=keeper, 3=up)
 * bits 4:2: mux (0=gpio, 1=qup11)
 * bits 8:6: drive strength (2=6mA, 7=16mA)
 * bit 9:    output enable (0=in, 1=out)
 */
#define TB_PIN_CFG_MASK         (0x3u | (0x7u << 2) | (0x7u << 6) | (1u << 9)) /* 0x3df */
#define TB_PIN_CFG_RESET        (3u | (0u << 2) | (7u << 6) | (1u << 9))        /* 0x3c3: GPIO, 16mA, pull-up, output */
#define TB_PIN_CFG_IRQ          (1u | (0u << 2) | (7u << 6) | (0u << 9))        /* 0x1c1: GPIO, 16mA, pull-down, input */
#define TB_PIN_CFG_SPI          (0u | (1u << 2) | (2u << 6) | (0u << 9))        /* 0x084: QUP11, 6mA, no pull, input */

#define TB_SPI_TARGET_HZ        5000000u
#define TB_PANEL_WIDTH_PX       1080
#define TB_PANEL_HEIGHT_PX      2400

/* --- GENI Serial Engine Registers & Constants --- */
#define GENI_FORCE_DEFAULT_REG       0x20
#define GENI_OUTPUT_CTRL             0x24
#define SE_GENI_CGC_CTRL             0x28
#define SE_GENI_STATUS               0x40
#define GENI_SER_M_CLK_CFG           0x48
#define GENI_IF_DISABLE_RO           0x64
#define GENI_FW_REVISION_RO          0x68
#define SE_GENI_CLK_SEL              0x7c
#define SE_GENI_BYTE_GRAN            0x254
#define SE_GENI_DMA_MODE_EN          0x258
#define SE_GENI_TX_PACKING_CFG0      0x260
#define SE_GENI_TX_PACKING_CFG1      0x264
#define SE_GENI_RX_PACKING_CFG0      0x284
#define SE_GENI_RX_PACKING_CFG1      0x288
#define SE_SPI_CPHA                  0x224
#define SE_SPI_LOOPBACK              0x22c
#define SE_SPI_CPOL                  0x230
#define GENI_SPI_CPHA                0x01
#define GENI_SPI_CPOL                0x02
#define GENI_SPI_LSB_FIRST           0x08
#define SE_SPI_DEMUX_OUTPUT_INV      0x24c
#define SE_SPI_DEMUX_SEL             0x250
#define SE_SPI_TRANS_CFG             0x25c
#define SE_SPI_WORD_LEN              0x268
#define SE_SPI_TX_TRANS_LEN          0x26c
#define SE_SPI_RX_TRANS_LEN          0x270
#define SE_GENI_M_CMD0               0x600
#define SE_GENI_M_CMD_CTRL_REG       0x604
#define SE_GENI_M_IRQ_STATUS         0x610
#define SE_GENI_M_IRQ_EN             0x614
#define SE_GENI_M_IRQ_CLEAR          0x618
#define SE_GENI_S_IRQ_EN             0x644
#define SE_GENI_S_IRQ_CLEAR          0x648
#define SE_GENI_TX_FIFOn             0x700
#define SE_GENI_RX_FIFOn             0x780
#define SE_GENI_TX_FIFO_STATUS       0x800
#define SE_GENI_RX_FIFO_STATUS       0x804
#define SE_GENI_TX_WATERMARK_REG     0x80c
#define SE_GENI_RX_WATERMARK_REG     0x810
#define SE_GENI_RX_RFR_WATERMARK_REG 0x814
#define SE_IRQ_EN                    0xe1c
#define SE_HW_PARAM_0                0xe24
#define SE_HW_PARAM_1                0xe28
#define SE_GSI_EVENT_EN              0xe18
#define QUPV3_HW_VER_REG             0x4

#define GENI_PROTO_SPI               1
#define GENI_M_CMD_TX_ONLY           1
#define GENI_M_CMD_RX_ONLY           2
#define GENI_M_CMD_TX_RX             3
#define GENI_M_PARAM_FRAGMENTATION   (1u << 2)

#define GENI_SER_CLK_EN              (1u << 0)
#define GENI_CLK_DIV_SHIFT           4
#define GENI_CLK_DIV_MAX             0xfffu
#define GENI_FIFO_IF_DISABLE         (1u << 0)
#define GENI_FW_PROTO_SHIFT          8
#define GENI_M_OPCODE_SHIFT          27
#define GENI_TRANS_LEN_MASK          0xffffffu
#define GENI_CS_TOGGLE               (1u << 1)
#define GENI_CPHA_BIT                (1u << 0)
#define GENI_CPOL_BIT                (1u << 2)

#define GENI_M_CMD_DONE              (1u << 0)
#define GENI_M_CMD_OVERRUN           (1u << 1)
#define GENI_M_ILLEGAL_CMD           (1u << 2)
#define GENI_M_CMD_FAILURE           (1u << 3)
#define GENI_M_RX_FIFO_RD_ERR        (1u << 24)
#define GENI_M_RX_FIFO_WR_ERR        (1u << 25)
#define GENI_M_RX_FIFO_WATERMARK     (1u << 26)
#define GENI_M_RX_FIFO_LAST          (1u << 27)
#define GENI_M_TX_FIFO_RD_ERR        (1u << 28)
#define GENI_M_TX_FIFO_WR_ERR        (1u << 29)
#define GENI_M_TX_FIFO_WATERMARK     (1u << 30)
#define GENI_M_COMMON_IRQ_EN         (0x7eu | GENI_M_RX_FIFO_RD_ERR | GENI_M_RX_FIFO_WR_ERR | GENI_M_TX_FIFO_RD_ERR | GENI_M_TX_FIFO_WR_ERR)
#define GENI_S_COMMON_IRQ_EN         (0x3eu | 0x3e00u | (1u << 24) | (1u << 25))
#define GENI_M_ERRORS                (GENI_M_CMD_OVERRUN | GENI_M_ILLEGAL_CMD | GENI_M_CMD_FAILURE | GENI_M_RX_FIFO_RD_ERR | \
                                      GENI_M_RX_FIFO_WR_ERR | GENI_M_TX_FIFO_RD_ERR | GENI_M_TX_FIFO_WR_ERR)
#define GENI_TX_WC_MASK              0xfffffffu
#define GENI_RX_LAST                 (1u << 31)
#define GENI_RX_LAST_VALID_SHIFT     28
#define GENI_RX_LAST_VALID_MASK      7u
#define GENI_RX_WC_MASK              0x1ffffffu

#define GENI_PACK_START_SHIFT        5
#define GENI_PACK_DIR_SHIFT          4
#define GENI_PACK_LEN_SHIFT          1
#define GENI_PACK_VECTORS            4

struct geni_io {
    void *ctx;
    uint32_t (*rd)(void *ctx, uint32_t reg);
    void (*wr)(void *ctx, uint32_t reg, uint32_t value);
    void (*delay_us)(void *ctx, unsigned us);
};

struct geni_spi_info {
    uint32_t fw_revision, protocol, if_disable, hw_param0, hw_param1, geni_status;
    uint32_t qup_hw_version;
    unsigned tx_fifo_depth, rx_fifo_depth, fifo_width_bits;
    int fifo_disabled;
};

struct geni_spi {
    struct geni_io se;
    struct geni_io wrapper;
    struct geni_spi_info info;
    unsigned oversampling, tx_wm;
    unsigned cur_bpw;
    unsigned cur_speed_hz;
    uint32_t last_mode;
    int last_cs;
    unsigned poll_limit;
    int poll_only;
};

/* --- NVT Protocol Constants & Structs --- */
#define NVT_TRANSFER_LEN        (63 * 1024)
#define NVT_READ_MAX            8192
#define NVT_MAX_PARTITIONS      64
#define NVT_STOCK_TRAILER_SIZE  0x2000u
#define NVT_IMAGE_SCAN_MAX      0x8000u
#define NVT_REPORT_LEN          65u
#define NVT_REPORT_POINTS       10u

#define NVT_EVENT_HOST_CMD       0x50
#define NVT_EVENT_RESET_COMPLETE 0x60
#define NVT_EVENT_FWINFO         0x78
#define NVT_RESET_STATE_INIT     0xa0
#define NVT_RESET_STATE_MAX      0xaf
#define NVT_HOST_CMD_ENABLE_FW_CRC 0xae

#define NVT_ERR_BUS     (-5)
#define NVT_ERR_PARAM   (-22)
#define NVT_ERR_FW      (-8)
#define NVT_ERR_CHIP    (-19)
#define NVT_ERR_TIMEOUT (-110)

struct nvt_map {
    const char *name;
    uint32_t event_buf;
    uint32_t boot_rdy;
    uint32_t spi_rd_fast;
    uint32_t ilm_des, ilm_length, dlm_des, dlm_length;
    uint32_t g_ilm_checksum, g_dlm_checksum, r_ilm_checksum, r_dlm_checksum;
    uint32_t bld_ilm_dlm_crc, bld_crc_en, dma_crc_flag;
    uint32_t eng_rst, chip_info, swrst_sif;
};

static const struct nvt_map map_nt36675 = {
    .name = "NT36672C/NT36675",
    .event_buf = 0x22D00,
    .boot_rdy = 0x3F10D,
    .spi_rd_fast = 0x3F310,
    .ilm_des = 0x3F128,
    .ilm_length = 0x3F118,
    .dlm_des = 0x3F12C,
    .dlm_length = 0x3F130,
    .g_ilm_checksum = 0x3F100,
    .g_dlm_checksum = 0x3F104,
    .r_ilm_checksum = 0x3F120,
    .r_dlm_checksum = 0x3F124,
    .bld_ilm_dlm_crc = 0x3F133,
    .bld_crc_en = 0x3F30E,
    .dma_crc_flag = 0x3F134,
    .eng_rst = 0x7FFF80,
    .chip_info = 0x1F64E,
    .swrst_sif = 0x3F0FE,
};

struct nvt_trim_id {
    uint8_t id[6], mask[6];
    const struct nvt_map *map;
    unsigned hw_crc;
};

static const struct nvt_trim_id trim_ids[] = {
    {{0x0c, 0xff, 0xff, 0x72, 0x66, 0x03}, {1, 0, 0, 1, 1, 1}, &map_nt36675, 2},
};

struct nvt_bus {
    void *ctx;
    int (*xfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
    void (*delay_ms)(void *ctx, unsigned ms);
};

struct nvt_chip {
    struct nvt_bus *bus;
    const struct nvt_map *map;
    uint8_t *scratch;
    uint8_t *rxbuf;
    uint8_t id[7];
    unsigned hw_crc;
    unsigned reset_retries;
    unsigned dl_attempt, dl_stage, dl_polls;
    int dl_rc;
    uint8_t dl_reset_state[6];
};

struct nvt_fw_partition {
    char name[12];
    uint32_t sram_addr, size, bin_addr, crc, crc_calc;
};

struct nvt_fw {
    const uint8_t *data;
    size_t size, write_size;
    uint8_t fw_ver, fw_ver_bar;
    unsigned count;
    size_t source_offset;
    struct nvt_fw_partition part[NVT_MAX_PARTITIONS];
};

struct nvt_fw_info {
    uint8_t fw_ver;
    uint8_t x_num, y_num;
    uint16_t abs_x_max, abs_y_max;
    int defaulted;
};

/* Finger tracking record */
struct contact_state {
    bool down;
    bool seen;
    uint32_t x;
    uint32_t y;
};

/* Driver memory & state (in BSS to avoid kernel stack overflow) */
static vm_offset_t gcc_base = 0;
static vm_offset_t tlmm_east_base = 0;
static vm_offset_t tlmm_west_base = 0;
static vm_offset_t wrapper_base = 0;
static vm_offset_t se_base = 0;

static uint8_t nvt_tx_buf[NVT_TRANSFER_LEN + 8];
static uint8_t nvt_rx_buf[NVT_READ_MAX + 8];
static uint8_t nvt_report_buf[NVT_REPORT_LEN + 8];
static struct contact_state contacts[NVT_REPORT_POINTS];
static uint32_t touch_flip_flags = 0;

/* --- Register access helpers --- */
static inline uint32_t rd32(vm_offset_t base, uint32_t reg) {
    return *(volatile uint32_t *)(base + reg);
}

static inline void wr32(vm_offset_t base, uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(base + reg) = val;
    __asm__ volatile("dsb sy" ::: "memory");
}

static uint32_t se_rd(void *ctx, uint32_t reg) {
    return rd32((vm_offset_t)ctx, reg);
}

static void se_wr(void *ctx, uint32_t reg, uint32_t val) {
    wr32((vm_offset_t)ctx, reg, val);
}

static void se_delay_us(__unused void *ctx, unsigned us) {
    sleep_us(us);
}

static void bus_delay_ms(__unused void *ctx, unsigned ms) {
    delay_for_interval(ms, NSEC_PER_MSEC);
}

/* --- GENI SPI Implementation --- */
static uint32_t geni_rd(struct geni_spi *s, uint32_t reg) { return s->se.rd(s->se.ctx, reg); }
static void geni_wr(struct geni_spi *s, uint32_t reg, uint32_t v) { s->se.wr(s->se.ctx, reg, v); }
static void rmw_set(struct geni_spi *s, uint32_t reg, uint32_t bits) { geni_wr(s, reg, geni_rd(s, reg) | bits); }

static void irq_clear(struct geni_spi *s) {
    geni_wr(s, SE_GSI_EVENT_EN, 0);
    geni_wr(s, SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
    geni_wr(s, SE_GENI_S_IRQ_CLEAR, 0xffffffffu);
    geni_wr(s, SE_IRQ_EN, 0xffffffffu);
}

static unsigned bytes_per_word(const struct geni_spi *s) {
    return s->info.fifo_width_bits / 8;
}

static int geni_spi_read_info(struct geni_spi *s) {
    if (!s || !s->se.rd || !s->wrapper.rd) return -1;
    uint32_t fw = geni_rd(s, GENI_FW_REVISION_RO);
    s->info.fw_revision = fw;
    s->info.protocol = (fw >> GENI_FW_PROTO_SHIFT) & 0xff;
    s->info.if_disable = geni_rd(s, GENI_IF_DISABLE_RO);
    s->info.fifo_disabled = (s->info.if_disable & GENI_FIFO_IF_DISABLE) != 0;
    s->info.geni_status = geni_rd(s, SE_GENI_STATUS);
    s->info.hw_param0 = geni_rd(s, SE_HW_PARAM_0);
    s->info.hw_param1 = geni_rd(s, SE_HW_PARAM_1);
    s->info.qup_hw_version = s->wrapper.rd(s->wrapper.ctx, QUPV3_HW_VER_REG);

    uint32_t major = s->info.qup_hw_version >> 28, minor = (s->info.qup_hw_version >> 16) & 0xfff;
    uint32_t mask = ((major == 3 && minor >= 10) || major > 3) ? 0xff0000u : 0x3f0000u;
    s->info.tx_fifo_depth = (s->info.hw_param0 & mask) >> 16;
    s->info.rx_fifo_depth = (s->info.hw_param1 & mask) >> 16;
    if (s->info.rx_fifo_depth < 4) s->info.rx_fifo_depth = 4;
    s->info.fifo_width_bits = (s->info.hw_param0 >> 24) & 0x3f;
    return 0;
}

static void config_packing(struct geni_spi *s, unsigned bpw, unsigned pack_words, int msb_first) {
    uint32_t cfg[4] = {0};
    int idx_start = msb_first ? (int)bpw - 1 : 0, idx = idx_start;
    int idx_delta = msb_first ? -8 : 8;
    unsigned ceil_bpw = (bpw + 7) & ~7u;
    unsigned iter = ceil_bpw * pack_words / 8;
    int temp_bpw = (int)bpw;
    if (iter == 0 || iter > 4) return;
    for (unsigned i = 0; i < iter; i++) {
        int len = (temp_bpw < 8 ? temp_bpw : 8) - 1;
        cfg[i] = ((uint32_t)idx << 5) | ((uint32_t)(msb_first != 0) << 4) |
                 ((uint32_t)len << 1);
        if (temp_bpw <= 8) {
            idx = (int)(i + 1) * 8 + idx_start;
            temp_bpw = (int)bpw;
        } else {
            idx += idx_delta;
            temp_bpw -= 8;
        }
    }
    cfg[iter - 1] |= 1u; /* GENI_PACK_STOP */

    uint32_t cfg0 = cfg[0] | (cfg[1] << 10);
    uint32_t cfg1 = cfg[2] | (cfg[3] << 10);

    geni_wr(s, SE_GENI_TX_PACKING_CFG0, cfg0);
    geni_wr(s, SE_GENI_TX_PACKING_CFG1, cfg1);
    geni_wr(s, SE_GENI_RX_PACKING_CFG0, cfg0);
    geni_wr(s, SE_GENI_RX_PACKING_CFG1, cfg1);
    geni_wr(s, SE_GENI_BYTE_GRAN, bpw >> 4);
}

static void set_word_len(struct geni_spi *s, unsigned bpw, int msb_first) {
    geni_wr(s, SE_SPI_WORD_LEN, bpw);
    config_packing(s, bpw, bytes_per_word(s), msb_first);
}

static int geni_spi_init(struct geni_spi *s) {
    if (geni_spi_read_info(s)) return -1;
    if (s->info.protocol != GENI_PROTO_SPI) return -1;

    geni_wr(s, GENI_FORCE_DEFAULT_REG, 1);
    geni_wr(s, GENI_OUTPUT_CTRL, 0x7f);
    geni_wr(s, SE_GENI_CGC_CTRL, 0x7f);
    geni_wr(s, SE_GENI_BYTE_GRAN, 0);

    set_word_len(s, 8, 1);
    s->cur_bpw = 8;
    geni_wr(s, SE_SPI_TRANS_CFG, 0); /* CS stays asserted for full transfer (no toggle per word) */
    geni_wr(s, SE_GENI_TX_WATERMARK_REG, 1);
    geni_wr(s, SE_GENI_RX_WATERMARK_REG, s->info.rx_fifo_depth - 2);
    geni_wr(s, SE_GENI_RX_RFR_WATERMARK_REG, s->info.rx_fifo_depth - 2);
    rmw_set(s, SE_GENI_M_IRQ_EN, GENI_M_COMMON_IRQ_EN);
    rmw_set(s, SE_GENI_S_IRQ_EN, GENI_S_COMMON_IRQ_EN);
    s->tx_wm = 1;

    uint32_t major = s->info.qup_hw_version >> 28, minor = (s->info.qup_hw_version >> 16) & 0xfff;
    s->oversampling = (major == 1 && minor == 0) ? 2 : 1;

    irq_clear(s);
    rmw_set(s, SE_GENI_M_IRQ_EN, GENI_M_CMD_DONE | GENI_M_TX_FIFO_WATERMARK | GENI_M_RX_FIFO_WATERMARK | GENI_M_RX_FIFO_LAST);
    geni_wr(s, SE_GENI_DMA_MODE_EN, geni_rd(s, SE_GENI_DMA_MODE_EN) & ~1u);
    geni_wr(s, SE_SPI_LOOPBACK, 0);

    if (s->poll_only == 1) geni_wr(s, SE_IRQ_EN, 0);
    return 0;
}

static int geni_spi_setup(struct geni_spi *s, unsigned mode, unsigned chip_select) {
    if (chip_select > 15) return -1;
    s->last_cs = (int)chip_select;
    s->last_mode = mode;
    geni_wr(s, SE_SPI_DEMUX_SEL, chip_select);
    geni_wr(s, SE_SPI_CPHA, (mode & 1) ? GENI_CPHA_BIT : 0);
    geni_wr(s, SE_SPI_CPOL, (mode & 2) ? GENI_CPOL_BIT : 0);
    return 0;
}

static int geni_spi_set_clock(struct geni_spi *s, unsigned speed_hz, unsigned sclk_hz, unsigned clk_sel) {
    if (!speed_hz || !sclk_hz || clk_sel > 7 || !s->oversampling) return -1;
    unsigned long long denom = (unsigned long long)s->oversampling * speed_hz;
    unsigned long long div = (sclk_hz + denom - 1) / denom;
    if (div == 0 || div > GENI_CLK_DIV_MAX) return -1;
    geni_wr(s, SE_GENI_CLK_SEL, clk_sel);
    geni_wr(s, GENI_SER_M_CLK_CFG, ((uint32_t)div << GENI_CLK_DIV_SHIFT) | GENI_SER_CLK_EN);
    s->cur_speed_hz = speed_hz;
    return 0;
}

struct xfer {
    const uint8_t *tx;
    uint8_t *rx;
    size_t tx_rem, rx_rem, len;
};

static int handle_tx(struct geni_spi *s, struct xfer *x) {
    unsigned bpw = bytes_per_word(s);
    const unsigned wc = geni_rd(s, SE_GENI_TX_FIFO_STATUS) & GENI_TX_WC_MASK;
    const unsigned free_words = wc < s->info.tx_fifo_depth ? s->info.tx_fifo_depth - wc : 0;
    size_t max = (size_t)free_words * bpw;
    if (x->tx_rem < max) max = x->tx_rem;
    if (!max) return 1;
    const uint8_t *p = x->tx + (x->len - x->tx_rem);
    size_t i = 0;
    while (i < max) {
        uint32_t word = 0;
        size_t n = max - i < bpw ? max - i : bpw;
        for (size_t j = 0; j < n; j++) word |= (uint32_t)p[i++] << (8 * j);
        geni_wr(s, SE_GENI_TX_FIFOn, word);
    }
    x->tx_rem -= max;
    if (!x->tx_rem) {
        geni_wr(s, SE_GENI_TX_WATERMARK_REG, 0);
        return 0;
    }
    return 1;
}

static void handle_rx(struct geni_spi *s, struct xfer *x) {
    unsigned bpw = bytes_per_word(s);
    uint32_t st = geni_rd(s, SE_GENI_RX_FIFO_STATUS);
    size_t bytes = (size_t)(st & GENI_RX_WC_MASK) * bpw;
    if (st & GENI_RX_LAST) {
        unsigned valid = (st >> GENI_RX_LAST_VALID_SHIFT) & GENI_RX_LAST_VALID_MASK;
        if (valid && valid < 4) bytes -= bpw - valid;
    }
    if (!bytes) return;
    printf("novatek: handle_rx: rxfs=%08x wc=%u last=%d valid=%u bytes=%zu rx_rem=%zu\n",
           st, (st & GENI_RX_WC_MASK), !!(st & GENI_RX_LAST),
           (st >> GENI_RX_LAST_VALID_SHIFT) & GENI_RX_LAST_VALID_MASK, bytes, x->rx_rem);
    if (!x->rx) {
        for (size_t i = 0; i < (bytes + bpw - 1) / bpw; i++) (void)geni_rd(s, SE_GENI_RX_FIFOn);
        return;
    }
    if (x->rx_rem < bytes) bytes = x->rx_rem;
    uint8_t *p = x->rx + (x->len - x->rx_rem);
    size_t i = 0;
    while (i < bytes) {
        uint32_t word = geni_rd(s, SE_GENI_RX_FIFOn);
        size_t n = bytes - i < bpw ? bytes - i : bpw;
        for (size_t j = 0; j < n; j++) p[i++] = (uint8_t)(word >> (8 * j));
        printf("novatek: rx word=%08x (n=%zu)\n", word, n);
    }
    x->rx_rem -= bytes;
}

static int geni_spi_transfer(struct geni_spi *s, const uint8_t *tx, uint8_t *rx, size_t len, int keep_cs) {
    if (!s || !len || len > GENI_TRANS_LEN_MASK || (!tx && !rx) || !s->cur_speed_hz) return -1;
    if (s->cur_bpw != 8) {
        set_word_len(s, 8, 1);
        s->cur_bpw = 8;
    }
    struct xfer x = {tx, rx, tx ? len : 0, rx ? len : 0, len};
    uint32_t cmd = 0;
    if (tx) {
        cmd |= GENI_M_CMD_TX_ONLY;
        geni_wr(s, SE_SPI_TX_TRANS_LEN, (uint32_t)len);
    }
    if (rx) {
        cmd |= GENI_M_CMD_RX_ONLY;
        geni_wr(s, SE_SPI_RX_TRANS_LEN, (uint32_t)len);
    }
    if (cmd == (GENI_M_CMD_TX_ONLY | GENI_M_CMD_RX_ONLY)) cmd = GENI_M_CMD_TX_RX;
    geni_wr(s, SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
    geni_wr(s, SE_GENI_M_CMD0, (cmd << GENI_M_OPCODE_SHIFT) | (keep_cs ? GENI_M_PARAM_FRAGMENTATION : 0));
    if (tx && handle_tx(s, &x)) geni_wr(s, SE_GENI_TX_WATERMARK_REG, s->tx_wm);

    int status = NVT_ERR_TIMEOUT;
    for (unsigned n = 0; n < s->poll_limit; n++) {
        uint32_t st = geni_rd(s, SE_GENI_M_IRQ_STATUS);
        if (!st) {
            if (s->se.delay_us) s->se.delay_us(s->se.ctx, 1);
            continue;
        }
        geni_wr(s, SE_GENI_M_IRQ_CLEAR, st);
        if (st & GENI_M_ERRORS) {
            printf("novatek: geni_spi ERRORS st=%08x (tx_rem=%zu rx_rem=%zu)\n", st, x.tx_rem, x.rx_rem);
            status = NVT_ERR_BUS;
            break;
        }
        if (st & (GENI_M_RX_FIFO_WATERMARK | GENI_M_RX_FIFO_LAST)) handle_rx(s, &x);
        if ((st & GENI_M_TX_FIFO_WATERMARK) && tx) handle_tx(s, &x);
        if (st & GENI_M_CMD_DONE) {
            for (unsigned drain = 0; drain < 2000 && x.rx_rem; drain++) {
                handle_rx(s, &x);
                if (!x.rx_rem) break;
                if (s->se.delay_us) s->se.delay_us(s->se.ctx, 5);
            }
            status = (x.tx_rem || x.rx_rem) ? NVT_ERR_BUS : 0;
            if (status != 0) {
                printf("novatek: geni_spi CMD_DONE rem: tx_rem=%zu rx_rem=%zu st=%08x\n",
                       x.tx_rem, x.rx_rem, st);
            }
            if (x.tx_rem) geni_wr(s, SE_GENI_TX_WATERMARK_REG, 0);
            break;
        }
    }
    if (status == NVT_ERR_TIMEOUT) {
        printf("novatek: geni_spi TIMEOUT len=%zu cmd=%x st=%08x geni_st=%08x\n",
               len, cmd, geni_rd(s, SE_GENI_M_IRQ_STATUS), geni_rd(s, SE_GENI_STATUS));
    }
    return status;
}

/* --- NVT Protocol Implementation --- */
static int bus_xfer_wrapper(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    struct geni_spi *spi = (struct geni_spi *)ctx;
    return geni_spi_transfer(spi, tx, rx, len, 0);
}

static void nvt_delay(struct nvt_chip *c, unsigned ms) {
    if (c->bus && c->bus->delay_ms) c->bus->delay_ms(c->bus->ctx, ms);
}

static int nvt_bus_xfer(struct nvt_chip *c, size_t len, uint8_t *rx) {
    if (!c->bus || !c->bus->xfer || !c->scratch) return NVT_ERR_PARAM;
    if (!len || len > NVT_TRANSFER_LEN + 1) return NVT_ERR_PARAM;
    int err = c->bus->xfer(c->bus->ctx, c->scratch, rx, len);
    if (err) return err;
    return 0;
}

static int nvt_set_page(struct nvt_chip *c, uint32_t addr) {
    c->scratch[0] = 0xff;
    c->scratch[1] = (uint8_t)((addr >> 15) & 0xff);
    c->scratch[2] = (uint8_t)((addr >> 7) & 0xff);
    return nvt_bus_xfer(c, 3, NULL);
}

static int nvt_write(struct nvt_chip *c, uint32_t addr, const uint8_t *data, size_t len) {
    if (!len || len > NVT_TRANSFER_LEN || !data) return NVT_ERR_PARAM;
    int r = nvt_set_page(c, addr);
    if (r) return r;
    c->scratch[0] = (uint8_t)((addr & 0x7f) | 0x80);
    memcpy(c->scratch + 1, data, len);
    return nvt_bus_xfer(c, len + 1, NULL);
}

static int nvt_write_byte(struct nvt_chip *c, uint32_t addr, uint8_t value) {
    return nvt_write(c, addr, &value, 1);
}

static int nvt_read(struct nvt_chip *c, uint32_t addr, uint8_t *out, size_t len) {
    if (!len || len > NVT_READ_MAX || !out || !c->rxbuf) return NVT_ERR_PARAM;
    int r = nvt_set_page(c, addr);
    if (r) return r;
    memset(c->scratch, 0, len + 2);
    c->scratch[0] = (uint8_t)(addr & 0x7f);
    int x = nvt_bus_xfer(c, len + 2, c->rxbuf);
    if (x) return x;
    memcpy(out, c->rxbuf + 2, len);
    return 0;
}

static int bootloader_reset(struct nvt_chip *c) {
    int r = nvt_write_byte(c, c->map->swrst_sif, 0x69);
    nvt_delay(c, 35);
    if (r) return r;
    r = nvt_write_byte(c, c->map->spi_rd_fast, 0);
    nvt_delay(c, 1);
    return r;
}

static int nvt_identify(struct nvt_chip *c) {
    if (!c) return NVT_ERR_PARAM;
    c->map = &map_nt36675;
    c->hw_crc = 0;
    memset(c->id, 0, sizeof(c->id));

    int r = nvt_write_byte(c, c->map->eng_rst, 0x5a);
    nvt_delay(c, 15);
    nvt_delay(c, 30);
    if (r) return r;

    int matched = 0;
    for (unsigned retry = 0; retry < 5 && !matched; ++retry) {
        r = bootloader_reset(c);
        if (r) return r;
        uint8_t raw[7] = {0};
        r = nvt_read(c, c->map->chip_info, raw, sizeof(raw));
        if (r) return r;
        for (unsigned t = 0; t < sizeof(trim_ids) / sizeof(trim_ids[0]) && !matched; ++t) {
            int ok = 1;
            for (unsigned i = 0; i < 6 && ok; ++i) {
                if (trim_ids[t].mask[i] && raw[i] != trim_ids[t].id[i]) ok = 0;
            }
            if (!ok) continue;
            memcpy(c->id, raw, sizeof(c->id));
            c->map = trim_ids[t].map;
            c->hw_crc = trim_ids[t].hw_crc;
            matched = 1;
        }
        if (!matched) nvt_delay(c, 10);
    }
    return matched ? 0 : NVT_ERR_CHIP;
}

static int word_at(const uint8_t *d, size_t size, size_t off, uint32_t *out) {
    if (off + 4 > size) return -1;
    *out = (uint32_t)d[off] | ((uint32_t)d[off + 1] << 8) | ((uint32_t)d[off + 2] << 16) | ((uint32_t)d[off + 3] << 24);
    return 0;
}

static int nvt_fw_parse(struct nvt_fw *fw, const uint8_t *d, size_t size) {
    if (!fw || !d || size < 0x40) return NVT_ERR_PARAM;
    memset(fw, 0, sizeof(*fw));
    fw->data = d;
    fw->size = size;

    uint32_t end, ilm_len, ilm_sram, dlm_len, dlm_sram;
    if (word_at(d, size, 0, &end) || end < 0x30 || end > size) return NVT_ERR_FW;
    if (word_at(d, size, 8, &ilm_len) || word_at(d, size, 12, &ilm_sram)) return NVT_ERR_FW;
    if (word_at(d, size, 20, &dlm_len) || word_at(d, size, 24, &dlm_sram)) return NVT_ERR_FW;

    fw->part[0].sram_addr = ilm_sram;
    fw->part[0].size = ilm_len;
    fw->part[0].bin_addr = end;
    memcpy(fw->part[0].name, "ILM", 4);

    fw->part[1].sram_addr = dlm_sram;
    fw->part[1].size = dlm_len;
    fw->part[1].bin_addr = end + ilm_len;
    memcpy(fw->part[1].name, "DLM", 4);

    uint32_t g_ilm, g_dlm;
    if (word_at(d, size, 0x18, &g_ilm) || word_at(d, size, 0x1c, &g_dlm)) return NVT_ERR_FW;
    fw->part[0].crc = g_ilm;
    fw->part[1].crc = g_dlm;

    uint32_t ovl_sec_num, info_sec_num;
    if (word_at(d, size, 0x28, &ovl_sec_num) || word_at(d, size, 0x2c, &info_sec_num)) return NVT_ERR_FW;
    fw->count = 2 + info_sec_num + ovl_sec_num;
    if (fw->count > NVT_MAX_PARTITIONS) return NVT_ERR_FW;

    size_t write_sz = fw->part[1].bin_addr + fw->part[1].size;
    if (write_sz > size) return NVT_ERR_FW;
    fw->write_size = write_sz;

    if (write_sz >= 4096) {
        const uint8_t *m = d + write_sz - 4096;
        if (!memcmp(m, "NVT", 3) || !memcmp(m, "MOD", 3)) {
            fw->fw_ver = m[3];
            fw->fw_ver_bar = m[4];
        }
    }

    for (unsigned list = 2; list < fw->count; ++list) {
        struct nvt_fw_partition *p = &fw->part[list];
        if (list < 2 + info_sec_num) {
            const size_t pos = 0x30 + 0x10 * (list - 2);
            if (word_at(d, size, pos, &p->sram_addr) || word_at(d, size, pos + 4, &p->size) ||
                word_at(d, size, pos + 8, &p->bin_addr) || word_at(d, size, pos + 12, &p->crc)) return NVT_ERR_FW;
            if (p->bin_addr == 0 && p->size != 0) memcpy(p->name, "Header", 7);
            else memcpy(p->name, "Info", 5);
        } else {
            const size_t pos = (size_t)fw->part[1].bin_addr + 0x10 * (list - 2 - info_sec_num);
            if (word_at(d, size, pos, &p->sram_addr) || word_at(d, size, pos + 4, &p->size) ||
                word_at(d, size, pos + 8, &p->bin_addr) || word_at(d, size, pos + 12, &p->crc)) return NVT_ERR_FW;
            memcpy(p->name, "Overlay", 8);
        }
    }
    return 0;
}

static int nvt_fw_parse_stock(struct nvt_fw *fw, const uint8_t *d, size_t size) {
    if (!fw || !d || size < 0x40) return NVT_ERR_PARAM;
    if (!nvt_fw_parse(fw, d, size)) return 0;
    if (size > NVT_STOCK_TRAILER_SIZE) {
        if (!nvt_fw_parse(fw, d + NVT_STOCK_TRAILER_SIZE, size - NVT_STOCK_TRAILER_SIZE)) {
            fw->source_offset = NVT_STOCK_TRAILER_SIZE;
            return 0;
        }
    }
    return NVT_ERR_FW;
}

static int write_firmware(struct nvt_chip *c, const struct nvt_fw *fw) {
    for (unsigned i = 0; i < fw->count; ++i) {
        const struct nvt_fw_partition *p = &fw->part[i];
        if (!p->size) continue;
        size_t sent = 0;
        const size_t total = (size_t)p->size + 1;
        while (sent < total) {
            size_t chunk = total - sent;
            if (chunk > NVT_TRANSFER_LEN) chunk = NVT_TRANSFER_LEN;
            int r = nvt_write(c, p->sram_addr + (uint32_t)sent, fw->data + p->bin_addr + sent, chunk);
            if (r) return r;
            sent += chunk;
        }
    }
    return 0;
}

static int set_bld_crc(struct nvt_chip *c, const struct nvt_fw *fw) {
    uint8_t d[8];
    d[0] = (uint8_t)fw->part[0].sram_addr;
    d[1] = (uint8_t)(fw->part[0].sram_addr >> 8);
    d[2] = (uint8_t)(fw->part[0].sram_addr >> 16);
    d[3] = (uint8_t)fw->part[0].size;
    d[4] = (uint8_t)(fw->part[0].size >> 8);
    d[5] = (uint8_t)fw->part[1].sram_addr;
    d[6] = (uint8_t)(fw->part[1].sram_addr >> 8);
    d[7] = (uint8_t)(fw->part[1].sram_addr >> 16);
    int r = nvt_write(c, c->map->ilm_des, d, sizeof(d));
    if (r) return r;

    uint8_t len[2] = {(uint8_t)fw->part[1].size, (uint8_t)(fw->part[1].size >> 8)};
    r = nvt_write(c, c->map->dlm_length, len, sizeof(len));
    if (r) return r;

    uint8_t crc[8];
    for (unsigned i = 0; i < 4; ++i) {
        crc[i] = (uint8_t)(fw->part[0].crc >> (8 * i));
        crc[4 + i] = (uint8_t)(fw->part[1].crc >> (8 * i));
    }
    return nvt_write(c, c->map->g_ilm_checksum, crc, sizeof(crc));
}

static int enable_bld_crc(struct nvt_chip *c) {
    return nvt_write_byte(c, c->map->bld_crc_en, 0x80);
}

static int fw_crc_enable(struct nvt_chip *c) {
    return nvt_write_byte(c, c->map->event_buf + NVT_EVENT_HOST_CMD, NVT_HOST_CMD_ENABLE_FW_CRC);
}

static int check_reset_state(struct nvt_chip *c) {
    const unsigned max = c->reset_retries ? c->reset_retries : 10;
    for (unsigned retry = 0; retry <= max; ++retry) {
        uint8_t b[6] = {0};
        int r = nvt_read(c, c->map->event_buf + NVT_EVENT_RESET_COMPLETE, b, sizeof(b));
        if (r) return r;
        memcpy(c->dl_reset_state, b, sizeof(c->dl_reset_state));
        c->dl_polls = retry + 1;
        if (b[0] >= NVT_RESET_STATE_INIT && b[0] <= NVT_RESET_STATE_MAX) return 0;
        nvt_delay(c, 10);
    }
    return NVT_ERR_TIMEOUT;
}

static int nvt_download_firmware(struct nvt_chip *c, const struct nvt_fw *fw) {
    if (!c->map || fw->count < 2 || !fw->part[0].size || !fw->part[1].size) return NVT_ERR_PARAM;
    int last = NVT_ERR_TIMEOUT;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        c->dl_attempt = attempt + 1;
        int ret = bootloader_reset(c);
        if (ret) { last = ret; continue; }
        ret = set_bld_crc(c, fw);
        if (ret) { last = ret; continue; }
        ret = write_firmware(c, fw);
        if (ret) { last = ret; continue; }
        ret = enable_bld_crc(c);
        if (ret) { last = ret; continue; }
        ret = fw_crc_enable(c);
        if (ret) { last = ret; continue; }
        ret = nvt_write_byte(c, c->map->boot_rdy, 1);
        if (ret) { last = ret; continue; }
        nvt_delay(c, 10);
        ret = check_reset_state(c);
        if (ret) { last = ret; continue; }
        return 0;
    }
    return last;
}

static int nvt_get_fw_info(struct nvt_chip *c, struct nvt_fw_info *info) {
    if (!c || !c->map || !info) return NVT_ERR_PARAM;
    uint8_t b[17];
    memset(info, 0, sizeof(*info));
    for (int attempt = 0; attempt < 4; ++attempt) {
        int r = nvt_read(c, c->map->event_buf + NVT_EVENT_FWINFO, b, sizeof(b));
        if (r) return r;
        if ((uint8_t)(b[0] + b[1]) == 0xff) {
            info->fw_ver = b[0];
            info->x_num = b[2];
            info->y_num = b[3];
            info->abs_x_max = (uint16_t)((b[4] << 8) | b[5]);
            info->abs_y_max = (uint16_t)((b[6] << 8) | b[7]);
            return 0;
        }
        nvt_delay(c, 10);
    }
    info->defaulted = 1;
    return -1;
}

static int nvt_start_scanning(struct nvt_chip *c, struct nvt_fw_info *info) {
    nvt_delay(c, 100);
    return nvt_get_fw_info(c, info);
}

static int nvt_read_report(struct nvt_chip *c, uint8_t *report) {
    if (!c || !c->map || !report) return NVT_ERR_PARAM;
    return nvt_read(c, c->map->event_buf, report, NVT_REPORT_LEN);
}

/* --- DFS Clock Calculations --- */
static uint64_t tb_src_hz(unsigned sel) {
    return sel == 0 ? 19200000ULL : sel == 1 ? 600000000ULL : sel == 6 ? 300000000ULL : 0;
}

static uint64_t tb_calc_rate(uint64_t prate, uint32_t m, uint32_t n, unsigned mode, unsigned hid) {
    if (hid) { prate *= 2; prate /= hid + 1; }
    if (mode && n) { prate *= m; prate /= n; }
    return prate;
}

static uint64_t tb_dfs_level_hz(uint32_t cfg, uint32_t mreg, uint32_t nreg) {
    unsigned src = (cfg >> 8) & 7, mode = (cfg >> 12) & 3, hid = cfg & 0x1f;
    uint32_t m = 0, n = 0;
    if (mode) { m = mreg & 0xffff; n = ((~nreg) & 0xffff) + m; }
    return tb_calc_rate(tb_src_hz(src), m, n, mode, hid);
}

struct tb_clock_choice {
    unsigned level, div;
    uint64_t sclk_hz, actual_hz;
};

static int tb_choose_clock(const uint64_t *level_hz, unsigned n, unsigned oversampling, unsigned target,
                           struct tb_clock_choice *out) {
    int found = 0;
    if (!oversampling || !target) return -1;
    for (unsigned l = 0; l < n; ++l) {
        const uint64_t f = level_hz[l];
        if (!f) continue;
        uint64_t d = (f + (uint64_t)oversampling * target - 1) / ((uint64_t)oversampling * target);
        if (d < 1) d = 1;
        if (d > 0xfff) continue;
        const uint64_t actual = f / (oversampling * d);
        if (actual > target) continue;
        if (!found || actual > out->actual_hz || (actual == out->actual_hz && d < out->div)) {
            out->level = l; out->div = (unsigned)d; out->sclk_hz = f; out->actual_hz = actual; found = 1;
        }
    }
    return found ? 0 : -1;
}

/* --- Pin helpers --- */
static inline bool sw_branch_running(uint32_t cbcr) {
    return !(cbcr & TB_CBCR_CLK_OFF) || (cbcr & 2u);
}

static inline vm_offset_t tlmm_pin(unsigned gpio) {
    if (gpio <= TB_GPIO_IRQ) {
        return tlmm_east_base + (gpio * TB_GPIO_STRIDE);
    }
    return tlmm_west_base + (gpio * TB_GPIO_STRIDE);
}

static void pin_config(unsigned gpio, uint32_t cfg) {
    vm_offset_t p = tlmm_pin(gpio);
    uint32_t val = rd32(p, 0);
    wr32(p, 0, (val & ~TB_PIN_CFG_MASK) | cfg);
}

static void pin_set_out(unsigned gpio, unsigned v) {
    vm_offset_t p = tlmm_pin(gpio) + TB_TLMM_IO_OFFSET;
    uint32_t val = rd32(p, 0);
    wr32(p, 0, (val & ~2u) | ((v != 0) << 1));
}

/* --- Novatek Touch Bring-up & Polling Thread --- */
static void
novatek_thread(__unused void *parameter, __unused wait_result_t result)
{
    printf("novatek: starting NT36672C touch bring-up on Picasso (SM7250 SE7 SPI)\n");

    /* 1. Map MMIO Regions */
    gcc_base = ml_io_map(TB_GCC_BASE, TB_GCC_SIZE);
    tlmm_east_base = ml_io_map(TB_TLMM_EAST_BASE, TB_TLMM_TILE_SIZE);
    tlmm_west_base = ml_io_map(TB_TLMM_WEST_BASE, TB_TLMM_TILE_SIZE);
    wrapper_base = ml_io_map(TB_QUP_WRAPPER_BASE, TB_QUP_WRAPPER_SIZE);
    se_base = ml_io_map(TB_SE_BASE, TB_SE_SIZE);

    if (!gcc_base || !tlmm_east_base || !tlmm_west_base || !wrapper_base || !se_base) {
        printf("novatek: failed to map MMIO regions\n");
        thread_terminate(current_thread());
        return;
    }

    /* 2. Vote Clocks */
    wr32(gcc_base, TB_GCC_GPLL0_ENABLE, rd32(gcc_base, TB_GCC_GPLL0_ENABLE) | 1u);
    bool gpll0_locked = false;
    for (unsigned i = 0; i < 10000 && !gpll0_locked; ++i) {
        gpll0_locked = (rd32(gcc_base, TB_GCC_GPLL0_MODE) & TB_GPLL0_LOCK) != 0;
        if (!gpll0_locked) sleep_us(10);
    }
    if (!gpll0_locked) {
        printf("novatek: GPLL0 did not lock\n");
        thread_terminate(current_thread());
        return;
    }

    wr32(gcc_base, TB_GCC_VOTE_ENABLE, rd32(gcc_base, TB_GCC_VOTE_ENABLE) | TB_VOTE_ALL);
    bool running = false;
    uint32_t m_ahb = 0, s_ahb = 0, core = 0, core2x = 0, s1 = 0;
    for (unsigned i = 0; i < 2000 && !running; ++i) {
        m_ahb = rd32(gcc_base, TB_GCC_BRANCH_M_AHB);
        s_ahb = rd32(gcc_base, TB_GCC_BRANCH_S_AHB);
        core = rd32(gcc_base, TB_GCC_BRANCH_CORE);
        core2x = rd32(gcc_base, TB_GCC_BRANCH_CORE_2X);
        s1 = rd32(gcc_base, TB_GCC_BRANCH_S1);
        running = sw_branch_running(m_ahb) && sw_branch_running(s_ahb) &&
                  !(core & TB_CBCR_CLK_OFF) && !(core2x & TB_CBCR_CLK_OFF) && !(s1 & TB_CBCR_CLK_OFF);
        if (!running) sleep_us(10);
    }
    printf("novatek: QUP branches: m_ahb=%x s_ahb=%x core=%x core2x=%x s1=%x (running=%d)\n",
           m_ahb, s_ahb, core, core2x, s1, running);
    if (!running) {
        printf("novatek: QUPv3 wrapper 1 branches failed to turn on\n");
        thread_terminate(current_thread());
        return;
    }

    /* 3. Configure TLMM GPIOs */
    printf("novatek: initial TLMM pins: rst=%08x irq=%08x clk=%08x miso=%08x mosi=%08x cs=%08x\n",
           rd32(tlmm_pin(TB_GPIO_RESET), 0), rd32(tlmm_pin(TB_GPIO_IRQ), 0),
           rd32(tlmm_pin(TB_GPIO_SPI_CLK), 0), rd32(tlmm_pin(TB_GPIO_SPI_MISO), 0),
           rd32(tlmm_pin(TB_GPIO_SPI_MOSI), 0), rd32(tlmm_pin(TB_GPIO_SPI_CS), 0));

    pin_config(TB_GPIO_SPI_CLK, TB_PIN_CFG_SPI);
    pin_config(TB_GPIO_SPI_MISO, TB_PIN_CFG_SPI);
    pin_config(TB_GPIO_SPI_MOSI, TB_PIN_CFG_SPI);
    pin_config(TB_GPIO_SPI_CS, TB_PIN_CFG_SPI);
    pin_config(TB_GPIO_RESET, TB_PIN_CFG_RESET);
    pin_config(TB_GPIO_IRQ, TB_PIN_CFG_IRQ);

    printf("novatek: configured TLMM pins: rst=%08x irq=%08x clk=%08x miso=%08x mosi=%08x cs=%08x\n",
           rd32(tlmm_pin(TB_GPIO_RESET), 0), rd32(tlmm_pin(TB_GPIO_IRQ), 0),
           rd32(tlmm_pin(TB_GPIO_SPI_CLK), 0), rd32(tlmm_pin(TB_GPIO_SPI_MISO), 0),
           rd32(tlmm_pin(TB_GPIO_SPI_MOSI), 0), rd32(tlmm_pin(TB_GPIO_SPI_CS), 0));

    /* 4. Initialize GENI SPI */
    struct geni_spi spi;
    memset(&spi, 0, sizeof(spi));
    spi.se = (struct geni_io){(void *)se_base, se_rd, se_wr, se_delay_us};
    spi.wrapper = (struct geni_io){(void *)wrapper_base, se_rd, se_wr, se_delay_us};
    spi.poll_limit = 400000;
    spi.poll_only = 1;

    int rc = geni_spi_init(&spi);
    if (rc) {
        printf("novatek: geni_spi_init failed rc=%d\n", rc);
        thread_terminate(current_thread());
        return;
    }

    uint64_t level_hz[TB_DFS_LEVELS];
    for (unsigned l = 0; l < TB_DFS_LEVELS; ++l) {
        uint32_t cfg = rd32(gcc_base, TB_GCC_DFS_S1_RCG + TB_RCG_PERF_CFG(l));
        uint32_t m = rd32(gcc_base, TB_GCC_DFS_S1_RCG + TB_RCG_PERF_M(l));
        uint32_t n = rd32(gcc_base, TB_GCC_DFS_S1_RCG + TB_RCG_PERF_N(l));
        level_hz[l] = tb_dfs_level_hz(cfg, m, n);
    }
    struct tb_clock_choice choice;
    memset(&choice, 0, sizeof(choice));
    if (tb_choose_clock(level_hz, TB_DFS_LEVELS, spi.oversampling, TB_SPI_TARGET_HZ, &choice)) {
        choice.level = 0;
        choice.div = 1;
        choice.sclk_hz = TB_XO_HZ;
        choice.actual_hz = TB_XO_HZ / (spi.oversampling * choice.div);
    }
    printf("novatek: SPI clock: sclk=%llu actual=%llu level=%u div=%u\n",
           choice.sclk_hz, choice.actual_hz, choice.level, choice.div);
    geni_spi_set_clock(&spi, TB_SPI_TARGET_HZ, (unsigned)choice.sclk_hz, choice.level);

    /* 5. Identify Novatek Chip (sweep modes 0, 3, 1, 2) */
    struct nvt_bus bus = {
        .ctx = &spi,
        .xfer = bus_xfer_wrapper,
        .delay_ms = bus_delay_ms,
    };
    struct nvt_chip chip;
    memset(&chip, 0, sizeof(chip));
    chip.bus = &bus;
    chip.scratch = nvt_tx_buf;
    chip.rxbuf = nvt_rx_buf;
    chip.reset_retries = 60;

    static const unsigned mode_order[4] = {0, 3, 1, 2};
    rc = NVT_ERR_CHIP;
    for (unsigned m = 0; m < 4 && rc != 0; ++m) {
        unsigned mode = mode_order[m];
        geni_spi_setup(&spi, ((mode & 1) ? GENI_SPI_CPHA : 0) | ((mode & 2) ? GENI_SPI_CPOL : 0), 0);

        /* Hardware reset pulse */
        pin_set_out(TB_GPIO_RESET, 0);
        sleep_ms(10); /* 10 ms low */
        pin_set_out(TB_GPIO_RESET, 1);
        sleep_ms(20); /* 20 ms settle */

        chip.map = NULL;
        chip.hw_crc = 0;
        memset(chip.id, 0, sizeof(chip.id));
        rc = nvt_identify(&chip);
        if (rc == 0) {
            printf("novatek: identified on SPI mode %u!\n", mode);
            break;
        }
        printf("novatek: mode %u identify rc=%d (id: %02x %02x %02x %02x %02x %02x)\n",
               mode, rc, chip.id[0], chip.id[1], chip.id[2], chip.id[3], chip.id[4], chip.id[5]);
    }

    if (rc) {
        printf("novatek: nvt_identify failed after all modes (id: %02x %02x %02x %02x %02x %02x)\n",
               chip.id[0], chip.id[1], chip.id[2], chip.id[3], chip.id[4], chip.id[5]);
        thread_terminate(current_thread());
        return;
    }
    printf("novatek: detected %s chip id=%02x%02x%02x%02x%02x%02x\n",
           chip.map->name, chip.id[0], chip.id[1], chip.id[2], chip.id[3], chip.id[4], chip.id[5]);

    /* 7. Parse & Download Firmware */
    struct nvt_fw fw;
    rc = nvt_fw_parse_stock(&fw, novatek_fw01, sizeof(novatek_fw01));
    if (rc) {
        printf("novatek: nvt_fw_parse_stock failed rc=%d\n", rc);
        thread_terminate(current_thread());
        return;
    }
    printf("novatek: firmware ver=%02x/%02x parts=%u write_size=%u bytes\n",
           fw.fw_ver, fw.fw_ver_bar, fw.count, (unsigned)fw.write_size);

    rc = nvt_download_firmware(&chip, &fw);
    if (rc) {
        printf("novatek: nvt_download_firmware failed rc=%d\n", rc);
        thread_terminate(current_thread());
        return;
    }

    struct nvt_fw_info info;
    rc = nvt_start_scanning(&chip, &info);
    printf("novatek: scan active, fwinfo: rc=%d ver=%u geometry=%ux%u max=%ux%u\n",
           rc, info.fw_ver, info.x_num, info.y_num, info.abs_x_max, info.abs_y_max);

    printf("novatek: touch screen ready, publishing samples to /dev/hakios-touch (125 Hz)\n");

    /* 8. Touch Event Polling Loop (125 Hz) */
    PE_parse_boot_argn("touch_flip", &touch_flip_flags, sizeof(touch_flip_flags));

    for (;;) {
        uint8_t report[NVT_REPORT_LEN];
        int r = nvt_read_report(&chip, report);
        if (r == 0) {
            /* Reset seen flags */
            for (unsigned i = 0; i < NVT_REPORT_POINTS; ++i) {
                contacts[i].seen = false;
            }

            /* Decode up to 10 points */
            for (unsigned i = 0; i < NVT_REPORT_POINTS; ++i) {
                unsigned pos = 1 + 6 * i;
                unsigned id = report[pos] >> 3;
                unsigned state = report[pos] & 0x07u;
                if (!id || id > NVT_REPORT_POINTS || (state != 1 && state != 2)) continue;

                int raw_x = (report[pos + 1] << 4) | (report[pos + 3] >> 4);
                int raw_y = (report[pos + 2] << 4) | (report[pos + 3] & 0x0f);
                if (raw_x > TB_PANEL_WIDTH_PX || raw_y > TB_PANEL_HEIGHT_PX) continue;

                /* Normalize to 0..65535 */
                uint32_t x = (uint32_t)(((uint64_t)raw_x * 65535ULL) / TB_PANEL_WIDTH_PX);
                uint32_t y = (uint32_t)(((uint64_t)raw_y * 65535ULL) / TB_PANEL_HEIGHT_PX);

                if (touch_flip_flags & 1) x = 65535 - x;
                if (touch_flip_flags & 2) y = 65535 - y;
                if (touch_flip_flags & 4) {
                    uint32_t tmp = x; x = y; y = tmp;
                }

                unsigned slot = id - 1;
                contacts[slot].seen = true;
                if (!contacts[slot].down) {
                    contacts[slot].down = true;
                    hakios_touch_post(HAKIOS_TOUCH_DOWN, slot, x, y);
                } else if (contacts[slot].x != x || contacts[slot].y != y) {
                    hakios_touch_post(HAKIOS_TOUCH_MOVE, slot, x, y);
                }
                contacts[slot].x = x;
                contacts[slot].y = y;
            }

            /* Detect finger lifts */
            for (unsigned i = 0; i < NVT_REPORT_POINTS; ++i) {
                if (contacts[i].down && !contacts[i].seen) {
                    contacts[i].down = false;
                    hakios_touch_post(HAKIOS_TOUCH_UP, i, contacts[i].x, contacts[i].y);
                }
            }
        }

        delay_for_interval(8, NSEC_PER_MSEC); /* 8 ms ~ 125 Hz */
    }
}

void
hakios_novatek_init(void)
{
    uint32_t enabled = 0;
    thread_t thread;

    if (!PE_parse_boot_argn("touchscreen", &enabled, sizeof(enabled)) || !enabled) {
        return;
    }
    printf("novatek: initializing Novatek NT36672C touch driver\n");
    if (kernel_thread_start(novatek_thread, NULL, &thread) == KERN_SUCCESS) {
        thread_deallocate(thread);
    }
}
