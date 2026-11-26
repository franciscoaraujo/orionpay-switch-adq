# Template de Tarefa: OrionPay Switch Adquirente (`orionpay-switch-adq`)

## Título

(Resuma a tarefa em uma única linha. Ex: "Implementar tratamento reativo de timeout de rede para geração de Desfazimento MTI 0420")

---

## Descrição

(Detalhe o que precisa ser feito sob a ótica de um sistema core de adquirência em C++. Qual é o objetivo técnico de baixa latência ou resiliência? Qual problema de concorrência ou protocolo estamos resolvendo no Caminho Quente?)

**Exemplo de Cenário:**
Precisamos desacoplar o envio de mensagens ISO 8583 do fluxo de persistência síncrona, garantindo que o insert na tabela pai particionada `switch.transactions` via `libpq` ocorra através de uma Worker Thread dedicada, liberando o Event Loop (`epoll`) de rede imediatamente após o recebimento do MTI 0110/0210.

---

## Critérios de Aceitação

(Liste os pontos técnicos cruciais que definem que a tarefa foi concluída com sucesso em nível industrial Tier 1. Use o formato de checklist.)

- [ ] O código compila sem erros ou warnings utilizando as flags rigorosas (`-std=c++20 -Wall -Wextra -Werror`).
- [ ] O recurso de rede/banco de dados implementado segue estritamente o padrão RAII (sem memory leaks ou conexões órfãs).
- [ ] A thread principal de rede (Event Loop) permanece não-bloqueante através do uso correto de filas atômicas (`std::atomic`) ou travas granulares.
- [ ] O empacotamento/parsing dos campos da ISO 8583 respeita o formato estrito da especificação (Bitmaps, BCD, LLVAR/LLLVAR).
- [ ] A escrita no banco de dados é direcionada corretamente para a tabela pai `switch.transactions`, validando que as propriedades ACID foram mantidas.
- [ ] Em cenários de falha de conexão ou timeout com a VisaNet externa, o mecanismo SAF é disparado com a criação do MTI 0420 de desfazimento.

---

## Arquivos e Módulos Afetados (Opcional)

(Enumere os diretórios ou arquivos internos do projeto que serão modificados com base na separação de responsabilidades (SoC).)

- `src/network/event_loop.cpp` (Gerenciamento de sockets e multiplexação)
- `src/iso8583/packager.cpp` (Montagem e parsing de Bitmaps/Campos)
- `src/database/pg_client.cpp` (Interação nativa com a libpq)
- `src/crypto/hsm_queue.cpp` (Fila não-bloqueante de chamadas do SoftHSM)
- `src/saf/store_forward.cpp` (Fila persistente e repetição do MTI 0420)

---

## Como Validar

(Descreva o passo a passo técnico de como testar a funcionalidade de adquirência localmente utilizando o ambiente Sandbox isolado via WSL/Docker.)

**Exemplo de Fluxo de Teste:**
1. Inicializar os containers do PostgreSQL (com as partições diárias ativas) e o emulador da VisaNet.
2. Disparar uma carga de requisição síncrona simulando a perna do `merchant-service`.
3. Validar via log do `spdlog` que a thread criptográfica interceptou o pacote, acionou o `orionpay-hsm` para traduzir o PIN Block com sucesso e devolveu o controle para a thread de transmissão de rede.
4. Confirmar através da rede externa (VisaNet Simulator) o recebimento do MTI 0100/0200 estruturado corretamente.
5. Derrubar o link de rede com a VisaNet intencionalmente durante o envio para certificar que o Switch gerou o log de erro apropriado e empurrou o Desfazimento (MTI 0420) para o motor do SAF em background.
6. Executar uma query direto no Postgres para certificar que a transação foi persistida na tabela pai `switch.transactions` (caindo na partição física correspondente ao `created_at` do dia).

---

## Contexto Adicional (Dumps ISO, Logs do spdlog, Erros de Banco)

(Cole aqui os dumps hexadecimais das mensagens ISO 8583, strings de erro retornadas pela libpq ou comportamentos inesperados de concorrência observados no ambiente de laboratório.)