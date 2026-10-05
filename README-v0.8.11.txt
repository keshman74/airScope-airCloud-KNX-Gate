KNX → airScope / airCloud Gateway v0.8.11

Based on hardware-confirmed v0.8.10.

Changes only in firmware update subsystem:
- Removed visible GitHub repository label from Web UI.
- Added CHECK & UPDATE ONLINE.
- Added OTA busy overlay/spinner and local upload progress.
- Added DO NOT POWER OFF warning.
- Online OTA reads latest.json from the official repository.
- Added GitHub Actions tag workflow to build firmware.bin, create a Release, and update latest.json.
- LittleFS configuration remains preserved by firmware-only OTA.

First online-update test plan:
1. Install v0.8.11 locally.
2. Push this project to main.
3. Create the next firmware version v0.8.12 and push tag v0.8.12.
4. GitHub Actions builds/releases firmware.bin and updates latest.json.
5. On v0.8.11 press CHECK & UPDATE ONLINE.
