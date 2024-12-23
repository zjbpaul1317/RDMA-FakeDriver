#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/infiniband/verbs.h>

struct fake_rdma_device {
    struct ib_device ib_dev;
};


static struct fake_rdma_device *fake_dev;


static int fake_query_device(struct ib_device *ib_dev, struct ib_device_attr *device_attr, struct ib_udata *udata);

static struct ib_pd *fake_alloc_pd(struct ib_device *ib_dev, struct ib_udata *udata);
static int fake_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata);

static struct ib_qp *fake_create_qp(struct ib_pd *pd, struct ib_qp_init_attr *attr, struct ib_udata *udata);
static int fake_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata);
static int fake_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int mask, struct ib_udata *udata);

static struct ib_cq *fake_create_cq(struct ib_device *ib_dev, const struct ib_cq_init_attr *attr, struct ib_udata *udata);
static int fake_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);

static int fake_poll_cq(struct ib_cq *ibcq, struct ib_udata *udata);
static int fake_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)


static struct ib_mr *fake_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 length, u64 virt_addr, u64 iova, int access, struct ib_udata *udata);
static struct ib_mr *fake_get_dma_mr(struct ib_pd *ibpd, int acc);

static int fake_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr, struct ib_send_wr **bad_wr);
static int fake_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr, struct ib_recv_wr **bad_wr);

// ib_device_ops
static struct ib_device_ops fake_dev_ops = {
    .owner = THIS_MODULE,
    .query_device = fake_query_device,
    .alloc_pd = fake_alloc_pd,
    .dealloc_pd = fake_dealloc_pd,
    .create_qp = fake_create_qp,
    .destroy_qp = fake_destroy_qp,
    .modify_qp = fake_modify_qp,
    .create_cq = fake_create_cq,
    .destroy_cq = fake_destroy_cq,
    .poll_cq = fake_poll_cq,
    .req_notify_cq = fake_req_notify_cq,
    .reg_user_mr = fake_reg_user_mr,
    .get_dma_mr = fake_get_dma_mr,
    .post_send = fake_post_send,
    .post_recv = fake_post_recv,
};

// init function
static int __init fake_rdma_init(void)
{
    int ret;

    // allocate memory for the fake device
    fake_dev = kzalloc(sizeof(*fake_dev), GFP_KERNEL);
    if(!fake_dev) {
        return -ENOMEM;
    }

    // initialize the ib_device structure
    ib_device_init(&fake_dev->ib_dev, &fake_dev->ib_dev, NULL);
    fake_dev->ib_dev.node_type = RDMA_NODE_UNKNOWN;
    fake_dev->ib_dev.device = "fake_rdma";
    fake_dev->ib_dev.ops = &fake_dev_ops;

    ret = ib_register_device(&fake_dev->ib_dev, NULL);
    if (ret) {
        ib_device_put(&fake_dev->ib_dev);
        kfree(fake_dev);
        return ret;
    }

    ret = ib_register_device(&fake_dev->ib_dev, NULL);
    if (ret) {
        ib_device_put(&fake_dev->ib_dev);
        kfree(fake_dev);
        return ret;
    }

    printk(KERN_INFO "Fake RDMA device registered\n");
    return 0;
}

static void __exit fake_rdma_exit(void)
{
    if (fake_dev) {
        ib_unregister_device(&fake_dev->ib_dev);
        ib_device_put(&fake_dev->ib_dev);
        kfree(fake_dev);
        printk(KERN_INFO "Fake RDMA device unregistered\n");
    }
}

module_init(fake_rdma_init);
module_exit(fake_rdma_exit);




