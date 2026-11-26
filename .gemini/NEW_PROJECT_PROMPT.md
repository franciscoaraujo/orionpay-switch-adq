# Prompt para Geração de Novo Projeto C++: Switch Adquirente com Módulo HSM e Persistência

**Contexto:** Atue como um Engenheiro de Software Sênior especialista em Meios de Pagamento. O objetivo é criar um novo projeto C++ para um **Switch Adquirente**, focado em alta performance e segurança. Uma das funcionalidades críticas é o módulo de integração com o nosso **HSM** (Hardware Security Module) para realizar a tradução de PIN (PIN Translation). Além disso, o switch deve ser capaz de persistir todas as transações em um banco de dados PostgreSQL.

**Objetivo Geral do Projeto:** Gerar um novo projeto C++ moderno, configurado para desenvolver um serviço de rede assíncrono de alta performance, utilizando as melhores práticas e bibliotecas comuns em sistemas de backend, com foco inicial na integração com HSM e persistência de transações.

**Nome do Projeto:** [**Orionpay-switch-adiquirente**]

**Linguagem e Padrão:** C++20

**Sistema de Build:** CMake (com `FetchContent` para gerenciamento de dependências externas)

---

## Módulo de Integração HSM (PIN Translation)

**Requisito:** Criar o módulo de integração do **Switch Adquirente** com o **HSM** para realizar a tradução de PIN.

**Comunicação com HSM:**
*   **Transporte:** Socket TCP persistente.
*   **Protocolo:** Pacotes binários (raw bytes) utilizando o padrão *length-prefixed*, onde os 2 primeiros bytes indicam o tamanho total do payload em Big-Endian.
*   **Endereço:** IP do HSM é `127.0.0.1` e a porta é `15001`.

**Especificações da Requisição (Command: CT - Command Translation):**
Deve ser criada uma função/método que receba os parâmetros reais da transação, mas por enquanto, implemente chamando a função com os seguintes valores **mockados** para testes:

*   **Message Length:** (Calculado dinamicamente: tamanho do payload) (2 bytes)
*   **Command Code:** `"CT"` (em ASCII) (2 bytes)
*   **Sequence ID:** `0001` (Hex, Big-Endian) (2 bytes)
*   **In Format:** `06` (Hex) -> Representa ISO-4 (1 byte)
*   **PIN Block (In):** `112233445566778899AABBCCDDEEFF` (Hex) (16 bytes)
*   **Out Format:** `02` (Hex) -> Representa ISO-3 (1 byte)
*   **ZPK Index:** `0001` (Hex, Big-Endian) (2 bytes)
*   **PAN (Masked):** `0123456789ABCDEF` (Hex) (8 bytes)
*   **KCV:** `A1B2C3` (Hex) (3 bytes)

**Especificações da Resposta (Command: RT - Response Translation):**
O HSM devolverá a resposta no mesmo Socket TCP. O seu código deve escutar o evento de dados (data/recv), extrair os 2 primeiros bytes para saber o tamanho, e então ler o payload:

*   **Command Code:** `"RT"` (ASCII) (2 bytes)
*   **Sequence ID:** (Mesmo enviado) (2 bytes)
*   **Status:** `00` para sucesso (1 byte)
*   **PIN Block (Out):** O novo PIN Block gerado (16 bytes)

**Requisitos do Código para o Módulo HSM:**
1.  Crie uma estrutura modular onde os dados mockados sejam facilmente substituídos por variáveis reais vindas de uma transação ISO-8583 (0200).
2.  Trate o retorno de forma assíncrona, associando a resposta correta ao pedido usando o `Sequence ID`.
3.  Adicione logs para mostrar o Hexadecimal da requisição montada e o Hexadecimal do PIN Block traduzido recebido.

---

## Módulo de Persistência de Transações

**Requisito:** O switch deve armazenar todas as transações na tabela `switch.transactions` em um banco de dados PostgreSQL.

**DDL da Tabela `switch.transactions`:**

