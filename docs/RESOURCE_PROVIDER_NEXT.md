# Próximos proveedores de alto nivel

La extracción genérica ya cubre contenedores y sistemas de archivos. Para convertir bytes internos en recursos listos para motores se deben implementar proveedores específicos de juego.

Prioridad para la línea Pokémon:

1. GBA: tiles, paletas, sprites, mapas, texto, música/SFX y tablas Pokémon para Ruby/Sapphire/Emerald/FireRed/LeafGreen.
2. NDS: abrir NARC y formatos gráficos Nitro (`NCGR`, `NCLR`, `NSCR`, `NANR`, `NCER`) además de SDAT.
3. 3DS: abrir GARC y convertir BCH/BCRES, BCLIM/CTPK y audio CTR a formatos interoperables.
4. Normalizar la salida semántica a `models/`, `textures/`, `sprites/`, `maps/`, `audio/`, `data/` y `text/` sin borrar el recurso original.

Regla: toda conversión debe mantener el recurso fuente dentro del paquete o dejar su id y origen trazables en `manifest.json`.
