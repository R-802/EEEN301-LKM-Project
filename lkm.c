/* LKM: reads PRU shared RAM, exposes /dev/ultrasonic */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/uaccess.h>

#define DEV_NAME "ultrasonic"
#define CLASS_NAME "ultra"
#define PRU_PHYS 0x4A310000 // Linux physical address of PRU shared RAM
#define PRU_MAP_SIZE 0x3000
#define SENSOR_COUNT 1
#define HISTORY_SIZE 8
#define DEFAULT_SPEED 2915 /* us/m at room temperature */
#define POLL_MS 100

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EEEN301");
MODULE_DESCRIPTION("HC-SR04 distance sensor via PRU");
MODULE_VERSION("0.7");

// Must match data_t in pru.c
typedef struct {
  uint32_t sensor_count;
  uint32_t latest[SENSOR_COUNT];
  uint32_t history[SENSOR_COUNT][HISTORY_SIZE];
  uint32_t index[SENSOR_COUNT];
  uint32_t sequence;
} data_t;

static int major;
static struct class *cls;
static struct device *dev;
static void __iomem *pru_mem;
static data_t __iomem *pru;
static struct timer_list poll_timer;

static uint32_t last_seq;
static uint32_t distance_cm;
static uint32_t speed_us_per_m = DEFAULT_SPEED;

// Convert round trip time (us) to distance in cm
static uint32_t tof_to_cm(uint32_t tof_us) {
  if (!tof_us)
    return 0;

  return (tof_us * 100) / (2 * speed_us_per_m);
}

// Timer callback: check PRU shared RAM for new samples
static void poll_pru(struct timer_list *unused) {
  uint32_t seq = readl(&pru->sequence);

  if (seq != last_seq) {
    last_seq = seq;
    distance_cm = tof_to_cm(readl(&pru->latest[0]));
  }

  mod_timer(&poll_timer, jiffies + msecs_to_jiffies(POLL_MS));
}

// read: return formatted distance string once per open
static ssize_t lkm_read(struct file *filp, char __user *buf, size_t len,
                        loff_t *pos) {
  char out[24];
  int n;

  if (*pos > 0)
    return 0;

  n = snprintf(out, sizeof(out), "%u cm", distance_cm);
  if (n <= 0 || n >= (int)sizeof(out))
    return -EIO;

  if (copy_to_user(buf, out, n))
    return -EFAULT;

  *pos = n;
  return n;
}

// write: set speed-of-sound calibration (us/m)
static ssize_t lkm_write(struct file *filp, const char __user *buf, size_t len,
                         loff_t *pos) {
  char in[16] = {0};
  uint32_t speed;

  if (len >= sizeof(in))
    return -EINVAL;
  if (copy_from_user(in, buf, len))
    return -EFAULT;
  if (kstrtouint(in, 10, &speed) || !speed)
    return -EINVAL;

  speed_us_per_m = speed;
  return len;
}

static const struct file_operations fops = {
    .read = lkm_read,
    .write = lkm_write,
};

static int __init lkm_init(void) {
  int err;

  // Create /dev/ultrasonic
  major = register_chrdev(0, DEV_NAME, &fops);
  if (major < 0)
    return major;

  cls = class_create(THIS_MODULE, CLASS_NAME);
  if (IS_ERR(cls)) {
    err = PTR_ERR(cls);
    cls = NULL;
    goto out_chrdev;
  }

  dev = device_create(cls, NULL, MKDEV(major, 0), NULL, DEV_NAME);
  if (IS_ERR(dev)) {
    err = PTR_ERR(dev);
    dev = NULL;
    goto out_class;
  }

  // Map PRU shared memory into kernel space
  pru_mem = ioremap(PRU_PHYS, PRU_MAP_SIZE);
  if (!pru_mem) {
    err = -ENOMEM;
    goto out_device;
  }
  pru = pru_mem;

  // Start polling PRU data
  timer_setup(&poll_timer, poll_pru, 0);
  mod_timer(&poll_timer, jiffies + msecs_to_jiffies(POLL_MS));
  return 0;

out_device:
  device_destroy(cls, MKDEV(major, 0));
out_class:
  class_destroy(cls);
out_chrdev:
  unregister_chrdev(major, DEV_NAME);
  return err;
}

static void __exit lkm_exit(void) {
  del_timer_sync(&poll_timer);
  iounmap(pru_mem);
  device_destroy(cls, MKDEV(major, 0));
  class_destroy(cls);
  unregister_chrdev(major, DEV_NAME);
}

module_init(lkm_init);
module_exit(lkm_exit);
