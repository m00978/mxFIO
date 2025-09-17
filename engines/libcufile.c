/*
 * Copyright (c)2020 System Fabric Works, Inc. All Rights Reserved.
 * mailto:info@systemfabricworks.com
 *
 * License: GPLv2, see COPYING.
 *
 * libcufile engine
 *
 * fio I/O engine using the NVIDIA cuFile API.
 *
 * 2025 - Modified by MetaX Integrated Circuits (Shanghai) Co., Ltd. All Rights Reserved.
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#ifndef CONFIG_NDS
#include "cudatomaca.h"
#include <mcfile.h>
#else
#include "cudatocann.h"
#include "nds_file.h"
#include <stdio.h>
#endif
#include <cuda.h>
#include <cuda_runtime.h>
#include <pthread.h>

#include "../fio.h"
#include "../lib/pow2.h"
#include "../optgroup.h"
#include "../lib/memalign.h"

#define ALIGNED_4KB(v) (((v) & 0x0fff) == 0)

#define LOGGED_BUFLEN_NOT_ALIGNED     0x01
#define LOGGED_GPU_OFFSET_NOT_ALIGNED 0x02
#define GPU_ID_SEP ":"

enum {
	IO_CUFILE    = 1,
	IO_POSIX     = 2
};

struct libcufile_options {
	struct thread_data *td;
	char               *gpu_ids;       /* colon-separated list of GPU ids,
					      one per job */
	void               *cu_mem_ptr;    /* GPU memory */
	void               *junk_buf;      /* buffer to simulate cudaMemcpy with
					      posix I/O write */
	int                 my_gpu_id;     /* GPU id to use for this job */
	unsigned int        cuda_io;       /* Type of I/O to use with CUDA */
	size_t              total_mem;     /* size for cu_mem_ptr and junk_buf */
	int                 logged;        /* bitmask of log messages that have
					      been output, prevent flood */
};
#ifndef CONFIG_NDS
struct fio_libcufile_data {
	CUfileDescr_t  cf_descr;
	CUfileHandle_t cf_handle;
};
#endif
static struct fio_option options[] = {
	{
		.name	  = "gpu_dev_ids",
		.lname	  = "libcufile engine gpu dev ids",
		.type	  = FIO_OPT_STR_STORE,
		.off1	  = offsetof(struct libcufile_options, gpu_ids),
		.help	  = "GPU IDs, one per subjob, separated by " GPU_ID_SEP,
		.category = FIO_OPT_C_ENGINE,
		.group	  = FIO_OPT_G_LIBCUFILE,
	},
	{
		.name	  = "cuda_io",
		.lname	  = "libcufile cuda io",
		.type	  = FIO_OPT_STR,
		.off1	  = offsetof(struct libcufile_options, cuda_io),
		.help	  = "Type of I/O to use with CUDA",
		.def      = "cufile",
		.posval   = {
			    { .ival = "cufile",
			      .oval = IO_CUFILE,
			      .help = "libcufile nvidia-fs"
			    },
			    { .ival = "posix",
			      .oval = IO_POSIX,
			      .help = "POSIX I/O"
			    }
		},
		.category = FIO_OPT_C_ENGINE,
		.group	  = FIO_OPT_G_LIBCUFILE,
	},
	{
		.name	 = NULL,
	},
};

static int running = 0;
static int cufile_initialized = 0;
static pthread_mutex_t running_lock = PTHREAD_MUTEX_INITIALIZER;

