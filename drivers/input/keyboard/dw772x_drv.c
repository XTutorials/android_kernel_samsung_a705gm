/*
 * Vibrator driver for DW7720
 *
 * Copyright (C) 2018 Dongwoon Anatech Co. Ltd. All Rights Reserved.
 *
 * license : GPL/BSD Dual 
 *
 * The DW7720 is Digital Hall Sensor for Mobile phone. 
 *
 * DW7720 includes internal hall and A/D converter and D/A converter for biasing of internal Hall sensor.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/of_gpio.h>
#include <linux/ktime.h>
#include <linux/firmware.h>
#include <linux/wakelock.h>
#include "dw772x_drv.h"
#include <linux/input.h>
#include <linux/pm_runtime.h>

//#define DWA_DEBUG
//#define REQUEST_FW
#define AUTO_UPDATE

#define BBOX_SENSOR_PROBE_FAIL do {gprint("BBox::UEC;19::0\n");} while (0);
#define BBOX_SENSOR_ENABLE_FAIL do {gprint("BBox::UEC;19::3\n");} while (0);

#ifdef DWA_DEBUG
#define ggprint(fmt, args...) pr_info("%s : " fmt, __func__, ##args)
#else
#define ggprint(x...) do { } while (0)
#endif
#define gprint(fmt, args...) pr_info("%s : " fmt, __func__, ##args)

extern struct device *sec_key;

static bool digital_hall_status = 1;

#ifdef REQUEST_FW
#define FW_FILE "dw7720a.bin"
#else
#include "dw772x_fw.h"
#endif

int g_hall_addr=0x2;
int g_hall_to_dist = 0;
int g_hall_sector = 0;
int g_hall_max_point;
int g_hall_slope;
int g_fw_version;

static ssize_t digital_hall_detect_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	if (digital_hall_status)
		sprintf(buf, "OPEN\n");
	else
		sprintf(buf, "CLOSE\n");

	return strlen(buf);
}
static DEVICE_ATTR(digital_hall_detect, 0444, digital_hall_detect_show, NULL);

static int digital_hall_open(struct input_dev *input)
{
//	struct dw772x_priv *dw772x = input_get_drvdata(input);
	/* update the current status */
//	schedule_delayed_work(&dw772x->digital_up_hall_dwork, HZ / 2);
	/* Report current state of buttons that are connected to GPIOs */
	input_sync(input);

	return 0;
}

static void digital_hall_close(struct input_dev *input)
{
}

	 
/* =====================================================================================
function : dw772x devices register write function
descript : devices id 0x18
version  : 1.0
release  : 2019.01.16
====================================================================================== */
int dw772x_byte_write(u8 addr, u8 data)
{
	int ret;

	ret = i2c_smbus_write_byte_data(dw772x->dwclient, addr, data);

	if (ret < 0) {
	 pr_info("%s i2c byte write fail.!\n",dw772x->dev_name);
	}

	return ret;
}


/* =====================================================================================
function : dw772x devices register word write function
descript : devices id 0x18
version  : 1.0
release  : 2019.01.16
====================================================================================== */
int dw772x_word_write(u8 addr, u32 data)
{
	int ret;
	ret = i2c_smbus_write_byte_data(dw772x->dwclient, addr, data>>8);
	ret = i2c_smbus_write_byte_data(dw772x->dwclient, addr+1, (u8)data);

	return ret;
}

/* =====================================================================================
function : dw772x devices register word write function
descript : devices id 0x18
version  : 1.0
release  : 2019.01.16
====================================================================================== */
int dw772x_word_read(u8 addr)
{
	int ret, msb, lsb;

	msb = i2c_smbus_read_byte_data(dw772x->dwclient, addr);
	lsb = i2c_smbus_read_byte_data(dw772x->dwclient, addr+1);

	ret = (msb<<8) | lsb;

	if (ret < 0) {
		gprint("%s i2c byte read fail.!\n",dw772x->dev_name);
	}	 

	return ret;

}

/* =====================================================================================
function : dw772x devices register read function
descript : devices id 0x18
version  : 1.0
release  : 2019.01.16
====================================================================================== */
int dw772x_byte_read(u8 addr)
{
	int ret;

	ret = i2c_smbus_read_byte_data(dw772x->dwclient, addr);

	if (ret < 0) {
	 pr_info("%s i2c byte read fail.!\n",dw772x->dev_name);
	}	 

	return ret;
}

/* =====================================================================================
function : dw772x hall data read function
descript : devices id 0x18
version  : 1.0
release  : 2019.01.16
====================================================================================== */
int dw772x_hall_read(void)
{
	int ret;
	u8 adc[2];

	adc[0] = i2c_smbus_read_byte_data(dw772x->dwclient, HALL_MSB);
	adc[1] = i2c_smbus_read_byte_data(dw772x->dwclient, HALL_LSB);

	ret = ((u32)adc[0]<<4) | (adc[1]>>4);

	if (ret < 0) {
	 pr_info("%s i2c byte read fail.!\n", dw772x->dev_name);
	}
	else pr_info("Hall Read Out: %d\n", ret);

	return ret;
}

