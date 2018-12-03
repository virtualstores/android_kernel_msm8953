/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
//#define DEBUG
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include "msm_dba_internal.h"
#include <linux/mdss_io_util.h>

#define EDID_ADDR 0x50
#define LT8912_REG_CHIP_REVISION_0 (0x00)
#define LT8912_REG_CHIP_REVISION_1 (0x01)

#define LT8912_VAL_CHIP_REVISION_0 (0x12)
#define LT8912_VAL_CHIP_REVISION_1 (0xB2)
#define LT8912_DSI_CEC_I2C_ADDR_REG (0xE1)
#define LT8912_RESET_DELAY (100)

#define PINCTRL_STATE_ACTIVE    "pmx_lt8912_active"
#define PINCTRL_STATE_SUSPEND   "pmx_lt8912_suspend"

#define MDSS_MAX_PANEL_LEN      256
#define EDID_SEG_SIZE 0x100
/* size of audio and speaker info Block */
#define AUDIO_DATA_SIZE 32

/* 0x94 interrupts */
#define HPD_INT_ENABLE           BIT(7)
#define MONITOR_SENSE_INT_ENABLE BIT(6)
#define ACTIVE_VSYNC_EDGE        BIT(5)
#define AUDIO_FIFO_FULL          BIT(4)
#define EDID_READY_INT_ENABLE    BIT(2)

#define MAX_WAIT_TIME (100)
#define MAX_RW_TRIES (3)

/* 0x95 interrupts */
#define CEC_TX_READY             BIT(5)
#define CEC_TX_ARB_LOST          BIT(4)
#define CEC_TX_RETRY_TIMEOUT     BIT(3)
#define CEC_TX_RX_BUF3_READY     BIT(2)
#define CEC_TX_RX_BUF2_READY     BIT(1)
#define CEC_TX_RX_BUF1_READY     BIT(0)

#define HPD_INTERRUPTS           (HPD_INT_ENABLE | \
					MONITOR_SENSE_INT_ENABLE)
#define EDID_INTERRUPTS          EDID_READY_INT_ENABLE
#define CEC_INTERRUPTS           (CEC_TX_READY | \
					CEC_TX_ARB_LOST | \
					CEC_TX_RETRY_TIMEOUT | \
					CEC_TX_RX_BUF3_READY | \
					CEC_TX_RX_BUF2_READY | \
					CEC_TX_RX_BUF1_READY)

#define CFG_HPD_INTERRUPTS       BIT(0)
#define CFG_EDID_INTERRUPTS      BIT(1)
#define CFG_CEC_INTERRUPTS       BIT(3)

#define MAX_OPERAND_SIZE	14
#define CEC_MSG_SIZE            (MAX_OPERAND_SIZE + 2)


#define HDMI_MODE_480P   0
#define HDMI_MODE_720P   0
#define HDMI_MODE_1080P  1

static int hdmi_connected = 0;

enum lt8912_i2c_addr {
	I2C_ADDR_MAIN = 0x48,
	I2C_ADDR_CEC_DSI = 0x49,
	I2C_ADDR_I2S = 0x4a,
};

enum lt8912_cec_buf {
	LT8912_CEC_BUF1,
	LT8912_CEC_BUF2,
	LT8912_CEC_BUF3,
	LT8912_CEC_BUF_MAX,
};

struct lt8912_reg_cfg {
	u8 i2c_addr;
	u8 reg;
	u8 val;
	int sleep_in_ms;
};

struct lt8912_cec_msg {
	u8 buf[CEC_MSG_SIZE];
	u8 timestamp;
	bool pending;
};

struct lt8912 {
	u8 video_mode;
	int irq;
	u32 irq_gpio;
	u32 irq_flags;
	u32 rst_gpio;
	u32 rst_flags;
	struct pinctrl *ts_pinctrl;
	struct pinctrl_state *pinctrl_state_active;
	struct pinctrl_state *pinctrl_state_suspend;
	bool audio;
	bool disable_gpios;
	bool is_power_on;
	void *edid_data;
	u8 edid_buf[EDID_SEG_SIZE];
	u8 audio_spkr_data[AUDIO_DATA_SIZE];
	struct workqueue_struct *workq;
	//struct delayed_work lt8912_intr_work_id;
	struct delayed_work lt8912_check_hpd_work_id;
	struct msm_dba_device_info dev_info;
	struct lt8912_cec_msg cec_msg[LT8912_CEC_BUF_MAX];
	struct i2c_client *i2c_client;
	struct mutex ops_mutex;
};

static char mdss_mdp_panel[MDSS_MAX_PANEL_LEN];

static struct lt8912_reg_cfg lt8912_init_setup[] = {
/* Digital clock en*/
	/* power down */
	{I2C_ADDR_MAIN, 0x08, 0xff, 0},
	/* HPD override */
	{I2C_ADDR_MAIN, 0x09, 0xff, 0},
	/* color space */
	{I2C_ADDR_MAIN, 0x0a, 0xff, 0},
	/* Fixed */
	{I2C_ADDR_MAIN, 0x0b, 0x7c, 0},
	/* HDCP */
	{I2C_ADDR_MAIN, 0x0c, 0xff, 0},
/*Tx Analog*/
	/* Fixed */
	{I2C_ADDR_MAIN, 0x31, 0xA1, 0},
	/* V1P2 */
	{I2C_ADDR_MAIN, 0x32, 0xA1, 0},
	/* Fixed */
	{I2C_ADDR_MAIN, 0x33, 0x03, 0},
	/* Fixed */
	{I2C_ADDR_MAIN, 0x37, 0x00, 0},
	/* Fixed */
	{I2C_ADDR_MAIN, 0x38, 0x22, 0},
	/* Fixed */
	{I2C_ADDR_MAIN, 0x60, 0x82, 0},
/*Cbus Analog*/
	/* Fixed */
	{I2C_ADDR_MAIN, 0x39, 0x45, 0},
	{I2C_ADDR_MAIN, 0x3b, 0x00, 0},
/*HDMI Pll Analog*/	
	{I2C_ADDR_MAIN, 0x44, 0x31, 0},
	/* Fixed */
	{I2C_ADDR_MAIN, 0x55, 0x44, 0},
	/* Fixed */
	{I2C_ADDR_MAIN, 0x57, 0x01, 0},
	{I2C_ADDR_MAIN, 0x5a, 0x02, 0},
/*MIPI Analog*/
	/* Fixed */
	{I2C_ADDR_MAIN, 0x3e, 0x8e, 0},
	{I2C_ADDR_MAIN, 0x41, 0x3c, 0},

};

static struct lt8912_reg_cfg lt8912_mipi_basic_set[] = {
	{I2C_ADDR_CEC_DSI, 0x12,0x04, 0}, 
	{I2C_ADDR_CEC_DSI, 0x13,0x00, 0}, 
	{I2C_ADDR_CEC_DSI, 0x14,0x00, 0}, 
	{I2C_ADDR_CEC_DSI, 0x15,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x1a,0x03, 0}, 
	{I2C_ADDR_CEC_DSI, 0x1b,0x03, 0}, 
};

