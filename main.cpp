#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

#ifdef _WIN32
#define NOMINMAX
#include <conio.h>
#include <windows.h>
#include <winhttp.h>
#endif

/// Приостанавливает завершение программы при ошибке, чтобы пользователь
/// успел прочитать сообщение в консоли (в частности при запуске двойным кликом).
static void pauseBeforeExit() {
#ifdef _WIN32
    std::cerr << "Нажмите любую клавишу для выхода...";
    _getch();
    std::cerr << std::endl;
#endif
}

namespace fs = std::filesystem;

/// Возвращает папку, в которой находится запущенный исполняемый файл.
/// Это важнее, чем fs::current_path(), потому что при двойном клике через
/// Проводник текущая директория может совпадать не с папкой программы.
static fs::path getExeDirectory() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return fs::current_path();
    return fs::path(buf).parent_path();
#else
    return fs::current_path();
#endif
}

/// Скачивает файл по HTTP(S) с помощью WinHTTP (стандартная Windows API).
/// Показывает прогресс в консоли. Возвращает true при успехе.
static bool downloadFile(const std::string& url, const fs::path& destination) {
#ifdef _WIN32
    HINTERNET hSession = WinHttpOpen(L"BlackLawDictionarySearch/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        std::cerr << "Не удалось инициализировать WinHTTP.\n";
        return false;
    }

    std::wstring wurl(url.begin(), url.end());
    URL_COMPONENTSW urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = -1;
    urlComp.dwHostNameLength = -1;
    urlComp.dwUrlPathLength = -1;
    urlComp.dwExtraInfoLength = -1;
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &urlComp)) {
        std::cerr << "Не удалось разобрать URL: " << url << "\n";
        WinHttpCloseHandle(hSession);
        return false;
    }

    // WinHttpCrackUrl заполняет lpszScheme/lpszHostName/lpszUrlPath/lpszExtraInfo
    // как указатели на части исходной строки wurl.
    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.dwExtraInfoLength > 0) {
        path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }

    INTERNET_PORT port = urlComp.nPort ? urlComp.nPort : INTERNET_DEFAULT_HTTPS_PORT;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        std::cerr << "Не удалось подключиться к серверу.\n";
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = WINHTTP_FLAG_SECURE;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        std::cerr << "Не удалось создать запрос.\n";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        std::cerr << "Не удалось отправить запрос.\n";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        std::cerr << "Не удалось получить ответ от сервера.\n";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode,
                             &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        std::cerr << "Не удалось получить статус HTTP.\n";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (statusCode != 200) {
        std::cerr << "Ошибка HTTP: " << statusCode << "\n";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::ofstream out(destination, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось создать файл: " << destination.string() << "\n";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    const DWORD bufferSize = 8192;
    std::vector<char> buffer(bufferSize);
    DWORD bytesRead = 0;
    DWORD totalDownloaded = 0;
    bool firstProgress = true;

    while (WinHttpReadData(hRequest, buffer.data(), bufferSize, &bytesRead) && bytesRead > 0) {
        out.write(buffer.data(), static_cast<std::streamsize>(bytesRead));
        totalDownloaded += bytesRead;

        if (totalDownloaded / (1024 * 1024) > firstProgress) {
            firstProgress = totalDownloaded / (1024 * 1024) + 1;
            std::cerr << "\rСкачано: " << (totalDownloaded / (1024 * 1024)) << " МБ...";
        }
    }

    out.close();
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!out) {
        std::cerr << "\nОшибка записи файла.\n";
        return false;
    }

    std::cerr << "\rСкачано: " << (totalDownloaded / (1024 * 1024)) << " МБ. Готово.\n";
    return true;
#else
    std::cerr << "Скачивание с Яндекс Диска поддерживается только на Windows.\n";
    return false;
#endif
}

/// Извлекает URL скачивания из download.json (простой парсинг без внешних библиотек).
static std::optional<std::string> extractDownloadUrl(const fs::path& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    const std::string key = "\"href\":\"";
    auto pos = content.find(key);
    if (pos == std::string::npos) return std::nullopt;
    pos += key.size();

    auto end = content.find('"', pos);
    if (end == std::string::npos) return std::nullopt;

    std::string url = content.substr(pos, end - pos);
    // Экранированные символы в JSON: \/ -> /
    std::string result;
    for (std::size_t i = 0; i < url.size(); ++i) {
        if (url[i] == '\\' && i + 1 < url.size() && url[i + 1] == '/') {
            result.push_back('/');
            ++i;
        } else {
            result.push_back(url[i]);
        }
    }
    return result;
}

