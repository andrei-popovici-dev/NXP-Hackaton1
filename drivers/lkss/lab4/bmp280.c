// SPDX-License-Identifier: GPL-2.0
/*
 * LKSS Day 4 - BMP280 I2C pressure/temperature sensor driver (LAB SKELETON)
 *
 * This is a teaching skeleton. Complete every block marked
 *
 *      TODO n
 *
 * The number n matches the exercise/sub-task in the Day 4 lab text.
 * Lines that already work are left intact; do not delete the scaffolding.
 *
 * Build: enable CONFIG_LKSS_DRIVERS_LAB4 in menuconfig.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/unaligned.h>

/* ------------------------------------------------------------------ */
/* Register map (BMP280 datasheet section 4)                          */
/* ------------------------------------------------------------------ */
#define BMP280_REG_ID         0xD0
#define BMP280_REG_RESET      0xE0
#define BMP280_REG_STATUS     0xF3
#define BMP280_REG_CTRL_MEAS  0xF4
#define BMP280_REG_CONFIG     0xF5
#define BMP280_REG_PRESS_MSB  0xF7   /* press[3] then temp[3] = 6 bytes */
#define BMP280_REG_CALIB00    0x88   /* 24 calibration bytes: 0x88..0x9F */

#define BMP280_CHIP_ID        0x58
#define BMP280_CALIB_LEN      24
#define BMP280_RESET_VALUE    0xB6

/* ctrl_meas = osrs_t x1 (001) | osrs_p x1 (001) | forced mode (01) = 0x27 */
#define BMP280_CTRL_MEAS_FORCED   0x27

#define BMP280_STATUS_MEASURING   BIT(3)

/* ===== TODO 7 (given) ===== per-chip factory calibration coefficients */
struct bmp280_calib {
	u16 dig_T1;
	s16 dig_T2;
	s16 dig_T3;
	u16 dig_P1;
	s16 dig_P2;
	s16 dig_P3;
	s16 dig_P4;
	s16 dig_P5;
	s16 dig_P6;
	s16 dig_P7;
	s16 dig_P8;
	s16 dig_P9;
};

/* ===== TODO 11 + TODO 15 ===== driver private data */
struct bmp280_data {
	struct i2c_client    *client;
	struct bmp280_calib   calib;
	s32                   t_fine;
	struct mutex          lock;        /* serialises trigger+read on the bus */
	struct timer_list     poll_timer;  /* Exercise 8 */
	struct work_struct    poll_work;   /* Exercise 8 */
};

/* ================================================================== */
/* Low-level sensor helpers                                           */
/* ================================================================== */

/* ===== TODO 5 ===== kick off one forced-mode measurement */
static int bmp280_trigger_measurement(struct i2c_client *client)
{
	return i2c_smbus_write_byte_data(client, BMP280_REG_CTRL_MEAS,
					 BMP280_CTRL_MEAS_FORCED);
}

/* ===== TODO 6 ===== burst-read the 6 raw bytes and unpack two 20-bit values */
static int bmp280_read_raw(struct i2c_client *client, s32 *adc_t, s32 *adc_p)
{
	u8 buf[6];
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, BMP280_REG_PRESS_MSB,
					    sizeof(buf), buf);
	if (ret < 0)
		return ret;
	if (ret != sizeof(buf))
		return -EIO;

	*adc_p = ((s32)buf[0] << 12) | ((s32)buf[1] << 4) | (buf[2] >> 4);
	*adc_t = ((s32)buf[3] << 12) | ((s32)buf[4] << 4) | (buf[5] >> 4);
	return 0;
}

/* ===== TODO 8 ===== read + decode the 24 little-endian calibration bytes */
static int bmp280_read_calib(struct i2c_client *client,
			     struct bmp280_calib *calib)
{
	u8 buf[BMP280_CALIB_LEN];
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, BMP280_REG_CALIB00,
					    BMP280_CALIB_LEN, buf);
	if (ret < 0)
		return ret;
	if (ret != BMP280_CALIB_LEN)
		return -EIO;

	calib->dig_T1 =      get_unaligned_le16(buf + 0);
	calib->dig_T2 = (s16)get_unaligned_le16(buf + 2);
	calib->dig_T3 = (s16)get_unaligned_le16(buf + 4);
	calib->dig_P1 =      get_unaligned_le16(buf + 6);
	calib->dig_P2 = (s16)get_unaligned_le16(buf + 8);
	calib->dig_P3 = (s16)get_unaligned_le16(buf + 10);
	calib->dig_P4 = (s16)get_unaligned_le16(buf + 12);
	calib->dig_P5 = (s16)get_unaligned_le16(buf + 14);
	calib->dig_P6 = (s16)get_unaligned_le16(buf + 16);
	calib->dig_P7 = (s16)get_unaligned_le16(buf + 18);
	calib->dig_P8 = (s16)get_unaligned_le16(buf + 20);
	calib->dig_P9 = (s16)get_unaligned_le16(buf + 22);
	return 0;
}

