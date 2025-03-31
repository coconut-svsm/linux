// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Red Hat, Inc.
 *
 * Test for x86-specific VM plane functionality
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_util.h"

#include "kvm_util.h"
#include "processor.h"
#include "apic.h"
#include "asm/kvm.h"
#include "linux/kvm.h"

static void test_plane_regs(void)
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	struct kvm_plane *plane;
	struct kvm_plane_vcpu *plane_vcpu;

	struct kvm_regs regs0, regs1;

	vm = vm_create_barebones();
	vcpu = __vm_vcpu_add(vm, 0);
	plane = vm_plane_add(vm, 1);
	plane_vcpu = __vm_plane_vcpu_add(vcpu, plane);

	vcpu_ioctl(vcpu, KVM_GET_REGS, &regs0);
	plane_vcpu_ioctl(plane_vcpu, KVM_GET_REGS, &regs1);
	regs0.rax = 0x12345678;
	regs1.rax = 0x87654321;

	vcpu_ioctl(vcpu, KVM_SET_REGS, &regs0);
	plane_vcpu_ioctl(plane_vcpu, KVM_SET_REGS, &regs1);

	vcpu_ioctl(vcpu, KVM_GET_REGS, &regs0);
	plane_vcpu_ioctl(plane_vcpu, KVM_GET_REGS, &regs1);
	TEST_ASSERT_EQ(regs0.rax, 0x12345678);
	TEST_ASSERT_EQ(regs1.rax, 0x87654321);

	kvm_vm_free(vm);
	ksft_test_result_pass("get/set regs for planes\n");
}

/* Offset of XMM0 in the legacy XSAVE area.  */
#define XSTATE_BV_OFFSET	(0x200/4)
#define XMM_OFFSET		(0xa0/4)
#define PKRU_OFFSET		(0xa80/4)

static void test_plane_fpu_nonshared(void)
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	struct kvm_plane *plane;
	struct kvm_plane_vcpu *plane_vcpu;

	struct kvm_xsave xsave0, xsave1;

	vm = vm_create_barebones();
	TEST_ASSERT_EQ(vm_check_cap(vm, KVM_CAP_PLANES_FPU), false);

	vcpu = __vm_vcpu_add(vm, 0);
	vcpu_init_cpuid(vcpu, kvm_get_supported_cpuid());
	vcpu_set_cpuid(vcpu);

	plane = vm_plane_add(vm, 1);
	plane_vcpu = __vm_plane_vcpu_add(vcpu, plane);

	vcpu_ioctl(vcpu, KVM_GET_XSAVE, &xsave0);
	xsave0.region[XSTATE_BV_OFFSET] |= XFEATURE_MASK_FP | XFEATURE_MASK_SSE;
	xsave0.region[XMM_OFFSET] = 0x12345678;
	vcpu_ioctl(vcpu, KVM_SET_XSAVE, &xsave0);

	plane_vcpu_ioctl(plane_vcpu, KVM_GET_XSAVE, &xsave1);
	xsave1.region[XSTATE_BV_OFFSET] |= XFEATURE_MASK_FP | XFEATURE_MASK_SSE;
	xsave1.region[XMM_OFFSET] = 0x87654321;
	plane_vcpu_ioctl(plane_vcpu, KVM_SET_XSAVE, &xsave1);

	memset(&xsave0, 0, sizeof(xsave0));
	vcpu_ioctl(vcpu, KVM_GET_XSAVE, &xsave0);
	TEST_ASSERT_EQ(xsave0.region[XMM_OFFSET], 0x12345678);

	memset(&xsave1, 0, sizeof(xsave0));
	plane_vcpu_ioctl(plane_vcpu, KVM_GET_XSAVE, &xsave1);
	TEST_ASSERT_EQ(xsave1.region[XMM_OFFSET], 0x87654321);

	ksft_test_result_pass("get/set FPU not shared across planes\n");
}

