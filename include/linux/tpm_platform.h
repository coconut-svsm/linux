/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 James.Bottomley@HansenPartnership.com
 *
 * Interface specification for platforms wishing to activate the
 * platform tpm device.  The device must be a platform device created
 * with the name "tpm" and it must populate platform_data with struct
 * tpm_platform_ops
 */

/*
 * The current MSSIM TPM commands we support.  The complete list is
 * in the TcpTpmProtocol header:
 *
 * https://github.com/microsoft/ms-tpm-20-ref/blob/main/TPMCmd/Simulator/include/TpmTcpProtocol.h
 */

#define TPM_SEND_COMMAND		8
#define TPM_SIGNAL_CANCEL_ON		9
#define TPM_SIGNAL_CANCEL_OFF		10
/*
 * Any platform specific commands should be placed here and should start
 * at 0x8000 to avoid clashes with the MSSIM protocol.  They should follow
 * the same self describing buffer format below
 */

#define TPM_PLATFORM_MAX_BUFFER		4096 /* max req/resp buffer size */

/**
 * struct tpm_platform_ops - the share platform operations
 *
 * @sendrecv:	Send a TPM command using the MSSIM protocol.
 *
 * The MSSIM protocol is designed for a network, so the buffers are
 * self describing.  The minimum buffer size is sizeof(u32).  Every
 * MSSIM command defines its own transport buffer and the command is
 * sent in the first u32 array.  The only modification we make is that
 * the MSSIM uses network order and we use the endianness of the
 * architecture.  The response to every command (in the same buffer)
 * is a u32 size preceded array.  Most of the MSSIM commands simply
 * return zero here because they have no defined response.
 *
 * The only command with a defined request/response size is TPM_SEND_COMMAND
 * The definition is in the structures below
 */
struct tpm_platform_ops {
	int (*sendrcv)(u8 *buffer);
};

/**
 * struct tpm_send_cmd_req - Structure for a TPM_SEND_COMMAND
 *
 * @cmd:	The command (must be TPM_SEND_COMMAND)
 * @locality:	The locality
 * @inbuf_size:	The size of the input buffer following
 * @inbuf:	A buffer of size inbuf_size
 *
 * Note that @inbuf_size must be large enough to hold the response so
 * represents the maximum buffer size, not the size of the specific
 * TPM command.
 */
struct tpm_send_cmd_req {
	u32	cmd;
	u8	locality;
	u32	inbuf_size;
	u8	inbuf[];
} __packed;

/**
 * struct tpm_req - generic request header for single word command
 *
 * @cmd:	The command to send
 */
struct tpm_req {
	u32	cmd;
} __packed;

/**
 * struct tpm_resp - generic response header
 *
 * @size:	The response size (zero if nothing follows)
 *
 * Note: most MSSIM commands simply return zero here with no indication
 * of success or failure.
 */

struct tpm_resp {
	s32	size;
} __packed;

