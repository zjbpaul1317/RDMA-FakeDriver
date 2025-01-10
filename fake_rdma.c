#include "linux/init.h"
#include "linux/kern_levels.h"
#include "linux/kmod.h"
#include "linux/printk.h"
#include <linux/module.h>

#include <rdma/ib_verbs.h>

#include "fake_rdma.h"

MODULE_AUTHOR("zhangjiabao");
MODULE_LICENSE("Dual BSD/GPL");

static inline struct frdma_dev *to_fdev(struct ib_device *ibdev)
{
    return container_of(ibdev, struct frdma_dev, ibdev);
}

struct frdma_qp
{
    struct ib_qp *qp;
};

struct frdma_cq
{
    struct ib_cq *cq;
};

// static struct ib_device *frdma_device;

static int frdma_get_port_immutable(struct ib_device *ibdev,
                                    u32 port,
                                    struct ib_port_immutable *port_immutable)
{
    struct ib_port_attr attr = {};
    int err;

    if (port != 1)
    {
        err = -EINVAL;
        dev_err(&ibdev->dev, "bad port_num = %d\n", port);
        goto err_out;
    }

    err = ib_query_port(ibdev, port, &attr);
    if (err)
        goto err_out;

    port_immutable->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;
    port_immutable->pkey_tbl_len = attr.pkey_tbl_len;
    port_immutable->gid_tbl_len = attr.gid_tbl_len;
    port_immutable->max_mad_size = IB_MGMT_MAD_SIZE;

    return 0;

err_out:
    return err;
}

static int frdma_query_device(struct ib_device *ibdev,
                              struct ib_device_attr *attr,
                              struct ib_udata *data)
{
    struct frdma_dev *dev = to_fdev(ibdev);
    int err;

    if (data->inlen || data->outlen)
    {
        dev_err(&ibdev->dev, "malformed udata\n");
        err = -EINVAL;
        goto err_out;
    }

    memset(attr, 0, sizeof(*attr));
    memcpy(attr, &dev->attrs, sizeof(*attr));

    return 0;

err_out:
    dev_err(&ibdev->dev, "returned err = %d\n", err);
    return err;
}

static int frdma_query_port(struct ib_device *ibdev,
                            u32 port,
                            struct ib_port_attr *attr)
{
    struct frdma_dev *dev = to_fdev(ibdev);
    int err;

    if (port != 1)
    {
        err = -EINVAL;
        dev_err(&ibdev->dev, "bad port_num = %d\n", port);
        goto err_out;
    }

    memcpy(attr, &dev->port.attr, sizeof(*attr));

    attr->state = IB_PORT_ACTIVE;
    attr->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
    return 0;

err_out:
    dev_err(&ibdev->dev, "returned err = %d\n", err);
    return err;
}

static int frdma_query_pkey(struct ib_device *ibdev,
                            u32 port_num,
                            u16 index,
                            u16 *pkey)
{
    // struct frdma_dev* dev = to_fdev( ibdev );
    int err;

    if (index != 0)
    {
        err = -EINVAL;
        dev_err(&ibdev->dev, "bad pkey index = %d\n", index);
        goto err_out;
    }
    *pkey = IB_DEFAULT_PKEY_FULL;
    return 0;

err_out:
    dev_err(&ibdev->dev, "returned err = %d\n", err);
    return err;
}

static int frdma_enable_driver(struct ib_device *ibdev)
{
    dev_warn(&ibdev->dev, "entry enable_driver");
    // ibdev->kverbs_provider = true;
    dev_info(&ibdev->dev, "Device node created: %s\n", ibdev->dev.kobj.name);
    dev_info(&ibdev->dev, "device->kverbs_provider is %s", ibdev->kverbs_provider ? "true" : "false");
    dev_info(&ibdev->dev, "device->node_type: %d\n", ibdev->node_type);

    struct frdma_dev *dev = to_fdev(ibdev);
    struct ib_event ev;
    dev->port.attr.state = IB_PORT_ACTIVE;

    ev.device = &dev->ibdev;
    ev.element.port_num = 1;
    // ev.event = IB_EVENT_PORT_ACTIVE;
    ib_dispatch_event(&ev);

    return 0;
}

