#
# Makefile stub for bmi_rdma.
#
# Copyright (C) 2003-6 Pete Wyckoff <pw@osc.edu>
# Copyright (C) 2017 David Reynolds <david@omnibond.com>
#
# See COPYING in top-level directory.
#

# only do any of this if configure decided to use RDMA
ifneq (,$(BUILD_RDMA))

#
# Local definitions.
#
DIR := src/io/bmi/bmi_rdma
cfiles := rdma.c util.c mem.c rdma-int.c
apis := -DRDMA

#
# Export these to the top Makefile to tell it what to build.
#
src := $(patsubst %,$(DIR)/%,$(cfiles))
LIBSRC    += $(src)
SERVERSRC += $(src)
LIBBMISRC += $(src)

#
# Each particular RDMA API needs its headers.
#
MODCFLAGS_$(DIR)/rdma-int.c := -I/usr/include 

#
# Tell the main driver about the APIs that are available.
#
MODCFLAGS_$(DIR)/ib.c := $(apis)

endif  # BUILD_RDMA
