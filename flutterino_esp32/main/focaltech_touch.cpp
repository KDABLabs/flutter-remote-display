/////////////////////////////////////////////////////////////////
/*
MIT License

Copyright (c) 2019 lewis he

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

focaltech.cpp - Arduino library for focaltech chip, support FT5206,FT6236,FT5336,FT6436L,FT6436
Created by Lewis on April 17, 2019.
github:https://github.com/lewisxhe/FocalTech_Library
*/
/////////////////////////////////////////////////////////////////
#include "focaltech_touch.h"
#include "esp_log.h"


#define FT5206_VENDID                   (0x11)
#define FT6206_CHIPID                   (0x06)
#define FT6236_CHIPID                   (0x36)
#define FT6236U_CHIPID                  (0x64)
#define FT5206U_CHIPID                  (0x64)

#define FOCALTECH_REGISTER_MODE         (0x00)
#define FOCALTECH_REGISTER_GEST         (0x01)
#define FOCALTECH_REGISTER_STATUS       (0x02)
#define FOCALTECH_REGISTER_TOUCH1_XH    (0x03)
#define FOCALTECH_REGISTER_TOUCH1_XL    (0x04)
#define FOCALTECH_REGISTER_TOUCH1_YH    (0x05)
#define FOCALTECH_REGISTER_TOUCH1_YL    (0x06)
#define FOCALTECH_REGISTER_THRESHHOLD   (0x80)
#define FOCALTECH_REGISTER_CONTROL      (0x86)
#define FOCALTECH_REGISTER_MONITORTIME  (0x87)
#define FOCALTECH_REGISTER_ACTIVEPERIOD  (0x88)
#define FOCALTECH_REGISTER_MONITORPERIOD  (0x89)

#define FOCALTECH_REGISTER_LIB_VERSIONH (0xA1)
#define FOCALTECH_REGISTER_LIB_VERSIONL (0xA2)
#define FOCALTECH_REGISTER_INT_STATUS   (0xA4)
#define FOCALTECH_REGISTER_POWER_MODE   (0xA5)
#define FOCALTECH_REGISTER_VENDOR_ID    (0xA3)
#define FOCALTECH_REGISTER_VENDOR1_ID   (0xA8)
#define FOCALTECH_REGISTER_ERROR_STATUS (0xA9)

static const char *vendor_id_to_string(uint8_t id)
{
    switch (id) {
    case FT5206_VENDID:
        return "FT5206";
    case FT6206_CHIPID:
        return "FT6206";
    case FT6236_CHIPID:
        return "FT6236";
    case FT6236U_CHIPID:
        return "FT6236U / FT5206U";
    default:
        return "Unknown";
    }
}

bool FocalTech_Class::probe(void) {
    uint8_t vendor = readRegister8(FOCALTECH_REGISTER_VENDOR_ID);
    if (vendor == 0) {
        return false;
    }

    ESP_LOGI("FocalTech driver", "Vendor ID: %s", vendor_id_to_string(vendor));

    initialization = true;
    return true;
}

bool FocalTech_Class::begin(iic_com_fptr_u8_t read_cb, iic_com_fptr_u8_t write_cb, uint8_t addr)
{
    if (read_cb == nullptr || write_cb == nullptr) {
        return false;
    }

    _readCallbackFunc = read_cb;
    _writeCallbackFunc = write_cb;
    _address = addr;
    return probe();
}

uint8_t FocalTech_Class::getControl(void)
{
    if (!initialization) {
        return 0;
    }

    return readRegister8(FOCALTECH_REGISTER_CONTROL);
}

uint8_t FocalTech_Class::getDeviceMode(void)
{
    if (!initialization) {
        return 0;
    }

    return (readRegister8(FOCALTECH_REGISTER_MODE) >> 4) & 0x07;
}

GesTrue_t FocalTech_Class::getGesture(void)
{
    if (!initialization) {
        return FOCALTECH_NO_GESTRUE;
    }

    uint8_t val = readRegister8(FOCALTECH_REGISTER_GEST);
    switch (val) {
    case 0x10:
        return FOCALTECH_MOVE_UP;
    case 0x14:
        return FOCALTECH_MOVE_RIGHT;
    case 0x18:
        return FOCALTECH_MOVE_DOWN;
    case 0x1C:
        return FOCALTECH_MOVE_LEFT;
    case 0x48:
        return FOCALTECH_ZOOM_IN;
    case 0x49:
        return FOCALTECH_ZOOM_OUT;
    default:
        break;
    }

    return FOCALTECH_NO_GESTRUE;
}

void FocalTech_Class::setTheshold(uint8_t value)
{
    if (!initialization) {
        return;
    }

    writeRegister8(FOCALTECH_REGISTER_THRESHHOLD, value);
}

uint8_t FocalTech_Class::getThreshold(void)
{
    if (!initialization) {
        return 0;
    }

    return readRegister8(FOCALTECH_REGISTER_THRESHHOLD);
}

uint8_t FocalTech_Class::getMonitorTime(void)
{
    if (!initialization) {
        return 0;
    }

    return readRegister8(FOCALTECH_REGISTER_MONITORTIME);
}

void FocalTech_Class::setMonitorTime(uint8_t sec)
{
    if (!initialization) {
        return;
    }

    writeRegister8(FOCALTECH_REGISTER_MONITORTIME, sec);
}

uint8_t FocalTech_Class::getActivePeriod(void)
{
    if (!initialization) {
        return 0;
    }

    return readRegister8(FOCALTECH_REGISTER_ACTIVEPERIOD);
}

