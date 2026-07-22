# MODRA — definición y reglas del producto

> **MODRA**: Organización, Decisiones, Responsables y Avances.  
> Comando principal: `modra`.

## 1. Definición del producto

MODRA es una herramienta personal de consola para un único usuario. Su objetivo es servir como centro local de control y memoria para el trabajo de desarrollo: recordar, entender, revisar y encontrar información con rapidez.

MODRA no es un producto comercial, colaborativo ni multiusuario. Jira u otras herramientas externas continúan siendo la fuente oficial de tickets, asignaciones, comentarios y flujos del equipo. MODRA puede guardar una referencia personal y simplificada de un tema externo, pero no debe replicar ni sincronizar completamente esas herramientas.

Toda funcionalidad nueva debe evaluarse con esta pregunta:

> **¿Esto me ayuda a recordar, entender, revisar o encontrar información más rápido?**

Si la respuesta es no, probablemente no corresponde a MODRA.

## 2. Propósito práctico

MODRA debe ayudar a:

- Ver rápidamente los proyectos y temas que están en el radar.
- Organizar actividades propias.
- Recordar qué revisar, consultar o seguir.
- Mantener referencias breves de asuntos importantes.
- Centralizar notas, conocimiento técnico y contexto reutilizable.
- Encontrar información que normalmente queda dispersa.
- Conservar los datos localmente con bajo mantenimiento.

La utilidad diaria, la velocidad y la claridad tienen prioridad sobre la cantidad de funcionalidades.

## 3. Principios

1. **Uso personal real**: la aplicación se diseña para una sola persona y su flujo cotidiano.
2. **Complemento, no réplica**: la información oficial del equipo permanece en Jira u otras fuentes externas.
3. **Información accionable**: el dashboard debe señalar qué conviene revisar, no medir productividad.
4. **Simplicidad**: preferir campos fijos, texto libre y operaciones directas frente a configuraciones complejas.
5. **Datos locales**: no requieren servidor, nube, autenticación ni permisos.
6. **Evolución por uso**: no preparar infraestructura para necesidades que todavía no aparecieron en el uso real.

## 4. Modelo funcional vigente

La jerarquía principal de trabajo es:

```text
Proyecto → Tarea
```

No existe el concepto funcional de módulos ni debe agregarse una capa equivalente con otro nombre.

Las notas son conocimiento independiente y su relación es opcional:

```text
Proyecto → Tarea
          ↘ Nota opcional

Proyecto → Nota opcional

Nota global
```

### 4.1 Proyectos

Un proyecto agrupa contexto personal y elementos en seguimiento.

Campos actuales:

- Nombre y alias.
- Descripción.
- Estado.
- Fecha inicial y fecha objetivo.
- Ruta local.
- Fechas de creación, actualización y archivado.

Estados actuales: Planificado, Activo, En pausa, Finalizado y Archivado.

El proyecto podrá reunir más adelante notas, referencias y accesos útiles, siempre como contexto personal.

### 4.2 Tareas o tarjetas de radar

Una tarea de MODRA es un elemento personal de seguimiento, no un ticket oficial completo.

Puede representar:

- Algo que hay que hacer.
- Algo que se debe revisar o consultar.
- Un tema cuya respuesta se está esperando.
- Algo que otra persona está haciendo y conviene seguir.
- Una referencia simplificada a Jira u otra fuente externa.
- Un pendiente técnico, administrativo o de gestión.

Conceptos permitidos:

- Proyecto obligatorio.
- Título.
- Descripción u observación breve.
- Responsable textual opcional.
- Tipo, estado y prioridad.
- Fecha de seguimiento o límite.
- Motivo de bloqueo.
- Referencia externa opcional en una etapa posterior.
- Archivado lógico.

Tipos actuales: Técnica, Administrativa, Gestión, Investigación, Documentación y Seguimiento.

Estados actuales: Pendiente, En curso, Bloqueada, En revisión, Finalizada y Cancelada.

Prioridades actuales: Baja, Normal, Alta y Crítica.

Los estados y prioridades son ayudas personales fijas. No deben evolucionar hacia flujos configurables o esquemas de aprobación.

### 4.3 Responsable textual

El responsable es texto libre opcional e informativo.

