/*
 * Bluetooth driver for Xiaomi Redmi K30 5G (picasso / Qualcomm SM7250):
 * Qualcomm WCN3990 Bluetooth controller attached via QUPV3 SE5 UART (0x894000).
 *
 * Boot-args:
 *   bt=1           Step 1: start bt_thread, register /dev/hakios-bt, probe pins & QUPV3 SE5
 *   bt=2           Step 2: perform power sequence (2400/115200 baud pulses) & start RX
 *   bt=3           Step 3: send EDL version request, HCI Reset, and Read BD_ADDR
 *   bt_again=<s>   second look at pin state this many seconds later (default 30)
 */
#include <stdint.h>
#include <stdbool.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/startup.h>
#include <kern/thread_call.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <mach/mach_time.h>
#include <miscfs/devfs/devfs.h>
#include <pexpert/pexpert.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/uio.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void loader_log_putc(char c);
extern kern_return_t thread_terminate(thread_t thread);

static int say(const char *format, ...) __printflike(1, 2);

static int
say(const char *format, ...)
{
	char line[256];
	va_list list;

	va_start(list, format);
	vsnprintf(line, sizeof(line), format, list);
	va_end(list);
	for (const char *c = line; *c != 0; c++) {
		loader_log_putc(*c);
	}
	return printf("%s", line);
}

static void
spin_us(uint32_t us)
{
	uint64_t until;
	nanoseconds_to_absolutetime((uint64_t)us * NSEC_PER_USEC, &until);
	until += mach_absolute_time();
	while (mach_absolute_time() < until) {
		__builtin_arm_isb(15);
	}
}

static void
sleep_ms(uint32_t ms)
{
	spin_us(ms * 1000);
}

/* Picasso (SM7250) TLMM EAST tile */
#define PICASSO_TLMM_EAST_PHYS  0x0f900000UL
#define PICASSO_TLMM_SIZE       0x100000UL

/* Picasso QUPV3 SE5 UART */
#define PICASSO_QUP_SE5_PHYS    0x00894000UL
#define PICASSO_QUP_SE5_SIZE    0x4000UL

/* GENI SE Registers */
#define SE_GENI_FORCE_DEFAULT   0x20
#define SE_GENI_OUTPUT_CTRL     0x24
#define SE_GENI_CGC_CTRL        0x28
#define SE_GENI_STATUS          0x40
#define SE_GENI_SER_M_CLK_CFG   0x48
#define SE_GENI_SER_S_CLK_CFG   0x4c
#define SE_GENI_IF_DISABLE_RO   0x64
#define SE_GENI_FW_REVISION_RO  0x68
#define SE_GENI_CLK_SEL         0x7c
#define SE_GENI_DMA_MODE_EN     0x258
#define SE_UART_TX_TRANS_CFG    0x25c
#define SE_GENI_TX_PACKING_CFG0 0x260
#define SE_GENI_TX_PACKING_CFG1 0x264
#define SE_UART_TX_WORD_LEN     0x268
#define SE_UART_TX_STOP_BIT_LEN 0x26c
#define SE_UART_TX_TRANS_LEN    0x270
#define SE_UART_RX_TRANS_CFG    0x280
#define SE_GENI_RX_PACKING_CFG0 0x284
#define SE_GENI_RX_PACKING_CFG1 0x288
#define SE_UART_RX_WORD_LEN     0x28c
#define SE_UART_RX_STALE_CNT    0x294
#define SE_UART_TX_PARITY_CFG   0x2a4
#define SE_UART_RX_PARITY_CFG   0x2a8
#define SE_UART_MANUAL_RFR      0x2ac
#define SE_GENI_M_CMD0          0x600
#define SE_GENI_M_CMD_CTRL_REG  0x604
#define SE_GENI_M_IRQ_STATUS    0x610
#define SE_GENI_M_IRQ_EN        0x614
#define SE_GENI_M_IRQ_CLEAR     0x618
#define SE_GENI_S_CMD0          0x630
#define SE_GENI_S_CMD_CTRL_REG  0x634
#define SE_GENI_S_IRQ_STATUS    0x640
#define SE_GENI_S_IRQ_EN        0x644
#define SE_GENI_S_IRQ_CLEAR     0x648
#define SE_GENI_TX_FIFOn        0x700
#define SE_GENI_RX_FIFOn        0x780
#define SE_GENI_TX_FIFO_STATUS  0x800
#define SE_GENI_RX_FIFO_STATUS  0x804
#define SE_GENI_TX_WATERMARK    0x80c
#define SE_GENI_RX_WATERMARK    0x810
#define SE_GENI_RX_RFR_WATERMARK 0x814

