#!/bin/sh
# Additional, independent original Alpine services behind ONE QEMU NIC.
# M233 owns the DNS watcher and the two physical QEMU boots; this module
# owns MQTT/cache watchers and is only sourced by the explicit M239 opt-in.
m239_mqtt_pid=
m239_cache_pid=
m239_stop_host() {
    for pid in "$m239_mqtt_pid" "$m239_cache_pid"; do
        if [ -n "$pid" ]; then
            kill "$pid" 2>/dev/null || :
            wait "$pid" 2>/dev/null || :
        fi
    done
    m239_mqtt_pid=
    m239_cache_pid=
}
m239_start_host() {
    phase=$1
    host_serial=$2
    mqtt_receipt="$PWD/$work/m231-$phase-host-mqtt.json"
    cache_receipt="$PWD/$work/m232-$phase-host-cache.json"
    mqtt_log="$PWD/$work/m231-$phase-host-mqtt.log"
    cache_log="$PWD/$work/m232-$phase-host-cache.log"
    for item in "$mqtt_receipt" "$cache_receipt" "$mqtt_log" "$cache_log"; do
        test ! -e "$item" || {
            echo "[M239 HOST FAIL] stale $phase evidence: $item" >&2
            exit 1
        }
    done
    if [ "$phase" = reopen ]; then
        python3 -u scripts/m231-host-mqtt-check.py "$phase" "$host_serial" \
            15447 "$mqtt_receipt" \
            --first-receipt "$PWD/$work/m231-first-host-mqtt.json" \
            --ready-timeout "${NAS_M239_HOST_READY_SECONDS:-2750}" \
            --mqtt-timeout 45 > "$mqtt_log" 2>&1 &
        m239_mqtt_pid=$!
        python3 -u scripts/m232-host-cache-check.py "$phase" "$host_serial" \
            15448 "$cache_receipt" \
            --first-receipt "$PWD/$work/m232-first-host-cache.json" \
            --ready-timeout "${NAS_M239_HOST_READY_SECONDS:-2750}" \
            --cache-timeout 45 > "$cache_log" 2>&1 &
        m239_cache_pid=$!
    else
        python3 -u scripts/m231-host-mqtt-check.py "$phase" "$host_serial" \
            15447 "$mqtt_receipt" \
            --ready-timeout "${NAS_M239_HOST_READY_SECONDS:-2750}" \
            --mqtt-timeout 45 > "$mqtt_log" 2>&1 &
        m239_mqtt_pid=$!
        python3 -u scripts/m232-host-cache-check.py "$phase" "$host_serial" \
            15448 "$cache_receipt" \
            --ready-timeout "${NAS_M239_HOST_READY_SECONDS:-2750}" \
            --cache-timeout 45 > "$cache_log" 2>&1 &
        m239_cache_pid=$!
    fi
}
m239_finish_host() {
    phase=$1
    for service in mqtt cache; do
        case "$service" in
            mqtt) pid=$m239_mqtt_pid; receipt="$PWD/$work/m231-$phase-host-mqtt.json";
                  logfile="$PWD/$work/m231-$phase-host-mqtt.log" ;;
            cache) pid=$m239_cache_pid; receipt="$PWD/$work/m232-$phase-host-cache.json";
                   logfile="$PWD/$work/m232-$phase-host-cache.log" ;;
        esac
        test -n "$pid" || {
            echo "[M239 HOST FAIL] no $service watcher" >&2; exit 1;
        }
        join=0
        while [ ! -s "$receipt" ] && kill -0 "$pid" 2>/dev/null &&
              [ "$join" -lt 8 ]; do
            sleep 1
            join=$((join+1))
        done
        if [ ! -s "$receipt" ]; then
            echo "[M239 HOST FAIL] $phase missing real $service receipt" >&2
            tail -100 "$logfile" >&2 || :
            exit 1
        fi
        if ! wait "$pid"; then
            echo "[M239 HOST FAIL] $phase $service protocol mismatch" >&2
            tail -100 "$logfile" >&2 || :
            exit 1
        fi
        case "$service" in mqtt) m239_mqtt_pid= ;; cache) m239_cache_pid= ;; esac
    done
}
