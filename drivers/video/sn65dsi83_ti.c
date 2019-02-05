#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/fb.h>

#define RESET_GPIO 33

struct vs_info {
    struct i2c_client *client;
    int reset;
    struct notifier_block fb_notif;
};

struct reg_cfg {
    u8 reg;
    u8 val;
    int sleep;
};

static struct reg_cfg init_cfg[] = {
    {0x09, 0x00, 0},
    {0x0A, 0x03, 0},
    {0x0B, 0x10, 0},
    {0x0D, 0x00, 10},
    {0x10, 0x26, 0},
    {0x11, 0x00, 0},
    {0x12, 0x28, 0},
    {0x13, 0x00, 0},
    {0x18, 0x7a, 0},
    {0x19, 0x00, 0},
    {0x1A, 0x03, 0},
    {0x1B, 0x00, 0},
    {0x20, 0x00, 0},
    {0x21, 0x04, 0},
    {0x22, 0x00, 0},
    {0x23, 0x00, 0},
    {0x24, 0x58, 0},
    {0x25, 0x02, 0},
    {0x26, 0x00, 0},
    {0x27, 0x00, 0},
    {0x28, 0x20, 0},
    {0x29, 0x00, 0},
    {0x2A, 0x00, 0},
    {0x2B, 0x00, 0},
    {0x2C, 0x0c, 0},
    {0x2D, 0x00, 0},
    {0x2E, 0x00, 0},
    {0x2F, 0x00, 0},
    {0x30, 0x04, 0},
    {0x31, 0x00, 0},
    {0x32, 0x00, 0},
    {0x33, 0x00, 0},
    {0x34, 0x2c, 0},
    {0x35, 0x00, 0},
    {0x36, 0x09, 0},
    {0x37, 0x00, 0},
    {0x38, 0x30, 0},
    {0x39, 0x00, 0},
    {0x3A, 0x09, 0},
    {0x3B, 0x00, 0},
    {0x3C, 0x00, 0},
    {0x3D, 0x00, 0},
    {0x3E, 0x00, 5},
    {0x0D, 0x01, 10},
    {0x09, 0x01, 5},
    {0xe5, 0xff, 0}
};

static struct reg_cfg enable_cfg[] = {
    {0x0D, 0x01, 10},
    {0x09, 0x01, 5},
    {0xe5, 0xff, 0}
};

static struct reg_cfg disable_cfg[] = {
    {0x0D, 0x00, 10},
    {0xe5, 0xff, 0}
};

static int sn_write_i2c(struct i2c_client *client, u8 reg, u8 value)
{
    int ret = 0;
    client->addr = 0x2c;
    ret = i2c_smbus_write_byte_data(client, reg, value);
    if(ret < 0)
        dev_err(&client->dev, "I2C write reg:%x error\n", reg);
    return ret;
}

static int sn_read_i2c(struct i2c_client *client, u8 reg, u8 *value)
{
    int ret = 0;
    ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0) {
        dev_err(&client->dev, "I2C read reg:%x error\n", reg);
        return ret;
    }
    *value = (u8)ret;
    dev_info(&client->dev, "reg: %x, READ8: %x\n", reg, *value);
    return 0;
}

static int sn_write_array(struct i2c_client *client,
    struct reg_cfg *cfg, int size)
{
    int ret = 0;
    int i;

    size = size / sizeof(struct reg_cfg);
    for (i = 0; i < size; i++) {
        ret = sn_write_i2c(client, cfg[i].reg, cfg[i].val);
        if (ret != 0){
            dev_err(&client->dev, "%s: sn reg write %02X to %02X failed.\n",
                __func__, cfg[i].val, cfg[i].reg);
            return ret;
        }
        if (cfg[i].sleep)
            msleep(cfg[i].sleep);
    }
    return 0;
}

static int sn_init(struct i2c_client *client) {
    return sn_write_array(client, init_cfg, sizeof(init_cfg)); 
}

static int sn_enable_screen(struct i2c_client *client) {
    return sn_write_array(client, enable_cfg, sizeof(enable_cfg)); 
}

static int sn_disable_screen(struct i2c_client *client) {
    return sn_write_array(client, disable_cfg, sizeof(disable_cfg));
}

static int sn_suspend(struct device *dev) {
    struct vs_info *chip = dev_get_drvdata(dev);
    struct i2c_client *client = chip->client;//container_of(dev, struct i2c_client, dev);
    u8 value;
    int ret = sn_disable_screen(client);
    
    if(ret != 0) {
        dev_err(dev, "%s: Failed to turn off screen, returned %d\n", __func__, ret);
        return ret;
    }
    
    ret = sn_read_i2c(client, 0xe5, &value);
    if(ret != 0) {
        dev_err(&client->dev, "%s: Failed to read error register, returned %d\n", __func__, ret);
        return ret;
    }
    dev_info(&client->dev, "%s: Read error register, returned %x\n", __func__, value);

    dev_info(dev, "%s: Suspending sn65dsi83 IC\n", __func__);
    return 0;
}

