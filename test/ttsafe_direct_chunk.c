/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the LICENSE file, which can be found at the root of the source code       *
 * distribution tree, or in https://www.hdfgroup.org/licenses.               *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/********************************************************************
 *
 * Testing direct chunk reads from several threads
 * -----------------------------------------------
 *
 * Several threads read raw chunks, whole and by sub-chunk range, from one
 * file opened read-only, while another thread opens, queries and closes
 * the same dataset and the same file.  In a threadsafe library a direct
 * chunk read of such a file through the sec2 driver releases the global
 * lock while the driver reads, so this checks that the readers get the
 * right bytes while other threads use the file.
 *
 * Temporary files generated:
 *   ttsafe_direct_chunk.h5
 *
 ********************************************************************/
#include "ttsafe.h"

#ifdef H5_HAVE_THREADSAFE_API

#define DIRECT_CHUNK_FILENAME "ttsafe_direct_chunk.h5"
#define DIRECT_CHUNK_NCHUNKS  64
#define DIRECT_CHUNK_NELMTS   4096
#define DIRECT_CHUNK_NREADERS 8
#define DIRECT_CHUNK_NPASSES  20

typedef struct direct_chunk_info_t {
    hid_t file;
    hid_t dset;
    int   errors;
} direct_chunk_info_t;

static int
direct_chunk_value(unsigned chunk, unsigned element)
{
    return (int)(chunk * DIRECT_CHUNK_NELMTS + element);
}

static H5TS_THREAD_RETURN_TYPE
direct_chunk_reader(void *_info)
{
    direct_chunk_info_t *info = (direct_chunk_info_t *)_info;
    int                 *buf  = malloc(DIRECT_CHUNK_NELMTS * sizeof(int));
    hid_t                dxpl = H5Pcreate(H5P_DATASET_XFER);
    unsigned             pass, chunk, u;

    if (!buf || dxpl < 0 || H5Pset_sub_chunk(dxpl, 100 * sizeof(int), 50 * sizeof(int)) < 0)
        info->errors++;
    for (pass = 0; pass < DIRECT_CHUNK_NPASSES && !info->errors; pass++)
        for (chunk = 0; chunk < DIRECT_CHUNK_NCHUNKS; chunk++) {
            hsize_t  offset[2] = {chunk, 0};
            uint32_t filters   = 0;
            size_t   nbytes    = DIRECT_CHUNK_NELMTS * sizeof(int);

            if (H5Dread_chunk2(info->dset, H5P_DEFAULT, offset, &filters, buf, &nbytes) < 0 ||
                nbytes != DIRECT_CHUNK_NELMTS * sizeof(int))
                info->errors++;
            for (u = 0; u < DIRECT_CHUNK_NELMTS; u++)
                if (buf[u] != direct_chunk_value(chunk, u)) {
                    info->errors++;
                    break;
                }

            nbytes = DIRECT_CHUNK_NELMTS * sizeof(int);
            if (H5Dread_chunk2(info->dset, dxpl, offset, &filters, buf, &nbytes) < 0 ||
                nbytes != 50 * sizeof(int))
                info->errors++;
            for (u = 0; u < 50; u++)
                if (buf[u] != direct_chunk_value(chunk, 100 + u)) {
                    info->errors++;
                    break;
                }
        }
    H5Pclose(dxpl);
    free(buf);
    return (H5TS_thread_ret_t)0;
}

static H5TS_THREAD_RETURN_TYPE
direct_chunk_opener(void *_info)
{
    direct_chunk_info_t *info = (direct_chunk_info_t *)_info;
    unsigned             pass;

    for (pass = 0; pass < DIRECT_CHUNK_NPASSES * 4 && !info->errors; pass++) {
        hid_t   file = H5Fopen(DIRECT_CHUNK_FILENAME, H5F_ACC_RDONLY, H5P_DEFAULT);
        hid_t   dset = H5Dopen2(info->file, "data", H5P_DEFAULT);
        hid_t   space;
        hsize_t size      = 0;
        hsize_t offset[2] = {pass % DIRECT_CHUNK_NCHUNKS, 0};

        if (file < 0 || dset < 0)
            info->errors++;
        if ((space = H5Dget_space(dset)) < 0 || H5Sclose(space) < 0)
            info->errors++;
        if (H5Dget_chunk_storage_size(dset, offset, &size) < 0 || size != DIRECT_CHUNK_NELMTS * sizeof(int))
            info->errors++;
        if (H5Dclose(dset) < 0 || H5Fclose(file) < 0)
            info->errors++;
    }
    return (H5TS_thread_ret_t)0;
}

