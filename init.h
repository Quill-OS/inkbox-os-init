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

// Defines
#define initModule(module_image, len, param_values) syscall(__NR_init_module, module_image, len, param_values)

// Variables
char * device;
static int skfd = -1; // AF_INET socket for ioctl() calls
char * kernel_version;
char * kernel_build_id;
char * kernel_git_commit;

// Functions
void run_command(const char * path, const char * args[], bool wait);
bool file_exists(char * file_path);
char * read_file(char * file_path);
int load_module(char * module_path, char * params);
int set_if_flags(char * ifname, short flags);
int set_if_up(char * ifname);

#endif // INIT_H
