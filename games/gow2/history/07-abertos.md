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

## A19. VU1 nativo/interpretador vs PCSX2 (2026-10-16) — **resolvido para entradas sãs**
* **Método:** `pcsx2_vu1_inject.py` edita um savestate do PCSX2 (código/dados/registradores do VU1, TPC, bit de execução em
  VU0.VI[29], TOP/ITOP do VIF1, EE em `b .`), carrega num PCSX2 privado (sem MTVU e sem flag hack) e lê o `vu1Memory.bin`
  do novo estado; `pcsx2_vu1_batch.py` repete para vários vetores; `vu1_ss_inputs.py` monta entradas a partir do próprio
  estado do VU1 salvo pelo PCSX2. Offsets derivados de `vuMicroFreeze()` (VUmicroMem.cpp).
* **Resultado:** com entradas sãs (estado do próprio PCSX2, até 103 qwords alterados e pacote de 3,8 KB) **6 de 6 idênticos
  byte a byte ao PCSX2**, tanto o interpretador quanto o **nativo** (0 bytes de diferença).
* **As diferenças iniciais** (248 bytes em 2 casos capturados do nosso runtime) vinham de entradas com registradores
  saturados (±FLT_MAX), onde o resultado depende de como cada implementação trata overflow em cadeia. Não é prova de erro.

## A20. Registradores do VU1 no nosso runtime chegam saturados (descoberto 2026-10-16) — **pista para A16**
* **O que é:** em **844 de 845** chamadas capturadas do nosso runtime, o VU1 começa com >=3 registradores VF absurdos
  (inf/NaN ou |x|>1e15, tipicamente ±FLT_MAX). No estado real do PCSX2 nenhum passa de ~1e12.
* **Por que importa:** geometria calculada com matrizes/vértices saturados cai fora da tela, o que bate com os
  desenhos do fundo do menu chegando ao GS com X/Y = 0 (A16). O VU1 em si reproduz o PCSX2; o lixo vem de **antes**
  (dados que chegam por VIF/EE, matrizes calculadas pelo EE, ou carregamento do IOP).
* **Como atacar:** achar o primeiro registrador/dado saturado na cadeia (qual programa VU1 o escreve, de qual memória
  vem) e comparar com o equivalente no PCSX2 (mesmo ponto do menu).

## A21. MAC final do VU1: nativo e interpretador divergem em ~1,2% das chamadas ao vivo (2026-10-16) — **baixa prioridade**
* **O que é:** `PS2_VU1_NATIVE_VERIFY=1` (nativo contra interpretador, por chamada, no jogo): 98,8% idênticas; memória
  e pacotes 100%. A diferença está **só no registrador MAC** ao fim do programa (status, clip, Q, P, ACC, VI e VF
  iguais). Offline (`gow2_vu1_prog_test`, `VU1_TAIL=1`) reproduz em 84 de 845 capturas (10%); o teste antigo nem
  comparava MAC/status/clip/Q/P.
* **Efeito:** nenhum na saída (só importa se o próximo programa ler o MAC antes de escrevê-lo).
* **Não se resolve com o PCSX2:** o savestate dele devolve MAC=0 e status=cc0 (nem o nativo nem o interpretador:
  fc1), ou seja, não guarda esses flags no mesmo formato. Sem referência, sem boa hipótese de qual lado está certo.
* **Pista:** o rastreio por par mostra o MAC visível divergindo cedo porque o nativo confirma flags de forma preguiçosa
  (só quando lidos); a diferença final (nativo 0004 = lane do último op, interpretador 0008 = op anterior) sugere ordem
  de confirmação no fim do programa, mas não foi provado.

## A22. `989 ERROR 84 0 0 0 0` no log (driver de som da IOP, 989snd)
* Aparece com e sem GPU/VU1 nativo (4 a 6 vezes por execução). É o módulo de som do jogo reclamando de um comando;
  provável SPU2/RPC de som não emulado. Não afeta imagem. Sem hipótese ainda.

### A20, evidência nova (2026-10-16): NaN padrão do x86 na memória do VU1 — pista forte para o EE
* A memória de dados do VU1 nas 845 capturas do nosso runtime tem **107.319 palavras `0xFFC00000`** (o NaN "real
  indefinite" que o x86 gera em operações inválidas: 0*inf, inf-inf, 0/0), ~127 por chamada. No savestate do menu do
  PCSX2: **0** na memória do VU1 e 8 em 32 MB de RAM do EE.
* O VU1 não gera NaN na própria aritmética (operandos passam por `opnd`, resultados saturam), então esses NaN entram
  como bits crus: vêm do EE/DMA. O EE do PS2 não tem NaN/Inf (saturação em ±Fmax, flush de denormais, arredondamento
  para zero), mas as macros do FPU do EE no runtime são IEEE puro: `FPU_ADD_S/SUB_S/MUL_S/DIV_S/SQRT_S` em
  `ps2xRuntime/include/ps2_runtime_macros.h` (~l.610). **Hipótese principal para o fundo preto**, ainda não provada.
* Falta provar: contar `0xFFC00000` na RAM do EE do nosso runtime e achar quem escreve o primeiro.

### A20 RESOLVIDO (2026-10-16): FPU do EE seguia IEEE; agora segue as regras do PS2
* **Origem achada** com a instrumentação `ps2x_fp_report` (primeiras operações que criam NaN/Inf a partir de entradas finitas):
  `SQRT.S` de números **negativos** (ex.: -0,0187) dava NaN `0xFFC00000` nas funções do jogo `0x18B2D0` e `0x146818`
  (no PS2 usa o valor absoluto), e um `VMUL` do VU0 em modo macro (`0x18AEA0`) estourava para +Inf (no PS2 satura em Fmax).
  Esses NaN/Inf entravam nas matrizes enviadas ao VU1 e saturavam os VF.
* **Peça:** `ps2xRuntime/include/ps2_ee_fp.h` (operandos: denormal -> 0, Inf/NaN -> ±Fmax; resultados: Inf -> ±Fmax,
  denormal -> 0; SQRT usa |x|; DIV por zero -> ±Fmax) usada pelos macros `FPU_*` e `PS2_V*`. Teste
  `games/gow2/tools/ee_fp_test.cpp` (30 casos, regras conferidas com o comportamento do FPU.cpp do PCSX2, lido e reimplementado).
* **Resultado medido (mesmo cenário, menu, ~50 s):** palavras `0xFFC00000` na memória do VU1: 107.319 (127/chamada) -> **0**;
  chamadas com >=3 VF saturados: 800 de 845 (95%) -> 5 de 46 (11%). O fundo deixou de ser preto (agora há textura
  vermelha/laranja), mas ainda não parece o rosto do Kratos do PCSX2.
* **Não feito (refinamento):** flags do FPU (FCR31) e MAC/status do VU0, arredondamento para zero do EE, `CVT.W.S` com
  saturação inteira.
