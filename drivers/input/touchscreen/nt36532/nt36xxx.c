// SPDX-License-Identifier: GPL-2.0-only
/*
 * Novatek NT36532 touchscreen driver, TB371FC stock-parity rebuild.
 * Specification source: original firmware Image reverse engineering only.
 */

#include "nt36xxx.h"

struct nvt_ts_data *nvt_ts;
bool nvt_gesture_flag;
u8 bTouchIsAwake;
u8 gpio_82;
u32 nvt_hall_pm;
u32 nvt_hall_pm_status;
u32 nvt_hall_status;
u32 nvt_hall_work_status;
s64 nvt_hall_timer0;
u32 nvt_x_error_count;
char *BOOT_UPDATE_FIRMWARE_NAME = "novatek_ts_fw_boe_6.bin";
char *MP_UPDATE_FIRMWARE_NAME = "novatek_ts_mp_boe_6.bin";
/* TASK-081 D90b: stock MP grid is 60x80 (4800 points); the 32x32/4K
 * arrays from D81a overflowed on the first self_test run (boot panic,
 * uptime 33s, panic_on_oops=1). Size for the largest shipping grid. */
static int32_t nvt_mdata[64 * 128];

struct workqueue_struct *nvt_fwu_wq;

static const u32 nvt_hw_reg_addr_info[3] = { 0x001fb104, 0x001fb43e, 0x001fb535 };
static const u32 nvt_hw_reg_addr_info_old_w_isp[3] = { 0x0003f004, 0x0003f0fe, 0x0003f135 };
static const u32 nvt_hw_reg_addr_info_legacy_w_isp[3] = { 0x0001f64e, 0x0001f01a, 0x00000000 };
static u32 nvt_spi_rd_fast_addr;

#define NVT_MM ((const struct nvt_mm *)nvt_ts->mmap)
/* Original trim ID table for IC identification. */
struct nvt_trim_entry {
	u8 id[8];
	u8 mmap_idx;
	u8 cascade_idx;
	u8 cfg_idx;
	/* stock hwinfo per trim entry: hw_crc (HWCRC_NOSUPPORT/2B/3B)
	 * and auto_copy (AUTOCOPY check mode). hw_crc is not spi_dma.
	 */
	u8 hw_crc;
	u8 auto_copy;
};

struct nvt_cfg_entry {
	u8 chip_ver_trim_slot;
	u8 crc_err_flag_slot;
	const u32 *swrst_sif_addr;
};

static const struct nvt_cfg_entry nvt_cfg_table[9] = {
	{ 0x02, 0x02, nvt_hw_reg_addr_info },
	{ 0x02, 0x01, nvt_hw_reg_addr_info_old_w_isp },
	{ 0x02, 0x00, nvt_hw_reg_addr_info_old_w_isp },
	{ 0x02, 0x00, nvt_hw_reg_addr_info_old_w_isp },
	{ 0x01, 0x00, nvt_hw_reg_addr_info_old_w_isp },
	{ 0x02, 0x00, nvt_hw_reg_addr_info_old_w_isp },
	{ 0x00, 0x00, nvt_hw_reg_addr_info_legacy_w_isp },
	{ 0x00, 0x00, nvt_hw_reg_addr_info_legacy_w_isp },
	{ 0x00, 0x00, nvt_hw_reg_addr_info_legacy_w_isp },
};

const struct nvt_trim_entry trim_id_table[36] = {
	{ { 0xff, 0xff, 0xff, 0x32, 0x65, 0x03, 0x00, 0x00 }, 0, 1, 0, 2, 2 },
	{ { 0x17, 0xff, 0xff, 0x23, 0x65, 0x03, 0x01, 0x00 }, 2, 255, 1, 2, 1 },
	{ { 0xff, 0xff, 0xff, 0x77, 0x66, 0x03, 0x00, 0x00 }, 3, 255, 2, 2, 0 },
	{ { 0xff, 0xff, 0xff, 0x28, 0x65, 0x03, 0x00, 0x00 }, 4, 255, 3, 2, 0 },
	{ { 0x0d, 0xff, 0xff, 0x72, 0x66, 0x03, 0x01, 0x00 }, 3, 255, 4, 2, 0 },
	{ { 0x20, 0xff, 0xff, 0x72, 0x66, 0x03, 0x01, 0x00 }, 3, 255, 5, 2, 0 },
	{ { 0x00, 0xff, 0xff, 0x80, 0x66, 0x03, 0x01, 0x00 }, 3, 255, 5, 2, 0 },
	{ { 0x0c, 0xff, 0xff, 0x25, 0x65, 0x03, 0x01, 0x00 }, 5, 255, 1, 1, 0 },
	{ { 0x0e, 0xff, 0xff, 0x72, 0x66, 0x03, 0x01, 0x00 }, 3, 255, 4, 2, 0 },
	{ { 0x20, 0xff, 0xff, 0x23, 0x65, 0x03, 0x01, 0x00 }, 6, 255, 1, 2, 1 },
	{ { 0x0c, 0xff, 0xff, 0x23, 0x65, 0x03, 0x01, 0x00 }, 6, 255, 1, 2, 1 },
	{ { 0x0b, 0xff, 0xff, 0x23, 0x65, 0x03, 0x01, 0x00 }, 6, 255, 1, 2, 1 },
	{ { 0x0a, 0xff, 0xff, 0x23, 0x65, 0x03, 0x01, 0x00 }, 6, 255, 1, 2, 1 },
	{ { 0xff, 0xff, 0xff, 0x23, 0x65, 0x03, 0x00, 0x00 }, 6, 255, 1, 2, 1 },
	{ { 0x0c, 0xff, 0xff, 0x72, 0x66, 0x03, 0x01, 0x00 }, 3, 255, 4, 2, 0 },
	{ { 0xff, 0xff, 0xff, 0x26, 0x65, 0x03, 0x00, 0x00 }, 7, 255, 2, 2, 0 },
	{ { 0xff, 0xff, 0xff, 0x75, 0x66, 0x03, 0x00, 0x00 }, 3, 255, 5, 2, 0 },
	{ { 0x0b, 0xff, 0xff, 0x72, 0x66, 0x03, 0x01, 0x00 }, 5, 255, 4, 1, 0 },
	{ { 0x0b, 0xff, 0xff, 0x82, 0x66, 0x03, 0x01, 0x00 }, 5, 255, 5, 1, 0 },
	{ { 0x0b, 0xff, 0xff, 0x25, 0x65, 0x03, 0x01, 0x00 }, 5, 255, 1, 1, 0 },
	{ { 0x0a, 0xff, 0xff, 0x72, 0x65, 0x03, 0x01, 0x00 }, 5, 255, 4, 1, 0 },
	{ { 0x0a, 0xff, 0xff, 0x72, 0x66, 0x03, 0x01, 0x00 }, 5, 255, 4, 1, 0 },
	{ { 0x0a, 0xff, 0xff, 0x82, 0x66, 0x03, 0x01, 0x00 }, 5, 255, 5, 1, 0 },
	{ { 0x0a, 0xff, 0xff, 0x70, 0x66, 0x03, 0x01, 0x00 }, 5, 255, 4, 1, 0 },
	{ { 0x0b, 0xff, 0xff, 0x70, 0x66, 0x03, 0x01, 0x00 }, 5, 255, 4, 1, 0 },
	{ { 0x0a, 0xff, 0xff, 0x72, 0x67, 0x03, 0x01, 0x00 }, 5, 255, 5, 1, 0 },
	{ { 0x55, 0x00, 0xff, 0x00, 0x00, 0x00, 0x01, 0x01 }, 8, 255, 6, 0, 0 },
	{ { 0x55, 0x72, 0xff, 0x00, 0x00, 0x00, 0x01, 0x01 }, 8, 255, 6, 0, 0 },
	{ { 0xaa, 0x00, 0xff, 0x00, 0x00, 0x00, 0x01, 0x01 }, 8, 255, 6, 0, 0 },
	{ { 0xaa, 0x72, 0xff, 0x00, 0x00, 0x00, 0x01, 0x01 }, 8, 255, 6, 0, 0 },
	{ { 0xff, 0xff, 0xff, 0x72, 0x67, 0x03, 0x00, 0x00 }, 8, 255, 6, 0, 0 },
	{ { 0xff, 0xff, 0xff, 0x70, 0x66, 0x03, 0x00, 0x00 }, 8, 255, 6, 0, 0 },
	{ { 0xff, 0xff, 0xff, 0x70, 0x67, 0x03, 0x00, 0x00 }, 8, 255, 6, 0, 0 },
	{ { 0xff, 0xff, 0xff, 0x72, 0x66, 0x03, 0x00, 0x00 }, 8, 255, 6, 0, 0 },
	{ { 0xff, 0xff, 0xff, 0x25, 0x65, 0x03, 0x00, 0x00 }, 9, 255, 6, 0, 0 },
	{ { 0xff, 0xff, 0xff, 0x76, 0x66, 0x03, 0x00, 0x00 }, 9, 255, 6, 0, 0 },
};

/* Original mmap tables, byte-for-byte from the stock Image. */
static const u32 nvt_mmap_0[50] = {
	0x00125800, 0x0010c1ec, 0x0010c1ec, 0x0010b1ec, 0x00000000, 0x0011bfc4,
	0x0011cfc4, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0011e5ec,
	0x00126c00, 0x0011e7ec, 0x00126e00, 0x0011e1ec, 0x0011e9ec, 0x0011e3ec,
	0x0011ebec, 0x00120cec, 0x00120efc, 0x0012110c, 0x0012131c, 0x00126a00,
	0x00126a80, 0x00126b00, 0x00126b80, 0x001fb12c, 0x00000001, 0x00122930,
	0x00122970, 0x00000000, 0x00000000, 0x001fb50d, 0x00000000, 0x001fc925,
	0x001fc914, 0x001fb538, 0x001fb518, 0x001fb530, 0x001fb514, 0x001fb528,
	0x001fb52c, 0x001fb500, 0x001fb504, 0x001fb520, 0x001fb524, 0x001fb536,
	0x001fb533, 0x001fb534
};

static const u32 nvt_mmap_1[50] = {
	0x00125800, 0x0010b200, 0x0010b200, 0x00109e00, 0x00000000, 0x00128140,
	0x00129540, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0010f940,
	0x0010fd40, 0x00110140, 0x00110540, 0x00111140, 0x00111540, 0x00111940,
	0x00111d40, 0x00126e00, 0x00127210, 0x00127620, 0x00127a30, 0x00127e40,
	0x00127ec0, 0x00127f40, 0x00127fc0, 0x001fb12c, 0x00000001, 0x00121afc,
	0x00121b3c, 0x00000000, 0x00000000, 0x001fb50d, 0x00000000, 0x001fc925,
	0x001fc914, 0x001fb538, 0x001fb518, 0x001fb530, 0x001fb514, 0x001fb528,
	0x001fb52c, 0x001fb500, 0x001fb504, 0x001fb520, 0x001fb524, 0x001fb536,
	0x001fb533, 0x001fb534
};

static const u32 nvt_mmap_2[50] = {
	0x0002fd00, 0x00030fa0, 0x00030fa0, 0x00036510, 0x00000000, 0x000373e8,
	0x00038068, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0002988a,
	0x00029a1a, 0x00029baa, 0x00029d3a, 0x00029eca, 0x0002a05a, 0x0002a1ea,
	0x0002a37a, 0x0002a50a, 0x0002a69a, 0x0002a82a, 0x0002a9ba, 0x0002ab4a,
	0x0002abae, 0x0002ac12, 0x0002ac76, 0x00000000, 0x00000000, 0x00038d54,
	0x00038d94, 0x00000000, 0x00000000, 0x0003f10d, 0x00000000, 0x0003f7e8,
	0x0003f7f1, 0x0003f138, 0x0003f118, 0x0003f130, 0x0003f114, 0x0003f128,
	0x0003f12c, 0x0003f100, 0x0003f104, 0x0003f120, 0x0003f124, 0x0003f136,
	0x0003f133, 0x0003f134
};

static const u32 nvt_mmap_3[50] = {
	0x00022d00, 0x00024000, 0x00024000, 0x00021b90, 0x00000000, 0x00020c60,
	0x00024c60, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00023d10,
	0x00023d50, 0x00000000, 0x00000000, 0x0003f10d, 0x00000000, 0x00000000,
	0x00000000, 0x0003f138, 0x0003f118, 0x0003f130, 0x0003f114, 0x0003f128,
	0x0003f12c, 0x0003f100, 0x0003f104, 0x0003f120, 0x0003f124, 0x0003f136,
	0x0003f133, 0x0003f134
};

static const u32 nvt_mmap_4[50] = {
	0x00022d00, 0x00024000, 0x00024000, 0x00021880, 0x00000000, 0x00020b00,
	0x00024b00, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00023768,
	0x000237a8, 0x00000000, 0x00000000, 0x0003f10d, 0x00000000, 0x00000000,
	0x00000000, 0x0003f138, 0x0003f118, 0x0003f130, 0x0003f114, 0x0003f128,
	0x0003f12c, 0x0003f100, 0x0003f104, 0x0003f120, 0x0003f124, 0x0003f136,
	0x0003f133, 0x0003f134
};

static const u32 nvt_mmap_5[50] = {
	0x00021c00, 0x00020000, 0x00023000, 0x00020bfc, 0x00023bfc, 0x000206dc,
	0x000236dc, 0x00020510, 0x00023510, 0x00020bf0, 0x00023bf0, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x000218d6,
	0x00024b2d, 0x00000000, 0x00000000, 0x0003f10d, 0x00000000, 0x00000000,
	0x00000000, 0x0003f10e, 0x0003f118, 0x0003f130, 0x0003f114, 0x0003f128,
	0x0003f12c, 0x0003f100, 0x0003f104, 0x0003f120, 0x0003f124, 0x0003f132,
	0x0003f133, 0x0003f134
};

static const u32 nvt_mmap_6[50] = {
	0x0002fe00, 0x00030fa0, 0x00030fa0, 0x00036510, 0x00000000, 0x000373e8,
	0x00038068, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0002988a,
	0x00029a1a, 0x00029baa, 0x00029d3a, 0x00029eca, 0x0002a05a, 0x0002a1ea,
	0x0002a37a, 0x0002a50a, 0x0002a69a, 0x0002a82a, 0x0002a9ba, 0x0002ab4a,
	0x0002abae, 0x0002ac12, 0x0002ac76, 0x00000000, 0x00000000, 0x00038d54,
	0x00038d94, 0x00000000, 0x00000000, 0x0003f10d, 0x00000000, 0x0003f7e8,
	0x0003f7f1, 0x0003f138, 0x0003f118, 0x0003f130, 0x0003f114, 0x0003f128,
	0x0003f12c, 0x0003f100, 0x0003f104, 0x0003f120, 0x0003f124, 0x0003f136,
	0x0003f133, 0x0003f134
};

static const u32 nvt_mmap_7[50] = {
	0x00022d00, 0x00024000, 0x00024000, 0x00021758, 0x00000000, 0x00020ab0,
	0x00024ab0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00023c38,
	0x00023c78, 0x00000000, 0x00000000, 0x0003f10d, 0x00000000, 0x00000000,
	0x00000000, 0x0003f138, 0x0003f118, 0x0003f130, 0x0003f114, 0x0003f128,
	0x0003f12c, 0x0003f100, 0x0003f104, 0x0003f120, 0x0003f124, 0x0003f136,
	0x0003f133, 0x0003f134
};

static const u32 nvt_mmap_8[50] = {
	0x00011e00, 0x00010000, 0x00012000, 0x00010e70, 0x00012e70, 0x00010830,
	0x00012830, 0x00010e60, 0x00012e60, 0x00010e68, 0x00012e68, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x0001f141, 0x0001f61c, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x0001bf00, 0x00000000, 0x00000000,
	0x00000000, 0x00000000
};

static const u32 nvt_mmap_9[50] = {
	0x00011a00, 0x00010000, 0x00012000, 0x00010b08, 0x00012b08, 0x0001064c,
	0x0001264c, 0x00010634, 0x00012634, 0x00010afc, 0x00012afc, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x0001f141, 0x0001f61c, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x0001bf00, 0x00000000, 0x00000000,
	0x00000000, 0x00000000
};

static const u32 nvt_mmap_10[50] = {
	0x00011a00, 0x00010000, 0x00012000, 0x00010b08, 0x00012b08, 0x0001064c,
	0x0001264c, 0x00010634, 0x00012634, 0x00010afc, 0x00012afc, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x0001f141, 0x0001f61c, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x0001bf00, 0x00000000, 0x00000000,
	0x00000000, 0x00000000
};

static const u32 *const nvt_mmap_tables[11] = {
	nvt_mmap_0, nvt_mmap_1, nvt_mmap_2, nvt_mmap_3, nvt_mmap_4,
	nvt_mmap_5, nvt_mmap_6, nvt_mmap_7, nvt_mmap_8, nvt_mmap_9,
	nvt_mmap_10
};

enum nvt_spi_rw {
	NVTWRITE = 0,
	NVTREAD,
};

/* D41: old-main-tree spi_read_write (source-side reference). All SPI
 * traffic goes through the preallocated DMA buffers ts->xbuf (tx) and
 * ts->rbuf (rx). The p548 code handed caller stack/heap buffers
 * directly to the SPI core for 64.5 KB transfers; on this board every
 * frame's last byte was corrupted (DLM tail byte 0x27 -> 0x00, every
 * frame), failing the DLM CRC. The old-tree buffer management is the
 * factory-verified one.
 */
static int nvt_spi_read_write(struct spi_device *client, u8 *buf, size_t len,
			      enum nvt_spi_rw rw)
{
	struct spi_message m;
	struct spi_transfer t = {
		.len = len,
	};

	memset(nvt_ts->xbuf, 0, len + DUMMY_BYTES);
	memcpy(nvt_ts->xbuf, buf, len);

