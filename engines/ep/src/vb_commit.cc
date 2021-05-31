/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2020-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */

#include "vb_commit.h"

#include <utility>

namespace VB {
Commit::Commit(Collections::VB::Manifest& manifest,
               BlindWrite blindWrite,
               vbucket_state vbs,
               SysErrorCallback sysErrorCallback)
    : collections(manifest),
      blindWrite(blindWrite),
      proposedVBState(std::move(vbs)),
      sysErrorCallback(std::move(sysErrorCallback)) {
}
} // namespace VB