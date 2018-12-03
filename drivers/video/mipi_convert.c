#define DEBUG
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
#include <linux/regulator/consumer.h>

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif

//#define CONVERT_DEBUG

#define PINCTRL_STATE_ACTIVE    "pmx_convert_active"
#define PINCTRL_STATE_SUSPEND   "pmx_convert_suspend"

enum convert_i2c_addr {
    I2C_ADDR_MAIN = 0x2c,
};

struct convert_platform_data {
    struct i2c_client *client;
    struct pinctrl *convert_pinctrl;
    struct pinctrl_state *pinctrl_state_active;
    struct pinctrl_state *pinctrl_state_suspend;
    u32 irq_gpio;
    u32 irq_flags;
    u32 rst_gpio;
    u32 rst_flags;
    u32 backlight_en;
    u32 backlight_en_flags;
    struct regulator *avdd;
#ifdef CONVERT_DEBUG
    struct delayed_work convert_check_hpd_work_id;
    struct workqueue_struct *workq;
#endif
#if defined(CONFIG_FB)
    struct notifier_block fb_notif;
#endif
};

struct convert_reg_cfg {
    u8 i2c_addr;
    u8 reg;
    u8 val;
    int sleep_in_ms;
};
#if 1
static struct convert_reg_cfg convert_init_setup[] = {    //1024/50fps
{I2C_ADDR_MAIN, 0x09, 0x00, 0},
{I2C_ADDR_MAIN, 0x0A, 0x01, 0},
{I2C_ADDR_MAIN, 0x0B, 0x29, 0},
{I2C_ADDR_MAIN, 0x0D, 0x00, 10},
{I2C_ADDR_MAIN, 0x10, 0x26, 0},
{I2C_ADDR_MAIN, 0x11, 0x00, 0},
{I2C_ADDR_MAIN, 0x12, 0x2b, 0},
{I2C_ADDR_MAIN, 0x13, 0x00, 0},
{I2C_ADDR_MAIN, 0x18, 0x6c, 0},
{I2C_ADDR_MAIN, 0x19, 0x00, 0},
{I2C_ADDR_MAIN, 0x1A, 0x03, 0},
{I2C_ADDR_MAIN, 0x1B, 0x00, 0},
{I2C_ADDR_MAIN, 0x20, 0x00, 0},
{I2C_ADDR_MAIN, 0x21, 0x05, 0},
{I2C_ADDR_MAIN, 0x22, 0x00, 0},
{I2C_ADDR_MAIN, 0x23, 0x00, 0},
{I2C_ADDR_MAIN, 0x24, 0x00, 0},
{I2C_ADDR_MAIN, 0x25, 0x00, 0},
{I2C_ADDR_MAIN, 0x26, 0x00, 0},
{I2C_ADDR_MAIN, 0x27, 0x00, 0},
{I2C_ADDR_MAIN, 0x28, 0x21, 0},
{I2C_ADDR_MAIN, 0x29, 0x00, 0},
{I2C_ADDR_MAIN, 0x2A, 0x00, 0},
{I2C_ADDR_MAIN, 0x2B, 0x00, 0},
{I2C_ADDR_MAIN, 0x2C, 0x06, 0},
{I2C_ADDR_MAIN, 0x2D, 0x00, 0},
{I2C_ADDR_MAIN, 0x2E, 0x00, 0},
{I2C_ADDR_MAIN, 0x2F, 0x00, 0},
{I2C_ADDR_MAIN, 0x30, 0x04, 0},
{I2C_ADDR_MAIN, 0x31, 0x00, 0},
{I2C_ADDR_MAIN, 0x32, 0x00, 0},
{I2C_ADDR_MAIN, 0x33, 0x00, 0},
{I2C_ADDR_MAIN, 0x34, 0x17, 0},
{I2C_ADDR_MAIN, 0x35, 0x00, 0},
{I2C_ADDR_MAIN, 0x36, 0x00, 0},
{I2C_ADDR_MAIN, 0x37, 0x00, 0},
{I2C_ADDR_MAIN, 0x38, 0x00, 0},
{I2C_ADDR_MAIN, 0x39, 0x00, 0},
{I2C_ADDR_MAIN, 0x3A, 0x00, 0},
{I2C_ADDR_MAIN, 0x3B, 0x00, 0},
{I2C_ADDR_MAIN, 0x3C, 0x00, 0},
{I2C_ADDR_MAIN, 0x3D, 0x00, 0},
{I2C_ADDR_MAIN, 0x3E, 0x00, 5},

//{I2C_ADDR_MAIN, 0xE0, 0x00, 1},
{I2C_ADDR_MAIN, 0x0D, 0x01, 10}, //PLL enable
{I2C_ADDR_MAIN, 0x09, 0x01, 0}, //soft reset
};
#else   //test 60fps
static struct convert_reg_cfg convert_init_setup[] = {
{I2C_ADDR_MAIN, 0x09, 0x00, 0},
{I2C_ADDR_MAIN, 0x0A, 0x03, 0},
{I2C_ADDR_MAIN, 0x0B, 0x20, 0},
{I2C_ADDR_MAIN, 0x0D, 0x00, 0},
{I2C_ADDR_MAIN, 0x10, 0x26, 0},
{I2C_ADDR_MAIN, 0x11, 0x00, 0},
{I2C_ADDR_MAIN, 0x12, 0x2b, 0},
{I2C_ADDR_MAIN, 0x13, 0x00, 0},
{I2C_ADDR_MAIN, 0x18, 0x6c, 0},
{I2C_ADDR_MAIN, 0x19, 0x00, 0},
{I2C_ADDR_MAIN, 0x1A, 0x03, 0},
{I2C_ADDR_MAIN, 0x1B, 0x00, 0},
{I2C_ADDR_MAIN, 0x20, 0x80, 0},
{I2C_ADDR_MAIN, 0x21, 0x02, 0},
{I2C_ADDR_MAIN, 0x22, 0x00, 0},
{I2C_ADDR_MAIN, 0x23, 0x00, 0},
{I2C_ADDR_MAIN, 0x24, 0x00, 0},
{I2C_ADDR_MAIN, 0x25, 0x04, 0},
{I2C_ADDR_MAIN, 0x26, 0x00, 0},
{I2C_ADDR_MAIN, 0x27, 0x00, 0},
{I2C_ADDR_MAIN, 0x28, 0xa7, 0},
{I2C_ADDR_MAIN, 0x29, 0x00, 0},
{I2C_ADDR_MAIN, 0x2A, 0x00, 0},
{I2C_ADDR_MAIN, 0x2B, 0x00, 0},
{I2C_ADDR_MAIN, 0x2C, 0x06, 0}, //HPW_L
{I2C_ADDR_MAIN, 0x2D, 0x00, 0}, //HPW_H
{I2C_ADDR_MAIN, 0x2E, 0x00, 0},
{I2C_ADDR_MAIN, 0x2F, 0x00, 0},
{I2C_ADDR_MAIN, 0x30, 0x04, 0}, //VPW_L
{I2C_ADDR_MAIN, 0x31, 0x00, 0}, //VPW_H
{I2C_ADDR_MAIN, 0x32, 0x00, 0},
{I2C_ADDR_MAIN, 0x33, 0x00, 0},
{I2C_ADDR_MAIN, 0x34, 0x17, 0}, //HBP
{I2C_ADDR_MAIN, 0x35, 0x00, 0},
{I2C_ADDR_MAIN, 0x36, 0x09, 0}, //test VBP
{I2C_ADDR_MAIN, 0x37, 0x00, 0},
{I2C_ADDR_MAIN, 0x38, 0x17, 0}, //test HFP
{I2C_ADDR_MAIN, 0x39, 0x00, 0},
{I2C_ADDR_MAIN, 0x3A, 0x09, 0}, //test VFP
{I2C_ADDR_MAIN, 0x3B, 0x00, 0},
{I2C_ADDR_MAIN, 0x3C, 0x10, 0},
{I2C_ADDR_MAIN, 0x3D, 0x00, 0},
{I2C_ADDR_MAIN, 0x3E, 0x00, 5},

{I2C_ADDR_MAIN, 0x0D, 0x01, 8}, //PLL enable
{I2C_ADDR_MAIN, 0x09, 0x01, 0}, //soft reset
};
#endif