```sql
CREATE TABLE switch.transactions (
	id uuid NOT NULL,
	merchant_id uuid NOT NULL,
	terminal_id varchar(8) NOT NULL,
	amount numeric(12, 2) NOT NULL,
	currency_code int4 NOT NULL,
	mti varchar(4) NOT NULL,
	pan_masked varchar(19) NOT NULL,
	nsu_adquirer varchar(12) NOT NULL,
	nsu_bandeira varchar(12) NULL,
	response_code varchar(2) NOT NULL,
	created_at timestamptz NOT NULL,
	CONSTRAINT pk_transactions_partitioned PRIMARY KEY (id, created_at)
)
PARTITION BY RANGE (created_at);
CREATE INDEX idx_transactions_merchant_date ON ONLY switch.transactions USING btree (merchant_id, created_at DESC);
CREATE INDEX idx_transactions_nsu ON ONLY switch.transactions USING btree (nsu_adquirer);
```

**Requisitos do Código para o Módulo de Persistência:**
1.  Utilizar uma biblioteca cliente PostgreSQL para C++ (ex: `libpqxx`).
2.  Implementar um `TransactionRepository` ou similar para gerenciar as operações de CRUD (especialmente `INSERT`) na tabela `switch.transactions`.
3.  Garantir que as operações de banco de dados sejam assíncronas ou executadas em um `ThreadPool` dedicado para não bloquear o `io_context` principal.

---

## Funcionalidades Essenciais do Switch (Gerais):

*   **Servidor TCP Assíncrono:** Implementação de um servidor TCP robusto e assíncrono usando a biblioteca Asio (standalone).
*   **Framing de Mensagens:** Suporte para um protocolo de framing TCP onde cada mensagem é precedida por um cabeçalho de 2 bytes (Network Byte Order - Big Endian) indicando o tamanho do payload.
*   **Serialização/Deserialização de Mensagens:** Utilização de Protobuf para serializar e deserializar o payload das mensagens.
*   **Logging:** Sistema de logging eficiente e configurável baseado em `spdlog`.
*   **Gerenciamento de Threads:** Um `ThreadPool` básico para processamento de tarefas que não bloqueiem o `io_context` principal.
*   **Injeção de Dependência (Callback):** O servidor TCP deve aceitar um callback para processar as requisições recebidas, desacoplando a lógica de rede da lógica de negócio.

**Estrutura de Diretórios Proposta:**

```
[Nome do Projeto]/
├── src/
│   ├── main.cpp
│   ├── network/
│   │   ├── tcp_server.h
│   │   ├── tcp_server.cpp
│   │   ├── session.h
│   │   └── session.cpp
│   ├── proto/
│   │   └── service.proto  (arquivo .proto de exemplo)
│   ├── common/
│   │   ├── logger.h
│   │   ├── logger.cpp
│   │   ├── thread_pool.h
│   │   └── thread_pool.cpp
│   ├── security/
│   │   ├── hsm_client.h  // Novo arquivo para o cliente HSM
│   │   └── hsm_client.cpp // Novo arquivo para o cliente HSM
│   ├── persistence/
│   │   ├── transaction_repository.h // Novo arquivo para persistência
│   │   └── transaction_repository.cpp // Novo arquivo para persistência
│   └── service/
│       ├── request_processor.h
│       └── request_processor.cpp
├── cmake/
│   └── compiler_settings.cmake
├── test/
│   └── main_test.cpp (esqueleto para Google Test)
├── .gitignore
└── CMakeLists.txt
```

**Arquivos Iniciais a Serem Gerados e Conteúdo:**

1.  **`CMakeLists.txt` (na raiz do projeto):**
    *   Configuração para C++20.
    *   Inclusão de `FetchContent` para Asio (standalone), Protobuf, spdlog e `libpqxx`.
    *   Definição de um executável principal (`[Nome do Projeto]`).
    *   Configuração para geração automática de arquivos Protobuf a partir de `src/proto/service.proto` (gerando `.pb.h`, `.pb.cc`, `.grpc.pb.h`, `.grpc.pb.cc` em um diretório de build e incluindo-os corretamente).
    *   Linkagem com as bibliotecas necessárias (Protobuf, gRPC (para as libs geradas), Asio, spdlog, `libpqxx`).
    *   Configuração para testes com Google Test.

