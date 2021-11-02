#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#define program_name "lunix"

#define authors "Konstantinos Papaioannou and Orfeas Zografos"

int sensorsarr[20];
char dir[254];
char measurementsarr[8][10];
int sensit = 0;
int measit = 0;
int timeOfLog = 0;

void alarm_handler(int signum)
{

    //If time passes without read then exit
    exit(0);
}

void wait_for_ready_children(int cnt)
{
    int i;
    pid_t p;
    int status;

    for (i = 0; i < cnt; i++)
    {
        /* Wait for any child, also get status for stopped children */
        p = waitpid(-1, &status, WUNTRACED);
        if (!WIFSTOPPED(status))
        {
            fprintf(stderr, "Process with PID %ld has died unexpectedly!\n",
                    (long)p);
            exit(1);
        }
    }
}

void wait_for_terminated_children(int cnt)
{
    int i;
    pid_t p;
    int status;

    for (i = 0; i < cnt; i++)
    {
        /* Wait for any child, also get status for terminated children */
        p = waitpid(-1, &status, 0);

        if (p == -1)
        {
            fprintf(stderr, "Process with PID %ld has died unexpectedly!\n", (long)p);
            exit(1);
        }
    }
}

void usage(void)
{

    printf("Usage: %s [DIR] [TIME] [OPTIONS]...\n", program_name);

    printf("Userspace program keeping track of lunix device driver measurements and presentig them.\n");

    printf("[DIR] Path of directory to store log file(s).\n");
    printf("[TIME] Time in second(s) to track sensor(s) and measurement(s).\n");

    printf("\n\
    -A, --track-all          track all sensors and mesurements\n\
    -s, --sensors            sensors numbers to be tracked\n\
    -m, --measurements       measurements to be tracked\n\
    ");

    printf("\
    \n\
    -o, --output             print all tracked measurements to standard output\n\
    -v, --version            output version information and exit\n\
    -h, --help               disply this help text and exit\n");

    printf("\nExamples:\n\
    %s . 10 -s 1 2                  Create log files for all measurements of sensors 1 and 2 in current directory for 10 secs.\n\
    %s lunix-logs 20 -s 1 -m batt   Create log file of sensor 1 battery measurement inside lunix-logs for 20 secs.\n",
           program_name, program_name);

    exit(0);
}

void version(void)
{
    printf("%s\n", program_name);
    printf("Copyright (C) 2020.\n");
    printf("Written by %s\n", authors);

    exit(0);
}

int collect_sensors(int i, char *argv[], int argc)
{
    int j = 0;

    while (i < argc)
    {
        if (argv[i][0] == '-')
            break;

        if (atoi(argv[i]) > 2)
        {
            printf("Please specify a valid device number. 0, 1, 2 are valid\n");
            exit(0);
        }
        sensorsarr[j] = atoi(argv[i]);
        j++;
        i++;
    }

    sensit = j;
    return i - 1;
}

int collect_measurements(int i, char *argv[], int argc)
{
    int j = 0;

    while (i < argc)
    {
        if (argv[i][0] == '-')
            break;

        if (strcmp(argv[i], "batt") == 0)
        {
            strcpy(measurementsarr[j], "batt");
        }
        else if (strcmp(argv[i], "temp") == 0)
        {
            strcpy(measurementsarr[j], "temp");
        }
        else if (strcmp(argv[i], "light") == 0)
        {
            strcpy(measurementsarr[j], "light");
        }
        else
        {
            printf("Please specify a valid device type. batt, temp, light are valid\n");
            exit(0);
        }
        j++;
        i++;
    }

    measit = j;
    return i - 1;
}