#define FRDMA_MAX_CONTEXT 1024
#define FRDMA_MMAP_IO_NC 0

struct frdma_ext_db_info
{
    bool enable;
    u16 sdb_off;
    u16 rdb_off;
    u16 cdb_off;
};

struct frdma_ucontext
{
    struct frdma_dev *dev;
    struct ib_ucontext ibucontext;

    struct frdma_ext_db_info ext_db;

    struct list_head dbrecords_page_list;
    struct mutex dbrecords_page_mutex;

    u64 *sdb;
    u64 *rdb;
    u64 *cdb;

    struct rdma_user_mmap_entry *sq_db_mmap_entry;
    struct rdma_user_mmap_entry *rq_db_mmap_entry;
    struct rdma_user_mmap_entry *cq_db_mmap_entry;
};

static inline struct frdma_ucontext *to_fctx(struct ib_ucontext *ibuc)
{
    return container_of(ibuc, struct frdma_ucontext, ibucontext);
}

int alloc_db_resources(struct frdma_dev *dev, struct frdma_ucontext *ctx, bool extend_db)
{
    ctx->sdb = kzalloc(PAGE_SIZE, GFP_KERNEL);
    if (!ctx->sdb)
        return -ENOMEM;

    ctx->rdb = kzalloc(PAGE_SIZE, GFP_KERNEL);
    if (!ctx->rdb)
        goto err_free_sdb;

    ctx->cdb = kzalloc(PAGE_SIZE, GFP_KERNEL);
    if (!ctx->cdb)
        goto err_free_rdb;

    if (extend_db)
    {
    }

    return 0;

err_free_rdb:
    kfree(ctx->rdb);
err_free_sdb:
    kfree(ctx->sdb);
    return -ENOMEM;
}

void free_db_resources(struct frdma_dev *dev, struct frdma_ucontext *ctx)
{
    kfree(ctx->sdb);
    kfree(ctx->rdb);
    kfree(ctx->cdb);
}

void *frdma_user_mmap_entry_insert(struct frdma_ucontext *ctx, void *addr, size_t size, int flags, void *uresp_field)
{
    return addr;
}

void frdma_uctx_user_mmap_entries_remove(struct frdma_ucontext *ctx)
{
}

#define FRDMA_DEV_CAP_FLAGS_EXTEND_DB 0x01

