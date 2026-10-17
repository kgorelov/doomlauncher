# Problem
Currently modules are tight too closely to WADs which makes a lot of confusion. It implies complex directory structure and that people will deploy modules along with WADs.
Which usually isn't the case. People would have one directory full of downloaded WADs.


# Solution

Separate WADs from modules and savedirs. Inverse control flow. Now mudules will provide support to run the game if the user has the WAD. Many modues will come pre-installed to support known WAD. The menu item will be defined by WAD+Module. WADs will be discivered in wad_dirs which will include known system dirs and user dirs. And of course a user will be able to add a single WAD (iwads config section).


## Config and the state
~/.config/doomlauncher/config.toml
~/.config/doomlauncher/state.toml

The config points to module dirs (modules must be an array, we should have a directory with pre-installed modules and user modules),

In the config:
  - `wad_dirs` is used for automatic WAD discovery, for instance to find a dependency (DOOM2.WAD for example)
  - `iwad` is used for manually adding WADs

## WADs
Some WADs must be autodiscovered using default wad_dirs. Other WADs will be discivered using wad_dirs from the user config ~/.config/doomlauncher/config.toml
When a WAD (or a set of WADs) satisfy a module requirements the module becomes active and visible as a menu entry.

A user shall a way to add a single (or multiple WADs) using either by editing the config or by using the "Add Dialog" (to be implemented) navigating the filesystem, selecting the file[s] and adding them. After the WAS is added, module discovery is lauched. If a module for this WAD is found it is activated. If there's no module for the WAD a user is prompted to create one using a simple dialog that set's the complevel, allows to select dependencies from the list of other known WADs (prioritizing main doom.wad and doom2.wad on the top of the list).

## Save dirs
All save dirs must reside under ~/.config/doomlauncher/saves/<modulename/


## Modules

Pre-installed modules will be stored in system directory.
Modules added by user will be stored under ~/.config/doomlauncher/modules/


### Module detection
When a WAD (or WADs) is added a hash is calculated from each WAD contents (sha1) and it's used to determin whether modules exist which dependencies are satisfied by this wad.


### Adding modules
Modules can be added either by adding module configs to the modules gmaes directory or in the doomlauncher by choosing a WAD (or WADs) and filling out a form with dependencies, complevel, supported engines, etc...
