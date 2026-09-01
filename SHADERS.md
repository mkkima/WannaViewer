# Shaders

WannaViewer uses unmodified mpv-compatible GLSL files. The build downloads the
official Anime4K v4.0.1 archive, verifies its digest, and places the original
files under `shaders/Anime4K`. User files belong under `shaders/Custom`.

Presets are data-driven in `presets/shaders.json`:

- Ctrl+0: Off
- Ctrl+1: Anime4K A (restore)
- Ctrl+2: Anime4K B (soft restore)
- Ctrl+3: Anime4K C (denoise)
- Ctrl+4: Anime4K A+A (second restore pass)
- Ctrl+5: Anime4K B+B (second soft-restore pass)
- Ctrl+6: Anime4K C+A (denoise followed by restore)

These are the official Anime4K v4 processing modes using the upstream Fast
shader chains. The bundled preset aliases migrate the former Fast, Balanced,
High, and Ultra identifiers to mode A.

Paths must be relative, remain inside the shader root after canonicalization,
exist, and have a `.glsl` extension. A preset is cleared before its ordered
files are appended. mpv compilation errors are surfaced and logged; selecting a
missing or invalid preset immediately returns to Off.

Automatic performance downgrade is off. The player never changes image quality
mid-film without an explicit future opt-in policy. Use benchmark mode to compare
presets against 16.67 ms (60 fps) or 41.67 ms (24 fps) frame budgets with an
external GPU timing tool.
