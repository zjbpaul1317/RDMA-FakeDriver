#include "linux/init.h"
#include "linux/kern_levels.h"
#include "linux/kmod.h"
#include "linux/printk.h"
#include <linux/module.h>

#include <rdma/ib_verbs.h>

#include "fake_rdma.h"

MODULE_AUTHOR("zhangjiabao");
MODULE_LICENSE("Dual BSD/GPL");

struct frdma_pd {
    struct ib_pd ibpd;
    struct frdma_dev *dev;
    int pd_id;
};

static inline struct frdma_pd *to_fpd(struct ib_pd *ibpd)
{
    return container_of(ibpd, struct frdma_pd, ibpd);
}

static int frdma_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
    struct frdma_dev *dev = to_fdev(ibpd->device);
    struct frdma_pd *pd;
    int ret = 0;
    
    pd = kzalloc(sizeof(*pd), GFP_KERNEL);
    if (!pd) {
        return -ENOMEM;
    }
    
    pd->dev = dev;
    pd->pd_id = ida_simple_get(&dev->pd_ida, 0, FRDMA_MAX_PD, GFP_KERNEL);
    if (pd->pd_id < 0) {
        ret = pd->pd_id;
        goto err_free_pd;
    }
    
    dev_dbg(&dev->ibdev.dev, "Allocated PD %d\n", pd->pd_id);
    
    // 如果用户提供了足够的输出空间，将pd_id复制给用户
    if (udata && udata->outlen >= sizeof(u32)) {
        if (ib_copy_to_udata(udata, &pd->pd_id, sizeof(u32))) {
            ret = -EFAULT;
            goto err_free_id;
        }
    }
    
    ibpd->device = &dev->ibdev;
    return 0;

err_free_id:
    ida_simple_remove(&dev->pd_ida, pd->pd_id);
err_free_pd:
    kfree(pd);
    return ret;
}

static int frdma_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
    struct frdma_pd *pd = to_fpd(ibpd);
    struct frdma_dev *dev = pd->dev;

    dev_dbg(&dev->ibdev.dev, "Deallocating PD %d\n", pd->pd_id);
    ida_simple_remove(&dev->pd_ida, pd->pd_id);
    kfree(pd);
    return 0;
}

struct frdma_mr {
    struct ib_mr ibmr;
    struct frdma_pd *pd;
    u64 iova;
    u64 size;
    u32 access;
    int mr_id;
};

static inline struct frdma_mr *to_fmr(struct ib_mr *ibmr)
{
    return container_of(ibmr, struct frdma_mr, ibmr);
}

static struct ib_mr *frdma_alloc_mr(struct ib_pd *ibpd, enum ib_mr_type mr_type,
                                   u32 max_num_sg, struct ib_udata *udata)
{
    struct frdma_pd *pd = to_fpd(ibpd);
    struct frdma_dev *dev = pd->dev;
    struct frdma_mr *mr;
    int ret;

    if (mr_type != IB_MR_TYPE_MEM_REG) {
        dev_err(&dev->ibdev.dev, "Unsupported MR type %d\n", mr_type);
        return ERR_PTR(-EINVAL);
    }

    mr = kzalloc(sizeof(*mr), GFP_KERNEL);
    if (!mr)
        return ERR_PTR(-ENOMEM);

    mr->pd = pd;
    mr->mr_id = ida_simple_get(&dev->mr_ida, 0, FRDMA_MAX_MR, GFP_KERNEL);
    if (mr->mr_id < 0) {
        ret = mr->mr_id;
        goto err_free_mr;
    }
    
    mr->ibmr.lkey = mr->mr_id << 8 | 0x11; // 简单生成一个lkey
    mr->ibmr.rkey = mr->ibmr.lkey;         // rkey与lkey相同
    
    dev_dbg(&dev->ibdev.dev, "Allocated MR %d, lkey/rkey=0x%x\n", 
           mr->mr_id, mr->ibmr.lkey);
           
    return &mr->ibmr;

err_free_mr:
    kfree(mr);
    return ERR_PTR(ret);
}

static int frdma_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
    struct frdma_mr *mr = to_fmr(ibmr);
    struct frdma_dev *dev = mr->pd->dev;

    dev_dbg(&dev->ibdev.dev, "Deregistering MR %d\n", mr->mr_id);
    ida_simple_remove(&dev->mr_ida, mr->mr_id);
    kfree(mr);
    return 0;
}

static int frdma_reg_user_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
    struct ib_reg_user_mr_resp resp;
    struct frdma_mr *mr = to_fmr(ibmr);
    
    resp.lkey = mr->ibmr.lkey;
    resp.rkey = mr->ibmr.rkey;
    
    if (udata->outlen >= sizeof(resp)) {
        if (ib_copy_to_udata(udata, &resp, sizeof(resp)))
            return -EFAULT;
    }
    
    return 0;
}

