/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * fbh5_write.c                                                                *
 * ------------                                                                *
 * Write an FBH5 dump.                                     .                   *
 *                                                                             *
 * HDF 5 library functions used:                                               *
 * - H5Dset_extent        - Define new file size to include this new dump      *
 * - H5Dget_space         - Get a space handle for writing                     *
 * - H5Sselect_hyperslab  - Define hyperslab offset and length in to write     *
 * - H5Dwrite             - Write the hyperslab                                *
 *                                                                             *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include "fbh5_defs.h"

/***
	Main entry point.
***/
int fbh5_write(fbh5_context_t * p_fbh5_ctx, fb_hdr_t * p_fb_hdr, void * p_buffer, size_t bufsize, int debug_callback) {
    herr_t      status;          // Status from HDF5 function call
    size_t      ntints;          // Number of time integrations in the current dump
    hid_t       filespace_id;    // Identifier for a copy of the dataspace 
    hsize_t     selection[3];    // Current selection
    clock_t     clock_1;         // Debug time measurement
    double      cpu_time_used;   // Debug time measurement

    /*
     * Initialise write loop.
     */
    if(debug_callback)
        fbh5_show_context("fbh5_write", p_fbh5_ctx);
    ntints = bufsize / p_fbh5_ctx->tint_size;  // Compute the number of time integrations in the current dump.
    p_fbh5_ctx->dump_count += 1;               // Bump the dump count.

    /*
     * Bump the count of time integrations.
     * One was already accounted for at open time - required by HDF5 library.
     * So, 
     *    If this is the very first write, bump by (ntints - 1);
     *    Else, bumpy by ntints.
     */
    if(p_fbh5_ctx->offset_dims[0] > 0)
        p_fbh5_ctx->filesz_dims[0] += ntints;
    else
        p_fbh5_ctx->filesz_dims[0] += (ntints - 1);

    /*
     * Define the current slab selection in terms of its shape.
     */
    selection[0] = ntints;
    selection[1] = p_fb_hdr->nifs;
    selection[2] = p_fb_hdr->nchans;

    if(debug_callback) {
        fbh5_info("fbh5_write: dump %ld, offset=(%lld, %lld, %lld), selection=(%lld, %lld, %lld), filesize=(%lld, %lld, %lld)\n",
               p_fbh5_ctx->dump_count,
               p_fbh5_ctx->offset_dims[0], 
               p_fbh5_ctx->offset_dims[1], 
               p_fbh5_ctx->offset_dims[2],
               selection[0], 
               selection[1], 
               selection[2],
               p_fbh5_ctx->filesz_dims[0], 
               p_fbh5_ctx->filesz_dims[1], 
               p_fbh5_ctx->filesz_dims[2]);
        clock_1 = clock();
     }

    /*
     * Extend dataset.
     */
    status = H5Dset_extent(p_fbh5_ctx->dataset_id,    // Dataset handle
                           p_fbh5_ctx->filesz_dims);  // New dataset shape
    if(status < 0) {
        fbh5_error(__FILE__, __LINE__, "fbh5_write: H5Dset_extent/dataset_id FAILED");
        p_fbh5_ctx->active = 0;
        return 1;
    }

    /*
     * Reset dataspace extent to match current slab selection.
     */
    status = H5Sset_extent_simple(p_fbh5_ctx->dataspace_id, // Dataspace handle
                                  NDIMS,                    // Repeat rank from previous API calls
                                  selection,                // New dataspace size shape
                                  p_fbh5_ctx->filesz_dims); // Max dataspace dimensions
    if(status < 0) {
        fbh5_error(__FILE__, __LINE__, "fbh5_write: H5Dset_extent/dataset_id FAILED");
        p_fbh5_ctx->active = 0;
        return 1;
    }

    /*
     * Get filespace.
     */
    filespace_id = H5Dget_space(p_fbh5_ctx->dataset_id);    // Dataset handle
    if(filespace_id < 0) {
        fbh5_error(__FILE__, __LINE__, "fbh5_write: H5Dget_space FAILED");
        p_fbh5_ctx->active = 0;
        return 1;
    }

    /*
     * Select the filespace hyperslab.
     */
    status = H5Sselect_hyperslab(filespace_id,              // Filespace handle
                                 H5S_SELECT_SET,            // Replace preexisting selection
                                 p_fbh5_ctx->offset_dims,   // Starting offset dimensions of first element
                                 NULL,                      // Not "striding"
                                 selection,                 // Selection dimensions
                                 NULL);                     // Block parameter : default value
    if(status < 0) {
        fbh5_error(__FILE__, __LINE__, "fbh5_write: H5Sselect_hyperslab/filespace FAILED");
        p_fbh5_ctx->active = 0;
        return 1;
    }

    /*
     * Write out current time integration to the hyperslab.
     */
    status = H5Dwrite(p_fbh5_ctx->dataset_id,   // Dataset handle
                      p_fbh5_ctx->elem_type,    // HDF5 element type
                      p_fbh5_ctx->dataspace_id, // Dataspace handle
                      filespace_id,             // Filespace_id
                      H5P_DEFAULT,              // Default data transfer properties
                      p_buffer);                // Buffer holding the data
    if(status < 0) {
        fbh5_error(__FILE__, __LINE__, "fbh5_write: H5Dwrite FAILED");
        p_fbh5_ctx->active = 0;
        return 1;
    }

    /*
     * Point ahead for the next call to fbh5_write.
     */
    p_fbh5_ctx->offset_dims[0] += ntints;

    /*
     * Close temporary filespace handle.
     */
    status = H5Sclose(filespace_id);
    if(status < 0) {
        fbh5_error(__FILE__, __LINE__, "fbh5_close H5Sclose/filespace_id FAILED\n");
        p_fbh5_ctx->active = 0;
        return 1;
    }
    if(debug_callback) {
        cpu_time_used = ((double) (clock() - clock_1)) / CLOCKS_PER_SEC;
        fbh5_info("fbh5_write: dump %ld E.T. = %.3f s\n", p_fbh5_ctx->dump_count, cpu_time_used);
    }

    /*
     * Bump counters.
     */
    p_fbh5_ctx->byte_count += bufsize;

    /*
     * Bye-bye.
     */
    return 0;
}
