module;

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <expected>
#include <span>
#include <stop_token>
#include <system_error>
#include <thread>
#include <variant>
#include <vector>

#include <nuxe/macros.h>

export module nuxe.extract.executor;

import nuxe.error;
import nuxe.extract.plan;
import nuxe.extract.worker;

export namespace nuxe {

fn extract_buffered(const extraction_plan &plan,
                    std::span<extraction_worker> workers,
                    overwrite_policy overwrite = overwrite_policy::fail,
                    std::stop_token cancellation = {}) -> result<void> {
    if (cancellation.stop_requested())
        return std::unexpected(operation_cancelled{});
    if (plan.groups.empty())
        return {};
    if (workers.empty())
        return std::unexpected(extraction_error::invalid_worker_buffer);

    const let count = std::min(workers.size(), plan.groups.size());
    let next_group = std::atomic<std::size_t>{0};
    let stop = std::stop_source{};
    const let token = stop.get_token();

    const let forward_stop = [&] { stop.request_stop(); };
    let callback = std::stop_callback{cancellation, forward_stop};
    let results = std::vector<result<void>>(count);

    const let run = [&](std::size_t index) -> result<void> {
        while (!token.stop_requested()) {
            const let group =
                next_group.fetch_add(1, std::memory_order_relaxed);
            if (group >= plan.groups.size())
                break;

            let outcome = run_group(plan, plan.groups[group], workers[index],
                                    overwrite, token);
            if (!outcome) {
                stop.request_stop();
                return outcome;
            }
        }
        return {};
    };

    {
        let threads = std::vector<std::jthread>(count - 1);
        try {
            for (std::size_t index = 1; index < count; ++index) {
                threads[index - 1] =
                    std::jthread([&, index] { results[index] = run(index); });
            }
        } catch (const std::system_error &failure) {
            stop.request_stop();
            return std::unexpected(worker_launch_error{failure.code()});
        }
        results[0] = run(0);
    }

    for (const let &outcome : results) {
        if (!outcome &&
            !std::holds_alternative<operation_cancelled>(outcome.error()))
            return std::unexpected(outcome.error());
    }
    if (token.stop_requested())
        return std::unexpected(operation_cancelled{});
    return {};
}

} // namespace nuxe