/// Проверяет наличие PDF в корневой папке. Если нет — скачивает с Яндекс Диска
/// по ссылке из download.json. Возвращает путь к PDF-файлу.
static fs::path ensurePdfExists(const fs::path& root) {
    // Сначала ищем любой PDF
    std::vector<fs::path> pdfFiles;
    for (const auto& item : fs::directory_iterator(root)) {
        if (!item.is_regular_file()) continue;
        std::string extension = item.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".pdf") pdfFiles.push_back(item.path());
    }

    if (!pdfFiles.empty()) {
        if (pdfFiles.size() == 1) return pdfFiles.front();
        throw std::runtime_error("В корневой папке найдено несколько PDF-файлов. Оставьте один словарь.");
    }

    // PDF нет — пробуем скачать с Яндекс Диска
    const fs::path downloadJson = root / "download.json";
    if (!fs::exists(downloadJson)) {
        throw std::runtime_error("В корневой папке не найден PDF-файл и файл download.json со ссылкой на скачивание.");
    }

    auto urlOpt = extractDownloadUrl(downloadJson);
    if (!urlOpt) {
        throw std::runtime_error("Не удалось извлечь ссылку на скачивание из download.json.");
    }

    const std::string pdfName = "Blacks_Law_Dictionary__9ed.2009_.pdf";
    const fs::path pdfPath = root / pdfName;

    std::cerr << "PDF-файл не найден. Скачивание с Яндекс Диска...\n";
    std::cerr << "URL: " << *urlOpt << "\n";

    if (!downloadFile(*urlOpt, pdfPath)) {
        throw std::runtime_error("Не удалось скачать PDF-файл с Яндекс Диска. Проверьте подключение к интернету.");
    }

    if (!fs::exists(pdfPath) || fs::file_size(pdfPath) == 0) {
        throw std::runtime_error("Скачанный файл пуст или не был создан.");
    }

    std::cerr << "PDF-файл успешно скачан: " << pdfPath.string() << "\n";
    return pdfPath;
}

