# Bronco - Tradutor Overlay para Guild Wars 2

Bronco e um overlay de traducao para Guild Wars 2 que roda em paralelo com o jogo,
traduzindo textos da interface (tooltips de itens, habilidades e dialogos de NPC) para
o idioma desejado em tempo real.

O software funciona como uma DLL proxy de DirectX 11, interceptando a renderizacao do
jogo para exibir traducoes sobrepostas via Dear ImGui. E 100% passivo - apenas le
pixels da tela e renderiza texto por cima, sem modificar memoria do jogo, enviar
comandos ao servidor ou alterar arquivos do cliente.

## Conformidade com a ArenaNet

O Bronco respeita **todas** as restricoes da ArenaNet para add-ons de Guild Wars 2:

- **Regra 1:1**: O Bronco NAO envia nenhum comando ao servidor. Zero inputs, zero acoes.
  Apenas le a tela e exibe texto traduzido.
- **Vantagem Competitiva Zero**: O overlay apenas traduz texto visivel na tela. Nao
  revela dados ocultos, nao oferece auxilio em combate, nao modifica velocidade.
- **Integridade de Memoria/Economia**: Nao modifica memoria do processo do jogo, nao
  altera arquivos .dat, nao interage com a loja de gemas.

A arquitetura utilizada (DLL proxy DX11 + overlay) e a mesma de add-ons aprovados
como arcdps, ReShade e Nexus/Raidcore.

## Arquitetura

```
+-------------------+     +-------------------+     +-------------------+
|    GW2.exe        | --> |  d3d11.dll        | --> |  d3d11.dll real   |
|   (Guild Wars 2)  |     |  (Bronco proxy)   |     |  (System32)       |
+-------------------+     +-------------------+     +-------------------+
                                    |
                                    v
                          +-------------------+
                          |  Present() Hook   |
                          +-------------------+
                                    |
                    +---------------+---------------+
                    |               |               |
                    v               v               v
            +------------+  +------------+  +------------+
            |    OCR     |  | Traducao   |  |  Overlay   |
            | (Tesseract)|  | (Dicionario)|  | (ImGui)   |
            +------------+  +------------+  +------------+
                                    |
                                    v
                          +-------------------+
                          |   Cache LRU       |
                          +-------------------+
```

### Modulos

| Modulo | Diretorio | Descricao |
|--------|-----------|-----------|
| DLL Proxy | `src/proxy/` | Carrega d3d11.dll real e encaminha chamadas |
| Present Hook | `src/hook/` | Intercepta IDXGISwapChain::Present() |
| Overlay | `src/overlay/` | Renderiza traducoes via Dear ImGui |
| OCR | `src/ocr/` | Extrai texto da tela via Tesseract 5 |
| Traducao | `src/translation/` | Busca em dicionarios locais JSON |
| Cache | `src/cache/` | Cache LRU para performance |
| Config | `src/config/` | Gerenciador de configuracao JSON |

## Requisitos

