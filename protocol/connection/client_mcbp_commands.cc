/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2016 Couchbase, Inc.
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
#include "client_mcbp_commands.h"
#include <libmcbp/mcbp.h>
#include <array>

void BinprotCommand::encode(std::vector<uint8_t>& buf) const {
    protocol_binary_request_header header;
    fillHeader(header);
    buf.insert(
            buf.end(), header.bytes, &header.bytes[0] + sizeof(header.bytes));
}

void BinprotCommand::fillHeader(protocol_binary_request_header& header,
                                size_t payload_len,
                                size_t extlen) const {
    header.request.magic = PROTOCOL_BINARY_REQ;
    header.request.opcode = opcode;
    header.request.keylen = htons(key.size());
    header.request.extlen = extlen;
    header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
    header.request.vbucket = htons(vbucket);
    header.request.bodylen = htonl(key.size() + extlen + payload_len);
    header.request.opaque = 0xdeadbeef;
    header.request.cas = cas;
}

void BinprotCommand::writeHeader(std::vector<uint8_t>& buf,
                                 size_t payload_len,
                                 size_t extlen) const {
    protocol_binary_request_header* hdr;
    buf.resize(sizeof(hdr->bytes));
    hdr = reinterpret_cast<protocol_binary_request_header*>(buf.data());
    fillHeader(*hdr, payload_len, extlen);
}

void BinprotGenericCommand::encode(std::vector<uint8_t>& buf) const {
    writeHeader(buf, value.size(), extras.size());
    buf.insert(buf.end(), extras.begin(), extras.end());
    buf.insert(buf.end(), key.begin(), key.end());
    buf.insert(buf.end(), value.begin(), value.end());
}

BinprotSubdocCommand::BinprotSubdocCommand(protocol_binary_command cmd_,
                                           const std::string& key_,
                                           const std::string& path_,
                                           const std::string& value_,
                                           protocol_binary_subdoc_flag flags_,
                                           uint64_t cas_)
    : BinprotCommandT() {
    setOp(cmd_);
    setKey(key_);
    setPath(path_);
    setValue(value_);
    setFlags(flags_);
    setCas(cas_);
}

BinprotSubdocCommand& BinprotSubdocCommand::setPath(const std::string& path_) {
    if (path.size() > std::numeric_limits<uint16_t>::max()) {
        throw std::out_of_range("BinprotSubdocCommand::setPath: Path too big");
    }
    path = path_;
    return *this;
}

void BinprotSubdocCommand::encode(std::vector<uint8_t>& buf) const {
    if (key.empty()) {
        throw std::logic_error("BinprotSubdocCommand::encode: Missing a key");
    }

    protocol_binary_request_subdocument request;
    memset(&request, 0, sizeof(request));

    // Expiry (optional) is encoded in extras. Only include if non-zero or
    // if explicit encoding of zero was requested.
    const bool include_expiry = (expiry.getValue() != 0 || expiry.isSet());

    // Populate the header.
    const size_t extlen = sizeof(uint16_t) + // Path length
                          1 + // flags
                          (include_expiry ? sizeof(uint32_t) : 0);

    fillHeader(request.message.header, path.size() + value.size(), extlen);

    // Add extras: pathlen, flags, optional expiry
    request.message.extras.pathlen = htons(path.size());
    request.message.extras.subdoc_flags = flags;
    buf.insert(buf.end(),
               request.bytes,
               &request.bytes[0] + sizeof(request.bytes));

    if (include_expiry) {
        // As expiry is optional (and immediately follows subdoc_flags,
        // i.e. unaligned) there's no field in the struct; so use low-level
        // memcpy to populate it.
        uint32_t encoded_expiry = htonl(expiry.getValue());
        char* expbuf = reinterpret_cast<char*>(&encoded_expiry);
        buf.insert(buf.end(), expbuf, expbuf + sizeof encoded_expiry);
    }

    // Add Body: key; path; value if applicable.
    buf.insert(buf.end(), key.begin(), key.end());
    buf.insert(buf.end(), path.begin(), path.end());
    buf.insert(buf.end(), value.begin(), value.end());
}

void BinprotResponse::assign(std::vector<uint8_t>&& srcbuf) {
    payload = std::move(srcbuf);
}

