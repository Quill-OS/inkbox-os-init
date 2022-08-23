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
	kernel_version = uname_data.version;
	// Kernel build ID
	kernel_build_id = read_file("/opt/build_id");
	// Kernel Git commit
	kernel_git_commit = read_file("/opt/commit");
}

// https://github.com/Kobo-InkBox/inkbox-power-daemon/blob/8296c4a1811e3921ff98e9980504c24d23435dac/src/functions.cpp#L415-L430
void run_command(const char * path, const char * args[], bool wait) {
	int status = -1;
	int pid = 0;

	status = posix_spawn(&pid, path, NULL, NULL, (char**)args, NULL);
	if(status != 0) {
		fprintf(stderr, "Error in spawning process %s\n", path);
	}
	if(wait == true) {
		waitpid(pid, 0, 0);
	}
}

// https://stackoverflow.com/a/230070/14164574
bool file_exists(char * file_path) {
	struct stat buffer;
	return (stat(file_path, &buffer) == 0);
}

// https://stackoverflow.com/a/3747128/14164574
char * read_file(char * file_path) {
	FILE * fp;
	long lSize;
	char * buffer;

	fp = fopen(file_path , "rb");
	if(!fp) perror("Error opening file"), exit(1);

	fseek(fp, 0L , SEEK_END);
	lSize = ftell(fp);
	rewind(fp);

	/* Allocate memory for entire content */
	buffer = calloc(1, lSize+1);
	if(!buffer) fclose(fp), fputs("Memory allocation failed", stderr), exit(1);

	/* Copy the file into the buffer */
	if(1 != fread(buffer, lSize, 1, fp)) {
		fclose(fp),free(buffer),fputs("File reading failed", stderr), exit(1);
	}

	return buffer;

	fclose(fp);
	free(buffer);
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
