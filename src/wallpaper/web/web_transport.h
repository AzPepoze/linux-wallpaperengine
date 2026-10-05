#ifndef WEB_TRANSPORT_H
#define WEB_TRANSPORT_H

#include <string>

// Frame transports the web renderer can use.
enum class WebTransport {
    Auto,      // try DmaBuf, then OffScreen, then Snapshot
    DmaBuf,    // zero-copy DMA-BUF sampled by the engine
    OffScreen, // offscreen render-control with asynchronous readback
    Snapshot,  // in-process widget grab
};

// false when the name is not one of the four.
inline bool parseWebTransport(const std::string& name, WebTransport& out) {
    if (name == "auto") {
        out = WebTransport::Auto;
        return true;
    }
    if (name == "dma-buf") {
        out = WebTransport::DmaBuf;
        return true;
    }
    if (name == "off-screen") {
        out = WebTransport::OffScreen;
        return true;
    }
    if (name == "snapshot") {
        out = WebTransport::Snapshot;
        return true;
    }
    return false;
}

// Canonical spelling, used on the command line and between processes.
inline const char* webTransportName(WebTransport transport) {
    switch (transport) {
        case WebTransport::DmaBuf:
            return "dma-buf";
        case WebTransport::OffScreen:
            return "off-screen";
        case WebTransport::Snapshot:
            return "snapshot";
        case WebTransport::Auto:
            break;
    }
    return "auto";
}

#endif  // WEB_TRANSPORT_H
