#include "init.h"

int main() {
	device = read_file("/opt/device");
	// Remove newline
	strtok(device, "\n");

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
	strtok(kernel_build_id, "\n");
	// Kernel Git commit
	kernel_git_commit = read_file("/opt/commit");
	strtok(kernel_git_commit, "\n");
	// Setting up boot flags partition (P1)
	mount("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");
	mkdir("/mnt/flags", 0755);

	// Handling DISPLAY_DEBUG flag (https://inkbox.ddns.net/wiki/index.php?title=Boot_flags)
	display_debug = read_file("/mnt/flags/DISPLAY_DEBUG");
	if(strcmp(display_debug, "true\n") == 0 || strcmp(display_debug, "true") == 0) {
		mkfifo(serial_fifo_path, 0x29A);
		const char * arguments[] = { "/etc/init.d/inkbox-splash", "display_debug", NULL }; run_command("/etc/init.d/inkbox-splash", arguments, false);
		sleep(5);
		// Redirecting all stdout output to display debug's named pipe
		freopen(serial_fifo_path, "a+", stdout);
	}

	// USBNET_IP
	usbnet_ip = read_file("/mnt/flags/USBNET_IP");
	strtok(usbnet_ip, "\n");

	// Information header
	printf("\n%s GNU/Linux\nInkBox OS, kernel build %s, commit %s\n\n", kernel_version, kernel_build_id, kernel_git_commit);
	
	// Checking filesystems
	info("Checking filesystems ...", INFO_OK);
	// Unmounting P1 for inspection by fsck.ext4
	umount("/mnt");
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
	// Remounting P1
	mount("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");

	// Universal ID check
	{
		const char * arguments[] = { "/opt/bin/uidgen", NULL }; run_command("/opt/bin/uidgen", arguments, true);
	}

	// DONT_BOOT
	dont_boot = read_file("/mnt/flags/DONT_BOOT");
	if(strcmp(dont_boot, "true\n") == 0 || strcmp(dont_boot, "true") == 0) {
		info("Device is locked down and will not boot", INFO_FATAL);
		show_alert_splash(1);
		exit(EXIT_FAILURE);
	}

	// ENCRYPT_LOCK
	encrypt_lock = read_file("/mnt/flags/ENCRYPT_LOCK");
	if(encrypt_lock[0] != '\0') {
		unsigned long current_epoch = time(NULL);
		unsigned long lock_epoch = strtoul(encrypt_lock, NULL, 0);
		// Comparing lockdown time limit to current time
		if(current_epoch < lock_epoch) {
			info("Too many incorrect encrypted storage unlocking attempts have locked down this device. Shutting down ...", INFO_FATAL);
			// Splash time
			show_alert_splash(7);
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
		info("Entering Direct Firmware Loader mode (DFL) ...", INFO_OK);
		// Re-setting flag
		write_file("/mnt/flags/DFL", "false\n");
		launch_dfl();
	}

	// BOOT_USB_DEBUG
	boot_usb_debug = read_file("/mnt/flags/BOOT_USB_DEBUG");
	if(strcmp(boot_usb_debug, "true\n") == 0 || strcmp(boot_usb_debug, "true") == 0) {
		setup_usb_debug(true);
	}

	// INITRD_DEBUG
	initrd_debug = read_file("/mnt/flags/INITRD_DEBUG");
	if(strcmp(initrd_debug, "true\n") == 0 || strcmp(initrd_debug, "true") == 0) {
		setup_usb_debug(false);
	}

	// Unmounting boot flags partition
	umount("/mnt");

	// Are we spawning a shell?
	// https://stackoverflow.com/a/19186027/14164574
	{
		char * value;
		struct timeval tmo;
		fd_set readfds;

		printf("(initrd) Hit ENTER to stop auto-boot ... ");
		fflush(stdout);

		// Wait only 3 seconds for user input
		FD_ZERO(&readfds);
		FD_SET(0, &readfds);
		tmo.tv_sec = 3;
		tmo.tv_usec = 0;

		switch(select(1, &readfds, NULL, NULL, &tmo)) {
			case -1:
				break;
			case 0:
				printf("\n\n");
				goto boot;
		}

		// This is only executed if the Enter key has been pressed
		setup_shell();
	}

	boot:;
	// Checking if the 'root' flag is set
	{
		// MMC
		char root_flag[6];
		if(strcmp(device, "kt") == 0) {
			read_sector("/dev/mmcblk0", ROOT_FLAG_SECTOR_KT, 512, 6);
		}
		else {
			read_sector("/dev/mmcblk0", ROOT_FLAG_SECTOR, 512, 6);
		}
		sprintf(root_flag, "%s", &sector_content);

		if(strcmp(root_flag, "rooted") == 0) {
			root_mmc = true;
		}
		else {
			root_mmc = false;
		}
	}
	{
		// Init ramdisk
		char * root_flag = read_file("/opt/root");
		if(strcmp(root_flag, "rooted\n") == 0 || strcmp(root_flag, "rooted") == 0) {
			root_initrd = true;
		}
		else {
			root_initrd = false;
		}
	}

	if(root_mmc == true && root_initrd == true) {
		root = true;
	}
	else {
		root = false;
		if(root_mmc == true || root_initrd == true) {
			write_file("/mnt/flags/DONT_BOOT", "true\n");
			info("Security policy was violated! Shutting down ...", INFO_FATAL);
			show_alert_splash(1);
			exit(EXIT_FAILURE);
		}
	}

	if(root == true) {
		info("Device is rooted; not enforcing security policy", INFO_WARNING);
	}
	else {
		info("Device is not rooted; enforcing security policy", INFO_OK);
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

		// Allocate memory for entire content
		buffer = calloc(1, lSize+1);
		if(!buffer) fclose(fp);

		// Copy the file into the buffer
		if(1 != fread(buffer, lSize, 1, fp)) {
			fclose(fp);
		}

		return(buffer);

		fclose(fp);
		free(buffer);
	}
	else {
		return("");
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
int set_if_flags(char * if_name, short flags) {
	struct ifreq ifr;
	int res = 0;

	ifr.ifr_flags = flags;
	strncpy(ifr.ifr_name, if_name, IFNAMSIZ);

	if((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("socket error %s\n", strerror(errno));
		res = 1;
		goto out;
	}

	res = ioctl(skfd, SIOCSIFFLAGS, &ifr);
	if (res < 0) {
		printf("Interface '%s': Error: SIOCSIFFLAGS failed: %s\n", if_name, strerror(errno));
	}
	else {
		printf("Interface '%s': flags set to %04X.\n", if_name, flags);
	}

	out:
	return res;
}

int set_if_up(char * if_name) {
    return set_if_flags(if_name, IFF_UP);
}

int set_if_ip_address(char * if_name, char * ip_address) {
	int fd;
	struct ifreq ifr;
	struct sockaddr_in * addr;

	// AF_INET - to define network interface IPv4
	// Creating soket for it
	fd = socket(AF_INET, SOCK_DGRAM, 0);

	// AF_INET - to define IPv4 Address type
	ifr.ifr_addr.sa_family = AF_INET;

	// eth0 - define the ifr_name - port name
	memcpy(ifr.ifr_name, if_name, IFNAMSIZ - 1);

	// Defining sockaddr_in
	addr = (struct sockaddr_in*)&ifr.ifr_addr;

	// Convert IP address in correct format to write
	inet_pton(AF_INET, ip_address, &addr->sin_addr);

	// Setting IP Address using ioctl
	int res = ioctl(fd, SIOCSIFADDR, &ifr);
	// Closing file descriptor
	close(fd);

	// Clear ip_address buffer with 0x20- space
	memset((unsigned char*)ip_address, 0x20, 15);
	ioctl(fd, SIOCGIFADDR, &ifr);

	return res;
}

int info(char * message, int mode) {
	/*
	 * Modes:
	 * - 0: Normal logging (green) - INFO_OK
	 * - 1: Warning logging (yellow) - INFO_WARNING
	 * - 2: Error logging (red) - INFO_FATAL
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
		load_module("/modules/g_mass_storage.ko", "file=/dev/mmcblk0 removable=y stall=0");
	}

	// Splash time
	const char * arguments[] = { "/etc/init.d/inkbox-splash", "dfl", NULL }; run_command("/etc/init.d/inkbox-splash", arguments, true);
	while(true) {
		info("This device is in DFL mode. Please reset it to resume normal operation.", INFO_OK);
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
		// Set up USB networking interface
		setup_usbnet();
		// Splash time
		const char * arguments[] = { "/etc/init.d/inkbox-splash", "usb_debug", NULL }; run_command("/etc/init.d/inkbox-splash", arguments, true);
		while(true) {
			info("This device is in boot-time USB debug mode. Please reset or reboot it to resume normal operation.", INFO_OK);
			sleep(30);
		}
	}
}

void setup_usbnet() {
	// Load modules
	mkdir("/modules", 0755);
	mount("/opt/modules.sqsh", "/modules", "squashfs", 0, "");

	if(strcmp(device, "n705") == 0 || strcmp(device, "n905b") == 0 || strcmp(device, "n905c") == 0 || strcmp(device, "n613") == 0) {
		load_module("/modules/arcotg_udc.ko", "");
	}
	if(strcmp(device, "n705") == 0 || strcmp(device, "n905b") == 0 || strcmp(device, "n905c") == 0 || strcmp(device, "n613") == 0 || strcmp(device, "n236") == 0 || strcmp(device, "n437") == 0) {
		load_module("/module/g_ether.ko", "");
	}
	else if(strcmp(device, "n306") == 0 || strcmp(device, "n873") == 0 || strcmp(device, "bpi") == 0) {
		load_module("/modules/fs/configfs/configfs.ko", "");
		load_module("/modules/drivers/usb/gadget/libcomposite.ko", "");
		load_module("/modules/drivers/usb/gadget/function/u_ether.ko", "");
		load_module("/modules/drivers/usb/gadget/function/usb_f_ecm.ko", "");
		if(file_exists("/modules/drivers/usb/gadget/function/usb_f_eem.ko")) {
			load_module("/modules/drivers/usb/gadget/function/usb_f_eem.ko", "");
		}
		load_module("/modules/drivers/usb/gadget/function/usb_f_ecm_subset.ko", "");
		load_module("/modules/drivers/usb/gadget/function/usb_f_rndis.ko", "");
		load_module("/modules/drivers/usb/gadget/legacy/g_ether.ko", "");
	}
	else if(strcmp(device, "kt") == 0) {
		load_module("/modules/2.6.35-inkbox/kernel/drivers/usb/gadget/arcotg_udc.ko", "");
		load_module("/modules/2.6.35-inkbox/kernel/drivers/usb/gadget/g_ether.ko", "");
	}
	else if(strcmp(device, "emu") == 0) {
		;
	}
	else {
		load_module("/modules/g_ether.ko", "");
	}

	// Setting up network interface
	set_if_up("usb0");
	// Checking for custom IP address
	if(strcmp(usbnet_ip, "") != 0) {
		if(set_if_ip_address("usb0", usbnet_ip) != 0) {
			set_if_ip_address("usb0", "192.168.2.2");
		}
	}
	else {
		set_if_ip_address("usb0", "192.168.2.2");
	}
}

void setup_shell() {
	// /etc/inittab hackery
	remove("/usr/sbin/chroot");
	if(strcmp(device, "emu") == 0) {
		write_file("/usr/sbin/chroot", "#!/bin/sh\n\n/sbin/getty -L ttyAMA0 115200 linux");
	}
	else if(strcmp(device, "bpi") == 0) {
		write_file("/usr/sbin/chroot", "#!/bin/sh\n\n/sbin/getty -L ttyS0 115200 linux");
	}
	else {
		write_file("/usr/sbin/chroot", "#!/bin/sh\n\n/sbin/getty -L ttymxc0 115200 linux");
	}
	// Setting executable bit
	chmod("/usr/sbin/chroot", 0777);
	exit(EXIT_SUCCESS);
}

void read_sector(char * device_node, unsigned long sector, int sector_size, unsigned long bytes_to_read) {
	int fd = open(device_node, O_RDONLY);
	sector = sector * sector_size;
	lseek(fd, sector, SEEK_SET);
	read(fd, &sector_content, bytes_to_read);
	close(fd);
}

void show_alert_splash(int error_code) {
	// Converting error code to char
	char code[sizeof(error_code)];
	sprintf(code, "%d", error_code);

	const char * arguments[] = { "/etc/init.d/inkbox-splash", "alert_splash", code, NULL };
	run_command("/etc/init.d/inkbox-splash", arguments, true);

}
