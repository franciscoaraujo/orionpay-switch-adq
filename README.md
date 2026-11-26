# OrionPay Switch Adquirente (`orionpay-switch-adq`)

Este é o motor core de roteamento transacional síncrono da OrionPay. Operando sob o Caminho Quente (Hot Path), este componente é o coração da nossa infraestrutura de adquirência de cartões, focado em alta disponibilidade, baixíssima latência e segurança criptográfica financeira.

## Visão Geral da Arquitetura

O `orionpay-switch-adq` abandonou a arquitetura antiga de "Server/Mock" passivo e foi totalmente refatorado para um modelo reativo **Client/Router** com inversão de fluxo. 

Ele captura requisições de autorização provenientes do nosso serviço interno (`merchant-service`), as empacota para o protocolo financeiro binário ISO 8583, interage assincronamente com o SoftHSM, e envia as transações para a rede da bandeira (ex: VisaNet/Mastercard) através de Sockets TCP/IP persistentes não-bloqueantes.

Todo o gerenciamento de eventos de E/S é baseado em multiplexação nativa (`epoll`/Boost.Asio), blindando a thread principal contra gargalos de rede ou lentidão em sistemas de terceiros.

### Módulos Implementados (Separação de Responsabilidades Estrita)

A arquitetura respeita rigorosamente a divisão lógica por domínios (`src/`):

1. **Network (`src/network/visanet_client.cpp`)**: Motor de socket assíncrono mantendo conexão outbound TCP constante com a rede externa da bandeira (VisaNet).
2. **Router (`src/switch/router.cpp`)**: Máquina de estados baseada em callbacks, gerenciando o trânsito da requisição entre Crypto -> Bandeira -> Persistência, sem nunca bloquear.
3. **Integração Criptográfica (`src/crypto/hsm_worker.cpp`)**: Abstração lock-free contendo uma fila nativa e uma Worker Thread que consome payloads para tradução de PIN Block interagindo com o `orionpay-hsm`.
4. **Persistência Particionada (`src/database/pg_worker.cpp`)**: Fila assíncrona dedicada que, assim que recebe a autorização final, realiza comandos nativos (`INSERT`) via `libpq` direto na tabela particionada do PostgreSQL (`switch.transactions`).
5. **Mecanismo SAF / Contingência (`src/saf/saf_manager.cpp`)**: O motor de Store and Forward (Resiliência Financeira). Em cenários de Timeout na comunicação com a bandeira, o switch gera o Desfazimento (MTI 0420) e empilha na thread de background do SAF, que repete o envio persistentemente sem poluir o pipeline principal de autorizações.

## Requisitos de Engenharia Atuais

A infraestrutura foi recentemente migrada para os padrões industriais rigorosos do C++ moderno.

- **Padrão Obrigatório:** `C++20` (Configurado estritamente no `CMakeLists.txt`). Permite uso nativo de `<concepts>`, `std::stop_token` e `std::jthread`.
- **Gerenciamento de Memória:** Implementação implacável de RAII (`std::unique_ptr`, `std::shared_ptr`). Qualquer operação suscetível a vazamentos de memória (memory leaks manuais) é rejeitada pelo pipeline.
- **Concorrência Segura:** Utilização de concorrência baseada em canais e Workers, limitando a contenção de locks diretos no Event Loop principal.

---

## 🚀 Guia de Implantação (Deployment em Servidor Linux)

Este guia descreve os passos necessários para compilar e executar o `orionpay-switch-adq` em um ambiente de produção (ou homologação) baseado em Linux.

### 1. Infraestrutura Recomendada

Para sustentar a alta taxa de transferência exigida no "Caminho Quente" financeiro, as seguintes especificações mínimas são recomendadas para o servidor:

*   **Sistema Operacional**: Ubuntu 22.04 LTS ou 24.04 LTS (ou equivalente baseado em Debian).
*   **CPU**: Mínimo 4 vCPUs (Recomendado 8+ vCPUs para alta simultaneidade em threads de I/O, gRPC e Workers de Crypto).
*   **RAM**: Mínimo 8 GB (Recomendado 16 GB+ para buffers do gRPC e TCP sockets).
*   **Rede**: Latência de rede ultrabaixa para instâncias de Banco de Dados e instâncias HSM.
*   **Disco**: SSD (I/O intensivo é offloaded para RAM, mas logs requerem escrita rápida).