struct TextBlock {
    int page = 0;
    int top = 0;
    int left = 0;
    std::string html;
};

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string decodeUtf8Codepoint(unsigned int codepoint) {
    std::string result;
    if (codepoint <= 0x7F) {
        result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        result.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return result;
}

std::string decodeEntities(const std::string& value) {
    std::string result;
    for (std::size_t i = 0; i < value.size();) {
        if (value[i] != '&') {
            result.push_back(value[i++]);
            continue;
        }
        const auto semicolon = value.find(';', i + 1);
        if (semicolon == std::string::npos || semicolon - i > 16) {
            result.push_back(value[i++]);
            continue;
        }
        const std::string entity = value.substr(i + 1, semicolon - i - 1);
        if (entity == "amp") result.push_back('&');
        else if (entity == "lt") result.push_back('<');
        else if (entity == "gt") result.push_back('>');
        else if (entity == "quot") result.push_back('"');
        else if (entity == "apos") result.push_back('\'');
        else if (entity == "nbsp") result.push_back(' ');
        else if (entity == "shy") result += "\xC2\xAD";
        else if (!entity.empty() && entity[0] == '#') {
            try {
                const bool hex = entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X');
                const unsigned int codepoint = static_cast<unsigned int>(
                    std::stoul(entity.substr(hex ? 2 : 1), nullptr, hex ? 16 : 10));
                result += decodeUtf8Codepoint(codepoint);
            } catch (...) {
                result.append(value, i, semicolon - i + 1);
            }
        } else {
            result.append(value, i, semicolon - i + 1);
        }
        i = semicolon + 1;
    }
    return result;
}

std::string stripTags(const std::string& html) {
    std::string text;
    bool insideTag = false;
    for (char c : html) {
        if (c == '<') insideTag = true;
        else if (c == '>') insideTag = false;
        else if (!insideTag) text.push_back(c);
    }
    return decodeEntities(text);
}

std::string renderMarkup(const std::string& html) {
    std::string result;
    for (std::size_t i = 0; i < html.size();) {
        if (html[i] != '<') {
            const auto nextTag = html.find('<', i);
            result += decodeEntities(html.substr(i, nextTag == std::string::npos
                                                        ? std::string::npos
                                                        : nextTag - i));
            i = nextTag == std::string::npos ? html.size() : nextTag;
            continue;
        }
        const auto end = html.find('>', i);
        if (end == std::string::npos) break;
        const std::string tag = html.substr(i, end - i + 1);
        if (tag == "<b>") result += "**";
        else if (tag == "</b>") {
            while (!result.empty() && result.back() == ' ') result.pop_back();
            result += "**";
        } else if (tag == "<i>") result += "_";
        else if (tag == "</i>") {
            while (!result.empty() && result.back() == ' ') result.pop_back();
            result += "_";
        }
        i = end + 1;
    }
    return trim(result);
}

std::optional<int> readAttribute(const std::string& tag, const std::string& name) {
    const std::string key = name + "=\"";
    const auto start = tag.find(key);
    if (start == std::string::npos) return std::nullopt;
    const auto valueStart = start + key.size();
    const auto valueEnd = tag.find('"', valueStart);
    if (valueEnd == std::string::npos) return std::nullopt;
    try {
        return std::stoi(tag.substr(valueStart, valueEnd - valueStart));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<TextBlock> parseTextBlock(const std::string& line, int page) {
    const auto start = line.find("<text ");
    if (start == std::string::npos) return std::nullopt;
    const auto tagEnd = line.find('>', start);
    const auto contentEnd = line.find("</text>", tagEnd);
    if (tagEnd == std::string::npos || contentEnd == std::string::npos) return std::nullopt;
    const std::string tag = line.substr(start, tagEnd - start + 1);
    const auto top = readAttribute(tag, "top");
    const auto left = readAttribute(tag, "left");
    if (!top || !left) return std::nullopt;
    return TextBlock{page, *top, *left, line.substr(tagEnd + 1, contentEnd - tagEnd - 1)};
}

std::string normalizeTerm(std::string value) {
    value = decodeEntities(value);
    std::string normalized;
    bool pendingSpace = false;
    for (std::size_t i = 0; i < value.size();) {
        if (value.compare(i, 2, "\xC2\xAD") == 0) {
            i += 2;
            continue;
        }
        const unsigned char c = static_cast<unsigned char>(value[i++]);
        if (std::isspace(c)) {
            pendingSpace = !normalized.empty();
            continue;
        }
        if (pendingSpace) {
            normalized.push_back(' ');
            pendingSpace = false;
        }
        if (c < 0x80) normalized.push_back(static_cast<char>(std::tolower(c)));
        else normalized.push_back(static_cast<char>(c));
    }
    normalized = trim(normalized);
    while (!normalized.empty() &&
           (normalized.back() == '.' || normalized.back() == ',' ||
            normalized.back() == ';' || normalized.back() == ':')) normalized.pop_back();
    return trim(normalized);
}

std::vector<std::string> termSearchForms(const std::string& value) {
    const std::string term = normalizeTerm(value);
    std::vector<std::string> forms{term};
    auto addForm = [&](std::string form) {
        if (!form.empty() && std::find(forms.begin(), forms.end(), form) == forms.end()) {
            forms.push_back(std::move(form));
        }
    };
    auto endsWith = [](const std::string& text, const std::string& suffix) {
        return text.size() >= suffix.size() &&
               text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    if (endsWith(term, "ies") && term.size() > 3) {
        addForm(term.substr(0, term.size() - 3) + "y");
    } else if (endsWith(term, "sses") || endsWith(term, "ches") ||
               endsWith(term, "shes") || endsWith(term, "xes") ||
               endsWith(term, "zes")) {
        addForm(term.substr(0, term.size() - 2));
    } else if (term.size() > 2 && endsWith(term, "s") &&
               !endsWith(term, "ss") && !endsWith(term, "us") &&
               !endsWith(term, "is") && !endsWith(term, "ics") &&
               !endsWith(term, "news")) {
        addForm(term.substr(0, term.size() - 1));
    }
    return forms;
}

std::array<int, 2> findColumnLeftMargins(const std::vector<TextBlock>& blocks) {
    std::array<int, 2> margins{std::numeric_limits<int>::max(),
                               std::numeric_limits<int>::max()};
    for (const TextBlock& block : blocks) {
        if (block.top < 70 || block.top >= 1000 || trim(stripTags(block.html)).empty()) continue;
        const std::size_t column = block.left < 370 ? 0 : 1;
        margins[column] = std::min(margins[column], block.left);
    }
    return margins;
}

bool isEntryStart(const TextBlock& block, const std::array<int, 2>& margins,
                 bool hasEntrySpacing) {
    if (block.page < 29 || block.top < 70 || block.top >= 1000 ||
        !hasEntrySpacing || trim(stripTags(block.html)).empty()) return false;
    const std::size_t column = block.left < 370 ? 0 : 1;
    return margins[column] != std::numeric_limits<int>::max() &&
           block.left <= margins[column] + 8;
}

std::string textLineForBlock(const std::vector<TextBlock>& blocks, std::size_t index) {
    const TextBlock& first = blocks[index];
    const int column = first.left < 370 ? 0 : 1;
    std::string text = stripTags(first.html);
    for (std::size_t i = index + 1; i < blocks.size(); ++i) {
        const TextBlock& next = blocks[i];
        if (next.top != first.top || (next.left < 370 ? 0 : 1) != column) break;
        const std::string part = stripTags(next.html);
        if (!part.empty()) text += " " + part;
    }
    return text;
}

bool headingMatches(const std::string& line, const std::string& requestedTerm) {
    const std::string text = normalizeTerm(line);
    if (text.compare(0, requestedTerm.size(), requestedTerm) != 0) return false;
    if (text.size() == requestedTerm.size()) return true;

    std::size_t next = requestedTerm.size();
    while (next < text.size() && std::isspace(static_cast<unsigned char>(text[next]))) ++next;
    if (next == text.size()) return true;
    if (text[next] == '(') return true;
    // Запятая, точка с запятой и двоеточие после термина означают
    // инвертированную подзапись (например, "misconduct, affirmative"),
    // а не часть основной статьи — такие совпадения отвергаем.
    if (text[next] != '.') {
        return false;
    }

    ++next;
    while (next < text.size() && std::isspace(static_cast<unsigned char>(text[next]))) ++next;
    if (next == text.size()) return true;

    // These starts are continuation prose or citations, not dictionary definitions.
    if (text[next] == '-' || text.compare(next, 3, "\xE2\x80\x93") == 0 ||
        text.compare(next, 3, "\xE2\x80\x94") == 0 ||
        text.compare(next, 6, "also t") == 0 || text.compare(next, 3, "cf.") == 0 ||
        text.compare(next, 6, "[cases") == 0) return false;
    return true;
}

std::string headingRemainder(const TextBlock& block, const std::string& requestedTerm) {
    const std::string plain = stripTags(block.html);
    std::size_t source = 0;
    for (std::size_t query = 0; query < requestedTerm.size();) {
        if (plain.compare(source, 2, "\xC2\xAD") == 0) {
            source += 2;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(requestedTerm[query]))) {
            while (source < plain.size() &&
                   std::isspace(static_cast<unsigned char>(plain[source]))) ++source;
            ++query;
            continue;
        }
        if (source >= plain.size()) return {};
        const unsigned char sourceChar = static_cast<unsigned char>(plain[source++]);
        const unsigned char queryChar = static_cast<unsigned char>(requestedTerm[query++]);
        if (sourceChar < 0x80 && queryChar < 0x80) {
            if (std::tolower(sourceChar) != queryChar) return {};
        } else if (sourceChar != queryChar) {
            return {};
        }
    }

    std::string remainder = trim(plain.substr(source));
    if (!remainder.empty() && (remainder.front() == '.' || remainder.front() == ',' ||
                               remainder.front() == ';' || remainder.front() == ':')) {
        remainder.erase(remainder.begin());
    }
    return trim(remainder);
}

std::string appendText(std::string current, const std::string& html) {
    std::string part = renderMarkup(html);
    if (part.empty()) return current;

    const std::string softHyphen = "\xC2\xAD";
    bool joinWithoutSpace = false;
    if (current.size() >= softHyphen.size() &&
        current.compare(current.size() - softHyphen.size(), softHyphen.size(), softHyphen) == 0) {
        current.erase(current.size() - softHyphen.size());
        joinWithoutSpace = true;
    } else if (!current.empty() && current.back() == '-' &&
               !part.empty() && std::islower(static_cast<unsigned char>(part.front()))) {
        joinWithoutSpace = true;
    }
    if (!current.empty() && !joinWithoutSpace && part.front() != '.' && part.front() != ',' &&
        part.front() != ';' && part.front() != ':' && part.front() != ')' && part.front() != ']') {
        current.push_back(' ');
    }
    current += part;
    return current;
}

std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') quoted += "'\\''";
        else quoted.push_back(c);
    }
    quoted.push_back('\'');
    return quoted;
}

#ifdef _WIN32
std::string shellQuoteWin(const std::string& value) {
    std::string quoted = "\"";
    for (char c : value) {
        if (c == '"') quoted += "\"\"";
        else quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}
#endif

struct TemporaryFile {
    fs::path path;
    ~TemporaryFile() {
        std::error_code error;
        fs::remove(path, error);
    }
};

fs::path findPdfInProjectRoot(const fs::path& root) {
    std::vector<fs::path> pdfFiles;
    for (const auto& item : fs::directory_iterator(root)) {
        if (!item.is_regular_file()) continue;
        std::string extension = item.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".pdf") pdfFiles.push_back(item.path());
    }
    if (pdfFiles.empty()) throw std::runtime_error("В корневой папке проекта не найден PDF-файл.");
    if (pdfFiles.size() != 1) {
        throw std::runtime_error("В корневой папке найдено несколько PDF-файлов. Оставьте один словарь.");
    }
    return pdfFiles.front();
}

/// Ищет pdftohtml.exe: сначала локально рядом с программой (poppler/bin, bin),
/// затем полагается на системный PATH.
fs::path findPdftohtml(const fs::path& root) {
    std::vector<fs::path> candidates = {
        root / "poppler" / "bin" / "pdftohtml.exe",
        root / "bin" / "pdftohtml.exe",
        root / "poppler" / "pdftohtml.exe",
    };
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) return candidate;
    }
    // Fallback: надеемся, что pdftohtml доступен в PATH.
    return fs::path("pdftohtml");
}

