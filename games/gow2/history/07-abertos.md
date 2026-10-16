# Problemas abertos

Cada item: **o que é**, **por que não foi resolvido**, **quanto afeta**, **como atacar**.
Ordem aproximada por impacto no objetivo (jogo nativo, correto, em tempo real).

## A1. GPU ligada ao runtime, mas o jogo vivo ainda roda a ~3–4 VSync/s (parcialmente resolvido 2026-10-16)
* **Feito:** `PS2_GS_BACKEND=gpu` (`gs_gpu_backend.cpp`, thread própria com contexto GL via janela GLFW oculta). Ver 03 (G11).
* **Falta:** os 36% de VU1 e ~12% do escalonador do IOP dominam; ~340 draws/quadro com splits por hazard; sem teste de gameplay vivo.
* **(texto original abaixo)**
* **Por quê:** priorizei validar a correção do GS antes de integrar; faltam contexto EGL próprio no
  `ps2EntryRunner`, leitura do quadro para o raylib, e otimizar desenhos (~12 ms/quadro).
* **Impacto:** **alto** — é o que torna o jogo jogável.
* **Como atacar:** (1) `GpuGsExecutor` como `GSRasterBackend` do runtime; (2) thread/contexto GL dedicado;
  (3) apresentar via textura; (4) medir; (5) só então otimizar (fp64, barreiras, quads justos).

## A2. VU1 é interpretado (e VU0 em modo macro)
* **Por quê:** o foco foi o GS; no menu o GS dominava. Quando o GS cair para ~15 ms, o VU1 interpretado
  passará a ser o limite (PCSX2 mostrou 78 entradas de microVU1 em ~11 s; microVU0 nenhuma).
* **Impacto:** **alto** (próximo gargalo). Objetivo original do usuário.
* **Como atacar:** auditar os microprogramas usados, recompilar VU1 (estático/JIT) com validação contra o
  PCSX2 (microVU) por dumps de memória de VU; estudar `microVU` do PCSX2.

## A3. Metade inferior do quadro (linhas >= 214) sem medição exata
* **Por quê:** em modo progressivo a janela do PCSX2 mostra ~214 linhas na escala 345/128.
* **Impacto:** **médio** (ponto cego). Erros no rodapé/legenda podem passar.
* **Como atacar:** blit da metade inferior para a superior no dump (sprite com Y deslocado), ou outra escala
  de janela (redimensionar a janela do PCSX2 ou reduzir via `DISPLAY.DH`).

## A4. Cobertura restrita a uma cena
* **Por quê:** só há um dump (menu principal); 4 quadros do mesmo estado.
* **Impacto:** **alto** para generalização (regras de borda, pontos, linhas, mipmap, dither, STQ com perspectiva,
  texturas T4/T8 com CLUT variados, Z16, fog, FMV).
* **Como atacar:** gerar mais dumps (menu -> jogo -> luta -> FMV), repetir a bissecção com o método exato;
  executar o oráculo em lote.

## A5. Comportamento do PCSX2 em triângulos com TEX1=0
* **O que é:** em `tri_texN` o PCSX2 devolve uma rampa (filtragem linear) mesmo com MMAG=MMIN=0, enquanto
  sprites respeitam o ponto. Nós usamos amostragem por ponto.
* **Por quê:** descoberto tarde; a contaminação de instâncias (O3) atrasou a análise; falta ler o código.
* **Impacto:** **desconhecido** — as tiras do menu usam `mmag/mmin=1`; outras cenas podem usar 0.
* **Como atacar:** repetir `tri_texN` limpo (duas vezes), ler `GSVertexTrace`/`GSRendererSW` para a regra
  de "linear forçado" em triângulos (possivelmente depende de ser magnificação/escala).

## A6. Primitivas de **linha** na GPU
* **Estado:** não suportadas (`unsupported: line prims`). CPU também não validada contra o PCSX2.
* **Impacto:** **médio-desconhecido**; o menu não usa. Debug overlays/FMV podem usar.

