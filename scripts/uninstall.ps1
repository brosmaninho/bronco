#Requires -Version 5.1
<#
.SYNOPSIS
    Desinstalador do Bronco - Remove o overlay de traducao do Guild Wars 2

.DESCRIPTION
    Este script remove todos os arquivos do Bronco da pasta do Guild Wars 2,
    restaurando o jogo ao estado original.

.NOTES
    Requer PowerShell 5.1 ou superior (incluido no Windows 10/11).
    Execute com: powershell -ExecutionPolicy Bypass -File uninstall.ps1
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

# --- Funcoes auxiliares ---

function Write-Banner {
    $banner = @"

    ============================================
    |                                          |
    |   BRONCO - Desinstalador                 |
    |                                          |
    ============================================

"@
    Write-Host $banner -ForegroundColor Cyan
}

function Write-Step {
    param([string]$Message)
    Write-Host "[*] " -ForegroundColor Green -NoNewline
    Write-Host $Message
}

function Write-Warning-Message {
    param([string]$Message)
    Write-Host "[!] " -ForegroundColor Yellow -NoNewline
    Write-Host $Message
}

function Write-Error-Message {
    param([string]$Message)
    Write-Host "[X] " -ForegroundColor Red -NoNewline
    Write-Host $Message
}

function Write-Success {
    param([string]$Message)
    Write-Host "[OK] " -ForegroundColor Green -NoNewline
    Write-Host $Message
}

function Find-GW2Path {
    <#
    .SYNOPSIS
        Tenta encontrar a pasta de instalacao do Guild Wars 2.
    #>
    $candidates = @()

    # Caminhos comuns
    $commonPaths = @(
        "C:\Program Files\Guild Wars 2",
        "C:\Program Files (x86)\Guild Wars 2",
        "D:\Program Files\Guild Wars 2",
        "D:\Guild Wars 2",
        "C:\Games\Guild Wars 2"
    )

    foreach ($path in $commonPaths) {
        if (Test-Path (Join-Path $path "Gw2-64.exe")) {
            $candidates += $path
        }
    }

    # Verificar registro
    $regPaths = @(
        "HKLM:\SOFTWARE\ArenaNet\Guild Wars 2",
        "HKLM:\SOFTWARE\WOW6432Node\ArenaNet\Guild Wars 2",
        "HKCU:\SOFTWARE\ArenaNet\Guild Wars 2"
    )

    foreach ($regPath in $regPaths) {
        try {
            if (Test-Path $regPath) {
                $regValue = Get-ItemProperty -Path $regPath -Name "Path" -ErrorAction SilentlyContinue
                if ($regValue -and $regValue.Path) {
                    $gw2Dir = Split-Path $regValue.Path -Parent
                    if ((Test-Path $gw2Dir) -and ($candidates -notcontains $gw2Dir)) {
                        $candidates += $gw2Dir
                    }
                }
            }
        }
        catch {
            # Ignora erros de acesso ao registro
        }
    }

    return $candidates
}

# --- Inicio do desinstalador ---

Clear-Host
Write-Banner

# --- Passo 1: Detectar pasta do GW2 ---
Write-Step "Procurando instalacao do Guild Wars 2..."
Write-Host ""

$gw2Paths = Find-GW2Path
$gw2Path = $null

if ($gw2Paths.Count -eq 0) {
    Write-Warning-Message "Nao foi possivel detectar a pasta do Guild Wars 2 automaticamente."
    Write-Host ""
    $gw2Path = Read-Host "Digite o caminho da pasta do Guild Wars 2 onde o Bronco esta instalado"

    if (-not (Test-Path $gw2Path)) {
        Write-Error-Message "O caminho informado nao existe: $gw2Path"
        Read-Host "Pressione Enter para sair"
        exit 1
    }
}
elseif ($gw2Paths.Count -eq 1) {
    $gw2Path = $gw2Paths[0]
    Write-Success "Guild Wars 2 encontrado em: $gw2Path"
    Write-Host ""
    $confirm = Read-Host "Desinstalar o Bronco deste local? (S/N)"
    if ($confirm -notin @('S', 's', 'Sim', 'sim', 'Y', 'y')) {
        $gw2Path = Read-Host "Digite o caminho da pasta do Guild Wars 2"
        if (-not (Test-Path $gw2Path)) {
            Write-Error-Message "O caminho informado nao existe: $gw2Path"
            Read-Host "Pressione Enter para sair"
            exit 1
        }
    }
}
else {
    Write-Host "Multiplas instalacoes encontradas:" -ForegroundColor Cyan
    for ($i = 0; $i -lt $gw2Paths.Count; $i++) {
        Write-Host "  [$($i + 1)] $($gw2Paths[$i])"
    }
    Write-Host "  [0] Digitar caminho manualmente"
    Write-Host ""
    $choice = Read-Host "Escolha uma opcao"

    if ($choice -eq '0') {
        $gw2Path = Read-Host "Digite o caminho da pasta do Guild Wars 2"
        if (-not (Test-Path $gw2Path)) {
            Write-Error-Message "O caminho informado nao existe: $gw2Path"
            Read-Host "Pressione Enter para sair"
            exit 1
        }
    }
    else {
        $index = [int]$choice - 1
        if ($index -ge 0 -and $index -lt $gw2Paths.Count) {
            $gw2Path = $gw2Paths[$index]
        }
        else {
            Write-Error-Message "Opcao invalida."
            Read-Host "Pressione Enter para sair"
            exit 1
        }
    }
}

