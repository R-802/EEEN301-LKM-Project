/* HC-SR04 LKM via PRU shared RAM.
 * read:  distance (cm below 1 m, metres above)
 * write: speed-of-sound calibration (us/m)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/timer.h>

#define DEVICE_NAME "ultrasonic"
#define CLASS_NAME  "ultra"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EEEN301");
MODULE_DESCRIPTION("HC-SR04 ultrasonic sensor LKM via PRU");
MODULE_VERSION("0.6");

#define SENSOR_COUNT    1
#define HISTORY_SIZE    8
#define PRU_SHARED_PHYS 0x4A310000
#define PRU_SHARED_SIZE 0x3000
#define DEFAULT_SPEED   2915
#define POLL_MS         100
#define METRE_THRESHOLD_MM 1000

struct pru_data {
    uint32_t sensor_count;
    uint32_t latest[SENSOR_COUNT];
    uint32_t history[SENSOR_COUNT][HISTORY_SIZE];
    uint32_t index[SENSOR_COUNT];
    uint32_t sequence;
};

static int majorNumber;
static struct class  *ultraClass;
static struct device *ultraDevice;
static void __iomem *shmem;
static struct pru_data __iomem *pru;
static struct timer_list poll_timer;
static uint32_t last_sequence = 0;
static uint32_t distance_mm = 0;
static uint32_t speed_of_sound = DEFAULT_SPEED;

/* Avoid libgcc div helpers in kernel modules. */
static uint32_t us_to_mm(uint32_t tof_us)
{
    uint32_t denom, q, r;

    if (!tof_us)
        return 0;
    denom = 2 * speed_of_sound;
    q = tof_us / denom;
    r = tof_us - q * denom;
    return q * 1000 + (r * 1000) / denom;
}

static char *put_u32(char *p, char *end, unsigned int v)
{
    char tmp[10];
    int n = 0, i;

    if (!v) {
        if (p < end)
            *p++ = '0';
        return p;
    }
    for (; v && n < 10; v /= 10)
        tmp[n++] = '0' + (v % 10);
    for (i = n - 1; i >= 0 && p < end; i--)
        *p++ = tmp[i];
    return p;
}

static int format_distance(char *buf, size_t size)
{
    char *p = buf, *end = buf + size;
    unsigned int whole, frac;
    const char *suffix;

    if (size < 8)
        return -1;

    if (distance_mm >= METRE_THRESHOLD_MM) {
        whole = distance_mm / 1000;
        frac  = (distance_mm / 10) - whole * 100;
        suffix = " m";
        if ((p = put_u32(p, end, whole)) + 4 >= end)
            return -1;
        *p++ = '.';
        *p++ = '0' + (frac / 10);
        *p++ = '0' + (frac % 10);
    } else {
        whole = distance_mm / 10;
        frac  = distance_mm - whole * 10;
        suffix = " cm";
        if ((p = put_u32(p, end, whole)) + 3 >= end)
            return -1;
        *p++ = '.';
        *p++ = '0' + frac;
    }

    while (*suffix && p < end)
        *p++ = *suffix++;
    return p - buf;
}

static void poll_callback(struct timer_list *t)
{
    uint32_t seq = readl(&pru->sequence);

    if (seq != last_sequence) {
        last_sequence = seq;
        distance_mm = us_to_mm(readl(&pru->latest[0]));
    }
    mod_timer(&poll_timer, jiffies + msecs_to_jiffies(POLL_MS));
}

static ssize_t dev_read(struct file *filep, char *buffer,
                        size_t len, loff_t *offset)
{
    char msg[32];
    int msg_len;

    if (*offset > 0)
        return 0;

    msg_len = format_distance(msg, sizeof(msg));
    if (msg_len < 0 || msg_len >= (int)sizeof(msg))
        return -EIO;

    if (copy_to_user(buffer, msg, msg_len) != 0) {
        printk(KERN_ERR "ultrasonic: copy_to_user failed\n");
        return -EFAULT;
    }

    *offset = msg_len;
    return msg_len;
}

static ssize_t dev_write(struct file *filep, const char *buffer,
                         size_t len, loff_t *offset)
{
    char msg[32] = {0};
    uint32_t new_speed;

    if (len >= sizeof(msg))
        return -EINVAL;
    if (copy_from_user(msg, buffer, len) != 0)
        return -EFAULT;
    if (kstrtouint(msg, 10, &new_speed) != 0 || new_speed == 0) {
        printk(KERN_ERR "ultrasonic: invalid calibration value\n");
        return -EINVAL;
    }

    speed_of_sound = new_speed;
    printk(KERN_INFO "ultrasonic: speed of sound set to %u us/m\n", new_speed);
    return len;
}

static struct file_operations fops = {
    .read  = dev_read,
    .write = dev_write,
};

static int __init ultrasonic_init(void)
{
    int ret;

    printk(KERN_INFO "ultrasonic: loading\n");

    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
    if (majorNumber < 0) {
        printk(KERN_ERR "ultrasonic: failed to register major number\n");
        return majorNumber;
    }

    ultraClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(ultraClass)) {
        ret = PTR_ERR(ultraClass);
        ultraClass = NULL;
        goto fail_chrdev;
    }

    ultraDevice = device_create(ultraClass, NULL,
                                MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
    if (IS_ERR(ultraDevice)) {
        ret = PTR_ERR(ultraDevice);
        ultraDevice = NULL;
        goto fail_class;
    }

    shmem = ioremap(PRU_SHARED_PHYS, PRU_SHARED_SIZE);
    if (!shmem) {
        printk(KERN_ERR "ultrasonic: ioremap failed\n");
        ret = -ENOMEM;
        goto fail_device;
    }
    pru = (struct pru_data __iomem *)shmem;

    timer_setup(&poll_timer, poll_callback, 0);
    mod_timer(&poll_timer, jiffies + msecs_to_jiffies(POLL_MS));
    printk(KERN_INFO "ultrasonic: loaded, /dev/%s ready (poll %d ms)\n",
           DEVICE_NAME, POLL_MS);
    return 0;

fail_device:
    device_destroy(ultraClass, MKDEV(majorNumber, 0));
fail_class:
    if (ultraClass)
        class_destroy(ultraClass);
fail_chrdev:
    unregister_chrdev(majorNumber, DEVICE_NAME);
    return ret;
}

static void __exit ultrasonic_exit(void)
{
    del_timer_sync(&poll_timer);
    iounmap(shmem);
    device_destroy(ultraClass, MKDEV(majorNumber, 0));
    class_unregister(ultraClass);
    class_destroy(ultraClass);
    unregister_chrdev(majorNumber, DEVICE_NAME);
    printk(KERN_INFO "ultrasonic: unloaded\n");
}

module_init(ultrasonic_init);
module_exit(ultrasonic_exit);
