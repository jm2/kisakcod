KisakCOD @VERSION@ for macOS (Apple silicon, macOS 26 or later)
================================================================

KisakCOD is an open-source reimplementation of the Call of Duty 4 multiplayer
game. It plays with the Steam release of Call of Duty 4, version 1.8. You need
your own licensed copy of the game; no game data is included here.

Install
-------
1. Drag KisakCOD.app onto the Applications folder in this window.
2. Copy "Dedicated Server" to any folder if you want to host games.

First launch
------------
KisakCOD is not notarized by Apple, so macOS blocks the first launch.
1. Open KisakCOD.app once. macOS says it cannot verify the app; click Done.
2. Open System Settings > Privacy & Security, scroll to Security, and click
   "Open Anyway" next to KisakCOD. Confirm with your password.
Or, in Terminal:
    xattr -dr com.apple.quarantine /Applications/KisakCOD.app

Game data
---------
The app never writes inside itself. Your game data, profiles, config and logs
live in:
    ~/Library/Application Support/KisakCOD
Copy these from your Steam Call of Duty 4 folder into it:
    main/  zone/  players/ (optional, for existing profiles)
On a Windows PC with Steam the folder is usually
    C:\Program Files (x86)\Steam\steamapps\common\Call of Duty 4

Dedicated server
----------------
This disk image is read-only, so copy "Dedicated Server" to a folder you can
write to before running it. The server needs the same game data, and keeps
its logs and downloads in fs_homepath, which defaults to fs_basepath. From
Terminal:
    cd "<folder with KisakCOD-dedi>"
    ./KisakCOD-dedi +set fs_basepath "<folder with main and zone>" +map mp_crash
To keep using the app's data folder instead, add
    +set fs_homepath ~/Library/Application\ Support/KisakCOD
The first time it listens on the network, allow incoming connections when
macOS asks, or add it in System Settings > Network > Firewall > Options.
Clients on other machines cannot reach it until you do.

Licenses
--------
KisakCOD is free software under the GNU General Public License version 3
(LICENSE.txt). The libraries it bundles, and the code it is built from, are
listed with their licenses in THIRD-PARTY-NOTICES.txt.

Source code: https://github.com/jm2/kisakcod
