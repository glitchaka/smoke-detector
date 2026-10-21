# Resource extraction layer

## Objetivo

Separar la lectura de una ROM de la interpretación de un modelo 3D. El visor BCH sigue existiendo, pero ya no define qué es un recurso exportable.

## Capas

1. `RomInspector` identifica el contenedor.
2. `IRomResourceSource` enumera y extrae recursos de una ROM.
3. `RomResourceArchive` agrega varias ROMs bajo rutas virtuales estables.
4. `ResourcePackageExporter` escribe los recursos, el manifiesto y el paquete `.sdpack`.
5. `resource_unpack` extrae `.sdpack` o un ejecutable SFX.

## Proveedores iniciales

### Nintendo 3DS

Reutiliza `RumbleWorldArchive`, que ya puede recorrer RomFS. La capa de recursos expone todos los archivos, no solo `.bch`. La clasificación por extensión es informativa; los bytes originales se conservan.

### Nintendo DS

Lee FNT/FAT y reconstruye NitroFS. También expone encabezado, ARM9 y ARM7 como recursos de sistema.

### GB / GBC / GBA

No poseen un sistema de archivos comparable a NitroFS/RomFS. El proveedor base exporta la imagen y el encabezado. Los extractores específicos de cada juego deben añadirse encima de `IRomResourceSource` para producir sprites, tilesets, mapas, texto, audio y tablas semánticas sin heurísticas destructivas.

## Formato SDPACK v1

Todos los enteros son little-endian.

```text
8 bytes   magic "SDPKG001"
u32       version = 1
u32       cantidad de archivos

repetido N veces:
  u32     longitud UTF-8 de la ruta
  u64     tamaño del archivo
  bytes   ruta relativa
  bytes   contenido
```

Un SFX es el ejecutable `smoke_sfx_stub` seguido por un SDPACK y este pie:

```text
8 bytes   magic "SDSFX001"
u64       offset donde comienza SDPACK
```

El extractor rechaza rutas absolutas, `.` y `..` para que el paquete no pueda escribir fuera del directorio destino.
