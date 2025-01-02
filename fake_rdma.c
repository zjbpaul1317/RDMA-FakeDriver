#include <linux/init.h>
#include <linux/kern_levels.h>
#include <linux/kmod.h>
#include <linux/printk.h>
#include <linux/module.h>

#include <rdma/ib_verbs.h>

#include "fake_driver.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhangjiabao");
MODULE_DESCRIPTION("A simple fake RDMA device");

static inline struct frdma_dev *to_fdev(struct ib_device *ibdev)
{
    return container_of(ibdev, struct frdma_dev, ibdev);
}

// some apis
static struct ib_pd *frdma_alloc_pd(struct ib_device *ib_dev, struct ib_udata *udata)
{
    return ib_alloc_pd(ib_dev, 0, udata);
}

static int frdma_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata)
{
    ib_dealloc_pd(pd);
    return 0;
}

static struct ib_qp *frdma_create_qp(struct ib_pd *pd, struct ib_qp_init_attr *attr,
                                     struct ib_udata *udata)
{
    struct ib_qp *qp;
    qp = ib_create_qp(pd, attr, udata);
    return qp;
}

static int frdma_destroy_qp(struct ib_qp *qp, struct ib_udata *udata)
{
    return ib_destroy_qp(qp, udata);
}

static int frdma_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr,
                           int attr_mask, struct ib_udata *udata)
{
    return ib_modify_qp(qp, attr, attr_mask, udata);
}

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
    dev_info(&ibdev->dev, "device->kverbs_provider is %s", ibdev->kverbs_provider ? "true" : "false");
    dev_info(&ibdev->dev, "device->node_type: %d\n", ibdev->node_type); // ibdev->node_type

    struct frdma_dev *dev = to_fdev(ibdev);
    struct ib_event ev;
    dev->port.attr.state = IB_PORT_ACTIVE;

    ev.device = &dev->ibdev;
    ev.element.port_num = 1;
    ev.event = IB_EVENT_PORT_ACTIVE;
    ib_dispatch_event(&ev);

    return 0;
}

static int frdma_alloc_ucontext(struct ib_ucontext *ibuc, struct ib_udata *udata)
{
    pr_err("frdma_alloc_pd called in %s at %d lines\n", __FILE__, __LINE__);
    return 0;
}

const struct ib_device_ops frdma_device_ops = {
    .owner = THIS_MODULE,
    .driver_id = RDMA_DRIVER_RXE, // must be registered in kernel, or maybe in rdma-core provider?
    .uverbs_abi_ver = 2,          // This depends on the abi of driver_id

    .query_device = frdma_query_device,
    .query_port = frdma_query_port,
    .query_pkey = frdma_query_pkey,
    .get_port_immutable = frdma_get_port_immutable,
    .alloc_ucontext = frdma_alloc_ucontext,
    .enable_driver = frdma_enable_driver,
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
    ret = request_module("ib_uverbs");
    if (ret)
    {
        pr_err("load ib_verbs module error, ret = %d\n", ret);
        return ret;
    }

    dev = ib_alloc_device(frdma_device_ops, sizeof(struct frdma_dev));
    if (IS_ERR(dev))
    {
        ret = PTR_ERR(dev);
        pr_err("ib_alloc_device failed\n");
        return ret;
    }

    frdma_attr_init(dev);
    frdma_port_init(dev);
    return ret;
}

static __exit void frdma_cleanup_module(void)
{
    ib_free_device(&dev->ibdev);
    pr_warning("frdma cleanup module\n");
}

module_init(frdma_init_module);
module_exit(frdma_cleanup_module);
