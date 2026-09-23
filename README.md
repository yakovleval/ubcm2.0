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

`TYPE=0` означает встроенную команду, `TYPE=1` — вызов процедуры. Узел типа
`1` читает дескриптор `[proc_reg:5][rs_reg:5][rs_entry:16]` из своего
логического сверхлокального `r0`.

Формат каждой записи инициализатора:

```text
[node_index:u64][logical_name:u8][size_bits:u64][data:ceil(size_bits/8)]
```

Файлы без magic поддерживаются как старый формат: последовательность
64-битных узлов без инициализаторов.

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

bits = "100000000000010110"
Path("program.ubc").write_bytes(
    int(bits.ljust(24, "0"), 2).to_bytes(3, "big")
)
Path("1.rn").write_bytes(rn([
    node(3, 1, 1), node(9, 2, 2), node(3, 3, 3), node(11),
]))
Path("2.rn").write_bytes(rn([
    node(3, 1, 1), node(3, 2, 2), node(9, 3, 3),
    node(3, 4, 4), node(11),
]))
```

```sh
./debug/ubcm program.ubc 1.rn   # RETURN RESULT: 0
./debug/ubcm program.ubc 2.rn   # RETURN RESULT: 3
```

`program.ubc` содержит 18 значимых битов и 6 битов padding:

```text
100000000000010110 000000
```

`1.rn` распознаёт `RETURN` после одного бита:

```text
1 | 00 000 00000001 0 | 1
    mode  hdr size=1   0   CHOICE → EXIT
```

`2.rn` распознаёт `RETURN` после двух битов:

```text
10 | 00 000 00000010 11 | 0
     mode  hdr size=2   3    CHOICE → EXIT
```

Узлы `1.rn`:

```text
0003000100010000  CHOICE → 1
0009000200020000  RETURN → 2
0003000300030000  CHOICE → 3
000b000000000000  EXIT
```

Узлы `2.rn`:

```text
0003000100010000  CHOICE → 1
0003000200020000  CHOICE → 2
0009000300030000  RETURN → 3
0003000400040000  CHOICE → 4
000b000000000000  EXIT
```
