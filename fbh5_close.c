/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * fbh5_close.c                                                                *
 * ----------                                                                  *
 * Close an FBH5 writing session:                          .                   *
 * Dataspace, Dataset, and File (in that order).                               *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include "fbh5_defs.h"
#define MILLION 1000000.0


/***
	Main entry point.
***/
void fbh5_close(fbh5_context_t * p_fbh5_ctx, int debug_callback) {
    herr_t      status;         // Status from HDF5 function call
    hsize_t     sz_store;       // Storage size
    double      MiBstore;       // sz_store converted to MiB
    double      MiBlogical;     // sz_store converted to MiB
    
    sz_store = H5Dget_storage_size(p_fbh5_ctx->dataset_id);
    MiBlogical = (double) p_fbh5_ctx->tint_size * (double) p_fbh5_ctx->offset_dims[0] / MILLION;
    
    /*
     * Attach "dimension scale" labels.
     */
    fbh5_set_ds_label(p_fbh5_ctx, "time", 0, debug_callback);
    fbh5_set_ds_label(p_fbh5_ctx, "feed_id", 1, debug_callback);
    fbh5_set_ds_label(p_fbh5_ctx, "frequency", 2, debug_callback);

    /*
     * Close dataspace.
     */
    status = H5Sclose(p_fbh5_ctx->dataspace_id);
    if(status != 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_close H5Sclose dataspace FAILED\n");
        
    /*
     * Close dataset.
     */
    status = H5Dclose(p_fbh5_ctx->dataset_id);
    if(status != 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_close H5Dclose dataset 'data' FAILED\n");
        
    /*
     * Close file.
     */
    status = H5Fclose(p_fbh5_ctx->file_id);
    if(status != 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_close H5Fclose FAILED\n");
    p_fbh5_ctx->active = 0;

    /*
     * Closing statistics.
     */
    if(debug_callback) {
        printf("fbh5_close: Context closed.\n");
        printf("fbh5_close: %ld dumps processed.\n", p_fbh5_ctx->dump_count);
        printf("fbh5_close: %lld time integrations processed.\n", p_fbh5_ctx->offset_dims[0]);
        MiBstore = (double) sz_store / MILLION;
        printf("fbh5_close: Compressed %.2f MiB --> %.2f MiB\n", MiBlogical, MiBstore);
    }

    /*
     * Bye-bye.
     */
}
