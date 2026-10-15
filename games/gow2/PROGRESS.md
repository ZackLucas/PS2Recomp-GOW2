# God of War II (SCUS_974.81) — Progresso e próximos passos

Estado em 14/10/2026. Branch `gow2` do PS2Recomp: uma vertente dedicada ao GoW 2,
para não ficarmos presos só à engenharia genérica do projeto. As correções
genéricas ficam em commits separados, para virar PR no upstream (ran-j/PS2Recomp)
via cherry-pick.

> Nunca commitar o C++ gerado do jogo (`ps2xRuntime/src/runner/*`,
> `ps2xRuntime/include/ps2_recompiled_*.h`), ELF, IRX ou ISO. Tudo é
> reproduzível a partir do disco. `register_functions.cpp` fica com
> `git update-index --skip-worktree`.

## Onde estamos

- O jogo boota, lê o disco (ISO dual-layer), carrega os drivers IOP reais e
  chega ao **menu principal renderizado corretamente**. O controle funciona.
- **Desempenho: ~2 VSyncs/s no menu (~3% da velocidade real)**; o boot roda a
  15–32 VSyncs/s. Medido com `PS2_PERF_STATS=1` no build `-O2` sem logs.
- A suíte `ps2x_tests` roda **inteira: 438/438**. No `main` do upstream ela
  abortava no PS2Memory, e as suítes seguintes (incluindo a do VU1) nunca rodavam.

## O que foi feito

### Boot, disco e IOP
- Imagem de disco opcional (`argv[2]` / `PS2_CD_IMAGE`), com LBNs reais do ISO9660.
- IOP: `printf`/`sprintf` reais, arquivos do `ioman`. A arena do `sysmem` agora é
  unificada (módulos + `AllocSysMemory`, com alocação FIRST/LAST).
- Arena de sistema no EE (`0x80000`–ELF) para pacotes temporários e pilhas de
  callback. Antes eles pisavam no heap do jogo e na pilha principal, no topo da RAM.
- O COP0 Count avança com o clock do EE. Adicionada a interrupção FINISH do GS.

### Controle
- Emulação do SIO2 + protocolo DualShock 2 no ps2xIOP (`iop_sio2.*`).
- `sifman:32` `sceSifSetDmaIntr`, com o callback de conclusão em contexto de
  interrupção. A falta dele travava o DBCMAN.

### VU1 / VIF / GS (menu)
- GIFtag FLG=3 tratado como IMAGE.
- Falhas no XGKICK não abortam mais o microprograma.
- A tabela de opcodes da EFU estava deslocada. Agora: ERSQRT 0x79, ESIN 0x7C,
  EATAN 0x7D, EEXP 0x7E.
- VIF UNPACK: V2 grava xyxy, V3 preenche o W com o stream, V4-5 expande para 8 bits.
- **JR/JALR usam o valor novo do VI.** Só os branches condicionais veem o valor
  antigo (confirmado no microVU do PCSX2). Era isso que gerava os polígonos de lixo.

### Testes
- `EeScheduler::dispatchIrq` retorna cedo quando nenhum guest está vinculado. Era
  esse assert que abortava a suíte.
- Testes alinhados ao comportamento correto: EFU, arena de sistema, arena do IOP.
- Teste novo para a syscall `0x6B`.

### Desempenho e apresentação
- VU1: early-out no `commitReadyPipelines` e classificação de flags do FMAC em
  `double`, com resultado idêntico e fallback para `long double` em `FLT_MAX`.
- Removido o debug que chamava `getenv` dentro do loop por instrução do VU1.
- A imagem agora sai em 4:3 com filtro bilinear (antes era 512×448 pixelado).
- `PS2_PERF_STATS=1` imprime VSyncs/s e flips/s.

### Auditorias estáticas
A ideia é listar o que o jogo usa antes que ele quebre (detalhes no README):
- **IOP** (`scripts/irx_import_audit.py`): 11 imports faltando. Entre eles,
  `secrman:6` SecrAuthCard, que provavelmente explica o memory card sem dispositivo.
- **Syscalls do EE** (`scripts/ee_syscall_audit.py`): 50 são chamadas e todas
  estão implementadas agora (faltava a `0x6B` sceSifStopDma).

