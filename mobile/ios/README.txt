DUAL HD  -  DS EMULATOR FOR iPHONE / iPAD
(with the Yu-Gi-Oh! 5D's WC2009 HD look built in)
=========================================

What's here
-----------
  WC2009HD.xcodeproj   The Xcode project. Open this.
  WC2009HD/            The app's code, icon and fonts.
  lib/                 The game engine, already built for iPhone/iPad.
  include/             Its header.


Install it on your iPhone (with Xcode)
--------------------------------------
  1. Plug the iPhone into the Mac with a cable and unlock it.
  2. Double-click WC2009HD.xcodeproj to open it in Xcode.
  3. Click "WC2009HD" at the top of the left sidebar, then the "Signing & Capabilities" tab.
     Under Team, choose your Apple ID (Add an Account... if it's not there).
     If Xcode says the bundle identifier isn't available, change it to something of your own,
     for example io.github.yourname.wc2009hd
  4. At the top of the window, pick your iPhone as the run destination.
  5. Press the Play button (or Cmd+R).
  6. The first time, the iPhone asks you to trust the developer:
     Settings > General > VPN & Device Management > your Apple ID > Trust.
     It may also ask you to turn on Developer Mode:
     Settings > Privacy & Security > Developer Mode (the iPhone restarts).
  With a free Apple ID the app works for 7 days; after that, plug in and press Play again
  (your saves are kept).


Add games
---------
  The app shows its own folder in the Files app and in Finder.

  From the Mac: with the iPhone plugged in, open Finder, click the iPhone in the sidebar,
  open the "Files" tab, and drag your .nds files onto "Dual HD". For Yu-Gi-Oh! 5D's WC2009
  also drag its WC2009-HD folder (next to the .nds). Folders with a game inside work too.

  Or on the iPhone: Files app > On My iPhone > Dual HD.

  Every game shows up in the Library. Pull down to refresh after adding some.
  Custom cover art: put a .png or .jpg with the same name as the game next to it.


Playing
-------
  Tap a game to play; touch and hold for more (start from the in-game save, remove the
  resume point).
  Touch the bottom screen like on the DS. The buttons are on screen too, and game
  controllers work.
  In game, the menu button (three lines) has save/load state, settings, restart and
  "Back to library". The double arrow is fast forward.
  Settings (gear in the Library): resolution 2x to 8x, smooth or sharp screen, smooth 2D,
  the WC2009 HD options, buttons, opacity, vibration, resume, sound, fast forward speed.
  Saves and save states go to a "Saves" folder next to each game, same format as the
  computer version.
