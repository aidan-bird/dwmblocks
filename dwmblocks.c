#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <libnotify/notify.h>

#include "./blocks.h"

#define LENGTH(X) (sizeof(X) / sizeof (X[0]))
#define BLOCKLENGTH CMDLENGTH - PADDING * 2 + 1
#define NOTIFY_APP_NAME "dwmblocks_notifier"

void sighandler(int num);
void getcmds();
void getcmd(const Block *restrict block, char *restrict output, int isPadded);
int getstatus(char *str, char *last);
void setroot();
void statusloop();
void termhandler(int signum);
void cleanup();
int sendUrgentNotification(const char *text, int timeout);

static Display *dpy;
static int screen;
static Window root;
static unsigned deltas[LENGTH(blocks)];
static char statusbar[LENGTH(blocks)][CMDLENGTH] = {0};
static char statusstr[2][LENGTH(blocks) * CMDLENGTH];
static int statusContinue = 1;
static void (*writestatus) () = setroot;

static unsigned sigBlocks[LENGTH(blocks)];
static unsigned intervalBlocks[LENGTH(blocks)];
static int sigBlocksLen;
static int intervalBlocksLen;
static int isLowBatteryWarnSent;

int
execCmd(const char *restrict cmd, char *restrict output)
{
    int len;
    FILE *cmdf;

    if (!(cmdf = popen(cmd, "r")) || !fgets(output, BLOCKLENGTH, cmdf))
        return 0;
	pclose(cmdf);
    len = strlen(output);
    if (output[len - 1] == '\n')
        return len - 1;
    return len;
}

int
sendUrgentNotification(const char *text, int timeout)
{
    NotifyNotification *notification;

    if (!(notification = notify_notification_new(text, NULL, NULL)))
        return -1;
    notify_notification_set_urgency(notification, NOTIFY_URGENCY_CRITICAL);
    notify_notification_set_timeout(notification, timeout);
    notify_notification_show(notification, NULL);
    g_object_unref(G_OBJECT(notification));
    return 0;
}

int
blockEventMpd(char *output)
{
    /* TODO: replace script with c function */ 
    return execCmd("~/.scripts/i3mpd.sh", output);
}

int
blockEventVol(char *output)
{
    /* TODO: replace script with c function */ 
    return execCmd("~/.scripts/i3vol.sh", output);
}

int
blockEventGetTime(char *output)
{
    struct tm *t;
    time_t epoch;

    epoch = time(NULL);
    if (epoch == (time_t)(-1) || !(t = localtime(&epoch)))
        return 0;
    return strftime(output, BLOCKLENGTH, "%e %h %Y %H:%M", t);
}

int
blockEventGetBattery(char *output)
{
    FILE *fpCharge;
    FILE *fpStatus;
    int currentCharge;
    int batteryStat;
    int ret;
    const char MSG_BATTERY_FULL[] = "BATT 100";
    /* the escaped characters represent a battery emoji*/  
    const char *lowBatteryNotificationText = "\xf0\x9f\x94\x8b Battery low!"; 
    const int lowBatteryNotificationTimeout = 1000 * 5;

    if (!(fpStatus = fopen(BATT_STATUS, "r")))
        goto error1;
    if (!(fpCharge = fopen(BATT_NOW, "r")))
        goto error2;
    batteryStat = getc(fpStatus);
    if (batteryStat == BATT_STATUS_FULL) {
        memcpy(output, MSG_BATTERY_FULL, sizeof(MSG_BATTERY_FULL) - 1);
        return sizeof(MSG_BATTERY_FULL) - 1;
    }
    if (fscanf(fpCharge, "%d", &currentCharge) != 1)
        goto error3;
    switch (batteryStat) {
        case BATT_STATUS_DISCHARGING:
            if ((currentCharge / BATT_FULL) < BATTERY_WARN_LEVEL) {
                if (!isLowBatteryWarnSent) {
                    isLowBatteryWarnSent = 1;
                    sendUrgentNotification(lowBatteryNotificationText,
                        lowBatteryNotificationTimeout);
                }
            }
            ret = sprintf(output, "BATT %d", currentCharge /= BATT_FULL);
            break;
        case BATT_STATUS_CHARGING:
            isLowBatteryWarnSent = 0;
            ret = sprintf(output, "BATT+ %d", currentCharge /= BATT_FULL);
            break;
    }
    fclose(fpCharge);
    fclose(fpStatus);
    return ret;
error3:;
    fclose(fpCharge);
error2:;
    fclose(fpStatus);
error1:;
    return 0;
}