/*static struct lt8912_reg_cfg lt8912_480p_mode[] = {
	{I2C_ADDR_CEC_DSI, 0x10,0x01, 0}, 
	{I2C_ADDR_CEC_DSI, 0x11,0x04, 0}, 
	{I2C_ADDR_CEC_DSI, 0x18,0x60, 0},
	{I2C_ADDR_CEC_DSI, 0x19,0x02, 0},
	{I2C_ADDR_CEC_DSI, 0x1c,0x80, 0},
	{I2C_ADDR_CEC_DSI, 0x1d,0x02, 0},
	{I2C_ADDR_CEC_DSI, 0x2f,0x0c, 0},
	{I2C_ADDR_CEC_DSI, 0x34,0x10, 0},
	{I2C_ADDR_CEC_DSI, 0x35,0x03, 0},
	{I2C_ADDR_CEC_DSI, 0x36,0x0d, 0},
	{I2C_ADDR_CEC_DSI, 0x37,0x02, 0},
	{I2C_ADDR_CEC_DSI, 0x38,0x0a, 0},
	{I2C_ADDR_CEC_DSI, 0x39,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x3a,0x21, 0},
	{I2C_ADDR_CEC_DSI, 0x3b,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x3c,0x28, 0},
	{I2C_ADDR_CEC_DSI, 0x3d,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x3e,0x08, 0},
	{I2C_ADDR_CEC_DSI, 0x3f,0x00, 0},
};*/

#if HDMI_MODE_480P
static struct lt8912_reg_cfg lt8912_480p_mode[] = {
	{I2C_ADDR_CEC_DSI, 0x10,0x01, 0}, 
	{I2C_ADDR_CEC_DSI, 0x11,0x04, 0}, 
	{I2C_ADDR_CEC_DSI, 0x18,0x80, 0},
	{I2C_ADDR_CEC_DSI, 0x19,0x04, 0},
	{I2C_ADDR_CEC_DSI, 0x1c,0x20, 0},
	{I2C_ADDR_CEC_DSI, 0x1d,0x03, 0},
	{I2C_ADDR_CEC_DSI, 0x2f,0x0c, 0},
	{I2C_ADDR_CEC_DSI, 0x34,0x20, 0},
	{I2C_ADDR_CEC_DSI, 0x35,0x04, 0},
	{I2C_ADDR_CEC_DSI, 0x36,0x74, 0},
	{I2C_ADDR_CEC_DSI, 0x37,0x02, 0},
	{I2C_ADDR_CEC_DSI, 0x38,0x17, 0},
	{I2C_ADDR_CEC_DSI, 0x39,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x3a,0x01, 0},
	{I2C_ADDR_CEC_DSI, 0x3b,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x3c,0x58, 0},
	{I2C_ADDR_CEC_DSI, 0x3d,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x3e,0x28, 0},
	{I2C_ADDR_CEC_DSI, 0x3f,0x00, 0},
};

#elif HDMI_MODE_720P
static struct lt8912_reg_cfg lt8912_720p_mode[] = {
	{I2C_ADDR_CEC_DSI, 0x10,0x01, 0}, 
	{I2C_ADDR_CEC_DSI, 0x11,0x0a, 0}, 
	{I2C_ADDR_CEC_DSI, 0x18,0x28, 0},
	{I2C_ADDR_CEC_DSI, 0x19,0x05, 0},
	{I2C_ADDR_CEC_DSI, 0x1c,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x1d,0x05, 0},
	{I2C_ADDR_CEC_DSI, 0x2f,0x0c, 0},
	{I2C_ADDR_CEC_DSI, 0x34,0x72, 0},
	{I2C_ADDR_CEC_DSI, 0x35,0x06, 0},
	{I2C_ADDR_CEC_DSI, 0x36,0xee, 0},
	{I2C_ADDR_CEC_DSI, 0x37,0x02, 0},
	{I2C_ADDR_CEC_DSI, 0x38,0x14, 0},
	{I2C_ADDR_CEC_DSI, 0x39,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x3a,0x05, 0},
	{I2C_ADDR_CEC_DSI, 0x3b,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x3c,0xdc, 0},
	{I2C_ADDR_CEC_DSI, 0x3d,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x3e,0x6e, 0},
	{I2C_ADDR_CEC_DSI, 0x3f,0x00, 0},
};
#elif HDMI_MODE_1080P
static struct lt8912_reg_cfg lt8912_1080p_mode[] = {
	{I2C_ADDR_CEC_DSI, 0x18,0x2c, 0},
	{I2C_ADDR_CEC_DSI, 0x19,0x05, 0},
	{I2C_ADDR_CEC_DSI, 0x1c,0x80, 0},
	{I2C_ADDR_CEC_DSI, 0x1d,0x07, 0},
	{I2C_ADDR_CEC_DSI, 0x2f,0x0c, 0},
	{I2C_ADDR_CEC_DSI, 0x34,0x98, 0},
	{I2C_ADDR_CEC_DSI, 0x35,0x08, 0},
	{I2C_ADDR_CEC_DSI, 0x36,0x65, 0},
	{I2C_ADDR_CEC_DSI, 0x37,0x04, 0},
	{I2C_ADDR_CEC_DSI, 0x38,0x24, 0},
	{I2C_ADDR_CEC_DSI, 0x39,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x3a,0x04, 0},
	{I2C_ADDR_CEC_DSI, 0x3b,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x3c,0x94, 0},
	{I2C_ADDR_CEC_DSI, 0x3d,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x3e,0x58, 0},
	{I2C_ADDR_CEC_DSI, 0x3f,0x00, 0},
};

#endif