/* ===== TODO 9 ===== temperature compensation (datasheet 3.11.3), s32 maths */
static s32 bmp280_compensate_temp(struct bmp280_calib *c, s32 adc_T,
				  s32 *t_fine)
{
	s32 var1, var2;

	var1 = ((((adc_T >> 3) - ((s32)c->dig_T1 << 1))) * ((s32)c->dig_T2)) >> 11;
	var2 = (((((adc_T >> 4) - (s32)c->dig_T1) *
		  ((adc_T >> 4) - (s32)c->dig_T1)) >> 12) * (s32)c->dig_T3) >> 14;
	*t_fine = var1 + var2;
	return (*t_fine * 5 + 128) >> 8;   /* 0.01 degC */
}

/* ===== TODO 10 ===== pressure compensation (datasheet 3.11.3), s64 maths.
 * Returns Q24.8 Pa: result / 256 == Pa. */
static u32 bmp280_compensate_press(struct bmp280_calib *c, s32 adc_P,
				   s32 t_fine)
{
	s64 var1, var2, p;

	var1 = ((s64)t_fine) - 128000;
	var2 = var1 * var1 * (s64)c->dig_P6;
	var2 = var2 + ((var1 * (s64)c->dig_P5) << 17);
	var2 = var2 + (((s64)c->dig_P4) << 35);
	var1 = ((var1 * var1 * (s64)c->dig_P3) >> 8) +
	       ((var1 * (s64)c->dig_P2) << 12);
	var1 = (((((s64)1) << 47) + var1)) * ((s64)c->dig_P1) >> 33;

	if (var1 == 0)
		return 0;       /* avoid divide-by-zero */

	p = 1048576 - adc_P;
	p = (((p << 31) - var2) * 3125) / var1;
	var1 = (((s64)c->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
	var2 = (((s64)c->dig_P8) * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (((s64)c->dig_P7) << 4);

	return (u32)p;       /* Q24.8 Pa */
}

/*
 * Convenience: trigger -> wait -> read -> compensate, under the bus mutex so
 * concurrent sysfs reads and the periodic worker never interleave on the wire
 * or clobber t_fine. (Exercise 7, question 2.)
 */
static int bmp280_measure(struct bmp280_data *data, s32 *temp_cC, u32 *press_q24)
{
	s32 adc_t, adc_p;
	int ret;

	mutex_lock(&data->lock);

	ret = bmp280_trigger_measurement(data->client);
	if (ret < 0)
		goto out;

	msleep(10); /* worst-case conversion time at osrs x1 */

	ret = bmp280_read_raw(data->client, &adc_t, &adc_p);
	if (ret < 0)
		goto out;

	*temp_cC   = bmp280_compensate_temp(&data->calib, adc_t, &data->t_fine);
	*press_q24 = bmp280_compensate_press(&data->calib, adc_p, data->t_fine);
	ret = 0;
out:
	mutex_unlock(&data->lock);
	return ret;
}

/* ================================================================== */
/* sysfs attributes (Exercise 7)                                      */
/* ================================================================== */

/* ===== TODO 13 ===== temperature_show */
static ssize_t temperature_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bmp280_data *data = dev_get_drvdata(dev);
	s32 temp;
	u32 praw;
	int ret;

	ret = bmp280_measure(data, &temp, &praw);
	if (ret < 0)
		return ret;

	/* temp is in 0.01 degC. Note: this simple split drops the minus sign
	 * for values in (-1.00, 0.00) degC; fine for this lab's climate. */
	return sysfs_emit(buf, "%d.%02d\n", temp / 100, abs(temp % 100));
}

/* ===== TODO 13 ===== pressure_show */
static ssize_t pressure_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct bmp280_data *data = dev_get_drvdata(dev);
	s32 temp;
	u32 praw, pa;
	int ret;

	ret = bmp280_measure(data, &temp, &praw);
	if (ret < 0)
		return ret;

	pa = praw / 256;                 /* Q24.8 -> Pa */
	/* pa / 100 = hPa integer part, pa % 100 = hundredths of hPa */
	return sysfs_emit(buf, "%u.%02u\n", pa / 100, pa % 100);
}

/* ===== TODO 12 ===== declare the read-only attributes and group them */
static DEVICE_ATTR_RO(temperature);
static DEVICE_ATTR_RO(pressure);

static struct attribute *bmp280_attrs[] = {
	&dev_attr_temperature.attr,
	&dev_attr_pressure.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bmp280);   /* generates bmp280_groups[] */

/* ================================================================== */
/* Exercise 8 bonus: periodic sampling                                */
/* ================================================================== */

/* ===== TODO 18 ===== work handler - PROCESS context, may sleep / do I2C */
static void bmp280_poll_work(struct work_struct *w)
{
	struct bmp280_data *data = container_of(w, struct bmp280_data, poll_work);
	s32 temp;
	u32 praw, pa;

	if (bmp280_measure(data, &temp, &praw) < 0)
		return;

	pa = praw / 256;
	dev_info(&data->client->dev,
		 "poll: temperature = %d.%02d degC, pressure = %u.%02u hPa\n",
		 temp / 100, abs(temp % 100), pa / 100, pa % 100);
}

/* ===== TODO 17 ===== timer callback - ATOMIC context, must NOT do I2C */
static void bmp280_poll_timer(struct timer_list *t)
{
	/* from_timer() recovers the containing struct; on kernels >= 6.16 the
	 * same macro is spelled timer_container_of(). */
	struct bmp280_data *data = timer_container_of(data, t, poll_timer);

	/* I2C sleeps, so we cannot read here. Bounce to the workqueue ... */
	schedule_work(&data->poll_work);
	/* ... and re-arm for the next second. */
	mod_timer(&data->poll_timer, jiffies + HZ);
}

/* ================================================================== */
/* probe / remove                                                     */
/* ================================================================== */

static int bmp280_probe(struct i2c_client *client)
{
	struct bmp280_data *data;
	int ret, id;

	/* ===== TODO 2 ===== announce binding */
	dev_info(&client->dev, "BMP280 driver bound at address 0x%02x\n",
		 client->addr);

	/* ===== TODO 4 ===== read & verify chip ID */
	id = i2c_smbus_read_byte_data(client, BMP280_REG_ID);
	if (id < 0) {
		dev_err(&client->dev, "failed to read chip ID: %d\n", id);
		return id;
	}
	dev_info(&client->dev, "Chip ID: 0x%02x (expected 0x%02x)\n",
		 id, BMP280_CHIP_ID);
	if (id != BMP280_CHIP_ID)
		return -ENODEV;

	/* ===== TODO 11 ===== allocate + store private data */
	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->client = client;
	mutex_init(&data->lock);
	i2c_set_clientdata(client, data);

	/* ===== TODO 8 (call) ===== cache calibration once */
	ret = bmp280_read_calib(client, &data->calib);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read calibration: %d\n", ret);
		return ret;
	}

	/* sysfs attributes are created automatically from .dev_groups (TODO 14).
	 * Manual alternative:
	 *   ret = sysfs_create_group(&client->dev.kobj, &bmp280_group);
	 */

	/* ===== TODO 16 ===== set up the periodic poll (Exercise 8) */
	INIT_WORK(&data->poll_work, bmp280_poll_work);
	timer_setup(&data->poll_timer, bmp280_poll_timer, 0);
	mod_timer(&data->poll_timer, jiffies + HZ);

	return 0;
}