#define check_cudaruntimecall(fn, rc)                                               \
	do {                                                                        \
		cudaError_t res = fn;                                               \
		if (res != cudaSuccess) {                                           \
			const char *str = cudaGetErrorName(res);                    \
			log_err("cuda runtime api call failed %s:%d : err=%d:%s\n", \
				#fn, __LINE__, res, str);                           \
			rc = -1;                                                    \
		} else                                                              \
			rc = 0;                                                     \
	} while(0)

#ifndef CONFIG_NDS
static const char *fio_libcufile_get_cuda_error(CUfileError_t st)
{
	if (IS_CUFILE_ERR(st.err))
		return mcfileop_status_error(st.err);
	return "unknown";
}
#else
static const char *fio_libcufile_get_cuda_error(CUfileOpError err)
{
	if (IS_CUFILE_ERR(err))
		return cufileop_status_error(err);
	return "unknown";
}
#endif

/*
 * Assign GPU to subjob roundrobin, similar to how multiple
 * entries in 'directory' are handled by fio.
 */
static int fio_libcufile_find_gpu_id(struct thread_data *td)
{
	struct libcufile_options *o = td->eo;
	int gpu_id = 0;

	if (o->gpu_ids != NULL) {
		char *gpu_ids, *pos, *cur;
		int i, id_count, gpu_idx;

		for (id_count = 0, cur = o->gpu_ids; cur != NULL; id_count++) {
			cur = strchr(cur, GPU_ID_SEP[0]);
			if (cur != NULL)
				cur++;
		}

		gpu_idx = td->subjob_number % id_count;

		pos = gpu_ids = strdup(o->gpu_ids);
		if (gpu_ids == NULL) {
			log_err("strdup(gpu_ids): err=%d\n", errno);
			return -1;
		}

		i = 0;
		while (pos != NULL && i <= gpu_idx) {
			i++;
			cur = strsep(&pos, GPU_ID_SEP);
		}

		if (cur)
			gpu_id = atoi(cur);

		free(gpu_ids);
	}

	return gpu_id;
}

static int fio_libcufile_init(struct thread_data *td)
{
	struct libcufile_options *o = td->eo;
#ifndef CONFIG_NDS
	CUfileError_t status;
#else
	int status;
#endif
	int initialized;
	int rc;

#ifndef CONFIG_NDS
	pthread_mutex_lock(&running_lock);
	if (running == 0) {
		assert(cufile_initialized == 0);
		if (o->cuda_io == IO_CUFILE) {
			/* only open the driver if this is the first worker thread */
			status = cuFileDriverOpen();
			if (status.err != CU_FILE_SUCCESS)
				log_err("cuFileDriverOpen: err=%d:%s\n", status.err,
					fio_libcufile_get_cuda_error(status));
			else
				cufile_initialized = 1;
		}
	}
	running++;
	initialized = cufile_initialized;
	pthread_mutex_unlock(&running_lock);
#else // Currently, Huawei NDS file system supports multi-process, but not multi-thread read/write.
	o->my_gpu_id = fio_libcufile_find_gpu_id(td);
	if (o->my_gpu_id < 0)
		return 1;
	status = nds_driver_open(o->my_gpu_id);
	if (status != NDS_FILE_SUCCESS)
		log_err("nds_driver_open: err=%d:%s\n", status,
			fio_libcufile_get_cuda_error(status));
	else
		cufile_initialized = 1;
#endif

	if (o->cuda_io == IO_CUFILE && !initialized)
		return 1;
#ifndef CONFIG_NDS
	o->my_gpu_id = fio_libcufile_find_gpu_id(td);
	if (o->my_gpu_id < 0)
		return 1;
#endif
	dprint(FD_MEM, "Subjob %d uses GPU %d\n", td->subjob_number, o->my_gpu_id);
	check_cudaruntimecall(cudaSetDevice(o->my_gpu_id), rc);
	if (rc != 0)
		return 1;

	return 0;
}

static inline int fio_libcufile_pre_write(struct thread_data *td,
					  struct libcufile_options *o,
					  struct io_u *io_u,
					  size_t gpu_offset)
{
	int rc = 0;

	if (o->cuda_io == IO_CUFILE) {
		if (td->o.verify) {
			/*
			  Data is being verified, copy the io_u buffer to GPU memory.
			  This isn't done in the non-verify case because the data would
			  already be in GPU memory in a normal cuFile application.
			*/
			check_cudaruntimecall(cudaMemcpy(((char*) o->cu_mem_ptr) + gpu_offset,
							 io_u->xfer_buf,
							 io_u->xfer_buflen,
							 cudaMemcpyHostToDevice), rc);
			if (rc != 0) {
				log_err("DDIR_WRITE cudaMemcpy H2D failed\n");
				io_u->error = EIO;
			}
		}
	} else if (o->cuda_io == IO_POSIX) {

		/*
		  POSIX I/O is being used, the data has to be copied out of the
		  GPU into a CPU buffer. GPU memory doesn't contain the actual
		  data to write, copy the data to the junk buffer. The purpose
		  of this is to add the overhead of cudaMemcpy() that would be
		  present in a POSIX I/O CUDA application.
		*/
		check_cudaruntimecall(cudaMemcpy(o->junk_buf + gpu_offset,
						 ((char*) o->cu_mem_ptr) + gpu_offset,
						 io_u->xfer_buflen,
						 cudaMemcpyDeviceToHost), rc);
		if (rc != 0) {
			log_err("DDIR_WRITE cudaMemcpy D2H failed\n");
			io_u->error = EIO;
		}
	} else {
		log_err("Illegal CUDA IO type: %d\n", o->cuda_io);
		assert(0);
		rc = EINVAL;
	}

	return rc;
}

static inline int fio_libcufile_post_read(struct thread_data *td,
					  struct libcufile_options *o,
					  struct io_u *io_u,
					  size_t gpu_offset)
{
	int rc = 0;

	if (o->cuda_io == IO_CUFILE) {
		if (td->o.verify) {
			/* Copy GPU memory to CPU buffer for verify */
			check_cudaruntimecall(cudaMemcpy(io_u->xfer_buf,
							 ((char*) o->cu_mem_ptr) + gpu_offset,
							 io_u->xfer_buflen,
							 cudaMemcpyDeviceToHost), rc);
			if (rc != 0) {
				log_err("DDIR_READ cudaMemcpy D2H failed\n");
				io_u->error = EIO;
			}
		}
	} else if (o->cuda_io == IO_POSIX) {
		/* POSIX I/O read, copy the CPU buffer to GPU memory */
		check_cudaruntimecall(cudaMemcpy(((char*) o->cu_mem_ptr) + gpu_offset,
						 io_u->xfer_buf,
						 io_u->xfer_buflen,
						 cudaMemcpyHostToDevice), rc);
		if (rc != 0) {
			log_err("DDIR_READ cudaMemcpy H2D failed\n");
			io_u->error = EIO;
		}
	} else {
		log_err("Illegal CUDA IO type: %d\n", o->cuda_io);
		assert(0);
		rc = EINVAL;
	}

	return rc;
}

static enum fio_q_status fio_libcufile_queue(struct thread_data *td,
					     struct io_u *io_u)
{
	struct libcufile_options *o = td->eo;
#ifndef CONFIG_NDS
    struct fio_libcufile_data *fcd = FILE_ENG_DATA(io_u->file);
#else
	int *fd = FILE_ENG_DATA(io_u->file);
#endif
	unsigned long long io_offset;
	ssize_t sz;
	ssize_t remaining;
	size_t xfered;
	size_t gpu_offset;
	int rc;
#ifndef CONFIG_NDS
    if (o->cuda_io == IO_CUFILE && fcd == NULL) {
		io_u->error = EINVAL;
		td_verror(td, EINVAL, "xfer");
		return FIO_Q_COMPLETED;
	}
#endif
	fio_ro_check(td, io_u);

	switch(io_u->ddir) {
	case DDIR_SYNC:
		rc = fsync(io_u->file->fd);
		if (rc != 0) {
			io_u->error = errno;
			log_err("fsync: err=%d\n", errno);
		}
		break;

	case DDIR_DATASYNC:
		rc = fdatasync(io_u->file->fd);
		if (rc != 0) {
			io_u->error = errno;
			log_err("fdatasync: err=%d\n", errno);
		}
		break;

	case DDIR_READ:
	case DDIR_WRITE:
		/*
		  There may be a better way to calculate gpu_offset. The intent is
		  that gpu_offset equals the the difference between io_u->xfer_buf and
		  the page-aligned base address for io_u buffers.
		*/
		gpu_offset = io_u->index * io_u->xfer_buflen;
		io_offset = io_u->offset;
		remaining = io_u->xfer_buflen;

		xfered = 0;
		sz = 0;

		assert(gpu_offset + io_u->xfer_buflen <= o->total_mem);

		if (o->cuda_io == IO_CUFILE) {
			if (!(ALIGNED_4KB(io_u->xfer_buflen) ||
			      (o->logged & LOGGED_BUFLEN_NOT_ALIGNED))) {
				log_err("buflen not 4KB-aligned: %llu\n", io_u->xfer_buflen);
				o->logged |= LOGGED_BUFLEN_NOT_ALIGNED;
			}

			if (!(ALIGNED_4KB(gpu_offset) ||
			      (o->logged & LOGGED_GPU_OFFSET_NOT_ALIGNED))) {
				log_err("gpu_offset not 4KB-aligned: %lu\n", gpu_offset);
				o->logged |= LOGGED_GPU_OFFSET_NOT_ALIGNED;
			}
		}

		if (io_u->ddir == DDIR_WRITE)
			rc = fio_libcufile_pre_write(td, o, io_u, gpu_offset);

		if (io_u->error != 0)
			break;
#ifdef CONFIG_NDS
		nds_devid_t nds_devid;
		nds_devid.dev_id = o->my_gpu_id;
#endif
		while (remaining > 0) {
			assert(gpu_offset + xfered <= o->total_mem);
			if (io_u->ddir == DDIR_READ) {
				if (o->cuda_io == IO_CUFILE) {
#ifndef CONFIG_NDS
					sz = cuFileRead(fcd->cf_handle, o->cu_mem_ptr, remaining,
							io_offset + xfered, gpu_offset + xfered);
					if (sz == -1) {
						io_u->error = errno;
						log_err("cuFileRead: err=%d\n", errno);
					} else if (sz < 0) {
						io_u->error = EIO;
						log_err("cuFileRead: err=%ld:%s\n", sz,
							mcfileop_status_error(-sz));
					}
#else
					sz = nds_read(*fd, io_offset + xfered, nds_devid, o->cu_mem_ptr + gpu_offset + xfered, remaining);
					if (sz == -1) {
						io_u->error = errno;
						log_err("nds_read: err=%d\n", errno);
					} else if (sz < 0) {
						io_u->error = EIO;
						log_err("nds_read: err=%ld:%s\n", sz,
							cufileop_status_error(-sz));
					}
#endif
				} else if (o->cuda_io == IO_POSIX) {
					sz = pread(io_u->file->fd, ((char*) io_u->xfer_buf) + xfered,
						   remaining, io_offset + xfered);
					if (sz < 0) {
						io_u->error = errno;
						log_err("pread: err=%d\n", errno);
					}
				} else {
					log_err("Illegal CUDA IO type: %d\n", o->cuda_io);
					io_u->error = -1;
					assert(0);
				}
			} else if (io_u->ddir == DDIR_WRITE) {
				if (o->cuda_io == IO_CUFILE) {
#ifndef CONFIG_NDS
					sz = cuFileWrite(fcd->cf_handle, o->cu_mem_ptr, remaining,
							 io_offset + xfered, gpu_offset + xfered);
					if (sz == -1) {
						io_u->error = errno;
						log_err("cuFileWrite: err=%d\n", errno);
					} else if (sz < 0) {
						io_u->error = EIO;
						log_err("cuFileWrite: err=%ld:%s\n", sz,
							mcfileop_status_error(-sz));
					}
#else
					sz = nds_write(*fd, io_offset + xfered, nds_devid, o->cu_mem_ptr + gpu_offset + xfered, remaining);
					if (sz == -1) {
						io_u->error = errno;
						log_err("nds_write: err=%d\n", errno);
					} else if (sz < 0) {
						io_u->error = EIO;
						log_err("nds_write: err=%ld:%s\n", sz,
							cufileop_status_error(-sz));
					}
#endif
				} else if (o->cuda_io == IO_POSIX) {
					sz = pwrite(io_u->file->fd,
						    ((char*) io_u->xfer_buf) + xfered,
						    remaining, io_offset + xfered);
					if (sz < 0) {
						io_u->error = errno;
						log_err("pwrite: err=%d\n", errno);
					}
				} else {
					log_err("Illegal CUDA IO type: %d\n", o->cuda_io);
					io_u->error = -1;
					assert(0);
				}
			} else {
				log_err("not DDIR_READ or DDIR_WRITE: %d\n", io_u->ddir);
				io_u->error = -1;
				assert(0);
				break;
			}

			if (io_u->error != 0)
				break;

			remaining -= sz;
			xfered += sz;

			if (remaining != 0)
				log_info("Incomplete %s: %ld bytes remaining\n",
					 io_u->ddir == DDIR_READ? "read" : "write", remaining);
		}

		if (io_u->error != 0)
			break;

		if (io_u->ddir == DDIR_READ)
			rc = fio_libcufile_post_read(td, o, io_u, gpu_offset);
		break;

	default:
		io_u->error = EINVAL;
		break;
	}

	if (io_u->error != 0) {
		log_err("IO failed\n");
		td_verror(td, io_u->error, "xfer");
	}

	return FIO_Q_COMPLETED;
}

static int fio_libcufile_open_file(struct thread_data *td, struct fio_file *f)
{
	struct libcufile_options *o = td->eo;
#ifndef CONFIG_NDS
	struct fio_libcufile_data *fcd = NULL;
#else
	int *fd = NULL;
#endif
	int rc;
#ifndef CONFIG_NDS
	CUfileError_t status;
#endif

	rc = generic_open_file(td, f);
	if (rc)
		return rc;
#ifndef CONFIG_NDS
	if (o->cuda_io == IO_CUFILE) {
		fcd = calloc(1, sizeof(*fcd));
		if (fcd == NULL) {
			rc = ENOMEM;
			goto exit_err;
		}

		fcd->cf_descr.handle.fd = f->fd;
		fcd->cf_descr.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
		status = cuFileHandleRegister(&fcd->cf_handle, &fcd->cf_descr);
		if (status.err != CU_FILE_SUCCESS) {
			log_err("cufile register: err=%d:%s\n", status.err,
				fio_libcufile_get_cuda_error(status));
			rc = EINVAL;
			goto exit_err;
		}
	}
	FILE_SET_ENG_DATA(f, fcd);
#else
	if (o->cuda_io == IO_CUFILE) {
		fd = calloc(1, sizeof(*fd));
		if (fd == NULL) {
			rc = ENOMEM;
			goto exit_err;
		}

		*fd = f->fd;
	}
	FILE_SET_ENG_DATA(f, fd);
#endif

	return 0;

exit_err:
#ifndef CONFIG_NDS
	if (fcd) {
		free(fcd);
		fcd = NULL;
	}
#else
	if (fd) {
		free(fd);
		fd = NULL;
	}
#endif
	if (f) {
		int rc2 = generic_close_file(td, f);
		if (rc2)
			log_err("generic_close_file: err=%d\n", rc2);
	}
	return rc;
}

static int fio_libcufile_close_file(struct thread_data *td, struct fio_file *f)
{
#ifndef CONFIG_NDS
	struct fio_libcufile_data *fcd = FILE_ENG_DATA(f);
#endif
	int rc;
	FILE_SET_ENG_DATA(f, NULL);
#ifndef CONFIG_NDS
	if (fcd != NULL) {
		cuFileHandleDeregister(fcd->cf_handle);
		FILE_SET_ENG_DATA(f, NULL);
		free(fcd);
	}
#endif
	rc = generic_close_file(td, f);

	return rc;
}

static int fio_libcufile_iomem_alloc(struct thread_data *td, size_t total_mem)
{
	struct libcufile_options *o = td->eo;
	int rc;
#ifndef CONFIG_NDS
	CUfileError_t status;
#endif
	o->total_mem = total_mem;
	o->logged = 0;
	o->cu_mem_ptr = NULL;
	o->junk_buf = NULL;
	td->orig_buffer = calloc(1, total_mem);
	if (!td->orig_buffer) {
		log_err("orig_buffer calloc failed: err=%d\n", errno);
		goto exit_error;
	}

	if (o->cuda_io == IO_POSIX) {
		o->junk_buf = calloc(1, total_mem);
		if (o->junk_buf == NULL) {
			log_err("junk_buf calloc failed: err=%d\n", errno);
			goto exit_error;
		}
	}

	dprint(FD_MEM, "Alloc %zu for GPU %d\n", total_mem, o->my_gpu_id);
	check_cudaruntimecall(cudaMalloc(&o->cu_mem_ptr, total_mem), rc);
	if (rc != 0)
		goto exit_error;
	check_cudaruntimecall(cudaMemset(o->cu_mem_ptr, 0xab, total_mem), rc);
	if (rc != 0)
		goto exit_error;
#ifndef CONFIG_NDS
	if (o->cuda_io == IO_CUFILE) {
		status = cuFileBufRegister(o->cu_mem_ptr, total_mem, 0);
		if (status.err != CU_FILE_SUCCESS) {
			log_err("cuFileBufRegister: err=%d:%s\n", status.err,
				fio_libcufile_get_cuda_error(status));
			goto exit_error;
		}
	}
#endif
	return 0;

exit_error:
	if (td->orig_buffer) {
		free(td->orig_buffer);
		td->orig_buffer = NULL;
	}
	if (o->junk_buf) {
		free(o->junk_buf);
		o->junk_buf = NULL;
	}
	if (o->cu_mem_ptr) {
		cudaFree(o->cu_mem_ptr);
		o->cu_mem_ptr = NULL;
	}
	return 1;
}

static void fio_libcufile_iomem_free(struct thread_data *td)
{
	struct libcufile_options *o = td->eo;

	if (o->junk_buf) {
		free(o->junk_buf);
		o->junk_buf = NULL;
	}
	if (o->cu_mem_ptr) {
#ifndef CONFIG_NDS
		if (o->cuda_io == IO_CUFILE)
			cuFileBufDeregister(o->cu_mem_ptr);
#endif
		cudaFree(o->cu_mem_ptr);
		o->cu_mem_ptr = NULL;
	}
	if (td->orig_buffer) {
		free(td->orig_buffer);
		td->orig_buffer = NULL;
	}
}

static void fio_libcufile_cleanup(struct thread_data *td)
{
	struct libcufile_options *o = td->eo;

	pthread_mutex_lock(&running_lock);
	running--;
	assert(running >= 0);
	if (running == 0) {
		/* only close the driver if initialized and
		   this is the last worker thread */
		if (o->cuda_io == IO_CUFILE && cufile_initialized)
#ifndef CONFIG_NDS
			cuFileDriverClose();
#else
			nds_driver_close();
#endif
		cufile_initialized = 0;
	}
	pthread_mutex_unlock(&running_lock);
}

FIO_STATIC struct ioengine_ops ioengine = {
	.name                = "libcufile",
	.version             = FIO_IOOPS_VERSION,
	.init                = fio_libcufile_init,
	.queue               = fio_libcufile_queue,
	.get_file_size       = generic_get_file_size,
	.open_file           = fio_libcufile_open_file,
	.close_file          = fio_libcufile_close_file,
	.iomem_alloc         = fio_libcufile_iomem_alloc,
	.iomem_free          = fio_libcufile_iomem_free,
	.cleanup             = fio_libcufile_cleanup,
	.flags               = FIO_SYNCIO,
	.options             = options,
	.option_struct_size  = sizeof(struct libcufile_options)
};

void fio_init fio_libcufile_register(void)
{
	register_ioengine(&ioengine);
}

void fio_exit fio_libcufile_unregister(void)
{
	unregister_ioengine(&ioengine);
}