/* =====================================================================================

function : dw772x convert hall value to distance value

descript : devices id 0x18

version  : 1.0

release  : 2019.01.30

====================================================================================== */
int dw772x_hall_out(void)
{
     int tmp;
     int adc[2];

	if(g_fw_version == 0x0801) {
		adc[0] = i2c_smbus_read_byte_data(dw772x->dwclient, 0x84);
		adc[1] = i2c_smbus_read_byte_data(dw772x->dwclient, 0x85);
	}
	else {
		adc[0] = i2c_smbus_read_byte_data(dw772x->dwclient, 0x08);
		adc[1] = i2c_smbus_read_byte_data(dw772x->dwclient, 0x09);
	}

	tmp = ((int)adc[0]<<4) | (adc[1]>>4);

    if(tmp >= 2000) { 
		g_hall_sector = 1;
	} else { 
		g_hall_sector = 2; 
	}			

	g_hall_to_dist = (g_hall_slope * (g_hall_max_point - tmp) + 512) / 1024;
	
	pr_info("%s : Hall Read Out: %d\n", __func__, tmp);

	return tmp;
}

/* =====================================================================================
function : dw772x magnet cal data read function
descript : auto detected devices model
version  : 1.0
release  : 2019.01.16
====================================================================================== */
static int dw772x_magnet_cal_set(void)
{
	int tmp[2];
	int hall_min_point;
	
	tmp[0] = dw772x_byte_read(0x5C);
	tmp[1] = dw772x_byte_read(0x5D);
	g_hall_max_point = (tmp[0] << 8) | tmp[1];

	#if 1
	tmp[0] = dw772x_byte_read(0x5E);
	tmp[1] = dw772x_byte_read(0x5F);
	hall_min_point = (tmp[0] << 8) | tmp[1];
	#else
	hall_min_point = 2000;
	#endif

	g_hall_slope=(1024*4)/(g_hall_max_point-hall_min_point);
	
	if(g_hall_max_point < hall_min_point) {
		pr_info("%s : Hall load failed: %d, %d\n", __func__, g_hall_max_point, hall_min_point);
	}
	else 
		pr_info("%s : hall_max: %d hall_min: %d\n", __func__, g_hall_max_point, hall_min_point);
	
	return 0;
}

/* =====================================================================================
function : dw772x devices default setting function
descript : auto detected devices model
version  : 1.0
release  : 2019.01.16
====================================================================================== */
static int dw772x_device_init(struct i2c_client *client)
{
	int ret=0;

	dw772x_init_mode_sel(INT_MODE, 0);
	mdelay(5);
	dw772x_init_mode_sel(ACTIVE_MODE, 0);
	dw772x_init_mode_sel(HALL_CAL_MODE, 0);

	return ret;
}

/* =====================================================================================
function : define the play concept on the dw772x.
descript : auto detected devices model
version  : 1.0
release  : 2019.01.16
====================================================================================== */
int dw772x_init_mode_sel(int mode, u32 data)
{
	int ret=0;
	u8 tmp[2];

	pr_info("%s : mode=%d , data=%d!\n", __func__, mode, data);
	switch(mode) {
		case SLEEP_MODE:
			dw772x_byte_write(0x02, 0x20);
		break;

		case STANDBY_MODE:
			dw772x_byte_write(0x02, 0x40);
		break;

		case INT_MODE:
			dw772x_byte_write(0x3B, 0x94);
			dw772x_byte_write(0x6C, 0x30);
			dw772x_byte_write(0x6D, 0x05);
			dw772x_byte_write(0x6E, 0xFF);
			dw772x_byte_write(0x6F, 0xC0);
			dw772x_byte_write(0x7B, 0x03);	// jks 2019.03.12
			dw772x_byte_write(0x7C, 0x2C);
			dw772x_byte_write(0x7E, 0x72); 

			dw772x_byte_write(0x52, 0x00);  // Interrupt time MSB
			dw772x_byte_write(0x53, 0x01);  // Interrupt time LSB

			// fw check!-------------------------
			tmp[0] = dw772x_byte_read(0x0C);
			tmp[1] = dw772x_byte_read(0x0D);
			g_fw_version = (int)tmp[0] << 8 | tmp[1];
			ggprint("dw772x fw version: %04x\n", g_fw_version);

			// set lpf --------------------------				
			if(g_fw_version <= 0x0801) {
				dw772x_byte_write(0x60, 0x0D);
				dw772x_byte_write(0x61, 0xD3);
				dw772x_byte_write(0x62, 0x02);
				dw772x_byte_write(0x63, 0x2C);
			}
			else {
				dw772x_byte_write(0x60, 0x3E);
				dw772x_byte_write(0x61, 0x5E);
				dw772x_byte_write(0x62, 0x01);
				dw772x_byte_write(0x63, 0xA1);
			}

			// cal flag check! ------------------
			if(dw772x_byte_read(0x5B) == 0) {
				dw772x_word_write(0x4A, 2500);
				dw772x_word_write(0x4C, 2300);
				dw772x_word_write(0x4E, 2500);
				dw772x_word_write(0x50, 2300);
			}
			
			//add register hall chopping
			dw772x_byte_write(0x2F, 0xDA);
			dw772x_byte_write(0x98, 0x27);
			dw772x_byte_write(0x03, 0x03);
			mdelay(15);
			dw772x_byte_write(0x03, 0x01); // STORE
			mdelay(15);
			dw772x_byte_write(0x04, 0x01); // SW RESET
			mdelay(5);
		break;
		
		case SEQ_MODE:
			dw772x_byte_write(0x3B, 0x94);  // Protection off
			dw772x_byte_write(0x52, 0x00);  // Interrupt time MSB
			dw772x_byte_write(0x53, 0x0A);  // Interrupt time LSB			
			dw772x_byte_write(0x02, 0x00);	// active mode
			mdelay(5);
		break;
		
		case SEQ_STORE_MODE:
			dw772x_byte_write(0x3B, 0x94);  // Protection off
			dw772x_byte_write(0x6C, 0x30);
			dw772x_byte_write(0x6D, 0x05);
			dw772x_byte_write(0x6E, 0xFF);
			dw772x_byte_write(0x6F, 0xC0);
			dw772x_byte_write(0x7B, 0x05);
			dw772x_byte_write(0x7C, 0x2C);
			dw772x_byte_write(0x7D, 0x30); 
			dw772x_byte_write(0x7E, 0x72); 
			dw772x_byte_write(0x52, 0x00);  // Interrupt time MSB
			dw772x_byte_write(0x53, 0x0A);  // Interrupt time LSB
			dw772x_byte_write(0x03, 0x01); 	// store
			mdelay(15);
			dw772x_byte_write(0x04, 0x01); // SW RESET
			mdelay(5);
		break;
		
		case HALL_SET_MODE:
			dw772x_byte_write(0x3B, 0x94);  // Protection off
			dw772x_byte_write(0x6C, 0x30);
			dw772x_byte_write(0x6D, 0x05);
			dw772x_byte_write(0x6E, 0xFF);
			dw772x_byte_write(0x6F, 0xC0);
			dw772x_byte_write(0x7B, 0x05);
			dw772x_byte_write(0x7C, 0x2C);
			dw772x_byte_write(0x7D, 0x30); 
			dw772x_byte_write(0x7E, 0x72); 			
		break;
		
		case ACTIVE_MODE:
			dw772x_byte_write(0x02, 0x00); 	// active
			mdelay(5);
		break;

		case HALL_READOUT:
			dw772x_hall_read();
		break;

		case HALL_CAL_MODE:
			dw772x_magnet_cal_set();
			break;

	}	 

	
	return ret;
}

