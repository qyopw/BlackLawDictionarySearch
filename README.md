# Поиск определений в Black's Law Dictionary

Программа читает термины из `input.txt`, ищет соответствующие статьи в единственном PDF-файле из корневой папки проекта и записывает определения в `output.txt`. Она не обращается к интернету или другим словарям. Заголовки, полужирный текст и курсив сохраняются в Markdown.

Для извлечения текста из PDF требуется Poppler с утилитой `pdftohtml`:

- macOS: `brew install poppler`
- Ubuntu/Debian: `sudo apt install poppler-utils`

Сборка из корневой папки проекта:

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

Результат появится в `output.txt`. Пустые строки во входном файле игнорируются. Если статья не найдена, программа запишет об этом сообщение. В корневой папке должен находиться ровно один PDF-файл; `input.txt` и `output.txt` тоже находятся в корне проекта.
