# Phone side of the stale UI dev session (3a-IOS/docs/stale-ui.md). sh phone.sh STEP [TAG]. Frames and notes go to /private/var/tmp/stale.
PATH=/usr/local/bin:/var/jb/usr/bin:/var/jb/bin:/usr/bin:/bin:/usr/sbin:/sbin; export PATH
O=/private/var/tmp/stale; mkdir -p $O
L=/usr/local/lib/bgmtl; C=/private/var/db/bgmtl
log() { echo "$(date +%T) $*" | tee -a $O/steps.log; }
shot() { timeout 30 sweetshot "$O/$1.png" 3 > /dev/null 2>&1 || log "FAIL shot $1"; }
t() { timeout 15 sweettouch "$@" > /dev/null 2>&1 || log "FAIL touch $*"; }
home() { t swipe 187 808 187 480 200; sleep 1; t home; sleep 3; }
up() {          # after a backboardd restart: SpringBoard back, past the lock screen
	n=0
	until timeout 30 sweetopen com.apple.Preferences > /dev/null 2>&1; do
		n=$((n + 1)); [ $n -gt 24 ] && { log "FAIL springboard not back"; return 1; }
		t home; sleep 1; t swipe 187 808 187 300 250; sleep 5
	done
	sleep 5; home; killall -9 Preferences 2> /dev/null; sleep 1
}
# SIGTERM, a plain exit: launchd counts only crash signals. 150 s apart, so the last Metal launch was productive (600 command
# buffers, 120 s after its first) and the launcher restarts on Metal at once instead of counting a retry (bgl-launch.c, retry policy).
bb() {
	last=$(cat $O/bb.last 2> /dev/null || echo 0); now=$(date +%s); w=$((last + 150 - now)); [ $w -gt 0 ] && { log "wait $w s before the restart"; sleep $w; }
	log "restart backboardd"; killall backboardd; date +%s > $O/bb.last; sleep 20; up; log "backboardd up: $(grep 'Metal mode' /private/var/tmp/bgl-backboardd.log 2> /dev/null | tail -1 | cut -c1-220)"; }
cap() { touch /private/var/tmp/bgmtl.capnow; sleep 2; log "capture $1"; }
repro() {       # $1 tag, $2 capture or not
	killall -9 MobileNotes 2> /dev/null; sleep 1
	timeout 30 sweetopen com.apple.mobilenotes > /dev/null 2>&1; sleep 7
	shot "$1-0list"; [ "$2" = cap ] && cap "$1-list"
	t tap 75 213; sleep 3; shot "$1-1open"
	t tap 37 63; sleep 5; shot "$1-2left"; [ "$2" = cap ] && cap "$1-left"
	home; timeout 30 sweetopen com.apple.mobilenotes > /dev/null 2>&1; sleep 4; shot "$1-3reopen"
	home
}
case "$1" in
setup)
	log "setup: $(ls -la $C | tr '\n' ' ')"
	[ -e $C/shatter.capture ] && cp $C/shatter.capture $O/shatter.capture.orig
	[ -e $C/bgmtl-bb.texpool ] && log "cfg texpool: $(cat $C/bgmtl-bb.texpool)"
	cp $L/bgmtl-inject.dylib $O/bgmtl-inject.dylib.daily
	shasum -a 256 $L/bgmtl-inject.dylib | tee -a $O/steps.log
	date +%s > $O/bb.last ;;      # boot counts as the last start
base) repro A ;;
capon)          # the capture build in, "v2 0 now", stamp present before the restart so its first look only learns it
	cp $O/bgmtl-inject.dylib.cap $L/.cap.tmp && mv -f $L/.cap.tmp $L/bgmtl-inject.dylib
	echo "v2 0 now" > $C/shatter.capture; touch /private/var/tmp/bgmtl.capnow; rm -f /private/var/tmp/shatter-captures-v2
	bb; repro C cap ;;
pool0)
	if [ -e $C/bgmtl-bb.texpool ]; then cp $C/bgmtl-bb.texpool $O/texpool.orig; echo 0 > $C/bgmtl-bb.texpool; else echo 0 > /private/var/tmp/bgmtl-bb.texpool; fi
	bb; repro P cap ;;
restore)        # the daily driver and config back, as found
	cp $O/bgmtl-inject.dylib.daily $L/.d.tmp && mv -f $L/.d.tmp $L/bgmtl-inject.dylib
	if [ -e $O/shatter.capture.orig ]; then cp $O/shatter.capture.orig $C/shatter.capture; else rm -f $C/shatter.capture; fi
	if [ -e $O/texpool.orig ]; then cp $O/texpool.orig $C/bgmtl-bb.texpool; fi
	rm -f /private/var/tmp/bgmtl-bb.texpool /private/var/tmp/bgmtl.capnow
	bb; repro R
	shasum -a 256 $L/bgmtl-inject.dylib | tee -a $O/steps.log; ls $C | tee -a $O/steps.log ;;
esac
