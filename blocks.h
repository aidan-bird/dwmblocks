#define BATT_NOW "/sys/class/power_supply/BAT0/charge_now"
#define BATT_FULL (3631000 / 100)
#define BATT_STATUS "/sys/class/power_supply/BAT0/status"
#define BATT_STATUS_CHARGING 'C'
#define BATT_STATUS_DISCHARGING 'D'
#define BATT_STATUS_FULL 'F'
#define CPU_TEMP "/sys/class/hwmon/hwmon0/device/temp"
#define MPD_HOST_ADDR "127.0.0.1"
#define MPD_HOST_PORT "0"
#define MPD_HOST_CON_TIMEOUT 3000
#define PADDING 4

static const char delim = '|';

static const Block blocks[] = {
    { 
        /* mpd */
        .func = blockEventMpd,
        .interval = 0,
        .signal = 11,
    }, 
    {
        /* vol */
        .func = blockEventVol, 
        .interval = 0, 
        .signal = 10,
    }, 
    { 
        /* cpu */
        .func = blockEventGetCpuTemp,
        .interval = 5, 
        .signal = 0 
    },
    { 
        /* batt */
        .func = blockEventGetBattery,
        .interval = 30, 
        .signal = 0
    }, 
    { 
        /* date */  
        .func = blockEventGetTime, 
        .interval = 30, 
        .signal = 0
    }, 
};
