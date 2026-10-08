#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "shared/core/build_config.h"
#include "shared/media/media_session.h"
#include "shared/media/mpris_metadata.h"
#include "shared/media/thumbnail_colors.h"

#if LWE_MPRIS
#include <systemd/sd-bus.h>

#include <cctype>

#include "shared/core/logger.h"
#include "stb_image.h"
#endif

namespace wallpaper_engine {

namespace {
std::mutex g_thumbnail_fallback_mutex;
std::string g_thumbnail_fallback_path;
}  // namespace

// Artwork for $mediaThumbnail when the track has no cover; set by the app before the source starts.
void setMediaThumbnailFallbackImage(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_thumbnail_fallback_mutex);
    g_thumbnail_fallback_path = path;
}

#if LWE_MPRIS

namespace {

constexpr const char* kMprisPrefix = "org.mpris.MediaPlayer2.";
constexpr std::size_t kMprisPrefixLength = 23;
constexpr const char* kMprisPath = "/org/mpris/MediaPlayer2";
constexpr const char* kPlayerInterface = "org.mpris.MediaPlayer2.Player";
constexpr const char* kPropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr const char* kNameOwnerMatch =
    "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
    "member='NameOwnerChanged',arg0namespace='org.mpris.MediaPlayer2'";
constexpr const char* kPropertiesMatch =
    "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',"
    "path='/org/mpris/MediaPlayer2',arg0='org.mpris.MediaPlayer2.Player'";

struct Player {
    std::string service;
    // Unique bus name that owns `service`; signals carry this as their sender, not the well-known name.
    std::string owner;
    PlaybackState state = PlaybackState::Stopped;
    MediaProperties props;
    std::string art_url;
    double position = 0.0;
    double duration = 0.0;
    uint64_t stamp = 0;
    bool published = false;
    PlaybackState emitted_state = PlaybackState::Stopped;
    std::string emitted_metadata;
    std::string emitted_art;
};

PlaybackState parsePlaybackState(const char* value) {
    if (value && std::strcmp(value, "Playing") == 0) return PlaybackState::Playing;
    if (value && std::strcmp(value, "Paused") == 0) return PlaybackState::Paused;
    return PlaybackState::Stopped;
}

void readStringValue(sd_bus_message* message, std::string& out) {
    const char* value = nullptr;
    if (sd_bus_message_read(message, "s", &value) > 0 && value) out = value;
}

void readStringArrayValue(sd_bus_message* message, std::string& out) {
    std::string joined;
    if (sd_bus_message_enter_container(message, 'a', nullptr) > 0) {
        const char* value = nullptr;
        while (sd_bus_message_read(message, "s", &value) > 0) {
            if (!value) continue;
            if (!joined.empty()) joined += ", ";
            joined += value;
        }
        sd_bus_message_exit_container(message);
    } else {
        readStringValue(message, joined);
    }
    out = joined;
}

void readMetadataDict(sd_bus_message* message, Player& player) {
    while (sd_bus_message_enter_container(message, 'e', "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read(message, "s", &key) < 0) break;
        if (sd_bus_message_enter_container(message, 'v', nullptr) > 0) {
            switch (classifyMprisMetadataKey(key)) {
                case MprisMetadataField::Title:
                    readStringValue(message, player.props.title);
                    break;
                case MprisMetadataField::Artist:
                    readStringArrayValue(message, player.props.artist);
                    break;
                case MprisMetadataField::Album:
                    readStringValue(message, player.props.albumTitle);
                    break;
                case MprisMetadataField::AlbumArtist:
                    readStringArrayValue(message, player.props.albumArtist);
                    break;
                case MprisMetadataField::Genre:
                    readStringArrayValue(message, player.props.genres);
                    break;
                case MprisMetadataField::Length: {
                    int64_t micros = 0;
                    if (sd_bus_message_read(message, "x", &micros) > 0)
                        player.duration = static_cast<double>(micros) / 1e6;
                    break;
                }
                case MprisMetadataField::ArtUrl:
                    readStringValue(message, player.art_url);
                    break;
                case MprisMetadataField::Ignore:
                default:
                    sd_bus_message_skip(message, nullptr);
                    break;
            }
            sd_bus_message_exit_container(message);
        } else {
            sd_bus_message_skip(message, nullptr);
        }
        sd_bus_message_exit_container(message);
    }
}

void readPropertiesDict(sd_bus_message* message, Player& player) {
    while (sd_bus_message_enter_container(message, 'e', "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read(message, "s", &key) < 0) break;
        if (sd_bus_message_enter_container(message, 'v', nullptr) > 0) {
            if (key && std::strcmp(key, "PlaybackStatus") == 0) {
                const char* value = nullptr;
                if (sd_bus_message_read(message, "s", &value) > 0) player.state = parsePlaybackState(value);
            } else if (key && std::strcmp(key, "Metadata") == 0) {
                if (sd_bus_message_enter_container(message, 'a', "{sv}") > 0) {
                    readMetadataDict(message, player);
                    sd_bus_message_exit_container(message);
                } else {
                    sd_bus_message_skip(message, nullptr);
                }
            } else if (key && std::strcmp(key, "Position") == 0) {
                int64_t micros = 0;
                if (sd_bus_message_read(message, "x", &micros) > 0) player.position = static_cast<double>(micros) / 1e6;
            } else {
                sd_bus_message_skip(message, nullptr);
            }
            sd_bus_message_exit_container(message);
        } else {
            sd_bus_message_skip(message, nullptr);
        }
        sd_bus_message_exit_container(message);
    }
}

std::string percentDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const bool escape = text[i] == '%' && i + 2 < text.size() &&
                            std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
                            std::isxdigit(static_cast<unsigned char>(text[i + 2]));
        if (escape) {
            out.push_back(static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

// Loads the configured fallback artwork on demand; empty when unset or undecodable.
ThumbnailColors loadFallbackThumbnail() {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(g_thumbnail_fallback_mutex);
        path = g_thumbnail_fallback_path;
    }
    if (path.empty()) return {};

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) return {};

    ThumbnailColors thumbnail = extractThumbnailColors(pixels, width, height);
    stbi_image_free(pixels);
    return thumbnail;
}

}  // namespace

class MprisMediaSource : public MediaSource {
   public:
    void start() override {
        if (running_.exchange(true)) return;
        worker_ = std::thread(&MprisMediaSource::run, this);
    }

    void stop() override {
        if (!running_.exchange(false)) return;
        if (worker_.joinable()) worker_.join();
    }

    std::vector<MediaEvent> poll() override {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::vector<MediaEvent> events;
        events.swap(queue_);
        return events;
    }

   private:
    void run();
    void listPlayers();
    void addPlayer(const std::string& service);
    void removePlayer(const std::string& service);
    void processPendingRefreshes();
    bool applyPlayerProperties(Player& player);
    double readPosition(const std::string& service);
    void reselect();
    void publish(Player& player);
    void emitThumbnail(Player& player);
    const ThumbnailColors& fallbackThumbnail();
    void emitTimeline();
    void emit(MediaEvent event);

    static int onNameOwnerChanged(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int onPropertiesChanged(sd_bus_message* message, void* userdata, sd_bus_error* error);

    sd_bus* bus_ = nullptr;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::mutex queue_mutex_;
    std::vector<MediaEvent> queue_;
    std::map<std::string, Player> players_;
    std::set<std::string> pending_refresh_;
    std::string selected_;
    uint64_t stamp_counter_ = 0;
    bool http_art_warned_ = false;
    ThumbnailColors fallback_thumbnail_;
};

void MprisMediaSource::emit(MediaEvent event) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.push_back(std::move(event));
}

void MprisMediaSource::run() {
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) return;
    bus_ = bus;

    sd_bus_add_match(bus, nullptr, kNameOwnerMatch, &MprisMediaSource::onNameOwnerChanged, this);
    sd_bus_add_match(bus, nullptr, kPropertiesMatch, &MprisMediaSource::onPropertiesChanged, this);

    MediaEvent status;
    status.kind = MediaEvent::Kind::Status;
    status.enabled = true;
    emit(std::move(status));

    listPlayers();

    auto next_timeline = std::chrono::steady_clock::now();
    while (running_.load()) {
        const int processed = sd_bus_process(bus, nullptr);
        if (processed < 0) break;
        if (processed == 0) sd_bus_wait(bus, 100 * 1000);

        processPendingRefreshes();

        const auto now = std::chrono::steady_clock::now();
        if (now >= next_timeline) {
            next_timeline = now + std::chrono::seconds(1);
            emitTimeline();
        }
    }

    bus_ = nullptr;
    sd_bus_flush_close_unref(bus);
}

void MprisMediaSource::listPlayers() {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    if (sd_bus_call_method(bus_, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames",
                           &error, &reply, nullptr) < 0) {
        sd_bus_error_free(&error);
        return;
    }

    if (sd_bus_message_enter_container(reply, 'a', "s") > 0) {
        const char* name = nullptr;
        while (sd_bus_message_read(reply, "s", &name) > 0) {
            if (name && std::strncmp(name, kMprisPrefix, kMprisPrefixLength) == 0) addPlayer(name);
        }
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);
}

bool MprisMediaSource::applyPlayerProperties(Player& player) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    if (sd_bus_call_method(bus_, player.service.c_str(), kMprisPath, kPropertiesInterface, "GetAll", &error, &reply,
                           "s", kPlayerInterface) < 0) {
        sd_bus_error_free(&error);
        return false;
    }