int frdma_alloc_ucontext(struct ib_ucontext *ibuc, struct ib_udata *udata)
{
    printk(KERN_WARNING "test01--------\n");
    struct frdma_ucontext *ctx;
    struct frdma_dev *dev = to_fdev(ibuc->device);
    int ret;
    struct
    {
        u32 dev_id;

    } uresp = {};

    printk(KERN_WARNING "test02--------\n");

    if (atomic_inc_return(&dev->num_ctx) > FRDMA_MAX_CONTEXT)
    {
        ret = -ENOMEM;
        goto err_out;
    }

    printk(KERN_WARNING "test03--------\n");

    if (udata->outlen < sizeof(uresp))
    {
        ret = -EINVAL;
        goto err_out;
    }

    printk(KERN_WARNING "test04--------\n");

    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
    {
        ret = -ENOMEM;
        goto err_out;
    }

    printk(KERN_WARNING "test05--------\n");
    ctx->dev = dev;
    INIT_LIST_HEAD(&ctx->dbrecords_page_list);
    mutex_init(&ctx->dbrecords_page_mutex);

    printk(KERN_WARNING "test06--------\n");

    ret = alloc_db_resources(dev, ctx, !!(dev->attrs.device_cap_flags & FRDMA_DEV_CAP_FLAGS_EXTEND_DB));
    if (ret)
        goto err_free_ctx;

    printk(KERN_WARNING "test07--------\n");

    ctx->sq_db_mmap_entry = frdma_user_mmap_entry_insert(ctx, ctx->sdb, PAGE_SIZE, FRDMA_MMAP_IO_NC, &uresp.dev_id);
    if (!ctx->sq_db_mmap_entry)
    {
        ret = -ENOMEM;
        goto err_free_resources;
    }

    printk(KERN_WARNING "test08--------\n");
    ctx->rq_db_mmap_entry = frdma_user_mmap_entry_insert(ctx, ctx->rdb, PAGE_SIZE, FRDMA_MMAP_IO_NC, &uresp.dev_id);
    if (!ctx->rq_db_mmap_entry)
    {
        ret = -EINVAL;
        goto err_remove_mmap_entries;
    }

    printk(KERN_WARNING "test09--------\n");
    ctx->cq_db_mmap_entry = frdma_user_mmap_entry_insert(ctx, ctx->cdb, PAGE_SIZE, FRDMA_MMAP_IO_NC, &uresp.dev_id);
    if (!ctx->cq_db_mmap_entry)
    {
        ret = -EINVAL;
        goto err_remove_mmap_entries;
    }

    // ibuc->driver_ctx = ctx;

    uresp.dev_id = dev->pdev->device;

    printk(KERN_WARNING "test10--------\n");

    ret = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
    if (ret)
        goto err_remove_mmap_entries;

err_remove_mmap_entries:
    frdma_uctx_user_mmap_entries_remove(ctx);
err_free_resources:
    free_db_resources(dev, ctx);
err_free_ctx:
    kfree(ctx);
err_out:
    atomic_dec(&dev->num_ctx);
    return ret;
}

void frdma_dealloc_ucontext(struct ib_ucontext *ibctx)
{
    struct frdma_dev *dev = to_fdev(ibctx->device);
    struct frdma_ucontext *ctx = to_fctx(ibctx);

    frdma_uctx_user_mmap_entries_remove(ctx);
    free_db_resources(dev, ctx);
    atomic_dec(&dev->num_ctx);
    return;
}

static int frdma_mmap(struct ib_ucontext *ibuc, struct vm_area_struct *vma)
{
    return 0;
}

static int frdma_alloc_pd(struct ib_pd *pd, struct ib_udata *udata)
{
    return 0;
}

static int frdma_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata)
{
    return 0;
}

static int frdma_alloc_mr(struct ib_pd *pd, struct ib_udata *udata)
{
    return 0;
}

static int frdma_alloc_mw(struct ib_pd *pd, struct ib_udata *udata)
{
    return 0;
}

static int frdma_post_send(struct ib_qp *qp, const struct ib_send_wr *wr,
                           const struct ib_send_wr **bad_wr)
{
    // pr_info("%s: Post send\n", DRIVER_NAME);
    // return ib_post_send(qp, wr, bad_wr);
    return -EOPNOTSUPP;
}

static int frdma_post_recv(struct ib_qp *qp, const struct ib_recv_wr *wr,
                           const struct ib_recv_wr **bad_wr)
{
    // pr_info("%s: Post recv\n", DRIVER_NAME);
    // return ib_post_recv(qp, wr, bad_wr);
    return -EOPNOTSUPP;
}

static int frdma_create_qp(struct ib_qp *qp, struct ib_qp_init_attr *qp_init_attr,
                           struct ib_udata *udata)
{
    // pr_info("%s: Create QP\n", DRIVER_NAME);
    // *qp = ib_create_qp(ib_dev, init_attr);
    // if (IS_ERR(*qp))
    //     return PTR_ERR(*qp);

    return 0;
}

static int frdma_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr,
                           int attr_mask, struct ib_udata *udata)
{
    // pr_info("%s: Modify QP\n", DRIVER_NAME);
    // return ib_modify_qp(qp, attr, attr_mask, init_attr);
    return 0;
}

