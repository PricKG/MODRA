# MODRA

MODRA es un centro personal de control y memoria para organizar proyectos, tarjetas de seguimiento y conocimiento técnico reutilizable desde la consola.

## Estado actual

La versión `0.1.0` contiene la base técnica y gestión persistente de proyectos, tareas y notas personales desde la interfaz FTXUI. Cada tarea pertenece directamente a un proyecto, sin una entidad intermedia. El responsable es texto opcional y no existe un catálogo de personas.

## Qué es MODRA

MODRA está diseñada para un único usuario. Sus tareas funcionan como tarjetas personales de radar: algo que hacer, revisar, consultar, esperar o seguir. También pueden resumir un ticket o asunto cuya información oficial vive fuera de MODRA.

Jira y otras herramientas externas siguen siendo la fuente oficial del trabajo del equipo. MODRA no intenta reemplazar sus tickets, comentarios, asignaciones ni flujos.

La pregunta para evaluar una funcionalidad nueva es:

> ¿Esto me ayuda a recordar, entender, revisar o encontrar información más rápido?

MODRA no planifica usuarios, permisos, catálogo formal de personas, subtareas, dependencias complejas, comentarios encadenados, auditoría detallada, flujos configurables, métricas de productividad ni gestión de carga del equipo.

## Dependencias

- C++20 y CMake 3.24 o posterior.
- FTXUI, CLI11, SQLite, spdlog, nlohmann/json y Catch2.
- Git para descargar dependencias mediante `FetchContent`.

CMake descarga las dependencias fijadas a versiones concretas durante la configuracion. No se requiere Conan, vcpkg ni una instalacion del SDK de SQLite.

## Configurar, compilar y probar

En Windows, con Visual Studio 2026 y la carga de trabajo **Desktop development with C++** instalada:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Ejecutar

Desde la raiz del repositorio, luego de compilar:

```powershell
.\build\debug\Debug\modra.exe
.\build\debug\Debug\modra.exe --help
.\build\debug\Debug\modra.exe --version
.\build\debug\Debug\modra.exe doctor
```

`modra` abre una estructura permanente con encabezado, menú lateral, panel principal y barra de ayuda. Las flechas cambian la sección y actualizan inmediatamente el panel, sin presionar `Enter`. `Tab` o `→` mueve el foco al contenido; `Esc` vuelve al nivel anterior y, desde la raíz de una sección, devuelve el foco al menú. `j` y `k` no son atajos de MODRA. Los demás atajos alfabéticos aceptan mayúsculas y minúsculas (`n` equivale a `N`). Desde el menú, `q` solicita confirmación y una segunda `q` cierra MODRA.

## Instalador Release para Windows

MODRA genera un único instalador en la raíz del proyecto. No necesita la carpeta `migrations/` al ejecutarse e incluye los runtimes de Visual C++ requeridos.

```powershell
cmake --preset release-windows
cmake --build --preset release-windows
.\packaging\windows\BuildInstaller.ps1
```

El resultado es:

```text
MODRA-Setup-0.1.0.exe
MODRA-Setup-0.1.0.exe.sha256
```

El instalador utiliza IExpress, incluido en Windows, y no necesita NSIS ni WiX. Instala MODRA por usuario en `%LOCALAPPDATA%\Programs\MODRA`, agrega esa carpeta al `PATH`, crea un acceso en el menú Inicio y registra MODRA en **Aplicaciones instaladas**. No elimina la base ni los datos personales al desinstalar.

Después de instalar, abrí una terminal nueva y ejecutá:

```powershell
modra --version
modra doctor
modra
```

## Gestión de proyectos

Seleccioná **Proyectos** con las flechas y el listado aparecerá inmediatamente; usá `Tab` o `→` para mover el foco al contenido. La pantalla permite crear, consultar, editar, buscar y archivar proyectos. Los proyectos archivados se conservan en SQLite y se consultan desde una vista separada; no existe eliminación física.

Atajos disponibles:

- Flechas: mover la selección.
- `Enter`: abrir el detalle.
- `n`: crear un proyecto.
- `e`: editar el proyecto seleccionado.
- `a`: solicitar el archivado con confirmación.
- `v`: alternar entre proyectos activos y archivados.
- `/`: buscar por nombre o alias.
- `r`: recargar el listado.
- `Ctrl+S`: guardar el formulario.
- `Esc` o `q`: cancelar o volver.
- `?`: abrir la ayuda contextual.

