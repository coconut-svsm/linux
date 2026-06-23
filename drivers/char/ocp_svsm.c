// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>

#define OCP_CLASS "ocp"
#define OCP_DEVICE "ocp"

#define OCP_MAX_BUFFER_SIZE 4096

static struct class *cls;
static dev_t ocp_dev_num;

struct ocp_dev {
	struct cdev cdev;
	struct mutex buffer_mutex;
	u8 *buffer;
};

static int ocp_open(struct inode *inode, struct file *file)
{
	struct ocp_dev *ocp;

	ocp = container_of(inode->i_cdev, struct ocp_dev, cdev);

	file->private_data = ocp;

	return 0;
}

static const struct file_operations ocp_fops = {
	.owner = THIS_MODULE,
	.open = ocp_open,
};

static int __init ocp_svsm_probe(struct platform_device *pdev)
{
	struct ocp_dev *ocp;
	struct device *dev;
	int ret;

	ocp = kzalloc_obj(*ocp);
	if (!ocp)
		return -ENOMEM;

	ocp->buffer = kzalloc(OCP_MAX_BUFFER_SIZE, GFP_KERNEL);
	if (!ocp->buffer) {
		ret = -ENOMEM;
		goto err_ocp;
	}

	mutex_init(&ocp->buffer_mutex);

	cdev_init(&ocp->cdev, &ocp_fops);
	ocp->cdev.owner = THIS_MODULE;

	platform_set_drvdata(pdev, ocp);

	ret = cdev_add(&ocp->cdev, ocp_dev_num, 1);
	if (ret < 0)
		goto err_buffer;

	dev = device_create(cls, &pdev->dev, ocp_dev_num, NULL, OCP_DEVICE);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_dev;
	}

	return 0;

err_dev:
	cdev_del(&ocp->cdev);
err_buffer:
	mutex_destroy(&ocp->buffer_mutex);
	kfree(ocp->buffer);
err_ocp:
	kfree(ocp);
	return ret;
}

static void __exit ocp_svsm_remove(struct platform_device *pdev)
{
	struct ocp_dev *ocp = platform_get_drvdata(pdev);

	device_destroy(cls, ocp_dev_num);
	cdev_del(&ocp->cdev);
	mutex_destroy(&ocp->buffer_mutex);
	kfree(ocp->buffer);
	kfree(ocp);
}

/*
 * ocp_svsm_remove() lives in .exit.text. For drivers registered via
 * platform_driver_probe() this is ok because they cannot get unbound
 * at runtime. So mark the driver struct with __refdata to prevent modpost
 * triggering a section mismatch warning.
 */
static struct platform_driver ocp_svsm_driver __refdata = {
	.remove = __exit_p(ocp_svsm_remove),
	.driver = {
		.name = "ocp-svsm",
	},
};

static int __init ocp_svsm_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&ocp_dev_num, 0, 1, OCP_DEVICE);
	if (ret < 0)
		return ret;

	cls = class_create(OCP_CLASS);
	if (IS_ERR(cls)) {
		ret = PTR_ERR(cls);
		goto err_class;
	}

	// TODO: add permission to avoid using sudo?

	ret = platform_driver_probe(&ocp_svsm_driver, ocp_svsm_probe);
	if (ret < 0)
		goto err_probe;

	pr_info("ocp-svsm: successfully initialized ocp driver\n");
	return 0;

err_probe:
	class_destroy(cls);
err_class:
	unregister_chrdev_region(ocp_dev_num, 1);
	return ret;
}

static void __exit ocp_svsm_exit(void)
{
	platform_driver_unregister(&ocp_svsm_driver);
	class_destroy(cls);
	unregister_chrdev_region(ocp_dev_num, 1);
	pr_info("successfully removed ocp driver\n");
}

module_init(ocp_svsm_init);
module_exit(ocp_svsm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nicola Ramacciotti");
MODULE_DESCRIPTION("SNP SVSM OCP Driver");