### Ferramentas (`scripts/`)
- `vudis.py`: desmonta microcódigo do VU.
- `irxdis.py`: desmonta IRX.
- `isoextract.py`: lista e extrai arquivos do ISO.
- As duas auditorias acima.
- Helpers de gdb.

## Achados importantes

- O código EE recompilado é barato. O tempo vai para as partes **interpretadas**:
  VU1 (~35%, pipeline ciclo a ciclo) e GS em software (~20%, pixel a pixel).
- `PS2X_ENABLE_RUNTIME_LOGS`, `PS2X_ENABLE_AGRESSIVE_LOGS` e
  `PS2X_ENABLE_IOP_RPC_TRACE` vêm **ligados por padrão**. O rastreador de funções
  grava e dá flush no `ps2_log.txt` a cada chamada de função do jogo
  (~97 MB por execução). Desligue os três para medir desempenho.
- Release com LTO leva mais de 25 min no link (um único `ltrans`). `-O2` sem LTO
  e sem logs compila em ~5,5 min e é o build prático.

## Oráculo PCSX2 e replay de GS (15/10/2026)

O PCSX2 v2.9.127 roda o jogo e serve de referência. Gravamos um dump de GS do
menu (`Shift+F8`) e um screenshot do mesmo frame; ficam **fora do repositório**
(`~/gow2/pcsx2-ref/`), porque contêm texturas e geometria do jogo.

Ferramentas novas:
- `scripts/gsdump.py`: lê o `.gs`/`.gs.zst`. `draws <dump> <frame>` lista as
  configurações de desenho (formato de textura, blend, CLAMP, etc.).
- `tools/gs_replay.cpp` (`-DPS2X_BUILD_GOW2_TOOLS=ON`, alvo `gow2_gs_replay`):
  restaura estado e VRAM no nosso `GS`, reproduz os pacotes GIF, cronometra cada
  frame e compara com a referência. O formato do dump foi derivado do próprio
  arquivo e validado: o stream termina exatamente no EOF, e todo pacote termina
  em fronteira de tag GIF.

Resultados (build `-O2`, sem logs, dump do menu):
- A VRAM do PCSX2 usa o mesmo layout físico do nosso GS: carregada no offset 425 do
  bloco de estado, o nosso `Present` reproduz o frame inicial com erro médio 1,4.
- O display do jogo é **512x448** (MAGH=4), não 640x448.
- **Custo do GS: ~665 ms por frame do menu** (3,7 MB de GIF; 7,7 mil triângulos desenhados a partir de ~40 mil vértices enviados),
  só o GS, sem EE, VIF nem VU1. A 60 Hz o orçamento é 16,7 ms (40x). Isso é
  quase todo o tempo de frame medido no menu (~2 VSyncs/s), então **o GS, e não o
  VU1, é o gargalo do menu**.
- **Corretude:** frame 0 (a VRAM inicial) bate com o PCSX2. Os frames 1 a 3 (o que
  o nosso GS desenhou) divergem: erro médio ~11,3 e 64% dos pixels com diferença
  maior que 8, enquanto os screenshots do PCSX2 diferem entre si em 1,8 a 3,3.
  Não é borrão nem deslocamento; o erro é localizado (região esquerda-inferior).
  Alguma(s) draw(s) específica(s) saem erradas. Próximo passo: isolar quais.
- **81% dos vértices enviados têm ADC=1**: o microcódigo do VU1 já descartou esses
  triângulos (culling/clipping) e o GS só enfileira o vértice, sem desenhar. Nosso
  `vertexKick` já respeita isso (o vértice entra na fila da tira e não dispara).
- O frame do menu usa T4/T8 com CLUT (CLD=1), TFX=3 (HIGHLIGHT2), blends variados,
  Z16/Z24 como textura/frame e um composite final que mistura 50% com o frame
  anterior em CT24.

## Próximos passos

1. **Isolar as draws que o nosso GS desenha errado** no replay do menu (ablação por
   configuração de desenho) e corrigir com teste antes. Depois, **desempenho do GS**
   medido pelo mesmo replay (665 ms/frame hoje).