static int frdma_destroy_qp(struct ib_qp *qp, struct ib_udata *udata)
{
    // pr_info("%s: Destroy QP\n", DRIVER_NAME);
    // ib_destroy_qp(qp);
    return 0;
}

static int frdma_create_cq(struct ib_device *ib_dev, const struct ib_cq_init_attr *cq_attr,
                           struct ib_cq **cq, struct ib_udata *udata)
{
    // pr_info("%s: Create CQ\n", DRIVER_NAME);
    // *cq = ib_create_cq(ib_dev, NULL, NULL, udata, cq_attr->cqe);
    // if (IS_ERR(*cq))
    //     return PTR_ERR(*cq);

    return 0;
}

static int frdma_destroy_cq(struct ib_cq *cq, struct ib_udata *udata)
{
    // pr_info("%s: Destroy CQ\n", DRIVER_NAME);
    // ib_destroy_cq(cq);
    return 0;
}

static int frdma_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc)
{
    // pr_info("%s: Poll CQ\n", DRIVER_NAME);
    // return ib_poll_cq(cq, num_entries, wc);
    return 0;
}

struct rdma_hw_stats *frdma_ib_alloc_hw_port_stats(struct ib_device *ibdev, u32 port_num)
{
    return 0;
}

const struct ib_device_ops frdma_device_ops = {
    .owner = THIS_MODULE,
    .driver_id = RDMA_DRIVER_RXE,
    .uverbs_abi_ver = 2,

    .alloc_hw_port_stats = frdma_ib_alloc_hw_port_stats,
    //.alloc_mr = frdma_alloc_mr,
    //.alloc_mw = frdma_alloc_mw,
    .alloc_ucontext = frdma_alloc_ucontext,
    .alloc_pd = frdma_alloc_pd,
    .create_qp = frdma_create_qp,
    .modify_qp = frdma_modify_qp,
    .destroy_qp = frdma_destroy_qp,
    .poll_cq = frdma_poll_cq,
    .dealloc_pd = frdma_dealloc_pd,
    .query_device = frdma_query_device,
    .query_port = frdma_query_port,
    .query_pkey = frdma_query_pkey,
    .get_port_immutable = frdma_get_port_immutable,
    .dealloc_ucontext = frdma_dealloc_ucontext,
    .enable_driver = frdma_enable_driver,
    .post_send = frdma_post_send,
    .post_recv = frdma_post_recv,
};

static struct frdma_dev *dev;

static void frdma_attr_init(struct frdma_dev *dev)
{
    dev->attrs.vendor_id = FRDMA_VENDOR_ID;
    dev->attrs.max_mr_size = FRDMA_MAX_MR_SIZE;
    dev->attrs.page_size_cap = FRDMA_PAGE_SIZE_CAP;
    dev->attrs.max_qp = FRDMA_MAX_QP;
    dev->attrs.max_qp_wr = FRDMA_MAX_QP_WR;
    dev->attrs.device_cap_flags = FRDMA_DEVICE_CAP_FLAGS;
    dev->attrs.kernel_cap_flags = IBK_ALLOW_USER_UNREG;
    dev->attrs.max_send_sge = FRDMA_MAX_SGE;
    dev->attrs.max_recv_sge = FRDMA_MAX_SGE;
    dev->attrs.max_sge_rd = FRDMA_MAX_SGE_RD;
    dev->attrs.max_cq = FRDMA_MAX_CQ;
    dev->attrs.max_cqe = (1 << FRDMA_MAX_LOG_CQE) - 1;
    dev->attrs.max_mr = FRDMA_MAX_MR;
    dev->attrs.max_mw = FRDMA_MAX_MW;
    dev->attrs.max_pd = FRDMA_MAX_PD;
    dev->attrs.max_qp_rd_atom = FRDMA_MAX_QP_RD_ATOM;
    dev->attrs.max_res_rd_atom = FRDMA_MAX_RES_RD_ATOM;
    dev->attrs.max_qp_init_rd_atom = FRDMA_MAX_QP_INIT_RD_ATOM;
    dev->attrs.atomic_cap = IB_ATOMIC_HCA;
    dev->attrs.max_mcast_grp = FRDMA_MAX_MCAST_GRP;
    dev->attrs.max_mcast_qp_attach = FRDMA_MAX_MCAST_QP_ATTACH;
    dev->attrs.max_total_mcast_qp_attach = FRDMA_MAX_TOT_MCAST_QP_ATTACH;
    dev->attrs.max_ah = FRDMA_MAX_AH;
    dev->attrs.max_srq = FRDMA_MAX_SRQ;
    dev->attrs.max_srq_wr = FRDMA_MAX_SRQ_WR;
    dev->attrs.max_srq_sge = FRDMA_MAX_SRQ_SGE;
    dev->attrs.max_fast_reg_page_list_len = FRDMA_MAX_FMR_PAGE_LIST_LEN;
    dev->attrs.max_pkeys = FRDMA_MAX_PKEYS;
    dev->attrs.local_ca_ack_delay = FRDMA_LOCAL_CA_ACK_DELAY;
    dev->attrs.sys_image_guid = 0x66616b6572646d61;
}

