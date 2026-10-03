#include "mdl_parser.h"

#include <string.h>

namespace wallpaper_engine {
namespace {

struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;

    bool u8(uint8_t& out) {
        if (pos + 1 > size) return false;
        out = data[pos++];
        return true;
    }
    bool u16(uint16_t& out) {
        if (pos + 2 > size) return false;
        memcpy(&out, data + pos, 2);
        pos += 2;
        return true;
    }
    bool u32(uint32_t& out) {
        if (pos + 4 > size) return false;
        memcpy(&out, data + pos, 4);
        pos += 4;
        return true;
    }
    bool f32(float& out) {
        uint32_t bits = 0;
        if (!u32(bits)) return false;
        memcpy(&out, &bits, 4);
        return true;
    }
    bool cstring(std::string& out) {
        const uint8_t* end = (const uint8_t*)memchr(data + pos, 0, size - pos);
        if (!end) return false;
        out.assign((const char*)data + pos, (size_t)(end - (data + pos)));
        pos = (size_t)(end - data) + 1;
        return true;
    }
};

bool findBytes(const uint8_t* data, size_t size, size_t from, const uint8_t* tag, size_t tag_len, size_t& out) {
    if (from >= size || tag_len == 0 || size - from < tag_len) return false;
    const uint8_t* hit = (const uint8_t*)memmem(data + from, size - from, tag, tag_len);
    if (!hit) return false;
    out = (size_t)(hit - data);
    return true;
}

bool findTag(const uint8_t* data, size_t size, size_t from, const char* tag, size_t& out) {
    return findBytes(data, size, from, (const uint8_t*)tag, strlen(tag), out);
}

bool parseBones(Reader& r, const uint8_t* data, size_t size, MdlModel& out) {
    std::string header;
    if (!r.cstring(header) || header.rfind("MDLS", 0) != 0) return false;
    uint32_t next_offset = 0;
    uint32_t num_bones = 0;
    uint8_t reserved = 0;
    if (!r.u32(next_offset) || !r.u32(num_bones) || !r.u8(reserved)) return false;
    if (num_bones == 0 || num_bones > 4096) return false;

    out.bones.reserve(num_bones);
    for (uint32_t i = 0; i < num_bones; ++i) {
        MdlBone bone;
        uint32_t type = 0;
        uint32_t parent = 0;
        uint32_t byte_len = 0;
        if (!r.u32(type) || !r.u32(parent) || !r.u32(byte_len)) return false;
        (void)type;
        if (byte_len == 0 || byte_len > 4096) return false;
        const uint32_t floats = byte_len / 4;
        for (uint32_t slot = 0; slot < floats; ++slot) {
            float value = 0.0f;
            if (!r.f32(value)) return false;
            if (slot < 16) bone.bind_matrix[slot] = value;
        }
        std::string info;
        if (!r.cstring(info) || !r.cstring(bone.name)) return false;
        bone.parent = parent;
        out.bones.push_back(std::move(bone));
    }

    // Keep the cursor consistent with the section's declared end so the next
    // search starts after any undecoded per-bone metadata.
    if (next_offset >= r.pos && next_offset <= size) r.pos = next_offset;
    (void)data;
    return true;
}

bool parseClip(Reader& r, uint32_t id, MdlAnimationClip& clip) {
    clip.id = id;
    uint32_t reserved = 0;
    if (!r.cstring(clip.name) || !r.cstring(clip.loop_mode) || !r.f32(clip.fps) || !r.u32(clip.frame_count) ||
        !r.u32(reserved)) {
        return false;
    }
    uint32_t track_count = 0;
    if (!r.u32(track_count)) return false;
    if (clip.name.empty() || clip.loop_mode.size() > 32 || !(clip.fps > 0.0f && clip.fps <= 1000.0f) ||
        clip.frame_count == 0 || clip.frame_count > 1000000 || track_count == 0 || track_count > 4096) {
        return false;
    }

    const size_t expected_bytes = ((size_t)clip.frame_count + 1) * 36;
    clip.tracks.resize(track_count);
    for (uint32_t t = 0; t < track_count; ++t) {
        uint32_t track_reserved = 0;
        uint32_t keyframe_bytes = 0;
        if (!r.u32(track_reserved) || !r.u32(keyframe_bytes)) return false;
        if (keyframe_bytes != expected_bytes || r.pos + keyframe_bytes > r.size) return false;
        std::vector<MdlKeyframe>& track = clip.tracks[t];
        track.resize((size_t)clip.frame_count + 1);
        for (MdlKeyframe& keyframe : track) {
            if (!r.f32(keyframe.translation[0]) || !r.f32(keyframe.translation[1]) || !r.f32(keyframe.translation[2]) ||
                !r.f32(keyframe.rotation[0]) || !r.f32(keyframe.rotation[1]) || !r.f32(keyframe.rotation[2]) ||
                !r.f32(keyframe.scale[0]) || !r.f32(keyframe.scale[1]) || !r.f32(keyframe.scale[2])) {
                return false;
            }
        }
    }
    return true;
}

bool parseAnimations(Reader& r, MdlModel& out) {
    std::string header;
    if (!r.cstring(header) || header.rfind("MDLA", 0) != 0) return false;
    uint32_t end_offset = 0;
    uint32_t num_animations = 0;
    uint32_t first_id = 0;
    uint32_t reserved = 0;
    if (!r.u32(end_offset) || !r.u32(num_animations) || !r.u32(first_id) || !r.u32(reserved)) return false;
    if (num_animations == 0 || num_animations > 64) return false;

    for (uint32_t i = 0; i < num_animations; ++i) {
        uint32_t id = first_id;
        if (i > 0) {
            uint8_t byte = 0;
            while (r.pos < r.size && (byte = r.data[r.pos]) == 0) ++r.pos;
            uint32_t skipped = 0;
            if (!r.u32(id) || !r.u32(skipped)) return false;
        }
        MdlAnimationClip clip;
        if (!parseClip(r, id, clip)) return false;
        out.clips.push_back(std::move(clip));
    }
    (void)end_offset;
    return true;
}

}  // namespace

