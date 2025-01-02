#ifndef __FAKE_DRIVER_H__
#define __FAKE_DRIVER_H__

#include "rdma/ib_mad.h"
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <rdma/ib_verbs.h>

#define FRDMA_MODULE_NAME "frdma"
#define FRDMA_NODE_DESC "Fake RDMA stack"

#define DEFAULT_MAX_VALUE (1 << 20)

enum rxe_device_param
{
    FRDMA_MAX_MR_SIZE = -1ull,
    FRDMA_PAGE_SIZE_CAP = 0xfffff000,
    FRDMA_MAX_QP_WR = DEFAULT_MAX_VALUE,
};

enum frdma_port_param
{
    FRDMA_PORT_GID_TBL_LEN = 1024,
    FRDMA_PORT_PORT_CAP_FLAGS = IB_PORT_CM_SUP,
    FRDMA_PORT_MAX_MSG_SZ = 0x800000,
    FRDMA_PORT_BAD_PKEY_CNTR = 0,
    FRDMA_PORT_QKEY_VIOL_CNTR = 0,
    FRDMA_PORT_LID = 0,
    FRDMA_PORT_SM_LID = 0,
    FRDMA_PORT_SM_SL = 0,
    FRDMA_PORT_LMC = 0,
    FRDMA_PORT_MAX_VL_NUM = 1,
    FRDMA_PORT_SUBNET_TIMEOUT = 0,
    FRDMA_PORT_INIT_TYPE_REPLY = 0,
    FRDMA_PORT_ACTIVE_WIDTH = IB_WIDTH_1X,
    FRDMA_PORT_ACTIVE_SPEED = 1,
    FRDMA_PORT_PKEY_TBL_LEN = 1,
    FRDMA_PORT_PHYS_STATE = IB_PORT_PHYS_STATE_POLLING,
    FRDMA_PORT_SUBNET_PREFIX = 0xfe80000000000000ULL,
};

struct frdma_port
{
    struct ib_port_attr attr;
};

struct frdma_devattr
{
    unsigned char peer_addr[ETH_ALEN];
    int numa_node;

    u64 fw_ver;
    __be64 sys_image_guid;
    u64 max_mr_size;
    u64 page_size_cap;
    u32 vendor_id;
    u32 vendor_part_id;
    u32 hw_ver;
    int max_qp;
    int max_qp_wr;
    u64 device_cap_flags;
    u64 kernel_cap_flags;
    int max_send_sge;
    int max_recv_sge;
    int max_sge_rd;
    int max_cq;
    int max_cqe;
    int max_mr;
    int max_pd;
    int max_qp_rd_atom;
    int max_ee_rd_atom;
    int max_res_rd_atom;
    int max_qp_init_rd_atom;
    int max_ee_init_rd_atom;
    enum ib_atomic_cap atomic_cap;
    enum ib_atomic_cap masked_atomic_cap;
    int max_ee;
    int max_rdd;
    int max_mw;
    int max_raw_ipv6_qp;
    int max_raw_ethy_qp;
    int max_mcast_grp;
    int max_mcast_qp_attach;
    int max_total_mcast_qp_attach;
    int max_ah;
    int max_srq;
    int max_srq_wr;
    int max_srq_sge;
    unsigned int max_fast_reg_page_list_len;
    unsigned int max_pi_fast_reg_page_list_len;
    u16 max_pkeys;
    u8 local_ca_ack_delay;
    int sig_prot_cap;
    int sig_guard_cap;
    struct ib_odp_caps odp_caps;
    uint64_t timestamp_mask;
    uint64_t hca_core_clock; /* in KHZ */
    struct ib_rss_caps rss_caps;
    u32 max_wq_type_rq;
    u32 raw_packet_caps; /* Use ib_raw_packet_caps enum */
    struct ib_tm_caps tm_caps;
    struct ib_cq_caps cq_caps;
    u64 max_dm_size;
    /* Max entries for sgl for optimized performance per READ */
    u32 max_sgl_rd;
};

struct frdma_dev
{
    struct ib_device ibdev;
    struct net_device *netdev;

    // struct frdma_devattr attrs;
    struct ib_device_attr attrs;
    struct frdma_port port;

    struct list_head cep_list;
};

#endif //! __FAKE_DRIVER_H__