Estados permitidos:

- `planned` — Planificado.
- `active` — Activo.
- `paused` — En pausa.
- `completed` — Finalizado.
- `archived` — Archivado.

Ejemplo básico:

1. Ejecutá `modra` y abrí **Proyectos**.
2. Presioná `n`, ingresá el nombre y revisá el alias generado.
3. Completá opcionalmente descripción, fechas y ruta local.
4. Presioná `Ctrl+S` para guardar.
5. Usá `e` para editar o `a` para archivar.

El alias se normaliza a minúsculas y admite letras ASCII, números, guion y guion bajo. Las fechas usan el formato `YYYY-MM-DD`; la fecha objetivo no puede ser anterior a la inicial. La ruta local no necesita existir todavía.

## Gestión de tareas

Seleccioná un proyecto y presioná `t`, tanto desde el listado como desde su detalle. La pantalla de tareas permite crear, consultar, editar, buscar y archivar tareas del proyecto seleccionado.

Atajos disponibles:

- Flechas: mover la selección.
- `Enter`: abrir el detalle de la tarea.
- `n`: crear una tarea.
- `e`: editar la tarea seleccionada.
- `a`: archivar con confirmación.
- `u`: desarchivar la tarea seleccionada desde la vista de archivadas.
- `v`: alternar entre tareas activas y archivadas.
- `/`: buscar por título.
- `r`: recargar.
- `Ctrl+S`: guardar el formulario.
- `Esc` o `q`: cancelar o volver al proyecto.
- `?`: abrir la ayuda contextual.

Toda tarea requiere un proyecto y un título. El nombre de la persona responsable es opcional, informativo y se escribe libremente. Su ausencia no representa un error. La tarea comienza en estado `pending` y prioridad `normal` si no se indica otra. Una tarea bloqueada requiere un motivo; al marcarla como finalizada se registra automáticamente la fecha de finalización. La fecha visible como **Seguimiento** puede representar una revisión personal o un límite real.

Tipos permitidos: `technical`, `administrative`, `management`, `research`, `documentation` y `follow_up`.

Estados permitidos: `pending`, `in_progress`, `blocked`, `in_review`, `completed` y `cancelled`.

Prioridades permitidas: `low`, `normal`, `high` y `critical`.

## Vista global Mi trabajo

La sección **Mi trabajo** consulta y gestiona tareas de todos los proyectos sin entrar en cada proyecto. Las tareas de proyectos archivados no aparecen en vistas activas y quedan disponibles, en modo consulta, dentro de **Archivadas**.

Vistas rápidas:

- `1` Todas las tareas no archivadas.
- `2` Hoy.
- `3` Atrasadas: la fecha de seguimiento o límite ya pasó.
- `4` Próximas durante los siguientes siete días.
- `5` Bloqueadas.
- `6` Finalizadas durante los últimos siete días.
- `7` Archivadas.
- `v` alterna rápidamente entre Todas y Archivadas.

La búsqueda `/` incluye título, descripción, responsable, nombre y alias del proyecto. Se combina con los filtros abiertos mediante `f`: proyecto, responsable textual, tareas sin responsable, estado, tipo, prioridad y presencia de fecha de seguimiento. `c` limpia los filtros dentro del panel.

El orden inicial prioriza seguimientos atrasados, prioridad crítica/alta y fechas próximas. `s` alterna entre orden recomendado, fecha de seguimiento, prioridad, estado, proyecto, responsable y última actualización. Los filtros y el orden no se guardan al cerrar MODRA.

Desde esta vista se puede crear (`n`), editar (`e`), archivar (`a`), desarchivar (`u`) y abrir el detalle (`Enter`). El formulario global permite elegir o cambiar el proyecto usando únicamente proyectos no archivados. Una tarea solo puede desarchivarse si su proyecto continúa activo. Desde el detalle, `p` abre el proyecto relacionado.

## Notas y base de conocimiento

La sección **Conocimiento** guarda información personal reutilizable en Markdown dentro de SQLite: notas generales, información técnica, soluciones, minutas, consultas SQL, procedimientos, configuraciones y referencias. Una nota puede ser global o relacionarse opcionalmente con un proyecto y una tarea.

Tipos disponibles:

- `general` — General.
- `technical` — Técnica.
- `solution` — Solución.
- `meeting` — Reunión.
- `sql` — SQL.
- `procedure` — Procedimiento.
- `configuration` — Configuración.
- `reference` — Referencia.

