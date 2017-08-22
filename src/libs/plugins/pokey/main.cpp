#include <assert.h>
#include <iostream>
#include <map>
#include <memory>
#include <uv.h>
#include <vector>

#include "main.h"
#include "plugins/common/simhubdeviceplugin.h"

// -- public C FFI

extern "C" {

int simplug_init(SPHANDLE *plugin_instance, LoggingFunctionCB logger)
{
    *plugin_instance = new PokeyDevicePluginStateManager(logger);
    return 0;
}

int simplug_config_passthrough(SPHANDLE plugin_instance, void *libconfig_instance)
{
    return static_cast<PluginStateManager *>(plugin_instance)->configPassthrough(static_cast<libconfig::Config *>(libconfig_instance));
}

int simplug_preflight_complete(SPHANDLE plugin_instance)
{
    return static_cast<PluginStateManager *>(plugin_instance)->preflightComplete();
}

void simplug_commence_eventing(SPHANDLE plugin_instance, EnqueueEventHandler enqueue_callback, void *arg)
{
    static_cast<PluginStateManager *>(plugin_instance)->commenceEventing(enqueue_callback, arg);
}

int simplug_deliver_value(SPHANDLE plugin_instance, GenericTLV *value)
{
    return static_cast<PluginStateManager *>(plugin_instance)->deliverValue(value);
}

void simplug_cease_eventing(SPHANDLE plugin_instance)
{
    static_cast<PluginStateManager *>(plugin_instance)->ceaseEventing();
}

void simplug_release(SPHANDLE plugin_instance)
{
    assert(plugin_instance);
    delete static_cast<PluginStateManager *>(plugin_instance);
}
}

PokeyDevicePluginStateManager *PokeyDevicePluginStateManager::_StateManagerInstance = NULL;

// -- private C++ pokey plugin implementation

PokeyDevicePluginStateManager::PokeyDevicePluginStateManager(LoggingFunctionCB logger)
    : PluginStateManager(logger)
{
    _numberOfDevices = 0; ///< 0 devices discovered
    _name = "pokey";
    _devices = (sPoKeysNetworkDeviceSummary *)calloc(sizeof(sPoKeysNetworkDeviceSummary),
        16); ///< 0 initialise the network device summary
}

//! static getter for singleton instance of our class
PokeyDevicePluginStateManager *PokeyDevicePluginStateManager::StateManagerInstance(void)
{
    return _StateManagerInstance;
}

PokeyDevicePluginStateManager::~PokeyDevicePluginStateManager(void)
{
    if (_pluginThread) {
        if (_pluginThread->joinable()) {
            ceaseEventing();
            _pluginThread->join();
        }
    }

    free(_devices);
}

void PokeyDevicePluginStateManager::ceaseEventing(void)
{
    PluginStateManager::ceaseEventing();

    for (auto devPair : _deviceMap) {
        devPair.second->stopPolling();
    }
}

int PokeyDevicePluginStateManager::deliverValue(GenericTLV *data)
{
    assert(data);

    std::shared_ptr<PokeyDevice> device = targetFromDeviceTargetList(data->name);

    if (device) {
        if (data->type == ConfigType::CONFIG_BOOL) {
            device->targetValue(data->name, (int)data->value);
        }
    }

    return 0;
}

void PokeyDevicePluginStateManager::commenceEventing(EnqueueEventHandler enqueueCallback, void *arg)
{
    _enqueueCallback = enqueueCallback;
    _callbackArg = arg;

    for (auto devPair : _deviceMap) {
        devPair.second->setCallbackInfo(_enqueueCallback, _callbackArg, this);
    }
}

void PokeyDevicePluginStateManager::enumerateDevices(void)
{
    _numberOfDevices = PK_EnumerateNetworkDevices(_devices, 800);

    for (int i = 0; i < _numberOfDevices; i++) {
        try {
            std::shared_ptr<PokeyDevice> device = std::make_shared<PokeyDevice>(_devices[i], i);

            if (device->pokey()) {
                _logger(LOG_INFO, "    - #%s %s %s (v%d.%d.%d) - %u.%u.%u.%u ", device->serialNumber().c_str(), device->hardwareTypeString().c_str(),
                    device->deviceData().DeviceName, device->firmwareMajorMajorVersion(), device->firmwareMajorVersion(), device->firmwareMinorVersion(), device->ipAddress()[0],
                    device->ipAddress()[1], device->ipAddress()[2], device->ipAddress()[3]);
            }

            _deviceMap.emplace(device->serialNumber(), device);
        }
        catch (const std::exception &except) {
            _logger(LOG_ERROR, "couldn't connect to referenced pokey device");
        }
    }
}