- Puede identificar a la persona de la que se espera algo.
- No implica asignación oficial, usuario, cuenta ni permiso.
- Su ausencia no es automáticamente un error o incidencia.
- Los nombres existentes pueden agruparse para filtros ignorando mayúsculas y espacios.
- No existe ni se planifica como prioridad un catálogo formal de personas.

## 5. Áreas principales

### Dashboard

Debe responder rápidamente:

- ¿Qué tengo que revisar hoy?
- ¿Qué está atrasado o bloqueado?
- ¿Qué temas importantes requieren atención?
- ¿Cuáles son los próximos seguimientos?
- ¿Qué proyectos están en el radar?

El dashboard es una vista exclusivamente informativa. Sus tarjetas, listados y métricas no son seleccionables y no deben abrir Proyectos, Mi trabajo ni detalles de tareas; el acceso a esas áreas se realiza desde la navegación principal.

No debe convertirse en un panel de productividad, rendimiento personal o carga de equipo.

### Proyectos

- Contexto general y ruta local.
- Referencias importantes.
- Tareas en el radar.
- Notas personales relacionadas opcionalmente.

### Mi trabajo

- Cosas que debo hacer o consultar.
- Temas que estoy esperando.
- Seguimientos próximos.
- Vista global de tarjetas personales.

El nombre visible puede seguir siendo **Mi trabajo** aunque una tarjeta mencione a otra persona como responsable textual.

### Conocimiento

Esta es una de las áreas centrales:

- Notas técnicas y soluciones a errores.
- Consultas SQL y fragmentos de código.
- Procedimientos y minutas.
- Configuraciones e información de ambientes.
- Referencias reutilizables.

Las notas son personales, se almacenan como Markdown dentro de SQLite y pueden ser globales o relacionarse opcionalmente con un proyecto y una tarea. El editor externo configurado, `$VISUAL` o `$EDITOR` modifica el contenido extenso; MODRA no implementa un editor Markdown completo.

Conocimiento no debe evolucionar hacia una wiki, colaboración, versionado, adjuntos ni documentación empresarial oficial. Jira y las fuentes externas continúan siendo la referencia oficial del equipo.

### Herramientas

- Comandos frecuentes.
- Consulta de Git y SVN.
- Backups y restauración.
- Exportaciones y accesos rápidos.

Las integraciones de control de versiones comenzarán en modo consulta.

## 6. Funcionalidades fuera de alcance

No implementar ni mantener como objetivo prioritario:

- Historial detallado de cambios de tareas o auditoría automática.
- Comentarios encadenados, respuestas o menciones.
- Subtareas o dependencias complejas.
- Flujos configurables o aprobaciones.
- Catálogo formal de personas.
- Usuarios, autenticación, roles o permisos.
- Gestión formal, carga u horas del equipo.
- Métricas de productividad o rendimiento.
- Funciones colaborativas o chat.
- Replicación o sincronización completa con Jira.
- Diagramas de Gantt.
- Servidor web, aplicación móvil, nube o multiusuario.
- Sistema de plugins.

No crear una entidad alternativa que vuelva a introducir estas capacidades con otro nombre.

## 7. Clasificación de alcance

### Mantener

- Base C++20, CMake, FTXUI y SQLite.
- Proyectos y tarjetas de radar.
- Relación directa `Proyecto → Tarea`.
- Responsable textual opcional.
- Tipos, estados y prioridades fijos.
- Fecha de seguimiento o límite y bloqueos simples.
- Búsqueda, filtros, ordenamiento y archivado.
- El archivado de tareas y notas es reversible; restaurar una tarea requiere que su proyecto esté activo.
- Mi trabajo, dashboard informativo, logs y pruebas.

### Simplificar

- Usar la tarea como recordatorio o referencia breve, no como expediente completo.
- Mostrar métricas solo cuando conducen a una revisión concreta.
- Tratar el responsable como información, no como requisito de asignación.
- Mantener formularios y consultas directos, sin motores genéricos.

### Posponer

- Referencia externa en tareas.
- Captura rápida.
- Recordatorios personales.
- Mejoras de notas basadas únicamente en uso real.
- Comandos frecuentes.
- Backups, Git/SVN y exportaciones.

### Descartar

- Modelos equivalentes a tickets oficiales completos.
- Personas, equipos y asignaciones formales.
- Comentarios y colaboración.
- Dependencias y subtareas.
- Auditoría e historial detallado.
- Productividad, carga o rendimiento.
- Flujos configurables.

