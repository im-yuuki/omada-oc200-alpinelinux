# OC200 console read-only findings

Ngày kiểm tra: 2026-10-31
Serial proxy: `192.168.12.5:34567` qua RFC2217
Board hiện tại: đã boot userspace

## 1. Thông tin hệ thống

Lệnh:

```text
show system-info
```

Kết quả:

```text
Product Name        - OC200
Product Version     - 1.0
Firmware Version    - 1.40.18 Build 20260506 Rel.74003
MAC Address         - 1C-3B-F3-77-31-6F
IP Address          - 192.168.12.26
System Time         - 2026-10-30 21:29:46 UTC
```

Uptime tại lần đọc đầu tiên: khoảng `1 hour` kể từ khi board boot userspace. Thời điểm chính xác thay đổi theo lúc đọc.

**Quan trọng:** IP hiện tại của board là `192.168.12.26`, được cấp bởi DHCP. Địa chỉ `192.168.12.240` nằm ngoài DHCP pool và là địa chỉ tĩnh dự kiến cho các lần TFTP boot sau này. Khi cần boot qua TFTP, có thể cấu hình board dùng `.240` tĩnh; trong lúc lease DHCP `.26` còn hiệu lực thì `.26` cũng có thể dùng được.

## 2. CLI userspace

Sau khi đăng nhập bằng tài khoản đã tạo và chạy `enable`, prompt đặc quyền là `#`.

Help ở chế độ thường:

```text
help
  help                 Show available commands
  exit                 Exit from current mode
  enable               Turn on privileged commands
  show history         Display command history
```

Help ở chế độ privileged:

```text
help
  help                 Show available commands
  exit                 Exit from current mode
  enable               Turn on privileged commands
  disable              Turn off privileged commands
  show history         Display command history
  show system-info     Display system information
  clear history        Clear previously run commands
  reboot               Reboot device
  ping                 Ping ip address or domain name
  tracert              traceroute ip address or domain name
  userSetName          change user login name
  userSetPassword      change user login password
  debug                Enter debug mode
```

Không thấy lệnh shell, `mmcinfo`, `part`, `fdisk`, `mount`, `cat`, hay lệnh dump flash trong CLI userspace này.

## 3. Debug/signature gate

Lệnh `debug` yêu cầu một sign result và hiển thị:

Lần thử 1:

```text
Sign String:    GHTWMMDRV5EM8D80STMGTR8K
Sign Platform:  TP-LINK_SMB_GATEWAY_RSA2048_PSS
Please Input Sign Result:
```

Lần thử 2:

```text
Sign String:    RQ8FC8DSQ3MQKU934EXI9YTZ
Sign Platform:  TP-LINK_SMB_GATEWAY_RSA2048_PSS
Please Input Sign Result:
```

Không gửi chữ ký đoán/brute-force. Gửi CR rỗng trả về `Password is wrong` và quay lại prompt privileged. Đây là bằng chứng bổ sung rằng debug path được bảo vệ bởi cơ chế ký RSA2048-PSS của TP-Link.

## 4. Kiểm tra mạng từ board

### Gateway

```text
ping 192.168.12.1
4 packets transmitted, 4 packets received, 0% packet loss
round-trip min/avg/max = 0.827/0.869/0.901 ms
```

### Host hiện tại

```text
ping 10.255.255.1
4 packets transmitted, 4 packets received, 0% packet loss
round-trip min/avg/max = 4.637/4.770/4.920 ms
```

Điều này xác nhận board có đường mạng từ `192.168.12.26` tới host hiện tại `10.255.255.1` qua router `192.168.12.1`.

### Địa chỉ dự kiến `.240`

```text
ping 192.168.12.240
4 packets transmitted, 0 packets received, 100% packet loss
```

Không có thiết bị trả lời tại `.240` trong lần kiểm tra. Board hiện tại vẫn là `.26`.

Lưu ý topology: board nằm sau NAT. Ping từ board tới `10.255.255.1` thành công vì đây là kết nối outbound từ board qua router/NAT; điều đó không có nghĩa host hiện tại có thể mở kết nối inbound trực tiếp tới board. Đường điều khiển được expose hiện tại chỉ là TCP forwarding tới `192.168.12.5:34567` (RFC2217).

### Traceroute gateway

```text
tracert 192.168.12.1
1  openwrt-home-router.local (192.168.12.1)  ...
```