	switch (rw) {
	case NVTREAD:
		t.tx_buf = nvt_ts->xbuf;
		t.rx_buf = nvt_ts->rbuf;
		t.len = len + DUMMY_BYTES;
		break;

	case NVTWRITE:
		t.tx_buf = nvt_ts->xbuf;
		break;
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(client, &m);
}

int CTP_SPI_READ(struct spi_device *client, void *buf, size_t len)
{
	int ret = -1;
	int retries = 0;

	mutex_lock(&nvt_ts->xbuf_lock);

	((u8 *)buf)[0] = ((u8 *)buf)[0] & 0x7F;

	while (retries < 5) {
		ret = nvt_spi_read_write(client, buf, len, NVTREAD);
		if (ret == 0)
			break;
		retries++;
	}

	if (unlikely(retries == 5)) {
		pr_err("NVT-ts: CTP_SPI_READ error, ret=%d\n", ret);
		ret = -EIO;
	}

	memcpy((u8 *)buf + 1, nvt_ts->rbuf + 2, len - 1);

	mutex_unlock(&nvt_ts->xbuf_lock);

	return ret;
}

int CTP_SPI_WRITE(struct spi_device *client, void *buf, size_t len)
{
	int ret = -1;
	int retries = 0;

	mutex_lock(&nvt_ts->xbuf_lock);

	((u8 *)buf)[0] = ((u8 *)buf)[0] | 0x80;

	while (retries < 5) {
		ret = nvt_spi_read_write(client, buf, len, NVTWRITE);
		if (ret == 0)
			break;
		retries++;
	}

	if (unlikely(retries == 5)) {
		pr_err("NVT-ts: CTP_SPI_WRITE error, ret=%d\n", ret);
		ret = -EIO;
	}

	mutex_unlock(&nvt_ts->xbuf_lock);

	return ret;
}

int nvt_set_page(u32 addr)
{
	u8 buf[3] = { 0xff, addr >> 15, addr >> 7 };

	return CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
}

int nvt_write_addr(u32 addr, u8 value)
{
	u8 buf[2] = { addr & 0x7f, value };
	int ret;

	ret = nvt_set_page(addr);
	if (ret)
		return ret;

	return CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
}

int nvt_read_reg(u64 addr_mask, u8 *value)
{
	u32 addr = (u32)addr_mask;
	u32 mask = addr_mask >> 32;
	u8 buf[2] = { addr & 0x7f, 0 };
	u8 page[3] = { 0xff, addr >> 15, addr >> 7 };
	int shift = 0;
	int ret;

	if (!(mask & 1)) {
		int i;

		for (i = 0; i < 8; i++) {
			if ((mask >> i) & 0x2) {
				shift = i + 1;
				break;
			}
		}
		if (i == 8) {
			pr_err("NVT-ts: invalid read mask=0x%08x\n", mask);
			shift = 8;
		}
	}

	CTP_SPI_WRITE(nvt_ts->client, page, sizeof(page));
	ret = CTP_SPI_READ(nvt_ts->client, buf, sizeof(buf));
	if (ret < 0) {
		pr_err("NVT-ts: read reg 0x%06x failed, ret=%d\n", addr, ret);
		return ret;
	}

	*value = (buf[1] & mask) >> shift;
	return ret;
}

int nvt_get_fw_info(void)
{
	u8 buf[64] = { 0 };
	u32 retry_count = 0;
	int32_t ret = 0;

info_retry:
	/* old-tree source; set_page(EVENT | 0x78) == stock set_page(EVENT)
	 * because 0x78 < 0x80 does not change the >>15 / >>7 page bytes. */
	nvt_set_page(NVT_MM->EVENT_BUF_ADDR | EVENT_MAP_FWINFO);

	buf[0] = EVENT_MAP_FWINFO;
	CTP_SPI_READ(nvt_ts->client, buf, 39);
	if ((buf[1] + buf[2]) != 0xFF) {
		pr_err("NVT-ts: FW info is broken! fw_ver=0x%02X, ~fw_ver=0x%02X\n",
		       buf[1], buf[2]);
		if (retry_count < 3) {
			retry_count++;
			pr_err("NVT-ts: retry_count=%d\n", retry_count);
			goto info_retry;
		} else {
			/* D63: stock failure path @0x8ad8d98 stores
			 * fw_ver=0, {abs_x_max,abs_y_max}={0x730,0xb80}
			 * (one 32-bit store to ts+0xdc), max_button_num=0.
			 * The previous 730/1280 were old-tree constants. */
			nvt_ts->fw_ver = 0;
			nvt_ts->abs_x_max = 0x730;
			nvt_ts->abs_y_max = 0xb80;
			nvt_ts->max_button_num = 0;
			pr_err("NVT-ts: Set default fw_ver=%d, abs_x_max=%d, abs_y_max=%d!\n",
			       nvt_ts->fw_ver, nvt_ts->abs_x_max, nvt_ts->abs_y_max);
			ret = -1;
			goto out;
		}
	}
	nvt_ts->fw_ver = buf[1];
	nvt_ts->fw_sub_ver = buf[2];
	nvt_ts->x_num = buf[3];
	nvt_ts->y_num = buf[4];
	nvt_ts->max_button_num = buf[11];
	nvt_ts->nvt_pid = (uint16_t)((buf[36] << 8) | buf[35]);
	if (nvt_ts->pen_support) {
		nvt_ts->x_gang_num = buf[37];
		nvt_ts->y_gang_num = buf[38];
	}
	/* D63: stock nvt_get_fw_info @0x8ad8c28 NEVER parses abs_x_max /
	 * abs_y_max from the fw-info block on the success path (verified in
	 * decompile: only fw_ver ts+0xd8, x_num/y_num ts+0xd9, max_button
	 * ts+0xe1, PID ts+0x122, gang ts+0x15b are stored; ts+0xdc is only
	 * written by the failure-path default 0x0b800730). This FW changes
	 * the buf[5..6] bytes at runtime (observed 0x0101 vs 0x0730), and
	 * the stale old-tree parse clobbered abs_x_max to 257 on every late
	 * call (fw download / proc fw_version read), after which the
	 * workfunc range check dropped every point with x > 2570 - the
	 * "only the dock strip responds" bug. abs stays probe-initialized
	 * 0x730/0xb80 (= stock defaults) from now on. */
	pr_info("NVT-ts: fw_ver=0x%02X, fw_type=0x%02X, PID=0x%04X\n",
		nvt_ts->fw_ver, buf[14], nvt_ts->nvt_pid);

	ret = 0;
out:
	return ret;
}

int nvt_check_fw_reset_state(u8 check_reset_state)
{
	u8 buf[8] = { 0 };
	int32_t ret = 0;
	int32_t retry = 0;
	int32_t retry_max = (check_reset_state == RESET_STATE_INIT) ? 10 : 50;

	nvt_set_page(NVT_MM->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE);

	while (1) {
		buf[0] = EVENT_MAP_RESET_COMPLETE;
		buf[1] = 0x00;
		CTP_SPI_READ(nvt_ts->client, buf, 6);

		if ((buf[1] >= check_reset_state) && (buf[1] <= RESET_STATE_MAX)) {
			ret = 0;
			break;
		}

		retry++;
		if (retry > retry_max) {
			pr_err("NVT-ts: nvt_check_fw_reset_state error, retry=%d, buf[1]=0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
			       retry, buf[1], buf[2], buf[3], buf[4], buf[5]);
			ret = -1;
			break;
		}

		usleep_range(10000, 10000);
	}

	return ret;
}

int nvt_change_mode(u8 mode)
{
	u8 buf[2] = { 0x50, mode };
	int ret;

	/* D110: stock nvt_change_mode (0x8ade3e8) sets the page first:
	 * w0 = *mmap[0] | 0x50 (0x8ade428 orr), bl nvt_set_page
	 * (0x8ade42c), THEN writes {0x50, mode} (0x8ade448). Session-39
	 * check started past the function head and missed it. usleep
	 * (20000, 20000) confirmed by 0x4e20 @0x8ade450. */
	nvt_set_page(NVT_MM->EVENT_BUF_ADDR | 0x50);

	ret = CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
	if (ret)
		return ret;
	if (mode == 0) {
		usleep_range(20000, 20000);
		buf[0] = 0x51;
		buf[1] = 0xbb;
		ret = CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
		usleep_range(20000, 20000);
	}
	return ret;
}

/* D62: stock nvt_cmd_store @0xffffff8008adef78 - 2-byte command frame
 * {0x50, value}. Stock semantics (decompile): set_page(mmap[0] | 0x50)
 * once, then up to 5 attempts of CTP_SPI_WRITE + msleep(20) +
 * CTP_SPI_READ(2B) with buf[1] prefilled 0xff; completion is
 * buf[1] == 0x00 (0xff means the FW has not acked yet). The previous
 * build sent an old-tree 3-byte frame {0x50, 0x66 + value, value},
 * skipped set_page and inverted the completion test, so every write was
 * repeated 5x, always returned -EIO, and could land on the wrong page.
 */
int nvt_cmd_store(u8 value)
{
	u8 buf[2];
	int ret = -1;
	int attempt;

	/* D113 (F8, review REVIEW-VERDICT-F7-F10.md): stock nvt_cmd_store
	 * (0x8adef78) uses mutex_lock_interruptible (ts+0xf8, returns
	 * -ERESTARTSYS on signal) and gates on global bTouchIsAwake
	 * (0xa4e5530):
	 * ==0 -> pr_err "Touch is already suspend, ignore 0x%02x cmd"
	 * (995) + return -1. Ours used mutex_lock() and had no gate. */
	if (mutex_lock_interruptible(&nvt_ts->lock))
		return -ERESTARTSYS;
	/* Stock cmd_store 0x8adeff4 gates on global bTouchIsAwake. */
	if (!bTouchIsAwake) {
		pr_err("NVT-ts: nvt_cmd_store: Touch is already suspend, ignore 0x%02x cmd\n",
		       value);
		mutex_unlock(&nvt_ts->lock);
		return -1;
	}
	if (nvt_set_page(nvt_ts->mmap[0] | 0x50)) {
		pr_err("NVT-ts: nvt_cmd_store: set_page error\n");
		mutex_unlock(&nvt_ts->lock);
		return -1;
	}
	for (attempt = 0; attempt < 5; attempt++) {
		buf[0] = 0x50;
		buf[1] = value;
		CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
		msleep(20);
		buf[0] = 0x50;
		buf[1] = 0xff;
		CTP_SPI_READ(nvt_ts->client, buf, sizeof(buf));
		if (buf[1] == 0x00) {
			ret = 0;
			break;
		}
	}
	pr_info("NVT-ts: nvt_cmd_store: value=0x%02X, retry=%d\n", value, attempt);
	mutex_unlock(&nvt_ts->lock);
	return ret;
}

/* D62: stock nvt_ext_cmd_store @0xffffff8008adf250 - 3-byte frame
 * {0x50, mode, value}, set_page(mmap[0] | 0x50) once, 5 attempts,
 * completion buf[1] == 0x00. Same old-tree divergences fixed as above.
 */
int nvt_ext_cmd_store(u8 mode, u8 value)
{
	u8 buf[3];
	int ret = -1;
	int attempt;

	/* D113 (F8): ext_cmd_store mirrors cmd_store (stock sibling). */
	if (mutex_lock_interruptible(&nvt_ts->lock))
		return -ERESTARTSYS;
	/* Same global-awake gate as cmd_store (stock 0x8adf2d4). */
	if (!bTouchIsAwake) {
		pr_err("NVT-ts: nvt_ext_cmd_store: Touch is already suspend, ignore cmd\n");
		mutex_unlock(&nvt_ts->lock);
		return -1;
	}
	if (nvt_set_page(nvt_ts->mmap[0] | 0x50)) {
		pr_err("NVT-ts: nvt_ext_cmd_store: set_page error\n");
		mutex_unlock(&nvt_ts->lock);
		return -1;
	}
	for (attempt = 0; attempt < 5; attempt++) {
		buf[0] = 0x50;
		buf[1] = mode;
		buf[2] = value;
		CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
		msleep(20);
		buf[0] = 0x50;
		buf[1] = 0xff;
		CTP_SPI_READ(nvt_ts->client, buf, 2);
		if (buf[1] == 0x00) {
			ret = 0;
			break;
		}
	}
	pr_info("NVT-ts: nvt_ext_cmd_store: mode=0x%02X, value=0x%02X, retry=%d\n",
		mode, value, attempt);
	mutex_unlock(&nvt_ts->lock);
	return ret;
}

/* D62: stock nvt_edge_grid_zone_set @0xffffff8008adf580 - same frame,
 * but set_page(mmap[0] | 0x50) once, 3 attempts, and the 9-byte
 * completion read with buf[1..8] prefilled 0xff, success buf[1] == 0x00.
 */
int nvt_edge_grid_zone_set(u8 x1, u8 y1, u16 x2, u16 y2)
{
	u8 buf[9] = { 0x50, 0x7f, 0x01, x1, y1, x2 & 0xff, x2 >> 8, y2 & 0xff, y2 >> 8 };
	int ret = -1;
	int attempt;

	if (mutex_lock_interruptible(&nvt_ts->lock))
		return -ERESTARTSYS;
	if (!bTouchIsAwake) {
		pr_err("3[%s] %s %d: Touch is already suspend, ignore cmd\n",
		       "nvt_edge_grid_zone_set", "nvt_edge_grid_zone_set", 1196);
		mutex_unlock(&nvt_ts->lock);
		return -1;
	}
	if (nvt_set_page(nvt_ts->mmap[0] | 0x50)) {
		pr_err("NVT-ts: nvt_edge_grid_zone_set: set_page error\n");
		mutex_unlock(&nvt_ts->lock);
		return -1;
	}
	for (attempt = 0; attempt < 3; attempt++) {
		CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
		msleep(20);
		buf[0] = 0x50;
		memset(&buf[1], 0xff, sizeof(buf) - 1);
		CTP_SPI_READ(nvt_ts->client, buf, sizeof(buf));
		if (buf[1] == 0x00) {
			ret = 0;
			break;
		}
	}
	if (ret)
		pr_err("NVT-ts: nvt_edge_grid_zone_set: not ready, retry=%d\n", attempt);
	else
		pr_info("NVT-ts: nvt_edge_grid_zone_set: x1=%d, y1=%d, x2=%d, y2=%d\n",
			x1, y1, x2, y2);
	mutex_unlock(&nvt_ts->lock);
	return ret;
}

/* D62: stock nvt_edge_reject_set @0xffffff8008adf548 - value > 3 -> 0,
 * else the jump-table byte 0xba/0xbb/0xbc/0xbd through the 2-byte
 * nvt_cmd_store frame. */
int nvt_edge_reject_set(u8 value)
{
	if ((value & 0xff) >= 4)
		return 0;
	return nvt_cmd_store((0xbcbdbbba >> (value * 8)) & 0xff);
}

int nvt_game_mode_set(u8 value)
{
	if ((value & 0xff) >= 3)
		return 0;
	return nvt_ext_cmd_store(0x79, (0x00010200 >> (value * 8)) & 0xff);
}

int nvt_set_charger(u8 charger)
{
	u8 buf[2];
	int ret;

	pr_debug("NVT-ts: nvt_set_charger: charger=%d\n", charger);
	msleep(20);
	mutex_lock(&nvt_ts->lock);
	nvt_set_page(nvt_ts->mmap[0] | 0x50);
	/* D114 (F7, review REVIEW-VERDICT-F7-F10.md): stock nvt_set_charger
	 * (0x8adfcb8) accepts ONLY charger==1 (strh 0x5350 = {0x50,0x53});
	 * charger==0 and anything else fall to pr_info + -EINVAL. The
	 * charger==0 -> {0x50,0x51} branch was our own extension. */
	if (charger == 1) {
		buf[0] = 0x50;
		buf[1] = 0x53;
	} else {
		mutex_unlock(&nvt_ts->lock);
		return -EINVAL;
	}
	ret = CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
	mutex_unlock(&nvt_ts->lock);
	return ret;
}

void nvt_charger_notify_work(struct work_struct *work)
{
	if (!work) {
		pr_err("NVT-ts: parameter work are null!\n");
		return;
	}
	pr_info("NVT-ts: enter nvt_charger_notify_work\n");
	if (nvt_ts->charger_present == 0) {
		pr_info("NVT-ts: USB plug out\n");
		nvt_set_charger(0);
	} else if (nvt_ts->charger_present == 1) {
		pr_info("NVT-ts: USB plug in\n");
		nvt_set_charger(1);
	} else {
		pr_info("NVT-ts: Charger flag:%d not currently required!\n",
			nvt_ts->charger_present);
	}
}

int nvt_charger_notifier_callback(struct notifier_block *nb, unsigned long event, void *v);

void nvt_charger_init(void)
{
	union power_supply_propval val;
	struct power_supply *psy;
	int ret;

	/* D103 (F6 final): review side was right, D102 was wrong. Stock
	 * nvt_lenovo_register 0x8ad9900-0x8ad9928 is
	 * alloc_workqueue("%s", 0xE000A, 1, "nvt_charger_wq"):
	 *   mov w1,#0xa (0x8ad9900, encoding 0x52800141, imm16=0xa)
	 *   movk w1,#0xe,lsl #16 (0x8ad990c)
	 * 0xE000A = WQ_UNBOUND(0x2) | WQ_MEM_RECLAIM(0x8) | __WQ_ORDERED
	 * (1<<17) | __WQ_LEGACY(1<<18) | __WQ_ORDERED_EXPLICIT(1<<19)
	 * = exactly create_singlethread_workqueue() in this tree. D102
	 * misread the movz imm16 as #0x8 (that encoding belongs to
	 * nvt_hall_check_wq, 0x8adaf70 movk #0x4) and dropped
	 * WQ_UNBOUND. Roll back to the macro (review option B). */
	nvt_ts->charger_wq = create_singlethread_workqueue("nvt_charger_wq");
	if (!nvt_ts->charger_wq) {
		pr_err("NVT-ts: allocate nvt_charger_notify_wq failed\n");
		return;
	}
	INIT_WORK(&nvt_ts->charger_work, nvt_charger_notify_work);
	nvt_ts->psy_nb.notifier_call = nvt_charger_notifier_callback;
	ret = power_supply_reg_notifier(&nvt_ts->psy_nb);
	if (ret) {
		pr_err("NVT-ts: Unable to register charger_notifier: %d\n", ret);
		return;
	}
	psy = power_supply_get_by_name("usb");
	if (!psy) {
		pr_err("NVT-ts: Couldn't get usbpsy\n");
		return;
	}
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT, &val);
	if (ret < 0) {
		pr_err("NVT-ts: Couldn't get POWER_SUPPLY_PROP_PRESENT rc=%d\n",
		       ret);
		return;
	}
	nvt_ts->charger_present = val.intval;
	pr_info("NVT-ts: boot check usb_plug_status = %d\n",
		nvt_ts->charger_present);
}

int nvt_charger_notifier_callback(struct notifier_block *nb,
				  unsigned long event, void *v)
{
	struct power_supply *psy = v;
	union power_supply_propval val;
	int ret;

	/* Stock nvt_charger_notifier_callback @0x8ad9b48 gates on the
	 * firmware-update flag, POWER_SUPPLY_PROP_STATUS (0), a live work
	 * queue, and bTouchIsAwake before reading or queuing charger work. */
	if (!nvt_ts || nvt_ts->fw_update_stat != 1)
		return NOTIFY_DONE;
	if (!psy) {
		pr_err("NVT-ts: Couldn't get usbpsy\n");
		return -EINVAL;
	}
	if (strcmp(psy->desc->name, "usb") != 0)
		return NOTIFY_DONE;
	if (event != POWER_SUPPLY_PROP_STATUS)
		return NOTIFY_DONE;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT, &val);
	if (ret < 0) {
		pr_err("NVT-ts: Couldn't get POWER_SUPPLY_PROP_PRESENT rc=%d\n", ret);
		return -EINVAL;
	}

	if (val.intval != nvt_ts->charger_present) {
		pr_info("NVT-ts: usb_plug_status = %d\n", val.intval);
		nvt_ts->charger_present = val.intval;
		if (bTouchIsAwake && nvt_ts->charger_wq)
			queue_work_on(8, nvt_ts->charger_wq,
				      &nvt_ts->charger_work);
	}
	return NOTIFY_DONE;
}
int nvt_support_pen_set(u8 enable, u8 version)
{
	u8 buf[4];
	int retry;

	if (mutex_lock_interruptible(&nvt_ts->lock))
		return -ERESTARTSYS;
	if (!bTouchIsAwake) {
		pr_err("3[%s] %s %d: Touch is already suspend, ignore cmd\n",
		       "nvt_support_pen_set", "nvt_support_pen_set", 1413);
		mutex_unlock(&nvt_ts->lock);
		return -1;
	}
	/* TASK-081 D93 (pen regression part 2): stock nvt_support_pen_set
	 * (0x8adf9e8) writes a 4-byte frame {0x50, 0x7b, enable, version}
	 * after nvt_set_page(mmap[0]) - NOT the old community protocol
	 * {0x7b, 0x50, ...} with page mmap[0]|0x50 that our rebuild copied.
	 * With the old frame the IC never executed the pen enable, which is
	 * why the tip stayed dead even with pen_support fixed. Read back is
	 * 4 bytes of {0x50, 0xff, 0xff, 0xff} requiring byte[1] == 0, five
	 * retries. */
	for (retry = 0; retry < 5; retry++) {
		nvt_set_page(nvt_ts->mmap[0]);
		buf[0] = 0x50;
		buf[1] = 0x7b;
		buf[2] = enable;
		buf[3] = version;
		CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
		msleep(20);
		buf[0] = 0x50;
		buf[1] = 0xff;
		buf[2] = 0xff;
		buf[3] = 0xff;
		if (!CTP_SPI_READ(nvt_ts->client, buf, 4) && buf[1] == 0) {
			mutex_unlock(&nvt_ts->lock);
			return 0;
		}
	}
	pr_err("NVT-ts: nvt_support_pen_set: failed enable=%d version=%d\n",
	       enable, version);
	mutex_unlock(&nvt_ts->lock);
	return -EIO;
}

void nvt_support_pen_set_nfc(bool normal)
{
	if (normal == true) {
		nvt_ts->pen_suppress = 0;
		if (nvt_ts->pen_enable == 1)
			nvt_support_pen_set(1, nvt_ts->pen_version);
		else
			pr_info("3[%s] %s %d: The pen no connect!\n",
			       "nvt_support_pen_set_nfc",
			       "nvt_support_pen_set_nfc", 1387);
	} else {
		nvt_support_pen_set(0, 0);
		nvt_ts->pen_suppress = 1;
	}
}

/* stock nvt_set_pen_inband_mode_1 @0xffffff8008ade4b8 (TASK-081) */
int nvt_set_pen_inband_mode_1(u8 enable, u8 version)
{
	u8 buf[5];
	int retry;

	nvt_set_page(nvt_ts->mmap[0] | 0x50);
	buf[0] = 0x50;
	buf[1] = 0xc1;
	buf[2] = 0x02;
	buf[3] = enable;
	buf[4] = version;
	CTP_SPI_WRITE(nvt_ts->client, buf, 5);
	for (retry = 0; retry < 5; retry++) {
		buf[0] = 0x50;
		buf[1] = 0xff;
		CTP_SPI_READ(nvt_ts->client, buf, 2);
		if (buf[1] == 0)
			return 0;
		usleep_range(10000, 10000);
	}
	pr_err("3[%s] %s %d: failed, i=%d, buf[1]=0x%02X\n",
	       "nvt_set_pen_inband_mode_1", "nvt_set_pen_inband_mode_1",
	       155, 5, buf[1]);
	return -1;
}

/* stock nvt_set_pen_normal_mode @0xffffff8008ade670 (TASK-081) */
int nvt_set_pen_normal_mode(void)
{
	u8 buf[3];
	int retry;

	nvt_set_page(nvt_ts->mmap[0] | 0x50);
	buf[0] = 0x50;
	buf[1] = 0xc1;
	buf[2] = 0x04;
	CTP_SPI_WRITE(nvt_ts->client, buf, 3);
	for (retry = 0; retry < 5; retry++) {
		buf[0] = 0x50;
		buf[1] = 0xff;
		CTP_SPI_READ(nvt_ts->client, buf, 2);
		if (buf[1] == 0)
			return 0;
		usleep_range(10000, 10000);
	}
	pr_err("3[%s] %s %d: failed, i=%d, buf[1]=0x%02X\n",
	       "nvt_set_pen_normal_mode", "nvt_set_pen_normal_mode",
	       189, 5, buf[1]);
	return -1;
}

int nvt_get_fw_pipe(void)
{
	u8 buf[2];

	nvt_set_page(nvt_ts->mmap[0] | 0x51);
	buf[0] = 0x51;
	CTP_SPI_READ(nvt_ts->client, buf, sizeof(buf));
	return buf[1] & 1;
}

/* ---- nvt_read_mdata / nvt_read_get_num_mdata
 * (stock @0xffffff8008ade898 / @0xffffff8008adea98, TASK-081) ----
 * Both helpers fill the shared raw byte buffer with 256-byte SPI page
 * reads (buf[0]=0, set_page over page bases starting at addr & ~0xff),
 * re-read the trailing partial page, then convert the wire
 * little-endian int16 stream into int32 words.  nvt_read_mdata targets
 * the x_num*y_num grid in nvt_mdata; nvt_read_get_num_mdata writes
 * `count` words into `buf`.
 */
static u8 nvt_mdata_raw[0x2800];

void nvt_read_mdata(u32 addr)
{
	u8 buf[0x101];
	u32 x_num = nvt_ts->x_num;
	u32 y_num = nvt_ts->y_num;
	u32 count = x_num * y_num;
	u32 total = count * 2;
	u32 addr_low = addr & 0xff;
	u32 full = (total + addr_low) >> 8;
	u32 rem = (total + addr_low) & 0xff;
	u32 page = addr - addr_low;
	u32 off = 0;
	u32 i;

	memset(buf, 0, sizeof(buf));
	for (i = 0; i < full; i++) {
		nvt_set_page(page);
		buf[0] = 0;
		CTP_SPI_READ(nvt_ts->client, buf, 0x101);
		memcpy(nvt_mdata_raw + off, buf + 1, 0x100);
		page += 0x100;
		off += 0x100;
	}
	if (rem) {
		nvt_set_page((total + addr) & ~0xff);
		buf[0] = 0;
		CTP_SPI_READ(nvt_ts->client, buf, 0x101);
		memcpy(nvt_mdata_raw + ((total + addr_low) & ~0xff),
		       buf + 1, 0x100);
	}
	for (i = 0; i < count; i++)
		nvt_mdata[i] = (s16)((nvt_mdata_raw[addr_low + 2 * i + 1] << 8) |
				     nvt_mdata_raw[addr_low + 2 * i]);
	nvt_set_page(nvt_ts->mmap[0]);
}

void nvt_get_mdata(int32_t *buf, u8 *x_num, u8 *y_num)
{
	*x_num = nvt_ts->x_num;
	*y_num = nvt_ts->y_num;
	/* D90b: clamp to the array capacity - the grid can exceed 32x32. */
	memcpy(buf, nvt_mdata,
	       min_t(size_t, (size_t)nvt_ts->x_num * nvt_ts->y_num,
		     ARRAY_SIZE(nvt_mdata)) * sizeof(int32_t));
}