#define M_CMD_DONE_EN           (1u << 0)
#define M_CMD_ABORT_EN          (1u << 5)
#define S_CMD_DONE_EN           (1u << 0)
#define S_CMD_ABORT_EN          (1u << 5)
#define M_GENI_CMD_ACTIVE       (1u << 0)
#define S_GENI_CMD_ACTIVE       (1u << 12)
#define M_GENI_CMD_ABORT        (1u << 1)
#define S_GENI_CMD_ABORT        (1u << 1)
#define UART_START_TX           (1u << 27)
#define UART_START_READ         (1u << 27)
#define UART_PARAM_RFR_OPEN     (1u << 7)
#define UART_CTS_MASK           (1u << 1)

#define GPIO_CFG(base, n)       ((base) + 0x1000UL * (n))
#define GPIO_IO(base, n)        ((base) + 0x1000UL * (n) + 4)
#define CFG_OUTPUT              (1u << 9)

static vm_offset_t tlmm_base;
static vm_offset_t qup_se5_base;
static vm_offset_t gcc_base;
static uint32_t again_seconds = 0;
static thread_call_t again_call;
static uint32_t bt_level = 0;
static void *bt_devfs_node = NULL;

static const struct {
	unsigned pin;
	const char *what;
} picasso_pins[] = {
	{ 73, "qca,bt-sw-ctrl-gpio: antenna switch" },
	{ 38, "qup05: UART RX" },
	{ 39, "qup05: UART RTS" },
	{ 40, "qup05: UART TX" },
	{ 41, "qup05: UART CTS" },
};

static inline uint32_t
rd(vm_offset_t address)
{
	return *(volatile uint32_t *)address;
}

static inline void
wr(vm_offset_t address, uint32_t val)
{
	*(volatile uint32_t *)address = val;
}

static void
look(const char *when)
{
	if (!tlmm_base) return;
	for (unsigned i = 0; i < sizeof(picasso_pins) / sizeof(picasso_pins[0]); i++) {
		uint32_t cfg = rd(GPIO_CFG(tlmm_base, picasso_pins[i].pin));
		uint32_t io = rd(GPIO_IO(tlmm_base, picasso_pins[i].pin));

		say("bt: %s: gpio%u (%s): function %u, pull %u, %u mA, %s, reads %u\n",
		    when, picasso_pins[i].pin, picasso_pins[i].what, (cfg >> 2) & 15,
		    cfg & 3, (((cfg >> 6) & 7) + 1) * 2,
		    (cfg & CFG_OUTPUT) ? (io & 2 ? "drives high" : "drives low") : "input",
		    io & 1);
	}
}

