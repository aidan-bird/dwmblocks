#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <libnotify/notify.h>

#include "./blocks.h"
#include "./dwmblocks.h"

static Display *dpy;
static int screen;
static Window root;
static unsigned deltas[LENGTH(blocks)];
static char statusbar[LENGTH(blocks)][CMDSIZE] = {0};
static char statusstr[2][LENGTH(blocks) * CMDSIZE];
static int statusContinue = 1;
static void (*writestatus) () = setroot;
static unsigned sigBlocks[LENGTH(blocks)];
static unsigned intervalBlocks[LENGTH(blocks)];
static int sigBlocksLen;
static int intervalBlocksLen;
static int isLowBatteryWarnSent;
static AppendDelimFunc delimFunc = appendDelimStr;

/*
 * REQUIRES
 * output is a buffer that can store (BLOCKLENGTH + 1) or more characters
 *
 * MODIFIES
 * output 
 *
 * EFFECTS
 * execute the command given by cmd and write the output to output.
 * returns the number of characters written excluding the null terminator.
 */
int
execCmd(const char *restrict cmd, char *restrict output)
{
    int len;
    FILE *cmdf;

    if (!(cmdf = popen(cmd, "r")) || !fgets(output, BLOCKLENGTH + 1, cmdf))
        return 0;
	pclose(cmdf);
    len = strlen(output);
    if (output[len - 1] == '\n')
        len--;
    output[len] = '\0';
    return len;
}

/*
 * REQUIRES
 * output is a buffer that can store (BLOCKLENGTH + 1) or more characters
 *
 * MODIFIES
 * none
 *
 * EFFECTS
 * emits a desktop notification that displays text and exits after timeout.
 * returns non-zero on error.
 */
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

/*
 * REQUIRES
 * i3mpd.sh
 * output is a buffer that can store (BLOCKLENGTH + 1) or more characters
 *
 * MODIFIES
 * output
 *
 * EFFECTS
 * writes the name of the currently playing song to output.
 * returns the number of characters written to output.
 */
int
blockEventMpd(char *output)
{
    /* TODO: replace script with c function */ 
    return execCmd("i3mpd.sh", output);
}

/*
 * REQUIRES
 * i3vol.sh
 * output is a buffer that can store (BLOCKLENGTH + 1) or more characters
 *
 * MODIFIES
 * output
 *
 * EFFECTS
 * writes the current sound volume to output.
 * returns the number of characters written to output.
 */
int
blockEventVol(char *output)
{
    /* TODO: replace script with c function */ 
    return execCmd("i3vol.sh", output);
}

/*
 * REQUIRES
 * output is a buffer that can store (BLOCKLENGTH + 1) or more characters
 *
 * MODIFIES
 * output
 *
 * EFFECTS
 * writes the current date and time to output.
 * returns the number of characters written to output.
 */
int
blockEventGetTime(char *output)
{
    struct tm *t;
    time_t epoch;

    epoch = time(NULL);
    if (epoch == (time_t)(-1) || !(t = localtime(&epoch)))
        return 0;
    return strftime(output, BLOCKLENGTH + 1, "%e %h %Y %H:%M", t);
}

/*
 * REQUIRES 
 * output is a buffer that can store (BLOCKLENGTH + 1) or more characters
 *
 * MODIFIES
 * output
 *
 * EFFECTS
 * writes the current battery percentage and status to output
 * returns the number of characters written to output.
 */
int
blockEventGetBattery(char *output)
{
    FILE *fpCharge;
    FILE *fpStatus;
    int currentCharge;
    int batteryStat;
    int ret;
    static const char *msgBatteryFull = "BATT 100";
    static const char *lowBatteryNotificationText = "🔋 Battery low!"; 

    if (!(fpStatus = fopen(BATT_STATUS, "r")))
        goto error1;
    if ((batteryStat = getc(fpStatus)) == EOF)
        goto error2;
    fclose(fpStatus);
    if (batteryStat == BATT_STATUS_FULL) {
        return sprintf(output, "%s", msgBatteryFull);
    }
    if (!(fpCharge = fopen(BATT_NOW, "r")))
        goto error3;
    if (fscanf(fpCharge, "%d", &currentCharge) != 1)
        goto error4;
    fclose(fpCharge);
    switch (batteryStat) {
        case BATT_STATUS_DISCHARGING:
            if (currentCharge < BATT_WARN_LEVEL) {
                if (!isLowBatteryWarnSent) {
                    isLowBatteryWarnSent = 1;
                    sendUrgentNotification(lowBatteryNotificationText,
                        BATT_WARN_TIMEOUT);
                }
            }
            ret = sprintf(output, "BATT %d", currentCharge);
            break;
        case BATT_STATUS_CHARGING:
            isLowBatteryWarnSent = 0;
            ret = sprintf(output, "BATT %d+", currentCharge);
            break;
        case BATT_STATUS_UNKNOWN:
            ret = sprintf(output, "BATT %d?", currentCharge);
            break;
    }
    return ret;
error4:;
    fclose(fpCharge);
error3:;
    return 0;
error2:;
    fclose(fpStatus);
error1:;
    return 0;
}

