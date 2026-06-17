//
// Created by A on 17.06.2026.
//
module;

#include <vector>
#include <thread>
#include <semaphore>
#include <atomic>

export module host_sender;

export import voxel_health_protocol;
export import native_tcp;

export
class HostSender {
private:
    std::vector<VoxelBatchUpdate> batches;
    std::jthread sender_;
    std::counting_semaphore<1> start_signal_{0};
    std::atomic<bool> should_exit_{false};
    std::mutex mutex_;

public:
    HostSender() {
        sender_ = std::jthread([this](std::stop_token stoken) {
            while (!should_exit_ && !stoken.stop_requested()) {

                start_signal_.acquire();
                if (should_exit_ || stoken.stop_requested()) break;

                {
                    std::lock_guard<std::mutex> guard(mutex_);
                    for (VoxelBatchUpdate& batch : batches) {
                        net::send_typed(batch);
                    }
                    batches.clear();
                }
            }
        });
    }

    ~HostSender() {
        should_exit_ = true;
        start_signal_.release();
    }

    void TriggerWork() {
        start_signal_.release();
    }

    void add_batch(VoxelBatchUpdate batch)
    {
        std::lock_guard lock(mutex_);
        batches.push_back(batch);
    }
};