2. **Desempenho**, do mais barato ao mais caro:
   1. Modo rápido do VU1, sem simulação ciclo a ciclo.
   2. Escalonador do IOP sem varrer o `std::map` de threads várias vezes por fatia.
   3. GS multithread/SIMD.
   4. **Recompilar os microprogramas do VU1 para C++**: eles são fixos no GoW 2.
   5. **GS na GPU**, no espírito do RT64.

   Os itens 4 e 5 mudam a arquitetura; vale discutir com o upstream antes.
3. **Input por script** (`PS2_PAD_SCRIPT`, ainda não testado), para testar o
   New Game sem intervenção.
4. **Fechar os buracos do IOP**:
   - SecrAuthCard (memory card);
   - sceCdReadClock;
   - sceCdStop/sceCdBreak;
   - GetSystemTimeLow;
   - dmacman (hoje é stub).
5. **Terminar as auditorias**: opcodes do VU1 (contagem em execução) e
   registradores/primitivas do GS.
6. Investigar o "989 ERROR 84" do som (SPU2 / 989snd).
7. **Draft PR no upstream** só com os commits genéricos.

## Método combinado

1. Primeiro a auditoria estática, depois rodar.
2. Teste antes da correção, com o comportamento tirado de uma referência
   (PCSX2, ps2sdk, manuais da Sony).
3. Linha de base contra o `main` limpo antes de culpar a mudança.
4. Suíte verde em todo commit.
5. Medir antes de otimizar.

## Como rodar

```bash
# configurar o build rápido (uma vez)
cmake -S . -B build-o2 -G Ninja -DCMAKE_CXX_FLAGS="-O2 -msse4.1 -DNDEBUG" \
  -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF \
  -DPS2X_ENABLE_RUNTIME_LOGS=OFF -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF -DPS2X_ENABLE_IOP_RPC_TRACE=OFF
cmake --build build-o2 --target ps2EntryRunner

# rodar
PS2_HIDE_DEBUG_UI=1 PS2_PERF_STATS=1 build-o2/ps2xRuntime/ps2EntryRunner SCUS_974.81 "God of War II (USA).iso"
```

Testes: configure um build com `-DPS2X_BUILD_TEST=ON -DPS2X_BUILD_RECOMP=ON
-DPS2X_BUILD_ANALYZER=ON` e rode `ps2xTest/ps2x_tests`.

## Benchmark de GPU (15/10/2026)

`tools/gpu_bench.cpp` (`gow2_gpu_bench`, EGL headless + OpenGL 4.5 core) mede o que
um GS em GPU custaria nesta máquina (RTX 5060 Ti, PCIe Gen4 x8). A parte B desenha
os triângulos reais de um frame do dump (`gsdump.py tris <dump> <frame> <saida>`),
com as mesmas 58 trocas de estado, textura (conteúdo aleatório), blend e depth.

- Dados por frame: 0,83 MB de vértices; 1,27 MB de uploads de imagem do jogo (no
  formato do GS). No pior caso, re-subir as 31 MB de texturas decodificadas custa
  ~5 ms.
- PCIe: 8 a 15 GB/s em transferências de MB; leitura sincronizada de um frame de
  0,9 MB custa ~0,1 ms. **A banda não é o gargalo; latência e pontos de sincronização
  é que importam.**
- Desenho: 2,37 M fragmentos por frame (10x a tela) em ~0,05 ms. Vértices + desenho
  ~0,14 ms por frame, contra ~665 ms do GS em CPU.

Limites deste número: o shader é simplificado (sem CLUT nem decodificação de
swizzle do GS, blend por função fixa) e as texturas são ruído. A decodificação das
texturas a partir da VRAM do PS2 **não** foi medida e é onde o custo real vai estar.

## GS em GPU, nível 1: VRAM como buffer (15/10/2026)

Decisão: o GS vai para a GPU com a **VRAM do PS2 inteira como um buffer** (4 MiB de
`uint`). Os uploads de imagem do jogo vão crus para a GPU (um compute shader grava no
layout embaralhado), e o shader de fragmento amostra direto desse buffer, com a conta
de endereço e a paleta feitas ali. A CPU não decodifica textura. Frame buffer, Z e
textura são o mesmo buffer, então o aliasing do GS deixa de ser problema.

