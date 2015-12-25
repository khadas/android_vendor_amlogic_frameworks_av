/*
 * Copyright (C) 2015, Amlogic Inc.
 * All rights reserved
 */

#ifndef HLS_DATA_SOURCE_H_

#define HLS_DATA_SOURCE_H_

#include <media/stagefright/foundation/ABase.h>
#include <media/stagefright/DataSource.h>

namespace android {

struct ABuffer;

struct HLSDataSource : public DataSource {
    HLSDataSource(const sp<ABuffer> &buffer);

    virtual status_t initCheck() const;
    virtual ssize_t readAt(off64_t offset, void *data, size_t size);
    virtual status_t getSize(off64_t *size);

protected:
    virtual ~HLSDataSource();

private:
    sp<ABuffer> mBuffer;

    DISALLOW_EVIL_CONSTRUCTORS(HLSDataSource);
};

}  // namespace android

#endif  // HLS_DATA_SOURCE_H_