void
tts_direct_chunk(void H5_ATTR_UNUSED *params)
{
    H5TS_thread_t       threads[DIRECT_CHUNK_NREADERS + 1];
    direct_chunk_info_t info[DIRECT_CHUNK_NREADERS + 1];
    hsize_t             dims[2]  = {DIRECT_CHUNK_NCHUNKS, DIRECT_CHUNK_NELMTS};
    hsize_t             chunk[2] = {1, DIRECT_CHUNK_NELMTS};
    int                *buf;
    hid_t               file, dset, space, dcpl;
    unsigned            u, v;
    herr_t              status;

    /* Write a dataset of one row per chunk */
    buf = malloc(DIRECT_CHUNK_NCHUNKS * DIRECT_CHUNK_NELMTS * sizeof(int));
    CHECK_PTR(buf, "malloc");
    for (u = 0; u < DIRECT_CHUNK_NCHUNKS; u++)
        for (v = 0; v < DIRECT_CHUNK_NELMTS; v++)
            buf[u * DIRECT_CHUNK_NELMTS + v] = direct_chunk_value(u, v);
    file = H5Fcreate(DIRECT_CHUNK_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    CHECK(file, H5I_INVALID_HID, "H5Fcreate");
    space = H5Screate_simple(2, dims, NULL);
    CHECK(space, H5I_INVALID_HID, "H5Screate_simple");
    dcpl = H5Pcreate(H5P_DATASET_CREATE);
    CHECK(dcpl, H5I_INVALID_HID, "H5Pcreate");
    status = H5Pset_chunk(dcpl, 2, chunk);
    CHECK(status, FAIL, "H5Pset_chunk");
    dset = H5Dcreate2(file, "data", H5T_NATIVE_INT, space, H5P_DEFAULT, dcpl, H5P_DEFAULT);
    CHECK(dset, H5I_INVALID_HID, "H5Dcreate2");
    status = H5Dwrite(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf);
    CHECK(status, FAIL, "H5Dwrite");
    H5Dclose(dset);
    H5Pclose(dcpl);
    H5Sclose(space);
    H5Fclose(file);
    free(buf);

    /* Read it from several threads at once */
    file = H5Fopen(DIRECT_CHUNK_FILENAME, H5F_ACC_RDONLY, H5P_DEFAULT);
    CHECK(file, H5I_INVALID_HID, "H5Fopen");
    dset = H5Dopen2(file, "data", H5P_DEFAULT);
    CHECK(dset, H5I_INVALID_HID, "H5Dopen2");
    for (u = 0; u <= DIRECT_CHUNK_NREADERS; u++) {
        info[u].file   = file;
        info[u].dset   = dset;
        info[u].errors = 0;
        if (H5TS_thread_create(&threads[u],
                               u < DIRECT_CHUNK_NREADERS ? direct_chunk_reader : direct_chunk_opener,
                               &info[u]) < 0)
            TestErrPrintf("thread %u did not start", u);
    }
    for (u = 0; u <= DIRECT_CHUNK_NREADERS; u++) {
        if (H5TS_thread_join(threads[u], NULL) < 0)
            TestErrPrintf("thread %u failed to join", u);
        if (info[u].errors)
            TestErrPrintf("thread %u saw %d errors", u, info[u].errors);
    }
    status = H5Dclose(dset);
    CHECK(status, FAIL, "H5Dclose");
    status = H5Fclose(file);
    CHECK(status, FAIL, "H5Fclose");
} /* end tts_direct_chunk() */

void
cleanup_direct_chunk(void H5_ATTR_UNUSED *params)
{
    if (GetTestCleanup())
        HDunlink(DIRECT_CHUNK_FILENAME);
}
#endif /* H5_HAVE_THREADSAFE_API */