static int convert_i2c_write8(struct i2c_client *client, u8 addr, u8 reg, u8 value)
{
    int ret = 0;
    client->addr = addr;
    ret = i2c_smbus_write_byte_data(client, reg, value);
    if(ret < 0)
        dev_err(&client->dev, "I2C write reg:%x error\n", reg);
    return ret;
}

static int convert_i2c_read8(struct i2c_client *client, u8 addr, u8 reg, u8 *value)
{
    int ret = 0;
    client->addr = addr;
    ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0) {
        dev_err(&client->dev, "I2C read reg:%x error\n", reg);
        return ret;
    }
    *value = (u8)ret;
    dev_dbg(&client->dev, "reg: %x, READ8: %x\n", reg, *value);
    return 0;
}

static void convert_read_array(struct i2c_client *client,
    struct convert_reg_cfg *cfg, int size)
{
    int ret = 0;
    int i;
    u8 value;

    size = size / sizeof(struct convert_reg_cfg);
    for (i = 0; i < size; i++) {
        ret = convert_i2c_read8(client, cfg[i].i2c_addr,
            cfg[i].reg, &value);
        if (cfg[i].sleep_in_ms)
            msleep(cfg[i].sleep_in_ms);
    }
}

static void convert_write_array(struct i2c_client *client,
    struct convert_reg_cfg *cfg, int size)
{
    int ret = 0;
    int i;