2.  **`src/main.cpp`:**
    *   Inicialização do `Logger`.
    *   Criação de um `asio::io_context`.
    *   Criação e inicialização de um `ThreadPool` para processamento de requisições.
    *   **ADICIONAR:** Inicialização do módulo de persistência (ex: `PgWorker` ou `DatabaseManager`).
    *   **ADICIONAR:** Instanciação e início do `HsmClient`.
    *   Definição de um callback de processamento de requisições (mockado inicialmente).
    *   Instanciação e início do `TcpServer` em uma porta configurável, passando o `io_context`, `ThreadPool` e o callback.
    *   Execução do `io_context` em múltiplas threads.

3.  **`src/common/logger.h` e `src/common/logger.cpp`:**
    *   Implementação básica de um logger usando `spdlog`, com funções como `LOG_INFO`, `LOG_ERROR`, `LOG_DEBUG`.

4.  **`src/common/thread_pool.h` e `src/common/thread_pool.cpp`:**
    *   Esqueleto de uma classe `ThreadPool` para enfileirar e executar tarefas assíncronas.

5.  **`src/network/tcp_server.h` e `src/network/tcp_server.cpp`:**
    *   Esqueletos para a classe `TcpServer` que aceita conexões e cria `Session`s.
    *   O construtor deve aceitar o `asio::io_context`, a porta, o `ThreadPool` e o callback de processamento.

6.  **`src/network/session.h` e `src/network/session.cpp`:**
    *   Esqueletos para a classe `Session` que gerencia uma única conexão TCP.
    *   Deve usar `std::enable_shared_from_this`.
    *   Implementar a lógica de leitura do cabeçalho de 2 bytes, leitura do payload, chamada ao callback de processamento e escrita da resposta (cabeçalho + payload).
    *   Definir um `using` para o tipo do callback de processamento (ex: `RequestProcessingCallback`).

7.  **`src/proto/service.proto`:**
    *   Um arquivo `.proto` de exemplo com uma mensagem de `Request` e uma de `Response` simples, e um serviço gRPC de exemplo (mesmo que não seja usado diretamente, a geração dos arquivos `.grpc.pb.h` e `.grpc.pb.cc` é útil para o CMake).
    *   Ex: `message MyRequest { string data = 1; }; message MyResponse { string result = 1; }; service MyService { rpc Process(MyRequest) returns (MyResponse); };`

8.  **`src/security/hsm_client.h` e `src/security/hsm_client.cpp`:**
    *   **NOVO MÓDULO:** Esqueletos para a classe `HsmClient` que gerencia a conexão persistente com o HSM.
    *   Deve incluir a função `pin_translate` com os parâmetros mockados e a lógica de montagem/parsing/logging conforme as especificações acima.
    *   Gerenciamento assíncrono de requisições/respostas usando `Sequence ID`.

9.  **`src/persistence/transaction_repository.h` e `src/persistence/transaction_repository.cpp`:**
    *   **NOVO MÓDULO:** Esqueletos para a classe `TransactionRepository` que encapsula a lógica de persistência de transações.
    *   Deve conter um método `save_transaction` que receba os dados de uma transação e os insira na tabela `switch.transactions`.
    *   Utilizar `libpqxx` para interação com o PostgreSQL.

10. **`src/service/request_processor.h` e `src/service/request_processor.cpp`:**
    *   Esqueletos para uma classe ou funções que implementam a lógica de negócio que será chamada pelo callback da `Session`.

11. **`cmake/compiler_settings.cmake`:**
    *   Configurações básicas de compilador (warnings, otimizações, etc.).

12. **`test/main_test.cpp`:**
    *   Esqueleto para Google Test, com testes básicos para `HsmClient` e `TransactionRepository`.

13. **`.gitignore`:**
    *   Um arquivo `.gitignore` padrão para projetos C++ com CMake e CLion.

**Considerações Adicionais:**

*   O projeto deve ser inicializado como um repositório Git.
*   A estrutura de includes deve ser configurada para que os arquivos gerados pelo Protobuf sejam encontrados.