void create_log_files(int sensorcnt, int measurecnt, int output)
{
    pid_t pid;
    pid_t pid_table[sensorcnt * measurecnt];
    int i, j, cnt;

    cnt = 0;
    for (i = 0; i < sensorcnt; i++)
    {
        for (j = 0; j < measurecnt; j++)
        {
            pid = fork();
            if (pid < 0)
            {
                printf("Fork for /dev/lunix%d-%s failed\n", sensorsarr[i], measurementsarr[j]);
                exit(1);
            }
            if (pid == 0)
            {
                int fd, logfd;
                time_t startTime, endTime, timeNow;
                struct tm *tm_info;
                char specialFile[32];
                char logFile[32];
                char timeBuffer[70];
                char writableData[90];
                unsigned char readedData[20];

                //printf("Trying to open /dev/lunix%d-%s\n", sensorsarr[i], measurementsarr[j]);

                sprintf(specialFile, "/dev/lunix%d-%s", sensorsarr[i], measurementsarr[j]);
                fd = open(specialFile, O_RDONLY | O_CLOEXEC);
                if (fd < 0)
                {
                    printf("Couldn't open /dev/lunix%d-%s\n", sensorsarr[i], measurementsarr[j]);
                    raise(SIGSTOP);
                    exit(1);
                }

                //printf("Trying to open %s/log-lunix%d-%s\n", dir, sensorsarr[i], measurementsarr[j]);

                sprintf(logFile, "%s/log-lunix%d-%s", dir, sensorsarr[i], measurementsarr[j]);
                logfd = open(logFile, O_CREAT | O_WRONLY | O_APPEND | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
                if (logfd < 0)
                {
                    printf("Couldn't open log-lunix%d-%s\n", sensorsarr[i], measurementsarr[j]);
                    close(fd);
                    raise(SIGSTOP);
                    exit(1);
                }

                raise(SIGSTOP);
                signal(SIGALRM, alarm_handler);

                startTime = time(NULL);
                endTime = startTime + timeOfLog;
                alarm (timeOfLog + 5);

                while (startTime < endTime)
                {
                    if (read(fd, readedData, 20))
                    {
                        timeNow = time(NULL);
                        tm_info = localtime(&timeNow);
                        strftime(timeBuffer, sizeof(timeBuffer), "%c", tm_info);

                        //Output result
                        if (output == 1)
                        {
                            printf("Sensor %d-%s: %s|%s", sensorsarr[i], measurementsarr[j], timeBuffer, readedData);
                        }

                        sprintf(writableData, "%s|%s", timeBuffer, readedData);
                        write(logfd, writableData, sizeof(writableData));
                    }

                    startTime = time(NULL);
                }

                //printf("Terminating myslef %d\n", getpid());

                close(logfd);
                close(fd);
                sleep(2);
                exit(0);
            }

            pid_table[cnt] = pid;
            cnt++;
        }
    }

    //Wait for children to open files
    //printf("Wait for children to open files\n");
    wait_for_ready_children(sensorcnt * measurecnt);

    //Tell them to start logging
    //printf("Tell them to start logging\n");
    for (i = 0; i < sensorcnt * measurecnt; i++)
    {
        kill(pid_table[i], SIGCONT);
    }

    //Progress bar implementation
    if (output == 0)
    {
        time_t startTimer = time(NULL);
        time_t endTimer = startTimer + timeOfLog;
        float progress;
        char buffer[100];
        int i, bytes;

        strcpy(buffer, "\rProgress: [");
        while (startTimer < endTimer)
        {
            //printf("Progress %d\n", ((endTimer - startTimer) / timeOfLog));
            progress = ((timeOfLog - endTimer + startTimer) / (float)timeOfLog) * 100;
            i = 1;
            for (i = 1; i < 39; i++)
            {
                if (progress >= i * 2.5)
                {
                    buffer[11 + i] = '#';
                }
                else
                {
                    buffer[11 + i] = '.';
                }
            }
            fprintf(stdout, "%s]", buffer);
            usleep(900);
            startTimer = time(NULL);
        }
        printf("\n");
    }

    printf("Finishing...\n");
    //Wait for them to terminate
    wait_for_terminated_children(sensorcnt * measurecnt);
}

int main(int argc, char *argv[])
{
    struct stat st;
    int i;
    int children = 0;

    if (argc > 1)
    {
        if ((strcmp(argv[1], "-h") == 0) | (strcmp(argv[1], "--help") == 0))
        {
            usage();
        }
        if ((strcmp(argv[1], "-v") == 0) | (strcmp(argv[1], "--version") == 0))
        {
            version();
        }
    }

    if (argc < 3)
    {
        printf("Usage: %s [DIR] [TIME] [OPTIONS]...\n", program_name);
        exit(0);
    }

    /*Check if [DIR] is actually a directory */
    stat(argv[1], &st);
    if (!S_ISDIR(st.st_mode))
    {
        printf("[DIR] is not a directory\n");
        exit(0);
    }
    strcpy(dir, argv[1]);

    /*Check if [TIME] is an integer */
    if (atoi(argv[2]) > 0)
    {
        timeOfLog = atoi(argv[2]);
    }
    else
    {
        printf("[TIME] is not a valid number of seconds\n");
        exit(0);
    }

    /* Variables that are set according to the specified options.  */
    int track_all = 0;
    int sensors = 0;
    int measurements = 0;
    int output = 0;

    /* Parse arguments */
    i = 3;
    while (i != argc)
    {
        if (strcmp(argv[i], "-A") == 0 || strcmp(argv[i], "--track-all") == 0)
        {
            track_all = 1;
        }

        else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--sensors") == 0)
        {
            sensors = 1;
            i = collect_sensors(i + 1, argv, argc);
            if (sensit == 0)
            {
                printf("Unused option -s!\n");
                exit(0);
            }
        }

        else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--measurements") == 0)
        {
            measurements = 1;
            i = collect_measurements(i + 1, argv, argc);
            if (measit == 0)
            {
                printf("Unused option -m!\n");
                exit(0);
            }
        }

        else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0)
        {
            output = 1;
        }

        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
        {
            version();
        }

        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            usage();
        }
        else
        {
            printf("Invalid parameter, please use -help to learn more.\n");
            exit(0);
        }

        i++;
    }

    if ((track_all == 1 && sensors == 1) || (track_all == 1 && measurements == 1))
    {
        printf("Parameter -A can't be used with -s or -m enabled.\n");
        exit(0);
    }

    if (track_all == 1)
    {
        //Add all default sensors
        sensit = 3;
        i = 0;
        for (int i = 0; i < sensit; i++)
        {
            sensorsarr[i] = i;
        }

        //Add all default measurements
        measit = 3;
        strcpy(measurementsarr[0], "batt");
        strcpy(measurementsarr[1], "temp");
        strcpy(measurementsarr[2], "light");
        create_log_files(sensit, measit, output);
    }

    else if (sensors == 1 && measurements == 1)
    {
        create_log_files(sensit, measit, output);
    }

    else if (sensors == 1)
    {
        //Add all default measurements
        measit = 3;
        strcpy(measurementsarr[0], "batt");
        strcpy(measurementsarr[1], "temp");
        strcpy(measurementsarr[2], "light");
        create_log_files(sensit, measit, output);
    }

    else if (measurements == 1)
    {
        //Add all default sensors
        sensit = 3;
        i = 0;
        for (int i = 0; i < sensit; i++)
        {
            sensorsarr[i] = i;
        }
        create_log_files(sensit, measit, output);
    }

    return 0;
}