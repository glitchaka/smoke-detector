# smoke-detector

Visor y extractor local de recursos 3D desde ROMs.

Esta herramienta nació como una variante independiente de `promptemon-steidium`. No depende del repo del juego y está enfocada en inspeccionar y exportar assets.

## ROMs

Coloca tus dumps locales dentro de:

```text
smoke-detector/roms/
```

La biblioteca detecta GB, GBC, GBA, NDS, CIA, 3DS, CCI y CXI. Las ROMs 3DS con un RomFS legible y recursos BCH pueden aportar modelos 3D. Las ROMs antiguas siguen apareciendo en la biblioteca aunque no contengan geometría 3D compatible.

Puedes tener varias ROMs simultáneamente. `Modelos...` reúne en un solo catálogo los modelos encontrados en todas las fuentes 3D compatibles y muestra la ROM de origen.

## Visor

- `Modelos...`: busca y cambia el modelo 3D sin reiniciar.
- `ROMs...`: muestra las ROMs detectadas y qué proveedor puede leerlas.
- `Idle`: vuelve a la animación idle.
- `Accion 1` / `Accion 2`: reproduce clips no-loop disponibles.
- arrastrar con el botón izquierdo: rota el modelo.
- rueda: zoom.
- `Exportar`: crea un paquete del modelo seleccionado.

## Exportación

Los paquetes se escriben localmente en:

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
audio/*                  audio o bancos de audio 3DS relacionados
manifest.json             origen, recursos y clips detectados
```

Para audio se prefieren recursos cuyo nombre/ruta coincide con el modelo. Cuando el juego guarda los sonidos dentro de un banco global, se conserva el banco `BCSAR/CSAR` para no perder esos datos. Los contenedores de audio se exportan sin recomprimir ni convertir destructivamente.

ROMs, builds y assets exportados están ignorados por Git y no se suben al repositorio público.

## Windows

Desde la raíz:

```powershell
.\build.cmd
```

El ejecutable queda en:

```text
build\Release\smoke_detector.exe
```
