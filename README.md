# UBCM Virtual Machine

Виртуальная машина на C11. Требуются GCC, GNU Make и Python 3 для генерации тестов.

## Сборка и запуск

```sh
make                 # debug/ubcm с ASan/UBSan
make release         # release/ubcm
./debug/ubcm program.ubc network.rn
```

ВМ загружает процедуру в `r1`, РС в `r2` и начинает с бита `0` и узла `0`.

```sh
make test            # генерация и запуск интеграционных тестов
make clean
```

## Формат `.ubc`

Сырой битовый поток процедуры без заголовка. Биты и байты записываются в
Big-Endian; размер процедуры равен размеру файла в байтах, умноженному на 8.

## Формат `.rn`

Все числа записываются в Big-Endian:

```text
8 байт   magic: "UBCMRN01"
8 байт   число узлов N
8 байт   число инициализаторов M
N×8      64-битные узлы РС
M записей начальных сверхлокальных регистров
```

Формат узла:

```text
[TYPE:1][DATA:15][next0:16][next1:16][resolver:16]
```

`TYPE=0` означает встроенную команду, `TYPE=1` — пользовательскую процедуру. Узел типа
`1` читает дескриптор `[proc_reg:5][rs_reg:5][rs_entry:16]` из своего
логического сверхлокального `r0`.

Формат каждой записи инициализатора:

```text
[node_index:u64][logical_name:u8][size_bits:u64][data:ceil(size_bits/8)]
```

## Одна программа, две РС

Сохраните как `example.py` и запустите `python3 example.py`:

```python
from pathlib import Path

def node(data, next0=0, next1=0):
    value = data << 48 | next0 << 32 | next1 << 16
    return value.to_bytes(8, "big")

def rn(nodes):
    return (b"UBCMRN01" + len(nodes).to_bytes(8, "big")
            + (0).to_bytes(8, "big") + b"".join(nodes))

def standard_nodes():
    nodes = [node(3, 2*i + 1, 2*i + 2) for i in range(15)]
    nodes += [node(11) for _ in range(16)]
    nodes[24] = node(9)   # 1001: RETURN
    nodes[26] = node(11)  # 1011: EXIT
    return nodes

bits = "1001000000000000101011"
Path("program.ubc").write_bytes(
    int(bits.ljust(24, "0"), 2).to_bytes(3, "big")
)
Path("1.rn").write_bytes(rn(standard_nodes()))
Path("2.rn").write_bytes(rn([
    node(3, 8, 1), node(3, 2, 8), node(3, 3, 8),
    node(3, 8, 4), node(3, 5, 8), node(9, 6, 6),
    node(3, 8, 7), node(3, 8, 8), node(11),
]))
```

```sh
./debug/ubcm program.ubc 1.rn   # RETURN RESULT: 0
./debug/ubcm program.ubc 2.rn   # RETURN RESULT: 2
```

`program.ubc` содержит 22 значимых бита и 2 бита padding:

```text
1001000000000000101011 00
```

Стандартная `1.rn` использует обычные четырёхбитные имена команд:

```text
1001 | 00 000 00000001 0 | 1011
RETURN   immediate: 0       EXIT
```

Нестандартная `2.rn` захватывает первый бит immediate в имя `RETURN`:

```text
10010 | 00 000 00000010 10 | 11
RETURN    immediate: 2        EXIT
```

Значимые пути `1.rn`:

```text
1001: 0 → 2 → 5 → 11 → 24 (RETURN)
1011: 0 → 2 → 5 → 12 → 26 (EXIT)
```

Значимые пути `2.rn`:

```text
10010: 0 → 1 → 2 → 3 → 4 → 5 (RETURN)
11:    6 → 7 → 8 (EXIT)
```
