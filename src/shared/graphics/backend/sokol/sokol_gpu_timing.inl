#pragma once

#include <array>

#include "../gpu_timing.h"

namespace {
constexpr uint32_t kTimingRanges = 256;
struct TimingSlot {
    VkQueryPool pool = VK_NULL_HANDLE;
    std::array<std::string, kTimingRanges> labels;
    uint32_t ranges = 0;
    bool pending = false;
};
std::array<TimingSlot, SG_NUM_INFLIGHT_FRAMES> timing_slots;
TimingSlot* timing_active = nullptr;
uint32_t timing_valid_bits = 0;
double timing_period_ns = 0.0;

bool collectTimingSlot(TimingSlot& slot, std::vector<GpuTimingSample>& samples) {
    if (!slot.pending) return true;
    // Interleaved uint64 timestamp and availability; no WAIT_BIT.
    std::array<uint64_t, kTimingRanges * 4> values = {};
    const uint32_t queries = slot.ranges * 2;
    const VkResult status =
        vkGetQueryPoolResults(_sg.vk.dev, slot.pool, 0, queries, queries * 2 * sizeof(uint64_t), values.data(),
                              2 * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (status != VK_SUCCESS) return false;
    for (uint32_t i = 0; i < queries; ++i)
        if (!values[i * 2 + 1]) return false;
    for (uint32_t i = 0; i < slot.ranges; ++i) {
        samples.push_back(
            {slot.labels[i], gpu_timestamp_ms(values[i * 4], values[i * 4 + 2], timing_valid_bits, timing_period_ns)});
    }
    slot.pending = false;
    return true;
}
std::vector<GpuTimingSample> timing_completed;
}  // namespace

bool gpu_timing_initialize() {
    if (!sg_isvalid() || !_sg.vk.dev) return false;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(_sg.vk.phys_dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(_sg.vk.phys_dev, &count, queues.data());
    if (_sg.vk.queue_family_index >= count) return false;
    timing_valid_bits = queues[_sg.vk.queue_family_index].timestampValidBits;
    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(_sg.vk.phys_dev, &properties);
    timing_period_ns = properties.limits.timestampPeriod;
    if (!timing_valid_bits || !(timing_period_ns > 0.0)) return false;
    for (TimingSlot& slot : timing_slots) {
        VkQueryPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = kTimingRanges * 2;
        if (vkCreateQueryPool(_sg.vk.dev, &info, nullptr, &slot.pool) != VK_SUCCESS) {
            gpu_timing_shutdown();
            return false;
        }
    }
    return true;
}

std::vector<GpuTimingSample> gpu_timing_poll() {
    std::vector<GpuTimingSample> samples = std::move(timing_completed);
    timing_completed.clear();
    for (TimingSlot& slot : timing_slots)
        if (slot.pool) collectTimingSlot(slot, samples);
    return samples;
}

void gpu_timing_begin_frame() {
    timing_active = nullptr;
    if (!timing_slots[0].pool) return;
    // Uses Sokol's existing frame acquisition/fence, never an additional wait.
    _sg_vk_acquire_frame_command_buffers();
    if (!_sg.vk.frame.cmd_buf) return;
    TimingSlot& slot = timing_slots[_sg.vk.frame_slot];
    if (!collectTimingSlot(slot, timing_completed)) return;
    slot.ranges = 1;
    slot.labels[0] = "full-render";
    vkCmdResetQueryPool(_sg.vk.frame.cmd_buf, slot.pool, 0, kTimingRanges * 2);
    vkCmdWriteTimestamp(_sg.vk.frame.cmd_buf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.pool, 0);
    timing_active = &slot;
}

int gpu_timing_begin_pass(const std::string& label) {
    if (!timing_active || timing_active->ranges == kTimingRanges) return -1;
    const uint32_t token = timing_active->ranges++;
    timing_active->labels[token] = label;
    vkCmdWriteTimestamp(_sg.vk.frame.cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing_active->pool, token * 2);
    return (int)token;
}

void gpu_timing_end_pass(int token) {
    if (!timing_active || token < 1 || (uint32_t)token >= timing_active->ranges) return;
    vkCmdWriteTimestamp(_sg.vk.frame.cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing_active->pool,
                        (uint32_t)token * 2 + 1);
}

void gpu_timing_end_frame() {
    if (!timing_active) return;
    vkCmdWriteTimestamp(_sg.vk.frame.cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing_active->pool, 1);
    timing_active->pending = true;
    timing_active = nullptr;
}

void gpu_timing_shutdown() {
    timing_active = nullptr;
    for (TimingSlot& slot : timing_slots) {
        if (slot.pool) vkDestroyQueryPool(_sg.vk.dev, slot.pool, nullptr);
        slot = {};
    }
    timing_completed.clear();
    timing_valid_bits = 0;
}
