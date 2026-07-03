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

/* TODO 7  (given - study it)
 * Per-chip factory calibration coefficients.
 * Mind the signedness: dig_T1 and dig_P1 are UNSIGNED, the rest are SIGNED.
 */
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

/* TODO 11  driver private data
 * Add the timer_list and work_struct fields needed by Exercise 8.
 */
struct bmp280_data {
	struct i2c_client    *client;
	struct bmp280_calib   calib;
	s32                   t_fine;
	/* TODO 15 (Exercise 8 bonus) */
	struct timer_list poll_timer;
	struct work_struct poll_work;
};

/* ================================================================== */
/* Low-level sensor helpers                                           */
/* ================================================================== */

/* TODO 5  Exercise 5
 * Kick off a single forced-mode measurement.
 * Write BMP280_CTRL_MEAS_FORCED to BMP280_REG_CTRL_MEAS.
 * Return 0 on success or the negative errno from the i2c call.
 */
static int bmp280_trigger_measurement(struct i2c_client *client)
{
	/*
	 * ctrl_meas (0xF4):
	 *   osrs_t [7:5] = 001  -> temperature oversampling x1
	 *   osrs_p [4:2] = 001  -> pressure    oversampling x1
	 *   mode   [1:0] = 01   -> forced mode (one shot, then back to sleep)
	 * 001 001 01 = 0x27
	 */
	return i2c_smbus_write_byte_data(client, BMP280_REG_CTRL_MEAS, 0x27);
}

/* TODO 6  Exercise 5
 * Burst-read the 6 raw bytes at 0xF7 and unpack the two 20-bit ADC values.
 * Layout: buf[0..2] = pressure MSB/LSB/XLSB, buf[3..5] = temperature.
 * Remember the >> 4 on the XLSB byte. Check the return value (< 0 == error).
 */
static int bmp280_read_raw(struct i2c_client *client, s32 *adc_t, s32 *adc_p)
{
	u8 buf[6];
	int ret;

	/* ret = i2c_smbus_read_i2c_block_data(client, ......, 6, buf); */
	/* if (ret < 0) return ret; */
	/* *adc_p = ((s32)buf[0] << 12) | ((s32)buf[1] << 4) | (buf[2] >> 4); */
	/* *adc_t = ((s32)buf[3] << 12) | ((s32)buf[4] << 4) | (buf[5] >> 4); */
	(void)buf; (void)ret;
	ret = i2c_smbus_read_i2c_block_data(client, BMP280_REG_PRESS_MSB, 6, buf);
	if (ret < 0)
    	return ret;
	*adc_p = ((s32)buf[0] << 12) | ((s32)buf[1] << 4) | (buf[2] >> 4);
	*adc_t = ((s32)buf[3] << 12) | ((s32)buf[4] << 4) | (buf[5] >> 4);
	
	return ret;
}

/* TODO 8  Exercise 6
 * Read 24 calibration bytes from 0x88 and decode the little-endian s16/u16
 * fields into *calib (use get_unaligned_le16(buf + offset), cast to s16 for
 * the signed fields). Offsets: T1@0 T2@2 T3@4 P1@6 P2@8 ... P9@22.
 */
static int bmp280_read_calib(struct i2c_client *client,
			     struct bmp280_calib *calib)
{
	u8 buf[BMP280_CALIB_LEN];
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, BMP280_REG_CALIB00,
	                                    BMP280_CALIB_LEN, buf);
	if (ret < 0) return ret;
	
	calib->dig_T1 = get_unaligned_le16(buf + 0);
	calib->dig_T2 = (s16)get_unaligned_le16(buf + 2);
	calib->dig_T3 = (s16)get_unaligned_le16(buf + 4);
	calib->dig_P1 = (s16)get_unaligned_le16(buf + 6);
	calib->dig_P2 = (s16)get_unaligned_le16(buf + 8);
	calib->dig_P3 = (s16)get_unaligned_le16(buf + 10);
	calib->dig_P4 = (s16)get_unaligned_le16(buf + 12);
	calib->dig_P5 = (s16)get_unaligned_le16(buf + 14);
	calib->dig_P6 = (s16)get_unaligned_le16(buf + 16);
	calib->dig_P7 = (s16)get_unaligned_le16(buf + 18);
	calib->dig_P8 = (s16)get_unaligned_le16(buf + 20);
	calib->dig_P9 = (s16)get_unaligned_le16(buf + 22);
	
	(void)buf; (void)ret;

	return ret;
}