void nvt_read_get_num_mdata(u32 addr, int32_t *buf, u32 count)
{
	u8 rbuf[0x101];
	u32 total = count * 2;
	u32 addr_low = addr & 0xff;
	u32 full = (total + addr_low) >> 8;
	u32 rem = (total + addr_low) & 0xff;
	u32 page = addr - addr_low;
	u32 off = 0;
	u32 i;

	memset(rbuf, 0, sizeof(rbuf));
	for (i = 0; i < full; i++) {
		nvt_set_page(page);
		rbuf[0] = 0;
		CTP_SPI_READ(nvt_ts->client, rbuf, 0x101);
		memcpy(nvt_mdata_raw + off, rbuf + 1, 0x100);
		page += 0x100;
		off += 0x100;
	}
	if (rem) {
		nvt_set_page((total + addr) & ~0xff);
		rbuf[0] = 0;
		CTP_SPI_READ(nvt_ts->client, rbuf, 0x101);
		memcpy(nvt_mdata_raw + ((total + addr_low) & ~0xff),
		       rbuf + 1, 0x100);
	}
	for (i = 0; i < count; i++)
		buf[i] = (s16)((nvt_mdata_raw[addr_low + 2 * i + 1] << 8) |
			       nvt_mdata_raw[addr_low + 2 * i]);
	nvt_set_page(nvt_ts->mmap[0]);
}

/* ---- nvt_read_fw_history ---- */
void nvt_read_fw_history(u32 addr)
{
	u8 buf[0x42];
	int i;

	if (!addr)
		return;

	nvt_set_page(addr);
	buf[0] = addr & 0x7f;
	CTP_SPI_READ(nvt_ts->client, buf, 0x41);
	for (i = 0; i < 4; i++)
		pr_info("NVT-ts: nvt_read_fw_history: 0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
			buf[1 + i * 0x10], buf[2 + i * 0x10], buf[3 + i * 0x10],
			buf[4 + i * 0x10], buf[5 + i * 0x10], buf[6 + i * 0x10],
			buf[7 + i * 0x10], buf[8 + i * 0x10], buf[9 + i * 0x10],
			buf[0xa + i * 0x10], buf[0xb + i * 0x10], buf[0xc + i * 0x10],
			buf[0xd + i * 0x10], buf[0xe + i * 0x10], buf[0xf + i * 0x10],
			buf[0x10 + i * 0x10]);
	nvt_set_page(nvt_ts->mmap[0]);
}

/* ---- nvt_lenovo_register / nvt_lenovo_report ---- */
static u8 nvt_lenovo_buf[0x20][0x28];
static u8 nvt_lenovo_out[0x288];
static u8 nvt_lenovo_count;
static u8 nvt_lenovo_seq;
static u8 nvt_lenovo_index;
static u32 nvt_lenovo_major;
static dev_t nvt_lenovo_dev;
static struct cdev nvt_lenovo_cdev;
static struct class *nvt_lenovo_class;

static long penraw_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -EFAULT;

	if (cmd == _IOC(_IOC_READ, 0x85, 0, 0x288)) {
				memset(nvt_lenovo_out, 0, 0x288);
		nvt_lenovo_out[0] = nvt_lenovo_count;
		if (nvt_lenovo_count) {
			/* TASK-081 D98: stock penraw_ioctl (0x8ad9cb0) start-slot
			 * logic: slot=(index+16)&0x1f, but count<16 reads from 0.
			 * The rebuild also had an orphan pagefault_enable() with
			 * no matching disable - preempt-count imbalance on every
			 * HAL ioctl. */
			u8 slot = (nvt_lenovo_index + 0x10) & 0x1f;
			u32 i;

			if (nvt_lenovo_count < 0x10)
				slot = 0;

			for (i = 0; i < nvt_lenovo_count; i++) {
				memcpy(nvt_lenovo_out + 8 + i * 0x28,
				       &nvt_lenovo_buf[slot][0], 0x28);
				slot = (slot == 0x1f) ? 0 : slot + 1;
			}
		}

		ret = -EFAULT;
		if (access_ok(VERIFY_WRITE, (void __user *)arg, 0x288))
			ret = copy_to_user((void __user *)arg, nvt_lenovo_out,
					   0x288) ? -EFAULT : 0;
	} else {
		pr_err("NVT-ts: unsupported command %d\n", cmd);
	}
	return ret;
}

