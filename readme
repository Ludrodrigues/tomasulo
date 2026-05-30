# Simulador do Algoritmo de Tomasulo

Este projeto consiste em um simulador funcional do **Algoritmo de Tomasulo**, desenvolvido em C++. O objetivo é modelar o comportamento de um pipeline de processador superescalar com agendamento dinâmico de instruções, permitindo a **execução fora de ordem** (*Out-of-Order Execution*) e a resolução dinâmica de hazards de dados.

## Funcionalidades

- **Parser de Assembly Dinâmico:** Lê e interpreta um arquivo de texto (`.asm`) contendo instruções clássicas de ponto flutuante MIPS/RISC-V.
- **Configuração pelo Terminal:** Permite definir dinamicamente a quantidade de Estações de Reserva disponíveis para cada tipo de unidade funcional.
- **Visualização Ciclo a Ciclo:** Exibe o estado detalhado das instruções, das estações de reserva e dos registradores a cada pulso de clock.

## Estrutura do Pipeline Simulado

O simulador implementa as três etapas clássicas do algoritmo:
1. **Issue (Emissão):** Despacha uma instrução por ciclo (em ordem) se houver estação de reserva livre.
2. **Execute (Execução):** Avança o tempo de processamento conforme as latências de hardware (`LD=2`, `ADD/SUB=2`, `MUL=10`, `DIV=40`), iniciando no ciclo seguinte ao Issue.
3. **Write Result (Escrita):** Disponibiliza o resultado no Barramento de Dados Comum (CDB) para atualizar registradores e outras estações pendentes.

---

## Como Compilar e Executar

### Passo 1: Compilação
No terminal, navegue até a pasta do projeto e execute:
```bash
g++ tomasulo.cpp -o tomasulo
./tomasulo