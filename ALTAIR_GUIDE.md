# Guía completa de Altair Language 1.8.5

Esta guía cubre **todo altair**: sintaxis, tipos, control de flujo, funciones,
clases, listas, punteros (`p#` y `lba%`), registros, persistencia, manejo
de errores y las capas avanzadas (servidor HTTP, gráficos, jobs,
introspección). Cada ejemplo lleva una marca honesta:

- ✅ = lo compilé **y ejecuté**, y el resultado mostrado es el real.
- ⚠️ = sintaxis correcta (compila), pero no lo ejecuté a fondo o no
  verifiqué el comportamiento en runtime.
- ❌ = lo probé **y encontré un bug real** del compilador/runtime — lo
  documento igualmente porque es información útil, pero no es "así se usa
  y funciona", es "así se escribe, y esto es lo que pasa hoy".

Compilar y ejecutar:
```
altairc programa.at -o salida
./salida
```
Otras opciones: `-icon`, `--emit-c` (vuelca el C generado), `--emit-ast`,
`--no-sema`, `-v`, `-h`, y `altairc guide` (escribe este fichero).

---

## 1. Lo primero que hay que saber: los bloques se cierran con `break`

Altair **no** usa indentación para delimitar bloques ni una palabra `end`.
Cada bloque compuesto (`if`, `while`, `foreach`, `try`, `catch`, `fun`,
`class`) se cierra con la palabra `break`. Si un bloque tiene sub-bloques
(por ejemplo `try` y su `catch`), **cada uno** necesita su propio `break`.

```altair
✅
while i < 5;
    log i
    i = i + 1
break
```

Las declaraciones simples (`numeric x = 1`, `log x`, `x = x + 1`) no
necesitan `;` al final ni `break` — el `;` solo aparece detrás de la
cabecera de un bloque (`while i < 5;`, `if x > 0;`, `fun nombre;`).

Las declaraciones de `class` y `fun` de nivel superior deben ir **antes**
que el resto del código ejecutable del fichero (igual que en todos los
ejemplos incluidos con el compilador). Mezclarlas después de sentencias
sueltas puede dar un error de parseo.

---

## 2. Tipos

| Tipo       | Palabra clave | Notas |
|------------|---------------|-------|
| Numérico   | `numeric`     | `double` en general; en bucles calientes con solo aritmética entera, el compilador detecta un *fast path* y usa `long long` nativo (ver §9). |
| Texto      | `text`        | Cadena UTF-8/byte. Concatenar con `+`/`+=`. |
| Booleano   | `bool`        | `true` / `false`. |
| Lista      | `list`        | Array dinámico heterogéneo. |
| Objeto     | `object`      | Instancia de una `class`. |
| Token      | `token`       | Envoltorio de "consumir una vez" (parsers, colas de un solo uso). |
| Puntero RAM| `p#node`      | Buffer crudo en memoria (§10). |
| Puntero LBA| `lba%node`    | Buffer/dispositivo respaldado en disco (§11). |
| Fichero    | `file`        | Handle de fichero abierto. |
| (gráficos) | `image`, `sound`, `music`, `color` | Subsistema GUI/juegos, ver §14. |

Declaración: `<tipo> <nombre> = <expr> [ram|disk|cache|temp|auto] [const] [expire <n>]`.

```altair
✅
numeric edad = 30
text nombre = "Ada"
bool activo = true
list nums = [1, 2, 3]
```

---

## 3. Operadores

Aritméticos: `+ - * / %` · Comparación: `== != < > <= >=` ·
Lógicos: `and or not` (también `&& || !` según contexto léxico) ·
Bit a bit: `& | ^ << >>` (`altair_band/bor/bxor/shl/shr`) ·
Compuestos: `+= -= *= /= %=`.

`+` sobre `text` concatena; si cualquiera de los dos lados es `text`, el
resultado es `text` (el otro operando se convierte a su representación en
texto). `+=` sobre `text` está optimizado con capacidad amortizada, no
copia toda la cadena en cada iteración de un bucle (ver §9).

```altair
✅
text t = ""
numeric i = 0
while i < 5;
    t = t + "x"
    i = i + 1
break
log t          -- xxxxx
```

---

## 4. Control de flujo

```altair
✅
numeric x = 7
if x > 10;
    log "grande"
elif x > 5;
    log "mediano"
else;
    log "pequeño"
break
```