    if (const char* owner = sd_bus_message_get_sender(reply)) player.owner = owner;
    if (sd_bus_message_enter_container(reply, 'a', "{sv}") > 0) {
        readPropertiesDict(reply, player);
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);

    sd_bus_error position_error = SD_BUS_ERROR_NULL;
    int64_t micros = 0;
    if (sd_bus_get_property_trivial(bus_, player.service.c_str(), kMprisPath, kPlayerInterface, "Position",
                                    &position_error, 'x', &micros) >= 0) {
        player.position = static_cast<double>(micros) / 1e6;
    }
    sd_bus_error_free(&position_error);
    return true;
}

double MprisMediaSource::readPosition(const std::string& service) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int64_t micros = 0;
    const int result = sd_bus_get_property_trivial(bus_, service.c_str(), kMprisPath, kPlayerInterface, "Position",
                                                   &error, 'x', &micros);
    sd_bus_error_free(&error);
    return result < 0 ? 0.0 : static_cast<double>(micros) / 1e6;
}

void MprisMediaSource::addPlayer(const std::string& service) {
    Player& player = players_[service];
    player.service = service;
    pending_refresh_.insert(service);
}

void MprisMediaSource::removePlayer(const std::string& service) {
    players_.erase(service);
    pending_refresh_.erase(service);
    reselect();
}

