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

<img src="images/01_launcher_home.jpg" alt="Launcher home screen" width="200">

Either tap **NEXT** on the device screen twice or tap on the WUI button, and tap **SEL**.

<img src="images/02_wui_selected.jpg" alt="WUI selected" width="200">

Tap on **My Network** on the pop-up menu. Press **NEXT/SEL** as needed to highlight and select your WiFi SSID.

Enter your WiFi SSID details.

Once connected, your device will display the WebUI connection screen with the T-Deck Pro IP address.

Open a browser on your computer — Chrome, Firefox, or Safari will do, but Firefox tends to be easiest — and type in the IP address displayed on your T-Deck Pro into your computer browser address bar, and press enter.

<img src="images/03_webui_ip.jpg" alt="WebUI IP address screen" width="200">

In this instance, for example, I would type `192.168.1.118`, and once I've pressed enter, the address bar now displays `http://192.168.1.118/` (as per the photo). If you're having trouble loading the IP address page, double check your browser hasn't automatically changed it to `https`. If it has, delete the `s` out of the URL and hit enter to load the page.

Login to the browser page with the username **admin** and password **launcher**, and click **Login**. The browser will refresh and display your SD card file list.

<img src="images/04_browser_login.jpg" alt="Browser login" width="450">

<img src="images/05_send_files.png" alt="SD card file list with Send Files button" width="450">

Scroll down to the bottom of the browser page, and click the **Send Files** button.

Your computer/device will load the file browser. Navigate to wherever you've previously saved your new Meck firmware `.bin` file, select the bin file, and click **Open**.

Wait for the blue loading bar on the bottom of the browser page to finish, and then check you can see the file name in the list in green. Also worth checking the file is at least 1.2MB — if it is under 1MB, the file hasn't uploaded properly and you will need to go through the **Send Files** button to try uploading it again.

<img src="images/06_check_file_uploaded.png" alt="Check file uploaded" width="450">

You can then either close the browser window or just leave it. Go back to your T-Deck Pro and press **SEL** to disconnect the WUI mode.

<img src="images/07_disconnect_wui.png" alt="Disconnect WUI" width="200">

Either press **PREV** twice to navigate to it and then press **SEL** again to open, or tap right on the **SD** button to open the SD card menu.

<img src="images/08_sd_button.jpg" alt="SD button on Launcher home" width="200">

The Launcher SD file browser will open. You will most likely have to tap **Page Down** at least twice to scroll to where the name of your new file is.

<img src="images/09_sd_file_list.png" alt="SD file list page 1" width="200">

<img src="images/10_page_down.png" alt="Page Down to find file" width="200">

Either press **NEXT** to navigate until the new file is highlighted with the `>`, or just tap right on the file name, and press **SEL** to bring up the file menu.

<img src="images/11_select_file.png" alt="Select the firmware file" width="200">

The first option on the file menu list will be **>Install**. You can either tap right on **Install** or tap **SEL**.

<img src="images/12_install_option.png" alt="Install option" width="200">

**Wait for the firmware to finish installing.** It will reboot itself automatically.

<img src="images/13_installing_fw.jpg" alt="Installing firmware" width="200">

> **Note:** On first flash of a new firmware version, the "Loading…" screen will most likely display for about 70 seconds. This is a known bug. **Please be patient** if this is the first time loading your new Meck firmware.

<img src="images/14_loading_screen.png" alt="Loading screen" width="200">

On every boot, the firmware will scan your SD card and `/books` folder for any new `.txt` or `.epub` files that haven't yet been cached. It's usually very quick even if you have a lot of ebook files, and even faster after the first boot.

<img src="images/15_indexing_pages.jpg" alt="Indexing pages" width="200">

You'll then see the firmware version splash screen for a split second.

<img src="images/16_version_splash.jpg" alt="Version splash screen" width="200">

Then the Meck home screen will display, and you're good to go. Here's an example of what the Meck 4G WiFi companion firmware home screen looks like:

<img src="images/17_meck_home.jpg" alt="Meck home screen" width="200">

> **Tip:** Every time you reset the device, the Launcher splash screen will display. Wait about six seconds if you just want the Meck firmware to boot by default. Otherwise, tap the **LAUNCHER** text at the bottom to boot back into the Launcher home screen, to get access to the SD menu and WUI menu again.

<img src="images/18_launcher_boot.jpg" alt="Launcher boot screen" width="200">
