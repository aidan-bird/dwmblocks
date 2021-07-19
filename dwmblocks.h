#ifndef DWMBLOCKS_H
#define DWMBLOCKS_H

#define LENGTH(X) (sizeof(X) / sizeof (X[0]))
#define BLOCKLENGTH (CMDLENGTH - (sizeof(delim) - 1))
#define CMDSIZE (CMDLENGTH + 1)
#define NOTIFY_APP_NAME "dwmblocks_notifier"

typedef void (*AppendDelimFunc)(char*);

int execCmd(const char *restrict cmd, char *restrict output);
int sendUrgentNotification(const char *text, int timeout);
int blockEventMpd(char *output);
int blockEventVol(char *output);
int blockEventGetTime(char *output);
int blockEventGetBattery(char *output);
void debugtest(char *output);
int blockEventGetCpuTemp(char *output);
void sighandler(int num);
void appendDelimStr(char *output);
void appendSimpleDelim(char *output);
void appendFastDelim(char *output);
void getcmds(void);
void getcmd(const Block *restrict block, char *restrict output, int isPadded);
void getAllCmds(void);
int getstatus(char *restrict str, char *restrict last);
void setroot(void);
void statusloop(void);
void termhandler(int signum);
void cleanup(void);
#endif