Write-Host ""

# --- Passo 2: Verificar se o Bronco esta instalado ---
$dllPath = Join-Path $gw2Path "d3d11.dll"
$ocrDllPath = Join-Path $gw2Path "bronco_ocr.dll"
$configPath = Join-Path $gw2Path "config"
$dataPath = Join-Path $gw2Path "data"
$logPath = Join-Path $gw2Path "bronco_log.txt"
$iniPath = Join-Path $gw2Path "bronco_imgui.ini"

$broncoFound = $false
if (Test-Path $dllPath) { $broncoFound = $true }
if (Test-Path $ocrDllPath) { $broncoFound = $true }
if (Test-Path $configPath) { $broncoFound = $true }
if (Test-Path $dataPath) { $broncoFound = $true }
if (Test-Path $logPath) { $broncoFound = $true }
if (Test-Path $iniPath) { $broncoFound = $true }

if (-not $broncoFound) {
    Write-Warning-Message "Nenhum arquivo do Bronco encontrado em: $gw2Path"
    Write-Host ""
    $forceRemove = Read-Host "Deseja tentar remover mesmo assim? (S/N)"
    if ($forceRemove -notin @('S', 's', 'Sim', 'sim', 'Y', 'y')) {
        Write-Host ""
        Write-Host "Desinstalacao cancelada." -ForegroundColor Yellow
        Read-Host "Pressione Enter para sair"
        exit 0
    }
}

# --- Passo 3: Confirmar desinstalacao ---
Write-Host ""
Write-Warning-Message "Os seguintes arquivos serao removidos de: $gw2Path"
Write-Host ""
if (Test-Path $dllPath) { Write-Host "  - d3d11.dll" }
if (Test-Path $ocrDllPath) { Write-Host "  - bronco_ocr.dll" }
if (Test-Path $configPath) { Write-Host "  - config/ (pasta completa)" }
if (Test-Path $dataPath) { Write-Host "  - data/ (pasta completa)" }
if (Test-Path $logPath) { Write-Host "  - bronco_log.txt" }
if (Test-Path $iniPath) { Write-Host "  - bronco_imgui.ini" }
Write-Host ""

$finalConfirm = Read-Host "Confirma a remocao? Esta acao nao pode ser desfeita. (S/N)"
if ($finalConfirm -notin @('S', 's', 'Sim', 'sim', 'Y', 'y')) {
    Write-Host ""
    Write-Host "Desinstalacao cancelada." -ForegroundColor Yellow
    Read-Host "Pressione Enter para sair"
    exit 0
}

Write-Host ""

# --- Passo 4: Remover arquivos ---
Write-Step "Removendo arquivos do Bronco..."
Write-Host ""

$errors = @()

# Remover d3d11.dll (with ownership verification)
if (Test-Path $dllPath) {
    try {
        $isBroncoDll = $false
        $fileInfo = $null

        try {
            $fileInfo = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($dllPath)
        }
        catch {
            # Unable to read version info - proceed with caution
        }

        if ($fileInfo -and $fileInfo.ProductName -like "*Bronco*") {
            $isBroncoDll = $true
        }

        if ($isBroncoDll) {
            Remove-Item -Path $dllPath -Force
            Write-Host "  Removido: d3d11.dll" -ForegroundColor Green
        }
        else {
            Write-Warning-Message "O d3d11.dll encontrado pode nao pertencer ao Bronco."
            Write-Warning-Message "Outros overlays (arcdps, ReShade, GW2Hook) tambem usam esse arquivo."
            if ($fileInfo -and $fileInfo.ProductName) {
                Write-Warning-Message "ProductName identificado: $($fileInfo.ProductName)"
            }
            Write-Host ""
            $dllConfirm = Read-Host "Deseja remover mesmo assim? (S/N)"
            if ($dllConfirm -in @('S', 's', 'Sim', 'sim', 'Y', 'y')) {
                Remove-Item -Path $dllPath -Force
                Write-Host "  Removido: d3d11.dll" -ForegroundColor Green
            }
            else {
                Write-Warning-Message "d3d11.dll mantido."
            }
        }
    }
    catch {
        $errors += "d3d11.dll: $($_.Exception.Message)"
        Write-Error-Message "Falha ao remover d3d11.dll: $($_.Exception.Message)"
    }
}