static struct lt8912_reg_cfg lt8912_ddsconfig[] = {
	{I2C_ADDR_CEC_DSI, 0x4e,0x6A, 0},
	{I2C_ADDR_CEC_DSI, 0x4f,0x4D, 0},
	{I2C_ADDR_CEC_DSI, 0x50,0xF3, 0},
	{I2C_ADDR_CEC_DSI, 0x51,0x80, 0},
	{I2C_ADDR_CEC_DSI, 0x1f,0x90, 0},
	{I2C_ADDR_CEC_DSI, 0x20,0x01, 0},
	{I2C_ADDR_CEC_DSI, 0x21,0x68, 0},
	{I2C_ADDR_CEC_DSI, 0x22,0x01, 0},
	{I2C_ADDR_CEC_DSI, 0x23,0x5E, 0},
	{I2C_ADDR_CEC_DSI, 0x24,0x01, 0},
	{I2C_ADDR_CEC_DSI, 0x25,0x54, 0},
	{I2C_ADDR_CEC_DSI, 0x26,0x01, 0},
	{I2C_ADDR_CEC_DSI, 0x27,0x90, 0},
	{I2C_ADDR_CEC_DSI, 0x28,0x01, 0},
	{I2C_ADDR_CEC_DSI, 0x29,0x68, 0},
	{I2C_ADDR_CEC_DSI, 0x2a,0x01, 0},
	{I2C_ADDR_CEC_DSI, 0x2b,0x5E, 0},
	{I2C_ADDR_CEC_DSI, 0x2c,0x01, 0},
	{I2C_ADDR_CEC_DSI, 0x2d,0x54, 0},
	{I2C_ADDR_CEC_DSI, 0x2e,0x01, 0},
	{I2C_ADDR_CEC_DSI, 0x42,0x64, 0},
	{I2C_ADDR_CEC_DSI, 0x43,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x44,0x04, 0},
	{I2C_ADDR_CEC_DSI, 0x45,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x46,0x59, 0},
	{I2C_ADDR_CEC_DSI, 0x47,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x48,0xf2, 0},
	{I2C_ADDR_CEC_DSI, 0x49,0x06, 0},
	{I2C_ADDR_CEC_DSI, 0x4a,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x4b,0x72, 0},
	{I2C_ADDR_CEC_DSI, 0x4c,0x45, 0},
	{I2C_ADDR_CEC_DSI, 0x4d,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x52,0x08, 0},
	{I2C_ADDR_CEC_DSI, 0x53,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x54,0xb2, 0},
	{I2C_ADDR_CEC_DSI, 0x55,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x56,0xe4, 0},
	{I2C_ADDR_CEC_DSI, 0x57,0x0d, 0},
	{I2C_ADDR_CEC_DSI, 0x58,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x59,0xe4, 0},
	{I2C_ADDR_CEC_DSI, 0x5a,0x8a, 0},
	{I2C_ADDR_CEC_DSI, 0x5b,0x00, 0},
	{I2C_ADDR_CEC_DSI, 0x5c,0x34, 0},
	{I2C_ADDR_CEC_DSI, 0x1e,0x4f, 0},
	{I2C_ADDR_CEC_DSI, 0x51,0x00, 0},
};

static struct lt8912_reg_cfg lt8912_rxlogicres[] = {
	{I2C_ADDR_MAIN, 0x03,0x7f, 100},
	{I2C_ADDR_MAIN, 0x03,0xff, 0},
};

static struct lt8912_reg_cfg I2S_cfg[] = {
	{I2C_ADDR_MAIN, 0xB2, 0x01, 0},
	{I2C_ADDR_I2S, 0x06, 0x08, 0},
	{I2C_ADDR_I2S, 0x07, 0xf0, 0},
	{I2C_ADDR_I2S, 0x34, 0xd2, 0},
};

static int lt8912_write(struct lt8912 *pdata, u8 addr, u8 reg, u8 val)
{
	int ret = 0;

	if (!pdata) {
		pr_debug("%s: Invalid argument\n", __func__);
		return -EINVAL;
	}

	ret = msm_dba_helper_i2c_write_byte(pdata->i2c_client, addr, reg, val);
	if (ret)
		pr_err_ratelimited("%s: wr err: addr 0x%x, reg 0x%x, val 0x%x\n",
				__func__, addr, reg, val);
	return ret;
}

static int lt8912_read(struct lt8912 *pdata, u8 addr,
		u8 reg, char *buf, u32 size)
{
	int ret = 0;

	if (!pdata) {
		pr_debug("%s: Invalid argument\n", __func__);
		return -EINVAL;
	}

	ret = msm_dba_helper_i2c_read(pdata->i2c_client, addr, reg, buf, size);
	if (ret)
		pr_err_ratelimited("%s: read err: addr 0x%x, reg 0x%x, size 0x%x\n",
				__func__, addr, reg, size);
	return ret;
}

static int lt8912_dump_debug_info(struct msm_dba_device_info *dev, u32 flags)
{
	int rc = 0;
	u8 byte_val = 0;
	u16 addr = 0;
	struct lt8912 *pdata = NULL;

	if (!dev) {
		pr_err("%s: dev is NULL\n", __func__);
		return -EINVAL;
	}
	pdata = container_of(dev, struct lt8912, dev_info);
	if (!pdata) {
		pr_err("%s: pdata is NULL\n", __func__);
		return -EINVAL;
	}

	/* dump main addr*/
	pr_err("========Main I2C Start==========\n");
	for (addr = 0; addr <= 0xFF; addr++) {
		rc = lt8912_read(pdata, I2C_ADDR_MAIN,
			(u8)addr, &byte_val, 1);
		if (rc)
			pr_err("%s: read reg=0x%02x failed @ addr=I2C_ADDR_MAIN\n",
				__func__, addr);
		else
			pr_err("0x%02x -> 0x%02X\n", addr, byte_val);
	}
	pr_err("========Main I2C End==========\n");
	/* dump CEC addr*/
	pr_err("=======CEC I2C Start=========\n");
	for (addr = 0; addr <= 0xFF; addr++) {
		rc = lt8912_read(pdata, I2C_ADDR_CEC_DSI,
			(u8)addr, &byte_val, 1);
		if (rc)
			pr_err("%s: read reg=0x%02x failed @ addr=I2C_ADDR_CEC_DSI\n",
				__func__, addr);
		else
			pr_err("0x%02x -> 0x%02X\n", addr, byte_val);
	}
	pr_err("========CEC I2C End==========\n");

	return rc;
}

static void lt8912_write_array(struct lt8912 *pdata,
	struct lt8912_reg_cfg *cfg, int size)
{
	int ret = 0;
	int i;

	size = size / sizeof(struct lt8912_reg_cfg);
	for (i = 0; i < size; i++) {
		ret = lt8912_write(pdata, cfg[i].i2c_addr,
			cfg[i].reg, cfg[i].val);
		if (ret != 0){
			pr_err("%s: lt8912 reg write %02X to %02X failed.\n",
				__func__, cfg[i].val, cfg[i].reg);
		}
		if (cfg[i].sleep_in_ms)
			msleep(cfg[i].sleep_in_ms);
	}
}

static int lt8912_read_device_rev(struct lt8912 *pdata)
{
	u8 rev = 0;
	int ret;

	ret = lt8912_read(pdata, I2C_ADDR_MAIN, LT8912_REG_CHIP_REVISION_0,
                                                        &rev, 1);
	if (ret || rev != LT8912_VAL_CHIP_REVISION_0)
	{
		pr_err("check chip revision not match reg = 0x%x, val = 0x%x\n", LT8912_REG_CHIP_REVISION_0, rev);
	//	return -1;
	}
	pr_debug("%s: LT8912_REG_CHIP_REVISION_0= %d\n", __func__, rev);
	ret = lt8912_read(pdata, I2C_ADDR_MAIN, LT8912_REG_CHIP_REVISION_1,
                                                        &rev, 1);
	if (ret || rev != LT8912_VAL_CHIP_REVISION_1)
	{
		pr_err("check chip revision not match reg = 0x%x, val = 0x%x\n", LT8912_REG_CHIP_REVISION_1, rev);
	//	return -1;
	}
	pr_debug("%s: LT8912_REG_CHIP_REVISION_1= %d\n", __func__, rev);

  //  return ret;
    return 0;
}