static void
probe_qup_se(void)
{
	if (!qup_se5_base) return;

	uint32_t fw_rev = rd(qup_se5_base + SE_GENI_FW_REVISION_RO);
	uint32_t status = rd(qup_se5_base + SE_GENI_STATUS);
	uint32_t if_dis = rd(qup_se5_base + SE_GENI_IF_DISABLE_RO);
	uint32_t m_clk = rd(qup_se5_base + SE_GENI_SER_M_CLK_CFG);
	uint32_t s_clk = rd(qup_se5_base + SE_GENI_SER_S_CLK_CFG);
	uint32_t tx_st = rd(qup_se5_base + SE_GENI_TX_FIFO_STATUS);
	uint32_t rx_st = rd(qup_se5_base + SE_GENI_RX_FIFO_STATUS);
	uint32_t tx_p0 = rd(qup_se5_base + SE_GENI_TX_PACKING_CFG0);
	uint32_t rx_p0 = rd(qup_se5_base + SE_GENI_RX_PACKING_CFG0);
	uint32_t tx_cfg = rd(qup_se5_base + SE_UART_TX_TRANS_CFG);
	uint32_t dma_mode = rd(qup_se5_base + SE_GENI_DMA_MODE_EN);
	uint32_t proto = (fw_rev >> 8) & 0xff;

	say("bt: picasso: QUPV3 SE5 (0x894000): proto=%u (%s), rev=0x%04x, status=0x%08x, if_dis=%u, dma_mode=0x%x\n",
	    proto, proto == 2 ? "UART" : (proto == 1 ? "SPI" : (proto == 3 ? "I2C" : "UNKNOWN")),
	    fw_rev & 0xffff, status, if_dis & 1, dma_mode);
	say("bt: picasso: QUPV3 SE5 clk_cfg: m=0x%08x, s=0x%08x; fifo: tx=0x%08x, rx=0x%08x; pack: tx=0x%08x, rx=0x%08x, tx_cfg=0x%08x\n",
	    m_clk, s_clk, tx_st, rx_st, tx_p0, rx_p0, tx_cfg);
}

/* Configure Picasso TLMM pins for WCN3990 Bluetooth UART */
static void
picasso_gpio_setup(void)
{
	/* GPIO 73: Antenna switch (qca,bt-sw-ctrl-gpio) -> output high */
	wr(GPIO_CFG(tlmm_base, 73), (0 << 2) | CFG_OUTPUT | 0);
	wr(GPIO_IO(tlmm_base, 73), 2); /* drive high */

	/* GPIO 38: UART RX -> func 1 (qup05), no-pull */
	wr(GPIO_CFG(tlmm_base, 38), (1 << 2) | 0);

	/* GPIO 39: UART RTS -> func 1 (qup05), pull-down */
	wr(GPIO_CFG(tlmm_base, 39), (1 << 2) | 1);

	/* GPIO 40: UART TX -> func 1 (qup05), pull-up */
	wr(GPIO_CFG(tlmm_base, 40), (1 << 2) | 3);

	/* GPIO 41: UART CTS -> func 1 (qup05), no-pull */
	wr(GPIO_CFG(tlmm_base, 41), (1 << 2) | 0);

	say("bt: picasso: GPIO 38-41 configured for qup05 UART, GPIO 73 antenna switch high\n");
}

/* Set baud rate on QUPV3 SE5 UART */
static void
qup_uart_set_baud(uint32_t baud)
{
	uint32_t clk_div = 1;
	if (baud == 2400) {
		clk_div = 48;
	} else if (baud == 115200) {
		clk_div = 1;
	} else if (baud == 3000000) {
		clk_div = 1;
	}

	uint32_t ser_clk_cfg = 1 | (clk_div << 4); /* SER_CLK_EN | (clk_div << 4) */
	say("bt: picasso: qup_uart_set_baud(%u): writing ser_clk_cfg=0x%08x\n", baud, ser_clk_cfg);
	wr(qup_se5_base + SE_GENI_SER_M_CLK_CFG, ser_clk_cfg);
	wr(qup_se5_base + SE_GENI_SER_S_CLK_CFG, ser_clk_cfg);
	say("bt: picasso: qup_uart_set_baud(%u) done\n", baud);
}

