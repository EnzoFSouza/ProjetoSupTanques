#include <iostream>     /* cerr */
#include <algorithm>
#include <cstdint>      /* uint16_t */
#include "supservidor.h"

using namespace std;

/* ========================================
   CLASSE SUPSERVIDOR
   ======================================== */

/// Construtor
SupServidor::SupServidor()
  : Tanks()
  , server_on(false)
  , LU()
  /*ACRESCENTAR*/
  ,thr_server()
  ,sock_server()
{
  // Inicializa a biblioteca de sockets
  /*ACRESCENTAR*/
  mysocket_status iResult = mysocket::init();
  // Em caso de erro, mensagem e encerra
  if (/*MODIFICAR*/iResult != mysocket_status::SOCK_OK)
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
  /*ACRESCENTAR*/
  sock_server.close();


  // Espera o fim da thread do servidor
  /*ACRESCENTAR*/
  if (thr_server.joinable()) thr_server.join();
  thr_server = thread();

  // Encerra a biblioteca de sockets
  /*ACRESCENTAR*/
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
    /*ACRESCENTAR*/
    mysocket_status iResult = sock_server.listen(SUP_PORT);

    // Em caso de erro, gera excecao
    if (/*MODIFICAR*/iResult != mysocket_status::SOCK_OK) throw 1;

    // Lanca a thread do servidor que comunica com os clientes
    /*ACRESCENTAR*/
    thr_server = thread( [this]()
    {
      this->thr_server_main();
    } );

    // Em caso de erro, gera excecao
    if (/*MODIFICAR*/!thr_server.joinable()) throw 2;
  }
  catch(int i)
  {
    cerr << "Erro " << i << " ao iniciar o servidor\n";

    // Deve parar a thread do servidor
    server_on = false;

    // Fecha o socket do servidor
    /*ACRESCENTAR*/
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
  /*ACRESCENTAR*/
  sock_server.close();

  // Espera pelo fim da thread do servidor
  /*ACRESCENTAR*/
  if (thr_server.joinable()) thr_server.join();

  // Faz o identificador da thread apontar para thread vazia
  /*ACRESCENTAR*/
  thr_server = thread();

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

  while (server_on)
  {
    // Erros mais graves que encerram o servidor
    // Parametro do throw e do catch eh uma const char* = "texto"
    try
    {
    ///Divergencia sock.close()
      // Encerra se o socket de conexoes estiver fechado
      if (!sock_server.accepting())
      {
        throw "socket de conexoes fechado";
      }

      // Inclui na fila de sockets todos os sockets que eu
      // quero monitorar para ver se houve chegada de dados

      // Limpa a fila de sockets
      f.clear();

      // Inclui na fila o socket de conexoes
      f.include(sock_server);

      // Inclui na fila todos os sockets dos clientes conectados
      for (auto& U : LU)
      {
        if (U.isConnected()) f.include(U.sock);
      }

      // Espera ateh que chegue dado em algum socket (com timeout)
      iResult = f.wait_read(SUP_TIMEOUT*1000);
      if (iResult != mysocket_status::SOCK_OK) throw "fila de espera"; //Do mensageiro1cpp

      // De acordo com o resultado da espera:
      switch (iResult):
          case mysocket_status::SOCK_TIMEOUT:
          // SOCK_TIMEOUT:
          // Saiu por timeout: nao houve atividade em nenhum socket
          // Aproveita para salvar dados ou entao nao faz nada
          case mysocket_status::SOCK_ERROR:
          // SOCK_ERROR:
          // Erro no select: encerra o servidor
          case mysocket_status::SOCK_OK:
          // SOCK_OK:
          // Houve atividade em algum socket da fila:
          // Testa se houve atividade nos sockets dos clientes. Se sim:
              try // Erros nos clientes: catch fecha a conexao com esse cliente
              {

              }
              //   - Leh o comando
              //   - Executa a acao
              //   = Envia resposta

              //   Depois, testa se houve atividade no socket de conexao. Se sim:
              if (server_on && sock_server.connected() && f.had_activity(sock_server)){
                //   - Estabelece nova conexao em socket temporario
                iResult = sock_server.accept(t);
                if (iResult != mysocket_status::SOCK_OK) throw 3; // Erro grave: encerra o servidor

                try //Erros na conexao com cliente, testa socket temporario ou desconecta novo cliente
                {
                    //  - Leh comando, login e senha
                    // Leh o comando do usuario que deseja se conectar
                    iResult = t.read_uint16(cmd, SUP_TIMEOUT*1000);
                    if (iResult != mysocket_status::SOCK_OK) throw 1;
                    // Leh o login do usuario que deseja se conectar
                    iResult = t.read_string(login, SUP_TIMEOUT*1000);
                    if (iResult != mysocket_status::SOCK_OK) throw 2;
                    // Leh a senha do usuario que deseja se conectar
                    iResult = t.read_string(password, SUP_TIMEOUT*1000);
                    if (iResult != mysocket_status::SOCK_OK) throw 3;
                    //   - Testa usuario
                    // Testa o comando
                    if (cmd!=CMD_LOGIN_USER && cmd!=CMD_NEW_USER) throw 4;
                    // Testa o login e senha
                    if (login.size()<SUP_LOGIN_SIZE_MIN || login.size()>SUP_LOGIN_SIZE_MAX ||
                    password.size()<SUP_PASSW_SIZE_MIN || password.size()>SUP_PASSW_SIZE_MAX) throw 5;

                    //   - Se deu tudo certo, faz o socket temporario ser o novo socket
                    //     do cliente e envia confirmacao
                    // Verifica se jah existe um usuario cadastrado com esse login
                    iU = find(LU.begin(), LU.end(), login);
                    // Testa se o usuario eh adequado, o que depende se estah se logando como
                    // novo cliente ou como cliente jah cadastrado
                    if (cmd == CMD_NEW_USER){
                        if (iU!=LU.end()) throw 6; // Erro se jah existir
                        // Insere novo usuario
                        User novo;
                        novo.login = login;
                        novo.password = password;
                        novo.sock.swap(t);
                        LU.push_back(move(novo));
                        iU = --LU.end(); // iU aponto para o ultimo elemento de LU == novo
                    }
                    else{ //implica cmd = CMD_LOGIN
                        if (iU==LU.end()) throw 8; // Erro se nao existir
                        // Testa se a senha confere
                        if (iU->password != password) throw 9; // Senha nao confere
                        // Testa se o cliente jah estah conectado
                        if (iU->isConnected()) throw 10; // User jah conectado
                        // Associa o socket que se conectou a um usuario cadastrado
                        iU->sock.swap(t);
                    }
                    // Envia a confirmacao de conexao para o novo cliente
                    iResult = iU->sock.write_int16(CMD_LOGIN_OK);
                    if (iResult != mysocket_status::SOCK_OK) throw 11;
                    iResult = iU->sock.write_int16(iU->last_id);
                    if (iResult != mysocket_status::SOCK_OK) throw 12;
                }
                catch (int erro){ //Erros na conexao de novo cliente
                    if (erro>=5 && erro<=10){
                        // Comunicacao com socket temporario OK, login invalido
                        // Envia comando informando login invalido
                        t.write_int16(CMD_LOGIN_INVALIDO);
                        // Fecha o socket temporario
                        t.close();
                    }
                    else{
                      // Erro na comunicacao
                      // Fecha o socket
                      if (erro>=11) iU->close(); // Erro na comunicacao com socket do novo cliente
                      else t.close(); // erro 1-4 // Erro na comunicacao com socket temporario

                      // Informa erro nao previsto
                      cerr << "Erro " << erro << " na conexao de novo cliente" << endl;
                    }
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