### 2. Preparação do Servidor (Instalação de Dependências)

Conecte-se ao seu servidor Linux e atualize o repositório de pacotes. Em seguida, instale as ferramentas de build (C++20, CMake, Ninja) e as bibliotecas essenciais para criptografia, banco de dados e comunicação gRPC/Protobuf.

Execute o seguinte comando no terminal do servidor:

```bash
sudo apt update && sudo apt upgrade -y

# Instalação de compiladores e ferramentas de Build
sudo apt install -y build-essential gcc-13 g++-13 cmake ninja-build git pkg-config

# Configurar o GCC-13 como compilador padrão (necessário para C++20 completo)
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100

# Bibliotecas Essenciais (OpenSSL para Criptografia, libpq para PostgreSQL)
sudo apt install -y libssl-dev libpq-dev

# Binários e Bibliotecas pré-compiladas do gRPC e Protobuf
# (Isso elimina a necessidade de compilar o gRPC do zero via FetchContent)
sudo apt install -y protobuf-compiler libprotobuf-dev libgrpc++-dev protobuf-compiler-grpc
```

### 3. Clone do Repositório e Compilação

Agora, faça o clone seguro do projeto para a sua máquina e inicie o processo de compilação utilizando o `Ninja` (para builds ultrarrápidos paralelos).

```bash
# Clone o repositório
git clone https://[SEU_REPOSITORIO]/orionpay-switch-adq.git
cd orionpay-switch-adq

# Cria e entra no diretório de build
mkdir -p build && cd build

# Configura o CMake utilizando Ninja como gerador, apontando para Release (otimização máxima)
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..

# Realiza a compilação
ninja
```

> Se a compilação finalizar com sucesso, você terá um executável chamado `gateway_visa` dentro da pasta `build`.

### 4. Configuração do Ambiente e Execução

O sistema foi desenhado segundo os princípios da "Configuração Estrita", não aceitando senhas e IPs configurados direto no código (Hardcoded). O Switch exige que as configurações sejam passadas via Variáveis de Ambiente.

Antes de executar, você deve exportar (ou usar um arquivo `.env` carregado pelo Systemd) as seguintes variáveis essenciais:

```bash
# --- Conexão Banco de Dados (PostgreSQL) ---
export PGHOST="IP_DO_BANCO"
export PGPORT="5432"
export PGUSER="orion_admin"
export PGPASSWORD="secure_pass"
export PGDATABASE="orionpay"

# --- Conexão com a Bandeira (VisaNet / Bandeira Externa) ---
export VISANET_IP="192.168.1.50"
export VISANET_PORT="5000"

# --- Serviço gRPC Interno (Recebendo do Merchant Service) ---
export GRPC_SERVER_PORT="50051"

# --- Outras Configurações (ex: HSM, Kafka) ---
# Adicione conforme a topologia do seu cluster
```

Após configurar as variáveis, você pode iniciar o serviço:

```bash
./gateway_visa
```

### 5. Boas Práticas para Produção (Systemd)

Em produção, o Switch **não deve** rodar solto no terminal. É crucial envelopar o executável usando o **Systemd** para garantir inicialização automática, reinicialização em falhas e captura segura de logs rotacionados.

Crie o arquivo `/etc/systemd/system/orionpay-switch.service`:

```ini
[Unit]
Description=OrionPay Adquirer Switch Core
After=network.target

[Service]
Type=simple
User=orionpay
Group=orionpay
WorkingDirectory=/opt/orionpay-switch-adq/build
ExecStart=/opt/orionpay-switch-adq/build/gateway_visa

# Variáveis de Ambiente cruciais:
Environment="PGHOST=10.0.0.5"
Environment="PGPORT=5432"
Environment="PGUSER=orion_admin"
Environment="PGPASSWORD=sua_senha_forte"
Environment="PGDATABASE=orionpay"
Environment="VISANET_IP=192.168.1.50"
Environment="VISANET_PORT=5000"

Restart=on-failure
RestartSec=5
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

Ative e inicialize o serviço:

```bash
sudo systemctl daemon-reload
sudo systemctl enable orionpay-switch.service
sudo systemctl start orionpay-switch.service

# Acompanhe os logs em tempo real
sudo journalctl -u orionpay-switch.service -f
```

---
*OrionPay Technology Team.*
