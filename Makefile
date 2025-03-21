PWD := $(shell pwd)
KERNEL_VERSION := $(shell uname -r)
MODULE_TARGET := /lib/modules/$(KERNEL_VERSION)/kernel/drivers/infiniband/hw/frdma

DEPS_LINUX := /usr/src/linux-headers-6.5.0-28-generic

obj-m := fake_rdma.o
fake_rdma-objs := fake_rdma.o fake_rdma_ops.o

all:
	$(MAKE) -C $(DEPS_LINUX) M=$(PWD) modules
	@sudo $(DEPS_LINUX)/scripts/sign-file sha512 $(DEPS_LINUX)/certs/signing_key.x509 $(DEPS_LINUX)/certs/signing_key.pem fake_driver.ko
	@sudo mkdir -p $(MODULE_TARGET)
	@sudo cp $(PWD)/fake_driver.ko $(MODULE_TARGET)
	@sudo depmod

clean:
	$(MAKE) -C $(DEPS_LINUX) M=$(PWD) clean
	@sudo rm -rf $(MODULE_TARGET)