static struct workqueue_struct *rtp_workqueue;

static void dw772x_irq_work(struct work_struct *work)
{
	u32 hall, comp, detach;

	gprint("irq_rtp_work\n");
	comp = dw772x_word_read(0x4C);
	hall = dw772x_hall_out();

	if(hall >= comp) {	// closed
		detach = 0;
	}
	else {				// opened
		detach = 1;
	}

	gprint("hall status %s", detach?"open":"close");
	dw772x->digital_hall = detach;
	digital_hall_status = detach;
	input_report_switch(dw772x->input,
			SW_DIGITALUPHALL, dw772x->digital_hall);
	input_sync(dw772x->input);
}

DECLARE_WORK(work, dw772x_irq_work);

static irqreturn_t irq_rtp_handler(int irq, void *unuse)
{	 
	queue_work(rtp_workqueue, &work);
	return IRQ_HANDLED;
}

static int irq_rtp_exit(void)
{
	free_irq(dw772x->irq_num, NULL);
	cancel_work_sync(&work);
	destroy_workqueue(rtp_workqueue);
	return 0;
}


static int irq_rtp_init(void)
{
	int ret;

	if (gpio_request(dw772x->irq_gpio, "request_irq_gpio")) {
		pr_info("GPIO request faiure: %d\n", dw772x->irq_gpio);
		return -1;
	}

	ret = gpio_direction_input(dw772x->irq_gpio);

	if(ret < 0) {
		pr_info(KERN_INFO "Can't set GPIO direction, error %i\n", ret);
	 	gpio_free(dw772x->irq_gpio);
	 	return -EINVAL;
	}
	else pr_info("GPIO direction input: %d\n", dw772x->irq_gpio);


	dw772x->irq_num = gpio_to_irq(dw772x->irq_gpio);

	pr_info("IRQ Line: %d\n", dw772x->irq_num);

	rtp_workqueue = create_singlethread_workqueue("rtp_work_queue");
	if(!rtp_workqueue) {
		pr_info("%s : error when creating rtp_work_queue\n", __func__);
		return -1;
	}

	ret = request_irq(dw772x->irq_num, (irq_handler_t)irq_rtp_handler, 
		IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, "trig_interrupt", NULL);

	if(ret < 0) {
		irq_rtp_exit();
	 	pr_info("IRQ requset error: %d\n", ret);
	}

	return ret;	 
}