`while <cond>;` · `repeat <n>;` · `forever;` (bucle infinito, se sale con
`exit`) · `foreach <var> in <lista>;` · `wait <segundos>` (pausa) ·
`exit` (rompe el bucle actual) · `release <var>` (libera una variable
explícitamente).

```altair
✅
list nums = [10, 20, 30]
foreach n in nums;
    log n
break
```

### Bucles con contador

```altair
✅
numeric total = 0
repeat 5 times;
    total = total + 1
break
log total          -- 5
```

`repeat <n>` (la palabra `times` es opcional). `forever;` es un bucle
infinito; se sale con `exit`:

```altair
✅
numeric i = 0
forever;
    i = i + 1
    if i >= 3;
        exit
    break
break
log i               -- 3
```



```altair
✅
try;
    numeric x = 10 / 0
    log x
break
catch e;
    log "caught an error"
break
```

Cada excepción interna lanza un código `ALT00NN` (hay 20 códigos definidos,
`ALT0001`–`ALT0020`, cubriendo desde variables desconocidas hasta punteros
inválidos). `catch e;` captura el error en `e`.

### `choose` (selección aleatoria ponderada) ⚠️

No es un `switch`/`case` — es un selector aleatorio por pesos, orientado a
juegos/loot tables:
```altair
⚠️
choose pick;
    70% = "common"
    30% = "rare"
define
log pick
```

---

## 5. Funciones

```altair
✅
fun suma numeric a, numeric b;
    return a + b
break

log suma(3, 4)
```

Con tipo de retorno explícito: `fun nombre -> tipo <params>;`. Si el
cuerpo nunca ejecuta un `return <valor>`, la función es "void" — el
compilador lo detecta en un pre-análisis y en cada sitio donde se llama
descartando el resultado, genera la llamada directa sin envolver en una
caja `AltairVal` (optimización D, ver §9).

Los parámetros de tipo `list`/`text`/`object` que la función solo lee o
sobre los que solo llama a `.append()`/`.remove()`/`.clear()`/`.length()`
se pasan **por referencia sin copiar** (*borrow-safe*); si la función hace
algo que podría dejar escapar la referencia, el compilador copia el
argumento automáticamente para que sea seguro.

```altair
✅
fun fill list arr, numeric n;
    numeric i = 0
    while i < n;
        arr.append(i * i)
        i = i + 1
    break
break

list nums = []
fill(nums, 5)
log length(nums)   -- 5
log nums[0]        -- 0
log nums[4]        -- 16
```

---

## 6. Clases y objetos

```altair
✅
class Animal;
    text name = "?"
    numeric legs = 4

    fun describe;
        log name
        log legs
    break
break

object a = Animal()
a.name = "dog"
a.legs = 4
a.describe()
```

Instanciar con `object <var> = ClaseNombre()`; opcionalmente con clase de
almacenamiento y peso: `object hero = Player() ram weight 900` (visto en
`examples/game.at`). Dentro de un método, los campos se leen/escriben por
su nombre directo (sin `self.`), y `self` se maneja internamente.

---

## 7. Listas

```altair
✅
list nums = [0, 0, 0, 0, 0]
numeric i = 0
while i < 5;
    nums[i] = i * i
    i = i + 1
break
log nums[4]          -- 16
log length(nums)      -- 5
nums.append(99)
nums.remove(0)
nums.clear()
```

`nums[i] = expr` dentro de un `while`/`if` numérico también participa en
el *fast path* (ver §9) — no fuerza a todo el programa a compilarse en modo
"lento" solo por tener asignación indexada en un bucle caliente.

Cuando el patrón es "declarar lista vacía + bucle que solo hace
`.append(numérico)`", el compilador (con `-DENABLE_FNUMLIST`, activado por
defecto en el `Makefile`) detecta el patrón y compila la lista a un array
denso de `double`/`long long` en vez de un array de punteros `AltairVal*`.

---

## 8. Precisión numérica y enteros grandes

Los literales numéricos grandes (por encima de 2⁵³, donde `double` empieza
a perder dígitos) se preservan exactos cuando el cálculo ocurre en el
*fast path* entero:

```altair
✅
numeric a = 2432902008176640000    -- 20!
log a                               -- 2432902008176640000, exacto
numeric b = a + 1
log b                               -- 2432902008176640001, exacto
```

Internamente, `AltairVal` guarda un flag `num_is_int` + `num_i64` además
del `double`, así que mostrar (`log`) y devolver desde una función no pasan
por una conversión a `double` que trunque el valor.

---

## 9. Cómo compila internamente (para quien le importa el rendimiento)