# Remover bronco_ocr.dll
if (Test-Path $ocrDllPath) {
    try {
        Remove-Item -Path $ocrDllPath -Force
        Write-Host "  Removido: bronco_ocr.dll" -ForegroundColor Green
    }
    catch {
        $errors += "bronco_ocr.dll: $($_.Exception.Message)"
        Write-Error-Message "Falha ao remover bronco_ocr.dll: $($_.Exception.Message)"
    }
}

# Remover bronco_log.txt (arquivo gerado em runtime)
if (Test-Path $logPath) {
    try {
        Remove-Item -Path $logPath -Force
        Write-Host "  Removido: bronco_log.txt" -ForegroundColor Green
    }
    catch {
        $errors += "bronco_log.txt: $($_.Exception.Message)"
        Write-Error-Message "Falha ao remover bronco_log.txt: $($_.Exception.Message)"
    }
}

# Remover bronco_imgui.ini (arquivo gerado em runtime)
if (Test-Path $iniPath) {
    try {
        Remove-Item -Path $iniPath -Force
        Write-Host "  Removido: bronco_imgui.ini" -ForegroundColor Green
    }
    catch {
        $errors += "bronco_imgui.ini: $($_.Exception.Message)"
        Write-Error-Message "Falha ao remover bronco_imgui.ini: $($_.Exception.Message)"
    }
}

# Remover config/
if (Test-Path $configPath) {
    try {
        # Verificar se ha apenas arquivos do Bronco
        $configFiles = Get-ChildItem -Path $configPath -Recurse
        $broncoConfigFile = Join-Path $configPath "bronco_config.json"

        if ((Test-Path $broncoConfigFile) -and ($configFiles.Count -le 1)) {
            # Apenas o arquivo do Bronco - remover pasta inteira
            Remove-Item -Path $configPath -Recurse -Force
            Write-Host "  Removido: config/" -ForegroundColor Green
        }
        elseif (Test-Path $broncoConfigFile) {
            # Outros arquivos existem - remover apenas o do Bronco
            Remove-Item -Path $broncoConfigFile -Force
            Write-Host "  Removido: config/bronco_config.json" -ForegroundColor Green
            Write-Warning-Message "Pasta config/ mantida (contem outros arquivos)"
        }
        else {
            # Pasta config existe mas nao tem arquivo do Bronco
            Write-Warning-Message "Pasta config/ nao contem arquivo do Bronco, mantida."
        }
    }
    catch {
        $errors += "config/: $($_.Exception.Message)"
        Write-Error-Message "Falha ao remover config/: $($_.Exception.Message)"
    }
}

# Remover data/
if (Test-Path $dataPath) {
    try {
        # Verificar se tem subpastas do Bronco
        $dictPath = Join-Path $dataPath "dictionaries"
        $tessPath = Join-Path $dataPath "tessdata"
        $hasBroncoData = (Test-Path $dictPath) -or (Test-Path $tessPath)

        if ($hasBroncoData) {
            # Remover subpastas do Bronco
            if (Test-Path $dictPath) {
                Remove-Item -Path $dictPath -Recurse -Force
                Write-Host "  Removido: data/dictionaries/" -ForegroundColor Green
            }
            if (Test-Path $tessPath) {
                Remove-Item -Path $tessPath -Recurse -Force
                Write-Host "  Removido: data/tessdata/" -ForegroundColor Green
            }

            # Remover pasta data se estiver vazia
            $remaining = Get-ChildItem -Path $dataPath -Recurse
            if ($remaining.Count -eq 0) {
                Remove-Item -Path $dataPath -Force
                Write-Host "  Removido: data/ (vazia)" -ForegroundColor Green
            }
            else {
                Write-Warning-Message "Pasta data/ mantida (contem outros arquivos)"
            }
        }
        else {
            Write-Warning-Message "Pasta data/ nao contem dados do Bronco, mantida."
        }
    }
    catch {
        $errors += "data/: $($_.Exception.Message)"
        Write-Error-Message "Falha ao remover data/: $($_.Exception.Message)"
    }
}

Write-Host ""

# --- Passo 5: Resultado ---
if ($errors.Count -eq 0) {
    $successMessage = @"

    ============================================
    |                                          |
    |   Desinstalacao concluida com sucesso!   |
    |                                          |
    ============================================

    O Bronco foi removido de:
    $gw2Path

    O Guild Wars 2 voltara ao funcionamento normal.
    Nenhuma modificacao foi feita no jogo em si.

"@
    Write-Host $successMessage -ForegroundColor Green
}
else {
    Write-Host ""
    Write-Warning-Message "Desinstalacao concluida com erros:"
    foreach ($err in $errors) {
        Write-Host "  - $err" -ForegroundColor Red
    }
    Write-Host ""
    Write-Warning-Message "Tente executar novamente como Administrador."
    Write-Warning-Message "Se o GW2 estiver aberto, feche-o primeiro e tente novamente."
}

Read-Host "Pressione Enter para sair"
