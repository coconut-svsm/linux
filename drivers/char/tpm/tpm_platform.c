// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform based TPM emulator
 *
 * Copyright (C) 2023 James.Bottomley@HansenPartnership.com
 *
 * Designed to handle a simple function request/response single buffer
 * TPM or vTPM rooted in the platform.  This device driver uses the
 * MSSIM protocol from the Microsoft reference implementation
 *
 * https://github.com/microsoft/ms-tpm-20-ref
 *
 * to communicate between the driver and the platform.  This is rich
 * enough to allow platform operations like cancellation The platform
 * should not act on platform commands like power on/off and reset
 * which can disrupt the TPM guarantees.
 *
 * This driver is designed to be single threaded (one call in to the
 * platform TPM at any one time).  The threading guarantees are
 * provided by the chip mutex.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/tpm_platform.h>

#include "tpm.h"

static struct tpm_platform_ops *pops;

static u8 *buffer;
/*
 * FIXME: before implementing locality we need to agree what it means
 * to the platform
 */
static u8 locality;

static int tpm_platform_send(struct tpm_chip *chip, u8 *buf, size_t len)
{
	int ret;
	struct tpm_send_cmd_req *req = (struct tpm_send_cmd_req *)buffer;

	if (len > TPM_PLATFORM_MAX_BUFFER - sizeof(*req))
		return -EINVAL;
	req->cmd = TPM_SEND_COMMAND;
	req->locality = locality;
	req->inbuf_size = len;
	memcpy(req->inbuf, buf, len);

	ret = pops->sendrcv(buffer);
	if (ret)
		return ret;

	return 0;
}

static int tpm_platform_recv(struct tpm_chip *chip, u8 *buf, size_t len)
{
	struct tpm_resp *resp = (struct tpm_resp *)buffer;

	if (resp->size < 0)
		return resp->size;

	if (len < resp->size)
		return -E2BIG;

	memcpy(buf, buffer + sizeof(*resp), resp->size);

	return resp->size;
}

static struct tpm_class_ops tpm_chip_ops = {
	.flags = TPM_OPS_AUTO_STARTUP,
	.send = tpm_platform_send,
	.recv = tpm_platform_recv,
};

static struct platform_driver tpm_platform_driver = {
	.driver = {
		.name = "tpm",
	},
};

static int __init tpm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tpm_chip *chip;
	int err;

	if (!dev->platform_data)
		return -ENODEV;

	/*
	 * in theory platform matching should mean this is always
	 * true, but just in case anyone tries force binding
	 */
	if(strcmp(pdev->name, tpm_platform_driver.driver.name) != 0)
		return -ENODEV;

	if (!buffer)
		buffer = kmalloc(TPM_PLATFORM_MAX_BUFFER, GFP_KERNEL);

	if (!buffer)
		return -ENOMEM;

	pops = dev->platform_data;

	chip = tpmm_chip_alloc(dev, &tpm_chip_ops);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	/*
	 * Setting TPM_CHIP_FLAG_IRQ guarantees that ->recv will be
	 * called straight after ->send and means we don't need to
	 * implement any other chip ops.
	 */
	chip->flags |= TPM_CHIP_FLAG_IRQ;
	err = tpm2_probe(chip);
	if (err)
		return err;

	err = tpm_chip_register(chip);
	if (err)
		return err;

	dev_info(dev, "TPM %s platform device\n",
		 (chip->flags & TPM_CHIP_FLAG_TPM2) ? "2.0" : "1.2");

	return 0;
}

module_platform_driver_probe(tpm_platform_driver, tpm_platform_probe);

MODULE_AUTHOR("James Bottomley <James.Bottomley@HansenPartnership.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Platform TPM Driver");
MODULE_ALIAS("platform:tpm");
