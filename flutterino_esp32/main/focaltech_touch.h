#ifndef _FOCALTECH_TOUCH_H
#define _FOCALTECH_TOUCH_H

#include <cstdint>
#include <cstring>

#define FOCALTECH_SLAVE_ADDRESS    (0x38)

typedef enum {
    FOCALTECH_NO_GESTRUE,
    FOCALTECH_MOVE_UP,
    FOCALTECH_MOVE_LEFT,
    FOCALTECH_MOVE_DOWN,
    FOCALTECH_MOVE_RIGHT,
    FOCALTECH_ZOOM_IN,
    FOCALTECH_ZOOM_OUT,
} GesTrue_t;

enum ft_touch_phase {
    FOCALTECH_EVENT_DOWN,
    FOCALTECH_EVENT_UP,
    FOCALTECH_EVENT_CONTACT,
    FOCALTECH_EVENT_NONE,
};

enum ft_power_mode {
    FOCALTECH_PMODE_ACTIVE = 0,         // ~4mA
    FOCALTECH_PMODE_MONITOR = 1,        // ~3mA
    FOCALTECH_PMODE_DEEPSLEEP = 3,      // ~100uA  The reset pin must be pulled down to wake up
};

typedef uint8_t (*iic_com_fptr_u8_t)(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len);

class FocalTech_Class
{
public:

    bool begin(iic_com_fptr_u8_t read_cb, iic_com_fptr_u8_t write_cb, uint8_t addr = FOCALTECH_SLAVE_ADDRESS);

    void    setTheshold(uint8_t value);
    uint8_t getThreshold(void);

    uint8_t getMonitorTime(void);
    void    setMonitorTime(uint8_t sec);
    uint8_t getActivePeriod(void);
    void    setActivePeriod(uint8_t rate);
    uint8_t getMonitorPeriod(void);
    void    setMonitorPeriod(uint8_t rate);

    void    enableAutoCalibration(void);
    void    disableAutoCalibration(void);

    void    getLibraryVersion(uint16_t &version);

    void    setPowerMode(enum ft_power_mode mode);
    enum ft_power_mode    getPowerMode(void);

    uint8_t getVendorID(void);
    uint8_t getVendor1ID(void);
    uint8_t getErrorCode(void);

    void    enableINT(void);
    void    disableINT(void);
    uint8_t getINTMode(void);

    bool    getPoint(uint16_t &x, uint16_t &y);
    // bool    getPoint(uint8_t *x, uint8_t *y);

    uint8_t getTouched(void);

    uint8_t     getControl(void);
    uint8_t     getDeviceMode(void);
    GesTrue_t   getGesture(void);

    enum ft_touch_phase event;
private:
    bool probe(void);

    uint8_t readRegister8( uint8_t reg);
    void writeRegister8( uint8_t reg, uint8_t value);
    bool readBytes( uint8_t reg, uint8_t *data, uint8_t nbytes);
    bool writeBytes( uint8_t reg, uint8_t *data, uint8_t nbytes);


    uint8_t _address;
    bool initialization = false;
    iic_com_fptr_u8_t _readCallbackFunc = nullptr;
    iic_com_fptr_u8_t _writeCallbackFunc = nullptr;
};

#endif