## A7. Transferências local->host e local->local na GPU
* **Estado:** não suportadas (`unsupported: non-host->local transfers`). Também não há leitura do FIFO (path 3
  de leitura) no replay real.
* **Impacto:** **médio-alto** em jogo: cópias de buffer para buffer e *readbacks* são comuns.
* **Como atacar:** compute shader de cópia com os mesmos endereços (tabelas já existem).

## A8. Mipmaps, dither, COLCLAMP não modelados (CPU e GPU)
* **Impacto:** **baixo-médio**. GoW2 usa CT32 majoritariamente; dither só vale para 16 bits; COLCLAMP muda
  saturação. Mipmap pode aparecer em geometria 3D de cenário.
* **Como atacar:** sondas sintéticas por característica + leitura do código do PCSX2.

## A9. Pontos (`PRIM POINT`) e regra de +0,5
* **Estado:** o PCSX2 usa `v.p + 0.5` em `DrawPoint`; nós mantivemos o caminho antigo. Não validado.
* **Impacto:** *baixo* (poucos pontos no menu).

## A10. Resíduo de Z no byte baixo em primitivas reais
* **Estado:** antes das correções havia +4 no byte baixo do Z em prims reais; **não re-medido** depois de
  centro inteiro/trunc/top-left. A cadeia de blur (que reutiliza a memória do Z-buffer) ficou em 0,6, o que
  sugere que sumiu, mas não há medição direta do Z-buffer.
* **Impacto:** *médio* (afeta testes de profundidade em cenas 3D).
* **Como atacar:** reproduzir `trunc_bisect` em `p=152` na fase de cena (cortes 22/200/400/1050).

## A11. Lacunas de IOP (auditoria estática)
`secrman:6 SecrAuthCard` (provável "sem dispositivo" no cartão), `cdvdman:24/15/39`
(`sceCdReadClock`, `sceCdStop`, `sceCdBreak`), `thbase:43`, `dmacman` stubado. **Impacto:** *desconhecido*;
podem aparecer ao salvar/carregar ou em eventos de disco.

## A12. Entrada por script (`PS2_PAD_SCRIPT`)
* Commitado, nunca compilado nem testado. **Impacto:** *baixo* (conveniência de teste).

## A13. Repositório / publicação
* Rascunho de PR para `ran-j/PS2Recomp` com apenas os commits genéricos: **ainda não feito** (confirmar antes).
* Padrão do fork continua `main`; usuário pode querer `gow2` como padrão para o `recomp.board` ver `.recomp.json`.
* **Impacto:** nenhum técnico.

## A14. Licença
* O PCSX2 é GPL-3. Ler o código para entender comportamento e reimplementar é aceitável; **copiar trechos**
  para o PS2Recomp depende da licença do projeto. Mantido: leitura, sem cópia.

## A15. Peculiaridades do modelo CPU copiadas para a GPU **sem** validação contra o PCSX2
Para obter equivalência CPU==GPU, a GPU reproduz de propósito comportamentos do modelo CPU que **podem não ser do
hardware**:
* pontos nunca amostram textura;
* upload CT24 descarta um pixel que cruza a fronteira de pacote GIF (a GPU trata o fluxo como contínuo);
* CLUT T8+CT32 com CSA != 0 carrega "a partir do CSA" sem deslocar;
* sem COLCLAMP, sem dither, sem mipmap;
* regra de borda inclusiva com `eps = 1e-4` (parcialmente superada pela regra top-left, R3).
* **Impacto:** *médio-desconhecido*. **Como atacar:** uma sonda sintética por item, comparando com o PCSX2.
  O CLUT T8/T4 já foi provado 100% idêntico em dumps sintéticos (CSM1, CT32), mas não todos os ramos.

