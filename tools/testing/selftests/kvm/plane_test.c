// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Red Hat, Inc.
 *
 * Test for architecture-neutral VM plane functionality
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_util.h"

#include "kvm_util.h"
#include "asm/kvm.h"
#include "linux/kvm.h"

void test_create_plane_errors(int max_planes)
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	int planefd, plane_vcpufd;

	vm = vm_create_barebones();
	vcpu = __vm_vcpu_add(vm, 0);

	planefd = __vm_ioctl(vm, KVM_CREATE_PLANE, (void *)(unsigned long)0);
	TEST_ASSERT(planefd == -1 && errno == EEXIST,
		    "Creating existing plane, expecting EEXIST. ret: %d, errno: %d",
		    planefd, errno);

	planefd = __vm_ioctl(vm, KVM_CREATE_PLANE, (void *)(unsigned long)max_planes);
	TEST_ASSERT(planefd == -1 && errno == EINVAL,
		    "Creating plane %d, expecting EINVAL. ret: %d, errno: %d",
		    max_planes, planefd, errno);

	plane_vcpufd = __vm_ioctl(vm, KVM_CREATE_VCPU_PLANE, (void *)(unsigned long)vcpu->fd);
	TEST_ASSERT(plane_vcpufd == -1 && errno == ENOTTY,
		    "Creating vCPU for plane 0, expecting ENOTTY. ret: %d, errno: %d",
		    plane_vcpufd, errno);

	kvm_vm_free(vm);
	ksft_test_result_pass("error conditions\n");
}

void test_create_plane(void)
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	struct kvm_plane *plane;
	int r;

	vm = vm_create_barebones();
	vcpu = __vm_vcpu_add(vm, 0);

	plane = vm_plane_add(vm, 1);

	r = __plane_ioctl(plane, KVM_CHECK_EXTENSION, (void *)(unsigned long)KVM_CAP_PLANES);
	TEST_ASSERT(r == 0,
		    "Checking KVM_CHECK_EXTENSION(KVM_CAP_PLANES). ret: %d", r);

	r = __plane_ioctl(plane, KVM_CHECK_EXTENSION, (void *)(unsigned long)KVM_CAP_CHECK_EXTENSION_VM);
	TEST_ASSERT(r == 1,
		    "Checking KVM_CHECK_EXTENSION(KVM_CAP_CHECK_EXTENSION_VM). ret: %d", r);

	r = __vm_ioctl(vm, KVM_CREATE_PLANE, (void *)(unsigned long)1);
	TEST_ASSERT(r == -1 && errno == EEXIST,
		    "Creating existing plane, expecting EEXIST. ret: %d, errno: %d",
		    r, errno);

	__vm_plane_vcpu_add(vcpu, plane);

	r = __plane_ioctl(plane, KVM_CREATE_VCPU_PLANE, (void *)(unsigned long)vcpu->fd);
	TEST_ASSERT(r == -1 && errno == EEXIST,
		    "Creating vCPU again for plane 1. ret: %d, errno: %d",
		    r, errno);

	r = __plane_ioctl(plane, KVM_RUN, (void *)(unsigned long)0);
	TEST_ASSERT(r == -1 && errno == ENOTTY,
		    "Running plane vCPU again for plane 1. ret: %d, errno: %d",
		    r, errno);

	kvm_vm_free(vm);
	ksft_test_result_pass("basic planefd and plane_vcpufd operation\n");
}

int main(int argc, char *argv[])
{
	int cap_planes = kvm_check_cap(KVM_CAP_PLANES);
	TEST_REQUIRE(cap_planes);

	ksft_print_header();
	ksft_set_plan(2);

	pr_info("# KVM_CAP_PLANES: %d\n", cap_planes);

	test_create_plane_errors(cap_planes);

	if (cap_planes > 1)
		test_create_plane();

	ksft_finished();
}