Feito e testado:
- `shaders/gs_vram.glsl`: endereçamento de todos os 13 formatos (CT32/24/16/16S, T8,
  T4, T8H, T4HL, T4HH, Z32/24/16/16S). As tabelas (`gs_vram_tables.glsl`) são
  **geradas** de `ps2_gs_memory.cpp` por `scripts/gen_gs_tables_glsl.py`.
- `gow2_gs_addr_test`: compara a leitura em GLSL com `GSMem::Read*` em 13,6 milhões
  de leituras aleatórias (bp, bw ímpar incluso, x e y até 2047, endereços que dão a
  volta). Resultado: 0 divergências. O teste foi validado quebrando uma tabela de
  propósito (1,04 milhão de falhas).

- `gsWriteRaw` e `shaders/gs_upload.glsl`: escrita de pixel nos 13 formatos (atomics
  só na faixa de bits do próprio pixel) e upload host->local em compute shader.
- `gow2_gs_upload_test`: transferências aleatórias (13 formatos, 60 por formato)
  contra `GSCpuBackend::UploadImage`, comparando a VRAM inteira. 0 divergências;
  validado quebrando a máscara do CT24 de propósito (266 mil palavras erradas).

Observação: o upload em CPU descarta bytes de um pixel CT24 que cai na fronteira de
um pacote GIF (`return` quando sobram menos de 3 bytes). A versão GPU trata a
transferência como um fluxo contínuo, que é o comportamento do hardware.

Nota técnica: as tabelas de endereçamento (~1 200 entradas) ficam num buffer
somente-leitura (binding 15, `gs_tables_gl.h`), não em `const` do shader: o compilador
da NVIDIA as copia para memória local a cada chamada inlinada e estourava os
registradores assim que havia mais de uma chamada por pixel.

Próximos passos, em ordem (cada um com teste contra a implementação em C++):
1. (feito) escrita de pixel e upload de imagem.
2. (feito) carga de CLUT (CSM1/CSM2, CPSM CT32/CT24/CT16/CT16S, CSA, CLD 0 a 7) e
   consulta de paleta com TEXA (`shaders/gs_clut_load.glsl`, `gs_clut.glsl`).
   `gow2_gs_clut_test`: 6000 iterações, 3732 cargas, 9,2 milhões de consultas contra
   `GSCpuBackend::LoadClut`/`LookupCLUT`, 0 divergências; validado quebrando o alfa
   do TEXA e o swizzle CSM1 de propósito. A lógica de CLD (carregar ou não) fica na
   CPU: é controle de fluxo. A versão GPU reproduz as peculiaridades do modelo em
   CPU (ex.: T8 com CT32 e CSA != 0 carrega a partir de CSA sem deslocar); se o
   PCSX2 divergir ali, corrigimos os dois lados com teste.
3a. (feito) pipeline de pixel isolado: `shaders/gs_pixel.glsl` (`gsWritePixel`):
   scissor, fog, teste de alfa e AFAIL, DATE/DATM, teste de Z (ZTST), blend a/b/c/d
   com FIX, PABE, FBA, FBMSK, escritas de FB e Z nos 8 formatos de frame. Espelha
   o `WritePixel` do CPU, inclusive as simplificações (sem COLCLAMP nem dither).
   `gow2_gs_pixel_test`: 72 000 escritas aleatórias em sequência contra
   `GSCpuBackend::Submit(POINT)`, VRAM inteira comparada, 0 divergências; 5 de 6
   mutações detectadas (a 6a é equivalente: o fog nunca passa de 254).
