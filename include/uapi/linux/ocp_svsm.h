/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace interface for /dev/ocp_svsm - SVSM observability and configuration protocol
 *
 * This header provides the IOCTL commands and data structures that enable programs
 * to interact with the SVSM OCP driver.
 *
 */

#ifndef _UAPI_OCP_SVSM_H
#define _UAPI_OCP_SVSM_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define OCP_OBJECT_DETAILS_SIZE 12
#define OCP_SOURCE_ENTRY_SIZE 128
#define OCP_MAX_BUFFER_SIZE 4096

struct ocp_svsm_list_objects {
	__u64 buf_ptr;
	__u32 first_entry;
	__u32 buf_size;
};

struct ocp_svsm_list_object_sources {
	__u64 buf_ptr;
	__u32 object_index;
	__u32 first_entry;
	__u32 buf_size;
};

struct ocp_svsm_read_source {
	__u64 buf_ptr;
	__u32 object_index;
	__u32 source_index;
	__u32 offset;
	__u32 bytes_to_read;
};

struct ocp_svsm_write_source {
	__u64 buf_ptr;
	__u32 object_index;
	__u32 source_index;
	__u32 offset;
	__u32 bytes_to_write;
};

/* The IOCTL code defined in ioctl-number.rst */
#define OCP_SVSM_IOCTL_TYPE 0xFF

/* The available IOCTL commands */
#define OCP_SVSM_IOCTL_LIST_OBJECTS \
	_IOWR(OCP_SVSM_IOCTL_TYPE, 0x01, struct ocp_svsm_list_objects)
#define OCP_SVSM_IOCTL_LIST_OBJECT_SOURCES \
	_IOWR(OCP_SVSM_IOCTL_TYPE, 0x02, struct ocp_svsm_list_object_sources)
#define OCP_SVSM_IOCTL_READ_SOURCE \
	_IOWR(OCP_SVSM_IOCTL_TYPE, 0x03, struct ocp_svsm_read_source)
#define OCP_SVSM_IOCTL_WRITE_SOURCE \
	_IOW(OCP_SVSM_IOCTL_TYPE, 0x04, struct ocp_svsm_write_source)

#endif /* _UAPI_OCP_SVSM_H */
