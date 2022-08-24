#include "init.h"

int main() {
	device = read_file("/opt/device");
	device[strcspn(device, "\n")] = 0;

	// Filesystems
	mount("proc", "/proc", "proc", MS_NOSUID, "");
	mount("sysfs", "/sys", "sysfs", 0, "");
	sleep(1);
	mount("devtmpfs", "/dev", "devtmpfs", 0, "");
	mount("tmpfs", "/tmp", "tmpfs", 0, "size=16M");

	// Framebuffer (Kindle Touch-specific)
	if(strcmp(device, "kt") == 0) {
		// Unsquashing modules
		{
			const char * arguments[] = { "/bin/unsquashfs", "-d", "/lib/modules", "/opt/modules.sqsh", NULL }; run_command("/bin/unsquashfs", arguments, true);
		}

		// Loading framebuffer modules
		// eink_fb_waveform
		load_module("/lib/modules/2.6.35-inkbox/kernel/drivers/video/eink/waveform/eink_fb_waveform.ko", "");
		// Mounting P1 of MMC to retrieve FB waveform
		mount("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");
		// Check if waveform data exists
		// mxc_epdc_fb
		if(file_exists("/mnt/waveform/waveform.wbf") && file_exists("/mnt/waveform/waveform.wrf")) {
			// Use device waveform
			load_module("/lib/modules/2.6.35-inkbox/kernel/drivers/video/mxc/mxc_epdc_fb.ko", "default_panel_hw_init=1 default_update_mode=1 waveform_to_use=/mnt/waveform/waveform.wbf");
		}
		else {
			// Use built-in waveform
			load_module("/lib/modules/2.6.35-inkbox/kernel/drivers/video/mxc/mxc_epdc_fb.ko", "default_panel_hw_init=1 default_update_mode=1 waveform_to_use=built-in");
		}
		// Unmounting P1
		umount("/mnt");

		{
			// Refresh screen twice to initialize FB module properly
			for(int i = 0; i < 2; i++) {
				const char * arguments[] = { "/usr/bin/fbink", "-k", "-f", "-w", "-q", NULL }; run_command("/usr/bin/fbink", arguments, true);
			}
		}
	}

	// Setting hostname
	char hostname[6] = "inkbox";
	sethostname(hostname, sizeof(hostname));

	// Setting loopack interface UP
	set_if_up("lo");

	// TODO: Handle DFL mode

	// Getting kernel information
	// Kernel version
	struct utsname uname_data;
	uname(&uname_data);
	kernel_version = uname_data.sysname;
	strncat(kernel_version, space, sizeof(space));
	strncat(kernel_version, uname_data.nodename, sizeof(uname_data.nodename));
	strncat(kernel_version, space, sizeof(space));
	strncat(kernel_version, uname_data.version, sizeof(uname_data.version));
	// Kernel build ID
	kernel_build_id = read_file("/opt/build_id");
	kernel_build_id[strcspn(kernel_build_id, "\n")] = 0;
	// Kernel Git commit
	kernel_git_commit = read_file("/opt/commit");
	kernel_git_commit[strcspn(kernel_git_commit, "\n")] = 0;
	// Setting up boot flags partition (P1)
	mount("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");
	mkdir("/mnt/flags", 0755);
	umount("/mnt");

	// Handling DISPLAY_DEBUG flag (https://inkbox.ddns.net/wiki/index.php?title=Boot_flags)
	display_debug = read_file("/mnt/flags/DISPLAY_DEBUG");
	if(strcmp(display_debug, "true\n") == 0 || strcmp(display_debug, "true") == 0) {
		mkfifo(serial_fifo_path, 0x29A);
		const char * arguments[] = { "/etc/init.d/inkbox-splash", "display_debug", NULL }; run_command("/etc/init.d/inkbox-splash", arguments, false);
		sleep(5);
		// Redirecting all stdout output to display debug's named pipe
		freopen(serial_fifo_path, "a+", stdout);
	}

	// Information header
	printf("\n%s GNU/Linux\nInkBox OS, kernel build %s, commit %s\n\n", kernel_version, kernel_build_id, kernel_git_commit);
	
	// Checking filesystems
	info("Checking filesystems ...", 0);
	printf("\n");
	{
		// P1
		const char * arguments[] = { "/usr/bin/fsck.ext4", "-y", "/dev/mmcblk0p1", NULL };
		run_command("/usr/bin/fsck.ext4", arguments, true);
	}
	{
		// P2
		const char * arguments[] = { };
		arguments[0] = "/usr/bin/fsck.ext4", arguments[1] = "-y";
		if(strcmp(device, "n873") == 0) {
			arguments[2] = "/dev/mmcblk0p5";
		}
		else {
			arguments[2] = "/dev/mmcblk0p2";
		}
		arguments[3] = NULL;
		run_command("/usr/bin/fsck.ext4", arguments, true);
	}
	{
		// P3
		const char * arguments[] = { "/usr/bin/fsck.ext4", "-y", "/dev/mmcblk0p3", NULL };
		run_command("/usr/bin/fsck.ext4", arguments, true);
	}
	{
		// P4
		const char * arguments[] = { "/usr/bin/fsck.ext4", "-y", "/dev/mmcblk0p4", NULL };
		run_command("/usr/bin/fsck.ext4", arguments, true);
	}
	printf("\n");

	// Universal ID check
	{
		const char * arguments[] = { "/opt/bin/uidgen", NULL }; run_command("/opt/bin/uidgen", arguments, true);
	}

	// DONT_BOOT
	dont_boot = read_file("/mnt/flags/DONT_BOOT");
	if(strcmp(dont_boot, "true\n") == 0 || strcmp(dont_boot, "true") == 0) {
		info("Device is locked down and will not boot.", 2);
	}

	// ENCRYPT_LOCK
	encrypt_lock = read_file("/mnt/flags/ENCRYPT_LOCK");
	if(encrypt_lock[0] != '\0') {
		unsigned long current_epoch = time(NULL);
		unsigned long lock_epoch = strtoul(encrypt_lock, NULL, 0);
		// Comparing lockdown time limit to current time
		if(current_epoch < lock_epoch) {
			info("Too many incorrect encrypted storage unlocking attempts have locked down this device. Shutting down ...", 2);
			const char * arguments[] = { "/etc/init.d/inkbox-splash", "alert_splash", "7", NULL }; run_command("/etc/init.d/inkbox-splash", arguments, true);
			sync();
			exit(EXIT_FAILURE);
			// rcS will power off the device after this
		}
		else {
			// Avoid running this block of code again
			remove("/mnt/flags/ENCRYPT_LOCK");
		}
	}

	// DFL
	dfl = read_file("/mnt/flags/DFL");
	if(strcmp(dfl, "true\n") == 0 || strcmp(dfl, "true") == 0) {
		info("Entering Direct Firmware Loader mode (DFL) ...", 0);
		// Re-setting flag
		write_file("/mnt/flags/DFL", "false\n");
		launch_dfl();
	}

	// BOOT_USB_DEBUG
	boot_usb_debug = read_file("/mnt/flags/BOOT_USB_DEBUG");
	if(strcmp(boot_usb_debug, "true\n") == 0 || strcmp(boot_usb_debug, "true") == 0) {
		setup_usb_debug(true);
	}
}