/* Initialize QUPV3 SE5 UART controller */
static void
qup_uart_init(void)
{
	say("bt: picasso: qup_uart_init start\n");

	/* Select FIFO mode, disable DMA */
	wr(qup_se5_base + SE_GENI_DMA_MODE_EN, 0);

	/* Configure standard Qualcomm 4-byte packing */
	wr(qup_se5_base + SE_GENI_TX_PACKING_CFG0, 0x0004380e);
	wr(qup_se5_base + SE_GENI_TX_PACKING_CFG1, 0x000c3a0e);
	wr(qup_se5_base + SE_GENI_RX_PACKING_CFG0, 0x0004380e);
	wr(qup_se5_base + SE_GENI_RX_PACKING_CFG1, 0x000c3a0e);

	/* Ensure S-sequencer (RX) clock matches M-sequencer clock */
	uint32_t m_clk = rd(qup_se5_base + SE_GENI_SER_M_CLK_CFG);
	if (!m_clk) m_clk = 0x11;
	wr(qup_se5_base + SE_GENI_SER_S_CLK_CFG, m_clk);

	/* Word length: 8 bits */
	wr(qup_se5_base + SE_UART_TX_WORD_LEN, 8);
	wr(qup_se5_base + SE_UART_RX_WORD_LEN, 8);

	/* Stop bits: 1 */
	wr(qup_se5_base + SE_UART_TX_STOP_BIT_LEN, 0);

	/* Parity: none */
	wr(qup_se5_base + SE_UART_TX_PARITY_CFG, 0);
	wr(qup_se5_base + SE_UART_RX_PARITY_CFG, 0);

	/* RX stale timeout */
	wr(qup_se5_base + SE_UART_RX_STALE_CNT, 160);

	/* Transmit config: mask CTS during bringup so TX never blocks */
	wr(qup_se5_base + SE_UART_TX_TRANS_CFG, UART_CTS_MASK);
	wr(qup_se5_base + SE_UART_RX_TRANS_CFG, 0);

	/* Watermarks */
	wr(qup_se5_base + SE_GENI_TX_WATERMARK, 2);
	wr(qup_se5_base + SE_GENI_RX_WATERMARK, 2);

	/* Assert RTS (manual flow control ready) so WCN3990 knows host is ready */
	wr(qup_se5_base + SE_UART_MANUAL_RFR, (1u << 31) | 1u);

	/* Enable M and S interrupts in hardware so M_CMD_DONE and S_CMD_DONE are latched */
	wr(qup_se5_base + SE_GENI_M_IRQ_EN,
	   M_CMD_DONE_EN | M_CMD_ABORT_EN | (1u << 4) | (1u << 30) | (1u << 23) | (1u << 22));
	wr(qup_se5_base + SE_GENI_S_IRQ_EN,
	   S_CMD_DONE_EN | S_CMD_ABORT_EN | (1u << 26) | (1u << 27));

	/* Clear pending interrupts */
	wr(qup_se5_base + SE_GENI_M_IRQ_CLEAR, 0xffffffff);
	wr(qup_se5_base + SE_GENI_S_IRQ_CLEAR, 0xffffffff);
	say("bt: picasso: qup_uart_init complete (dma_mode=0x%08x, tx_p0=0x%08x, m_irq_en=0x%08x)\n",
	    rd(qup_se5_base + SE_GENI_DMA_MODE_EN),
	    rd(qup_se5_base + SE_GENI_TX_PACKING_CFG0),
	    rd(qup_se5_base + SE_GENI_M_IRQ_EN));
}