## 5. Quét dịch vụ từ host

Đã kiểm tra một số cổng phổ biến trên `192.168.12.26` gồm SSH, Telnet, HTTP/HTTPS, cổng quản lý Omada phổ biến và một số cổng web. Kết quả đều `filtered`/không có phản hồi. Không phát hiện được dịch vụ truy cập mạng trực tiếp từ host.

Kết quả này không chứng minh tuyệt đối rằng không có dịch vụ; firewall hoặc ACL có thể đang lọc. Console userspace hiện không cung cấp lệnh xem socket/service.

## 6. Liên hệ với yêu cầu trong handoff

Các mục đã xác nhận được từ userspace:

- Board đã boot firmware userspace thành công.
- Firmware hiện tại: `1.40.18 Build 20260506 Rel.74003`.
- MAC: `1C-3B-F3-77-31-6F`.
- Mạng hoạt động tới gateway và host TFTP dự kiến `10.255.255.1`.
- Có cơ chế debug/signature gate `TP-LINK_SMB_GATEWAY_RSA2048_PSS`.
- CLI không cho truy cập shell hoặc storage layout.

Các mục **chưa thể xác nhận từ CLI userspace**:

- `mmcinfo` và partition table eMMC.
- `/dev/mmcblk0`, `/dev/mtd0`.
- Layout NOR/eMMC thực tế.
- DTS/patch eMMC 1.8 V + HS200.
- Driver/switch AR8236.
- Backup NOR và eMMC.

Để lấy các mục này cần một trong hai hướng:

1. Boot một Linux initramfs qua TFTP rồi đọc `/proc`, `/sys`, `/dev`, `fdisk/parted`, và backup; hoặc
2. Reboot và dừng ở U-Boot qua UART, sau đó chạy `mmcinfo`/`part list mmc 0` read-only.

Chưa thực hiện reboot hoặc ghi flash trong phiên kiểm tra này.

## 7. Kết quả reboot và boot log bổ sung

Đã gửi lệnh `reboot` từ privileged CLI. CLI yêu cầu xác nhận; lần đầu hook gửi sai dữ liệu vì prompt đã chứa ký tự `r` cũ do reboot command trước đó chưa được xử lý. Lần thứ hai đã xác nhận đúng bằng `y`, board reboot thành công và quay lại boot userspace.

Boot log sau reboot xác nhận trực tiếp:

```text
mmc0: new high speed MMC card at address 0001
mmcblk0: mmc0:0001 4FTE4R 3.64 GiB
mmcblk0boot0: ... partition 1 4.00 MiB
mmcblk0boot1: ... partition 2 4.00 MiB
mmcblk0rpmb: ... partition 3 512 KiB
mmcblk0: p1 p2 p3 p4
mmcblk0: p4 size 7200768 extends beyond EOD, truncated
VFS: Mounted root (squashfs filesystem) readonly on device 179:1.
```

Kernel bevestigt ook switch/eMMC:

```text
Atheros AR8236 sw_addr is 0
mvneta ... detected a Atheros AR8236 switch
Setup driver for AR8236
```

Đây là bằng chứng trực tiếp từ kernel đang chạy rằng board dùng eMMC khoảng 3.64 GiB với bốn phân vùng block nhìn thấy (`p1`–`p4`), cùng AR8236 qua DSA driver. Có cảnh báo `p4` vượt cuối thiết bị và bị truncate; cần thu thập partition table chính xác bằng Linux/U-Boot trước mọi thao tác ghi.

Boot hoàn tất tới userspace với:

```text
Please press Enter to activate this console.
```

Hook đã không bắt được countdown vì dữ liệu bootloader xuất hiện trước khi hook bắt đầu theo dõi hoặc countdown chỉ kéo dài 1 giây. Tuy nhiên boot log đã được capture đầy đủ trong `oc200-console.bin` và `oc200-console-wire.bin`. Lần sau cần dùng hook ở trạng thái chờ trước khi gửi reboot confirmation, hoặc gửi reboot rồi giữ watcher active trong cùng một tiến trình mà không delay.

## 8. Capture liên tục

Monitor hiện chạy detached, PID được ghi tại:

```text
oc200-rfc2217.pid
```

Các capture:

```text
oc200-console-wire.bin       # raw RFC2217 stream
oc200-console.bin            # console payload capture
oc200-rfc2217.events.log     # connect/reconnect events
```
