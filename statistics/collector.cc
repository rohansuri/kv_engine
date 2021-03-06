/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2020 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include "statistics/collector.h"

#include "statistics/labelled_collector.h"
#include <memcached/dockey.h>
#include <spdlog/fmt/fmt.h>
#include <utilities/hdrhistogram.h>

#include <string_view>

using namespace std::string_view_literals;
BucketStatCollector StatCollector::forBucket(std::string_view bucket) const {
    return {*this, bucket};
}

const cb::stats::StatDef& StatCollector::lookup(cb::stats::Key key) {
    Expects(size_t(key) < size_t(cb::stats::Key::enum_max));
    return cb::stats::statDefinitions[size_t(key)];
}