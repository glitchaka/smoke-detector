# smoke-detector

Visor y extractor local de recursos desde ROMs.

Esta herramienta nació como una variante independiente de `promptemon-steidium`. No depende del repo del juego y está enfocada en inspeccionar y exportar assets sin incluir ROMs ni contenido propietario en el repositorio.

## ROMs

Coloca tus dumps locales dentro de:

```text
smoke-detector/roms/
```

La biblioteca detecta GB, GBC, GBA, NDS, CIA, 3DS, CCI y CXI. Puedes tener varias ROMs simultáneamente.

La lectura de recursos está separada del visor 3D:

- **Nintendo 3DS:** conserva la imagen ROM completa, indexa todo el RomFS legible y expande recursivamente contenedores DARC reconocibles. Los contenedores comprimidos LZH8 (`0x40`) y LZ11 (`0x11`) se descomprimen para poder inspeccionar su contenido, pero el archivo original del RomFS también se conserva.
- **Nintendo DS:** conserva la imagen ROM completa, lee el encabezado, ARM9, ARM7 y el árbol NitroFS mediante FNT/FAT.
- **GB / GBC:** exporta la imagen y el encabezado, separa la ROM en bancos de 16 KiB y genera vistas TGA 2bpp de cada banco para localizar gráficos sin afirmar que cada bloque sea un asset semántico.
- **GBA:** exporta la imagen y el encabezado, localiza y descomprime streams BIOS LZ77 (`0x10`) y contenedores `AT4PX`, genera vistas TGA 4bpp y vistas BGR555 para bloques compatibles, y crea un índice de cadenas ASCII con su offset dentro de la ROM.

Los recursos derivados conservan su offset u origen para poder volver siempre al lugar exacto dentro de la ROM. Las vistas gráficas son interpretaciones de inspección; los bytes originales se conservan.

## Interfaz Windows

`smoke_detector.exe` usa una interfaz Win32 propia en tema oscuro, con visor 3D, selector de modelos, estado de operación y progreso del dump.

Controles principales:

- `Modelos`: busca y cambia el modelo 3D sin reiniciar.
- `ROMs`: muestra las ROMs detectadas, proveedor y cantidad de recursos indexados.
- `Exportar modelo`: exporta el modelo BCH seleccionado con animaciones y texturas.
- `DUMP COMPLETO`: recorre todas las ROMs de `roms/` y escribe el dump completo en `exports/resources/`, mostrando recurso actual y progreso.
- `Idle`, `Accion 1`, `Accion 2`: reproducen animaciones disponibles.
- arrastrar con el botón izquierdo: rota el modelo.
- rueda: zoom.

El dump iniciado desde la GUI es **directory-only** para no duplicar innecesariamente una ROM grande dentro de `.sdpack` y de un autoextraíble. La utilidad de consola puede seguir generando esos paquetes portables.

## Exportación de modelos

Los paquetes de modelos se escriben localmente en:

```text
exports/<ROM>/<MODELO>/
```

Estructura:

```text
model/model.bch          BCH descomprimido del modelo
animations/*.bch         contenedores BCH de animación asociados
textures/*.tga           texturas decodificadas
preview/model.obj        vista estática interoperable
preview/model.mtl        materiales del OBJ
audio/*                  audio o bancos de audio relacionados
manifest.json             origen, recursos y clips detectados
```

## Dump completo de recursos

La salida de cada ROM queda en:

```text
exports/resources/<ROM>/
├─ manifest.json
└─ files/
   ├─ raw/                 # imagen ROM completa
   ├─ system/              # cabeceras / ejecutables cuando el formato los expone
   ├─ romfs/               # Nintendo 3DS
   ├─ nitrofs/             # Nintendo DS
   └─ ...
```

Cuando un archivo del RomFS contiene un DARC reconocido, sus archivos internos se conservan con una jerarquía legible:

```text
romfs/ui.arc.cx
romfs/ui.arc.cx.contents/menu/button.bclim
romfs/ui.arc.cx.contents/font/main.bcfnt
```

Si aparece un contenedor propietario que Smoke Detector todavía no sabe abrir, su archivo original sigue presente en el dump y además la imagen ROM completa queda en `raw/`; por tanto no se descartan bytes aunque aún no exista un decodificador semántico para ese formato.

## Exportación portable por consola

`resource_export` recorre todas las ROMs de `roms/` y además del directorio genera un `.sdpack` y, en Windows, un autoextraíble si encuentra `smoke_sfx_stub.exe`.

```text
exports/resources/<ROM>/
exports/resources/<ROM>.sdpack
exports/resources/<ROM>.resources.exe
```

Uso normal desde la raíz:

```powershell
.\build\Release\resource_export.exe
```

También acepta rutas explícitas:

```powershell
.\build\Release\resource_export.exe .\roms .\exports
```

El formato `.sdpack` puede extraerse con:

```powershell
.\build\Release\resource_unpack.exe .\exports\resources\mi_rom.sdpack
```

> Una ROM no equivale a un sistema de archivos universal. 3DS y NDS permiten enumerar archivos; GB/GBC/GBA mezclan código y datos en el espacio del cartucho. `smoke-detector` conserva los bytes originales y solo etiqueta como recurso decodificado aquello que puede reconocer de forma verificable.

## Windows

Desde la raíz:

```powershell
.\build.cmd
```

Los ejecutables quedan en:

```text
build\Release\smoke_detector.exe
build\Release\resource_export.exe
build\Release\resource_unpack.exe
build\Release\smoke_sfx_stub.exe
```

ROMs, builds y assets exportados están ignorados por Git y no se suben al repositorio público.