static int penraw_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int penraw_close(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations nvt_lenovo_fops = {
	.unlocked_ioctl = penraw_ioctl,
	.open = penraw_open,
	.release = penraw_close,
};

void nvt_lenovo_report(enum pen_status status, u32 x, u32 y, u8 tilt_x, u8 tilt_y, u32 pressure)
{
	u8 *entry;

	(void)status;
	entry = nvt_lenovo_buf[nvt_lenovo_index];
	memset(entry, 0, 0x28);
	entry[0] = nvt_lenovo_seq;
	/* TASK-081 D97 (external AI review): stock ring entry layout
	 * (0x8ad9638 L265-273): seq@0, u16=1@8, x/10@0x10(u64),
	 * y/10@0x18(u64), pressure@0x20(u64), 0@0x1f. D97 originally
	 * wrote "tilt_x@4, tilt_y@5" here - that was a base-arithmetic
	 * error; the real tilt offsets are 0x0a/0x0b, see D99 below.
	 * The rebuild had a made-up layout (u16=1@2, tilt@6) that
	 * misaligns every field the HAL reads through /dev/lenovo_penraw. */
	/* D99: tilt is at +0x0a/+0x0b in the stock ring entry (0x8adbd78
	 * inline: strb w23,[x24,#0xa] / strb w22,[x24,#0xb]; the D97
	 * offsets 0x04/0x05 were a base-arithmetic error: 0x980-0x976=0x0a). */
	entry[0x0a] = tilt_x;
	entry[0x0b] = tilt_y;
	*(u16 *)&entry[8] = 1;
	*(u64 *)&entry[0x10] = x / 10;
	*(u64 *)&entry[0x18] = y / 10;
	*(u64 *)&entry[0x20] = pressure;
	nvt_lenovo_seq++;
	if (nvt_lenovo_count < 0x10)
		nvt_lenovo_count++;
	if (nvt_lenovo_index == 0x1f)
		nvt_lenovo_index = 0;
	else
		nvt_lenovo_index++;
}

int nvt_lenovo_register(void)
{
	int ret;

	nvt_lenovo_count = 0;
	nvt_lenovo_seq = 0;
	nvt_lenovo_index = 0;
	ret = alloc_chrdev_region(&nvt_lenovo_dev, 0, 1,
				  "lenovo_penraw_driver");
	if (ret) {
		pr_err("NVT-ts: failed to alloc_chrdev_region()\n");
		return -EINVAL;
	}
	nvt_lenovo_major = MAJOR(nvt_lenovo_dev);
	cdev_init(&nvt_lenovo_cdev, &nvt_lenovo_fops);
	nvt_lenovo_cdev.owner = NULL;
	ret = cdev_add(&nvt_lenovo_cdev, nvt_lenovo_dev, 1);
	if (ret) {
		pr_err("NVT-ts: failed to cdev_add()\n");
		unregister_chrdev_region(nvt_lenovo_dev, 1);
		return -EINVAL;
	}
	nvt_lenovo_class = class_create(THIS_MODULE, "lenovo_penraw");
	if (IS_ERR(nvt_lenovo_class)) {
		pr_err("NVT-ts: class_create()\n");
		cdev_del(&nvt_lenovo_cdev);
		unregister_chrdev_region(nvt_lenovo_dev, 1);
		return -EINVAL;
	}
	device_create(nvt_lenovo_class, NULL, nvt_lenovo_dev, NULL,
		      "lenovo_penraw");
	return 0;
}

void nvt_lenovo_unregister(void)
{
	if (nvt_lenovo_class) {
		device_destroy(nvt_lenovo_class, nvt_lenovo_dev);
		class_destroy(nvt_lenovo_class);
		nvt_lenovo_class = NULL;
	}
	cdev_del(&nvt_lenovo_cdev);
	unregister_chrdev_region(nvt_lenovo_dev, 1);
}

/* ---- nvt_check_stylus_state ---- */
int nvt_check_stylus_state(u8 id, u8 *buf)
{
	if (id == 0x1e && buf[2] == 5) {
		if (buf[3] == 0) {
			nvt_ts->restore_pending = 0;
			pr_info("NVT-ts: nvt_check_stylus_state: stylus release\n");
			return 1;
		}
		if (buf[3] == 1) {
			input_report_key(nvt_ts->input_dev, 0x25e, 1);
			input_sync(nvt_ts->input_dev);
			input_report_key(nvt_ts->input_dev, 0x25e, 0);
			input_sync(nvt_ts->input_dev);
			pr_info("NVT-ts: nvt_check_stylus_state: stylus press\n");
			return 1;
		}
		if (buf[3] == 2) {
			nvt_ts->restore_pending = 1;
			pr_info("NVT-ts: nvt_check_stylus_state: stylus pending\n");
			return 1;
		}
		pr_err("NVT-ts: nvt_check_stylus_state: stylus state error\n");
		return -1;
	}
	return 0;
}

/* ---- nvt_ts_wakeup_gesture_report ---- */
static void nvt_gesture_report_power(void)
{
	/* D67/p146 (old-tree proven fix): ZUI input policy (mTpWakeUp=false)
	 * drops wake attempts from the touch panel device; report on the
	 * dedicated wake keyboard device like a power button. */
	input_report_key(nvt_ts->wake_input_dev, KEY_POWER, 1);
	input_sync(nvt_ts->wake_input_dev);
	input_report_key(nvt_ts->wake_input_dev, KEY_POWER, 0);
	input_sync(nvt_ts->wake_input_dev);
}

void nvt_ts_wakeup_gesture_report(u8 id, u8 *buf)
{
	u8 idx;

	/* D65: stock @0x8ad8ed8 - when id==0x1e && buf[2]==1 the EFFECTIVE
	 * gesture id is buf[3], not id (stock: mov w8, buf[3] before the
	 * (id-12)<=12 jump table). The old-tree form used id itself, so a
	 * 0x1e sub-frame computed idx=0x12 (18) and was dropped as invalid
	 * - double-tap wake could never fire. */
	if (id == 0x1e && buf[2] == 1)
		id = buf[3];
	if (id >= 0x1f)
		goto invalid;
	pr_info("NVT-ts: nvt_ts_wakeup_gesture_report: gesture id=0x%02x\n", id);
	idx = id - 12;
	if (idx < 13) {
		nvt_gesture_report_power();
		return;
	}
invalid:
	pr_err("NVT-ts: nvt_ts_wakeup_gesture_report: invalid gesture\n");
}

/* ---- nvt_gesture_switch ---- */
int nvt_gesture_switch(struct input_dev *dev, unsigned int type,
		       unsigned int code, int value)
{
	pr_info("NVT-ts: nvt_gesture_switch: %s:start\n", "nvt_gesture_switch");
	if (type == 0 && code == 1) {
		if (value == 1) {
			nvt_gesture_flag = 1;
			pr_info("NVT-ts: nvt_gesture_switch: gesture enabled:%d\n", 1);
		} else if (value == 0) {
			nvt_gesture_flag = 0;
			pr_info("NVT-ts: nvt_gesture_switch: gesture disabled:%d\n", 0);
		}
	}
	return 0;
}

/* ---- nvt_hall_check_func ---- */
static struct delayed_work nvt_hall_check_work;
static struct workqueue_struct *nvt_hall_check_wq;

static void nvt_hall_check_func(struct work_struct *work)
{
	u32 elapsed;
	int hall;

	elapsed = jiffies_to_msecs(jiffies - nvt_hall_timer0);
	hall = gpiod_get_raw_value(gpio_to_desc(1275));

	if (elapsed > 1000 && !bTouchIsAwake && hall != ((~nvt_hall_status) & 1)) {
		if (!nvt_ts->hw_sleep_check) {
			if (nvt_hall_pm_status == 1) {
				pr_info("NVT-ts: nvt_hall_check_func: The status of double click has changed\n");
				nvt_hall_status = 1;
			} else if (hall == 0) {
				/* D66: stock 0x8adc228 stores halfword 0x1150 ->
				 * frame {0x50, 0x11} (cmd 0x50, deep sleep).
				 * The previous {0x11, 0x50} wrote 0x50 to
				 * register 0x11 instead. */
				u8 buf[2] = { 0x50, 0x11 };

				pr_info("NVT-ts: nvt_hall_check_func: The keyboard close!\n");
				CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
				nvt_hall_status = 1;
				nvt_hall_timer0 = jiffies;
			} else {
				pr_info("NVT-ts: nvt_hall_check_func: The keyboard open!\n");
				nvt_hall_status = 0;
			}
		} else {
			nvt_hall_pm = 1;
			pr_info("NVT-ts: nvt_hall_check_func: The status of pm is suspend\n");
		}
	}
	queue_delayed_work_on(8, nvt_hall_check_wq,
			      &nvt_hall_check_work,
			      msecs_to_jiffies(250));
}

/* ---- nvt_restore_cmd_func ---- */
static struct work_struct nvt_restore_cmd_work;
struct proc_dir_entry *NVT_proc_entry;
/* D55: the focaltech driver keeps its own `static` active_panel, so the
 * extern below must bind to the synaptics global definition (the only
 * non-static one in the build). Keep the same symbol name. */
/* ---- nvt_flash_read / open / close / /proc/NVTSPI fops
 * (stock @0xffffff8008adc9a0 / @0x8adcda0 / @0x8adce18, TASK-081) ----
 * User frame: buf[0] = addr[14:8] | (0x80 = read), buf[1] = addr[7:0],
 * buf[2..] = payload; the SPI transfer length equals the 15-bit address
 * value.  Read path copies the IC data back over buf[2..] and returns
 * the whole frame; write path sends the payload as-is.
 */
static int nvt_flash_open(struct inode *inode, struct file *file)
{
	file->private_data = kzalloc(8, GFP_KERNEL);
	if (!file->private_data) {
		pr_err("3[%s] %s %d: Failed to allocate memory for nvt flash data\n",
		       "nvt_flash_open", "nvt_flash_open", 1005);
		return -ENOMEM;
	}
	return 0;
}

static int nvt_flash_close(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static ssize_t nvt_flash_read(struct file *file, char __user *buff,
			      size_t count, loff_t *pos)
{
	int ret = 0;
	u8 *buf;
	u8 *rbuf;
	u32 addr;
	int retry;
	bool read_ok;

	if (count - 3 > 0xfc00) {
		pr_err("3[%s] %s %d: invalid transfer len!\n",
		       "nvt_flash_read", "nvt_flash_read", 899);
		return -EFAULT;
	}

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf) {
		pr_err("3[%s] %s %d: kzalloc for buf failed!\n",
		       "nvt_flash_read", "nvt_flash_read", 906);
		return -ENOMEM;
	}

	rbuf = kmalloc(count, GFP_KERNEL | __GFP_ZERO);
	if (!rbuf) {
		pr_err("3[%s] %s %d: kzalloc for buf failed!\n",
		       "nvt_flash_read", "nvt_flash_read", 913);
		kfree(buf);
		return -ENOMEM;
	}

	if (copy_from_user(buf, buff, count)) {
		pr_err("3[%s] %s %d: copy from user error\n",
		       "nvt_flash_read", "nvt_flash_read", 921);
		ret = -EFAULT;
		goto out;
	}

	addr = buf[1] | ((buf[0] & 0x7f) << 8);
	memcpy(rbuf, buf + 2, addr);

	if (buf[0] & 0x80) {
		read_ok = false;
		for (retry = 0; retry < 20; retry++) {
			ret = CTP_SPI_READ(nvt_ts->client, rbuf, addr);
			if (ret == 0) {
				read_ok = true;
				break;
			}
			pr_err("3[%s] %s %d: error, retries=%d, ret=%d\n",
			       "nvt_flash_read", "nvt_flash_read", 960,
			       retry, ret);
		}
		if (read_ok) {
			memcpy(buf + 2, rbuf, addr);
			if (copy_to_user(buff, buf, count))
				ret = -EFAULT;
		} else {
			pr_err("3[%s] %s %d: error, ret = %d\n",
			       "nvt_flash_read", "nvt_flash_read", 975, ret);
			ret = -EIO;
		}
	} else {
		for (retry = 0; retry < 20; retry++) {
			ret = CTP_SPI_WRITE(nvt_ts->client, rbuf, addr);
			if (ret == 0)
				break;
			pr_err("3[%s] %s %d: error, retries=%d, ret=%d\n",
			       "nvt_flash_read", "nvt_flash_read", 944,
			       retry, ret);
		}
		if (ret) {
			pr_err("3[%s] %s %d: error, ret = %d\n",
			       "nvt_flash_read", "nvt_flash_read", 950, ret);
			ret = -EIO;
		}
	}

out:
	kfree(buf);
	kfree(rbuf);
	return ret;
}

static const struct file_operations nvt_flash_fops = {
	.read = nvt_flash_read,
	.open = nvt_flash_open,
	.release = nvt_flash_close,
};
static void nvt_restore_cmd_func(struct work_struct *work)
{
	pr_info("NVT-ts: nvt_restore_cmd_func: %s++\n", "nvt_restore_cmd_func");
	nvt_edge_reject_set(nvt_ts->edge_reject);
	nvt_edge_grid_zone_set(nvt_ts->edge_grid_zone[0], nvt_ts->edge_grid_zone[1],
			       nvt_ts->edge_grid_zone[2], nvt_ts->edge_grid_zone[3]);
	nvt_game_mode_set(nvt_ts->game_mode);
	if (!nvt_ts->pen_suppress)
		nvt_support_pen_set(nvt_ts->pen_enable, nvt_ts->pen_version);
	else
		nvt_support_pen_set(0, 0);
	nvt_ts->restore_pending = 0;
	pr_info("NVT-ts: nvt_restore_cmd_func: %s--\n", "nvt_restore_cmd_func");
}

/* ---- nvt_drm_panel_notifier_callback ---- */
static int nvt_drm_panel_notifier_callback(struct notifier_block *nb,
					   unsigned long event, void *data)
{
	struct drm_panel_notifier *panel_data;
	struct gpio_desc *hall_desc;
	u32 i;
	int hall;
	int blank_val = -1;

	if (!data || (event != DRM_PANEL_EARLY_EVENT_BLANK &&
		      event != DRM_PANEL_EVENT_BLANK))
		return NOTIFY_DONE;
	panel_data = (struct drm_panel_notifier *)data;
	if (!panel_data->data)
		return NOTIFY_DONE;
	blank_val = *(int *)panel_data->data;
	pr_info("NVT-ts: nvt_drm_panel_notifier_callback: hid-keyboard,%s,event=%lu, blank=%d\n",
		"nvt_drm_panel_notifier_callback", event, blank_val);

	if (event == DRM_PANEL_EVENT_BLANK &&
	    blank_val == DRM_PANEL_BLANK_UNBLANK) {
		if (nvt_gesture_flag)
			cancel_delayed_work_sync(&nvt_hall_check_work);
		if (bTouchIsAwake) {
			pr_info("NVT-ts: nvt_drm_panel_notifier_callback: Touch is already resume\n");
			kb_hid_resume();
			return NOTIFY_OK;
		}
		mutex_lock(&nvt_ts->lock);
		gpiod_set_raw_value(gpio_to_desc(nvt_ts->reset_gpio_num), 1);
		if (nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME) == 0)
			nvt_check_fw_reset_state(NVT_FW_INIT_RESET_STATE);
		else
			pr_info("NVT-ts: nvt_ts_resume: download firmware failed, ignore check fw state\n");
		nvt_irq_enable(true);
		bTouchIsAwake = 1;
		mutex_unlock(&nvt_ts->lock);
		queue_work_on(8, nvt_fwu_wq, &nvt_restore_cmd_work);
		pr_info("NVT-ts: nvt_ts_resume: end\n");
		kb_hid_resume();
	} else if (event == DRM_PANEL_EARLY_EVENT_BLANK &&
		   blank_val == DRM_PANEL_BLANK_POWERDOWN) {
		pr_info("NVT-ts: nvt_ts_suspend: start\n");
		hall_desc = gpio_to_desc(1275);
		hall = gpiod_get_raw_value(hall_desc);
		if (!bTouchIsAwake) {
			pr_info("NVT-ts: nvt_ts_suspend: Touch is already suspend\n");
			kb_hid_suspend();
			return NOTIFY_OK;
		}
		if (!nvt_gesture_flag)
			nvt_irq_enable(false);
		mutex_lock(&nvt_ts->lock);
		bTouchIsAwake = 0;
		if (hall == 0 || !nvt_gesture_flag) {
			u8 buf[2] = { 0x50, 0x11 };

			CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
		} else {
			u8 buf[2] = { 0x50, 0x13 };

			CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
			irq_set_irq_wake(nvt_ts->irq, 1);
			pr_info("NVT-ts: nvt_ts_suspend: Enabled touch wakeup gesture\n");
			nvt_hall_pm = 0;
			nvt_hall_pm_status = 0;
			queue_delayed_work_on(8, nvt_hall_check_wq,
					      &nvt_hall_check_work,
					      msecs_to_jiffies(250));
		}
		mutex_unlock(&nvt_ts->lock);
		{
			for (i = 0; i < nvt_ts->max_finger_num; i++) {
				input_mt_slot(nvt_ts->input_dev, i);
				input_report_abs(nvt_ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
				input_report_abs(nvt_ts->input_dev, ABS_MT_PRESSURE, 0);
				input_mt_report_slot_state(nvt_ts->input_dev,
							   MT_TOOL_FINGER, false);
			}
		}
		input_report_key(nvt_ts->input_dev, BTN_TOUCH, 0);
		input_sync(nvt_ts->input_dev);
		if (nvt_ts->pen_support) {
			input_report_abs(nvt_ts->pen_dev, ABS_X, 0);
			input_report_abs(nvt_ts->pen_dev, ABS_Y, 0);
			input_report_abs(nvt_ts->pen_dev, ABS_PRESSURE, 0);
			input_report_abs(nvt_ts->pen_dev, ABS_TILT_X, 0);
			input_report_abs(nvt_ts->pen_dev, ABS_TILT_Y, 0);
			input_report_abs(nvt_ts->pen_dev, ABS_DISTANCE, 0);
			input_report_key(nvt_ts->pen_dev, BTN_TOUCH, 0);
			input_report_key(nvt_ts->pen_dev, BTN_TOOL_PEN, 0);
			input_report_key(nvt_ts->pen_dev, BTN_STYLUS, 0);
			input_report_key(nvt_ts->pen_dev, BTN_STYLUS2, 0);
			nvt_lenovo_report(TS_RELEASE, 0, 0, 0, 0, 0);
			input_sync(nvt_ts->pen_dev);
		}
		msleep(50);
		pr_info("NVT-ts: nvt_ts_suspend: end\n");
		kb_hid_suspend();
	}
	return NOTIFY_OK;
}

/* ---- nvt_ts_pm_suspend / nvt_ts_pm_resume ---- */
int nvt_ts_pm_suspend(struct device *dev)
{
	pr_info("NVT-ts: nvt_ts_pm_suspend: %s++\n", "nvt_ts_pm_suspend");
	nvt_ts->hw_sleep_check = 1;
	/* stock 0xffffff8008adce78: str wzr, [x8, #0x198] - clear done so the
	 * next resume complete() re-signals from a clean state. */
	reinit_completion(&nvt_ts->pm_completion);
	pr_info("NVT-ts: nvt_ts_pm_suspend: %s--\n", "nvt_ts_pm_suspend");
	return 0;
}

int nvt_ts_pm_resume(struct device *dev)
{
	int hall;

	pr_info("NVT-ts: nvt_ts_pm_resume: %s++\n", "nvt_ts_pm_resume");
	nvt_ts->hw_sleep_check = 0;
	/* stock 0xffffff8008adcec8-d0: complete(ts+0x198) right after clearing
	 * hw_sleep_check - wakes any PM waiter blocked since suspend re-armed. */
	complete(&nvt_ts->pm_completion);
	if (nvt_hall_pm == 1 && nvt_gesture_flag && nvt_hall_pm_status == 0) {
		/* W3 (stock 0x8adcefc): gpio_to_desc(1275) +
		 * gpiod_get_raw_value, with no gpio_direction_input. The frame
		 * is {0x50, 0x11} (halfword 0x1150 at 0x8adcf18). */
		hall = gpiod_get_raw_value(gpio_to_desc(1275));
		if (hall == 0) {
			u8 buf[2] = { 0x50, 0x11 };

			pr_info("NVT-ts: nvt_ts_pm_resume: The keyboard still close!\n");
			CTP_SPI_WRITE(nvt_ts->client, buf, sizeof(buf));
			nvt_hall_pm_status = 1;
		}
	}
	pr_info("NVT-ts: nvt_ts_pm_resume: %s--\n", "nvt_ts_pm_resume");
	return 0;
}

/* ---- nvt_write_firmware ---- */
/* ---- old-main-tree download chain (source-side reference) ---- */

static ktime_t fw_dl_start, fw_dl_end;
const struct firmware *fw_entry;
static size_t fw_need_write_size;
static uint8_t *fwbuf;

static uint32_t CheckSum(const u8 *data, size_t len)
{
	uint32_t i = 0;
	uint32_t checksum = 0;

	for (i = 0; i < len + 1; i++)
		checksum += data[i];

	checksum += len;
	checksum = ~checksum + 1;

	return checksum;
}

static uint32_t byte_to_word(const uint8_t *data)
{
	return data[0] + (data[1] << 8) + (data[2] << 16) + (data[3] << 24);
}

static int32_t nvt_get_fw_need_write_size(const struct firmware *fw)
{
	int32_t i = 0;
	int32_t total_sectors_to_check = 0;

	total_sectors_to_check = fw->size / FLASH_SECTOR_SIZE;

	for (i = total_sectors_to_check; i > 0; i--) {
		if (strncmp(&fw->data[i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN],
			    "NVT", NVT_FLASH_END_FLAG_LEN) == 0) {
			fw_need_write_size = i * FLASH_SECTOR_SIZE;
			pr_info("NVT-ts: fw_need_write_size = %zu(0x%zx), NVT end flag\n",
				fw_need_write_size, fw_need_write_size);
			return 0;
		}

		if (strncmp(&fw->data[i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN],
			    "MOD", NVT_FLASH_END_FLAG_LEN) == 0) {
			fw_need_write_size = i * FLASH_SECTOR_SIZE;
			pr_info("NVT-ts: fw_need_write_size = %zu(0x%zx), MOD end flag\n",
				fw_need_write_size, fw_need_write_size);
			return 0;
		}
	}

	pr_err("NVT-ts: end flag \"NVT\" \"MOD\" not found!\n");
	return -1;
}

static int32_t nvt_download_init(void)
{
	if (fwbuf == NULL) {
		fwbuf = kzalloc(NVT_TRANSFER_LEN + 1 + DUMMY_BYTES, GFP_KERNEL);
		if (fwbuf == NULL) {
			pr_err("NVT-ts: kzalloc for fwbuf failed!\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static uint32_t partition;
static uint8_t ilm_dlm_num = 2;
static uint8_t cascade_2nd_header_info;
static struct nvt_ts_bin_map *bin_map;

static int32_t nvt_bin_header_parser(const u8 *fwdata, size_t fwsize)
{
	uint32_t list = 0;
	uint32_t pos = 0x00;
	uint32_t end = 0x00;
	uint8_t info_sec_num = 0;
	uint8_t ovly_sec_num = 0;
	uint8_t ovly_info = 0;
	uint8_t find_bin_header = 0;

	end = fwdata[0] + (fwdata[1] << 8) + (fwdata[2] << 16) + (fwdata[3] << 24);

	cascade_2nd_header_info = (fwdata[0x20] & 0x02) >> 1;
	pr_info("NVT-ts: cascade_2nd_header_info = %d\n", cascade_2nd_header_info);

	if (cascade_2nd_header_info) {
		pos = 0x30;
		while (pos < (end / 2)) {
			info_sec_num++;
			pos += 0x10;
		}
		info_sec_num = info_sec_num + 1;
	} else {
		pos = 0x30;
		while (pos < end) {
			info_sec_num++;
			pos += 0x10;
		}
	}

	ovly_info = (fwdata[0x28] & 0x10) >> 4;
	ovly_sec_num = (ovly_info) ? (fwdata[0x28] & 0x0F) : 0;

	partition = ilm_dlm_num + ovly_sec_num + info_sec_num;
	pr_info("NVT-ts: ovly_info = %d, ilm_dlm_num = %d, ovly_sec_num = %d, info_sec_num = %d, partition = %d\n",
		ovly_info, ilm_dlm_num, ovly_sec_num, info_sec_num, partition);

	bin_map = kzalloc((partition + 1) * sizeof(struct nvt_ts_bin_map), GFP_KERNEL);
	if (bin_map == NULL) {
		pr_err("NVT-ts: kzalloc for bin_map failed!\n");
		return -ENOMEM;
	}

	for (list = 0; list < partition; list++) {
		if (list < ilm_dlm_num) {
			bin_map[list].BIN_addr = byte_to_word(&fwdata[0 + list * 12]);
			bin_map[list].SRAM_addr = byte_to_word(&fwdata[4 + list * 12]);
			bin_map[list].size = byte_to_word(&fwdata[8 + list * 12]);
			if (nvt_ts->hw_crc)
				bin_map[list].crc = byte_to_word(&fwdata[0x18 + list * 4]);
			else {
				if ((bin_map[list].BIN_addr + bin_map[list].size) < fwsize)
					bin_map[list].crc = CheckSum(&fwdata[bin_map[list].BIN_addr], bin_map[list].size);
				else {
					pr_err("NVT-ts: access range (0x%08X to 0x%08X) is larger than bin size!\n",
					       bin_map[list].BIN_addr, bin_map[list].BIN_addr + bin_map[list].size);
					return -EINVAL;
				}
			}
			if (list == 0)
				sprintf(bin_map[list].name, "ILM");
			else if (list == 1)
				sprintf(bin_map[list].name, "DLM");
		}

		if ((list >= ilm_dlm_num) && (list < (ilm_dlm_num + info_sec_num))) {
			if (find_bin_header == 0) {
				pos = 0x30 + (0x10 * (list - ilm_dlm_num));
			} else if (find_bin_header && cascade_2nd_header_info) {
				pos = end - 0x10;
			}

			bin_map[list].SRAM_addr = byte_to_word(&fwdata[pos]);
			bin_map[list].size = byte_to_word(&fwdata[pos + 4]);
			bin_map[list].BIN_addr = byte_to_word(&fwdata[pos + 8]);
			if (nvt_ts->hw_crc)
				bin_map[list].crc = byte_to_word(&fwdata[pos + 12]);
			else {
				if ((bin_map[list].BIN_addr + bin_map[list].size) < fwsize)
					bin_map[list].crc = CheckSum(&fwdata[bin_map[list].BIN_addr], bin_map[list].size);
				else {
					pr_err("NVT-ts: access range (0x%08X to 0x%08X) is larger than bin size!\n",
					       bin_map[list].BIN_addr, bin_map[list].BIN_addr + bin_map[list].size);
					return -EINVAL;
				}
			}
			if ((bin_map[list].BIN_addr < end) && (bin_map[list].size != 0)) {
				sprintf(bin_map[list].name, "Header");
				find_bin_header = 1;
			} else {
				sprintf(bin_map[list].name, "Info-%d", (list - ilm_dlm_num));
			}
		}

		if (list >= (ilm_dlm_num + info_sec_num)) {
			pos = bin_map[1].BIN_addr + (0x10 * (list - ilm_dlm_num - info_sec_num));

			bin_map[list].SRAM_addr = byte_to_word(&fwdata[pos]);
			bin_map[list].size = byte_to_word(&fwdata[pos + 4]);
			bin_map[list].BIN_addr = byte_to_word(&fwdata[pos + 8]);
			if (nvt_ts->hw_crc)
				bin_map[list].crc = byte_to_word(&fwdata[pos + 12]);
			else {
				if ((bin_map[list].BIN_addr + bin_map[list].size) < fwsize)
					bin_map[list].crc = CheckSum(&fwdata[bin_map[list].BIN_addr], bin_map[list].size);
				else {
					pr_err("NVT-ts: access range (0x%08X to 0x%08X) is larger than bin size!\n",
					       bin_map[list].BIN_addr, bin_map[list].BIN_addr + bin_map[list].size);
					return -EINVAL;
				}
			}
			sprintf(bin_map[list].name, "Overlay-%d", (list - ilm_dlm_num - info_sec_num));
		}

		if ((bin_map[list].BIN_addr + bin_map[list].size) > fwsize) {
			pr_err("NVT-ts: access range (0x%08X to 0x%08X) is larger than bin size!\n",
			       bin_map[list].BIN_addr, bin_map[list].BIN_addr + bin_map[list].size);
			return -EINVAL;
		}
	}

	return 0;
}

static int32_t nvt_write_sram(const u8 *fwdata,
			      uint32_t SRAM_addr, uint32_t size, uint32_t BIN_addr)
{
	int32_t ret = 0;
	uint32_t i = 0;
	uint16_t len = 0;
	int32_t count = 0;

	if (size % NVT_TRANSFER_LEN)
		count = (size / NVT_TRANSFER_LEN) + 1;
	else
		count = (size / NVT_TRANSFER_LEN);

	for (i = 0; i < count; i++) {
		len = (size < NVT_TRANSFER_LEN) ? size : NVT_TRANSFER_LEN;

		ret = nvt_set_page(SRAM_addr);
		if (ret) {
			pr_err("NVT-ts: set page failed, ret = %d\n", ret);
			return ret;
		}

		fwbuf[0] = SRAM_addr & 0x7F;
		memcpy(fwbuf + 1, &fwdata[BIN_addr], len);
		ret = CTP_SPI_WRITE(nvt_ts->client, fwbuf, len + 1);
		if (ret) {
			pr_err("NVT-ts: write to sram failed, ret = %d\n", ret);
			return ret;
		}

		SRAM_addr += NVT_TRANSFER_LEN;
		BIN_addr += NVT_TRANSFER_LEN;
		size -= NVT_TRANSFER_LEN;
	}

	return ret;
}

int32_t nvt_write_firmware(const u8 *fwdata, size_t fwsize)
{
	uint32_t list = 0;
	uint32_t BIN_addr, SRAM_addr, size;
	int32_t ret = 0;

	memset(fwbuf, 0, NVT_TRANSFER_LEN + 1);

	for (list = 0; list < partition; list++) {
		SRAM_addr = bin_map[list].SRAM_addr;
		size = bin_map[list].size;
		BIN_addr = bin_map[list].BIN_addr;

		if ((BIN_addr + size) > fwsize) {
			pr_err("NVT-ts: access range (0x%08X to 0x%08X) is larger than bin size!\n",
			       BIN_addr, BIN_addr + size);
			ret = -EINVAL;
			goto out;
		}

		if (!size)
			continue;
		else
			size = size + 1;

		ret = nvt_write_sram(fwdata, SRAM_addr, size, BIN_addr);
		if (ret) {
			pr_err("NVT-ts: sram program failed, ret = %d\n", ret);
			goto out;
		}
	}

out:
	return ret;
}

static void nvt_set_bld_crc_bank(uint32_t DES_ADDR, uint32_t SRAM_ADDR,
				 uint32_t LENGTH_ADDR, uint32_t size,
				 uint32_t G_CHECKSUM_ADDR, uint32_t crc)
{
	/* write destination address */
	nvt_set_page(DES_ADDR);
	fwbuf[0] = DES_ADDR & 0x7F;
	fwbuf[1] = (SRAM_ADDR) & 0xFF;
	fwbuf[2] = (SRAM_ADDR >> 8) & 0xFF;
	fwbuf[3] = (SRAM_ADDR >> 16) & 0xFF;
	CTP_SPI_WRITE(nvt_ts->client, fwbuf, 4);

	/* write length */
	fwbuf[0] = LENGTH_ADDR & 0x7F;
	fwbuf[1] = (size) & 0xFF;
	fwbuf[2] = (size >> 8) & 0xFF;
	fwbuf[3] = (size >> 16) & 0xFF;
	if (nvt_ts->hw_crc == HWCRC_LEN_2Bytes) {
		CTP_SPI_WRITE(nvt_ts->client, fwbuf, 3);
	} else if (nvt_ts->hw_crc >= HWCRC_LEN_3Bytes) {
		CTP_SPI_WRITE(nvt_ts->client, fwbuf, 4);
	}

	/* write golden dlm checksum */
	fwbuf[0] = G_CHECKSUM_ADDR & 0x7F;
	fwbuf[1] = (crc) & 0xFF;
	fwbuf[2] = (crc >> 8) & 0xFF;
	fwbuf[3] = (crc >> 16) & 0xFF;
	fwbuf[4] = (crc >> 24) & 0xFF;
	CTP_SPI_WRITE(nvt_ts->client, fwbuf, 5);
}

static void nvt_set_bld_hw_crc(void)
{
	/* [0] ILM */
	nvt_set_bld_crc_bank(NVT_MM->ILM_DES_ADDR, bin_map[0].SRAM_addr,
			     NVT_MM->ILM_LENGTH_ADDR, bin_map[0].size,
			     NVT_MM->G_ILM_CHECKSUM_ADDR, bin_map[0].crc);

	/* [1] DLM */
	nvt_set_bld_crc_bank(NVT_MM->DLM_DES_ADDR, bin_map[1].SRAM_addr,
			     NVT_MM->DLM_LENGTH_ADDR, bin_map[1].size,
			     NVT_MM->G_DLM_CHECKSUM_ADDR, bin_map[1].crc);
}

static void nvt_read_bld_hw_crc(void)
{
	u8 buf[8] = { 0 };
	uint32_t g_crc = 0, r_crc = 0;

	nvt_set_page(NVT_MM->BLD_ILM_DLM_CRC_ADDR);
	buf[0] = NVT_MM->BLD_ILM_DLM_CRC_ADDR & 0x7F;
	buf[1] = 0x00;
	CTP_SPI_READ(nvt_ts->client, buf, 2);
	pr_err("NVT-ts: crc_done = %d, ilm_crc_flag = %d, dlm_crc_flag = %d\n",
	       (buf[1] >> 2) & 0x01, (buf[1] >> 0) & 0x01, (buf[1] >> 1) & 0x01);

	nvt_set_page(NVT_MM->G_ILM_CHECKSUM_ADDR);
	buf[0] = NVT_MM->G_ILM_CHECKSUM_ADDR & 0x7F;
	CTP_SPI_READ(nvt_ts->client, buf, 5);
	g_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	nvt_set_page(NVT_MM->R_ILM_CHECKSUM_ADDR);
	buf[0] = NVT_MM->R_ILM_CHECKSUM_ADDR & 0x7F;
	CTP_SPI_READ(nvt_ts->client, buf, 5);
	r_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	pr_err("NVT-ts: ilm: bin crc = 0x%08X, golden = 0x%08X, result = 0x%08X\n",
	       bin_map[0].crc, g_crc, r_crc);

	nvt_set_page(NVT_MM->G_DLM_CHECKSUM_ADDR);
	buf[0] = NVT_MM->G_DLM_CHECKSUM_ADDR & 0x7F;
	CTP_SPI_READ(nvt_ts->client, buf, 5);
	g_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	nvt_set_page(NVT_MM->R_DLM_CHECKSUM_ADDR);
	buf[0] = NVT_MM->R_DLM_CHECKSUM_ADDR & 0x7F;
	CTP_SPI_READ(nvt_ts->client, buf, 5);
	r_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	pr_err("NVT-ts: dlm: bin crc = 0x%08X, golden = 0x%08X, result = 0x%08X\n",
	       bin_map[1].crc, g_crc, r_crc);
}

static int32_t nvt_download_firmware_hw_crc(void)
{
	uint8_t retry = 0;
	int32_t ret = 0;

	fw_dl_start = ktime_get();

	while (1) {
		nvt_bootloader_reset();

		nvt_set_bld_hw_crc();

		if (cascade_2nd_header_info) {
			nvt_tx_auto_copy_mode();

			ret = nvt_write_firmware(fw_entry->data, fw_entry->size);
			if (ret) {
				pr_err("NVT-ts: Write_Firmware failed. (%d)\n", ret);
				goto fail;
			}

			ret = nvt_wait_auto_copy();
			if (ret) {
				pr_err("NVT-ts: wait auto copy failed. (%d)\n", ret);
				goto fail;
			}
		} else {
			ret = nvt_write_firmware(fw_entry->data, fw_entry->size);
			if (ret) {
				pr_err("NVT-ts: Write_Firmware failed. (%d)\n", ret);
				goto fail;
			}
		}

		nvt_fw_crc_enable();

		nvt_boot_ready();

		ret = nvt_check_fw_reset_state(RESET_STATE_INIT);
		if (ret) {
			pr_err("NVT-ts: nvt_check_fw_reset_state failed. (%d)\n", ret);
			goto fail;
		} else {
			break;
		}

fail:
		retry++;
		if (retry > 2) {
			pr_err("NVT-ts: error, retry=%d\n", retry);
			nvt_read_bld_hw_crc();
			break;
		}
	}

	fw_dl_end = ktime_get();

	return ret;
}

void nvt_boot_ready(void)
{
	nvt_write_addr(NVT_MM->BOOT_RDY_ADDR, 1);

	mdelay(5);

	if (nvt_ts->hw_crc == HWCRC_NOSUPPORT) {
		nvt_write_addr(NVT_MM->BOOT_RDY_ADDR, 0);
		nvt_write_addr(NVT_MM->POR_CD_ADDR, 0xA0);
	}
}

void nvt_eng_reset(void)
{
	nvt_write_addr(0x7fff80, 0x5a);
	udelay(1000);
}

void nvt_bootloader_reset(void)
{
	/* stock nvt_bootloader_reset @0x8ad8808:
	 *   nvt_write_addr(ts->swrst_sif_addr, 0x69)     ; ts+396
	 *   __const_udelay(0x147aeb8)                    ; 5 ms
	 *   if (ts->spi_rd_fast_addr) nvt_write_addr(ts->spi_rd_fast_addr, 0)
	 * (ts+1324 == spi_rd_fast_addr; 0x3f010 was a hardcoded fallback.)
	 */
	nvt_write_addr(nvt_ts->swrst_sif_addr, 0x69);
	__const_udelay(0x147aeb8);
	if (nvt_spi_rd_fast_addr)
		nvt_write_addr(nvt_spi_rd_fast_addr, 0);
}

void nvt_fw_crc_enable(void)
{
	u8 buf[8] = { 0 };

	nvt_set_page(NVT_MM->EVENT_BUF_ADDR);

	buf[0] = EVENT_MAP_RESET_COMPLETE & (0x7F);
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	buf[5] = 0x00;
	buf[6] = 0x00;
	CTP_SPI_WRITE(nvt_ts->client, buf, 7);

	buf[0] = EVENT_MAP_HOST_CMD & (0x7F);
	buf[1] = 0xAE;
	buf[2] = 0x00;
	CTP_SPI_WRITE(nvt_ts->client, buf, 3);
}

/* ---- nvt_check_spi_dma_tx_info / nvt_tx_auto_copy_mode / nvt_wait_auto_copy ---- */
int32_t nvt_check_spi_dma_tx_info(void)
{
	u8 buf[8] = { 0 };
	int32_t i = 0;
	const int32_t retry = 200;

	if (NVT_MM->SPI_DMA_TX_INFO == 0) {
		pr_err("NVT-ts: error, SPI_DMA_TX_INFO = 0\n");
		return -1;
	}

	for (i = 0; i < retry; i++) {
		nvt_set_page(NVT_MM->SPI_DMA_TX_INFO);

		buf[0] = NVT_MM->SPI_DMA_TX_INFO & 0x7F;
		buf[1] = 0xFF;
		CTP_SPI_READ(nvt_ts->client, buf, 2);

		if (buf[1] == 0x00)
			break;

		usleep_range(1000, 1000);
	}

	if (i >= retry) {
		pr_err("NVT-ts: failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -1;
	} else {
		return 0;
	}
}

void nvt_tx_auto_copy_mode(void)
{
	/* D111 (F10, review REVIEW-VERDICT-F7-F10.md): D109 misread the
	 * branch targets - 0x8ad8424 b.eq 0x8ad843c means ==2 jumps to the
	 * 0x56 slot, ==1 falls through to the 0x69 slot. Stock truth:
	 * ==1(CHECK_SPI_DMA_TX_INFO) -> 0x69, ==2(CHECK_TX_AUTO_COPY_EN)
	 * -> 0x56. D109 had it backwards; restored. */
	if (nvt_ts->auto_copy == CHECK_SPI_DMA_TX_INFO) {
		nvt_write_addr(NVT_MM->TX_AUTO_COPY_EN, 0x69);
	} else if (nvt_ts->auto_copy == CHECK_TX_AUTO_COPY_EN) {
		nvt_write_addr(NVT_MM->TX_AUTO_COPY_EN, 0x56);
	}

	pr_info("NVT-ts: tx auto copy mode %d enable\n", nvt_ts->auto_copy);
}

int32_t nvt_check_tx_auto_copy(void)
{
	u8 buf[8] = { 0 };
	int32_t i = 0;
	const int32_t retry = 200;

	if (NVT_MM->TX_AUTO_COPY_EN == 0) {
		pr_err("NVT-ts: error, TX_AUTO_COPY_EN = 0\n");
		return -1;
	}

	for (i = 0; i < retry; i++) {
		nvt_set_page(NVT_MM->TX_AUTO_COPY_EN);

		buf[0] = NVT_MM->TX_AUTO_COPY_EN & 0x7F;
		buf[1] = 0xFF;
		CTP_SPI_READ(nvt_ts->client, buf, 2);

		if (buf[1] == 0x00)
			break;

		usleep_range(1000, 1000);
	}

	if (i >= retry) {
		pr_err("NVT-ts: failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -1;
	} else {
		return 0;
	}
}

int32_t nvt_wait_auto_copy(void)
{
	if (nvt_ts->auto_copy == CHECK_SPI_DMA_TX_INFO) {
		return nvt_check_spi_dma_tx_info();
	} else if (nvt_ts->auto_copy == CHECK_TX_AUTO_COPY_EN) {
		return nvt_check_tx_auto_copy();
	} else {
		pr_err("NVT-ts: failed, not support mode %d!\n", nvt_ts->auto_copy);
		return -1;
	}
}

int nvt_update_firmware(const char *firmware_name)
{
	const struct firmware *fw = NULL;
	struct device *dev = &nvt_ts->client->dev;
	int ret;

	ret = request_firmware(&fw, firmware_name, dev);
	if (ret) {
		pr_err("NVT-ts: nvt_update_firmware: request %s failed %d\n",
		       firmware_name, ret);
		return ret;
	}

	ret = nvt_get_fw_need_write_size(fw);
	if (ret)
		goto out;

	ret = nvt_download_init();
	if (ret)
		goto out;

	fw_entry = fw;
	ret = nvt_bin_header_parser(fw->data, fw->size);
	if (ret)
		goto out_free_map;

	if (nvt_ts->hw_crc)
		ret = nvt_download_firmware_hw_crc();
	else {
		pr_err("NVT-ts: nvt_update_firmware: non-hw-crc path not supported (hw_crc=%u)\n",
		       nvt_ts->hw_crc);
		ret = -EINVAL;
	}

	if (!ret) {
		pr_info("NVT-ts: Update firmware success! <%ld us>\n",
			(long)ktime_us_delta(fw_dl_end, fw_dl_start));
		nvt_ts->fw_update_stat = 1;
		ret = nvt_get_fw_info();
		if (nvt_ts->charger_wq)
			queue_work_on(8, nvt_ts->charger_wq,
				       &nvt_ts->charger_work);
	}

out_free_map:
	if (bin_map) {
		kfree(bin_map);
		bin_map = NULL;
	}
out:
	fw_entry = NULL;
	release_firmware(fw);
	return ret;
}

static char tp_version_info[128];

/* stock get_tp_info @0x8ad8e20: refresh fw info, format by gpio_82, print,
 * then publish HWID_CTP (0x23) through the stock hq_register_hw_info()
 * provider (drivers/misc/hqsysfs). */
void get_tp_info(void)
{
	nvt_get_fw_info();
	if (gpio_82 == 0) {
		sprintf(tp_version_info,
			"[Vendor]:BOE, [TP-IC]:NT36532, [fw]:0x%02X",
			nvt_ts->fw_ver);
		pr_info("[%s]: tp_version %s\n", __func__, tp_version_info);
	} else if (gpio_82 == 1) {
		sprintf(tp_version_info,
			"[Vendor]:TIANMA, [TP-IC]:NT36532, [fw]:0x%02X",
			nvt_ts->fw_ver);
		pr_info("[%s]: tp_version %s\n", __func__, tp_version_info);
	} else {
		pr_err("failed to add tp_info\n");
	}
	hq_register_hw_info(HWID_CTP, tp_version_info);
}

static void nvt_boot_update_firmware_work(struct work_struct *work)
{
	/* D55 final: the DSI panel registers at ~1.08s but NVT probes at
	 * ~0.6s, so the panel lookup at probe time always fails. Retry the
	 * notifier registration here (work runs 14s after probe, panel is
	 * long up). Idempotent: drm_panel_notifier_register refuses dupes. */
	if (!active_panel) {
		struct device_node *np = nvt_ts->client->dev.of_node;
		struct device_node *pnode;
		struct drm_panel *panel;
		int cnt = of_count_phandle_with_args(np, "panel", NULL);
		int k;

		for (k = 0; k < cnt; k++) {
			pnode = of_parse_phandle(np, "panel", k);
			panel = of_drm_find_panel(pnode);
			of_node_put(pnode);
			if (!IS_ERR(panel)) {
				active_panel = panel;
				break;
			}
		}
	}
	if (active_panel &&
	    drm_panel_notifier_register(active_panel, &nvt_ts->drm_nb) < 0)
		pr_err("NVT-ts: fwu-work notifier registration failed\n");

	mutex_lock(&nvt_ts->lock);
	nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
	get_tp_info();
	mutex_unlock(&nvt_ts->lock);

	pr_info("NVT-ts: nvt_restore_cmd_func: %s++\n", "nvt_restore_cmd_func");
	nvt_edge_reject_set(nvt_ts->edge_reject);
	nvt_edge_grid_zone_set(nvt_ts->edge_grid_zone[0], nvt_ts->edge_grid_zone[1],
			       nvt_ts->edge_grid_zone[2], nvt_ts->edge_grid_zone[3]);
	nvt_game_mode_set(nvt_ts->game_mode);
	if (!nvt_ts->pen_suppress)
		nvt_support_pen_set(nvt_ts->pen_enable, nvt_ts->pen_version);
	else
		nvt_support_pen_set(0, 0);
	pr_info("NVT-ts: nvt_restore_cmd_func: %s--\n", "nvt_restore_cmd_func");
}

/* ---- nvt_clear_fw_status (stock @0xffffff8008ad8870, TASK-081) ---- */
int nvt_clear_fw_status(void)
{
	u8 buf[3];
	int retry;

	for (retry = 0; retry < 20; retry++) {
		nvt_set_page(nvt_ts->mmap[0]);
		buf[0] = 0x51;
		buf[1] = 0x00;
		CTP_SPI_WRITE(nvt_ts->client, buf, 2);
		buf[0] = 0x51;
		buf[1] = 0xff;
		CTP_SPI_READ(nvt_ts->client, buf, 2);
		if (buf[1] == 0)
			return 0;
		usleep_range(10000, 10000);
	}
	pr_err("3[%s] %s %d: failed, i=%d, buf[1]=0x%02X\n",
	       "nvt_clear_fw_status", "nvt_clear_fw_status", 711, 20, buf[1]);
	return -1;
}

/* D59: stock nvt_irq_enable - tracks ts->irq_enabled and pairs
 * enable/disable. Lost in the old-tree merge; restored. */
void nvt_irq_enable(bool enable)
{
	if (enable) {
		if (!nvt_ts->irq_enabled) {
			enable_irq(nvt_ts->irq);
			nvt_ts->irq_enabled = 1;
		}
	} else {
		if (nvt_ts->irq_enabled) {
			disable_irq(nvt_ts->irq);
			nvt_ts->irq_enabled = 0;
		}
	}
}

/* ---- nvt_check_fw_status (stock @0xffffff8008ad8998, TASK-081) ---- */
int nvt_check_fw_status(void)
{
	u8 buf[2];
	int retry;

	usleep_range(20000, 20000);
	for (retry = 0; retry < 50; retry++) {
		nvt_set_page(nvt_ts->mmap[0]);
		buf[0] = 0x51;
		buf[1] = 0x00;
		CTP_SPI_READ(nvt_ts->client, buf, 2);
		if ((buf[1] & 0xf0) == 0xa0)
			return 0;
		usleep_range(10000, 10000);
	}
	pr_err("3[%s] %s %d: failed, i=%d, buf[1]=0x%02X\n",
	       "nvt_check_fw_status", "nvt_check_fw_status", 749, 50, buf[1]);
	return -1;
}

/* ---- proc fw_version ---- */
static int nvt_fw_version_show(struct seq_file *s, void *v)
{
	/* D42: do NOT hold xbuf_lock here - CTP_SPI_READ/WRITE take it
	 * internally; the old wrapper caused a self-deadlock (cat /proc/
	 * nvt_fw_version stuck in D state inside CTP_SPI_WRITE).
	 */
	nvt_get_fw_info();
	seq_printf(s, "FW Version: %d\n\n", nvt_ts->fw_ver);
	return 0;
}

static int nvt_fw_version_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvt_fw_version_show, NULL);
}

static const struct file_operations nvt_fw_version_fops = {
	.open = nvt_fw_version_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* ---- proc fw_status ---- */
static int nvt_fw_status_show(struct seq_file *s, void *v)
{
	u8 buf[6] = { 0x60, 0xff, 0xff, 0xff, 0xff, 0xff };

	/* stock nvt_fw_status_show @0x8ae1220: mutex_lock_interruptible(SPI
	 * bus lock, ts+0xf8 == our ->lock) -> set_page(mmap[0] | 0x60) ->
	 * CTP_SPI_READ 6 bytes -> unlock -> seq_printf("%02X\n", buf[1]).
	 * The previous 0x50/2-byte read was a different register entirely, so
	 * the value it printed was not the FW status.
	 */
	if (mutex_lock_interruptible(&nvt_ts->lock))
		return -ERESTARTSYS;
	nvt_set_page(nvt_ts->mmap[0] | 0x60);
	CTP_SPI_READ(nvt_ts->client, buf, sizeof(buf));
	mutex_unlock(&nvt_ts->lock);
	seq_printf(s, "%02X\n", buf[1]);
	return 0;
}

static int nvt_fw_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvt_fw_status_show, NULL);
}

static const struct file_operations nvt_fw_status_fops = {
	.open = nvt_fw_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* ---- proc edge_reject ---- */
static int nvt_edge_reject_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%d\n", nvt_ts->edge_reject);
	return 0;
}

static int nvt_edge_reject_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvt_edge_reject_show, NULL);
}

static ssize_t nvt_edge_reject_store(struct file *file, const char __user *buf,
				     size_t count, loff_t *pos)
{
	u8 value;

	if (kstrtou8_from_user(buf, count, 10, &value))
		return -EINVAL;
	nvt_ts->edge_reject = value;
	/* D62: stock nvt_edge_reject_store @0x8ae1328: value <= 3 ->
	 * nvt_cmd_store(jump-table byte 0xba/0xbb/0xbc/0xbd), 2-byte frame. */
	if (value < 4)
		nvt_cmd_store((0xbcbdbbba >> (value * 8)) & 0xff);
	return count;
}

static const struct file_operations nvt_edge_reject_fops = {
	.open = nvt_edge_reject_open,
	.read = seq_read,
	.write = nvt_edge_reject_store,
	.llseek = seq_lseek,
	.release = single_release,
};

/* ---- proc game_mode ---- */
static int nvt_game_mode_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%d\n", nvt_ts->game_mode);
	return 0;
}

static int nvt_game_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvt_game_mode_show, NULL);
}