static int dw772x_seq_write(u32 addr, u32 ram_addr, u32 ram_bit, u8* data, u32 size)
{
	int ret=0;
	u8 xbuf[1028];
	struct i2c_msg xfer[1];
	struct i2c_client *i2c_fnc = dw772x->dwclient;

	memset(xbuf, 0, sizeof(xbuf));

	if(size > 1024) { 
		ret = -1;
		pr_info("%s : The transferable size has been exceeded.\n", __func__);		
		return ret;
	}

	if(ram_bit == RAM_ADDR8) { 
		xbuf[0] = (u8)addr;	
		xfer[0].addr  = i2c_fnc->addr;
		xfer[0].len   = size + 1;
		xfer[0].flags = 0;
		xfer[0].buf = xbuf;
		memcpy((u8*)xbuf + 1, (u8*)data, size);
	}
    else if(ram_bit == RAM_ADDR16) {
        xbuf[0] = addr;
        xbuf[1] = ram_addr >> 8;
        xbuf[2] = (u8)ram_addr;

        xfer[0].addr  = i2c_fnc->addr;
        xfer[0].len   = size + 3;
        xfer[0].flags = 0;
        xfer[0].buf = xbuf;
        memcpy((u8*)xbuf + 3, (u8*)data, size);
    }

	ret = i2c_transfer(i2c_fnc->adapter, xfer, 1);
	pr_info("%s : dw772x i2c_transfer ret:%d\n", __func__, ret);
	
	return ret;
}

static int dw772x_seq_read(u32 addr, u32 ram_addr, u32 ram_bit, u8* data, u32 size)
{
	int ret=0;
	u8 xbuf[3];
//	u8 rbuf[1028];
	struct i2c_msg xfer[2];
	struct i2c_client *i2c_fnc = dw772x->dwclient;

	memset(xbuf, 0, sizeof(xbuf));
//	memset(rbuf, 0, sizeof(rbuf));

	if(ram_bit == RAM_ADDR8) { 
		xbuf[0] = (u8)addr;	
		xfer[0].addr  = i2c_fnc->addr;
		xfer[0].len   = size + 1;
		xfer[0].flags = 0;
		xfer[0].buf = xbuf;
		memcpy((u8*)xbuf + 1, (u8*)data, size);
	}
    else if(ram_bit == RAM_ADDR16) {
        xbuf[0] = addr;
        xbuf[1] = ram_addr >> 8;
        xbuf[2] = (u8)ram_addr;

        xfer[0].addr  = i2c_fnc->addr;
        xfer[0].len   = 3;
        xfer[0].flags = 0;
        xfer[0].buf = xbuf;

		xfer[1].addr  = i2c_fnc->addr;
        xfer[1].len   = size;
        xfer[1].flags = I2C_M_RD;
        xfer[1].buf = data;

//        memcpy((u8*)data, (u8*)rbuf, size);
    }

	ret = i2c_transfer(i2c_fnc->adapter, xfer, 2);
	pr_info("%s : dw772x i2c_transfer ret:%d\n", __func__, ret);
	
	return ret;
}

static int request_transfer_firmware(u8* fw_data, u32 size)
{
	int ret=0;
	u32 i, start_addr, trans_size;

	//PT2 OFF
	ret = dw772x_byte_write(0x2F, 0xDA);
	if(ret < 0) return ret;
	mdelay(15);

	//MEM_WE On
	ret = dw772x_byte_write(0x3C, 0x01);
	if(ret < 0) return ret;
	mdelay(15);

	//All Erase
	dw772x_byte_write(0x3D, 0xC0);
	dw772x_byte_write(0x3E, 0x00);
	mdelay(20);

	//4-Kbyte Update
	pr_info("%s : dw772x start loop\n", __func__);
	trans_size=64;
	for(i=0; i<4096; i+=trans_size) {
		start_addr = (i + 0x4000);
		//printk("dw772x %s loop %d: %d\n", __func__, i, start_addr);
		dw772x_seq_write(0x3D, start_addr, RAM_ADDR16, (u8*)fw_data + i, trans_size);
		mdelay(10);
	}

	// SW reset
	dw772x_byte_write(0x04, 0x01);
    mdelay(15);

    dw772x_init_mode_sel(INT_MODE, 0);
    mdelay(5);
    dw772x_init_mode_sel(ACTIVE_MODE, 0);
    dw772x_init_mode_sel(HALL_CAL_MODE, 0);

	pr_info("%s : dw772x requeset firmware transfer complete!\n", __func__);
	
	return ret;
}

static int request_verify_firmware(u8* fw_data, u32 size)
{
	int ret=0;
	int ret1=0, ret2=0;
	u32 i, j, start_addr, trans_size;
	u8 buf[1024];

	//PT2 OFF
	ret = dw772x_byte_write(0x2F, 0xDA);
	if(ret < 0) return ret;
	mdelay(10);

	//MEM_WE On
	ret = dw772x_byte_write(0x3C, 0x01);
	if(ret < 0) return ret;
	mdelay(10);

	//4-Kbyte read
	pr_info("%s : dw772x start loop\n", __func__);
	trans_size=64;
	for(i=0; i<4096; i+=trans_size) {
		start_addr = i;
		//printk("dw772x %s loop %d: %d\n", __func__, i, start_addr);
		dw772x_seq_read(0x3D, start_addr, RAM_ADDR16, buf, trans_size);//(u8*)fw_data + i, trans_size);
		for(j=0; j<trans_size; j++) {
			if(*(fw_data + i + j) != buf[j]) {
				pr_info("%s : dw772x not match! %d %d %02x %02x\n", __func__, i, j, *(fw_data+i+j),buf[j]);
				return -1;
			}
		}
	}

	// SW reset
	dw772x_byte_write(0x04, 0x01);
    mdelay(15);

    dw772x_init_mode_sel(INT_MODE, 0);
    mdelay(5);
    dw772x_init_mode_sel(ACTIVE_MODE, 0);
    dw772x_init_mode_sel(HALL_CAL_MODE, 0);

	ret1 = dw772x_byte_read(0x0C);
	ret2 = dw772x_byte_read(0x0D);

	ret = ret1<<8;
	ret += ret2;

	pr_info("%s : dw772x requeset firmware transfer complete!\n", __func__);
	
	return ret;
}

