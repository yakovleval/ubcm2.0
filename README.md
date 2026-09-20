# UBCM Virtual Machine

Небольшая виртуальная машина на C11. ВМ исполняет процедуры,
представленные битовыми потоками, с помощью распознающих сетей.

## Требования

Для сборки и запуска необходимы:

- GCC с поддержкой C11;
- GNU Make;
- стандартная библиотека C;
- AddressSanitizer и UndefinedBehaviorSanitizer для debug-сборки.

Для Debian/Ubuntu:

```sh
sudo apt install build-essential python3
```

## Сборка

Debug:

```sh
make
# или явно
make debug
```

Результат в `debug/ubcm (release/ubcm)`.

Release:

```sh
make release
```

Результат в `release/ubcm`.

## Очистка

```sh
make clean
```

Удаляет каталоги `debug/`, `release/` и файлы
`*.ubc` и `*.rs`.