static ssize_t nvt_game_mode_store(struct file *file, const char __user *buf,
				   size_t count, loff_t *pos)
{
	u8 value;
	u8 mapped;

	if (kstrtou8_from_user(buf, count, 10, &value))
		return -EINVAL;
	nvt_ts->game_mode = value;
	mapped = (value == 0) ? 0 : (value == 1) ? 2 : 1;
	nvt_ext_cmd_store(0x79, mapped);
	return count;
}

static const struct file_operations nvt_game_mode_fops = {
	.open = nvt_game_mode_open,
	.read = seq_read,
	.write = nvt_game_mode_store,
	.llseek = seq_lseek,
	.release = single_release,
};

/* ---- proc support_pen ---- */
static int nvt_support_pen_show(struct seq_file *s, void *v)
{
	seq_printf(s, "Pen state %d!\n", nvt_ts->pen_enable);
	return 0;
}

static int nvt_support_pen_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvt_support_pen_show, NULL);
}

static ssize_t nvt_support_pen_store(struct file *file, const char __user *buf,
				     size_t count, loff_t *pos)
{
	char data[33] = { 0 };
	unsigned int enable, version;

	/* stock nvt_support_pen_store @0x8ae18c0: __check_object_size ->
	 * copy_from_user into a kernel stack buffer -> sscanf. Passing the
	 * __user pointer straight to sscanf faults under PAN and, with
	 * panic_on_oops=1, panics the box when the Lenovo touchscreen HAL
	 * writes /proc/support_pen during boot (root cause of the #360/#364
	 * boot loop, confirmed from the kmsg_tb371_log flight recorder).
	 */
	if (count > sizeof(data) - 1)
		count = sizeof(data) - 1;
	if (copy_from_user(data, buf, count))
		return -EFAULT;
	data[count] = '\0';

	if (sscanf(data, "%u,%u", &enable, &version) != 2)
		return -EINVAL;
	nvt_ts->pen_enable = enable;
	nvt_ts->pen_version = version;
	if (!nvt_ts->pen_suppress)
		nvt_support_pen_set(enable, version);
	return count;
}

static const struct file_operations nvt_support_pen_fops = {
	.open = nvt_support_pen_open,
	.read = seq_read,
	.write = nvt_support_pen_store,
	.llseek = seq_lseek,
	.release = single_release,
};


/* ---- gesture_control (stock 0x8ae0e38/0x8ae0f78/0x8ae0fa0) ---- */
static int gesture_control_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%d\n", nvt_gesture_flag);
	return 0;
}

static int gesture_control_open(struct inode *inode, struct file *file)
{
	return single_open(file, gesture_control_show, NULL);
}

/* stock gesture_control_write: copy 1 byte, first byte '1'(0x31) -> flag=1,
 * anything else -> flag=0; no mutex, no set_page.
 */
static ssize_t gesture_control_write(struct file *file,
				     const char __user *buf, size_t count,
				     loff_t *pos)
{
	u8 value;

	if (copy_from_user(&value, buf, 1))
		return -EFAULT;
	nvt_gesture_flag = (value == '1') ? 1 : 0;
	return count;
}

static const struct file_operations gesture_control_fops = {
	.read = seq_read,
	.write = gesture_control_write,
	.llseek = seq_lseek,
	.open = gesture_control_open,
	.release = single_release,
};

/* ---- edge_grid_zone (stock 0x8ae14f0/0x8ae16a0/0x8ae16c8) ---- */
static int nvt_edge_grid_zone_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%d,%d,%d,%d\n",
		   nvt_ts->edge_grid_zone[0], nvt_ts->edge_grid_zone[1],
		   nvt_ts->edge_grid_zone[2], nvt_ts->edge_grid_zone[3]);
	return 0;
}

static int nvt_edge_grid_zone_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvt_edge_grid_zone_show, NULL);
}

/* stock store: copy<=32 bytes + NUL, sscanf "%d,%d,%d,%d" into the four
 * members, printk, then nvt_edge_grid_zone_set().
 */
static ssize_t nvt_edge_grid_zone_store(struct file *file,
					const char __user *buf, size_t count,
					loff_t *pos)
{
	char data[33] = { 0 };
	int x1, y1, x2, y2;

	if (count > sizeof(data) - 1)
		count = sizeof(data) - 1;
	if (copy_from_user(data, buf, count))
		return -EFAULT;
	data[count] = '\0';

	if (sscanf(data, "%d,%d,%d,%d", &x1, &y1, &x2, &y2) != 4)
		return -EINVAL;

	mutex_lock(&nvt_ts->lock);
	nvt_ts->edge_grid_zone[0] = x1;
	nvt_ts->edge_grid_zone[1] = y1;
	nvt_ts->edge_grid_zone[2] = x2;
	nvt_ts->edge_grid_zone[3] = y2;
	mutex_unlock(&nvt_ts->lock);

	pr_info("NVT-ts: %s: x1=%d, y1=%d, x2=%d, y2=%d\n",
		__func__, x1, y1, x2, y2);
	nvt_edge_grid_zone_set((u8)x1, (u8)y1, (u16)x2, (u16)y2);
	return count;
}

static const struct file_operations nvt_edge_grid_zone_fops = {
	.read = seq_read,
	.write = nvt_edge_grid_zone_store,
	.llseek = seq_lseek,
	.open = nvt_edge_grid_zone_open,
	.release = single_release,
};

/* ---- fw_pen_state (stock 0x8ae1af0/0x8ae1b18) ---- */
static int nvt_get_fw_pen_state_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%d\n", nvt_ts->fw_pen_state);
	return 0;
}

static int nvt_get_fw_pen_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvt_get_fw_pen_state_show, NULL);
}

static const struct file_operations nvt_get_fw_pen_state_fops = {
	.read = seq_read,
	.llseek = seq_lseek,
	.open = nvt_get_fw_pen_state_open,
	.release = single_release,
};

/* ---- baseline / raw / diff / pen_diff data chains (TASK-081) ----
 * Stock runs the full SPI acquisition sequence in *_open() and serves the
 * data through shared seq_ops: nvt_seq_ops @0x9813f48 (c_start/c_stop/
 * c_next/c_show @0x8adec70..0x8adecd8) for baseline/raw/diff and
 * nvt_pen_diff_seq_ops @0x9813f68 (c_pen_1d_diff_show @0x8adeda8) for
 * pen_diff.  fops = {seq_lseek, seq_read, open, seq_release}.
 */
static int32_t nvt_pen_1d_tip_x[0x100];
static int32_t nvt_pen_1d_tip_y[0x100];
static int32_t nvt_pen_1d_ring_x[0x100];
static int32_t nvt_pen_1d_ring_y[0x100];

static void *nvt_c_start(struct seq_file *m, loff_t *pos)
{
	return (void *)(unsigned long)(*pos < 1);
}

static void nvt_c_stop(struct seq_file *m, void *v)
{
}

static void *nvt_c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

/* stock c_show @0xffffff8008adecd8 (shared by baseline/raw/diff) */
static int nvt_c_show(struct seq_file *m, void *v)
{
	u32 i, j;

	seq_printf(m, "fw_ver=%d, x_num=%d, y_num=%d, button_num=%d\n",
		   nvt_ts->fw_ver, nvt_ts->x_num, nvt_ts->y_num,
		   nvt_ts->max_button_num);
	for (i = 0; i < nvt_ts->y_num; i++) {
		for (j = 0; j < nvt_ts->x_num; j++)
			seq_printf(m, "%5d, ", nvt_mdata[i * nvt_ts->x_num + j]);
		seq_puts(m, "\n");
	}
	seq_printf(m, "\n\n");
	return 0;
}

/* stock c_pen_1d_diff_show @0xffffff8008adeda8 */
static int nvt_c_pen_1d_diff_show(struct seq_file *m, void *v)
{
	u32 i;

	seq_puts(m, "Tip X:\n");
	for (i = 0; i < nvt_ts->x_num; i++)
		seq_printf(m, "%5d, ", nvt_pen_1d_tip_x[i]);
	seq_puts(m, "\n");
	seq_puts(m, "Tip Y:\n");
	for (i = 0; i < nvt_ts->y_num; i++)
		seq_printf(m, "%5d, ", nvt_pen_1d_tip_y[i]);
	seq_puts(m, "\n");
	seq_puts(m, "Ring X:\n");
	for (i = 0; i < nvt_ts->x_num; i++)
		seq_printf(m, "%5d, ", nvt_pen_1d_ring_x[i]);
	seq_puts(m, "\n");
	seq_puts(m, "Ring Y:\n");
	for (i = 0; i < nvt_ts->y_num; i++)
		seq_printf(m, "%5d, ", nvt_pen_1d_ring_y[i]);
	seq_puts(m, "\n");
	seq_printf(m, "\n\n");
	return 0;
}

static const struct seq_operations nvt_seq_ops = {
	.start = nvt_c_start,
	.stop = nvt_c_stop,
	.next = nvt_c_next,
	.show = nvt_c_show,
};

static const struct seq_operations nvt_pen_diff_seq_ops = {
	.start = nvt_c_start,
	.stop = nvt_c_stop,
	.next = nvt_c_next,
	.show = nvt_c_pen_1d_diff_show,
};

static int nvt_baseline_open(struct inode *inode, struct file *file)
{
	u8 buf[2];

	if (mutex_lock_interruptible(&nvt_ts->lock))
		return -ERESTARTSYS;
	pr_info("3[%s] %s %d: ++\n", "nvt_baseline_open", "nvt_baseline_open", 639);
	if (nvt_clear_fw_status() == 0) {
		buf[0] = 0x50;
		buf[1] = 0x22;
		nvt_set_page(nvt_ts->mmap[0] | 0x50);
		CTP_SPI_WRITE(nvt_ts->client, buf, 2);
		if (nvt_check_fw_status() == 0 && nvt_get_fw_info() == 0) {
			nvt_read_mdata(nvt_ts->mmap[3] /* BASELINE_ADDR */);
			nvt_set_page(nvt_ts->mmap[0] | 0x50);
			buf[0] = 0x50;
			buf[1] = 0x00;
			CTP_SPI_WRITE(nvt_ts->client, buf, 2);
			usleep_range(20000, 20000);
			buf[0] = 0x51;
			buf[1] = 0xbb;
			CTP_SPI_WRITE(nvt_ts->client, buf, 2);
			usleep_range(20000, 20000);
			mutex_unlock(&nvt_ts->lock);
			pr_info("3[%s] %s %d: --\n",
				"nvt_baseline_open", "nvt_baseline_open", 668);
			return seq_open(file, &nvt_seq_ops);
		}
	}
	mutex_unlock(&nvt_ts->lock);
	return -EIO;
}

static int nvt_raw_open(struct inode *inode, struct file *file)
{
	u8 buf[2];
	u8 pipe_buf[2];

	if (mutex_lock_interruptible(&nvt_ts->lock))
		return -ERESTARTSYS;
	pr_info("3[%s] %s %d: ++\n", "nvt_raw_open", "nvt_raw_open", 703);
	if (nvt_clear_fw_status() == 0) {
		buf[0] = 0x50;
		buf[1] = 0x22;
		nvt_set_page(nvt_ts->mmap[0] | 0x50);
		CTP_SPI_WRITE(nvt_ts->client, buf, 2);
		if (nvt_check_fw_status() == 0 && nvt_get_fw_info() == 0) {
			int pipe;

			nvt_set_page(nvt_ts->mmap[0] | 0x51);
			pipe_buf[0] = 0x51;
			pipe_buf[1] = 0x00;
			CTP_SPI_READ(nvt_ts->client, pipe_buf, 2);
			pipe = pipe_buf[1] & 1;
			/* RAW_PIPE0_ADDR / RAW_PIPE1_ADDR */
			nvt_read_mdata(nvt_ts->mmap[pipe ? 2 : 1]);
			nvt_set_page(nvt_ts->mmap[0] | 0x50);
			buf[0] = 0x50;
			buf[1] = 0x00;
			CTP_SPI_WRITE(nvt_ts->client, buf, 2);
			usleep_range(20000, 20000);
			buf[0] = 0x51;
			buf[1] = 0xbb;
			CTP_SPI_WRITE(nvt_ts->client, buf, 2);
			usleep_range(20000, 20000);
			mutex_unlock(&nvt_ts->lock);
			pr_info("3[%s] %s %d: --\n",
				"nvt_raw_open", "nvt_raw_open", 735);
			return seq_open(file, &nvt_seq_ops);
		}
	}
	mutex_unlock(&nvt_ts->lock);
	return -EIO;
}

