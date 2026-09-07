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
#include <uapi/linux/ocp_svsm.h>
#include <asm/sev.h>

#define OCP_CLASS "ocp"
#define OCP_DEVICE "ocp"

#define OCP_MAX_BUFFER_SIZE 4096
#define OCP_MAX_NAME_SIZE 240

static struct class *cls;
static dev_t ocp_dev_num;

struct ocp_dev {
	struct cdev cdev;
	struct mutex buffer_mutex;
	u8 *bytes_buffer;
	u8 *name_buffer;
};

static int ocp_open(struct inode *inode, struct file *file)
{
	struct ocp_dev *ocp;

	ocp = container_of(inode->i_cdev, struct ocp_dev, cdev);

	file->private_data = ocp;

	return 0;
}

static long ocp_ioctl_list_objects(char *bytes_buffer, void __user *argp)
{
	struct ocp_svsm_list_objects list_objects;
	int ret = 0;
	u32 value_moved = 0;
	u32 buf_size_required = 0;

	if (copy_from_user(&list_objects, argp, sizeof(list_objects)))
		return -EFAULT;

	if (list_objects.buf_size > OCP_MAX_BUFFER_SIZE) {
		return -EINVAL;
	}

	ret = snp_svsm_ocp_list_sources(bytes_buffer, NULL,
					list_objects.buf_size, &value_moved,
					&buf_size_required);
	if (ret)
		return ret;

	list_objects.buf_size_required = buf_size_required;

	if (copy_to_user(u64_to_user_ptr(list_objects.buf_ptr), bytes_buffer,
			 value_moved))
		return -EFAULT;

	if (copy_to_user(argp, &list_objects, sizeof(list_objects)))
		return -EFAULT;

	return value_moved;
}

static long ocp_ioctl_list_object_sources(char *bytes_buffer, char *name_buffer,
					  void __user *argp)
{
	struct ocp_svsm_list_sources list_sources;
	int ret = 0;
	u32 value_moved = 0;
	u32 buf_size_required = 0;

	if (copy_from_user(&list_sources, argp, sizeof(list_sources)))
		return -EFAULT;

	if (list_sources.buf_size > OCP_MAX_BUFFER_SIZE) {
		return -EINVAL;
	}

	// TODO: size check for name_buffer, it is not always OCP_MAX_NAME_SIZE
	if (copy_from_user(name_buffer,
			   u64_to_user_ptr(list_sources.name_buf_ptr),
			   OCP_MAX_NAME_SIZE))
		return -EFAULT;

	ret = snp_svsm_ocp_list_sources(bytes_buffer, name_buffer,
					list_sources.buf_size, &value_moved,
					&buf_size_required);
	if (ret)
		return ret;

	list_sources.buf_size_required = buf_size_required;

	if (copy_to_user(u64_to_user_ptr(list_sources.buf_ptr), bytes_buffer,
			 value_moved))
		return -EFAULT;

	if (copy_to_user(argp, &list_sources, sizeof(list_sources)))
		return -EFAULT;

	return value_moved;
}

static long ocp_ioctl_read_source(char *bytes_buffer, char *name_buffer,
				  void __user *argp)
{
	struct ocp_svsm_read_write_source read_source;
	int ret = 0;
	u32 value_moved = 0;

	if (copy_from_user(&read_source, argp, sizeof(read_source)))
		return -EFAULT;

	if (read_source.bytes_to_copy > OCP_MAX_BUFFER_SIZE) {
		return -EINVAL;
	}

	// TODO: size check for name_buffer, it is not always OCP_MAX_SOURCE_NAME_SIZE
	if (copy_from_user(name_buffer,
			   u64_to_user_ptr(read_source.name_buf_ptr),
			   OCP_MAX_SOURCE_NAME_SIZE * 2))
		return -EFAULT;

	ret = snp_svsm_ocp_read_source(bytes_buffer, name_buffer,
				       read_source.bytes_to_copy,
				       read_source.offset, &value_moved);
	if (ret)
		return ret;

	if (copy_to_user(u64_to_user_ptr(read_source.buf_ptr), bytes_buffer,
			 value_moved))
		return -EFAULT;

	return value_moved;
}

