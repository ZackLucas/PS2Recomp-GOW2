# Resolvidos: oráculo PCSX2, medição e ambiente

O PCSX2 v2.9.127 (AppImage, renderizador por software, `Renderer = 13`) com BIOS `scph77001-us` (do usuário)
é o oráculo. Scripts em `games/gow2/scripts/`.

| # | Problema | Causa | Solução | Estado |
|---|---|---|---|---|
| O1 | Captura F8 do PCSX2 sempre mostrava o menu | em *replay* de dump o F8 não devolve o quadro exibido | capturar a **janela** (PIL `ImageGrab` no X aninhado `:7`) | resolvido |
| O2 | `DISPFB` alterado no dump era ignorado | PCSX2 usa o do estado salvo | copiar o buffer desejado para o buffer de exibição original com um sprite de blit | resolvido |
| O3 | Instâncias antigas do PCSX2 contaminavam capturas (falsos "0,3% iguais"; depois 20+ vivas) | `kill` por `comm`/SIGTERM não matava | `kill -9` por linha de comando (`pcsx2-v2.9`) e esperar sumir; função em `pcsx2_exact.py` | resolvido |
| O4 | Dump truncado deixava `PRMODECONT=0` -> atributos do blit (TME/ABE/FST) ignorados | estado do jogo | forçar `PRMODECONT=1` em `gsdump_trunc.py` | resolvido |
| O5 | Janela escala o quadro com fator não inteiro / linhas de campo | modo entrelaçado + aspecto | modo progressivo forçado (`SMODE2=0`), `IntegerScaling=true`, `linear_present_mode=0`, `AspectRatio=Stretch`: x exato 2x, y 345/128 | resolvido |
| O6 | Janela em posição diferente após reinício (origem y=55 -> 30) | gerenciador de janelas | origem = posição da janela + (13, 30), lida de `x11_nested.py list` | resolvido |
| O7 | Só ~214 linhas visíveis em modo progressivo; as 16 primeiras cobertas pelo texto do PCSX2 | tamanho da janela / OSD | medir só linhas 16..213 | **contornado** (ver 07: metade inferior não medida) |
| O8 | Comparar dumps diferentes dos dois lados | ruído de método | comparar o **mesmo arquivo truncado** nos dois lados | resolvido |
| O9 | `pkill -f`/`pgrep -f | kill` matava meu próprio shell (saída 144) | o padrão casava com a linha de comando da ferramenta | matar só por PID explícito | resolvido |
| O10 | Falso "PCSX2 força bilinear (`filter=2`)" | capturas contaminadas por instância antiga | **retirado**: `filter` 2 ou 3 não altera nada | retirado (ver 05) |
| O11 | `/tmp` limpo após reinício (scripts auxiliares, diretório de dados) | reinício da máquina | `pcsx2_exact.py` cria Xwayland `:7` e o diretório de dados sozinho | resolvido |
| O12 | `perf` exigia `perf_event_paranoid=1` após cada reinício | configuração do kernel | usuário roda `sudo sysctl kernel.perf_event_paranoid=1` | contornado |
| O13 | `git push` falhou: "agent communication failed" | ssh-agent (gcr) perdido | usuário reautenticou | resolvido |

## Ferramentas de medição (referência rápida)
* `pcsx2_oracle.sh dump out.png` — captura simples (LANCZOS, piso de ~2).
* `trunc_bisect.py K...` — bissecção com captura redimensionada (piso 1,9 no menu; **não** usar para <2).
* `trunc_exact.py K[,K...]` — comparação exata (env `TB_BLIT`, `TB_WH`, `TB_TAG`).
* `syn_exact.py <exp> W H` — mesma coisa para dumps sintéticos (`SYN_CPU=1`, `SYN_REUSE=1`).
* `gsdump_synth.py` — sondas: `z_*`, `tri_edge`, `tri_tex[L|N][Q]`, `gouraud`, `blend_grid`, `modulate0/2`,
  `spr_up_*`, `spr_lin<N>`, `spr_ramp<N>`, `spr_zero`, `spr_min`, `*_upload`, etc.
* `gs_gpu_replay` (env): `GT_MODE`, `GT_SERIALIZE`, `GT_FIRSTDIFF`, `GT_LIST_PACKET[a-b]`, `GT_LIST_XFER`,
  `GT_VRAM_PACKETS`, `GT_WATCH_WORD`, `GT_ISOLATE`, `GT_SNAP*`, `GT_CPU_FLUSH_EACH`, `GT_ZERO_*`.

## Captura de dumps de gameplay (2026-10-16)
Roteiro automático no X aninhado `:7` (`/tmp/boot_play.py`, teclas por `x11_nested.py key`): boot ~60 s -> K (New Game) -> K (Spartan)
-> Return a cada 8 s até ~200 s (pula o FMV; **parar depois**, Start abre a pausa) -> J/W/D para combater -> `Shift+F8` a cada 15 s
(dump de 1 quadro; ~7–12 MB). Dumps de FMV têm 1–2 primitivas. Replay `gow2_gs_gpu_replay --out prefix` grava `prefixgpuN.ppm`
só no modo CPU+GPU. Nomear saídas pelo horário do dump (bug do octal). `hud_exact.py` (DBY para ver linhas baixas) ficou experimental.
`perf` exige `kernel.perf_event_paranoid` liberado pelo usuário (estava em 4).