- Windows 10/11 (64-bit)
- Visual Studio 2022 (com componentes C++ Desktop)
- [vcpkg](https://github.com/microsoft/vcpkg)
- CMake 3.21+

## Instalacao Rapida (Recomendado)

A maneira mais simples de instalar o Bronco:

1. Baixe o ZIP da [pagina de Releases](https://github.com/brosmaninho/bronco/releases)
2. Extraia em qualquer pasta
3. De duplo-clique em `scripts\Instalar.bat`

O instalador detecta automaticamente a pasta do Guild Wars 2, copia todos os
arquivos necessarios e configura o idioma de traducao.

> **Nota:** O arquivo `.bat` ja cuida automaticamente da politica de execucao do
> PowerShell. Nao e necessario executar nenhum comando manual.

Para desinstalar, de duplo-clique em `scripts\Desinstalar.bat`.

Para instrucoes detalhadas, veja [docs/INSTALL.md](docs/INSTALL.md).

## Dependencias

Gerenciadas via vcpkg:

- **Dear ImGui** - UI overlay com binding DirectX 11
- **Tesseract 5** - OCR para extracao de texto da tela
- **SQLite3** - Armazenamento opcional de dicionarios
- **nlohmann/json** - Configuracao e dicionarios em JSON

## Build (Compilacao)

### 1. Instalar vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat
```

Defina a variavel de ambiente:
```powershell
$env:VCPKG_ROOT = "C:\vcpkg"
```

### 2. Instalar dependencias

```powershell
vcpkg install imgui[dx11-binding,win32-binding]:x64-windows-static-md
vcpkg install tesseract:x64-windows-static-md
vcpkg install sqlite3:x64-windows-static-md
vcpkg install nlohmann-json:x64-windows-static-md
```

### 3. Compilar o projeto

```powershell
# Clonar o repositorio
git clone https://github.com/brosmaninho/bronco.git
cd bronco

# Configurar com CMake (usando preset)
cmake --preset windows-msvc

# Compilar
cmake --build build/windows-msvc --config Release
```

A DLL compilada estara em `build/windows-msvc/bin/Release/d3d11.dll`.

### Build Debug

```powershell
cmake --preset windows-msvc-debug
cmake --build build/windows-msvc-debug --config Debug
```

## Instalacao Manual

Se preferir instalar manualmente:

1. Compile o projeto (veja secao Build acima) ou baixe a release
2. Copie os seguintes arquivos para a pasta do Guild Wars 2
   (normalmente `C:\Program Files\Guild Wars 2\`):

```
Guild Wars 2/
    d3d11.dll              <- DLL compilada do Bronco
    config/
        bronco_config.json <- Configuracao
    data/
        dictionaries/
            pt-br/
                items.json
                skills.json
                npc_dialogues.json
        tessdata/
            eng.traineddata <- Dados do Tesseract (baixar separadamente)
```

3. Baixe o arquivo `eng.traineddata` do
   [repositorio do Tesseract](https://github.com/tesseract-ocr/tessdata_best)
   e coloque em `data/tessdata/`

4. Inicie o Guild Wars 2 normalmente. O Bronco sera carregado automaticamente.

## Uso

- **F8** (padrao): Liga/desliga o overlay de traducao
- **ALT**: O painel e passivo por padrao e nao intercepta o mouse. Segure ALT
  para "destravar" o painel e arrasta-lo pela barra de titulo. Ao soltar ALT o
  painel volta a ser passivo e o mouse funciona normalmente no jogo.
- As traducoes aparecem automaticamente sobre tooltips e dialogos de NPC
- Configuracoes podem ser ajustadas em `config/bronco_config.json`

## Configuracao

O arquivo `config/bronco_config.json` permite ajustar:

| Campo | Descricao | Padrao |
|-------|-----------|--------|
| `target_locale` | Codigo do idioma alvo | `"pt-br"` |
| `dictionary_path` | Caminho para dicionarios | `"data/dictionaries"` |
| `tessdata_path` | Caminho para dados do Tesseract | `"data/tessdata"` |
| `cache_capacity` | Tamanho maximo do cache LRU | `5000` |
| `font_size` | Tamanho da fonte do overlay | `16.0` |
| `toggle_hotkey` | Virtual key code do hotkey | `119` (F8) |
| `ocr_confidence_threshold` | Confianca minima do OCR (0-100) | `60.0` |
| `ocr_interval_ms` | Intervalo entre leituras OCR (ms) | `500` |
| `ocr_follow_mouse` | Segue o cursor para o OCR (modo padrao) | `true` |
| `ocr_follow_width` | Largura da regiao ao redor do cursor (px) | `500` |
| `ocr_follow_height` | Altura da regiao ao redor do cursor (px) | `400` |
| `ocr_regions` | Regioes fixas da tela para OCR (fallback) | Tooltips + Dialogos |

### Regioes OCR

Por padrao o Bronco usa o modo "seguir o mouse" (`ocr_follow_mouse: true`). Como os
tooltips do Guild Wars 2 aparecem onde o cursor esta, a cada leitura o Bronco captura
uma unica regiao centrada na posicao atual do mouse, com tamanho `ocr_follow_width` x
`ocr_follow_height` (padrao 500x400). A regiao e ajustada automaticamente para caber
dentro da tela capturada. Isso e muito mais eficaz que regioes fixas.

Se preferir usar regioes fixas, defina `ocr_follow_mouse: false`. Nesse caso o Bronco
usa a lista `ocr_regions` como fallback. As regioes fixas sao configuradas para
resolucao 1920x1080; se voce usa outra resolucao, ajuste os valores `x`, `y`, `width` e
`height` de cada regiao.

## Adicionando Novos Idiomas

Veja [docs/ADDING_LANGUAGES.md](docs/ADDING_LANGUAGES.md) para instrucoes detalhadas.

Resumo rapido:

1. Crie um diretorio em `data/dictionaries/<codigo-idioma>/`
2. Adicione `items.json`, `skills.json`, `npc_dialogues.json` seguindo o formato:

```json
{
    "locale": "es",
    "category": "items",
    "version": "0.1.0",
    "entries": [
        { "en": "Copper Ore", "translated": "Mineral de Cobre" }
    ]
}
```

3. Altere `target_locale` na configuracao para o codigo do novo idioma

## Estrutura do Projeto

```
bronco/
    CMakeLists.txt          # Build principal
    CMakePresets.json       # Presets para Windows/MSVC
    vcpkg.json              # Manifesto de dependencias
    config/
        bronco_config.json  # Configuracao padrao
    data/
        dictionaries/
            pt-br/          # Dicionarios Portugues BR
                items.json
                skills.json
                npc_dialogues.json
    docs/
        ARCHITECTURE.md     # Documentacao de arquitetura
        ADDING_LANGUAGES.md # Como adicionar idiomas
    src/
        dllmain.cpp         # Entry point da DLL
        CMakeLists.txt      # Build dos modulos
        proxy/              # DLL proxy (d3d11)
        hook/               # Hook do Present()
        overlay/            # ImGui overlay
        ocr/                # Motor OCR (Tesseract)
        translation/        # Motor de traducao
        cache/              # Cache LRU
        config/             # Gerenciador de config
```

## Contribuindo

1. Fork o repositorio
2. Crie uma branch para sua feature (`git checkout -b feature/nova-feature`)
3. Faca commit das suas mudancas
4. Push para a branch (`git push origin feature/nova-feature`)
5. Abra um Pull Request

### Diretrizes

- Use C++20 e siga o estilo existente do codigo
- Namespace `bronco::` para todo codigo
- Include guards com `#pragma once`
- Documente funcoes publicas com comentarios `///`
- Teste mudancas localmente antes de submeter PR

## Licenca

Este projeto e distribuido sob a licenca MIT. Veja o arquivo LICENSE para detalhes.

## Aviso Legal

Este software nao e afiliado, endossado ou aprovado pela ArenaNet ou NCSOFT.
Guild Wars 2 e marcas registradas da NCSOFT Corporation. Use por sua conta e risco.
