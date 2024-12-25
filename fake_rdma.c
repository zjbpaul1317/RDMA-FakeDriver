#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#define DEVICE_NAME "uverbs0"
#define CLASS_NAME "infiniband"
static int major;
static struct class *ib_class;
static struct device *ib_device;
static struct cdev cdev;

static struct file_operations fops = {
    .owner = THIS_MODULE,
};
static int __init uverbs_init(void)
{
    int ret;
    dev_t dev;
    printk(KERN_INFO "Initializing fake_rdma module\n");

    ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "Failed to allocate char device region, ret = %d\n", ret);
        return ret;
    }
    major = MAJOR(dev);
    printk(KERN_INFO "Allocated char device region successfully, major = %d, minor = %d\n", MAJOR(dev), MINOR(dev));

    cdev_init(&cdev, &fops);
    cdev.owner = THIS_MODULE;
    ret = cdev_add(&cdev, dev, 1);
    if (ret < 0) {
        printk(KERN_ERR "Failed to add char device, ret = %d\n", ret);
        unregister_chrdev_region(dev, 1);
        return ret;
    }
    printk(KERN_INFO "Added char device successfully\n");

    ib_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(ib_class)) {
        printk(KERN_ERR "Failed to create device class, error = %ld\n", PTR_ERR(ib_class));
        cdev_del(&cdev);
        unregister_chrdev_region(dev, 1);
        return PTR_ERR(ib_class);
    }
    printk(KERN_INFO "Created device class successfully\n");

    ib_device = device_create(ib_class, NULL, dev, NULL, DEVICE_NAME);
    if (IS_ERR(ib_device)) {
        printk(KERN_ERR "Failed to create device, error = %ld\n", PTR_ERR(ib_device));
        class_destroy(ib_class);
        cdev_del(&cdev);
        unregister_chrdev_region(dev, 1);
        return PTR_ERR(ib_device);
    }
    printk(KERN_INFO "Created device successfully\n");
    printk(KERN_INFO "fake_rdma device created successfully\n");
    return 0;
}
static void __exit uverbs_exit(void)
{
    device_destroy(ib_class, MKDEV(major, 0));
    class_destroy(ib_class);
    cdev_del(&cdev);
    unregister_chrdev_region(MKDEV(major, 0), 1);
    printk(KERN_INFO "fake_rdma device removed\n");
}
module_init(uverbs_init);
module_exit(uverbs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhang Jaibao");
MODULE_DESCRIPTION("A fake RDMA uverbs device driver");