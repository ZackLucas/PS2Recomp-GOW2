# Runtime, recompilador, IOP, build e repositório

Resumo dos problemas fora do GS. Detalhes das etapas anteriores ao GS estão no `git log` das branches `gow2` e
`gow2-gpu-gs` e em `../PROGRESS.md`.

## Testes do runtime
| Problema | Situação |
|---|---|
| Em `main` do upstream os testes abortavam em `PS2Memory`; suítes VU1 nunca rodavam | corrigido na branch `gow2`; `ps2x_tests` = **438/438** |
| Testes com expectativa errada/ambígua do GS (5 de sprite zero, 3 de STQ/filtro/fan) | revistos contra o PCSX2 e corrigidos (ver 02 e 05) |
| Teste do fan lia VRAM CT32 linearmente | bug do teste; agora `gs.ReadVram` |

## Build
* `build-o2` (-O2, sem LTO, logs OFF): ~5,5 min completo; é onde vivem as ferramentas GPU
  (`-DPS2X_BUILD_GOW2_TOOLS=ON`).
* `build-dev` (-O1, PS2X_BUILD_TEST/RECOMP/ANALYZER=ON): roda `ps2xTest/ps2x_tests`.
* `build-rel` (O3+LTO): link > 25 min; evitar.
* `PS2X_ENABLE_RUNTIME_LOGS`, `AGRESSIVE_LOGS`, `IOP_RPC_TRACE` ficam **ON por padrão** e deixam tudo lento:
  desligar para medir.
* `cmake` e `ninja` ficam em `/home/dev-lucas/gow2/tools/bin` (fora do PATH): `export PATH=...:$PATH`.
* Python com PIL/capstone: `/home/dev-lucas/gow2/tools/bin/python3`; `numpy` **não** está instalado.

## Dados e repositório
* **Nunca** commitar: C++ gerado do GoW2 (`ps2xRuntime/src/runner/*`, `ps2_recompiled_*.h`), ELF, IRX, ISO, dumps GS,
  capturas. `games/gow2/out/` é ignorado. `register_functions.cpp` está em *skip-worktree*.
* `.recomp.json` está em `gow2`. O padrão do fork segue `main`.
* Remoto `gow2` (fork público ZackLucas/PS2Recomp-GOW2); `origin` = ran-j/PS2Recomp.
* Dados fora do git: `/home/dev-lucas/gow2/` (ELF, irx, shots, `pcsx2-ref/` com dump, imagens e saídas).

## VU / IOP
* VU1/GS interpretados no runtime; microVU0 não usado em ~11 s de execução no PCSX2.
* Lacunas de IOP: ver A11 em 07.
* Verificação estática de `Z`/MMI etc. não repetida nesta fase.

## Decisões de arquitetura registradas
* **GS exato em GPU antes de qualquer especialização:** referência correta primeiro, depois render targets nativos
  por passada conhecida (ideia do usuário de "recriar o jogo").
* O renderizador por hardware do PCSX2 usa cache de texturas/render targets com heurísticas e não reproduz a VRAM;
  nosso desenho reproduz. Etapa 2 (especializada) deve provar equivalência contra a etapa 1.