static int sn_resume(struct device *dev) {
    struct vs_info *chip = dev_get_drvdata(dev);
    struct i2c_client *client = chip->client;//container_of(dev, struct i2c_client, dev);
    u8 value;
    int ret = 0;
    ret = sn_enable_screen(client);

    ret = sn_read_i2c(client, 0xe5, &value);
    if(ret != 0) {
        dev_err(&client->dev, "%s: Failed to read error register, returned %d\n", __func__, ret);
        return ret;
    }
    dev_info(&client->dev, "%s: Read error register, returned %x\n", __func__, value);



    if(ret != 0) {
        dev_err(dev, "%s: Failed to enable screen, returned %d\n", __func__, ret);
        return ret;
    }

    dev_info(dev, "%s: Resuming sn65dsi83 IC\n", __func__);
    return 0;
}

static int fb_notifier_callback(struct notifier_block *self,
                unsigned long event, void *data)
{
    struct fb_event *evdata = data;
    int *blank;
    struct vs_info *pdata =
        container_of(self, struct vs_info, fb_notif);

    if (evdata && evdata->data && event == FB_EVENT_BLANK &&
            pdata && pdata->client) {
        blank = evdata->data;
        if (*blank == FB_BLANK_UNBLANK)
            sn_resume(&pdata->client->dev);
        else if (*blank == FB_BLANK_POWERDOWN)
            sn_suspend(&pdata->client->dev);
    }

    return 0;
}

static int sn_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret = -1;
    struct vs_info *chip;
    u8 value;

    chip = devm_kzalloc(&client->dev, sizeof(struct vs_info), GFP_KERNEL);
    if(!chip) {
        dev_err(&client->dev, "%s: Failed to allocate memoery for vs_info\n", __func__);
        return -12;
    } 

    chip->client = client;
    chip->reset = RESET_GPIO;

    
    if(gpio_is_valid(chip->reset)) {
        ret = gpio_request(chip->reset, "sn_reset");
        if(ret < 0) {
            dev_err(&client->dev, "%s: Failed to request GPIO %d, returned %d\n", __func__, chip->reset, ret);
            goto free_chip;
        }
        ret = gpio_direction_output(chip->reset, 1);
        if(ret) {
            dev_err(&client->dev, "%s: Failed to set direction out on GPIO %d, returned %d\n", __func__, chip->reset, ret);
            goto free_gpio;
        }
    } else {
        dev_err(&client->dev, "%s: Failed to validate GPIO %d\n", __func__, chip->reset);
        goto free_chip;
    }

    i2c_set_clientdata(client, chip);
    dev_info(&client->dev, "%s: Successfully registered i2c device\n", __func__);
   
    ret = sn_init(client);

    if(ret != 0) {
        dev_err(&client->dev, "%s: Failed to initialize sn65dsi83 IC, returned %d\n", __func__, ret);
        goto free_gpio;
    }

    ret = sn_read_i2c(client, 0xe5, &value);
    if(ret != 0) {
        dev_err(&client->dev, "%s: Failed to read error register, returned %d\n", __func__, ret);
        goto free_gpio;
    }
    dev_info(&client->dev, "%s: Read error register, returned %x\n", __func__, value);

    chip->fb_notif.notifier_call = fb_notifier_callback;
    ret = fb_register_client(&chip->fb_notif);
    if(ret){
        dev_err(&client->dev, "Unable to register fb_notifier: %d\n", ret);
    }

    return 0;

free_gpio:
    if(gpio_is_valid(chip->reset))
        gpio_free(chip->reset);
free_chip:
    devm_kfree(&client->dev, chip);
    return ret;
}

static int sn_remove(struct i2c_client *client) {
    struct vs_info *chip = i2c_get_clientdata(client);

    if(gpio_is_valid(chip->reset))
        gpio_free(chip->reset);

    if(fb_unregister_client(&chip->fb_notif))
        dev_err(&client->dev, "Error occurred while unregistering fb_notifier.\n");

    devm_kfree(&client->dev, chip);
    chip = NULL;
    return 0;
}

static const struct of_device_id sn_match_table[] = {
    { .compatible = "vs,sn65dsi83",},
    {},
};

static const struct i2c_device_id sn_id[] = {
    {"sn65dsi83", 0},
    {},
};
MODULE_DEVICE_TABLE(i2c, sn_id);


static const struct dev_pm_ops sn_pm_ops =
{ 
    .suspend = sn_suspend,
    .resume = sn_resume, 
};


static struct i2c_driver sn_driver = {
    .driver = {
        .name = "sn65dsi83",
        .owner = THIS_MODULE,
        .of_match_table = sn_match_table,
        .pm = &sn_pm_ops
    },
    .probe    = sn_probe,
    .remove   = sn_remove,
    .id_table = sn_id,
};

module_i2c_driver(sn_driver);

MODULE_AUTHOR("Patrik Nyman <patrik.nyman@virtualstores.se>");
MODULE_DESCRIPTION("TEXAS INSTRUMENTS SN65DSI83 driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