    size = size / sizeof(struct convert_reg_cfg);
    for (i = 0; i < size; i++) {
        ret = convert_i2c_write8(client, cfg[i].i2c_addr,
            cfg[i].reg, cfg[i].val);
        if (ret != 0){
            dev_err(&client->dev, "%s: convert reg write %02X to %02X failed.\n",
                __func__, cfg[i].val, cfg[i].reg);
        }
        if (cfg[i].sleep_in_ms)
            msleep(cfg[i].sleep_in_ms);
    }
}


static int convert_gpio_configure(struct convert_platform_data *pdata, bool on)
{
    int ret = 0;
    if (on) {
        if (gpio_is_valid(pdata->rst_gpio)) {
            ret = gpio_request(pdata->rst_gpio, "convert_rst_gpio");
            if (ret) {
                dev_err(&pdata->client->dev, "%d unable to request gpio [%d] ret=%d\n",
                    __LINE__, pdata->rst_gpio, ret);
                goto err_none;
            }
            ret = gpio_direction_output(pdata->rst_gpio, 1);
            if (ret) {
                dev_err(&pdata->client->dev, "unable to set dir for gpio[%d]\n",
                    pdata->rst_gpio);
                goto err_rst_gpio;
            }
        } else {
            dev_err(&pdata->client->dev, "rst gpio not provided\n");
            goto err_none;
        }
    
        if (gpio_is_valid(pdata->backlight_en)) {
            ret = gpio_request(pdata->backlight_en, "backlight_en");
            if (ret) {
                dev_err(&pdata->client->dev, "%d unable to backlight_en gpio [%d] ret=%d\n",
                    __LINE__, pdata->backlight_en, ret);
                goto err_rst_gpio;
            }
            ret = gpio_direction_output(pdata->backlight_en, 1);
            if (ret) {
                dev_err(&pdata->client->dev, "unable to set dir for gpio[%d]\n",
                    pdata->backlight_en);
                goto err_bk_gpio;
            }
        } else {
            dev_err(&pdata->client->dev, "backlight_en gpio not provided\n");
        }    

        if (gpio_is_valid(pdata->irq_gpio)) {
            ret = gpio_request(pdata->irq_gpio, "convert_irq_gpio");
            if (ret) {
                dev_err(&pdata->client->dev,"%d unable to request gpio [%d] ret=%d\n",
                    __LINE__, pdata->irq_gpio, ret);
                goto err_bk_gpio;
            }
            ret = gpio_direction_input(pdata->irq_gpio);
            if (ret) {
                dev_err(&pdata->client->dev, "unable to set dir for gpio[%d]\n",
                    pdata->irq_gpio);
                goto err_irq_gpio;
            }
        } else {
            dev_err(&pdata->client->dev, "irq gpio not provided\n");
        }

        return 0;
    } else {
        if (gpio_is_valid(pdata->rst_gpio))
            gpio_free(pdata->rst_gpio);
        if (gpio_is_valid(pdata->backlight_en))
            gpio_free(pdata->backlight_en);
        if (gpio_is_valid(pdata->irq_gpio))
            gpio_free(pdata->irq_gpio);

        return 0;
    }

err_irq_gpio:
    if (gpio_is_valid(pdata->irq_gpio))
        gpio_free(pdata->irq_gpio);
err_bk_gpio:
    if (gpio_is_valid(pdata->backlight_en))
        gpio_free(pdata->backlight_en);
err_rst_gpio:
    if (gpio_is_valid(pdata->rst_gpio))
        gpio_free(pdata->rst_gpio);
err_none:
    return ret;
}

