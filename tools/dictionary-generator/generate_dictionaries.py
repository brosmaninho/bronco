#!/usr/bin/env python3
"""
Gerador de dicionarios de traducao PT-BR para o Bronco.

Pipeline por categoria:
  1. Busca a lista de IDs em /v2/<endpoint> (cacheada).
  2. Busca detalhes em lotes de <= 200 via /v2/<endpoint>?ids=...&lang=en (cacheado).
  3. Para cada nome em ingles unico (dedup por chave normalizada/minuscula),
     traduz para PT-BR via Argos Translate offline (cacheado).
  4. Monta o dicionario no schema que o loader C++ do Bronco espera.
  5. Escreve o JSON em data/dictionaries/pt-br/<categoria>.json.

Fonte de dados: API REST oficial do GW2 (api.guildwars2.com/v2), uso
EXPLICITAMENTE permitido pela ArenaNet (paineis que consomem a API REST publica).

Traducao: Opcao C (Argos Translate) - offline, gratis, sem chave, sem limites.

Modos que NAO exigem rede nem Argos:
  --dry-run       roda self-checks offline e sai (0 = ok, != 0 = falha)
  --no-translate  monta a estrutura com 'translated' = ingles (passthrough)

Idempotente e resumivel: todo I/O de rede e traducao e cacheado em disco.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Dict, List

# Imports locais (o script e pensado para rodar de qualquer cwd).
sys.path.insert(0, str(Path(__file__).resolve().parent))

import gw2_api  # noqa: E402
import translator  # noqa: E402

TOOL_DIR = Path(__file__).resolve().parent
DEFAULT_CACHE_DIR = TOOL_DIR / "cache"
DEFAULT_OUTPUT_DIR = Path("data/dictionaries/pt-br")
VERSION = "1.0.0"
LOCALE = "pt-br"

# Categoria -> nome de arquivo de saida.
CATEGORY_FILENAMES = {
    "skills": "skills.json",
    "items": "items.json",
    "traits": "traits.json",
    "specializations": "specializations.json",
    "professions": "professions.json",
    "pets": "pets.json",
    "masteries": "masteries.json",
}


# ---------------------------------------------------------------------- #
# Builder do dicionario
# ---------------------------------------------------------------------- #
def normalize_key(text: str) -> str:
    """Mesma normalizacao do loader C++: minusculas + trim."""
    return (text or "").strip().lower()


def build_dictionary(category: str, pairs: List[tuple]) -> dict:
    """Monta o dict no schema exato do loader C++.

    `pairs` e uma lista de (en, translated). Dedup por chave normalizada
    (minuscula) para bater com o lookup case-insensitive do C++; nomes vazios
    sao descartados; a primeira ocorrencia de cada chave vence.
    """
    seen = set()
    entries = []
    for en, translated in pairs:
        en = (en or "").strip()
        if not en:
            continue
        key = normalize_key(en)
        if key in seen:
            continue
        seen.add(key)
        entries.append({"en": en, "translated": (translated or "").strip()})

    return {
        "locale": LOCALE,
        "category": category,
        "version": VERSION,
        "entries": entries,
    }


def write_dictionary(output_dir: Path, category: str, dictionary: dict) -> Path:
    filename = CATEGORY_FILENAMES.get(category, f"{category}.json")
    out_path = Path(output_dir) / filename
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as fh:
        json.dump(dictionary, fh, ensure_ascii=False, indent=4)
        fh.write("\n")
    return out_path


# ---------------------------------------------------------------------- #
# Pipeline
# ---------------------------------------------------------------------- #
def _progress(iterable, desc: str, total=None):
    try:
        from tqdm import tqdm

        return tqdm(iterable, desc=desc, total=total)
    except ImportError:
        return iterable


def generate_category(
    category: str,
    client: gw2_api.GW2Client,
    trans: translator.Translator,
    output_dir: Path,
    limit: int = 0,
    no_translate: bool = False,
    batch_size: int = gw2_api.DEFAULT_BATCH_SIZE,
) -> dict:
    print(f"[{category}] buscando lista de IDs...")
    ids = client.fetch_id_list(category)
    if limit and limit > 0:
        ids = ids[:limit]
    print(f"[{category}] {len(ids)} IDs (limit={limit or 'nenhum'})")

    # Coleta nomes unicos em ingles preservando a primeira ocorrencia.
    batches = gw2_api.chunk_ids(ids, batch_size)
    seen_keys = set()
    english_names: List[str] = []
    fetched = 0
    for batch in _progress(batches, f"[{category}] baixando lotes", total=len(batches)):
        if not batch:
            continue
        for obj in client.fetch_batch(category, batch, lang="en"):
            fetched += 1
            fields = gw2_api.extract_text_fields(obj)
            name = fields["name"]
            if not name:
                continue
            key = normalize_key(name)
            if key in seen_keys:
                continue
            seen_keys.add(key)
            english_names.append(name)

    print(f"[{category}] {fetched} objetos, {len(english_names)} nomes unicos")

    pairs: List[tuple] = []
    if no_translate:
        pairs = [(name, name) for name in english_names]
    else:
        for name in _progress(english_names, f"[{category}] traduzindo", total=len(english_names)):
            pairs.append((name, trans.translate(name)))
        if trans.cache is not None:
            trans.cache.save()

    dictionary = build_dictionary(category, pairs)
    out_path = write_dictionary(output_dir, category, dictionary)
    print(f"[{category}] escrito: {out_path} ({len(dictionary['entries'])} entradas)")
    return dictionary


# ---------------------------------------------------------------------- #
# Self-checks offline (--dry-run): sem rede, sem Argos
# ---------------------------------------------------------------------- #
def run_self_checks() -> int:
    failures = []

    def check(name: str, cond: bool):
        status = "OK" if cond else "FALHOU"
        print(f"  [{status}] {name}")
        if not cond:
            failures.append(name)

    print("Rodando self-checks offline (sem rede, sem Argos)...")

    # (a) Schema do dicionario a partir de amostra em memoria.
    sample = [("Fireball", "Bola de Fogo"), ("Meteor Shower", "Chuva de Meteoros")]
    d = build_dictionary("skills", sample)
    schema_ok = (
        d.get("locale") == LOCALE
        and d.get("category") == "skills"
        and d.get("version") == VERSION
        and isinstance(d.get("entries"), list)
        and len(d["entries"]) == 2
        and all(
            isinstance(e.get("en"), str)
            and e["en"]
            and isinstance(e.get("translated"), str)
            and e["translated"]
            for e in d["entries"]
        )
    )
    check("schema do dicionario (locale/category/version/entries + en/translated)", schema_ok)

    # (b) Round-trip do cache de traducao.
    import tempfile

    with tempfile.TemporaryDirectory() as tmp:
        cache_path = Path(tmp) / "translations.json"
        c1 = translator.TranslationCache(cache_path)
        c1.put("Fireball", "Bola de Fogo")
        c1.put("Água", "Água")  # verifica UTF-8
        c1.save()
        c2 = translator.TranslationCache(cache_path)
        roundtrip_ok = c2.get("Fireball") == "Bola de Fogo" and c2.get("Água") == "Água"
    check("round-trip do cache de traducao (UTF-8)", roundtrip_ok)

    # (c) Batching em lotes de <= 200 sem perda.
    ids = list(range(1, 1005))  # 1004 ids
    batches = gw2_api.chunk_ids(ids, 200)
    reassembled = [i for b in batches for i in b]
    batch_ok = (
        all(len(b) <= gw2_api.MAX_IDS_PER_BATCH for b in batches)
        and len(batches) == 6
        and reassembled == ids
    )
    check("batching <= 200 divide e reassembla sem perda", batch_ok)

    # (d) Dedup por chave normalizada (case-insensitive) descarta duplicatas.
    pairs = [
        ("Fireball", "Bola de Fogo"),
        ("fireball", "IGNORADO"),  # duplicata por caso
        ("FIREBALL", "IGNORADO2"),
        ("", "vazio deve sumir"),  # nome vazio descartado
        ("Meteor Shower", "Chuva de Meteoros"),
    ]
    dd = build_dictionary("skills", pairs)
    dedup_ok = (
        len(dd["entries"]) == 2
        and dd["entries"][0]["en"] == "Fireball"
        and dd["entries"][0]["translated"] == "Bola de Fogo"
        and dd["entries"][1]["en"] == "Meteor Shower"
    )
    check("dedup por chave normalizada descarta duplicatas de caso e nomes vazios", dedup_ok)

    # (e) Translator importa sem argostranslate; so falha ao traduzir de fato.
    lazy_ok = True
    try:
        t = translator.Translator(cache=None)  # construcao nao deve exigir Argos
    except Exception:  # noqa: BLE001
        lazy_ok = False
    check("Translator instancia sem exigir argostranslate (import preguicoso)", lazy_ok)

    if failures:
        print(f"\n{len(failures)} self-check(s) falharam: {', '.join(failures)}")
        return 1
    print("\nTodos os self-checks passaram.")
    return 0


# ---------------------------------------------------------------------- #
# CLI
# ---------------------------------------------------------------------- #
def parse_args(argv=None):
    p = argparse.ArgumentParser(
        description="Gerador de dicionarios PT-BR do Bronco (GW2 API + Argos Translate offline).",
    )
    p.add_argument(
        "--category",
        choices=gw2_api.SUPPORTED_ENDPOINTS + ["all"],
        default="skills",
        help="categoria a gerar (ou 'all' para todas).",
    )
    p.add_argument("--limit", type=int, default=0, help="limita a quantidade de IDs (amostragem).")
    p.add_argument(
        "--install-model",
        action="store_true",
        help="baixa/instala o modelo Argos en->pt e sai.",
    )
    p.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="diretorio de saida (padrao: data/dictionaries/pt-br).",
    )
    p.add_argument(
        "--cache-dir",
        type=Path,
        default=DEFAULT_CACHE_DIR,
        help="diretorio de cache (padrao: tools/dictionary-generator/cache).",
    )
    p.add_argument(
        "--min-delay",
        type=float,
        default=gw2_api.DEFAULT_MIN_DELAY,
        help="delay minimo entre requests em segundos (rate limit).",
    )
    p.add_argument(
        "--no-translate",
        action="store_true",
        help="nao traduz; 'translated' recebe o ingles (demo de estrutura, sem Argos/rede de traducao).",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="roda self-checks offline (sem rede, sem Argos) e sai.",
    )
    return p.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)

    if args.dry_run:
        return run_self_checks()

    if args.install_model:
        print("Instalando modelo Argos en->pt (pode baixar ~100MB+)...")
        translator.install_model()
        print("Modelo en->pt instalado.")
        return 0

    cache_dir = Path(args.cache_dir)
    client = gw2_api.GW2Client(cache_dir=cache_dir, min_delay=args.min_delay)

    trans_cache = None
    trans = None
    if not args.no_translate:
        trans_cache = translator.TranslationCache(cache_dir / "translations.json")
    trans = translator.Translator(cache=trans_cache)

    categories = (
        gw2_api.SUPPORTED_ENDPOINTS if args.category == "all" else [args.category]
    )

    for category in categories:
        generate_category(
            category=category,
            client=client,
            trans=trans,
            output_dir=args.output_dir,
            limit=args.limit,
            no_translate=args.no_translate,
        )

    print("\nResumo:")
    print(f"  requests HTTP: {client.stats['requests']}")
    print(f"  cache hits (API): {client.stats['cache_hits']}")
    print(f"  retries: {client.stats['retries']}")
    print(f"  traducoes feitas: {trans.stats['translated']}")
    print(f"  cache hits (traducao): {trans.stats['cache_hits']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