/* TODO 9  Exercise 6
 * Temperature compensation (datasheet 3.11.3). All locals s32.
 * Returns temperature in 0.01 degC; writes t_fine through the pointer.
 */
static s32 bmp280_compensate_temp(struct bmp280_calib *c, s32 adc_T,
				  s32 *t_fine)
{
    s32 var1, var2;

    var1 = ((((adc_T >> 3) - ((s32)c->dig_T1 << 1))) * ((s32)c->dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - (s32)c->dig_T1) *
              ((adc_T >> 4) - (s32)c->dig_T1)) >> 12) * (s32)c->dig_T3) >> 14;
    *t_fine = var1 + var2;
    return (*t_fine * 5 + 128) >> 8;   /* 0.01 degC units */
}

/* TODO 10  Exercise 6
 * Pressure compensation (datasheet 3.11.3). All locals s64.
 * Returns pressure in Q24.8 Pa (result / 256 == Pa).
 * See the lab Theory section for the complete formula.
 */
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

/* Convenience: trigger -> wait -> read -> compensate. Used by sysfs + bonus. */
static int bmp280_measure(struct bmp280_data *data, s32 *temp_cC, u32 *press_q24)
{
	s32 adc_t, adc_p;
	int ret;

	ret = bmp280_trigger_measurement(data->client);
	if (ret < 0)
		return ret;

	msleep(10); /* worst-case conversion time at osrs x1 */

	ret = bmp280_read_raw(data->client, &adc_t, &adc_p);
	if (ret < 0)
		return ret;

	*temp_cC   = bmp280_compensate_temp(&data->calib, adc_t, &data->t_fine);
	*press_q24 = bmp280_compensate_press(&data->calib, adc_p, data->t_fine);
	return 0;
}

/* ================================================================== */
/* sysfs attributes (Exercise 7)                                      */
/* ================================================================== */

/* TODO 13  Exercise 7
 * temperature_show: recover bmp280_data, take a measurement, emit "%d.%02d\n".
 */
static ssize_t temperature_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	/* recover your data with dev_get_drvdata(dev) */
	struct bmp280_data *data = dev_get_drvdata(dev);
	s32 temp; u32 praw;
	int ret;

	/* take a measurement, call bmp280_measure() */
	ret = bmp280_measure(data, &temp, &praw);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d.%02d\n", temp / 100, abs(temp % 100));
}

/* TODO 13  Exercise 7
 * pressure_show: same idea, convert Q24.8 -> hPa (/ 25600) and emit it.
 */
static ssize_t pressure_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	/* recover your data with dev_get_drvdata(dev) */
	struct bmp280_data *data = dev_get_drvdata(dev);
	s32 temp; u32 praw;
	int ret;

	/* take a measurement, call bmp280_measure() */
	ret = bmp280_measure(data, &temp, &praw);
	if (ret < 0)
		return ret;

	s32 phpa = praw / 256;
	return sysfs_emit(buf, "%d.%02d\n", phpa / 100, phpa % 100); 
}

/* TODO 12  Exercise 7
 * Declare the two read-only attributes and bundle them into a group.
 */
static DEVICE_ATTR_RO(temperature);
static DEVICE_ATTR_RO(pressure);

static struct attribute *bmp280_attrs[] = {
	&dev_attr_temperature.attr,
	&dev_attr_pressure.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bmp280);

/* ================================================================== */
/* Exercise 8 bonus: periodic sampling                                */
/* ================================================================== */

/* TODO 18  work handler: runs in process context, may sleep / do I2C */
static void bmp280_poll_work(struct work_struct *w){

}
/* TODO 17  timer callback: atomic context, must NOT do I2C            */
static void bmp280_poll_timer(struct timer_list *t){

}

/* ================================================================== */
/* probe / remove                                                     */
/* ================================================================== */