void BinprotSubdocResponse::assign(std::vector<uint8_t>&& srcbuf) {
    BinprotResponse::assign(std::move(srcbuf));
    if (getBodylen() - getExtlen() > 0) {
        value.assign(payload.data() + sizeof(protocol_binary_response_header) +
                             getExtlen(),
                     payload.data() + payload.size());
    }
}

void BinprotSaslAuthCommand::encode(std::vector<uint8_t>& buf) const {
    if (key.empty()) {
        throw std::logic_error("BinprotSaslAuthCommand: Missing mechanism (setMechanism)");
    }

    writeHeader(buf, challenge.size(), 0);
    buf.insert(buf.end(), key.begin(), key.end());
    buf.insert(buf.end(), challenge.begin(), challenge.end());
}

void BinprotSaslStepCommand::encode(std::vector<uint8_t>& buf) const {
    if (key.empty()) {
        throw std::logic_error("BinprotSaslStepCommand::encode: Missing mechanism (setMechanism");
    }
    if (challenge_response.empty()) {
        throw std::logic_error("BinprotSaslStepCommand::encode: Missing challenge response");
    }

    writeHeader(buf, challenge_response.size(), 0);
    buf.insert(buf.end(), key.begin(), key.end());
    buf.insert(buf.end(), challenge_response.begin(), challenge_response.end());
}

void BinprotCreateBucketCommand::setConfig(const std::string& module,
                                           const std::string& config) {
    module_config.assign(module.begin(), module.end());
    module_config.push_back(0x00);
    module_config.insert(module_config.end(), config.begin(), config.end());
}

void BinprotCreateBucketCommand::encode(std::vector<uint8_t>& buf) const {
    if (module_config.empty()) {
        throw std::logic_error("BinprotCreateBucketCommand::encode: Missing bucket module and config");
    }
    writeHeader(buf, module_config.size(), 0);
    buf.insert(buf.end(), key.begin(), key.end());
    buf.insert(buf.end(), module_config.begin(), module_config.end());
}

void BinprotGetCommand::encode(std::vector<uint8_t>& buf) const {
    writeHeader(buf, 0, 0);
    buf.insert(buf.end(), key.begin(), key.end());
}

uint32_t BinprotGetResponse::getDocumentFlags() const {
    if (!isSuccess()) {
        return 0;
    }
    return ntohl(*reinterpret_cast<const uint32_t*>(getPayload()));
}

BinprotMutationCommand& BinprotMutationCommand::setMutationType(
        const Greenstack::mutation_type_t type) {
    if (type == Greenstack::MutationType::Append) {
        setOp(PROTOCOL_BINARY_CMD_APPEND);
    } else if (type == Greenstack::MutationType::Prepend) {
        setOp(PROTOCOL_BINARY_CMD_PREPEND);
    } else if (type == Greenstack::MutationType::Replace) {
        setOp(PROTOCOL_BINARY_CMD_REPLACE);
    } else if (type == Greenstack::MutationType::Set) {
        setOp(PROTOCOL_BINARY_CMD_SET);
    } else if (type == Greenstack::MutationType::Add) {
        setOp(PROTOCOL_BINARY_CMD_ADD);
    } else {
        throw std::invalid_argument("BinprotMutationCommand::setMutationType: Mutation type not supported");
    }
    return *this;
}

BinprotMutationCommand& BinprotMutationCommand::setDocumentInfo(
        const DocumentInfo& info) {
    if (!info.id.empty()) {
        setKey(info.id);
    }

    setDocumentFlags(info.flags);
    setCas(info.cas);
    // TODO: Set expiration from DocInfo

    // Determine datatype
    switch (info.compression) {
    case Greenstack::Compression::None:
        break;
    case Greenstack::Compression::Snappy:
        datatype |= PROTOCOL_BINARY_DATATYPE_COMPRESSED;
        break;
    default:
        throw std::invalid_argument("BinprotMutationCommand::setDocumentInfo: Unrecognized compression type");
    }

    switch (info.datatype) {
    case Greenstack::Datatype::Raw:
        break;
    case Greenstack::Datatype::Json:
        datatype |= PROTOCOL_BINARY_DATATYPE_JSON;
        break;
    default:
        throw std::invalid_argument("BinprotMutationCommand::setDocumentInfo: Unknown datatype");
    }

    return *this;
}

