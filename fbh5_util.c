/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * fbh5_util.c                                                                 *
 * -----------                                                                 *
 * Utility functions                                       .                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include "fbh5_defs.h"


/***
	Report bad news, cleanup, and exit to OS
***/
void fbh5_oops(char * srcfile, int linenum, char * msg) {
	fprintf(stderr, "\n*** OOPS (fbh5), fatal error detected in %s at line %d !!\n*** %s !!\n\n", srcfile, linenum, msg);
	exit(86);
}


/***
	Set a file-level or dataset-level attribute in string-value format.
***/
void fbh5_set_str_attr(hid_t file_or_dataset_id, char * tag, char * value, int debug_callback) {
    herr_t status;
    hid_t id_scalar, atype, id_attr;
   
    if(debug_callback)
        printf("fbh5_set_str_attr: %s = %s\n", tag, value);
    id_scalar  = H5Screate(H5S_SCALAR);
    atype = H5Tcopy(H5T_C_S1);  // attr type = string
    H5Tset_size(atype, strlen(value));  // set create the string
    H5Tset_strpad(atype, H5T_STR_NULLTERM);
    id_attr = H5Acreate(file_or_dataset_id, tag, atype, id_scalar, H5P_DEFAULT, H5P_DEFAULT);
    status = H5Awrite(id_attr, atype, value); 
    if(status != 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_set_str_attr H5Awrite set_file_str_attr FAILED");
    status = H5Aclose(id_attr);    
    if(status != 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_set_str_attr H5Aclose set_file_str_attr FAILED");
}


/***
	Set a dataset-level attribute in double-value format.
***/
void fbh5_set_dataset_double_attr(hid_t dataset_id, char * tag, double * p_value, int debug_callback) {
    herr_t status;
    hid_t id_attr, id_scalar;
    
    if(debug_callback)
        printf("fbh5_set_dataset_double_attr: %s = %f\n", tag, *p_value);
    id_scalar  = H5Screate(H5S_SCALAR);
    if(id_scalar < 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_set_dataset_double_attr H5Screate FAILED");
    id_attr = H5Acreate2(dataset_id, tag, H5T_NATIVE_DOUBLE, id_scalar, H5P_DEFAULT, H5P_DEFAULT);
    if(id_attr < 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_set_dataset_double_attr H5Acreate2 FAILED");
    status = H5Awrite(id_attr, H5T_NATIVE_DOUBLE, p_value);
    if(status != 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_set_dataset_double_attr H5Awrite FAILED");
    status = H5Aclose(id_attr);
    if(status != 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_set_dataset_double_attr H5Aclose FAILED");
}


/***
	Set a dataset-level attribute in int-value format.
***/
void fbh5_set_dataset_int_attr(hid_t dataset_id, char * tag, int * p_value, int debug_callback) {
    herr_t status;
    hid_t id_attr, id_scalar;
    
    if(debug_callback)
        printf("fbh5_set_dataset_int_attr: %s = %d\n", tag, *p_value);
    id_scalar  = H5Screate(H5S_SCALAR);
    if(id_scalar < 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_set_dataset_int_attr H5Screate FAILED");
    id_attr = H5Acreate2(dataset_id, tag, H5T_NATIVE_INT, id_scalar, H5P_DEFAULT, H5P_DEFAULT);
    if(id_attr < 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_set_dataset_int_attr H5Acreate2 FAILED");
    status = H5Awrite(id_attr, H5T_NATIVE_INT, p_value);
    if(status != 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_set_dataset_int_attr H5Awrite FAILED");
    status = H5Aclose(id_attr);
    if(status != 0)
        fbh5_oops(__FILE__, __LINE__, "fbh5_set_dataset_int_attr H5Aclose FAILED");
}


/***
	Write metadata to FBH5 file dataset.
***/
void fbh5_write_metadata(hid_t dataset_id, fb_hdr_t *p_md, int debug_callback) {
    fbh5_set_dataset_int_attr(dataset_id, "machine_id", &(p_md->machine_id), debug_callback);
    fbh5_set_dataset_int_attr(dataset_id, "telescope_id", &(p_md->telescope_id), debug_callback);
    fbh5_set_dataset_int_attr(dataset_id, "data_type", &(p_md->data_type), debug_callback);
    fbh5_set_dataset_int_attr(dataset_id, "nchans", &(p_md->nchans), debug_callback);
    fbh5_set_dataset_int_attr(dataset_id, "nfpc", &(p_md->nfpc), debug_callback);
    fbh5_set_dataset_int_attr(dataset_id, "nbeams", &(p_md->nbeams), debug_callback);
    fbh5_set_dataset_int_attr(dataset_id, "ibeam", &(p_md->ibeam), debug_callback);
    fbh5_set_dataset_int_attr(dataset_id, "nbits", &(p_md->nbits), debug_callback);
    fbh5_set_dataset_int_attr(dataset_id, "nifs", &(p_md->nifs), debug_callback);
    fbh5_set_dataset_double_attr(dataset_id, "src_raj", &(p_md->src_raj), debug_callback);
    fbh5_set_dataset_double_attr(dataset_id, "src_dej", &(p_md->src_dej), debug_callback);
    fbh5_set_dataset_double_attr(dataset_id, "az_start", &(p_md->az_start), debug_callback);
    fbh5_set_dataset_double_attr(dataset_id, "za_start", &(p_md->za_start), debug_callback);
    fbh5_set_dataset_double_attr(dataset_id, "fch1", &(p_md->fch1), debug_callback);
    fbh5_set_dataset_double_attr(dataset_id, "foff", &(p_md->foff), debug_callback);
    fbh5_set_dataset_double_attr(dataset_id, "tstart", &(p_md->tstart), debug_callback);
    fbh5_set_dataset_double_attr(dataset_id, "tsamp", &(p_md->tsamp), debug_callback);
    fbh5_set_str_attr(dataset_id, "source_name", &(p_md->source_name[0]), debug_callback);
    fbh5_set_str_attr(dataset_id, "rawdatafile", &(p_md->rawdatafile[0]), debug_callback);
}

/***
	Set up a dimension scale label.
	
	* Create a secondary dataset, dscale_id.
	* Attach dscale_id to the file's primary dataset as a Dimension Scale label.
***/
void fbh5_set_ds_label(fbh5_context_t * p_fbh5_ctx, char * label, int dims_index, int debug_callback) {
    herr_t status;
    char wstr[256];

    if(debug_callback)
        printf("fbh5_set_ds_label: label = %s, dims_index = %d\n", label, dims_index);
    status = H5DSset_label(p_fbh5_ctx->dataset_id,  // Dataset ID
                              dims_index,           // Dimension index to which dscale_id applies to
                              label);               // Label
    if(status < 0) {
        sprintf(wstr, "fbh5_set_ds_label H5DSset_label FAILED (%s)", label);
        fbh5_oops(__FILE__, __LINE__, wstr);
    }
}

