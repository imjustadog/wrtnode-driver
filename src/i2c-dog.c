#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <uapi/linux/i2c-dev.h>

#define I2C_DOG_MAJOR	91		/* Device major number		*/
struct i2cdog_data {
	dev_t devt;
	struct list_head device_entry;
	unsigned int users;
	struct i2c_adapter *adap;
        struct i2c_client *cli
};
static LIST_HEAD(device_list);

static noinline int i2cdog_ioctl_rdrw(struct i2c_client *client, unsigned long arg)
{
	struct i2c_rdwr_ioctl_data rdwr_arg;
	struct i2c_msg *rdwr_pa;
	u8 __user **data_ptrs;
	int i, res;

	if (copy_from_user(&rdwr_arg,
			   (struct i2c_rdwr_ioctl_data __user *)arg,
			   sizeof(rdwr_arg)))
		return -EFAULT;

	/* Put an arbitrary limit on the number of messages that can
	 * be sent at once */
	if (rdwr_arg.nmsgs > I2C_RDRW_IOCTL_MAX_MSGS)
		return -EINVAL;

	rdwr_pa = memdup_user(rdwr_arg.msgs,
			      rdwr_arg.nmsgs * sizeof(struct i2c_msg));
	if (IS_ERR(rdwr_pa))
		return PTR_ERR(rdwr_pa);

	data_ptrs = kmalloc(rdwr_arg.nmsgs * sizeof(u8 __user *), GFP_KERNEL);
	if (data_ptrs == NULL) {
		kfree(rdwr_pa);
		return -ENOMEM;
	}

	res = 0;
	for (i = 0; i < rdwr_arg.nmsgs; i++) {
		/* Limit the size of the message to a sane amount */
		if (rdwr_pa[i].len > 8192) {
			res = -EINVAL;
			break;
		}

		data_ptrs[i] = (u8 __user *)rdwr_pa[i].buf;
		rdwr_pa[i].buf = memdup_user(data_ptrs[i], rdwr_pa[i].len);
		if (IS_ERR(rdwr_pa[i].buf)) {
			res = PTR_ERR(rdwr_pa[i].buf);
			break;
		}

		/*
		 * If the message length is received from the slave (similar
		 * to SMBus block read), we must ensure that the buffer will
		 * be large enough to cope with a message length of
		 * I2C_SMBUS_BLOCK_MAX as this is the maximum underlying bus
		 * drivers allow. The first byte in the buffer must be
		 * pre-filled with the number of extra bytes, which must be
		 * at least one to hold the message length, but can be
		 * greater (for example to account for a checksum byte at
		 * the end of the message.)
		 */
		if (rdwr_pa[i].flags & I2C_M_RECV_LEN) {
			if (!(rdwr_pa[i].flags & I2C_M_RD) ||
			    rdwr_pa[i].buf[0] < 1 ||
			    rdwr_pa[i].len < rdwr_pa[i].buf[0] +
					     I2C_SMBUS_BLOCK_MAX) {
				res = -EINVAL;
				break;
			}

			rdwr_pa[i].len = rdwr_pa[i].buf[0];
		}
	}
	if (res < 0) {
		int j;
		for (j = 0; j < i; ++j)
			kfree(rdwr_pa[j].buf);
		kfree(data_ptrs);
		kfree(rdwr_pa);
		return res;
	}

	res = i2c_transfer(client->adapter, rdwr_pa, rdwr_arg.nmsgs);
	while (i-- > 0) {
		if (res >= 0 && (rdwr_pa[i].flags & I2C_M_RD)) {
			if (copy_to_user(data_ptrs[i], rdwr_pa[i].buf,
					 rdwr_pa[i].len))
				res = -EFAULT;
		}
		kfree(rdwr_pa[i].buf);
	}
	kfree(data_ptrs);
	kfree(rdwr_pa);
	return res;
}

static long i2cdog_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct i2cdog_data *i2cdog;
	struct i2c_client *client;
	unsigned long funcs;

	i2cdog = filp->private_data;
	client = i2cdog->cli;

	dev_dbg(&client->adapter->dev, "ioctl, cmd=0x%02x, arg=0x%02lx\n",
		cmd, arg);

	switch (cmd) {

	case I2C_RDWR:
		return i2cdog_ioctl_rdrw(client, arg);

	default:
		/* NOTE:  returning a fault code here could cause trouble
		 * in buggy userspace code.  Some old kernel bugs returned
		 * zero in this case, and userspace code might accidentally
		 * have depended on that bug.
		 */
		return -ENOTTY;
	}
	return 0;
}

