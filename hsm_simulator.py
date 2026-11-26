import socket
import struct
import binascii

# Emulador Simples em Python do SoftHSM - Escuta na porta 15001
# Ele lê pacotes de 'Command Translation' (CT) em Length-Prefixed 
# e devolve 'Response Translation' (RT) com Status=0 e PIN Mockado.

HOST = '0.0.0.0'
PORT = 15001

def recvall(sock, n):
    """Lê exatamente n bytes do socket, se o EOF bater ele quebra"""
    data = bytearray()
    while len(data) < n:
        packet = sock.recv(n - len(data))
        if not packet:
            return None
        data.extend(packet)
    return data

def start_simulator():
    print(f"[*] Iniciando Emulador do HSM em {HOST}:{PORT}...")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((HOST, PORT))
        s.listen()
        
        while True:
            print("[*] Aguardando conexao do orionpay-switch-adq...")
            conn, addr = s.accept()
            with conn:
                print(f"[+] Switch Adquirente conectado: {addr}")
                while True:
                    try:
                        # 1. Lê os 2 bytes de Tamanho (Length-Prefixed Big Endian) usando recvall para TCP safe read
                        length_prefix = recvall(conn, 2)
                        if not length_prefix:
                            print("[-] Conexao fechada pelo Switch (EOF recebido no header).")
                            break
                        
                        payload_length = struct.unpack('>H', length_prefix)[0]
                        print(f"   [->] Recebido Length Prefix: {payload_length} bytes esperados.")
                        
                        # Se payload for zero ou lixo, ignora
                        if payload_length == 0:
                            continue

                        # 2. Lê o payload (Corpo)
                        payload = recvall(conn, payload_length)
                        if not payload:
                            print("[-] Payload incompleto recebido. Quebrando.")
                            break
                        
                        print(f"   [->] Payload (Hex): {binascii.hexlify(payload).decode('utf-8').upper()}")

                        # Verifica se é um comando 'CT' (Command Translation)
                        if payload[0:2] == b'CT':
                            seq_id = payload[2:4]
                            print(f"   [*] Processando Command 'CT' - Sequence ID: {binascii.hexlify(seq_id).decode('utf-8')}")

                            # 3. Monta a Resposta 'RT'
                            response_body = bytearray()
                            response_body.extend(b'RT')         # Command Code
                            response_body.extend(seq_id)        # Sequence ID ecoado
                            response_body.append(0x00)          # Status (00 = Sucesso)
                            
                            # O PIN Traduzido Mockado (16 bytes)
                            mock_translated_pin = binascii.unhexlify("FFCCDDEE11223344AABBCCDDEEFF0011")
                            response_body.extend(mock_translated_pin)

                            # Calcula tamanho e envia Length-Prefix + Payload
                            resp_length = len(response_body)
                            conn.sendall(struct.pack('>H', resp_length))
                            conn.sendall(response_body)

                            print(f"   [<-] Resposta 'RT' Enviada. Status=00. PIN={binascii.hexlify(mock_translated_pin).decode('utf-8').upper()}\n")
                        else:
                            print("   [!] Comando desconhecido recebido.")

                    except ConnectionResetError:
                        print("[-] Connection Reset (HSM desconectado).")
                        break

if __name__ == '__main__':
    start_simulator()