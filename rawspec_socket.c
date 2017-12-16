#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include "rawspec_socket.h"
#include "rawspec_callback.h"

#define MAX_FLOATS_PER_PACKET (8192 / sizeof(float))

#define MIN(a,b) ((a < b) ? (a) : (b))

int open_output_socket(const char * host, const char * port)
{
  int rc;
  int sfd;
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

  return sfd;
}

void dump_net_callback(rawspec_context * ctx, int output_product)
{
  // This should use MAX_OUTPUTS, but that makes
  // initialization of this static array harder.
  static int first[4] = {1, 1, 1, 1};

  int i;
  char pkt[90000];
  char * ppkt = pkt;
  float * ppwr;

  callback_data_t * cb_data =
    &((callback_data_t *)ctx->user_data)[output_product];

  fb_hdr_t * fb_hdr = &cb_data->fb_hdr;

  // Fields to remember from header
  int hdr_nchans = fb_hdr->nchans;
  int hdr_fch1   = fb_hdr->fch1;

  int channels_per_packet;
  int spectra_per_packet;
  int chan_remaining;
  int spec_remaining = ctx->Nds[output_product];

  int pkt_nchan;
  int pkt_nspec;

  int total_packets = 0;
  int error_packets = 0;

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

  if(first[output_product]) {
    fprintf(stderr, "output product %d: chan_per_pkt %4d spec_per_pkt %d\n",
        output_product, channels_per_packet, spectra_per_packet);
  }

  // Outer loop over all spectra for this dump
  while(spec_remaining) {
    pkt_nspec = MIN(spectra_per_packet, spec_remaining);

    // Restore header fch1
    fb_hdr->fch1 = hdr_fch1;

    // Set ppwr to start of current output spectrum
    ppwr = ctx->h_pwrbuf[output_product]
         + (ctx->Nds[output_product] - spec_remaining) * hdr_nchans;

    // Inner loop over all channels for this dump
    chan_remaining = hdr_nchans;
    while(chan_remaining) {
      pkt_nchan = MIN(channels_per_packet, chan_remaining);

      // Update header nchans
      fb_hdr->nchans = pkt_nchan;

      // Output header to packet buffer
      ppkt = fb_buf_write_header(pkt, fb_hdr);

      // Copy spectra to buffer
      for(i=0; i<pkt_nspec; i++) {
        memcpy(ppkt, ppwr + i*hdr_nchans, pkt_nchan*sizeof(float));
        ppkt += pkt_nchan*sizeof(float);
      }

      // Send packet
      total_packets++;
      if(send(cb_data->fd, pkt, ppkt-pkt, 0) == -1) {
        if(errno == ENOTCONN) {
          // ENOTCONN means that there is no listener on the receive side.
          // Eventually we might want to stop sending packets if there is
          // no remote listener, but for now we try to send packet again
          // (in case the remote side is capturing packets with tcpdump).
          if(send(cb_data->fd, pkt, ppkt-pkt, 0) == -1) {
            error_packets++;
          }
        }
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
    printf("output product %d: error packets %d/%d\n",
        output_product, error_packets, total_packets);
  }

  first[output_product] = 0;
}
