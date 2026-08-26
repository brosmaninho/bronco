#Requires -Version 5.1
<#
.SYNOPSIS
    Instalador do Bronco - Tradutor Overlay para Guild Wars 2

.DESCRIPTION
    Este script instala o Bronco na pasta do Guild Wars 2, copiando todos os
    arquivos necessarios e configurando o idioma de traducao.

.NOTES
    Requer PowerShell 5.1 ou superior (incluido no Windows 10/11).
    Execute com: powershell -ExecutionPolicy Bypass -File install.ps1
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

# --- Funcoes auxiliares ---

function Write-Banner {
    $banner = @"

    ============================================
    |                                          |
    |   BRONCO - Tradutor para Guild Wars 2   |
    |                                          |
    |   Instalador v1.0                        |
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

function Test-Administrator {
    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentUser)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
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

    # Verificar registro (HKLM)
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

function Get-AvailableLanguages {
    <#
    .SYNOPSIS
        Lista idiomas disponiveis nos dicionarios.
    #>
    $scriptDir = Split-Path -Parent $PSScriptRoot
    $dictPath = Join-Path $scriptDir "data\dictionaries"

    if (-not (Test-Path $dictPath)) {
        # Tentar caminho relativo ao script
        $dictPath = Join-Path (Split-Path -Parent $PSCommandPath) "..\data\dictionaries"
    }

    if (Test-Path $dictPath) {
        $languages = Get-ChildItem -Path $dictPath -Directory | Select-Object -ExpandProperty Name
        return $languages
    }

    return @("pt-br")
}

# --- Inicio do instalador ---

Clear-Host
Write-Banner

# Verificar se esta rodando como administrador
if (-not (Test-Administrator)) {
    Write-Warning-Message "Voce nao esta executando como Administrador."
    Write-Warning-Message "Se o GW2 estiver em 'Program Files', a instalacao pode falhar."
    Write-Host ""
    $continueChoice = Read-Host "Deseja continuar mesmo assim? (S/N)"
    if ($continueChoice -notin @('S', 's', 'Sim', 'sim', 'Y', 'y')) {
        Write-Host ""
        Write-Host "Instalacao cancelada. Execute novamente como Administrador." -ForegroundColor Yellow
        Write-Host "Clique com botao direito no PowerShell > 'Executar como Administrador'"
        Read-Host "Pressione Enter para sair"
        exit 0
    }
    Write-Host ""
}

# --- Passo 1: Detectar pasta do GW2 ---
Write-Step "Procurando instalacao do Guild Wars 2..."
Write-Host ""

$gw2Paths = Find-GW2Path
$gw2Path = $null

if ($gw2Paths.Count -eq 0) {
    Write-Warning-Message "Nao foi possivel detectar a pasta do Guild Wars 2 automaticamente."
    Write-Host ""
    $gw2Path = Read-Host "Digite o caminho completo da pasta do Guild Wars 2"

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
    $confirm = Read-Host "Instalar neste local? (S/N)"
    if ($confirm -notin @('S', 's', 'Sim', 'sim', 'Y', 'y')) {
        $gw2Path = Read-Host "Digite o caminho completo da pasta do Guild Wars 2"
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
        $gw2Path = Read-Host "Digite o caminho completo da pasta do Guild Wars 2"
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
Write-Step "Pasta de destino: $gw2Path"
Write-Host ""

# --- Passo 2: Escolher idioma ---
Write-Step "Configurando idioma de traducao..."
Write-Host ""

$languages = Get-AvailableLanguages
$selectedLocale = "pt-br"

if ($languages.Count -gt 1) {
    Write-Host "Idiomas disponiveis:" -ForegroundColor Cyan
    for ($i = 0; $i -lt $languages.Count; $i++) {
        $marker = ""
        if ($languages[$i] -eq "pt-br") { $marker = " (padrao)" }
        Write-Host "  [$($i + 1)] $($languages[$i])$marker"
    }
    Write-Host ""
    $langChoice = Read-Host "Escolha o idioma (Enter para padrao: pt-br)"

    if ($langChoice -ne '') {
        $langIndex = [int]$langChoice - 1
        if ($langIndex -ge 0 -and $langIndex -lt $languages.Count) {
            $selectedLocale = $languages[$langIndex]
        }
    }
}

Write-Success "Idioma selecionado: $selectedLocale"
Write-Host ""

# --- Passo 3: Copiar arquivos ---
Write-Step "Copiando arquivos para a pasta do Guild Wars 2..."
Write-Host ""

$scriptRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)

# Fallback: if d3d11.dll is not found at the expected root, try $PSScriptRoot directly
if (-not (Test-Path (Join-Path $scriptRoot "d3d11.dll"))) {
    $altRoot = $PSScriptRoot
    if (Test-Path (Join-Path $altRoot "d3d11.dll")) {
        $scriptRoot = $altRoot
    }
}

# --- Pre-flight validation: check all required files before copying anything ---
$requiredFiles = @(
    (Join-Path $scriptRoot "d3d11.dll"),
    (Join-Path $scriptRoot "config\bronco_config.json"),
    (Join-Path $scriptRoot "data\tessdata\eng.traineddata")
)

$missingFiles = @()
foreach ($f in $requiredFiles) {
    if (-not (Test-Path $f)) {
        $missingFiles += $f
    }
}

# Check that at least one dictionary directory exists
$dictRoot = Join-Path $scriptRoot "data\dictionaries"
$hasDictionaries = $false
if (Test-Path $dictRoot) {
    $dictDirs = Get-ChildItem -Path $dictRoot -Directory
    if ($dictDirs.Count -gt 0) {
        $hasDictionaries = $true
    }
}

if ($missingFiles.Count -gt 0 -or -not $hasDictionaries) {
    Write-Error-Message "Arquivos necessarios nao encontrados. Verifique se voce extraiu o ZIP completo."
    Write-Host ""
    foreach ($mf in $missingFiles) {
        Write-Host "  Faltando: $mf" -ForegroundColor Red
    }
    if (-not $hasDictionaries) {
        Write-Host "  Faltando: dicionarios em data\dictionaries\" -ForegroundColor Red
    }
    Write-Host ""
    Write-Error-Message "Resolucao esperada do pacote: $scriptRoot"
    Read-Host "Pressione Enter para sair"
    exit 1
}

try {
    # Copiar d3d11.dll
    $dllSource = Join-Path $scriptRoot "d3d11.dll"

    Write-Host "  Copiando d3d11.dll..." -NoNewline
    Copy-Item -Path $dllSource -Destination $gw2Path -Force
    Write-Host " OK" -ForegroundColor Green

    # Copiar config/
    $configSource = Join-Path $scriptRoot "config"
    $configDest = Join-Path $gw2Path "config"
    if (Test-Path $configSource) {
        Write-Host "  Copiando config/..." -NoNewline
        if (-not (Test-Path $configDest)) {
            New-Item -ItemType Directory -Path $configDest -Force | Out-Null
        }
        Copy-Item -Path "$configSource\*" -Destination $configDest -Recurse -Force
        Write-Host " OK" -ForegroundColor Green
    }

    # Copiar data/
    $dataSource = Join-Path $scriptRoot "data"
    $dataDest = Join-Path $gw2Path "data"
    if (Test-Path $dataSource) {
        Write-Host "  Copiando data/..." -NoNewline
        if (-not (Test-Path $dataDest)) {
            New-Item -ItemType Directory -Path $dataDest -Force | Out-Null
        }
        Copy-Item -Path "$dataSource\*" -Destination $dataDest -Recurse -Force
        Write-Host " OK" -ForegroundColor Green
    }
}
catch {
    Write-Host ""
    Write-Error-Message "Erro ao copiar arquivos: $($_.Exception.Message)"
    Write-Host ""
    Write-Warning-Message "Possivel causa: permissao negada. Tente executar como Administrador."
    Read-Host "Pressione Enter para sair"
    exit 1
}

# --- Passo 4: Atualizar configuracao ---
Write-Step "Atualizando configuracao com idioma selecionado..."

try {
    $configFile = Join-Path $gw2Path "config\bronco_config.json"
    if (Test-Path $configFile) {
        $config = Get-Content -Path $configFile -Raw | ConvertFrom-Json
        $config.target_locale = $selectedLocale
        $config | ConvertTo-Json -Depth 10 | Set-Content -Path $configFile -Encoding UTF8
        Write-Success "Configuracao atualizada: target_locale = $selectedLocale"
    }
    else {
        Write-Warning-Message "Arquivo de configuracao nao encontrado. Usando configuracao padrao."
    }
}
catch {
    Write-Warning-Message "Nao foi possivel atualizar a configuracao: $($_.Exception.Message)"
    Write-Warning-Message "Voce pode editar manualmente: config\bronco_config.json"
}

Write-Host ""

# --- Passo 5: Mensagem de sucesso ---
$successMessage = @"

    ============================================
    |                                          |
    |   Instalacao concluida com sucesso!      |
    |                                          |
    ============================================

    Arquivos instalados em:
    $gw2Path

    Idioma configurado: $selectedLocale

    Para usar o Bronco:
    1. Inicie o Guild Wars 2 normalmente
    2. O overlay sera carregado automaticamente
    3. Pressione F8 para ligar/desligar a traducao

    Para desinstalar:
    Execute scripts\uninstall.ps1 do pacote extraido

"@

Write-Host $successMessage -ForegroundColor Green
Read-Host "Pressione Enter para sair"
