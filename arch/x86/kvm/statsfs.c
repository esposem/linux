// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * Copyright 2016 Red Hat, Inc. and/or its affiliates.
 */
#include <linux/kvm_host.h>
#include <linux/statsfs.h>
#include "lapic.h"

#define VCPU_ARCH_STATSFS(n, s, x, ...)								\
			{ n, offsetof(struct s, x), .aggr_kind = STATSFS_SUM, ##__VA_ARGS__ }

struct statsfs_value statsfs_vcpu_tsc_offset[] = {
	VCPU_ARCH_STATSFS("tsc-offset", kvm_vcpu_arch, tsc_offset,
						.type = STATSFS_S64, .mode = 0444),
	{ NULL }
};

struct statsfs_value statsfs_vcpu_arch_lapic_timer[] = {
	VCPU_ARCH_STATSFS("lapic_timer_advance_ns", kvm_timer, timer_advance_ns,
						.type = STATSFS_U64, .mode = 0444),
	{ NULL }
};

struct statsfs_value statsfs_vcpu_arch_tsc_ratio[] = {
	VCPU_ARCH_STATSFS("tsc-scaling-ratio", kvm_vcpu_arch, tsc_scaling_ratio,
						.type = STATSFS_U64, .mode = 0444),
	{ NULL }
};

struct statsfs_value statsfs_vcpu_arch_tsc_frac[] = {
	{ "tsc-scaling-ratio-frac-bits", 0, .type = STATSFS_U64, .mode = 0444 },
	{ NULL } /* base is &kvm_tsc_scaling_ratio_frac_bits */
};

void kvm_arch_create_vcpu_statsfs(struct kvm_vcpu *vcpu)
{
	statsfs_source_add_values(vcpu->statsfs_src, statsfs_vcpu_tsc_offset, &vcpu->arch);

	if (lapic_in_kernel(vcpu))
		statsfs_source_add_values(vcpu->statsfs_src, statsfs_vcpu_arch_lapic_timer, &vcpu->arch.apic->lapic_timer);

	if (kvm_has_tsc_control) {
		statsfs_source_add_values(vcpu->statsfs_src, statsfs_vcpu_arch_tsc_ratio, &vcpu->arch);
		statsfs_source_add_values(vcpu->statsfs_src, statsfs_vcpu_arch_tsc_frac,
								&kvm_tsc_scaling_ratio_frac_bits);
	}
}
