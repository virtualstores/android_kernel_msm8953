#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/gpio.h>                 
#include <linux/fs.h>  
#include <linux/interrupt.h>

#define DEVICE_NAME "solenoid"
#define CLASS_NAME "gpio_solenoid"
#define SOLENOID_GPIO 36

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrik Nyman");
MODULE_DESCRIPTION("Solenoid driver for TT1");
MODULE_VERSION("0.1");

static int majorNumber;
static struct class* solenoidClass = NULL;
static struct device* solenoidDevice = NULL;
static struct delayed_work solenoidWork;

//todo: Implement the function aswell
//static ssize_t dev_write(struct file *, char *, size_t, loff_t *);

/*static struct file_operations fops =
{
   .owner = THIS_MODULE,
   .write = dev_write
};
*/
/*static int charArrayToInteger(const char *numArray, int *value) {
    int n = 0;
    return sscanf(numArray, "%d%n", value, &n) > 0
       &&  numArray[n] == '\0';
}*/

/*
static int vs_activate_solenoid(int delay) {
    cancel_delayed_work_sync(vs_solenoid_work);
    gpio_set_value(SOLENOID_GPIO, 1);
    schedule_delayed_work(vs_solenoid_work, delay);
}*/

static void vs_deactivate_solenoid(void) {
    gpio_set_value(SOLENOID_GPIO, 0);
    printk(KERN_INFO "Deactivated solenoid\n");
}

static void vs_solenoid_work(struct work_struct *work) {
    vs_deactivate_solenoid();
}

static void vs_activate_solenoid(const int unsigned delay) {
    cancel_delayed_work(&solenoidWork);
    gpio_set_value(SOLENOID_GPIO, 1);
    schedule_delayed_work(&solenoidWork, msecs_to_jiffies(delay));

    printk(KERN_INFO "Started solenoid\n");
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
    unsigned int value;
    char* ptr;
    value = simple_strtoul(buffer, &ptr, 10);
    printk(KERN_INFO "PParsed number");
    if(value != 0) {
	printk(KERN_INFO "Activating solenoid");
        vs_activate_solenoid(value);
    } else {
        printk(KERN_INFO "Solenoid: Bad delay intput, must be a number\n");
    }
    return len;
}

static struct file_operations fops =
{
   .owner = THIS_MODULE,
   .write = dev_write
};

/*static int vs_activate_solenoid(int delay) {
    cancel_delayed_work_sync(vs_solenoid_work);
    gpio_set_value(SOLENOID_GPIO, 1);
    schedule_delayed_work(vs_solenoid_work, delay);
}

static int vs_deactivate_solenoid() {
    gpio_set_value(SOLENOID_GPIO, 0);
}

static void vs_solenoid_work(struct work_struct *work) {
    vs_deactivate_solenoid();
}
*/
static int __init vs_init(void){
    int result = 0;

    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
    if (majorNumber < 0){
        printk(KERN_ALERT "Solenoid failed to register a major number\n");
        return majorNumber;
    }
    printk(KERN_INFO "Solenoid: registered correctly with major number %d\n", majorNumber);

    solenoidClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(solenoidClass)) {
        unregister_chrdev(majorNumber, DEVICE_NAME);
        printk(KERN_ALERT "Failed to register device class\n");
        return PTR_ERR(solenoidClass);
    }

    solenoidDevice = device_create(solenoidClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
    if (IS_ERR(solenoidDevice)) {
        class_destroy(solenoidClass);
        unregister_chrdev(majorNumber, DEVICE_NAME);
        printk(KERN_ALERT "Failed to create the device\n");
        return PTR_ERR(solenoidDevice);
    }
    printk(KERN_INFO "Solenoid: device class created correctly\n");

    //Setup hrtimer
    INIT_DELAYED_WORK(&solenoidWork, vs_solenoid_work);

    //Change to a struct and send it via the hrtimer of_device thingie

    //Need exit function properly and error habndling above is ugly

    if(!gpio_is_valid(SOLENOID_GPIO)) {
        printk(KERN_ALERT "Solenoid: Invalid GPIO 146 \n");
        goto fail;
    }

    if(gpio_request(SOLENOID_GPIO, "solenoid") < 0) {
        printk(KERN_ALERT "Solenoid: Failed to request GPIO 146 \n");
        goto fail;
    } 

    if(gpio_direction_output(SOLENOID_GPIO, 0)) {
        printk(KERN_ALERT "Solenoid: Failed to set direction for GPIO 146 \n");
        goto free_gpio;
    };

    printk(KERN_INFO "Solenoid: Finished setup\n");

   return result;

free_gpio:
   gpio_free(SOLENOID_GPIO);
   
fail:
   return -ENODEV; 
}
 
static void __exit vs_exit(void){
    gpio_set_value(SOLENOID_GPIO, 0);
    gpio_free(SOLENOID_GPIO);
    cancel_delayed_work(&solenoidWork);
    class_destroy(solenoidClass);
    unregister_chrdev(majorNumber, DEVICE_NAME);
}
 
module_init(vs_init);
module_exit(vs_exit);