struct frdma_qp
{
    struct ib_qp ibqp;
    struct frdma_pd *pd;
    enum ib_qp_state state;
    enum ib_qp_type qp_type;
    
    int qp_id;
    
    struct frdma_cq *send_cq;
    struct frdma_cq *recv_cq;
    
    u32 qp_num;
    u32 max_send_wr;
    u32 max_recv_wr;
    u32 max_send_sge;
    u32 max_recv_sge;
};

static inline struct frdma_qp *to_fqp(struct ib_qp *ibqp)
{
    return container_of(ibqp, struct frdma_qp, ibqp);
}

struct frdma_cq
{
    struct ib_cq ibcq;
    struct frdma_dev *dev;
    int cq_id;
    u32 cqe;
};

static inline struct frdma_cq *to_fcq(struct ib_cq *ibcq)
{
    return container_of(ibcq, struct frdma_cq, ibcq);
}

static int frdma_create_cq(struct ib_cq *cq, const struct ib_cq_init_attr *attr,
                          struct ib_udata *udata)
{
    struct frdma_dev *dev = to_fdev(cq->device);
    struct frdma_cq *fcq;
    int ret = 0;
    
    fcq = kzalloc(sizeof(*fcq), GFP_KERNEL);
    if (!fcq)
        return -ENOMEM;
        
    fcq->dev = dev;
    fcq->cqe = attr->cqe;
    fcq->cq_id = ida_simple_get(&dev->cq_ida, 0, FRDMA_MAX_CQ, GFP_KERNEL);
    if (fcq->cq_id < 0) {
        ret = fcq->cq_id;
        goto err_free_cq;
    }
    
    dev_dbg(&dev->ibdev.dev, "Created CQ %d with %d entries\n", 
           fcq->cq_id, attr->cqe);
    
    // 如果用户提供了足够的输出空间，将cq_id复制给用户
    if (udata && udata->outlen >= sizeof(u32)) {
        if (ib_copy_to_udata(udata, &fcq->cq_id, sizeof(u32))) {
            ret = -EFAULT;
            goto err_free_id;
        }
    }
    
    return 0;
    
err_free_id:
    ida_simple_remove(&dev->cq_ida, fcq->cq_id);
err_free_cq:
    kfree(fcq);
    return ret;
}

static int frdma_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
    struct frdma_cq *cq = to_fcq(ibcq);
    struct frdma_dev *dev = cq->dev;
    
    dev_dbg(&dev->ibdev.dev, "Destroying CQ %d\n", cq->cq_id);
    ida_simple_remove(&dev->cq_ida, cq->cq_id);
    kfree(cq);
    return 0;
}

static int frdma_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *init_attr,
                          struct ib_udata *udata)
{
    struct ib_pd *ibpd = init_attr->pd;
    struct frdma_pd *pd = to_fpd(ibpd);
    struct frdma_dev *dev = pd->dev;
    struct frdma_qp *qp;
    int ret = 0;
    
    // 检查QP类型是否支持
    if (init_attr->qp_type != IB_QPT_RC &&
        init_attr->qp_type != IB_QPT_UC &&
        init_attr->qp_type != IB_QPT_UD) {
        dev_err(&dev->ibdev.dev, "Unsupported QP type %d\n", init_attr->qp_type);
        return -EINVAL;
    }
    
    qp = kzalloc(sizeof(*qp), GFP_KERNEL);
    if (!qp)
        return -ENOMEM;
        
    qp->pd = pd;
    qp->qp_type = init_attr->qp_type;
    qp->state = IB_QPS_RESET;
    
    qp->max_send_wr = init_attr->cap.max_send_wr;
    qp->max_recv_wr = init_attr->cap.max_recv_wr;
    qp->max_send_sge = init_attr->cap.max_send_sge;
    qp->max_recv_sge = init_attr->cap.max_recv_sge;
    
    qp->qp_id = ida_simple_get(&dev->qp_ida, 0, FRDMA_MAX_QP, GFP_KERNEL);
    if (qp->qp_id < 0) {
        ret = qp->qp_id;
        goto err_free_qp;
    }
    
    qp->qp_num = qp->qp_id;
    ibqp->qp_num = qp->qp_num;
    
    dev_dbg(&dev->ibdev.dev, "Created QP %d, type %d\n", qp->qp_id, init_attr->qp_type);
    
