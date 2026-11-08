#!/bin/sh
# M197 family: the one authoritative legacy NAS_Mxxx selector parser.
# Source this file, then call nas_acceptance_parse_profile before any guest
# staging or host watcher starts. The 27 historical selectors remain accepted;
# no new external profile selector or combination is introduced here.
nas_acceptance_parse_profile() {
    nas_acceptance_profile=m197

    m198=${NAS_M198_INTEGRATED_DESKTOP:-0}
    case "$m198" in 0) ;; 1) nas_acceptance_profile=m198 ;; *) echo '[M198 FAIL] invalid desktop selector' >&2; exit 2 ;; esac
    m199=${NAS_M199_ORIGINAL_OPENRC_DESKTOP:-0}
    case "$m199" in 0) ;; 1) nas_acceptance_profile=m199 ;; *) echo '[M199 FAIL] invalid original OpenRC selector' >&2; exit 2 ;; esac
    m204=${NAS_M204_COMPAT_COHORT:-0}
    case "$m204" in 0) ;; 1) nas_acceptance_profile=m204 ;; *) echo '[M204 FAIL] invalid compatibility selector' >&2; exit 2 ;; esac
    m205=${NAS_M205_COMPAT_COHORT:-0}
    case "$m205" in 0) ;; 1) nas_acceptance_profile=m205 ;; *) echo '[M205 FAIL] invalid compatibility selector' >&2; exit 2 ;; esac
    m206=${NAS_M206_COMPAT_COHORT:-0}
    case "$m206" in 0) ;; 1) nas_acceptance_profile=m206 ;; *) echo '[M206 FAIL] invalid compatibility selector' >&2; exit 2 ;; esac
    m235=${NAS_M235_COMPAT_COHORT:-0}
    case "$m235" in 0) ;; 1) nas_acceptance_profile=m235 ;; *) echo '[M235 FAIL] invalid batch selector' >&2; exit 2 ;; esac
    m236=${NAS_M236_COMPAT_COHORT:-0}
    case "$m236" in 0) ;; 1) nas_acceptance_profile=m236 ;; *) echo '[M236 FAIL] invalid batch selector' >&2; exit 2 ;; esac
    m237=${NAS_M237_COMPAT_COHORT:-0}
    case "$m237" in 0) ;; 1) nas_acceptance_profile=m237 ;; *) echo '[M237 FAIL] invalid batch selector' >&2; exit 2 ;; esac
    m238=${NAS_M238_COMPAT_COHORT:-0}
    case "$m238" in 0) ;; 1) nas_acceptance_profile=m238 ;; *) echo '[M238 FAIL] invalid batch selector' >&2; exit 2 ;; esac
    m215=${NAS_M215_PILOT:-0}
    case "$m215" in 0) ;; 1) nas_acceptance_profile=m215 ;; *) echo '[M215 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
    m218=${NAS_M218_PILOT:-0}
    case "$m218" in 0) ;; 1) nas_acceptance_profile=m218 ;; *) echo '[M218 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
    m219=${NAS_M219_PILOT:-0}
    case "$m219" in 0) ;; 1) nas_acceptance_profile=m219 ;; *) echo '[M219 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
    m220=${NAS_M220_PILOT:-0}
    case "$m220" in 0) ;; 1) nas_acceptance_profile=m220 ;; *) echo '[M220 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
    m221=${NAS_M221_PILOT:-0}
    case "$m221" in 0) ;; 1) nas_acceptance_profile=m221 ;; *) echo '[M221 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
    m222=${NAS_M222_PILOT:-0}
    case "$m222" in 0) ;; 1) nas_acceptance_profile=m222 ;; *) echo '[M222 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
    m223=${NAS_M223_PILOT:-0}
    case "$m223" in 0) ;; 1) nas_acceptance_profile=m223 ;; *) echo '[M223 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
    m224=${NAS_M224_PILOT:-0}
    case "$m224" in 0) ;; 1) nas_acceptance_profile=m224 ;; *) echo '[M224 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
    m225=${NAS_M225_PILOT:-0}
    case "$m225" in 0) ;; 1) nas_acceptance_profile=m225 ;; *) echo '[M225 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
    m226=${NAS_M226_PILOT:-0}
    case "$m226" in 0) ;; 1) nas_acceptance_profile=m226 ;; *) echo '[M226 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
    m227=${NAS_M227_INTEGRATED_OPENRC:-0}
    case "$m227" in 0) ;; 1) nas_acceptance_profile=m227 ;; *) echo '[M227 FAIL] invalid integrated OpenRC selector' >&2; exit 2 ;; esac
    m228=${NAS_M228_HOST_INBOUND:-0}
    case "$m228" in 0) ;; 1) nas_acceptance_profile=m228 ;; *) echo '[M228 FAIL] invalid host ingress selector' >&2; exit 2 ;; esac
    m229=${NAS_M229_VALKEY_HOST:-0}
    case "$m229" in 0) ;; 1) nas_acceptance_profile=m229 ;; *) echo '[M229 FAIL] invalid host RESP selector' >&2; exit 2 ;; esac
    m230=${NAS_M230_DROPBEAR_HOST:-0}
    case "$m230" in 0) ;; 1) nas_acceptance_profile=m230 ;; *) echo '[M230 FAIL] invalid host SSH selector' >&2; exit 2 ;; esac
    m231=${NAS_M231_MOSQUITTO_HOST:-0}
    case "$m231" in 0) ;; 1) nas_acceptance_profile=m231 ;; *) echo '[M231 FAIL] invalid host MQTT selector' >&2; exit 2 ;; esac
    m232=${NAS_M232_MEMCACHED_HOST:-0}
    case "$m232" in 0) ;; 1) nas_acceptance_profile=m232 ;; *) echo '[M232 FAIL] invalid host cache selector' >&2; exit 2 ;; esac
    m233=${NAS_M233_DNSMASQ_HOST:-0}
    case "$m233" in 0) ;; 1) nas_acceptance_profile=m233 ;; *) echo '[M233 FAIL] invalid host UDP+TCP DNS selector' >&2; exit 2 ;; esac
    m239=${NAS_M239_MULTISERVICE:-0}
    case "$m239" in 0) ;; 1) nas_acceptance_profile=m239 ;; *) echo '[M239 FAIL] invalid multi-service selector' >&2; exit 2 ;; esac

    if [ "$m239" = 1 ]; then
        [ "$m233" = 1 ] || {
            echo '[M239 FAIL] multi-service owner requires the M233 DNS base' >&2
            exit 2
        }
    fi
    # Desktop cannot own a software cohort, including the M239/M233 pair.
    if { [ "$m204" = 1 ] || [ "$m205" = 1 ] || [ "$m206" = 1 ] || [ "$m215" = 1 ] || [ "$m218" = 1 ] || [ "$m219" = 1 ] || [ "$m220" = 1 ] || [ "$m221" = 1 ] || [ "$m222" = 1 ] || [ "$m223" = 1 ] || [ "$m224" = 1 ] || [ "$m225" = 1 ] || [ "$m226" = 1 ] || [ "$m227" = 1 ] || [ "$m228" = 1 ] || [ "$m229" = 1 ] || [ "$m230" = 1 ] || [ "$m231" = 1 ] || [ "$m232" = 1 ] || [ "$m233" = 1 ] || [ "$m235" = 1 ] || [ "$m236" = 1 ] || [ "$m237" = 1 ] || [ "$m238" = 1 ]; } && \
       { [ "$m198" = 1 ] || [ "$m199" = 1 ]; }; then
        echo '[COMPAT FAIL] software cohort cannot enable a desktop' >&2
        exit 2
    fi
    if [ "$((m204+m205+m206+m215+m218+m219+m220+m221+m222+m223+m224+m225+m226+m227+m228+m229+m230+m231+m232+m233+m235+m236+m237+m238))" -gt 1 ]; then
        echo '[COMPAT FAIL] software cohorts must own separate ledgers' >&2
        exit 2
    fi
    if [ "$m199" = 1 ]; then
        # Original M199 has always implied the M198 hardware/kernel profile.
        m198=1
    fi
}
