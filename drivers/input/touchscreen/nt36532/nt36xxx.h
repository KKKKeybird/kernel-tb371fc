/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _NT36XXX_H
#define _NT36XXX_H

#include <linux/bitops.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <drm/drm_panel.h>
#include <linux/seq_file.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#define NVT_SPI_NAME "nvt_ts"
#define NVT_I2C_NAME "ctn730"
#define NVT_BOOT_UPDATE_FIRMWARE_NAME "novatek_ts_fw.bin"
#define NVT_MP_UPDATE_FIRMWARE_NAME "novatek_mp_fw.bin"

#define NVT_ABS_X_MAX 1840
#define NVT_ABS_Y_MAX 2944
#define NVT_ABS_TOUCH_LIMIT 2
#define NVTPEN_MAX_CLICK 2048
#define NVT_MAX_KEY_NUM 8
#define NVT_HALL_WORK_DELAY 250

/* ---- old-main-tree (source-side) download constants ---- */
#define DUMMY_BYTES            1
#define NVT_TRANSFER_LEN       (63 * 1024)
#define NVT_READ_LEN           (2 * 1024)
#define NVT_XBUF_LEN           (NVT_TRANSFER_LEN + 1 + DUMMY_BYTES)
#define FLASH_SECTOR_SIZE      4096
#define NVT_FLASH_END_FLAG_LEN 3
#define RESET_STATE_INIT       0xa0
#define RESET_STATE_MAX        0xaf
#define EVENT_MAP_FWINFO         0x78
#define EVENT_MAP_HOST_CMD       0x50
#define EVENT_MAP_RESET_COMPLETE 0x60

enum nvt_hwcrc {
	HWCRC_NOSUPPORT  = 0x00,
	HWCRC_LEN_2Bytes = 0x01,
	HWCRC_LEN_3Bytes = 0x02,
};

enum nvt_autocopy_check {
	AUTOCOPY_NOSUPPORT    = 0x00,
	CHECK_SPI_DMA_TX_INFO = 0x01,
	CHECK_TX_AUTO_COPY_EN = 0x02,
};

extern struct drm_panel *active_panel;

struct nvt_ts_bin_map {
	char name[12];
	uint32_t BIN_addr;
	uint32_t SRAM_addr;
	uint32_t size;
	uint32_t crc;
};

/* Struct view over the p547 u32 mmap table. Layout equals the old
 * main-tree nvt_ts_mem_map (50 words incl. ENB_CASC addr+mask). */
struct nvt_mm {
	uint32_t EVENT_BUF_ADDR;
	uint32_t RAW_PIPE0_ADDR;
	uint32_t RAW_PIPE1_ADDR;
	uint32_t BASELINE_ADDR;
	uint32_t BASELINE_BTN_ADDR;
	uint32_t DIFF_PIPE0_ADDR;
	uint32_t DIFF_PIPE1_ADDR;
	uint32_t RAW_BTN_PIPE0_ADDR;
	uint32_t RAW_BTN_PIPE1_ADDR;
	uint32_t DIFF_BTN_PIPE0_ADDR;
	uint32_t DIFF_BTN_PIPE1_ADDR;
	uint32_t PEN_2D_BL_TIP_X_ADDR;
	uint32_t PEN_2D_BL_TIP_Y_ADDR;
	uint32_t PEN_2D_BL_RING_X_ADDR;
	uint32_t PEN_2D_BL_RING_Y_ADDR;
	uint32_t PEN_2D_DIFF_TIP_X_ADDR;
	uint32_t PEN_2D_DIFF_TIP_Y_ADDR;
	uint32_t PEN_2D_DIFF_RING_X_ADDR;
	uint32_t PEN_2D_DIFF_RING_Y_ADDR;
	uint32_t PEN_2D_RAW_TIP_X_ADDR;
	uint32_t PEN_2D_RAW_TIP_Y_ADDR;
	uint32_t PEN_2D_RAW_RING_X_ADDR;
	uint32_t PEN_2D_RAW_RING_Y_ADDR;
	uint32_t PEN_1D_DIFF_TIP_X_ADDR;
	uint32_t PEN_1D_DIFF_TIP_Y_ADDR;
	uint32_t PEN_1D_DIFF_RING_X_ADDR;
	uint32_t PEN_1D_DIFF_RING_Y_ADDR;
	uint32_t ENB_CASC_REG_ADDR;
	uint32_t ENB_CASC_REG_MASK;
	uint32_t MMAP_HISTORY_EVENT0;
	uint32_t MMAP_HISTORY_EVENT1;
	uint32_t READ_FLASH_CHECKSUM_ADDR;
	uint32_t RW_FLASH_DATA_ADDR;
	uint32_t BOOT_RDY_ADDR;
	uint32_t POR_CD_ADDR;
	uint32_t TX_AUTO_COPY_EN;
	uint32_t SPI_DMA_TX_INFO;
	uint32_t BLD_LENGTH_ADDR;
	uint32_t ILM_LENGTH_ADDR;
	uint32_t DLM_LENGTH_ADDR;
	uint32_t BLD_DES_ADDR;
	uint32_t ILM_DES_ADDR;
	uint32_t DLM_DES_ADDR;
	uint32_t G_ILM_CHECKSUM_ADDR;
	uint32_t G_DLM_CHECKSUM_ADDR;
	uint32_t R_ILM_CHECKSUM_ADDR;
	uint32_t R_DLM_CHECKSUM_ADDR;
	uint32_t DMA_CRC_EN_ADDR;
	uint32_t BLD_ILM_DLM_CRC_ADDR;
	uint32_t DMA_CRC_FLAG_ADDR;
};