static int lt8912_parse_dt(struct device *dev,
	struct lt8912 *pdata)
{
	struct device_node *np = dev->of_node;
	u32 temp_val = 0;
	int ret = 0;

	ret = of_property_read_u32(np, "instance_id", &temp_val);
	pr_debug("%s: DT property %s is %X\n", __func__, "instance_id",
		temp_val);
	if (ret)
		goto end;
	pdata->dev_info.instance_id = temp_val;

	pdata->audio = of_property_read_bool(np, "lt,enable-audio");

	/* Get pinctrl if target uses pinctrl */
	pdata->ts_pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(pdata->ts_pinctrl)) {
		ret = PTR_ERR(pdata->ts_pinctrl);
		pr_err("%s: Pincontrol DT property returned %X\n",
			__func__, ret);
	}

	pdata->pinctrl_state_active = pinctrl_lookup_state(pdata->ts_pinctrl,
		"pmx_lt8912_active");
	if (IS_ERR_OR_NULL(pdata->pinctrl_state_active)) {
		ret = PTR_ERR(pdata->pinctrl_state_active);
		pr_err("Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_ACTIVE, ret);
	}

	pdata->pinctrl_state_suspend = pinctrl_lookup_state(pdata->ts_pinctrl,
		"pmx_lt8912_suspend");
	if (IS_ERR_OR_NULL(pdata->pinctrl_state_suspend)) {
		ret = PTR_ERR(pdata->pinctrl_state_suspend);
		pr_err("Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_SUSPEND, ret);
	}

	pdata->disable_gpios = of_property_read_bool(np,
			"lt,disable-gpios");

	if (!(pdata->disable_gpios)) {
		pdata->irq_gpio = of_get_named_gpio_flags(np,
				"lt,irq-gpio", 0, &pdata->irq_flags);
		
		pdata->rst_gpio = of_get_named_gpio_flags(np,
				"lt,rst-gpio", 0, &pdata->rst_flags);
	}

end:
	return ret;
}

static int lt8912_gpio_configure(struct lt8912 *pdata, bool on)
{
	int ret = 0;

	if (pdata->disable_gpios)
		return 0;

	if (on) {
		if (gpio_is_valid(pdata->rst_gpio)) {
			ret = gpio_request(pdata->rst_gpio, "lt8912_rst_gpio");
			if (ret) {
				pr_err("%d unable to request gpio [%d] ret=%d\n",
					__LINE__, pdata->rst_gpio, ret);
				goto err_none;
			}
			ret = gpio_direction_output(pdata->rst_gpio, 1);
			if (ret) {
				pr_err("unable to set dir for gpio[%d]\n",
					pdata->rst_gpio);
				goto err_rst_gpio;
			}
		} else {
			pr_err("rst gpio not provided\n");
			goto err_none;
		}

		if (gpio_is_valid(pdata->irq_gpio)) {
			ret = gpio_request(pdata->irq_gpio, "lt8912_irq_gpio");
			if (ret) {
				pr_err("%d unable to request gpio [%d] ret=%d\n",
					__LINE__, pdata->irq_gpio, ret);
				goto err_rst_gpio;
			}
			ret = gpio_direction_input(pdata->irq_gpio);
			if (ret) {
				pr_err("unable to set dir for gpio[%d]\n",
					pdata->irq_gpio);
				goto err_irq_gpio;
			}
		} else {
			pr_err("irq gpio not provided\n");
			goto err_rst_gpio;
		}

		return 0;
	} else {
		if (gpio_is_valid(pdata->rst_gpio))
			gpio_free(pdata->rst_gpio);
		if (gpio_is_valid(pdata->irq_gpio))
			gpio_free(pdata->irq_gpio);

		return 0;
	}

err_irq_gpio:
	if (gpio_is_valid(pdata->irq_gpio))
		gpio_free(pdata->irq_gpio);
err_rst_gpio:
	if (gpio_is_valid(pdata->rst_gpio))
		gpio_free(pdata->rst_gpio);
err_none:
	return ret;
}

static void lt8912_notify_clients(struct msm_dba_device_info *dev,
		enum msm_dba_callback_event event)
{
	struct msm_dba_client_info *c;
	struct list_head *pos = NULL;

	if (!dev) {
		pr_err("%s: invalid input\n", __func__);
		return;
	}

	list_for_each(pos, &dev->client_list) {
		c = list_entry(pos, struct msm_dba_client_info, list);

		pr_debug("%s: notifying event %d to client %s\n", __func__,
			event, c->client_name);

		if (c && c->cb)
			c->cb(c->cb_data, event);
	}
}

u32 lt8912_read_edid(struct lt8912 *pdata, u32 size, char *edid_buf)
{
	u32 ret = 0, read_size = size / 2;
	u8 edid_addr;
	int ndx;

	if (!pdata || !edid_buf)
		return 0;

	pr_err("%s: size %d\n", __func__, size);
	udelay(10 * 1000);

	//lt8912_read(pdata, I2C_ADDR_MAIN, 0x43, &edid_addr, 1);

	pr_debug("%s: edid address 0x%x\n", __func__, edid_addr);

	lt8912_read(pdata, EDID_ADDR, 0x00, edid_buf, read_size);

	lt8912_read(pdata, EDID_ADDR, read_size,
		edid_buf + read_size, read_size);

	for (ndx = 0; ndx < size; ndx += 4)
		pr_debug("%s: EDID[%02x-%02x] %02x %02x %02x %02x\n",
			__func__, ndx, ndx + 3,
			edid_buf[ndx + 0], edid_buf[ndx + 1],
			edid_buf[ndx + 2], edid_buf[ndx + 3]);

	return ret;
}


/*static int lt8912_edid_read_init(struct lt8912 *pdata)
{
	int ret = -EINVAL;

	if (!pdata) {
		pr_err("%s: invalid pdata\n", __func__);
		goto end;
	}

	//initiate edid read in lt8912 
	//lt8912_write(pdata, I2C_ADDR_MAIN, 0x41, 0x10);
	//lt8912_write(pdata, I2C_ADDR_MAIN, 0xC9, 0x13);

end:
	return ret;
}
*/

/*static int lt8912_disable_interrupts(struct lt8912 *pdata, int interrupts)
{
	int ret = 0;
	u8 reg_val, init_reg_val;

	if (!pdata) {
		pr_err("%s: invalid input\n", __func__);
		goto end;
	}

	lt8912_read(pdata, I2C_ADDR_MAIN, 0x94, &reg_val, 1);

	init_reg_val = reg_val;

	if (interrupts & CFG_HPD_INTERRUPTS)
		reg_val &= ~HPD_INTERRUPTS;

	if (interrupts & CFG_EDID_INTERRUPTS)
		reg_val &= ~EDID_INTERRUPTS;

	if (reg_val != init_reg_val) {
		pr_debug("%s: disabling 0x94 interrupts\n", __func__);
		//lt8912_write(pdata, I2C_ADDR_MAIN, 0x94, reg_val);
	}

	lt8912_read(pdata, I2C_ADDR_MAIN, 0x95, &reg_val, 1);

	init_reg_val = reg_val;

	if (interrupts & CFG_CEC_INTERRUPTS)
		reg_val &= ~CEC_INTERRUPTS;

	if (reg_val != init_reg_val) {
		pr_debug("%s: disabling 0x95 interrupts\n", __func__);
		//lt8912_write(pdata, I2C_ADDR_MAIN, 0x95, reg_val);
	}
end:
	return ret;
}*/

static void lt8912_check_hpd_work(struct work_struct *work)
{
	int ret;
	int connected = 0;
	struct lt8912 *pdata;
	struct delayed_work *dw = to_delayed_work(work);
	u8 reg_val = 0;

	pdata = container_of(dw, struct lt8912,
			lt8912_check_hpd_work_id);
	if (!pdata) {
		pr_err("%s: invalid input\n", __func__);
		return;
	}

	/* Check if cable is already connected.
	 * Since lt8912_irq line is edge triggered,
	 * if cable is already connected by this time
	 * it won't trigger HPD interrupt.
	 */
	lt8912_read(pdata, I2C_ADDR_MAIN, 0xC1, &reg_val, 1);

	connected  = (reg_val & BIT(7));
	pr_debug("%s: connected = %d\n", __func__, connected);
	
	if(connected != hdmi_connected){
		hdmi_connected = connected;
		if (connected) {
			ret = lt8912_read_edid(pdata, sizeof(pdata->edid_buf),
				pdata->edid_buf);
			if (ret)
				pr_err("%s: edid read failed\n", __func__);

			lt8912_notify_clients(&pdata->dev_info,
				MSM_DBA_CB_HPD_CONNECT);
			pr_err("%s: Rx CONNECTED\n", __func__);
		} else {
			pr_err("%s: Rx DISCONNECTED\n", __func__);

			lt8912_notify_clients(&pdata->dev_info,
				MSM_DBA_CB_HPD_DISCONNECT);
		} 
	}

	queue_delayed_work(pdata->workq, &pdata->lt8912_check_hpd_work_id, 500);

}

static struct i2c_device_id lt8912_id[] = {
	{ "lt8912", 0},
	{}
};

static struct lt8912 *lt8912_get_platform_data(void *client)
{
	struct lt8912 *pdata = NULL;
	struct msm_dba_device_info *dev;
	struct msm_dba_client_info *cinfo =
		(struct msm_dba_client_info *)client;

	if (!cinfo) {
		pr_err("%s: invalid client data\n", __func__);
		goto end;
	}

	dev = cinfo->dev;
	if (!dev) {
		pr_err("%s: invalid device data\n", __func__);
		goto end;
	}

	pdata = container_of(dev, struct lt8912, dev_info);
	if (!pdata)
		pr_err("%s: invalid platform data\n", __func__);

end:
	return pdata;
}

/*static int lt8912_cec_enable(void *client, bool cec_on, u32 flags)
{
	int ret = -EINVAL;
	struct lt8912 *pdata = lt8912_get_platform_data(client);

	if (!pdata) {
		pr_err("%s: invalid platform data\n", __func__);
		goto end;
	}

	if (cec_on) {
		lt8912_write_array(pdata, lt8912_cec_en,
					sizeof(lt8912_cec_en));
		lt8912_write_array(pdata, lt8912_cec_tg_init,
					sizeof(lt8912_cec_tg_init));
		lt8912_write_array(pdata, lt8912_cec_power,
					sizeof(lt8912_cec_power));

		pdata->cec_enabled = true;

		ret = lt8912_enable_interrupts(pdata, CFG_CEC_INTERRUPTS);

	} else {
		pdata->cec_enabled = false;
		ret = lt8912_disable_interrupts(pdata, CFG_CEC_INTERRUPTS);
	}
end:
	return ret;
}*/
static void lt8912_set_audio_block(void *client, u32 size, void *buf)
{
	struct lt8912 *pdata =
		lt8912_get_platform_data(client);

	if (!pdata || !buf) {
		pr_err("%s: invalid data\n", __func__);
		return;
	}

	mutex_lock(&pdata->ops_mutex);

	size = min_t(u32, size, AUDIO_DATA_SIZE);

	memset(pdata->audio_spkr_data, 0, AUDIO_DATA_SIZE);
	memcpy(pdata->audio_spkr_data, buf, size);

	mutex_unlock(&pdata->ops_mutex);
}

static void lt8912_get_audio_block(void *client, u32 size, void *buf)
{
	struct lt8912 *pdata =
		lt8912_get_platform_data(client);

	if (!pdata || !buf) {
		pr_err("%s: invalid data\n", __func__);
		return;
	}

	mutex_lock(&pdata->ops_mutex);

	size = min_t(u32, size, AUDIO_DATA_SIZE);

	memcpy(buf, pdata->audio_spkr_data, size);

	mutex_unlock(&pdata->ops_mutex);
}


static int lt8912_check_hpd(void *client, u32 flags)
{
	struct lt8912 *pdata = lt8912_get_platform_data(client);
	u8 reg_val = 0;
	int connected = 0;

	if (!pdata) {
		pr_err("%s: invalid platform data\n", __func__);
		return -EINVAL;
	}
	
	pr_debug("%s: enter\n", __func__);
	/* Check if cable is already connected.
	 * Since lt8912_irq line is edge triggered,
	 * if cable is already connected by this time
	 * it won't trigger HPD interrupt.
	 */
	mutex_lock(&pdata->ops_mutex);
	lt8912_read(pdata, I2C_ADDR_MAIN, 0xC1, &reg_val, 1);

	connected  = (reg_val & BIT(7));
	if (connected) {
		pr_debug("%s: cable is connected\n", __func__);

		//lt8912_edid_read_init(pdata);
	}
	mutex_unlock(&pdata->ops_mutex);

	return connected;
}

/* Device Operations */
static int lt8912_power_on(void *client, bool on, u32 flags)
{
	int ret = -EINVAL;
	struct lt8912 *pdata = lt8912_get_platform_data(client);

	if (!pdata) {
		pr_err("%s: invalid platform data\n", __func__);
		return ret;
	}

	pr_debug("%s: %d\n", __func__, on);
	mutex_lock(&pdata->ops_mutex);

	if (on && !pdata->is_power_on) {
		lt8912_write_array(pdata, lt8912_init_setup,
					sizeof(lt8912_init_setup));

		lt8912_write_array(pdata, lt8912_mipi_basic_set,
                                sizeof(lt8912_mipi_basic_set));
		pdata->is_power_on = true;
	} else if (!on) {
		/* power down hdmi */
		pdata->is_power_on = false;
		lt8912_notify_clients(&pdata->dev_info,
			MSM_DBA_CB_HPD_DISCONNECT);
	}

	mutex_unlock(&pdata->ops_mutex);
	return ret;
}

#if 0
static void lt8912_video_setup(struct lt8912 *pdata,
    struct msm_dba_video_cfg *cfg)
{
    u32 h_total, hpw, hfp, hbp;
    u32 v_total, vpw, vfp, vbp;

    if (!pdata || !cfg) {
        pr_err("%s: invalid input\n", __func__);
        return;
    }

    h_total = cfg->h_active + cfg->h_front_porch +
          cfg->h_pulse_width + cfg->h_back_porch;
    v_total = cfg->v_active + cfg->v_front_porch +
          cfg->v_pulse_width + cfg->v_back_porch;

    hpw = cfg->h_pulse_width;
    hfp = cfg->h_front_porch;
    hbp = cfg->h_back_porch;

    vpw = cfg->v_pulse_width;
    vfp = cfg->v_front_porch;
    vbp = cfg->v_back_porch;

    pr_debug("h_total 0x%x, h_active 0x%x, hfp 0x%d, hpw 0x%x, hbp 0x%x\n",
        h_total, cfg->h_active, cfg->h_front_porch,
        cfg->h_pulse_width, cfg->h_back_porch);

    pr_debug("v_total 0x%x, v_active 0x%x, vfp 0x%x, vpw 0x%x, vbp 0x%x\n",
        v_total, cfg->v_active, cfg->v_front_porch,
        cfg->v_pulse_width, cfg->v_back_porch);

	#if 0
    /* h_width */
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x28, ((h_total & 0xFF0) >> 4));
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x29, ((h_total & 0xF) << 4));

    /* hsync_width */
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x2A, ((hpw & 0xFF0) >> 4));
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x2B, ((hpw & 0xF) << 4));

    /* hfp */
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x2C, ((hfp & 0xFF0) >> 4));
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x2D, ((hfp & 0xF) << 4));

    /* hbp */
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x2E, ((hbp & 0xFF0) >> 4));
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x2F, ((hbp & 0xF) << 4));

    /* v_total */
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x30, ((v_total & 0xFF0) >> 4));
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x31, ((v_total & 0xF) << 4));

    /* vsync_width */
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x32, ((vpw & 0xFF0) >> 4));
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x33, ((vpw & 0xF) << 4));

    /* vfp */
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x34, ((vfp & 0xFF0) >> 4));
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x35, ((vfp & 0xF) << 4));

    /* vbp */
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x36, ((vbp & 0xFF0) >> 4));
    lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x37, ((vbp & 0xF) << 4));
	#endif
}
#endif

