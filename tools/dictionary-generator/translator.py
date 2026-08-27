"""
Camada de traducao offline EN->PT-BR (Opcao C: Argos Translate).

Argos Translate e gratuito, roda 100% local/offline, sem chave de API e sem
limites de uso. Esta camada e DESACOPLADA da busca de dados: o import de
argostranslate e feito de forma PREGUICOSA (dentro das funcoes), de modo que
este modulo pode ser importado, e os modos --dry-run / --no-translate podem
rodar, MESMO sem argostranslate instalado.

A traducao so falha (com erro claro e acionavel) quando de fato tentada sem o
modelo en->pt instalado.

Cache de traducao: cache/translations.json, indexado pela string em ingles, para
nao re-traduzir termos repetidos e permitir runs resumiveis.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Dict, Optional

FROM_CODE = "en"
TO_CODE = "pt"


class TranslationError(RuntimeError):
    """Erro claro e acionavel quando a traducao e tentada sem o modelo/lib."""


class TranslationCache:
    """Cache JSON simples indexado pela string de origem em ingles."""

    def __init__(self, path: Path):
        self.path = Path(path)
        self._data: Dict[str, str] = {}
        self._dirty = False
        self.load()

    def load(self) -> None:
        if self.path.exists():
            try:
                with self.path.open("r", encoding="utf-8") as fh:
                    loaded = json.load(fh)
                if isinstance(loaded, dict):
                    self._data = {str(k): str(v) for k, v in loaded.items()}
            except (json.JSONDecodeError, OSError):
                self._data = {}

    def save(self) -> None:
        if not self._dirty and self.path.exists():
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(self.path.suffix + ".tmp")
        with tmp.open("w", encoding="utf-8") as fh:
            json.dump(self._data, fh, ensure_ascii=False, indent=2, sort_keys=True)
        os.replace(tmp, self.path)
        self._dirty = False

    def get(self, key: str) -> Optional[str]:
        return self._data.get(key)

    def put(self, key: str, value: str) -> None:
        if self._data.get(key) != value:
            self._data[key] = value
            self._dirty = True

    def __contains__(self, key: str) -> bool:
        return key in self._data

    def __len__(self) -> int:
        return len(self._data)


# ---------------------------------------------------------------------- #
# Instalacao do modelo (--install-model)
# ---------------------------------------------------------------------- #
def install_model() -> None:
    """Baixa e instala o pacote de idioma en->pt do Argos Translate.

    Import preguicoso: so exige argostranslate quando esta funcao e chamada.
    """
    try:
        import argostranslate.package  # noqa: WPS433 (import local proposital)
    except ImportError as exc:
        raise TranslationError(
            "argostranslate nao esta instalado. Rode:\n"
            "  pip install -r tools/dictionary-generator/requirements.txt"
        ) from exc

    argostranslate.package.update_package_index()
    available = argostranslate.package.get_available_packages()
    match = next(
        (
            p
            for p in available
            if p.from_code == FROM_CODE and p.to_code == TO_CODE
        ),
        None,
    )
    if match is None:
        raise TranslationError(
            f"pacote de idioma {FROM_CODE}->{TO_CODE} nao encontrado no indice do Argos"
        )
    download_path = match.download()
    argostranslate.package.install_from_path(download_path)


class Translator:
    """Wrapper preguicoso do Argos Translate com cache de traducao."""

    def __init__(self, cache: Optional[TranslationCache] = None):
        self.cache = cache
        self._translate_fn = None  # carregado sob demanda
        self.stats = {"translated": 0, "cache_hits": 0}

    def _ensure_loaded(self) -> None:
        if self._translate_fn is not None:
            return
        try:
            import argostranslate.translate  # noqa: WPS433
        except ImportError as exc:
            raise TranslationError(
                "argostranslate nao esta instalado. Rode:\n"
                "  pip install -r tools/dictionary-generator/requirements.txt\n"
                "Ou use --no-translate / --dry-run para rodar sem traducao."
            ) from exc

        installed = argostranslate.translate.get_installed_languages()
        from_lang = next((l for l in installed if l.code == FROM_CODE), None)
        to_lang = next((l for l in installed if l.code == TO_CODE), None)
        if from_lang is None or to_lang is None:
            raise TranslationError(
                f"modelo {FROM_CODE}->{TO_CODE} nao instalado. Instale com:\n"
                "  python3 tools/dictionary-generator/generate_dictionaries.py --install-model"
            )
        self._translate_fn = from_lang.get_translation(to_lang).translate

    def translate(self, text: str) -> str:
        text = (text or "").strip()
        if not text:
            return ""
        if self.cache is not None:
            cached = self.cache.get(text)
            if cached is not None:
                self.stats["cache_hits"] += 1
                return cached

        self._ensure_loaded()
        result = self._translate_fn(text)
        self.stats["translated"] += 1
        if self.cache is not None:
            self.cache.put(text, result)
        return result
