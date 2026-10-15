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
