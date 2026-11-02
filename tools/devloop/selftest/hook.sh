# Unattended self-test (plans/devloop). dev.py puts this at the top of /usr/local/libexec/renegade-firstboot.sh, the one
# root job that already runs as root at every boot, so no new file and no reflash. Without the boot-arg sweetselftest=N it
# does nothing and the first-boot script below carries on. /bin/sh is dash; the tools are toybox in /usr/local/bin.
st_arg=$(/var/jb/usr/sbin/sysctl -n kern.bootargs 2>/dev/null)
case " $st_arg " in
*" sweetselftest=0 "*) st_run=0 ;;
*" sweetselftest="*) st_run=1 ;;
*) st_run=0 ;;
esac
case "$0" in *renegade-firstboot*) [ -x /usr/local/bin/sweetselftest ] && st_run=0 ;; esac     # the root patch's job runs it
if [ "$st_run" = 1 ] && [ ! -e /private/var/db/.renegade-firstboot-done ]; then
	echo "SELFTEST: SKIP first-boot job has not finished, sweetdeadman ends this boot" > /dev/console
	st_run=0
fi
if [ "$st_run" = 1 ]; then
	PATH=/usr/local/bin:/var/jb/usr/bin:/var/jb/bin:/usr/bin:/bin:/usr/sbin:/sbin
	export PATH
	ST_OUT=/private/var/selftest/frames
	st_n=0
	say() { echo "SELFTEST: $*" > /dev/console; }
	# A wedged GPU can block any of these tools. timeout keeps the script moving, sweetdeadman covers the rest.
	touch_() { timeout 15 sweettouch "$@" > /dev/null 2>&1 || say "FAIL touch $*"; }
	step() { say "step $1"; }
	# shot NAME [check]: a frame. check means the frame must differ from the one before it, or the gesture did nothing.
	shot() {
		st_n=$((st_n + 1))
		st_f="$ST_OUT/$(printf %02d $st_n)-$1${2:+.check}.png"
		timeout 30 sweetshot "$st_f" 3 > /dev/null 2>&1 || say "FAIL shot $1"
	}
	openapp() { say "open $1"; timeout 30 sweetopen "$1" > /dev/null 2>&1 || say "FAIL open $1"; }
	# 375x812 points: sweettouch divides by these, so this holds on the 420x930 screen as fractions of its size.
	go_home() { touch_ swipe 187 808 187 480 200; sleep 1; touch_ home; sleep 3; }
	finish() {
		say "DONE"
		sync
		sleep 1
		timeout 30 sweetreboot fastboot
		say "FAIL sweetreboot came back, waiting for sweetdeadman"
		sleep 100000
		exit 1
	}
	rm -rf "$ST_OUT"
	mkdir -p "$ST_OUT"
	say "start"
	n=0
	until timeout 30 sweetopen com.apple.Preferences > /dev/null 2>&1; do
		n=$((n + 1))
		if [ "$n" -gt 48 ]; then
			say "FAIL springboard not up after 240 s"
			finish
		fi
		touch_ home                          # sweetopen fails behind the lock screen (ast32): wake it and swipe up
		sleep 1
		touch_ swipe 187 808 187 300 250
		sleep 5
	done
	say "springboard up after $((n * 5)) s"
	sleep 15
	go_home
	touch_ swipe 187 808 187 300 250     # lock screen, if it is showing
	sleep 3
	go_home
	# @BODY@
	finish
fi
unset st_arg st_run
