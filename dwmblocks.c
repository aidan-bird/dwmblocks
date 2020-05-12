/* 
 * RATIONAL
 * Most of the changes are made with my system in mind (thinkpad x220 
 * + Arch Linux). These changes may not work for your system.
 *
 * CHANGES 
 * - Removed OpenBSD code. (I dont run OpenBSD)
 * - Removed block icons. (I dont use this feature)
 * - Removed command args. (or this one)
 * - Removed pstdout().
 * - Added padding. (I wanted a look similar to i3blocks)
 * - Added various functions that replace the scripts that would otherwise be
 *   called by dwmblocks. I want the functionality of these scripts to be baked
 *   into the program.
 * - Increased cmd length to 128. (To accommodate long song names for the mpd
 *   block)
 * - Made the size of statusstr depend on amount of blocks.
 * - Assume that the amt of blocks > 0.
 * - Assume that delim is not \0.
 * - Changed code style.
 * - Changed the fields in the Block struct. The block struct now contains
 *   a func pointer instead of a shell command. Scripts can be called using
 *   a helper func and execCmd().
 * - Changed how the interval system works. 
 */ 

#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <X11/Xlib.h>

#define LENGTH(X) (sizeof(X) / sizeof (X[0]))
#define CMDLENGTH 128

typedef struct Block Block;

typedef int(*BlockFunc)(char *output);

struct Block {
    BlockFunc func;
	unsigned interval;
	unsigned signal;
};

void sighandler(int num);
void getcmds();
void getcmd(const Block *restrict block, char *restrict output, int isPadded);
int getstatus(char *str, char *last);
void setroot();
void statusloop();
void termhandler(int signum);

/* BlockFuncs */ 
int blockEventGetTime(char *output);
int blockEventGetBattery(char *output);
int blockEventGetCpuTemp(char *output);
int blockEventMpd(char *output);
int blockEventVol(char *output);

#include "./blocks.h"

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

#define BLOCKLENGTH CMDLENGTH - PADDING * 2 + 1

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
blockEventMpd(char *output)
{
    /* TODO: replace script with c function */ 
    /* Can you tell that I used to use i3? */ 
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
    int i;

    if (!(fpStatus = fopen(BATT_STATUS, "r")))
        goto error1;
    switch (getc(fpStatus)) {
        case BATT_STATUS_FULL:
            *((uint64_t *)output) = *(uint64_t *)"BATT 100";
            return 8;
        case BATT_STATUS_CHARGING:
            *((uint64_t *)output) = *(uint64_t *)"BATT +\0\0";
            i = 6;
            break;
        default:
            *((uint64_t *)output) = *(uint64_t *)"BATT \0\0\0";
            i = 5;
            break;
    }
    if (!(fpCharge = fopen(BATT_NOW, "r")))
        goto error2;
    fscanf(fpCharge, "%d", &currentCharge);
    fclose(fpCharge);
    fclose(fpStatus);
    return sprintf(output + i, "%d", currentCharge /= BATT_FULL) + i;
error2:;
    fclose(fpStatus);
error1:;
    return 0;
}

int
blockEventGetCpuTemp(char *output)
{
    const char degC[] = "°C\0"; /* this actually takes up 4 bytes */
    const char cpu[] = "CPU ";
    FILE *fp;

    if (!(fp = fopen(CPU_TEMP, "r")))
        return 0;
    *(uint32_t *)output = *(uint32_t*)cpu;
    fread(output + 4, 1, 3, fp);
    fclose(fp);
    if (output[6] == '0') {
        *(uint32_t *)((void *)output + 6) = *(uint32_t*)degC;
        return 9;
    }
    *(uint64_t *)((void *)output + 7) = *(uint64_t*)degC;
    return 10;
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
	exit(0);
}

int
main(int argc, char **argv)
{
    int j;
    
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
}