static void bmp280_remove(struct i2c_client *client)
{
	struct bmp280_data *data = i2c_get_clientdata(client);

	/* ===== TODO 19 ===== cancel producer (timer) BEFORE consumer (work).
	 * timer_delete_sync() guarantees the callback isn't mid-flight and
	 * won't re-arm; only then is it safe to drain the queued work. */

	timer_delete_sync(&data->poll_timer);
	cancel_work_sync(&data->poll_work);

	/* ===== TODO 3 ===== say goodbye */
	dev_info(&client->dev, "BMP280 driver removed\n");
}

/* ===== TODO 1 ===== match tables */
static const struct of_device_id bmp280_of_match[] = {
	{ .compatible = "lkss,bmp280" },
	{ }
};
MODULE_DEVICE_TABLE(of, bmp280_of_match);

static const struct i2c_device_id bmp280_id[] = {
	{ "bmp280", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bmp280_id);

static struct i2c_driver bmp280_driver = {
	.driver = {
		.name		= "lkss_bmp280",
		.of_match_table	= bmp280_of_match,
		.dev_groups	= bmp280_groups,   /* ===== TODO 14 ===== */
	},
	.probe		= bmp280_probe,
	.remove		= bmp280_remove,
	.id_table	= bmp280_id,
};
module_i2c_driver(bmp280_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("NXP Linux Kernel Summer School");
MODULE_DESCRIPTION("Lab4: BMP280 I2C pressure/temperature sensor driver");