bool PokeyDevicePluginStateManager::addTargetToDeviceTargetList(std::string target, std::shared_ptr<PokeyDevice> device)
{
    _deviceMap.emplace(target, device);
    return true;
}

std::shared_ptr<PokeyDevice> PokeyDevicePluginStateManager::targetFromDeviceTargetList(std::string key)
{
    std::map<std::string, std::shared_ptr<PokeyDevice>>::iterator it = _deviceMap.find(key);

    if (it != _deviceMap.end()) {
        return (*it).second;
    }

    return NULL;
}

std::shared_ptr<PokeyDevice> PokeyDevicePluginStateManager::device(std::string serialNumber)
{
    std::shared_ptr<PokeyDevice> retVal;

    if (_deviceMap.count(serialNumber)) {
        retVal = _deviceMap.find(serialNumber)->second;
    }

    return retVal;
}

bool PokeyDevicePluginStateManager::validateConfig(libconfig::SettingIterator iter)
{
    bool retValue = true;

    try {
        iter->lookup("pins");
    }
    catch (const libconfig::SettingNotFoundException &nfex) {
        _logger(LOG_ERROR, "Config file parse error at %s. Skipping....", nfex.getPath());
        retValue = false;
    }

    return retValue;
}

bool PokeyDevicePluginStateManager::deviceConfiguration(libconfig::SettingIterator iter, std::shared_ptr<PokeyDevice> pokeyDevice)
{
    bool retVal = true;
    std::string configSerialNumber = "";
    std::string configName = "";

    try {
        iter->lookupValue("serialNumber", configSerialNumber);

        if (pokeyDevice == NULL) {
            _logger(LOG_ERROR, "    - #%s. No physical device. Skipping....", configSerialNumber.c_str());
            retVal = false;
        }
        else {
            iter->lookupValue("name", configName);

            if (configName != pokeyDevice->name().c_str()) {
                uint32_t retValue = pokeyDevice->name(configName);
                if (retValue == PK_OK) {
                    _logger(LOG_INFO, "      - Device name set (%s)", configName.c_str());
                }
                else {
                    _logger(LOG_INFO, "      - Error setting device name (%s)", configName.c_str());
                }
            }
        }
    }
    catch (const libconfig::SettingNotFoundException &nfex) {
        _logger(LOG_ERROR, "Config file parse error at %s. Skipping....", nfex.getPath());
        retVal = false;
    }

    return retVal;
}

bool PokeyDevicePluginStateManager::devicePinsConfiguration(libconfig::Setting *pins, std::shared_ptr<PokeyDevice> pokeyDevice)
{
    /** pin = 4,
        name = "S_OH_GROUND_CALL",
        type = "DIGITAL_INPUT",
        default = 0
    **/

    bool retVal = true;
    int pinCount = pins->getLength();

    if (pinCount > 0) {
        _logger(LOG_INFO, "      - Found %d pins", pinCount);
        int pinIndex = 0;

        for (libconfig::SettingIterator iter = pins->begin(); iter != pins->end(); iter++) {
            int pinNumber = 0;
            std::string pinName = "";
            std::string pinType = "";
            std::string description = "";
            bool pinDefault = false;

            try {
                iter->lookupValue("pin", pinNumber);
                iter->lookupValue("name", pinName);
                iter->lookupValue("type", pinType);
                iter->lookupValue("description", description);
            }
            catch (const libconfig::SettingNotFoundException &nfex) {
                _logger(LOG_ERROR, "Config file parse error at %s. Skipping....", nfex.getPath());
            }

            if (pokeyDevice->validatePinCapability(pinNumber, pinType)) {

                if (pinType == "DIGITAL_OUTPUT") {
                    int defaultValue = 0;

                    if (addTargetToDeviceTargetList(pinName, pokeyDevice)) {

                        if (iter->exists("default"))
                            iter->lookupValue("default", defaultValue);

                        pokeyDevice->addPin(pinName, pinNumber, pinType, defaultValue, description);
                        _logger(LOG_INFO, "        - [%d] Added target %s on pin %d", pinIndex, pinName.c_str(), pinNumber);
                    }
                }
                else if (pinType == "DIGITAL_INPUT") {
                    pokeyDevice->addPin(pinName, pinNumber, pinType, 0, description);
                    _logger(LOG_INFO, "        - [%d] Added source %s on pin %d", pinIndex, pinName.c_str(), pinNumber);
                }
                pinIndex++;
            }
            else {
                _logger(LOG_ERROR, "        - [%d] Invalid pin type %s on pin %d", pinIndex, pinType.c_str(), pinNumber);
                continue;
            }
        }
    }
    else {
        retVal = false;
    }

    return retVal;
}