static void do_firmware_update(void)
{
#ifdef REQUEST_FW
	const struct firmware *fw;
	if(request_firmware(&fw, FW_FILE, &dw772x->dwclient->dev) == 0) {
		gprint("dw772x fw_name=%s fw_size=%ld\n", FW_FILE, fw->size);
		request_transfer_firmware((u8*)fw->data, fw->size);
		release_firmware(fw);
	} else {
		gprint("dw772x request_firmware failed\n");
	}
#else
	request_transfer_firmware(firmware_binary, sizeof(firmware_binary));
	gprint("dw772x firmware size: %d\n", (int)sizeof(firmware_binary));
#endif
}

static int check_firmware_update(void)
{
	int ret=0;
#ifdef REQUEST_FW
	const struct firmware *fw;

	if(request_firmware(&fw, FW_FILE, &dw772x->dwclient->dev) == 0) {
		gprint("dw772x fw_name=%s fw_size=%ld\n", FW_FILE, fw->size);
		ret = request_verify_firmware((u8*)fw->data, fw->size);
		release_firmware(fw);
	} else {
		gprint("dw772x request_firmware failed\n");
		return -1;
	}
#else
	ret = request_verify_firmware(firmware_binary, sizeof(firmware_binary));
#endif

	return ret;
}

static ssize_t enableDW772x_set (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int temp=0, temp1=0;
	u8 addr, data;

	pm_runtime_get_sync(dw772x->dev);
	sscanf(buf, "%x %x", &temp, &temp1);


	addr = temp >> 8;
	data = 0xFF & temp;

	dw772x_byte_write(addr, data);	
	
	pr_info("%s : write Addr: %02x  Data: %02x\n", __func__, addr, data);
	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);
	return count;
}


/* =====================================================================================

function : dw772x system file system

descript : hall sector , hall distance, hall data return

version  : 1.0

release  : 2019.01.30

====================================================================================== */

static ssize_t enableDW772x_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int data;

	pm_runtime_get_sync(dw772x->dev);

	data = dw772x_hall_out();
	pr_info("%s : s=%d,d=%d,adc=%d,slope=%d,max=%d,cal1=%d,cal2=%d\n",
		__func__, g_hall_sector, g_hall_to_dist, data,
		g_hall_slope, g_hall_max_point,dw772x->cal1,dw772x->cal2);

	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);
	return snprintf(buf, PAGE_SIZE, "%d %d %d\n", g_hall_sector, g_hall_to_dist, data);
}

static ssize_t calDW772x_set (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int mode=0, value=0, diff_e=0;
	int max=0, max_th, min_th;

	pm_runtime_get_sync(dw772x->dev);

	sscanf(buf, "%d %d", &mode, &value);

	if(mode == 1) { 		// cal 1point
		dw772x_word_write(0x5C, value);
	}
	else if(mode == 2) {	// cal 2point
		dw772x_word_write(0x5E, value);

		max = dw772x_word_read(0x5C);

		if(max > value) {
			diff_e = (max - value) >> 3;
			max_th = (max - (max - value) / 2) + diff_e;
			min_th = (max - (max - value) / 2) - diff_e;

			dw772x_word_write(0x4A, max_th);
			dw772x_word_write(0x4C, min_th);
			dw772x_word_write(0x4E, max_th);
			dw772x_word_write(0x50, min_th);
			dw772x_byte_write(0x5B, 0x01);	// cAL Flag Set
		}
		else {
			dw772x_word_write(0x4A, 2500);
			dw772x_word_write(0x4C, 2300);
			dw772x_word_write(0x4E, 2500);
			dw772x_word_write(0x50, 2300);
			dw772x_byte_write(0x5B, 0x00);	// cAL Flag Set
		}			
	}
	else if(mode == 12) {	// cal clear
		dw772x_word_write(0x5C, 0);
		dw772x_word_write(0x5E, 0);
	}

	if(mode==1 || mode==2 || mode==12) {
		dw772x_magnet_cal_set();		// Reapply the formula
		dw772x_byte_write(0x03, 0x01);	// STORE
		mdelay(15);
		dw772x_byte_write(0x04, 0x01);	// SW RESET
		mdelay(10);
		dw772x_init_mode_sel(ACTIVE_MODE, 0);
	}
	
	gprint("write mode: %d	value: %d\n", mode, value);

	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);

	return count;
}

