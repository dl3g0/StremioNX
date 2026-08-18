# StremioNX v0.2

Release **v0.2** de **StremioNX**, un cliente de Stremio para Nintendo Switch (homebrew). Navegá catálogos, elegí películas o episodios de series, y reproducí directo desde los add-ons que instales en tu servidor local.

> Proyecto homebrew con fines educativos/personales. No está afiliado a Stremio ni a Nintendo.

## Cambios desde v0.1

### Correcciones

- **Crash al ocultar/reordenar catálogos**: se eliminaron varios *use-after-free* que ocurrían al modificar la visibilidad u orden de los catálogos desde Ajustes. Se corrigió el manejo de foco en `Application::giveFocus(nullptr)` (el foco quedaba apuntando a memoria liberada), el reciclado de celdas de la grilla de catálogos (`dequeueReusableCell`) y la liberación de celdas sin cola (`freeView`). Además se restaura el foco de navegación después de reconstruir la vista de Ajustes.
- **Búsqueda sin resultados**: buscar series/películas (p. ej. "la casa de papel") podía devolver "Sin resultados" porque el parseo de JSON fallaba cuando la respuesta de Cinemeta traía campos `null` (`runtime`, `description`...). El parseo ahora tolera valores ausentes o `null` en manifest, catálogos, streams, episodios, meta y resultados de búsqueda.
- **Loader del reproductor**: ahora muestra el logo de la película/serie mientras carga el video, en lugar del póster.

### Nuevas funcionalidades

- **Sección de búsqueda**: nuevo buscador de películas y series con filtro por tipo (Películas / Series / Todo) y grilla de resultados que abre la ficha del contenido.
- **Series completas**: soporte de series con selector de temporada, lista de episodios con sinopsis y carga de fuentes por episodio, con filtro por add-on desde la barra lateral.

### Mejoras

- **Nuevo logo de la app**: icono propio en el menú home del Switch y logo en Ajustes → Sobre.
- **Versión 0.2.0** (se actualiza el NACP y el panel Sobre).

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