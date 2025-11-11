# SPDX-License-Identifier: GPL-2.0+
#
# (C) Copyright 2015 Mateusz Kulikowski <mateusz.kulikowski@gmail.com>

obj-y += board.o
obj-$(CONFIG_EFI_HAVE_CAPSULE_SUPPORT) += capsule_update.o
obj-$(CONFIG_OF_LIVE_SA8775P) += qcom_sa8775p_fixup.o
obj-$(CONFIG_OF_LIVE_QCS615) += qcom_qcs615_fixup.o
obj-$(CONFIG_OF_LIVE) += qcom_fixup_boardinfo.o
obj-$(CONFIG_OF_LIVE) += qcom_fixup_ddrinfo.o
obj-$(CONFIG_OF_LIVE) += qcom_fixup_hypervisor.o
obj-$(CONFIG_OF_LIVE) += qcom_fixup_subsetparts.o
obj-$(CONFIG_OF_LIVE) += of_fixup.o