void
debugtest(char *output)
{
    memset(output, 'a', BLOCKLENGTH);
    output[BLOCKLENGTH] = '\0';
}

/*
 * REQUIRES 
 * output is a buffer that can store (BLOCKLENGTH + 1) or more characters
 *
 * MODIFIES
 * output
 *
 * EFFECTS
 * writes the current cpu temperature.
 * returns the number of characters written to output.
 */
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

/*
 * REQUIRES 
 *
 * MODIFIES
 *
 * EFFECTS
 *
 */
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
appendDelimStr(char *output)
{
    memcpy(output, delim, sizeof(delim));
    output[sizeof(delim)] = '\0';
}

void
appendSimpleDelim(char *output)
{}

void
appendFastDelim(char *output)
{}

/*
 * REQUIRES 
 * output is a buffer that can store whatever is required by the function 
 * in block + the separator string.
 *
 * MODIFIES
 * output
 *
 * EFFECTS
 * calls the function in block and writes its output to output, then a 
 * separator string is appended to the output.
 * setting isPadded to zero will not append the separator string.
 * no separator string will be appended if the block function writes zero 
 * characters.
 */
void
getcmd(const Block *restrict block, char *restrict output, int isPadded)
{
    int i;

    i = block->func(output);
    if (isPadded && i)
        delimFunc(output + i);
}

/*
 * REQUIRES 
 * none
 *
 * MODIFIES
 * deltas, statusbar
 *
 * EFFECTS
 * updates all interval blocks
 */
void
getcmds()
{
    for (int i = 0; i < intervalBlocksLen; i++) {
        if (deltas[intervalBlocks[i]]) {
            deltas[intervalBlocks[i]]--;
        } else {
            deltas[intervalBlocks[i]] = (blocks + intervalBlocks[i])->interval;
            /* no padding for last block */
            getcmd(blocks + intervalBlocks[i], statusbar[intervalBlocks[i]], 
                intervalBlocksLen - (i + 1));
        }
    }
}

/*
 * REQUIRES 
 * none
 *
 * MODIFIES
 * statusbar
 *
 * EFFECTS
 * executes all block functions
 */
void
getAllCmds()
{
    /* no padding for last block */ 
    for (int i = 0; i < LENGTH(blocks); i++)
        getcmd(blocks + i, statusbar[i], LENGTH(blocks) - (i + 1));
}

/*
 * REQUIRES 
 * str and last are both buffer.
 * last can store at least what str can store.
 * str and last can store at least CMDSIZE characters
 *
 * MODIFIES
 * str, last
 *
 * EFFECTS
 * last is used to store str before it is used to store the next statusbar string.
 * the next statusbar string does not differ from the last, then non-zero is returned.
 */
int
getstatus(char *restrict str, char *restrict last)
{
	strcpy(last, str);
	str[0] = '\0';
	for (int i = 0; i < LENGTH(blocks); i++)
		strcat(str, statusbar[i]);
	str[strlen(str)] = '\0';
	return strcmp(str, last);
}

/*
 * REQUIRES 
 * none
 *
 * MODIFIES
 * statusstr, statusbar
 *
 * EFFECTS
 * updates the status bar.
 * will terminate the program on error.
 */
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

/*
 * REQUIRES 
 * none
 *
 * MODIFIES
 * none
 *
 * EFFECTS
 * continuously updates the status bar
 */
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

/*
 * REQUIRES 
 * none
 *
 * MODIFIES
 * none
 *
 * EFFECTS
 * called when the program receives SIGTERM or SIGINT.
 * cleans up and terminates the program.
 */
void
termhandler(int signum)
{
	statusContinue = 0;
    cleanup();
	exit(0);
}

/*
 * REQUIRES 
 * none
 *
 * MODIFIES
 * none
 *
 * EFFECTS
 * cleans up the program before exiting.
 */
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
   
    /* set up the blocks */ 
    if (notify_init(NOTIFY_APP_NAME) == FALSE)
        goto error1;
    j = 0;
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
