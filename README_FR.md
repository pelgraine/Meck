> **Remarque :** Meck est développé par une personne qui ne parle pas français. Cette version française du README a été entièrement traduite par une IA. Si vous souhaitez proposer d'autres traductions, merci de les envoyer sur le [canal Meck du Discord MeshCore](https://discord.com/channels/1495203904898728149/1496789639556501614) ou sur la [page Issues du dépôt](https://github.com/pelgraine/Meck/issues).
>
> **Note:** The developer of Meck does not speak French. This French version of the README is entirely AI-translated. Please submit suggested alternative translations to the [Meck channel on the MeshCore Discord](https://discord.com/channels/1495203904898728149/1496789639556501614) or to the [issues page on the repo](https://github.com/pelgraine/Meck/issues).

🇬🇧[English version / Version anglaise](README.md)🇬🇧

## Meshcore + Fork = Meck

Un fork créé spécifiquement pour proposer un firmware compagnon BLE et WiFi pour le LilyGo T-Deck Pro et le LilyGo T-Deck Max. Entièrement créé avec Claude AI à partir du code de Meshcore v1.11. 100 % vibecodé.

[Découvrez le canal de discussion Meck sur le Discord MeshCore](https://discord.com/channels/1495203904898728149/1496789639556501614)

<img src="https://github.com/user-attachments/assets/2cf48a91-807c-4e45-8260-291e637d9868" alt="IMG_4500-EDIT" width="325" height="650">

### Sommaire
- [Appareils pris en charge](#appareils-pris-en-charge)
- [Carte SD requise](#carte-sd-requise)
- [Flasher le firmware](#flasher-le-firmware)
  - [Premier flashage (firmware fusionné)](#premier-flashage-firmware-fusionné)
  - [Mettre à jour le firmware](#mettre-à-jour-le-firmware)
  - [Launcher](#launcher)
  - [Mise à jour du firmware par OTA (v1.3+)](#mise-à-jour-du-firmware-par-ota-v13)
- [Mode de hash de chemin (v0.9.9+)](#mode-de-hash-de-chemin-v099)
- [Portée de région (v1.7+)](#portée-de-région-v17)
- [T-Deck Pro](#t-deck-pro)
  - [Variantes de build du T-Deck Pro](#variantes-de-build-du-t-deck-pro)
    - [Puces tactiles et builds HYN](#puces-tactiles-et-builds-hyn)
    - [Builds économie de batterie à 40 MHz](#builds-économie-de-batterie-à-40-mhz)
  - [Commandes clavier du T-Deck Pro](#commandes-clavier-du-t-deck-pro)
  - [Navigation (écran d'accueil)](#navigation-écran-daccueil)
  - [Bluetooth (BLE)](#bluetooth-ble)
  - [Compagnon WiFi](#compagnon-wifi)
  - [Build combiné Bluetooth + WiFi (T-Deck Max)](#build-combiné-bluetooth--wifi-t-deck-max)
  - [Horloge et fuseau horaire](#horloge-et-fuseau-horaire)
  - [Fuseaux horaires (horloge mondiale) (v1.15+)](#fuseaux-horaires-horloge-mondiale-v115)
  - [Écran des messages de canal](#écran-des-messages-de-canal)
  - [Sélecteur de canal](#sélecteur-de-canal)
  - [Écran des contacts](#écran-des-contacts)
  - [Envoyer un message privé](#envoyer-un-message-privé)
  - [Room servers](#room-servers)
  - [Écran d'administration des répéteurs](#écran-dadministration-des-répéteurs)
  - [Écran de trace de route (v1.9+)](#écran-de-trace-de-route-v19)
  - [Journal RX (Rx Log)](#journal-rx-rx-log)
  - [Supprimer l'historique des messages (v1.10+)](#supprimer-lhistorique-des-messages-v110)
  - [Préférences de notification par canal (v1.10+)](#préférences-de-notification-par-canal-v110)
  - [Sons de notification personnalisés (v1.10+)](#sons-de-notification-personnalisés-v110)
  - [Jeux (v1.10+)](#jeux-v110)
    - [Émulateur Game Boy / Game Boy Color (v1.14+)](#émulateur-game-boy--game-boy-color-v114)
  - [Canaux privés (v1.11+)](#canaux-privés-v111)
  - [Partage de canal par MP (v1.11+)](#partage-de-canal-par-mp-v111)
  - [Export/import de la configuration (v1.11+)](#exportimport-de-la-configuration-v111)
  - [Écran des paramètres](#écran-des-paramètres)
  - [Fonctions expérimentales (v1.15+)](#fonctions-expérimentales-v115)
  - [Polices](#polices)
  - [Mode rédaction](#mode-rédaction)
  - [Saisie des symboles (touche Sym)](#saisie-des-symboles-touche-sym)
  - [Sélecteur d'emoji](#sélecteur-demoji)
  - [Appli SMS et téléphone (Pro 4G et Max uniquement)](#appli-sms-et-téléphone-pro-4g-et-max-uniquement)
  - [Navigateur web et IRC](#navigateur-web-et-irc)
  - [Réveil (Pro Audio et Max uniquement)](#réveil-pro-audio-et-max-uniquement)
  - [Messages vocaux par LoRa (Pro Audio et Max uniquement)](#messages-vocaux-par-lora-pro-audio-et-max-uniquement)
  - [Écran de verrouillage (T-Deck Pro et Max)](#écran-de-verrouillage-t-deck-pro-et-max)
  - [Extinction (T-Deck Pro et Max)](#extinction-t-deck-pro-et-max)
- [T-Deck Max](#t-deck-max)
  - [Variantes de build du T-Deck Max](#variantes-de-build-du-t-deck-max)
  - [4G et audio en même temps](#4g-et-audio-en-même-temps)
  - [Antenne (interne / externe)](#antenne-interne--externe)
  - [Notifications par vibreur (Buzzer)](#notifications-par-vibreur-buzzer)
  - [Écran tactile capacitif et boutons](#écran-tactile-capacitif-et-boutons)
  - [Messages prédéfinis (T-Deck Max)](#messages-prédéfinis-t-deck-max)
  - [Éclairage frontal et luminosité du rétroéclairage](#éclairage-frontal-et-luminosité-du-rétroéclairage)
  - [Rétroéclairage du clavier](#rétroéclairage-du-clavier)
  - [GPS multi-constellation](#gps-multi-constellation)
- [Répéteur distant (T-Deck Pro 4G)](#répéteur-distant-t-deck-pro-4g)
- [Répéteur WiFi](#répéteur-wifi)
- [Paramètres série (USB)](Serial%20Settings%20Guide.md)
- [Lecteur de texte et EPUB](TXT%20%26%20EPUB%20Reader%20Guide.md)
- [Guide du navigateur web et IRC](Web%20App%20Guide.md)
- [Guide de l'appli SMS et téléphone](SMS%20%26%20Phone%20App%20Guide.md)
- [Guide du lecteur de livres audio](Audiobook%20Player%20Guide.md)
- [Application web Meck-Mycelium](#application-web-meck-mycelium)
- [À propos de MeshCore](#à-propos-de-meshcore)
- [Qu'est-ce que MeshCore ?](#quest-ce-que-meshcore-)
- [Fonctionnalités principales](#fonctionnalités-principales)
- [À quoi peut servir MeshCore ?](#à-quoi-peut-servir-meshcore-)
- [Pour commencer](#pour-commencer)
- [Clients MeshCore](#clients-meshcore)
- [Compatibilité matérielle](#-compatibilité-matérielle)
- [Contribuer](#contribuer)
- [Feuille de route / À faire](#feuille-de-route--à-faire)
- [Obtenir de l'aide](#-obtenir-de-laide)
- [Licence](#-licence)
  - [Bibliothèques tierces](#bibliothèques-tierces)

---

## Appareils pris en charge

Meck cible actuellement deux appareils LilyGo et prend aussi en charge les Heltec V3 et V4 en tant que répéteurs distants :

| Appareil | Écran | Saisie | LoRa | Batterie | GPS | RTC |
|--------|---------|-------|------|---------|-----|-----|
| **T-Deck Pro** | e-ink 240×320 (GxEPD2) | Clavier TCA8418 + tactile en option | SX1262 | Jauge de batterie BQ27220, 1 400 mAh | Oui | Non (utilise l'heure GPS) |
| **T-Deck Max** | e-ink 240×320 (GxEPD2) + éclairage frontal | Clavier TCA8418 + tactile capacitif CST328 + 3 boutons capacitifs | SX1262 | Jauge de batterie BQ27220, 1 400 mAh | Oui (multi-constellation) | Non (utilise l'heure GPS) |
| **Heltec V3** (répéteur distant uniquement) | OLED 0,96" (SSD1306) | — | SX1262 | — | Non | Non |
| **Heltec V4** (répéteur distant uniquement) | OLED 0,96" (SSD1306) | — | SX1262 | — | Non | Non |

Le T-Deck Pro et le T-Deck Max utilisent l'ESP32-S3 avec 16 Mo de flash et 8 Mo de PSRAM. Les Heltec V3 et V4 utilisent l'ESP32-S3 avec 8 Mo de flash et 8 Mo de PSRAM.

---

## Carte SD requise

**Une carte SD est indispensable au bon fonctionnement de Meck.** De nombreuses fonctions (notamment le lecteur de livres numériques, les notes, les signets, le cache du lecteur web, la lecture des livres audio, les mises à jour du firmware, l'import/export des contacts, l'enregistrement des identifiants WiFi, ainsi que les ROM et sauvegardes Game Boy) reposent sur des fichiers stockés sur la carte SD. Sans carte SD insérée, l'appareil démarre et gère la messagerie mesh, mais la plupart des fonctions avancées seront indisponibles ou échoueront sans message d'erreur.

**Recommandé :** une carte microSD de **32 Go ou plus** formatée en **FAT32**. Les nombreuses fonctions de Meck (livres audio, livres numériques, enregistrements vocaux, exports de contacts, sons d'alarme, cache du lecteur web, notes et images de firmware) peuvent occuper beaucoup d'espace avec le temps, une carte plus grande vaut donc la peine. Les utilisateurs de MeshCore ont constaté que les cartes microSD **SanDisk** sont les plus fiables sur le T-Deck Pro.

---

## Flasher le firmware

Téléchargez le dernier firmware sur la page [Releases](https://github.com/pelgraine/Meck/releases). Chaque version comprend deux types de fichiers `.bin` pour chaque variante de build :

| Type de fichier | Quand l'utiliser |
|-----------|-------------|
| `*-merged.bin` | **Premier flashage** : contient le bootloader, la table de partitions et le firmware dans un seul fichier. À flasher à l'adresse `0x0`. |
| `*.bin` (non fusionné) | **Mise à jour d'un firmware existant** : image du firmware seule. Sert aussi pour charger le firmware depuis une carte SD via le Launcher. |

### Premier flashage (firmware fusionné)

Si l'appareil n'a jamais reçu le firmware Meck (ou si vous voulez repartir de zéro), utilisez le fichier `.bin` **fusionné**. Il contient le bootloader, la table de partitions et le firmware de l'application réunis dans une seule image.

**Avec esptool.py :**

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0 meck_max_standalone-merged.bin
```

Sur macOS, le port est généralement `/dev/cu.usbmodem*`. Sur Windows, ce sera un port COM comme `COM3`.

**Avec le MeshCore Flasher (en ligne, T-Deck Pro et T-Deck Max) :**

1. Allez sur https://flasher.meshcore.io
2. Sélectionnez **Custom Firmware**
3. Sélectionnez le fichier `.bin` **fusionné** que vous avez téléchargé
4. Cliquez sur **Flash**, sélectionnez votre appareil dans la fenêtre qui s'affiche, puis cliquez sur **Connect**

> **Remarque :** le MeshCore Flasher reconnaît un firmware fusionné grâce au suffixe `-merged.bin` dans le nom du fichier et le flashe automatiquement à l'adresse `0x0`. Si le nom du fichier ne se termine pas par `-merged.bin`, le flasher écrit à l'adresse `0x10000`, ce qui échouera sur un appareil vierge.

### Mettre à jour le firmware

Si l'appareil exécute déjà Meck (ou tout firmware basé sur MeshCore avec un bootloader valide), utilisez le fichier `.bin` **non fusionné**. Il est plus petit et plus rapide à flasher, car il ne contient que le firmware de l'application.

**Avec esptool.py :**

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x10000 meck_max_standalone.bin
```

> **Astuce :** si vous ne savez pas si l'appareil a déjà un bootloader, vous pouvez toujours utiliser sans risque le fichier fusionné et le flasher à `0x0` : il écrasera proprement l'ensemble.

> **Premier démarrage :** après un flashage neuf, l'appareil formate sa partition de stockage interne. L'écran affiche "Formatting storage... First boot - please wait" : cela prend 1 à 2 minutes et ne se produit qu'une seule fois. Si l'appareil exécutait auparavant un autre firmware (par exemple le firmware d'origine LilyGo ou Meshtastic), la partition est automatiquement effacée et reformatée pour garantir un démarrage propre.

### Launcher

Si vous chargez le firmware depuis une carte SD via le firmware LilyGo Launcher, utilisez le fichier `.bin` **non fusionné**. Le Launcher fournit son propre bootloader et n'a besoin que de l'image de l'application.

### Mise à jour du firmware par OTA (v1.3+)

Une fois Meck installé, vous pouvez mettre à jour le firmware directement depuis votre téléphone, sans ordinateur ni câble série. L'appareil crée un point d'accès WiFi temporaire et vous envoyez le nouveau `.bin` depuis le navigateur de votre téléphone.

1. Téléchargez le nouveau `.bin` **non fusionné** sur votre téléphone (depuis les Releases GitHub, Discord, etc.)
2. Sur l'appareil : **Paramètres → Outils OTA → Mise à jour du firmware → Entrée** (Settings → OTA Tools → Firmware Update → Enter) (T-Deck Pro et T-Deck Max)
3. L'appareil lance un réseau WiFi nommé `Meck-Update-XXXX` et affiche les informations de connexion
4. Sur votre téléphone : connectez-vous au réseau WiFi `Meck-Update`, ouvrez un navigateur, allez à l'adresse `192.168.4.1`
5. Touchez **Choose File**, sélectionnez le `.bin`, touchez **Upload**
6. L'appareil reçoit le fichier, l'enregistre sur la SD, le vérifie, le flashe et redémarre

La table des partitions prend en charge deux emplacements OTA : l'ancien firmware reste sur la partition inactive et sert de cible de retour automatique. Si le nouveau firmware ne parvient pas à démarrer, le bootloader de l'ESP32 revient automatiquement à la version fonctionnelle précédente.

> **Remarque :** utilisez le `.bin` **non fusionné** pour les mises à jour OTA. Le binaire fusionné n'est nécessaire que pour le premier flashage par USB.

**Outils OTA (v1.5+) :** la mise à jour du firmware se trouve désormais dans **Paramètres → Outils OTA**, un sous-menu qui contient aussi le nouveau **Gestionnaire de fichiers SD** (SD File Manager). Le gestionnaire de fichiers crée le même point d'accès WiFi et propose une interface dans le navigateur où vous pouvez parcourir, envoyer, télécharger et supprimer les fichiers de la carte SD depuis votre téléphone : pratique pour gérer les livres audio, les sons d'alarme, les livres numériques et les notes sans retirer la carte SD. Les deux outils OTA fonctionnent sur toutes les variantes, y compris les builds standalone, sauf les [builds économie de batterie à 40 MHz](#builds-économie-de-batterie-à-40-mhz), où la ligne Outils OTA ne fait rien.

---

## Mode de hash de chemin (v0.9.9+)

Meck prend en charge le hash de chemin multi-octets, ce qui l'aligne sur le firmware MeshCore v1.14. Le hash de chemin détermine le nombre d'octets que chaque répéteur utilise pour s'identifier dans les paquets flood retransmis. Des hashs plus grands réduisent le risque de collisions d'identité, au prix d'un nombre maximal de sauts par paquet plus faible.

Vous pouvez configurer la taille du hash de chemin dans les paramètres de l'appareil (appuyez sur **S** depuis l'écran d'accueil sur le T-Deck Pro et le T-Deck Max) ou la définir via le port série USB :

```
set path.hash.mode 1
```

| Mode | Octets par saut | Sauts max | Remarques |
|------|--------------|----------|-------|
| 0 | 1 | 64 | Ancien mode : sujet aux collisions de hash dans les grands réseaux |
| 1 | 2 | 32 | Recommandé : élimine en pratique les collisions |
| 2 | 3 | 21 | Précision maximale, rarement nécessaire |

Des nœuds utilisant des modes de hash de chemin différents peuvent coexister sur le même réseau. Le mode ne concerne que les paquets émis par votre nœud : la taille du hash est encodée dans l'en-tête de chaque paquet, donc les nœuds qui les reçoivent s'adaptent automatiquement.

Pour une explication détaillée de ce qu'est le hash de chemin multi-octets et de son importance, consultez l'[article Path Diagnostics & Improvements](https://buymeacoffee.com/ripplebiz/path-diagnostics-improvements).

---

## Portée de région (v1.7+)

Les régions limitent la distance de propagation de vos messages flood dans le mesh. Lorsque vous définissez une région, les messages sortants sont marqués d'un code de transport que les répéteurs utilisent pour décider s'ils doivent les retransmettre. Les messages envoyés sans région atteignent tous les répéteurs via le joker par défaut, comme toujours.

Meck ne prédéfinit aucune région après un flashage neuf. Les noms de région sont fixés par votre communauté mesh locale : renseignez-vous auprès de votre groupe local pour connaître les noms utilisés. Les conventions courantes suivent les codes de pays/subdivisions ISO 3166 (par exemple `au` pour l'Australie, `gb-eng` pour l'Angleterre, `us-ca` pour la Californie), mais les communautés peuvent aussi utiliser des noms personnalisés pour leur zone. Les noms de région ne peuvent contenir que des caractères alphanumériques en minuscules et des tirets, 29 caractères au maximum.

**Région par défaut de l'appareil :** à définir dans **Paramètres → Région déf.** (Settings → Default Region). Elle s'applique à tous les canaux et MP, sauf si un canal a son propre réglage.

**Région par canal :** dans Paramètres → Canaux (Settings → Channels), sélectionnez un canal et appuyez sur Entrée pour modifier sa portée de région. Ce réglage remplace la valeur par défaut de l'appareil pour ce canal uniquement.

**Portée de l'application compagnon (v1.15+) :** lorsqu'une application compagnon est connectée et a choisi une région (ou aucune région) pour l'envoi, les messages de canal et les messages privés utilisent en priorité le choix de l'application. Auparavant, les messages privés utilisaient toujours la région par défaut de l'appareil.

**Rappel dans les paramètres :** en quittant les paramètres, si aucune région n'est configurée nulle part (ni région par défaut de l'appareil, ni portée par canal), un message vous invite à envisager d'en définir une. Vous pouvez l'ignorer pour rester sans portée.

La portée de région peut aussi être configurée par commandes série : consultez le [guide des paramètres série](Serial%20Settings%20Guide.md) pour les commandes `set region`, `get region`, `set channel.scope` et `get channel.scope`.

---

## T-Deck Pro

### Variantes de build du T-Deck Pro

| Variante | Environnement | BLE | WiFi | Modem 4G | DAC audio | Lecteur web | Contacts max |
|---------|------------|-----|------|----------|-----------|------------|-------------|
| Audio + BLE | `meck_audio_ble` | Oui | Oui (lecteur web uniquement) | — | PCM5102A | Oui | 2 000 |
| Audio + BLE, tactile HYN | `meck_audio_ble_hyn` | Oui | Oui (lecteur web uniquement) | — | PCM5102A | Oui | 2 000 |
| Audio + WiFi | `meck_audio_wifi` | — | Oui (TCP:5000) | — | PCM5102A | Oui | 2 000 |
| Audio + WiFi, tactile HYN | `meck_audio_wifi_hyn` | — | Oui (TCP:5000) | — | PCM5102A | Oui | 2 000 |
| Audio + autonome | `meck_audio_standalone` | — | — | — | PCM5102A | Non | 2 000 |
| Audio + autonome, tactile HYN | `meck_audio_standalone_hyn` | — | — | — | PCM5102A | Non | 2 000 |
| Audio + autonome, 40 MHz | `meck_audio_standalone_40mhz` | — | — | — | PCM5102A (fonctions audio désactivées) | Non | 2 000 |
| 4G + BLE | `meck_4g_ble` | Oui | Oui | A7682E | — | Oui | 2 000 |
| 4G + WiFi | `meck_4g_wifi` | — | Oui (TCP:5000) | A7682E | — | Oui | 2 000 |
| 4G + autonome | `meck_4g_standalone` | — | Oui | A7682E | — | Oui | 2 000 |
| 4G + autonome, 40 MHz | `meck_4g_standalone_40mhz` | — | — | A7682E (éteint au démarrage) | — | Non | 2 000 |
| Répéteur distant (4G) | `meck_remote_repeater` | — | — | A7682E (MQTT) | — | Non | — |
| Répéteur WiFi | `meck_wifi_repeater` | — | Oui (MQTT) | — | — | Non | — |

Le DAC audio et le modem 4G occupent le même emplacement matériel et s'excluent mutuellement. (Le T-Deck Max lève cette restriction : il fait fonctionner les deux en même temps. Voir [T-Deck Max](#t-deck-max).) Les variantes répéteur distant et répéteur WiFi fonctionnent comme des répéteurs MeshCore dédiés : elles relaient le trafic du réseau maillé et répondent normalement aux connexions invité, mais **l'administration se fait à distance via MQTT** depuis le [tableau de bord Meck-Mycelium](https://pelgraine.github.io/Meck-Mycelium), et non via la connexion habituelle par mot de passe administrateur sur le réseau maillé. Voir [Répéteur distant](#répéteur-distant-t-deck-pro-4g) et [Répéteur WiFi](#répéteur-wifi) ci-dessous.

#### Puces tactiles et builds HYN

Les T-Deck Pro v1.1 étaient livrés avec une puce tactile CST328, mais LilyGo a ensuite monté à la place une CST3530 sur des appareils toujours étiquetés v1.1. Sur ces appareils, l'écran tactile ne fonctionne pas avec les builds standard. Ils ont besoin des builds HYN (`meck_audio_ble_hyn`, `meck_audio_wifi_hyn` et `meck_audio_standalone_hyn`), qui utilisent le même pilote tactile Hynitron que le T-Deck Max. Ce pilote fonctionne aussi avec la CST328, mais les builds standard restent ceux par défaut. Il n'existe pas de version HYN des builds 4G.

#### Builds économie de batterie à 40 MHz

`meck_audio_standalone_40mhz`, `meck_4g_standalone_40mhz` ainsi que `meck_max_standalone_40mhz` du T-Deck Max maintiennent le processeur à 40 MHz pour économiser la batterie. Les fonctions qui demandent plus de vitesse, le WiFi, le GPS ou le matériel audio sont désactivées sur ces builds :

- le navigateur web et les Outils OTA (OTA Tools)
- le GPS (la page d'accueil GPS est masquée et le GPS reste éteint) et la carte
- les livres audio, le réveil (les alarmes ne sonnent pas), les messages vocaux et les sons de notification personnalisés (le buzzer par défaut sonne à la place)
- l'appli SMS et téléphone (le modem 4G est éteint au démarrage)
- l'émulateur Game Boy (Snake et Démineur (Minesweeper) fonctionnent toujours)

Les tuiles de l'écran d'accueil correspondant à ces fonctions sont grisées et ne réagissent pas quand on appuie dessus.

### Commandes clavier du T-Deck Pro

Le firmware du T-Deck Pro prend entièrement en charge le clavier pour la messagerie autonome, sans téléphone.

### Navigation (écran d'accueil)

| Touche | Action |
|-----|--------|
| W / A | Page précédente |
| D | Page suivante |
| Entrée | Sélectionner / Confirmer |
| M | Ouvrir le sélecteur de canal |
| C | Ouvrir la liste des contacts |
| E | Ouvrir le lecteur de livres numériques |
| N | Ouvrir les notes |
| S | Ouvrir les paramètres |
| B | Ouvrir le navigateur web (variantes BLE, WiFi et 4G, mais pas audio autonome) |
| T | Ouvrir l'appli SMS et téléphone (Pro 4G et Max uniquement) |
| P | Ouvrir le lecteur de livres audio (Pro Audio et Max uniquement) |
| K | Ouvrir le réveil (Pro Audio et Max uniquement) |
| F | Ouvrir la découverte de nœuds (recherche des répéteurs/nœuds à proximité) |
| H | Ouvrir la liste des derniers nœuds entendus (historique passif des annonces) |
| R | Ouvrir l'écran de trace de route (v1.9+) : voir [Écran de trace de route](#écran-de-trace-de-route-v19) |
| J | Ouvrir le menu des jeux (v1.10+) : voir [Jeux](#jeux-v110) |
| G | Ouvrir l'écran de carte (affiche les contacts ayant une position GPS) |
| Mic | Ouvrir les messages vocaux (Pro Audio et Max uniquement) |
| Q | Retour à l'écran d'accueil |
| Double-clic sur Boot | Verrouiller / déverrouiller l'écran |

**Écran d'accueil à tuiles tactiles :** la première page d'accueil est une grille de tuiles (Messages, Contacts, Cartes (Map), Notes, Paramètres (Settings) et les autres) sur lesquelles vous pouvez appuyer directement au lieu d'utiliser les raccourcis clavier ci-dessus. D'abord propre au T-Deck Max, c'est désormais l'écran d'accueil de **tous les builds T-Deck Pro**. Les tuiles des fonctions absentes d'un build donné (par exemple Téléphone (Phone) sur les builds audio, ou Livres audio (Audiobooks) sur les builds 4G) sont inactives.

### Bluetooth (BLE)

Le BLE est **désactivé par défaut** au démarrage, pour privilégier un usage autonome. L'appareil est entièrement fonctionnel sans téléphone : vous pouvez envoyer et recevoir des messages, parcourir les contacts, lire des livres numériques et régler votre fuseau horaire directement depuis le clavier.

Pour vous connecter à l'appli compagnon MeshCore, allez à la page d'accueil **Bluetooth** (utilisez D pour faire défiler les pages) et appuyez sur **Entrée** pour activer le BLE. Le code PIN BLE s'affiche à l'écran. Désactivez-le de la même façon lorsque vous avez terminé.

### Compagnon WiFi

Les variantes compagnon WiFi (`meck_audio_wifi`, `meck_audio_wifi_hyn`, `meck_4g_wifi` et `meck_max_wifi` pour le T-Deck Max) se connectent à l'appli web MeshCore, à meshcore.js ou à la CLI Python via votre réseau local, en TCP sur le port 5000. Les identifiants WiFi sont enregistrés sur la carte SD dans `/web/wifi.cfg`.

**Connexion :**

Sur ces builds, le WiFi est activé au démarrage (même si vous l'aviez désactivé avant de redémarrer), et l'appareil essaie de rejoindre votre réseau enregistré pendant le démarrage. Le build combiné `meck_max_ble_wifi` est différent : le WiFi y est désactivé au démarrage (voir [Build combiné Bluetooth + WiFi](#build-combiné-bluetooth--wifi-t-deck-max)).

Pour rejoindre un réseau pour la première fois, ou pour changer de réseau :

1. Ouvrez **Paramètres** (Settings). Si la ligne **Radio WiFi** (WiFi Radio) affiche **NON** (OFF), sélectionnez-la pour la passer à **OUI** (ON). Il faut plusieurs secondes avant que la ligne affiche OUI, merci de patienter.
2. Sélectionnez la ligne **WiFi** (elle affiche **WiFi : (non connecté)** (WiFi: (not connected)) tant que vous n'avez pas rejoint de réseau). L'appareil recherche les réseaux.
3. Sélectionnez votre réseau, saisissez le mot de passe et appuyez sur **Entrée**.
4. Une fenêtre **Connexion...** (Connecting...) s'affiche pendant que l'appareil rejoint le réseau. Cela peut prendre jusqu'à 15 secondes.
5. Une fois la connexion établie, une fenêtre affiche l'adresse IP de l'appareil pendant 2 secondes, puis vous revenez à Paramètres. L'adresse IP s'affiche aussi sur la page d'accueil WiFi.
6. Si l'appareil ne parvient pas à se connecter en 15 secondes, une fenêtre **Connexion impossible** (Could not connect) s'affiche et vous revenez à la liste des réseaux pour réessayer. La deuxième ligne de la fenêtre en indique la raison, par exemple **Mot de passe erroné ?** (Wrong password?), **Réseau introuvable** (Network not found), **Signal perdu** (Signal lost) ou **Délai dépassé** (Timed out).

**Désactiver et activer le WiFi (v1.15+) :** sur la page d'accueil **WiFi**, appuyez sur **Entrée** (ou faites un appui long) pour couper ou activer la radio WiFi, comme avec la ligne **Radio WiFi** dans Paramètres. Le bas de la page affiche **Entrée : couper le WiFi** (Press Enter to Turn Off Wifi) ou **Entrée : activer le WiFi** (Press Enter to Turn On Wifi). La configuration réseau et les mots de passe restent dans Paramètres.

Activer le WiFi (depuis la page d'accueil ou la ligne **Radio WiFi**) permet de rejoindre votre réseau enregistré, si vous en avez un. Une fenêtre **Connexion au WiFi :** (Connecting to Saved Wifi:) affiche le nom du réseau, suivi de **Connecté** (Connected) et de l'adresse IP, ou de **Connexion impossible** et de la raison.

Connectez l'appli web MeshCore ou meshcore.js à `<device IP>:5000`.

Sur les variantes WiFi, le WiFi sert aussi au lecteur web et au client IRC. Le lecteur web utilise la même connexion : aucune configuration supplémentaire n'est nécessaire.

### Build combiné Bluetooth + WiFi (T-Deck Max)

Le build combiné BLE et WiFi n'est disponible que pour le T-Deck Max en v1.15. Le build combiné pour le T-Deck Pro sera proposé dans une version ultérieure.

Le build `meck_max_ble_wifi` propose à la fois la connexion compagnon Bluetooth et la connexion compagnon WiFi, une seule à la fois. Les deux sont désactivées au démarrage. Après un redémarrage, celle que vous activez en premier fonctionne immédiatement : depuis sa page d'accueil (appuyez sur **Entrée** ou faites un appui long), ou, pour le WiFi, depuis **Paramètres** (Settings).

**Passer du Bluetooth au WiFi, ou l'inverse, redémarre l'appareil, à chaque fois.** Le Bluetooth ne libère sa mémoire qu'au redémarrage de l'appareil, et le WiFi a besoin de cette mémoire pour démarrer. Donc, une fois que vous avez utilisé une connexion depuis le dernier redémarrage, activer l'autre se passe ainsi :

1. La page d'accueil de l'autre connexion affiche une indication de redémarrage : **Entrée : redémarrer en WiFi** (Press Enter to Restart into Wifi) sur la page WiFi, ou **redémarrer en Bluetooth** (restart into Bluetooth) sur la page Bluetooth.
2. Appuyez sur **Entrée** (ou faites un appui long). Une fenêtre demande **Passer au WiFi ?** (Switch to WiFi?) (ou **Passer au Bluetooth ?** (Switch to Bluetooth?)). Appuyez de nouveau sur **Entrée** dans les 5 secondes pour redémarrer.
3. L'appareil redémarre et active de lui-même la connexion choisie. Le WiFi rejoint votre réseau enregistré, avec la fenêtre Connexion (Connecting).

Après un redémarrage normal ou une mise sous tension, les deux connexions sont de nouveau désactivées.

Depuis l'appli compagnon, l'activation ou la désactivation du WiFi (`set wifi.enabled`) est appliquée de la même façon, par le redémarrage que lance l'appli.

### Horloge et fuseau horaire

Le T-Deck Pro et le T-Deck Max n'ont pas de puce RTC dédiée : après chaque redémarrage, l'horloge de l'appareil n'est donc pas réglée. L'heure apparaît dans la barre de navigation (entre le nom du nœud et la batterie) dès qu'elle a été synchronisée par l'une de ces méthodes :

1. **Fix GPS** (autonome) : dès que le GPS obtient un fix satellite, l'heure est automatiquement synchronisée à partir des données NMEA. Aucun téléphone ni aucune connexion BLE n'est nécessaire. Le premier fix prend en général de 30 à 90 secondes en extérieur, par ciel dégagé.
2. **Appli compagnon BLE/WiFi** : si l'appareil est connecté à l'appli compagnon MeshCore (en BLE ou en WiFi), l'appli envoie l'heure actuelle à l'appareil.

**Régler votre fuseau horaire :**

Le décalage UTC peut être réglé depuis l'écran **Paramètres** (Settings) (appuyez sur **S** depuis l'écran d'accueil), ou depuis la page d'accueil **GPS** en appuyant sur **U** pour ouvrir l'éditeur de décalage UTC.

| Touche | Action |
|-----|--------|
| W | Augmenter le décalage (+1 heure) |
| S | Diminuer le décalage (-1 heure) |
| Entrée | Enregistrer et quitter |
| Q | Annuler et quitter |

Le décalage UTC est enregistré en flash et conservé après un redémarrage : vous n'avez besoin de le régler qu'une fois. La plage valide va de UTC-12 à UTC+14. Par exemple, AEST correspond à UTC+10 et AEDT à UTC+11.

La page GPS affiche aussi l'heure actuelle, le nombre de satellites, la position, l'altitude et le décalage UTC configuré, pour référence.

### Fuseaux horaires (horloge mondiale) (v1.15+)

La dernière page d'accueil est une horloge mondiale à trois lignes : **Local** (Home), qui correspond au décalage UTC de votre appareil, et deux fuseaux supplémentaires, **Zone 1** et **Zone 2**. Chaque ligne affiche le décalage UTC du fuseau, son heure locale en grands chiffres, des codes de villes façon aéroport pour ce décalage (par exemple `BNE/POM/VLA` pour UTC+10), et un indicateur de jour comme `+1D` ou `-1D` lorsque le fuseau est sur un autre jour calendaire que Local.

Pour y accéder, appuyez sur **A** depuis la première page d'accueil (les tuiles), ou sur **D** depuis la page d'extinction. Tant que l'horloge n'a pas été réglée (par le GPS ou l'appli compagnon), la page affiche **Heure non réglée** (Clock not set).

| Touche | Action |
|-----|--------|
| W / S | Sélectionner Local, Zone 1 ou Zone 2 |
| Entrée | Modifier le décalage UTC de la ligne sélectionnée |
| W / S (en édition) | Augmenter / diminuer le décalage (UTC-12 à UTC+14) |
| Entrée (en édition) | Enregistrer |
| Q (en édition) | Annuler sans enregistrer |

Sur l'écran tactile, faites un appui long sur une ligne pour la modifier. L'indication affichée par l'éditeur montre les commandes tactiles (**Appui:-/+  Maintien:enreg.** (Tap:-/+  Hold:save)).

Modifier **Local** change le décalage UTC de l'appareil, le même réglage que **Paramètres → Décalage UTC** (Settings → UTC Offset). Zone 1 et Zone 2 sont enregistrées et conservées après un redémarrage.

### Écran des messages de canal

| Touche | Action |
|-----|--------|
| W / S | Faire défiler les messages vers le haut / le bas |
| Entrée | Rédiger un nouveau message |
| R | Répondre à un message : passez en mode sélection de réponse, faites défiler jusqu'à un message avec W/S, puis appuyez sur Entrée pour rédiger une réponse avec une @mention |
| V | Afficher le chemin de relais du dernier message reçu (défilable, jusqu'à 20 sauts) |
| Q | Retour au sélecteur de canal |

### Sélecteur de canal

Appuyer sur **M** depuis l'écran d'accueil ouvre le sélecteur de canal. Tous vos canaux et la boîte de réception des MP sont affichés dans une seule vue avec des badges de messages non lus, ce qui vous permet d'accéder directement à n'importe quel canal au lieu de les faire défiler un par un. Appuyer sur **Q** depuis la vue d'un canal (par exemple le fil Public) ramène aussi au sélecteur de canal.

| Touche | Action |
|-----|--------|
| W / S | Naviguer vers le haut / le bas |
| Entrée | Passer au canal sélectionné |
| X | Supprimer l'historique des messages du canal en surbrillance (v1.10+) |
| Q | Retour à l'écran d'accueil |

Appuyer sur **X** sur un canal en surbrillance affiche une fenêtre de confirmation. Appuyez sur **Entrée** pour confirmer la suppression ou sur **Q** pour annuler. Cela efface tous les messages enregistrés de ce canal dans le tampon circulaire et enregistre le résultat sur la SD. Le canal lui-même n'est pas supprimé, seulement son historique de messages.

**Compteurs de non-lus et appli compagnon (v1.12.3+) :** sur les builds autonomes (sans BLE ni WiFi), le compteur **MSG** de l'écran d'accueil et les badges `*N` de chaque canal augmentent à l'arrivée des messages, et ouvrir un canal remet son compteur à zéro. Sur les builds BLE et WiFi, les compteurs de l'appareil restent volontairement à zéro tant qu'une appli compagnon est connectée, car l'appli marque chaque message comme lu dès son arrivée ; ils reprennent le comptage dès que l'appli se déconnecte. Les firmwares précédents traitaient les builds autonomes comme si une appli compagnon était connectée en permanence, si bien que ces compteurs (ainsi que le compteur de MP et le réveil de l'écran à l'arrivée d'un nouveau message) ne se mettaient jamais à jour.

### Écran des contacts

Appuyez sur **C** depuis l'écran d'accueil pour ouvrir la liste des contacts. Tous les contacts mesh connus sont affichés, triés du plus récemment entendu au plus ancien, avec leur préfixe de type, leur nombre de sauts estimé et le temps écoulé depuis leur dernière annonce.

**Préfixes de type de contact**

| Préfixe | Type |
|--------|------|
| C | Nœud de chat |
| R | Répéteur |
| RS | Room server |
| ? | Inconnu / capteur |

**Affichage du nombre de sauts**

| Affichage | Signification |
|---------|---------|
| `D` | Chemin direct connu (échange de chemin effectué) |
| `D*` | Chemin direct, verrouillé manuellement |
| `N` | Chemin à N sauts connu (par ex. `2` = 2 sauts) |
| `N*` | Chemin à N sauts, verrouillé manuellement |
| `~D` | Entendu en direct via une annonce flood (pas encore d'échange de chemin) |
| `~N` | N sauts estimés via une annonce flood |
| `?` | Aucune information de chemin disponible |

Les estimations de sauts basées sur le flood (`~D`, `~N`) proviennent d'un cache contenant jusqu'à 1 000 annonces récemment entendues et reviennent à `?` au redémarrage, jusqu'à ce que chaque contact émette une nouvelle annonce. Les valeurs de chemin confirmées (`D`, `N`) sont conservées jusqu'à ce qu'un nouvel échange de chemin les remplace.

**Commandes en mode normal**

| Touche | Action |
|-----|--------|
| W / S | Faire défiler les contacts vers le haut / le bas |
| Shift+W / Shift+S | Page précédente / page suivante |
| A / D | Faire défiler les filtres : Tous (All) → Chat → Rép. (Rptr) → Salon (Room) → Capt. (Sens) → Fav |
| Entrée | Passer en mode sélection (met en surbrillance le contact actuel, active les opérations groupées) |
| P | Ouvrir l'éditeur de chemin pour le contact en surbrillance |
| Q | Retour à l'écran d'accueil |

> **Remarque :** Le filtre **Fav** n'affiche que les contacts que vous avez marqués comme favoris. S'il semble vide, c'est qu'aucun contact n'a encore été mis en favori : utilisez le mode sélection (Entrée) puis **F** pour marquer des contacts.

**Mode sélection** : appuyez sur Entrée depuis la liste des contacts pour passer en mode sélection. Le contact en surbrillance est présélectionné. Utilisez W/S pour faire défiler et Entrée pour sélectionner ou désélectionner n'importe quelle ligne.

| Touche | Action |
|-----|--------|
| W / S | Faire défiler vers le haut / le bas |
| Entrée | Sélectionner / désélectionner le contact actuel |
| A | Sélectionner tous les contacts du filtre actuel |
| D | Tout désélectionner |
| F | Ajouter aux favoris / retirer des favoris tous les contacts sélectionnés |
| X | Exporter les contacts sélectionnés sur la carte SD |
| Backspace | Supprimer les contacts sélectionnés |
| Q | Quitter le mode sélection |

**Ajouter des contacts**

Il existe trois façons d'ajouter des contacts :

1. **Automatiquement** : si Paramètres → Contacts → Ajout (Settings → Contacts → Add Mode) est réglé sur *Auto (tous)* (Auto All), tout nœud dont l'annonce est entendue est ajouté automatiquement. Le mode *Personnalisé* (Custom) n'ajoute que les nœuds correspondant aux interrupteurs de type activés (Compagnon (Companion), Répéteur (Repeater), Serveur salon (Room Server), Capteur (Sensor)) : chaque interrupteur détermine si la réception d'une annonce de ce type déclenche un ajout automatique. *Manuel seul* (Manual Only) désactive tout ajout automatique.

2. **Depuis l'écran Entendus** (Last Heard) : appuyez sur **H** depuis l'écran d'accueil pour ouvrir la liste des dernières annonces entendues. Faites défiler jusqu'au nœud voulu et appuyez sur **Entrée** (ou touchez la ligne) pour l'ajouter aux contacts. Appuyez de nouveau sur **Entrée** sur un contact existant pour le retirer (pour les favoris, une seconde pression dans les 3 secondes est nécessaire pour confirmer). Les entrées affichent `[+]` si elles figurent déjà dans les contacts, `[★]` s'il s'agit d'un favori.

   > **Remarque :** La liste Entendus contient jusqu'à 1 000 entrées en PSRAM, et les données d'annonce sont stockées de façon persistante sur la carte SD : vous pouvez donc ajouter des contacts longtemps après la réception de l'annonce d'origine, même après des redémarrages. La liste Entendus est ainsi particulièrement utile lorsque l'ajout automatique est réglé sur *Manuel seul*, car elle fournit un catalogue passif de tous les nœuds entendus sur le réseau.

3. **Depuis l'écran Discovery** : appuyez sur **F** depuis l'écran d'accueil pour lancer une recherche active de nœuds. Les nœuds qui répondent apparaissent dans une liste ; appuyez sur **Entrée** sur n'importe quelle entrée pour l'ajouter aux contacts.

**Supprimer des contacts**

Passez en mode sélection (Entrée), sélectionnez les contacts à retirer (Entrée pour sélectionner ou désélectionner, A pour tout sélectionner), puis appuyez sur **Backspace** pour les supprimer. Vous revenez à la liste des contacts une fois la suppression terminée.

Pour supprimer tous les contacts d'un coup, utilisez **Paramètres → Expérimental → Supprimer tous les contacts** (Settings → Experimental Features → Delete all contacts) (v1.15+). Voir [Fonctions expérimentales](#fonctions-expérimentales-v115).

**Exporter et importer des contacts**

En mode sélection, appuyez sur **X** pour exporter. Si des contacts sont sélectionnés, seuls ceux-ci sont exportés ; si aucun n'est sélectionné, tous les contacts sont exportés. Les contacts sont enregistrés dans un fichier JSON dans `/meshcore/` sur la carte SD, avec un horodatage dans le nom du fichier. Le format JSON est compatible avec les applications compagnon MeshCore : vous pouvez copier le fichier depuis la carte SD et l'importer dans l'application compagnon Android, iOS ou web.

Appuyez sur **R** dans la liste des contacts (hors mode sélection) pour importer des contacts depuis un fichier JSON de la carte SD. Le fichier d'export le plus récent dans `/meshcore/` est utilisé automatiquement.

**Limites de contacts :** Toutes les variantes prennent en charge jusqu'à 2 000 contacts (stockés en PSRAM).

### Envoyer un message privé

Sélectionnez un contact **Chat** dans la liste des contacts et appuyez sur **Entrée** pour commencer à rédiger un message privé. L'écran de rédaction affiche `DM: ContactName` dans l'en-tête. Tapez votre message et appuyez sur **Entrée** pour l'envoyer. Le MP est envoyé chiffré directement à ce contact (ou en flood si aucun chemin direct n'est connu). Après l'envoi ou l'annulation, vous revenez à la liste des contacts.

Pendant l'acheminement d'un MP, la vue de conversation affiche son état de distribution : **Envoi x/N** (Sending x/N) pendant que l'appareil fait de nouvelles tentatives, **Distribué** (Delivered) une fois que le destinataire en accuse réception, ou **Échec** (Failed) si aucune tentative n'obtient de réponse. Les nouvelles tentatives sont gérées par l'appareil, cela fonctionne donc sans application compagnon connectée.

Les contacts ayant des messages privés non lus affichent un marqueur `*` à côté de leur nom dans la liste des contacts.

**Lire les MP reçus :** Depuis l'écran d'accueil, appuyez sur **M** pour ouvrir le sélecteur de canal, puis sélectionnez l'entrée **DM Inbox** pour voir les messages privés reçus. Elle affiche tous les messages privés reçus avec le nom de l'expéditeur et l'horodatage. L'ouverture de la boîte de réception des MP marque tous les MP comme lus et efface l'indicateur de non-lus.

### Room servers

Les room servers sont des nœuds MeshCore qui hébergent des salons de discussion persistants. Les messages envoyés à un room server sont stockés et relayés à toute personne qui s'y connecte. Dans Meck, les messages des room servers arrivent comme des messages de contact et apparaissent dans la boîte de réception des MP, à côté des messages privés ordinaires.

Pour interagir avec un room server, allez dans l'écran des contacts, filtrez sur les contacts **Salon** (Room), sélectionnez le salon et appuyez sur **Entrée** pour ouvrir l'écran d'administration des répéteurs. Connectez-vous avec le mot de passe administrateur du salon pour accéder à l'administration du salon. Une fois la connexion réussie, tous les messages non lus de ce salon sont automatiquement marqués comme lus.

Les messages des room servers sont aussi synchronisés avec l'application compagnon lorsqu'elle est connectée en BLE ou en WiFi : l'application compagnon les récupère et les affiche avec les autres messages.

### Écran d'administration des répéteurs

Sélectionnez un contact **Répéteur** (Repeater) dans la liste des contacts et appuyez sur **Entrée** pour ouvrir l'écran d'administration du répéteur. Le mot de passe administrateur du répéteur vous est demandé. Les caractères apparaissent brièvement pendant la saisie avant d'être masqués, ce qui facilite la saisie des symboles et des chiffres sur le clavier du T-Deck Pro et du T-Deck Max.

Après une connexion réussie, un menu propose les commandes d'administration à distance suivantes :

| Élément du menu | Description |
|-----------|-------------|
| Synchro horloge (Clock Sync) | Envoyer l'heure de l'horloge de votre appareil au répéteur |
| Envoyer une annonce (Send Advert) | Demander au répéteur de diffuser une annonce |
| Voisins (Neighbors) | Voir les autres répéteurs entendus via des annonces à zéro saut |
| Lire l'horloge (Get Clock) | Lire la valeur actuelle de l'horloge du répéteur |
| Version | Demander la version du firmware du répéteur |
| Get Status | Récupérer les informations d'état du répéteur |

| Touche | Action |
|-----|--------|
| W / S | Naviguer dans les éléments du menu |
| Entrée | Exécuter la commande sélectionnée |
| Q | Retour aux contacts (depuis le menu) ou annuler la connexion |

Les réponses aux commandes s'affichent dans une vue défilante. Utilisez **W / S** pour faire défiler les réponses longues et **Q** pour revenir au menu.

### Écran de trace de route (v1.9+)

L'écran de trace de route vous permet de construire une chaîne de répéteurs et d'y faire passer une trace : la même fonction que dans l'application compagnon MeshCore, mais directement sur l'appareil. Chaque répéteur de la chaîne qui reconnaît son hash ajoute son SNR de réception avant de retransmettre le paquet, ce qui vous donne la qualité du signal saut par saut pour toute la route.

Appuyez sur **R** depuis l'écran d'accueil pour ouvrir l'écran de trace.

**Construire le chemin**

Il existe deux façons d'ajouter des sauts au chemin :

- **+ Ajouter un répéteur** (+ Add repeater) : ouvre un sélecteur affichant tous les contacts de type répéteur connus. Appuyez sur W/S pour faire défiler, sur Entrée pour ajouter le répéteur en surbrillance à la fin du chemin. Le sélecteur de contacts utilise directement les octets de clé publique enregistrés du répéteur, le hash est donc toujours correct quel que soit le mode.
- **Saisir chemin** (Type Path) : ouvre un éditeur de texte intégré pour des valeurs de hash décimales séparées par des virgules, au même format que celui de l'application compagnon (par ex. `3601,2198,1244,2198,3601`). Utile lorsque le répéteur ne figure pas dans vos contacts mais que vous connaissez son hash grâce à l'application ou à une liste communautaire.

Le chemin s'affiche sous forme de liste numérotée sous le menu. Chaque saut peut être modifié ou retiré, et le chemin entier peut être effacé si vous devez recommencer.

**Mode de hash**

Meck prend en charge les modes de hash à 1 octet et à 2 octets. L'écran utilise par défaut le réglage `path.hash.mode` de votre appareil, mais il peut être basculé à la volée :

- Mode 1 octet (`Mode: 1-byte`) : anciens réseaux, plus sujet aux collisions mais paquets plus petits
- Mode 2 octets (`Mode: 2-byte`) : valeur par défaut actuelle de MeshCore dans la plupart des régions, moins de collisions de hash

Utilisez **A / D** sur la ligne du mode pour basculer, ou Entrée pour passer d'un mode à l'autre.

**Lancer une trace**

Dès qu'au moins un saut figure dans le chemin, descendez jusqu'à **Lancer le traçage** (Run Trace) et appuyez sur Entrée. Meck crée un paquet `PAYLOAD_TYPE_TRACE` et l'envoie en routage direct à travers la chaîne. L'écran passe à l'état "Traçage..." (Tracing...) avec un compteur de temps écoulé pendant l'attente de la réponse.

Une fois la trace terminée, l'écran affiche :

- Chaque saut dans l'ordre avec son SNR de réception (en dB)
- Le SNR final du paquet de réponse à son retour sur votre appareil
- Le temps aller-retour total en millisecondes

Si 30 secondes s'écoulent sans réponse, la trace expire et l'écran revient à la vue de construction pour que vous puissiez ajuster le chemin et réessayer.

**Chemins symétriques et visibilité directe**

Pour une trace aller-retour, le chemin doit être symétrique. Pour tracer à travers les répéteurs A → B → C et retour, tapez `A,B,C,B,A`. Le paquet de trace part par A→B→C, et la réponse revient par C→B→A.

Vous devez aussi pouvoir **entendre directement le dernier répéteur de la chaîne** : c'est lui qui renvoie la réponse vers votre appareil. Si le dernier saut est trop loin pour être entendu, la trace expirera même si l'aller a réussi.

| Touche | Action |
|-----|--------|
| W / S | Naviguer dans les éléments du menu |
| A / D | Basculer le mode de hash (1 octet / 2 octets) sur la ligne du mode |
| Entrée | Sélectionner / confirmer / ouvrir l'éditeur |
| 0–9 , | Valeurs du chemin (lorsque l'éditeur de texte intégré est ouvert) |
| Q | Annuler la modification / retour à l'écran d'accueil |

L'écran prend en charge jusqu'à **16 sauts** par trace.

### Journal RX (Rx Log)

Le Journal RX (Rx Log) est un renifleur de paquets intégré à l'appareil, à l'image du Rx Log de l'application compagnon MeshCore. Ouvrez-le depuis **Paramètres -> Journal RX >>** (Settings -> Rx Log >>). Il capture chaque paquet reçu par la radio (y compris les relais destinés à d'autres nœuds, puisque la capture a lieu avant le filtrage) dans une mémoire tampon des 100 paquets les plus récents, affichés du plus récent au plus ancien.

Chaque entrée indique le type de route (flood ou direct) et le type de charge utile, l'heure de réception et la taille transmise, le hash du paquet, le chemin des sauts, ainsi que le hash/nom du canal (pour les messages de groupe) ou les hashes des nœuds De/À (From/To) (pour les paquets adressés). Pour les messages de canal que votre appareil peut déchiffrer, la ligne décodée "expéditeur : message" est également jointe.

| Touche | Action |
|-----|--------|
| W / S | Faire défiler les entrées (la plus récente en haut) |
| Q | Retour aux paramètres |

Le journal est conservé uniquement en RAM et il est effacé au redémarrage.

Un compteur en continu **Paquets RX** (RX packets) apparaît aussi sur la page des détails radio de l'écran d'accueil (parcourez les pages avec **D**), juste sous la valeur du bruit de fond. Il compte les paquets flood et directs reçus depuis le démarrage et se remet à zéro au redémarrage ou chaque fois que vous modifiez les paramètres radio (fréquence, largeur de bande ou facteur d'étalement).

### Supprimer l'historique des messages (v1.10+)

Vous pouvez effacer tous les messages enregistrés d'un canal donné ou de la boîte de réception des MP sans supprimer le canal lui-même.

Depuis l'écran d'accueil, appuyez sur **M** pour ouvrir le sélecteur de canal. Allez jusqu'au canal à vider et appuyez sur **X**. Une fenêtre de confirmation s'affiche avec la question "Supprimer l'historique ?" (Delete message history?) : appuyez sur **Entrée** pour confirmer ou sur **Q** pour annuler.

Les messages sont invalidés dans la mémoire tampon circulaire et la modification est immédiatement enregistrée sur la SD. Le compteur de non-lus est également remis à zéro. Les nouveaux messages continuent d'apparaître à leur arrivée.

### Préférences de notification par canal (v1.10+)

Chaque canal (et la boîte de réception des MP) peut être réglé individuellement sur l'un des trois niveaux de notification suivants :

- **Tous** (All) : notification à chaque message (par défaut)
- **@ (Mentions)** : notification uniquement lorsque quelqu'un vous mentionne avec @YourNodeName ou @[YourNodeName]
- **Non** (Off) : silence complet (pas de buzzer, pas de flash du clavier, pas de réveil de l'écran, pas de toast)

Les messages sont toujours enregistrés dans l'historique, quel que soit le réglage de notification : seuls les alertes et les badges de non-lus sont désactivés.

Pour modifier la préférence de notification d'un canal : depuis l'écran d'accueil, appuyez sur **S** pour ouvrir les paramètres, descendez jusqu'à la section **Canaux >>** (Channels >>) et ouvrez-la. Allez jusqu'au canal à configurer et appuyez sur **N** pour faire défiler les trois modes. Le réglage actuel est indiqué dans l'aide de la ligne du canal sous la forme `N:All`, `N:@` ou `N:Off`.

### Sons de notification personnalisés (v1.10+)

Chaque canal peut avoir son propre son de notification à la place du son de buzzer par défaut. Lorsqu'un message arrive sur un canal auquel un son personnalisé est attribué, ce son est joué par le haut-parleur à la place du buzzer RTTTL.

Pour attribuer un son : depuis l'écran d'accueil, appuyez sur **S** pour ouvrir les paramètres, descendez jusqu'à la section **Canaux >>** (Channels >>) et ouvrez-la. Allez jusqu'au canal voulu et appuyez sur **T**. Le sélecteur de son apparaît avec la liste des sons disponibles. Utilisez **W/S** pour parcourir la liste, **Entrée** pour sélectionner ou **Q** pour annuler. Sélectionnez "Défaut (silencieux)" (Default (silent)) pour retirer un son personnalisé et revenir au buzzer standard.

**Variante audio (DAC PCM5102A) :** Une sélection de sons fournis est copiée dans le dossier `/alarms/` de la carte SD au premier démarrage. Vous pouvez aussi ajouter vos propres fichiers MP3 dans ce dossier : ils apparaissent dans le sélecteur de son à côté des sons fournis. Vous disposez ainsi d'une liberté totale pour utiliser n'importe quel MP3 court comme son de notification.

**Variante 4G (modem A7682E) :** Sept sons de notification fournis sont intégrés au firmware sous forme de fichiers WAV mono 8 kHz et transférés vers le système de fichiers interne du modem au démarrage. La lecture passe par l'amplificateur de haut-parleur propre au modem via AT+CCMXPLAY. Les sons personnalisés fournis par l'utilisateur ne sont pas pris en charge sur la variante 4G : seul l'ensemble fourni est disponible.

**Sons fournis disponibles :** Bell, Ding, High Trill, Low Soft Ding (x2), Mid Trill et Soft Notif. Ce sont tous des sons d'alerte courts, de 1 à 2 secondes.

**T-Deck Max, Buzzer (vibreur)** (Buzzer (vibrate)) : Sur le MAX, le sélecteur de son comporte une option supplémentaire **Buzzer (vibreur)** au-dessus des fichiers audio. Lorsqu'elle est sélectionnée, un message entrant sur ce canal fait vibrer le moteur haptique DRV2605 du MAX au lieu de jouer un son ou le buzzer RTTTL, ce qui est utile pour des alertes silencieuses. Comme le MAX possède son propre codec audio ES8311, il prend aussi en charge les sons MP3 personnalisés du dossier `/alarms/`, exactement comme la variante audio du T-Deck Pro (et contrairement à la variante 4G du T-Deck Pro). Voir [Notifications par vibreur (Buzzer)](#notifications-par-vibreur-buzzer).

### Jeux (v1.10+)

Appuyez sur **J** depuis l'écran d'accueil, ou touchez la tuile **Jeux** (Games), pour ouvrir le menu des jeux. Deux jeux classiques sont inclus dans tous les builds, et le T-Deck Max ajoute un émulateur Game Boy (voir ci-dessous) :

**Snake** : le classique de Nokia. Guidez le serpent sur l'écran pour manger de la nourriture et grandir sans heurter les murs ni votre propre queue. Le jeu tourne sur l'écran e-ink à un rythme adapté à sa fréquence de rafraîchissement.

| Touche | Action |
|-----|--------|
| W | Tourner vers le haut |
| A | Tourner à gauche |
| S | Tourner vers le bas |
| D | Tourner à droite |
| Q | Revenir au menu des jeux |

**Démineur** (Minesweeper) : dégagez le plateau sans tomber sur une mine. Les chiffres indiquent combien de cases adjacentes contiennent des mines. Placez un drapeau sur les cases que vous soupçonnez de cacher une mine pour vous y retrouver.

| Touche | Action |
|-----|--------|
| W / A / S / D | Déplacer le curseur |
| Entrée | Révéler la case |
| F | Placer ou retirer un drapeau sur la case |
| Q | Revenir au menu des jeux |

Sur le T-Deck Max, le plateau est plus grand, avec une grille de 15x20 et 50 mines.

#### Émulateur Game Boy / Game Boy Color (v1.14+)

La troisième entrée du menu des jeux est **Game Boy** : un émulateur basé sur le cœur [Peanut-GB](https://github.com/deltabeard/Peanut-GB) qui fait tourner les jeux Game Boy originaux (`.gb`) et Game Boy Color (`.gbc`) à leur vitesse réelle. La Game Boy Advance (`.gba`) est une machine différente et n'est pas prise en charge. Il fonctionne sur le T-Deck Pro et le T-Deck Max. **Le son est réservé au T-Deck Max** dans cette version ; les variantes audio du Pro peuvent produire du son, mais ce n'est pas encore mis en place, et les variantes Pro 4G n'ont pas de matériel audio.

**Obtenir des jeux.** Aucun jeu n'est fourni : vous utilisez vos propres fichiers ROM. Deux titres homebrew gratuits qui fonctionnent bien : [uCity](https://github.com/AntonioND/ucity) (GPL-3.0, un jeu de construction de ville pour Game Boy Color ; le `.gbc` se trouve sur sa page Releases) et [Halo: Combat Devolved](https://sofaswordsman.itch.io/halo-combat-devolved) (un demake gratuit de 2 Mo pour Game Boy Color ; téléchargez la ROM depuis la page). Tout le reste dépend de vous et de vos propres copies.

**Stocker les jeux.** Placez les fichiers ROM dans un dossier nommé **`roms`** à la racine de la carte SD. La liste affiche uniquement les fichiers `.gb` et `.gbc` de ce dossier (pas de sous-dossiers), jusqu'à 32, avec des noms de fichier de moins de 48 caractères ; les noms plus longs sont ignorés sans message, et les fichiers de métadonnées `._` que macOS laisse sur les cartes FAT sont ignorés.

**Lancer un jeu.** Ouvrez le menu des jeux, choisissez **Game Boy**, placez-vous sur une ROM avec **W / S** et appuyez sur **Entrée**. Une fenêtre "Chargement..." ("Loading...") s'affiche pendant la lecture de la ROM sur la carte (une ou deux secondes pour un gros jeu). Le **réseau mesh continue de fonctionner** pendant que vous jouez (les messages arrivent toujours et l'en-tête affiche le nombre de messages non lus), mais les sons de notification et les pop-ups "new message" sont mis en attente pour ne pas interrompre le jeu. Le processeur est maintenu à 240 MHz pendant toute la partie : attendez-vous à une consommation de batterie plus élevée qu'en usage normal.

| Touche | Bouton Game Boy |
|-----|-----------------|
| W / A / S / D | Croix directionnelle |
| K | A |
| J | B |
| Entrée | Start |
| Space | Select |
| Q (ou Shift+Backspace) | Revenir à la liste des ROM (la sauvegarde est écrite d'abord) |

Maintenir une touche maintient le bouton correspondant : vous pouvez donc marcher tout en maintenant un bouton, comme sur la vraie console. Le rôle des boutons en jeu est défini par chaque jeu ; selon la convention de la Game Boy, A valide, donc **K** sélectionne dans la plupart des menus. Appuyez de nouveau sur **Q** dans la liste des ROM pour revenir au menu des jeux.

**Son (T-Deck Max).** Les jeux démarrent **sans le son** ; appuyez sur la touche **Mic** pour activer le son, et de nouveau pour le couper. Le pied de page indique l'action que fera la touche. Le son se limite au chiptune de la Game Boy Color, à un niveau bien inférieur à celui des sons de notification.

**À quoi ressemble l'image.** Le jeu est affiché à 1,5x (240x216, toute la largeur de l'écran) en noir et blanc, avec un tramage ordonné qui remplace les nuances de la Game Boy, si bien que les couleurs claires apparaissent comme un fin pointillé. Comme l'e-ink ne peut se redessiner qu'environ une fois toutes les 0,7 seconde, l'image se met à jour environ 1,4 fois par seconde, tandis que le jeu lui-même tourne en arrière-plan à 59,7 images par seconde : les animations rapides apparaissent comme une série d'instantanés, mais le timing, le gameplay et les boîtes de texte ne sont pas affectés. Quand vous quittez, l'écran effectue un flash noir et blanc complet pour effacer la rémanence laissée par les rafraîchissements partiels. Les jeux avec beaucoup de sprites en mouvement (la carte du monde de Pokemon Gold, par exemple) présentent une certaine rémanence des éléments en mouvement pendant que vous jouez ; c'est une limite de l'e-ink à cette vitesse, pas un défaut. En mode sombre, l'image est inversée comme tout le reste.

**Sauvegardes.** Les jeux qui sauvegardent dans la RAM de la cartouche conservent leurs sauvegardes : un **fichier `.sav` est écrit à côté de la ROM** (`game.gbc` devient `game.sav`) et chargé au lancement suivant. C'est le format brut standard de RAM de cartouche qu'utilisent les émulateurs de bureau, donc les sauvegardes peuvent être copiées entre l'appareil et un PC dans les deux sens. Le fichier est écrit en arrière-plan peu de temps après que vous avez sauvegardé dans le jeu (et à nouveau en quittant s'il reste quelque chose à sauvegarder), vous n'avez donc normalement pas besoin de quitter pour conserver votre progression ; mais l'écriture n'a lieu qu'une fois que le jeu est resté inactif un instant, il est donc plus sûr de faire une pause avant d'éteindre. L'état de l'horloge temps réel (le cycle jour/nuit de Pokemon Gold, Silver et Crystal) n'est pas conservé ; l'horloge du jeu repart de zéro à chaque fois.

**Limites de cette version.** Son sur le T-Deck Max uniquement ; un seul dossier de ROM, 32 ROM, pas de sous-dossiers ; un jeu de 2 Mo nécessite un bloc de 2 Mo de PSRAM, qui est réservé au premier lancement après le démarrage puis conservé, donc lancer n'importe quel jeu tôt garantit que les gros jeux fonctionneront ensuite. L'émulateur a besoin de 240 MHz pour tourner à pleine vitesse et n'apparaît pas dans le menu des jeux des builds économie de batterie à 40 MHz.

### Canaux privés (v1.11+)

Meck prend en charge à la fois les canaux publics à hashtag et les canaux privés. La différence tient à la façon dont le secret du canal est généré :

- **Canaux publics (hashtag)** : tapez un nom commençant par `#` (par ex. `#camping`). Le secret de 16 octets est dérivé de façon déterministe du nom via SHA-256, de sorte que toute personne qui crée le même nom de hashtag sur n'importe quel appareil MeshCore obtient la même clé et peut communiquer sur ce canal.
- **Canaux privés** : tapez un nom **sans** le préfixe `#` (par ex. `team-alpha`). Un secret aléatoire de 16 octets, de qualité cryptographique, est généré, ce qui signifie que seuls les appareils auxquels la clé a été explicitement transmise peuvent participer.

**Comment créer un canal privé :**

1. Depuis l'écran d'accueil, appuyez sur **S** pour ouvrir les paramètres
2. Faites défiler jusqu'à la section **Canaux >>** (Channels >>) et appuyez sur Entrée pour l'ouvrir
3. Allez sur **+ Ajouter canal (# = public)** (+ Add Channel (# = public)) et appuyez sur Entrée
4. Tapez le nom du canal **sans** préfixe `#`, puis appuyez sur Entrée

Le canal est créé avec un secret aléatoire. Pour permettre à d'autres utilisateurs de Meck de le rejoindre, utilisez le partage de canal (voir ci-dessous).

### Partage de canal par MP (v1.11+)

Vous pouvez partager n'importe quel canal, public ou privé, avec un autre utilisateur de Meck en lui envoyant le nom et le secret du canal dans un message privé chiffré. C'est particulièrement utile pour les canaux privés, pour lesquels partager le secret manuellement obligerait à taper 32 caractères hexadécimaux.

**Comment partager un canal :**

1. Depuis l'écran d'accueil, appuyez sur **S** pour ouvrir les paramètres
2. Faites défiler jusqu'à la section **Canaux >>** (Channels >>) et appuyez sur Entrée pour l'ouvrir
3. Allez sur le canal que vous souhaitez partager
4. Appuyez sur **C** pour ouvrir le sélecteur de contacts
5. Sélectionnez un contact et appuyez sur Entrée pour envoyer

L'appareil du destinataire ajoute automatiquement le canal à sa liste de canaux (s'il n'existe pas déjà et qu'un emplacement est libre). Une alerte confirme que le canal a été ajouté. Dans la conversation privée, l'expéditeur comme le destinataire voient un message épuré, "Canal partagé : nom" ("Shared channel: name"), plutôt que les données brutes du protocole.

### Export/import de la configuration (v1.11+)

Le sous-écran **Exporter/Importer >>** (Export/Import >>) des Paramètres (Settings) vous permet de sauvegarder et de restaurer la configuration de votre appareil via des fichiers JSON sur la carte SD. C'est utile pour migrer vos réglages vers une nouvelle carte électronique, garder une sauvegarde avant de reflasher, ou cloner une configuration sur plusieurs appareils.

**Exporter :**

1. Depuis l'écran d'accueil, appuyez sur **S** pour ouvrir les paramètres
2. Faites défiler jusqu'à **Exporter/Importer >>** et appuyez sur Entrée
3. Sélectionnez **Exporter vers SD >>** (Export to SD >>) et appuyez sur Entrée
4. Choisissez les sections à inclure en appuyant sur Entrée sur chaque case à cocher :
   - **Identité** (Identity) : clé publique et clé privée (sauvegarde complète de l'identité)
   - **Réglages radio** (Radio Settings) : fréquence, bande passante, facteur d'étalement, taux de codage, puissance TX et position GPS
   - **Canaux** (Channels) : tous les noms et secrets des canaux
   - **Contacts** : tous les contacts avec leurs clés publiques, leur type, leurs indicateurs, leur position et leurs horodatages
   - **Préf. d'ajout auto** (Auto-Add Prefs) : mode d'ajout automatique des contacts et options par type
5. Allez sur **>> Exporter maintenant** (>> Export Now) et appuyez sur Entrée
6. Une fenêtre **Compilation... Veuillez patienter...** (Compiling... Please Wait...) s'affiche pendant l'export, suivie de **Exporté vers** (Exported to) et du nom du fichier, ou de **Échec export (SD ?)** (Export failed (SD?)) (v1.15+)

La configuration est enregistrée dans un fichier JSON horodaté dans `/meshcore/` sur la carte SD (par ex. `meshcore_config_20260523_1430.json`). Le format est compatible avec l'export de configuration de l'application compagnon MeshCore.

**Importer :**

Placez un fichier nommé `import.json` dans le dossier `/meshcore/` de votre carte SD. Ensuite, au choix :

- **Depuis les paramètres :** allez dans **Exporter/Importer >> → Importer depuis SD** (Export/Import >> → Import from SD) et appuyez sur Entrée
- **Au démarrage :** le firmware vérifie automatiquement la présence de `/meshcore/import.json` au démarrage et l'importe s'il le trouve

L'import depuis les Paramètres affiche une fenêtre **Importation... Veuillez patienter...** (Importing... Please Wait...) pendant l'import, suivie de **Config importée !** (Config imported!), **import.json introuvable** (No import.json found) ou **Échec de l'import** (Import failed) (v1.15+).

Si l'import contient une identité différente (clé privée), l'appareil redémarre après avoir appliqué la nouvelle identité. Les canaux et les contacts sont fusionnés avec la configuration existante.

### Écran des paramètres

Appuyez sur **S** depuis l'écran d'accueil pour ouvrir les paramètres. Au premier démarrage (quand le nom de l'appareil est encore l'identifiant hexadécimal par défaut), l'écran des paramètres se lance automatiquement sous forme d'assistant de configuration pour définir le nom de votre appareil et le préréglage radio.

| Touche | Action |
|-----|--------|
| W / S | Monter / descendre dans les paramètres |
| Shift+W / Shift+S | Page précédente / page suivante |
| Entrée | Modifier le paramètre sélectionné, ou entrer dans un sous-écran |
| Q | Revenir d'un niveau (sous-écran → niveau principal → écran d'accueil) |

**Paramètres disponibles :**

| Paramètre | Méthode de modification |
|---------|-------------|
| Antenne LoRa (LoRa Antenna) | Entrée pour basculer entre Interne (Internal) et Externe (External) (MAX uniquement) ; voir [Antenne](#antenne-interne--externe) |
| Device Name | Saisie de texte : tapez un nom, Entrée pour confirmer |
| Radio Preset | A / D pour faire défiler les préréglages régionaux, Entrée pour appliquer. Comprend Australia, Australia (Mid), Australia (Wide), Australia: SA/WA, Australia: QLD, EU/UK (Narrow / Long Range / Medium Range), Czech Republic et d'autres |
| Frequency | Saisie de texte : tapez la valeur exacte (par ex. 916.575), Entrée pour confirmer |
| Bandwidth | W / S pour faire défiler les valeurs standard (31.25 / 62.5 / 125 / 250 / 500 kHz), Entrée pour confirmer |
| Spreading Factor | W / S pour régler (5–12), Entrée pour confirmer |
| Coding Rate | W / S pour régler (5–8), Entrée pour confirmer |
| TX Power | W / S pour régler (1–22 dBm), Entrée pour confirmer |
| Décalage UTC (UTC Offset) | W / S pour régler (-12 à +14), Entrée pour confirmer |
| Messages prédéfinis >> (Canned Messages >>) | Ouvre les dix emplacements de messages prédéfinis (MAX uniquement) ; voir [Messages prédéfinis](#messages-prédéfinis-t-deck-max) |
| Msg Rcvd LED Pulse | Active ou désactive le clignotement du rétroéclairage du clavier à l'arrivée d'un nouveau message (Entrée pour basculer) |
| GPS Baud Rate | A / D pour faire défiler (Défaut 38400 (Default 38400) / 4800 / 9600 / 19200 / 38400 / 57600 / 115200), Entrée pour confirmer. **Nécessite un redémarrage pour prendre effet.** |
| Path Hash Mode | W / S pour faire défiler (1 octet / 2 octets / 3 octets), Entrée pour confirmer |
| Région déf. (Default Region) | Saisie de texte : tapez un nom de région (par ex. `au-nsw`), Entrée pour confirmer. Vide = sans portée. Voir [Portée de région](#portée-de-région-v17). |
| Mode sombre (Dark Mode) | Active ou désactive l'affichage inversé : texte blanc sur fond noir (Entrée pour basculer) |
| Taille du texte (Font Size) | Active ou désactive une taille de texte plus grande pour les messages de canal, les contacts, la boîte de réception des MP et les écrans d'administration des répéteurs (Entrée pour basculer) |
| Police (Font) | A / D pour faire défiler les styles (Classic / Noto Sans / Montserrat), Entrée pour appliquer. Voir [Polices](#polices). |
| Verrou auto (Auto Lock) | A / D pour faire défiler le délai (Aucun (None) / 2 / 5 / 10 / 15 / 30 min), Entrée pour confirmer |
| Rétroéclairage (Backlight Brightness) | W / S pour régler de 5 à 100 % par pas de 5 % (MAX uniquement) ; niveau de l'éclairage frontal e-ink |
| LED clavier (Keyboard LED) | W / S pour régler de 5 à 100 % par pas de 5 %, 50 % par défaut (MAX uniquement) ; voir [Rétroéclairage du clavier](#rétroéclairage-du-clavier) |
| WiFi | Entrée pour rechercher les réseaux, puis choisissez-en un et tapez son mot de passe (builds WiFi) ; voir [Compagnon WiFi](#compagnon-wifi) |
| Radio WiFi (WiFi Radio) | Entrée pour activer ou désactiver le WiFi (builds WiFi) |
| Modem 4G (4G Modem) | Entrée pour activer ou désactiver le modem 4G ; le choix est enregistré (builds Pro 4G et Max) |
| Contacts >> | Ouvre le sous-écran Contacts (voir ci-dessous) |
| Canaux >> (Channels >>) | Ouvre le sous-écran Canaux (voir ci-dessous) |
| Outils OTA >> (OTA Tools >>) | Ouvre le sous-écran OTA : Mise à jour du firmware (Firmware Update) et Gestionnaire de fichiers SD (SD File Manager) (voir [Mise à jour du firmware par OTA](#mise-à-jour-du-firmware-par-ota-v13)) |
| Exporter/Importer >> (Export/Import >>) | Ouvre le sous-écran Exporter/Importer : export de la configuration de l'appareil vers la SD ou import depuis `/meshcore/import.json` (voir [Export/import de la configuration](#exportimport-de-la-configuration-v111)) |
| Journal RX >> (Rx Log >>) | Ouvre le renifleur de paquets Journal RX (voir [Journal RX](#journal-rx-rx-log)) |
| Expérimental >> (Experimental Features >>) | Ouvre le sous-écran Expérimental : Langue (Language), Gain RX renforcé (RX Boosted Gain), Reset AGC (AGC Reset Int), Supprimer tous les contacts (Delete all contacts) et, sur le MAX, Rétroéclairage Alt+B (Change Backlight to Alt+B) (voir [Fonctions expérimentales](#fonctions-expérimentales-v115)) |
| Infos appareil (Device Info) | Clé publique et version du firmware (lecture seule) |

**Sous-écran Contacts** : appuyez sur Entrée sur la ligne `Contacts >>` pour l'ouvrir. Il contient le sélecteur du mode d'ajout automatique des contacts et, lorsque ce mode est réglé sur Personnalisé (Custom), des options par type :

| Option | Effet quand elle est sur OUI (ON) |
|--------|----------------|
| Compagnon (Companion) | Ajoute automatiquement un nœud de chat lorsque son annonce est reçue |
| Répéteur (Repeater) | Ajoute automatiquement les répéteurs reçus par annonce |
| Serveur salon (Room Server) | Ajoute automatiquement les room servers reçus par annonce |
| Capteur (Sensor) | Ajoute automatiquement les nœuds capteurs reçus par annonce |
| Écraser anciens (Overwrite Oldest) | Quand la liste de contacts est pleine, écrase l'entrée non favorite la plus ancienne au lieu d'ignorer le nouveau contact |

Appuyez sur Q pour revenir à la liste principale des paramètres.

**Sous-écran Canaux** : appuyez sur Entrée sur la ligne `Channels >>` pour l'ouvrir. Il liste tous les canaux actuels avec leurs étiquettes de portée de région (par ex. `[au-nsw]`, ou `[*]` pour la valeur par défaut de l'appareil). La ligne d'aide de chaque canal indique sa préférence de notification actuelle (`N:All`, `N:@` ou `N:Off`) et les actions disponibles.

| Touche | Action |
|-----|--------|
| Entrée | Modifier la portée de région du canal |
| N | Faire défiler la préférence de notification (Tous (All) / Mentions / Non (Off)) ; voir [Préférences de notification par canal](#préférences-de-notification-par-canal-v110) |
| T | Ouvrir le sélecteur de son de notification (variantes audio et 4G) ; voir [Sons de notification personnalisés](#sons-de-notification-personnalisés-v110) |
| C | Partager le canal avec un contact par MP ; voir [Partage de canal par MP](#partage-de-canal-par-mp-v111) |
| X | Supprimer le canal (canaux non principaux uniquement) |
| Q | Revenir à la liste principale des paramètres |

Quand le nom du canal en surbrillance, son étiquette de région et sa ligne d'aide sont trop longs pour une seule ligne, la ligne affiche le début puis la fin, en alternant toutes les 2 secondes (v1.15+).

L'écran principal des paramètres affiche aussi l'ID de votre nœud et la version du firmware. Sur la variante 4G, l'IMEI, le nom de l'opérateur et les informations d'APN y sont également affichés.

Lors de l'ajout d'un canal, tapez le nom du canal et appuyez sur Entrée. Les noms commençant par `#` créent un canal public à hashtag (secret dérivé du nom via SHA-256, conformément à la convention standard de MeshCore). Les noms sans préfixe `#` créent un canal privé avec un secret aléatoire ; voir [Canaux privés](#canaux-privés-v111).

Si vous avez modifié des paramètres radio, appuyer sur Q vous proposera d'appliquer les changements avant de quitter.

> **Astuce :** Tous les paramètres de l'appareil (ainsi que des paramètres de réglage fin du mesh non disponibles à l'écran) peuvent aussi être configurés via le port série USB. Consultez le [Guide des paramètres série](Serial%20Settings%20Guide.md) pour une documentation complète.

### Fonctions expérimentales (v1.15+)

**Paramètres → Expérimental >>** (Settings → Experimental Features >>), sous Journal RX (Rx Log), regroupe des paramètres encore en phase d'essai. Appuyez sur **Entrée** sur une ligne pour la modifier et sur **Q** pour revenir en arrière.

| Paramètre | Effet |
|---------|--------------|
| Langue (Language) | **Language: Change to French** passe l'interface en français. En français, la ligne affiche **Langue : Passer à l'anglais** pour revenir à l'anglais. Classic n'a pas de lettres accentuées, donc choisir le français alors que le style de police est Classic le remplace par Noto Sans. Voir le [Guide de la langue française](docs/French_Language_Guide.md). |
| Rétroéclairage Alt+B (Change Backlight to Alt+B) (MAX uniquement) | Quand il est sur **OUI** (ON), le bouton Cœur n'allume et n'éteint plus l'éclairage frontal, et c'est **Alt+B** qui l'allume, au niveau défini dans Rétroéclairage (Backlight Brightness), et l'éteint à la place. Par défaut : **NON** (OFF). |
| Gain RX renforcé (RX Boosted Gain) | Quand il est sur **OUI** (valeur par défaut), le récepteur LoRa utilise son mode de gain de réception renforcé. Sur **NON**, il utilise le mode de gain normal. Le changement s'applique en moins d'une demi-seconde. |
| Reset AGC (AGC Reset Int) | La fréquence à laquelle le contrôle automatique de gain du récepteur LoRa est réinitialisé. Tapez une durée en secondes et appuyez sur **Entrée** : elle est arrondie au multiple de 4 inférieur, jusqu'à 1 020 secondes. Laissez vide (ou 0) pour **Arrêt** (Off), la valeur par défaut. |
| Supprimer tous les contacts (Delete all contacts) | Supprime tous les contacts (favoris et chemins personnalisés compris) ainsi que l'historique des MP. Les messages des canaux sont conservés. Deux confirmations suivent, **Supprimer les contacts ?** (Delete all contacts?) puis **Êtes-vous sûr ?** (Are you sure?) (Entrée pour oui, Q pour non). L'appareil affiche **Suppression** (Purging), puis le résultat, puis redémarre. Ne l'éteignez pas pendant la suppression. |

### Polices

Meck prend en charge trois styles de police dans toute l'interface : **Classic** (l'apparence FreeSans d'origine), **Noto Sans** (net, excellente couverture du latin étendu) et **Montserrat** (géométrique, distinctif).

Changez la police dans **Paramètres → Police** (Settings → Font) : utilisez A/D pour faire défiler les styles avec un aperçu en direct, puis Entrée pour appliquer. Appuyez sur Q pour annuler et revenir au style précédent. Le style choisi s'applique à tous les écrans, y compris les messages de canal, les contacts, les paramètres et la liseuse.

Les styles de police sont disponibles dans les deux modes de taille de texte, PETIT (Tiny) et GRAND (Larger). En taille PETIT, les polices personnalisées utilisent des glyphes de 7 pt ; en taille GRAND, de 9 pt, ce qui correspond à la mise en page FreeSans existante. La préférence de police est enregistrée et conservée après un redémarrage.

**Prise en charge des caractères accentués (v1.8+, étendue en v1.15) :** Les trois styles de police affichent les caractères accentués et diacritiques (tchèque, polonais, français, allemand, etc.) au lieu de les supprimer. **Noto Sans** affiche nativement les signes diacritiques (carons, accents, cédilles) à toutes les tailles. **Montserrat** les affiche dans le texte en taille PETIT et dans les titres, mais le texte dessiné en taille 9 pt ramène les caractères accentués à leur lettre ASCII de base (ě→e, ž→z, ñ→n), comme le fait toujours **Classic** : la lettre reste toujours visible, simplement sans le signe diacritique. La taille 9 pt est utilisée pour la taille de texte GRAND, ainsi que pour certains textes quel que soit le réglage, comme les lignes d'état de la page WiFi de l'écran d'accueil.

### Mode rédaction

| Touche | Action |
|-----|--------|
| Entrée | Envoyer le message |
| Backspace | Effacer le dernier caractère |
| Shift + Backspace | Annuler et quitter le mode rédaction |

### Saisie des symboles (touche Sym)

Appuyez sur la touche **Sym** puis sur une touche de lettre pour saisir des chiffres et des symboles :

| Touche | Sym+ | | Touche | Sym+ | | Touche | Sym+ |
|-----|------|-|-----|------|-|-----|------|
| Q | # | | A | * | | Z | 7 |
| W | 1 | | S | 4 | | X | 8 |
| E | 2 | | D | 5 | | C | 9 |
| R | 3 | | F | 6 | | V | ? |
| T | ( | | G | / | | B | ! |
| Y | ) | | H | : | | N | , |
| U | _ | | J | ; | | M | . |
| I | - | | K | ' | | Mic | 0 |
| O | + | | L | " | | $ | Sélecteur d'emoji (Sym+$ pour un $ littéral) |
| P | @ | | | | | | |

### Autres touches

| Touche | Action |
|-----|--------|
| Shift | Majuscule pour la lettre suivante |
| Alt | Identique à Sym (pour les chiffres et symboles) |
| Space | Caractère espace / Suivant dans la navigation |
| Q | Retour / quitter l'écran en cours (voir la remarque ci-dessous) |

**Touche de sortie :** sur le T-Deck Pro et le Max, **Q** est la touche retour/sortie dans toute l'interface. L'exception est la saisie de texte : pendant que vous tapez, Q est un caractère normal, c'est donc **Shift+Backspace** qui permet de sortir.

### Sélecteur d'emoji

En mode rédaction, appuyez sur la touche **$** pour ouvrir le sélecteur d'emoji. Une grille défilante de 79 emoji s'affiche sur 5 colonnes, avec les visages et les émotions regroupés en premier. Le défilement est circulaire : appuyer sur W sur la première ligne mène à la dernière ligne, et inversement.

| Touche | Action |
|-----|--------|
| W / S | Monter / descendre |
| A / D | Aller à gauche / à droite |
| Entrée | Insérer l'emoji sélectionné |
| $ / Q / Backspace | Annuler et revenir à la rédaction |

### Appli SMS et téléphone (Pro 4G et Max uniquement)

Appuyez sur **T** depuis l'écran d'accueil pour ouvrir l'appli SMS et téléphone. L'appli s'ouvre sur un écran de menu comportant quatre entrées :

| Ligne | Rôle |
|-----|---------|
| **Composer** (Dial) | Pavé numérique pour appeler n'importe quel numéro |
| **Journal d'appels** (Call Log) | Appels entrants, sortants et manqués, enregistrés sur la carte SD |
| **Contacts** | Contacts enregistrés : ajouter, modifier, appeler, envoyer un message et supprimer |
| **SMS reçus** (SMS Inbox) | Conversations SMS |

Deux de ces lignes affichent un compteur entre crochets. **Journal d'appels [n]** indique les appels manqués que vous n'avez pas encore consultés, et **SMS reçus [n]** indique les messages reçus non lus. Chaque badge s'efface lorsque vous ouvrez l'écran correspondant, et les deux sont conservés après un redémarrage.

Le **Journal d'appels** enregistre les 32 appels les plus récents avec le numéro, le nom du contact lorsqu'il est connu, le type d'appel et la durée. La touche Entrée rappelle l'entrée en surbrillance, et la touche D la supprime. Les appels reçus sans identification de l'appelant sont tout de même enregistrés, et affichés comme `Unknown`.

L'écran **Contacts** liste les contacts enregistrés, avec A pour en ajouter un nouveau et M pour envoyer un SMS. Ouvrir un contact affiche une page avec les champs modifiables Nom (Name) et Numéro (Number) et une ligne Enregistrer (Save), ainsi que F pour appeler et D pour supprimer. Les contacts sont enregistrés sur la carte SD dans `/sms/contacts.txt`.

**Remarque pour les utilisateurs aux États-Unis : le modem A7682E du T-Deck Pro et du T-Deck Pro Max ne prend en charge que très peu de bandes cellulaires américaines, si bien que la 4G est presque entièrement inutilisable avec des cartes SIM américaines. Par exemple, la 4G est totalement inutilisable sur T-Mobile et peut n'avoir qu'un usage très limité sur AT&T, en raison de la nature de l'A7682E et des bandes qu'il utilise.**

Pour la documentation complète, notamment l'affectation des touches, l'utilisation du pavé numérique, la gestion des contacts et le dépannage, consultez le [guide de l'appli SMS et téléphone](SMS%20%26%20Phone%20App%20Guide.md).

### Navigateur web et IRC

Appuyez sur **B** depuis l'écran d'accueil pour ouvrir le navigateur web. Il est disponible sur les variantes BLE, WiFi et 4G (pas sur la variante audio autonome, qui exclut le WiFi pour préserver une conception à consommation de batterie minimale).

L'écran d'accueil du navigateur donne accès au **client IRC**, à la **barre d'URL**, ainsi qu'à vos **favoris** (Bookmarks) et à votre **historique** (History). Sélectionnez IRC Chat et appuyez sur Entrée pour configurer un serveur IRC et vous y connecter. Sélectionnez la barre d'URL pour saisir une adresse web, ou faites défiler vers le bas pour ouvrir un favori ou une entrée de l'historique.

Le navigateur est un lecteur centré sur le texte, plus adapté aux sites web riches en texte. Il comprend aussi une recherche web basique via DuckDuckGo Lite, et peut télécharger des fichiers EPUB : suivez un lien vers un `.epub` et il sera enregistré dans le dossier books de votre carte SD, pour être lu plus tard dans le lecteur de livres numériques.

Pour la documentation complète, notamment l'affectation des touches, la configuration du WiFi, les favoris, la configuration IRC et la structure de la carte SD, consultez le [guide de l'appli web](Web%20App%20Guide.md).

### Réveil (Pro Audio et Max uniquement)

Appuyez sur **K** depuis l'écran d'accueil pour ouvrir le réveil. Il est disponible sur la variante T-Deck Pro Audio (DAC PCM5102A) et sur le T-Deck Max (codec ES8311). Programmez jusqu'à cinq alarmes quotidiennes qui jouent vos propres fichiers MP3 via la prise casque.

**Configuration :**

1. Placez des fichiers MP3 (fréquence d'échantillonnage de 44 100 Hz) dans `/alarms/` sur la carte SD
2. Appuyez sur **K** pour ouvrir le réveil
3. Sélectionnez un emplacement d'alarme (1–5) avec **W / S** et appuyez sur **Entrée** pour le modifier
4. Réglez l'heure et les minutes, puis choisissez un fichier MP3 dans la liste
5. Appuyez sur **Entrée** pour enregistrer l'alarme

| Touche | Action |
|-----|--------|
| W / S | Parcourir les emplacements d'alarme / régler l'heure |
| A / D | Passer du champ des heures à celui des minutes |
| Entrée | Modifier l'emplacement / enregistrer l'alarme / choisir le MP3 |
| X | Supprimer l'alarme sélectionnée |
| Q | Retour à l'écran d'accueil |

**Quand une alarme se déclenche :**

Le MP3 choisi est joué via la prise casque, même si vous êtes sur un autre écran ou en train d'écouter un livre audio.

| Touche | Action |
|-----|--------|
| Z | Rappel dans 5 minutes |
| Toute autre touche | Arrêter l'alarme |

La configuration des alarmes est enregistrée dans `/alarms/.alarmcfg` sur la carte SD. Les alarmes sont conservées après un redémarrage : si l'horloge RTC a une heure valide (via le GPS ou la synchronisation avec l'appli compagnon), les alarmes se déclenchent à la bonne heure après un redémarrage.

> **Remarque :** les fichiers MP3 doivent être encodés avec une fréquence d'échantillonnage de **44 100 Hz**. Des fréquences d'échantillonnage plus basses peuvent provoquer de la distorsion en raison des limites matérielles de l'I2S de l'ESP32-S3 (même exigence que pour le lecteur de livres audio).

**Structure des dossiers de la carte SD :**

```
SD Card
├── alarms/
│   ├── .alarmcfg             (auto-created, stores alarm slot config)
│   ├── morning-chime.mp3
│   ├── rooster.mp3
│   └── gentle-bells.mp3
├── audiobooks/               (existing — audiobook player)
│   └── ...
├── books/                    (existing — text reader)
│   └── ...
└── ...
```

### Messages vocaux par LoRa (Pro Audio et Max uniquement)

Appuyez sur la **touche Microphone** (la touche zéro du clavier) pour ouvrir l'écran Messages vocaux (Voice Messages). Il est disponible sur la variante T-Deck Pro Audio (DAC PCM5102A) et sur le T-Deck Max (codec ES8311).

Enregistrez et envoyez par LoRa des messages vocaux d'une durée maximale de 12 secondes. L'audio est encodé sur l'appareil avec Codec2 à 1 200 bit/s, ce qui compresse chaque seconde de parole dans un seul paquet LoRa de 150 octets. Les messages vocaux utilisent très peu de temps d'antenne par rapport à ce qu'ils transmettent : un message de 5 secondes ne représente que 5 paquets.

Les messages vocaux peuvent être envoyés à un autre appareil T-Deck Pro Audio (lecture automatique via la prise casque), à un T-Deck Max (lecture automatique), ou à tout appareil compagnon MeshCore connecté à l'[application web Meck-Mycelium](https://pelgraine.github.io/Meck-Mycelium) (lecture par le haut-parleur de votre téléphone, sous forme de bulle à toucher dans la vue MP).

**Envoyer un message vocal :**

1. Appuyez sur la **touche Microphone** pour ouvrir l'écran Messages vocaux
2. Appuyez sur la touche Microphone et **maintenez-la** enfoncée pour enregistrer, puis relâchez-la pour arrêter (12 secondes maximum)
3. Appuyez sur **S** pour ouvrir le sélecteur de contacts : les contacts ayant un chemin direct apparaissent en haut
4. Faites défiler jusqu'à votre contact et appuyez sur **Entrée** pour envoyer

Les paquets sont envoyés de façon échelonnée, à 3 secondes d'intervalle, pour éviter d'encombrer le canal. Avec un préréglage radio à 62,5 kHz / SF7 (par exemple Australia Narrow), un message vocal de 5 secondes arrive en 20 secondes environ, et un enregistrement de 12 secondes en 42 secondes environ.

**Recevoir des messages vocaux :**

* **Sur un appareil T-Deck Pro Audio :** l'écran Messages vocaux s'ouvre automatiquement et le message est joué via la prise casque. **Un casque est recommandé** : le haut-parleur intégré est très faible.
* **Sur un T-Deck Max :** l'écran Messages vocaux s'ouvre automatiquement et le message est joué.
* **Via Meck-Mycelium :** les messages vocaux apparaissent sous forme de bulles "🎙️ Voice message" dans la vue MP. Touchez-les pour les écouter. Le décodage Codec2 se fait entièrement dans le navigateur, grâce à WebAssembly.

> **Remarque :** l'enregistrement et l'envoi de messages vocaux nécessitent le matériel T-Deck Pro **variante Audio** (DAC PCM5102A) ou un T-Deck Max (codec ES8311). La réception et la lecture fonctionnent sur toutes les variantes Audio et sur le T-Deck Max. Les appareils Meck sans audio peuvent recevoir et relayer les paquets vocaux, mais ne peuvent pas les lire localement : utilisez l'application web Meck-Mycelium pour la lecture sur ces appareils.

| Touche | Action |
|-----|--------|
| Mic (maintenue) | Enregistrer un message vocal |
| Mic (relâchée) | Arrêter l'enregistrement |
| S | Ouvrir le sélecteur de contacts pour envoyer |
| Entrée | Envoyer au contact sélectionné |
| Q | Retour à l'écran d'accueil |

### Écran de verrouillage (T-Deck Pro et Max)

Double-cliquez sur le bouton Boot pour verrouiller l'écran. L'écran de verrouillage affiche l'heure actuelle, le pourcentage de batterie et le nombre de messages non lus. Le CPU descend à 40 MHz pendant le verrouillage pour réduire la consommation d'énergie. Sur le MAX, l'éclairage frontal e-ink s'éteint lorsque l'écran de verrouillage s'active, et il n'est pas rétabli automatiquement au déverrouillage : appuyez sur le bouton Cœur pour le rallumer.

Sur les builds Pro 4G et Max, l'écran de verrouillage affiche aussi les **SMS non lus** et les **appels manqués** en attente. Ces compteurs sont lus depuis la boîte de réception SMS et le journal d'appels au lieu d'être comptés pendant le verrouillage : ils sont donc conservés après un verrouillage, un déverrouillage et un redémarrage, et ne s'effacent que lorsque vous ouvrez la conversation concernée ou le Journal d'appels (Call Log).

Double-cliquez à nouveau sur le bouton Boot pour déverrouiller et revenir à l'écran sur lequel vous étiez.

Un délai de verrouillage automatique peut être configuré dans **Paramètres → Verrou auto** (Settings → Auto Lock) (Aucun (None) / 2 / 5 / 10 / 15 / 30 minutes d'inactivité).

### Extinction (T-Deck Pro et Max)

L'écran d'accueil comprend une page **Shutdown**. La sélectionner éteint complètement l'appareil : l'ESP32-S3 passe en sommeil profond sans aucune source de réveil, l'alimentation des périphériques est coupée et le module LoRa est mis hors tension. Seule une réinitialisation matérielle (bouton reset) ou une mise sous tension par USB réveille l'appareil. Ce mode est différent de l'hibernation du verrouillage automatique, qui conserve la possibilité de réveil par LoRa.

---

## T-Deck Max

Le LilyGo T-Deck Max est un proche parent du T-Deck Pro : même écran e-ink 240×320 et même clavier TCA8418, même ESP32-S3 et même interface sur l'appareil. **Toutes les commandes clavier et tous les écrans du [T-Deck Pro](#t-deck-pro) s'appliquent sans changement** : cette section ne couvre que ce qui diffère sur le MAX.

Depuis la v1.14, le Max est aussi le seul appareil Meck qui joue le son de l'[émulateur Game Boy](#émulateur-game-boy--game-boy-color-v114) (l'émulateur lui-même fonctionne aussi sur le T-Deck Pro).

La principale différence est que le MAX embarque à la fois un modem 4G A7682E **et** un codec audio ES8311, câblés via un expandeur d'E/S XL9555 pour pouvoir fonctionner en même temps. Sur le T-Deck Pro, le DAC audio et le modem 4G partagent un même emplacement matériel et s'excluent mutuellement ; sur le MAX, vous disposez de l'appli SMS et téléphone, du lecteur de livres audio, du réveil **et** des données cellulaires sur un seul appareil. Le MAX ajoute aussi un écran tactile capacitif CST328, trois boutons capacitifs en façade, un moteur haptique DRV2605 pour les alertes par vibration, un éclairage frontal e-ink et une batterie de 1 400 mAh.

### Variantes de build du T-Deck Max

| Variante | Environnement | BLE | WiFi | Modem 4G | Codec audio | Navigateur web | Contacts max |
|---------|------------|-----|------|----------|-------------|------------|-------------|
| MAX + BLE | `meck_max_ble` | Oui | — | A7682E | ES8311 | Oui | 2 000 |
| MAX + WiFi | `meck_max_wifi` | — | Oui (TCP:5000) | A7682E | ES8311 | Oui | 2 000 |
| MAX + BLE + WiFi (v1.15+) | `meck_max_ble_wifi` | Oui (un à la fois) | Oui (TCP:5000) | A7682E | ES8311 | Oui | 2 000 |
| MAX + Autonome | `meck_max_standalone` | — | — | A7682E | ES8311 | Oui | 2 000 |
| MAX + Autonome, 40 MHz | `meck_max_standalone_40mhz` | — | — | A7682E (éteint au démarrage) | ES8311 (fonctions audio désactivées) | Non | 2 000 |

Contrairement au T-Deck Pro, **chaque** variante MAX comprend à la fois le modem 4G et le codec audio : il n'y a pas de séparation entre "audio" et "4G", car le MAX fait fonctionner les deux. Le navigateur web est disponible sur toutes les variantes sauf le build 40 MHz (il dispose d'un accès aux données via le modem 4G, plus le WiFi sur le build WiFi), et toutes prennent en charge jusqu'à 2 000 contacts en PSRAM. Pour savoir ce que le build 40 MHz laisse de côté, consultez [Builds économie de batterie à 40 MHz](#builds-économie-de-batterie-à-40-mhz). Le build combiné `meck_max_ble_wifi` est décrit dans [Build combiné Bluetooth + WiFi](#build-combiné-bluetooth--wifi-t-deck-max).

**Puissance d'émission Bluetooth (v1.15+) :** `meck_max_ble` et `meck_max_ble_wifi` règlent la puissance d'émission Bluetooth à +15 dBm. Les autres builds Bluetooth, y compris ceux du T-Deck Pro, utilisent +9 dBm.

### 4G et audio en même temps

Comme le modem et le codec sont alimentés indépendamment via l'expandeur XL9555, l'écran d'accueil du MAX propose l'ensemble des applis qui sont sinon réparties entre les variantes audio et 4G du T-Deck Pro :

- **[P] Livres audio / Audio** (Audiobooks / Audio) : le lecteur de livres audio (lit aussi la musique de `/audiobooks/music`)
- **[K] Alarme** (Alarm) : le réveil, avec des sons MP3 personnalisés
- **[T] Téléphone** (Phone) : l'appli SMS et téléphone pour les appels et les SMS via le modem 4G
- **[B] Navigateur** (Browser) : le navigateur web et client IRC (builds BLE / WiFi)
- **[F] Recherche** (Discover) : la découverte de nœuds
- **Mic** : l'écran Messages vocaux (Voice Messages), avec enregistrement et lecture sur le MAX (voir la remarque ci-dessous)

La sortie audio passe par un multiplexeur de haut-parleur partagé : quand un appel, une sonnerie ou une tonalité de notification du modem est joué, le MAX bascule le haut-parleur vers le modem ; pour les livres audio, les alarmes et les MP3 de notification, il le bascule vers le codec ES8311.

> **L'enregistrement et la lecture des messages vocaux fonctionnent tous deux sur le MAX.** La capture passe par l'ADC de l'ES8311 et la lecture par la sortie de l'ES8311 (le même chemin que pour les livres audio et les alarmes). L'envoi des messages enregistrés sur le réseau mesh est implémenté, mais n'a pas encore été vérifié de bout en bout sur le MAX.

### Antenne (interne / externe)

Le MAX dispose à la fois d'une antenne interne intégrée et d'un connecteur d'antenne externe MMCX, avec un réglage dans **Paramètres** (Settings) pour choisir entre les deux. **La valeur par défaut est Interne (Internal)** : si vous voulez utiliser une antenne MMCX externe, modifiez d'abord ce réglage dans Paramètres.

### Notifications par vibreur (Buzzer)

Le MAX dispose d'un moteur haptique DRV2605 : chaque canal (ainsi que la boîte de réception des MP) peut donc être réglé pour vibrer au lieu de jouer un son. Dans **Paramètres → Canaux** (Settings → Channels), mettez un canal en surbrillance et appuyez sur **T** pour ouvrir le sélecteur de son de notification : sur le MAX, une entrée **Buzzer (vibreur)** (Buzzer (vibrate)) apparaît juste sous "Défaut (silencieux)" ("Default (silent)"). La sélectionner fait vibrer le moteur à l'arrivée de messages sur ce canal, au lieu de faire sonner le buzzer ou de jouer un son. Consultez [Sons de notification personnalisés](#sons-de-notification-personnalisés-v110).

### Écran tactile capacitif et boutons

Le MAX et le T-Deck Pro ont tous deux un écran tactile capacitif. Le MAX utilise une puce tactile CST328 avec le pilote tactile Hynitron. Les T-Deck Pro v1.1 étaient aussi livrés avec une CST328, mais certains exemplaires plus récents, toujours étiquetés v1.1, ont une CST3530 et nécessitent les builds HYN (voir [Puces tactiles et builds HYN](#puces-tactiles-et-builds-hyn)). Ce que le MAX ajoute, ce sont **trois boutons capacitifs** le long du bas du cadre avant, que le T-Deck Pro n'a pas :

| Bouton | Action |
|--------|--------|
| **Cœur** | Allume/éteint l'éclairage frontal e-ink (à la luminosité définie dans les paramètres, voir ci-dessous) |
| **Bulle de dialogue** | Dans une conversation de canal ou de MP ouverte, ouvre vos messages prédéfinis (voir [Messages prédéfinis](#messages-prédéfinis-t-deck-max)) ; ailleurs, ouvre le sélecteur de canal (comme un appui sur M) |
| **Avion en papier** | Accès rapide à la boîte de réception des MP |

### Messages prédéfinis (T-Deck Max)

Enregistrez jusqu'à dix messages tout prêts et envoyez-en un d'une simple pression.

**Configuration :** ouvrez **Paramètres → Messages prédéfinis >>** (Settings → Canned Messages >>) pour voir les dix emplacements. Sélectionnez un emplacement et appuyez sur **Entrée** pour le modifier, tapez le message (133 caractères maximum ; un compteur indique combien vous en avez utilisé) et appuyez sur **Entrée** pour l'enregistrer. Enregistrer un emplacement vide l'efface, et **Shift+Backspace** annule sans enregistrer.

**Envoi :** avec un canal ou une conversation MP ouverte, appuyez sur le bouton **bulle de dialogue**. La liste de vos messages enregistrés apparaît ; touchez-en un pour l'envoyer à ce canal ou à ce contact. Toucher n'importe où ailleurs, ou appuyer sur une touche, ferme la liste. Si tous les emplacements sont vides, **Aucun message prédéfini** (No canned messages) s'affiche à la place.

### Éclairage frontal et luminosité du rétroéclairage

Le MAX dispose d'un éclairage frontal e-ink. La luminosité à laquelle il s'allume se règle dans **Paramètres → Rétroéclairage** (Settings → Backlight Brightness), de **5 % à 100 %** par pas de 5 % (100 % par défaut). Le bouton capacitif Cœur allume et éteint l'éclairage frontal à ce niveau. Ce réglage n'apparaît que sur le MAX.

**Alt+B** allume et éteint aussi l'éclairage frontal. Par défaut, Alt+B l'allume à son niveau le plus bas. Lorsque **Paramètres → Expérimental → Rétroéclairage Alt+B** (Settings → Experimental Features → Change Backlight to Alt+B) est activé, Alt+B utilise plutôt le niveau de Rétroéclairage, et le bouton Cœur n'allume et n'éteint plus l'éclairage frontal (voir [Fonctions expérimentales](#fonctions-expérimentales-v115)).

### Rétroéclairage du clavier

Appuyez sur **les deux touches Shift en même temps** pour allumer ou éteindre le rétroéclairage du clavier.

La luminosité se règle dans **Paramètres -> LED clavier** (Settings -> Keyboard LED), juste sous Rétroéclairage (Backlight Brightness), de **5 % à 100 %** par pas de 5 % (**50 %** par défaut). Avant la v1.12.3, le rétroéclairage du clavier était fixé à un niveau très faible. Une modification de ce réglage prend effet la prochaine fois que vous allumez le rétroéclairage.

### GPS multi-constellation

Le GPS du MAX est configuré pour un positionnement multi-constellation (GPS, Galileo et BeiDou) via la commande `$PCAS04,7`, pour une acquisition de position plus rapide et une meilleure couverture qu'un GPS mono-constellation.

---

## Répéteur distant (T-Deck Pro 4G)

> **À FAIRE : cette section doit être entièrement documentée.** La fonctionnalité est implémentée et disponible. Ébauche du plan ci-dessous.

La variante répéteur distant (`meck_remote_repeater`) transforme une carte T-Deck Pro 4G en répéteur MeshCore dédié, administrable à distance par MQTT via le réseau cellulaire. Le répéteur fonctionne comme un répéteur MeshCore normal sur le maillage (il relaie les paquets et répond aux connexions invité), mais **l'administration (synchronisation de l'horloge, envoi d'annonce, redémarrage, lecture de l'état, configuration) s'effectue à distance par MQTT** via le [tableau de bord Meck-Mycelium](https://pelgraine.github.io/Meck-Mycelium), et non par la connexion standard avec mot de passe administrateur du maillage. L'appareil se connecte à un broker MQTT (HiveMQ Cloud recommandé, offre gratuite disponible) via les données cellulaires, publie sa télémétrie (temps de fonctionnement, batterie, force du signal, température, nombre de voisins) et s'abonne aux commandes d'administration.

**Sections à rédiger :**

- **Prérequis** : T-Deck Pro 4G (A7682E), carte SIM active avec forfait de données, carte SD, compte de broker MQTT
- **Configurer HiveMQ Cloud** : création pas à pas d'un compte gratuit, configuration du cluster, identifiants
- **Configuration de la carte SD** : `/remote/mqtt.cfg` (broker, port, nom d'utilisateur, mot de passe, identifiant de l'appareil) et `/remote/apn.cfg` facultatif
- **Déploiement** : flasher `meck_remote_repeater`, insérer la SIM et la carte SD, séquence de démarrage, indicateurs d'état à l'écran
- **Utilisation du tableau de bord** : connecter Meck-Mycelium à votre broker MQTT, commandes disponibles (synchronisation de l'horloge, envoi d'annonce, redémarrage, lecture de l'état), affichage de la télémétrie
- **Dépannage** : problèmes courants (SIM qui ne s'enregistre pas sur le réseau, échecs d'authentification MQTT, détection automatique de l'APN)

---

## Répéteur WiFi

> **À FAIRE : cette section doit être entièrement documentée.** La fonctionnalité est implémentée. Ébauche du plan ci-dessous.

Les variantes de répéteur WiFi transforment un appareil en répéteur MeshCore dédié, administrable à distance par MQTT via WiFi, comme le répéteur distant cellulaire mais en WiFi au lieu de la 4G. Le répéteur relaie le trafic du maillage et répond normalement aux connexions invité, mais **l'administration s'effectue à distance par MQTT** via le tableau de bord Meck-Mycelium, et non par la connexion standard avec mot de passe administrateur du maillage. Disponible pour les plateformes suivantes :

| Variante | Environnement | Plateforme |
|---------|------------|----------|
| Répéteur WiFi T-Deck Pro | `meck_wifi_repeater` | LilyGo T-Deck Pro |
| Répéteur WiFi Heltec V3 | `meck_wifi_repeater_heltec_v3` | Heltec V3 |
| Répéteur WiFi Heltec V4 | `meck_wifi_repeater_heltec_v4` | Heltec V4 |
| Répéteur WiFi Heltec V4 (headless) | `meck_wifi_repeater_heltec_v4_headless` | Heltec V4 (sans écran) |

**Sections à rédiger :**

- **Prérequis** : appareil, réseau WiFi, compte de broker MQTT, carte SD (T-Deck Pro) ou configuration SPIFFS (Heltec V4)
- **Configuration de la carte SD** : `/remote/wifi.cfg` (plusieurs SSID possibles) et `/remote/mqtt.cfg`
- **Particularités du Heltec V4** : pas de lecteur de carte SD, configuration stockée dans SPIFFS, variante headless ou avec écran
- **Mises à jour OTA** : synchronisation de l'heure par NTP, téléchargement HTTP du firmware via WiFi
- **Tableau de bord** : même tableau de bord Meck-Mycelium que pour le répéteur distant cellulaire

---

## Application web Meck-Mycelium

L'[application web Meck-Mycelium](https://pelgraine.github.io/Meck-Mycelium) est un compagnon dans le navigateur qui se connecte à votre appareil MeshCore via BLE (avec WebBLE dans Chrome) ou à votre broker MQTT pour administrer les répéteurs distants.

**Fonctionnalités :**

- **Lecture des messages vocaux** : les messages vocaux envoyés depuis un appareil Meck Audio apparaissent sous forme de bulles de lecture à toucher dans la vue des MP. Le décodage Codec2 se fait entièrement dans le navigateur grâce à WebAssembly : aucune installation d'application ni aucun matériel audio n'est nécessaire côté réception.
- **Tableau de bord des répéteurs distants** : connectez-vous à votre broker MQTT pour administrer les répéteurs distants (cellulaires ou WiFi). Consultez la télémétrie en direct, envoyez des commandes d'administration (synchronisation de l'horloge, envoi d'annonce, redémarrage, lecture de l'état) et gérez des répéteurs hors de portée LoRa. Cela remplace la connexion standard avec mot de passe administrateur du maillage pour les variantes de répéteur distant.
- **Fonctions compagnon standard** : messagerie, contacts, messages de canal via BLE.

Ouvrez **https://pelgraine.github.io/Meck-Mycelium** dans Chrome sur votre téléphone ou votre ordinateur.

> **Remarque :** WebBLE nécessite Chrome (ou un navigateur basé sur Chromium). Safari et Firefox ne prennent pas en charge WebBLE.

---

## À propos de MeshCore

MeshCore est une bibliothèque C++ légère et portable qui permet le routage de paquets multi-sauts pour les projets embarqués utilisant LoRa et d'autres radios par paquets. Elle s'adresse aux développeurs qui veulent créer des réseaux de communication résilients et décentralisés fonctionnant sans Internet.

## Qu'est-ce que MeshCore ?

MeshCore prend désormais en charge une gamme d'appareils LoRa, que l'on peut flasher facilement sans avoir à compiler soi-même le firmware. Les utilisateurs peuvent flasher un binaire précompilé avec des outils comme esptool.py et interagir avec le réseau via une console série.
MeshCore permet de créer des réseaux maillés sans fil, comme Meshtastic et Reticulum, mais en privilégiant un routage de paquets multi-sauts léger pour les projets embarqués. Contrairement à Meshtastic, conçu pour une communication LoRa occasionnelle, ou à Reticulum, qui offre des fonctions réseau avancées, MeshCore concilie simplicité et évolutivité, ce qui le rend idéal pour les solutions embarquées sur mesure, où les appareils (nœuds) peuvent communiquer sur de longues distances en relayant les messages par des nœuds intermédiaires. C'est particulièrement utile hors réseau, en situation d'urgence ou dans des contextes tactiques, lorsque l'infrastructure de communication traditionnelle n'est pas disponible.

## Fonctionnalités principales

* Routage de paquets multi-sauts
  * Les appareils peuvent relayer les messages à travers plusieurs nœuds, ce qui étend la portée au-delà de celle d'une seule radio.
  * Prend en charge un nombre configurable de sauts pour équilibrer l'efficacité du réseau et éviter un trafic excessif.
  * Les nœuds ont des rôles fixes : les nœuds "Compagnon" ne répètent aucun message, afin d'éviter l'utilisation de chemins de routage défavorables.
* Prise en charge des radios LoRa : fonctionne avec Heltec, RAK Wireless et d'autres matériels basés sur LoRa.
* Décentralisé et résilient : aucun serveur central ni Internet n'est nécessaire ; le réseau s'auto-répare.
* Faible consommation : idéal pour les appareils alimentés par batterie ou par énergie solaire.
* Simple à déployer : des applications d'exemple précompilées facilitent la prise en main.

## À quoi peut servir MeshCore ?

* Communication hors réseau : restez connecté même dans les zones isolées.
* Intervention d'urgence et reprise après sinistre : mettez en place des réseaux instantanés là où l'infrastructure est hors service.
* Activités de plein air : communication pour la randonnée, le camping et les raids d'aventure.
* Applications tactiques et de sécurité : usages militaires, forces de l'ordre et sécurité privée.
* IoT et réseaux de capteurs : collectez les données de capteurs distants et relayez-les vers un point central.

## Pour commencer

- Regardez la [vidéo d'introduction à MeshCore](https://www.youtube.com/watch?v=t1qne8uJBAc) d'Andy Kirby.
- Lisez notre section [Foire aux questions](./docs/faq.md).
- Téléchargez le firmware depuis la page [Releases](https://github.com/pelgraine/Meck/releases) et flashez-le en suivant les instructions ci-dessus.
- Connectez-vous avec un client pris en charge.

Pour les développeurs :

- Installez [PlatformIO](https://docs.platformio.org) dans [Visual Studio Code](https://code.visualstudio.com).
- Clonez le dépôt Meck et ouvrez-le dans Visual Studio Code.
- Compilez pour votre appareil cible en utilisant les noms d'environnement indiqués dans les tableaux des variantes de build ci-dessus.

## Clients MeshCore

**Firmware compagnon**

Vous pouvez vous connecter au firmware compagnon via BLE (variantes BLE du T-Deck Pro et du T-Deck Max) ou via WiFi (variantes WiFi du T-Deck Pro et du T-Deck Max, port TCP 5000).

> **Remarque :** sur le T-Deck Pro et le T-Deck Max, le Bluetooth est désactivé au démarrage : allez sur la page d'accueil Bluetooth et appuyez sur Entrée pour l'activer. Les builds WiFi activent le WiFi au démarrage et tentent de se connecter à votre réseau enregistré (voir [Compagnon WiFi](#compagnon-wifi)). Sur le build combiné `meck_max_ble_wifi`, le Bluetooth et le WiFi sont tous deux désactivés au démarrage (voir [Build combiné Bluetooth + WiFi](#build-combiné-bluetooth--wifi-t-deck-max)).

- Web : https://app.meshcore.nz
- Meck-Mycelium : https://pelgraine.github.io/Meck-Mycelium (lecture des messages vocaux, tableau de bord des répéteurs distants)
- Android : https://play.google.com/store/apps/details?id=com.liamcottle.meshcore.android
- iOS : https://apps.apple.com/us/app/meshcore/id6742354151?platform=iphone
- NodeJS : https://github.com/liamcottle/meshcore.js
- Python : https://github.com/fdlamotte/meshcore-cli

**Données de canal (v1.15+) :** les applications compagnon qui le prennent en charge peuvent envoyer et recevoir des paquets de données de canal (`PAYLOAD_TYPE_GRP_DATA` de MeshCore) sur vos canaux. Les données de canal reçues sont mises en file d'attente jusqu'à ce que l'application les récupère. Elles ne sont pas affichées sur l'appareil.

## 🛠 Compatibilité matérielle

MeshCore est conçu pour les appareils listés dans le [MeshCore Flasher](https://flasher.meshcore.io). Meck cible spécifiquement le LilyGo T-Deck Pro, le LilyGo T-Deck Max, le Heltec V3 (répéteur distant uniquement) et le Heltec V4 (répéteur distant uniquement).

## Contribuer

Merci de soumettre vos PR en prenant 'dev' comme branche de base !
Pour les modifications mineures, soumettez simplement votre PR et j'essaierai de l'examiner, mais pour tout changement plus 'conséquent', merci d'ouvrir d'abord une Issue et de lancer une discussion. Il vaut mieux commencer par exposer ce que vous voulez accomplir et essayer de parvenir à un consensus sur la meilleure approche, en particulier lorsque cela touche à la structure ou à l'architecture de ce code.

Voici quelques principes généraux que vous devriez essayer de respecter :
* Restez simple. S'il vous plaît, ne pensez pas comme un programmeur de langage de haut niveau. Pensez embarqué, et gardez un code concis, sans couches inutiles.
* Pas d'allocation dynamique de mémoire, sauf dans les fonctions setup/begin.
* Utilisez le même style d'accolades et d'indentation que dans les modules sources du cœur. (Un .clang-format sera probablement ajouté bientôt, mais merci de NE PAS reformater rétroactivement le code existant. Cela crée simplement des diffs inutiles qui rendent la recherche de problèmes plus difficile)

## Feuille de route / À faire

Plusieurs fonctionnalités assez importantes sont en préparation, sans calendrier précis pour l'instant. Dans un ordre en partie chronologique :

**T-Deck Pro :**
- [X] Radio compagnon : BLE
- [X] Saisie de texte pour les messages du canal Public dans le firmware compagnon BLE
- [X] Lecture et rédaction de tous les messages de canal dans le firmware compagnon BLE
- [X] Fonction MP autonome pour le firmware compagnon BLE
- [X] Liste des contacts avec filtrage pour le firmware compagnon BLE
- [X] Accès autonome à l'administration des répéteurs pour le firmware compagnon BLE
- [X] Synchronisation de l'heure par GPS avec réglage du fuseau horaire sur l'appareil
- [X] Écran Paramètres (Settings) avec préréglages radio, gestion des canaux et assistant de premier démarrage
- [X] Extension de l'appli SMS pour permettre les appels téléphoniques
- [X] Appli de lecture web basique avec client IRC
- [X] Écran de verrouillage avec minuterie de verrouillage automatique et veille basse consommation
- [X] Liste Entendus (Last heard) des annonces reçues passivement
- [X] Sélection tactile sur les écrans contacts, découverte, paramètres, lecteur de texte et notes
- [X] Écran de carte avec rendu des tuiles GPS
- [X] Environnement compagnon WiFi
- [X] Mise à jour du firmware par OTA depuis le téléphone
- [X] Boîte de réception des MP avec indicateurs de non-lus par contact
- [X] Gestion des messages des room servers et marquage comme lus à la connexion
- [X] Réveil avec sons MP3 personnalisés (variante audio)
- [X] Option utilisateur personnalisée pour un mode à grande police
- [X] Messages vocaux par LoRa (Codec2, variante audio)
- [X] Mode de sélection des contacts avec mise en favori, export, import et suppression par lot
- [X] Éditeur de chemin pour la gestion manuelle des routes des contacts
- [X] Répéteur distant avec administration MQTT par réseau cellulaire (variante 4G)
- [X] Répéteur distant WiFi avec administration MQTT
- [X] Gestionnaire de fichiers SD (SD File Manager) via Outils OTA (OTA Tools)
- [X] Prise en charge de 2 000 contacts (PSRAM, toutes les variantes)
- [X] Écran sélecteur de canal avec badges de non-lus
- [X] Portée de région (compatibilité MeshCore v1.15+)
- [X] Polices au choix (Classic, Noto Sans, Montserrat)
- [X] Sélecteur d'emoji étendu (79 emoji, réordonnés, défilement en boucle)
- [X] Cache de 1 000 chemins d'annonces (PSRAM)
- [X] Prise en charge des caractères accentués / diacritiques (tchèque, polonais, français, allemand, latin étendu)
- [X] Défilement par page (Shift+W/S) sur tous les écrans de liste
- [X] Véritable extinction (sommeil profond, aucune source de réveil)
- [X] BLE 2M PHY, DLE et intervalle d'écriture plus rapide
- [X] Écran de trace de route avec sélecteur de contact et saisie manuelle du chemin (v1.9)
- [X] Conservation des MP après redémarrage (v1.9)
- [X] Suppression de l'historique des messages par canal (v1.10)
- [X] Préférences de notification par canal avec prise en charge des @mentions (v1.10)
- [X] Sons de notification personnalisés par canal : variante audio (MP3) et variante 4G (WAV via le modem) (v1.10)
- [X] Menu Jeux (Games) avec Snake et Démineur (Minesweeper) (v1.10)
- [X] Émulateur Game Boy / Game Boy Color, T-Deck Max (v1.14) : voir [Jeux](#jeux-v110)
- [X] Émulateur Game Boy sur le T-Deck Pro (v1.14 ; pas encore de son sur le Pro)
- [X] Son de l'émulateur Game Boy sur le T-Deck Max, via l'ES8311, touche Mic pour couper le son (v1.14)
- [ ] Son de l'émulateur Game Boy sur les variantes audio du T-Deck Pro (PCM5102A)
- [X] Émulateur retiré du menu Jeux sur les builds économie de batterie à 40 MHz (v1.14)
- [ ] État de l'horloge temps réel Game Boy dans les fichiers de sauvegarde
- [X] MAX_GROUP_CHANNELS porté à 40 pour tous les builds (v1.10)
- [X] Prise en charge des canaux privés avec génération aléatoire du secret (v1.11)
- [X] Partage de canal par MP chiffré avec ajout automatique à la réception (v1.11)
- [X] Export/import de la configuration sur carte SD avec sections au choix (v1.11)
- [X] Correction de l'ancienneté des contacts pour les nœuds dont l'horloge est bloquée ou en retard (v1.11)
- [X] Sélecteur d'emoji étendu (79 emoji) (v1.11)
- [X] Page d'accueil Timezones (horloge mondiale), Local (Home) plus deux fuseaux (v1.15)
- [X] Expérimental (Experimental Features) : interface en français, Gain RX renforcé (RX boosted gain), Reset AGC (AGC reset interval), Supprimer tous les contacts (delete all contacts) (v1.15)
- [X] Polices accentuées Noto Sans et Montserrat en taille PETIT (Tiny) (v1.15)
- [X] Popups de connexion WiFi indiquant la cause de l'échec ; activation et désactivation du WiFi depuis la page d'accueil WiFi (v1.15)
- [X] Popups de progression de l'export et de l'import (v1.15)
- [X] Lignes de canal dans Paramètres : les lignes longues alternent entre le début et la fin (v1.15)
- [X] Paquets de données de canal pour l'application compagnon (v1.15)
- [X] Les MP suivent la région choisie dans l'application compagnon (v1.15)
- [X] Correctifs de MeshCore v1.18 (v1.15)
- [ ] Build combiné Bluetooth + WiFi pour le T-Deck Pro
- [ ] Corriger le rendu M4B pour permettre la lecture des livres audio avec chapitres
- [ ] Meilleur décodage JPEG et PNG
- [ ] Améliorer le rendu EPUB et la prise en charge du format EPUB
- [ ] Mise en silence de la sonnerie des appels entrants (limitation matérielle : l'A7682E pilote le haut-parleur de façon autonome sur RING, aucun moyen logiciel de couper le son)

**T-Deck Max :**
- [X] Portage de base : écran e-ink, clavier TCA8418, LoRa, batterie, GPS (découplé de la variante T-Deck Pro)
- [X] Radio compagnon : variantes BLE, WiFi et autonome (`meck_max_ble` / `meck_max_wifi` / `meck_max_standalone`)
- [X] 4G (A7682E) et audio (ES8311) fonctionnant simultanément grâce à l'extenseur d'E/S XL9555
- [X] Initialisation du codec audio ES8311 : livres audio, musique, alarmes et MP3 de notification
- [X] Lecture de musique depuis `/audiobooks/music` (sans signets, durée des WAV lue dans l'en-tête)
- [X] Appli SMS et téléphone disponible aux côtés des applis audio sur le même appareil
- [X] Prise en charge du tactile capacitif CST328 (pilote Hynitron intégré au dépôt)
- [X] Trois boutons capacitifs en façade : cœur (éclairage frontal), bulle de dialogue (sélecteur de canal), avion en papier (boîte de réception des MP)
- [X] Éclairage frontal e-ink à luminosité réglable (Paramètres, 5 à 100 %)
- [X] Activation et désactivation du rétroéclairage du clavier (appuyez sur les deux touches Shift)
- [X] Option de notification par canal Buzzer (vibreur) (Buzzer (vibrate)) via le moteur haptique DRV2605
- [X] GPS multi-constellation : GPS, Galileo, BeiDou (`$PCAS04,7`)
- [X] Corrections du décalage X et du retour à la ligne de l'écran d'accueil e-ink
- [X] Lecture des messages vocaux (chemin de sortie ES8311)
- [X] Enregistrement des messages vocaux sur le MAX (chemin de capture ADC de l'ES8311)
- [X] Build combiné Bluetooth + WiFi, le basculement redémarre l'appareil (`meck_max_ble_wifi`) (v1.15)
- [X] Option Rétroéclairage Alt+B (Change Backlight to Alt+B) dans Expérimental (v1.15)
- [X] Puissance d'émission Bluetooth +15 dBm sur `meck_max_ble` et `meck_max_ble_wifi` (v1.15)
- [ ] Prise en charge du gyroscope / IMU BHI260AP (0x28) : nouveau sur le MAX, pas encore utilisé par Meck

**Heltec V4 :**
- [X] Répéteur distant WiFi avec administration MQTT
- [X] Variante de répéteur WiFi headless (sans écran)

**Heltec V3 :**
- [X] Répéteur distant WiFi avec administration MQTT

## 📞 Obtenir de l'aide

- Rejoignez le [Discord MeshCore](https://discord.gg/KWFeY45sN) pour discuter avec les développeurs et obtenir de l'aide de la communauté.

## 📜 Licence

La bibliothèque [MeshCore](https://github.com/meshcore-dev/MeshCore) en amont est publiée sous la **MIT License** (Copyright © 2025 Scott Powell / rippleradios.com). Le code propre à Meck (écrans de l'interface, fonctions d'aide à l'affichage, intégration matérielle) est lui aussi fourni sous la MIT License.

Cependant, ce firmware est lié à des bibliothèques dont les conditions de licence diffèrent. Comme certaines dépendances utilisent la licence copyleft **GPL-3.0** (GxEPD2, ESP32-audioI2S) et d'autres la licence **LGPL-2.1** (Codec2, ESPAsyncWebServer, Arduino_LPS22HB), le binaire du firmware combiné est de fait soumis aux obligations de la GPL-3.0 lorsqu'il est distribué. Veuillez consulter les licences individuelles ci-dessous si vous comptez redistribuer ou modifier ce firmware.

### Bibliothèques tierces

| Bibliothèque | Licence | Auteur / Source |
|---------|---------|-----------------|
| [MeshCore](https://github.com/meshcore-dev/MeshCore) | MIT | Scott Powell / rippleradios.com |
| [RadioLib](https://github.com/jgromes/RadioLib) | MIT | Jan Gromeš |
| [GxEPD2](https://github.com/ZinggJM/GxEPD2) | GPL-3.0 | Jean-Marc Zingg (T-Deck Pro) |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) | BSD | Adafruit |
| [SensorLib](https://github.com/lewisxhe/SensorLib) | MIT | Lewis He |
| [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) | GPL-3.0 | schreibfaul1 / Wolle |
| [Codec2](https://github.com/sh123/esp32_codec2_arduino) | LGPL-2.1 | sh123 (portage ESP32) |
| [Peanut-GB](https://github.com/deltabeard/Peanut-GB) | MIT | Mahyar Koshkouei / deltabeard (cœur de l'émulateur Game Boy, intégré au dépôt) |
| [JPEGDEC](https://github.com/bitbank2/JPEGDEC) | Apache-2.0 | Larry Bank / bitbank2 |
| [PNGdec](https://github.com/bitbank2/PNGdec) | Apache-2.0 | Larry Bank / bitbank2 |
| [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) | LGPL-2.1 | Hristo Gochkov / me-no-dev (OTA) |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | MIT | Nick O'Leary (MQTT) |
| [Arduino Crypto](https://github.com/rweather/arduinolibs) | MIT | Rhys Weatherley |
| [base64](https://github.com/Densaugeo/base64_arduino) | MIT | densaugeo |
| [CRC32](https://github.com/bakercp/CRC32) | MIT | Christopher Baker |
| [RTClib](https://github.com/adafruit/RTClib) | MIT | Adafruit |
| [Melopero RV3028](https://github.com/melopero/Melopero_RV-3028_Arduino_Library) | MIT | Melopero |
| [MicroNMEA](https://github.com/stevemarple/MicroNMEA) | MIT | Steve Marple (GPS) |
| [CayenneLPP](https://github.com/ElectronicCats/CayenneLPP) | MIT | Electronic Cats |
| [Adafruit ST7735/ST7789](https://github.com/adafruit/Adafruit-ST7735-Library) | MIT | Adafruit (Heltec V4 TFT) |
| [INA226](https://github.com/RobTillaart/INA226) | MIT | Rob Tillaart |
| [Arduino_LPS22HB](https://github.com/arduino-libraries/Arduino_LPS22HB) | LGPL-2.1 | Arduino |
| Pilotes de capteurs Adafruit¹ | MIT / BSD | Adafruit |
| [Sensirion I2C SHT4x](https://github.com/Sensirion/arduino-i2c-sht4x) | BSD-3-Clause | Sensirion |

¹ Comprend INA219, INA260, INA3221, AHTX0, BME280, BMP280, BME680, BMP085, SHTC3, MLX90614, VL53L0X, tous sous licence MIT ou BSD. Utilisés via le gestionnaire de capteurs pour la surveillance environnementale facultative.

Les textes complets des licences de chaque dépendance sont disponibles dans leurs dépôts respectifs, dont les liens figurent ci-dessus.

Aucun logiciel Game Boy, de quelque nature que ce soit, n'est inclus dans Meck ; l'émulateur exécute uniquement les fichiers ROM que l'utilisateur place sur la carte SD.