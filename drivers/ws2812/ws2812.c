/*
 * Driver for simulating a mouse on GPIO lines.
 *
 * Copyright (C) 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/irq.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input-polldev.h>
#include <linux/gpio.h>
#include <linux/gpio_mouse.h>
#include <linux/init.h>
static int hello_init(void)
{
	gpio_set_value(12,0);
	//gpio_direction_output(GPIO_PF0,1);
	printk(KERN_INFO "[init] Can you feel me?\n");
	return 0;
}

static void hello_exit(void)
{
    printk(KERN_INFO "[exit] Yes.\n");
}


module_init(hello_init);
module_exit(hello_exit);

MODULE_AUTHOR("Sanby <admin@trtos.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ws2812 Rgb Module");
MODULE_ALIAS("Ws2812  module");
