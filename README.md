# Поиск определений в Black's Law Dictionary

Программа читает термины из `input.txt`, ищет соответствующие статьи в единственном PDF-файле из корневой папки проекта и записывает определения в `output.txt`. Она не обращается к интернету или другим словарям.

## Важное о работе с программой

Все глаголы должны быть в инфинитиве. Регистр не важен.
Программа актуальной версии не отличает существительное от глагола, а так же почти никогда не находит словосочетания (когда в строке 2 слова).

Результат появится в `output.txt`. Пустые строки во входном файле игнорируются. Если точного заголовка нет, программа также проверит обычную форму единственного числа (например, `cases` → `case`). Если статья всё равно не найдена, она запишет об этом сообщение. В корневой папке должен находиться ровно один PDF-файл.

### Сделано специально для юридического факультета МГУ им. Ломоносова в 2026 году.

### Планируемые обновения

Улучшение поиска словосочетаний.
Поддержка разных частей речи.
(на самом деле мне впаду этим заниматься, так что если не лень - делайте сами)

# Техническая часть

Для извлечения текста из PDF требуется Poppler с утилитой `pdftohtml`:

- macOS: `brew install poppler`
- Ubuntu/Debian: `sudo apt install poppler-utils`

### Сборка через CMake (рекомендуется, кроссплатформенно)

```sh
cmake -S . -B build
cmake --build build --config Release
```

Исполняемый файл `dictionary_search` (или `dictionary_search.exe` на Windows) окажется в корне проекта, рядом с `input.txt` и `output.txt`.

---

## Пересборка для macOS

1. Установите зависимости:

   ```sh
   brew install poppler cmake
   ```

   (`poppler` предоставляет утилиту `pdftohtml`, `cmake` — для сборки.)

2. Соберите приложение из корня проекта:

   ```sh
   cmake -S . -B build
   cmake --build build --config Release
   ```

3. Готовый бинарник `dictionary_search` появится в корне папки проекта.

4. Запустите:
   ```sh
   ./dictionary_search
   ```

---

## Пересборка для Windows

1. Установите зависимости:
   - **[CMake](https://cmake.org/download/)** — добавьте `cmake` в переменную `PATH`.
   - **Компилятор C++**:
     - **Вариант A (рекомендуется):** [Visual Studio Community](https://visualstudio.microsoft.com/downloads/) с компонентом «Разработка классических приложений на C++».
     - **Вариант B:** [MinGW-w64](https://www.mingw-w64.org/) — добавьте `bin`-папку MinGW в `PATH`.
   - **[Poppler для Windows](https://github.com/oschwartz10612/poppler-windows)** — распакуйте архив и добавьте папку с `pdftohtml.exe` в `PATH`.

2. Соберите приложение (PowerShell, из корня проекта):

   ```powershell
   cmake -S . -B build
   cmake --build build --config Release
   ```

3. Готовый файл `dictionary_search.exe` появится в корне папки проекта, рядом с `input.txt` и `output.txt`.

4. Запустите:
   ```powershell
   .\dictionary_search.exe
   ```

---

### Сборка вручную (g++/clang++)

```sh
g++ -std=c++17 -O2 -Wall -Wextra main.cpp -o dictionary_search
```

В `input.txt` укажи по одному термину на строку, затем запусти программу из корневой папки:

```sh
./dictionary_search
```

Пример `input.txt`:

```text
doctrine
doctrine of approximation
```