    // 如果用户提供了足够的输出空间，将qp_num复制给用户
    if (udata && udata->outlen >= sizeof(struct ib_uverbs_create_qp_resp)) {
        struct ib_uverbs_create_qp_resp resp = {
            .qp_handle = qp->qp_id,
            .qpn = qp->qp_num,
            .max_send_wr = qp->max_send_wr,
            .max_recv_wr = qp->max_recv_wr,
            .max_send_sge = qp->max_send_sge,
            .max_recv_sge = qp->max_recv_sge,
        };
        
        if (ib_copy_to_udata(udata, &resp, sizeof(resp))) {
            ret = -EFAULT;
            goto err_free_id;
        }
    }
    
    return 0;
    
err_free_id:
    ida_simple_remove(&dev->qp_ida, qp->qp_id);
err_free_qp:
    kfree(qp);
    return ret;
}

static int frdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
                          int attr_mask, struct ib_udata *udata)
{
    struct frdma_qp *qp = to_fqp(ibqp);
    struct frdma_dev *dev = qp->pd->dev;
    enum ib_qp_state cur_state, new_state;
    
    cur_state = qp->state;
    new_state = attr_mask & IB_QP_STATE ? attr->qp_state : cur_state;
    
    dev_dbg(&dev->ibdev.dev, "Modifying QP %d state %d -> %d, attr_mask=0x%x\n", 
           qp->qp_id, cur_state, new_state, attr_mask);
    
    // 检查QP状态转换是否合法
    switch (cur_state) {
    case IB_QPS_RESET:
        if (new_state != IB_QPS_INIT)
            goto err_invalid_state;
        break;
    case IB_QPS_INIT:
        if (new_state != IB_QPS_RTR)
            goto err_invalid_state;
        break;
    case IB_QPS_RTR:
        if (new_state != IB_QPS_RTS && new_state != IB_QPS_ERR)
            goto err_invalid_state;
        break;
    case IB_QPS_RTS:
        if (new_state != IB_QPS_SQD && new_state != IB_QPS_ERR)
            goto err_invalid_state;
        break;
    case IB_QPS_SQD:
        if (new_state != IB_QPS_RTS && new_state != IB_QPS_ERR)
            goto err_invalid_state;
        break;
    case IB_QPS_ERR:
        if (new_state != IB_QPS_RESET)
            goto err_invalid_state;
        break;
    default:
        goto err_invalid_state;
    }
    
    // 更新QP状态
    qp->state = new_state;
    
    return 0;
    
err_invalid_state:
    dev_err(&dev->ibdev.dev, "Invalid QP state transition %d -> %d\n", 
           cur_state, new_state);
    return -EINVAL;
}

static int frdma_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
    struct frdma_qp *qp = to_fqp(ibqp);
    struct frdma_dev *dev = qp->pd->dev;
    
    dev_dbg(&dev->ibdev.dev, "Destroying QP %d\n", qp->qp_id);
    ida_simple_remove(&dev->qp_ida, qp->qp_id);
    kfree(qp);
    return 0;
}

static int frdma_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
                          const struct ib_send_wr **bad_wr)
{
    struct frdma_qp *qp = to_fqp(ibqp);
    struct frdma_dev *dev = qp->pd->dev;
    
    // 检查QP状态是否允许发送操作
    if (qp->state != IB_QPS_RTS) {
        dev_err(&dev->ibdev.dev, "QP %d in wrong state %d for post_send\n", 
               qp->qp_id, qp->state);
        *bad_wr = wr;
        return -EINVAL;
    }
    
    // 简单地返回成功，实际上并不处理wr
    // 在真实驱动中应该处理work request
    dev_dbg(&dev->ibdev.dev, "Post send on QP %d\n", qp->qp_id);
    return 0;
}