static int lt8912_video_on(void *client, bool on,
	struct msm_dba_video_cfg *cfg, u32 flags)
{
	int ret = -EINVAL;
	struct lt8912 *pdata = lt8912_get_platform_data(client);

//	if (!pdata || !cfg) {
	if (!pdata) {  //cfg could not been read now
		pr_err("%s: invalid platform data\n", __func__);
		return ret;
	}
	pr_debug("%s: enter\n", __func__);
	mutex_lock(&pdata->ops_mutex);
	
//	lt8912_video_setup(pdata, cfg);
#if HDMI_MODE_480P
	lt8912_write_array(pdata, lt8912_480p_mode,
				sizeof(lt8912_480p_mode));
#elif HDMI_MODE_720P
	lt8912_write_array(pdata, lt8912_720p_mode,
				sizeof(lt8912_720p_mode));
#elif HDMI_MODE_1080P
	lt8912_write_array(pdata, lt8912_1080p_mode,
				sizeof(lt8912_1080p_mode));
#endif

	lt8912_write_array(pdata, lt8912_ddsconfig,
				sizeof(lt8912_ddsconfig));

	lt8912_write_array(pdata, lt8912_rxlogicres,
				sizeof(lt8912_rxlogicres));

	mutex_unlock(&pdata->ops_mutex);
	
