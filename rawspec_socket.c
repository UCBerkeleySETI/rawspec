#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include "rawspec_socket.h"
#include "rawspec_callback.h"

#define MIN_MTU (8600)
#define MAX_FLOATS_PER_PACKET (8192 / sizeof(float))

#define MIN(a,b) ((a < b) ? (a) : (b))

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

int open_output_socket(const char * host, const char * port)
{
  int rc;
  int sfd;
  unsigned int mtu = 0;
  socklen_t ss = sizeof(int);
  struct addrinfo hints;
  struct addrinfo * result;
  struct addrinfo * rp;

  // Obtain address(es) matching host/port

  memset(&hints, 0, sizeof(struct addrinfo));
  //hints.ai_family = AF_UNSPEC;    // Allow IPv4 or IPv6
  hints.ai_family = AF_INET;      // Allow IPv4 only (for now)
  hints.ai_socktype = SOCK_DGRAM; // Datagram socket
  hints.ai_flags = 0;
  hints.ai_protocol = 0;          // Any protocol

  rc = getaddrinfo(host, port, &hints, &result);
  if (rc != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
    return -1;
  }

  // getaddrinfo() returns a list of address structures.
  // Try each address until we successfully connect(2).
  // If socket(2) (or connect(2)) fails, we (close the socket
  // and) try the next address.

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype,
        rp->ai_protocol);
    if (sfd == -1)
      continue;

    if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
      break; // Success

    close(sfd);
  }

  // If no address succeeded
  if (rp == NULL) {
    fprintf(stderr, "could not open output socket\n");
    return -1;
  }

  freeaddrinfo(result); // No longer needed

  // Get MTU and make sure it's large enough.
  // At this point it is probably only local MTU, but it's a start.
  rc = getsockopt(sfd, IPPROTO_IP, IP_MTU, &mtu, &ss);
  if(rc < 0) {
    perror("getsockopt");
    fprintf(stderr, "warning: could not determine MTU\n");
  } else if(mtu < MIN_MTU) {
    fprintf(stderr, "MTU %u is too small, need at least %u\n", mtu, MIN_MTU);
    close(sfd);
    return -1;
  }

  // Set IP_MTU_DISCOVER socket option to IP_PMTUDISC_PROBE.  This sets the DF
  // (don't fragment) flag on outbound packets and ignores MTU changes.  This
  // is probably not essential.
  mtu = IP_PMTUDISC_PROBE;
  rc = setsockopt(sfd, IPPROTO_IP, IP_MTU_DISCOVER, &mtu, sizeof(int));
  if(rc < 0) {
    perror("setsockopt");
    fprintf(stderr,
        "warning: could not set IP_MTU_DISCOVER to IP_PMTUDISC_PROBE\n");
  }

  return sfd;
}

#if 0
void set_socket_options(rawspec_context * ctx)
{
  int i;
  int rc;
  int fd = -1;
  unsigned int bufsize = 0;
  socklen_t ss = sizeof(int);
  callback_data_t * cb_data = (callback_data_t *)ctx->user_data;

  // Calculate sum of all power buffer sizes.
  // TODO Be more selective if/when multiple sockets are used.
  for(i=0; i < ctx->No; i++) {
    bufsize += ctx->h_pwrbuf_size[i];
  }
  bufsize *= 2;

  for(i=0; i < ctx->No; i++) {
    if(fd != cb_data[i].fd) {
      fd = cb_data[i].fd;

      printf("setting socket send buffer to %u bytes\n", bufsize);

      rc = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(int));
      if(rc < 0) {
        perror("setsockopt");
      }

      rc = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, &ss);
      if(!rc) {
        printf("send buffer size is %d bytes\n", bufsize);
      }
    }
  }
}
#endif