int
blockEventGetCpuTemp(char *output)
{
    FILE *fp;
    int cputemp;
    int ret;

    if (!(fp = fopen(CPU_TEMP, "r")))
        goto error1;
    if (fscanf(fp, "%d", &cputemp) != 1)
        goto error2;
    cputemp /=  1000;
    ret = sprintf(output, "CPU %d°C", cputemp);
    fclose(fp);
    return ret;
error2:;
    fclose(fp);
error1:;
    return 0;
}

void
sighandler(int num)
{
    for (int i = 0; i < sigBlocksLen; i++) {
        if (blocks[sigBlocks[i]].signal == (num - SIGRTMIN)) {
            getcmd(blocks + sigBlocks[i], statusbar[sigBlocks[i]],
                sigBlocks[i] != LENGTH(blocks) - 1);
            writestatus();
            return;
        }
    }
}

void
getcmd(const Block *restrict block, char *restrict output, int isPadded)
{
    int i;

    i = block->func(output);
    if (isPadded && i) {
        memset(output + i, ' ', PADDING * 2 + 1);
        output[i + PADDING] = delim;
        i += PADDING * 2 + 1;
    }
	output[i] = '\0';
}

void
getcmds()
{
    for (int i = 0; i < intervalBlocksLen - 1; i++) {
        if (deltas[intervalBlocks[i]]) {
            deltas[intervalBlocks[i]]--;
        } else {
            deltas[intervalBlocks[i]] = (blocks + intervalBlocks[i])->interval;
            getcmd(blocks + intervalBlocks[i], statusbar[intervalBlocks[i]], 1);
        }
    }
    /* no padding for the last block */ 
    if (deltas[intervalBlocks[intervalBlocksLen - 1]]) {
        deltas[intervalBlocks[intervalBlocksLen - 1]]--;
    } else {
        deltas[intervalBlocks[intervalBlocksLen - 1]] = (blocks + 
            intervalBlocks[intervalBlocksLen - 1])->interval;
        getcmd(blocks + intervalBlocks[intervalBlocksLen - 1],
            statusbar[intervalBlocks[intervalBlocksLen - 1]], 0);
    }
}

void
getAllCmds()
{
    for (int i = 0; i < LENGTH(blocks) - 1; i++)
        getcmd(blocks + i, statusbar[i], 1);
    /* no padding for the last block */ 
    getcmd(blocks + LENGTH(blocks) - 1, statusbar[intervalBlocksLen - 1], 0);
}

int
getstatus(char *str, char *last)
{
	strcpy(last, str);
	str[0] = '\0';
	for (int i = 0; i < LENGTH(blocks); i++)
		strcat(str, statusbar[i]);
	str[strlen(str)] = '\0';
	return strcmp(str, last);
}

void
setroot()
{
    Display *d;
    
	if (!getstatus(statusstr[0], statusstr[1]))
		return;
    if (d = XOpenDisplay(NULL))
        dpy = d;
    if (!dpy)
        exit(-1);
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	XStoreName(dpy, root, statusstr[0]);
    XFlush(dpy);
	XCloseDisplay(dpy);
}

void
statusloop()
{
    getAllCmds();
	while(statusContinue) {
		getcmds();
		writestatus();
		sleep(1);
	}
}

void
termhandler(int signum)
{
	statusContinue = 0;
    cleanup();
	exit(0);
}

void
cleanup()
{
    if (notify_is_initted() == TRUE) {
        puts("Closing notifier");
        notify_uninit();
    }
}

int
main(int argc, char **argv)
{
    int j;
    
    j = 0;
    if (notify_init(NOTIFY_APP_NAME) == FALSE)
        goto error1;
    for (int i = 0; i < LENGTH(blocks); i++)
        if (blocks[i].interval)
            intervalBlocks[j++] = i;
    intervalBlocksLen = j;
    j = 0;
    for (int i = 0; i < LENGTH(blocks); i++) {
        if (blocks[i].signal) {
            signal(SIGRTMIN+blocks[i].signal, sighandler);
            sigBlocks[j++] = i;
        }
    }
    sigBlocksLen = j;
	signal(SIGTERM, termhandler);
	signal(SIGINT, termhandler);
	statusloop();
    return 0;
error1:;
    return -1;
}