	ret = 0;
	return ret;
}

static int lt8912_get_edid_size(void *client, u32 *size, u32 flags)
{
	int ret = 0;
	struct lt8912 *pdata =
		lt8912_get_platform_data(client);

	if (!pdata) {
		pr_err("%s: invalid platform data\n", __func__);
		return ret;
	}

	mutex_lock(&pdata->ops_mutex);

	if (!size) {
		ret = -EINVAL;
		goto end;
	}

	*size = EDID_SEG_SIZE;
end:
	mutex_unlock(&pdata->ops_mutex);
	return ret;
}

#if 0
static int lt8912_configure_audio(void *client,
	struct msm_dba_audio_cfg *cfg, u32 flags)
{
	int ret = -EINVAL;
	int sampling_rate = 0;
	struct lt8912 *pdata =
		lt8912_get_platform_data(client);
	struct lt8912_reg_cfg reg_cfg[] = {
		{I2C_ADDR_MAIN, 0x12, 0x00, 0},
		{I2C_ADDR_MAIN, 0x13, 0x00, 0},
		{I2C_ADDR_MAIN, 0x14, 0x00, 0},
		{I2C_ADDR_MAIN, 0x15, 0x00, 0},
		{I2C_ADDR_MAIN, 0x0A, 0x00, 0},
		{I2C_ADDR_MAIN, 0x0C, 0x00, 0},
		{I2C_ADDR_MAIN, 0x0D, 0x00, 0},
		{I2C_ADDR_MAIN, 0x03, 0x00, 0},
		{I2C_ADDR_MAIN, 0x02, 0x00, 0},
		{I2C_ADDR_MAIN, 0x01, 0x00, 0},
		{I2C_ADDR_MAIN, 0x09, 0x00, 0},
		{I2C_ADDR_MAIN, 0x08, 0x00, 0},
		{I2C_ADDR_MAIN, 0x07, 0x00, 0},
		{I2C_ADDR_MAIN, 0x73, 0x00, 0},
		{I2C_ADDR_MAIN, 0x76, 0x00, 0},
	};

	if (!pdata || !cfg) {
		pr_err("%s: invalid data\n", __func__);
		return ret;
	}

	mutex_lock(&pdata->ops_mutex);

	if (cfg->copyright == MSM_DBA_AUDIO_COPYRIGHT_NOT_PROTECTED)
		reg_cfg[0].val |= BIT(5);

	if (cfg->pre_emphasis == MSM_DBA_AUDIO_PRE_EMPHASIS_50_15us)
		reg_cfg[0].val |= BIT(2);