// https://github.com/Kobo-InkBox/inkbox-power-daemon/blob/8296c4a1811e3921ff98e9980504c24d23435dac/src/functions.cpp#L415-L430
bool run_command(const char * path, const char * arguments[], bool wait) {
	int status = -1;
	int pid = 0;

	status = posix_spawn(&pid, path, NULL, NULL, (char**)arguments, NULL);
	if(status != 0) {
		fprintf(stderr, "Error in spawning process %s\n", path);
		return false;
	}
	
	if(wait == true) {
		waitpid(pid, 0, 0);
	}

	return true;
}

// https://stackoverflow.com/a/230070/14164574
bool file_exists(char * file_path) {
	struct stat buffer;
	return (stat(file_path, &buffer) == 0);
}

// https://stackoverflow.com/a/3747128/14164574
char * read_file(char * file_path) {
	// Ensure that specified file exists, then try to read it
	if(access(file_path, F_OK) == 0) {
		FILE * fp;
		long lSize;
		char * buffer;

		fp = fopen(file_path , "rb");

		fseek(fp, 0L , SEEK_END);
		lSize = ftell(fp);
		rewind(fp);

		/* Allocate memory for entire content */
		buffer = calloc(1, lSize+1);
		if(!buffer) fclose(fp);

		/* Copy the file into the buffer */
		if(1 != fread(buffer, lSize, 1, fp)) {
			fclose(fp);
		}

		return buffer;

		fclose(fp);
		free(buffer);
	}
	else {
		return "";
	}
}