Las favoritas se identifican con `*` y aparecen primero. Cada nota conserva su propio valor de favorita: marcar una no modifica las demás y se pueden mantener tantas como sean necesarias. El listado permite buscar por título, contenido, tipo, proyecto y tarea. Los filtros se alternan por tipo (`t`), proyecto o notas globales (`p`) y alcance (`g`: todas, favoritas, con tarea o sin tarea); `c` limpia todos los filtros. El alcance **Favoritas** muestra todas las notas favoritas activas.

Atajos principales:

- Flechas: navegar.
- `Enter`: abrir el detalle.
- `n`: crear una nota.
- `e`: editar metadatos y contenido.
- `f`: marcar o quitar favorita.
- `a`: archivar con confirmación.
- `u`: desarchivar la nota seleccionada desde la vista de archivadas.
- `v`: alternar activas y archivadas.
- `/`: buscar.
- `t`, `p`, `g`: cambiar filtros.
- `c`: limpiar filtros.
- `r`: recargar.
- `Esc` o `q`: cancelar o volver.
- `?`: abrir la ayuda contextual.

En el detalle, `o` vuelve a abrir el contenido en el editor externo; `p` abre el proyecto y `t` la tarea relacionada. Las flechas desplazan el contenido. En los detalles de proyecto y tarea, `n` crea una nota relacionada y las teclas `1` a `5` abren las notas recientes mostradas.

### Editor externo

MODRA edita el título y el cuerpo juntos en un archivo temporal UTF-8 con extensión `.md`. El documento siempre comienza con una ayuda identificada por `MODRA_DOCUMENT_V1`, seguida del primer encabezado `#`, que representa el título definitivo. Todo lo posterior es el cuerpo Markdown:

````md
<!--
MODRA_DOCUMENT_V1

Formato:
- El primer encabezado "# " es el título del documento.
- Todo lo que aparece después es el cuerpo en Markdown.
- Se permiten subtítulos, listas, tablas, enlaces y bloques de código.
-->

# Problema de generación de PDF

## Contexto

El cierre mensual falla cuando...

```sql
SELECT * FROM monthly_closures;
```
````

El comentario es únicamente una guía del archivo editable: no se guarda como contenido ni aparece en el detalle. El encabezado `#` debe tener un título y el cuerpo posterior no puede quedar vacío. Los encabezados `##`, listas, tablas, comentarios HTML y bloques de código del cuerpo se conservan sin intentar renderizar Markdown de forma completa.

Al abrir una nota anterior, MODRA construye este documento usando el título y contenido ya almacenados; no se requiere migración ni se modifican notas sin que el usuario las edite. Si el título se cambia en el editor, el listado y el detalle utilizan el nuevo valor al guardar.

MODRA suspende temporalmente la interfaz, espera al editor, interpreta el documento y persiste título y cuerpo por separado en SQLite. El temporal se elimina después de guardar correctamente. Ante un fallo del editor, un formato inválido o un error de persistencia, la nota original permanece intacta y el temporal se conserva para recuperar el texto; el mensaje de error informa su ruta.

El editor se resuelve en este orden:

1. `editor.command` dentro de `config.json`.
2. Variable `VISUAL`.
3. Variable `EDITOR`.
4. `notepad.exe` en Windows; `nano` o `vi` cuando estén disponibles en otros sistemas.

Ejemplo de configuración:

```json
{
  "version": 1,
  "editor": {
    "command": "code --wait"
  }
}
```

Los comandos con rutas que contienen espacios deben escribir el ejecutable entre comillas. MODRA lanza el proceso directamente y no envía el contenido Markdown a una shell.

El intérprete reconoce solamente el formato controlado de MODRA: no es un parser Markdown general. El primer `# ` visible fuera de un bloque de código es el título; el resto se conserva como cuerpo.

## Dashboard

El dashboard es un panel visual, informativo y compacto. Sus cuatro tarjetas superiores muestran proyectos activos, tareas en radar, tareas para hoy y seguimientos atrasados. Las tarjetas, métricas y filas de tareas no son seleccionables y no abren otras pantallas.

También incluye:

- Distribución compacta de las tarjetas en radar por estado y prioridad.
- Los cinco próximos seguimientos, ordenados por fecha y prioridad.
- Hasta cinco tareas que requieren atención, sin duplicados: atrasadas, bloqueadas o críticas.
- Hasta cinco notas favoritas activas, ordenadas por última modificación, con tipo, proyecto, tarea y fecha de actualización.
- Un indicador `+ N favoritas más` cuando existen otras; `Enter` abre la nota seleccionada o Conocimiento filtrado por todas las favoritas.
- Estados vacíos específicos y un inicio guiado cuando todavía no existen datos.

El contenido se carga al seleccionar Dashboard y se actualiza con `r` o al volver desde Conocimiento. La barra lateral permanece siempre visible. `Tab` o `→` mueve el foco al panel de favoritas; allí las flechas seleccionan una nota y `Enter` abre su detalle. El último renglón abre Conocimiento con el filtro **Favoritas**. `Esc`, `←` o `q` devuelve el foco al menú.

Una nota favorita global se identifica como `Global`. Las notas relacionadas muestran el proyecto y, cuando corresponde, `Tarea: <título>`. Si la tarea o el proyecto están archivados, el dashboard lo indica sin ocultar la nota ni impedir abrirla. Archivar una nota la quita del panel; restaurarla conserva su condición de favorita.

En terminales amplias las tarjetas y gráficos se distribuyen en columnas. En tamaños medianos las tarjetas se organizan en dos filas. En terminales estrechas la barra lateral reduce su ancho pero permanece visible, mientras los paneles se apilan y los títulos largos se recortan. Por debajo de 52 columnas, el panel muestra un aviso para ampliar la terminal y mantiene disponible el menú.

El dashboard evita métricas de productividad. Sus cantidades sirven para decidir qué revisar, no para medir rendimiento personal o de equipo.

## Roadmap

1. Revisión y alineación del producto.
2. Notas y base de conocimiento.
3. Captura rápida.
4. Seguimientos y recordatorios personales.
5. Referencias externas opcionales en tareas.
6. Comandos frecuentes.
7. Backups y restauración.
8. Consulta Git/SVN.
9. Exportaciones.
10. Mejoras basadas en uso real.

No forman parte del roadmap el historial detallado de tareas, comentarios encadenados, dependencias complejas, catálogo de personas, gestión formal del equipo, métricas de productividad ni funciones equivalentes a Jira.

## Datos locales

- Windows: `%LOCALAPPDATA%\MODRA`
- Linux: `$XDG_DATA_HOME/modra` o `~/.local/share/modra`
- macOS: `~/Library/Application Support/MODRA`

El directorio contiene `modra.db`, `config.json`, `backups/`, `exports/` y `logs/`. El log principal se escribe en `logs/modra.log`.

## Migraciones de base de datos

Los archivos versionados de [`migrations/`](migrations/) son la única fuente de verdad del esquema. Durante la configuración, CMake valida sus nombres, los ordena por versión y genera código C++ dentro de `build/` con el SQL embebido. Por eso el ejecutable no necesita encontrar la carpeta `migrations/` en tiempo de ejecución.

Una migración futura debe agregarse como un nuevo archivo `NNN_nombre.sql`, con una versión no utilizada y un nombre claro. Una migración que ya fue aplicada es inmutable: cualquier modificación posterior del esquema requiere otro archivo versionado; el SQL no debe copiarse en `Database.cpp` ni en otro código C++.

## Estructura

- `cmake/`: cabeceras configuradas por CMake.
- `migrations/`: SQL versionado y legible de las migraciones.
- `src/application/`: informacion y coordinacion basica de la aplicacion.
- `src/domain/`: entidades Project, Task y Note con sus valores controlados.
- `src/infrastructure/`: datos locales, SQLite y deteccion del entorno.
- `src/ui/`: interfaz interactiva FTXUI.
- `tests/`: pruebas unitarias y de integracion con recursos temporales.

## Limitaciones actuales

- El responsable se conserva deliberadamente como texto opcional; no se planifica un catálogo formal de personas.
- Las notas no tienen etiquetas, adjuntos, enlaces entre sí, historial de versiones ni renderizado Markdown completo.
- No se planifican dependencias complejas ni comentarios encadenados; las etiquetas quedan pospuestas hasta demostrar una necesidad personal real.
- La vista Mi trabajo no asume qué responsable representa al usuario.
- Los filtros y ordenamientos globales no persisten entre ejecuciones.
- No se pueden restaurar proyectos archivados ni eliminarlos físicamente.
- La búsqueda de proyectos se realiza en memoria sobre el listado cargado.
