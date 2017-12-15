#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "rawspec_socket.h"
#include "rawspec_callback.h"

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
  // TODO
}
