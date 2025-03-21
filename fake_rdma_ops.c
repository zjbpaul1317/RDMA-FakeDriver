#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <rdma/ib_verbs.h>

#include "fake_rdma.h"

/**
 * frdma_perform_write - 执行RDMA写操作的模拟
 * @qp: QP结构
 * @wr: 发送工作请求
 *
 * 模拟RDMA写操作，通过memcpy将数据从本地内存复制到远端内存
 * 由于是fake driver，实际只在本机内存中操作
 */
static int frdma_perform_write(struct frdma_qp *qp, const struct ib_send_wr *wr)
{
    struct frdma_dev *dev = qp->pd->dev;
    struct ib_sge *sge;
    u64 local_addr, remote_addr;
    u32 length;
    void *local_ptr, *remote_ptr;
    int i;

    // 对于每个SGE列表中的段
    for (i = 0; i < wr->num_sge; i++) {
        sge = &wr->sg_list[i];
        
        // 获取本地和远程地址
        local_addr = sge->addr;
        remote_addr = wr->wr.rdma.remote_addr + 
                      (i > 0 ? wr->sg_list[i-1].length : 0);
        length = sge->length;
        
        // 转换为内核虚拟地址
        // 注意：在真实驱动中，这里需要进行适当的地址转换和访问检查
        local_ptr = (void *)(unsigned long)local_addr;
        remote_ptr = (void *)(unsigned long)remote_addr;
        
        // 执行内存复制
        memcpy(remote_ptr, local_ptr, length);
        
        dev_dbg(&dev->ibdev.dev, "RDMA Write: local=0x%llx, remote=0x%llx, len=%u\n",
               local_addr, remote_addr, length);
    }
    
    return 0;
}

/**
 * frdma_perform_read - 执行RDMA读操作的模拟
 * @qp: QP结构
 * @wr: 发送工作请求
 *
 * 模拟RDMA读操作，通过memcpy将数据从远端内存复制到本地内存
 */
static int frdma_perform_read(struct frdma_qp *qp, const struct ib_send_wr *wr)
{
    struct frdma_dev *dev = qp->pd->dev;
    struct ib_sge *sge;
    u64 local_addr, remote_addr;
    u32 length;
    void *local_ptr, *remote_ptr;
    int i;

    // 对于每个SGE列表中的段
    for (i = 0; i < wr->num_sge; i++) {
        sge = &wr->sg_list[i];
        
        // 获取本地和远程地址
        local_addr = sge->addr;
        remote_addr = wr->wr.rdma.remote_addr + 
                      (i > 0 ? wr->sg_list[i-1].length : 0);
        length = sge->length;
        
        // 转换为内核虚拟地址
        local_ptr = (void *)(unsigned long)local_addr;
        remote_ptr = (void *)(unsigned long)remote_addr;
        
        // 执行内存复制（远程到本地）
        memcpy(local_ptr, remote_ptr, length);
        
        dev_dbg(&dev->ibdev.dev, "RDMA Read: remote=0x%llx, local=0x%llx, len=%u\n",
               remote_addr, local_addr, length);
    }
    
    return 0;
}

/**
 * frdma_post_send_process - 处理发送请求
 * @qp: QP结构
 * @wr: 发送工作请求
 * @bad_wr: 出错时设置为导致错误的工作请求
 *
 * 根据不同的操作类型进行相应处理
 */
int frdma_post_send_process(struct frdma_qp *qp, const struct ib_send_wr *wr,
                           const struct ib_send_wr **bad_wr)
{
    struct frdma_dev *dev = qp->pd->dev;
    int ret = 0;
    
    while (wr) {
        switch (wr->opcode) {
        case IB_WR_RDMA_WRITE:
            dev_dbg(&dev->ibdev.dev, "Processing RDMA_WRITE request\n");
            ret = frdma_perform_write(qp, wr);
            break;
            
        case IB_WR_RDMA_READ:
            dev_dbg(&dev->ibdev.dev, "Processing RDMA_READ request\n");
            ret = frdma_perform_read(qp, wr);
            break;
            
        case IB_WR_SEND:
            // 简单模拟Send操作，不做实际数据传输
            dev_dbg(&dev->ibdev.dev, "Processing SEND request (simulated)\n");
            ret = 0;
            break;
            
        default:
            dev_warn(&dev->ibdev.dev, "Unsupported opcode %d\n", wr->opcode);
            ret = -EINVAL;
            break;
        }
        
        if (ret) {
            *bad_wr = wr;
            return ret;
        }
        
        // 处理下一个工作请求
        wr = wr->next;
    }
    
    return 0;
}

/**
 * frdma_post_recv_process - 处理接收请求
 * @qp: QP结构
 * @wr: 接收工作请求
 * @bad_wr: 出错时设置为导致错误的工作请求
 *
 * 由于是fake driver，我们只是简单地记录接收请求，不做实际操作
 */
int frdma_post_recv_process(struct frdma_qp *qp, const struct ib_recv_wr *wr,
                           const struct ib_recv_wr **bad_wr)
{
    struct frdma_dev *dev = qp->pd->dev;
    
    while (wr) {
        dev_dbg(&dev->ibdev.dev, "Processing RECV request (simulated)\n");
        
        // 处理下一个工作请求
        wr = wr->next;
    }
    
    return 0;
}

/**
 * frdma_poll_cq_process - 模拟CQ轮询处理
 * @cq: 完成队列
 * @num_entries: 要获取的最大完成条目数
 * @wc: 完成条目数组
 *
 * 返回模拟的完成条目数
 */
int frdma_poll_cq_process(struct frdma_cq *cq, int num_entries, struct ib_wc *wc)
{
    // 因为是fake driver，我们假装没有完成的工作请求
    return 0;
}

// 导出符号供主驱动文件使用
EXPORT_SYMBOL(frdma_post_send_process);
EXPORT_SYMBOL(frdma_post_recv_process);
EXPORT_SYMBOL(frdma_poll_cq_process);