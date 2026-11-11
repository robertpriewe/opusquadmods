# R116 release presentation metadata

## R117: display-only choice ordering and safe labels

`choices.order` may contain lines such as `mode=3,0,1,2`. Each line must be a
complete permutation of one registered choice option. The host ignores invalid,
out-of-range or duplicate indices. This changes button positions only: saved
settings, API/native values and acknowledgements keep their original meaning.
Painting, touch and main-knob actions use the same mapping. Choice labels now
have 64 bytes of storage, allowing `Native Height 30 (recommended)` in full.

Use ASCII in menu titles and descriptions: the current native text path does
not interpret curly punctuation as UTF-8. For example, use `TYTO's`, not a curly
apostrophe. Presentation files are read at software startup.

Each mod folder can supply `menu.title`, `version.txt`, `description.txt`,
`menu.order` and its own 48x48 P5 grayscale `icon.pgm`.
The host reads bounded regular files without following symlinks at startup.
Missing metadata falls back to the compiled mod information. `menu.order`
is applied after initialization, so a compiled priority cannot override it.
The selected page heading displays the mod name and version.

The curated profile ships fifteen feature folders; excluded tools and unfinished
features remain outside its MODS directory. Version files describe individual
mods, not the firmware release. Preserve existing version numbers and increment
them when behavior changes; use 1.0 only when no prior version exists.

Four-Deck Waveforms now owns the render/timing provider as well as geometry.
Its fourth setting, `mode`, is saved in `MODS/four-deck/settings.txt` and defaults
to 3 (Native Height 30). The previous standalone smooth-waveforms folder must not
be loaded alongside the combined implementation: there is one timing provider.
Older foundations do not allow that provider on the waveform-layout service;
the matching R116 foundation is required. Switching OFF restores stock timing.

Release pages use concise descriptions and hide developer status text. TYTO's
capture option is omitted from its release page, but report collection remains
available internally. Diagnostics are not part of the fifteen-mod release list.
No source, user settings or excluded feature implementation is deleted.
