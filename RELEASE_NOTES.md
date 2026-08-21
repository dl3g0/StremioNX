# StremioNX v0.3

Release **v0.3** de **StremioNX**, un cliente de Stremio para Nintendo Switch (homebrew). Navegá catálogos, elegí películas o episodios de series, y reproducí directo desde tus complementos y torrents en tu servidor local.

> Proyecto homebrew con fines educativos/personales. No está afiliado a Stremio ni a Nintendo.

## Cambios y Novedades en v0.3

### Hotfixes de emergencia y estabilidad en reproducción

- **Solución definitiva a cuelgues por saturación de red / sockets**:
  - Eliminada la inyección masiva concurrente de subtítulos externos que saturaba el límite de 16 sockets BSD del sistema operativo de la Switch.
  - Subtítulos desactivados por defecto para arranque instantáneo de cualquier stream sin bloqueos.
- **Compatibilidad con servidores y CDNs Debrid (TorBox, RealDebrid, AllDebrid, etc.)**:
  - Configurado `User-Agent` de navegador moderno y cabeceras estándar para evitar errores `ERR_INVALID_RESPONSE` y bloqueos 403 Forbidden.
  - Reconexión y reintentos automáticos de FFmpeg (`stream-lavf-o = reconnect=1,reconnect_streamed=1,reconnect_delay_max=5`) ante demoras de servidores Debrid.
  - Eliminada la pausa forzada de búfer al arrancar (`demuxer-cache-wait`).
- **Estabilidad de Audio y Video**:
  - **Downmix automático a Stereo 48kHz PCM 16-bit** (`audio-format = s16`, `audio-samplerate = 48000`, `audio-channels = stereo`): elimina errores del chip de sonido (`ao/hos: Error writing audio to device`) y tartamudeos (*audio underruns*) en pistas de audio multicanal 5.1/7.1 (Dolby Digital, DTS, Atmos).
  - **Aceleración por Hardware GPU Tegra X1** (`hwdec = auto`, `opengl-glfinish = yes`, `vd-lavc-dr = yes`): reproducción a 60 FPS sin sobrecarga de CPU.
  - **Exclusión de carátulas adjuntas** (`audio-display = no`): evita que se muestren imágenes estáticas en lugar del video real.
  - **Solución al bloqueo de fuentes de `libass` en Horizon OS** (`sub-font-provider = none`, `sub-font = sans-serif`).
  - **Avance continuo de fotogramas**: integración directa de `mpv_render_context_update` en el bucle de dibujo de OpenGL.

### Motor de streaming por torrent

1. Soporte completo de trackers devueltos por complementos como Torrentio (`sources`) y fallback de trackers de alta disponibilidad.
2. Resolución rápida de metadatos (BEP 9).
3. **Caché en RAM de 128 MB**: buffer continuo para evitar congelaciones de reproducción.
4. Cancelación limpia con botón **(B)** en cualquier momento sin congelaciones ni cuelgues.
5. **Sincronización con cuenta Stremio**: inicio de sesión y sincronización directa de complementos desde tu cuenta oficial de Stremio.
6. **Ajustes como pestaña nativa**: interfaz moderna, fluida y 100% libre de crasheos de memoria o fugas.
7. **Servidor Web Local**: gestión de complementos vía `http://<ip-switch>:8080`.

## Características

- **Catálogos**: navegá add-ons basados en catálogos (Cinemeta, etc.) con grillas de pósters.
- **Series completas**: selector de temporada (reabrible desde la barra lateral), lista de episodios con sinopsis, y fuentes que se cargan al elegir un episodio.
- **Fuentes por add-on**: barra lateral con filtro por add-on (botón "Fuentes") para elegir de dónde sale el stream.
- **Reproducción**: playback vía `libmpv` con preferencias de idioma de audio y subtítulos.
- **Gestión de add-ons**: instalación/gestión vía servidor web local (`http://<ip>:8080`), reordenamiento y ocultado desde ajustes.
- **Búsqueda**: buscá películas y series con filtro por tipo (Películas / Series / Todo).

## Add-ons probados

Configuración de prueba: **aiostream** con **debrid TorBox**.

| Add-on | Tipo | Estado |
| ------ | ---- | ------ |
| Cinemeta | Catálogo | ✔ Probado |
| Torrentio | Fuentes | ✔ Probado |
| MediaFusion | Fuentes | ✔ Probado |
| StremThru Torz | Fuentes | ✔ Probado |
| StremThru Store | Fuentes | ✔ Probado |

## Instalación

1. Copiá `StremioNX.nro` a la carpeta `switch/` de tu tarjeta SD.
2. Lanzalo desde Homebrew Menu.
3. Conectate a tu servidor de add-ons desde los ajustes y agregá tus add-ons.

## Controles

| Entrada | Acción |
| ------- | ------ |
| **A** | Confirmar / abrir stream / seleccionar episodio |
| **B** | Volver (de streams → vuelve a la lista de episodios en series) |
| **Izq / Der** | Navegar catálogos y barras laterales |
| **X** | Abrir ajustes |

## Compilación

Requisitos: devkitPro (toolchain Switch), MSYS2, `libmpv`, `curl`, `libwebp`, `libnx`, CMake y pkg-config.

```bash
./build.sh                # build completo
./build.sh StremioNX.nro  # empaquetar solo el .nro
```

> Nota (Windows/MSYS2): CMake 4.0 con el generador "Unix Makefiles" regenera `compiler_depend.make` con rutas Windows mangled que rompen `make`. `build.sh` aplica un workaround con `sed` + reintentos.

## Librerías (open source)

borealis (Apache-2.0) · nlohmann/json (MIT) · curl/libcurl (curl/MIT) · mpv (GPL-2.0+) · libwebp (BSD-3-Clause) · libnx (devkitPro) · tinyxml2 (zlib) · yoga (MIT) · fmt (MIT) · tweeny (MIT) · nanovg (zlib)

## Autor

Hecho por **DL3G0**.