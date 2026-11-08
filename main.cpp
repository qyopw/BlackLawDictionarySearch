#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/wait.h>

namespace fs = std::filesystem;

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
    if (text[next] != '.' && text[next] != ',' && text[next] != ';' && text[next] != ':') {
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

TemporaryFile createTemporaryXmlPath() {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::random_device random;
    const auto suffix = static_cast<unsigned long long>(stamp) ^ random();
    return TemporaryFile{fs::temp_directory_path() /
                         ("black-law-dictionary-" + std::to_string(suffix) + ".xml")};
}

int extractPdfXml(const fs::path& pdfPath, const fs::path& xmlPath) {
    const std::string command = "pdftohtml -xml -hidden -i -stdout " +
                                shellQuote(pdfPath.string()) + " > " +
                                shellQuote(xmlPath.string()) + " 2>/dev/null";
    const int status = std::system(command.c_str());
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
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
                saveCurrent();
                // The first entries are on PDF page 29. Their text is not bold, so
                // entry starts are detected by the left edge of each dictionary column.
                if (!dictionaryStarted && block.page >= 29) dictionaryStarted = true;
                const std::string headingLine = textLineForBlock(blocks, i);
                auto match = std::find_if(requested.begin(), requested.end(),
                                          [&](const std::string& term) {
                                              return headingMatches(headingLine, term);
                                          });
                if (dictionaryStarted && match != requested.end()) {
                    currentKey = *match;
                    capturing = true;
                    currentEntry = "**" + currentKey + "**";
                    const std::string remainder = headingRemainder(block, currentKey);
                    if (!remainder.empty()) currentEntry += " " + remainder;
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
        const fs::path root = fs::current_path();
        const fs::path inputPath = root / "input.txt";
        const fs::path outputPath = root / "output.txt";

        std::ifstream input(inputPath);
        if (!input) {
            std::cerr << "Не удалось открыть input.txt в корневой папке проекта.\n";
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
            return 1;
        }

        const fs::path pdfPath = findPdfInProjectRoot(root);
        TemporaryFile xmlFile = createTemporaryXmlPath();
        if (extractPdfXml(pdfPath, xmlFile.path) != 0) {
            std::cerr << "Не удалось извлечь текст из PDF. Установите Poppler и проверьте, что pdftohtml доступен в PATH.\n";
            return 1;
        }
        std::ifstream xml(xmlFile.path);
        if (!xml) {
            std::cerr << "Не удалось прочитать временный файл с текстом PDF.\n";
            return 1;
        }
        const auto entries = findRequestedEntries(xml, requested);

        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "Не удалось создать output.txt.\n";
            return 1;
        }
        for (std::size_t i = 0; i < terms.size(); ++i) {
            if (i != 0) output << "\n\n";
            auto entry = entries.end();
            for (const std::string& form : termSearchForms(terms[i])) {
                entry = entries.find(form);
                if (entry != entries.end()) break;
            }
            if (entry == entries.end()) {
                output << "**" << terms[i] << "** Определение не найдено в PDF.";
            } else {
                output << entry->second;
            }
        }
        if (!output) {
            std::cerr << "Ошибка при записи output.txt.\n";
            return 1;
        }
        std::cout << "Поиск завершён. Результаты сохранены в output.txt.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ошибка: " << error.what() << '\n';
        return 1;
    }
}
