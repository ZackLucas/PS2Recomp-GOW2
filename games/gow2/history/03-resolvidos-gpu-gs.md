# Resolvidos: implementação do GS na GPU (VRAM como buffer)

Desenho "nível 1": 4 MiB de VRAM num SSBO; upload GIF cru + swizzle em compute; sombreador de fragmento amostra
direto do buffer, com tabelas de endereçamento em SSBO; leitura-modificação-escrita por pixel ordenada por
`ARB_fragment_shader_interlock`. Código em `games/gow2/tools/`.

| # | Problema | Causa | Solução | Estado |
|---|---|---|---|---|
| G1 | Z fora por 1 em 14,6% das amostras | divisão GLSL **não é IEEE** na NVIDIA | `gsDiv = float(double(a)/double(b))` (exato: 53 ≥ 2·24+2) | resolvido |
| G2 | FMA alterava arredondamento | contração do compilador | `precise` + `#pragma optionNV(fastmath off)`; `precise` também em `is/it/iq`, `texUf`, `su`, `fx` | resolvido |
| G3 | Erro C5041 do compilador NVIDIA | tabelas `const` em vários pontos de chamada | tabelas em SSBO só leitura (binding 15), geradas por `gen_gs_tables_glsl.py` | resolvido |
| G4 | Corrida entre prims com ZBP diferente | interlock só ordena o mesmo pixel de tela | dividir o desenho ao mudar FBP/FBW/FPSM/ZBP/ZPSM + `glMemoryBarrier` | resolvido (conservador: 756 divisões em 4 quadros) |
| G5 | Render-to-texture, CLUT e transferências | dependências de dados entre prims | dividir também nesses pontos | resolvido |
| G6 | `gsWriteRaw` quebrou o teste de endereçamento | buffer `readonly` | removido o `readonly` | resolvido |
| G7 | Quadro apresentado parecia repetido 4x | faltou o *repack* do stride de 640 pixels | repack na apresentação | resolvido |
| G8 | Bench medindo 0 fragmentos | contador lido antes do primeiro upload | reordenado + leitura de sanidade | resolvido |
| G9 | "5 ms de GPU" | latência escondida pelo rasterizador CPU | medido com `GT_MODE=gpuonly`: **14,6 ms** (~12 ms em desenhos) | corrigido (ver 05) |
| G10 | Lerp truncado divergia CPU×GPU em 1/16000 triângulos | interpolação STQ sem `precise` | `precise` nas variáveis acima | resolvido (16 000 prims: 0) |

## Verificações que sustentam a GPU
`gow2_gs_addr_test`, `gow2_gs_upload_test`, `gow2_gs_clut_test`, `gow2_gs_pixel_test` (72 000 escritas),
`gow2_gs_draw_test` (80 000 prims, 7/7 mutações detectadas). Cada componente tem teste aleatório contra o modelo
em C++ **e** teste de mutação que prova que o teste consegue falhar.

## Números de desempenho (RTX 5060 Ti, PCIe Gen4 x8, -O2)
CPU ~650 ms/quadro; GPU ~14,6 ms (frontend 1,1 + GPU ~13,5: desenhos ~12, uploads 0,12, CLUT 0,23).
PCIe não é gargalo: ~2,1 MB/quadro; leitura síncrona de 0,9 MB = 0,1 ms.
81% dos 40 mil vértices/quadro vêm com ADC=1 (VU1 já cortou): só ~7,7 mil triângulos desenhados.
Suspeitas **não medidas** para os 12 ms: Z/gsDiv em fp64, quads de bounding box em vez de triângulos justos,
leituras de swizzle por amostra, número de barreiras.