## 8. Navegación conceptual

```text
Dashboard
├── Para hoy
├── Requieren atención
├── Bloqueadas
└── Próximos seguimientos

Proyectos
└── Proyecto
    ├── Resumen
    ├── Tareas en radar
    └── Contexto relacionado

Mi trabajo
├── Todas
├── Hoy
├── Atrasadas
├── Próximas
├── Bloqueadas
└── Finalizadas recientemente

Conocimiento
├── Notas
├── Soluciones
├── Consultas y fragmentos
└── Procedimientos y referencias

Herramientas
├── Comandos
├── Git/SVN
├── Backups
└── Exportaciones
```

No agregar una sección de módulos, personas, equipos ni administración de Jira.

## 9. Arquitectura técnica vigente

- Lenguaje: C++20.
- Compilación: CMake.
- Interfaz: FTXUI.
- Argumentos: CLI11.
- Persistencia: SQLite.
- Logs: spdlog.
- JSON: nlohmann/json.
- Pruebas: Catch2.
- Dependencias: `FetchContent` con versiones fijadas.

```text
src/
├── ui/              Pantallas, navegación y entrada.
├── application/     Casos de uso y coordinación.
├── domain/          Project, Task, Note y valores controlados.
└── infrastructure/  SQLite, configuración y sistema.
```

La aplicación es un monolito local con responsabilidades separadas. No crear interfaces con una sola implementación, repositorios genéricos ni infraestructura para múltiples bases de datos.

## 10. Persistencia y compatibilidad

Ubicación de datos:

- Windows: `%LOCALAPPDATA%\MODRA`.
- Linux: `$XDG_DATA_HOME/modra` o `~/.local/share/modra`.
- macOS: `~/Library/Application Support/MODRA`.

El directorio contiene `modra.db`, `config.json`, `backups/`, `exports/` y `logs/`.

Reglas:

- No modificar migraciones ya aplicadas.
- Todo cambio real de esquema requiere una migración nueva y versionada.
- No crear migraciones para terminología o documentación.
- No eliminar campos ni datos existentes solo para simplificar conceptos.
- Usar consultas parametrizadas.
- No consultar SQLite dentro del renderizado de FTXUI.
- Las pruebas deben usar bases y directorios temporales, nunca datos reales.

## 11. Fechas

- Guardar fechas como ISO 8601 y timestamps con hora en UTC.
- `due_date` representa en la interfaz una fecha de seguimiento o límite.
- Usar una fecha de referencia única por carga.
- Las pruebas de fechas deben usar referencias controladas.

## 12. Interfaz

- El shell principal conserva siempre encabezado, menú lateral, panel de contenido y barra inferior.
- El menú lateral permanece visible en todas las secciones; las flechas cambian inmediatamente la selección y el contenido, sin requerir `Enter`.
- `Tab` o `→` mueve el foco del menú al panel. `Esc` vuelve dentro de la sección y desde su raíz devuelve el foco al menú.
- `j` y `k` no son atajos y no deben ejecutar acciones en ninguna pantalla.
- Navegación completa mediante teclado.
- Bordes simples, color moderado y jerarquía clara.
- No depender solamente del color.
- Mantener estados vacíos útiles y errores recuperables.
- Adaptar razonablemente el contenido a diferentes terminales.
- `?` abre ayuda contextual; `Esc` y `q` conservan comportamiento coherente.
- Los atajos alfabéticos no distinguen mayúsculas de minúsculas: `n` y `N`, por ejemplo, ejecutan la misma acción.

Terminología preferida:

- “En seguimiento” o “en radar” para el conjunto personal.
- “Fecha de seguimiento” cuando no es un vencimiento oficial.
- “Responsable” como texto informativo.
- “Referencia externa” para Jira, GitHub, correos o documentos.

Los identificadores internos y columnas existentes no necesitan renombrarse por cambios de texto visible.

## 13. Logs, errores y seguridad

Registrar inicio, cierre, migraciones, errores de SQLite, creación, edición, archivado y errores recuperables. No registrar descripciones completas, credenciales, tokens ni información sensible.

Confirmar acciones destructivas. No construir comandos de shell inseguros ni ejecutar escritura de Git/SVN desde MODRA en el alcance inicial.

## 14. Pruebas

