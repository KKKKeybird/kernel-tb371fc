/*
 * KTZ Semiconductor KTZ8866b LED Driver
 *
 * Copyright (C) 2013 Ideas on board SPRL
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/backlight.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/ktz8866ab.h>

#define BL_I2C_ADDRESS			  0x11
#define u8	unsigned char
#define LCD_BL_BIAS_I2C_ID_NAME "ktz8866b"

static DEFINE_MUTEX(read_lock);
static struct i2c_client *lcd_bl_bias_i2c_client;
static int lcd_bl_bias_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int lcd_bl_bias_i2c_remove(struct i2c_client *client);

int lcd_bl_bias_write_byte(unsigned char addr, unsigned char value)
{
	int ret = 0;
	unsigned char write_data[2] = {0};

	write_data[0] = addr;
	write_data[1] = value;

	if (NULL == lcd_bl_bias_i2c_client) {
		dev_warn(&lcd_bl_bias_i2c_client->dev, "[LCD][BL] lcd_bl_bias_i2c_client is null!!\n");
		return -EINVAL;
	}
	ret = i2c_master_send(lcd_bl_bias_i2c_client, write_data, 2);

	if (ret < 0)
		dev_warn(&lcd_bl_bias_i2c_client->dev, "[LCD][BL] i2c write data fail !!\n");

	return ret;
}
EXPORT_SYMBOL(lcd_bl_bias_write_byte);

static noinline int lcd_bl_bias_read_byte(u8 regnum)
{
	u8 buffer[1], reg_value[1];
	int res = 0;

	if (NULL == lcd_bl_bias_i2c_client) {
		dev_warn(&lcd_bl_bias_i2c_client->dev, "[LCD][BL] lcd_bl_bias_i2c_client is null!!\n");
		return -EINVAL;
	}

	mutex_lock(&read_lock);

	buffer[0] = regnum;
	res = i2c_master_send(lcd_bl_bias_i2c_client, buffer, 0x1);
	if (res <= 0)	{
	  mutex_unlock(&read_lock);
	  dev_warn(&lcd_bl_bias_i2c_client->dev, "read reg send res = %d\n", res);
	  return res;
	}
	res = i2c_master_recv(lcd_bl_bias_i2c_client, reg_value, 0x1);
	if (res <= 0) {
	  mutex_unlock(&read_lock);
	  dev_warn(&lcd_bl_bias_i2c_client->dev, "read reg recv res = %d\n", res);
	  return res;
	}
	mutex_unlock(&read_lock);

	return reg_value[0];
}

static const struct of_device_id i2c_of_match[] = {
	{ .compatible = "ktz,ktz8866b", },
	{},
};

static const struct i2c_device_id lcd_bl_bias_i2c_id[] = {
	{LCD_BL_BIAS_I2C_ID_NAME, 0},
	{},
};

static struct i2c_driver lcd_bl_bias_i2c_driver = {
/************************************************************
Attention:
Althouh i2c_bus do not use .id_table to match, but it must be defined,
otherwise the probe function will not be executed!
************************************************************/
	.id_table = lcd_bl_bias_i2c_id,
	.probe = lcd_bl_bias_i2c_probe,
	.remove = lcd_bl_bias_i2c_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = LCD_BL_BIAS_I2C_ID_NAME,
		.of_match_table = i2c_of_match,
	},
};

