#!/bin/sh
# One unmodified Alpine PID1/OpenRC controls four simultaneous services:
# lighttpd, dnsmasq, mosquitto and memcached. The independent HOST must
# challenge DNS (UDP+TCP), retained MQTT and volatile cache on both boots.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
export PATH
busy=/bin/busybox
fail() { echo "M239_GUEST_FAIL $*" >&2; exit 1; }
case "${1:-}" in --first) phase=FIRST ;; --reopen) phase=REOPEN ;; *) fail phase ;; esac

for package in dnsmasq dnsmasq-openrc mosquitto mosquitto-clients \
        mosquitto-openrc memcached memcached-openrc; do
    apk info -e "$package" >/dev/null 2>&1 || fail "missing signed $package"
done
for program in /usr/sbin/dnsmasq /usr/sbin/mosquitto \
        /usr/bin/mosquitto_pub /usr/bin/mosquitto_sub /usr/bin/memcached; do
    test -x "$program" || fail "missing original $program"
    magic="$($busy od -An -tx1 -N4 "$program" | $busy tr -d ' \t\r\n')"
    test "$magic" = 7f454c46 || fail "not original ELF: $program"
done
for service in dnsmasq mosquitto memcached; do
    test -x "/etc/init.d/$service" || fail "missing original OpenRC $service"
done

if [ "$phase" = FIRST ]; then
    /bin/sh /root/m227-alpine-openrc-workload.sh --first || fail 'M227 first boot'
    for service in dnsmasq mosquitto memcached; do
        test ! -e "/etc/runlevels/default/$service" || fail "preenabled $service"
    done
    test ! -e /var/lib/mosquitto/m231.db || fail 'MQTT database preseeded'
    test ! -e /etc/mosquitto/m231.conf || fail 'MQTT config preseeded'

    # Disposable test-only root/loopback services. No production DAC or
    # authentication claim; external hostfwd binds only to 127.0.0.1.
    $busy cat > /etc/dnsmasq.conf <<'DNS'
port=15453
listen-address=0.0.0.0
bind-interfaces
no-resolv
no-hosts
no-poll
log-facility=-
no-dhcp-interface=eth0
user=root
group=root
local=/m233.naseed.test/
address=/m233.naseed.test/198.51.100.233
DNS
    $busy chmod 0644 /etc/dnsmasq.conf

    $busy mkdir -p /etc/mosquitto /var/lib/mosquitto
    $busy chmod 0700 /var/lib/mosquitto
    $busy cat > /etc/mosquitto/m231.conf <<'MQTT'
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
MQTT
    $busy chmod 0644 /etc/mosquitto/m231.conf
    $busy printf '%s\n' 'cfgfile="/etc/mosquitto/m231.conf"' >> /etc/conf.d/mosquitto

    $busy cat >> /etc/conf.d/memcached <<'CACHE'
MEMCACHED_RUNAS="root"
LISTENON="0.0.0.0"
PORT="15448"
UDPPORT="0"
MEMUSAGE="8"
MAXCONN="64"
MISC_OPTS="-t 1"
CACHE

    for service in dnsmasq mosquitto memcached; do
        rc-update add "$service" default || fail "rc-update $service"
        test -L "/etc/runlevels/default/$service" || fail "OpenRC link $service"
        rc-service "$service" start || fail "OpenRC start $service"
    done
else
    # The second QEMU shares only the original physical ext4 disk. Do not
    # manually start a daemon: the real default runlevel must revive all.
    /bin/sh /root/m227-alpine-openrc-workload.sh --reopen || fail 'M227 second boot'
    test -s /var/lib/mosquitto/m231.db || fail 'retained MQTT DB lost'
fi

for service in lighttpd dnsmasq mosquitto memcached; do
    test -L "/etc/runlevels/default/$service" || fail "lost runlevel $service"
    rc-service "$service" status || fail "service $service not live"
done
$busy grep -qx 'address=/m233.naseed.test/198.51.100.233' \
    /etc/dnsmasq.conf || fail 'DNS configuration not durable'
$busy grep -qx 'persistence true' /etc/mosquitto/m231.conf ||
    fail 'MQTT persistence disabled'
$busy grep -qx 'PORT="15448"' /etc/conf.d/memcached ||
    fail 'memcached wrong port'

ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if /usr/bin/mosquitto_pub -h 127.0.0.1 -p 15447 \
            -t m231/liveness -m ready >/dev/null 2>&1; then
        ready=1
        break
    fi
    $busy sleep 1
done
test "$ready" = 1 || fail 'MQTT broker not listening'

# Each host observer waits for its own marker in the live UART, then issues
# fresh external protocol challenges over the SAME real VirtIO-Net adapter.
echo "M233_${phase}_HOST_DNS_READY"
echo "M231_${phase}_HOST_MQTT_READY"
echo "M232_${phase}_HOST_CACHE_READY"
echo "M239_${phase}_FOUR_OPENRC_SERVICES_READY"
$busy sleep 78
for service in lighttpd dnsmasq mosquitto memcached; do
    rc-service "$service" status || fail "service $service died under concurrent load"
done
# A normal broker stop flushes its retained database, preventing a simulated
# power cut in the middle of .db.new rename/unlink from corrupting ext4.
rc-service mosquitto stop || fail 'MQTT graceful stop'
test -s /var/lib/mosquitto/m231.db || fail 'MQTT DB missing after stop'
$busy sync || fail 'physical disk sync'
echo "M231_${phase}_HOST_MQTT_WINDOW_COMPLETE"
echo "M232_${phase}_HOST_CACHE_WINDOW_COMPLETE"
echo "M233_${phase}_HOST_DNS_WINDOW_COMPLETE"
echo "M239_${phase}_FOUR_OPENRC_SERVICES_COMPLETE"
