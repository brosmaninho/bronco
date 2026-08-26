# Como Adicionar Novos Idiomas

## Estrutura de Dicionario

Cada idioma e um diretorio dentro de `data/dictionaries/` com o codigo do locale
(ex: `pt-br`, `es`, `fr`, `de`, `ja`).

Dentro de cada diretorio de idioma, crie os seguintes arquivos JSON:

```
data/dictionaries/<locale>/
    items.json
    skills.json
    npc_dialogues.json
```

## Formato do JSON

Cada arquivo segue o formato:

```json
{
    "locale": "<codigo-do-idioma>",
    "category": "<items|skills|npc_dialogues>",
    "version": "0.1.0",
    "entries": [
        { "en": "Texto em ingles", "translated": "Texto traduzido" },
        ...
    ]
}
```

## Exemplo: Adicionando Espanhol

1. Crie o diretorio: `data/dictionaries/es/`
2. Crie `items.json`:

```json
{
    "locale": "es",
    "category": "items",
    "version": "0.1.0",
    "entries": [
        { "en": "Copper Ore", "translated": "Mineral de Cobre" },
        { "en": "Iron Sword", "translated": "Espada de Hierro" }
    ]
}
```

3. Repita para `skills.json` e `npc_dialogues.json`
4. Altere `target_locale` em `config/bronco_config.json` para `"es"`

## Dicas

- A busca e case-insensitive, entao "Fireball" e "fireball" encontram a mesma entrada
- Mantenha os textos em ingles exatamente como aparecem no jogo
- O sistema suporta caracteres UTF-8 (acentos, cedilha, etc.)
- Voce pode adicionar novos arquivos de categoria alem dos tres padroes
  (mas sera necessario atualizar o DictionaryManager para carrega-los)