void * dump_net_thread_func(void *arg)
{
  int i;
  char pkt[90000];
  char * ppkt = pkt;
  float * ppwr;
  int32_t data_offset;
  size_t pkt_size;
  double sec_per_packet;
  double modf_int;
  struct timespec sleep_time = {0, 0};
  uint64_t elapsed_ns=0;
  uint64_t pkt_ns=0;
  int64_t sleep_ns=0;
  struct timespec ts_start, ts_stop;

  callback_data_t * cb_data = (callback_data_t *)arg;
  fb_hdr_t * fb_hdr = &cb_data->fb_hdr;

  // Fields to remember from header
  int hdr_nchans = fb_hdr->nchans;
  double hdr_fch1 = fb_hdr->fch1;

  int channels_per_packet;
  int spectra_per_packet;
  int chan_remaining;
  int spec_remaining = cb_data->Nds;

  int pkt_nchan;
  int pkt_nspec;

  uint64_t total_bytes = 0;
  int total_packets = 0;
  int error_packets = 0;

  // Get time at start of output
  clock_gettime(CLOCK_MONOTONIC, &ts_start);

  if(hdr_nchans >= MAX_FLOATS_PER_PACKET) {
    channels_per_packet = MAX_FLOATS_PER_PACKET;
    spectra_per_packet = 1;
  } else {
    channels_per_packet = hdr_nchans;
    spectra_per_packet = MAX_FLOATS_PER_PACKET / hdr_nchans;
    if(spectra_per_packet > spec_remaining) {
      spectra_per_packet = spec_remaining;
    }
  }

#if defined(DEBUG_CALLBACKS) && DEBUG_CALLBACKS != 0
  if(cb_data->debug_callback) {
    fprintf(stderr,
        "tsamp %.4g * spec/pkt %u * chan/pkt %u / Nf %u = sec/pkt %.4g\n",
        fb_hdr->tsamp, spectra_per_packet, channels_per_packet, cb_data->Nf,
        sec_per_packet);
  }
#endif

  // Outer loop over all spectra for this dump
  while(spec_remaining) {
    pkt_nspec = MIN(spectra_per_packet, spec_remaining);

    // Restore header fch1
    fb_hdr->fch1 = hdr_fch1;

    // Set ppwr to start of current output spectrum
    ppwr = cb_data->h_pwrbuf + (cb_data->Nds - spec_remaining) * hdr_nchans;

    // Inner loop over all channels for this dump
    chan_remaining = hdr_nchans;
    while(chan_remaining) {
      pkt_nchan = MIN(channels_per_packet, chan_remaining);

      // Update header nchans
      fb_hdr->nchans = pkt_nchan;

      // Output header to packet buffer
      ppkt = fb_buf_write_header(pkt, fb_hdr);

      // Calculate data offset as next multiple of 512 in packet socket UDP
      // frame (which starts 0x6c bytes from start of page aligned frame).
      data_offset = 0x6c + (ppkt - pkt);
      if(data_offset % 512 != 0) {
        data_offset += 512 - (data_offset % 512);
      }
      // If data offset is beyond header
      if(data_offset > (0x6c + ppkt - pkt)) {
        // If data offset is less than 20 bytes beyond header
        if(data_offset < (0x6c + ppkt - pkt) + 20) {
          // Add 512 more bytes since less than 20 is not paddable
          data_offset += 512;
        }
        // Output padded header to packet buffer
        ppkt = fb_buf_write_padded_header(pkt, fb_hdr,
                                          data_offset - 0x6c);
      }

      // Copy spectra to buffer
      for(i=0; i<pkt_nspec; i++) {
        memcpy(ppkt, ppwr + i*hdr_nchans, pkt_nchan*sizeof(float));
        ppkt += pkt_nchan*sizeof(float);
      }

      // Send packet
      // TODO Handle EMSGSIZE errors from send()?
      total_packets++;
      pkt_size = ppkt - pkt;
      total_bytes += pkt_size;
      if(send(cb_data->fd, pkt, pkt_size, 0) == -1) {
        if(errno == ENOTCONN) {
          // ENOTCONN means that there is no listener on the receive side.
          // Eventually we might want to stop sending packets if there is
          // no remote listener, but for now we try to send packet again
          // in case the remote side is capturing packets with packet sockets
          // (e.g. hashpipe or libpcap/tcpdump).
          if(send(cb_data->fd, pkt, pkt_size, 0) == -1) {
            error_packets++;
          }
        }
      }

      clock_gettime(CLOCK_MONOTONIC, &ts_stop);
      elapsed_ns = ELAPSED_NS(ts_start, ts_stop);
      sleep_ns = ((int64_t)(8.0 * total_bytes / cb_data->rate)) - elapsed_ns;

      // Sleep to throttle output rate.  Wait for sleep time to build up to 100
      // usec to minimize oversleeping due to timer granularity problems.
      if(sleep_ns > 100*1000) {
        sleep_time.tv_sec  = sleep_ns / (1000*1000*1000);
        sleep_time.tv_nsec = sleep_ns % (1000*1000*1000);
        nanosleep(&sleep_time, NULL);
      }

      // Advance ppwr
      ppwr += pkt_nchan;

      // Update header fch1
      fb_hdr->fch1 += pkt_nchan * fb_hdr->foff;

      // Decrement chan_remaining
      chan_remaining -= pkt_nchan;

    } // Inner loop over all channels

    // Update header tstart
    fb_hdr->tstart += pkt_nspec * fb_hdr->tsamp / 86400.0;

    // Decrement spec_remaining
    spec_remaining -= pkt_nspec;

  } // Outer loop over all spectra

  // Restore fch1
  fb_hdr->fch1 = hdr_fch1;

  // Restore nchans
  fb_hdr->nchans = hdr_nchans;

  if(error_packets > 0) {
    printf("fine channels %10d: error packets %d/%d\n",
        cb_data->Nf, error_packets, total_packets);
  }

  // Final sleep to throttle output rate.  Don't worry about granularity for
  // this final sleep.
  clock_gettime(CLOCK_MONOTONIC, &ts_stop);
  elapsed_ns = ELAPSED_NS(ts_start, ts_stop);
      sleep_ns = ((int64_t)(8.0 * total_bytes / cb_data->rate)) - elapsed_ns;
  sleep_time.tv_sec  = sleep_ns / (1000*1000*1000);
  sleep_time.tv_nsec = sleep_ns % (1000*1000*1000);
  nanosleep(&sleep_time, NULL);

  clock_gettime(CLOCK_MONOTONIC, &ts_stop);
  elapsed_ns = ELAPSED_NS(ts_start, ts_stop);

#if defined(DEBUG_CALLBACKS) && DEBUG_CALLBACKS != 0
  if(cb_data->debug_callback) {
    cb_data->debug_callback--;
    fprintf(stderr,
        "fine channels %10d: output thread took %.6f ms elapsed (%.3f Gbps)\n",
        cb_data->Nf, elapsed_ns / 1e6, (8.0 * total_bytes) / elapsed_ns);
  }
#endif

  // Increment total spectra and total packets counters
  cb_data->total_spectra += cb_data->Nds;
  cb_data->total_packets += total_packets;
  cb_data->total_bytes += total_bytes;
  cb_data->total_ns += elapsed_ns;

  return NULL;
}

void dump_net_callback(
    rawspec_context * ctx,
    int output_product,
    int callback_type)
{
  int rc;
  callback_data_t * cb_data =
      &((callback_data_t *)ctx->user_data)[output_product];

  if(callback_type == RAWSPEC_CALLBACK_PRE_DUMP) {
    if(cb_data->output_thread_valid) {
      // Join output thread
      if((rc=pthread_join(cb_data->output_thread, NULL))) {
        fprintf(stderr, "pthread_join: %s\n", strerror(rc));
      }
      // Flag thread as invalid
      cb_data->output_thread_valid = 0;
    }
  } else if(callback_type == RAWSPEC_CALLBACK_POST_DUMP) {
    // Create output thread
    if((rc=pthread_create(&cb_data->output_thread, NULL,
                      dump_net_thread_func, cb_data))) {
      fprintf(stderr, "pthread_create: %s\n", strerror(rc));
    } else {
      cb_data->output_thread_valid = 1;
    }
  }
}
