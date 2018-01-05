#ifndef _RAWSPEC_SOCKET_H_
#define _RAWSPEC_SOCKET_H_

#include "rawspec.h"

#ifdef __cplusplus
extern "C" {
#endif

int open_output_socket(const char * host, const char * port);

#if 0
void set_socket_options(rawspec_context * ctx);
#endif

void dump_net_callback(
    rawspec_context * ctx, int output_product, int callback_type);

#ifdef __cplusplus
}
#endif

#endif // _RAWSPEC_SOCKET_H_