static int convert_power_configure(struct convert_platform_data *pdata, bool on)
{
    int ret = 0;
    if(on){
        //pdata->avdd = regulator_get_optional(&pdata->client->dev, "avdd");
        pdata->avdd = regulator_get(&pdata->client->dev, "avdd");
        if (IS_ERR(pdata->avdd)) {
            ret = PTR_ERR(pdata->avdd);
            dev_err(&pdata->client->dev,
                "Regulator get failed avdd ret=%d\n", ret);
        }
    }else{
        if(!IS_ERR(pdata->avdd)){
           regulator_put(pdata->avdd); 
        }
    }
    return ret;
}

static int convert_power_on(struct convert_platform_data *pdata)
{
    int ret = 0;
    if (!IS_ERR(pdata->avdd)) {
        ret = regulator_enable(pdata->avdd);
        if (ret) {
            dev_err(&pdata->client->dev,
                "Regulator avdd enable failed ret=%d\n", ret);
        }
    }
    return ret;
}

static int convert_power_off(struct convert_platform_data *pdata)
{
    int ret;
    if (!IS_ERR(pdata->avdd)) {
        ret = regulator_disable(pdata->avdd);
        if (ret) {
            dev_err(&pdata->client->dev,
                "Regulator avdd enable failed ret=%d\n", ret);
        }
    }
    return ret; 
}

static int convert_parse_dt(struct device *dev,
    struct convert_platform_data *pdata)
{
    struct device_node *np = dev->of_node;
    int ret = 0;
    /* Get pinctrl if target uses pinctrl */
    pdata->convert_pinctrl = devm_pinctrl_get(dev);
    if (IS_ERR_OR_NULL(pdata->convert_pinctrl)) {
        dev_err(dev, "%s: Pincontrol DT property failed\n", __func__);
    }else{
        pdata->pinctrl_state_active = pinctrl_lookup_state(pdata->convert_pinctrl,
            "pmx_convert_active");
        if (IS_ERR_OR_NULL(pdata->pinctrl_state_active)) {
            dev_err(dev, "%s:Can not lookup pmx_convert_active pinstate %ld\n", __func__,
                    PTR_ERR(pdata->pinctrl_state_active));
        }

        pdata->pinctrl_state_suspend = pinctrl_lookup_state(pdata->convert_pinctrl,
            "pmx_convert_suspend");
        if (IS_ERR_OR_NULL(pdata->pinctrl_state_suspend)) {
            dev_err(dev, "%s: Can not lookup pmx_convert_suspend pinstate %ld\n", __func__,
                    PTR_ERR(pdata->pinctrl_state_suspend));
        }
    }
    pdata->irq_gpio = of_get_named_gpio_flags(np,
            "convert,irq-gpio", 0, &pdata->irq_flags);
    if(pdata->irq_gpio < 0){
        dev_err(dev,"%s: not find convert,irq-gpio\n", __func__);
    }
        