static long ocp_ioctl_write_source(char *bytes_buffer, char *name_buffer,
				   void __user *argp)
{
	struct ocp_svsm_read_write_source write_source;
	int ret = 0;
	u32 value_moved = 0;

	if (copy_from_user(&write_source, argp, sizeof(write_source)))
		return -EFAULT;

	if (write_source.bytes_to_copy > OCP_MAX_BUFFER_SIZE) {
		return -EINVAL;
	}

	// TODO: size check for name_buffer, it is not always OCP_MAX_SOURCE_NAME_SIZE
	if (copy_from_user(name_buffer,
			   u64_to_user_ptr(write_source.name_buf_ptr),
			   OCP_MAX_SOURCE_NAME_SIZE * 2))
		return -EFAULT;

	if (copy_from_user(bytes_buffer, u64_to_user_ptr(write_source.buf_ptr),
			   write_source.bytes_to_copy))
		return -EFAULT;

	ret = snp_svsm_ocp_write_source(bytes_buffer, name_buffer,
					write_source.bytes_to_copy,
					write_source.offset, &value_moved);
	if (ret)
		return ret;

	return value_moved;
}

static long ocp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ocp_dev *ocp = file->private_data;
	void __user *argp = (void __user *)arg;
	int ret = 0;

	mutex_lock(&ocp->buffer_mutex);

	switch (cmd) {
	case OCP_SVSM_IOCTL_LIST_OBJECTS:
		ret = ocp_ioctl_list_objects(ocp->bytes_buffer, argp);
		break;
	case OCP_SVSM_IOCTL_LIST_OBJECT_SOURCES:
		ret = ocp_ioctl_list_object_sources(ocp->bytes_buffer,
						    ocp->name_buffer, argp);
		break;
	case OCP_SVSM_IOCTL_READ_SOURCE:
		ret = ocp_ioctl_read_source(ocp->bytes_buffer, ocp->name_buffer,
					    argp);
		break;
	case OCP_SVSM_IOCTL_WRITE_SOURCE:
		ret = ocp_ioctl_write_source(ocp->bytes_buffer,
					     ocp->name_buffer, argp);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	mutex_unlock(&ocp->buffer_mutex);
	return ret;
}

static const struct file_operations ocp_fops = {
	.owner = THIS_MODULE,
	.open = ocp_open,
	.unlocked_ioctl = ocp_ioctl,
};

static int __init ocp_svsm_probe(struct platform_device *pdev)
{
	struct ocp_dev *ocp;
	struct device *dev;
	int ret;

	ocp = kzalloc_obj(*ocp);
	if (!ocp)
		return -ENOMEM;

	ocp->bytes_buffer = kzalloc(OCP_MAX_BUFFER_SIZE, GFP_KERNEL);
	if (!ocp->bytes_buffer) {
		ret = -ENOMEM;
		goto err_ocp;
	}

	ocp->name_buffer = kzalloc(OCP_MAX_NAME_SIZE, GFP_KERNEL);
	if (!ocp->name_buffer) {
		ret = -ENOMEM;
		goto err_buffer;
	}

	mutex_init(&ocp->buffer_mutex);

	cdev_init(&ocp->cdev, &ocp_fops);
	ocp->cdev.owner = THIS_MODULE;

	platform_set_drvdata(pdev, ocp);

	ret = cdev_add(&ocp->cdev, ocp_dev_num, 1);
	if (ret < 0)
		goto err_name;

	dev = device_create(cls, &pdev->dev, ocp_dev_num, NULL, OCP_DEVICE);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_dev;
	}

	return 0;

err_dev:
	cdev_del(&ocp->cdev);
err_name:
	mutex_destroy(&ocp->buffer_mutex);
	kfree(ocp->name_buffer);
err_buffer:
	kfree(ocp->bytes_buffer);
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
	kfree(ocp->name_buffer);
	kfree(ocp->bytes_buffer);
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