int lcd_set_bias(bool enable)
{
        dev_warn(&lcd_bl_bias_i2c_client->dev, "[%s], bias:value = %d", enable);
        if (enable) {
                if (strstr(saved_command_line,
                           "mdss_dsi_panel_spinel_boe_nt36532_dsc_3k_video"))
                        lcd_bl_bias_write_byte(KTZ8866_DISP_BC1, 0xDA);
                else
                        lcd_bl_bias_write_byte(KTZ8866_DISP_BC1, 0xDB);
                lcd_bl_bias_write_byte(KTZ8866_DISP_BC2, 0xED);
                lcd_bl_bias_write_byte(KTZ8866_DISP_DIMMING, 0xAA);
                lcd_bl_bias_write_byte(KTZ8866_DISP_OPTION2, 0x37);
                lcd_bl_bias_write_byte(KTZ8866_DISP_FULL_CURRENT, 0xA5);
                lcd_bl_bias_write_byte(KTZ8866_DISP_BIAS_BOOST, 0x2E);
                lcd_bl_bias_write_byte(KTZ8866_DISP_BIAS_VPOS, 0x24);
                lcd_bl_bias_write_byte(KTZ8866_DISP_BIAS_VNEG, 0x24);
                lcd_bl_bias_write_byte(KTZ8866_DISP_BIAS_CONF1, 0x9C);
                lcd_bl_bias_read_byte(KTZ8866_DISP_BIAS_CONF1);
                udelay(5000);
                lcd_bl_bias_write_byte(KTZ8866_DISP_BIAS_CONF1, 0x9E);
                lcd_bl_bias_read_byte(KTZ8866_DISP_BIAS_CONF1);
        } else {
                lcd_bl_bias_write_byte(KTZ8866_DISP_BIAS_CONF1, 0x9C);
                lcd_bl_bias_read_byte(KTZ8866_DISP_BIAS_CONF1);
                udelay(5000);
                lcd_bl_bias_write_byte(KTZ8866_DISP_BIAS_CONF1, 0x98);
                lcd_bl_bias_read_byte(KTZ8866_DISP_BIAS_CONF1);
        }
        return 0;
}
EXPORT_SYMBOL(lcd_set_bias);

static int lcd_bl_bias_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
        int ret;

        if (NULL == client) {
                dev_warn(&lcd_bl_bias_i2c_client->dev, "[LCD][BL] i2c_client is NULL\n");
                return -EINVAL;
        }

        lcd_bl_bias_i2c_client = client;
        dev_warn(&lcd_bl_bias_i2c_client->dev, "backlight, i2c address: %0x\n", client->addr);

        ret = lcd_bl_bias_write_byte(KTZ8866_DISP_BIAS_BOOST, 0x2E);
        ret = lcd_bl_bias_write_byte(KTZ8866_DISP_BIAS_VPOS, 0x24);
        ret = lcd_bl_bias_write_byte(KTZ8866_DISP_BIAS_VNEG, 0x24);
        ret = lcd_bl_bias_write_byte(KTZ8866_DISP_BIAS_CONF1, 0x99);
        ret = lcd_bl_bias_write_byte(KTZ8866_DISP_BC2, 0xED);
        if (strstr(saved_command_line,
                   "mdss_dsi_panel_spinel_boe_nt36532_dsc_3k_video"))
                ret = lcd_bl_bias_write_byte(KTZ8866_DISP_BC1, 0xDA);
        else
                ret = lcd_bl_bias_write_byte(KTZ8866_DISP_BC1, 0xDB);
        ret = lcd_bl_bias_write_byte(KTZ8866_DISP_DIMMING, 0xAA);
        ret = lcd_bl_bias_write_byte(KTZ8866_DISP_OPTION2, 0x37);
        ret = lcd_bl_bias_write_byte(KTZ8866_DISP_FULL_CURRENT, 0xA5);
        ret = lcd_bl_bias_write_byte(KTZ8866_DISP_BL_ENABLE, 0x4F);
        if (ret < 0) {
                dev_warn(&lcd_bl_bias_i2c_client->dev, "I2C write reg is fail!");
                return -EINVAL;
        } else {
                dev_warn(&lcd_bl_bias_i2c_client->dev, "I2C write reg is success!");
        }

        return 0;
}
static int lcd_bl_bias_i2c_remove(struct i2c_client *client)
{
        lcd_bl_bias_i2c_client = NULL;
        i2c_unregister_device(client);
        return 0;
}
module_i2c_driver(lcd_bl_bias_i2c_driver);

MODULE_AUTHOR("caiyifeng <caiyifeng@huanqin.com>");
MODULE_DESCRIPTION("LCD BL I2C Driver");
MODULE_LICENSE("GPL");