/* Transmit bytes over QUPV3 SE5 UART */
static int
qup_uart_tx(const uint8_t *buf, size_t len)
{
	if (!qup_se5_base || !buf || len == 0) return 0;

	/* Wait for any ongoing M-sequencer command to complete */
	int retries = 5000;
	while ((rd(qup_se5_base + SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) && retries-- > 0) {
		spin_us(10);
	}
	if (retries <= 0) {
		say("bt: picasso: TX aborted: M_GENI_CMD_ACTIVE stuck (status=0x%08x)\n",
		    rd(qup_se5_base + SE_GENI_STATUS));
		wr(qup_se5_base + SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
		int a_retries = 5000;
		while (!(rd(qup_se5_base + SE_GENI_M_IRQ_STATUS) & M_CMD_ABORT_EN) && a_retries-- > 0) spin_us(10);
		wr(qup_se5_base + SE_GENI_M_IRQ_CLEAR, 0xffffffff);
	}

	wr(qup_se5_base + SE_GENI_M_IRQ_CLEAR, 0xffffffff);
	wr(qup_se5_base + SE_GENI_TX_WATERMARK, 4);

	wr(qup_se5_base + SE_UART_TX_TRANS_LEN, (uint32_t)len);
	wr(qup_se5_base + SE_GENI_M_CMD0, UART_START_TX);

	for (size_t i = 0; i < len; i += 4) {
		int w_retries = 5000;
		while ((rd(qup_se5_base + SE_GENI_TX_FIFO_STATUS) & 0x7f) >= 16 && w_retries-- > 0) {
			spin_us(10);
		}
		if (w_retries <= 0) {
			say("bt: picasso: TX FIFO full timeout at offset %zu, aborting\n", i);
			wr(qup_se5_base + SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
			int a_retries = 5000;
			while (!(rd(qup_se5_base + SE_GENI_M_IRQ_STATUS) & M_CMD_ABORT_EN) && a_retries-- > 0) spin_us(10);
			wr(qup_se5_base + SE_GENI_M_IRQ_CLEAR, 0xffffffff);
			return -1;
		}
		uint32_t word = 0;
		for (size_t b = 0; b < 4 && i + b < len; b++) {
			word |= ((uint32_t)buf[i + b]) << (b * 8);
		}
		wr(qup_se5_base + SE_GENI_TX_FIFOn, word);
		wr(qup_se5_base + SE_GENI_M_IRQ_CLEAR, (1u << 30));
	}

	retries = 20000;
	while (retries-- > 0) {
		uint32_t status = rd(qup_se5_base + SE_GENI_M_IRQ_STATUS);
		if (status & M_CMD_DONE_EN) {
			wr(qup_se5_base + SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN);
			break;
		}
		if (!(rd(qup_se5_base + SE_GENI_STATUS) & M_GENI_CMD_ACTIVE)) {
			break;
		}
		spin_us(10);
	}
	return 0;
}

/* Flush any stale bytes from RX FIFO */
static void
qup_uart_flush_rx(void)
{
	if (!qup_se5_base) return;
	int loops = 100;
	while (loops-- > 0) {
		uint32_t st = rd(qup_se5_base + SE_GENI_RX_FIFO_STATUS);
		uint32_t wc = st & 0x01ffffff;
		if (wc == 0) break;
		while (wc-- > 0) {
			rd(qup_se5_base + SE_GENI_RX_FIFOn);
		}
	}
	wr(qup_se5_base + SE_GENI_S_IRQ_CLEAR, 0xffffffff);
}

/* Start RX sequencer */
static void
qup_uart_start_rx(void)
{
	wr(qup_se5_base + SE_GENI_SER_S_CLK_CFG, rd(qup_se5_base + SE_GENI_SER_M_CLK_CFG));
	/* Assert RTS (manual flow control ready) so WCN3990 knows host is ready */
	wr(qup_se5_base + SE_UART_MANUAL_RFR, (1u << 31) | 1u);

	uint32_t status = rd(qup_se5_base + SE_GENI_STATUS);
	if (!(status & S_GENI_CMD_ACTIVE)) {
		say("bt: picasso: qup_uart_start_rx: writing S_CMD0 (0x%08x)\n", UART_START_READ | UART_PARAM_RFR_OPEN);
		wr(qup_se5_base + SE_GENI_S_CMD0, UART_START_READ | UART_PARAM_RFR_OPEN);
		int retries = 1000;
		while (!(rd(qup_se5_base + SE_GENI_STATUS) & S_GENI_CMD_ACTIVE) && retries-- > 0) {
			spin_us(10);
		}
		say("bt: picasso: qup_uart_start_rx: S_CMD active=%u (status=0x%08x)\n",
		    !!(rd(qup_se5_base + SE_GENI_STATUS) & S_GENI_CMD_ACTIVE), rd(qup_se5_base + SE_GENI_STATUS));
	} else {
		say("bt: picasso: qup_uart_start_rx: already active (status=0x%08x)\n", status);
	}
}

/* Receive bytes from QUPV3 SE5 UART with timeout (milliseconds) */
static size_t
qup_uart_rx(uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
	if (!qup_se5_base || max_len == 0) return 0;

	size_t rx_len = 0;
	uint32_t loops = timeout_ms * 100;

	while (rx_len < max_len && loops-- > 0) {
		uint32_t status = rd(qup_se5_base + SE_GENI_RX_FIFO_STATUS);
		uint32_t word_cnt = status & 0x01ffffff;
		if (word_cnt > 0) {
			for (uint32_t w = 0; w < word_cnt && rx_len < max_len; w++) {
				uint32_t word = rd(qup_se5_base + SE_GENI_RX_FIFOn);
				say("bt: rx word: 0x%08x (status=0x%08x)\n", word, status);
				uint32_t byte_cnt = 4;
				if ((w == word_cnt - 1) && (status & (1u << 31))) {
					uint32_t last_valid = (status >> 28) & 0x7;
					if (last_valid > 0 && last_valid < 4) {
						byte_cnt = last_valid;
					}
				}
				uint32_t val = word;
				for (uint32_t b = 0; b < byte_cnt && rx_len < max_len; b++) {
					buf[rx_len++] = (uint8_t)(val & 0xff);
					val >>= 8;
				}
			}
			wr(qup_se5_base + SE_GENI_S_IRQ_CLEAR, rd(qup_se5_base + SE_GENI_S_IRQ_STATUS));
			loops = 500;
		} else {
			spin_us(10);
		}
	}
	return rx_len;
}

/* Send WCN3990 in-band power pulses per Linux hci_qca */
static void
wcn3990_power_on(void)
{
	qup_uart_init();

	/* 1. Send power pulses at 2400 baud */
	say("bt: picasso: sending WCN3990 power-off pulse (2400 baud, 0xC0)...\n");
	qup_uart_set_baud(2400);
	sleep_ms(10);
	uint8_t p_off = 0xC0;
	qup_uart_tx(&p_off, 1);
	sleep_ms(10);

	say("bt: picasso: sending WCN3990 power-on pulse (2400 baud, 0xFC)...\n");
	uint8_t p_on = 0xFC;
	qup_uart_tx(&p_on, 1);
	sleep_ms(100);

	/* 2. Set 115200 baud and repeat power-on pulse (as required by some revisions) */
	qup_uart_set_baud(115200);
	sleep_ms(10);
	say("bt: picasso: sending WCN3990 power-on pulse (115200 baud, 0xFC)...\n");
	qup_uart_tx(&p_on, 1);

	say("bt: picasso: waiting 100ms for WCN3990 to boot...\n");
	sleep_ms(100);

	say("bt: picasso: WCN3990 power sequence complete!\n");
}

/* Probe WCN3990 with standard HCI commands */
static void
wcn3990_probe_hci(void)
{
	qup_uart_flush_rx();
	qup_uart_start_rx();

	/* 1. Qualcomm EDL Patch Version Request: 0x01, 0x00, 0xFC, 0x01, 0x19 */
	say("bt: picasso: sending Qualcomm EDL Patch Version Request...\n");
	const uint8_t edl_req[] = { 0x01, 0x00, 0xFC, 0x01, 0x19 };
	qup_uart_tx(edl_req, sizeof(edl_req));

	uint8_t resp[64];
	size_t n = qup_uart_rx(resp, sizeof(resp), 500);
	if (n > 0) {
		say("bt: picasso: EDL Version Response (%zu bytes):", n);
		for (size_t i = 0; i < n; i++) {
			say(" %02x", resp[i]);
		}
		say("\n");
		if (n >= 12 && resp[0] == 0x04 && resp[1] == 0xff) {
			uint32_t product_id = resp[4] | (resp[5] << 8) | (resp[6] << 16) | (resp[7] << 24);
			uint16_t patch_ver = resp[8] | (resp[9] << 8);
			uint16_t rome_ver = resp[10] | (resp[11] << 8);
			say("bt: picasso: Qualcomm WCN3990 detected: product_id=0x%08x, patch_ver=0x%04x, rome_ver=0x%04x\n",
			    product_id, patch_ver, rome_ver);
		}
	} else {
		say("bt: picasso: EDL Version Request: no response (will attempt HCI_Reset)\n");
	}

	qup_uart_flush_rx();

	/* 2. HCI_Reset: 0x01, 0x03, 0x0C, 0x00 */
	say("bt: picasso: sending HCI_Reset (0x01, 0x03, 0x0C, 0x00)...\n");
	const uint8_t hci_reset[] = { 0x01, 0x03, 0x0C, 0x00 };
	qup_uart_tx(hci_reset, sizeof(hci_reset));

	n = qup_uart_rx(resp, sizeof(resp), 500);
	if (n > 0) {
		say("bt: picasso: HCI_Reset Response (%zu bytes):", n);
		for (size_t i = 0; i < n; i++) {
			say(" %02x", resp[i]);
		}
		say("\n");
		if (n >= 6 && resp[0] == 0x04 && resp[1] == 0x0e && resp[6] == 0x00) {
			say("bt: picasso: HCI_Reset SUCCESS (Status: 0x00)!\n");
		}
	} else {
		say("bt: picasso: HCI_Reset: no response\n");
	}

	qup_uart_flush_rx();

	/* 3. HCI_Read_BD_ADDR: 0x01, 0x09, 0x10, 0x00 */
	say("bt: picasso: sending HCI_Read_BD_ADDR (0x01, 0x09, 0x10, 0x00)...\n");
	const uint8_t hci_bdaddr[] = { 0x01, 0x09, 0x10, 0x00 };
	qup_uart_tx(hci_bdaddr, sizeof(hci_bdaddr));

	n = qup_uart_rx(resp, sizeof(resp), 500);
	if (n > 0) {
		say("bt: picasso: HCI_Read_BD_ADDR Response (%zu bytes):", n);
		for (size_t i = 0; i < n; i++) {
			say(" %02x", resp[i]);
		}
		say("\n");
		if (n >= 12 && resp[0] == 0x04 && resp[1] == 0x0e && resp[6] == 0x00) {
			say("bt: picasso: Bluetooth BD_ADDR: %02X:%02X:%02X:%02X:%02X:%02X\n",
			    resp[12], resp[11], resp[10], resp[9], resp[8], resp[7]);
		}
	} else {
		say("bt: picasso: HCI_Read_BD_ADDR: no response\n");
	}
}

/* Character device /dev/hakios-bt implementation */
static int
bt_open(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	qup_uart_start_rx();
	return 0;
}

static int
bt_close(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	return 0;
}

static int
bt_read(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
	uint8_t temp[128];
	size_t requested = uio_resid(uio);
	if (requested == 0) return 0;
	if (requested > sizeof(temp)) requested = sizeof(temp);

	size_t read_bytes = qup_uart_rx(temp, requested, 100);
	if (read_bytes == 0) return 0;

	return uiomove((caddr_t)temp, (int)read_bytes, uio);
}

static int
bt_write(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
	uint8_t temp[256];
	size_t len = uio_resid(uio);
	if (len == 0) return 0;
	if (len > sizeof(temp)) len = sizeof(temp);

	int error = uiomove((caddr_t)temp, (int)len, uio);
	if (error) return error;

	int ret = qup_uart_tx(temp, len);
	return (ret == 0) ? 0 : EIO;
}

static int
bt_ioctl(__unused dev_t dev, __unused u_long cmd, __unused caddr_t data, __unused int flag, __unused struct proc *p)
{
	return ENOTTY;
}

static const struct cdevsw bt_cdevsw = {
	.d_open = bt_open,
	.d_close = bt_close,
	.d_read = bt_read,
	.d_write = bt_write,
	.d_ioctl = bt_ioctl,
	.d_stop = eno_stop,
	.d_reset = eno_reset,
	.d_select = eno_select,
	.d_mmap = eno_mmap,
	.d_strategy = eno_strat,
	.d_type = 0
};

static void
bt_thread(__unused void *parameter, __unused wait_result_t result)
{
	say("bt: picasso: background worker thread started for bt_level=%u\n", bt_level);
	/* Wait 5.5s for modem to boot and power WCN3990 rails (mission mode at ~4.5s) */
	sleep_ms(5500);

	if (!tlmm_base) {
		tlmm_base = ml_io_map(PICASSO_TLMM_EAST_PHYS, PICASSO_TLMM_SIZE);
	}
	if (!qup_se5_base) {
		qup_se5_base = ml_io_map(PICASSO_QUP_SE5_PHYS, PICASSO_QUP_SE5_SIZE);
	}
	if (!gcc_base) {
		gcc_base = ml_io_map(0x00100000, 0x100000);
	}

	if (bt_level >= 1) {
		if (gcc_base) {
			uint32_t vote = rd(gcc_base + 0x52008);
			uint32_t halt = rd(gcc_base + 0x175fc);
			uint32_t rcgr = rd(gcc_base + 0x17600);
			say("bt: picasso: GCC vote (0x52008)=0x%08x (bit 15=%u), halt=0x%08x, rcgr=0x%08x\n",
			    vote, (vote >> 15) & 1, halt, rcgr);
			wr(gcc_base + 0x52008, vote | (1u << 15) | (1u << 9) | (1u << 8) | (1u << 7) | (1u << 6));
			say("bt: picasso: GCC vote after enable: 0x%08x, halt: 0x%08x\n",
			    rd(gcc_base + 0x52008), rd(gcc_base + 0x175fc));
		}

		say("bt: picasso: 1. configuring GPIO pins...\n");
		picasso_gpio_setup();
		sleep_ms(20);

		say("bt: picasso: 2. initializing QUP UART...\n");
		qup_uart_init();
		sleep_ms(20);

		say("bt: picasso: 3. creating /dev/hakios-bt...\n");
		int major = cdevsw_add(-1, &bt_cdevsw);
		if (major >= 0) {
			bt_devfs_node = devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "hakios-bt");
			if (bt_devfs_node) {
				say("bt: registered Darwin character device /dev/hakios-bt (major %d)\n", major);
			} else {
				say("bt: failed to create /dev/hakios-bt in devfs\n");
			}
		} else {
			say("bt: failed to add cdevsw for hakios-bt\n");
		}
	}

	if (bt_level >= 2) {
		say("bt: picasso: 4. sending power pulses...\n");
		wcn3990_power_on();
		sleep_ms(20);

		say("bt: picasso: 5. starting RX sequencer...\n");
		qup_uart_start_rx();
		sleep_ms(20);

		say("bt: picasso: 6. checking pin levels...\n");
		look("after power-on");
		say("bt: picasso: 7. probing QUP SE...\n");
		probe_qup_se();
	}

	if (bt_level >= 3) {
		say("bt: picasso: 8. probing HCI...\n");
		wcn3990_probe_hci();
	}

	say("bt: picasso: background worker thread finished\n");
	thread_terminate(current_thread());
}

void
hakios_bt_init(void)
{
	thread_t thread;

	say("bt: hakios_bt_init entry, checking bt boot-arg\n");
	if (!PE_parse_boot_argn("bt", &bt_level, sizeof(bt_level)) || !bt_level) {
		return;
	}

	say("bt: starting bt_thread for bt_level=%u...\n", bt_level);
	if (kernel_thread_start(bt_thread, NULL, &thread) == KERN_SUCCESS) {
		say("bt: bt_thread started successfully\n");
		thread_deallocate(thread);
	} else {
		say("bt: failed to start kernel thread\n");
	}
}

static void
look_again(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
{
	look("again");
}

__startup_func
static void
picasso_bt_early_init(void)
{
	uint32_t value;
	uint64_t deadline;

	if (!PE_parse_boot_argn("bt", &bt_level, sizeof(bt_level)) || !bt_level) {
		return;
	}

	if (PE_parse_boot_argn("bt_again", &value, sizeof(value)) && value <= 3600) {
		again_seconds = value;
	}

	tlmm_base = ml_io_map(PICASSO_TLMM_EAST_PHYS, PICASSO_TLMM_SIZE);
	if (tlmm_base == 0) {
		say("bt: picasso: could not map TLMM EAST (0x0f900000); off\n");
		return;
	}
	qup_se5_base = ml_io_map(PICASSO_QUP_SE5_PHYS, PICASSO_QUP_SE5_SIZE);
	if (qup_se5_base == 0) {
		say("bt: picasso: could not map QUPV3 SE5 (0x00894000)\n");
		return;
	}

	say("bt: level %u: picasso Qualcomm WCN3990 via QUPV3 SE5 UART\n", bt_level);
	look("at start");
	probe_qup_se();

	if (again_seconds != 0) {
		again_call = thread_call_allocate(look_again, NULL);
		clock_interval_to_deadline(again_seconds, NSEC_PER_SEC, &deadline);
		thread_call_enter_delayed(again_call, deadline);
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, picasso_bt_early_init);