static ssize_t calDW772x_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int tmp[2];
	int cal1, cal2;

	pm_runtime_get_sync(dw772x->dev);

	tmp[0] = dw772x_byte_read(0x5C);
	tmp[1] = dw772x_byte_read(0x5D);
	cal1 = (tmp[0] << 8) | tmp[1];

	tmp[0] = dw772x_byte_read(0x5E);
	tmp[1] = dw772x_byte_read(0x5F);
	cal2 = (tmp[0] << 8) | tmp[1];

	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);

	return snprintf(buf, PAGE_SIZE, "cal1: %d cal2: %d\n", cal1, cal2);
}

static ssize_t dumpDW772x_set (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int value=0;
	
	sscanf(buf, "%02x", &value);
	g_hall_addr = value;
	 
	return count;
}

static ssize_t dumpDW772x_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int value;

	pm_runtime_get_sync(dw772x->dev);

	value = dw772x_byte_read(g_hall_addr);

	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);

	return snprintf(buf, PAGE_SIZE, "%02x: %02x\n", (char)g_hall_addr, (char)value);
}

static ssize_t verDW772x_set (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{	 
	return count;
}


static ssize_t verDW772x_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ic, fw;

	pm_runtime_get_sync(dw772x->dev);

	ic = dw772x_word_read(0x0C);
	fw = ((int)firmware_binary[4]<<8) | firmware_binary[5];

	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);

	return snprintf(buf, PAGE_SIZE, "%04x %04x\n", ic, fw);
}

static ssize_t firmware_update_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	pm_runtime_get_sync(dw772x->dev);

	gprint("dw772x firmware_update\n");
	do_firmware_update();

	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);

	return count;
}

static ssize_t firmware_update_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;

	pm_runtime_get_sync(dw772x->dev);

	ret = check_firmware_update();
	
	gprint("dw772x : %04x\n", ret);

	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", ret);
}

static ssize_t self_test_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	pm_runtime_get_sync(dw772x->dev);

	gprint("dw772x self_test_set\n");

	dw772x_byte_write(0x3B, 0x94); //PT1 Off
	dw772x_byte_write(0x54, 0x32); //ST_MARGIN1
	dw772x_byte_write(0x55, 0x64); //ST_MARGIN2
	dw772x_byte_write(0x56, 0x14); //ST_MARGIN3

	dw772x_byte_write(0x03, 0x01); //Store
	mdelay(5);
	dw772x_byte_write(0x02, 0x40);
	dw772x_byte_write(0x02, 0x04);

	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);

	return count;
}

static ssize_t self_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;

	pm_runtime_get_sync(dw772x->dev);

	// Read Test Status
	dw772x_byte_write(0x3b, 0x94); //PT1 Off
	ret = dw772x_byte_read(0x45);

	// SW reset
	dw772x_byte_write(0x04, 0x01);
    mdelay(5);

    dw772x_init_mode_sel(INT_MODE, 0);
    mdelay(5);
    dw772x_init_mode_sel(ACTIVE_MODE, 0);
    dw772x_init_mode_sel(HALL_CAL_MODE, 0);

	gprint("dw772x %02x\n", ret);

	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);

	return snprintf(buf, PAGE_SIZE, "%02x\n", ret);
}

static ssize_t suspend_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int mode=0;

	pm_runtime_get_sync(dw772x->dev);

	sscanf(buf, "%d", &mode);

	if(mode == 0) {			// resume
		dw772x_init_mode_sel(STANDBY_MODE, 0);
		dw772x_init_mode_sel(ACTIVE_MODE, 0);				
		gprint("dw772x power_resume_mode_set!\n");
	}
	else if(mode == 1) {	// suspend
		dw772x_init_mode_sel(SLEEP_MODE, 0);
		gprint("dw772x power_suspend_mode_set!\n");
	}

	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);

	return count;
}

static ssize_t suspend_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret=0, set=0;

	pm_runtime_get_sync(dw772x->dev);

	ret = dw772x_byte_read(0x02);

	if(ret == 0x20) {
		set = 1;
		gprint("dw772x suspend %02x\n", ret);
	}
	else if(ret == 0x40) {
		set  = 0;
		gprint("dw772x resume %02x\n", ret);
	}

	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", set);
}


DEVICE_ATTR(enableHALL, (S_IWUSR|S_IRUGO), enableDW772x_show, enableDW772x_set);
DEVICE_ATTR(firmware_update, (S_IWUSR|S_IRUGO), firmware_update_show, firmware_update_set);
DEVICE_ATTR(calHALL, (S_IWUSR|S_IRUGO), calDW772x_show, calDW772x_set);
DEVICE_ATTR(dumpHALL, (S_IWUSR|S_IRUGO), dumpDW772x_show, dumpDW772x_set);
DEVICE_ATTR(self_test, (S_IWUSR|S_IRUGO), self_test_show, self_test_set);
DEVICE_ATTR(verHALL, (S_IWUSR|S_IRUGO), verDW772x_show, verDW772x_set);
DEVICE_ATTR(suspendHALL, (S_IWUSR|S_IRUGO), suspend_show, suspend_set);


