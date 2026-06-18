//
// Created by A on 18.06.2026.
//
module;

#include <vector>
#include <thread>
#include <semaphore>
#include <atomic>

export module client_sender;

export import voxel_health_protocol;
export import native_tcp;

export
class ClientSender {
private:
    std::vector<VoxelHealCommand> heals;
    std::jthread sender_;
    std::counting_semaphore<1> start_signal_{0};
    std::atomic<bool> should_exit_{false};
    std::mutex mutex_;

public:
    ClientSender() {
        sender_ = std::jthread([this](std::stop_token stoken) {
            while (!should_exit_ && !stoken.stop_requested()) {

                start_signal_.acquire();
                if (should_exit_ || stoken.stop_requested()) break;

                {
                    std::lock_guard<std::mutex> guard(mutex_);
                    for (VoxelHealCommand& heal : heals) {
                        net::send_typed(heal);
                    }
                    heals.clear();
                }
            }
        });
    }

    ~ClientSender() {
        should_exit_ = true;
        start_signal_.release();
    }

    void TriggerWork() {
        start_signal_.release();
    }

    void add_batch(VoxelHealCommand heal)
    {
        std::lock_guard lock(mutex_);
        heals.push_back(heal);
    }
};