static void frdma_port_init(struct frdma_dev *dev)
{
    dev->port.attr.state = IB_PORT_DOWN;
    dev->port.attr.max_mtu = IB_MTU_4096;
    dev->port.attr.active_mtu = IB_MTU_256;
    dev->port.attr.gid_tbl_len = FRDMA_PORT_GID_TBL_LEN;
    dev->port.attr.port_cap_flags = FRDMA_PORT_PORT_CAP_FLAGS;
    dev->port.attr.max_msg_sz = FRDMA_PORT_MAX_MSG_SZ;
    dev->port.attr.bad_pkey_cntr = FRDMA_PORT_BAD_PKEY_CNTR;
    dev->port.attr.qkey_viol_cntr = FRDMA_PORT_QKEY_VIOL_CNTR;
    dev->port.attr.pkey_tbl_len = FRDMA_PORT_PKEY_TBL_LEN;
    dev->port.attr.lid = FRDMA_PORT_LID;
    dev->port.attr.sm_lid = FRDMA_PORT_SM_LID;
    dev->port.attr.lmc = FRDMA_PORT_LMC;
    dev->port.attr.max_vl_num = FRDMA_PORT_MAX_VL_NUM;
    dev->port.attr.sm_sl = FRDMA_PORT_SM_SL;
    dev->port.attr.subnet_timeout = FRDMA_PORT_SUBNET_TIMEOUT;
    dev->port.attr.init_type_reply = FRDMA_PORT_INIT_TYPE_REPLY;
    dev->port.attr.active_width = FRDMA_PORT_ACTIVE_WIDTH;
    dev->port.attr.active_speed = FRDMA_PORT_ACTIVE_SPEED;
    dev->port.attr.phys_state = FRDMA_PORT_PHYS_STATE;
}

