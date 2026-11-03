#!/bin/sh
# CGI test harness (container-only).
set -u
RT="$(cd "$(dirname "$0")/../rootfs-overlay/www/cgi-bin" && pwd)"
T=/tmp/t
rm -rf $T; mkdir -p $T
dd if=/dev/zero of=$T/dev bs=1M count=64 status=none
printf 'rdinit=/init rootwait\n' > $T/cmdline
printf '/dev/vda /x ext4 rw 0 0\n' > $T/mounts
export T_DEVICE=$T/dev T_CMDLINE_FILE=$T/cmdline T_MOUNTS_FILE=$T/mounts

TOKEN=deadbeefcafe0102
printf '%s' "$TOKEN" > $T/token
export T_TOKEN_FILE=$T/token

sha_of() { sha256sum "$1" | awk '{print $1}'; }
jq_err() { tail -1 | sed -n 's/.*"err":"\([^"]*\)".*/\1/p'; }
last() { tail -1; }

cd $RT

echo "== t1 token wrong (expect bad token)"
head -c 512 /dev/urandom | T_TOKEN_FILE=$T/nope QUERY_STRING="t=WRONG&part=mbr&start=0&len=512&total=512&final=1" ./flash | jq_err

echo "== t2 not RAM recovery (expect not RAM recovery boot)"
head -c 10 /dev/urandom | T_CMDLINE_FILE=/dev/null QUERY_STRING="t=$TOKEN&part=mbr&start=0&len=512&total=512&final=1" ./flash | jq_err

echo "== t3 mounted partitions (expect refusing)"
printf '/dev/mmcblk0p1 /mnt f2fs rw 0 0\n' > $T/mounted
head -c 512 /dev/urandom | T_MOUNTS_FILE=$T/mounted QUERY_STRING="t=$TOKEN&part=mbr&start=0&len=512&total=512&final=1" ./flash | jq_err

P2BASE=$((262144*512))
echo "== t4 unaligned final p2 (expect len not 512-aligned)"
head -c 100 /dev/urandom | QUERY_STRING="t=$TOKEN&part=p2&start=$P2BASE&len=100&total=100&final=1" ./flash | jq_err

echo "== t5 dtb final span error (expect final span mismatch — no pad when start beyond base)"
head -c 512 /dev/urandom | QUERY_STRING="t=$TOKEN&part=dtb&start=$((896*512+12800))&len=512&total=512&final=1" CONTENT_LENGTH=512 ./flash | jq_err

echo "== t6 raw lba out of device (expect raw lba out of device)"
head -c 512 /dev/urandom | QUERY_STRING="t=$TOKEN&part=raw&lba=20000000&start=0&len=512&total=512&final=1" ./flash | jq_err

echo "== t7 CID mismatch gate (expect CID mismatch)"
mkdir -p $T/sys
printf 'AAABBBCCCDDDEEE1112223334445556\n' > $T/sys/cid
printf 'AAABBBCCCDDDEEE1112223334441111\n' > $T/webui-cid
head -c 512 /dev/urandom | T_CID_FILE=$T/sys/cid T_WEBUI_CID=$T/webui-cid QUERY_STRING="t=$TOKEN&part=mbr&start=0&len=512&total=512&final=1" ./flash | jq_err

echo "== t7b no CID gate file (pass through to next checks)"
head -c 512 /dev/urandom | T_CID_FILE=/dev/null CONTENT_LENGTH=512 QUERY_STRING="t=$TOKEN&part=mbr&start=0&len=512&total=512&final=1" ./flash | last

echo "== t8 happy mbr single chunk (final=1 exact 512)"
head -c 512 /dev/urandom > $T/mbr.bin
head -c 512 $T/mbr.bin | CONTENT_LENGTH=512 QUERY_STRING="t=$TOKEN&part=mbr&start=0&len=512&total=512&final=1" ./flash | last
echo "-- readback (expect match=1 vs file sha)"
head -c 0 /dev/null | QUERY_STRING="t=$TOKEN&part=mbr&start=0&len=512" ./verify | last
echo "-- expected: $(sha_of $T/mbr.bin)"

echo "== t9 dtb final 12,381 bytes + zero-pad"
head -c 12381 /dev/urandom > $T/dtb.bin
head -c 12381 $T/dtb.bin | CONTENT_LENGTH=12381 QUERY_STRING="t=$TOKEN&part=dtb&start=$((896*512))&len=12381&total=12381&final=1" ./flash | last
echo "-- exact-len verify (expect match=1)"
head -c 0 /dev/null | QUERY_STRING="t=$TOKEN&part=dtb&start=$((896*512))&len=12381" ./verify | last
echo "-- expected(exact): $(sha_of $T/dtb.bin)"
echo "-- padded 12800 verify (expect match=1)"
head -c 0 /dev/null | QUERY_STRING="t=$TOKEN&part=dtb&start=$((896*512))&len=12800" ./verify | last
cat $T/dtb.bin /dev/zero | head -c 12800 > $T/dtb.pad 2>/dev/null
echo "-- expected(pad): $(sha_of $T/dtb.pad)"

echo "== t10 two-chunk kernel 32768"
dd if=/dev/urandom of=$T/kern.bin bs=4096 count=8 status=none
dd if=$T/kern.bin bs=16384 skip=0 count=1 of=$T/kern.1 status=none
dd if=$T/kern.bin bs=16384 skip=1 count=1 of=$T/kern.2 status=none
head -c 16384 $T/kern.1 | CONTENT_LENGTH=16384 QUERY_STRING="t=$TOKEN&part=kernel&start=$((1024*512))&len=16384&total=32768&final=0" ./flash | last
head -c 16384 $T/kern.2 | CONTENT_LENGTH=16384 QUERY_STRING="t=$TOKEN&part=kernel&start=$((1024*512+16384))&len=16384&total=32768&final=1" ./flash | last
echo "-- readback (expect match=1)"
head -c 0 /dev/null | QUERY_STRING="t=$TOKEN&part=kernel&start=$((1024*512))&len=32768" ./verify | last
echo "-- expected: $(sha_of $T/kern.bin)"

echo "== t11 sha-param mismatch refusal (expect sha mismatch)"
head -c 512 $T/mbr.bin | CONTENT_LENGTH=512 QUERY_STRING="t=$TOKEN&part=mbr&start=0&len=512&total=512&final=1&sha=0000000000000000000000000000000000000000000000000000000000000000" ./flash | jq_err

echo DONE
