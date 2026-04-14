# Architecture Overview

## Core Principle

This repository is **C++-first**. Python exists only as a **subordinate inference compatibility layer** for the chosen Qwen3-TTS stack.

## High-Level Components

1. **C++/Qt Frontend + Orchestration (primary)**
   - App startup and tray lifecycle
   - Main window and UI state
   - Command dispatch from tray/UI
   - Runtime path resolution policy
   - Backend process launch/supervision seam
   - Future ownership: queueing, live-read watch, settings persistence, diagnostics orchestration, output workflow

2. **Python Backend Bridge (thin and replaceable)**
   - CLI entrypoint
   - Healthcheck command
   - Synthesis adapter command path (placeholder)
   - Model/bootstrap helper usage
   - Runtime capability snapshot (CUDA/precision profile)

## Boundary Rules

- Do not move orchestration or product behavior into Python unless unavoidable.
- Keep the Python API narrow so it can be minimized/replaced later.
- C++ remains source of truth for long-lived app behavior.

## Current Runtime Flow

1. User launches Qt app (`LocalComputerSpeech`).
2. C++ path resolver determines persistent root paths and checks local model directory presence.
3. If model assets are missing, C++ shows status that install is required (no silent download).
4. Python healthcheck/synth CLI can be called through scripts now; later through C++ process supervision.

## Repo Shape Rationale

- `src/` holds dominant C++ app surface.
- `backend/` is intentionally constrained to bridge responsibilities.
- `scripts/` + `run.ps1` orchestrate deterministic setup and model install into persistent large-data root.
