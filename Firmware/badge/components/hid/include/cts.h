#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Read the host's Current Time Service over an encrypted connection and set the badge clock from it (iOS only). */
void cts_sync(uint16_t conn_handle);

#ifdef __cplusplus
}
#endif
