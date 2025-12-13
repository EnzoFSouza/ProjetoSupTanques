#include <iostream>     /* cerr */
#include <algorithm>
#include <cstdint>      /* uint16_t */
#include "supservidor.h"
#include "../MySocket/MySocket/mysocket.h"

using namespace std;

/* ========================================
   CLASSE SUPSERVIDOR
   ======================================== */

/// Construtor
SupServidor::SupServidor()
  : Tanks()
  , server_on(false)
  , LU()
  , thr_server()
  , sock_server()
{
  // Inicializa a biblioteca de sockets
  mysocket_status iResult = mysocket::init();

  // Em caso de erro, mensagem e encerra
  if (iResult != mysocket_status::SOCK_OK)
  {
    cerr <<  "Biblioteca mysocket nao pode ser inicializada";
    exit(-1);
  }
}

/// Destrutor
SupServidor::~SupServidor()
{
  // Deve parar a thread do servidor
  server_on = false;

  // Fecha todos os sockets dos clientes
  for (auto& U : LU) U.close();

  // Fecha o socket de conexoes
  sock_server.close();

  // Espera o fim da thread do servidor
  if (thr_server.joinable()) thr_server.join();
  thr_server = std::thread();

  // Encerra a biblioteca de sockets
  mysocket::end();
}

/// Liga o servidor
bool SupServidor::setServerOn()
{
  // Se jah estah ligado, nao faz nada
  if (server_on) return true;

  // Liga os tanques
  setTanksOn();

  // Indica que o servidor estah ligado a partir de agora
  server_on = true;

  try
  {
    // Coloca o socket de conexoes em escuta
    mysocket_status iResult = sock_server.listen(SUP_PORT);

    // Em caso de erro, gera excecao
    if (iResult != mysocket_status::SOCK_OK) throw 1;

    // Lanca a thread do servidor que comunica com os clientes
    thr_server = std::thread( [this]()
    {
      this->thr_server_main();
    } );

    // Em caso de erro, gera excecao
    if (!thr_server.joinable()) throw 2;
  }
  catch(int i)
  {
    cerr << "Erro " << i << " ao iniciar o servidor\n";

    // Deve parar a thread do servidor
    server_on = false;

    // Fecha o socket do servidor
    sock_server.close();

    return false;
  }

  // Tudo OK
  return true;
}

/// Desliga o servidor
void SupServidor::setServerOff()
{
  // Se jah estah desligado, nao faz nada
  if (!server_on) return;

  // Deve parar a thread do servidor
  server_on = false;

  // Fecha todos os sockets dos clientes
  for (auto& U : LU) U.close();

  // Fecha o socket de conexoes
  sock_server.close();

  // Espera pelo fim da thread do servidor
  if (thr_server.joinable()) thr_server.join();

  // Faz o identificador da thread apontar para thread vazia
  thr_server = std::thread();

  // Desliga os tanques
  setTanksOff();
}

/// Leitura do estado dos tanques
void SupServidor::readStateFromSensors(SupState& S) const
{
  // Estados das valvulas: OPEN, CLOSED
  S.V1 = v1isOpen();
  S.V2 = v2isOpen();
  // Niveis dos tanques: 0 a 65535
  S.H1 = hTank1();
  S.H2 = hTank2();
  // Entrada da bomba: 0 a 65535
  S.PumpInput = pumpInput();
  // Vazao da bomba: 0 a 65535
  S.PumpFlow = pumpFlow();
  // Estah transbordando (true) ou nao (false)
  S.ovfl = isOverflowing();
}

/// Leitura e impressao em console do estado da planta
void SupServidor::readPrintState() const
{
  if (tanksOn())
  {
    SupState S;
    readStateFromSensors(S);
    S.print();
  }
  else
  {
    cout << "Tanques estao desligados!\n";
  }
}

/// Impressao em console dos usuarios do servidor
void SupServidor::printUsers() const
{
  for (const auto& U : LU)
  {
    cout << U.login << '\t'
         << "Admin=" << (U.isAdmin ? "SIM" : "NAO") << '\t'
         << "Conect=" << (U.isConnected() ? "SIM" : "NAO") << '\n';
  }
}

/// Adicionar um novo usuario
bool SupServidor::addUser(const string& Login, const string& Senha,
                             bool Admin)
{
  // Testa os dados do novo usuario
  if (Login.size()<6 || Login.size()>12) return false;
  if (Senha.size()<6 || Senha.size()>12) return false;

  // Testa se jah existe usuario com mesmo login
  auto itr = find(LU.begin(), LU.end(), Login);
  if (itr != LU.end()) return false;

  // Insere
  LU.push_back( User(Login,Senha,Admin) );

  // Insercao OK
  return true;
}

/// Remover um usuario
bool SupServidor::removeUser(const string& Login)
{
  // Testa se existe usuario com esse login
  auto itr = find(LU.begin(), LU.end(), Login);
  if (itr == LU.end()) return false;

  // Remove
  LU.erase(itr);

  // Remocao OK
  return true;
}