    pdata->backlight_en = of_get_named_gpio_flags(np,
            "backlight-en", 0, &pdata->backlight_en_flags);
    if(pdata->backlight_en < 0){
        dev_err(dev,"%s: not find backlight-en\n", __func__);
    }
    
    pdata->rst_gpio = of_get_named_gpio_flags(np,
            "convert,rst-gpio", 0, &pdata->rst_flags);
    if(pdata->rst_gpio < 0){
        dev_err(dev,"%s: not find convert,rst-gpio\n", __func__);
        ret = -1;
    }

    return ret;
}

static void convert_init(struct i2c_client *client)
{
    convert_write_array(client, convert_init_setup, sizeof(convert_init_setup));
    msleep(1000);
    convert_read_array(client, convert_init_setup, sizeof(convert_init_setup));
}

static int convert_resume(struct device *dev) 
{
    struct convert_platform_data *pdata = dev_get_drvdata(dev);
    struct i2c_client *client = container_of(dev, struct i2c_client, dev);
    convert_i2c_write8(client, I2C_ADDR_MAIN, 0x0D, 0x01);  //PLL enable
    msleep(10);
    convert_i2c_write8(client, I2C_ADDR_MAIN, 0x09, 0x01);  //soft reset

    if (gpio_is_valid(pdata->backlight_en))
        gpio_set_value_cansleep(pdata->backlight_en, 1);
    return 0;
}

static int convert_suspend(struct device *dev) {
    struct convert_platform_data *pdata = dev_get_drvdata(dev);
    struct i2c_client *client = container_of(dev, struct i2c_client, dev);
    convert_i2c_write8(client, I2C_ADDR_MAIN, 0x0D, 0x00);  //PLL disable
    msleep(10);
    convert_i2c_write8(client, I2C_ADDR_MAIN, 0x09, 0x00);  //soft reset
    
    if (gpio_is_valid(pdata->backlight_en))
        gpio_set_value_cansleep(pdata->backlight_en, 0);
    return 0;
}

static int convert_remove(struct i2c_client *client) {
    struct convert_platform_data *pdata = i2c_get_clientdata(client);
    convert_power_off(pdata);
    convert_power_configure(pdata, false);
    convert_gpio_configure(pdata, false);
   
#ifdef CONVERT_DEBUG
    cancel_delayed_work_sync(&pdata->convert_check_hpd_work_id);
    flush_workqueue(pdata->workq);
    destroy_workqueue(pdata->workq);
#endif
#if defined(CONFIG_FB)
    if(fb_unregister_client(&pdata->fb_notif))
        dev_err(&client->dev, "Error occurred while unregistering fb_notifier.\n");
#endif
    devm_kfree(&client->dev, pdata);
    return 0;
}


#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
                unsigned long event, void *data)
{
    struct fb_event *evdata = data;
    int *blank;
    struct convert_platform_data *pdata =
        container_of(self, struct convert_platform_data, fb_notif);

    if (evdata && evdata->data && event == FB_EVENT_BLANK &&
            pdata && pdata->client) {
        blank = evdata->data;
        if (*blank == FB_BLANK_UNBLANK)
            convert_resume(&pdata->client->dev);
        else if (*blank == FB_BLANK_POWERDOWN)
            convert_suspend(&pdata->client->dev);
    }

    return 0; 
}

#endif

#ifdef CONVERT_DEBUG
static void convert_check_hpd_work(struct work_struct *work)
{
    struct convert_platform_data *pdata;
    struct delayed_work *dw = to_delayed_work(work);
    u8 reg_val = 0;

    pdata = container_of(dw, struct convert_platform_data,
            convert_check_hpd_work_id);
    if (!pdata) {
        dev_err(&pdata->client->dev, "%s: invalid input\n", __func__);
        return;
    }

    convert_i2c_read8(pdata->client, I2C_ADDR_MAIN, 0xE5, &reg_val);
    convert_i2c_write8(pdata->client, I2C_ADDR_MAIN, 0xE5, 0xff);

    queue_delayed_work(pdata->workq, &pdata->convert_check_hpd_work_id, 500);
}
#endif

