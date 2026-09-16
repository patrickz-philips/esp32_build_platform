# Slide Player Control for macOS

Double-click `slide-player.command` in Finder, select the board serial port,
and use the following controls:

- Left arrow: previous slide
- Right arrow: next slide
- Number followed by Return: open that numbered PNG or GIF
- Escape: quit

Close `idf.py monitor` or any other program using the serial port before opening
the controller. The legacy root-level `slide_player_serial.sh` command launches
the same controller. The controller uses Python 3 and has no third-party
dependencies.