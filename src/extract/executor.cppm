module;

#include <nuxe/macros.h>
#include <span>
#include <stop_token>

export module nuxe.extract.executor;

import nuxe.error;
import nuxe.extract.plan;
import nuxe.extract.worker;

export namespace nuxe {

fn extract_buffered(const extraction_plan &plan,
                    std::span<extraction_worker> workers,
                    overwrite_policy overwrite = overwrite_policy::fail,
                    std::stop_token cancellation = {}) -> result<void>;

}
