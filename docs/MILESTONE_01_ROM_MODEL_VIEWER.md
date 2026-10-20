# Hito 01 — ROM Pokemon Model Viewer

El primer objetivo de `promptemon-steidium` no es el combate. Es comprobar que podemos convertir una ROM aportada por el usuario en contenido 3D manipulable dentro del motor.

## Flujo mínimo

1. Seleccionar/abrir una ROM local.
2. Detectar su formato y versión.
3. Enumerar los Pokémon que esa ROM contiene.
4. Seleccionar un Pokémon de la lista.
5. Cargar su modelo, materiales/texturas y set de animaciones disponibles.
6. Mostrarlo en una escena 3D sencilla.
7. Reproducir `Idle` en bucle mientras no se ejecuta otra acción.
8. Permitir rotar el modelo con arrastre horizontal/vertical del ratón.
9. Permitir zoom/acercamiento de cámara.
10. Mostrar las acciones/animaciones detectadas para ese Pokémon y permitir disparar algunas manualmente.
11. Al terminar una acción, regresar automáticamente a `Idle`.

## Criterios de aceptación

El hito se considera cumplido cuando, usando una ROM compatible real, podemos:

- ver una lista de Pokémon leída de la ROM;
- seleccionar uno sin reiniciar el programa;
- verlo renderizado en 3D con textura/material;
- observar una animación `Idle` continua;
- rotarlo 360° alrededor del eje vertical mientras sigue animándose;
- inclinar ligeramente la vista sin voltear el modelo;
- ejecutar al menos 2 acciones adicionales disponibles en sus datos;
- regresar a `Idle` al finalizar cada acción;
- seleccionar otro Pokémon y sustituir el anterior limpiamente.

## Fuera de alcance de este hito

Todavía no se implementan daño, turnos, estadísticas, movimientos, tipos, IA, matchmaking ni sincronización multijugador.

## Arquitectura

`IRomPokemonProvider` aísla el formato de la ROM del visor. Cada familia de juego tendrá su propio adaptador. El visor sólo recibe `PokemonAsset` normalizados y no debe conocer offsets ni formatos internos de Nintendo.

La capa gráfica debe usar W3D/WW3D2 ya importada al repositorio. Los recursos extraídos desde la ROM deben mantenerse locales y no versionarse en Git.