static int i2cdog_open(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "i2c dog open!!!\n");
	struct i2cdog_data *i2cdog;
	int status = -ENXIO;

	list_for_each_entry(i2cdog, &device_list, device_entry) 
	{
		if (i2cdog->devt == inode->i_rdev) 
		{
			status = 0;
			break;
		}
	}

	if (status) 
	{
		pr_debug("i2cdog: nothing for minor %d\n", iminor(inode));
		goto err_find_dev;
	}

	i2cdog->users++;
	filp->private_data = i2cdog;
	nonseekable_open(inode, filp);

	return 0;

err_find_dev:
	return status;
}

static int i2cdog_release(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "i2c dog release!!!\n");
	struct i2cdog_data *i2cdog;
	int status = 0;

	i2cdog = filp->private_data;
	filp->private_data = NULL;

	/* last close? */
	i2cdog->users--;

	return status;
}

static const struct file_operations i2cdog_fops = {
	.owner		= THIS_MODULE,
	.open		= i2cdog_open,
	.unlocked_ioctl	= i2cdog_ioctl,
	.release	= i2cdog_release,
};


static struct class *i2c_dog_class;

static int i2cdog_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	printk(KERN_INFO "i2c dog probe!!!\n");
	struct device *dev;
        struct i2cdog_data *i2cdog;
	int status;

	/* Allocate driver data */
	i2cdog = kzalloc(sizeof(*i2cdog), GFP_KERNEL);
	if (!i2cdog)
		return -ENOMEM;

	/* Initialize the driver data */
	i2cdog->devt = MKDEV(I2C_DOG_MAJOR, 0);
	i2cdog->adap = i2c->adapter;
        i2cdog->cli = i2c;
	i2cdog->users = 0;

	INIT_LIST_HEAD(&i2cdog->device_entry);
	dev = device_create(i2c_dog_class, &i2c->dev, MKDEV(I2C_DOG_MAJOR, 0),
				    NULL, "i2cdog");
	status = PTR_ERR_OR_ZERO(dev);
	if (status == 0) 
	{
		list_add(&i2cdog->device_entry, &device_list);
		i2c_set_clientdata(i2c, i2cdog);
	}
	else
		kfree(i2cdog);

	return status;
}

static int i2cdog_i2c_remove(struct i2c_client *i2c)
{
	printk(KERN_INFO "i2c dog remove!!!\n");
	struct i2cdog_data *i2cdog;
	i2cdog = i2c_get_clientdata(i2c);
	i2cdog->cli = NULL;
	list_del(&i2cdog->device_entry);
        device_destroy(i2c_dog_class, MKDEV(I2C_DOG_MAJOR, 0));
	if (i2cdog->users == 0)
		kfree(i2cdog);
	return 0;
}

static const struct i2c_device_id i2cdog_i2c_ids[] = {};

static const struct of_device_id i2cdog_dt_ids[] = {
    { .compatible = "adoge,i2ctest", },
    {  }
};

static struct i2c_driver i2cdog_i2c_driver = {
    .driver = {
        .name = "i2c_dog",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(i2cdog_dt_ids),
    },
    .probe = i2cdog_i2c_probe,
    .remove = i2cdog_i2c_remove,
    .id_table = i2cdog_i2c_ids,
};

static int __init i2c_dog_init(void)
{
	int res;

	printk(KERN_INFO "i2c dog entries driver\n");

	res = register_chrdev(I2C_DOG_MAJOR, "i2cdogdev", &i2cdog_fops);
	if (res)
		goto out;

	i2c_dog_class = class_create(THIS_MODULE, "i2cdogclass");
	if (IS_ERR(i2c_dog_class)) {
		res = PTR_ERR(i2c_dog_class);
		goto out_unreg_chrdev;
	}

	res = i2c_add_driver(&i2cdog_i2c_driver);
	if (res)
		goto out_unreg_class;

	return 0;

out_unreg_class:
	class_destroy(i2c_dog_class);
out_unreg_chrdev:
	unregister_chrdev(I2C_DOG_MAJOR, "i2cdogdev");
out:
	printk(KERN_ERR "%s: Driver Initialisation failed\n", __FILE__);
	return res;
}

static void __exit i2c_dog_exit(void)
{
	printk(KERN_INFO "i2c dog exit driver\n");
	i2c_del_driver(&i2cdog_i2c_driver);
	class_destroy(i2c_dog_class);
	unregister_chrdev(I2C_DOG_MAJOR, "i2cdogdev");
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl> and "
		"Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C dog entries driver");
MODULE_LICENSE("GPL");

module_init(i2c_dog_init);
module_exit(i2c_dog_exit);


