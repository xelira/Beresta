# Установщик

Один бинарник на Go, без внешних зависимостей. Собирает компилятор бересты и
кладёт его в удобное место.

## Сборка установщика

```sh
make installer           # build/beresta-install       (Linux)
make installer-windows   # build/beresta-install.exe   (Windows)
```

## Linux

```sh
./build/beresta-install
```

Что происходит:

1. Проверяются `g++`, `as`, `cc`. Если чего-то нет и установщик запущен от
   root — пакеты доставляются через apt/dnf/pacman.
2. Исходники берутся из текущего репозитория (если запущено из его корня) или
   скачиваются архивом с GitHub.
3. Компилятор собирается в временный каталог.
4. Бинарник кладётся в `~/.local/bin/berestac` (или `--prefix`).
5. Печатается подсказка про PATH.

Полезные флаги:

```sh
--prefix /usr/local     # куда ставить
--repo <url>            # свой архив с исходниками
--check                 # только проверить инструменты
--no-path               # не подсказывать про PATH
--keep-src              # оставить скачанные исходники
--deps=false            # не пытаться ставить пакеты
```

## Windows

Запустить `beresta-install.exe`. Режим по умолчанию — **нативный**, без WSL:

1. Скачивает `berestac-windows-amd64.exe` и `beresta-windows-amd64.exe` из
   последнего релиза.
2. Скачивает портативный MinGW-w64 (winlibs) и распаковывает в
   `%LOCALAPPDATA%\beresta\toolchain`.
3. Кладёт компилятор и лаунчер в `%LOCALAPPDATA%\beresta\bin`.
4. Добавляет оба каталога в пользовательский PATH.

После этого в новом терминале:

```bat
beresta version
beresta new моя_программа
beresta run моя_программа
```

`berestac` собирает настоящие Windows `.exe` (PE32+, Microsoft x64) через
`as.exe` и `gcc.exe` из MinGW.

Если нативная установка не удалась, установщик предложит WSL-режим. Его можно
запросить и явно:

```bat
beresta-install.exe --wsl
```

WSL-режим ставит `g++`/`binutils` внутри дистрибутива и создаёт обёртку
`berestac.bat`.

### Как это устроено

У бересты два бэкенда:

- Linux: System V AMD64, GNU as, ELF;
- Windows: Microsoft x64 (`rcx, rdx, r8, r9`, shadow space), PE, рантайм на C,
  который компилирует тот же MinGW.

Ограничение Windows-цели: не больше 4 обычных и 4 дробных параметров/аргументов
(регистров в ABI меньше, чем в System V). Массивы и уклады по-прежнему
передаются по ссылке и считаются одним параметром.

## Релизы

`.github/workflows/release.yml` на тег `v*` собирает:

- `beresta-install-linux-amd64`
- `beresta-install-linux-arm64`
- `beresta-install-windows-amd64.exe`
- `beresta-linux-amd64` (лаунчер с вложенным компилятором)
- `berestac-windows-amd64.exe` (нативный компилятор)
- `beresta-linux-arm64`
- `beresta-windows-amd64.exe`
- `beresta-bundle-linux-amd64.tar.gz` (лаунчер + компилятор + примеры + docs)
- `beresta-linux-amd64.tar.gz` (сам компилятор)
- `SHA256SUMS.txt`

и прикладывает всё к GitHub Release. Без тега те же файлы уходят в артефакты
workflow.

## Что установщик не делает

- Не трогает системный PATH (только пользовательский).
- Не ставит пакеты без явного намерения: на Linux нужен root; на Windows
  скачивает только портативный MinGW в свой каталог.
