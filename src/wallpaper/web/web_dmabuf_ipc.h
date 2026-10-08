#ifndef WEB_DMABUF_IPC_H
#define WEB_DMABUF_IPC_H

#include <stdint.h>

namespace web_renderer {

// Sends a WebDmaBufOffer with the fds attached as SCM_RIGHTS ancillary data.
bool sendDmaBufOffer(int socket_fd, const int* fds, uint32_t count);

// Non-blocking receive of a DMA-BUF offer; the caller owns the fds. Returns 0 when none, -1 on error.
int receiveDmaBufOffer(int socket_fd, int* fds, uint32_t max_count);

}  // namespace web_renderer

#endif  // WEB_DMABUF_IPC_H
