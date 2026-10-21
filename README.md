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

- **Nintendo 3DS:** indexa y exporta todos los archivos de un RomFS legible. Si el contenido está cifrado o no se puede abrir, conserva acceso a la imagen ROM como recurso bruto.
- **Nintendo DS:** lee el encabezado, ARM9, ARM7 y el árbol NitroFS mediante FNT/FAT.
- **GB / GBC / GBA:** conserva la imagen completa y el encabezado de cartucho como base sin inventar un sistema de archivos que esas plataformas no tienen. Los decodificadores específicos de juego se pueden montar sobre esta capa.

## Visor 3D

Las ROMs 3DS con RomFS legible y recursos BCH pueden aportar modelos 3D.

- `Modelos...`: busca y cambia el modelo 3D sin reiniciar.
- `ROMs...`: muestra las ROMs detectadas y qué proveedor puede leerlas.
- `Idle`: vuelve a la animación idle.
- `Accion 1` / `Accion 2`: reproduce clips no-loop disponibles.
- arrastrar con el botón izquierdo: rota el modelo.
- rueda: zoom.
- `Exportar`: crea un paquete del modelo seleccionado.

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

## Exportación completa de recursos

`resource_export` recorre todas las ROMs de `roms/` y genera tres representaciones:

```text
exports/resources/<ROM>/         directorio directamente utilizable
exports/resources/<ROM>.sdpack  paquete portable smoke-detector
exports/resources/<ROM>.resources.exe   autoextraíble en Windows
```

Uso:

```powershell
.\build\Release\resource_export.exe .\roms .\exports
```

Cada directorio contiene `manifest.json` y `files/` conservando la jerarquía del recurso original. El manifiesto indica ROM, formato, proveedor, id original, ruta exportada, tipo y tamaño.

El formato `.sdpack` es deliberadamente simple y autocontenido. Puede extraerse con:

```powershell
.\build\Release\resource_unpack.exe .\exports\resources\mi_rom.sdpack
```

En Windows, si `smoke_sfx_stub.exe` está junto a `resource_export.exe`, se genera además un `.resources.exe`. Ese archivo lleva el paquete adjunto y se extrae a sí mismo al ejecutarlo; no necesita la ROM original ni smoke-detector para entregar los recursos.

> Una ROM no equivale a un sistema de archivos universal. 3DS y NDS permiten enumerar archivos; GB/GBC/GBA requieren decodificadores específicos por juego para convertir gráficos, mapas, audio y tablas internas a formatos de alto nivel. La arquitectura nueva deja esos decodificadores como proveedores enchufables en lugar de etiquetar bytes arbitrarios como assets.

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