bool parseMdl(const uint8_t* data, size_t size, MdlModel& out) {
    out = MdlModel{};
    if (!data || size < 25) return false;

    const char* magic = (const char*)data;
    if (strncmp(magic, "MDLV", 4) != 0) return false;

    Reader r{data, size, 0};
    uint8_t reserved_byte = 0;
    uint32_t type_word = 0;
    uint16_t sub_version = 0;
    uint16_t flags = 0;
    uint32_t reserved = 0;
    r.pos = 8;  // skip the "MDLV00XX" version string
    if (!r.u8(reserved_byte) || !r.u32(type_word) || !r.u16(sub_version) || !r.u16(flags) || !r.u32(reserved)) {
        return false;
    }
    (void)type_word;
    (void)sub_version;
    (void)flags;
    (void)reserved;
    out.version.assign((const char*)data, 8);

    std::string material;
    if (!r.cstring(material)) return false;
    out.material = material;

    // The mesh block sits within the first 64 bytes after the material. Four
    // vertex layouts ship in real files: 80 bytes for the v0017+ skinned
    // layout, 84 bytes for its variant with one extra word, 52 bytes for the
    // older skinned layout (v0013) and 48 bytes for an unskinned puppet. Pick the first candidate whose indices are in
    // range, which disambiguates meshes that divide evenly by several strides.
    struct MeshLayout {
        size_t stride;
        size_t indices_offset;  // kNoBones when the mesh carries no skinning
        size_t weights_offset;
        size_t uv_offset;
    };
    constexpr size_t kNoBones = (size_t)-1;
    const MeshLayout kLayouts[] = {
        {80, 40, 56, 72},
        {48, kNoBones, kNoBones, 40},
        {52, 12, 28, 44},
        {84, 44, 60, 76},
    };

    size_t block_offset = 0;
    size_t indices_offset = 0;
    size_t vertex_count = 0;
    uint32_t index_bytes = 0;
    const MeshLayout* layout = nullptr;
    for (size_t off = r.pos; layout == nullptr && off + 12 <= size && off < r.pos + 64; ++off) {
        uint32_t candidate_vertices = 0;
        memcpy(&candidate_vertices, data + off + 4, 4);
        for (const MeshLayout& candidate : kLayouts) {
            if (candidate_vertices == 0 || candidate_vertices % candidate.stride != 0) continue;
            const size_t candidate_vertices_offset = off + 8;
            if (candidate_vertices_offset + candidate_vertices + 4 > size) continue;
            uint32_t candidate_indices = 0;
            memcpy(&candidate_indices, data + candidate_vertices_offset + candidate_vertices, 4);
            const size_t candidate_index_offset = candidate_vertices_offset + candidate_vertices + 4;
            if (candidate_indices == 0 || candidate_indices % 6 != 0 ||
                candidate_index_offset + candidate_indices > size) {
                continue;
            }
            const size_t candidate_count = candidate_vertices / candidate.stride;
            bool in_range = true;
            for (size_t t = 0; t < candidate_indices / 6 && in_range; ++t) {
                uint16_t idx[3];
                memcpy(idx, data + candidate_index_offset + t * 6, 6);
                in_range = idx[0] < candidate_count && idx[1] < candidate_count && idx[2] < candidate_count;
            }
            if (!in_range) continue;
            block_offset = off;
            indices_offset = candidate_index_offset;
            vertex_count = candidate_count;
            index_bytes = candidate_indices;
            layout = &candidate;
            break;
        }
    }
    if (layout == nullptr) return false;

    const size_t vertices_offset = block_offset + 8;
    out.vertices.resize(vertex_count);
    for (size_t i = 0; i < vertex_count; ++i) {
        const uint8_t* v = data + vertices_offset + i * layout->stride;
        MdlVertex& vertex = out.vertices[i];
        memcpy(vertex.position, v + 0, 12);
        if (layout->stride == 52) {
            vertex.normal[2] = 1.0f;
        } else {
            memcpy(vertex.normal, v + 12, 12);
            memcpy(vertex.tangent, v + 24, 12);
            memcpy(&vertex.tangent_w, v + 36, 4);
        }
        if (layout->indices_offset != kNoBones) {
            memcpy(vertex.bone_indices, v + layout->indices_offset, 16);
            memcpy(vertex.bone_weights, v + layout->weights_offset, 16);
        }
        memcpy(vertex.uv, v + layout->uv_offset, 8);
    }

    const size_t index_count = index_bytes / 2;
    out.triangles.resize(index_count / 3);
    for (size_t t = 0; t < index_count / 3; ++t) {
        uint16_t idx[3];
        memcpy(idx, data + indices_offset + t * 6, 6);
        if (idx[0] >= vertex_count || idx[1] >= vertex_count || idx[2] >= vertex_count) {
            out.triangles.clear();
            return false;
        }
        out.triangles[t] = {idx[0], idx[1], idx[2]};
    }

    // Skeleton and animation are optional; locate their sections after the
    // index buffer rather than decoding the mesh trailer between them.
    size_t search_from = indices_offset + index_bytes;
    size_t bones_offset = 0;
    if (findTag(data, size, search_from, "MDLS", bones_offset)) {
        Reader bone_reader{data, size, bones_offset};
        parseBones(bone_reader, data, size, out);
    }
    size_t anim_offset = 0;
    if (findTag(data, size, search_from, "MDLA", anim_offset)) {
        Reader anim_reader{data, size, anim_offset};
        parseAnimations(anim_reader, out);
    }
    return true;
}

}  // namespace wallpaper_engine
