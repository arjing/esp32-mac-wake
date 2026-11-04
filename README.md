# ESP32 Mac 远程唤醒 / ESP32 Mac Wake

[中文](#中文) | [English](#english)

## 中文

通过 MQTT 接收远程命令，用 ESP32 模拟蓝牙鼠标点击，尝试唤醒已配对、处于睡眠状态的 Mac，适合网络唤醒（WoL）无法满足需求的场景。它不能从关机状态开机，也不提供远程桌面功能。唤醒效果取决于 Mac 型号、macOS 版本、睡眠状态和蓝牙连接。

### 硬件支持

- **目前已测试：ESP32-S3-DevKitC-1，8 MB Flash。** 本项目的 PlatformIO 配置针对这块开发板。
- 其他同时支持 Wi-Fi 和蓝牙低功耗（BLE）的 ESP32 型号理论上可以适配，但**尚未测试，不保证开箱即用**；可能需要调整开发板、Flash、分区以及 BOOT 按键引脚。Wi-Fi 和 BLE 同时运行的兼容性也需验证。
- 设备需要持续供电，并连接 2.4 GHz Wi-Fi。Mac 需要开启蓝牙并与设备配对。

### 编译与烧录

安装 PlatformIO 后，在项目目录执行：

```sh
pio run
pio run -t upload
```

也可以在 VS Code 的 PlatformIO 中选择 Build 和 Upload。串口日志使用 115200 波特率，可用 `pio device monitor -b 115200` 查看。

### 首次配置与配对

1. 设备首次启动后，在手机上连接 `ESP32-Setup-XXXX` 热点（后缀因设备而异）。
2. 在浏览器打开 `http://192.168.8.1`，扫描并选择 Wi-Fi，也可手动输入名称。
3. 填写 Wi-Fi 密码、蓝牙设备名称（默认 `ESPblueMouse`）和 MQTT 服务器、端口、用户名及密码。
4. 选择连接方式：普通 MQTT 默认端口 1883；TLS 默认 8883，端口可修改。验证 CA 证书时需粘贴服务器 CA 的完整 PEM 文本；不验证证书的 TLS 无法验证服务器身份。
5. 保存后设备重启并连接网络。在 Mac 的蓝牙设置中找到配置的名称并配对。

设备正常运行时，长按开发板的 **BOOT 键约 3 秒**可开启配置热点。配置页面会加载上次保存的信息；密码默认隐藏，可按按钮显示或修改。首次无 Wi-Fi 配置时也会自动开启热点。已保存配置时，Wi-Fi 连接失败会持续重试，断线后立即重试，随后约每 30 秒重试一次。

### 如何唤醒

在 MQTT 客户端连接同一个服务器，向主题 **`wol`** 发送仅包含一个字符的消息 **`1`**。例如，使用 MQTTX 等客户端时，主题填 `wol`，消息填 `1`，不要加引号或其他字符。将 Mac 设为睡眠后测试，并保持 ESP32 供电及联网。

设备收到有效指令后尝试通过蓝牙鼠标发送左键点击；点击事件发出不等于已经确认 Mac 成功唤醒。Mac 的解锁和远程桌面需要自行配置。

### 如何查看 MQTT 连接和心跳

订阅主题 **`esp32-heart`**，设备连接 MQTT 后会发布 `device online; subscription requested`，之后约每 **15 秒**发布一次：

- `heartbeat: BLE connected`：设备报告蓝牙鼠标已连接。
- `heartbeat: BLE disconnected`：设备报告蓝牙未连接；检查 Mac 蓝牙、配对和距离。
- `wake received; waiting for BLE connection`：已收到唤醒指令，等待蓝牙准备。
- `wake event sent via BLE mouse`：已发送蓝牙点击，不代表 Mac 已确认唤醒。
- `wake failed: BLE connection timeout`：等待蓝牙连接超时（约 60 秒）。
- `device offline`：MQTT 遗嘱消息，在服务器检测到异常断线后可能出现，时间由服务器和网络状态决定。

这些消息不是保留消息；订阅晚于上线消息时可能看不到上线通知，请等待下一次心跳。收不到心跳时，确认客户端和设备使用同一服务器、订阅主题正确且账号具有发布/订阅权限，再查看串口中 Wi-Fi、时间同步、TLS 和 MQTT 的错误。TLS 证书验证需要正确的系统时间，设备联网后通过 NTP 同步；MQTT 未连接时约每 10 秒重试。客户端 ID 由设备 MAC 生成。

### 安全与限制

配置热点目前无密码，网页也没有身份验证，连接热点的人可以读取已保存的密码和证书；仅在可信环境开启配置。MQTT 的 `wol` 和 `esp32-heart` 是固定主题，多台设备共用时可能一起响应，心跳也无法直接区分设备。请使用可信 MQTT 服务、独立凭据和访问控制。不要将 Wi-Fi 或 MQTT 密码提交到仓库。配置信息存于设备闪存。

## English

This project receives MQTT commands and emulates a Bluetooth mouse click on an ESP32 to try to wake a paired sleeping Mac, without relying on Wake-on-LAN. It cannot power on a shut-down Mac and does not provide remote desktop access. Results depend on the Mac model, macOS version, sleep state, and Bluetooth connectivity.

### Hardware support

- **Tested so far: ESP32-S3-DevKitC-1 with 8 MB flash.** The supplied PlatformIO environment targets this board.
- Other ESP32 models with both Wi-Fi and Bluetooth Low Energy (BLE) may be adaptable, but are **untested and not guaranteed to work as-is**. Board configuration, flash size, partitions, BOOT pin, and simultaneous Wi-Fi/BLE operation may require changes and testing.
- Keep the board powered and connected to 2.4 GHz Wi-Fi. Enable Bluetooth on the Mac and pair the device.

### Build and flash

Install PlatformIO, then run from this directory:

```sh
pio run
pio run -t upload
```

Alternatively use PlatformIO in VS Code. Serial logs use 115200 baud: `pio device monitor -b 115200`.

### Initial setup and pairing

1. On first boot, connect your phone to the `ESP32-Setup-XXXX` hotspot (the suffix varies by device).
2. Open `http://192.168.8.1` in a browser. Scan for Wi-Fi or enter the SSID manually.
3. Enter the Wi-Fi password, Bluetooth name (default `ESPblueMouse`), and MQTT host, port, username, and password.
4. Choose a connection mode. Plain MQTT defaults to port 1883 and TLS to 8883; both can be changed. For verified TLS, paste the complete server CA certificate in PEM format. TLS without certificate verification does not authenticate the server.
5. Save and allow the device to reboot. Pair the configured Bluetooth device in macOS settings.

While running, hold the board's **BOOT button for about 3 seconds** to reopen the configuration hotspot. The page loads saved settings; passwords are hidden by default and can be revealed or edited. The hotspot also starts automatically when no Wi-Fi configuration exists. With saved settings, Wi-Fi retries after failure: immediately on disconnection and approximately every 30 seconds thereafter.

### Wake the Mac

Connect an MQTT client to the same broker and publish the single character **`1`** to topic **`wol`**. For example, in MQTTX, enter `wol` as the topic and `1` as the payload, without quotes or extra characters. Put the Mac to sleep and keep the ESP32 powered and online.

After receiving a valid command, the device attempts a Bluetooth left click. Sending the click does not confirm that the Mac actually woke. Configure unlocking and remote desktop separately.

### Check MQTT and heartbeat

Subscribe to **`esp32-heart`**. After connecting, the device publishes `device online; subscription requested`, then sends a heartbeat approximately every **15 seconds**:

- `heartbeat: BLE connected` — the Bluetooth mouse reports a connection.
- `heartbeat: BLE disconnected` — Bluetooth is disconnected; check pairing, Bluetooth settings, and range.
- `wake received; waiting for BLE connection` — wake command received; waiting for Bluetooth.
- `wake event sent via BLE mouse` — click sent; this does not confirm the Mac woke.
- `wake failed: BLE connection timeout` — Bluetooth did not connect within about 60 seconds.
- `device offline` — MQTT last-will message; the broker may publish it after detecting an unexpected disconnection, with timing dependent on the broker and network.

Messages are not retained, so a subscriber joining after startup may miss the online message; wait for the next heartbeat. If no heartbeat arrives, check the broker, topic, and publish/subscribe permissions, then inspect serial logs for Wi-Fi, time synchronization, TLS, and MQTT errors. Verified TLS requires a valid clock; the device syncs time via NTP after connecting. MQTT reconnects are attempted approximately every 10 seconds. The MQTT client ID is derived from the device MAC address.

### Security and limitations

The setup hotspot currently has no password and its page has no authentication; anyone connected can read saved passwords and certificates. Enable it only in a trusted environment. The `wol` and `esp32-heart` topics are fixed: multiple devices using them may all react, and heartbeats do not identify individual devices. Use a trusted broker, separate credentials, and access controls. Never commit Wi-Fi or MQTT passwords. Settings are stored in device flash.

## License / 许可证

本项目代码采用 MIT 许可证；蓝牙鼠标依赖的授权请单独确认。

This project's code is under the MIT license; see [LICENSE](LICENSE). The external [ESP32-BLE-Mouse](https://github.com/T-vK/ESP32-BLE-Mouse) dependency has no clearly identified license in its upstream repository; confirm its terms separately before redistribution or commercial use.
