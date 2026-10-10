# French Language: Guide and Translation Table

Status: draft for Meck v1.15, from the `dev` branch at commit `4fabd991`. French is an Experimental Feature, so its wording and behaviour may still change.
Last updated: 10 Oct 2026

This guide covers how to switch Meck into French, how the fonts behave in French, and a table of every English text that has a French version, for volunteer translators to review.

## Switching the language

The Language setting is in Settings > Experimental Features on every T-Deck Pro and T-Deck Max build.

To switch to French:

1. Open **Settings** (S key, or the Settings tile on the home screen).
2. Select **Experimental Features >>** (directly under **Rx Log >>**) and press Enter, or tap it.
3. Select **Language: Change to French** and press Enter, or tap it.

Meck switches to French straight away. The choice is saved, so Meck stays in French after a restart.

To switch back to English:

1. Open **Paramètres** (S key, or the Paramètres tile).
2. Select **Expérimental >>** and press Enter, or tap it.
3. Select **Langue : Passer à l'anglais** and press Enter, or tap it.

The row always describes what Enter will do: it offers French while Meck is in English, and English while Meck is in French.

## How the fonts behave in French

Meck has three font styles (Settings > Font, in French Police: Classic, Noto Sans, Montserrat) and two text sizes (Settings > Font Size, in French Taille du texte: Tiny, Larger).

- **Classic cannot show accents.** If Classic is selected when you switch to French, Meck changes the font to Noto Sans. While French is on, the Font setting skips Classic. Switching back to English keeps whichever font is selected at the time.
- **Text size is not changed.** French works at both Tiny and Larger.
- **Accented letters also show in node names and messages**, in either language, wherever the selected font can show them (see the table below).

Where accents can be shown:

Meck draws text at three sizes: small text (7 point), normal text (9 point), and titles and the large clock (12 point bold). Where a screen follows the text size setting, Tiny uses small text and Larger uses normal text. Some text is drawn at the normal size whatever the setting, for example the status lines on the WiFi home page.

| Font style | Small text | Normal text | Titles and the large clock |
| --- | --- | --- | --- |
| Noto Sans | Yes | Yes | Yes |
| Montserrat | Yes | Not yet: letters show without their accents | Yes |
| Classic | No (not offered in French) | No | No |

Two places always show French without accents or in English:

- The Game Boy in-game footer uses a fixed built-in font, so its French is written without accents.
- The start-up screens ("Loading...", "Formatting storage...") appear before the language setting is read, so they are always in English.

## Notes for translators

Each table row shows where the text appears (source file and line number at the commit above), the English, the current French, and notes.

Please keep these exactly as they are:

- **Format codes** such as `%d`, `%s`, `%02d`, `%.1f`, `%lu` and `%%`, in the same order. Meck replaces them with numbers, names and times.
- **Line breaks**, shown as `\n`.
- **Key names**: Q, W/S, A/D, Ent, Entrée, Sh+Del, S+D, Mic, and single letters such as C, F, N, R and Z. These are keys on the keyboard.
- **Symbols** such as `>`, `<`, `[ ]`, `*` and `#`.

Space is tight. The screen is 240 pixels wide, which at Larger size is roughly 28 characters per line, and roughly 33 to 35 at Tiny. Footers share one line between a left and a right part, so many words there are shortened on purpose (for example Ret, Ent, Suppr, Sél, Déf.). A few lines are already too long in English at Larger size.

