#include "wallpaper/web/web_dmabuf_ipc.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>

#include "wallpaper/web/web_ipc.h"

namespace web_renderer {

bool sendDmaBufOffer(int socket_fd, const int* fds, uint32_t count) {
    WebDmaBufOffer offer = {WEB_MSG_DMABUF_OFFER, count};

    struct iovec io = {&offer, sizeof(offer)};
    struct msghdr message = {};
    message.msg_iov = &io;
    message.msg_iovlen = 1;

    char control[CMSG_SPACE(sizeof(int) * kWebDmaBufBuffers)] = {};
    if (count > 0) {
        message.msg_control = control;
        message.msg_controllen = CMSG_SPACE(sizeof(int) * count);
        struct cmsghdr* header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(int) * count);
        memcpy(CMSG_DATA(header), fds, sizeof(int) * count);
    }
    return sendmsg(socket_fd, &message, MSG_NOSIGNAL) == static_cast<ssize_t>(sizeof(offer));
}

int receiveDmaBufOffer(int socket_fd, int* fds, uint32_t max_count) {
    WebDmaBufOffer offer = {};

    struct iovec io = {&offer, sizeof(offer)};
    struct msghdr message = {};
    message.msg_iov = &io;
    message.msg_iovlen = 1;

    char control[CMSG_SPACE(sizeof(int) * kWebDmaBufBuffers)] = {};
    message.msg_control = control;
    message.msg_controllen = sizeof(control);

    const ssize_t received = recvmsg(socket_fd, &message, MSG_DONTWAIT);
    if (received < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    if (received != static_cast<ssize_t>(sizeof(offer)) || offer.type != WEB_MSG_DMABUF_OFFER) return -1;

    int count = 0;
    for (struct cmsghdr* header = CMSG_FIRSTHDR(&message); header; header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS) continue;
        const int attached = static_cast<int>((header->cmsg_len - CMSG_LEN(0)) / sizeof(int));
        const int* data = reinterpret_cast<const int*>(CMSG_DATA(header));
        for (int i = 0; i < attached && count < static_cast<int>(max_count); ++i) {
            fds[count++] = data[i];
        }
    }
    return count;
}

}  // namespace web_renderer
