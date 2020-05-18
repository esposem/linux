// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * Copyright 2016 Red Hat, Inc. and/or its affiliates.
 */
#include <linux/kvm_host.h>
#include <linux/stats_fs.h>
#include "lapic.h"

#define VCPU_ARCH_STATS_FS(n, s, x, ...)					\
			{ n, offsetof(struct s, x), .aggr_kind = STATS_FS_SUM,	\
			  ##__VA_ARGS__ }

struct stats_fs_value stats_fs_vcpu_tsc_offset[] = {
	VCPU_ARCH_STATS_FS("tsc-offset", kvm_vcpu_arch, tsc_offset,
			   .type = &stats_fs_type_s64,
			   .value_flag = STATS_FS_FLOATING_VALUE),
	{ NULL }
};

struct stats_fs_value stats_fs_vcpu_arch_lapic_timer[] = {
	VCPU_ARCH_STATS_FS("lapic_timer_advance_ns", kvm_timer, timer_advance_ns,
			   .type = &stats_fs_type_u64,
			   .value_flag = STATS_FS_FLOATING_VALUE),
	{ NULL }
};

struct stats_fs_value stats_fs_vcpu_arch_tsc_ratio[] = {
	VCPU_ARCH_STATS_FS("tsc-scaling-ratio", kvm_vcpu_arch, tsc_scaling_ratio,
			   .type = &stats_fs_type_u64,
			   .value_flag = STATS_FS_FLOATING_VALUE),
	{ NULL }
};

struct stats_fs_value stats_fs_vcpu_arch_tsc_frac[] = {
	{ "tsc-scaling-ratio-frac-bits", 0, .type = &stats_fs_type_u64,
	  .value_flag = STATS_FS_FLOATING_VALUE },
	{ NULL } /* base is &kvm_tsc_scaling_ratio_frac_bits */
};

char *stats_fs_vcpu_get_mpstate(uint64_t state)
{
	char *state_str;

	state_str = kzalloc(20, GFP_KERNEL);
	if (!state_str)
		return ERR_PTR(-ENOMEM);

	switch (state) {
	case KVM_MP_STATE_RUNNABLE:
		strcpy(state_str, "RUNNABLE");
		break;
	case KVM_MP_STATE_UNINITIALIZED:
		strcpy(state_str, "UNINITIALIZED");
		break;
	case KVM_MP_STATE_INIT_RECEIVED:
		strcpy(state_str, "INIT_RECEIVED");
		break;
	case KVM_MP_STATE_HALTED:
		strcpy(state_str, "HALTED");
		break;
	case KVM_MP_STATE_SIPI_RECEIVED:
		strcpy(state_str, "SIPI_RECEIVED");
		break;
	case KVM_MP_STATE_STOPPED:
		strcpy(state_str, "STOPPED");
		break;
	case KVM_MP_STATE_CHECK_STOP:
		strcpy(state_str, "CHECK_STOP");
		break;
	case KVM_MP_STATE_OPERATING:
		strcpy(state_str, "OPERATING");
		break;
	case KVM_MP_STATE_LOAD:
		strcpy(state_str, "LOAD");
		break;
	default:
		strcpy(state_str, "UNRECOGNIZED");
		break;
	}

	return state_str;
}

struct stats_fs_value stats_fs_vcpu_mp_state[] = {
	VCPU_ARCH_STATS_FS("mp_state", kvm_vcpu_arch, mp_state,
			   .type = &stats_fs_type_u32,
			   .show = stats_fs_vcpu_get_mpstate),
	{ NULL }
};

void kvm_arch_create_vcpu_stats_fs(struct kvm_vcpu *vcpu)
{
	stats_fs_source_add_values(vcpu->stats_fs_src, stats_fs_vcpu_tsc_offset,
				   &vcpu->arch, 0);

	stats_fs_source_add_values(vcpu->stats_fs_src, stats_fs_vcpu_mp_state,
				   &vcpu->arch, 0);

	if (lapic_in_kernel(vcpu))
		stats_fs_source_add_values(vcpu->stats_fs_src,
					   stats_fs_vcpu_arch_lapic_timer,
					   &vcpu->arch.apic->lapic_timer, 0);

	if (kvm_has_tsc_control) {
		stats_fs_source_add_values(vcpu->stats_fs_src,
					   stats_fs_vcpu_arch_tsc_ratio,
					   &vcpu->arch, 0);
		stats_fs_source_add_values(vcpu->stats_fs_src,
					   stats_fs_vcpu_arch_tsc_frac,
					   &kvm_tsc_scaling_ratio_frac_bits, 0);
	}
}
