"""Paths and constants shared across the Lab package."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]          # repo root
LAB_DIR = ROOT / "Lab"
PROFILE_DIR = LAB_DIR / "profiles"
REPORT_DIR = LAB_DIR / "reports"
DB_PATH = LAB_DIR / "lab.sqlite"
SAVED = ROOT / "Saved" / "SymbioticWorld"           # where run CSVs land (both backends)
ANALYSIS_DIR = ROOT / "Analysis"

OLLAMA_URL = os.environ.get("OLLAMA_URL", "http://localhost:11434")
OLLAMA_MODEL = os.environ.get("LAB_MODEL", "llama3.1")

# Hard caps (design principle: structure over prompt-hope)
MAX_FINDINGS_PER_OBSERVER = 3
MAX_NEW_HYPOTHESES_PER_MEETING = 2
MAX_ACTIVE_CARDS_PER_MEETING = 4     # stance round covers at most this many cards
CONTEXT_TOKEN_BUDGET = 4000          # soft, enforced by truncating retrieved text

AGENT_DOMAINS = ("behavior", "genetics", "ecology")