#define NVT_FW_STATUS_ADDR 0x50ff
#define NVT_FW_INIT_RESET_STATE 0xa1
#define NVT_FW_UPDATE_RESET_STATE 0xa0

#define NVT_TOUCH_KEY_EVENT_ID 0x01
#define NVT_TOUCH_EVENT_ID 0x02
#define NVT_PEN_EVENT_ID 0x03
#define NVT_GESTURE_EVENT_ID 0x04

struct nvt_ts_data {
	struct spi_device *client;
	struct input_dev *input_dev;
	struct delayed_work nvt_fwu_work;
	struct workqueue_struct *nvt_fwu_wq;
	struct mutex xbuf_lock;
	struct mutex lock;
	struct wait_queue_head pm_wq;
	/* stock ts+0x198: suspend writes 0 (re-arm), resume calls complete()
	 * (0xffffff8008adcec8 add x0,x8,#0x198; bl complete). Paired with the
	 * waitqueue head stock inits at ts+0x1a0 (__init_waitqueue_head in
	 * probe 0xffffff8008ad9fc4). D83/TASK-081. */
	struct completion pm_completion;
	struct notifier_block drm_nb;
	struct power_supply *psy;
	bool psy_online;
	bool gesture_flag;
	bool bTouchIsAwake;
	bool hw_sleep_check;
	int32_t fw_ver;
	int32_t fw_sub_ver;
	int32_t charger_present;
	struct delayed_work charger_work;
	struct workqueue_struct *charger_wq;
	struct notifier_block psy_nb;
	u8 irq_enabled;
	u8 spi_dma;
	u8 auto_copy;
	struct input_dev *pen_dev;
	struct input_dev *wake_input_dev;
	int32_t fw_ver_sub_ver;
	uint8_t x_num;
	uint8_t y_num;
	uint8_t max_finger_num;
	const u32 *mmap;
	const u32 *hw_reg_addr;
	u32 swrst_sif_addr;
	u32 chip_ver_trim_addr;
	u32 crc_err_flag_addr;
	uint16_t abs_x_max;
	uint16_t abs_y_max;
	uint16_t nvt_pid;
	uint8_t max_button_num;
	uint8_t x_gang_num;
	uint8_t y_gang_num;
	uint32_t pen_support;
	uint32_t pen_enable;
	uint32_t pen_version;
	uint32_t pen_suppress;
	uint32_t edge_reject;
	uint32_t edge_grid_zone[5];
	uint32_t game_mode;
	uint32_t fw_pen_state;
	uint32_t restore_pending;
	uint32_t nfc_state;
	int32_t irq;
	uint32_t int_trigger_type;
	int reset_gpio_num;
	int irq_gpio_num;
	struct gpio_desc *cs_gpio;
	struct gpio_desc *pen_gpio;
	struct gpio_desc *pen_pwr_gpio;
	struct gpio_desc *hall_gpio;
	uint32_t hall_status;
	uint32_t hall_work_status;
	uint32_t hall_pm;
	uint32_t hall_pm_status;
	int64_t hall_timer0;
	uint32_t x_error_count;
	uint32_t pen_mode;
	uint32_t fw_entry_size;
	uint32_t hw_crc;
	uint32_t map_version;
	uint32_t partition_size;
	uint8_t *fw_entry;
	uint8_t *fw_section_data;
	u32 fw_section_count;
	/* stock-parity MP / proc fields (TASK-079) */
	uint32_t mp_x_ain[10];
	uint32_t mp_y_ain[10];
	int32_t *mp_record_short;
	int32_t *mp_record_open;
	int32_t *mp_record_fw_rawdata;
	int32_t *mp_record_fw_cc;
	int32_t *mp_record_fw_diff_max;
	int32_t *mp_record_fw_diff_min;
	int32_t *mp_rawdata_short;
	int32_t *mp_rawdata_open;
	int32_t *mp_rawdata_fw_cc;
	int32_t *mp_rawdata_diff_max;
	int32_t *mp_rawdata_diff_min;
	int32_t *mp_rawdata_fw_rawdata;
	int32_t *mp_rawdata_noise;
	int32_t *mp_rec_pen_tip_raw;
	int32_t *mp_rec_pen_ring_raw;
	int32_t *mp_rec_pen_tip_diff_max;
	int32_t *mp_rec_pen_tip_diff_min;
	int32_t *mp_rec_pen_ring_diff_max;
	int32_t *mp_rec_pen_ring_diff_min;
	int32_t *mp_raw_pen_tip_raw;
	int32_t *mp_raw_pen_ring_raw;
	int32_t *mp_raw_pen_tip_diff_max;
	int32_t *mp_raw_pen_tip_diff_min;
	int32_t *mp_raw_pen_ring_diff_max;
	int32_t *mp_raw_pen_ring_diff_min;
	int32_t mp_ic_x_cfg_size;
	int32_t mp_ic_y_cfg_size;
	int32_t mp_x_channel;
	int32_t mp_y_channel;
	int32_t *mp_criteria_short_n;
	int32_t *mp_criteria_open_p;
	int32_t *mp_criteria_open_n;
	int32_t *mp_criteria_fw_raw_p;
	int32_t *mp_criteria_fw_raw_n;
	int32_t *mp_criteria_fw_cc_p;
	int32_t *mp_criteria_fw_cc_n;
	int32_t *mp_criteria_fw_diff_p;
	int32_t *mp_criteria_fw_diff_n;
	int32_t *mp_criteria_pen_tip_raw_p;
	int32_t *mp_criteria_pen_tip_raw_n;
	int32_t *mp_criteria_pen_ring_raw_p;
	int32_t *mp_criteria_pen_ring_raw_n;
	int32_t *mp_criteria_pen_tip_diff_max_p;
	int32_t *mp_criteria_pen_tip_diff_max_n;
	int32_t *mp_criteria_pen_ring_diff_max_p;
	int32_t *mp_criteria_pen_ring_diff_max_n;
	int32_t *mp_criteria_pen_tip_diff_min_p;
	int32_t *mp_criteria_pen_tip_diff_min_n;
	int32_t *mp_criteria_pen_ring_diff_min_p;
	int32_t *mp_criteria_pen_ring_diff_min_n;
	uint32_t mp_diff_test_frame;
	struct proc_dir_entry *proc_nvt_dir;
	struct proc_dir_entry *proc_self_test;
	struct proc_dir_entry *proc_fw_version;
	struct proc_dir_entry *proc_baseline;
	struct proc_dir_entry *proc_gesture_control;
	struct proc_dir_entry *proc_diff;
	struct proc_dir_entry *proc_pen_diff;
	struct proc_dir_entry *proc_fw_status;
	struct proc_dir_entry *proc_edge_reject;
	struct proc_dir_entry *proc_edge_grid_zone;
	struct proc_dir_entry *proc_game_mode;
	struct proc_dir_entry *proc_support_pen;
	struct proc_dir_entry *proc_fw_pen_state;
	struct proc_dir_entry *proc_raw;
	uint8_t *xbuf;
	uint8_t *rbuf;
	char phys[32];
	uint8_t hw_reg_addr_info[128];
	uint8_t gesture_key_array[13];
	uint8_t gesture_key_buffer[6];
};