Priorizar reglas de proyectos, tareas y notas, consultas de hoy/atrasadas/próximas/bloqueadas, búsqueda de conocimiento, archivado, persistencia, editor externo controlado, datos del dashboard informativo, migraciones y directorios temporales.

Usar pruebas unitarias para lógica y de integración para SQLite. No es necesario automatizar cada detalle visual de FTXUI, pero se deben realizar pruebas manuales cuando el entorno lo permita.

## 15. Roadmap vigente

1. **Revisión y alineación del producto**: mantener MODRA enfocado en organización personal y eliminar promesas tipo Jira.
2. **Notas y base de conocimiento**: primera versión implementada; mejorar solo después de uso real.
3. **Captura rápida**: registrar con poca fricción una idea, pendiente o referencia.
4. **Seguimientos y recordatorios personales**: revisar fechas y temas pendientes sin notificaciones complejas.
5. **Referencias externas en tareas**: enlace o identificador opcional de Jira, GitHub, correo o documento.
6. **Comandos frecuentes**: guardar, previsualizar y ejecutar comandos controlados.
7. **Backups y restauración**: copias seguras de SQLite y recuperación verificable.
8. **Consulta Git/SVN**: información local en modo lectura.
9. **Exportaciones**: formatos simples para conservar o compartir información seleccionada.
10. **Mejoras basadas en uso real**: únicamente después de observar necesidades concretas.

No forman parte del roadmap: historial detallado, comentarios encadenados, dependencias complejas, catálogo de personas, gestión formal del equipo, métricas de productividad ni funciones equivalentes a Jira.

## 16. Convenciones de desarrollo

- Mantener C++20 y compatibilidad con Windows.
- Revisar estructura y patrones existentes antes de cambiar código.
- Reutilizar servicios y utilidades actuales.
- Mantener cambios acotados al problema solicitado.
- Evitar helpers para lógica mínima usada una sola vez.
- Preferir composición sobre herencia.
- No agregar dependencias sin necesidad real.
- No crear clases, carpetas o interfaces vacías para el futuro.
- Compilar con warnings razonables.
- Mantener SQL cerca de su repositorio específico.
- No refactorizar código no relacionado.

## 17. Instrucciones para Codex

Antes de modificar archivos:

1. Leer este documento completo.
2. Revisar código y documentación actuales.
3. Comprobar si la solicitud ayuda a recordar, entender, revisar o encontrar información.
4. Confirmar que no replica una herramienta oficial externa.
5. Preferir el cambio más pequeño, compatible y reversible.

Durante el trabajo:

- No avanzar con funcionalidades no solicitadas.
- No implementar colaboración, usuarios ni gestión formal de equipo.
- No modificar datos reales ni migraciones aplicadas.
- Agregar pruebas cuando cambie lógica relevante.
- Informar supuestos, riesgos y limitaciones reales.
- No afirmar que una validación funcionó si no se ejecutó.

## 18. Decisiones permanentes

- MODRA es personal y de un único usuario.
- Jira y otras herramientas externas son la fuente oficial del trabajo del equipo.
- MODRA no reemplaza ni replica Jira.
- Una tarea es una tarjeta personal de radar o seguimiento.
- El núcleo futuro es organización personal, notas y conocimiento.
- La jerarquía es `Proyecto → Tarea`.
- Las notas personales se guardan en Markdown dentro de SQLite y pueden ser globales o relacionarse opcionalmente con proyecto y tarea.
- El contenido extenso se modifica mediante un editor externo; no se planifican wiki, colaboración, versionado ni adjuntos en esta etapa.
- No existe el concepto funcional de módulos.
- El responsable es texto opcional y no existe catálogo obligatorio de personas.
- No habrá servidor, nube, autenticación ni multiusuario en el alcance actual.
- Git/SVN comenzará en modo consulta.
- No se medirán productividad, carga ni rendimiento individual.
- La utilidad diaria y el bajo mantenimiento tienen prioridad.

## 19. Criterios de aceptación

Un cambio corresponde a MODRA cuando resuelve una necesidad personal, reduce el tiempo para recordar o encontrar contexto, mantiene la aplicación simple y local, no duplica información oficial innecesariamente y no introduce colaboración o gestión de equipo.

Si una propuesta requiere muchas entidades, flujos, permisos o historial para ser útil, debe reconsiderarse antes de implementarla.
