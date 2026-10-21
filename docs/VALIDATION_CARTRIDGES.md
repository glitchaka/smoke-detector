# Validación de cartuchos GB/GBC/GBA

Los ROMs de prueba no se guardan en el repositorio. Esta tabla registra únicamente el resultado de ejecutar el extractor sobre dumps locales aportados para validación.

## Resultado

| ROM de prueba | Plataforma | Recursos enumerados | Bytes extraídos/derivados | Resultado |
| --- | --- | ---: | ---: | --- |
| Pokemon Esmeralda [E] | GBA | 7023 | 56,625,284 | OK |
| Pokemon Mundo misterioso [E] | GBA | 1363 | 48,205,520 | OK |
| Pokemon Pinball RZ [E] | GBA | 78 | 22,400,197 | OK |
| Pokémon Rojo fuego [E] | GBA | 5843 | 46,799,602 | OK |
| Dragon Tales - Dragon Wings | GBC | 131 | 6,392,001 | OK |
| Dragon Warrior Monsters | GBC | 259 | 13,206,465 | OK |
| Ghosts 'N Goblins | GBC | 131 | 6,323,691 | OK |
| Harvest Moon 2 GBC | GBC | 259 | 12,847,767 | OK |
| Pokemon - Edicion Azul | GB/SGB | 131 | 6,423,731 | OK |
| Pokemon - Edicion Cristal | GBC | 259 | 12,885,869 | OK |
| Super Mario Bros. Deluxe | GBC | 131 | 6,396,512 | OK |

`OK` significa que el proveedor pudo enumerar todos sus recursos declarados y `extract()` pudo materializar cada uno sin excepción.

## Cobertura que ejercitan estas muestras

- detección por cabecera en lugar de confiar solo en la extensión; `Pokemon - Edicion Azul ... .gbc` se reconoce como Game Boy/SGB, no como CGB;
- títulos CGB con manufacturer code separado del título;
- códigos regionales GBA españoles/europeos (`BPES`, `BPRS`, `B24P`, `BPPP`);
- bancos de 16 KiB y vistas 2bpp para GB/GBC;
- streams BIOS LZ77 `0x10` en GBA;
- contenedores `AT4PX` de Pokémon Mystery Dungeon: Red Rescue Team;
- vistas 4bpp y BGR555 derivadas sin descartar el binario descomprimido;
- extracción de cadenas ASCII con offsets.

Las vistas de gráficos/paletas son interpretaciones de inspección. No se presentan como límites semánticos de assets cuando el formato del juego no aporta esa información.