static int nvt_diff_open(struct inode *inode, struct file *file)
{
	u8 buf[2];
	u8 pipe_buf[2];

	if (mutex_lock_interruptible(&nvt_ts->lock))
		return -ERESTARTSYS;
	pr_info("3[%s] %s %d: ++\n", "nvt_diff_open", "nvt_diff_open", 770);
	if (nvt_clear_fw_status() == 0) {
		buf[0] = 0x50;
		buf[1] = 0x22;
		nvt_set_page(nvt_ts->mmap[0] | 0x50);
		CTP_SPI_WRITE(nvt_ts->client, buf, 2);
		if (nvt_check_fw_status() == 0 && nvt_get_fw_info() == 0) {
			int pipe;

			nvt_set_page(nvt_ts->mmap[0] | 0x51);
			pipe_buf[0] = 0x51;
			pipe_buf[1] = 0x00;
			CTP_SPI_READ(nvt_ts->client, pipe_buf, 2);
			pipe = pipe_buf[1] & 1;
			/* DIFF_PIPE0_ADDR / DIFF_PIPE1_ADDR */
			nvt_read_mdata(nvt_ts->mmap[pipe ? 6 : 5]);
			nvt_set_page(nvt_ts->mmap[0] | 0x50);
			buf[0] = 0x50;
			buf[1] = 0x00;
			CTP_SPI_WRITE(nvt_ts->client, buf, 2);
			usleep_range(20000, 20000);
			buf[0] = 0x51;
			buf[1] = 0xbb;
			CTP_SPI_WRITE(nvt_ts->client, buf, 2);
			usleep_range(20000, 20000);
			mutex_unlock(&nvt_ts->lock);
			pr_info("3[%s] %s %d: --\n",
				"nvt_diff_open", "nvt_diff_open", 802);
			return seq_open(file, &nvt_seq_ops);
		}
	}
	mutex_unlock(&nvt_ts->lock);
	return -EIO;
}

static int nvt_pen_diff_open(struct inode *inode, struct file *file)
{
	u8 buf[2];

	if (mutex_lock_interruptible(&nvt_ts->lock))
		return -ERESTARTSYS;
	pr_info("3[%s] %s %d: ++\n", "nvt_pen_diff_open", "nvt_pen_diff_open", 857);
	if (nvt_set_pen_inband_mode_1(0xff, 0) == 0 &&
	    nvt_check_fw_reset_state(0xa3) == 0 &&
	    nvt_clear_fw_status() == 0) {
		buf[0] = 0x50;
		buf[1] = 0x22;
		nvt_set_page(nvt_ts->mmap[0] | 0x50);
		CTP_SPI_WRITE(nvt_ts->client, buf, 2);
		if (nvt_check_fw_status() == 0 && nvt_get_fw_info() == 0) {
			/* PEN_1D_DIFF_{TIP_X,TIP_Y,RING_X,RING_Y}_ADDR */
			nvt_read_get_num_mdata(nvt_ts->mmap[23], nvt_pen_1d_tip_x,
					       nvt_ts->x_num);
			nvt_read_get_num_mdata(nvt_ts->mmap[24], nvt_pen_1d_tip_y,
					       nvt_ts->y_num);
			nvt_read_get_num_mdata(nvt_ts->mmap[25], nvt_pen_1d_ring_x,
					       nvt_ts->x_num);
			nvt_read_get_num_mdata(nvt_ts->mmap[26], nvt_pen_1d_ring_y,
					       nvt_ts->y_num);
			nvt_set_page(nvt_ts->mmap[0] | 0x50);
			buf[0] = 0x50;
			buf[1] = 0x00;
			CTP_SPI_WRITE(nvt_ts->client, buf, 2);
			usleep_range(20000, 20000);
			buf[0] = 0x51;
			buf[1] = 0xbb;
			CTP_SPI_WRITE(nvt_ts->client, buf, 2);
			usleep_range(20000, 20000);
			nvt_set_pen_normal_mode();
			nvt_check_fw_reset_state(0xa3);
			mutex_unlock(&nvt_ts->lock);
			pr_info("3[%s] %s %d: --\n",
				"nvt_pen_diff_open", "nvt_pen_diff_open", 903);
			return seq_open(file, &nvt_pen_diff_seq_ops);
		}
	}
	mutex_unlock(&nvt_ts->lock);
	return -EIO;
}

static const struct file_operations nvt_baseline_fops = {
	.read = seq_read,
	.llseek = seq_lseek,
	.open = nvt_baseline_open,
	.release = seq_release,
};

static const struct file_operations nvt_raw_fops = {
	.read = seq_read,
	.llseek = seq_lseek,
	.open = nvt_raw_open,
	.release = seq_release,
};

static const struct file_operations nvt_diff_fops = {
	.read = seq_read,
	.llseek = seq_lseek,
	.open = nvt_diff_open,
	.release = seq_release,
};

static const struct file_operations nvt_pen_diff_fops = {
	.read = seq_read,
	.llseek = seq_lseek,
	.open = nvt_pen_diff_open,
	.release = seq_release,
};

int nvt_extra_proc_init(void)
{
	struct proc_dir_entry *e;

	/* stock nvt_extra_proc_init @0xffffff8008adfe78: 12 nodes, ALL with
	 * parent=NULL (top level), stock order/mode/fail-line/success-line per
	 * NOTES-tb371fc-touch-stock-parity.md 2026-10-17 section A.
	 */
#define NVT_PROC_ENTRY(nm, fops, mode, fail_L, succ_L, store)	\
	do { \
		e = proc_create((nm), (mode), NULL, (fops)); \
		if (!e) { \
			pr_err("3[%s] %s %d: create proc/%s Failed!\n", \
			       "nvt_extra_proc_init", "nvt_extra_proc_init", \
			       (fail_L), (nm)); \
			nvt_extra_proc_deinit(); \
			return -ENOMEM; \
		} \
		pr_info("3[%s] %s %d: create proc/%s Succeeded!\n", \
			"nvt_extra_proc_init", "nvt_extra_proc_init", \
			(succ_L), (nm)); \
		(store) = e; \
	} while (0)

	NVT_PROC_ENTRY("nvt_fw_version", &nvt_fw_version_fops, 0x124, 1608, 1611, nvt_ts->proc_fw_version);
	NVT_PROC_ENTRY("nvt_baseline", &nvt_baseline_fops, 0x124, 1616, 1619, nvt_ts->proc_baseline);
	NVT_PROC_ENTRY("nvt_raw", &nvt_raw_fops, 0x124, 1624, 1627, nvt_ts->proc_raw);
	NVT_PROC_ENTRY("nvt_diff", &nvt_diff_fops, 0x124, 1632, 1635, nvt_ts->proc_diff);
	NVT_PROC_ENTRY("gesture_control", &gesture_control_fops, 0x124, 1640, 1643, nvt_ts->proc_gesture_control);
	NVT_PROC_ENTRY("nvt_pen_diff", &nvt_pen_diff_fops, 0x124, 1649, 1652, nvt_ts->proc_pen_diff);
	NVT_PROC_ENTRY("nvt_fw_status", &nvt_fw_status_fops, 0x124, 1658, 1661, nvt_ts->proc_fw_status);
	NVT_PROC_ENTRY("panel_direction", &nvt_edge_reject_fops, 0x1a4, 1667, 1670, nvt_ts->proc_edge_reject);
	NVT_PROC_ENTRY("edge_grid_zone", &nvt_edge_grid_zone_fops, 0x1a4, 1675, 1678, nvt_ts->proc_edge_grid_zone);
	NVT_PROC_ENTRY("game_mode", &nvt_game_mode_fops, 0x1b6, 1684, 1687, nvt_ts->proc_game_mode);
	NVT_PROC_ENTRY("support_pen", &nvt_support_pen_fops, 0x1b6, 1692, 1695, nvt_ts->proc_support_pen);
	NVT_PROC_ENTRY("fw_pen_state", &nvt_get_fw_pen_state_fops, 0x1b4, 1701, 1704, nvt_ts->proc_fw_pen_state);
#undef NVT_PROC_ENTRY

	return 0;
}

void nvt_extra_proc_deinit(void)
{
	/* stock nvt_extra_proc_deinit @0xffffff8008ae0448: 12x
	 * remove_proc_entry(name, NULL) + "Removed /proc/%s" printk.
	 */
#define NVT_PROC_REMOVE(nm, store) \
	do { \
		if (store) { \
			remove_proc_entry((nm), NULL); \
			(store) = NULL; \
			pr_info("3[%s] %s %d: Removed /proc/%s \n", \
				"nvt_extra_proc_deinit", "nvt_extra_proc_deinit", \
				__LINE__, (nm)); \
		} \
	} while (0)

	NVT_PROC_REMOVE("nvt_fw_version", nvt_ts->proc_fw_version);
	NVT_PROC_REMOVE("nvt_baseline", nvt_ts->proc_baseline);
	NVT_PROC_REMOVE("nvt_raw", nvt_ts->proc_raw);
	NVT_PROC_REMOVE("nvt_diff", nvt_ts->proc_diff);
	NVT_PROC_REMOVE("gesture_control", nvt_ts->proc_gesture_control);
	NVT_PROC_REMOVE("nvt_fw_status", nvt_ts->proc_fw_status);
	NVT_PROC_REMOVE("panel_direction", nvt_ts->proc_edge_reject);
	NVT_PROC_REMOVE("edge_grid_zone", nvt_ts->proc_edge_grid_zone);
	NVT_PROC_REMOVE("game_mode", nvt_ts->proc_game_mode);
	NVT_PROC_REMOVE("support_pen", nvt_ts->proc_support_pen);
	NVT_PROC_REMOVE("fw_pen_state", nvt_ts->proc_fw_pen_state);
	NVT_PROC_REMOVE("nvt_pen_diff", nvt_ts->proc_pen_diff);
#undef NVT_PROC_REMOVE
}

irqreturn_t nvt_ts_work_func(int irq, void *dev_id)
{
	u8 point_data[0x6d] = { 0 }; /* D45: stock zeroes the frame; buf[0] is the tx register byte */
	u8 finger_state[10] = { 0 };
	u8 i;
	u8 checksum = 0;
	u32 id;
	u32 x;
	u32 y;
	u32 pressure;
	int count = 0;
	int ret;

	/* W1 (stock 0x8adb758-0x8adb77c): when hw_sleep_check is set, wait
	 * up to 175 jiffies (literal immediate in stock) for resume to complete; only a timeout (0) drops
	 * this IRQ. */
	if (nvt_ts->hw_sleep_check &&
	    wait_for_completion_timeout(&nvt_ts->pm_completion, 175) == 0) {
		pr_err("NVT-ts: nvt_ts_work_func: system(bus) can't finished resuming procedure, skip it\n");
		return IRQ_HANDLED;
	}
	if (!bTouchIsAwake)
		pm_wakeup_dev_event(&nvt_ts->client->dev, 5000, 0);

	mutex_lock(&nvt_ts->lock);
	ret = CTP_SPI_READ(nvt_ts->client, point_data, sizeof(point_data));
	if (ret < 0) {
		pr_err("NVT-ts: nvt_ts_work_func: CTP_SPI_READ failed.(%d)\n", ret);
		goto unlock;
	}
	nvt_x_error_count++;
	if ((u8)(point_data[1] + 3) < 2 &&
	    (u8)(point_data[2] + 3) < 2 && (u8)(point_data[3] + 3) < 2 &&
	    (u8)(point_data[4] + 3) < 2 && (u8)(point_data[5] + 3) < 2 &&
	    (u8)(point_data[6] + 3) < 2) {
		if (nvt_x_error_count > 11) {
			nvt_x_error_count = 0;
			pr_err("NVT-ts: nvt_ts_work_func: Recover for fw reset, %02X\n", point_data[1]);
			if (point_data[1] == 0xfe) {
				nvt_write_addr(nvt_ts->mmap[0], 0xaa);
				msleep(15);
			}
			nvt_read_fw_history(nvt_ts->mmap[29]);
			nvt_read_fw_history(nvt_ts->mmap[30]);
			nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
			mutex_unlock(&nvt_ts->lock);
			queue_work_on(8, nvt_fwu_wq, &nvt_restore_cmd_work);
			return IRQ_HANDLED;
		}
	} else {
		nvt_x_error_count = 0;
		if (point_data[1] == 0xfe && point_data[2] == 0xfe &&
		    point_data[3] == 0xfe && point_data[4] == 0xfe &&
		    point_data[5] == 0xfe && point_data[6] == 0xfe)
			goto unlock;
	}

	for (i = 0; i < 64; i++)
		checksum += point_data[1 + i];
	if ((u8)(-checksum) != point_data[0x41]) {
		pr_err("NVT-ts: nvt_ts_point_data_checksum: i2c/spi packet checksum not match. (point_data[%d]=0x%02X, checksum=0x%02X)\n",
		       0x41, point_data[0x41], checksum);
		goto unlock;
	}

	if (nvt_check_stylus_state(point_data[1] >> 3, point_data) == 0) {
		if (!bTouchIsAwake) {
			nvt_ts_wakeup_gesture_report(point_data[1] >> 3, point_data);
		} else {
			for (i = 0; i < nvt_ts->max_finger_num; i++) {
				id = point_data[1 + i * 6] >> 3;
				x = ((u32)point_data[2 + i * 6] << 8) |
				    point_data[3 + i * 6];
				y = ((u32)point_data[4 + i * 6] << 8) |
				    point_data[5 + i * 6];
				pressure = point_data[6 + i * 6];
				if ((u8)(id - 1) < nvt_ts->max_finger_num &&
				    (u8)((point_data[1 + i * 6] & 7) - 1) < 2 &&
				    x <= nvt_ts->abs_x_max * 10 &&
				    y <= nvt_ts->abs_y_max * 10) {
					finger_state[id - 1] = 1;
					input_mt_slot(nvt_ts->input_dev, id - 1);
					input_mt_report_slot_state(nvt_ts->input_dev,
								   MT_TOOL_FINGER, true);
					input_report_abs(nvt_ts->input_dev,
							 ABS_MT_POSITION_X, x);
					input_report_abs(nvt_ts->input_dev,
							 ABS_MT_POSITION_Y, y);
					input_report_abs(nvt_ts->input_dev,
							 ABS_MT_TOUCH_MAJOR,
							 pressure ? pressure : 1);
					input_report_abs(nvt_ts->input_dev,
						 ABS_MT_PRESSURE,
						 pressure ? pressure : 1);
					count++;
				}
			}
			for (i = 0; i < nvt_ts->max_finger_num; i++) {
				if (!finger_state[i]) {
					input_mt_slot(nvt_ts->input_dev, i);
					input_mt_report_slot_state(nvt_ts->input_dev,
								   MT_TOOL_FINGER, false);
					input_report_abs(nvt_ts->input_dev,
							 ABS_MT_TOUCH_MAJOR, 0);
				}
			}
			input_report_key(nvt_ts->input_dev, BTN_TOUCH, count > 0);
			input_sync(nvt_ts->input_dev);

			if (nvt_ts->pen_support) {
				u8 pen_id = point_data[0x42];
				/* TASK-081 S3 diagnostic (D118: demoted from
				 * pr_info - it fired on every touch IRQ at
				 * ~250Hz and the console sync cost was
				 * user-visible as sluggish desktop scroll.
				 * Data already captured in STATE-084
				 * sessions 44-46; re-enable via dynamic
				 * debug if ever needed again.) */
				pr_debug("NVT-ts: pen raw 42-4d: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
				       point_data[0x42], point_data[0x43], point_data[0x44],
				       point_data[0x45], point_data[0x46], point_data[0x47],
				       point_data[0x48], point_data[0x49], point_data[0x4a],
				       point_data[0x4b], point_data[0x4c], point_data[0x4d]);
				/* TASK-081 D94 (pen hover/jitter): stock pen
				 * fields are BIG-ENDIAN (work_func pseudo
				 * L242-250 byte-swap). Live capture proved
				 * it: pen X reads 0xa833 with the low byte
				 * pinned at 0x33 while the high byte glides
				 * a8-98-79-50-30-0f - the true smooth value
				 * is 0x33a8. Little-endian reading made the
				 * cursor jump around the screen. */
				u16 pen_x = ((u16)point_data[0x43] << 8) |
					    point_data[0x44];
				u16 pen_y = ((u16)point_data[0x45] << 8) |
					    point_data[0x46];
				/* TASK-081 D95: stock work_func 0x8adbc40-24 maps
				 * pressure to buf[0x48]<<8|buf[0x47] and the
				 * signed tilts to buf[0x49]/buf[0x4a] - the
				 * rebuild had pressure and tilt swapped, so
				 * the hover tilt stream keyed BTN_TOUCH down
				 * and the cursor drew while hovering. */
				u16 pen_pressure = ((u16)point_data[0x48] << 8) |
						   point_data[0x47];
				s8 pen_tilt_x = point_data[0x49];
				s8 pen_tilt_y = point_data[0x4a];
				u16 pen_distance = ((u16)point_data[0x4b] << 8) |
						   point_data[0x4c];
				u8 pen_buttons = point_data[0x4d];

				if (pen_id == 0xff) {
					input_report_abs(nvt_ts->pen_dev, ABS_X, 0);
					input_report_abs(nvt_ts->pen_dev, ABS_Y, 0);
					input_report_abs(nvt_ts->pen_dev, ABS_PRESSURE, 0);
					input_report_abs(nvt_ts->pen_dev, ABS_TILT_X, 0);
					input_report_abs(nvt_ts->pen_dev, ABS_TILT_Y, 0);
					input_report_abs(nvt_ts->pen_dev, ABS_DISTANCE, 0);
					input_report_key(nvt_ts->pen_dev, BTN_TOUCH, 0);
					input_report_key(nvt_ts->pen_dev, BTN_TOOL_PEN, 0);
					input_report_key(nvt_ts->pen_dev, BTN_STYLUS, 0);
					input_report_key(nvt_ts->pen_dev, BTN_STYLUS2, 0);
					nvt_lenovo_report(TS_NONE, 0, 0, 0, 0, 0);
				} else if (pen_id == 1) {
					input_report_abs(nvt_ts->pen_dev, ABS_X, pen_x);
					input_report_abs(nvt_ts->pen_dev, ABS_Y, pen_y);
					input_report_abs(nvt_ts->pen_dev, ABS_PRESSURE,
							 pen_pressure);
					input_report_abs(nvt_ts->pen_dev, ABS_TILT_X,
							 pen_tilt_x);
					input_report_abs(nvt_ts->pen_dev, ABS_TILT_Y,
							 pen_tilt_y);
					input_report_abs(nvt_ts->pen_dev, ABS_DISTANCE,
							 pen_distance);
					input_report_key(nvt_ts->pen_dev, BTN_TOUCH,
							 pen_pressure != 0);
					input_report_key(nvt_ts->pen_dev, BTN_TOOL_PEN,
							 pen_distance != 0 ||
							 pen_pressure != 0);
					input_report_key(nvt_ts->pen_dev, BTN_STYLUS,
							 pen_buttons & 1);
					input_report_key(nvt_ts->pen_dev, BTN_STYLUS2,
							 (pen_buttons >> 1) & 1);
					nvt_lenovo_report(TS_TOUCH, pen_x, pen_y,
							  (u8)pen_tilt_x, (u8)pen_tilt_y,
							  pen_pressure);
				} else if (pen_id != 0xf0) {
				pr_err("NVT-ts: nvt_ts_work_func: Unknown pen format id!\n");
					goto unlock;
				}
				input_sync(nvt_ts->pen_dev);
			}
		}
	}

unlock:
	mutex_unlock(&nvt_ts->lock);
	return IRQ_HANDLED;
}



/* ---- TASK-081 D90: /proc/android_touch/self_test, phase 1 ----
 * Stock nvt_selftest_open (0x8ae4060, 11744 bytes) runs the full factory
 * sequence: lock, update_firmware, nvt_mp_parse_dt (child node
 * novatek-mp-criteria-*: geometry + Short/Open/CC/Raw/Diff/Pen limits),
 * check_fw_reset_state(0xa1), clear_fw_status, change_mode(0x41),
 * check_fw_status, then per-test raw acquisitions each dumped through
 * nvt_save_rawdata_to_csv to /sdcard/Download/*.csv (7 files), finishing
 * with another update_firmware to restore the normal image.
 * Phase 1 implements parse_dt + hand-shake + CSV writer + the FW rawdata
 * main chain; the Short/Open/CC/Noise/Pen segments (stock pseudo lines
 * L300-L2134) are translated incrementally in later phases. */

/* MP criteria parsed from the novatek-mp-criteria-* DT child (stock
 * globals 0xa316890..0xa316924). */