// Fills properties for newly seen players outside the signal handlers, so no bus call runs mid-dispatch.
void MprisMediaSource::processPendingRefreshes() {
    if (pending_refresh_.empty()) return;
    for (const std::string& service : pending_refresh_) {
        auto player = players_.find(service);
        if (player == players_.end()) continue;
        if (applyPlayerProperties(player->second)) player->second.stamp = ++stamp_counter_;
    }
    pending_refresh_.clear();
    reselect();
}

void MprisMediaSource::reselect() {
    std::string best;
    int best_rank = -1;
    uint64_t best_stamp = 0;
    for (const auto& [name, player] : players_) {
        const int rank = player.state == PlaybackState::Playing ? 1 : 0;
        if (rank > best_rank || (rank == best_rank && player.stamp > best_stamp)) {
            best = name;
            best_rank = rank;
            best_stamp = player.stamp;
        }
    }

    if (best != selected_) {
        selected_ = best;
        auto selected = players_.find(selected_);
        if (selected != players_.end()) selected->second.published = false;
    }

    auto selected = players_.find(selected_);
    if (selected != players_.end()) publish(selected->second);
}

void MprisMediaSource::publish(Player& player) {
    if (!player.published || player.state != player.emitted_state) {
        MediaEvent event;
        event.kind = MediaEvent::Kind::Playback;
        event.enabled = true;
        event.state = player.state;
        event.position = player.position;
        event.duration = player.duration;
        emit(std::move(event));
        player.emitted_state = player.state;
    }

    const std::string metadata = player.props.title + '\x1f' + player.props.artist + '\x1f' + player.props.albumTitle +
                                 '\x1f' + player.props.albumArtist + '\x1f' + player.props.subTitle + '\x1f' +
                                 player.props.genres + '\x1f' + player.props.contentType + '\x1f' +
                                 std::to_string(player.duration);
    if (!player.published || metadata != player.emitted_metadata) {
        MediaEvent event;
        event.kind = MediaEvent::Kind::Properties;
        event.enabled = true;
        event.state = player.state;
        event.properties = player.props;
        event.position = player.position;
        event.duration = player.duration;
        emit(std::move(event));
        player.emitted_metadata = metadata;
    }

    if (!player.published || player.art_url != player.emitted_art) {
        player.emitted_art = player.art_url;
        emitThumbnail(player);
    }

    player.published = true;
}

