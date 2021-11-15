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
void fbh5_write(fbh5_context_t * p_fbh5_ctx, fb_hdr_t * p_fb_hdr, void * p_buffer, size_t bufsize, int debug_callback) {
    herr_t      status;         // Status from HDF5 function call
    size_t      ntints;         // Number of time integrations in the current dump
    hid_t       filespace_id;   // Identifier for a copy of the dataspace 
    hsize_t     selection[3];   // Current selection
    void       *bufptr;         // Pointer into the current dump

    /*
     * Initialise write loop.
     */
    ntints = bufsize / p_fbh5_ctx->tint_size;  // Compute the number of time integrations in the current dump.
    p_fbh5_ctx->dump_count += 1;               // Bump the dump count.
    bufptr = p_buffer;                         // Point to the first time integration in the dump.

    /*
     * BEGIN write-loop for the current dump.
     * Write one time integration at a time.
     */
    for(int ii = 0; ii < ntints; ++ii) {
        /*
         *  Bump the count of time integrations.
         * If this is the very first write, no need to bump the file size
         * because fbh5_open already accounted for the first time integration.
         */
        if(p_fbh5_ctx->offset_dims[0] > 0)
            p_fbh5_ctx->filesz_dims[0] += 1;

        /*
         * Define the current slab selection in terms of its shape.
         */
        selection[0] = 1;
        selection[1] = p_fb_hdr->nifs;
        selection[2] = p_fb_hdr->nchans;

        if(debug_callback) {
            printf("fbh5_write: dump %ld, dump-tint %d, offset=(%lld, %lld, %lld), selection=(%lld, %lld, %lld), filesize=(%lld, %lld, %lld)\n",
                   p_fbh5_ctx->dump_count,
                   ii + 1,
                   p_fbh5_ctx->offset_dims[0], 
                   p_fbh5_ctx->offset_dims[1], 
                   p_fbh5_ctx->offset_dims[2],
                   selection[0], 
                   selection[1], 
                   selection[2],
                   p_fbh5_ctx->filesz_dims[0], 
                   p_fbh5_ctx->filesz_dims[1], 
                   p_fbh5_ctx->filesz_dims[2]);
        }
        status = H5Dset_extent(p_fbh5_ctx->dataset_id,          // Dataset handle
                               p_fbh5_ctx->filesz_dims);        // New file size shape
        if(status < 0)
            fbh5_oops(__FILE__, __LINE__, "fbh5_write: H5Dset_extent/dataset_id FAILED");

        /*
         * Get filespace.
         */
        filespace_id = H5Dget_space(p_fbh5_ctx->dataset_id);    // Dataset handle
        if(filespace_id < 0)
            fbh5_oops(__FILE__, __LINE__, "fbh5_write: H5Dget_space FAILED");

        /*
         * Select the filespace hyperslab.
         */
        status = H5Sselect_hyperslab(filespace_id,              // Filespace handle
                                     H5S_SELECT_SET,            // Replace preexisting selection
                                     p_fbh5_ctx->offset_dims,   // Starting offset dimensions of first element
                                     NULL,                      // Not "striding"
                                     selection,                 // Selection dimensions
                                     NULL);                     // Block parameter : default value
        if(status < 0)
            fbh5_oops(__FILE__, __LINE__, "fbh5_write: H5Sselect_hyperslab/filespace FAILED");

        /*
         * Write out current time integration to the hyperslab.
         */
        status = H5Dwrite(p_fbh5_ctx->dataset_id,   // Dataset handle
                          p_fbh5_ctx->elem_type,    // HDF5 element type
                          p_fbh5_ctx->dataspace_id, // Dataspace handle
                          filespace_id,             // Filespace_id
                          H5P_DEFAULT,              // Default data transfer properties
                          bufptr);                  // Buffer holding the data
        if(status < 0)
            fbh5_oops(__FILE__, __LINE__, "fbh5_write: H5Dwrite FAILED");

        /*
         * Close temporary filespace handle.
         */
        status = H5Sclose(filespace_id);
        if(status < 0)
            fbh5_oops(__FILE__, __LINE__, "fbh5_close H5Sclose/filespace_id FAILED\n");
        /*
         * Point ahead for the next call to fbh5_write.
         */
        p_fbh5_ctx->offset_dims[0] += 1;

        /*
         * Bump the dump buffer pointer to the next time integration.
         */
        bufptr += p_fbh5_ctx->tint_size;

    /*
     * END write-loop for the current dump.
     */
    }
     
    /*
     * Bump counters.
     */
    p_fbh5_ctx->byte_count += bufsize;

    /*
     * Bye-bye.
     */
}

