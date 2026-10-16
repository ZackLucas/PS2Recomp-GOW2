# Contornados e incompletos

Itens em que o sintoma foi tratado, mas a solução é empírica, parcial ou tem ressalva. Revisar quando houver
cenas novas ou quando o desempenho permitir.

## C1. Passo de UV do sprite: constantes "+2" (U) e "+3" (V)
* **O que foi feito:** `U = (u0·65536 + du·pixel + 2) >> 16`, `du = trunc(Δuv·65536/Δx)`; V com +3.
* **Por que é incompleto:** a constante foi **ajustada** às medidas (197/198 amostras idênticas), não derivada.
  Duas amostras discordam de qualquer constante única (n=24, i=6 e n=56, i=14), o que indica mecanismo de
  float/ponto fixo diferente do modelado. O valor +3 em V foi escolhido por melhorar a métrica, sem ajuste exato.
* **Impacto:** *baixo-médio*. Erros de 1/16 de texel; visíveis só em texturas de alto contraste muito ampliadas
  ou ao longo de cadeias de passadas.
* **Como revisar:** ler `GSDrawScanline` (caminho de amostragem FST e conversão float->int) e/ou sondar
  tamanhos e posições fracionárias de vértice (`spr_ramp` com sx,sy fracionários; `u0 != 0`; UV invertido).

## C2. Bilinear: ainda ±1 em parte dos pixels
* **Estado:** variante "horizontal primeiro, truncando em cada etapa" é a melhor entre 6 testadas, mas na cena
  real só 20–30% dos pixels são idênticos (erro médio 0,6). Diferença de ±1 nível, não >8.
* **Hipóteses:** ordem exata das operações do JIT (16 bits com `pmulhw`), pesos de 12 bits, arredondamento.
* **Impacto:** *baixo* (1/255); acumula pouco (cadeia de blur ficou em 0,6 de erro).

## C3. Gouraud ±1 em ~3% dos pixels
* **Medido:** triângulo opaco com 3 cores: 95,8–96,6% idêntico, diferenças só ±1.
* **Causa provável:** PCSX2 interpola cor em ponto fixo; nós usamos float e `static_cast<int>`.
* **Impacto:** *baixo*. Fácil de investigar com sonda `gouraud` + leitura do código.

## C4. Triângulos amostram o UV com `iu = uint16(u0*w0 + ...)` (float)
* **Estado:** o caminho de sprite foi reescrito em inteiro; o de triângulo FST ainda interpola em float e trunca.
* **Impacto:** desconhecido; sonda `tri_texL` (linear, skew leve) deu 99,9% idêntico, então provavelmente baixo.

## C5. Instantâneo de textura na GPU para primitivas que leem o próprio destino
* **CPU:** feito (`GSCpuBackend::Submit`, cópia pré-desenho quando a textura intersecta o frame buffer).
* **GPU:** **não** feito: a primitiva lê a VRAM viva e depende da ordem de execução/lote. O resultado medido
  contra o PCSX2 é bom (0,6), mas por sorte de ordenação.
* **Impacto:** *médio* (não determinismo possível em outras cenas). **Solução proposta:** copiar o SSBO da VRAM
  (4 MiB, microssegundos na GPU) para um SSBO sombra e amostrar dele quando o desenho for marcado como
  "realimentação" (flag no cabeçalho da primitiva).

## C6. 10 words/quadro divergem CPU×GPU (auto-realimentação, prim #7206)
* Explicado: o CPU lia o que acabara de escrever; a GPU lê o pré-desenho. Com o instantâneo da CPU (C5)
  isto e ~100 mil words de divergência do blur desaparecem em parte; restam ~6 mil words em sprites sequenciais.
* **Impacto:** *baixo para o produto*, *médio para os testes* (a verificação "CPU==GPU" em quadros reais deixa de ser
  limpa até a GPU ter o instantâneo).

## C7. Divisão: `gsDiv` usa `double` na GPU
* Garante igualdade bit a bit com a CPU, mas fp64 em GPUs de consumo é lento (1/64 da taxa fp32 em várias
  arquiteturas). Suspeita não medida de parte dos ~12 ms de desenho.
* **Impacto:** desempenho. Alternativas: reciprocal + Newton em fp32 com prova de exatidão por teste exaustivo.

## C8. Divisão conservadora de desenhos
* 756 divisões em 4 quadros do menu para garantir ordem. Correto mas caro.
* **Impacto:** desempenho. Refinar com análise de interseção real de regiões em vez de por mudança de estado.

## C9. Medição limitada a 214 linhas
* Ver O7 em 04 e item A3 em 07.

## C10. Oráculo é o PCSX2, não o hardware
* Todo comportamento "provado" é "igual ao PCSX2 software renderer". Se ele diverge do console, herdamos.
* **Mitigação:** o PCSX2 é a referência de fato da comunidade; para jogos específicos podem existir hacks do
  renderizador por hardware que **não** se aplicam ao software renderer.