static __init int frdma_init_module(void)
{
    printk(KERN_WARNING "frdma init module\n");

    int ret = 0;

    // Request the ib_uverbs module
    ret = request_module("ib_uverbs");
    if (ret)
    {
        pr_err("load ib_verbs module error, ret = %d\n", ret);
        return ret;
    }

    struct frdma_dev *frdma_dev;

    frdma_dev = kzalloc(sizeof(*frdma_dev), GFP_KERNEL);
    if (!frdma_dev)
        return -ENOMEM;

    // Allocate the device
    dev = ib_alloc_device(frdma_dev, ibdev);
    // dev = _ib_alloc_device(sizeof(struct frdma_dev));  //test
    // dev = _ib_alloc_device(sizeof(*dev));  //test
    if (!dev)
    {
        dev_err(&dev->ibdev.dev, "ib_alloc_device failed\n");
        return -ENOMEM;
    }

    idr_init(&dev->cq_idr);
    idr_init(&dev->qp_idr);
    spin_lock_init(&dev->cq_lock);
    spin_lock_init(&dev->qp_lock);

    atomic_set(&dev->num_ctx, 0);

    // Set the node type and kverbs_provider flag
    dev->ibdev.node_type = RDMA_NODE_IB_CA;
    dev->ibdev.kverbs_provider = true;
    dev_info(&dev->ibdev.dev, "Device node created: %s\n", dev->ibdev.dev.kobj.name);
    dev_info(&dev->ibdev.dev, "device->kverbs_provider is %s", dev->ibdev.kverbs_provider ? "true" : "false");
    dev_info(&dev->ibdev.dev, "device->node_type: %d\n", dev->ibdev.node_type);

    // Copy the node description
    memcpy(&dev->ibdev.node_desc, FRDMA_NODE_DESC, sizeof(FRDMA_NODE_DESC));

    // Initialize other attributes
    dev->ibdev.phys_port_cnt = 1;
    dev->ibdev.num_comp_vectors = num_possible_cpus();
    dev->ibdev.local_dma_lkey = 0;
    dev->ibdev.node_guid = 0x66616b6572646d61;

    // Set uverbs cmd mask
    dev->ibdev.uverbs_cmd_mask =
        (1ULL << IB_USER_VERBS_CMD_GET_CONTEXT) |
        (1ULL << IB_USER_VERBS_CMD_QUERY_DEVICE) |
        (1ULL << IB_USER_VERBS_CMD_QUERY_PORT) |
        (1ULL << IB_USER_VERBS_CMD_ALLOC_PD) |
        (1ULL << IB_USER_VERBS_CMD_DEALLOC_PD) |
        (1ULL << IB_USER_VERBS_CMD_POST_SEND) |
        (1ULL << IB_USER_VERBS_CMD_POST_RECV) |
        (1ULL << IB_USER_VERBS_CMD_CREATE_CQ) |
        (1ULL << IB_USER_VERBS_CMD_DESTROY_CQ) |
        (1ULL << IB_USER_VERBS_CMD_POLL_CQ) |
        (1ULL << IB_USER_VERBS_CMD_CREATE_QP) |
        (1ULL << IB_USER_VERBS_CMD_MODIFY_QP) |
        (1ULL << IB_USER_VERBS_CMD_DESTROY_QP);

    // dev->ibdev.uverbs_cmd_mask = BIT_ULL(IB_USER_VERBS_CMD_POST_SEND) |
    // 			BIT_ULL(IB_USER_VERBS_CMD_REQ_NOTIFY_CQ);

    // Initialize device attributes and port attributes
    frdma_attr_init(dev);
    frdma_port_init(dev);

    // Set the device operations
    ib_set_device_ops(&dev->ibdev, &frdma_device_ops);
    // ret = ib_device_set_netdev(&dev->ibdev, dev->netdev, 1);  //test
    // if (ret)  return ret;

    // Register the device
    ret = ib_register_device(&dev->ibdev, "frdma_%d", NULL);
    // printk(KERN_WARNING "register_device_test_success\n");
    dev_info(&dev->ibdev.dev, "test01_device->node_type: %d\n", dev->ibdev.node_type);
    if (ret)
    {
        dev_err(&dev->ibdev.dev, "ib_register_device: ret = %d\n", ret);
        ib_dealloc_device(&dev->ibdev);
        return ret;
    }
    // printk(KERN_WARNING "register_device_test_success_02\n");
    dev_info(&dev->ibdev.dev, "test02_device->node_type: %d\n", dev->ibdev.node_type);

    return ret;
}

static void __exit frdma_exit_module(void)
{
    printk(KERN_WARNING "frdma exit module\n");
    ib_unregister_device(&dev->ibdev);
    ib_dealloc_device(&dev->ibdev);
}

module_init(frdma_init_module);
module_exit(frdma_exit_module);