- **Fast path numérico**: si un `while`/`fun` solo usa aritmética numérica
  (sin listas de tipo mixto, sin texto), el compilador detecta el patrón
  entero completo del programa y compila esas variables como `long long`/
  `double` nativos de C en vez de `AltairVal*` boxeado — sin alloc/free por
  iteración. `nums[i]=expr` (asignación indexada) también se admite dentro
  de ese fast path.
- **`text` con capacidad amortizada**: `t += x` y `t = t + x` (patrón de
  auto-concatenación) crecen el buffer de `text` con `realloc` + doblado de
  capacidad, no reconstruyen la cadena entera en cada iteración.
- **Parámetros borrow-safe**: pasar una `list`/`text` a una función que
  solo la lee, o solo hace `.append/.remove/.clear/.length` sobre ella, no
  copia el valor completo en el sitio de la llamada.
- **Funciones "void" sin caja**: una función sin ningún `return <valor>`
  siempre devuelve `NULL` (no un `AltairVal` de relleno); cuando se llama
  descartando el resultado, el compilador emite la llamada directa sin la
  envoltura `{ AltairVal *_es=...; altair_val_free(_es); }`.
- Todo el runtime se compila con `gcc -O3 -flto -fomit-frame-pointer`
  (LTO real, no solo una unidad de compilación), así que el propio GCC
  hace *inlining* entre el runtime y el código generado cuando es rentable.

---

## 10. `p#` — punteros crudos en RAM

```altair
✅
p#node n = alloc(64)
p#write(n, 0, 42)
p#write(n, 1, 100)
numeric a = p#read(n, 0)
numeric b = p#read(n, 1)
log a                 -- 42
log b                 -- 100
log p#bytes(n)        -- 64
log p#null(n)         -- false
p#free(n)
log p#null(n)         -- true
```

`p#read`/`p#write` direccionan por slots de 8 bytes (`double`), no por
byte suelto. Hay comprobación de límites (`ALT0019`) y de uso-tras-liberar
(`ALT0018`). El buffer vive en RAM, se pierde al terminar el programa.

---

## 11. `lba%` — punteros respaldados en disco (y en dispositivo de bloques real)

Misma forma que `p#` (`node`/`read`/`write`/`bytes`/`null`/`free`), pero
con tres constructores distintos según qué tan "de verdad" quieras que sea:

```altair
✅  -- anónimo: fichero temporal, se autodestruye al liberar
lba%node d = dalloc(64)

✅  -- con nombre: PERSISTE en disco entre ejecuciones distintas
lba%node d = dopen("disco.img", 64)

⚠️  -- dispositivo de bloques REAL (Linux, O_DIRECT), NO es un fichero
lba%node d = draw("/dev/sdX")
```

- `dalloc(bytes)` y `dopen(path, bytes)` usan E/S de fichero normal
  (`fopen`/`fread`/`fwrite`), disponibles en Linux, macOS y Windows.
- `draw(path)` (el nombre es solo el que le puse yo, no es palabra
  reservada especial de LBA) abre de verdad el dispositivo con
  `O_RDWR | O_DIRECT` (salta la caché de página), lee el tamaño real con
  `ioctl(BLKGETSIZE64)` y el tamaño de sector real con `BLKSSZGET`, y hace
  *read-modify-write* de sector completo con buffers alineados
  (`posix_memalign`) porque `O_DIRECT` no admite E/S suelta. **Solo
  funciona en Linux** (en macOS/Windows lanza `ALT0017` con un mensaje
  claro; el resto del lenguaje sigue compilando igual en las tres
  plataformas). Solo acepta rutas bajo `/dev/`. **Cuidado real**: apuntar
  al dispositivo equivocado puede destrozar una tabla de particiones —
  pruébalo primero contra un `losetup` de un fichero, nunca contra un
  disco de producción a la primera.

```altair
✅
lba%node d = dopen("disklba.img", 64)
lba%write(d, 0, 777)
log lba%read(d, 0)     -- 777, y sigue ahí si relanzas el binario
lba%free(d)
```

---

## 12. Registros crudos (`reg&`)

Variables de ancho fijo con nombre de registro x86 (`rax/eax/ax/al`,
`rbx/ebx/bx/bl`, `rcx/ecx/cx/cl`, `rdx/edx/dx/dl` — 4 familias, cada una en
4 anchos: 64/32/16/8 bits). Pensado para el propio backend/compilador más
que para uso cotidiano, pero funciona como cualquier otra sentencia:

```altair
✅
reg&64 rax = 100
log reg&read(rax)      -- 100
reg&write(rax) = 250
log reg&read(rax)      -- 250
reg&free(rax)
```

Declarar: `reg&<bits> <nombre> = <expr>`. Escribir: `reg&write(<nombre>) =
<expr>`. Leer (es una expresión, no una sentencia): `reg&read(<nombre>)`.
Liberar: `reg&free(<nombre>)`.

---

## 13. Ficheros y sistema

`open(path)` / `open_write(path)` / `open_append(path)` → `file`,
`read(f)` / `read_line(f)` / `write(f, texto)` / `close(f)`,
`file_exists(path)` (`bool`), `create_file(path)`, `delete_file(path)`,
`mkdir(path)`, `list_dir(path)` → `list`, `exec(cmd)` → código de salida,
`exec_capture(cmd)` → `text` con la salida, `argc()`/`arg(i)` para
argumentos de línea de comandos.

Introspección con `namespace@clave`: `system@point(var)` /
`system@unpoint(var)` (dirección de memoria de una variable — distinto de
`p#`), y variantes bajo `system@`, `compiler@`, `program@` para más datos
del entorno (pid, hostname, versión...).

---

## 14. Persistencia y almacenamiento

Cualquier variable puede llevar una clase de almacenamiento:
`ram` (por defecto, se pierde al salir) · `disk` (persiste en fichero) ·
`cache` (persiste con posible expiración) · `temp` · `auto`. Modificadores
extra: `const`, `expire <duración>`, `weight = <n>`.

```altair
⚠️
numeric contador = 0 disk
```

### `orbit`/`migrate` — máquina de estados de almacenamiento ⚠️❌

Una variable `orbit` declara varios estados nombrados/numerados, cada uno
con su propia clase de almacenamiento, y `migrate` debería moverla de uno
a otro en caliente:

```altair
❌  -- compila, pero falla en tiempo de ejecución
numeric x = 5 orbit 1 "hot" ram, 2 "cold" disk
x migrate as "cold"     -- ALT0012: Orbit state 'cold' not found in 'x'
x migrate as 2           -- ALT0012: Orbit state 2 not found in 'x' (tampoco)
```

Lo probé y **no funciona**: compila sin error, pero `migrate` no encuentra
ningún estado aunque esté declarado tal cual en el `orbit`. Es un bug
preexistente del compilador (no introducido por mí esta sesión), no un
error de sintaxis por tu parte — lo dejo documentado para que lo sepas si
lo usas. `prefer` (lista de storages de fallback, sin nombres de estado)
usa la misma familia de código y probablemente tenga el mismo problema.

`snapshot create "nombre"` sí compila y ejecuta sin error (guarda el
estado del programa bajo un nombre) — no verifiqué que el snapshot se
pueda *restaurar* después, solo que crearlo no falla:

```altair
✅  -- solo confirma que "snapshot create" no rompe nada
numeric x = 5
snapshot create "inicio"
x = 10
log x     -- 10 (crear el snapshot no cambia el valor actual)
```

Además: `weight`, y el bloque `altair.doc; ... create altair.doc` al
principio de un programa para metadatos (`name`, `version`, `author`).

---

## 15. Servidor HTTP, sesiones, configuración, jobs

### `listen` / `route` / `respond` ❌

```altair
⚠️ -- compila y arranca, pero NO enruta correctamente
route "GET" "/hello";
    respond.text("hola mundo")
break

listen 8099;
break
```

Esto **compila y el servidor arranca** (`[altair] Server listening on
port 8099`), pero al hacerle un `curl` de verdad a `/hello` responde
`{"error":"route not found: GET /hello"}` — el registro de rutas no está
enlazando con el despachador. Y si en vez de declarar `route` *antes* de
`listen` lo anidas *dentro* (`listen 8099; route ...; break break`), ni
siquiera compila: el C generado referencia un handler con el nombre mal
formado (`_route_GET_get_hello_handler`, con el prefijo del método
duplicado) y falla en `gcc`. Ambas cosas las probé de verdad, no son
suposiciones — el subsistema HTTP tiene bugs reales, más allá de que ya
sabíamos que el socket subyacente es de un solo hilo y sin TLS.

Sintaxis reconocida (para cuando esto se arregle): `route "MÉTODO" "/ruta"
[rate_limit N per_minute|per_second]; ... break`, con `respond.text(...)`,
`respond.json(...)`, `respond.status(...)`, y dentro del handler:
`param("nombre")`, `header("nombre")`, `body()`.

