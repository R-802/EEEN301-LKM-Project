/**
 * User-space app for the HC-SR04 ultrasonic LKM.
 * Optional argv[1] or prompt: speed-of-sound calibration (us/m).
 * Polls distance every 0.5 s; re-opens device each read to reset file offset.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#define DEVICE_PATH   "/dev/ultrasonic"
#define BUFFER_LENGTH 256
#define POLL_US       500000

static volatile sig_atomic_t running = 1;
static char receive[BUFFER_LENGTH];

static void handle_sigint(int sig) { (void)sig; running = 0; }

static void trim_trailing(char *s)
{
    size_t n = strlen(s);

    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                     s[n - 1] == ' '  || s[n - 1] == '\t'))
        s[--n] = '\0';
}

static int send_calibration(const char *value)
{
    int fd, ret;

    if (!value[0]) {
        printf("Calibration: default (2915 us/m)\n");
        return 0;
    }

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device for calibration");
        return errno;
    }

    ret = write(fd, value, strlen(value));
    close(fd);
    if (ret < 0) {
        perror("Failed to write calibration");
        return errno;
    }

    printf("Calibration: %s us/m\n", value);
    return 0;
}

int main(int argc, char *argv[])
{
    int ret, fd;
    unsigned long sample = 0;
    char calibration[BUFFER_LENGTH];

    printf("HC-SR04 ultrasonic test\nDevice: %s\n", DEVICE_PATH);

    /* PO3-R2: calibration */
    if (argc > 1) {
        strncpy(calibration, argv[1], BUFFER_LENGTH - 1);
        calibration[BUFFER_LENGTH - 1] = '\0';
    } else {
        printf("Enter speed of sound (us/m) [enter for default 2915]: ");
        if (!fgets(calibration, BUFFER_LENGTH, stdin))
            calibration[0] = '\0';
        calibration[strcspn(calibration, "\n")] = '\0';
    }

    ret = send_calibration(calibration);
    if (ret)
        return ret;

    signal(SIGINT, handle_sigint);

    /* PO3-R1: poll distance */
    printf("\nDistance (0.5 s interval, Ctrl+C to stop):\n");

    while (running) {
        fd = open(DEVICE_PATH, O_RDONLY);
        if (fd < 0) {
            perror("Failed to open device for reading");
            return errno;
        }

        memset(receive, 0, BUFFER_LENGTH);
        ret = read(fd, receive, BUFFER_LENGTH);
        close(fd);

        if (ret < 0) {
            perror("Failed to read from device");
            return errno;
        }

        sample++;
        if (ret == 0) {
            printf("%6lu  (no data)\n", sample);
        } else {
            receive[ret] = '\0';
            trim_trailing(receive);
            printf("%6lu  %s\n", sample, receive);
        }

        fflush(stdout);
        usleep(POLL_US);
    }

    printf("\nStopped.\n");
    return 0;
}