static void test_plane_fpu_shared(void)
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	struct kvm_plane *plane;
	struct kvm_plane_vcpu *plane_vcpu;

	struct kvm_xsave xsave0, xsave1;

	vm = vm_create_barebones();
	vm_enable_cap(vm, KVM_CAP_PLANES_FPU, 1ul);
	TEST_ASSERT_EQ(vm_check_cap(vm, KVM_CAP_PLANES_FPU), true);

	vcpu = __vm_vcpu_add(vm, 0);
	vcpu_init_cpuid(vcpu, kvm_get_supported_cpuid());
	vcpu_set_cpuid(vcpu);

	plane = vm_plane_add(vm, 1);
	plane_vcpu = __vm_plane_vcpu_add(vcpu, plane);

	vcpu_ioctl(vcpu, KVM_GET_XSAVE, &xsave0);

	xsave0.region[XSTATE_BV_OFFSET] |= XFEATURE_MASK_FP | XFEATURE_MASK_SSE;
	xsave0.region[XMM_OFFSET] = 0x12345678;
	vcpu_ioctl(vcpu, KVM_SET_XSAVE, &xsave0);
	plane_vcpu_ioctl(plane_vcpu, KVM_GET_XSAVE, &xsave1);
	TEST_ASSERT_EQ(xsave1.region[XMM_OFFSET], 0x12345678);

	xsave1.region[XSTATE_BV_OFFSET] |= XFEATURE_MASK_FP | XFEATURE_MASK_SSE;
	xsave1.region[XMM_OFFSET] = 0x87654321;
	plane_vcpu_ioctl(plane_vcpu, KVM_SET_XSAVE, &xsave1);
	vcpu_ioctl(vcpu, KVM_GET_XSAVE, &xsave0);
	TEST_ASSERT_EQ(xsave0.region[XMM_OFFSET], 0x87654321);

	ksft_test_result_pass("get/set FPU shared across planes\n");

	if (!this_cpu_has(X86_FEATURE_PKU)) {
		ksft_test_result_skip("get/set PKRU with shared FPU\n");
		goto exit;
	}

	xsave0.region[XSTATE_BV_OFFSET] = XFEATURE_MASK_PKRU;
	xsave0.region[PKRU_OFFSET] = 0xffffffff;
	vcpu_ioctl(vcpu, KVM_SET_XSAVE, &xsave0);
	plane_vcpu_ioctl(plane_vcpu, KVM_GET_XSAVE, &xsave0);

	xsave0.region[XSTATE_BV_OFFSET] = XFEATURE_MASK_PKRU;
	xsave0.region[PKRU_OFFSET] = 0xaaaaaaaa;
	vcpu_ioctl(vcpu, KVM_SET_XSAVE, &xsave0);
	plane_vcpu_ioctl(plane_vcpu, KVM_GET_XSAVE, &xsave1);
	assert(xsave1.region[PKRU_OFFSET] == 0xffffffff);

	xsave1.region[XSTATE_BV_OFFSET] = XFEATURE_MASK_PKRU;
	xsave1.region[PKRU_OFFSET] = 0x55555555;
	plane_vcpu_ioctl(plane_vcpu, KVM_SET_XSAVE, &xsave1);
	vcpu_ioctl(vcpu, KVM_GET_XSAVE, &xsave0);
	assert(xsave0.region[PKRU_OFFSET] == 0xaaaaaaaa);

	ksft_test_result_pass("get/set PKRU with shared FPU\n");

exit:
	kvm_vm_free(vm);
}

#define APIC_SPIV		0xF0
#define APIC_IRR		0x200

#define MYVEC			192

#define MAKE_MSI(cpu, vector) ((struct kvm_msi){		\
	.address_lo = APIC_DEFAULT_GPA + (((cpu) & 0xff) << 8),	\
	.address_hi = (cpu) & ~0xff,				\
	.data = (vector),					\
})

static bool has_irr(struct kvm_lapic_state *apic, int vector)
{
	int word = vector >> 5;
	int bit_in_word = vector & 31;
	int bit = (APIC_IRR + word * 16) * CHAR_BIT + (bit_in_word & 31);

	return apic->regs[bit >> 3] & (1 << (bit & 7));
}

static void do_enable_lapic(struct kvm_lapic_state *apic)
{
	/* set bit 8 */
	apic->regs[APIC_SPIV + 1] |= 1;
}