static int convert_probe(struct i2c_client *client,
        const struct i2c_device_id *id) 
{
    int ret = 0;
    struct convert_platform_data *pdata;
    
    if(client->dev.of_node){
        pdata = devm_kzalloc(&client->dev, 
            sizeof(struct convert_platform_data), GFP_KERNEL);
        if(!pdata) {
            dev_err(&client->dev, "Failed to allocate memory for convert_platform_data\n");
            return -ENOMEM;
        }
        
        ret = convert_parse_dt(&client->dev, pdata);
        if(ret){
            dev_err(&client->dev, "%s: DT parsing failed\n", __func__);
            goto err_dt_parse;
        }
    } else {
        pdata = client->dev.platform_data;
    }

    if(!pdata) {
        dev_err(&client->dev, "Invalid pdata\n");
        return -EINVAL;
    }
    

    if (!i2c_check_functionality(client->adapter,
                I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
            return -EIO;
    }
    
    pdata->client = client;
    if(!IS_ERR_OR_NULL(pdata->convert_pinctrl)){ 
        ret = pinctrl_select_state(pdata->convert_pinctrl,
            pdata->pinctrl_state_active);
        if (ret < 0)
            dev_err(&client->dev, "%s: Failed to select %s pinstate %d\n",
                __func__, PINCTRL_STATE_ACTIVE, ret);
    }

    ret = convert_gpio_configure(pdata, true);
    if(ret){
        pr_err("%s: Failed to configure GPIOs\n", __func__);
        goto err_dt_parse;
    }
    
    ret = convert_power_configure(pdata, true);
    if(ret != 0){
       goto gpio_free; 
    }
 
    ret = convert_power_on(pdata);
    if(ret != 0){
        goto power_free;
    }

    i2c_set_clientdata(client, pdata);
    dev_err(&client->dev,"convert IC probe success\n");
    dev_err(&client->dev,"init convert IC ...\n");
    convert_init(client);

#ifdef CONVERT_DEBUG
    pdata->workq = create_workqueue("convert_workq");
    if (!pdata->workq) {
        dev_err(&client->dev, "%s: workqueue creation failed.\n", __func__);
        ret = -EPERM;
        goto err_workqueue;
    }
    INIT_DELAYED_WORK(&pdata->convert_check_hpd_work_id, convert_check_hpd_work); //there   is issue in irq, so we use work queue to read connect status
    queue_delayed_work(pdata->workq, &pdata->convert_check_hpd_work_id, 5 * 100);
#endif

#if defined(CONFIG_FB)
    pdata->fb_notif.notifier_call = fb_notifier_callback;
    ret = fb_register_client(&pdata->fb_notif);
    if(ret){
        dev_err(&client->dev, "Unable to register fb_notifier: %d\n", ret);
    }
#endif
    return 0;

#ifdef CONVERT_DEBUG
err_workqueue:
    convert_power_off(pdata);
#endif
power_free:
    convert_power_configure(pdata, false);
gpio_free:
    convert_gpio_configure(pdata, false);
err_dt_parse:
    devm_kfree(&client->dev, pdata);
   return ret; 
}

static const struct of_device_id convert_match_table[] = {
    { .compatible = "qcom,mipi_convert",},
    {},
};

static const struct i2c_device_id convert_id[] = {
    {"mipi_convert", 0},
    {},
};
MODULE_DEVICE_TABLE(i2c, convert_id);

static const struct dev_pm_ops convert_pm_ops =
{ 
    .suspend = convert_suspend,
    .resume = convert_resume, 
};

static struct i2c_driver convert_driver = {
    .driver = {
        .name = "mipi_convert",
        .owner    = THIS_MODULE,
        .of_match_table = convert_match_table,
        .pm = &convert_pm_ops,
    },
    .probe    = convert_probe,
    .remove   = convert_remove,
    .id_table = convert_id,
};

module_i2c_driver(convert_driver);

MODULE_AUTHOR("Kyle <kyle.gao@quectel.com>");
MODULE_DESCRIPTION("MIPI CONVERT driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
