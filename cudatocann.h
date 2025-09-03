// Copyright (c) 2025 MetaX Integrated Circuits (Shanghai) Co., Ltd. All Rights Reserved.
// This software contains source code provided by NVIDIA Corporation, namely, CUDA file IO header cufile.h.

#ifdef __cplusplus
extern "C"
{
#endif

/// @cond DOXYGEN_SKIP_MACRO
#ifndef __CUFILE_H_
#define __CUFILE_H_

#include <stdlib.h>
#include <stdbool.h>

#include <cuda.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define CUFILEOP_BASE_ERR 5000

//Note :Data path errors are captured via standard error codes
#define CUFILEOP_STATUS_ENTRIES \
	CUFILE_OP(0,                      NDS_FILE_SUCCESS, nds file success) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 1,  NDS_FILE_DRIVER_NOT_INITIALIZED, nds driver is not loaded. Set allow_compat_mode to true in cufile.json file to enable compatible mode) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 2,  NDS_FILE_DRIVER_INVALID_PROPS, invalid property) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 3,  NDS_FILE_DRIVER_UNSUPPORTED_LIMIT, property range error) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 4,  NDS_FILE_DRIVER_VERSION_MISMATCH, nds driver version mismatch) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 5,  NDS_FILE_DRIVER_VERSION_READ_ERROR, nds driver version read error) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 6,  NDS_FILE_DRIVER_CLOSING, driver shutdown in progress) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 7,  NDS_FILE_PLATFORM_NOT_SUPPORTED, GPUDirect Storage not supported on current platform) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 8,  NDS_FILE_IO_NOT_SUPPORTED, GPUDirect Storage not supported on current file) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 9,  NDS_FILE_DEVICE_NOT_SUPPORTED, GPUDirect Storage not supported on current GPU) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 11, NDS_FILE_CANN_DRIVER_ERROR, CANN Driver API error) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 12, NDS_FILE_CANN_POINTER_INVALID, invalid device pointer) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 13, NDS_FILE_CANN_MEMORY_TYPE_INVALID, invalid pointer memory type) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 14, NDS_FILE_CANN_POINTER_RANGE_ERROR, pointer range exceeds allocated address range) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 15, NDS_FILE_CANN_CONTEXT_MISMATCH, CANN context mismatch) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 16, NDS_FILE_INVALID_MAPPING_SIZE, access beyond maximum pinned size) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 17, NDS_FILE_INVALID_MAPPING_RANGE, access beyond mapped size) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 18, NDS_FILE_INVALID_FILE_TYPE, unsupported file type) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 19, NDS_FILE_INVALID_FILE_OPEN_FLAG, unsupported file open flags) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 20, NDS_FILE_DIO_NOT_SET, fd direct IO not set) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 22, NDS_FILE_INVALID_VALUE, invalid arguments) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 23, NDS_FILE_MEMORY_ALREADY_REGISTERED, device pointer already registered) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 24, NDS_FILE_MEMORY_NOT_REGISTERED, device pointer lookup failure) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 25, NDS_FILE_PERMISSION_DENIED, driver or file access error) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 26, NDS_FILE_DRIVER_ALREADY_OPEN, driver is already open) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 27, NDS_FILE_HANDLE_NOT_REGISTERED, file descriptor is not registered) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 28, NDS_FILE_HANDLE_ALREADY_REGISTERED, file descriptor is already registered) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 29, NDS_FILE_DEVICE_NOT_FOUND, GPU device not found) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 30, NDS_FILE_INTERNAL_ERROR, internal error) \
	CUFILE_OP(CUFILEOP_BASE_ERR + 31, NDS_FILE_NEWFD_FAILED, failed to obtain new file descriptor)


/**
 * @brief cufileop status enum
 *
 * @note on success the error code is set to  @ref CU_FILE_SUCCESS.
 * @note The error code can be inspected using @ref IS_CUFILE_ERR and @ref CUFILE_ERRSTR.
 * @note The error code if set to @ref CU_FILE_CUDA_DRIVER_ERROR, then cuda error can be inspected using @ref IS_CUDA_ERR and @ref CU_FILE_CUDA_ERR.
 * @note Data path errors are captured via standard error codes
 */
typedef enum CUfileOpError {
        /// @cond DOXYGEN_SKIP_MACRO
	#define CUFILE_OP(code, name, string) name = code,
	CUFILEOP_STATUS_ENTRIES
	#undef CUFILE_OP
        ///@endcond
} CUfileOpError;

/// @endcond

/**
 * @brief cufileop status string
 */
static inline const char *cufileop_status_error(CUfileOpError status)
{
	switch (status) {
	/// @cond DOXYGEN_SKIP_MACRO
	#define CUFILE_OP(code, name, string) \
	case name: return #string;
	CUFILEOP_STATUS_ENTRIES
	#undef CUFILE_OP
	///@endcond
	default:return "unknown cufile error";
	}
}

/**
 * @brief  error macros to inspect error status of type @ref CUfileOpError
 */

#define IS_CUFILE_ERR(err) \
	(abs((err)) > CUFILEOP_BASE_ERR)

#endif