# Playlist Editing — USB-owned implementation

Reorder tracks in USB playlists using main Browse.
This writes the library; keep a separate USB backup.
A per-boot backup is made before the first save.

Requires the matching R113 extraction foundation and appended SDK services.
This folder contains the actual feature implementation, not a settings-only wrapper.
See ../../ENGINE-EXTRACTION.md for shared bridges, resident lifetime,
hardware-validation limits and the required matched firmware package.