3b. (feito) rasterização, interpolação, amostragem de textura e ordenação por pixel:
   `shaders/gs_draw.{vert,frag}`, `gs_draw_common.glsl`, `gs_texture.glsl`,
   `gs_prim_pack.h`. Cada primitiva é uma instância que cobre a bounding box; o
   fragment shader decide a cobertura com a mesma fórmula do CPU (centro do pixel,
   bordas inclusivas), interpola cor/Z/UV/STQ, amostra (ponto e bilinear, 4 modos de
   wrap, 13 formatos, CLUT, TEXA, TFX 0 a 3) e chama `gsWritePixel` dentro de uma
   seção crítica ordenada por pixel (`ARB_fragment_shader_interlock`).
   `gow2_gs_draw_test`: 80 000 primitivas aleatórias (triângulo, strip, fan, sprite,
   ponto) com estado completo aleatório, em sementes novas, contra
   `GSCpuBackend::Submit`, VRAM inteira comparada: **0 divergências**. 7 de 7
   mutações detectadas (epsilon de borda, arredondamento do Z, UV de sprite, winding,
   flat shading, wrap, offset do bilinear).
   Lições registradas:
   - **Divisão float em GLSL não é IEEE** (difere em 14,6% das amostras na NVIDIA).
     Para bater bit a bit com o CPU, `gsDiv` calcula em `double` e arredonda uma vez
     (exato: 53 >= 2*24+2). Com o Z em ~10^7, 1 ulp nos pesos já vira off-by-one.
   - **`precise` e `optionNV(fastmath off)`** são necessários para evitar FMA.
   - **A seção crítica por pixel só ordena fragmentos na mesma posição de tela.**
     Quando FBP/FBW/PSM/ZBP/ZPSM mudam, endereços iguais da VRAM passam a vir de
     posições diferentes; é preciso **dividir o draw e colocar uma barreira**. O
     teste faz isso; o renderizador real também tem que fazer.
   - O CPU não amostra textura em pontos; a GPU copia isso (divergência conhecida
     do hardware, que amostraria).
   Fora do escopo ainda: linhas (Bresenham do CPU), mipmap, dither, COLCLAMP (o CPU
   também não aplica), regras de borda do GS real (aqui usamos as do modelo em CPU). (`ARB_fragment_shader_interlock`): função
   de textura, filtro, teste de alfa, teste de profundidade, blend a/b/c/d, máscaras.
4. Replay do frame do menu e comparação com a VRAM do GS em CPU e com o PCSX2.

### Etapa 4: frame real do menu, GPU contra CPU contra PCSX2 (15/10/2026)

`tools/gs_gpu_exec.h` (executor) + `tools/gs_gpu_replay.cpp` (`gow2_gs_gpu_replay`): o
frontend real do GS interpreta o dump e chama um backend "tee" que repassa cada comando
ao rasterizador em CPU e ao executor em GPU. Compara a VRAM por frame e as imagens.

Resultado (4 frames do menu, ~7,5 mil primitivas por frame):
- **VRAM:** idêntica ao CPU em todas as palavras, **menos 10 por frame**, sempre as
  mesmas. Causa investigada e explicada: o sprite `prim#7206` renderiza em `p=120` (o
  Z-buffer reaproveitado) e lê como textura o frame principal com `v` até 449, uma
  linha além do frame (448). Essa linha cai nas páginas do próprio destino. É
  realimentação dentro da primitiva: o CPU lê o que acabou de escrever, a GPU lê o
  conteúdo anterior ao draw (o que um renderizador de hardware faz). Em hardware real
  depende do cache de textura. Hipóteses descartadas por teste: corrida de ordem
  (serializar não mudou) e cache de páginas do CPU (invalidar a cada primitiva não mudou).
- **Imagem apresentada:** GPU e CPU idênticas (diferença média 0,00; 0% de pixels > 8),
  nos 4 frames.
- **Contra o PCSX2:** GPU e CPU têm exatamente o mesmo erro (1,42 no frame 0 e ~11,3 nos
  demais). Ou seja, o modelo compartilhado diverge do hardware; isso não é defeito da GPU.
  Próximo passo: achar qual regra do GS real ambos simplificam.
- **Tempo, caminho realista (frontend + GPU, sem o rasterizador CPU), por frame:**
  frontend 1,1 ms; total ~14,6 ms. GPU por categoria: draws ~12 ms, uploads 0,12 ms,
  CLUT 0,23 ms. O rasterizador CPU leva ~650 ms: ~45x mais lento.
  Observação de método: com o CPU rodando junto, a GPU parecia custar ~5 ms porque o
  trabalho do CPU escondia a latência; o número realista é o de `GT_MODE=gpuonly`.