static int dw772x_hall_sysfs_init(void)
{
	int ret = 0;
	
	ret = device_create_file(sec_key, &dev_attr_enableHALL);
	if (ret < 0) {
		pr_err("Failed to create device file(%s)!, error: %d\n",
		dev_attr_enableHALL.attr.name, ret);
	}

	ret = device_create_file(sec_key, &dev_attr_calHALL);
	if (ret < 0) {
		pr_err("Failed to create device file(%s)!, error: %d\n",
		dev_attr_calHALL.attr.name, ret);
	}

	ret = device_create_file(sec_key, &dev_attr_firmware_update);
	if (ret < 0) {
		pr_err("Failed to create device file(%s)!, error: %d\n",
		dev_attr_firmware_update.attr.name, ret);
	}

	ret = device_create_file(sec_key, &dev_attr_digital_hall_detect);
	if (ret < 0) {
		pr_err("Failed to create device file(%s)!, error: %d\n",
		dev_attr_digital_hall_detect.attr.name, ret);
	}

	ret = device_create_file(sec_key, &dev_attr_dumpHALL);
	if (ret < 0) {
		pr_err("Failed to create device file(%s)!, error: %d\n",
		dev_attr_dumpHALL.attr.name, ret);
	}

	ret = device_create_file(sec_key, &dev_attr_self_test);
	if (ret < 0) {
		pr_err("Failed to create device file(%s)!, error: %d\n",
		dev_attr_self_test.attr.name, ret);
	}

	ret = device_create_file(sec_key, &dev_attr_verHALL);
	if (ret < 0) {
		pr_err("Failed to create device file(%s)!, error: %d\n",
		dev_attr_self_test.attr.name, ret);
	}

	ret = device_create_file(sec_key, &dev_attr_suspendHALL);
	if (ret < 0) {
		pr_err("Failed to create device file(%s)!, error: %d\n",
		dev_attr_suspendHALL.attr.name, ret);
	}

	return ret;
}

#ifdef CONFIG_OF
static int dw772x_parse_dt(struct i2c_client *i2c, struct dw772x_priv *pdev)
{
	struct device *dev = &i2c->dev;
	struct device_node *np = dev->of_node;
	int external_vdd_use = 0;

	if (!np) return -1;

	pdev->irq_gpio = of_get_named_gpio(np, "dw772x,en-gpio", 0);

	if (pdev->irq_gpio < 0) {	 
		pr_info("Looking up %s property in node %s failed %d\n", "dw772x,en-gpio", dev->of_node->full_name, pdev->irq_gpio);
	 	pdev->irq_gpio = -1;
	}

	if(!gpio_is_valid(pdev->irq_gpio)) {	 
		 pr_info(KERN_ERR "dw772x enable pin(%u) is invalid\n", pdev->irq_gpio);
	}

	external_vdd_use = of_get_named_gpio(np, "hall,hall_ldo_en", 0);
	if (external_vdd_use > 0)
		gpio_set_value(external_vdd_use,1);		

	return 0;
}

#else
static int dw772x_parse_dt(struct i2c_client *i2c, struct dw772x_priv *pdev)
{
	return NULL;
}
#endif

static void check_cal_value(int *cal1, int*cal2)
{
	int tmp[2];
	pm_runtime_get_sync(dw772x->dev);

	tmp[0] = dw772x_byte_read(0x5C);
	tmp[1] = dw772x_byte_read(0x5D);
	*cal1 = (tmp[0] << 8) | tmp[1];

	tmp[0] = dw772x_byte_read(0x5E);
	tmp[1] = dw772x_byte_read(0x5F);
	*cal2 = (tmp[0] << 8) | tmp[1];

	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);
}

static int dw772x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	struct input_dev *input;

	pr_info("%s: dw772x haptic driver probe start....190318A\n", __func__);

	dw772x = kzalloc(sizeof(struct dw772x_priv), GFP_KERNEL);

	if (dw772x == NULL) {
	 BBOX_SENSOR_PROBE_FAIL
	 return -ENOMEM;
	}

	dw772x->dev = &client->dev;
	dw772x->is_suspend = false;
	dw772x->cal1=dw772x->cal2 = 0;
	dw772x_parse_dt(client, dw772x);
	pr_info("%s: parse_dt!\n", __func__);
	i2c_set_clientdata(client, dw772x);
	pr_info("%s: i2c_set_clientdata!\n", __func__);
	dw772x->dwclient = client; 
	dw772x_hall_sysfs_init();
	pr_info("%s: dw772x_hall_sysfs_init!\n", __func__);

#if !defined(REQUEST_FW) && defined(AUTO_UPDATE)
	ret = check_firmware_update();

	if (ret <= 0) {
		gprint("need update(%d)\n", ret);
		do_firmware_update();
		ret = check_firmware_update();
		gprint("after update(%04x)\n", ret);
	} else {
		gprint("firmware is latest(%04x)\n", ret);
	}
	ret=0;
#endif

	dw772x_device_init(client);
	pr_info("%s: dw772x_device_init!\n", __func__);

	input = input_allocate_device();
	if (!input) {
		pr_info("%s : failed to allocate state\n", __func__);
		ret = -ENOMEM;
		goto fail1;
	}

	dw772x->input = input;

