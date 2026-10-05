#ifndef WEB_DMABUF_IPC_H
#define WEB_DMABUF_IPC_H

#include <stdint.h>

namespace web_renderer {

// Sends a WebDmaBufOffer with the fds attached as SCM_RIGHTS ancillary data.
bool sendDmaBufOffer(int socket_fd, const int* fds, uint32_t count);

// Receives a DMA-BUF offer without blocking. On success the received fds are
// written to fds[0..return-1] and owned by the caller. Returns 0 when nothing is
// pending and -1 on error.
int receiveDmaBufOffer(int socket_fd, int* fds, uint32_t max_count);

}  // namespace web_renderer

#endif  // WEB_DMABUF_IPC_H
