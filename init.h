#ifndef INIT_H
#define INIT_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <linux/module.h>
#include <net/if.h>
#include <time.h>

// Defines
#define initModule(module_image, len, param_values) syscall(__NR_init_module, module_image, len, param_values)

// Variables
char * device;
char * space = " ";
static int skfd = -1; // AF_INET socket for ioctl() calls
char * serial_fifo_path = "/tmp/serial-fifo";
char * kernel_version;
char * kernel_build_id;
char * kernel_git_commit;
char * display_debug;
char * dont_boot;
char * encrypt_lock;
char * dfl;
char * boot_usb_debug;

// Functions
bool run_command(const char * path, const char * arguments[], bool wait);
bool file_exists(char * file_path);
char * read_file(char * file_path);
bool write_file(char * file_path, char * content);
int load_module(char * module_path, char * params);
int set_if_flags(char * ifname, short flags);
int set_if_up(char * ifname);
int info(char * message, int mode);
void launch_dfl();
void setup_usb_debug(bool boot);
void setup_usbnet();

#endif // INIT_H