int nvt_get_fw_info(void);
int nvt_check_fw_reset_state(uint8_t reset_state);
int nvt_update_firmware(const char *firmware_name);
void nvt_support_pen_set_nfc(bool no_pen);
int nvt_support_pen_set(u8 enable, u8 version);
int nvt_cmd_store(uint8_t value);
int nvt_ext_cmd_store(uint8_t mode, uint8_t value);

int nvt_edge_grid_zone_set(u8 x1, u8 y1, u16 x2, u16 y2);
int nvt_change_mode(uint8_t mode);
void nvt_irq_enable(bool enable);
int nvt_check_fw_status(void);
int nvt_clear_fw_status(void);

int CTP_SPI_READ(struct spi_device *client, void *buf, size_t len);
int CTP_SPI_WRITE(struct spi_device *client, void *buf, size_t len);
int nvt_write_addr(u32 addr, u8 value);
void nvt_bootloader_reset(void);
void nvt_eng_reset(void);
void nvt_tx_auto_copy_mode(void);
int32_t nvt_wait_auto_copy(void);
void nvt_fw_crc_enable(void);
void nvt_boot_ready(void);
int32_t nvt_write_firmware(const u8 *fwdata, size_t fwsize);
void nvt_read_fw_history(u32 addr);
int nvt_get_fw_pipe(void);
void nvt_read_mdata(uint32_t addr);
void nvt_get_mdata(int32_t *buf, u8 *x_num, u8 *y_num);
void nvt_read_get_num_mdata(uint32_t addr, int32_t *buf, uint32_t num);
int nvt_mp_proc_init(void);
void nvt_mp_proc_deinit(void);
int nvt_extra_proc_init(void);
void nvt_extra_proc_deinit(void);
int check_tp_state(unsigned long enable);



void nvt_update_firmware_work(struct work_struct *work);
irqreturn_t nvt_ts_work_func(int irq, void *dev_id);
extern struct spi_driver nvt_spi_driver;
extern struct workqueue_struct *nvt_fwu_wq;

#endif
