/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc.
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

#include "executors.h"

#include <daemon/cookie.h>
#include <memcached/engine.h>
#include <memcached/protocol_binary.h>

void collections_set_manifest_executor(Cookie& cookie) {
    auto& connection = cookie.getConnection();
    auto ret = cookie.swapAiostat(ENGINE_SUCCESS);
    if (ret == ENGINE_SUCCESS) {
        auto& req = cookie.getRequest();
        auto val = req.getValue();
        std::string_view jsonBuffer{reinterpret_cast<const char*>(val.data()),
                                    val.size()};
        auto status = connection.getBucketEngine().set_collection_manifest(
                &cookie, jsonBuffer);
        handle_executor_status(cookie, status);
    } else {
        handle_executor_status(cookie, ret);
    }
}