/// A thread que implementa o servidor.
/// Comunicacao com os clientes atraves dos sockets.
void SupServidor::thr_server_main(void)
{
    // Fila de sockets para aguardar chegada de dados
    mysocket_queue f;

    // Socket temporario para nova conexao
    tcp_mysocket t;

    // O comando recebido
    uint16_t cmd;

    // Dados da nova conexao
    string login, password;

    // Variaveis auxiliares
    mysocket_status iResult;
    uint16_t valor;

    // Iteradores
    auto iU = LU.begin();

    while (server_on){
        // Erros mais graves que encerram o servidor
        // Parametro do throw e do catch eh uma const char* = "texto"
        try{
            ///Divergencia sock.close()
            // Encerra se o socket de conexoes estiver fechado
            if (!sock_server.accepting()){
                throw "socket de conexoes fechado";
            }

            // Inclui na fila de sockets todos os sockets que eu quero monitorar para ver se houve chegada de dados
            // Limpa a fila de sockets
            f.clear();

            // Inclui na fila o socket de conexoes
            f.include(sock_server);

            // Inclui na fila todos os sockets dos clientes conectados
            for (auto& U : LU){
                if (U.isConnected()) f.include(U.sock);
            }

            // Espera ateh que chegue dado em algum socket (com timeout)
            iResult = f.wait_read(SUP_TIMEOUT*1000);
            //if (iResult != mysocket_status::SOCK_OK) throw "fila de espera"; //Do mensageiro1cpp

            // De acordo com o resultado da espera:
            switch (iResult){
                // SOCK_TIMEOUT:
                // Saiu por timeout: nao houve atividade em nenhum socket
                // Aproveita para salvar dados ou entao nao faz nada
                case mysocket_status::SOCK_TIMEOUT:
                    continue;
                // SOCK_ERROR:
                // Erro no select: encerra o servidor
                case mysocket_status::SOCK_ERROR:
                default:
                    throw "erro no select";

                // SOCK_OK:
                // Houve atividade em algum socket da fila:
                // Testa se houve atividade nos sockets dos clientes. Se sim:
                case mysocket_status::SOCK_OK:
                    // Erros nos clientes: catch fecha a conexao com esse cliente
                    try{
                        //Primeiro, testa os sockets dos clientes
                        for (iU = LU.begin(); server_on && iU != LU.end(); ++iU){
                            if (server_on && iU->isConnected() && f.had_activity(iU->sock)){
                                // Leh o comando recebido do cliente
                                iResult = iU->sock.read_uint16(cmd);
                                // Pode ser mysocket_status::SOCK_OK, mysocket_status::SOCK_TIMEOUT,
                                // mysocket_status::SOCK_DISCONNECTED ou mysocket_status::SOCK_ERRO
                                // Nao deve ser mysocket_status::SOCK_TIMEOUT porque a funcao read_int
                                // nao foi chamada com tempo maximo
                                if(iResult != mysocket_status::SOCK_OK) throw 1;

                                // Executa a acao
                                switch(cmd){
                                    case CMD_GET_DATA:{
                                        // Leh estado dos tanques
                                        SupState S;
                                        readStateFromSensors(S);

                                        // Envia confirmacao
                                        iResult = iU->sock.write_uint16(CMD_DATA);
                                        if(iResult != mysocket_status::SOCK_OK) throw 2;

                                        // Envia todos os dados do estado
                                        if (iU->sock.write_uint16(S.H1) != mysocket_status::SOCK_OK) throw 3;
                                        if (iU->sock.write_uint16(S.H2) != mysocket_status::SOCK_OK) throw 3;
                                        if (iU->sock.write_uint16(S.ovfl) != mysocket_status::SOCK_OK) throw 3;
                                        if (iU->sock.write_uint16(S.PumpFlow) != mysocket_status::SOCK_OK) throw 3;
                                        if (iU->sock.write_uint16(S.PumpInput) != mysocket_status::SOCK_OK) throw 3;
                                        if (iU->sock.write_uint16(S.V1) != mysocket_status::SOCK_OK) throw 3;
                                        if (iU->sock.write_uint16(S.V2) != mysocket_status::SOCK_OK) throw 3;

                                        //if(iResult != mysocket_status::SOCK_OK) throw 3;
                                        break;
                                    }
                                    case CMD_SET_V1:{
                                        // Apenas admin pode alterar
                                        if(!iU->isAdmin){
                                            iU->sock.write_uint16(CMD_ERROR);
                                            break;
                                        }

                                        // Leh o valor desejado para V1
                                        iResult = iU->sock.read_uint16(valor, SUP_TIMEOUT*1000);
                                        if(iResult != mysocket_status::SOCK_OK) throw 4;

                                        // Altera V1
                                        setV1Open(valor != 0);

                                        // Envia confirmacao
                                        iResult = iU->sock.write_uint16(CMD_OK);
                                        if(iResult != mysocket_status::SOCK_OK) throw 5;
                                        break;
                                    }
                                    case CMD_SET_V2:{
                                        // Apenas admin pode alterar
                                        if(!iU->isAdmin){
                                            iU->sock.write_uint16(CMD_ERROR);
                                            break;
                                        }

                                        // Leh o valor desejado para V2
                                        iResult = iU->sock.read_uint16(valor, SUP_TIMEOUT*1000);
                                        if(iResult != mysocket_status::SOCK_OK) throw 4;

                                        // Altera V2
                                        setV2Open(valor != 0);

                                        // Envia confirmacao
                                        iResult = iU->sock.write_uint16(CMD_OK);
                                        if(iResult != mysocket_status::SOCK_OK) throw 5;
                                        break;
                                    }
                                    case CMD_SET_PUMP:{
                                        // Apenas admin pode alterar
                                        if(!iU->isAdmin){
                                          iU->sock.write_uint16(CMD_ERROR);
                                          break;
                                        }

                                        // Leh o valor desejado para a bomba
                                        iResult = iU->sock.read_uint16(valor, SUP_TIMEOUT*1000);
                                        if(iResult != mysocket_status::SOCK_OK) throw 4;

                                        // Altera entrada da bomba
                                        setPumpInput(valor);

                                        // Envia confirmacao
                                        iResult = iU->sock.write_uint16(CMD_OK);
                                        if(iResult != mysocket_status::SOCK_OK) throw 5;
                                        break;
                                    }
                                    case CMD_LOGOUT:{
                                        // Desconecta o usuario
                                        iU->close();
                                        break;
                                    }
                                    default:
                                        iU->sock.write_uint16(CMD_ERROR);
                                        break;
                                }//fim switch (cmd)
                            } //Fim do if server on..
                        } //Fim do for
                    } //Fim do try para erros nos clientes
                    catch(int erro){
                        //Desconecta o usuario
                        iU->close();
                        cerr << "Erro " << erro << " na leitura de comando do cliente " << iU->login << endl;
                    }

                    // Depois, testa se houve atividade no socket de conexao. Se sim:
                    if (server_on && sock_server.accepting() && f.had_activity(sock_server)){
                        // Estabelece nova conexao em socket temporario
                        iResult = sock_server.accept(t);
                        if (iResult != mysocket_status::SOCK_OK) throw 3; // Erro grave: encerra o servidor

                        try { //Erros na conexao com cliente, testa socket temporario ou desconecta novo cliente
                            // Leh o comando do usuario que deseja se conectar
                            iResult = t.read_uint16(cmd, SUP_TIMEOUT*1000);
                            if (iResult != mysocket_status::SOCK_OK) throw 1;

                            // Testa o comando
                            if (cmd!=CMD_LOGIN) throw 2;

                            // Leh o login do usuario
                            iResult = t.read_string(login, SUP_TIMEOUT*1000);
                            if (iResult != mysocket_status::SOCK_OK) throw 3;

                            // Leh a senha do usuario
                            iResult = t.read_string(password, SUP_TIMEOUT*1000);
                            if (iResult != mysocket_status::SOCK_OK) throw 4;

                            // Testa o login e senha
                            if (login.size() < 6 || login.size() > 12 ||
                            password.size() < 6 || password.size() > 12) throw 5;

                            // Se deu tudo certo, faz o socket temporario ser o novo socket do cliente e envia confirmacao
                            // Verifica se jah existe um usuario cadastrado com esse login
                            // Procura usuario na lista
                            auto iU2 = find(LU.begin(), LU.end(), login);

                            // Usuario deve existir
                            if (iU2 == LU.end()) throw 6;

                            // Testa se a senha confere
                            if (iU2->password != password) throw 7;

                            // Testa se nao estah jah conectado
                            if (iU2->isConnected()) throw 8;

                            // Associa o socket do cliente ao usuario
                            iU2->sock.swap(t);

                            // Envia a confirmacao de conexao
                            if (iU2->isAdmin) iResult = iU2->sock.write_uint16(CMD_ADMIN_OK);

                            else iResult = iU2->sock.write_uint16(CMD_OK);

                            if (iResult != mysocket_status::SOCK_OK) throw 9;
                        } //fim try
                        catch (int erro){ //Erros na conexao de novo cliente
                            if (erro>=5 && erro<=8){
                                // Comunicacao com socket temporario OK, login invalido
                                // Envia comando informando login invalido
                                t.write_int16(CMD_ERROR);
                                // Fecha o socket temporario
                                t.close();
                            }
                            else t.close();
                            // Informa erro nao previsto
                            cerr << "Erro " << erro << " na conexao de novo cliente" << endl;
                        }
                    }
                    break;
                }
        } // fim try - Erros mais graves que encerram o servidor
        catch(const char* err)  // Erros mais graves que encerram o servidor
        {
            cerr << "Erro no servidor: " << err << endl;

            // Sai do while e encerra a thread
            server_on = false;

            // Fecha todos os sockets dos clientes
            for (auto& U : LU) U.close();

            // Fecha o socket de conexoes
            sock_server.close();

            // Os tanques continuam funcionando

        } // fim catch - Erros mais graves que encerram o servidor
    } // fim while (server_on)
}