- Execução em lotes: 980 draws em 4 frames; 76 divisões por mudança de alvo e 756 por
  render-to-texture. A regra de hazard é conservadora (usa o tamanho do scissor), então
  esses números são um teto.

Onde está o custo de 12 ms (hipóteses a medir, não medidas): `double` na interpolação
de Z e em `gsDiv` (fp64 é 1/64 da taxa nas placas de consumo), quads de bounding box
em vez de triângulos justos, leitura de textura via SSBO com conta de swizzle por
amostra, e o número de draws/barreiras. Nenhuma foi otimizada ainda; o objetivo desta
etapa era fidelidade, e a fidelidade ao CPU está provada.

Variáveis de ambiente do replay: `GT_MODE=both|gpuonly|nullonly`, `GT_SERIALIZE`,
`GT_PROFILE`, `GT_WATCH_WORD=<palavra>`, `GT_ISOLATE=<primitiva>`,
`GT_CPU_FLUSH_EACH`.

## Oráculo PCSX2 automatizado e bisseção (15/10/2026)

Objetivo: descobrir por que o nosso GS (CPU e GPU, idênticos entre si) diverge do PCSX2
no menu (erro ~12 contra a referência). Estado: **causa ainda não isolada**, método pronto.

### Como rodar o oráculo (PCSX2 de verdade, sem intervenção)
- `scripts/pcsx2_oracle.sh dump.gs out.png`: sobe um `Xwayland :7` isolado, roda o
  dump no PCSX2 com renderizador de software e **captura a janela** (não o F8).
  Usa uma pasta de dados própria (`/tmp/.../pcsx2-data`), com `SetupWizardIncomplete=false`,
  `Renderer=13` e BIOS por caminho absoluto; não toca em `~/.config/PCSX2`.
- `scripts/x11_nested.py list|key`: lista janelas / injeta teclas no X isolado.
- `scripts/gsdump_trunc.py`: trunca um dump depois do pacote N e acrescenta um sprite
  que copia um buffer (`--blit FBP,FBW,PSM --blit-wh W,H`) para o buffer de exibição.
  Força `PRMODECONT=1` (o fluxo do jogo deixa 0, o que faz o GS ignorar TME/ABE/FST do PRIM).
- `scripts/trunc_bisect.py K...`: mesmo dump truncado nos dois lados (PCSX2 e o nosso
  replay), imprime o erro por corte. `scripts/vram_bisect.py a-b ...`: zera um intervalo
  de páginas (8 KiB) da VRAM inicial nos dois lados e compara.
- `scripts/gsdump_synth.py`: dumps sintéticos (T8/T4 lidos como CT32, CT32 lido como T8,
  mapa de CLUT) para comparar uma feature por vez. Comparação exata por pixel funciona.
- Replay: `GT_ZERO_PAGES=a-b`, `GT_PRIM_PACKET=n,..` (primitiva -> pacote),
  `GT_LIST_PACKET=k` (lista as primitivas de um pacote), `GT_SNAP_PACKET`, `GT_WATCH_WORD`,
  `GT_ISOLATE`, `GT_TEXRANGE=a-b`, `GT_STATS`, `GT_MODE=both|gpuonly|nullonly`.

### Armadilhas descobertas (não repetir)
- **O F8 do PCSX2 não devolve o frame exibido durante replay de GS dump.** Ele devolveu
  sempre o menu, ignorando VRAM, pacotes e DISPFB. A janela é verdadeira; capture a janela.
- O PCSX2 **ignora** `DISPFB` alterado nos registradores do dump; para ver um buffer
  intermediário, copie-o para o buffer de exibição original com um sprite (blit).
- A janela do PCSX2 mostra o frame com **escala vertical ~12/7** e o OSD
  `Dump Frame/Packet Number` no canto; para comparação exata use escala inteira
  (`IntegerScaling=true`, `linear_present_mode=0`) e derive o mapa de linhas de um controle.
- **Mate o PCSX2 por PID antes de cada captura**; uma instância antiga ainda exibindo
  outro dump contaminou várias medições (resultados "0,3% idêntico" eram falsos).