static void test_plane_msi(void)
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	struct kvm_plane *plane;
	struct kvm_plane_vcpu *plane_vcpu;
	int r;

	struct kvm_msi msi = MAKE_MSI(0, MYVEC);
	struct kvm_lapic_state lapic0, lapic1;

	vm = __vm_create(VM_SHAPE_DEFAULT, 1, 0);

	vcpu = __vm_vcpu_add(vm, 0);
	vcpu_init_cpuid(vcpu, kvm_get_supported_cpuid());
	vcpu_set_cpuid(vcpu);

	plane = vm_plane_add(vm, 1);
	plane_vcpu = __vm_plane_vcpu_add(vcpu, plane);

	vcpu_set_msr(vcpu, MSR_IA32_APICBASE,
		     APIC_DEFAULT_GPA | MSR_IA32_APICBASE_ENABLE | X2APIC_ENABLE);
	vcpu_ioctl(vcpu, KVM_GET_LAPIC, &lapic0);
	do_enable_lapic(&lapic0);
	vcpu_ioctl(vcpu, KVM_SET_LAPIC, &lapic0);

	_plane_vcpu_set_msr(plane_vcpu, MSR_IA32_APICBASE,
			    APIC_DEFAULT_GPA | MSR_IA32_APICBASE_ENABLE | X2APIC_ENABLE);
	plane_vcpu_ioctl(plane_vcpu, KVM_GET_LAPIC, &lapic1);
	do_enable_lapic(&lapic1);
	plane_vcpu_ioctl(plane_vcpu, KVM_SET_LAPIC, &lapic1);

	r = __plane_ioctl(plane, KVM_SIGNAL_MSI, &msi);
	TEST_ASSERT(r == 1,
		   "Delivering interrupt to plane 1. ret: %d, errno: %d", r, errno);

	vcpu_ioctl(vcpu, KVM_GET_LAPIC, &lapic0);
	TEST_ASSERT(!has_irr(&lapic0, MYVEC), "Vector clear in plane 0");
	plane_vcpu_ioctl(plane_vcpu, KVM_GET_LAPIC, &lapic1);
	TEST_ASSERT(has_irr(&lapic1, MYVEC), "Vector set in plane 1");

	/* req_exit_planes always has priority */
	vcpu->run->req_exit_planes = (1 << 1);
	vcpu_run(vcpu);
	TEST_ASSERT_EQ(vcpu->run->exit_reason, KVM_EXIT_PLANE_EVENT);
	TEST_ASSERT_EQ(vcpu->run->plane_event.cause, KVM_PLANE_EVENT_INTERRUPT);
	TEST_ASSERT_EQ(vcpu->run->plane_event.pending_event_planes, (1 << 1));
	TEST_ASSERT_EQ(vcpu->run->plane_event.target, (1 << 1));

	r = __vm_ioctl(vm, KVM_SIGNAL_MSI, &msi);
	TEST_ASSERT(r == 1,
		   "Delivering interrupt to plane 0. ret: %d, errno: %d", r, errno);
	vcpu_ioctl(vcpu, KVM_GET_LAPIC, &lapic0);
	TEST_ASSERT(has_irr(&lapic0, MYVEC), "Vector set in plane 0");

	/* req_exit_planes ignores current plane; current plane is cleared */
	vcpu->run->plane = 1;
	vcpu->run->req_exit_planes = (1 << 0) | (1 << 1);
	vcpu_run(vcpu);
	TEST_ASSERT_EQ(vcpu->run->exit_reason, KVM_EXIT_PLANE_EVENT);
	TEST_ASSERT_EQ(vcpu->run->plane_event.cause, KVM_PLANE_EVENT_INTERRUPT);
	TEST_ASSERT_EQ(vcpu->run->plane_event.pending_event_planes, (1 << 0));
	TEST_ASSERT_EQ(vcpu->run->plane_event.target, (1 << 0));

	kvm_vm_free(vm);
	ksft_test_result_pass("signal MSI for planes\n");
}

int main(int argc, char *argv[])
{
	int cap_planes = kvm_check_cap(KVM_CAP_PLANES);
	TEST_REQUIRE(cap_planes && cap_planes > 1);

	ksft_print_header();
	ksft_set_plan(5);

	pr_info("# KVM_CAP_PLANES: %d\n", cap_planes);

	test_plane_regs();
	test_plane_fpu_nonshared();
	test_plane_fpu_shared();
	test_plane_msi();

	ksft_finished();
}
