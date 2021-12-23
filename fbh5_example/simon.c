/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * simon.c                                                                     *
 * -------                                                                     *
 * Sample fbh5 application.                                .                   *
 *                                                                             *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <fbh5_defs.h>
#include <rawspec_callback.h>
#include <rawspec_fbutils.h>


#define DEBUG_CALLBACK  0
#define NBITS           32
#define NCHANS          1048576
#define NFPC            1
#define NIFS            1
#define NTINTS          16
#define PATH_H5         "./simon.h5"


/***
	Initialize metadata to Voyager 1 values.
***/
void make_voyager_1_metadata(callback_data_t *cb_data) {
    memset(&(cb_data->fb_hdr), 0, sizeof(fb_hdr_t));    
    cb_data->fb_hdr.az_start = 0.0;
    cb_data->fb_hdr.data_type = 1;
    cb_data->fb_hdr.fch1 = 8421.386717353016;       // MHz
    cb_data->fb_hdr.foff = -2.7939677238464355e-06; // MHz
    cb_data->fb_hdr.ibeam = 1;
    cb_data->fb_hdr.machine_id = 42;
    cb_data->fb_hdr.nbeams = 1;
    cb_data->fb_hdr.nchans = NCHANS;            // # of fine channels
    cb_data->fb_hdr.nfpc = NFPC;                // # of fine channels per coarse channel
    cb_data->fb_hdr.nifs = NIFS;                // # of feeds (E.g. polarisations)
    cb_data->fb_hdr.nbits = 32;                 // 4 bytes i.e. float32
    cb_data->fb_hdr.src_raj = 171003.984;       // 17:12:40.481
    cb_data->fb_hdr.src_dej = 121058.8;         // 12:24:13.614
    cb_data->fb_hdr.telescope_id = 6;           // GBT
    cb_data->fb_hdr.tsamp = 18.253611008;       // seconds
    cb_data->fb_hdr.tstart = 57650.78209490741; // 2020-07-16T22:13:56.000
    cb_data->fb_hdr.za_start = 0.0;

    strcpy(cb_data->fb_hdr.source_name, "Voyager1");
    strcpy(cb_data->fb_hdr.rawdatafile, "guppi_57650_67573_Voyager1_0002.0000.raw");
}


void fatal_error(int linenum, char * msg) {
    fprintf(stderr, "\n*** simon: FATAL ERROR at line %d :: %s.\n", linenum, msg);
    exit(86);
}


/***
	Main entry point.
***/
int main(int argc, char **argv) {

    long            itime, jfreq;   // Loop controls for dummy spectra creation
    size_t          sz_alloc = 0;   // size of data matrix to allocate from the heap
    char            wstr[256];      // sprintf target
    float           *p_data;        // pointer to allocated heap
    float           *wfptr;         // working float pointer
    time_t          time1, time2;   // elapsed time calculation (seconds)

    // Data generation variables.
    float           low = 4.0e9, high = 9.0e9; // Element value boundaries
    float           freq = 8000.0e6;
    float           float_elem;         // Current float_element value
    unsigned long   count_elems;        // Elemount count
    float           amplitude;
    
    /*
     * Allocate enough heap for the entire data matrix.
     */
    sz_alloc = NTINTS * NIFS * NCHANS * NBITS / 8;
    p_data = malloc(sz_alloc);
    if(p_data == NULL) {
        sprintf(wstr, "main malloc(%ld) FAILED", (long)sz_alloc);
        fatal_error(__LINE__, wstr);
        exit(86);
    }
    printf("simon: Data matrix allocated, size  = %ld\n", (long) sz_alloc);

    /*
     * Make dummy spectra matrix.
     */
    wfptr = (float *) p_data;
    count_elems = 0;
	for(itime = 0; itime < NTINTS; itime++)
        for(jfreq = 0; jfreq < NCHANS; jfreq++) {
            amplitude = random() * (high - low);
            float_elem = amplitude * sin(freq);	        
            *wfptr++ = float_elem;
            count_elems++;
        }
    printf("simon: Matrix element count = %ld\n", count_elems);
	        
    /*
     * Create the callback data.
     */
    callback_data_t cb_data;
    memset(&cb_data, 0, sizeof(cb_data));
    make_voyager_1_metadata(&cb_data);
    cb_data.debug_callback = DEBUG_CALLBACK;    
    cb_data.h_pwrbuf = p_data;
    cb_data.h_pwrbuf_size = sz_alloc;
    printf("simon: Callback data ready.\n");
    
    /*
     * Create/recreate the file and store the metadata.
     */
    time(&time1);
    if(fbh5_open(&(cb_data.fbh5_ctx_ics), &(cb_data.fb_hdr), PATH_H5, cb_data.debug_callback) != 0) {
        fatal_error(__LINE__, "fbh5_open failed");
        exit(86);
    }

    /*
     * Write data.
     */
    for(int ii = 0; ii < NTINTS; ++ii)
        if(fbh5_write(&(cb_data.fbh5_ctx_ics), &(cb_data.fb_hdr), cb_data.h_pwrbuf, cb_data.h_pwrbuf_size, cb_data.debug_callback) != 0) {
            fatal_error(__LINE__, "fbh5_write failed");
            exit(86);
        }

    /*
     * Close FBH5 session.
     */
    if(fbh5_close(&(cb_data.fbh5_ctx_ics), cb_data.debug_callback) != 0) {
        fatal_error(__LINE__, "fbh5_close failed");
        exit(86);
    }

    /*
     * Compute elapsed time.    
     */
    time(&time2);
    free(p_data);
    printf("simon: End, e.t. = %.2f seconds.\n", difftime(time2, time1));
 	        
    /*
     * Bye-bye.
     */
    return 0;
}

