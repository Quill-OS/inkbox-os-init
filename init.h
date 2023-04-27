#ifndef INIT_H
#define INIT_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <linux/module.h>
#include <linux/input.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <libgen.h>
#include <dirent.h>

// Defines
#define initModule(module_image, len, param_values) syscall(__NR_init_module, module_image, len, param_values)
#define INFO_OK 0
#define INFO_WARNING 1
#define INFO_FATAL 2
#define ROOT_FLAG_SECTOR 79872
#define ROOT_FLAG_SECTOR_KT 98304
#define ROOT_FLAG_SIZE 6
#define BUTTON_INPUT_DEVICE "/dev/input/event0"
#define BOOT_STANDARD 0
#define BOOT_DIAGNOSTICS 1
#define SERIAL_FIFO_PATH "/tmp/serial-fifo"
#define PROGRESS_BAR_FIFO_PATH "/run/progress_bar_fifo"

// Variables
char * device;
bool root_mmc = false;
bool root_initrd = false;
bool root = false;
bool power_button_pressed = false;
bool other_button_pressed = false;
int boot_mode = BOOT_STANDARD;
static int skfd = -1; // AF_INET socket for ioctl() calls
char * kernel_version = NULL;
char * kernel_build_id;
char * kernel_git_commit;
char * display_debug;
char * dont_boot;
char * encrypt_lock;
char * dfl;
char * boot_usb_debug;
char * diags_boot;
char * usbnet_ip;
char * initrd_debug;
char sector_content[ROOT_FLAG_SIZE];
char * will_update;
int update_splash_pid;
char * mount_rw;
char * login_shell;
char * developer_key;
char * x11_start;
char * tty;

// Functions
int run_command(const char * path, const char * arguments[], bool wait);
bool file_exists(char * file_path);
char * read_file(char * file_path, bool strip_newline);
bool write_file(char * file_path, char * content);
bool copy_file(char * source_file, char * destination_file);
bool mkpath(char * path, mode_t mode);
int load_module(char * module_path, char * params);
int set_if_flags(char * if_name, short flags);
int set_if_up(char * if_name);
int set_if_ip_address(char * if_name, char * ip_address);
int info(char * message, int mode);
void launch_dfl();
void setup_usb_debug(bool boot);
void setup_usbnet();
void setup_shell();
void read_sector(char * device_node, unsigned long sector, int sector_size, unsigned long bytes_to_read);
void show_alert_splash(int error_code, bool flag);
void set_progress(int progress_value);
void progress_sleep();
int get_pid_by_name(char * name);
void kill_process(char * name, int signal);
void mount_essential_filesystems();
void mount_squashfs_archives();

#endif // INIT_H
