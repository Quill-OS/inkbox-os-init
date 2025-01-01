#ifndef INIT_H
#define INIT_H

/*
    inkbox-os-init: Initialization program of InkBox OS
    Copyright (C) 2022-2025 Nicolas Mailloux <nicolecrivain@gmail.com>
    SPDX-License-Identifier: GPL-3.0-only

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// GNUisms welcome!
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <alloca.h>
#include <signal.h>
#include <poll.h>
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
#include <sys/socket.h>
#include <linux/limits.h>
#include <linux/input.h>
#include <linux/netlink.h>
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
#define ROOT_FLAG_SIZE 6U
#define BOOT_STANDARD 0
#define BOOT_DIAGNOSTICS 1
#define SERIAL_FIFO_PATH "/tmp/serial-fifo"
#define PROGRESS_BAR_FIFO_PATH "/run/progress_bar_fifo"

// Variables
char * device = NULL;
char * device_variant = NULL;
char * button_input_device = NULL;
char tty[8] = { 0 };
char * usbnet_ip;
char * usbnet_device_address;
char * usbnet_host_address;
bool root = false;

// Macros
#define MATCH(s1, s2) ((s1 != NULL && s2 != NULL) && strcmp(s1, s2) == 0)
#define NOT_MATCH(s1, s2) ((!s1 || !s2) || strcmp(s1, s2) != 0)
#define FILE_EXISTS(path) access(path, F_OK) == 0
#define MOUNT(s, t, f, m, d) \
({ \
        if(mount(s, t, f, m, d) != 0) { \
                perror("Failed to mount " t); \
                exit(EXIT_FAILURE); \
        } \
})
// Have to waipid the returned value at a later time to collect zombies
#define RUN(prog, ...) \
({ \
        const char * const args[] = { prog, ##__VA_ARGS__, NULL }; \
	run_command(prog, args, false); \
})
// Will wait until termination to collect the process
#define REAP(prog, ...) \
({ \
        const char * const args[] = { prog, ##__VA_ARGS__, NULL }; \
	run_command(prog, args, true); \
})

// Functions
long int run_command(const char * path, const char * const arguments[], bool wait);
char * read_file(const char * file_path, bool strip_newline);
bool write_file(const char * file_path, const char * content);
bool copy_file(const char * source_file, const char * destination_file);
int mkpath(const char * path, mode_t mode);
int load_module(const char * module_path, const char * params);
int set_if_flags(const char * if_name, short flags);
int set_if_up(const char * if_name);
int set_if_ip_address(const char * if_name, const char * ip_address);
int info(const char * message, int mode);
void launch_dfl(void);
void setup_usb_debug(bool boot);
void setup_usbnet(void);
void setup_shell(void);
void read_sector(char * buff, size_t len, const char * device_node, off_t sector, size_t sector_size);
void show_alert_splash(int error_code, bool flag);
void set_progress(int progress_value);
void progress_sleep(void);
int get_pid_by_name(const char * name);
void kill_process(const char * name);
void mount_essential_filesystems(void);
void mount_squashfs_archives(void);

#endif // INIT_H
