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
`*.ubc`, `*.rs` и `*.rn`.

## Интеграционные тесты

Сгенерировать отдельные пары входных файлов для `RESIZE` и каждой команды CALL:

```sh
make fixtures
```

Запустить интеграционный тест:

```sh
make test
```

Файлы создаются прямо в `tests/` под именами соответствующих тестов, например
`call_new_procedure_0110.ubc` и `call_new_procedure_0110.rn`. Они не добавляются
в Git. Повторная генерация нужна только после изменения генератора или удаления
этих файлов.
