/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * fbh5_open.c                                                                 *
 * -----------                                                                 *
 * Begin an FBH5 writing session                           .                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include "fbh5_defs.h"


int CACHE_SPECS = 0;


/***
	Open-file entry point.
***/
int fbh5_open(fbh5_context_t * p_fbh5_ctx, fb_hdr_t * p_fb_hdr, char * output_path, int debug_callback) {
    hid_t       dcpl;               // Chunking handle - needed until dataset handle is produced
    hsize_t     max_dims[NDIMS];    // Maximum dataset allocation dimensions
    hsize_t     cdims[NDIMS];       // Chunking dimensions
    herr_t      status;             // Status from HDF5 function call
    char        wstr[256];          // sprintf target
    unsigned    hdf5_majnum, hdf5_minnum, hdf5_relnum;  // Version/release info for the HDF5 library

    // Caching
    hid_t       fapl;                   // File access property list identifier
    size_t      fcache_nslots = 521;    // Hash table number of slots.  Default value.
    size_t      fcache_nbytes = 0;      // Cache size in bytes
    
    // Filter identities:
    H5Z_filter_t filter_id_bitshuffle = 32008;
    int filter_available = 0;

    // Bitshuffle options:
    unsigned bitshuffle_opts[] = {0, 2};
    // Ref: def __init__ in class Bitshuffle in https://github.com/silx-kit/hdf5plugin/blob/main/src/hdf5plugin/__init__.py
    // 0 = take default like blimpy
    // 2 = use lz4 like blimpy

    // Default: Failed.
    p_fbh5_ctx->active = 0;

    // Announce versions:
    if(debug_callback) {
        fbh5_info("fbh5_open: FBH5 path: %s\n", output_path);
        H5get_libversion(&hdf5_majnum, &hdf5_minnum, &hdf5_relnum);
        fbh5_info("fbh5_open: HDF5 library version: %d.%d.%d\n", hdf5_majnum, hdf5_minnum, hdf5_relnum);
        fbh5_info("fbh5_open: Creating dataspace dimensions using nifs=%d and nchans=%d\n",
                p_fb_hdr->nifs,
                p_fb_hdr->nchans);
    }
    
    /*
     * Check whether or not the Bitshuffle filter is available.
     */
    if (H5Zfilter_avail(filter_id_bitshuffle) <= 0)
        fbh5_warning(__FILE__, __LINE__, "fbhf_open: Filter bitshuffle is NOT available\n");
    else {
        filter_available = 1;
    }
    
    /*
     * Validate fb_hdr: nifs, nbits, nfpc, nchans.
     */
    if((p_fb_hdr->nbits % 8 != 0) || (p_fb_hdr->nbits < 8) || (p_fb_hdr->nbits > 64)) {
        sprintf(wstr, "fbh5_open: nbits must be in [8, 16, 32, 64] but I saw %d", p_fb_hdr->nifs);
        fbh5_error(__FILE__, __LINE__, wstr);
        return 1;
    }
    if((p_fb_hdr->nifs < 1) || (p_fb_hdr->nifs > 4)) {
        sprintf(wstr, "fbh5_open: nifs must be in [1, 2, 3, 4] but I saw %d", p_fb_hdr->nifs);
        fbh5_error(__FILE__, __LINE__, wstr);
        return 1;
    }
    if(p_fb_hdr->nfpc < 1) {
        sprintf(wstr, "fbh5_open: nfpc must be > 0 but I saw %d", p_fb_hdr->nfpc);
        fbh5_error(__FILE__, __LINE__, wstr);
        return 1;
    }
    if(p_fb_hdr->nchans < 1) {
        sprintf(wstr, "fbh5_open: nchans must be > 0 but I saw %d", p_fb_hdr->nchans);
        fbh5_error(__FILE__, __LINE__, wstr);
        return 1;
    }
    
    /*
     * Initialize FBH5 context.
     */
    memset(p_fbh5_ctx, 0, sizeof(fbh5_context_t));
    p_fbh5_ctx->elem_size = p_fb_hdr->nbits / 8;
    p_fbh5_ctx->tint_size = p_fb_hdr->nifs * p_fb_hdr->nchans * p_fbh5_ctx->elem_size;
    p_fbh5_ctx->offset_dims[0] = 0;
    p_fbh5_ctx->offset_dims[1] = 0;
    p_fbh5_ctx->offset_dims[2] = 0;
    
    /*
     * Open HDF5 file.  Overwrite it if preexisting.
     */
    p_fbh5_ctx->file_id = H5Fcreate(output_path,    // Full path of output file
                                    H5F_ACC_TRUNC,  // Overwrite if preexisting.
                                    H5P_DEFAULT,    // Default creation property list 
                                    H5P_DEFAULT);   // Default access property list
    if(p_fbh5_ctx->file_id < 0) {
        sprintf(wstr, "fbh5_open: H5Fcreate of '%s' FAILED", output_path);
        fbh5_error(__FILE__, __LINE__, wstr);
        return 1;
    }

    /*
     * If specifying file-level caching specifications, do so now.
     */

    if(CACHE_SPECS == 1) {
        fcache_nbytes = p_fbh5_ctx->tint_size + 42;
        fapl = H5Fget_access_plist(p_fbh5_ctx->file_id);
        if(fapl < 0)
            fbh5_warning(__FILE__, __LINE__, "fbh5_open: H5Fget_access_plist FAILED; using default caching");
        else {
            status = H5Pset_cache(fapl, 
                                  0,                // ignored
                                  fcache_nslots,    // Hash table slot count
                                  fcache_nbytes,    // File cache size in bytes
                                  1.0);             // Rawspec writes data only once.  Never reads.
            if(status < 0)
                fbh5_warning(__FILE__, __LINE__, "fbh5_open: H5Pset_cache FAILED; using default caching");
        }
    }
 
    /*
     * Write the file-level metadata attributes.
     */
    fbh5_set_str_attr(p_fbh5_ctx->file_id, "CLASS", FILTERBANK_CLASS, debug_callback);
    fbh5_set_str_attr(p_fbh5_ctx->file_id, "VERSION", FILTERBANK_VERSION, debug_callback);

    /*
     * Initialise the total file size in terms of its shape.
     */
    p_fbh5_ctx->filesz_dims[0] = 1;
    p_fbh5_ctx->filesz_dims[1] = p_fb_hdr->nifs;
    p_fbh5_ctx->filesz_dims[2] = p_fb_hdr->nchans;
    
    /*
     * Set the maximum file size in terms of its shape.
     */
    max_dims[0] = H5S_UNLIMITED;
    max_dims[1] = p_fb_hdr->nifs;
    max_dims[2] = p_fb_hdr->nchans;

    /*
     * Create a dataspace which is extensible in the time dimension.
     */
    p_fbh5_ctx->dataspace_id = H5Screate_simple(NDIMS,                           // Rank
                                                p_fbh5_ctx->filesz_dims, // initial dimensions
                                                max_dims);               // maximum dimensions
    if(p_fbh5_ctx->dataspace_id < 0) {
        fbh5_error(__FILE__, __LINE__, "fbh5_open: H5Screate_simple FAILED");
        return 1;
    }
    
    /*
     * Initialise the dataset creation property list
     */
    dcpl = H5Pcreate(H5P_DATASET_CREATE);
    if(dcpl < 0) {
        fbh5_error(__FILE__, __LINE__, "fbh5_open: H5Pcreate/dcpl FAILED");
        return 1;
    }
         
    /*
     * Add chunking to the dataset creation property list.
     */
    cdims[0] = 1;
    cdims[1] = 1;
    cdims[2] = p_fb_hdr->nchans;
    status   = H5Pset_chunk(dcpl, NDIMS, cdims);
    if(status != 0) {
        fbh5_error(__FILE__, __LINE__, "fbh5_open: H5Pset_chunk FAILED");
        return 1;
    }
    if(debug_callback)
        fbh5_info("fbh5_open: Chunk dimensions = (%lld, %lld, %lld)\n", cdims[0], cdims[1], cdims[2]);

    /*
     * Add the Bitshuffle and LZ4 filters to the dataset creation property list.
     */
    if(filter_available) {
        status = H5Pset_filter(dcpl, filter_id_bitshuffle, H5Z_FLAG_MANDATORY, 2, bitshuffle_opts); // Bitshuffle Filter
        if(status < 0)
            fbh5_warning(__FILE__, __LINE__, "fbh5_open: H5Pset_filter FAILED; data will not be compressed");
    }
    
    /* 
     * Define datatype for the data in the file.
     * We will store little endian values.
     */
    switch(p_fb_hdr->nbits) {
        case 8:
            p_fbh5_ctx->elem_type = H5T_NATIVE_B8;
            break;
        case 16:
            p_fbh5_ctx->elem_type = H5T_NATIVE_B16;
            break;
        case 32:
            p_fbh5_ctx->elem_type = H5T_IEEE_F32LE;
            break;
        default: // 64
            p_fbh5_ctx->elem_type = H5T_IEEE_F64LE;
    }

    /*
     * Create the dataset.
     */
    p_fbh5_ctx->dataset_id = H5Dcreate(p_fbh5_ctx->file_id,       // File handle
                                       DATASETNAME,               // Dataset name
                                       p_fbh5_ctx->elem_type,     // HDF5 data type
                                       p_fbh5_ctx->dataspace_id,  // Dataspace handle
                                       H5P_DEFAULT,               // 
                                       dcpl,                      // Dataset creation property list
                                       H5P_DEFAULT);              // Default access properties
    if(p_fbh5_ctx->dataset_id < 0) {
        fbh5_error(__FILE__, __LINE__, "fbh5_open: H5Dcreate FAILED");
        return 1;
    }

    /*
     * Close dcpl handle.
     */
    status = H5Pclose(dcpl);
    if(status != 0)
        fbh5_warning(__FILE__, __LINE__, "fbh5_open: H5Pclose/dcpl FAILED\n");
    
    /*
     * Write dataset metadata attributes.
     */
    fbh5_write_metadata(p_fbh5_ctx->dataset_id, // Dataset handle
                        p_fb_hdr,               // Metadata (SIGPROC header)
                        debug_callback);        // Tracing flag
    if(debug_callback)
        fbh5_info("fbh5_open: Dataset metadata stored; done.\n");

    /*
     * Bye-bye.
     */
    p_fbh5_ctx->active = 1;
    if(debug_callback)
        fbh5_show_context("fbh5_open", p_fbh5_ctx);
    return 0;

}