void FocalTech_Class::setActivePeriod(uint8_t period)
{
    if (!initialization) {
        return;
    }

    writeRegister8(FOCALTECH_REGISTER_ACTIVEPERIOD, period);
}

uint8_t FocalTech_Class::getMonitorPeriod(void)
{
    if (!initialization) {
        return 0;
    }

    return readRegister8(FOCALTECH_REGISTER_MONITORPERIOD);
}

void FocalTech_Class::setMonitorPeriod(uint8_t period)
{
    if (!initialization) {
        return;
    }

    writeRegister8(FOCALTECH_REGISTER_MONITORPERIOD, period);
}

void FocalTech_Class::enableAutoCalibration(void)
{
    if (!initialization) {
        return;
    }

    writeRegister8(FOCALTECH_REGISTER_MONITORTIME, 0x00);
}

void FocalTech_Class::disableAutoCalibration(void)
{
    if (!initialization) {
        return;
    }

    writeRegister8(FOCALTECH_REGISTER_MONITORTIME, 0xFF);
}

void FocalTech_Class::getLibraryVersion(uint16_t &version)
{
    if (!initialization) {
        return;
    }

    uint8_t buffer[2];
    readBytes(FOCALTECH_REGISTER_LIB_VERSIONH, buffer, 2);
    version = (buffer[0] << 8) | buffer[1];
}

void FocalTech_Class::enableINT(void)
{
    if (!initialization) {
        return;
    }

    writeRegister8(FOCALTECH_REGISTER_INT_STATUS, 1);
}

void FocalTech_Class::disableINT(void)
{
    if (!initialization) {
        return;
    }

    writeRegister8(FOCALTECH_REGISTER_INT_STATUS, 0);
}

uint8_t FocalTech_Class::getINTMode(void)
{
    if (!initialization) {
        return 0;
    }

    return readRegister8(FOCALTECH_REGISTER_INT_STATUS);
}

bool FocalTech_Class::getPoint(uint16_t &x, uint16_t &y)
{
    if (!initialization) {
        return false;
    }

    uint8_t buffer[5];

    bool ok = readBytes(FOCALTECH_REGISTER_STATUS, buffer, 5);
    if (!ok) {
        return false;
    }

    if (buffer[0] == 0 || buffer[0] > 2) {
        return false;
    }

    event = (enum ft_touch_phase) ((buffer[1] & 0xC0) >> 6);
    x = (buffer[1] & 0x0F) << 8 | buffer[2];
    y =  (buffer[3] & 0x0F) << 8 | buffer[4];

    // printf("x=%03u y=%03u\n", x, y);

    return true;
}

uint8_t FocalTech_Class::getTouched()
{
    if (!initialization) {
        return 0;
    }

    return readRegister8(FOCALTECH_REGISTER_STATUS);
}

void FocalTech_Class::setPowerMode(enum ft_power_mode mode)
{
    if (!initialization) {
        return;
    }

    writeRegister8(FOCALTECH_REGISTER_POWER_MODE, mode);
}

enum ft_power_mode FocalTech_Class::getPowerMode(void)
{
    if (!initialization) {
        return FOCALTECH_PMODE_DEEPSLEEP;
    }

    return (enum ft_power_mode) readRegister8(FOCALTECH_REGISTER_POWER_MODE);
}

uint8_t FocalTech_Class::getVendorID(void)
{
    if (!initialization) {
        return 0;
    }

    return readRegister8(FOCALTECH_REGISTER_VENDOR_ID);
}

uint8_t FocalTech_Class::getVendor1ID(void)
{
    if (!initialization) {
        return 0;
    }

    return readRegister8(FOCALTECH_REGISTER_VENDOR1_ID);
}

uint8_t FocalTech_Class::getErrorCode(void)
{
    if (!initialization) {
        return 0;
    }

    return readRegister8(FOCALTECH_REGISTER_ERROR_STATUS);
}

// bool FocalTech_Class::getPoint(uint8_t *x, uint8_t *y)
// {
//     if (!initialization || x == nullptr || y == nullptr) {
//         return false;
//     }
//     // uint16_t _id[2];
//     uint8_t buffer[16];
//     readBytes(FOCALTECH_REGISTER_MODE, buffer, 16);
//     if (buffer[FOCALTECH_REGISTER_STATUS] == 0) {
//         return false;
//     }
//     for (uint8_t i = 0; i < 2; i++) {
//         x[i] = buffer[FOCALTECH_REGISTER_TOUCH1_XH + i * 6] & 0x0F;
//         x[i] <<= 8;
//         x[i] |= buffer[FOCALTECH_REGISTER_TOUCH1_XL + i * 6];
//         y[i] = buffer[FOCALTECH_REGISTER_TOUCH1_YH + i * 6] & 0x0F;
//         y[i] <<= 8;
//         y[i] |= buffer[FOCALTECH_REGISTER_TOUCH1_YL + i * 6];
//         // _id[i] = buffer[FOCALTECH_REGISTER_TOUCH1_YH + i * 6] >> 4;
//     }
//     return true;
// }

uint8_t FocalTech_Class::readRegister8(uint8_t reg)
{
    uint8_t value;
    (void)readBytes(reg, &value, 1);
    return value;
}

void FocalTech_Class::writeRegister8(uint8_t reg, uint8_t value)
{
    (void)writeBytes(reg, &value, 1);
}

bool FocalTech_Class::readBytes( uint8_t reg, uint8_t *data, uint8_t nbytes)
{
    return _readCallbackFunc(_address, reg, data, nbytes);
}

bool FocalTech_Class::writeBytes( uint8_t reg, uint8_t *data, uint8_t nbytes)
{
    return _writeCallbackFunc(_address, reg, data, nbytes);
}
