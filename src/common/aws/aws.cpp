#include <unistd.h>

#include "../log/clog.h"
#include "aws.h"

AWS awsHelper;

AWS::AWS()
{
    _options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Info;
    _options.loggingOptions.defaultLogPrefix = "../logs/aws/";
    _SDKVersion = Aws::Version::GetVersionString();
}

AWS::~AWS()
{
    Aws::ShutdownAPI(_options);
    delete _polly;
    delete _kinesis;
    logger.log(LOG_INFO, "AWS SDK shutdown");
}

Polly *AWS::polly()
{
    return _polly;
}

Kinesis *AWS::kinesis()
{
    return _kinesis;
}

void AWS::initPolly()
{
    _polly = new Polly;
}

void AWS::initKinesis(std::string streamName, std::string partition, std::string region)
{
    // TODO: Make this part of a config file
    _kinesis = new Kinesis(streamName, partition, region);
}

void AWS::init()
{
    Aws::InitAPI(_options);
    logger.log(LOG_INFO, "AWS SDK v%s started", _SDKVersion.c_str());
}
