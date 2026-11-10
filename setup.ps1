#===============================================================================
# Black's Law Dictionary Search — установщик зависимостей (Windows)
#===============================================================================
# Скачивает Poppler с утилитой pdftohtml и размещает её рядом с программой,
# чтобы dictionary_search.exe работал без ручной настройки PATH.
#
# Использование: дважды кликните на install.bat (или запустите setup.ps1 от имени
# администратора, если нужно добавить poppler в системный PATH).
#===============================================================================

$ErrorActionPreference = 'Stop'

# Папка, где лежит этот скрипт (и dictionary_search.exe)
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$popplerDir = Join-Path $scriptDir "poppler"
$binDir = Join-Path $popplerDir "bin"
$pdftohtmlExe = Join-Path $binDir "pdftohtml.exe"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Black's Law Dictionary Search — установка" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Если уже установлено — проверяем, что файл запускается, и предлагаем переустановку
if (Test-Path $pdftohtmlExe) {
    Write-Host "Poppler найден в: $popplerDir" -ForegroundColor Green
    Write-Host "Если dictionary_search.exe не работает, попробуйте переустановить:" -ForegroundColor Yellow
    Write-Host "  1. Удалите папку poppler вручную"
    Write-Host "  2. Запустите install.bat снова"
    Write-Host ""
    Read-Host "Нажмите Enter для выхода"
    exit 0
}

# Проверяем подключение к интернету
Write-Host "Проверка подключения к интернету..." -ForegroundColor Yellow
try {
    $response = Invoke-WebRequest -Uri "https://api.github.com" -TimeoutSec 10 -ErrorAction Stop
    Write-Host "Интернет доступен." -ForegroundColor Green
} catch {
    Write-Host "ОШИБКА: нет подключения к интернету." -ForegroundColor Red
    Write-Host "Подключите компьютер к сети и запустите install.bat повторно." -ForegroundColor Red
    Write-Host ""
    Read-Host "Нажмите Enter для выхода"
    exit 1
}

# Узнаём последнюю версию Poppler для Windows
Write-Host "Поиск последней версии Poppler..." -ForegroundColor Yellow
try {
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/oschwartz10612/poppler-windows/releases/latest" -TimeoutSec 30 -ErrorAction Stop
    $assets = $release.assets | Where-Object { $_.name -like "*.zip" -and $_.name -like "*x86_64*" }
    if ($assets.Count -eq 0) {
        # Попробуем любой zip без привязки к архитектуре
        $assets = $release.assets | Where-Object { $_.name -like "*.zip" }
    }
    if ($assets.Count -eq 0) {
        throw "Не найден подходящий архив в последнем релизе."
    }
    $asset = $assets[0]
    $downloadUrl = $asset.browser_download_url
    $fileName = $asset.name
    Write-Host "Найдено: $fileName" -ForegroundColor Green
} catch {
    Write-Host "ОШИБКА при получении информации о релизе: $_" -ForegroundColor Red
    Write-Host ""
    Read-Host "Нажмите Enter для выхода"
    exit 1
}

# Скачиваем архив
$zipPath = Join-Path $scriptDir ("poppler-" + [IO.Path]::GetRandomFileName() + ".zip")
Write-Host "Скачивание $fileName..." -ForegroundColor Yellow
try {
    Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath -TimeoutSec 120 -ErrorAction Stop
    Write-Host "Скачано: $zipPath" -ForegroundColor Green
} catch {
    Write-Host "ОШИБКА при скачивании: $_" -ForegroundColor Red
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Write-Host ""
    Read-Host "Нажмите Enter для выхода"
    exit 1
}

# Распаковываем
Write-Host "Распаковка в $popplerDir..." -ForegroundColor Yellow
try {
    if (Test-Path $popplerDir) { Remove-Item $popplerDir -Recurse -Force }
    New-Item -ItemType Directory -Path $popplerDir -Force | Out-Null
    Expand-Archive -Path $zipPath -DestinationPath $popplerDir -Force | Out-Null
    Write-Host "Распаковано." -ForegroundColor Green
} catch {
    Write-Host "ОШИБКА при распаковке: $_" -ForegroundColor Red
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Write-Host ""
    Read-Host "Нажмите Enter для выхода"
    exit 1
}

# Удаляем скачанный архив
Remove-Item $zipPath -Force

# Ищем pdftohtml.exe внутри распакованной папки
$foundItem = Get-ChildItem -Path $popplerDir -Filter "pdftohtml.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $foundItem) {
    Write-Host "ОШИБКА: pdftohtml.exe не найден внутри архива." -ForegroundColor Red
    Write-Host ""
    Read-Host "Нажмите Enter для выхода"
    exit 1
}

$found = $foundItem.FullName
$foundDir = Split-Path -Parent $found
$binExe = Join-Path $binDir "pdftohtml.exe"

# Если pdftohtml.exe уже в poppler/bin — ничего перемещать не надо.
# Если в подпапке — копируем ВСЮ папку bin (со всеми DLL) в poppler/bin.
if ($foundDir -ne $binDir) {
    if (-not (Test-Path $binDir)) {
        New-Item -ItemType Directory -Path $binDir -Force | Out-Null
    }
    Copy-Item -Path (Join-Path $foundDir "*") -Destination $binDir -Recurse -Force
    Write-Host "Скопировано содержимое из $foundDir в $binDir" -ForegroundColor Green
} else {
    Write-Host "pdftohtml.exe уже находится в $binDir" -ForegroundColor Green
}

# Проверяем, что рядом с pdftohtml.exe есть нужные DLL
$dllCount = (Get-ChildItem -Path $binDir -Filter "*.dll" -ErrorAction SilentlyContinue).Count
Write-Host "Найдено DLL в ${binDir}: $dllCount" -ForegroundColor Gray

# Очищаем вложенные папки, оставив только bin (содержимое bin не трогаем)
$childItems = Get-ChildItem -Path $popplerDir | Where-Object { $_.Name -ne "bin" }
if ($childItems) {
    $childItems | Remove-Item -Recurse -Force
}

# Добавляем в PATH пользователя (опционально, для удобства)
try {
    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $binDirResolved = $binDir
    if ($currentPath -notlike "*$binDirResolved*") {
        [Environment]::SetEnvironmentVariable("Path", $currentPath + ";" + $binDirResolved, "User")
        Write-Host "Путь к Poppler добавлен в PATH пользователя." -ForegroundColor Green
    } else {
        Write-Host "Путь к Poppler уже есть в PATH пользователя." -ForegroundColor Green
    }
} catch {
    Write-Host "Не удалось добавить в PATH (не критично — программа найдёт pdftohtml рядом с собой)." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Установка завершена!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Poppler установлен в: $popplerDir"
Write-Host "Теперь запустите dictionary_search.exe."
Write-Host ""
Read-Host "Нажмите Enter для выхода"