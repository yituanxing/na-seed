#!/bin/sh
# Original signed Alpine Mosquitto/OpenRC over a single real QEMU NIC.
# Host alone publishes the unpredictable retained message; reboot must reload it.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
export PATH
busy=/bin/busybox
cfg=/etc/mosquitto/m231.conf
database=/var/lib/mosquitto/m231.db
fail() { echo "M231_GUEST_FAIL $*" >&2; exit 1; }
case "${1:-}" in --first) phase=FIRST ;; --reopen) phase=REOPEN ;; *) fail phase ;; esac
for package in mosquitto mosquitto-clients mosquitto-openrc; do
    apk info -e "$package" >/dev/null 2>&1 || fail "missing signed $package"
done
for program in /usr/sbin/mosquitto /usr/bin/mosquitto_pub /usr/bin/mosquitto_sub; do
    test -x "$program" || fail "missing original $program"
    magic="$($busy od -An -tx1 -N4 "$program" | $busy tr -d ' \t\r\n')"
    test "$magic" = 7f454c46 || fail "not original ELF: $program"
done
test -x /etc/init.d/mosquitto || fail 'original OpenRC service absent'
case "$phase" in
    FIRST)
        /bin/sh /root/m227-alpine-openrc-workload.sh --first || fail 'M227 first boot'
        test ! -e "$cfg" && test ! -e "$database" || fail 'preseeded broker database'
        test ! -e /etc/runlevels/default/mosquitto || fail 'preenabled OpenRC'
        $busy mkdir -p /etc/mosquitto /var/lib/mosquitto
        $busy chmod 0700 /var/lib/mosquitto
        # Root-only test until NaSeed implements real non-root DAC. This is
        # intentionally NOT a secure/public MQTT deployment: hostfwd is
        # strictly bound to host 127.0.0.1 and the broker has no MQTT auth.
        $busy cat > "$cfg" <<'EOF'
listener 15447 0.0.0.0
protocol mqtt
allow_anonymous true
user root
persistence true
persistence_location /var/lib/mosquitto/
persistence_file m231.db
autosave_interval 1
autosave_on_changes false
log_dest stdout
EOF
        $busy chmod 0644 "$cfg"
        $busy printf '%s\n' 'cfgfile="/etc/mosquitto/m231.conf"' >> /etc/conf.d/mosquitto
        rc-update add mosquitto default || fail 'default runlevel registration'
        test -L /etc/runlevels/default/mosquitto || fail 'missing original OpenRC link'
        rc-service mosquitto start || fail 'original OpenRC broker start'
        ;;
    REOPEN)
        /bin/sh /root/m227-alpine-openrc-workload.sh --reopen || fail 'M227 second boot'
        test -s "$database" || fail 'first retained MQTT DB not durable'
        test -L /etc/runlevels/default/mosquitto || fail 'OpenRC runlevel link lost'
        ;;
esac
test -f "$cfg" && test ! -L "$cfg" || fail 'broker configuration absent'
$busy grep -qx 'persistence true' "$cfg" || fail 'broker persistence disabled'
$busy grep -qx 'user root' "$cfg" || fail 'non-root DAC unsupported'
$busy grep -qx 'listener 15447 0.0.0.0' "$cfg" || fail 'incorrect listener'
rc-service mosquitto status || fail 'broker not managed by original OpenRC'
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if /usr/bin/mosquitto_pub -h 127.0.0.1 -p 15447 -t m231/liveness -m ready \
        >/dev/null 2>&1; then ready=1; break; fi
    $busy sleep 1
done
test "$ready" = 1 || fail 'original MQTT broker not ready'
case "$phase" in
    FIRST) echo M231_FIRST_HOST_MQTT_READY ;;
    REOPEN) echo M231_REOPEN_HOST_MQTT_READY ;;
esac
$busy sleep 55
rc-service mosquitto status || fail 'broker died during host MQTT'
test -s "$database" || fail 'retained MQTT DB missing from ext4'
case "$phase" in
    FIRST) echo M231_FIRST_HOST_MQTT_WINDOW_COMPLETE ;;
    REOPEN) echo M231_REOPEN_HOST_MQTT_WINDOW_COMPLETE ;;
esac
