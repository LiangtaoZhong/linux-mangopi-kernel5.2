// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for keys on GPIO lines capable of not generating interrupts.
 *
 * Copyright 2022 Lorenzo
 * Copyright 2022, 2022 Lorenzo <liangtaozhong@gmail.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>
#include <dt-bindings/input/gpio-keys.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/of_gpio.h>
#include <linux/input.h>
#include <linux/semaphore.h>

#define KEYINPUT_NAME "keyinput"
#define DELAY_MS 50
static int nbuttons;
static bool rep;
struct key_dev {
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    struct timer_list timer;
    struct input_dev *inputdev;
};

struct key_desc {
    int gpio;
    u32 code;
    const char *label;
};

static struct key_desc* key = NULL;
static struct key_dev key_dev;

void timer_func(struct timer_list* timer) {
    int i = 0;
    struct key_desc *pkey = NULL;
    pkey = key;
    for (i = 0; i < nbuttons; ++i, ++pkey) {
        if (gpio_get_value(pkey->gpio) == 0) {
            mdelay(10);
            if (gpio_get_value(pkey->gpio) == 0) {
                input_report_key(key_dev.inputdev, pkey->code, 1);
                input_sync(key_dev.inputdev);
            } 
        } else {
            input_report_key(key_dev.inputdev, pkey->code, 0);
            input_sync(key_dev.inputdev);
        }
    }

    mod_timer(&key_dev.timer, jiffies + msecs_to_jiffies(DELAY_MS));

}


static const struct of_device_id gpio_keys_of_match[] = {
    {.compatible = "gpio-keys-scan"},
    { },
};

static int key_probe(struct platform_device *pdev) {
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;
    struct device_node *next;
    int i = 0;
    int ret;
    nbuttons = of_get_child_count(np);
    printk("having %d buttons\r\n", nbuttons);
    if (!nbuttons)
        return ERR_PTR(-ENODEV);
    key = devm_kzalloc(dev, nbuttons * sizeof(struct key_desc), 
        GFP_KERNEL);
    if (key == NULL) {
        return -ENOMEM; 
    }
    struct key_desc* pkey = key;

    rep = of_property_read_bool(np, "autorepeat");
    printk("rep : %d\n", rep);

    for_each_child_of_node(np, next) {
        
        of_property_read_string(next, "label", &(pkey->label));
        of_property_read_u32(next, "linux,code", &(pkey->code));
        printk("name: %s, label: %s, code: %d\n", next->name, 
            pkey->label, pkey->code);
        pkey->gpio = of_get_named_gpio(next, "gpios", 0);
        if (pkey->gpio < 0) {
            printk("can't request gpio: %s\r\n", pkey->label);
            return -ENODEV;
        }
        gpio_request(pkey->gpio, pkey->label);
        gpio_direction_input(pkey->gpio);
        pkey++;
    }

    key_dev.inputdev = input_allocate_device();
    if (NULL == key_dev.inputdev)
    {
            printk(KERN_ERR "input_allocate_device error");
            return -1;
    }

    key_dev.inputdev->name = KEYINPUT_NAME;
    key_dev.inputdev->evbit[0] = BIT_MASK(EV_KEY);
    if (rep) {
        key_dev.inputdev->evbit[0] = BIT_MASK(EV_REP);
    }

    for (i = 0, pkey = key; i < nbuttons; ++i, ++pkey) {
        input_set_capability(key_dev.inputdev, EV_KEY, pkey->code);
    }

    ret = input_register_device(key_dev.inputdev);
    if (ret) {
        printk("register input device failed\n");
        for (i = 0, pkey = key; i < nbuttons; ++i, ++pkey) {
            gpio_free(pkey->gpio);
        }
        input_unregister_device(key_dev.inputdev);
        return ret;
    }
    /*
     * Use timer to scan all buttons
     */
    timer_setup(&key_dev.timer, timer_func, 0);
    mod_timer(&key_dev.timer, jiffies + msecs_to_jiffies(DELAY_MS));
    return 0;
}

static int key_remove(struct platform_device *dev) {
    input_unregister_device(key_dev.inputdev);
    input_free_device(key_dev.inputdev);
    del_timer_sync(&key_dev.timer);

    return 0;
}


static struct platform_driver gpio_keys_scan_driver = {
    .driver = {
        .name = "scan-keys",
        .of_match_table = gpio_keys_of_match,
    },
    .probe = key_probe,
    .remove = key_remove,
};

static int  __init keydriver_init(void) {
    return platform_driver_register(&gpio_keys_scan_driver);
}

static void __exit keydriver_exit(void) {
    platform_driver_unregister(&gpio_keys_scan_driver);
}


module_init(keydriver_init);
module_exit(keydriver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lorenzo <liangtaozhong@gmail.com>");
MODULE_DESCRIPTION("Keyboard driver for GPIOs");
MODULE_ALIAS("platform:gpio-keys-scan");