	if (cfg->clock_accuracy == MSM_DBA_AUDIO_CLOCK_ACCURACY_LVL1)
		reg_cfg[0].val |= BIT(0);
	else if (cfg->clock_accuracy == MSM_DBA_AUDIO_CLOCK_ACCURACY_LVL3)
		reg_cfg[0].val |= BIT(1);

	reg_cfg[1].val = cfg->channel_status_category_code;

	reg_cfg[2].val = (cfg->channel_status_word_length & 0xF) << 0 |
		(cfg->channel_status_source_number & 0xF) << 4;

	if (cfg->sampling_rate == MSM_DBA_AUDIO_32KHZ)
		sampling_rate = 0x3;
	else if (cfg->sampling_rate == MSM_DBA_AUDIO_44P1KHZ)
		sampling_rate = 0x0;
	else if (cfg->sampling_rate == MSM_DBA_AUDIO_48KHZ)
		sampling_rate = 0x2;
	else if (cfg->sampling_rate == MSM_DBA_AUDIO_88P2KHZ)
		sampling_rate = 0x8;
	else if (cfg->sampling_rate == MSM_DBA_AUDIO_96KHZ)
		sampling_rate = 0xA;
	else if (cfg->sampling_rate == MSM_DBA_AUDIO_176P4KHZ)
		sampling_rate = 0xC;
	else if (cfg->sampling_rate == MSM_DBA_AUDIO_192KHZ)
		sampling_rate = 0xE;

	reg_cfg[3].val = (sampling_rate & 0xF) << 4;

	if (cfg->mode == MSM_DBA_AUDIO_MODE_MANUAL)
		reg_cfg[4].val |= BIT(7);

	if (cfg->interface == MSM_DBA_AUDIO_SPDIF_INTERFACE)
		reg_cfg[4].val |= BIT(4);

	if (cfg->interface == MSM_DBA_AUDIO_I2S_INTERFACE) {
		/* i2s enable */
		reg_cfg[5].val |= BIT(2);

		/* audio samp freq select */
		reg_cfg[5].val |= BIT(7);
	}

	/* format */
	reg_cfg[5].val |= cfg->i2s_fmt & 0x3;

	/* channel status override */
	reg_cfg[5].val |= (cfg->channel_status_source & 0x1) << 6;

	/* sample word lengths, default 24 */
	reg_cfg[6].val |= 0x18;

	/* endian order of incoming I2S data */
	if (cfg->word_endianness == MSM_DBA_AUDIO_WORD_LITTLE_ENDIAN)
		reg_cfg[6].val |= 0x1 << 7;

	/* compressed audio v - bit */
	reg_cfg[6].val |= (cfg->channel_status_v_bit & 0x1) << 5;

	/* ACR - N */
	reg_cfg[7].val |= (cfg->n & 0x000FF) >> 0;
	reg_cfg[8].val |= (cfg->n & 0x0FF00) >> 8;
	reg_cfg[9].val |= (cfg->n & 0xF0000) >> 16;

	/* ACR - CTS */
	reg_cfg[10].val |= (cfg->cts & 0x000FF) >> 0;
	reg_cfg[11].val |= (cfg->cts & 0x0FF00) >> 8;
	reg_cfg[12].val |= (cfg->cts & 0xF0000) >> 16;

	/* channel count */
	reg_cfg[13].val |= (cfg->channels & 0x3);

	/* CA */
	reg_cfg[14].val = cfg->channel_allocation;

	lt8912_write_array(pdata, reg_cfg, sizeof(reg_cfg));

	mutex_unlock(&pdata->ops_mutex);
	return ret;
}
#endif

/*static int lt8912_hdmi_cec_write(void *client, u32 size,
	char *buf, u32 flags)
{
	int ret = -EINVAL;
	struct lt8912 *pdata =
		lt8912_get_platform_data(client);

	if (!pdata) {
		pr_err("%s: invalid platform data\n", __func__);
		return ret;
	}

	mutex_lock(&pdata->ops_mutex);

	ret = lt8912_cec_prepare_msg(pdata, buf, size);
	if (ret)
		goto end;

	lt8912_write(pdata, I2C_ADDR_CEC_DSI, 0x81, 0x07);
end:
	mutex_unlock(&pdata->ops_mutex);
	return ret;
}*/

/*static int lt8912_hdmi_cec_read(void *client, u32 *size, char *buf, u32 flags)
{
	int ret = -EINVAL;
	int i;
	struct lt8912 *pdata =
		lt8912_get_platform_data(client);

	if (!pdata) {
		pr_err("%s: invalid platform data\n", __func__);
		return ret;
	}

	mutex_lock(&pdata->ops_mutex);

	for (i = 0; i < LT8912_CEC_BUF_MAX; i++) {
		struct lt8912_cec_msg *msg = &pdata->cec_msg[i];

		if (msg->pending && msg->timestamp) {
			memcpy(buf, msg->buf, CEC_MSG_SIZE);
			msg->pending = false;
			break;
		}
	}

	if (i < LT8912_CEC_BUF_MAX) {
		*size = CEC_MSG_SIZE;
		ret = 0;
	} else {
		pr_err("%s: no pending cec msg\n", __func__);
		*size = 0;
	}

	mutex_unlock(&pdata->ops_mutex);
	return ret;
}
*/

static int lt8912_get_raw_edid(void *client,
	u32 size, char *buf, u32 flags)
{
	struct lt8912 *pdata =
		lt8912_get_platform_data(client);

	if (!pdata || !buf) {
		pr_err("%s: invalid data\n", __func__);
		goto end;
	}

	mutex_lock(&pdata->ops_mutex);

	size = min_t(u32, size, sizeof(pdata->edid_buf));

	memcpy(buf, pdata->edid_buf, size);
end:
	mutex_unlock(&pdata->ops_mutex);
	return 0;
}

static int lt8912_write_reg(struct msm_dba_device_info *dev,
		u32 reg, u32 val)
{
	struct lt8912 *pdata;
	int ret = -EINVAL;
	u8 i2ca = 0;

	if (!dev)
		goto end;

	pdata = container_of(dev, struct lt8912, dev_info);
	if (!pdata)
		goto end;

	i2ca = I2C_ADDR_MAIN;

	lt8912_write(pdata, i2ca, (u8)(reg & 0xFF), (u8)(val & 0xFF));
end:
	return ret;
}

static int lt8912_read_reg(struct msm_dba_device_info *dev,
		u32 reg, u32 *val)
{
	int ret = 0;
	u8 byte_val = 0;
	u8 i2ca = 0;
	struct lt8912 *pdata;

	if (!dev)
		goto end;

	pdata = container_of(dev, struct lt8912, dev_info);
	if (!pdata)
		goto end;

	i2ca = I2C_ADDR_MAIN;

	lt8912_read(pdata, i2ca, (u8)(reg & 0xFF), &byte_val, 1);

	*val = (u32)byte_val;

end:
	return ret;
}