void BinprotMutationCommand::encode(std::vector<uint8_t>& buf) const {
    if (key.empty()) {
        throw std::invalid_argument("BinprotMutationCommand::encode: Key is missing!");
    }

    uint8_t extlen = 8;

    protocol_binary_request_header *header;
    buf.resize(sizeof(header->bytes));
    header = reinterpret_cast<protocol_binary_request_header*>(buf.data());

    if (getOp() == PROTOCOL_BINARY_CMD_APPEND ||
        getOp() == PROTOCOL_BINARY_CMD_PREPEND) {
        if (expiry.isSet()) {
            throw std::invalid_argument("BinprotMutationCommand::encode: Expiry invalid with append/prepend");
        }
        extlen = 0;
    }

    fillHeader(*header, value.size(), extlen);
    header->request.datatype = datatype;

    if (extlen != 0) {
        // Write the extras:

        protocol_binary_request_set *req;
        buf.resize(sizeof(req->bytes));
        req = reinterpret_cast<protocol_binary_request_set*>(buf.data());

        req->message.body.expiration = htonl(expiry.getValue());
        req->message.body.flags = htonl(flags);
    }

    buf.insert(buf.end(), key.begin(), key.end());
    buf.insert(buf.end(), value.begin(), value.end());
}

void BinprotMutationResponse::assign(std::vector<uint8_t>&& buf) {
    BinprotResponse::assign(std::move(buf));

    if (!isSuccess()) {
        // No point parsing the other info..
        return;
    }

    mutation_info.cas = getCas();
    mutation_info.size = 0; // TODO: what's this?

    if (getExtlen() == 0) {
        mutation_info.vbucketuuid = 0;
        mutation_info.seqno = 0;
    } else if (getExtlen() == 16) {
        auto const* bufs = reinterpret_cast<const uint64_t*>(getPayload());
        mutation_info.vbucketuuid = ntohll(bufs[0]);
        mutation_info.seqno = ntohll(bufs[1]);
    } else {
        throw std::runtime_error("BinprotMutationResponse::assign: Bad extras length");
    }
}

void BinprotHelloCommand::encode(std::vector<uint8_t>& buf) const {
    writeHeader(buf, features.size() * 2, 0);
    buf.insert(buf.end(), key.begin(), key.end());

    for (auto f : features) {
        uint16_t enc = htons(f);
        const char* p = reinterpret_cast<const char*>(&enc);
        buf.insert(buf.end(), p, p + 2);
    }
}

void BinprotHelloResponse::assign(std::vector<uint8_t>&& buf) {
    BinprotResponse::assign(std::move(buf));

    // Ensure body length is even
    if ((getBodylen() & 1) != 0) {
        throw std::runtime_error("BinprotHelloResponse::assign: Invalid response returned. Uneven body length");
    }

    auto const* end =
            reinterpret_cast<const uint16_t*>(getPayload() + getBodylen());
    auto const* cur = reinterpret_cast<const uint16_t*>(getPayload());

    for (; cur != end; ++cur) {
        features.push_back(mcbp::Feature(htons(*cur)));
    }
}

void BinprotIncrDecrCommand::encode(std::vector<uint8_t>& buf) const {
    writeHeader(buf, 0, 20);
    if (getOp() != PROTOCOL_BINARY_CMD_DECREMENT &&
        getOp() != PROTOCOL_BINARY_CMD_INCREMENT) {
        throw std::invalid_argument(
                "BinprotIncrDecrCommand::encode: Invalid opcode. Need INCREMENT or DECREMENT");
    }

    // Write the delta
    for (auto n : std::array<uint64_t, 2>{{delta, initial}}) {
        uint64_t tmp = htonll(n);
        auto const* p = reinterpret_cast<const char*>(&tmp);
        buf.insert(buf.end(), p, p + 8);
    }

    uint32_t exptmp = htonl(expiry.getValue());
    auto const* p = reinterpret_cast<const char*>(&exptmp);
    buf.insert(buf.end(), p, p + 4);
    buf.insert(buf.end(), key.begin(), key.end());
}

void BinprotIncrDecrResponse::assign(std::vector<uint8_t>&& buf) {
    BinprotMutationResponse::assign(std::move(buf));
    // Assign the value:
    if (isSuccess()) {
        value = htonll(*reinterpret_cast<const uint64_t*>(getData().data()));
    } else {
        value = 0;
    }
}

void BinprotRemoveCommand::encode(std::vector<uint8_t>& buf) const {
    writeHeader(buf, 0, 0);
    buf.insert(buf.end(), key.begin(), key.end());
}