## A16. Fundo do menu ausente no jogo vivo (descoberto 2026-10-16)
* **O que é:** ao vivo (CPU **e** GPU) o menu mostra só logo e texto sobre preto; o replay do dump do PCSX2 mostra
  o retrato do Kratos e o pano vermelho. A imagem também sai pequena (512x448 sem escala) e `flips/s=0.0`.
* **Por quê não resolvido:** só descartei o GS (GPU e CPU dão o mesmo). Candidatos: lacuna do IOP (dados não
  carregados), erro no VU1, fade-in lento a 3 VSync/s. Não separados.
* **Achado (16/10, `PS2_GS_LOG`/`PS2_GS_VRAMDUMP`):** texturas e CLUTs do fundo estão na VRAM (não-zero) e os
  desenhos do fundo (tristrip, 512x512, `fbp=0x8`) chegam ao GS, mas **todos** com X/Y = 0 e Z = 0 (cor preenchida):
  10000 de 10000 sem posição. `fbp 0x8` fica todo zerado. Causa **a montante do GS**: geometria do VU1 (ou dos
  dados que ele lê) chega zerada. Próximo: isolar o programa VU1 que gera esses strips.
* **Impacto:** **alto** (a imagem do jogo vivo está errada) e é o próximo alvo.
* **Como atacar:** gravar um dump de GS gerado pelo runtime (formato lido por `gs_replay`) e achar a primeira
  diferença contra o dump do PCSX2 (falta textura, primitiva ou upload).

## A17. Escalonador do IOP percorre `std::map` a cada fatia (~12% do tempo ao vivo)
* **O que é:** `IopKernel::beginNextReady`/`nextWakeCycle` varrem `m_threads` (std::map) várias vezes por fatia;
  o `perf` mostra `_Rb_tree_increment` com 9% + 3,5% nas duas funções. Chamador do `_Rb_tree` **não confirmado**.
* **Impacto:** médio (até ~12% de velocidade). **Como atacar:** manter fila de prontas/ordenada por acordar,
  confirmar com novo `perf` (`perf_event_paranoid` precisou ser liberado pelo usuário).

## A18. Linhas e leitura local->host não suportadas na GPU; 30–78 linhas por dump de gameplay
* **Impacto:** baixo/médio (contornos ou efeitos pequenos podem faltar). Ver `unsupported lines` em `PS2_GS_GPU_STATS=1`.

## A19. VU1 nativo vs PCSX2: primeiros casos de geometria diferem (descoberto 2026-10-16, **em andamento**)
* **Método (funciona):** `games/gow2/scripts/pcsx2_vu1_inject.py` edita um savestate do PCSX2 (código/dados/registradores
  do VU1, TPC, bit de execução em VU0.VI[29], TOP/ITOP do VIF1, EE em `b .`), carrega num PCSX2 privado (sem MTVU e sem
  flag hack) e lê o `vu1Memory.bin` do novo estado. `pcsx2_vu1_batch.py` faz isso para vários vetores capturados e compara.
  Offsets derivados de `vuMicroFreeze()` (VUmicroMem.cpp) e conferidos no savestate do menu.
* **Resultados até a pausa:** caso trivial (contadores, 4 bytes): **idêntico**. Dois casos com geometria
  (`in_5658e46f..._pc0_1`, `in_b952acb6..._pc0_1`): PCSX2 alterou 38 qwords, nosso interpretador 36; **248 bytes
  (20 qwords) diferem**, e o nativo difere igual ao interpretador (o nativo reproduz o interpretador; a diferença é
  interpretador vs PCSX2).
* **Ainda não sei a causa:** pode ser diferença real de cálculo (arredondamento, flags, instrução) ou lacuna da
  injeção (registradores/flags/pipelines que não injetei, micro_*flags). Próximo: ver quais qwords/campos diferem
  (se são posições X/Y/Z, cores, ou contadores) e testar a hipótese dos flags iniciais.
* Savestates, capturas e resultados ficam fora do repositório (`~/gow2/pcsx2-ref`, `/tmp/inj`).