#define NVT_MP_AIN_MAX 128
#define NVT_MP_LIMIT_MAX 512
static u32 nvt_mp_ic_x_cfg_size;
static u32 nvt_mp_ic_y_cfg_size;
static u32 nvt_mp_ic_key_cfg_size;
static u32 nvt_mp_x_channel;
static u32 nvt_mp_y_channel;
static u32 nvt_mp_ain_x[NVT_MP_AIN_MAX];
static u32 nvt_mp_ain_y[NVT_MP_AIN_MAX];
static u32 nvt_mp_ain_key[NVT_MP_AIN_MAX];
static s32 nvt_mp_limit_short_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_short_n[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_open_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_open_n[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_fw_raw_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_fw_raw_n[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_fw_cc_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_fw_cc_n[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_fw_diff_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_fw_diff_n[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_tipx_raw_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_tipx_raw_n[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_tipy_raw_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_tipy_raw_n[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_ringx_raw_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_ringx_raw_n[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_ringy_raw_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_ringy_raw_n[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_tipx_diff_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_tipx_diff_n[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_tipy_diff_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_tipy_diff_n[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_ringx_diff_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_ringx_diff_n[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_ringy_diff_p[NVT_MP_LIMIT_MAX];
static s32 nvt_mp_limit_pen_ringy_diff_n[NVT_MP_LIMIT_MAX];
static u32 nvt_mp_diff_test_frame;

/* stock nvt_mp_parse_u32 (0x8ae28d8): of_property_read_u32 wrapper. */
static int nvt_mp_parse_u32(struct device_node *child, const char *name,
			    u32 *out)
{
	int ret = of_property_read_u32(child, name, out);

	if (ret < 0)
		pr_err("NVT-ts: nvt_mp_parse_u32: %s miss, ret=%d\n", name, ret);
	else
		pr_info("NVT-ts: nvt_mp_parse_u32: %s=%d\n", name, *out);
	return ret < 0 ? -1 : 0;
}

/* stock nvt_mp_parse_ain (0x8ae2768): u32 array of `count` entries into
 * the AIN mapping buffer. */
static int nvt_mp_parse_ain(struct device_node *child, const char *name,
			    u32 *out, u32 count)
{
	int ret = of_property_read_u32_array(child, name, out, count);

	if (ret < 0) {
		pr_err("NVT-ts: nvt_mp_parse_ain: %s miss, ret=%d\n", name, ret);
		return -1;
	}
	pr_info("NVT-ts: nvt_mp_parse_ain: %s ok\n", name);
	return 0;
}

/* stock nvt_mp_parse_array (0x8ae2968): s32 limit array of x*y entries,
 * each logged through nvt_print_data_log_in_one_line. */
static int nvt_mp_parse_array(struct device_node *child, const char *name,
			      s32 *out, u32 count)
{
	int ret = of_property_read_u32_array(child, name, (u32 *)out, count);

	if (ret < 0) {
		pr_err("NVT-ts: nvt_mp_parse_array: %s miss, ret=%d\n", name, ret);
		return -1;
	}
	pr_info("NVT-ts: nvt_mp_parse_array: %s ok (%u entries)\n", name, count);
	return 0;
}

/* stock nvt_mp_parse_dt (0x8ae2c90): find the novatek-mp-criteria-*
 * child and pull the 4 geometry u32s + AIN maps + all limit arrays.
 * Any miss aborts the whole parse with -1 like stock. */
static int nvt_mp_parse_dt(struct device_node *parent, const char *name)
{
	struct device_node *child;
	u32 x_cfg, y_cfg;
	int ret;

	pr_info("NVT-ts: nvt_mp_parse_dt: looking for %s\n", name);
	for_each_child_of_node(parent, child) {
		if (of_node_name_eq(child, name))
			break;
	}
	if (!child) {
		pr_err("NVT-ts: nvt_mp_parse_dt: %s not found\n", name);
		return -1;
	}
	pr_info("NVT-ts: nvt_mp_parse_dt: found %s\n", name);

	if (nvt_mp_parse_u32(child, "IC_X_CFG_SIZE", &x_cfg) ||
	    nvt_mp_parse_u32(child, "IC_Y_CFG_SIZE", &y_cfg) ||
	    nvt_mp_parse_u32(child, "X_Channel", &nvt_mp_x_channel) ||
	    nvt_mp_parse_u32(child, "Y_Channel", &nvt_mp_y_channel))
		return -1;
	nvt_mp_ic_x_cfg_size = x_cfg;
	nvt_mp_ic_y_cfg_size = y_cfg;

	if (nvt_mp_parse_ain(child, "AIN_X", nvt_mp_ain_x,
			     min_t(u32, x_cfg, NVT_MP_AIN_MAX)) ||
	    nvt_mp_parse_ain(child, "AIN_Y", nvt_mp_ain_y,
			     min_t(u32, y_cfg, NVT_MP_AIN_MAX)))
		return -1;

	if (nvt_mp_parse_array(child, "PS_Config_Lmt_Short_Rawdata_P",
			       nvt_mp_limit_short_p,
			       min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel,
				     NVT_MP_LIMIT_MAX)) ||
	    	nvt_mp_parse_array(child, "PS_Config_Lmt_Short_Rawdata_N",
				nvt_mp_limit_short_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_Open_Rawdata_P",
				nvt_mp_limit_open_p,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_Open_Rawdata_N",
				nvt_mp_limit_open_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_FW_Rawdata_P",
				nvt_mp_limit_fw_raw_p,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_FW_Rawdata_N",
				nvt_mp_limit_fw_raw_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_FW_CC_P",
				nvt_mp_limit_fw_cc_p,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_FW_CC_N",
				nvt_mp_limit_fw_cc_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_FW_Diff_P",
				nvt_mp_limit_fw_diff_p,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_FW_Diff_N",
				nvt_mp_limit_fw_diff_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenTipX_FW_Raw_P",
				nvt_mp_limit_pen_tipx_raw_p,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenTipX_FW_Raw_N",
				nvt_mp_limit_pen_tipx_raw_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenTipY_FW_Raw_P",
				nvt_mp_limit_pen_tipy_raw_p,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenTipY_FW_Raw_N",
				nvt_mp_limit_pen_tipy_raw_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenRingX_FW_Raw_P",
				nvt_mp_limit_pen_ringx_raw_p,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenRingX_FW_Raw_N",
				nvt_mp_limit_pen_ringx_raw_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenRingY_FW_Raw_P",
				nvt_mp_limit_pen_ringy_raw_p,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenRingY_FW_Raw_N",
				nvt_mp_limit_pen_ringy_raw_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenTipX_FW_Diff_P",
				nvt_mp_limit_pen_tipx_diff_p,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenTipX_FW_Diff_N",
				nvt_mp_limit_pen_tipx_diff_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenTipY_FW_Diff_P",
				nvt_mp_limit_pen_tipy_diff_p,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenTipY_FW_Diff_N",
				nvt_mp_limit_pen_tipy_diff_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenRingX_FW_Diff_P",
				nvt_mp_limit_pen_ringx_diff_p,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenRingX_FW_Diff_N",
				nvt_mp_limit_pen_ringx_diff_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenRingY_FW_Diff_P",
				nvt_mp_limit_pen_ringy_diff_p,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)) ||
	    nvt_mp_parse_array(child, "PS_Config_Lmt_PenRingY_FW_Diff_N",
				nvt_mp_limit_pen_ringy_diff_n,
				min_t(u32, nvt_mp_x_channel * nvt_mp_y_channel, \
						NVT_MP_LIMIT_MAX)))
		return -1;

	nvt_mp_parse_u32(child, "PS_Config_Diff_Test_Frame",
			  &nvt_mp_diff_test_frame);

	return 0;
}

/* stock nvt_polling_hand_shake_status (0x8ae71f0): sleep 20ms then poll
 * event buf handshake 250 times (10ms apart) for the 0xa0-completion,
 * logging fw history on timeout. */
static int nvt_polling_hand_shake_status(void)
{
	u8 buf[4] = { 0 };
	int i;

	usleep_range(20000, 20000);
	for (i = 0; i < 250; i++) {
		nvt_set_page(0x51);
		buf[0] = 0x00;
		CTP_SPI_READ(nvt_ts->client, buf, 2);
		if ((buf[1] & 0xfe) == 0xa0)
			return 0;
		usleep_range(10000, 10000);
	}
	pr_err("NVT-ts: nvt_polling_hand_shake_status: handshake timeout 0x%02x\n",
	       buf[1]);
	return -1;
}

/* ---- nvt_selftest fops placeholder (stock nvt_selftest_fops@0x9814d48) ---- */
/* stock nvt_save_rawdata_to_csv (0x8ae6e40): build a comma-separated grid
 * (x_num columns of %7d, CRLF rows) into a kmalloc buffer, then write it
 * to the stock path via filp_open/O_CREAT/O_TRUNC + kernel_write. */
static int nvt_save_rawdata_to_csv(const s32 *data, u8 x_num, u8 y_num,
				   const char *path, u32 flags)
{
	struct file *fp;
	char *buf;
	u32 total = (u32)x_num * y_num;
	u32 row_cap = (u32)x_num * 7 + 2;
	u32 cap = row_cap * y_num + 1;
	u32 off = 0, i, x;
	loff_t pos = 0;
	int ret;

	pr_info("NVT-ts: nvt_save_rawdata_to_csv: %s++\n", __func__);
	buf = kmalloc(cap, GFP_KERNEL);
	if (!buf) {
		pr_err("NVT-ts: nvt_save_rawdata_to_csv: kmalloc failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < total; i++) {
		int n = scnprintf(buf + off, cap - off, "%7d,", data[i]);
		off += n;
		if ((i % x_num) == x_num - 1) {
			buf[off++] = 0x0d;
			buf[off++] = 0x0a;
		}
	}
	buf[off] = 0;

	fp = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(fp)) {
		ret = PTR_ERR(fp);
		pr_err("NVT-ts: nvt_save_rawdata_to_csv: open %s failed %d\n",
		       path, ret);
		kfree(buf);
		return ret;
	}
	ret = kernel_write(fp, buf, off, &pos);
	if (ret < 0)
		pr_err("NVT-ts: nvt_save_rawdata_to_csv: write failed %d\n", ret);
	filp_close(fp, NULL);
	kfree(buf);
	pr_info("NVT-ts: nvt_save_rawdata_to_csv: %s--\n", __func__);
	return ret < 0 ? ret : 0;
}

#define NVT_CSV_FW_RAWDATA "/sdcard/Download/FWRawdataTest.csv"

/* stock nvt_selftest_open (0x8ae4060) phase-1 main chain: lock, probe,
 * refresh firmware, parse MP criteria, reset(0xa1), clear+mode 0x41,
 * read the FW rawdata grid, dump FWRawdataTest.csv, restore firmware.
 * Short/Open/CC/Noise/Pen segments (stock pseudo L300-L2134) follow in
 * phase 2. */

static int nvt_selftest_run(void)
{
	int ret;
	s32 *md = NULL;
	u8 x_num, y_num;

	pr_info("NVT-ts: nvt_selftest_run: ++\n");

	mutex_lock(&nvt_ts->lock);

	ret = nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
	if (ret < 0) {
		pr_err("NVT-ts: nvt_selftest_run: update_firmware failed %d\n", ret);
		goto out_lock;
	}

	ret = nvt_get_fw_info();
	if (ret < 0)
		goto out_lock;

	ret = nvt_mp_parse_dt(nvt_ts->client->dev.of_node,
			      "novatek-mp-criteria-6067");
	if (ret != 0) {
		pr_err("NVT-ts: nvt_selftest_run: mp_parse_dt failed\n");
		nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
		goto out_lock;
	}

	if (nvt_check_fw_reset_state(NVT_FW_INIT_RESET_STATE))
		goto out_lock;

	if (nvt_clear_fw_status())
		goto out_lock;

	ret = nvt_change_mode(0x41 /* stock selftest rawdata mode */);
	if (ret < 0)
		goto out_lock;

	if (nvt_check_fw_status())
		goto out_lock;

	x_num = nvt_ts->x_num;
	y_num = nvt_ts->y_num;
	md = kcalloc((u32)x_num * y_num, sizeof(s32), GFP_KERNEL);
	if (!md) {
		ret = -ENOMEM;
		goto out_lock;
	}

	nvt_read_mdata(NVT_MM->BASELINE_ADDR); /* stock: mmap[0]+0xc */
	nvt_get_mdata(md, &x_num, &y_num);

	ret = nvt_save_rawdata_to_csv(md, x_num, y_num,
				      NVT_CSV_FW_RAWDATA, 0);
	if (ret < 0)
		pr_err("NVT-ts: nvt_selftest_run: save csv failed %d\n", ret);

	/* phase 2 (stock pseudo L300-L2134): Short/Open/CC/Noise/Pen
	 * segments with their own change_mode/hand-shake rounds. */

	nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);

out_lock:
	kfree(md);
	mutex_unlock(&nvt_ts->lock);
	pr_info("NVT-ts: nvt_selftest_run: -- ret=%d\n", ret);
	return ret;
}

static int nvt_selftest_show(struct seq_file *m, void *v)
{
	int ret = nvt_selftest_run();

	seq_printf(m, "%d\n", ret);
	return 0;
}

static int nvt_selftest_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvt_selftest_show, NULL);
}

static const struct file_operations nvt_selftest_fops = {
	.owner = THIS_MODULE,
	.open = nvt_selftest_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* ---- nvt_mp_proc_init / nvt_mp_proc_deinit (stock @0x8ae3448/0x8ae3cf8) ---- */
int nvt_mp_proc_init(void)
{
	struct proc_dir_entry *dir;
	struct proc_dir_entry *e;

	/* stock nvt_mp_proc_init @0xffffff8008ae3448:
	 * proc_mkdir("android_touch", NULL) + proc_create("self_test", 0x124,
	 * dir, nvt_selftest_fops). Nothing else is registered here; the 12
	 * extra nodes belong to nvt_extra_proc_init (top level).
	 */
	dir = proc_mkdir("android_touch", NULL);
	if (!dir) {
		pr_err("3[%s] %s %d: create proc/%s Failed!\n", "nvt_mp_proc_init", "nvt_mp_proc_init", 2728, "android_touch");
		return -ENOMEM;
	}
	nvt_ts->proc_nvt_dir = dir;

	e = proc_create("self_test", 0x124, dir, &nvt_selftest_fops);
	if (!e) {
		pr_err("3[%s] %s %d: create proc/%s Failed!\n", "nvt_mp_proc_init", "nvt_mp_proc_init", 2733, "self_test");
		remove_proc_entry("android_touch", NULL);
		nvt_ts->proc_nvt_dir = NULL;
		return -ENOMEM;
	}
	nvt_ts->proc_self_test = e;

	pr_info("3[%s] %s %d: create proc/%s Succeeded!\n", "nvt_mp_proc_init", "nvt_mp_proc_init", 2741, "self_test");
	return 0;
}

void nvt_mp_proc_deinit(void)
{
	if (nvt_ts->proc_self_test) {
		proc_remove(nvt_ts->proc_self_test);
		nvt_ts->proc_self_test = NULL;
	}
	if (nvt_ts->proc_nvt_dir) {
		proc_remove(nvt_ts->proc_nvt_dir);
		nvt_ts->proc_nvt_dir = NULL;
	}
}

/* ---- nvt_ts_probe ---- */


static int nvt_ts_probe(struct spi_device *client)
{
	struct nvt_ts_data *ts;
	int ret;
	int i, j;

	if (nvt_ts) {
		dev_err(&client->dev, "NVT-ts: already probed\n");
		return -EBUSY;
	}

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;
	nvt_ts = ts;
	ts->client = client;
	spi_set_drvdata(client, ts);

	ts->xbuf = devm_kzalloc(&client->dev, NVT_XBUF_LEN, GFP_KERNEL);
	if (!ts->xbuf)
		return -ENOMEM;

	ts->rbuf = devm_kzalloc(&client->dev, NVT_READ_LEN, GFP_KERNEL);
	if (!ts->rbuf)
		return -ENOMEM;

	/* D119: stock never overrides max_speed_hz (spi_setup @0x8ada010
	 * has no speed constant - the DT spi-max-frequency 46MHz stands,
	 * verified /proc/device-tree 0x02BEE680). Our 6MHz override made
	 * every SPI transfer 7.6x slower: firmware download dominated the
	 * 1.46s resume window -> sluggish scroll after each screen-on. */
	client->mode = SPI_MODE_0;
	client->bits_per_word = 8;
	if (spi_setup(client))
		return -EINVAL;

	/* GPIOs (stock: of_get_named_gpio_flags + gpio_request_one) */
	ret = of_get_named_gpio_flags(client->dev.of_node,
				      "novatek,reset-gpio", 0, NULL);
	if (ret < 0) {
		dev_err(&client->dev, "NVT-ts: get reset gpio failed %d\n", ret);
		return ret;
	}
	ts->reset_gpio_num = ret;
	ret = devm_gpio_request_one(&client->dev, ts->reset_gpio_num,
				    GPIOF_OUT_INIT_LOW, "NVT-tp-rst");
	if (ret) {
		dev_err(&client->dev, "NVT-ts: get reset gpio failed %d\n", ret);
		return ret;
	}
	ret = of_get_named_gpio_flags(client->dev.of_node,
				      "novatek,irq-gpio", 0, NULL);
	if (ret < 0) {
		dev_err(&client->dev, "NVT-ts: get irq gpio failed %d\n", ret);
		return ret;
	}
	ts->irq_gpio_num = ret;
	ret = devm_gpio_request_one(&client->dev, ts->irq_gpio_num,
				    GPIOF_IN, "NVT-tp-irq");
	if (ret) {
		dev_err(&client->dev, "NVT-ts: request irq gpio failed %d\n", ret);
		return ret;
	}
	ts->irq = gpio_to_irq(ts->irq_gpio_num);
	/* stock nvt_ts_probe @0xffffff8008ada7a8/0x8ada7b8:
	 * ts->int_trigger_type = 1 (IRQ_TYPE_EDGE_RISING), a constant --
	 * request_threaded_irq flags are that value | IRQF_ONESHOT
	 * (0x8adaa7c orr x3, x9, #0x2000). NOT irq_get_trigger_type(client->irq):
	 * an SPI device has no client->irq, so that returned 0/garbage.
	 */
	ts->int_trigger_type = IRQ_TYPE_EDGE_RISING;

	/* TASK-081 D92 (pen regression root cause): stock probe 0x8ada120-38
	 * sets ts->pen_support (struct+0x159) via of_find_property on
	 * novatek,pen-support (shipping DT carries it as a boolean) and
	 * ts+0x15a via novatek,stylus-resol-double. Our rebuild never
	 * assigned pen_support, so devm-kzalloc left it 0 and the
	 * NVTCapacitivePen input device was never registered - pen tip
	 * sliding had no input path while the pen buttons kept working over
	 * the pen bluetooth HID. */
	ts->pen_support = of_property_read_bool(client->dev.of_node,
			"novatek,pen-support");
	ts->stylus_resol_double = of_property_read_bool(client->dev.of_node,
			"novatek,stylus-resol-double");
	pr_info("NVT-ts: pen_support=%d stylus_resol_double=%d\n",
	       ts->pen_support, ts->stylus_resol_double);

	mutex_init(&ts->lock);
	mutex_init(&ts->xbuf_lock);
	/* stock probe 0xffffff8008ad9fc4: __init_waitqueue_head(ts+0x1a0) and
	 * done=0 at ts+0x198. Our pm_wq existed but was never initialized. */
	init_waitqueue_head(&ts->pm_wq);
	init_completion(&ts->pm_completion);

	/* eng reset, reset high, sleep 10ms, identify */
	nvt_eng_reset();
	gpio_set_value(ts->reset_gpio_num, 1);
	msleep(10);

	for (i = 0; i < 3; i++) {
		const u32 *candidates[3];
		candidates[0] = nvt_hw_reg_addr_info;
		candidates[1] = nvt_hw_reg_addr_info_old_w_isp;
		candidates[2] = nvt_hw_reg_addr_info_legacy_w_isp;
		for (j = 0; j < 5; j++) {
			u8 rbuf[7] = { 0 };
			const struct nvt_cfg_entry *cfg;
			const u32 *mmap_ptr;
			u32 idx;

			ts->chip_ver_trim_addr = candidates[i][0];
			ts->swrst_sif_addr = candidates[i][0];
			ts->crc_err_flag_addr = candidates[i][1];
			pr_info("NVT-ts: nvt_ts_check_chip_ver_trim: check chip ver trim with chip_ver_trim_addr=0x%06x, swrst_sif_addr=0x%06x, crc_err_flag_addr=0x%06x\n",
				ts->chip_ver_trim_addr, ts->swrst_sif_addr,
				ts->crc_err_flag_addr);
			nvt_bootloader_reset();
			pr_info("NVT-ts: nvt_bootloader_reset: end\n");
			nvt_set_page(ts->swrst_sif_addr);
			rbuf[0] = ts->swrst_sif_addr & 0x7f;
			CTP_SPI_READ(client, rbuf, 7);
			pr_info("NVT-ts: nvt_ts_check_chip_ver_trim: buf[1]=0x%02X, buf[2]=0x%02X, buf[3]=0x%02X, buf[4]=0x%02X, buf[5]=0x%02X, buf[6]=0x%02X\n",
				rbuf[1], rbuf[2], rbuf[3], rbuf[4], rbuf[5], rbuf[6]);
			for (idx = 0; idx < ARRAY_SIZE(trim_id_table); idx++) {
				/* stock: sp+33/34/36/37/38 = buf idx1,2,4,5,6
				 * compare: id0(idx1, skip-mask), id1(idx2, 0x1a..0x1d only),
				 * id3(idx4), id4(idx5), id5(idx6). */
				u64 skip = 0x0fc001a00dULL;
				u8 d0 = 0xff;

				if (!((skip >> idx) & 1))
					d0 = rbuf[1];
				if (d0 != trim_id_table[idx].id[0])
					continue;
				if (idx >= 0x1a && idx <= 0x1d &&
				    rbuf[2] != trim_id_table[idx].id[1])
					continue;
				if (rbuf[4] != trim_id_table[idx].id[3] ||
				    rbuf[5] != trim_id_table[idx].id[4] ||
				    rbuf[6] != trim_id_table[idx].id[5])
					continue;
				break;
			}
			if (idx == ARRAY_SIZE(trim_id_table))
				continue;
			pr_info("NVT-ts: nvt_ts_check_chip_ver_trim: This is NVT touch IC\n");
			mmap_ptr = nvt_mmap_tables[trim_id_table[idx].mmap_idx];
			if (trim_id_table[idx].cascade_idx != 0xff &&
			    mmap_ptr[27] != 0) {
				u8 casc = 0;

				ret = nvt_read_reg(((u64)mmap_ptr[28] << 32) |
						   mmap_ptr[27], &casc);
				if (ret == 0 && casc) {
					pr_info("NVT-ts: nvt_ts_check_chip_ver_trim: Single Chip\n");
					ts->mmap = mmap_ptr;
				} else {
					pr_info("NVT-ts: nvt_ts_check_chip_ver_trim: Cascade Chip\n");
					ts->mmap = nvt_mmap_tables[trim_id_table[idx].cascade_idx];
				}
			} else {
				pr_info("NVT-ts: nvt_ts_check_chip_ver_trim: Single Chip\n");
				ts->mmap = mmap_ptr;
			}
			cfg = &nvt_cfg_table[trim_id_table[idx].cfg_idx];
			/* The nvt_hw_reg_addr_info table is {chip_ver_trim, swrst_sif,
			 * crc_err_flag}; chip_ver_trim_slot / crc_err_flag_slot are
			 * indices into it, not addresses. For this IC stock (and the
			 * working driver) logs
			 *   chip_ver_trim_addr=0x1fb104, swrst_sif_addr=0x1fb43e,
			 *   crc_err_flag_addr=0x1fb535
			 * i.e. entries [0], [1] and [2].
			 */
			ts->chip_ver_trim_addr = cfg->swrst_sif_addr[0];
			ts->swrst_sif_addr = cfg->swrst_sif_addr[1];
			ts->crc_err_flag_addr = cfg->swrst_sif_addr[2];
			pr_info("NVT-ts: nvt_ts_check_chip_ver_trim: set reg chip_ver_trim_addr=0x%06x, swrst_sif_addr=0x%06x, crc_err_flag_addr=0x%06x\n",
				ts->chip_ver_trim_addr, ts->swrst_sif_addr,
				ts->crc_err_flag_addr);
			pr_info("NVT-ts: nvt_ts_check_chip_ver_trim: trim_id=%d\n",
				idx);
			/* stock probe @0x8ada708..0x8ada734 copies the per-candidate
			 * hw_crc and auto_copy values from the trim entry. hw_crc
			 * selects the CRC bank width / download branch; auto_copy
			 * selects the tx auto-copy handshake.
			 */
			ts->auto_copy = trim_id_table[idx].auto_copy;
			/* stock ts+0x120 doubles as hw_crc: trim 0 -> 2 =
			 * HWCRC_LEN_3Bytes, selects the hw-crc download path
			 * and gates the DLM CRC-bank length write.
			 */
			ts->hw_crc = trim_id_table[idx].hw_crc;
			break;
		}
		if (ts->mmap)
			break;
	}
	if (!ts->mmap) {
		dev_err(&client->dev, "NVT-ts: chip is not identified\n");
		return -ENODEV;
	}

	ts->abs_x_max = 0x730;
	ts->abs_y_max = 0xb80;
	ts->max_finger_num = 10;

	ts->input_dev = devm_input_allocate_device(&client->dev);
	if (!ts->input_dev)
		return -ENOMEM;
	ts->input_dev->name = "NVTCapacitiveTouchScreen";
	/* D48: old-tree source sets phys = "input/ts" (main-nt36xxx.c:2659);
	 * EventHub exposes it as the device location, which Android's
	 * InputReader uses to associate the internal touchscreen with the
	 * built-in display. Without it the TouchInputMapper stays DISABLED. */
	sprintf(nvt_ts->phys, "input/ts");
	ts->input_dev->phys = nvt_ts->phys;
	ts->input_dev->id.bustype = BUS_SPI;
	ts->input_dev->event = nvt_gesture_switch;
	input_set_drvdata(ts->input_dev, ts);
	input_mt_init_slots(ts->input_dev, ts->max_finger_num, 0);
	/* D46: register BTN_TOUCH like the factory probe does
	 * (str 0x400 -> input_dev keybit[5] BIT(10) = 0x14a) and the
	 * old-tree source (keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK).
	 * input_mt_init_slots(flags=0) does NOT set it; the input core
	 * drops unregistered keys, so Android never saw BTN_TOUCH and
	 * ignored all MT events -> "touch no effect".
	 */
	ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	/* D50: factory probe sets propbit = 2 = INPUT_PROP_DIRECT
	 * (str 2 -> input_dev+0x20). Android's TouchInputMapper picks
	 * DeviceType by this property: DIRECT -> TOUCH_SCREEN (absolute),
	 * otherwise POINTER (relative cursor) - which is why the touch
	 * showed up as a mouse pointer with scrambled coordinates. */
	__set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
	input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, 1, 0, 0);
	input_set_capability(ts->input_dev, EV_ABS, ABS_MT_SLOT);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X,
			     0, ts->abs_x_max * 10, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y,
			     0, ts->abs_y_max * 10, 0, 0);
	input_set_capability(ts->input_dev, EV_KEY, 0x25e);
	/* D101 (F4): stock probe (0x8ada894-0x8ada994) issues 12 consecutive
	 * input_set_capability calls with w2 = w20 = 0x74 (KEY_POWER) - the
	 * SAME key 12 times (idempotent), not 14 distinct keys 0x74..0x82.
	 * Unique finger_dev caps: 0x25e + KEY_POWER + 0x20b. Registering the
	 * 13 extra keys skewed the InputReader device class. No reporter
	 * depends on 0x75..0x82: gesture wake reports KEY_POWER on
	 * wake_input_dev (D67), stylus press reports 0x25e on finger_dev. */
	input_set_capability(ts->input_dev, EV_KEY, KEY_POWER);
	input_set_capability(ts->input_dev, EV_KEY, 0x20b);

	ret = input_register_device(ts->input_dev);
	if (ret)
		return ret;

	/* pen_dev (optional) */
	if (nvt_ts->pen_support) {
		ts->pen_dev = devm_input_allocate_device(&client->dev);
		if (!ts->pen_dev)
			return -ENOMEM;
		ts->pen_dev->name = "NVTCapacitivePen";
		ts->pen_dev->id.bustype = BUS_SPI;
		/* D100 (F1): stock probe (0x8adaa2c-0x8adad78) sets
		 * pen_dev->phys = "input/pen" (inline str/strh into ts+0x168)
		 * and propbit word = 2 (bit1 = INPUT_PROP_DIRECT). Without
		 * DIRECT, EventHub classifies the pen as a POINTER device
		 * (mouse semantics) - hover lost, motion scrambled. */
		sprintf(nvt_ts->pen_phys, "input/pen");
		ts->pen_dev->phys = nvt_ts->pen_phys;
		__set_bit(INPUT_PROP_DIRECT, ts->pen_dev->propbit);
		input_set_drvdata(ts->pen_dev, ts);
		input_set_abs_params(ts->pen_dev, ABS_X, 0,
				     ts->abs_x_max * 10, 0, 0);
		input_set_abs_params(ts->pen_dev, ABS_Y, 0,
				     ts->abs_y_max * 10, 0, 0);
		input_set_abs_params(ts->pen_dev, ABS_PRESSURE, 0, 0xfff, 0, 0);
		input_set_abs_params(ts->pen_dev, ABS_TILT_X, -60, 60, 0, 0);
		input_set_abs_params(ts->pen_dev, ABS_TILT_Y, -60, 60, 0, 0);
		input_set_abs_params(ts->pen_dev, ABS_DISTANCE, 0, 1, 0, 0);
		input_set_capability(ts->pen_dev, EV_KEY, BTN_TOUCH);
		input_set_capability(ts->pen_dev, EV_KEY, BTN_TOOL_PEN);
		input_set_capability(ts->pen_dev, EV_KEY, BTN_STYLUS);
		input_set_capability(ts->pen_dev, EV_KEY, BTN_STYLUS2);
		/* D100 (F1): stock pen_dev keybit[5] = 0x400|0x1|0x800|0x1000
		 * = BTN_TOUCH/BTN_TOOL_PEN/BTN_STYLUS/BTN_STYLUS2, and that is
		 * the complete set. No BTN_TOOL_RUBBER, no 0x25e (stylus press
		 * is reported only through finger_dev), no 0x20b. */
		ret = input_register_device(ts->pen_dev);
		if (ret)
			return ret;
	}

	device_init_wakeup(&client->dev, 1);

	/* D67/p146: dedicated wake keyboard device (old-tree TASK-022).
	 * A plain KEY_WAKEUP keyboard is treated as wake-capable by the
	 * framework (like gpio-keys), unlike the touch panel device. */
	nvt_ts->wake_input_dev = devm_input_allocate_device(&client->dev);
	if (!nvt_ts->wake_input_dev) {
		dev_err(&client->dev, "NVT-ts: allocate wake input device failed\n");
		return -ENOMEM;
	}
	nvt_ts->wake_input_dev->name = "nvt_wake_key";
	nvt_ts->wake_input_dev->id.bustype = BUS_SPI;
	input_set_capability(nvt_ts->wake_input_dev, EV_KEY, KEY_WAKEUP);
	input_set_capability(nvt_ts->wake_input_dev, EV_KEY, KEY_POWER);
	ret = input_register_device(nvt_ts->wake_input_dev);
	if (ret) {
		dev_err(&client->dev, "NVT-ts: register wake input device failed. ret=%d\n", ret);
		return ret;
	}

	/* firmware name by panel vendor */
	if (gpio_82 == 1)
		BOOT_UPDATE_FIRMWARE_NAME = "novatek_ts_tm_fw_6.bin";
	else
		BOOT_UPDATE_FIRMWARE_NAME = "novatek_ts_fw_boe_6.bin";

	nvt_charger_init();

	/* fwu work (stock order: hall timer+init -> fwu wq -> queue_delayed -> hall wq -> flash proc -> extra -> mp -> panel -> dt2w/enable_irq) */
	INIT_DELAYED_WORK(&nvt_hall_check_work, nvt_hall_check_func);

	/* D102 (F5): stock probe 0x8adae68-0x8adae80 is alloc_workqueue(
	 * "nvt_fwu_wq", 0xa, 1) - 0xa = WQ_UNBOUND(0x2) | WQ_MEM_RECLAIM(0x8).
	 * Our WQ_FREEZABLE(0x4) was wrong (from the old community tree);
	 * MEM_RECLAIM gives the fwu work a rescue worker under OOM. */
	nvt_fwu_wq = alloc_workqueue("nvt_fwu_wq",
					 WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!nvt_fwu_wq) {
		pr_err("3[%s] %s %d: nvt_fwu_wq create workqueue failed\n", "nvt_ts_probe", "nvt_ts_probe", 2673);
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&ts->nvt_fwu_work, nvt_boot_update_firmware_work);
	queue_delayed_work_on(8, nvt_fwu_wq, &ts->nvt_fwu_work,
			      msecs_to_jiffies(1500));

	INIT_DELAYED_WORK(&nvt_hall_check_work, nvt_hall_check_func);
	/* stock flags 0x40008 = __WQ_LEGACY|WQ_MEM_RECLAIM == create_workqueue */
	nvt_hall_check_wq = create_workqueue("nvt_hall_check_wq");
	if (!nvt_hall_check_wq) {
		pr_err("3[%s] %s %d: nvt_hall_check_wqsd_check_wq create workqueue failed\n", "nvt_ts_probe", "nvt_ts_probe", 2700);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
		return -ENOMEM;
	}

	/* stock: /proc/NVT-ts-flash (nvt_flash_fops) */
	NVT_proc_entry = proc_create("NVTSPI", 0x124, NULL, &nvt_flash_fops);
	if (!NVT_proc_entry) {
		pr_err("3[%s] %s %d: Failed!\n", "nvt_flash_proc_init", "nvt_flash_proc_init", 1058);
		pr_err("3[%s] %s %d: nvt flash proc init failed. ret=%d\n", "nvt_ts_probe", "nvt_ts_probe", 2710, -ENOMEM);
		destroy_workqueue(nvt_hall_check_wq);
		nvt_hall_check_wq = NULL;
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
		return -ENOMEM;
	}
	pr_info("3[%s] %s %d: Succeeded!\n", "nvt_flash_proc_init", "nvt_flash_proc_init", 1061);
	pr_info("3[%s] %s %d: ============================================================\n", "nvt_flash_proc_init", "nvt_flash_proc_init", 1064);
	pr_info("3[%s] %s %d: Create /proc/%s\n", "nvt_flash_proc_init", "nvt_flash_proc_init", 1065, "NVTSPI");
	pr_info("3[%s] %s %d: ============================================================\n", "nvt_flash_proc_init", "nvt_flash_proc_init", 1066);

	ret = nvt_extra_proc_init();
	if (ret) {
		pr_err("3[%s] %s %d: nvt extra proc init failed. ret=%d\n", "nvt_ts_probe", "nvt_ts_probe", 2718, ret);
		goto err_extra_proc;
	}
	ret = nvt_mp_proc_init();
	if (ret) {
		pr_err("3[%s] %s %d: nvt mp proc init failed. ret=%d\n", "nvt_ts_probe", "nvt_ts_probe", 2726, ret);
		goto err_mp_proc;
	}

	/* panel notifier (stock: ts+0xC0 nb, active_panel global) */
	ts->drm_nb.notifier_call = nvt_drm_panel_notifier_callback;
	/* D55 (root cause): every other touchscreen driver here (fts,
	 * synaptics, focaltech) keeps its own `static` active_panel and
	 * resolves the panel itself via of_drm_find_panel on its DT
	 * "panel" phandle. NVT must do the same - the extern never
	 * pointed at a panel that was actually registered, so the blank
	 * events never reached the NVT callback and the IC never got the
	 * suspend/resume commands (fw died at every screen-off). */
	{
		struct device_node *np = client->dev.of_node;
		struct device_node *pnode;
		struct drm_panel *panel;
		int cnt = of_count_phandle_with_args(np, "panel", NULL);
		int k;

		for (k = 0; k < cnt; k++) {
			pnode = of_parse_phandle(np, "panel", k);
			panel = of_drm_find_panel(pnode);
			of_node_put(pnode);
			if (!IS_ERR(panel)) {
				active_panel = panel;
				break;
			}
		}
	}
	pr_info("NVT-ts: probe panel-check: active_panel=%p\n", active_panel);
	if (active_panel) {
		ret = drm_panel_notifier_register(active_panel, &ts->drm_nb);
		if (ret < 0) {
			pr_err("3[%s] %s %d: register drm_panel_notifier failed. ret=%d\n", "nvt_ts_probe", "nvt_ts_probe", 2736, ret);
			nvt_mp_proc_deinit();
			goto err_mp_proc;
		}
	} else {
		pr_err("NVT-ts: panel not ready yet, will retry via fwu work\n");
	}

	/* stock probe @0xffffff8008adaa74..0x8adaa9c:
	 * irq_enabled=1 -> request_threaded_irq("NVT-ts") -> disable_irq()
	 * -> irq_enabled=0. The paired disable is what keeps the success-tail
	 * enable_irq() balanced (missing it = "Unbalanced enable for IRQ"
	 * WARN at kernel/irq/manage.c:621 in probe context, #360 boot).
	 */
	ts->irq_enabled = 1;
	ret = request_threaded_irq(ts->irq, NULL, nvt_ts_work_func,
				   ts->int_trigger_type | IRQF_ONESHOT,
				   "NVT-ts", ts);
	if (ret) {
		pr_err("NVT-ts: request irq failed %d\n", ret);
		ts->irq_enabled = 0;
		goto err_mp_proc;
	}
	disable_irq(ts->irq);
	ts->irq_enabled = 0;
	{
		struct irq_desc *d = irq_to_desc(ts->irq);

		pr_info("3[%s] %s %d: enable=%d, desc->depth=%d\n", "nvt_irq_enable", "nvt_irq_enable", 230, 0, d ? d->depth : -1);
	}

	/* stock success tail */
	ts->nfc_state = 0;
	bTouchIsAwake = 1;
	ts->edge_reject = 0;
	INIT_WORK(&nvt_restore_cmd_work, nvt_restore_cmd_func);
	ts->restore_pending = 0;
	check_tp_state(1);
	pr_info("3[%s] %s %d: end\n", "nvt_ts_probe", "nvt_ts_probe", 2782);
	if (!ts->irq_enabled) {
		enable_irq(ts->irq);
		ts->irq_enabled = 1;
	}
	{
		struct irq_desc *d = irq_to_desc(ts->irq);
		pr_info("3[%s] %s %d: enable=%d, desc->depth=%d\n", "nvt_irq_enable", "nvt_irq_enable", 230, 1, d ? d->depth : -1);
	}
	return 0;

err_mp_proc:
	nvt_extra_proc_deinit();
err_extra_proc:
	if (NVT_proc_entry) {
		remove_proc_entry("NVTSPI", NULL);
		NVT_proc_entry = NULL;
		pr_info("3[%s] %s %d: nvt flash proc init failed. ret=%d\n", "nvt_ts_remove", "nvt_ts_remove", 1083, -ENOMEM);
	}
	if (nvt_hall_check_wq) {
		cancel_delayed_work_sync(&nvt_hall_check_work);
		destroy_workqueue(nvt_hall_check_wq);
		nvt_hall_check_wq = NULL;
	}
	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&ts->nvt_fwu_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
	if (ts->charger_wq) {
		cancel_work_sync(&ts->charger_work);
		destroy_workqueue(ts->charger_wq);
		ts->charger_wq = NULL;
	}
	power_supply_unreg_notifier(&ts->psy_nb);
	free_irq(ts->irq, ts);
	if (ts->pen_support && ts->pen_dev) {
		input_unregister_device(ts->pen_dev);
		ts->pen_dev = NULL;
	}
	if (ts->input_dev) {
		input_unregister_device(ts->input_dev);
		ts->input_dev = NULL;
	}
	return ret;
}

/* ---- nvt_ts_remove (stock @0xffffff8008adb398, TASK-081) ----
 * ts/xbuf/rbuf are devm-managed in this rebuild (stock kmalloc + kfree)
 * and the GPIOs are devm-requested (stock gpio_free), so those teardown
 * steps are covered by the driver core here.
 */
static int nvt_ts_remove(struct spi_device *client)
{
	pr_info("3[%s] %s %d: ++\n", "nvt_ts_remove", "nvt_ts_remove", 2907);
	if (active_panel &&
	    drm_panel_notifier_unregister(active_panel, &nvt_ts->drm_nb))
		pr_info("3[%s] %s %d: Error occurred while unregistering drm_panel_notifier.\n",
			"nvt_ts_remove", "nvt_ts_remove", 2912);
	power_supply_unreg_notifier(&nvt_ts->psy_nb);
	nvt_mp_proc_deinit();
	nvt_extra_proc_deinit();
	if (NVT_proc_entry) {
		remove_proc_entry("NVTSPI", NULL);
		NVT_proc_entry = NULL;
		pr_info("3[%s] %s %d: Removed /proc/%s\n",
			"nvt_ts_remove", "nvt_ts_remove", 1083, "NVTSPI");
	}
	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&nvt_ts->nvt_fwu_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
	device_init_wakeup(&nvt_ts->input_dev->dev, 0);
	if (nvt_ts->irq_enabled)
		nvt_irq_enable(false);
	{
		struct irq_desc *d = irq_to_desc(nvt_ts->irq);

		pr_info("3[%s] %s %d: enable=%d, desc->depth=%d\n",
			"nvt_irq_enable", "nvt_irq_enable", 230, 0,
			d ? d->depth : -1);
	}
	free_irq(nvt_ts->irq, nvt_ts);
	if (nvt_ts->pen_support && nvt_ts->pen_dev) {
		input_unregister_device(nvt_ts->pen_dev);
		nvt_ts->pen_dev = NULL;
	}
	if (nvt_ts->input_dev) {
		input_unregister_device(nvt_ts->input_dev);
		nvt_ts->input_dev = NULL;
	}
	spi_set_drvdata(client, NULL);
	nvt_ts = NULL;
	return 0;
}

/* ---- nvt_ts_shutdown (stock @0xffffff8008adb5a0, TASK-081) ---- */
static void nvt_ts_shutdown(struct spi_device *client)
{
	pr_info("3[%s] %s %d: Shutdown driver...\n",
		"nvt_ts_shutdown", "nvt_ts_shutdown", 2994);
	if (nvt_ts->irq_enabled)
		nvt_irq_enable(false);
	{
		struct irq_desc *d = irq_to_desc(nvt_ts->irq);

		pr_info("3[%s] %s %d: enable=%d, desc->depth=%d\n",
			"nvt_irq_enable", "nvt_irq_enable", 230, 0,
			d ? d->depth : -1);
	}
	if (active_panel &&
	    drm_panel_notifier_unregister(active_panel, &nvt_ts->drm_nb))
		pr_info("3[%s] %s %d: Error occurred while unregistering drm_panel_notifier.\n",
			"nvt_ts_shutdown", "nvt_ts_shutdown", 3001);
	nvt_mp_proc_deinit();
	nvt_extra_proc_deinit();
	if (NVT_proc_entry) {
		remove_proc_entry("NVTSPI", NULL);
		NVT_proc_entry = NULL;
		pr_info("3[%s] %s %d: Removed /proc/%s\n",
			"nvt_ts_shutdown", "nvt_ts_shutdown", 1083, "NVTSPI");
	}
	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&nvt_ts->nvt_fwu_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
	device_init_wakeup(&nvt_ts->input_dev->dev, 0);
}

static const struct spi_device_id nvt_ts_id[] = {
	{ "NVT-ts-spi", 0 },
	{ }
};

MODULE_DEVICE_TABLE(spi, nvt_ts_id);

static const struct of_device_id nvt_match_table[] = {
	{ .compatible = "novatek,NVT-ts-spi" },
	{ }
};

/* D68: stock nvt_ts_dev_pm_ops @0x9812ee8 wires pm_suspend/pm_resume;
 * the previous build left .pm=NULL so the hooks never ran and
 * hw_sleep_check was never armed for the workfunc gate. */
const struct dev_pm_ops nvt_ts_dev_pm_ops = {
	.suspend = nvt_ts_pm_suspend,
	.resume = nvt_ts_pm_resume,
};

struct spi_driver nvt_spi_driver = {
	.driver = {
		.name = "NVT-ts",
		.owner = THIS_MODULE,
		.of_match_table = nvt_match_table,
		.pm = &nvt_ts_dev_pm_ops,
	},
	.id_table = nvt_ts_id,
	.probe = nvt_ts_probe,
	.remove = nvt_ts_remove,
	.shutdown = nvt_ts_shutdown,
};

static int nvt_driver_init(void)
{
	struct gpio_desc *desc;

	pr_info("NVT-ts: tp_newdrv_start\n");
	desc = gpio_to_desc(1182);
	gpiod_direction_input(desc);
	desc = gpio_to_desc(1182);
	gpio_82 = gpiod_get_raw_value(desc);
	pr_info("NVT-ts: gpio_82 = %d\n", gpio_82);
	if (gpio_82 >= 2) {
		pr_info("NVT-ts: TP is not NVT\n");
		return 0;
	}
	/* TASK-081 D96 (pen regression part 5, external AI review finding):
	 * stock nvt_driver_init (0x9e4ffe8) registers the lenovo_penraw char
	 * device at module init - the raw pen data channel the Lenovo HAL
	 * reads to render pen strokes. Our rebuild defined
	 * nvt_lenovo_register but never called it, so /dev/lenovo_penraw
	 * never appeared and the HAL had no pen path (nvt_driver_exit
	 * already had the unregister side of the pair). */
	{
		int lret = nvt_lenovo_register();
		if (lret)
			pr_err("NVT-ts: nvt_lenovo_register failed %d\n", lret);
	}
	return spi_register_driver(&nvt_spi_driver);
}

static void nvt_driver_exit(void)
{
	driver_unregister(&nvt_spi_driver.driver);
	nvt_extra_proc_deinit();
	nvt_lenovo_unregister();
}

module_init(nvt_driver_init);
module_exit(nvt_driver_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Novatek NT36532 touchscreen driver for TB371FC");
MODULE_AUTHOR("rebuild");
