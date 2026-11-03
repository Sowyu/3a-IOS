#!/bin/bash
# Stale UI dev session (3a-IOS/docs/stale-ui.md, docs/dev-session.md). Takes the phone by the protocol (lowest priority, hold file
# /tmp/phone-stale-wants), boots plans/stale/out/stale-dev (no hw scale, SSH over USB, ends itself 1800 s after boot), runs phone.sh's
# steps, always restores the daily driver and config, pulls frames and captures into plans/stale/session/pull-TIME, and stops.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd); root="$here/../../.."; cd "$root"
ts=$(date +%H%M); out="$here/pull-$ts"; mkdir -p "$out"; log="$out/host.log"
say() { echo "$(date +%T) $*" | tee -a "$log"; }
touch /tmp/phone-stale-wants
while :; do
	others=$(ls /tmp/phone-*-wants 2> /dev/null | grep -v phone-stale-wants)
	if [ -z "$others" ] && ! pgrep -f 'dev\.py' > /dev/null && { fastboot devices | grep -q fastboot || adb devices | grep -q recovery; } && mkdir /tmp/phone.lock 2> /dev/null; then break; fi
	sleep 20
done
say "phone taken"
py="plans/devloop/""dev.py"
python3 "$py" plans/stale/out/stale-dev-boot.img --test usbdev --note stale-dev --key 'ASTEROIDS USB|SWEET USB' > "$out/dev.log" 2>&1 &
dev=$!
sleep 15; /usr/bin/trash /tmp/phone.lock
O=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o LogLevel=ERROR)
host=""
for i in $(seq 1 60); do
	kill -0 $dev 2> /dev/null || { say "dev.py ended before SSH"; break; }
	for ifc in en8 $(ifconfig -l | tr ' ' '\n' | grep '^en' | sort -t n -k2 -n -r); do
		ifconfig "$ifc" 2> /dev/null | grep -q 'inet6 fe80' || continue
		if ssh "${O[@]}" "root@fe80::5eff:fe77:ee02%$ifc" true 2> /dev/null; then host="fe80::5eff:fe77:ee02%$ifc"; break 2; fi
	done
	sleep 10
done
if [ -n "$host" ]; then
	say "ssh up on $host"
	s() { ssh "${O[@]}" "root@$host" "$@"; }
	scp -6 "${O[@]}" "$here/phone.sh" "root@[$host]:/private/var/tmp/stale-phone.sh" >> "$log" 2>&1
	s 'mkdir -p /private/var/tmp/stale' && scp -6 "${O[@]}" "$here/../drv/out/bgmtl-inject.dylib" "root@[$host]:/private/var/tmp/stale/bgmtl-inject.dylib.cap" >> "$log" 2>&1
	for st in setup base capon pool0; do say "step $st"; timeout 600 ssh "${O[@]}" "root@$host" "sh /private/var/tmp/stale-phone.sh $st" >> "$log" 2>&1 || say "step $st rc $?"; done
	say "step restore"; timeout 600 ssh "${O[@]}" "root@$host" "sh /private/var/tmp/stale-phone.sh restore" >> "$log" 2>&1 || say "step restore rc $?"
	scp -6 -r "${O[@]}" "root@[$host]:/private/var/tmp/stale" "$out/" >> "$log" 2>&1
	s 'ls /private/var/tmp/shatter-* 2> /dev/null' > "$out/captures.txt"
	for f in $(cat "$out/captures.txt"); do scp -6 "${O[@]}" "root@[$host]:$f" "$out/" >> "$log" 2>&1; done
	s 'cp /private/var/tmp/bgl-backboardd.log /private/var/tmp/stale/bgl-backboardd.log; rm -f /private/var/tmp/shatter-*' ; scp -6 "${O[@]}" "root@[$host]:/private/var/tmp/stale/bgl-backboardd.log" "$out/" >> "$log" 2>&1
	s 'rm -rf /private/var/tmp/stale /private/var/tmp/stale-phone.sh; touch /private/var/tmp/usbdev.stop'
	say "stop sent"
fi
wait $dev; say "dev.py rc $?"
/usr/bin/trash /tmp/phone-stale-wants
