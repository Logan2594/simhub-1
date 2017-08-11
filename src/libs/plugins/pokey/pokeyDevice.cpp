#include "pokeyDevice.h"
#include <string.h>

PokeyDevice::PokeyDevice(sPoKeysNetworkDeviceSummary deviceSummary, uint8_t index)
{
    _pokey = PK_ConnectToNetworkDevice(&deviceSummary);
    _index = index;
    _userId = deviceSummary.UserID;
    _serialNumber = std::to_string(deviceSummary.SerialNumber);
    _firwareVersionMajorMajor = (deviceSummary.FirmwareVersionMajor >> 4) + 1;
    _firwareVersionMajor = deviceSummary.FirmwareVersionMajor & 0x0F;
    _firwareVersionMinor = deviceSummary.FirmwareVersionMinor;
    memcpy(&_ipAddress, &deviceSummary.IPaddress, 4);
    _hardwareType = deviceSummary.HWtype;
    _dhcp = deviceSummary.DHCP;

    loadPinConfiguration();
    _pollTimer.data = this;
    _pollLoop = uv_loop_new();
    uv_timer_init(_pollLoop, &_pollTimer);

    int ret = uv_timer_start(&_pollTimer, (uv_timer_cb)&PokeyDevice::DigitalIOTimerCallback, DEVICE_START_DELAY, DEVICE_READ_INTERVAL);

    if (ret == 0) {
        _pollThread.reset(new std::thread([=] { uv_run(_pollLoop, UV_RUN_DEFAULT); }));
    }
}

void PokeyDevice::DigitalIOTimerCallback(uv_timer_t *timer, int status)
{
    PokeyDevice *self = static_cast<PokeyDevice *>(timer->data);

    int ret = PK_DigitalIOGet(self->_pokey);

    if (ret == PK_OK) {
        for (int i = 0; i < self->_pokey->info.iPinCount; i++) {

            if (self->_pins[i].type == "DIGITAL_INPUT") {
                if (self->_pins[i].value != self->_pokey->Pins[i].DigitalValueGet) {
                    // data has changed so send it ofr for processing
                    self->_pins[i].previousValue = self->_pins[i].value;
                    self->_pins[i].value = self->_pokey->Pins[i].DigitalValueGet;
                    printf("State Change %s %d --> %d\n", self->_pins[i].pinName.c_str(), self->_pins[i].previousValue, self->_pins[i].value);
                }
            }
        }
    }
}

void PokeyDevice::addPin(std::string pinName, int pinNumber, std::string pinType, int defaultValue)
{
    if (pinType == "DIGITAL_OUTPUT")
        outputPin(pinNumber);
    if (pinType == "DIGITAL_INPUT")
        inputPin(pinNumber);

    mapNameToPin(pinName.c_str(), pinNumber);

    _pins[pinNumber - 1].pinName = pinName;
    _pins[pinNumber - 1].type = pinType.c_str();
    _pins[pinNumber - 1].pinNumber = pinNumber;
    _pins[pinNumber - 1].defaultValue = defaultValue;
    _pins[pinNumber - 1].value = defaultValue;
}

void PokeyDevice::startPolling()
{
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
}

void PokeyDevice::stopPolling()
{
    assert(_pollLoop);
    uv_stop(_pollLoop);
}

/**
 *   @brief  Default  destructor for PokeyDevice
 *
 *   @return nothing
 */
PokeyDevice::~PokeyDevice()
{
    PK_DisconnectDevice(_pokey);
}

std::string PokeyDevice::hardwareTypeString()
{
    if (_hardwareType == 31) {
        return "Pokey 57E";
    }
    return "Unknown";
}

bool PokeyDevice::validatePinCapability(int pin, std::string type)
{
    assert(_pokey);
    bool retVal = false;

    if (type == "DIGITAL_OUTPUT") {
        retVal = isPinDigitalOutput(pin - 1);
    }
    else if (type == "DIGITAL_INPUT") {
        retVal = isPinDigitalInput(pin - 1);
    }
    return retVal;
}

uint32_t PokeyDevice::targetValue(std::string targetName, bool value)
{
    uint8_t pin = pinFromName(targetName) - 1;
    uint32_t retValue = PK_DigitalIOSetSingle(_pokey, pin, (uint8_t)value);

    if (retValue == PK_ERR_TRANSFER) {
        printf("----> PK_ERR_TRANSFER pin %d --> %d %d\n\n", pin, (uint8_t)value, retValue);
    }
    else if (retValue == PK_ERR_GENERIC) {
        printf("----> PK_ERR_GENERIC pin %d --> %d %d\n\n", pin, (uint8_t)value, retValue);
    }
    else if (retValue == PK_ERR_PARAMETER) {
        printf("----> PK_ERR_PARAMETER pin %d --> %d %d\n\n", pin, (uint8_t)value, retValue);
    }

    return retValue;
}

uint32_t PokeyDevice::outputPin(uint8_t pin)
{
    _pokey->Pins[--pin].PinFunction = PK_PinCap_digitalOutput;
    return PK_PinConfigurationSet(_pokey);
}

uint32_t PokeyDevice::inputPin(uint8_t pin)
{
    _pokey->Pins[--pin].PinFunction = PK_PinCap_digitalInput;
    return PK_PinConfigurationSet(_pokey);
}

int32_t PokeyDevice::name(std::string name)
{
    char *temp = (char *)std::calloc(1, 30);
    std::memcpy(_pokey->DeviceData.DeviceName, name.c_str(), 30);
    return PK_DeviceNameSet(_pokey);
}

int PokeyDevice::pinFromName(std::string targetName)
{
    std::map<std::string, int>::iterator it;
    it = _pinMap.find(targetName);

    if (it != _pinMap.end()) {
        return it->second;
    }
    else
        return -1;
}

void PokeyDevice::mapNameToPin(std::string name, int pin)
{
    // TODO: what if the pin name already exists as a key
    _pinMap.emplace(name, pin);
}

bool PokeyDevice::isPinDigitalOutput(uint8_t pin)
{
    return (bool)PK_CheckPinCapability(_pokey, pin, PK_AllPinCap_digitalOutput);
}

bool PokeyDevice::isPinDigitalInput(uint8_t pin)
{
    return (bool)PK_CheckPinCapability(_pokey, pin, PK_AllPinCap_digitalInput);
}