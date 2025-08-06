// Copyright (c) 2025 MetaX Integrated Circuits (Shanghai) Co., Ltd. All Rights Reserved.

#define CUfileError_t MCfileError_t

#define CUfileDescr_t MCfileDescr_t
#define CUfileHandle_t MCfileHandle_t
#define cuFileDriverOpen mcFileDriverOpen
#define CU_FILE_SUCCESS MC_FILE_SUCCESS
#define CU_FILE_HANDLE_TYPE_OPAQUE_FD MC_FILE_HANDLE_TYPE_OPAQUE_FD
#define cuFileHandleRegister mcFileHandleRegister
#define cuFileBufRegister mcFileBufRegister

#define cuFileWrite mcFileWrite
#define cuFileBufDeregister mcFileBufDeregister
#define cuFileHandleDeregister mcFileHandleDeregister

#define cuFileDriverClose mcFileDriverClose
#define cuFileRead mcFileRead

#define cuFileDriverSetMaxDirectIOSize mcFileDriverSetMaxDirectIOSize
#define cuFileDriverGetProperties mcFileDriverGetProperties
#define cuFileDriverSetMaxCacheSize mcFileDriverSetMaxCacheSize
#define cuFileDriverSetPollMode mcFileDriverSetPollMode
#define CUfileDrvProps_t MCfileDrvProps_t
#define cuFileDriverSetMaxPinnedMemSize mcFileDriverSetMaxPinnedMemSize


#define CU_FILE_PLATFORM_NOT_SUPPORTED MC_FILE_PLATFORM_NOT_SUPPORTED
#define CU_FILE_INVALID_VALUE MC_FILE_INVALID_VALUE
#define CU_FILE_CUDA_DRIVER_ERROR MC_FILE_MACA_DRIVER_ERROR
#define CU_FILE_INVALID_FILE_TYPE MC_FILE_INVALID_FILE_TYPE

#define CUFILE_ERRSTR MCFILE_ERRSTR

#define IS_CUFILE_ERR(err) \
    (abs((err)) > MCFILEOP_BASE_ERR)