static int bmp280_probe(struct i2c_client *client)
{
	struct bmp280_data *data;
	int ret, id;

	/* TODO 2  Exercise 3: announce binding */
	/* dev_info(&client->dev, "BMP280 driver bound at address 0x%02x\n",
	 *          client->addr); */

	dev_info(&client->dev, "BMP280 driver bound at address 0x%02x\n",
         client->addr);

	/* TODO 4  Exercise 4: read & verify chip ID  - use "i2c_smbus_read_byte_data(const struct i2c_client *client, u8 command)" */
	id = i2c_smbus_read_byte_data(client, BMP280_REG_ID);
	if (id < 0) {
		dev_err(&client->dev, "failed to read chip ID: %d\n", id);
		return id;
	}
	dev_info(&client->dev, "Chip ID: 0x%02x (expected 0x%02x)\n",
		 id, BMP280_CHIP_ID);
	if (id != BMP280_CHIP_ID)
		return -ENODEV;

	/* TODO Exercise 5: Call bmp280_trigger_measurement + bmp280_read_raw, print the raw values */
	bmp280_trigger_measurement(client);

	s32 adc_t, adc_p;
	bmp280_read_raw(client, &adc_t, &adc_p);

	dev_info(&client->dev, "raw adc_t=%d adc_p=%d\n", adc_t, adc_p);

	/* TODO 11  Exercise 7: allocate + store private data */
	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->client = client;
	i2c_set_clientdata(client, data);

	/* TODO 8 (call)  Exercise 6: cache calibration once */
	ret = bmp280_read_calib(client, &data->calib);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read calibration: %d\n", ret);
		return ret;
	}

	s32 temp = bmp280_compensate_temp(&data->calib, adc_t, &data->t_fine);  /* 0.01 degC */
	u32 praw = bmp280_compensate_press(&data->calib, adc_p, data->t_fine);  /* Q24.8 Pa  */
	u32 phpa = praw / 256;                /* Pa  */
	dev_info(&client->dev,
         "temperature = %d.%02d degC, pressure = %u.%02u hPa\n",
         temp / 100, abs(temp % 100),
         phpa / 100, phpa % 100);

	/* The attribute group is created automatically via .dev_groups below
	 * (see TODO 14 note). If you prefer the manual route, call 
	sysfs_create_group(&client->dev.kobj, &bmp280_group); */

	/* TODO 16  Exercise 8: timer_setup() + INIT_WORK() + mod_timer() */
	timer_setup(data->poll_timer, bmp280_poll_work, 0);

	mod_timer(&data->poll_timer, jiffies + HZ)

	INIT_WORK(bmp280_poll_work, )


	return 0;
}

static void bmp280_remove(struct i2c_client *client)
{
	/* TODO 19  Exercise 8: timer_delete_sync() THEN cancel_work_sync()
	 * timer_delete_sync() guarantees the callback isn't mid-flight and
	 * won't re-arm; only then is it safe to drain the queued work. */
	
	/* struct bmp280_data *data = i2c_get_clientdata(client); */ /* uncomment me on Exercise 8 */
	/* TODO 3  Exercise 3: say goodbye */
	dev_info(&client->dev, "BMP280 driver removed\n");

}

/* TODO 1  Exercise 3: match tables
 * Fill in the OF compatible "lkss,bmp280" and the i2c_device_id "bmp280".
 * Keep the trailing { } sentinel on both tables.
 */
static const struct of_device_id bmp280_of_match[] = {
	{ .compatible = "lkss,bmp280" },
	{/* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bmp280_of_match);

static const struct i2c_device_id bmp280_id[] = {
	{ "bmp280", 0 },
	{/* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, bmp280_id);

static struct i2c_driver bmp280_driver = {
	.driver = {
		.name		= "lkss_bmp280",
		.of_match_table	= bmp280_of_match,
		/* TODO 14  Exercise 7: expose attributes the easy way */
		.dev_groups = bmp280_groups,
		
	},
	.probe		= bmp280_probe,
	.remove		= bmp280_remove,
	.id_table	= bmp280_id,
};
module_i2c_driver(bmp280_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("NXP Linux Kernel Summer School");
MODULE_DESCRIPTION("Lab4: BMP280 I2C pressure/temperature sensor driver");

//Valentin Porumbel
//Andrei Cherechesu