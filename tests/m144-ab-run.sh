#!/bin/sh
set -eu
dir=/tmp/m144
/usr/bin/ab -n 4 -c 1 -s 8 http://10.0.2.2:15441/ab > "$dir/ab.log" 2>&1 || {
  /bin/busybox cat "$dir/ab.log"; exit 1;
}
/bin/busybox awk '$1=="Complete" && $2=="requests:" && $3==4 {ok=1} END{exit !ok}' "$dir/ab.log"
/bin/busybox awk '$1=="Failed" && $2=="requests:" && $3==0 {ok=1} END{exit !ok}' "$dir/ab.log"
echo M144_SIGNED_AB_PASS
