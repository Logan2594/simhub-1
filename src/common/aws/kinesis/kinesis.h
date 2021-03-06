#ifndef __AWS_KINESIS_H
#define __AWS_KINESIS_H

#include "../../libs/queue/concurrent_queue.h"
#include <aws/core/Aws.h>
#include <aws/core/Version.h>
#include <aws/core/utils/Outcome.h>
#include <aws/core/utils/memory/stl/AWSAllocator.h>
#include <aws/kinesis/KinesisClient.h>
#include <aws/kinesis/model/PutRecordRequest.h>
#include <condition_variable>
#include <cstdarg>
#include <iostream>
#include <stdio.h>
#include <string.h>

#include "common/support/threadmanager.h"

typedef Aws::Kinesis::KinesisClient KinesisClient;

static const char *ALLOCATION_TAG = "Kinesis::Main";
class Kinesis
{
protected:
    std::shared_ptr<KinesisClient> _kinesisClient; ///< main kinesis client
    ConcurrentQueue<Aws::Utils::ByteBuffer> _queue;
    std::string _partition;
    std::string _streamName;
    std::string _region;
    CancelableThreadManager _threadManager;
    long _recordCounter;

public:
    // Default constructor
    Kinesis(std::string streamName, std::string partition, std::string region);
    // Destructor
    ~Kinesis(void);
    void putRecord(Aws::Utils::ByteBuffer data);
    virtual void shutdown(void);
};

#endif // Kinesis
