#ifndef BLOCKS_H
#define BLOCKS_H

#define BATT_WARN_TIMEOUT 1000 * 5
#define BATT_NOW "/sys/class/power_supply/BAT0/capacity"
#define BATT_WARN_LEVEL 10
#define BATT_STATUS "/sys/class/power_supply/BAT0/status"
#define BATT_STATUS_CHARGING 'C'
#define BATT_STATUS_DISCHARGING 'D'
#define BATT_STATUS_FULL 'F'
#define BATT_STATUS_UNKNOWN 'U'

#define CPU_TEMP "/sys/class/thermal/thermal_zone0/temp"

#define MPD_HOST_ADDR "127.0.0.1"
#define MPD_HOST_PORT "0"
#define MPD_HOST_CON_TIMEOUT 3000

#define CMDLENGTH 127

typedef struct Block Block;
typedef int(*BlockFunc)(char *output);

struct Block {
    BlockFunc func;
	unsigned interval;
	unsigned signal;
};

/* functions that can be called by a block */ 
int blockEventGetTime(char *output);
int blockEventGetBattery(char *output);
int blockEventGetCpuTemp(char *output);
int blockEventMpd(char *output);
int blockEventVol(char *output);

static const char delim[] = "    |    ";


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
#endif