To suggest a change, open an issue or a pull request at [github.com/pelgraine/Meck](https://github.com/pelgraine/Meck), quoting the file and line from the table and your suggested French.

## What stays in English

- Start-up screens shown before the language setting is read.
- Replies to the phone app's WiFi settings commands, which match MeshCore.
- The contacts export file written to the SD card.
- The web pages shown on your phone during a firmware update or in the SD file manager.
- Carrier and APN names, radio preset names, sound file names and emoji names.
- Technical terms and codes: packet types in the Rx Log, FLOOD and DIRECT, SNR and dB, BW, SF, CR, TX, modem states (such as READY), and the names Snake and Game Boy.
- Short key hints such as "W/S:Nav Q:X" and "A/D:Ch $:Emoji".
- The emoji picker's title, "Select Emoji".
- The GPS home page's "sat", "pos" and "alt" labels.
- The "Search:" and "Web:" labels on the Web Reader's input lines.
- Commands sent to repeaters and IRC servers, and the replies they send back.
- Messages printed to the serial (USB) console.

## Translation table (976 entries)

### Home screen, header and system messages

Source: `examples/companion_radio/ui-new/UITask.cpp`. All builds. Some lines only appear in builds with that feature: the WiFi home page in WiFi builds, the restart lines in the combined T-Deck Max build (`meck_max_ble_wifi`), and the GPS page in builds with GPS (not the 40 MHz builds).

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 322 | `Home` | `Local` |  |
| 369, 1162 | `W/S:adj Enter:ok Q:cancel` | `W/S:régler Ent:ok Q:annuler` |  |
| 370 | `Tap:-/+  Hold:save` | `Appui:-/+  Maintien:enreg.` | double space |
| 378 | `Clock not set` | `Heure non réglée` |  |
| 411 | `%+dD` | `%+dJ` | keep %+d |
| 536 | `powering off...` | `extinction...` |  |
| 537, 540 | `plug in USB-C to turn on` | `USB-C pour rallumer` |  |
| 628 | `Messages` | `Messages` | home tile; same in both languages |
| 628 | `Contacts` | `Contacts` | home tile; same in both languages |
| 629 | `Settings` | `Paramètres` | home tile |
| 629 | `Discover` | `Recherche` | home tile |
| 630 | `Trace` | `Trace` | home tile; same in both languages |
| 630 | `Maps` | `Cartes` | home tile |
| 631 | `Notes` | `Notes` | home tile; same in both languages |
| 631 | `Reader` | `Lecteur` | home tile |
| 632 | `Audiobooks` | `Livres audio` | home tile |
| 632 | `Alarm` | `Alarme` | home tile |
| 633 | `Browser` | `Navigateur` | home tile |
| 633 | `Games` | `Jeux` | home tile |
| 678 | `Phone` | `Téléphone` |  |
| 703, 980, 1006 | `< Connected >` | `< Connecté >` |  |
| 927 | `%dm` | `%dmin` | keep %d |
| 945 | `H: Full Last Heard list` | `H: Liste complète` |  |
| 963 | `Noise floor: %d` | `Bruit de fond : %d` | keep %d |
| 966 | `RX packets: %u` | `Paquets RX : %u` | keep %u |
| 991 | `restart into Bluetooth:` | `redémarrer en Bluetooth :` |  |
| 992 | `long press or Enter` | `appui long ou Entrée` |  |
| 994, 1015 | `toggle: long press` | `basculer : appui long` |  |
| 995, 1016, 1077 | `or press Enter key` | `ou touche Entrée` |  |
| 1022 | `WiFi Companion` | `Compagnon WiFi` |  |
| 1042 | `< App Connected >` | `< Appli connectée >` |  |
| 1046 | `Waiting for app...` | `Attente de l'appli...` |  |
| 1050 | `Not connected` | `Non connecté` |  |
| 1053 | `Configure in Settings` | `Configurer dans Paramètres` |  |
| 1061 | `Press Enter to Turn Off Wifi` | `Entrée : couper le WiFi` |  |
| 1062 | `Press Enter to Turn On Wifi` | `Entrée : activer le WiFi` |  |
| 1067 | `Press Enter to Restart into Wifi` | `Entrée : redémarrer en WiFi` |  |
| 1076 | `advert: long press` | `annonce : appui long` |  |
| 1087 | `gps off` | `gps éteint` |  |
| 1089 | `gps on` | `gps allumé` |  |
| 1095 | `Can't access GPS` | `GPS inaccessible` |  |
| 1097 | `no fix` | `sans fix` |  |
| 1106 | `sentences` | `phrases` |  |
| 1112 | `hw off` | `éteint` |  |
| 1136, 1140 | `time(U)` | `heure(U)` |  |
| 1141 | `no sync` | `non synchro` |  |
| 1246 | `Battery Gauge` | `Jauge batterie` |  |
| 1253 | `remaining` | `autonomie` |  |
| 1255 | `depleted` | `épuisée` |  |
| 1255 | `charging` | `en charge` |  |
| 1257 | `%dh %dm` | `%dh %dmin` | keep %d |
| 1266 | `avg current` | `courant moyen` |  |
| 1273 | `avg power` | `puissance moy.` |  |
| 1280 | `voltage` | `tension` |  |
| 1290 | `remaining cap` | `capacité rest.` |  |
| 1297 | `temperature` | `température` |  |
| 1306 | `hibernating...` | `mise en veille...` |  |
| 1310 | `power off device?` | `éteindre l'appareil ?` |  |
| 1311 | `usb-c to wake` | `usb-c pour réveiller` |  |
| 1312 | `Enter:yes  q:no` | `Entrée:oui  q:non` | double space |
| 1319 | `%shibernate: long press/Enter` | `%sveille : appui long/Entrée` | keep %s |
| 1320 | `%spower off: long press/Enter` | `%sarrêt : appui long/Entrée` | keep %s |
| 1357 | `UTC offset saved` | `Décalage UTC enregistré` |  |
| 1419 | `Recent adverts` | `Annonces récentes` |  |
| 1433 | `Reboot for Bluetooth` | `Redémarrer (Bluetooth)` |  |
| 1461 | `WiFi failed to start` | `Échec du WiFi` |  |
| 1500 | `Advert sent!` | `Annonce envoyée !` |  |
| 1502 | `Advert failed..` | `Échec de l'annonce..` |  |
| 1582 | `%d%%  \|  %d unread` | `%d%%  \|  %d non lus` | keep %d; double space |
| 1602 | `SMS: %d  Missed: %d` | `SMS : %d  Manqués : %d` | keep %d; double space |
| 1606 | `Missed: %d` | `Manqués : %d` | keep %d |
| 2015 | `New: %s` | `De : %s` | keep %s |
| 2382 | `S:Settings  E:Reader` | `S:Options  E:Lecteur` | double space |
| 2383 | `N:Notes  W/S:Scroll` | `N:Notes  W/S:Défiler` | double space |
| 2384 | `A/D:Cycle Left/Right` | `A/D:Gauche/Droite` |  |
| 2385 | `[X to dismiss]` | `[X pour fermer]` |  |
| 2518 | `Low Battery.` | `Batterie faible.` |  |
| 2519 | `Shutting Down!` | `Extinction !` |  |
| 2661 | `GPS gated (40MHz build)` | `GPS bloqué (40 MHz)` |  |
| 2676 | `GPS: Enabled` | `GPS : activé` |  |
| 2676 | `GPS: Disabled` | `GPS : désactivé` |  |
| 2693 | `Buzzer: OFF` | `Buzzer : coupé` |  |
| 2693 | `Buzzer: ON` | `Buzzer : activé` |  |
| 3045 | `Unknown` | `Inconnu` |  |
| 3172 | `Game Boy needs 240MHz` | `Game Boy exige 240 MHz` |  |

### Settings

Source: `examples/companion_radio/ui-new/SettingsScreen.h`. All builds. Some rows only appear in builds with that hardware or feature (WiFi, 4G modem, firmware update tools), and some only on the T-Deck Max (LoRa Antenna, Canned Messages, Backlight Brightness, Keyboard LED, Change Backlight to Alt+B).

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 95 | `Default (38400)` | `Défaut 38400` |  |
| 112 | `None` | `Aucun` |  |
| 434 | `Auto All` | `Auto (tous)` |  |
| 435 | `Custom` | `Personnalisé` |  |
| 436 | `Manual Only` | `Manuel seul` |  |
| 941 | `IP: %d.%d.%d.%d` | `IP : %d.%d.%d.%d` | keep %d |
| 954 | `Could not connect` | `Connexion impossible` |  |
| 1152 | `File not found on SD` | `Fichier absent de la SD` |  |
| 1157 | `Bad file size (need 0.5-6MB)` | `Taille invalide (0,5 à 6 Mo)` |  |
| 1169 | `Not a firmware file (bad magic)` | `Pas un firmware (signature)` |  |
| 1178 | `Cannot open firmware file` | `Firmware illisible` |  |
| 1191 | `Out of memory` | `Mémoire insuffisante` |  |
| 1202 | `Flash write error` | `Erreur d'écriture flash` |  |
| 1216, 2797 | `Flashing Firmware` | `Écriture du firmware` |  |
| 1217 | `%d / %d KB` | `%d / %d Ko` | keep %d |
| 1220, 2799 | `DO NOT POWER OFF` | `NE PAS ÉTEINDRE` |  |
| 1299 | `Update Complete!` | `Mise à jour terminée !` |  |
| 1304 | `Firmware: %d KB` | `Firmware : %d Ko` | keep %d |
| 1307 | `Firmware written` | `Firmware écrit` |  |
| 1310 | `Rebooting in 3 seconds...` | `Redémarrage dans 3 s...` |  |
| 1813 | `Welcome! Setup` | `Bienvenue ! Configuration` |  |
| 1815 | `Settings > Contacts` | `Paramètres > Contacts` |  |
| 1817 | `Settings > Channels` | `Paramètres > Canaux` |  |
| 1820 | `Settings > OTA Tools` | `Paramètres > Outils OTA` |  |
| 1823 | `Settings > Experimental` | `Paramètres > Expérimental` |  |
| 1825 | `Settings` | `Paramètres` |  |
| 1873 | `Name: %s_` | `Nom : %s_` | keep %s |
| 1875 | `Name: %s` | `Nom : %s` | keep %s |
| 1886 | `< Custom >` | `< Personnalisé >` |  |
| 1890 | `Preset: %s` | `Profil : %s` | keep %s |
| 1892 | `Preset: Custom` | `Profil : Personnalisé` |  |
| 1901 | `Freq: %s_ MHz` | `Fréq : %s_ MHz` | keep %s |
| 1903 | `Freq: %.3f MHz` | `Fréq : %.3f MHz` | keep %.3f |
| 1948 | `UTC Offset: %+d` | `Décalage UTC : %+d` | keep %+d |
| 1955 | `Brightness: %d%% <W/S>` | `Luminosité : %d%% <W/S>` | keep %d |
| 1957 | `Backlight Brightness: %d%%` | `Rétroéclairage : %d%%` | keep %d |
| 1964 | `Keyboard LED: %d%% <W/S>` | `LED clavier : %d%% <W/S>` | keep %d |
| 1966 | `Keyboard LED: %d%%` | `LED clavier : %d%%` | keep %d |
| 1972 | `Msg LED Flash: %s` | `Flash LED message : %s` | keep %s |
| 1973, 2020, 2065, 2073, 2170, 2176, 2182, 2188, 2194, 2345, 2352 | `ON` | `OUI` |  |
| 1973, 2020, 2065, 2073, 2170, 2176, 2182, 2188, 2194, 2345, 2352 | `OFF` | `NON` |  |
| 1979 | `Path Hash Size: %d-byte <W/S>` | `Taille hash chemin : %d o <W/S>` | keep %d |
| 1981 | `Path Hash Size: %d-byte` | `Taille hash chemin : %d o` | keep %d |
| 1988 | `Region: %s_` | `Région : %s_` | keep %s |
| 1990 | `Default Region: %s` | `Région déf. : %s` | keep %s |
| 1992 | `Default Region: (none)` | `Région déf. : (aucune)` |  |
| 2000 | `< GPS Baud: %s > *` | `< Débit GPS : %s > *` | keep %s |
| 2003 | `GPS Baud: %s *` | `Débit GPS : %s *` | keep %s |
| 2012 | `LoRa Antenna: %s` | `Antenne LoRa : %s` | keep %s |
| 2013 | `External` | `Externe` |  |
| 2013 | `Internal` | `Interne` |  |
| 2019 | `Dark Mode: %s` | `Mode sombre : %s` | keep %s |
| 2025 | `Font Size: %s` | `Taille du texte : %s` | keep %s |
| 2026 | `LARGER` | `GRAND` |  |
| 2026 | `TINY` | `PETIT` |  |
| 2032 | `< Font: %s >` | `< Police : %s >` | keep %s |
| 2035 | `Font: %s` | `Police : %s` | keep %s |
| 2044 | `< Auto Lock: %s >` | `< Verrou auto : %s >` | keep %s |
| 2047 | `Auto Lock: %s` | `Verrou auto : %s` | keep %s |
| 2059 | `WiFi: (not connected)` | `WiFi : (non connecté)` |  |
| 2064 | `WiFi Radio: %s` | `Radio WiFi : %s` | keep %s |
| 2072 | `4G Modem: %s` | `Modem 4G : %s` | keep %s |
| 2092 | `Channels >>` | `Canaux >>` |  |
| 2097 | `Rx Log >>` | `Journal RX >>` |  |
| 2103 | `Export/Import >>` | `Exporter/Importer >>` |  |
| 2108 | `Export to SD >>` | `Exporter vers SD >>` |  |
| 2112 | `Import from SD` | `Importer depuis SD` |  |
| 2116 | `  [%c] Identity` | `  [%c] Identité` | keep %c; starts with a space |
| 2122 | `  [%c] Radio Settings` | `  [%c] Réglages radio` | keep %c; starts with a space |
| 2128 | `  [%c] Channels` | `  [%c] Canaux` | keep %c; starts with a space |
| 2140 | `    [%c] Auto-Add Prefs` | `    [%c] Préf. d'ajout auto` | keep %c; starts with a space |
| 2147 | `>> Export Now` | `>> Exporter maintenant` |  |
| 2159 | `< Add Mode: %s >` | `< Ajout : %s >` | keep %s |
| 2162 | `Add Mode: %s` | `Ajout : %s` | keep %s |
| 2169 | `  Companion: %s` | `  Compagnon : %s` | keep %s; starts with a space |
| 2175 | `  Repeater: %s` | `  Répéteur : %s` | keep %s; starts with a space |
| 2181 | `  Room Server: %s` | `  Serveur salon : %s` | keep %s; starts with a space |
| 2187 | `  Sensor: %s` | `  Capteur : %s` | keep %s; starts with a space |
| 2193 | `  Overwrite Oldest: %s` | `  Écraser anciens : %s` | keep %s; starts with a space |
| 2201 | `--- Channels ---` | `--- Canaux ---` |  |
| 2222 | `Off` | `Non` |  |
| 2223 | `All` | `Tous` |  |
| 2227 | `N:%s T:Tone X:Del` | `N:%s T:Son X:Suppr` | keep %s |
| 2229 | `N:%s T:Tone Ent:Region` | `N:%s T:Son Ent:Région` | keep %s |
| 2233 | `N:%s Ent:Region X:Del` | `N:%s Ent:Région X:Suppr` | keep %s |
| 2235 | `N:%s Ent:Region` | `N:%s Ent:Région` | keep %s |
| 2263 | ` (empty)` | ` (vide)` | starts with a space |
| 2274 | `+ Add Channel (# = public)` | `+ Ajouter canal (# = public)` |  |
| 2281 | `Canned Messages >>` | `Messages prédéfinis >>` |  |
| 2293 | `%u: (empty)` | `%u: (vide)` | keep %u |
| 2302 | `OTA Tools >>` | `Outils OTA >>` |  |
| 2306, 2739, 2754 | `Firmware Update` | `Mise à jour du firmware` |  |
| 2310, 2829, 2846 | `SD File Manager` | `Gestionnaire de fichiers SD` |  |
| 2316 | `--- Device Info ---` | `--- Infos appareil ---` |  |
| 2323 | `Node ID: %s` | `ID nœud : %s` | keep %s |
| 2335 | `Experimental Features >>` | `Expérimental >>` |  |
| 2339 | `Language: Change to French` | `Langue : Passer à l'anglais` | Language row: shows the other language |
| 2344 | `Change Backlight to Alt+B: %s` | `Rétroéclairage Alt+B : %s` | keep %s |
| 2351 | `RX Boosted Gain: %s` | `Gain RX renforcé : %s` | keep %s |
| 2358 | `AGC Reset Int: %s_ s` | `Reset AGC : %s_ s` | keep %s |
| 2360 | `AGC Reset Int: %u s` | `Reset AGC : %u s` | keep %u |
| 2363 | `AGC Reset Int: Off` | `Reset AGC : Arrêt` |  |
| 2369 | `Delete all contacts` | `Supprimer tous les contacts` |  |
| 2375 | `(unavailable)` | `(indisponible)` |  |
| 2385 | `Carrier: %s (%d/5)` | `Opérateur : %s (%d/5)` | keep %s %d |
| 2387 | `Carrier: (searching)` | `Opérateur : (recherche)` |  |
| 2411 | `APN: (none)` | `APN : (aucun)` |  |
| 2456 | `Delete %s?` | `Supprimer %s ?` | keep %s |
| 2459 | `Apply radio changes?` | `Appliquer réglages radio ?` |  |
| 2461 | `Region not set.` | `Région non définie.` |  |
| 2462 | `Leave unset?` | `Laisser non définie ?` |  |
| 2464, 2495, 2509 | `Enter:Yes  Q:No` | `Entrée:Oui  Q:Non` | double space |
| 2483 | `Delete all contacts?` | `Supprimer les contacts ?` |  |
| 2486 | `All %d contact%s, with` | `Les %d contact%s (favoris` | keep %d %s |
| 2488 | `favourites and custom` | `et chemins manuels inclus)` |  |
| 2489 | `paths, and the DM history` | `et l'historique des MP` |  |
| 2490 | `will be deleted. Channel` | `seront effacés. Messages` |  |
| 2491 | `messages are kept.` | `des canaux conservés.` |  |
| 2493 | `The device will RESTART.` | `L'appareil va REDÉMARRER.` |  |
| 2506 | `Are you sure?` | `Êtes-vous sûr ?` |  |
| 2508 | `This cannot be undone.` | `Action irréversible.` |  |
| 2513 | `Purging` | `Suppression` |  |
| 2516 | `Deleting all contacts and` | `Suppression des contacts et` |  |
| 2517 | `the DM history.` | `de l'historique des MP.` |  |
| 2518, 2980, 2992, 3002 | `Please wait...` | `Veuillez patienter...` |  |
| 2520 | `Do not switch off. The` | `Ne pas éteindre. L'appareil` |  |
| 2521 | `device restarts when done.` | `redémarre à la fin.` |  |
| 2525 | `Contacts deleted` | `Contacts supprimés` |  |
| 2525 | `Purge failed` | `Échec de la suppression` |  |
| 2529 | `Done. %d contact%s and the` | `Fait. %d contact%s et` | keep %d %s |
| 2531 | `DM history have been` | `l'historique des MP ont été` |  |
| 2532 | `deleted from this device.` | `supprimés de l'appareil.` |  |
| 2534 | `Contacts file: %s` | `Fichier contacts : %s` | keep %s |
| 2534 | `failed` | `échec` |  |
| 2536 | `DM history: %s` | `Historique MP : %s` | keep %s |
| 2536 | `SD not ready` | `pas de SD` |  |
| 2538 | `The restart reloads what` | `Le redémarrage recharge` |  |
| 2539 | `storage still holds.` | `ce qui reste en mémoire.` |  |
| 2542 | `RESTARTING NOW...` | `REDÉMARRAGE...` |  |
| 2563 | `Notification Tone` | `Son de notification` |  |
| 2599 | `Default (silent)` | `Défaut (silencieux)` |  |
| 2602 | `Buzzer (vibrate)` | `Buzzer (vibreur)` |  |
| 2620 | `Enter:Pick  Q:Back` | `Entrée:Choisir  Q:Retour` | double space |
| 2651 | `Scanning for networks...` | `Recherche des réseaux...` |  |
| 2657 | `No networks found.` | `Aucun réseau trouvé.` |  |
| 2660 | `Check your hotspot is on` | `Vérifiez que le partage est` |  |
| 2663 | `and set to 2.4GHz.` | `actif et en 2,4 GHz.` |  |
| 2666 | `Press R or Enter to rescan.` | `R ou Entrée pour relancer.` |  |
| 2669 | `Select network:` | `Choisir un réseau :` |  |
| 2704 | `Password:` | `Mot de passe :` |  |
| 2719, 3307 | `Connecting...` | `Connexion...` |  |
| 2742 | `Start WiFi upload server?` | `Lancer le serveur WiFi ?` |  |
| 2745 | `You will upload a .bin file` | `Envoyez un fichier .bin` |  |
| 2748 | `from your device's browser.` | `depuis votre navigateur.` |  |
| 2751 | `Starting WiFi...` | `Démarrage du WiFi...` |  |
| 2757, 2849 | `Connect to WiFi network:` | `Connectez-vous au WiFi :` |  |
| 2765, 2857 | `Then open browser:` | `Puis ouvrez le navigateur :` |  |
| 2775 | `Waiting for upload...` | `En attente du fichier...` |  |
| 2781 | `Receiving Firmware` | `Réception du firmware` |  |
| 2784 | `%d KB received` | `%d Ko reçus` | keep %d |
| 2788 | `Do not close browser` | `Ne fermez pas la page` |  |
| 2794 | `Verifying file...` | `Vérification du fichier...` |  |
| 2804 | `Update Failed` | `Échec de la mise à jour` |  |
| 2832 | `Start WiFi file server?` | `Lancer le serveur WiFi ?` |  |
| 2835 | `Upload and download files` | `Envoyer et télécharger des` |  |
| 2838 | `on SD card via browser.` | `fichiers SD par navigateur.` |  |
| 2842 | `LoRa paused while active.` | `LoRa mis en pause.` |  |
| 2867 | `File server active...` | `Serveur de fichiers actif...` |  |
| 2873 | `File Manager Error` | `Erreur du gestionnaire` |  |
| 2901 | `Share with contact:` | `Partager avec un contact :` |  |
| 2906 | `No contacts available` | `Aucun contact disponible` |  |
| 2957 | `Enter:Send  Q:Cancel` | `Entrée:Envoyer  Q:Annuler` | double space |
| 2968 | `Type, Enter:Ok Sh+Del:Cancel` | `Entrée:Ok Sh+Del:Annuler` |  |
| 2973 | `R/Enter:Rescan Q:Back` | `R/Entrée:Relancer Q:Retour` |  |
| 2975 | `W/S:Pick Enter:Sel R:Rescan` | `W/S:Choix Ent:OK R:Relance` |  |
| 2978 | `Enter:Connect Sh+Del:Exit` | `Ent:Connexion Sh+Del:Sortir` |  |
| 2986, 2996 | `Enter:Start  Q:Cancel` | `Entrée:Lancer  Q:Annuler` | double space |
| 2988 | `Q:Cancel` | `Q:Annuler` |  |
| 2990, 3000, 3015 | `Q:Back` | `Q:Retour` |  |
| 2998 | `Q:Stop` | `Q:Arrêter` |  |
| 3006 | `A/D:Choose Enter:Ok` | `A/D:Choisir Entrée:Ok` |  |
| 3008 | `W/S:Adj Enter:Ok Q:Cancel` | `W/S:+/- Ent:Ok Q:Annuler` |  |
| 3013 | `Q:Bk C:Share` | `Q:Ret C:Partager` |  |
| 3017 | `Q:Bk` | `Q:Ret` |  |
| 3019 | `Tap/Ent:Edit` | `Ent:Éditer` |  |

### Channel list

Source: `examples/companion_radio/ui-new/ChannelPickerScreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 81 | `Direct Messages` | `Messages privés` |  |
| 90 | `Ch %d` | `Canal %d` | keep %d |
| 141 | `Channels` | `Canaux` |  |
| 257 | `Delete message history?` | `Supprimer l'historique ?` |  |
| 263 | `Enter:Yes  Q:Cancel` | `Entrée:Oui  Q:Annuler` | double space |
| 276 | `Enter:Yes Q:Cancel` | `Entrée:Oui Q:Annuler` |  |
| 279 | `Ent:Open` | `Ent:Ouvrir` |  |

### Messages

Source: `examples/companion_radio/ui-new/ChannelScreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 732 | `Direct Messages` | `Messages privés` |  |
| 735 | `DM: %s` | `MP : %s` | keep %s |
| 741 | `Channel %d` | `Canal %d` | keep %d |
| 874 | `No direct messages` | `Aucun message privé` |  |
| 876, 1167 | `A/D: Switch channel` | `A/D : changer de canal` |  |
| 912, 1271 | `%dm` | `%dmin` | keep %d |
| 914, 1273 | `%dd` | `%dj` | keep %d |
| 937 | `Q:Bck` | `Q:Ret` |  |
| 938 | `Ent:Open` | `Ent:Ouvrir` |  |
| 959 | `No received messages` | `Aucun message reçu` |  |
| 974 | `Age: %ds` | `Âge : %ds` | keep %d |
| 975 | `Age: %dm` | `Âge : %dmin` | keep %d |
| 976 | `Age: %dh` | `Âge : %dh` | keep %d |
| 977 | `Age: %dd` | `Âge : %dj` | keep %d |
| 988 | `Route: Direct` | `Route : directe` |  |
| 991 | `Route: Local/Sent` | `Route : locale/envoyé` |  |
| 994 | `Route: %d hop%s (%d-byte)` | `Route : %d saut%s (%d o)` | keep %d %s |
| 1004 | `Region: %s` | `Région : %s` | keep %s |
| 1133 | `Q:Back` | `Q:Ret` |  |
| 1136 | `W/S:Scrl` | `W/S:Déf.` |  |
| 1141 | `Ent:Copy` | `Ent:Copie` |  |
| 1158 | `No messages from %s` | `Aucun message de %s` | keep %s |
| 1161 | `Q: Back to inbox` | `Q : retour à la liste` |  |
| 1163 | `Ent: Compose reply` | `Ent : répondre` |  |
| 1165 | `No messages yet` | `Aucun message` |  |
| 1169 | `C: Compose message` | `C : écrire un message` |  |
| 1236 | `No conversations` | `Aucune conversation` |  |
| 1370 | `>%dm ` | `>%dmin ` | keep %d; ends with a space |
| 1374 | `>%dd ` | `>%dj ` | keep %d; ends with a space |
| 1378 | `Sending %d/%d ` | `Envoi %d/%d ` | keep %d; ends with a space |
| 1381 | `Delivered ` | `Distribué ` | ends with a space |
| 1384 | `Failed ` | `Échec ` | ends with a space |
| 1394 | `(%dh)(%db) %ds ` | `(%dh)(%do) %ds ` | keep %d; ends with a space |
| 1396 | `(%dh)(%db) %dm ` | `(%dh)(%do) %dmin ` | keep %d; ends with a space |
| 1398 | `(%dh)(%db) %dh ` | `(%dh)(%do) %dh ` | keep %d; ends with a space |
| 1400 | `(%dh)(%db) %dd ` | `(%dh)(%do) %dj ` | keep %d; ends with a space |
| 1628 | `W/S:Sel V:Pth Q:X` | `W/S:Sél V:Chem Q:X` |  |
| 1629, 1638 | `Ent:Reply` | `Ent:Rép` |  |
| 1634 | `Q:Exit L:Admin` | `Q:Quitter L:Admin` |  |
| 1636 | `Q:Exit` | `Q:Quitter` |  |
| 1642 | `Q:Bck R:Rply` | `Q:Ret R:Rép` |  |
| 1643 | `Ent:New` | `Ent:Nouveau` |  |

### Pop-up alerts and compose screen

Source: `examples/companion_radio/main.cpp`. All builds. Voice and phone call alerts only appear in builds with those features, WiFi connection alerts only in WiFi builds, and the Switch to WiFi, Switch to Bluetooth and Restarting alerts only in the combined T-Deck Max build (`meck_max_ble_wifi`).

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 915 | `Timed out` | `Délai dépassé` |  |
| 921 | `Wrong password?` | `Mot de passe erroné ?` |  |
| 923 | `Network not found` | `Réseau introuvable` |  |
| 925 | `Signal lost` | `Signal perdu` |  |
| 929 | `Network refused (%u)` | `Refus du réseau (%u)` | keep %u |
| 932 | `WiFi error %u` | `Erreur WiFi %u` | keep %u |
| 955 | `Connecting to Saved Wifi:` | `Connexion au WiFi :` |  |
| 967 | `Connected\nIP: %d.%d.%d.%d` | `Connecté\nIP : %d.%d.%d.%d` | keep %d; keep the line breaks (\n) |
| 974 | `Could not connect` | `Connexion impossible` |  |
| 1258 | `Restarting...` | `Redémarrage...` |  |
| 1274 | `Switch to WiFi?\nEnter again to restart` | `Passer au WiFi ?\nEntrée encore : redémarrer` | keep the line breaks (\n) |
| 1275 | `Switch to Bluetooth?\nEnter again to restart` | `Passer au Bluetooth ?\nEntrée encore : redémarrer` | keep the line breaks (\n) |
| 1396 | `Favourite! Press again` | `Favori ! Appuyez encore` |  |
| 1404 | `Removed: %s` | `Retiré : %s` | keep %s |
| 1414, 5431 | `Added: %s` | `Ajouté : %s` | keep %s |
| 1421 | `Advert expired, try later` | `Annonce expirée` |  |
| 1495 | `No canned messages` | `Aucun message prédéfini` |  |
| 2855 | `Exported to %s` | `Exporté vers %s` | keep %s |
| 2858 | `Export failed (SD?)` | `Échec export (SD ?)` |  |
| 2873 | `Config imported!` | `Config importée !` |  |
| 2875 | `No import.json found` | `import.json introuvable` |  |
| 2877 | `Import failed` | `Échec de l'import` |  |
| 3359 | `Sending voice...` | `Envoi du vocal...` |  |
| 3395 | `Voice sent!` | `Vocal envoyé !` |  |
| 3395 | `Send partial` | `Envoi partiel` |  |
| 3398, 5932 | `Send failed!` | `Échec de l'envoi !` |  |
| 3409 | `Voice msg received!` | `Vocal reçu !` |  |
| 3412 | `Voice decode failed` | `Échec décodage vocal` |  |
| 3490 | `SMS: %s` | `SMS : %s` | keep %s |
| 3510 | `Call: %s` | `Appel : %s` | keep %s |
| 3537 | `Call Ended  %lu:%02lu` | `Appel terminé  %lu:%02lu` | keep %lu %02lu; double space |
| 3541, 4728 | `Call Ended` | `Appel terminé` |  |
| 3550 | `Missed: %s` | `Manqué : %s` | keep %s |
| 3555 | `Line busy` | `Ligne occupée` |  |
| 3559 | `No answer` | `Pas de réponse` |  |
| 3563 | `Call failed` | `Échec de l'appel` |  |
| 4199, 5903 | `DM sent!` | `MP envoyé !` |  |
| 4592 | `No sections selected` | `Aucune section choisie` |  |
| 4596 | `Compiling...\nPlease Wait...` | `Compilation...\nVeuillez patienter...` | keep the line breaks (\n) |
| 4606 | `Importing...\nPlease Wait...` | `Importation...\nVeuillez patienter...` | keep the line breaks (\n) |
| 4638 | `Shared channel: %s` | `Canal partagé : %s` | keep %s |
| 4643 | `Shared with %s` | `Partagé avec %s` | keep %s |
| 4646 | `Share failed` | `Échec du partage` |  |
| 4874, 4904, 4947 | `None selected` | `Aucune sélection` |  |
| 4879, 4908, 4951 | `Memory error` | `Erreur mémoire` |  |
| 4884 | `Deleted %d contacts` | `%d contacts supprimés` | keep %d |
| 4893 | `Delete %d? Shift+Del again` | `Supprimer %d ? Shift+Del` | keep %d |
| 4914 | `Exported %d to SD (JSON)` | `%d exportés sur SD (JSON)` | keep %d |
| 4918 | `Export failed` | `Échec de l'export` |  |
| 4932 | `+%d imported (JSON)` | `+%d importés (JSON)` | keep %d |
| 4935 | `No new contacts` | `Aucun nouveau contact` |  |
| 4937, 5496 | `Import failed (no file?)` | `Échec import (fichier ?)` |  |
| 4956 | `Toggled fav on %d` | `Favoris modifiés : %d` | keep %d |
| 5393 | `No contact selected` | `Aucun contact choisi` |  |
| 5428 | `Already in contacts` | `Déjà dans les contacts` |  |
| 5435 | `Add failed` | `Échec de l'ajout` |  |
| 5464 | `Exported %d to SD` | `%d exportés sur SD` | keep %d |
| 5467 | `Export failed (check serial)` | `Échec export (voir série)` |  |
| 5490 | `+%d imported (%d total)` | `+%d importés (total %d)` | keep %d |
| 5494 | `No new contacts to add` | `Aucun nouveau contact` |  |
| 5752 | `DM: %s` | `MP : %s` | keep %s |
| 5756 | `To: %s` | `À : %s` | keep %s |
| 5758 | `To: Channel %d` | `À : Canal %d` | keep %d |
| 5905 | `DM failed!` | `Échec du MP !` |  |
| 5908 | `No contact!` | `Aucun contact !` |  |
| 5930 | `Sent!` | `Envoyé !` |  |
| 5935 | `No channel!` | `Aucun canal !` |  |

### Contacts

Source: `examples/companion_radio/ui-new/ContactsScreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 63 | `All` | `Tous` |  |
| 65 | `Rptr` | `Rép.` |  |
| 66 | `Room` | `Salon` |  |
| 67 | `Sens` | `Capt.` |  |
| 143 | `%dm` | `%dmin` | keep %d |
| 147 | `%dd` | `%dj` | keep %d |
| 302 | `%d Selected [%s]` | `Sélection : %d [%s]` | keep %d %s |
| 334 | `No contacts` | `Aucun contact` |  |
| 336 | `A/D: Change filter` | `A/D : changer de filtre` |  |
| 450 | `A:All D:Clr` | `A:Tous D:Rien` |  |
| 451 | `X:Exp F:Fav Q:Done` | `X:Exp F:Fav Q:OK` |  |
| 455 | `A/D:Filter` | `A/D:Filtre` |  |
| 456 | `P:Path Ent:Sel` | `P:Chemin Ent:Sél` |  |

### Last Heard

Source: `examples/companion_radio/ui-new/LastHeardScreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 46 | `%dm` | `%dmin` | keep %d |
| 48 | `%dd` | `%dj` | keep %d |
| 116 | `Last Heard: %d nodes` | `Entendus : %d nœuds` | keep %d |
| 132 | `No adverts received yet` | `Aucune annonce reçue` |  |
| 134 | `Nodes appear as adverts arrive` | `Les nœuds apparaissent ici` |  |
| 203 | `Q:Bk` | `Q:Ret` |  |
| 204 | `Tap/Ent:Add/Del` | `Ent:Ajout/Suppr` |  |

### Discovery

Source: `examples/companion_radio/ui-new/DiscoveryScreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 33 | `Rptr` | `Rép.` |  |
| 34 | `Room` | `Salon` |  |
| 35 | `Sens` | `Capt.` |  |
| 81 | `Scanning... %d found` | `Recherche... %d trouvé(s)` | keep %d |
| 83 | `Scan done: %d found` | `Terminé : %d trouvé(s)` | keep %d |
| 102 | `Listening for adverts...` | `Écoute des annonces...` |  |
| 102 | `No nodes found` | `Aucun nœud trouvé` |  |
| 105 | `F: Scan again  Q: Back` | `F : relancer  Q : retour` | double space |
| 184 | `Q:X F:Scan` | `Q:X F:Relance` |  |
| 186 | `Tap/Ent:Add` | `Ent:Ajouter` |  |

### Trace

Source: `examples/companion_radio/ui-new/Tracescreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 373 | `Trace Path` | `Tracer un chemin` |  |
| 411 | `%c Mode: %d-byte` | `%c Mode : %d o` | keep %c %d |
| 424 | `  Path: %s_` | `  Chemin : %s_` | keep %s; starts with a space |
| 440 | `%c Path: %s` | `%c Chemin : %s` | keep %c %s |
| 443 | `%c Type Path: [Press Enter]` | `%c Saisir chemin : [Entrée]` | keep %c |
| 450 | `%c + Add repeater...` | `%c + Ajouter un répéteur...` | keep %c |
| 455 | `%c - Remove last` | `%c - Retirer le dernier` | keep %c |
| 461 | `%c   Run Trace` | `%c   Lancer le traçage` | keep %c; double space |
| 466 | `%c   Exit` | `%c   Quitter` | keep %c; double space |
| 504 | `Q:Cancel Enter:Apply` | `Q:Annuler Entrée:Appliquer` |  |
| 506 | `Q:Exit W/S:Nav Ent:Sel` | `Q:Quitter W/S:Nav Ent:Sél` |  |
| 522 | `No repeaters in contacts` | `Aucun répéteur connu` |  |
| 525 | `Press Q to go back` | `Q pour revenir` |  |
| 565 | `Q:Back W/S:Scroll Ent:Add` | `Q:Ret W/S:Défil Ent:Ajout` |  |
| 574 | `Tracing...` | `Traçage...` |  |
| 580 | `%d hops, %d-byte mode` | `%d sauts, mode %d o` | keep %d |
| 587 | `Elapsed: %lu ms` | `Écoulé : %lu ms` | keep %lu |
| 617 | `Q:Cancel` | `Q:Annuler` |  |
| 630 | `Trace timed out` | `Délai de traçage dépassé` |  |
| 633 | `No response after %ds` | `Pas de réponse après %ds` | keep %d |
| 639 | `Complete: %dms` | `Terminé : %dms` | keep %d |
| 695 | `Return SNR: %.1fdB` | `SNR retour : %.1fdB` | keep %.1f |
| 709 | `Q:Back  Ent:New Trace` | `Q:Ret  Ent:Nouveau traçage` | double space |

### Path editor

Source: `examples/companion_radio/ui-new/PathEditorScreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 219 | `Unknown` | `Inconnu` |  |
| 242 | `Path: %s` | `Chemin : %s` | keep %s |
| 245 | `Path: %.12s..` | `Chemin : %.12s..` | keep %.12s |
| 301 | `%c Mode: DIRECT` | `%c Mode : DIRECT` | keep %c |
| 303 | `%c Mode: %dB/hop` | `%c Mode : %d o/saut` | keep %c %d |
| 315 | `%c + Add hop...` | `%c + Ajouter un saut...` | keep %c |
| 321 | `%c * Direct (set)` | `%c * Direct (défini)` | keep %c |
| 323 | `%c * Set Direct` | `%c * Passer en direct` | keep %c |
| 329 | `%c - Remove last hop` | `%c - Retirer dernier saut` | keep %c |
| 334 | `%c   Clear custom path` | `%c   Effacer le chemin` | keep %c; double space |
| 339 | `%c   Save & Exit` | `%c   Enregistrer et quitter` | keep %c; double space |
| 382 | `Q:Bk W/S:Nav` | `Q:Ret W/S:Nav` |  |
| 383 | `Enter:Sel` | `Entrée:Sél` |  |
| 397 | `Select Repeater (%d)` | `Choisir répéteur (%d)` | keep %d |
| 413 | `No repeaters in contacts` | `Aucun répéteur connu` |  |
| 415 | `Add repeaters first` | `Ajoutez des répéteurs` |  |
| 463 | `Q:Cancel W/S:Scroll` | `Q:Ret W/S:Défil` |  |
| 464 | `Enter:Add` | `Ent:Ajouter` |  |

### Rx Log

Source: `examples/companion_radio/ui-new/RxLogScreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 71 | `%02d:%02d:%02d  %u bytes` | `%02d:%02d:%02d  %u octets` | keep %02d %u; double space |
| 79 | `Hash: ` | `Hash : ` | ends with a space |
| 92 | `Path: %d hops` | `Chemin : %d sauts` | keep %d |
| 116 | `From %02x  To %02x` | `De %02x  À %02x` | keep %02x; double space |
| 152 | `Rx Log: %d pkts` | `Journal RX : %d paq.` | keep %d |
| 164 | `No packets received yet` | `Aucun paquet reçu` |  |
| 166 | `Packets appear as they arrive` | `Les paquets apparaissent ici` |  |
| 188 | `Q:Bk  W/S:Scroll` | `Q:Ret  W/S:Défiler` | double space |

### Repeater admin

Source: `examples/companion_radio/ui-new/RepeaterAdminScreen.h`. All builds. Commands sent to the repeater, and the repeater's replies, stay in English.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 41 | `Clock Sync` | `Synchro horloge` | admin command |
| 42 | `Send Advert` | `Envoyer une annonce` | admin command |
| 43 | `Get Clock` | `Lire l'horloge` | admin command |
| 49 | `View Neighbors` | `Voir les voisins` | admin command |
| 50 | `Remove Neighbor` | `Retirer un voisin` | admin command |
| 50 | `Pubkey hex prefix:` | `Préfixe hex de la clé :` | admin prompt |
| 56 | `Version` | `Version` | admin command; same in both languages |
| 57 | `Board` | `Carte` | admin command |
| 63 | `Get Name` | `Lire le nom` | admin command |
| 64 | `Get TX Power` | `Lire puissance TX` | admin command |
| 65 | `Get AF` | `Lire AF` | admin command |
| 66 | `Get Repeat` | `Lire répétition` | admin command |
| 67 | `Get Radio` | `Lire radio` | admin command |
| 68 | `Get Flood Max` | `Lire flood max` | admin command |
| 69 | `Get RX Delay` | `Lire délai RX` | admin command |
| 70 | `Get TX Delay` | `Lire délai TX` | admin command |
| 71 | `Get Direct TX Delay` | `Lire délai TX direct` | admin command |
| 72 | `Get Int Thresh` | `Lire seuil interf.` | admin command |
| 73 | `Get AGC Reset Int` | `Lire interv. reset AGC` | admin command |
| 74 | `Get Multi Acks` | `Lire multi-acks` | admin command |
| 75 | `Get Advert Int` | `Lire interv. annonce` | admin command |
| 76 | `Get Flood Adv Int` | `Lire interv. ann. flood` | admin command |
| 77 | `Get Guest Password` | `Lire mdp invité` | admin command |
| 78 | `Get Allow R/O` | `Lire accès lecture seule` | admin command |
| 79 | `Get ADC Multiplier` | `Lire multiplicateur ADC` | admin command |
| 85 | `Set Name` | `Régler le nom` | admin command |
| 85 | `Name:` | `Nom :` | admin prompt |
| 86 | `Set TX Power` | `Régler puissance TX` | admin command |
| 86 | `TX power (dBm):` | `Puissance TX (dBm) :` | admin prompt |
| 87 | `Set AF` | `Régler AF` | admin command |
| 87 | `Airtime factor:` | `Facteur airtime :` | admin prompt |
| 88 | `Set Repeat` | `Régler répétition` | admin command |
| 88, 101 | `on/off:` | `on/off :` | admin prompt |
| 89 | `Set Flood Max` | `Régler flood max` | admin command |
| 89 | `Max hops (0-64):` | `Sauts max (0-64) :` | admin prompt |
| 90 | `Set Flood Max Unscoped` | `Régler flood max global` | admin command |
| 90 | `Max hops (64=off):` | `Sauts max (64=arrêt) :` | admin prompt |
| 91 | `Set Flood Adv Max` | `Régler flood max ann.` | admin command |
| 91 | `Max hops (def 8):` | `Sauts max (déf. 8) :` | admin prompt |
| 92 | `Set RX Delay` | `Régler délai RX` | admin command |
| 92 | `Base (0=off):` | `Base (0=arrêt) :` | admin prompt |
| 93 | `Set TX Delay` | `Régler délai TX` | admin command |
| 93, 94 | `Factor:` | `Facteur :` | admin prompt |
| 94 | `Set Direct TX Delay` | `Régler délai TX direct` | admin command |
| 95 | `Set Int Thresh` | `Régler seuil interf.` | admin command |
| 95 | `dB (0=off, def 14):` | `dB (0=arrêt, déf. 14) :` | admin prompt |
| 96 | `Set AGC Reset Int` | `Régler interv. reset AGC` | admin command |
| 96 | `Seconds (0=off):` | `Secondes (0=arrêt) :` | admin prompt |
| 97 | `Set Multi Acks` | `Régler multi-acks` | admin command |
| 97 | `0 or 1:` | `0 ou 1 :` | admin prompt |
| 98 | `Set Advert Interval` | `Régler interv. annonce` | admin command |
| 98 | `Minutes (0=off):` | `Minutes (0=arrêt) :` | admin prompt |
| 99 | `Set Flood Adv Int` | `Régler interv. ann. flood` | admin command |
| 99 | `Hours (0=off, 3-48):` | `Heures (0=arrêt, 3-48) :` | admin prompt |
| 100 | `Set Guest Password` | `Régler mdp invité` | admin command |
| 100 | `Password:` | `Mot de passe :` | admin prompt |
| 101 | `Set Allow R/O` | `Régler accès lect. seule` | admin command |
| 102 | `Set ADC Multiplier` | `Régler multiplic. ADC` | admin command |
| 102 | `Factor (0=default):` | `Facteur (0=défaut) :` | admin prompt |
| 103 | `Set Radio` | `Régler la radio` | admin command |
| 103 | `freq,bw,sf,cr:` | `freq,bw,sf,cr :` | admin prompt |
| 104 | `Temp Radio` | `Radio temporaire` | admin command |
| 104 | `freq,bw,sf,cr,mins:` | `freq,bw,sf,cr,mins :` | admin prompt |
| 105 | `Change Admin Pwd` | `Changer mdp admin` | admin command |
| 105 | `New password:` | `Nouveau mot de passe :` | admin prompt |
| 111 | `Powersaving Status` | `État économie énergie` | admin command |
| 112 | `Powersaving On` | `Économie énergie : oui` | admin command |
| 113 | `Powersaving Off` | `Économie énergie : non` | admin command |
| 119 | `Reboot` | `Redémarrer` | admin command |
| 120 | `Start OTA` | `Lancer l'OTA` | admin command |
| 146 | `Clock & Adverts` | `Horloge et annonces` | admin category |
| 147 | `Neighbors` | `Voisins` | admin category |
| 148 | `Get Config` | `Lire la config` | admin category |
| 149 | `Set Config` | `Modifier la config` | admin category |
| 150 | `Powersaving` | `Économie d'énergie` | admin category |
| 151 | `Reboot & Start OTA` | `Redémarrage et OTA` | admin category |
| 152 | `Firmware & Device Info` | `Firmware et infos` | admin category |
| 308 | `%ds ago` | `il y a %ds` | keep %d |
| 310 | `%dm ago` | `il y a %dmin` | keep %d |
| 314 | `%dh%dm ago` | `il y a %dh%dmin` | keep %d |
| 315 | `%dh ago` | `il y a %dh` | keep %d |
| 317 | `%dd%dh ago` | `il y a %dj%dh` | keep %d |
| 399 | `just now` | `à l'instant` |  |
| 418 | `Neighbors: %d\n` | `Voisins : %d\n` | keep %d; keep the line breaks (\n) |
| 535 | `Login failed.\nCheck password.` | `Échec de connexion.\nVérifiez le mot de passe.` | keep the line breaks (\n) |
| 579 | `Command sent.\nTimeout is expected\n(device is rebooting/updating).` | `Commande envoyée.\nPas de réponse attendue\n(redémarrage/mise à jour).` | keep the line breaks (\n) |
| 584 | `Timeout - no response.` | `Pas de réponse (délai).` |  |
| 605 | `Login` | `Connexion` |  |
| 625 | `Logging in...` | `Connexion...` |  |
| 630 | `Waiting...` | `Attente...` |  |
| 643 | `Sh+Del:Exit` | `Sh+Del:Sortir` |  |
| 644 | `Ent:Login` | `Ent:Connexion` |  |
| 649 | `Q:Cancel` | `Q:Annuler` |  |
| 653, 654 | `Q:Exit` | `Q:Sortir` |  |
| 654 | `Ent:Open` | `Ent:Ouvrir` |  |
| 654, 659 | `W/S:Sel` | `W/S:Sél` |  |
| 658, 659 | `Q:Back` | `Q:Ret` |  |
| 659 | `Ent:Run` | `Ent:Lancer` |  |
| 663 | `Sh+Del:Cancel` | `Sh+Del:Annuler` |  |
| 664 | `Ent:Send` | `Ent:Envoyer` |  |
| 668 | `Q:No` | `Q:Non` |  |
| 669 | `Ent:Yes` | `Ent:Oui` |  |
| 674 | `Q:Back` | `Q:Retour` |  |
| 676 | `W/S:Scrll` | `W/S:Défil` |  |
| 740 | `Password:` | `Mot de passe :` |  |
| 795 | `Synced` | `Synchro` |  |
| 796 | `Drift:%+ds` | `Écart:%+ds` | keep %+d |
| 801 | `Rpt:%s Us:%s %s` | `Rép:%s Ici:%s %s` | keep %s |
| 824 | `Telemetry: requesting...` | `Télémétrie : demande...` |  |
| 1024 | `Confirm:` | `Confirmer :` |  |
| 1038 | `Value: %s` | `Valeur : %s` | keep %s |
| 1045 | `Timeout response is normal.` | `Pas de réponse : normal.` |  |
| 1047 | `Enter=Yes  Q=No` | `Entrée=Oui  Q=Non` | double space |
| 1196 | `Send failed.` | `Échec de l'envoi.` |  |
| 1222 | `Send failed.\nCheck contact path.` | `Échec de l'envoi.\nVérifiez le chemin.` | keep the line breaks (\n) |

### Notes

Source: `examples/companion_radio/ui-new/NotesScreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 507 | `[R:Rename]` | `[R:Renommer]` |  |
| 548 | `> + New Note` | `> + Nouvelle note` |  |
| 548 | `  + New Note` | `  + Nouvelle note` | starts with a space |
| 563 | `Q:Bk` | `Q:Ret` |  |
| 564 | `Tap/Ent:Open` | `Ent:Ouvrir` |  |
| 574 | `(empty note)` | `(note vide)` |  |
| 580, 666 | `Q:Bk Ent:Edit` | `Q:Ret Ent:Éditer` |  |
| 581, 668 | `X:Delete` | `X:Suppr` |  |
| 688 | `Edit: %s%s` | `Édition : %s%s` | keep %s |
| 766 | `Pg %d/%d` | `p. %d/%d` | keep %d |
| 771 | `Q:Back` | `Q:Retour` |  |
| 773 | `Sh+Del:Save` | `Sh+Del:Enreg.` |  |
| 783 | `Rename Note` | `Renommer la note` |  |
| 791 | `From: ` | `De : ` | ends with a space |
| 801 | `To:   ` | `Vers : ` | ends with a space |
| 813 | `(.txt added automatically)` | `(.txt ajouté auto.)` |  |
| 820, 854 | `Q:Cancel` | `Q:Annuler` |  |
| 821 | `Ent:Confirm` | `Ent:Valider` |  |
| 830 | `Delete Note?` | `Supprimer la note ?` |  |
| 836 | `File:` | `Fichier :` |  |
| 847 | `This cannot be undone.` | `Action irréversible.` |  |
| 855 | `Ent:Delete` | `Ent:Supprimer` |  |
| 986 | `Renaming...` | `Renommage...` |  |
| 1026, 1308 | `Deleting...` | `Suppression...` |  |
| 1289 | `Saving...` | `Enregistrement...` |  |
| 1336 | `SD card not found` | `Carte SD introuvable` |  |
| 1338 | `Insert SD card` | `Insérez une carte SD` |  |

### Reader

Source: `examples/companion_radio/ui-new/TextReaderScreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 487, 548 | `Indexing` | `Indexation` |  |
| 489, 550 | `Pages...` | `des pages...` |  |
| 530, 598 | `Please wait.` | `Patientez.` |  |
| 532 | `Loading shortly...` | `Chargement imminent...` |  |
| 854 | `Converting EPUB...` | `Conversion EPUB...` |  |
| 854, 943, 972 | `Please wait` | `Patientez` |  |
| 865 | `Convert failed!` | `Échec de conversion !` |  |
| 943, 972 | `Indexing...` | `Indexation...` |  |
| 1075 | `Text Reader` | `Lecteur de texte` |  |
| 1096 | `No files found` | `Aucun fichier trouvé` |  |
| 1098 | `Add .txt or .epub to` | `Ajoutez des .txt ou .epub` |  |
| 1100 | `/books/ on SD card` | `dans /books/ sur la SD` |  |
| 1134 | `.. (up)` | `.. (parent)` |  |
| 1167 | `Q:Bk` | `Q:Ret` |  |
| 1169 | `Tap/Ent:Open` | `Ent:Ouvrir` |  |
| 1273 | `Go to: %.*s_` | `Aller à %.*s_` | keep %.*s |
| 1281 | `Ent:Go Sh+Del:Cancel` | `Ent:OK Sh+Del:Annul.` |  |
| 1281 | `Entr:Pg# Q:Bk` | `Ent:Pg# Q:Ret` |  |
| 1503 | `Scanning...` | `Analyse...` |  |
| 1735 | `SD card not found` | `Carte SD introuvable` |  |
| 1737 | `Insert SD with /books/` | `Insérez une SD avec /books/` |  |

### Map

Source: `examples/companion_radio/ui-new/MapScreen.h`. Builds with GPS. On the 40 MHz builds this screen can't be opened from the keyboard or the home screen.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 263 | `SD card not found` | `Carte SD introuvable` |  |
| 265 | `Insert SD with` | `Insérez une SD avec` |  |
| 913 | `WASD:pan Z/X:zoom` | `WASD:bouger Z/X:zoom` |  |

### Audiobooks

Source: `examples/companion_radio/ui-new/AudiobookPlayerScreen.h`. T-Deck Pro audio builds and all T-Deck Max builds. On the 40 MHz builds this screen can't be opened from the keyboard or the home screen.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 505 | `Loading` | `Chargement` |  |
| 507, 1271 | `Audiobooks` | `Livres audio` |  |
| 511 | `Please wait...` | `Veuillez patienter...` |  |
| 985 | `Loading...` | `Chargement...` |  |
| 1277 | `No audiobooks found.` | `Aucun livre audio trouvé.` |  |
| 1279 | `Place .m4b/.mp3 in` | `Placez des .m4b/.mp3 dans` |  |
| 1281 | `/audiobooks/ on SD` | `/audiobooks/ sur la SD` |  |
| 1283 | `0 files` | `0 fichier` |  |
| 1283 | `Q:Back` | `Q:Retour` |  |
| 1325 | `.. (up)` | `.. (parent)` |  |
| 1370 | `%d files` | `%d fichiers` | keep %d |
| 1371 | `W/S:Nav Enter:Open` | `W/S Ent:Ouvrir` |  |
| 1417 | `Ch %d/%d` | `Chap. %d/%d` | keep %d |
| 1427 | `Paused` | `En pause` |  |
| 1427 | `Playing` | `Lecture` |  |
| 1427 | `Stopped` | `Arrêté` |  |
| 1472 | `Track %d/%d` | `Piste %d/%d` | keep %d |
| 1478 | `Enter: Play/Pause` | `Entrée : lecture/pause` |  |
| 1491 | `Sleep: %d:%02d (Z:Off)` | `Veille : %d:%02d (Z:arrêt)` | keep %d %02d |
| 1495 | `Z: Start 45m sleep timer` | `Z : minuterie 45 min` |  |
| 1498 | `[/]: Prev/Next Chapter` | `[/] : chapitre préc./suiv.` |  |
| 1501 | `N: Next Track` | `N : piste suivante` |  |
| 1509 | `Q:Leave` | `Q:Sortir` |  |
| 1509 | `Q:Close` | `Q:Fermer` |  |
| 1511 | `A/D:Seek N:Next` | `A/D:Saut N:Suiv.` |  |
| 1513 | `A/D:Seek W/S:Vol` | `A/D:Saut W/S:Vol` |  |
| 1681 | `SD card not found` | `Carte SD introuvable` |  |
| 1683 | `Insert SD with` | `Insérez une SD avec` |  |
| 1685 | `/audiobooks/ folder` | `le dossier /audiobooks/` |  |

### Voice messages

Source: `examples/companion_radio/ui-new/VoiceMessageScreen.h`. T-Deck Pro audio builds and all T-Deck Max builds. On the 40 MHz builds this screen can't be opened from the keyboard or the home screen.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 1001 | `Send Voice To:` | `Envoyer le vocal à :` |  |
| 1006 | `No contacts with` | `Aucun contact avec` |  |
| 1008 | `direct path.` | `chemin direct.` |  |
| 1047 | `Ent:Send Q:Cancel` | `Ent:Envoyer Q:Annuler` |  |
| 1064 | `No direct path` | `Pas de chemin direct` |  |
| 1066 | `to contact.` | `vers ce contact.` |  |
| 1068 | `Set a path in` | `Définissez un chemin` |  |
| 1070 | `Contacts (P key)` | `dans Contacts (touche P)` |  |
| 1116, 1123 | `Voice Messages` | `Messages vocaux` |  |
| 1119 | `%d files` | `%d fichiers` | keep %d |
| 1130 | `No voice messages.` | `Aucun message vocal.` |  |
| 1132 | `Hold Mic key to record.` | `Mic maintenu : enregistrer` |  |
| 1176 | `Playing... Q:Stop` | `Lecture... Q:Arrêt` |  |
| 1178 | `Mic:Rec Ent:Ply F:Snd D:Del` | `Mic:Enr Ent:Lire F:Env D:Sup` |  |
| 1180 | `Mic:Record Q:Exit` | `Mic:Enregistrer Q:Quitter` |  |
| 1198 | `Loading...` | `Chargement...` |  |
| 1200 | `Encoding voice` | `Encodage du vocal` |  |
| 1211 | `RECORDING` | `ENREGISTREMENT` |  |
| 1266 | `Release Mic to stop` | `Relâchez Mic pour arrêter` |  |
| 1274 | `Review Recording` | `Écouter l'enregistrement` |  |
| 1284 | `Duration: %.1f seconds` | `Durée : %.1f secondes` | keep %.1f |
| 1292 | `Size: %.1f KB` | `Taille : %.1f Ko` | keep %.1f |
| 1294 | `Size: %d bytes` | `Taille : %d octets` | keep %d |
| 1303 | `Codec2: %d bytes (%d pkt%s)` | `Codec2 : %d o (%d paquet%s)` | keep %d %s |
| 1309 | `Codec2: encode failed` | `Codec2 : échec d'encodage` |  |
| 1315 | `Playing...` | `Lecture...` |  |
| 1317 | `Ready` | `Prêt` |  |
| 1325 | `Q:Stop` | `Q:Arrêt` |  |
| 1327 | `S:Send Ent:Play Mic:Redo Q:List` | `S:Env Ent:Lire Mic:Enr Q:Liste` |  |
| 1329 | `Ent:Play Mic:Redo D:Del Q:List` | `Ent:Lire Mic:Enr D:Sup Q:Liste` |  |

### Alarm clock

Source: `examples/companion_radio/ui-new/AlarmScreen.h`. T-Deck Pro audio builds and all T-Deck Max builds. On the 40 MHz builds this screen can't be opened from the keyboard or the home screen.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 203 | `Su` | `Di` | day |
| 203 | `Mo` | `Lu` | day |
| 203 | `Tu` | `Ma` | day |
| 203 | `We` | `Me` | day |
| 203 | `Th` | `Je` | day |
| 203 | `Fr` | `Ve` | day |
| 203 | `Sa` | `Sa` | day; same in both languages |
| 215 | `Every day` | `Tous les jours` |  |
| 216 | `Weekdays` | `En semaine` |  |
| 217 | `Weekend` | `Week-end` |  |
| 218 | `Never` | `Jamais` |  |
| 456 | `Alarm Clock` | `Réveil` |  |
| 463 | `Place 44kHz .mp3 in /alarms/` | `.mp3 (44 kHz) dans /alarms/` |  |
| 500 | `ON ` | `OUI` | ends with a space |
| 500, 555 | `OFF` | `NON` |  |
| 529 | `O:On/Off Enter:Edit` | `O:Oui/Non Ent:Éditer` |  |
| 539 | `Edit Alarm %d` | `Modifier le réveil %d` | keep %d |
| 555 | `ON` | `OUI` |  |
| 556 | `Enabled` | `Activé` |  |
| 559 | `Hour` | `Heure` |  |
| 568 | `Days` | `Jours` |  |
| 575, 716, 758 | `Buzzer (vibrate)` | `Buzzer (vibreur)` |  |
| 587 | `(default)` | `(défaut)` |  |
| 589 | `Sound` | `Son` |  |
| 627 | `A/D: toggle day  ` | `A/D : jour oui/non  ` | ends with a space |
| 649 | `Hour (0-23):` | `Heure (0-23) :` |  |
| 649 | `Min (0-59):` | `Min (0-59) :` |  |
| 660 | `Type digits` | `Chiffres` |  |
| 660 | `Enter:OK Sh+Del:Cancel` | `Ent:OK Sh+Del:Annuler` |  |
| 662 | `A/D:Adjust Enter:Type` | `A/D:Régler Ent:Saisie` |  |
| 662 | `Q:Save` | `Q:OK` |  |
| 672 | `Pick Alarm Sound` | `Choisir le son du réveil` |  |
| 679 | `No .mp3 files found.` | `Aucun fichier .mp3 trouvé.` |  |
| 681 | `Place 44kHz .mp3 in` | `Placez des .mp3 44 kHz dans` |  |
| 683 | `/alarms/ on SD card` | `/alarms/ sur la carte SD` |  |
| 684 | `0 files` | `0 fichier` |  |
| 684 | `Q:Back` | `Q:Retour` |  |
| 731 | `%d files` | `%d fichiers` | keep %d |
| 732 | `Enter:Pick Q:X` | `Ent:Choisir Q:X` |  |
| 751 | `Alarm %d` | `Réveil %d` | keep %d |
| 776 | `Unlock to dismiss` | `Déverrouillez pour arrêter` |  |
| 778 | `ANY KEY: Dismiss` | `UNE TOUCHE : arrêter` |  |
| 782 | `Z: Snooze 5 min` | `Z : rappel dans 5 min` |  |
| 1229 | `No SD card` | `Pas de carte SD` |  |
| 1231 | `Insert SD card and` | `Insérez une carte SD et` |  |
| 1233 | `create /alarms/` | `créez /alarms/` |  |

### Games menu

Source: `examples/companion_radio/ui-new/GamesMenuScreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 56 | `Snake` | `Snake` | game name; same in both languages |
| 57 | `Minesweeper` | `Démineur` | game name |
| 59 | `Game Boy` | `Game Boy` | game name; same in both languages |
| 117 | `Games` | `Jeux` |  |
| 152 | `Enter:Play  Q:Back` | `Ent:Jouer  Q:Retour` | double space |

### Snake

Source: `examples/companion_radio/ui-new/SnakeScreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 279 | `Jan` | `janv.` | month |
| 279 | `Feb` | `févr.` | month |
| 279 | `Mar` | `mars` | month |
| 279 | `Apr` | `avr.` | month |
| 279 | `May` | `mai` | month |
| 279 | `Jun` | `juin` | month |
| 279 | `Jul` | `juil.` | month |
| 279 | `Aug` | `août` | month |
| 279 | `Sep` | `sept.` | month |
| 279 | `Oct` | `oct.` | month |
| 279 | `Nov` | `nov.` | month |
| 279 | `Dec` | `déc.` | month |
| 380, 459 | `Score: %d` | `Score : %d` | keep %d |
| 391 | `Classic Snake` | `Snake classique` |  |
| 394 | `W/S/A/D to steer` | `W/S/A/D pour diriger` |  |
| 396 | `Eat food to grow` | `Mangez pour grandir` |  |
| 398 | `Steer clear of walls` | `Évitez les murs` |  |
| 403 | `-- High Scores --` | `-- Meilleurs scores --` |  |
| 421 | `Press Enter to start` | `Entrée pour commencer` |  |
| 456 | `Game Over` | `Partie terminée` |  |
| 465 | `New #%d High Score!` | `Nouveau record n°%d !` | keep %d |
| 470 | `Enter:Retry  Sh+Del:Back` | `Ent:Rejouer  Sh+Del:Retour` | double space |
| 480 | `Sh+Del:Back` | `Sh+Del:Retour` |  |
| 483 | `Enter:Start  Sh+Del:Back` | `Ent:Jouer  Sh+Del:Retour` | double space |

### Minesweeper

Source: `examples/companion_radio/ui-new/MinesweeperScreen.h`. All builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 377, 384 | `Minesweeper` | `Démineur` |  |
| 387 | `W/S/A/D to move cursor` | `W/S/A/D pour se déplacer` |  |
| 389 | `Enter to reveal a cell` | `Entrée : révéler une case` |  |
| 391 | `F to flag a mine` | `F : marquer une mine` |  |
| 395 | `%dx%d grid, %d mines` | `Grille %dx%d, %d mines` | keep %d |
| 398 | `Press Enter to start` | `Entrée pour commencer` |  |
| 405 | `Enter:Start  Sh+Del:Back` | `Ent:Jouer  Sh+Del:Retour` | double space |
| 454 | `Cleared!` | `Gagné !` |  |
| 456 | `Boom!` | `Boum !` |  |
| 460 | `Enter:Retry  Sh+Del:Back` | `Ent:Rejouer  Sh+Del:Retour` | double space |

### Game Boy

Source: `examples/companion_radio/ui-new/GBCEmulatorScreen.cpp`. All builds except the 40 MHz test builds.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 612 | `Out of memory` | `Mémoire insuffisante` |  |
| 621 | `Cannot open ROM` | `ROM illisible` |  |
| 629 | `Not a ROM` | `Pas une ROM` |  |
| 649 | `Out of PSRAM` | `PSRAM insuffisante` |  |
| 660 | `ROM read failed` | `Échec de lecture ROM` |  |
| 684 | `Unsupported ROM` | `ROM non prise en charge` |  |
| 785 | `Task create failed` | `Échec de démarrage` |  |
| 825 | `Game crashed` | `Le jeu a planté` |  |
| 826 | `Saved` | `Sauvegardé` |  |
| 916 | `No ROMs in /roms` | `Aucune ROM dans /roms` |  |
| 923 | `Loading...` | `Chargement...` |  |
| 945 | `%d Unread` | `%d non lus` | keep %d |
| 1025 | `No .gb/.gbc files` | `Aucun fichier .gb/.gbc` |  |
| 1026 | `in /roms on SD` | `dans /roms sur la SD` |  |
| 1054 | `Enter:Play  Q:Back` | `Ent:Jouer  Q:Retour` | double space |
| 1078, 1082 | `Saving...` | `Sauvegarde...` | no accents possible here (built-in font) |
| 1079, 1082 | `Q: Quit` | `Q: Quitter` | no accents possible here (built-in font) |
| 1080 | `Q: Quit   Press Mic key to unmute` | `Q: Quitter   Mic: remettre le son` | double space; no accents possible here (built-in font) |
| 1080 | `Q: Quit   Mic: Mute` | `Q: Quitter   Mic: Muet` | double space; no accents possible here (built-in font) |

### Web Reader (browser and IRC)

Source: `examples/companion_radio/ui-new/WebReaderScreen.h`. Builds with the Web Reader: T-Deck Pro 4G builds, T-Deck Pro audio builds except the standalone ones, and all T-Deck Max builds. On the 40 MHz builds this screen can't be opened from the keyboard or the home screen.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 756 | `Password` | `Mot de passe` |  |
| 766, 3958 | `Submit` | `Envoyer` |  |
| 1428, 2659 | `WiFi Setup` | `Configuration WiFi` |  |
| 1433, 2667 | `Scanning for networks...` | `Recherche des réseaux...` |  |
| 1456, 1483 | `No networks found` | `Aucun réseau trouvé` |  |
| 1459 | `Scan failed (err ` | `Échec du scan (err ` | ends with a space |
| 1470 | `Scan timeout` | `Délai de scan dépassé` |  |
| 1477 | `Scan failed` | `Échec du scan` |  |
| 1516 | `Connection timeout` | `Délai de connexion dépassé` |  |
| 1528, 2310, 2762, 5072 | `Web Reader` | `Navigateur` |  |
| 1533 | `Connected!` | `Connecté !` |  |
| 1768 | `SD write failed` | `Échec d'écriture SD` |  |
| 1788, 1972 | `Out of memory` | `Mémoire insuffisante` |  |
| 1802, 1853 | `Downloading` | `Téléchargement` |  |
| 1811 | `to /books/` | `vers /books/` |  |
| 1860 | `%d / %d KB (%d%%)` | `%d / %d Ko (%d%%)` | keep %d |
| 1863 | `%d KB downloaded` | `%d Ko téléchargés` | keep %d |
| 1889 | `SD write error` | `Erreur d'écriture SD` |  |
| 1889 | `Empty download` | `Téléchargement vide` |  |
| 1935 | `SSL error (Cloudflare)` | `Erreur SSL (Cloudflare)` |  |
| 1936 | `Blocked (403)` | `Bloqué (403)` |  |
| 1937 | `Unavailable (503)` | `Indisponible (503)` |  |
| 1940 | `Connection refused` | `Connexion refusée` |  |
| 1941 | `Send header failed` | `Échec d'envoi de l'en-tête` |  |
| 1942 | `Send payload failed` | `Échec d'envoi des données` |  |
| 1943, 4656 | `Not connected` | `Non connecté` |  |
| 1944 | `Connection lost` | `Connexion perdue` |  |
| 1945 | `No stream` | `Pas de flux` |  |
| 1946 | `No HTTP server` | `Pas de serveur HTTP` |  |
| 1947 | `Out of RAM` | `RAM insuffisante` |  |
| 1948 | `Encoding error` | `Erreur d'encodage` |  |
| 1949 | `Stream write error` | `Erreur d'écriture du flux` |  |
| 1950 | `Read timeout` | `Délai de lecture dépassé` |  |
| 1951 | `Error ` | `Erreur ` | ends with a space |
| 1999 | `Out of memory (HTML)` | `Mémoire insuffisante (HTML)` |  |
| 2084 | `Connection failed` | `Échec de connexion` |  |
| 2173, 2268 | `WiFi reconnect failed` | `Échec de reconnexion WiFi` |  |
| 2206 | `Redirect with no Location` | `Redirection sans adresse` |  |
| 2297 | `Too many redirects` | `Trop de redirections` |  |
| 2315 | `Fetch failed:` | `Échec du chargement :` |  |
| 2332 | `Returning to URL entry...` | `Retour à la saisie d'URL...` |  |
| 2448 | `Logging in...` | `Connexion...` |  |
| 2452 | `Refreshing session...` | `Actualisation de la session...` |  |
| 2697 | `Password:` | `Mot de passe :` |  |
| 2715, 3187, 4224 | `Connecting...` | `Connexion...` |  |
| 2723 | `WiFi Error:` | `Erreur WiFi :` |  |
| 2743 | `Enter: Retry  Sh+Del: Back` | `Ent:Réessayer  Sh+Del:Ret` | double space |
| 2752 | `Sh+Del:Back W/S:Nav Ent:Select` | `Sh+Del:Ret W/S:Nav Ent:Choisir` |  |
| 2777 | `Web Reader (Offline)` | `Navigateur (hors ligne)` |  |
| 2892 | `IRC: %s [connected]` | `IRC : %s [connecté]` | keep %s |
| 2895 | `IRC: connecting...` | `IRC : connexion...` |  |
| 2939 | `Web: [Enter URL]` | `Web: [saisir l'URL]` |  |
| 2985 | `-- Bookmarks --` | `-- Favoris --` |  |
| 3033 | `-- History --` | `-- Historique --` |  |
| 3110 | `Type URL  Ent:Go` | `Saisir l'URL  Ent:OK` | double space |
| 3112 | `Type query Ent:Search` | `Saisir la recherche  Ent:OK` | double space |
| 3118 | `Ent:Go Del:Del Bkmk X:Clr Ckies` | `Ent:OK Del:Suppr fav X:Cookies` |  |
| 3120 | `Q:Bk Ent:Go Del:Del Bkmk` | `Q:Ret Ent:OK Del:Suppr fav` |  |
| 3122 | `Q:Bk W/S Ent:Go X:Clr Ckies` | `Q:Ret W/S Ent:OK X:Cookies` |  |
| 3124 | `Q:Bk W/S:Nav Ent:Go` | `Q:Ret W/S:Nav Ent:OK` |  |
| 3154 | `Loading...` | `Chargement...` |  |
| 3180 | `Retry %d/4...  %ds` | `Nouvel essai %d/4...  %ds` | keep %d; double space |
| 3183 | `%d bytes  (%ds)` | `%d octets  (%ds)` | keep %d; double space |
| 3185 | `Connecting... %ds` | `Connexion... %ds` | keep %d |
| 3198 | `Download Complete` | `Téléchargement terminé` |  |
| 3204 | `Saved to /books/:` | `Enregistré dans /books/ :` |  |
| 3223 | `Ent: Open in Reader` | `Ent : ouvrir dans le lecteur` |  |
| 3225 | `Q:   Back to browser` | `Q :  retour au navigateur` | double space |
| 3228 | `Download Failed` | `Échec du téléchargement` |  |
| 3240 | `Q: Back to browser` | `Q : retour au navigateur` |  |
| 3249 | `Ent:Read  Q:Back` | `Ent:Lire  Q:Retour` | double space |
| 3249 | `Q:Back` | `Q:Retour` |  |
| 3256 | `No content` | `Aucun contenu` |  |
| 3375 | `#%d_ Ent:Go` | `#%d_ Ent:OK` | keep %d |
| 3378 | `L:Lnk F:Frm B:Bk Q:X` | `L:Lien F:Form B:Ret Q:X` |  |
| 3380 | `F:Frm B:Bk Q:X` | `F:Form B:Ret Q:X` |  |
| 3382 | `L:Lnk B:Bk Q:X` | `L:Lien B:Ret Q:X` |  |
| 3384 | `B:Bk Q:X` | `B:Ret Q:X` |  |
| 3703 | `Bookmark deleted` | `Favori supprimé` |  |
| 3802 | `Bookmarked!` | `Ajouté aux favoris !` |  |
| 3888 | `Form %d/%d` | `Formulaire %d/%d` | keep %d |
| 3891 | `Form` | `Formulaire` |  |
| 3974 | `(empty)` | `(vide)` |  |
| 3990 | `Type text  Ent:Next Sh+Del:Undo` | `Saisir  Ent:Suiv. Sh+Del:Annuler` | double space |
| 3994 | `W/S:Nav Ent:Edit </>:Form Sh+Del:Back` | `W/S Ent:Éditer </>:Form Sh+Del:Ret` |  |
| 3996, 4665 | `W/S:Nav Ent:Edit/Go Sh+Del:Back` | `W/S Ent:Éditer/OK Sh+Del:Ret` |  |
| 4248 | `Connection failed!` | `Échec de connexion !` |  |
| 4268 | `Registering...` | `Enregistrement...` |  |
| 4284 | `Disconnected` | `Déconnecté` |  |
| 4333 | `Registered! Use /join #channel` | `Enregistré ! Utilisez /join #canal` |  |
| 4344 | `Registered! Joining %s...` | `Enregistré ! Connexion à %s...` | keep %s |
| 4369 | `Nick taken, trying %s` | `Pseudo pris, essai de %s` | keep %s |
| 4399 | `Error %d: %s` | `Erreur %d : %s` | keep %d %s |
| 4435 | `Joined %s` | `Rejoint : %s` | keep %s |
| 4439 | `%s joined` | `%s a rejoint` | keep %s |
| 4448 | `%s left` | `%s est parti` | keep %s |
| 4458 | `%s quit (%s)` | `%s a quitté (%s)` | keep %s |
| 4460 | `%s quit` | `%s a quitté` | keep %s |
| 4472 | `%s is now %s` | `%s s'appelle désormais %s` | keep %s |
| 4495 | `Connection lost. Reconnecting...` | `Connexion perdue. Reconnexion...` |  |
| 4508 | `Ping timeout. Reconnecting...` | `Ping expiré. Reconnexion...` |  |
| 4541 | `Not in a channel` | `Pas dans un salon` |  |
| 4580 | `Not in a channel. Use /join #channel` | `Pas dans un salon. Utilisez /join #canal` |  |
| 4600 | `IRC Setup` | `Configuration IRC` |  |
| 4607 | `Server:` | `Serveur :` |  |
| 4607 | `Port:` | `Port :` |  |
| 4607 | `Nick:` | `Pseudo :` |  |
| 4607 | `Channel:` | `Salon :` |  |
| 4607, 4627 | `[ Connect ]` | `[ Connexion ]` |  |
| 4608 | `(none)` | `(aucun)` |  |
| 4627 | `> Connect <` | `> Connexion <` |  |
| 4654 | `Connected & joined` | `Connecté, dans le salon` |  |
| 4654 | `Connected...` | `Connecté...` |  |
| 4709 | `No network! Connect WiFi first.` | `Pas de réseau ! Connectez le WiFi.` |  |
| 4759 | `DISCONN` | `DÉCONN.` |  |
| 4763 | `joining` | `connexion` |  |
| 4795 | `Ent:Send Del:Exit` | `Ent:Envoyer Del:Quitter` |  |
| 4799 | `Ent:Msg W/S:Scrl Sh+Del:Bk` | `Ent:Msg W/S:Déf. Sh+Del:Ret` |  |
| 4814 | `No messages yet...` | `Aucun message...` |  |
| 5077 | `Connecting to WiFi...` | `Connexion au WiFi...` |  |

### Phone and SMS

Source: `examples/companion_radio/ui-new/SMSScreen.h`. T-Deck Pro 4G builds and all T-Deck Max builds. On the 40 MHz builds this screen can't be opened from the keyboard or the home screen.

| Line | English | French | Notes |
| --- | --- | --- | --- |
| 401 | `Phone & SMS` | `Téléphone et SMS` |  |
| 419 | `Dial` | `Composer` |  |
| 428, 653 | `Call Log` | `Journal d'appels` |  |
| 454, 786 | `SMS Inbox` | `SMS reçus` |  |
| 473 | `Please wait...` | `Veuillez patienter...` |  |
| 477, 806 | `Modem: %s` | `Modem : %s` | keep %s |
| 482 | `Ready!` | `Prêt !` |  |
| 492 | `Q:Back` | `Q:Retour` |  |
| 493, 1332 | `Ent:Open` | `Ent:Ouvrir` |  |
| 518 | `DEL` | `EFF` | dial pad key |
| 518 | `CALL` | `APPEL` | dial pad key |
| 557 | `Dial Number` | `Composer un numéro` |  |
| 630 | `Sh+Del:Bk` | `S+D:Ret` |  |
| 632 | `Ent:Call` | `Ent:Appel` |  |
| 665 | `No calls` | `Aucun appel` |  |
| 692 | `Unknown` | `Inconnu` |  |
| 703 | `Missed` | `Manqué` |  |
| 704 | `In` | `Reçu` |  |
| 704 | `Out` | `Émis` |  |
| 732, 866, 1149, 1328 | `Q:Back` | `Q:Ret` |  |
| 733 | `D:Del` | `D:Suppr` |  |
| 736 | `Ent:Dial` | `Ent:Appel` |  |
| 798 | `No conversations` | `Aucune conversation` |  |
| 800 | `Press C for new SMS` | `C pour un nouveau SMS` |  |
| 870 | `C:New` | `C:Écrire` |  |
| 897 | `No messages` | `Aucun message` |  |
| 931 | `%lum` | `%lumin` | keep %lu |
| 933 | `%lud` | `%luj` | keep %lu |
| 980 | `Q:Bk A:Add Contact` | `Q:Ret A:+Contact` |  |
| 981 | `C:Reply` | `C:Rép.` |  |
| 995 | `To: ` | `À : ` | ends with a space |
| 1004 | `To: %s` | `À : %s` | keep %s |
| 1050 | `Phone#` | `Numéro` |  |
| 1088 | `New Contact` | `Nouveau contact` |  |
| 1103 | `Name: %s_` | `Nom : %s_` | keep %s |
| 1105 | `Name: %s` | `Nom : %s` | keep %s |
| 1115 | `Number: %s_` | `Numéro : %s_` | keep %s |
| 1117 | `Number: %s` | `Numéro : %s` | keep %s |
| 1126 | `Save` | `Enregistrer` |  |
| 1133 | `A number is required` | `Un numéro est requis` |  |
| 1144 | `Sh+Del:Cancel` | `Sh+Del:Annuler` |  |
| 1145 | `Ent:Done` | `Ent:OK` |  |
| 1151 | `F:Dial D:Del` | `F:Appel D:Sup` |  |
| 1155 | `Ent:Sel` | `Ent:Sél` |  |
| 1268 | `SMS Contacts` | `Contacts SMS` |  |
| 1279 | `No contacts saved` | `Aucun contact enregistré` |  |
| 1281 | `Press A to add one` | `A pour en ajouter un` |  |
| 1329 | `A:Add` | `A:Ajouter` |  |
| 1344 | `Add Contact` | `Ajouter un contact` |  |
| 1344 | `Edit Contact` | `Modifier le contact` |  |
| 1353 | `Phone: ` | `Numéro : ` | ends with a space |
| 1359 | `Name: ` | `Nom : ` | ends with a space |
| 1373 | `Ent:Save` | `Ent:Enreg.` |  |
| 1389 | `Calling` | `Appel` |  |
| 1418 | `Dialing` | `Numérotation` |  |
| 1430 | `Ent/Sh+Del:Hang up` | `Ent/Sh+Del:Raccrocher` |  |
| 1444 | `Incoming Call` | `Appel entrant` |  |
| 1473 | `Ringing` | `Sonnerie` |  |
| 1485 | `Ent:Answer` | `Ent:Répondre` |  |
| 1486 | `Sh+Del:Reject` | `Sh+Del:Rejet` |  |
| 1503 | `In Call` | `En appel` |  |
| 1554 | `Ent:Hang  W/S:Vol 0-9:DTMF` | `Ent:Racc.  W/S:Vol 0-9:DTMF` | double space |