static int frdma_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
                          const struct ib_recv_wr **bad_wr)
{
    struct frdma_qp *qp = to_fqp(ibqp);
    struct frdma_dev *dev = qp->pd->dev;
    
    // 检查QP状态是否允许接收操作
    if (qp->state == IB_QPS_RESET) {
        dev_err(&dev->ibdev.dev, "QP %d in RESET state for post_recv\n", qp->qp_id);
        *bad_wr = wr;
        return -EINVAL;
    }
    
    // 简单地返回成功，实际上并不处理wr
    // 在真实驱动中应该处理work request
    dev_dbg(&dev->ibdev.dev, "Post recv on QP %d\n", qp->qp_id);
    return 0;
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

    // 初始化IDAs
    ida_init(&frdma_dev->pd_ida);
    ida_init(&frdma_dev->mr_ida);
    ida_init(&frdma_dev->cq_ida);
    ida_init(&frdma_dev->qp_ida);
    
    // 初始化锁
    spin_lock_init(&frdma_dev->pd_lock);
    spin_lock_init(&frdma_dev->mr_lock);
    spin_lock_init(&frdma_dev->cq_lock);
    spin_lock_init(&frdma_dev->qp_lock);

    atomic_set(&frdma_dev->num_ctx, 0);

    // Allocate the device
    dev = ib_alloc_device(frdma_dev, ibdev);
    if (!dev)
    {
        dev_err(&dev->ibdev.dev, "ib_alloc_device failed\n");
        ret = -ENOMEM;
        goto err_free_dev;
    }

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
        (1ULL << IB_USER_VERBS_CMD_REG_MR) |
        (1ULL << IB_USER_VERBS_CMD_DEREG_MR) |
        (1ULL << IB_USER_VERBS_CMD_POST_SEND) |
        (1ULL << IB_USER_VERBS_CMD_POST_RECV) |
        (1ULL << IB_USER_VERBS_CMD_CREATE_CQ) |
        (1ULL << IB_USER_VERBS_CMD_DESTROY_CQ) |
        (1ULL << IB_USER_VERBS_CMD_POLL_CQ) |
        (1ULL << IB_USER_VERBS_CMD_REQ_NOTIFY_CQ) |
        (1ULL << IB_USER_VERBS_CMD_CREATE_QP) |
        (1ULL << IB_USER_VERBS_CMD_MODIFY_QP) |
        (1ULL << IB_USER_VERBS_CMD_DESTROY_QP);

    // Initialize device attributes and port attributes
    frdma_attr_init(dev);
    frdma_port_init(dev);

    // Set the device operations
    ib_set_device_ops(&dev->ibdev, &frdma_device_ops);

    // Register the device
    ret = ib_register_device(&dev->ibdev, "frdma_%d", NULL);
    if (ret)
    {
        dev_err(&dev->ibdev.dev, "ib_register_device: ret = %d\n", ret);
        goto err_dealloc_dev;
    }
    
    dev_info(&dev->ibdev.dev, "Fake RDMA driver loaded successfully\n");
    return 0;

err_dealloc_dev:
    ib_dealloc_device(&dev->ibdev);
err_free_dev:
    kfree(frdma_dev);
    return ret;
}

const struct ib_device_ops frdma_device_ops = {
    .owner = THIS_MODULE,
    .driver_id = RDMA_DRIVER_RXE,
    .uverbs_abi_ver = 2,

    .alloc_hw_port_stats = frdma_ib_alloc_hw_port_stats,
    .alloc_mr = frdma_alloc_mr,
    .dereg_mr = frdma_dereg_mr,
    .reg_user_mr = frdma_reg_user_mr,
    .alloc_ucontext = frdma_alloc_ucontext,
    .alloc_pd = frdma_alloc_pd,
    .create_cq = frdma_create_cq,
    .destroy_cq = frdma_destroy_cq,
    .poll_cq = frdma_poll_cq,
    .create_qp = frdma_create_qp,
    .modify_qp = frdma_modify_qp,
    .destroy_qp = frdma_destroy_qp,
    .dealloc_pd = frdma_dealloc_pd,
    .query_device = frdma_query_device,
    .query_port = frdma_query_port,
    .query_pkey = frdma_query_pkey,
    .get_port_immutable = frdma_get_port_immutable,
    .dealloc_ucontext = frdma_dealloc_ucontext,
    .enable_driver = frdma_enable_driver,
    .post_send = frdma_post_send,
    .post_recv = frdma_post_recv,
    .mmap = frdma_mmap,
};
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
static int frdma_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
    const struct ib_send_wr **bad_wr)
{
struct frdma_qp *qp = to_fqp(ibqp);
struct frdma_dev *dev = qp->pd->dev;

// 检查QP状态是否允许发送操作
if (qp->state != IB_QPS_RTS) {
dev_err(&dev->ibdev.dev, "QP %d in wrong state %d for post_send\n", 
qp->qp_id, qp->state);
*bad_wr = wr;
return -EINVAL;
}

// 调用实际的处理函数
return frdma_post_send_process(qp, wr, bad_wr);
}

static int frdma_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
    const struct ib_recv_wr **bad_wr)
{
struct frdma_qp *qp = to_fqp(ibqp);
struct frdma_dev *dev = qp->pd->dev;

// 检查QP状态是否允许接收操作
if (qp->state == IB_QPS_RESET) {
dev_err(&dev->ibdev.dev, "QP %d in RESET state for post_recv\n", qp->qp_id);
*bad_wr = wr;
return -EINVAL;
}

// 调用实际的处理函数
return frdma_post_recv_process(qp, wr, bad_wr);
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

static int frdma_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
    struct frdma_cq *cq = to_fcq(ibcq);
    
    // 调用实际的处理函数
    return frdma_poll_cq_process(cq, num_entries, wc);
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