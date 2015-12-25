/*
 * Copyright (C) 2015, Amlogic Inc.
 * All rights reserved
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "NU-HLSDataSource"

#include "AmHLSDataSource.h"
#include <media/stagefright/foundation/ABuffer.h>

namespace android {

HLSDataSource::HLSDataSource(const sp<ABuffer> &buffer)
    : mBuffer(buffer) {
}

HLSDataSource::~HLSDataSource() {
}

status_t HLSDataSource::initCheck() const {
    return OK;
}

ssize_t HLSDataSource::readAt(off64_t offset, void *data, size_t size) {
    if ((offset < 0) || (offset >= (off64_t)mBuffer->size())) {
        return 0;
    }

    size_t copy = mBuffer->size() - offset;
    if (copy > size) {
        copy = size;
    }

    memcpy((uint8_t *)data, mBuffer->data() + offset, copy);

    return copy;
}

status_t HLSDataSource::getSize(off64_t *size) {
    *size = mBuffer->size();

    return OK;
}

}  // namespace android

