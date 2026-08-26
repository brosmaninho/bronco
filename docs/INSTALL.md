# Guia de Instalacao - Bronco

Guia completo para instalar, configurar e remover o Bronco - Tradutor Overlay para Guild Wars 2.

## Requisitos

- Windows 10 ou 11 (64-bit)
- Guild Wars 2 instalado
- PowerShell 5.1 ou superior (ja incluido no Windows 10/11)

## Instalacao Rapida

### 1. Baixar a Release

1. Acesse a pagina de [Releases do Bronco](https://github.com/brosmaninho/bronco/releases)
2. Baixe o arquivo `bronco-vX.Y.Z-win64.zip` da versao mais recente
3. Extraia o ZIP em qualquer pasta (ex: sua area de trabalho)

### 2. Executar o Instalador

**Opcao A: Duplo-clique no .bat (mais simples - recomendado)**

1. Abra a pasta extraida
2. Navegue ate a pasta `scripts/`
3. De duplo-clique em **`Instalar.bat`**

Pronto! O .bat executa o instalador automaticamente, sem necessidade de configurar
politicas de execucao do PowerShell.

**Opcao B: Via terminal (alternativa)**

1. Abra o PowerShell (tecla Windows > digite "PowerShell" > Enter)
2. Navegue ate a pasta extraida:
   ```powershell
   cd "C:\Users\SeuUsuario\Desktop\bronco-vX.Y.Z-win64"
   ```
3. Execute o instalador:
   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\install.ps1
   ```

### 3. Seguir as Instrucoes

O instalador ira:
1. Detectar automaticamente a pasta do Guild Wars 2
2. Pedir para voce confirmar o local
3. Permitir escolher o idioma de traducao
4. Copiar todos os arquivos necessarios
5. Configurar o idioma escolhido

### 4. Jogar!

Inicie o Guild Wars 2 normalmente. O Bronco sera carregado automaticamente.

- Pressione **F8** para ligar/desligar o overlay de traducao

## Desinstalacao

### Via Script

**Opcao A: Duplo-clique no .bat (recomendado)**

1. Abra a pasta onde voce extraiu o ZIP do Bronco
2. Navegue ate `scripts/`
3. De duplo-clique em **`Desinstalar.bat`**

**Opcao B: Via terminal**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\uninstall.ps1
```

### Remocao Manual

Se preferir remover manualmente, delete os seguintes arquivos da pasta do Guild Wars 2:

- `d3d11.dll`
- Pasta `config/` (contem `bronco_config.json`)
- Pasta `data/` (contem `dictionaries/` e `tessdata/`)

## Trocar Idioma

Apos a instalacao, voce pode trocar o idioma de traducao:

1. Abra o arquivo `config\bronco_config.json` na pasta do Guild Wars 2
2. Altere o valor de `"target_locale"` para o codigo do idioma desejado
3. Salve o arquivo
4. Reinicie o Guild Wars 2

Exemplo:
```json
{
    "target_locale": "pt-br"
}
```

Idiomas disponiveis estao em `data\dictionaries\`. Cada pasta representa um idioma.

## Solucao de Problemas

### "Nao e possivel executar scripts neste sistema"

Isso acontece devido a politica de execucao do PowerShell. Solucao:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install.ps1
```

Ou altere a politica globalmente (requer Administrador):
```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

### "Acesso negado" ao copiar arquivos

Se o Guild Wars 2 esta instalado em `C:\Program Files\`, voce precisa executar
o instalador como Administrador:

1. Clique com botao direito no PowerShell
2. Selecione **"Executar como Administrador"**
3. Execute o script novamente

### Guild Wars 2 nao encontrado automaticamente

Se o instalador nao encontrou sua instalacao do GW2:

1. O instalador pedira para voce digitar o caminho manualmente
2. O caminho deve ser a pasta que contem `Gw2-64.exe`
3. Exemplos comuns:
   - `C:\Program Files\Guild Wars 2\`
   - `D:\Games\Guild Wars 2\`

### Antivirus bloqueia a d3d11.dll

Alguns antivirus podem marcar a DLL como suspeita porque ela usa a tecnica de
DLL proxy (injecao de DLL), comum em overlays de jogos.

**O Bronco e seguro.** Ele apenas le pixels da tela e desenha texto por cima.
Nao modifica memoria do jogo e nao envia dados para nenhum servidor.

Para resolver:
1. Adicione uma excecao no seu antivirus para a pasta do Guild Wars 2
2. Ou adicione especificamente o arquivo `d3d11.dll` como excecao

### O overlay nao aparece no jogo

1. Verifique se o arquivo `d3d11.dll` esta na mesma pasta que `Gw2-64.exe`
2. Pressione **F8** para garantir que o overlay esta ligado
3. Verifique se `data\tessdata\eng.traineddata` existe
4. Verifique se os dicionarios existem em `data\dictionaries\`

### Conflito com outros addons (arcdps, ReShade)

Se voce usa arcdps ou ReShade que tambem usam d3d11.dll:

1. Renomeie o d3d11.dll do outro addon (ex: `d3d11_arcdps.dll`)
2. Ou consulte a documentacao do addon para configurar carregamento em cadeia

## Estrutura dos Arquivos Instalados

Apos a instalacao, os seguintes arquivos estarao na pasta do Guild Wars 2:

```
Guild Wars 2/
    Gw2-64.exe             <- Executavel do jogo (ja existia)
    d3d11.dll              <- DLL do Bronco (overlay)
    config/
        bronco_config.json <- Configuracoes do Bronco
    data/
        dictionaries/
            pt-br/
                items.json
                skills.json
                npc_dialogues.json
        tessdata/
            eng.traineddata <- Dados do OCR (Tesseract)
```

## Suporte

Se encontrar problemas nao listados aqui, abra uma issue no
[repositorio do Bronco](https://github.com/brosmaninho/bronco/issues).