TemporaryFile createTemporaryXmlPath() {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::random_device random;
    const auto suffix = static_cast<unsigned long long>(stamp) ^ random();
    return TemporaryFile{fs::temp_directory_path() /
                         ("black-law-dictionary-" + std::to_string(suffix) + ".xml")};
}

int extractPdfXml(const fs::path& root, const fs::path& pdfPath,
                  const fs::path& xmlPath) {
    const fs::path pdftohtml = findPdftohtml(root);

    if (!fs::exists(pdftohtml)) {
        std::cerr << "pdftohtml не найден по пути: " << pdftohtml.string() << '\n';
        return -1;
    }

    if (!fs::exists(pdfPath)) {
        std::cerr << "PDF-файл не найден: " << pdfPath.string() << '\n';
        return -1;
    }
    if (fs::file_size(pdfPath) == 0) {
        std::cerr << "PDF-файл пуст: " << pdfPath.string() << '\n';
        return -1;
    }

#if defined(_WIN32)
    // Используем CreateProcess вместо std::system, чтобы надёжно обработать
    // перенаправление stdout во временный файл без проблем с cmd.exe кавычками.
    // Дескриптор файла должен быть наследуемым, иначе дочерний процесс
    // pdftohtml не сможет записать в него через STARTF_USESTDHANDLES.
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hXmlFile = CreateFileW(
        xmlPath.wstring().c_str(),
        GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hXmlFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Не удалось создать временный файл: " << xmlPath.string() << "\n";
        return -1;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hXmlFile;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {};

    std::wstring wcmd = L"\"" + pdftohtml.wstring() + L"\" -xml -hidden -i -stdout \""
                        + pdfPath.wstring() + L"\"";

    BOOL success = CreateProcessW(
        nullptr, &wcmd[0], nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    if (!success) {
        CloseHandle(hXmlFile);
        std::cerr << "Не удалось запустить pdftohtml. Код ошибки: "
                  << GetLastError() << "\n";
        return -1;
    }

    CloseHandle(pi.hThread);
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 300000); // 5 минут
    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    } else {
        TerminateProcess(pi.hProcess, 1);
        std::cerr << "pdftohtml превысил лимит времени ожидания.\n";
    }
    CloseHandle(pi.hProcess);
    CloseHandle(hXmlFile); // закрываем файл только после завершения процесса

    if (exitCode != 0) {
        std::cerr << "pdftohtml завершился с ошибкой, код: " << exitCode << "\n";
    }

    // Проверяем, что XML-файл содержит данные
    if (fs::exists(xmlPath) && fs::file_size(xmlPath) == 0) {
        std::cerr << "Внимание: временный XML-файл пуст (" << xmlPath.string()
                  << "). pdftohtml не извлёк текст из PDF.\n";
        return -1;
    }

    return static_cast<int>(exitCode);
#else
    const std::string command = shellQuote(pdftohtml.string()) +
                                " -xml -hidden -i -stdout " +
                                shellQuote(pdfPath.string()) + " > " +
                                shellQuote(xmlPath.string()) + " 2>/dev/null";
    const int status = std::system(command.c_str());
    std::cerr << "Команда: " << command << "\n";
    if (status == -1) {
        std::cerr << "Ошибка запуска pdftohtml (std::system вернул -1). "
                  << "errno: " << errno << "\n";
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

std::vector<TextBlock> readPageBlocks(std::ifstream& xml, int& currentPage, std::string& line) {
    std::vector<TextBlock> blocks;
    while (std::getline(xml, line)) {
        const auto pageTag = line.find("<page ");
        if (pageTag != std::string::npos) {
            const auto number = readAttribute(line.substr(pageTag), "number");
            if (number) currentPage = *number;
            continue;
        }
        if (line.find("</page>") != std::string::npos) return blocks;
        if (auto block = parseTextBlock(line, currentPage)) blocks.push_back(std::move(*block));
    }
    return blocks;
}

// Убирает из текста статьи секции подзаписей (например, "affirmative misconduct",
// "juror misconduct" под "misconduct"), оставляя только определения основной статьи.
// Подзапись в рендеренном Markdown выглядит как _affirmative misconduct._(1897).
std::string stripSubEntries(std::string text) {
    const std::regex subEntryPattern(R"(_[^_]+_\s*\(\d{4}\))");
    std::sregex_iterator it(text.begin(), text.end(), subEntryPattern);
    std::sregex_iterator end;

    if (it == end) return text;

    std::string result = text.substr(0, it->position());

    std::size_t lastMatchEnd = 0;
    for (; it != end; ++it) {
        lastMatchEnd = it->position() + it->length();
    }

    if (lastMatchEnd < text.size()) {
        if (!result.empty()) result += " ";
        result += text.substr(lastMatchEnd);
    }

    return trim(result);
}

std::map<std::string, std::string> findRequestedEntries(
    std::ifstream& xml, const std::set<std::string>& requested) {
    std::map<std::string, std::string> found;
    bool dictionaryStarted = false;
    bool capturing = false;
    std::string currentKey;
    std::string currentEntry;
    int currentPage = 0;
    std::string line;

    auto saveCurrent = [&] {
        currentEntry = trim(currentEntry);
        if (capturing && !currentEntry.empty() && !found.count(currentKey)) {
            currentEntry = stripSubEntries(currentEntry);
            found.emplace(currentKey, currentEntry);
        }
        currentEntry.clear();
        currentKey.clear();
        capturing = false;
    };

    while (xml) {
        const std::vector<TextBlock> blocks = readPageBlocks(xml, currentPage, line);
        if (blocks.empty() && !xml) break;
        const auto leftMargins = findColumnLeftMargins(blocks);
        std::array<int, 2> previousTop{-1, -1};
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const TextBlock& block = blocks[i];
            const std::size_t column = block.left < 370 ? 0 : 1;
            const bool hasEntrySpacing = previousTop[column] < 0 ||
                                         block.top - previousTop[column] >= 17;
            if (isEntryStart(block, leftMargins, hasEntrySpacing)) {
                // The first entries are on PDF page 29. Their text is not bold, so
                // entry starts are detected by the left edge of each dictionary column.
                if (!dictionaryStarted && block.page >= 29) dictionaryStarted = true;
                const std::string headingLine = textLineForBlock(blocks, i);
                const std::string normHeading = normalizeTerm(headingLine);
                auto match = std::find_if(requested.begin(), requested.end(),
                                          [&](const std::string& term) {
                                              return headingMatches(headingLine, term);
                                          });
                if (dictionaryStarted && match != requested.end()) {
                    // Нашли запрошенную статью — сохраняем предыдущую и начинаем новую.
                    saveCurrent();
                    currentKey = *match;
                    capturing = true;
                    currentEntry = "**" + currentKey + "**";
                    const std::string remainder = headingRemainder(block, currentKey);
                    if (!remainder.empty()) currentEntry += " " + remainder;
                } else if (capturing &&
                           normHeading.find(currentKey) != std::string::npos) {
                    // Подзапись текущей статьи (например, "affirmative misconduct" под "misconduct").
                    // Не прерываем захват — продолжаем собирать определения основной статьи.
                } else if (capturing) {
                    // Началась совсем другая статья — сохраняем текущую и останавливаем захват.
                    saveCurrent();
                }
            } else if (capturing) {
                currentEntry = appendText(std::move(currentEntry), block.html);
            }
            if (block.top >= 70 && block.top < 1000) previousTop[column] = block.top;
        }
        if (!xml && !blocks.empty()) break;
    }
    saveCurrent();
    return found;
}

int main() {
    try {
        const fs::path root = getExeDirectory();
        const fs::path inputPath = root / "input.txt";
        const fs::path outputPath = root / "output.txt";

        std::ifstream input(inputPath);
        if (!input) {
            std::cerr << "Не удалось открыть input.txt в корневой папке проекта.\n";
            pauseBeforeExit();
            return 1;
        }
        std::vector<std::string> terms;
        std::set<std::string> requested;
        std::string term;
        while (std::getline(input, term)) {
            term = trim(term);
            if (term.empty()) continue;
            terms.push_back(term);
            const auto forms = termSearchForms(term);
            requested.insert(forms.begin(), forms.end());
        }
        if (terms.empty()) {
            std::cerr << "В input.txt нет терминов для поиска.\n";
            pauseBeforeExit();
            return 1;
        }

        const fs::path pdfPath = ensurePdfExists(root);
        TemporaryFile xmlFile = createTemporaryXmlPath();
        if (extractPdfXml(root, pdfPath, xmlFile.path) != 0) {
            std::cerr << "Не удалось извлечь текст из PDF. Установите Poppler (запустите install.bat) и проверьте, что pdftohtml доступен.\n";
            pauseBeforeExit();
            return 1;
        }
        std::ifstream xml(xmlFile.path);
        if (!xml) {
            std::cerr << "Не удалось прочитать временный файл с текстом PDF.\n";
            pauseBeforeExit();
            return 1;
        }
        const auto entries = findRequestedEntries(xml, requested);

        // При запуске двойным кликом интерактивный ввод невозможен —
        // по умолчанию записываем и найденные, и ненайденные термины.
        const bool writeNotFound = true;

        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "Не удалось создать output.txt.\n";
            pauseBeforeExit();
            return 1;
        }
        bool firstWritten = true;
        for (std::size_t i = 0; i < terms.size(); ++i) {
            auto entry = entries.end();
            for (const std::string& form : termSearchForms(terms[i])) {
                entry = entries.find(form);
                if (entry != entries.end()) break;
            }
            if (entry == entries.end() && !writeNotFound) {
                continue; // пропускаем ненайденные термины
            }
            if (!firstWritten) output << "\n\n";
            firstWritten = false;
            if (entry == entries.end()) {
                output << "**" << terms[i] << "** Определение не найдено в PDF.";
            } else {
                output << entry->second;
            }
        }
        if (!output) {
            std::cerr << "Ошибка при записи output.txt.\n";
            pauseBeforeExit();
            return 1;
        }
        std::cout << "Поиск завершён. Результаты сохранены в output.txt.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ошибка: " << error.what() << '\n';
        pauseBeforeExit();
        return 1;
    }
}
