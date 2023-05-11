# inkbox-os-init
InkBox OS init program. Is launched by BusyBox from `/etc/inittab` reference.
## Compilation
```
armv7l-linux-musleabihf-gcc init.c -o init -static
armv7l-linux-musleabihf-strip init
```
