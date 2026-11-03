#!/bin/sh
# OC200 WebUI flash recovery — shared library for the CGI scripts.
# Streaming eMMC user-area writer for the RAM recovery environment.
# Never touches /dev/mmcblk0boot0|boot1, SPI NOR, or RPMB.

DEVICE=${T_DEVICE:-/dev/mmcblk0}
CMDLINE_FILE=${T_CMDLINE_FILE:-/proc/cmdline}
MOUNTS_FILE=${T_MOUNTS_FILE:-/proc/mounts}
CID_FILE=${T_CID_FILE:-/sys/block/mmcblk0/device/cid}

# Beta 1 layout (512-byte sectors), from beta1-build-manifest.json.
# NOTE: decimal everywhere — busybox ash arithmetic does not parse 0x hex.
DTB_START=896        # 0x380
DTB_MAX=32           # 0x20
KERNEL_START=1024    # 0x400
KERNEL_END=131072    # 0x20000
P1_START=131072      # 0x20000
P1_END=262144        # 0x40000
P2_START=262144      # 0x40000
DEVICE_SECTORS=7631744

get_param() { # $1:key -> value ($QUERY_STRING url-decoded leaves alnum values intact)
	printf '%s\n' "$QUERY_STRING" | tr '&' '\n' | while IFS='=' read -r k v; do
		if [ "$k" = "$1" ]; then printf '%s\n' "$v"; fi
	done | tail -n 1
}

check_token() {
	_TOK=$(get_param t)
	if [ -z "$_TOK" ]; then
		printf '{"ok":0,"err":"missing token"}\n'; exit 1
	fi
	_TOKEN_FILE=${T_TOKEN_FILE:-/run/webui-token}
	if [ ! -r "$_TOKEN_FILE" ] || [ "$(cat "$_TOKEN_FILE")" != "$_TOK" ]; then
		printf '{"ok":0,"err":"bad token"}\n'; exit 1
	fi
}

check_device() {
	if [ ! -b "$DEVICE" ] && [ ! -f "$DEVICE" ]; then
		printf '{"ok":0,"err":"eMMC device missing"}\n'; exit 1
	fi
}

check_ram_recovery() {
	case "$(cat $CMDLINE_FILE 2>/dev/null)" in
		*rdinit=*) : ;;
		*) printf '{"ok":0,"err":"not RAM recovery boot"}\n'; exit 1 ;;
	esac
}

check_parts_unmounted() {
	if grep -q 'mmcblk0p[0-9]' $MOUNTS_FILE 2>/dev/null; then
		printf '{"ok":0,"err":"eMMC partitions mounted; refusing"}\n'; exit 1
	fi
}

check_cid() {
	if [ -s "${T_WEBUI_CID:-/etc/webui-cid}" ]; then
		_WANT=$(cat "${T_WEBUI_CID:-/etc/webui-cid}")
		_CID=$(cat $CID_FILE 2>/dev/null)
		if [ "$_CID" != "$_WANT" ]; then
			printf '{"ok":0,"err":"CID mismatch: got %s"}\n' "${_CID:-none}"; exit 1
		fi
	fi
}

region_bounds() { # $1:part $2:lba(optional for raw) -> sets R_START R_MAX bytes; $3: size
	case "$1" in
	mbr)    R_START=$((0)); R_MAX=512 ;;
	dtb)    R_START=$((DTB_START*512)); R_MAX=$((DTB_MAX*512)) ;;
	kernel) R_START=$((KERNEL_START*512)); R_MAX=$(((KERNEL_END-KERNEL_START)*512)) ;;
	p1)     R_START=$((P1_START*512)); R_MAX=$(((P1_END-P1_START)*512)) ;;
	p2)     R_START=$((P2_START*512)); R_MAX=$(((DEVICE_SECTORS-P2_START)*512)) ;;
	raw)
		case "$2" in ''|*[!0-9]*) printf '{"ok":0,"err":"bad raw lba"}\n'; exit 1 ;; esac
		[ "$2" -lt "$DEVICE_SECTORS" ] 2>/dev/null || {
			printf '{"ok":0,"err":"raw lba out of device"}\n'; exit 1
		}
		R_START=$(( $2 * 512 ))
		R_MAX=$(((DEVICE_SECTORS - $2) * 512)) ;;
	*) printf '{"ok":0,"err":"unknown part"}\n'; exit 1 ;;
	esac
}