### `session` / `config` — compilan ✅ (sin probar en runtime)

```altair
⚠️
session usuario_actual expires 30m;
config;
    puerto = 8080
break
```

### `job` / `schedule` — compila y ejecuta ✅

```altair
✅
job cleanup every 30;
    log "tick"
break
```

(Compilé y confirmé que produce un binario válido; no lo dejé corriendo
30s para ver el primer disparo real del temporizador, pero el arranque no
falla.)

### `window` (gráficos) — compila ✅ sin necesitar raylib para lo básico

```altair
✅
window;
    title = "demo"
    width = 640
    height = 480
create window
```

En cuanto se usan comandos de dibujo reales (`draw`, `image`, etc.) el
compilador activa el enlazado con raylib y necesitas `raylib.h`/la
librería disponibles (como en `test/paint.at`, que en este contenedor no
compiló por faltar esa dependencia). El resto de la DSL gráfica (`sound`,
`music`, `button`, `label`, `textbox`, `checkbox`, `slider`, `progress`,
`listview`, `menu`, `dialog`, `scene`, `goto`, `cursor`, `animate`,
`popup`, `canvas`, `column`, `row`, `grid`, `key`) no la probé una por
una — mismo patrón de declaración con `;`...`break` que el resto del
lenguaje.

### El resto de la DSL de servidor (sin probar) ⚠️

`middleware nombre; ... break` (interceptor de requests), `db_pool`
(pool de conexión a base de datos, con `max`/`connect`), `health;
... break` y `check`/`metrics` (endpoints de salud/métricas),
`on_shutdown; ... break` (hook de apagado limpio).

---

## 16. Módulos y tipo `token`

```altair
⚠️
import "utilidades" as util
```

`token` es un tipo "consumir una vez": se crea con `token_new(...)` y se
consume con `.use()` — pensado para colas de un solo uso o parsers que no
deben releer el mismo elemento dos veces. No lo probé en runtime.

---

## 17. Códigos de error

El runtime usa `ALT0001`–`ALT0020` para: variable desconocida, operación
inválida, error de parseo, división por cero, tipos incompatibles, const
reasignada, expresión inválida en input de usuario, punteros nulos/fuera
de rango/usados-tras-liberar, fallos de E/S de disco/LBA, y varios más.
`try; ... break catch e; ... break` los captura como texto en `e`.

---

## 18. Lo que se optimizó en esta sesión (por si afecta a tu código)

- Enteros grandes (`>2⁵³`) ya no pierden precisión al hacer `log` o
  `return` desde el *fast path*.
- `nums[i] = expr` dentro de bucles numéricos ya no saca a todo el
  programa del *fast path*.
- Pasar una `list`/`text` a una función que solo hace
  `.append/.remove/.clear/.length` ya no copia el valor completo.
- `text` con `+=`/auto-concatenación crece con capacidad amortizada, no
  copia toda la cadena en cada vuelta del bucle.
- Funciones sin `return <valor>` no reservan un `AltairVal` de relleno en
  cada llamada descartada.
- Nuevo tipo `lba%`, con la variante `draw()` de acceso a bloque real
  (`O_DIRECT`) solo en Linux, sin afectar a los builds de macOS/Windows.

Nada de lo anterior cambia la sintaxis ni el comportamiento observable de
tu código existente — son todo optimizaciones internas del compilador,
verificadas contra `examples/`, `test/` y los casos de esta guía.

## 19. Bugs encontrados al escribir esta guía (no arreglados, solo documentados)

Al verificar cada sección probé de verdad todo lo que se podía compilar y
ejecutar sin riesgo, y salieron dos bugs reales preexistentes que no
estaban en el alcance de lo que me pediste optimizar hasta ahora:

1. **`orbit`/`migrate` no funciona**: declarar una variable con `orbit` y
   luego intentar `migrate as <estado>` (por nombre o por número) siempre
   da `ALT0012: Orbit state ... not found`, aunque el estado exista
   textualmente en la declaración. Compila bien, falla en runtime.
2. **`route`/`listen` no enruta**: un servidor con `route "GET" "/x";
   ... break` y `listen N; break` arranca y escucha, pero cualquier
   petición real devuelve `route not found`. Y si anidas el `route`
   *dentro* del `listen` en vez de antes, ni siquiera compila (nombre de
   handler mal generado, con el método duplicado).

Si quieres que los arregle, dímelo en el próximo mensaje — no los toqué
porque no era lo que se pidió esta vez.
