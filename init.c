#include "init.h"

int main(void) {
	// Device identification
	device = read_file("/opt/device", true);
	if(!device) {
		device = alloca(8);
		if (!device) {
			// You've got bigger problems than no init if alloca failed...
			fprintf(stderr, "alloca failed\n");
			exit(EXIT_FAILURE);
		}
		strcpy(device, "UNKNOWN");
	}

	// TTY device
	if(MATCH(device, "emu")) {
		strcpy(tty, "ttyAMA0");
	}
	else if(MATCH(device, "bpi")) {
		strcpy(tty, "ttyS0");
	}
	else {
		strcpy(tty, "ttymxc0");
	}

	// Filesystems
	MOUNT("proc", "/proc", "proc", MS_NOSUID, "");
	MOUNT("sysfs", "/sys", "sysfs", 0, "");
	// Prevent unpriviledged users to read and write to sysfs by default
	REAP("/bin/sh", "-c", "find /sys -type f -print0 | xargs -0 chmod 700");
	sleep(1);
	MOUNT("devtmpfs", "/dev", "devtmpfs", 0, "");
	MOUNT("tmpfs", "/tmp", "tmpfs", 0, "size=16M");

	// Kobo Clara HD (N249) handling
	if(MATCH(device, "n249")) {
		// Unsquashing modules
		REAP("/usr/bin/unsquashfs", "-d", "/lib/modules", "/opt/modules.sqsh");

		// Put epdc.fw in the right place
		mkpath("/lib/firmware/imx", 0755);
		REAP("/usr/bin/unsquashfs", "-d", "/lib/firmware/imx/epdc", "/opt/firmware.sqsh");

		// Load some modules
		load_module("/lib/modules/5.16.0/kernel/drivers/hwmon/tps6518x-hwmon.ko", "");
		load_module("/lib/modules/5.16.0/kernel/drivers/regulator/tps6518x-regulator.ko", "");
		load_module("/lib/modules/5.16.0/kernel/drivers/video/fbdev/mxc/mxc_epdc_v2_fb.ko", "");
	}

	// Kindle Touch (KT) handling
	if(MATCH(device, "kt")) {
		// Unsquashing modules
		REAP("/usr/bin/unsquashfs", "-d", "/lib/modules", "/opt/modules.sqsh");

		// Loading framebuffer modules
		// eink_fb_waveform
		load_module("/lib/modules/2.6.35-inkbox/kernel/drivers/video/eink/waveform/eink_fb_waveform.ko", "");
		// Mounting P1 of MMC to retrieve FB waveform
		MOUNT("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");
		// Check if waveform data exists
		// mxc_epdc_fb
		if(FILE_EXISTS("/mnt/waveform/waveform.wbf") && FILE_EXISTS("/mnt/waveform/waveform.wrf")) {
			// Use device waveform
			load_module("/lib/modules/2.6.35-inkbox/kernel/drivers/video/mxc/mxc_epdc_fb.ko", "default_panel_hw_init=1 default_update_mode=1 waveform_to_use=/mnt/waveform/waveform.wbf");
		}
		else {
			// Use built-in waveform
			load_module("/lib/modules/2.6.35-inkbox/kernel/drivers/video/mxc/mxc_epdc_fb.ko", "default_panel_hw_init=1 default_update_mode=1 waveform_to_use=built-in");
		}
		// Unmounting P1
		umount("/mnt");

		// Refresh screen twice to initialize FB module properly
		for(int i = 0; i < 2; i++) {
			REAP("/usr/bin/fbink", "-k", "-f", "-w", "-q");
		}
	}

	// Setting hostname
	sethostname("inkbox", 6);

	// Setting loopack interface UP
	set_if_up("lo");

	// Setting up boot flags partition (P1)
	MOUNT("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");
	mkpath("/mnt/flags", 0755);

	// Handling DISPLAY_DEBUG flag (https://inkbox.ddns.net/wiki/index.php?title=Boot_flags)
	char * display_debug = read_file("/mnt/flags/DISPLAY_DEBUG", true);
	bool is_display_debug = false;
	if(MATCH(display_debug, "true")) {
		is_display_debug = true;
	}
	free(display_debug);
	display_debug = NULL;
	if(is_display_debug) {
		mkfifo(SERIAL_FIFO_PATH, 0x29A);
		pid_t splash_pid = RUN("/etc/init.d/inkbox-splash", "display_debug"); // FIXME: Kill and collect?
		sleep(5);
		// Redirecting all stdout output to display debug's named pipe
		freopen(SERIAL_FIFO_PATH, "a+", stdout);
	}

	// USB networking
	usbnet_device_address = read_file("/mnt/flags/USBNET_DEVICE_ADDRESS", true);
	usbnet_host_address = read_file("/mnt/flags/USBNET_HOST_ADDRESS", true);
	usbnet_ip = read_file("/mnt/flags/USBNET_IP", true);

	// MOUNT_RW
	char * mount_rw = read_file("/mnt/flags/MOUNT_RW", true);
	bool is_mount_rw = false;
	if(MATCH(mount_rw, "true")) {
		is_mount_rw = true;
	}
	free(mount_rw);
	mount_rw = NULL;

	// LOGIN_SHELL
	char * login_shell = read_file("/mnt/flags/LOGIN_SHELL", true);

	// X11_START
	char * x11_start = read_file("/mnt/flags/X11_START", true);
	bool with_x11 = false;
	if(MATCH(x11_start, "true")) {
		with_x11 = true;
	}
	free(x11_start);
	x11_start = NULL;

	// Information header
	// Getting kernel information
	// Kernel version
	struct utsname uname_data;
	uname(&uname_data);
	char * kernel_version = NULL;
	// Pass 0 to snprintf to let it tell us how large of a buffer we need
	int size = snprintf(kernel_version, 0U, "%s %s %s", uname_data.sysname, uname_data.nodename, uname_data.version);
	if(size < 0) {
		kernel_version = strdupa("UNKNOWN");
		if(!kernel_version) {
			perror("strdupa");
			exit(EXIT_FAILURE);
		}
	}
	else {
		kernel_version = alloca((size_t) size + 1U); // We need a trailing NULL
		if(!kernel_version) {
			fprintf(stderr, "alloca failed\n");
			exit(EXIT_FAILURE);
		}
		if(snprintf(kernel_version, (size_t) size + 1U, "%s %s %s", uname_data.sysname, uname_data.nodename, uname_data.version) < 0) {
			perror("snprintf");

			// Fallback
			kernel_version = strdupa("UNKNOWN");
			if(!kernel_version) {
				perror("strdupa");
				exit(EXIT_FAILURE);
			}
		}
	}
	// Kernel build ID
	char * kernel_build_id = read_file("/opt/build_id", true);
	// Kernel Git commit
	char * kernel_git_commit = read_file("/opt/commit", true);
	printf("\n%s GNU/Linux\nInkBox OS, kernel build %s, commit %s\n\n", kernel_version, kernel_build_id, kernel_git_commit);
	printf("Copyright (C) 2021-2023 Nicolas Mailloux <nicolecrivain@gmail.com>\n");
	free(kernel_git_commit);
	kernel_git_commit = NULL;
	free(kernel_build_id);
	kernel_build_id = NULL;

	// Checking filesystems
	info("Checking filesystems ...", INFO_OK);
	// Unmounting P1 for inspection by fsck.ext4
	umount("/mnt");
	printf("\n");
	// P1
	REAP("/usr/bin/fsck.ext4", "-y", "/dev/mmcblk0p1");
	// P2
	if(MATCH(device, "n873")) {
		REAP("/usr/bin/fsck.ext4", "-y", "/dev/mmcblk0p5");
	}
	else {
		REAP("/usr/bin/fsck.ext4", "-y", "/dev/mmcblk0p2");
	}
	// P3
	REAP("/usr/bin/fsck.ext4", "-y", "/dev/mmcblk0p3");
	// P4
	REAP("/usr/bin/fsck.ext4", "-y", "/dev/mmcblk0p4");
	printf("\n");
	// Remounting P1
	MOUNT("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");

	// Universal ID check
	REAP("/opt/bin/uidgen");

	// DONT_BOOT
	char * dont_boot = read_file("/mnt/flags/DONT_BOOT", true);
	if(MATCH(dont_boot, "true")) {
		info("Device is locked down and will not boot", INFO_FATAL);
		show_alert_splash(1, false);
		exit(EXIT_FAILURE);
	}
	free(dont_boot);
	dont_boot = NULL;

	// ENCRYPT_LOCK
	char * encrypt_lock = read_file("/mnt/flags/ENCRYPT_LOCK", true);
	if(encrypt_lock) {
		time_t current_epoch = time(NULL);
		long lock_epoch = strtol(encrypt_lock, NULL, 0);
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
	free(encrypt_lock);
	encrypt_lock = NULL;

	// Unmounting boot flags partition
	umount("/mnt");

	// Are we spawning a shell?
	// https://stackoverflow.com/a/19186027/14164574
	printf("(initrd) Hit ENTER to stop auto-boot ... ");
	fflush(stdout);

	struct pollfd pfd   = { 0 };
	pfd.fd              = fileno(stdin);
	pfd.events          = POLLIN;
	pfd.revents         = 0;

	while(true) {
		// Wait only 3 seconds for user input
		int poll_num = poll(&pfd, 1, 3 * 1000);
		if(poll_num == -1) {
			if (errno == EINTR) {
				continue;
			}
			perror("poll");
			exit(EXIT_FAILURE);
		}

		if(poll_num > 0) {
			// Drain stdin first
			char buf[PIPE_BUF]; // stdin should be line-buffered if it's a terminal, that should be more than large enough
			read(pfd.fd, buf, sizeof(buf));

			setup_shell();
			break;
		}

		if(poll_num == 0) {
			// Timed out
			printf("\n\n");
			break;
		}
	}

	// Checking if the 'root' flag is set
	bool root_mmc = false;
	char mmc_root_flag[ROOT_FLAG_SIZE] = { 0 };
	// MMC
	if(MATCH(device, "kt")) {
		read_sector(mmc_root_flag, sizeof(mmc_root_flag), "/dev/mmcblk0", ROOT_FLAG_SECTOR_KT, 512);
	}
	else {
		read_sector(mmc_root_flag, sizeof(mmc_root_flag), "/dev/mmcblk0", ROOT_FLAG_SECTOR, 512);
	}

	if(memcmp(mmc_root_flag, "rooted", ROOT_FLAG_SIZE) == 0) {
		root_mmc = true;
	}
	else {
		root_mmc = false;
	}
	bool root_initrd = false;
	// Init ramdisk
	char * root_flag = read_file("/opt/root", true);
	if(MATCH(root_flag, "rooted")) {
		root_initrd = true;
	}
	else {
		root_initrd = false;
	}
	free(root_flag);

	if(root_mmc && root_initrd) {
		root = true;
	}
	else {
		root = false;
		if(root_mmc || root_initrd) {
			MOUNT("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");
			write_file("/mnt/flags/DONT_BOOT", "true\n");
			sync();
			umount("/mnt");
			info("Security policy was violated! Shutting down ...", INFO_FATAL);
			show_alert_splash(1, false);
			exit(EXIT_FAILURE);
		}
	}

	if(root) {
		info("Device is rooted; not enforcing security policy", INFO_WARNING);
	}
	else {
		info("Device is not rooted; enforcing security policy", INFO_OK);
	}

	bool power_button_pressed = false;
	bool other_button_pressed = false;
	// Allow 3 seconds for power button input (boot mode & DFL mode)
	int fd = open(BUTTON_INPUT_DEVICE, O_RDONLY | O_NONBLOCK);
	if (fd == -1) {
		perror("open");
		exit(EXIT_FAILURE);
	}

	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;

	while(true) {
		/*
		 * Linux modifies tv to reflect the amount of time not slept (i.e., the amount of time left in the timeout),
		 * we rely on that to only loop for 3s max without having to compute anything ourselves.
		*/
		int rv = select(fd + 1, &rfds, NULL, NULL, &tv);

		if(rv == -1 && errno != EINTR) {
			perror("select");
			exit(EXIT_FAILURE);
		}
		else if(rv) {
			struct input_event ev;
			if(read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
				if(ev.code == KEY_POWER && ev.value == 1) {
					power_button_pressed = true;
				}
				// KEY_KATAKANA represents the brightness button on the Glo (N613)
				else if((ev.code == KEY_HOME || ev.code == KEY_KATAKANA) && ev.value == 1) {
					other_button_pressed = true;
				}
			}
		}
		else {
			// Timeout
			break;
		}
	}

	close(fd);

	// Mounting boot flags partition
	MOUNT("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");

	// DFL mode
	if(root) {
		char * dfl = read_file("/mnt/flags/DFL", true);
		if((power_button_pressed && other_button_pressed) || MATCH(dfl, "true")) {
			info("Entering Direct Firmware Loader mode (DFL) ...", INFO_OK);
			// Re-setting flag
			write_file("/mnt/flags/DFL", "false\n");
			launch_dfl();
		}
		free(dfl);
	}

	// BOOT_USB_DEBUG
	if(root) {
		char * boot_usb_debug = read_file("/mnt/flags/BOOT_USB_DEBUG", true);
		if(MATCH(boot_usb_debug, "true")) {
			info("Starting init ramdisk boot USB debug framework", INFO_OK);
			// Re-setting flag
			write_file("/mnt/flags/BOOT_USB_DEBUG", "false\n");
			setup_usb_debug(true);
		}
		free(boot_usb_debug);
	}

	// DIAGS_BOOT
	char * diags_boot = read_file("/mnt/flags/DIAGS_BOOT", true);

	// INITRD_DEBUG
	if(root) {
		char * initrd_debug = read_file("/mnt/flags/INITRD_DEBUG", true);
		if(MATCH(initrd_debug, "true")) {
			info("Starting init ramdisk USB debug framework", INFO_OK);
			setup_usb_debug(false);
		}
		free(initrd_debug);
	}

	// Unmounting boot flags partition
        umount("/mnt");

	// Handling boot mode switching
	int boot_mode = BOOT_STANDARD;
	if(power_button_pressed) {
		boot_mode = BOOT_DIAGNOSTICS;
		info("Power button input detected", INFO_WARNING);
		info("Boot mode: Diagnostics", INFO_OK);
	}
	else {
		if(MATCH(diags_boot, "true")) {
			boot_mode = BOOT_DIAGNOSTICS;
			info("Boot mode: Diagnostics", INFO_OK);
		}
		else {
			boot_mode = BOOT_STANDARD;
			info("Boot mode: Standard", INFO_OK);
		}
	}
	free(diags_boot);
	diags_boot = NULL;

	bool do_update = false;
	if(boot_mode == BOOT_STANDARD) {
		// Standard mode
		// Checking whether we need to show an update splash or not
		if(!is_display_debug) {
			// WILL_UPDATE flag
			MOUNT("/dev/mmcblk0p1", "/mnt", "ext4", 0, "");
			char * will_update = read_file("/mnt/flags/WILL_UPDATE", true);
			umount("/mnt");

			if(MATCH(will_update, "true")) {
				do_update = true;
				pid_t splash_pid = RUN("/etc/init.d/inkbox-splash", "update_splash"); // FIXME: Kill & collect?
			}
			else {
				// Showing 'InkBox' splash
				REAP("/etc/init.d/inkbox-splash");
				sleep(2);

				// Initializing progress bar
				pid_t splash_pid = RUN("/etc/init.d/inkbox-splash", "progress_bar_init"); // FIXME: Kill & collect?
				sleep(2);

				set_progress(0);
				progress_sleep();
				set_progress(5);
				progress_sleep();
			}
			free(will_update);
		}

		// Mounting root filesystem
		if(is_mount_rw) {
			// Mounting read-write
			long int exit_code = REAP("/etc/init.d/overlay-mount", "rw");
			if(exit_code != 0) {
				exit(exit_code);
			}
		}
		else {
			// Mounting read-only (default)
			long int exit_code = REAP("/etc/init.d/overlay-mount", "ro");
			if(exit_code != 0) {
				exit(exit_code);
			}
		}

		info("Mounted root filesystem", INFO_OK);
		set_progress(15);
		progress_sleep();

		// Mounting P1 in root filesystem
		MOUNT("/dev/mmcblk0p1", "/mnt/boot", "ext4", 0, "");
		MOUNT("tmpfs", "/mnt/root", "tmpfs", 0, "size=8M");

		// Handling LOGIN_SHELL
		if(root) {
			if(MATCH(login_shell, "bash")) {
				REAP("/bin/sed", "-i", "1s#.*#root:x:0:0:root:/root:/bin/bash#", "/opt/passwd_root");
				REAP("/bin/sed", "-i", "30s#.*#user:x:1000:1000:Linux User,,,:/:/bin/bash#", "/opt/passwd_root");
			}
			else if(MATCH(login_shell, "zsh")) {
				REAP("/bin/sed", "-i", "1s#.*#root:x:0:0:root:/root:/usr/local/bin/zsh#", "/opt/passwd_root");
				REAP("/bin/sed", "-i", "30s#.*#user:x:1000:1000:Linux User,,,:/:/usr/local/bin/zsh#", "/opt/passwd_root");
			}
			else if(MATCH(login_shell, "fish")) {
				REAP("/bin/sed", "-i", "1s#.*#root:x:0:0:root:/root:/usr/bin/fish#", "/opt/passwd_root");
				REAP("/bin/sed", "-i", "30s#.*#user:x:1000:1000:Linux User,,,:/:/usr/bin/fish#", "/opt/passwd_root");
				mkpath("/mnt/root/.config", 0755);
				mkpath("/mnt/root/.config/fish", 0755);
				write_file("/mnt/root/.config/fish/fish_variables", "# This file contains fish universal variable definitions.\n# VERSION: 3.0\nSETUVAR __fish_init_2_3_0:\\x1d\nSETUVAR __fish_init_3_x:\\x1d\nSETUVAR --export fish_user_paths:/usr/local/bin");
			}
			else {
				if(login_shell != NULL) {
					if(NOT_MATCH(login_shell, "") && NOT_MATCH(login_shell, "ash")) {
						char message_buff[256] = { 0 };
						snprintf(message_buff, sizeof(message_buff), "'%s' is not a valid login shell; falling back to default", login_shell);
						info(message_buff, INFO_WARNING);
					}
				}
			}
		}
		free(login_shell);
		login_shell = NULL;

		// passwd file
		// Bind-mounting directly from initrd filesystem does not seem to work; copying file to temporary filesystem
		copy_file("/opt/passwd_root", "/tmp/passwd");
		MOUNT("/tmp/passwd", "/mnt/etc/passwd", "", MS_BIND, "");
		// User storage
		MOUNT("/dev/mmcblk0p4", "/mnt/opt/storage", "ext4", 0, "");
		// Configuration files
		mkpath("/mnt/opt/storage/config", 0755);
		MOUNT("/mnt/opt/storage/config", "/mnt/opt/config", "", MS_BIND, "");
		// GUI bundle
		mkpath("/mnt/opt/storage/update", 0755);
		MOUNT("/mnt/opt/storage/update", "/mnt/opt/update", "", MS_BIND, "");
		// X11/KoBox
		mkpath("/mnt/opt/storage/X11/rootfs/work", 0755);
		mkpath("/mnt/opt/storage/X11/rootfs/write", 0755);
		MOUNT("/mnt/opt/storage/X11/rootfs", "/mnt/opt/X11/rootfs", "", MS_BIND, "");
		set_progress(30);
		progress_sleep();
		// GUI root filesystem
		mkpath("/mnt/opt/storage/gui_rootfs", 0755);
		MOUNT("/mnt/opt/storage/gui_rootfs", "/mnt/opt/gui_rootfs", "", MS_BIND, "");
		// SSHd
		mkpath("/mnt/opt/storage/ssh", 0755);
		MOUNT("/mnt/opt/storage/ssh", "/mnt/etc/ssh", "", MS_BIND, "");
		write_file("/mnt/opt/storage/ssh/sshd_config", "");
		write_file("/tmp/sshd_config", "PermitRootLogin yes\nSubsystem sftp internal-sftp\n# If sshfs doesn't work, first enable read-write support, then begin a connection with sshfs\n");
		MOUNT("/tmp/sshd_config", "/mnt/etc/ssh/sshd_config", "", MS_BIND, "");
		set_progress(40);
		progress_sleep();

		// SquashFS archives
		mount_squashfs_archives();
		info("Mounted core SquashFS archives", INFO_OK);

		// Essential filesystems
		mount_essential_filesystems();
		// developer
		if(root) {
			MOUNT("tmpfs", "/mnt/opt/developer", "tmpfs", 0, "size=128K");
		}
		info("Mounted essential filesystems", INFO_OK);
		set_progress(45);
		progress_sleep();

		// Wi-Fi
		// Firmware
		if(FILE_EXISTS("/opt/firmware.sqsh")) {
			REAP("/sbin/losetup", "/dev/loop4", "/opt/firmware.sqsh");
			MOUNT("/dev/loop4", "/mnt/lib/firmware", "squashfs", MS_NODEV | MS_NOSUID | MS_NOEXEC, "");
		}
		// resolv.conf
		write_file("/tmp/resolv.conf", "");
		MOUNT("/tmp/resolv.conf", "/mnt/etc/resolv.conf", "", MS_BIND, "");
		// DHCPcd
		MOUNT("tmpfs", "/mnt/var/db/dhcpcd", "tmpfs", MS_NODEV | MS_NOSUID | MS_NOEXEC, "size=512K");
		write_file("/mnt/var/db/dhcpcd/duid", "");
		write_file("/mnt/opt/storage/dhcpcd_duid", "");
		MOUNT("/mnt/opt/storage/dhcpcd_duid", "/mnt/var/db/dhcpcd/duid", "", MS_BIND, "");

		// Developer key
		char * developer_key = NULL;
		REAP("/etc/init.d/developer-key");
		developer_key = read_file("/mnt/opt/developer/key/valid-key", true);
		// 'Developer mode' splash
		if(MATCH(developer_key, "true")) {
			pid_t splash_pid = RUN("/etc/init.d/inkbox-splash", "developer_splash"); // FIXME: Kill & collect?
		}

		// GUI root filesystem
		// Validating digital signature
		long int rc = REAP("/usr/bin/openssl", "dgst", "-sha256", "-verify", "/mnt/opt/key/public.pem", "-signature", "/mnt/opt/storage/gui_rootfs.isa.dgst", "/mnt/opt/storage/gui_rootfs.isa");
		if(rc != 0 && NOT_MATCH(developer_key, "true")) {
			info("GUI root filesystem's digital signature is invalid!", INFO_FATAL);
			info("Aborting boot and powering off", INFO_FATAL);
			kill_process("inkbox-splash");
			show_alert_splash(2, true);
			exit(EXIT_FAILURE);
		}
		else {
			// Mounting GUI root filesystem
			REAP("/bin/busybox", "chroot", "/mnt", "/bin/squashfuse", "/opt/storage/gui_rootfs.isa", "/opt/gui_rootfs/read");
			// Setting up GUI root filesystem overlay
			REAP("/bin/busybox", "chroot", "/mnt", "/usr/local/bin/unionfs", "-o", "cow,nonempty", "/opt/gui_rootfs/write=RW:/opt/gui_rootfs/read=RO", "/kobo");
			write_file("/mnt/kobo/inkbox/remount", "true");
		}


		// X11
		write_file("/mnt/boot/flags/X11_STARTED", "false\n");
		set_progress(50);
		progress_sleep();

		if(with_x11) {
			REAP("/etc/init.d/startx");
		}
		set_progress(90);
		progress_sleep();

		// Starting OpenRC & friends
		// OpenRC sysinit
		REAP("/bin/busybox", "chroot", "/mnt", "/sbin/openrc", "sysinit");

		set_progress(100);
		usleep(500);
		write_file(PROGRESS_BAR_FIFO_PATH, "stop\n");

		// Init ramdisk named pipe
		REAP("/etc/init.d/initrd-fifo");

		// OpenRC boot
		REAP("/bin/busybox", "chroot", "/mnt", "/sbin/openrc", "boot");
		// OpenRC default
		REAP("/bin/busybox", "chroot", "/mnt", "/sbin/openrc", "default");
	}
	else {
		// Preparing Diagnostics chroot environment
		// Mounting base Diagnostics root filesystem
		long int rc = REAP("/etc/init.d/overlay-mount", "recovery");
		if(rc != 0) {
			exit(rc);
		}
		info("Mounted base recovery filesystem", INFO_OK);
		// Mounting boot flags partition
		MOUNT("/dev/mmcblk0p1", "/mnt/boot", "ext4", 0, "");
		// Essential filesystems
		mount_essential_filesystems();
		info("Mounted essential filesystems", INFO_OK);
		// SquashFS archives
		mount_squashfs_archives();
		info("Mounted SquashFS archives", INFO_OK);

		// Launching Diagnostics subsysten
		REAP("/bin/busybox", "chroot", "/mnt", "/opt/bin/diagnostics_splash");
		REAP("/bin/busybox", "chroot", "/mnt", "/opt/recovery/launch.sh");
	}

	// Start getty in chroot
	while(true) {
		REAP("/bin/busybox", "chroot", "/mnt", "/sbin/getty", "-L", tty, "115200", "linux");
	}
}

long int run_command(const char * path, const char * const arguments[], bool wait) {
	pid_t pid = -1;

#pragma GCC diagnostic   push
#pragma GCC diagnostic   ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic   ignored "-Wdiscarded-qualifiers"
#pragma GCC diagnostic   ignored "-Wincompatible-pointer-types"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types-discards-qualifiers"
	int status = posix_spawn(&pid, path, NULL, NULL, arguments, NULL);
#pragma GCC diagnostic pop
	if(status != 0) {
		fprintf(stderr, "Error in spawning process %s (%s)\n", path, strerror(status));
		return -(EXIT_FAILURE);
	}

	if(wait) {
		pid_t ret;
		int wstatus;
		do {
			ret = waitpid(pid, &wstatus, 0);
		} while(ret == -1 && errno == EINTR);
		if(ret != pid) {
			perror("waitpid");
			return -(EXIT_FAILURE);
		}
		else {
			if(WIFEXITED(wstatus)) {
				int exitcode = WEXITSTATUS(wstatus);
				return exitcode;
			}
			else if (WIFSIGNALED(wstatus)) {
				int sigcode = WTERMSIG(wstatus);
				psignal(sigcode, "child caught signal");
				// Help caller grok the difference between exited or terminated by a signal by using a negative return value.
				return -(sigcode);
			}
			// If the child caught a SIGSTOP because of ptrace, we're not handling it separately (i.e., we'll silently return `pid`).
			// There are facilities available to handle that, but they require greater care in how the looping is handled (c.f., waitpid(2)).
		}
	}

	return pid;
}

// https://stackoverflow.com/a/3747128/14164574
char * read_file(const char * file_path, bool strip_newline) {
	// Ensure that specified file exists, then try to read it
	if(access(file_path, F_OK) != 0) {
		return NULL;
	}

	FILE * fp = fopen(file_path , "rb");
	if(!fp) {
		return NULL;
	}

	fseek(fp, 0L, SEEK_END);
	long bytes = ftell(fp);
	rewind(fp);

	// Allocate memory for entire content
	char * buffer = NULL;
	buffer = calloc(sizeof(*buffer), (size_t) bytes + 1U);
	if(!buffer) {
		goto cleanup;
	}

	// Copy the file into the buffer
	if(fread(buffer, sizeof(*buffer), (size_t) bytes, fp) < (size_t) bytes || ferror(fp) != 0) {
		// Short read or error, meep!
		free(buffer);
		buffer = NULL;
		goto cleanup;
	}

	// If requested, remove trailing newline
	if(strip_newline && bytes > 0 && buffer[bytes - 1] == '\n') {
		buffer[bytes - 1] = '\0';
	}

cleanup:
	fclose(fp);
	return buffer; // May be NULL (indicates failure)
}

// https://stackoverflow.com/a/14576624/14164574
bool write_file(const char * file_path, const char * content) {
	FILE *file = fopen(file_path, "wb");
	if(!file) {
		return false;
	}

	int rc = fputs(content, file);
	fclose(file);
	return !!(rc != EOF);
}

// https://stackoverflow.com/a/39191360/14164574
bool copy_file(const char * source_file, const char * destination_file) {
	FILE * fp_I = fopen(source_file, "rb");
	if(!fp_I) {
		perror(source_file);
		return false;
	}

	FILE * fp_O = fopen(destination_file, "wb");
	if(!fp_O) {
		fclose(fp_I);
		return false;
	}

	char buffer[PIPE_BUF];
	size_t len = 0U;
	while((len = fread(buffer, sizeof(*buffer), PIPE_BUF, fp_I)) > 0) {
		fwrite(buffer, sizeof(*buffer), len, fp_O);
	}

	fclose(fp_I);
	if(fclose(fp_O) != 0) {
		return false;
	}
	return true;
}

int mkpath(const char * path, mode_t mode) {
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
int load_module(const char * module_path, const char * params) {
	int fd = open(module_path, O_RDONLY);
	if(fd == -1) {
		perror("open");
		return EXIT_FAILURE;
	}
	struct stat st;
	if(fstat(fd, &st) == -1) {
		perror("fstat");
		return EXIT_FAILURE;
	}
	size_t image_size = (size_t) st.st_size;
	void * image = malloc(image_size);
	if(!image) {
		perror("malloc");
		close(fd);
		return EXIT_FAILURE;
	}
	if(read(fd, image, image_size) != (ssize_t) image_size) {
		fprintf(stderr, "load_module %s: failed/short read\n", module_path);
		close(fd);
		return EXIT_FAILURE;
	}
	close(fd);

	int rc = initModule(image, image_size, params);
	free(image);
	if(rc != 0) {
		fprintf(stderr, "Couldn't init module %s\n", module_path);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

// https://stackoverflow.com/a/49334887/14164574
int set_if_flags(const char * if_name, short flags) {
	int res = 0;

	int fd = -1;
	if((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("socket");
		res = 1;
		goto cleanup;
	}

	struct ifreq ifr;
	ifr.ifr_flags = flags;
	strncpy(ifr.ifr_name, if_name, IFNAMSIZ);

	res = ioctl(fd, SIOCSIFFLAGS, &ifr);
	if(res < 0) {
		fprintf(stderr, "Interface '%s': Error: SIOCSIFFLAGS failed: %s\n", if_name, strerror(errno));
	}
	else {
		printf("Interface '%s': flags set to %04X.\n", if_name, (unsigned int) flags);
	}

cleanup:
	close(fd);
	return res;
}

int set_if_up(const char * if_name) {
    return set_if_flags(if_name, IFF_UP);
}

// https://www.includehelp.com/c-programs/set-ip-address-in-linux.aspx
int set_if_ip_address(const char * if_name, const char * ip_address) {
	int res = 0;

	// AF_INET - to define network interface IPv4
	// Creating socket for it
	int fd = -1;
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("socket");
		res = 1;
		goto cleanup;
	}

	struct ifreq ifr;
	// AF_INET - to define IPv4 Address type
	ifr.ifr_addr.sa_family = AF_INET;

	// eth0 - define the ifr_name - port name
	strncpy(ifr.ifr_name, if_name, IFNAMSIZ);

	// Defining sockaddr_in
	struct sockaddr_in * addr;
	addr = (struct sockaddr_in*)&ifr.ifr_addr;

	// Convert IP address in correct format to write
	inet_pton(AF_INET, ip_address, &addr->sin_addr);

	// Setting IP Address using ioctl
	res = ioctl(fd, SIOCSIFADDR, &ifr);

cleanup:
	close(fd);
	return res;
}

int info(const char * message, int mode) {
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

void launch_dfl(void) {
	// Loading USB Mass Storage (USBMS) modules
	mkpath("/modules", 0755);
	REAP("/sbin/losetup", "/dev/loop0", "/opt/modules.sqsh");
	MOUNT("/dev/loop0", "/modules", "squashfs", 0, "");

	if(MATCH(device, "n705") || MATCH(device, "n905b") || MATCH(device, "n905c") || MATCH(device, "n613")) {
		load_module("/modules/arcotg_udc.ko", "");
	}
	if(MATCH(device, "n306") || MATCH(device, "n873")) {
		load_module("/modules/fs/configfs/configfs.ko", "");
		load_module("/modules/drivers/usb/gadget/libcomposite.ko", "");
		load_module("/modules/drivers/usb/gadget/function/usb_f_mass_storage.ko", "");
	}
	if(NOT_MATCH(device, "emu")) {
		load_module("/modules/g_mass_storage.ko", "file=/dev/mmcblk0 removable=y stall=0");
	}

	// Unmount modules
	umount("/modules");
	REAP("/sbin/losetup", "/dev/loop0", "/opt/root.sqsh");

	// Splash time
	RUN("/etc/init.d/inkbox-splash", "dfl");
	while(true) {
		info("This device is in DFL mode. Please reset it to resume normal operation.", INFO_OK);
		sleep(30);
	}
}

void setup_usb_debug(bool boot) {
	mkpath("/dev/pts", 0755);
	MOUNT("devpts", "/dev/pts", "devpts", 0, "");
	// Telnet server
	RUN("/bin/busybox", "telnetd");
	// FTP server
	RUN("/bin/busybox", "tcpsvd", "-vE", "0.0.0.0", "21", "ftpd", "-A", "-w", "/");

	if(boot) {
		// Set up USB networking interface
		setup_usbnet();
		// Splash time
		REAP("/etc/init.d/inkbox-splash", "usb_debug"); // FIXME: Why does this one waits and collects?
		while(true) {
			info("This device is in boot-time USB debug mode. Please reset or reboot it to resume normal operation.", INFO_OK);
			sleep(30);
		}
	}
}

void setup_usbnet(void) {
	// Load modules
	mkpath("/modules", 0755);
	REAP("/sbin/losetup", "/dev/loop0", "/opt/modules.sqsh");
	MOUNT("/dev/loop0", "/modules", "squashfs", 0, "");

	if(!(usbnet_device_address && *usbnet_device_address)) {
		usbnet_device_address = '\0';
	}
	if(!(usbnet_host_address && *usbnet_host_address)) {
		usbnet_host_address = '\0';
	}
	int options_size = 54;
	char options[options_size];
	snprintf(options, options_size, "dev_addr=%s host_addr=%s", usbnet_device_address, usbnet_host_address);

	if(MATCH(device, "n705") || MATCH(device, "n905b") || MATCH(device, "n905c") || MATCH(device, "n613")) {
		load_module("/modules/arcotg_udc.ko", options);
	}
	if(MATCH(device, "n705") || MATCH(device, "n905b") || MATCH(device, "n905c") || MATCH(device, "n613") || MATCH(device, "n236") || MATCH(device, "n437")) {
		load_module("/modules/g_ether.ko", options);
	}
	else if(MATCH(device, "n306") || MATCH(device, "n873") || MATCH(device, "bpi")) {
		load_module("/modules/fs/configfs/configfs.ko", "");
		load_module("/modules/drivers/usb/gadget/libcomposite.ko", "");
		load_module("/modules/drivers/usb/gadget/function/u_ether.ko", "");
		load_module("/modules/drivers/usb/gadget/function/usb_f_ecm.ko", "");
		if(FILE_EXISTS("/modules/drivers/usb/gadget/function/usb_f_eem.ko")) {
			load_module("/modules/drivers/usb/gadget/function/usb_f_eem.ko", "");
		}
		load_module("/modules/drivers/usb/gadget/function/usb_f_ecm_subset.ko", "");
		load_module("/modules/drivers/usb/gadget/function/usb_f_rndis.ko", "");
		load_module("/modules/drivers/usb/gadget/legacy/g_ether.ko", options);
	}
	else if(MATCH(device, "kt")) {
		load_module("/modules/2.6.35-inkbox/kernel/drivers/usb/gadget/arcotg_udc.ko", "");
		load_module("/modules/2.6.35-inkbox/kernel/drivers/usb/gadget/g_ether.ko", options);
	}
	else if(MATCH(device, "emu")) {
		;
	}
	else {
		load_module("/modules/g_ether.ko", options);
	}

	// Unmount modules
	umount("/modules");
	REAP("/sbin/losetup", "-d", "/dev/loop0");

	// Setting up network interface
	set_if_up("usb0");
	// Checking for custom IP address
	if(usbnet_ip && *usbnet_ip) {
		if(set_if_ip_address("usb0", usbnet_ip) != 0) {
			set_if_ip_address("usb0", "192.168.2.2");
		}
	}
	else {
		set_if_ip_address("usb0", "192.168.2.2");
	}
}

void setup_shell(void) {
	// Starting getty in init ramdisk root
	REAP("/sbin/getty", "-L", tty, "115200", "linux");

	while (true) {
		sleep(-1U);
	}
}

void read_sector(char * buffer, size_t len, const char * device_node, off_t sector, size_t sector_size) {
	int fd = open(device_node, O_RDONLY);
	sector = sector * (off_t) sector_size;
	lseek(fd, sector, SEEK_SET);
	read(fd, buffer, len);
	close(fd);
}

void show_alert_splash(int error_code, bool flag) {
	// Converting error code to char
	char code[10] = { 0 };
	sprintf(code, "%d", error_code);

	// Showing alert splash
	if(flag) {
		REAP("/etc/init.d/inkbox-splash", "alert_splash", code, "flag");
	}
	else {
		REAP("/etc/init.d/inkbox-splash", "alert_splash", code);
	}
}

void set_progress(int progress_value) {
	// Converting progress value to char
	char progress[5] = { 0 };
	sprintf(progress, "%d\n", progress_value);

	// Sending progress value to named pipe
	write_file(PROGRESS_BAR_FIFO_PATH, progress);
}

void progress_sleep(void) {
	usleep(500);
}

void kill_process(const char * name) {
	// If you think this is lazy work, then go there: http://pkgs-inkbox.duckdns.org:25560/misc/kill_process_nonsense.txt
	REAP("/usr/bin/pkill", "-9", name);
}

void mount_essential_filesystems(void) {
	// proc
	MOUNT("/proc", "/mnt/proc", "", MS_BIND | MS_REC, "");
	// sys
	MOUNT("/sys", "/mnt/sys", "", MS_BIND | MS_REC, "");
	// dev
	MOUNT("/dev", "/mnt/dev", "", MS_BIND | MS_REC, "");
	// tmp
	MOUNT("tmpfs", "/mnt/tmp", "tmpfs", 0, "size=16M");
	// log
	MOUNT("tmpfs", "/mnt/var/log", "tmpfs", 0, "size=8M");
}

void mount_squashfs_archives(void) {
	// Userspace 'root' flag
	if(root) {
		REAP("/sbin/losetup", "/dev/loop7", "/opt/root.sqsh");
		MOUNT("/dev/loop7", "/mnt/opt/root", "squashfs", MS_NODEV | MS_NOSUID | MS_NOEXEC, "");
	}
	// Public key
	REAP("/sbin/losetup", "/dev/loop6", "/opt/key.sqsh");
	MOUNT("/dev/loop6", "/mnt/opt/key", "squashfs", MS_NODEV | MS_NOSUID | MS_NOEXEC, "");
	// Modules
	REAP("/sbin/losetup", "/dev/loop5", "/opt/modules.sqsh");
	MOUNT("/dev/loop5", "/mnt/lib/modules", "squashfs", MS_NODEV | MS_NOSUID | MS_NOEXEC, "");
}