- Nunca `pkill -f`/`pgrep -f` com `kill`: o padrão casa com o próprio shell.
- O dump é **uma sequência de ~5 mil pacotes** e o PCSX2 o repete em laço; o frame muda
  de acordo com o instante da captura se o dump não terminar em estado estável.

### O que está comprovado
- Layout de **T4 e T8** e **carga de CLUT** (8 e 4 bits, CSM1, CT32): idênticos ao PCSX2,
  100% por pixel nos dumps sintéticos.
- Sem VRAM residente (VRAM inicial zerada) nós e o PCSX2 concordamos (erro ~4, ruído ~2);
  **com** VRAM real divergimos (~12). Só um intervalo de páginas explica tudo: zerar as
  páginas **400-416** derruba o erro para 0,32 (texturas residentes T4 `tbp=12864` e T8
  `tbp=12928`, CLUT residente em 432-448).
- Com o mesmo dump truncado dos dois lados: erro 1 a 3 (ruído) **até o pacote 1212**;
  **21 no pacote 1216**. O pacote 1216 emite as primitivas #7174-#7205: o **composite
  aditivo do bloom** (32 triângulos, `alpha=0x48` = `Cs*As + Cd`, `FBMSK=ff000000`,
  `test=31001` NEVER/FB_ONLY, textura CT24 256x256 em `tbp=4864` = buffer `p=152`).

### Situação (atualizada)
A hipótese do composite do bloom estava errada: `p=152` está **dentro do Z-buffer** (`zbp=120`, Z24 ocupa as
páginas 120-232) e o erro nasce nos pacotes 21-22 (controle: 0,14 com 0 pacotes). A causa é a regra de centro
do pixel e o truncamento do Z, descritos abaixo. Experimento descartado: FBMSK no domínio de 16 bits.

## Regra do GS encontrada contra o PCSX2 (2026-10-15)

Duas regras do rasterizador de triângulos diferiam do PCSX2 (medido com dumps sintéticos, sonda de isolinha
com teste de Z GEQUAL, escala 2x exata da janela):

1. **Centro do pixel em coordenadas inteiras**: cobertura e interpolação avaliam em (x, y), não (x+0.5, y+0.5).
   O PCSX2 começa o triângulo uma coluna depois e posiciona a isolinha de Z um pixel antes. Confirmado em X e Y.
2. **Z é truncado**, não arredondado (`z + 0.5`): diferença exata de +1 em 432/432 amostras de um plano em X.

Com as duas, um plano com gradiente nos dois eixos fica idêntico ao PCSX2 em 957/1069 amostras (±1 no resto, aritmética
de ponto fixo). No quadro real, o erro do buffer p=152 (Z-buffer, zbp=120) após o pacote 22 caiu de 6.9 para 3.8.

**Aplicado** em `gs_cpu_backend.cpp` e `gs_draw.frag`. Três testes unitários codificavam a convenção antiga e
foram revistos: STQ (S/Q) e filtro linear STQ agora sondam o pixel que corresponde ao ponto de amostra inteiro
(valores recalculados à mão), e o teste do "fan quad sem buracos" lia a VRAM CT32 de forma linear (bug do teste,
passava por acaso com a área toda preenchida): agora lê por `gs.ReadVram`. ps2x_tests 438/438 e gow2_gs_*_test
PASS; GPU==CPU no menu continua com as mesmas 10 words explicadas.
Ferramentas novas: `GT_LIST_XFER`, `GT_VRAM_PACKETS`, scissor/offset em `GT_LIST_PACKET`; experimentos
`z_tri`, `z_flat`, `z_iso<k>`, `z_isoy<k>`, `z_plane2_<gx>_<gy>`, `z_planex<g>`, `z24_upload` em gsdump_synth.py.
Armadilha: TBP0 da textura é em blocos (página = 32 blocos); ZBP/FBP são em páginas.

3. **Sprites: UV sem o +0,5** (mesma regra do centro inteiro; sonda `spr_up_nearest/linear`, ampliação 16→32:
   o PCSX2 dá R=8·(x−9) no linear, nós 8·(x−8,5); G 88 vs 92). Aplicado em CPU e shader.

