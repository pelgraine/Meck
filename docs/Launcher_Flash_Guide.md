# How to Flash Meck Firmware Using Launcher over WiFi

## How to Install Launcher on Your T-Deck Pro

First, ensure your SD card is inserted into your T-Deck Pro. Your SD card should already have been formatted as FAT32.

1. Plug your T-Deck Pro into your computer via USB-C.
2. Go to [https://bmorcelli.github.io/Launcher/webflasher.html](https://bmorcelli.github.io/Launcher/webflasher.html) in Chrome browser.
3. Click on **LilyGo** under Choose a Vendor.
4. Click on **T-Deck Pro**.
5. Click on **Connect**.
6. In the serial connect popup, click on your device in the list (likely starts with "USB JTAG/serial debug unit"), and click **Connect**. Wait a few seconds for it to connect.
7. Click the **Install T-Deck Pro** popup.
8. Click **Next**. (Don't worry about ticking the Erase Device checkbox.)
9. Click **Install**.

### If You Don't Already Have a Meck Firmware File

Download one from [https://github.com/pelgraine/Meck/releases](https://github.com/pelgraine/Meck/releases).

## How to Install a New Meck Firmware .bin File via Launcher

After flashing using [https://bmorcelli.github.io/Launcher/webflasher.html](https://bmorcelli.github.io/Launcher/webflasher.html), your Pro will reboot itself automatically and display the main Launcher home screen, with the SD card option highlighted.

![Launcher home screen](images/01_launcher_home.jpg)

Either tap **NEXT** on the device screen twice or tap on the WUI button, and tap **SEL**.

![WUI selected](images/02_wui_selected.jpg)

Tap on **My Network** on the pop-up menu. Press **NEXT/SEL** as needed to highlight and select your WiFi SSID.

Enter your WiFi SSID details.

Once connected, your device will display the WebUI connection screen with the T-Deck Pro IP address.

Open a browser on your computer — Chrome, Firefox, or Safari will do, but Firefox tends to be easiest — and type in the IP address displayed on your T-Deck Pro into your computer browser address bar, and press enter.

![WebUI IP address screen](images/03_webui_ip.jpg)

In this instance, for example, I would type `192.168.1.118`, and once I've pressed enter, the address bar now displays `http://192.168.1.118/` (as per the photo). If you're having trouble loading the IP address page, double check your browser hasn't automatically changed it to `https`. If it has, delete the `s` out of the URL and hit enter to load the page.

Login to the browser page with the username **admin** and password **launcher**, and click **Login**. The browser will refresh and display your SD card file list.

![Browser login](images/04_browser_login.jpg)

![SD card file list with Send Files button](images/05_send_files.png)

Scroll down to the bottom of the browser page, and click the **Send Files** button.

Your computer/device will load the file browser. Navigate to wherever you've previously saved your new Meck firmware `.bin` file, select the bin file, and click **Open**.

Wait for the blue loading bar on the bottom of the browser page to finish, and then check you can see the file name in the list in green. Also worth checking the file is at least 1.2MB — if it is under 1MB, the file hasn't uploaded properly and you will need to go through the **Send Files** button to try uploading it again.

![Check file uploaded](images/06_check_file_uploaded.png)

You can then either close the browser window or just leave it. Go back to your T-Deck Pro and press **SEL** to disconnect the WUI mode.

![Disconnect WUI](images/07_disconnect_wui.png)

Either press **PREV** twice to navigate to it and then press **SEL** again to open, or tap right on the **SD** button to open the SD card menu.

![SD button on Launcher home](images/08_sd_button.jpg)

The Launcher SD file browser will open. You will most likely have to tap **Page Down** at least twice to scroll to where the name of your new file is.

![SD file list page 1](images/09_sd_file_list.png)

![Page Down to find file](images/10_page_down.png)

Either press **NEXT** to navigate until the new file is highlighted with the `>`, or just tap right on the file name, and press **SEL** to bring up the file menu.

![Select the firmware file](images/11_select_file.png)

The first option on the file menu list will be **>Install**. You can either tap right on **Install** or tap **SEL**.

![Install option](images/12_install_option.png)

**Wait for the firmware to finish installing.** It will reboot itself automatically.

![Installing firmware](images/13_installing_fw.jpg)

> **Note:** On first flash of a new firmware version, the "Loading…" screen will most likely display for about 70 seconds. This is a known bug. **Please be patient** if this is the first time loading your new Meck firmware.

![Loading screen](images/14_loading_screen.png)

On every boot, the firmware will scan your SD card and `/books` folder for any new `.txt` or `.epub` files that haven't yet been cached. It's usually very quick even if you have a lot of ebook files, and even faster after the first boot.

![Indexing pages](images/15_indexing_pages.jpg)

You'll then see the firmware version splash screen for a split second.

![Version splash screen](images/16_version_splash.jpg)

Then the Meck home screen will display, and you're good to go. Here's an example of what the Meck 4G WiFi companion firmware home screen looks like:

![Meck home screen](images/17_meck_home.jpg)

> **Tip:** Every time you reset the device, the Launcher splash screen will display. Wait about six seconds if you just want the Meck firmware to boot by default. Otherwise, tap the **LAUNCHER** text at the bottom to boot back into the Launcher home screen, to get access to the SD menu and WUI menu again.

![Launcher boot screen](images/18_launcher_boot.jpg)
