/* linux/drivers/modem/modem.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2010 Samsung Electronics.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>

#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/wakelock.h>

#include <linux/platform_data/modem.h>
#include "modem_prj.h"
#include "modem_variation.h"

static struct modem_ctl *create_modemctl_device(struct platform_device *pdev)
{
	int ret = 0;
	struct modem_data *pdata;
	struct modem_ctl *modemctl;
	struct device *dev = &pdev->dev;

	/* create modem control device */
	modemctl = kzalloc(sizeof(struct modem_ctl), GFP_KERNEL);
	if (!modemctl)
		return NULL;

    	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__); 
	#endif	

	modemctl->dev = dev;
	modemctl->phone_state = STATE_OFFLINE;

	pdata = pdev->dev.platform_data;
	modemctl->name = pdata->name;
    
	/* initialize link device list */
	INIT_LIST_HEAD(&modemctl->link_dev_list);

	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	
	
	/* init modemctl device for getting modemctl operations */
	ret = call_modem_init_func(modemctl, pdata);
	if (ret) {
		kfree(modemctl);
		return NULL;
	}
	
	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__); 
	#endif	
		
	pr_info("[MODEM_IF] %s:create_modemctl_device DONE\n", modemctl->name);
	return modemctl;
}

static struct io_device *create_io_device(struct modem_io_t *io_t,
		struct modem_ctl *modemctl, enum modem_network modem_net)
{
	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	
	
	int ret = 0;
	struct io_device *iod = NULL;

	iod = kzalloc(sizeof(struct io_device), GFP_KERNEL);
	if (!iod) {
		pr_err("[MODEM_IF] io device memory alloc fail\n");
		return NULL;
	}
	
	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	
	
	iod->name = io_t->name;
	iod->id = io_t->id;
	iod->format = io_t->format;
	iod->io_typ = io_t->io_type;
	iod->link_type = io_t->link;
	iod->net_typ = modem_net;

	/* link between io device and modem control */
	iod->mc = modemctl;
	if (iod->format == IPC_FMT)
		modemctl->iod = iod;
	
	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	
		
	/* register misc device or net device */
	
	ret = init_io_device(iod);
	if (ret) {
		kfree(iod);
		return NULL;
	}
	
	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	
		
	pr_info("[MODEM_IF] %s : create_io_device DONE\n", io_t->name);
	return iod;
}

static int __devinit modem_probe(struct platform_device *pdev)
{
	int i;
	struct modem_data *pdata;
	struct modem_ctl *modemctl;
	struct io_device *iod[MAX_NUM_IO_DEV];
	struct link_device *ld;
	struct io_raw_devices *io_raw_devs = NULL;

	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	
	 
	pdata = pdev->dev.platform_data;
	memset(iod, 0, sizeof(iod));

	modemctl = create_modemctl_device(pdev);
	if (!modemctl)
		return -ENOMEM;

	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	
	
	/* create link device */
	/* support multi-link device */
	for (i = 0; i < LINKDEV_MAX ; i++) {
		/* find matching link type */
		if (pdata->link_types & (1 << i)) {
			ld = call_link_init_func(pdev, i);
			if (ld) {
				ld->link_type = i;
				list_add(&ld->list, &modemctl->link_dev_list);
			} else
				goto err_free_modemctl;
		}
	}
	
	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	
	
	io_raw_devs = kzalloc(sizeof(struct io_raw_devices), GFP_KERNEL);
	if (!io_raw_devs)
		return -ENOMEM;

	/* create io deivces and connect to modemctl device */
	for (i = 0; i < pdata->num_iodevs; i++) {
		iod[i] = create_io_device(&pdata->iodevs[i], modemctl,
					pdata->modem_net);
		
		#ifdef MODEM_IF_DEBUG_STMTS
		printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
		#endif	
		
		if (!iod[i])
			goto err_free_modemctl;
		/* find link type for this io device */
		list_for_each_entry(ld, &modemctl->link_dev_list, list) {
			if (ld->link_type == iod[i]->link_type)
				break;
		}

		#ifdef MODEM_IF_DEBUG_STMTS
		printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
		#endif	
	
		if (!ld) {
			pr_err("[%s] no matching link device\n", __func__);
			goto err_free_modemctl;
		}

			#ifdef MODEM_IF_DEBUG_STMTS
			printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
			#endif	

		if (iod[i]->format == IPC_RAW) {
			int ch = iod[i]->id & 0x1F;
			io_raw_devs->raw_devices[ch] = iod[i];
			io_raw_devs->num_of_raw_devs++;
			iod[i]->link = ld;

			#ifdef MODEM_IF_DEBUG_STMTS
			printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
			#endif	

		} else {
			/* connect io devices to one link device */
			ld->attach(ld, iod[i]);
			#ifdef MODEM_IF_DEBUG_STMTS
			printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);	
			#endif	
	
		}

  		if (iod[i]->format == IPC_MULTI_RAW)
			iod[i]->private_data = (void *)io_raw_devs;

		#ifdef MODEM_IF_DEBUG_STMTS
		printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
		#endif	
		
	}

	platform_set_drvdata(pdev, modemctl);

	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	
	
	pr_info("[MODEM_IF] modem_probe DONE\n");
	return 0;

err_free_modemctl:
	/** TODO:
	 *  search link dev list in modemctl, and free its free(close) function
	 *  but we don't have link close function now...
	*/
	for (i = 0; i < pdata->num_iodevs; i++)
		if (iod[i] != NULL)
			kfree(iod[i]);

	if (io_raw_devs != NULL)
		kfree(io_raw_devs);

	if (modemctl != NULL)
		kfree(modemctl);
	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	

	return -ENOMEM;
}

static void modem_shutdown(struct platform_device *pdev)
{
	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	

	struct device *dev = &pdev->dev;
	struct modem_ctl *mc = dev_get_drvdata(dev);
	mc->ops.modem_off(mc);
}

static int modem_suspend(struct device *pdev)
{
	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	

	struct modem_ctl *mc = dev_get_drvdata(pdev);
	gpio_set_value(mc->gpio_pda_active, 0);
	return 0;
}

static int modem_resume(struct device *pdev)
{
	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	

	struct modem_ctl *mc = dev_get_drvdata(pdev);
	gpio_set_value(mc->gpio_pda_active, 1);
	return 0;
}

static const struct dev_pm_ops modem_pm_ops = {
	.suspend    = modem_suspend,
	.resume     = modem_resume,
};

static struct platform_driver modem_driver = {
	.probe = modem_probe,
	.shutdown = modem_shutdown,
	.driver = {
		.name = "modem_if",
		.pm   = &modem_pm_ops,
	},
};

static int __init modem_init(void)
{
	#ifdef MODEM_IF_DEBUG_STMTS
	printk("[MODEM_IF] inside  %s  %d \n",__FUNCTION__,__LINE__);
	#endif	

	return platform_driver_register(&modem_driver);
}

module_init(modem_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung Modem Interface Driver");