bool PokeyDevicePluginStateManager::deviceEncodersConfiguration(libconfig::Setting *encoders, std::shared_ptr<PokeyDevice> pokeyDevice)
{
    /**
    {
        # channel 1 - Fast Encoder 1 - uses pins 1 and 2
        encoder = 1,
        name = "",
        description = "",
        default = 100
      }
    **/

    bool retVal = true;
    int encoderCount = encoders->getLength();

    if (encoderCount > 0) {
        _logger(LOG_INFO, "    [%s]  - Found %i encoders", pokeyDevice->name().c_str(), encoderCount);
        int encoderIndex = 0;

        for (libconfig::SettingIterator iter = encoders->begin(); iter != encoders->end(); iter++) {
            int encoderNumber = 0;
            std::string encoderName = "";
            std::string encoderDescription = "";
            int encoderDefault = 0;
            int encoderMin = 0;
            int encoderMax = 1000;
            int encoderStep = 1;
            int invertDirection = 0;

            try {
                iter->lookupValue("encoder", encoderNumber);
                iter->lookupValue("name", encoderName);
                iter->lookupValue("default", encoderDefault);
                iter->lookupValue("description", encoderDescription);
                iter->lookupValue("min", encoderMin);
                iter->lookupValue("max", encoderMax);
                iter->lookupValue("step", encoderStep);
                iter->lookupValue("invertDirection", invertDirection);
            }
            catch (const libconfig::SettingNotFoundException &nfex) {
                _logger(LOG_ERROR, "Could not find %s. Skipping....", nfex.what());
            }

            if (pokeyDevice->validateEncoder(encoderNumber)) {
                pokeyDevice->addEncoder(encoderNumber, encoderDefault, encoderName, encoderDescription, encoderMin, encoderMax, encoderStep, invertDirection);
                _logger(LOG_INFO, "        - [%s] Added encoder %i (%s)", pokeyDevice->name().c_str(), encoderNumber, encoderName.c_str());
            }
        }
    }
    else {
        retVal = false;
    }

    return retVal;
}

int PokeyDevicePluginStateManager::preflightComplete(void)
{
    int retVal = PREFLIGHT_OK;
    libconfig::Setting *devicesConfiguraiton = NULL;

    enumerateDevices();

    try {
        devicesConfiguraiton = &_config->lookup("configuration");
    }
    catch (const libconfig::SettingNotFoundException &nfex) {
        _logger(LOG_ERROR, "Config file parse error at %s. Skipping....", nfex.getPath());
    }

    for (libconfig::SettingIterator iter = devicesConfiguraiton->begin(); iter != devicesConfiguraiton->end(); iter++) {

        std::string serialNumber = "";
        iter->lookupValue("serialNumber", serialNumber);

        std::shared_ptr<PokeyDevice> pokeyDevice = device(serialNumber);

        // check that the configuration has the required config sections
        if (!validateConfig(iter)) {
            continue;
        }

        if (deviceConfiguration(iter, pokeyDevice) == 0) {
            continue;
        }

        devicePinsConfiguration(&iter->lookup("pins"), pokeyDevice);

        ///! check if there is an encoder section in the config
        try {
            deviceEncodersConfiguration(&iter->lookup("encoders"), pokeyDevice);
        }
        catch (const libconfig::SettingNotFoundException &nfex) {
            printf("\n %s\n", nfex.what());
        }

        pokeyDevice->startPolling();
    }

    if (_numberOfDevices > 0) {
        _logger(LOG_INFO, "  - Discovered %d pokey devices", _numberOfDevices);
        retVal = PREFLIGHT_OK;
    }
    else {
        _logger(LOG_INFO, "   - No Pokey devices discovered");
    }

    return retVal;
}
