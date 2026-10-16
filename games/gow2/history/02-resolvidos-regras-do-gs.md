# Resolvidos: regras do GS que divergiam do PCSX2

Contexto: o modelo CPU (`ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp`) e o sombreador (`gs_draw.frag`) foram escritos
a partir da documentação/memória, sem checagem contra o hardware. O manual do GS define **o que** é feito, mas não a
**aritmética exata** (arredondamentos, regras de borda, precisão). Essas regras vieram de engenharia reversa do
PCSX2 e foram recuperadas aqui por **sondas sintéticas** (dumps de uma característica por vez) e pela leitura do
rasterizador de sprites do PCSX2 (`GSRasterizer::DrawSprite`).

Método de prova: comparar, pixel a pixel, o nosso resultado com a janela do PCSX2 (escala inteira exata). Ver 04.

---
## R1. Centro do pixel em coordenadas inteiras (triângulos)
* **Sintoma:** erro do quadro ~12 contra o PCSX2; Z-buffer com viés constante de ~+11 no byte baixo.
* **Causa:** cobertura e interpolação avaliadas em `(x+0,5, y+0,5)`; o PCSX2 avalia em `(x, y)`.
* **Prova:** isolinha de Z (plano + sprite branco com teste GEQUAL) deslocada exatamente 1 pixel em X e Y; o
  triângulo do PCSX2 começa 1 coluna depois do nosso para um vértice em 8,3125.
* **Correção:** `px = x`, `py = y` em `DrawTriangle` e em `gs_draw.frag`. Commit `f320836`.
* **Estado:** resolvido para triângulos. **Pontos** (`DrawPoint`) não foram revalidados (o PCSX2 usa `+0,5` ali).

## R2. Z truncado, não arredondado
* **Causa:** `z + 0.5` (arredondava). PCSX2 trunca.
* **Prova:** diferença exata de +1 em 432/432 amostras de um plano com fração constante 0,8125.
* **Correção:** `static_cast<u32>(z)` / `uint(zd)`. Commit `f320836`. **Resolvido.**

## R3. Regra de borda top-left (triângulos)
* **Sintoma:** listras verticais claras no quadro (blend aditivo desenhando 2x pixels em arestas compartilhadas);
  retângulo de 2 triângulos cobria 20..40 em vez de 20..39.
* **Causa:** borda inclusiva (`eps`) para qualquer aresta; com amostras inteiras, pixels exatamente sobre a
  aresta eram desenhados pelos dois triângulos e pelos limites direito/inferior.
* **Prova:** sonda `tri_edge` (duas cores opacas): PCSX2 cobre 20..39; a diagonal pertence ao triângulo de baixo.
* **Correção:** pixel sobre a aresta só entra se a aresta é esquerda (normal para +x) ou superior (horizontal,
  normal para +y). Commit `8a2b83a`. **Resolvido** (100% idêntico na sonda; 16 000 prims CPU==GPU).

## R4. UV do sprite sem o +0,5
* **Sintoma:** ampliação linear de sprite com R=8·(x−8,5) contra 8·(x−9) no PCSX2.
* **Causa:** `(x - x0 + 0.5)/largura`. O PCSX2 não soma o meio pixel.
* **Correção:** commit `26b832e`; depois superada por R6/R7.

## R5. Bilinear com pesos de 4 bits e truncamento em cada etapa
* **Sintoma:** cadeia de blur do bloom com erro crescendo a cada passada ping-pong (0,6 -> 7,8).
* **Causa:** interpolação em float com `lround`. O PCSX2 usa pesos de 4 bits (1/16) e trunca (`>>4`) em cada
  etapa, horizontal primeiro: `top = c00 + ((c10-c00)*wx >> 4)`, `bottom` idem, `out = top + ((bottom-top)*wy >> 4)`.
* **Prova:** testadas 6 variantes contra o PCSX2; a variante "horizontal primeiro, truncando" foi a melhor
  (90,6% / 95,5% idênticos nos sprites 3x e 2,5x contra 34,6% / 55,3% do float).
* **Correção:** `lerpChannel` (CPU) e `gsLerpChannel` (GLSL). Commit `8de9dfd`. **Resolvido na prática** (ver
  06: ainda há ±1 em parte dos pixels).

## R6. Cobertura do sprite: `ceil(p0)..ceil(p1)` exclusivo, cantos ordenados com o UV
* **Causa:** posições truncadas com `int()`; `max(1, largura)` fazia sprites de tamanho zero desenharem 1 pixel;
  os cantos eram trocados sem trocar o UV.
* **Fonte:** `GSRasterizer::DrawSprite` do PCSX2 (`r = ceil(p0)..ceil(p1)`, interseção com scissor).
* **Prova:** sonda `spr_zero`: sprite com vértices iguais não desenha (ambos), (40..41) desenha 1 pixel,
  (60,25..60,75) não desenha, (50..50,5) desenha 1 pixel. 100% idêntico.
* **Efeito colateral:** 5 testes unitários antigos assumiam que sprite de tamanho zero desenha 1 pixel; foram
  corrigidos (segundo canto 1 pixel adiante). **Resolvido.**

## R7. Passo do UV do sprite em 16.16 truncado
* **Sintoma:** diferenças de ±1/16 de texel exatamente nas posições em que o UV exato é inteiro.
* **Fonte:** PCSX2 usa `dt = duv/dxy` (passo float) a partir de `t0 + dt*(left - p0)`.
* **Achado empírico (rampa `R=16·x`, onde R de saída revela o UV):** `U(i) = (u0·65536 + du·i + 2) >> 16`
  com `du = trunc(Δuv·65536/Δx)`; para V a constante que melhor ajusta é +3. 197/198 amostras idênticas em 8
  tamanhos (24, 36, 40, 44, 48, 52, 56, 64). **Incompleto**: ver 06 (o "+2/+3" não tem explicação de mecanismo).

## R8. Auto-realimentação (textura sobreposta ao destino) no modelo CPU
* **Sintoma:** CPU lia o que acabara de escrever (sprite `p=152 -> p=152` do blur); GPU/PCSX2 leem o pré-desenho.
* **Correção CPU:** instantâneo da VRAM quando a região da textura intersecta o frame buffer. Commit `8de9dfd`.
* **Estado:** CPU feito; **GPU depende de ordem de lote** (ver 07).
