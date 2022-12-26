#include "init.h"

int main() {
	// Device identification
	device = read_file("/opt/device", true);

	// TTY device
	if(strstr(device, "emu")) {
		tty = "ttyAMA0";
	}
	else if(strstr(device, "bpi")) {
		tty = "ttyS0";
	}
	else {
		tty = "ttymxc0";
	}

	// Filesystems
	mount("proc", "/proc", "proc", MS_NOSUID, "");
	mount("sysfs", "/sys", "sysfs", 0, "");
	sleep(1);
	mount("devtmpfs", "/dev", "devtmpfs", 0, "");
	mount("tmpfs", "/tmp", "tmpfs", 0, "size=16M");

	// Framebuffer (Kindle Touch-specific)
	if(strstr(device, "kt")) {
		// Unsquashing modules
		{
			const char * arguments[] = { "/usr/bin/unsquashfs", "-d", "/lib/modules", "/opt/modules.sqsh", NULL }; run_command("/usr/bin/unsquashfs", arguments, true);
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
	kernel_build_id = read_file("/opt/build_id", true);
	// Kernel Git commit
	kernel_git_commit = read_file("/opt/commit", true);
	// Setting up boot flags partition (P1)
	mount("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");
	mkpath("/mnt/flags", 0755);

	// Handling DISPLAY_DEBUG flag (https://inkbox.ddns.net/wiki/index.php?title=Boot_flags)
	display_debug = read_file("/mnt/flags/DISPLAY_DEBUG", true);
	if(strstr(display_debug, "true")) {
		mkfifo(SERIAL_FIFO_PATH, 0x29A);
		const char * arguments[] = { "/etc/init.d/inkbox-splash", "display_debug", NULL }; run_command("/etc/init.d/inkbox-splash", arguments, false);
		sleep(5);
		// Redirecting all stdout output to display debug's named pipe
		freopen(SERIAL_FIFO_PATH, "a+", stdout);
	}

	// USBNET_IP
	usbnet_ip = read_file("/mnt/flags/USBNET_IP", true);
	
	// MOUNT_RW
	mount_rw = read_file("/mnt/flags/MOUNT_RW", true);

	// LOGIN_SHELL
	login_shell = read_file("/mnt/flags/LOGIN_SHELL", true);

	// X11_START
	x11_start = read_file("/mnt/flags/X11_START", true);

	// Information header
	printf("\n%s GNU/Linux\nInkBox OS, kernel build %s, commit %s\n\n", kernel_version, kernel_build_id, kernel_git_commit);
	printf("Copyright (C) 2021-2022 Nicolas Mailloux <nicolecrivain@gmail.com>\n");

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
		if(strstr(device, "n873")) {
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
	dont_boot = read_file("/mnt/flags/DONT_BOOT", true);
	if(strstr(dont_boot, "true")) {
		info("Device is locked down and will not boot", INFO_FATAL);
		show_alert_splash(1, false);
		exit(EXIT_FAILURE);
	}

	// ENCRYPT_LOCK
	encrypt_lock = read_file("/mnt/flags/ENCRYPT_LOCK", true);
	if(encrypt_lock[0] != '\0') {
		unsigned long current_epoch = time(NULL);
		unsigned long lock_epoch = strtoul(encrypt_lock, NULL, 0);
		// Comparing lockdown time limit to current time
		if(current_epoch < lock_epoch) {
			info("Too many incorrect encrypted storage unlocking attempts have locked down this device. Shutting down ...", INFO_FATAL);
			// Splash time
			show_alert_splash(7, false);
			sync();
			exit(EXIT_FAILURE);
			// rcS will power off the device after this
		}
		else {
			// Avoid running this block of code again
			remove("/mnt/flags/ENCRYPT_LOCK");
		}
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
		if(strstr(device, "kt")) {
			read_sector("/dev/mmcblk0", ROOT_FLAG_SECTOR_KT, 512, 6);
		}
		else {
			read_sector("/dev/mmcblk0", ROOT_FLAG_SECTOR, 512, 6);
		}
		sprintf(root_flag, "%s", &sector_content);

		if(strstr(root_flag, "rooted")) {
			root_mmc = true;
		}
		else {
			root_mmc = false;
		}
	}
	{
		// Init ramdisk
		char * root_flag = read_file("/opt/root", true);
		if(strstr(root_flag, "rooted")) {
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
			mount("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");
			write_file("/mnt/flags/DONT_BOOT", "true\n");
			sync();
			umount("/mnt");
			info("Security policy was violated! Shutting down ...", INFO_FATAL);
			show_alert_splash(1, false);
			exit(EXIT_FAILURE);
		}
	}

	if(root == true) {
		info("Device is rooted; not enforcing security policy", INFO_WARNING);
	}
	else {
		info("Device is not rooted; enforcing security policy", INFO_OK);
	}

	{
		// Allow 3 seconds for power button input (boot mode & DFL mode)
		int fd = open(BUTTON_INPUT_DEVICE, O_RDONLY | O_NONBLOCK);
		struct input_event ev;
		time_t monitor_input_start = time(NULL);
		while(time(NULL) - monitor_input_start <= 3) {
			if(read(fd, &ev, sizeof(struct input_event)) > 0) {
				if(ev.code == KEY_POWER) {
					power_button_pressed = true;
				}
				// KEY_KATAKANA represents the brightness button on the Glo (N613)
				else if(ev.code == KEY_HOME || ev.code == KEY_KATAKANA) {
					other_button_pressed = true;
				}
			}
		}
	}

	// Mounting boot flags partition
	mount("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");

	// DFL mode
	if(root == true) {
		dfl = read_file("/mnt/flags/DFL", true);
		if((power_button_pressed == true && other_button_pressed == true) || (strstr(dfl, "true"))) {
			info("Entering Direct Firmware Loader mode (DFL) ...", INFO_OK);
			// Re-setting flag
			write_file("/mnt/flags/DFL", "false\n");
			launch_dfl();
		}
	}

	// BOOT_USB_DEBUG
	if(root == true) {
		boot_usb_debug = read_file("/mnt/flags/BOOT_USB_DEBUG", true);
		if(strstr(boot_usb_debug, "true")) {
			info("Starting init ramdisk boot USB debug framework", INFO_OK);
			// Re-setting flag
			write_file("/mnt/flags/BOOT_USB_DEBUG", "false\n");
			setup_usb_debug(true);
		}
	}

	// DIAGS_BOOT
	diags_boot = read_file("/mnt/flags/DIAGS_BOOT", true);

	// INITRD_DEBUG
	if(root == true) {
		initrd_debug = read_file("/mnt/flags/INITRD_DEBUG", true);
		if(strstr(initrd_debug, "true")) {
			info("Starting init ramdisk USB debug framework", INFO_OK);
			setup_usb_debug(false);
		}
	}

	// Unmounting boot flags partition
        umount("/mnt");

	// Handling boot mode switching
	if(power_button_pressed == true) {
		boot_mode = BOOT_DIAGNOSTICS;
		info("Power button input detected", INFO_WARNING);
		info("Boot mode: Diagnostics", INFO_OK);
	}
	else {
		if(strstr(diags_boot, "true")) {
			boot_mode = BOOT_DIAGNOSTICS;
			info("Boot mode: Diagnostics", INFO_OK);
		}
		else {
			boot_mode = BOOT_STANDARD;
			info("Boot mode: Standard", INFO_OK);
		}
	}

	if(boot_mode == BOOT_STANDARD) {
		// Standard mode
		// Checking whether we need to show an update splash or not
		if(!(strstr(display_debug, "true"))) {
			// WILL_UPDATE flag
			mount("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");
			will_update = read_file("/mnt/flags/WILL_UPDATE", true);
			umount("/mnt");

			if(strstr(will_update, "true")) {
				const char * arguments[] = { "/etc/init.d/inkbox-splash", "update_splash", NULL }; update_splash_pid = run_command("/etc/init.d/inkbox-splash", arguments, false);
			}
			else {
				{
					// Showing 'InkBox' splash
					const char * arguments[] = { "/etc/init.d/inkbox-splash", NULL }; run_command("/etc/init.d/inkbox-splash", arguments, true);
					sleep(2);
				}
				{
					// Initializing progress bar
					const char * arguments[] = { "/etc/init.d/inkbox-splash", "progress_bar_init", NULL }; run_command("/etc/init.d/inkbox-splash", arguments, false);
					sleep(2);

					set_progress(0);
					progress_sleep();
					set_progress(5);
					progress_sleep();
				}
			}
		}

		// Mounting root filesystem
		if(strstr(mount_rw, "true")) {
			// Mounting read-write
			const char * arguments[] = { "/etc/init.d/overlay-mount", "rw", NULL };
			int exit_code =	run_command("/etc/init.d/overlay-mount", arguments, true);
			if(exit_code != 0) {
				exit(exit_code);
			}
		}
		else {
			// Mounting read-only (default)
			const char * arguments[] = { "/etc/init.d/overlay-mount", "ro", NULL };
			int exit_code = run_command("/etc/init.d/overlay-mount", arguments, true);
			if(exit_code != 0) {
				exit(exit_code);
			}
		}

		info("Mounted root filesystem", INFO_OK);
		set_progress(15);
		progress_sleep();

		// Mounting P1 in root filesystem
		mount("/dev/mmcblk0p1", "/mnt/boot", "ext4", 0, "");
		mount("tmpfs", "/mnt/root", "tmpfs", 0, "size=8M");

		// Handling LOGIN_SHELL
		if(root == true) {
			if(strstr(login_shell, "bash")) {
				{
					const char * arguments[] = { "/bin/sed", "-i", "1s#.*#root:x:0:0:root:/root:/bin/bash#", "/opt/passwd_root", NULL }; run_command("/bin/sed", arguments, true);
				}
				{
					const char * arguments[] = { "/bin/sed", "-i", "30s#.*#user:x:1000:1000:Linux User,,,:/:/bin/bash#", "/opt/passwd_root", NULL }; run_command("/bin/sed", arguments, true);
				}
			}
			else if(strstr(login_shell, "zsh")) {
				{
					const char * arguments[] = { "/bin/sed", "-i", "1s#.*#root:x:0:0:root:/root:/usr/local/bin/zsh#", "/opt/passwd_root", NULL }; run_command("/bin/sed", arguments, true);
				}
				{
					const char * arguments[] = { "/bin/sed", "-i", "30s#.*#user:x:1000:1000:Linux User,,,:/:/usr/local/bin/zsh#", "/opt/passwd_root", NULL }; run_command("/bin/sed", arguments, true);
				}
			}
			else if(strstr(login_shell, "fish")) {
				{
					const char * arguments[] = { "/bin/sed", "-i", "1s#.*#root:x:0:0:root:/root:/usr/bin/fish#", "/opt/passwd_root", NULL }; run_command("/bin/sed", arguments, true);
				}
				{
					const char * arguments[] = { "/bin/sed", "-i", "30s#.*#user:x:1000:1000:Linux User,,,:/:/usr/bin/fish#", "/opt/passwd_root", NULL }; run_command("/bin/sed", arguments, true);
				}
				mkpath("/mnt/root/.config", 0755);
				mkpath("/mnt/root/.config/fish", 0755);
				write_file("/mnt/root/.config/fish/fish_variables", "# This file contains fish universal variable definitions.\n# VERSION: 3.0\nSETUVAR __fish_init_2_3_0:\\x1d\nSETUVAR __fish_init_3_x:\\x1d\nSETUVAR --export fish_user_paths:/usr/local/bin");
			}
			else {
				if(!(strstr(login_shell, "")) && !(strstr(login_shell, "ash"))) {
					char * message;
					sprintf(message, "'%s' is not a valid login shell; falling back to default", login_shell);
					info(message, INFO_WARNING);
				}
			}
		}

		// passwd file
		// Bind-mounting directly from initrd filesystem does not seem to work; copying file to temporary filesystem
		copy_file("/opt/passwd_root", "/tmp/passwd");
		mount("/tmp/passwd", "/mnt/etc/passwd", "", MS_BIND, "");

		// User storage
		mount("/dev/mmcblk0p4", "/mnt/opt/storage", "ext4", 0, "");
		// Configuration files
		mkpath("/mnt/opt/storage/config", 0755);
		mount("/mnt/opt/storage/config", "/mnt/opt/config", "", MS_BIND, "");
		// GUI bundle
		mkpath("/mnt/opt/storage/update", 0755);
		mount("/mnt/opt/storage/update", "/mnt/opt/update", "", MS_BIND, "");
		// X11/KoBox
		mkpath("/mnt/opt/storage/X11/rootfs/work", 0755);
		mkpath("/mnt/opt/storage/X11/rootfs/write", 0755);
		mount("/mnt/opt/storage/X11/rootfs", "/mnt/opt/X11/rootfs", "", MS_BIND, "");
		set_progress(30);
		progress_sleep();
		// GUI root filesystem
		mkpath("/mnt/opt/storage/gui_rootfs", 0755);
		mount("/mnt/opt/storage/gui_rootfs", "/mnt/opt/gui_rootfs", "", MS_BIND, "");
		// SSHd
		mkpath("/mnt/opt/storage/ssh", 0755);
		mount("/mnt/opt/storage/ssh", "/mnt/etc/ssh", "", MS_BIND, "");
		write_file("/mnt/opt/storage/ssh/sshd_config", "");
		write_file("/tmp/sshd_config", "PermitRootLogin yes\nSubsystem sftp internal-sftp\n# If sshfs doesn't work, first enable read-write support, then begin a connection with sshfs\n");
		mount("/tmp/sshd_config", "/mnt/etc/ssh/sshd_config", "", MS_BIND, "");
		set_progress(40);
		progress_sleep();

		// SquashFS archives
		mount_squashfs_archives();
		info("Mounted core SquashFS archives", INFO_OK);

		// Essential filesystems
		mount_essential_filesystems();
		// developer
		if(root == true) {
			mount("tmpfs", "/mnt/opt/developer", "tmpfs", 0, "size=128K");
		}
		info("Mounted essential filesystems", INFO_OK);
		set_progress(45);
		progress_sleep();

		// Wi-Fi
		// Firmware
		if(file_exists("/opt/firmware.sqsh") == true) {
			const char * arguments[] = { "/sbin/losetup", "/dev/loop4", "/opt/firmware.sqsh", NULL }; run_command("/sbin/losetup", arguments, true);
			mount("/dev/loop4", "/mnt/lib/firmware", "squashfs", MS_NODEV | MS_NOSUID | MS_NOEXEC, "");
		}
		// resolv.conf
		write_file("/tmp/resolv.conf", "");
		mount("/tmp/resolv.conf", "/mnt/etc/resolv.conf", "", MS_BIND, "");
		// DHCPcd
		mount("tmpfs", "/mnt/var/db/dhcpcd", "tmpfs", MS_NODEV | MS_NOSUID | MS_NOEXEC, "size=512K");
		write_file("/mnt/var/db/dhcpcd/duid", "");
		write_file("/mnt/opt/storage/dhcpcd_duid", "");
		mount("/mnt/opt/storage/dhcpcd_duid", "/mnt/var/db/dhcpcd/duid", "", MS_BIND, "");

		// Developer key
		{
			{
				const char * arguments[] = { "/etc/init.d/developer-key", NULL }; run_command("/etc/init.d/developer-key", arguments, true);
			}
			developer_key = read_file("/mnt/opt/developer/key/valid-key", true);
			// 'Developer mode' splash
			if(strstr(developer_key, "true") && strstr(will_update, "true")) {
				const char * arguments[] = { "/etc/init.d/inkbox-splash", "developer_splash", NULL }; run_command("/etc/init.d/inkbox-splash", arguments, false);
			}
		}

		// GUI root filesystem
		{
			// Validating digital signature
			const char * arguments[] = { "/usr/bin/openssl", "dgst", "-sha256", "-verify", "/mnt/opt/key/public.pem", "-signature", "/mnt/opt/storage/gui_rootfs.isa.dgst", "/mnt/opt/storage/gui_rootfs.isa", NULL };
			if(!run_command("/usr/bin/openssl", arguments, true) && !strstr(developer_key, "true")) {
				info("GUI root filesystem's digital signature is invalid!", INFO_FATAL);
				info("Aborting boot and powering off", INFO_FATAL);
				kill_process("inkbox-splash", SIGTERM);
				show_alert_splash(2, true);
				exit(EXIT_FAILURE);
			}
			else {
				// Mounting GUI root filesystem
				{
					const char * arguments[] = { "/bin/busybox", "chroot", "/mnt", "/bin/squashfuse", "/opt/storage/gui_rootfs.isa", "/opt/gui_rootfs/read", NULL }; run_command("/bin/busybox", arguments, true);
				}
				// Setting up GUI root filesystem overlay
				{
					const char * arguments[] = { "/bin/busybox", "chroot", "/mnt", "/usr/local/bin/unionfs", "-o", "cow,nonempty", "/opt/gui_rootfs/write=RW:/opt/gui_rootfs/read=RO", "/kobo", NULL }; run_command("/bin/busybox", arguments, true);
				}
				write_file("/mnt/kobo/inkbox/remount", "true");
			}
		}


		// X11
		write_file("/mnt/boot/flags/X11_STARTED", "false\n");
		set_progress(50);
		progress_sleep();

		if(strstr(x11_start, "true")) {
			const char * arguments[] = { "/etc/init.d/startx", NULL }; run_command("/etc/init.d/startx", arguments, true);
		}
		set_progress(90);
		progress_sleep();

		// Starting OpenRC & friends
		{
			// OpenRC sysinit
			{
				const char * arguments[] = { "/bin/busybox", "chroot", "/mnt", "/sbin/openrc", "sysinit", NULL }; run_command("/bin/busybox", arguments, true);
			}

			set_progress(100);
			usleep(500);
			write_file(PROGRESS_BAR_FIFO_PATH, "stop\n");

			// Init ramdisk named pipe
			{
				const char * arguments[] = { "/etc/init.d/initrd-fifo", NULL }; run_command("/etc/init.d/initrd-fifo", arguments, true);
			}

			// OpenRC boot
			{
				const char * arguments[] = { "/bin/busybox", "chroot", "/mnt", "/sbin/openrc", "boot", NULL }; run_command("/bin/busybox", arguments, true);
			}
			// OpenRC default
			{
				const char * arguments[] = { "/bin/busybox", "chroot", "/mnt", "/sbin/openrc", "default", NULL }; run_command("/bin/busybox", arguments, true);
			}
		}
	}
	else {
		// Preparing Diagnostics chroot environment
		// Mounting base Diagnostics root filesystem
		{
			const char * arguments[] = { "/etc/init.d/overlay-mount", "recovery", NULL };
			int ret = run_command("/etc/init.d/overlay-mount", arguments, true);
			if(ret != 0) {
				exit(ret);
			}
			info("Mounted base recovery filesystem", INFO_OK);
		}
		// Mounting boot flags partition
		mount("/dev/mmcblk0p1", "/mnt/boot", "ext4", 0, "");
		// Essential filesystems
		mount_essential_filesystems();
		info("Mounted essential filesystems", INFO_OK);
		// SquashFS archives
		mount_squashfs_archives();
		info("Mounted SquashFS archives", INFO_OK);

		// Launching Diagnostics subsysten
		{
			const char * arguments[] = { "/bin/busybox", "chroot", "/mnt", "/opt/bin/diagnostics_splash", NULL }; run_command("/bin/busybox", arguments, true);
		}
		{
			const char * arguments[] = { "/bin/busybox", "chroot", "/mnt", "/opt/recovery/launch.sh", NULL }; run_command("/bin/busybox", arguments, true);
		}
	}

	// Start getty in chroot
	const char * arguments[] = { "/bin/busybox", "chroot", "/mnt", "/sbin/getty", "-L", tty, "115200", "linux", NULL }; run_command("/bin/busybox", arguments, true);
	sleep(-1);
}

// https://github.com/Kobo-InkBox/inkbox-power-daemon/blob/8296c4a1811e3921ff98e9980504c24d23435dac/src/functions.cpp#L415-L430
int run_command(const char * path, const char * arguments[], bool wait) {
	int status = -1;
	int pid = 0;

	status = posix_spawn(&pid, path, NULL, NULL, (char**)arguments, NULL);
	if(status != 0) {
		fprintf(stderr, "Error in spawning process %s\n", path);
		return false;
	}

	if(wait == true) {
		int exit_code = 0;
		waitpid(pid, &exit_code, 0);
		return exit_code;
	}

	return pid;
}

// https://stackoverflow.com/a/230070/14164574
bool file_exists(char * file_path) {
	struct stat buffer;
	return (stat(file_path, &buffer) == 0);
}

// https://stackoverflow.com/a/3747128/14164574
char * read_file(char * file_path, bool strip_newline) {
	// Ensure that specified file exists, then try to read it
	if(access(file_path, F_OK) == 0) {
		FILE * fp;
		long lSize;
		char * buffer;

		fp = fopen(file_path , "rb");

		fseek(fp, 0L , SEEK_END);
		// If requested, remove trailing newline
		if(strip_newline == true) {
			lSize = ftell(fp) - 1;
		}
		else {
			lSize = ftell(fp);
		}
		rewind(fp);

		// Allocate memory for entire content
		buffer = calloc(1, lSize+1);
		if(!buffer) fclose(fp);

		// Copy the file into the buffer
		if(1 != fread(buffer, lSize, 1, fp)) {
			fclose(fp);
		}
		else {
			fclose(fp);
		}

		// Return the buffer
		return(buffer);
		free(buffer);
	}
	else {
		return("");
	}
}

// https://stackoverflow.com/a/14576624/14164574
bool write_file(char * file_path, char * content) {
	FILE *file = fopen(file_path, "wb");

	int rc = fputs(content, file);
	if (rc == EOF) {
		return false;
	}
	else {
		fclose(file);
	}
}

// https://stackoverflow.com/a/39191360/14164574
bool copy_file(char * source_file, char * destination_file) {
	FILE *fp_I;
	FILE *fp_O;

	fp_I = fopen(source_file, "rb");
	if(!fp_I) {
		perror(source_file);
		return false;
	}

	fp_O = fopen(destination_file, "wb");
	if(!fp_O) {
		fclose(fp_I);
		return false;
	}

	int c;
	while((c = getc(fp_I)) != EOF) {
		putc(c, fp_O);
	}

	fclose(fp_I);
	if(fclose(fp_O) != 0) {
		return false;
	}
	return true;
}

bool mkpath(char * path, mode_t mode) {
	if(!path) {
		errno = EINVAL;
		return 1;
	}

	if(strlen(path) == 1 && path[0] == '/')
		return 0;

	mkpath(dirname(strdupa(path)), mode);

	return mkdir(path, mode);
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
		printf("Socket error %s\n", strerror(errno));
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

// https://www.includehelp.com/c-programs/set-ip-address-in-linux.aspx
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
	mkpath("/modules", 0755);
	{
		const char * arguments[] = { "/sbin/losetup", "/dev/loop0", "/opt/modules.sqsh", NULL }; run_command("/sbin/losetup", arguments, true);
		mount("/dev/loop0", "/modules", "squashfs", 0, "");
	}

	if(strstr(device, "n705") || strstr(device, "n905b") || strstr(device, "n905c") || strstr(device, "n613")) {
		load_module("/modules/arcotg_udc.ko", "");
	}
	if(strstr(device, "n306") || strstr(device, "n873")) {
		load_module("/modules/fs/configfs/configfs.ko", "");
		load_module("/modules/drivers/usb/gadget/libcomposite.ko", "");
		load_module("/modules/drivers/usb/gadget/function/usb_f_mass_storage.ko", "");
	}
	if(!strstr(device, "emu")) {
		load_module("/modules/g_mass_storage.ko", "file=/dev/mmcblk0 removable=y stall=0");
	}

	// Unmount modules
	{
		umount("/modules");
		const char * arguments[] = { "/sbin/losetup", "/dev/loop0", "/opt/root.sqsh", NULL }; run_command("/sbin/losetup", arguments, true);
	}

	// Splash time
	const char * arguments[] = { "/etc/init.d/inkbox-splash", "dfl", NULL }; run_command("/etc/init.d/inkbox-splash", arguments, true);
	while(true) {
		info("This device is in DFL mode. Please reset it to resume normal operation.", INFO_OK);
		sleep(30);
	}
}

void setup_usb_debug(bool boot) {
	mkpath("/dev/pts", 0755);
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
	mkpath("/modules", 0755);
	{
		const char * arguments[] = { "/sbin/losetup", "/dev/loop0", "/opt/modules.sqsh", NULL }; run_command("/sbin/losetup", arguments, true);
		mount("/dev/loop0", "/modules", "squashfs", 0, "");
	}

	if(strstr(device, "n705") || strstr(device, "n905b") || strstr(device, "n905c") || strstr(device, "n613")) {
		load_module("/modules/arcotg_udc.ko", "");
	}
	if(strstr(device, "n705") || strstr(device, "n905b") || strstr(device, "n905c") || strstr(device, "n613") || strstr(device, "n236") || strstr(device, "n437")) {
		load_module("/modules/g_ether.ko", "");
	}
	else if(strstr(device, "n306") || strstr(device, "n873") || strstr(device, "bpi")) {
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
	else if(strstr(device, "kt")) {
		load_module("/modules/2.6.35-inkbox/kernel/drivers/usb/gadget/arcotg_udc.ko", "");
		load_module("/modules/2.6.35-inkbox/kernel/drivers/usb/gadget/g_ether.ko", "");
	}
	else if(strstr(device, "emu")) {
		;
	}
	else {
		load_module("/modules/g_ether.ko", "");
	}

	// Unmount modules
	{
		umount("/modules");
		const char * arguments[] = { "/sbin/losetup", "-d", "/dev/loop0", NULL }; run_command("/sbin/losetup", arguments, true);
	}

	// Setting up network interface
	set_if_up("usb0");
	// Checking for custom IP address
	if(usbnet_ip != NULL && usbnet_ip[0] != '\0') {
		if(set_if_ip_address("usb0", usbnet_ip) != 0) {
			set_if_ip_address("usb0", "192.168.2.2");
		}
	}
	else {
		set_if_ip_address("usb0", "192.168.2.2");
	}
}

void setup_shell() {
	// Starting getty in init ramdisk root
	const char * arguments[] = { "/sbin/getty", "-L", tty, "115200", "linux", NULL }; run_command("/bin/busybox", arguments, true);
	sleep(-1);
}

void read_sector(char * device_node, unsigned long sector, int sector_size, unsigned long bytes_to_read) {
	int fd = open(device_node, O_RDONLY);
	sector = sector * sector_size;
	lseek(fd, sector, SEEK_SET);
	read(fd, &sector_content, bytes_to_read);
	close(fd);
}

void show_alert_splash(int error_code, bool flag) {
	// Converting error code to char
	char code[10];
	sprintf(code, "%d", error_code);

	// Showing alert splash
	if(flag == true) {
		const char * arguments[] = { "/etc/init.d/inkbox-splash", "alert_splash", code, "flag", NULL };
		run_command("/etc/init.d/inkbox-splash", arguments, true);
	}
	else {
		const char * arguments[] = { "/etc/init.d/inkbox-splash", "alert_splash", code, NULL };
		run_command("/etc/init.d/inkbox-splash", arguments, true);
	}
}

void set_progress(int progress_value) {
	// Converting progress value to char
	char progress[3];
	sprintf(progress, "%d\n", progress_value);

	// Sending progress value to named pipe
	write_file(PROGRESS_BAR_FIFO_PATH, progress);
}

void progress_sleep() {
	usleep(500);
}

// https://github.com/Kobo-InkBox/inkbox-power-daemon/blob/80eb0500c3f360f78563cc380617a56c5b62c01f/src/appsFreeze.cpp#L21-L44
int get_pid_by_name(char * name) {
	struct dirent * entry = NULL;
	DIR * dp = NULL;

	char proc[] = "/proc";
	dp = opendir(proc);
	while((entry = readdir(dp))) {
		// cmdline is more accurate, sometimes status may be buggy
		char cmdline_file[1024];
		sprintf(cmdline_file, "%s%s/cmdline", proc, entry->d_name);
		char * cmdline = read_file(cmdline_file, true);
		// https://stackoverflow.com/questions/2340281/check-if-a-string-contains-a-string-in-c
		if(strstr(cmdline, name)) {
			// After closing directory, it's impossible to call entry->d_name
			int return_pid = atoi(entry->d_name);
			closedir(dp);
			return return_pid;
		}
	}
	// If we get here, we haven't found any PID
	closedir(dp);
	return -1;
}

// https://github.com/Kobo-InkBox/inkbox-power-daemon/blob/80eb0500c3f360f78563cc380617a56c5b62c01f/src/appsFreeze.cpp#L108-L114
void kill_process(char * name, int signal) {
	int pid = get_pid_by_name(name);
	if(pid != -1) {
		kill(pid, signal);
	}
}

void mount_essential_filesystems() {
	// proc
	mount("/proc", "/mnt/proc", "", MS_BIND | MS_REC, "");
	// sys
	mount("/sys", "/mnt/sys", "", MS_BIND | MS_REC, "");
	// dev
	mount("/dev", "/mnt/dev", "", MS_BIND | MS_REC, "");
	// tmp
	mount("tmpfs", "/mnt/tmp", "tmpfs", 0, "size=16M");
	// log
	mount("tmpfs", "/mnt/var/log", "tmpfs", 0, "size=8M");
}

void mount_squashfs_archives() {
	// Userspace 'root' flag
	if(root == true) {
		const char * arguments[] = { "/sbin/losetup", "/dev/loop7", "/opt/root.sqsh", NULL }; run_command("/sbin/losetup", arguments, true);
		mount("/dev/loop7", "/mnt/opt/root", "squashfs", MS_NODEV | MS_NOSUID | MS_NOEXEC, "");
	}
	// Public key
	{
		const char * arguments[] = { "/sbin/losetup", "/dev/loop6", "/opt/key.sqsh", NULL }; run_command("/sbin/losetup", arguments, true);
		mount("/dev/loop6", "/mnt/opt/key", "squashfs", MS_NODEV | MS_NOSUID | MS_NOEXEC, "");
	}
	// Modules
	{
		const char * arguments[] = { "/sbin/losetup", "/dev/loop5", "/opt/modules.sqsh", NULL }; run_command("/sbin/losetup", arguments, true);
		mount("/dev/loop5", "/mnt/lib/modules", "squashfs", MS_NODEV | MS_NOSUID | MS_NOEXEC, "");
	}
}