static int lt8912_register_dba(struct lt8912 *pdata)
{
	struct msm_dba_ops *client_ops;
	struct msm_dba_device_ops *dev_ops;

	if (!pdata)
		return -EINVAL;

	client_ops = &pdata->dev_info.client_ops;
	dev_ops = &pdata->dev_info.dev_ops;

	client_ops->power_on        = lt8912_power_on;
	client_ops->video_on        = lt8912_video_on;
	//client_ops->configure_audio = lt8912_configure_audio;
	client_ops->hdcp_enable     = NULL;
	/*client_ops->hdmi_cec_on     = lt8912_cec_enable;
	client_ops->hdmi_cec_write  = lt8912_hdmi_cec_write;
	client_ops->hdmi_cec_read   = lt8912_hdmi_cec_read;*/
	client_ops->get_edid_size   = lt8912_get_edid_size;
	client_ops->get_raw_edid    = lt8912_get_raw_edid;
	client_ops->check_hpd	    = lt8912_check_hpd;
	client_ops->get_audio_block = lt8912_get_audio_block;
	client_ops->set_audio_block = lt8912_set_audio_block;

	dev_ops->write_reg = lt8912_write_reg;
	dev_ops->read_reg = lt8912_read_reg;
	dev_ops->dump_debug_info = lt8912_dump_debug_info;

	strlcpy(pdata->dev_info.chip_name, "lt8912",
		sizeof(pdata->dev_info.chip_name));

	mutex_init(&pdata->dev_info.dev_mutex);

	INIT_LIST_HEAD(&pdata->dev_info.client_list);

	return msm_dba_add_probed_device(&pdata->dev_info);
}

static void lt8912_unregister_dba(struct lt8912 *pdata)
{
	if (!pdata)
		return;

	msm_dba_remove_probed_device(&pdata->dev_info);
}

static int lt8912_probe(struct i2c_client *client,
	 const struct i2c_device_id *id)
{
	static struct lt8912 *pdata;
	int ret = 0;

	if (!client || !client->dev.of_node) {
		pr_err("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	pdata = devm_kzalloc(&client->dev,
		sizeof(struct lt8912), GFP_KERNEL);
	if (!pdata)
	{
		pr_err("alloc pdata failed\n");
		return -ENOMEM;
	}

	ret = lt8912_parse_dt(&client->dev, pdata);
	if (ret) {
		pr_err("%s: Failed to parse DT\n", __func__);
		goto err_dt_parse;
	}

	pdata->i2c_client = client;
	
	ret = lt8912_gpio_configure(pdata, true);
	if (ret) {
		pr_err("%s: Failed to configure GPIOs\n", __func__);
		goto err_dt_parse;
	}

	mutex_init(&pdata->ops_mutex);

	ret = lt8912_read_device_rev(pdata);
	if (ret) {
		pr_err("%s: Failed to read chip rev\n", __func__);
		goto err_i2c_rev;
	}

	ret = lt8912_register_dba(pdata);
	if (ret) {
		pr_err("%s: Error registering with DBA %d\n",
			__func__, ret);
		goto err_dba_reg;
	}

	ret = pinctrl_select_state(pdata->ts_pinctrl,
		pdata->pinctrl_state_active);
	if (ret < 0)
		pr_err("%s: Failed to select %s pinstate %d\n",
			__func__, PINCTRL_STATE_ACTIVE, ret);

	#if 0
	pdata->irq = gpio_to_irq(pdata->irq_gpio);
	ret = request_threaded_irq(pdata->irq, NULL, lt8912_irq,
        IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "lt8912", pdata);
    if (ret) {
        pr_err("%s: Failed to enable ADV7533 interrupt\n",
            __func__);
        goto err_irq;
    }	
	#endif

	dev_set_drvdata(&client->dev, &pdata->dev_info);
	ret = msm_dba_helper_sysfs_init(&client->dev);
	if (ret) {
		pr_err("%s: sysfs init failed\n", __func__);
		goto err_dba_helper;
	}

	pdata->workq = create_workqueue("lt8912_workq");
	if (!pdata->workq) {
		pr_err("%s: workqueue creation failed.\n", __func__);
		ret = -EPERM;
		goto err_workqueue;
	}

	if (pdata->audio) {
		pr_debug("%s: enabling default audio configs\n", __func__);
		lt8912_write_array(pdata, I2S_cfg, sizeof(I2S_cfg));
	}

	//INIT_DELAYED_WORK(&pdata->lt8912_intr_work_id, lt8912_intr_work);
	INIT_DELAYED_WORK(&pdata->lt8912_check_hpd_work_id, lt8912_check_hpd_work);  //there is issue in irq, so we use work queue to read connect status
	queue_delayed_work(pdata->workq, &pdata->lt8912_check_hpd_work_id, 10 * 100);

	pm_runtime_enable(&client->dev);
	pm_runtime_set_active(&client->dev);

	return 0;
err_workqueue:
	msm_dba_helper_sysfs_remove(&client->dev);
err_dba_helper:
	disable_irq(pdata->irq);
	free_irq(pdata->irq, pdata);
	lt8912_unregister_dba(pdata);
err_dba_reg:
err_i2c_rev:
	lt8912_gpio_configure(pdata, false);
err_dt_parse:
	devm_kfree(&client->dev, pdata);
	return ret;
}

static int lt8912_remove(struct i2c_client *client)
{
	int ret = -EINVAL;
	struct msm_dba_device_info *dev;
	struct lt8912 *pdata;

	if (!client)
		goto end;

	dev = dev_get_drvdata(&client->dev);
	if (!dev)
		goto end;

	pdata = container_of(dev, struct lt8912, dev_info);
	if (!pdata)
		goto end;

	pm_runtime_disable(&client->dev);
	disable_irq(pdata->irq);
	free_irq(pdata->irq, pdata);

	ret = lt8912_gpio_configure(pdata, false);

	mutex_destroy(&pdata->ops_mutex);

	devm_kfree(&client->dev, pdata);

end:
	return ret;
}

static const struct of_device_id lt8912_dt_match[] = {
        {.compatible = "qcom,lt8912"},
        {}
};

static struct i2c_driver lt8912_driver = {
	.driver = {
		.name = "lt8912",
		.of_match_table = lt8912_dt_match,
		.owner = THIS_MODULE,
	},
	.probe = lt8912_probe,
	.remove = lt8912_remove,
	.id_table = lt8912_id,
};

static int __init lt8912_init(void)
{
	return i2c_add_driver(&lt8912_driver);
}

static void __exit lt8912_exit(void)
{
	i2c_del_driver(&lt8912_driver);
}

module_param_string(panel, mdss_mdp_panel, MDSS_MAX_PANEL_LEN, 0);

module_init(lt8912_init);
module_exit(lt8912_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("lt8912 driver");