const ThumbnailColors& MprisMediaSource::fallbackThumbnail() {
    // Retry until the app has configured the fallback path and it decodes.
    if (!fallback_thumbnail_.has_thumbnail) fallback_thumbnail_ = loadFallbackThumbnail();
    return fallback_thumbnail_;
}

void MprisMediaSource::emitThumbnail(Player& player) {
    MediaEvent event;
    event.kind = MediaEvent::Kind::Thumbnail;
    event.enabled = true;
    event.state = player.state;

    bool loaded = false;
    if (player.art_url.rfind("file://", 0) == 0) {
        const std::string path = percentDecode(player.art_url.substr(7));
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (pixels) {
            event.thumbnail = extractThumbnailColors(pixels, width, height);
            stbi_image_free(pixels);
            loaded = event.thumbnail.has_thumbnail;
        }
    } else if (player.art_url.rfind("http://", 0) == 0 || player.art_url.rfind("https://", 0) == 0) {
        if (!http_art_warned_) {
            LOG_W("skipping remote art URL (no HTTP support): %s", player.art_url.c_str());
            http_art_warned_ = true;
        }
    }

    // No artwork: fall back to WE's placeholder media texture instead of a blank solid.
    if (!loaded) event.thumbnail = fallbackThumbnail();

    if (loaded) {
        LOG_I("Media thumbnail: %dx%d loaded from %s", event.thumbnail.width, event.thumbnail.height,
              player.art_url.c_str());
    } else if (event.thumbnail.has_thumbnail) {
        LOG_I("Media thumbnail: no cover art; using the default fallback");
    } else {
        LOG_W("Media thumbnail: no cover art and no default fallback configured");
    }

    emit(std::move(event));
}

void MprisMediaSource::emitTimeline() {
    auto selected = players_.find(selected_);
    if (selected == players_.end() || selected->second.state != PlaybackState::Playing) return;

    MediaEvent event;
    event.kind = MediaEvent::Kind::Timeline;
    event.enabled = true;
    event.state = selected->second.state;
    event.position = readPosition(selected->second.service);
    event.duration = selected->second.duration;
    emit(std::move(event));
}

int MprisMediaSource::onNameOwnerChanged(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    auto* self = static_cast<MprisMediaSource*>(userdata);
    const char* name = nullptr;
    const char* old_owner = nullptr;
    const char* new_owner = nullptr;
    if (sd_bus_message_read(message, "sss", &name, &old_owner, &new_owner) < 0 || !name) return 0;

    if (new_owner && *new_owner) {
        self->addPlayer(name);
    } else if (old_owner && *old_owner) {
        self->removePlayer(name);
    }
    return 0;
}

int MprisMediaSource::onPropertiesChanged(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/) {
    auto* self = static_cast<MprisMediaSource*>(userdata);
    const char* interface = nullptr;
    if (sd_bus_message_read(message, "s", &interface) < 0) return 0;
    if (!interface || std::strcmp(interface, kPlayerInterface) != 0) return 0;

    const char* sender = sd_bus_message_get_sender(message);
    const std::string sender_name = sender ? sender : "";
    auto player = self->players_.find(sender_name);
    if (player == self->players_.end()) {
        player = std::find_if(self->players_.begin(), self->players_.end(),
                              [&](const auto& entry) { return entry.second.owner == sender_name; });
    }
    if (player == self->players_.end()) return 0;

    if (sd_bus_message_enter_container(message, 'a', "{sv}") > 0) {
        readPropertiesDict(message, player->second);
        sd_bus_message_exit_container(message);
    }
    player->second.stamp = ++self->stamp_counter_;
    self->reselect();
    return 0;
}

#else

namespace {

class NoopMediaSource : public MediaSource {
   public:
    void start() override {}
    void stop() override {}
    std::vector<MediaEvent> poll() override {
        return {};
    }
};

}  // namespace

#endif

std::unique_ptr<MediaSource> createMprisMediaSource() {
#if LWE_MPRIS
    return std::make_unique<MprisMediaSource>();
#else
    return std::make_unique<NoopMediaSource>();
#endif
}

}  // namespace wallpaper_engine
