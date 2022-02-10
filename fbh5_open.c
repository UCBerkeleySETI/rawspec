/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * fbh5_open.c                                                                 *
 * -----------                                                                 *
 * Begin an FBH5 writing session                           .                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include "fbh5_defs.h"
#include "rawspec_version.h"

#define MIN(a,b) ((a < b) ? (a) : (b))
#define MAX(a,b) ((a > b) ? (a) : (b))

// Returns a pointer to a string containing the librawspec version
const char * get_librawspec_version();
// Returns a pointer to a string containing the cuFFT version
const char * get_cufft_version();


/***
	Open-file entry point.
***/
int fbh5_open(fbh5_context_t * p_fbh5_ctx, fb_hdr_t * p_fb_hdr, unsigned int Nd, char * output_path, int debug_callback) {
    hid_t       dcpl;               // Chunking handle - needed until dataset handle is produced
    hsize_t     max_dims[NDIMS];    // Maximum dataset allocation dimensions
    herr_t      status;             // Status from HDF5 function call
    char        wstr[256];          // sprintf target
    unsigned    hdf5_majnum, hdf5_minnum, hdf5_relnum;  // Version/release info for the HDF5 library

    // Chunking parameters
    int         USE_BLIMPY = 0;     // 1 : use blimpy's algorithm; 0 : don't do that
    hsize_t     cdims[NDIMS];       // Chunking dimensions array
 
    // Caching parameters  
    int         CACHING_TYPE = 1;       // 0 : no caching ; 1 : computed caching specifications; 2 : default caching
    hid_t       fapl;                   // File access property list identifier
    size_t      fcache_nslots = 0;      // Hash table number of slots.  Default value = 521.  Determined later.
    size_t      fcache_nbytes = 0;      // Cache size in bytes.  Determined later.
    
    // Bitshuffle plugin status:
    int         bitshuffle_available = 0;     // Bitshuffle availability: 1=yes, 0=no

    // Bitshuffle options
    // Ref: def __init__ in class Bitshuffle in https://github.com/silx-kit/hdf5plugin/blob/main/src/hdf5plugin/__init__.py
    // 0 = take default like blimpy
    // 2 = use lz4 like blimpy
    unsigned bitshuffle_opts[] = {0, 2};

    // Default status: no longer active.  Will be updated later as active.
    p_fbh5_ctx->active = 0;

    /*
     * Check whether or not the Bitshuffle filter is available.
     */
    if (H5Zfilter_avail(FILTER_ID_BITSHUFFLE) <= 0)
        fbh5_warning(__FILE__, __LINE__, "fbhf_open: Plugin bitshuffle is NOT available; data will not be compressed\n");
    else {
        bitshuffle_available = 1;
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
     * Write blimpy-required file-level metadata attributes.
     */
    fbh5_set_str_attr(p_fbh5_ctx->file_id, 
                      "CLASS", 
                      FILTERBANK_CLASS, 
                      debug_callback);
    fbh5_set_str_attr(p_fbh5_ctx->file_id, 
                      "VERSION", 
                      FILTERBANK_VERSION, 
                      debug_callback);

    /*
     * Get software versions and store them as file-level attributes.
     */
    strcpy(wstr, STRINGIFY(RAWSPEC_VERSION));
    fbh5_set_str_attr(p_fbh5_ctx->file_id, 
                      "VERSION_RAWSPEC", 
                      wstr, 
                      debug_callback);
    fbh5_set_str_attr(p_fbh5_ctx->file_id, 
                      "VERSION_LIBRAWSPEC", 
                      (char *) get_librawspec_version(), 
                      debug_callback);
    fbh5_set_str_attr(p_fbh5_ctx->file_id, 
                      "VERSION_CUFFT", 
                      (char *) get_cufft_version(), 
                      debug_callback);
    H5get_libversion(&hdf5_majnum, &hdf5_minnum, &hdf5_relnum);
    sprintf(wstr, "%d.%d.%d", hdf5_majnum, hdf5_minnum, hdf5_relnum);
    fbh5_set_str_attr(p_fbh5_ctx->file_id, 
                      "VERSION_HDF5", 
                      wstr, 
                      debug_callback);
    
    /*
     * Store bitshuffle availability as a file-level attribute.
     */
    if(bitshuffle_available > 0)
        strcpy(wstr, "ENABLED");
    else
        strcpy(wstr, "DISABLED");
    fbh5_set_str_attr(p_fbh5_ctx->file_id, 
                      "BITSHUFFLE", 
                      wstr, 
                      debug_callback);

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
    printf("Number of spectra per dump (Nd) = %u\n", Nd);
    printf("Number of fine channels per coarse channel (nfpc) = %u\n", p_fb_hdr->nfpc);
    if(USE_BLIMPY == 1)
        fbh5_blimpy_chunking(p_fb_hdr, &cdims[0]);
    else {
        cdims[0] = Nd;
        cdims[1] = 1;
        cdims[2] = p_fb_hdr->nfpc;
    }
    status = H5Pset_chunk(dcpl, NDIMS, cdims);
    if(status != 0) {
        fbh5_error(__FILE__, __LINE__, "fbh5_open: H5Pset_chunk FAILED");
        return 1;
    }
    printf("Chunk dimensions = (%lld, %lld, %lld)\n", cdims[0], cdims[1], cdims[2]);

    /*
     * Add the Bitshuffle and LZ4 filters to the dataset creation property list.
     */
    if(bitshuffle_available) {
        status = H5Pset_filter(dcpl, FILTER_ID_BITSHUFFLE, H5Z_FLAG_MANDATORY, 2, bitshuffle_opts); // Bitshuffle Filter
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
     * If specifying file-level caching specifications, do so now.
     */
    if(CACHING_TYPE != 2) { // not default caching
        if(CACHING_TYPE == 1) { // specifying cache parameters
            fcache_nslots = (cdims[0] * cdims[2]) + 1;
            fcache_nbytes = (Nd * p_fbh5_ctx->tint_size) + 1;
            printf("Cache nslots = %lu, nbytes = %lu\n", fcache_nslots, fcache_nbytes);
        } else // no caching (CACHING_TYPE = 0)
            fcache_nslots = 0;
            fcache_nbytes = 0;
        fapl = H5Fget_access_plist(p_fbh5_ctx->file_id);
        if(fapl < 0)
            fbh5_warning(__FILE__, __LINE__, "fbh5_open: H5Fget_access_plist FAILED; using default caching");
        else {
            // https://portal.hdfgroup.org/display/HDF5/H5P_SET_CACHE
            status = H5Pset_cache(fapl, 
                                    0,                // ignored
                                    fcache_nslots,    // Hash table slot count
                                    fcache_nbytes,    // File cache size in bytes
                                    0.75);            // Chunk preemption policy
            if(status < 0)
                fbh5_warning(__FILE__, __LINE__, "fbh5_open: H5Pset_cache FAILED; using default caching");
        }
    }
 
    /*
     * Close dcpl handle.
     */
    status = H5Pclose(dcpl);
    if(status != 0)
        fbh5_warning(__FILE__, __LINE__, "fbh5_open: H5Pclose/dcpl FAILED\n");
    
    /*
     * Close fapl handle.
     */
    if(CACHING_TYPE != 2) {
        status = H5Pclose(fapl);
        if(status != 0)
            fbh5_warning(__FILE__, __LINE__, "fbh5_open: H5Pclose/fapl FAILED\n");
    }

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