// https://stackoverflow.com/a/14576624/14164574
bool write_file(char * file_path, char * content) {
	FILE *file = fopen(file_path, "w");

	int rc = fputs(content, file);
	if (rc == EOF) {
		return false;
	}
	else {
		fclose(file);
	}
}

// https://github.com/Kobo-InkBox/inkbox-power-daemon/blob/8296c4a1811e3921ff98e9980504c24d23435dac/src/wifi.cpp#L181-L197
int load_module(char * module_path, char * params) {
	size_t image_size;
	struct stat st;
	void * image;

	int fd = open(module_path, O_RDONLY);
	fstat(fd, &st);
	image_size = st.st_size;
	image = malloc(image_size);
	read(fd, image, image_size);
	close(fd);

	if(initModule(image, image_size, params) != 0) {
		fprintf(stderr, "Couldn't init module %s\n", module_path);
	}

	free(image);
}

// https://stackoverflow.com/a/49334887/14164574
int set_if_flags(char * ifname, short flags) {
	struct ifreq ifr;
	int res = 0;

	ifr.ifr_flags = flags;
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

	if((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("socket error %s\n", strerror(errno));
		res = 1;
		goto out;
	}

	res = ioctl(skfd, SIOCSIFFLAGS, &ifr);
	if (res < 0) {
		printf("Interface '%s': Error: SIOCSIFFLAGS failed: %s\n", ifname, strerror(errno));
	}
	else {
		printf("Interface '%s': flags set to %04X.\n", ifname, flags);
	}

	out:
		return res;
}

int set_if_up(char * ifname) {
    return set_if_flags(ifname, IFF_UP);
}

int info(char * message, int mode) {
	/*
	 * Modes:
	 * - 0: Normal logging (green)
	 * - 1: Warning logging (yellow)
	 * - 2: Error logging (red)
	*/
	if(mode == 0) {
		printf("\e[1m\e[32m * \e[0m%s\n", message);
	}
	else if(mode == 1) {
		printf("\e[1m\e[33m * \e[0m%s\n", message);
	}
	else if(mode == 2) {
		printf("\e[1m\e[31m * \e[0m%s\n", message);
	}
	else {
		return 1;
	}
	return 0;
}

void launch_dfl() {
	// Loading USB Mass Storage (USBMS) modules
	mkdir("/modules", 0755);
	mount("/opt/modules.sqsh", "/modules", "squashfs", 0, "");
	if(strcmp(device, "n705") == 0 || strcmp(device, "n905b") == 0 || strcmp(device, "n905c") == 0 || strcmp(device, "n613") == 0) {
		load_module("/modules/arcotg_udc.ko", "");
	}
	if(strcmp(device, "n306") == 0 || strcmp(device, "n873") == 0) {
		load_module("/modules/fs/configfs/configfs.ko", "");
		load_module("/modules/drivers/usb/gadget/libcomposite.ko", "");
		load_module("/modules/drivers/usb/gadget/function/usb_f_mass_storage.ko", "");
	}
	if(strcmp(device, "emu") != 0) {
		load_module("/modules/g_mass_storage.ko", "");
	}

	// Splash time
	const char * arguments[] = { "/etc/init.d/inkbox-splash", "dfl", NULL }; run_command("/etc/init.d/inkbox-splash", arguments, true);
	while(true) {
		info("This device is in DFL mode. Please reset it to resume normal operation.", 0);
		sleep(30);
	}
}

void setup_usb_debug(bool boot) {
	mkdir("/dev/pts", 0755);
	mount("devpts", "/dev/pts", "devpts", 0, "");
	{
		// Telnet server
		const char * arguments[] = { "/bin/busybox", "telnetd", NULL }; run_command("/bin/busybox", arguments, false);
	}
	{
		// FTP server
		const char * arguments[] = { "/bin/busybox", "tcpsvd", "-vE", "0.0.0.0", "21", "ftpd", "-A", "-w", "/", NULL }; run_command("/bin/busybox", arguments, false);
	}

	if(boot == true) {
		setup_usbnet();
		const char * arguments[] = { "/etc/init.d/inkbox-splash", "usb_debug", NULL }; run_command("/etc/init.d/inkbox-splash", arguments, true);
		while(true) {
			info("This device is in boot-time USB debug mode. Please reset or reboot it to resume normal operation.", 0);
			sleep(30);
		}
	}
}

void setup_usbnet() {
}