//	platform_set_drvdata(pdev, dw772x);
	input_set_drvdata(input, dw772x);
	
	input->name = "digital_up_hall";
	input->phys = "digital_up_hall";
	input->dev.parent = &client->dev;

	input->evbit[0] |= BIT_MASK(EV_SW);
	input_set_capability(input, EV_SW, SW_DIGITALUPHALL);

	input->open = digital_hall_open;
	input->close = digital_hall_close;

	/* Enable auto repeat feature of Linux input subsystem */
	__set_bit(EV_REP, input->evbit);

	ret = input_register_device(input);
	if (ret) {
		pr_info("%s : Unable to register input device, error: %d\n", __func__, ret);
		goto fail1;
	}

	ret = irq_rtp_init();
	if (ret) {
		pr_info("%s : Unable to init irq_rtp_init, error: %d\n", __func__, ret);
		goto fail1;
	}

	check_cal_value(&dw772x->cal1, &dw772x->cal2);
	pm_runtime_use_autosuspend(dw772x->dev);
	pm_runtime_set_autosuspend_delay(dw772x->dev, 10000);
	pm_runtime_set_active(dw772x->dev);
	pm_runtime_enable(dw772x->dev);
	pm_runtime_get_sync(dw772x->dev);
	pm_runtime_mark_last_busy(dw772x->dev);
	pm_runtime_put_autosuspend(dw772x->dev);

	pr_info("%s probe success!\n", dw772x->dev_name);
	
fail1:
	return ret;
}


static int dw772x_remove(struct i2c_client *client)
{
	struct dw772x_priv *dw772x = i2c_get_clientdata(client);

	gpio_free(dw772x->irq_gpio);
	free_irq(dw772x->irq_num, NULL);
	i2c_set_clientdata(client, NULL);	
	kfree(dw772x);  
	return 0;
}


#ifdef CONFIG_PM
static void dw772x_make_suspend(int make_suspend)
{
	if (make_suspend) {
		gprint("dw772x power_suspend_mode_set!\n");
		dw772x_init_mode_sel(SLEEP_MODE, 0);
	} else {
		dw772x_init_mode_sel(STANDBY_MODE, 0);
		dw772x_init_mode_sel(ACTIVE_MODE, 0);
		gprint("dw772x power_resume_mode_set!\n");
	}
}

static int dw772x_runtime_suspend(struct device *dev)
{
	dw772x_make_suspend(1);
	return 0;
}

static int dw772x_runtime_resume(struct device *dev)
{
	dw772x_make_suspend(0);		
	return 0;
}

static int dw772x_system_suspend(struct device *dev)
{
	if (!pm_runtime_suspended(dev)) {
		dw772x_runtime_suspend(dev);
	} else {
		gprint("dw772x already suspend!\n");
	}
	return 0;
}

static int dw772x_system_resume(struct device *dev)
{
	if (!pm_runtime_suspended(dev)) {
		dw772x_runtime_resume(dev);
	} else {
		gprint("dw772x already resume!\n");
	}
	return 0;
}

#if 0
static const struct dev_pm_ops dw772x_pm_ops = {
	.thaw = dw772x_suspend,
	.restore = dw772x_resume,
};
#else
static const struct dev_pm_ops dw772x_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw772x_system_suspend, dw772x_system_resume)
	SET_RUNTIME_PM_OPS(dw772x_runtime_suspend, dw772x_runtime_resume,
				NULL)
};
#endif
//static SIMPLE_DEV_PM_OPS(dw772x_pm_ops, dw772x_suspend, dw772x_resume);

#define DW772x_HALL_PM_OPS (&dw772x_pm_ops)
#else
#define DW772x_HALL_PM_OPS NULL
#endif



MODULE_DEVICE_TABLE(i2c, dw772x_drv_id);

static struct i2c_driver dw772x_i2c_driver = {
	 .probe = dw772x_probe,
	 .remove = dw772x_remove,
	 .id_table = dw772x_drv_id,
	 .driver = {
		 .name = "dw772x-codec",
		 .owner = THIS_MODULE,
#ifdef CONFIG_OF
		 .of_match_table = of_match_ptr(dw772x_i2c_dt_ids),
#endif
#ifdef CONFIG_PM
		 .pm = DW772x_HALL_PM_OPS,
#endif
	 },
};

static int __init dw772x_modinit(void)
{
	int ret;

	pr_info("Hall Sensor Driver Initializes the I2C interface.\n");

	ret = i2c_add_driver(&dw772x_i2c_driver);

	if (ret) pr_info("Failed to register dw772x I2C driver: %d\n", ret);

	return ret;
}

late_initcall(dw772x_modinit);

static void __exit dw772x_exit(void)
{
	i2c_del_driver(&dw772x_i2c_driver);
	sysfs_remove_file(android_hall_kobj, &dev_attr_enableHALL.attr);
	kobject_del(android_hall_kobj);
	pr_info("DW772x Hall driver I2C interface remove.!\n");
}


module_exit(dw772x_exit);
MODULE_DESCRIPTION("DW772x hall sensor codec driver");
MODULE_AUTHOR("jks8051@dwanatech.com");
MODULE_LICENSE("GPL/BSD Dual");