4. **Regra de borda top-left** nos triângulos (sonda `tri_edge`, retângulo de 2 triângulos opacos): o PCSX2 cobre
   20..39 em X e Y (borda direita/inferior excluída) e a diagonal pertence ao triângulo de baixo; nós cobríamos
   20..40 e deixávamos a diagonal para ambos (com blend aditivo gerava listras claras). Aplicado em CPU e shader.

Medido contra o PCSX2 (erro médio por canal, mesmo dump truncado dos dois lados):

| corte | antes | depois das 4 regras |
|---|---|---|
| quadro inteiro, pacote 1212 | 1,9 | 1,9 |
| quadro inteiro, pacote 1216 (composite) | 21,3 | 7,8 |
| quadro inteiro, fim (1321) | 11,6 | 5,05 |
| `p=152` (blur/Z), pacote 1212 | 21,7 | 7,5 |

### Próximos passos (em ordem)
1. Resíduo: o erro do blur cresce devagar de 5 (pacote 400) a 11 (1050) e o fim do quadro ainda está em 5,5
   (piso do método ~2). Suspeitos: ponto fixo do plano de Z, borda top-left com amostras inteiras,
   regra de cobertura de sprites, arredondamento de UV/cores.
2. Só então otimizar os desenhos da GPU (~12 ms/quadro) e ligar o `GpuGsExecutor` ao `ps2EntryRunner`.

## Medição exata contra o PCSX2 (2026-10-15, noite)

**Método** (`scripts/trunc_exact.py`, sondas em `gsdump_synth.py`): dump truncado, varredura progressiva forçada,
PCSX2 com `IntegerScaling=true`, `linear_present_mode=0`, `AspectRatio=Stretch`; escala horizontal 2x exata e vertical
345/128, origem (14,55); cada pixel de origem é lido na sua posição de tela (nearest). Só as ~214 primeiras linhas
cabem na janela e as 16 primeiras são ignoradas (texto "Dump Frame" do PCSX2). Controle (corte 0): 95-98% idênticos.
**Armadilha grave:** instâncias antigas do PCSX2 se acumulam (20+ chegaram a ficar vivas) e a captura mostra o
quadro de um dump anterior; o `kill()` agora casa pela linha de comando (`pcsx2-v2.9`) com SIGKILL e espera sumir.
Capturas feitas durante o acúmulo foram descartadas. O valor de `filter` (2 ou 3) não altera nada.

Sondas sintéticas (nós vs PCSX2, pixel a pixel): blend `Cs*As+Cd*(1-As)` 100%, modulate e highlight 100%,
sprite ampliado linear e nearest 100%, `tri_edge` 100%, triângulo texturizado linear 99,9%, Gouraud 96,6%
(±1 em 3%: aritmética de ponto fixo).

Quadro do menu (região 512x214, erro médio abs por canal e % de pixels com erro >8):

| corte | erro | >8 |
|---|---|---|
| 1212 cena antes do composite | 0,75 | 0,02% |
| 1216 depois do composite | 7,0 | 60% |
| 1321 fim do quadro | 3,5 | 0,6% |

Cadeia do bloom (erro por etapa, exato): `p=152` 1186 **0,6**; `p=120` 1188 **0,9**; 1190 1,6; 1192 2,2;
1194 2,8; 1208 **6,8** (2,4% >8); 1210 7,0; 1212 7,8 (37% >8). O erro cresce passada a passada; salto grande entre
1194 e 1208. O composite (1216) herda isso.

### Próximos passos
1. Listar com `GT_LIST_PACKET=1195-1208` os sprites entre 1194 e 1208 e sondar o que eles têm de diferente
   (blend `alpha=64` = (Cs-Cd)*FIX>>7+Cd, `FBMSK=ff000000`, teste `31001` NEVER/FB_ONLY, leitura e escrita no mesmo
   buffer p=152 no 1212: auto-realimentação, ordem de leitura).
2. Investigar por que o PCSX2 filtra linearmente triângulos com TEX1=0 (sondas `tri_texN`), mas não sprites.
3. Depois: otimizar a GPU e ligar o `GpuGsExecutor` ao runtime.
