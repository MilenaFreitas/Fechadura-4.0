
/*
FECHADURA SALA TECNICA COM SENHA INDIVIDUAL
      MILENA FREITAS 2021
      versão final 
      Com reconexão do wifi
*/
#include <Arduino.h>
#include <Keypad.h>
#include <WiFiClient.h>
#include <NTPClient.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "C:\Users\Estudio\Desktop\dados fechadura.cpp"
const int tamanho_array=5;
const int STARTING_EEPROM_ADDRESS = 25; //primeiro endereço 
String novasSenhas[tamanho_array];
const int fechadura=14; 
const int botaoAbre=27; 
const int buzzer=13;
const int tranca=12;
int estado=0;
const byte linha = 4;
const byte coluna = 4;
String digitada;
String usuarios;
byte pinolinha[linha] = {33, 32, 5, 18};     //Declara os pinos de interpretação das linha
byte pinocoluna[coluna] = {23, 19, 22, 21};    //Declara os pinos de interpretação das coluna
const char keys [linha] [coluna]={
{'1','2','3','A'},
{'4','5','6','B'},
{'7','8','9','C'},
{'*','0','#','D'} 
};
Keypad keypad = Keypad(makeKeymap(keys), pinolinha, pinocoluna, linha, coluna);
#define EEPROM_SIZE 1024

uint64_t chipid = ESP.getEfuseMac(); // The chip ID is essentially its MAC address(length: 6 bytes).
uint16_t chip = (uint16_t)(chipid >> 32);
char DEVICE_ID[23];
char an = snprintf(DEVICE_ID, 23, "biit%04X%08X", chip, (uint32_t)chipid); // PEGA IP
WiFiClient espClient;
PubSubClient client(espClient);
WebServer server(80);
WiFiUDP udp;
IPAddress ip;  
char topic []= "fechadura";  // topico MQTT publica usuario
char topic1[]= "abrirPorta"; // topico MQTT comando virtual da porta
char topic2[]= "trancaPorta";// topico MQTT publica status da porta(open/close) 
NTPClient ntp(udp, "a.st1.ntp.br", -3 * 3600, 60000); //Hr do Br
struct tm data; //armazena data 
char data_formatada[64];
char timeStamp; 
bool acerteiSenha=false;
String comando;
String IP;
String mac;
unsigned long previousMillis = 0;
const long intervalo = 60*1000*1; //1 min
int leitura;
const char* host = "esp3";
hw_timer_t *timer = NULL;
int contar=0;
int rede;
static uint8_t taskCoreZero=0; 
////////////////////////////////////////////////////////////////
void callback(char* topicc, byte* payload, unsigned int length){
  //retorna infoMQTT
  String topicStr=topicc;  
  if(topicStr=="abrirPorta"){ //pega comando via MQTT
    for(int i=0; i<length;i++){
      comando=(char)payload[i]; //comando abre porta via mqtt
    }
  }
  topicStr="";
}
void conectaMQTT () {
  if(!client.connected()){
    Serial.println("conectando...");
    if (client.connect("FECHADURA-TRANSMISSOR")){
      Serial.println("CONECTADO! :)");
      client.publish ("teste", "hello word");
      client.subscribe ("fechadura");   //se inscreve no topico a ser usado
      client.subscribe ("abrirPorta");
      client.subscribe ("trancaPorta");   
    } else {
      Serial.println("Falha na conexao");
      Serial.println("Aguarde Reconexão");
    }
  }
}
void reconectaMQTT(){
  //reconecta MQTT caso desconecte
  if (!client.connected()) {
    rede=0;
    conectaMQTT();
  }
  client.loop(); //mantem conexao com o MQTT
}
void iniciaWifi(){
  int cont=0;
  WiFi.begin(WIFI_NOME, WIFI_SENHA);
  while (WiFi.status()!= WL_CONNECTED){//incia wifi ate q funcione
    Serial.println("wifiBegin...");
    delay(1000);
    cont++;
    if(cont==15){  //se n funcionar sai do loop e fica sem rede
      rede=0; //status da rede
      break;
    } else if(WiFi.status() == WL_CONNECTED){
      rede=1;
      if(!client.connected()){
        conectaMQTT();
      }
    }
  }      
}
void tentaReconexao(){ 
    iniciaWifi();
    ntp.forceUpdate();
}
void hora(){
  time_t tt=time(NULL);
  data= *gmtime(&tt);
  strftime(data_formatada, 64, "%a - %d/%m/%y %H:%M:%S", &data);
  int ano_esp = data.tm_year;
  if (ano_esp<2020){
    ntp.forceUpdate();
  }
}
void escreveEEPROM(int endereco, int senhas[], unsigned int length) {
  int adressIndex=endereco; //endereçamento
  for (int i=0; i<6; i++){
    EEPROM.write(adressIndex, senhas[i]>>8); //escreve ate 8bits
    EEPROM.write(adressIndex+1, senhas[i] & 0xFF);// escreve o byte menos significativo
    adressIndex+=2;
    Serial.println("escreveu na EEPROM");
  }
}
void leEEPROM (int endereco, String senhas[], unsigned int length){
  int adressIndex=endereco;
  for (int i=0; i<6; i++){
    senhas[i] = (EEPROM.read(adressIndex)<<8) + EEPROM.read(adressIndex+1); //soma a parte mais significativa c a menos sig
    adressIndex+=2; //soma 2 endereços pq é >8bits ent oculpa 2 end
  }
}
void pin(){
  pinMode(fechadura, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(botaoAbre, INPUT_PULLUP);
  pinMode(tranca, INPUT_PULLUP);
}
void estadoSenha (int estado){
// 0=espera 1=aceito 2=negado 
  if (estado==0){ //standy by da porta, esperando ação
    digitalWrite(buzzer, HIGH);
    digitalWrite(fechadura, HIGH); //trancada
  } else if (estado==1){ //abre
    digitalWrite(buzzer, LOW);
    digitalWrite(fechadura, LOW);
    delay(2000);
    digitalWrite(buzzer, HIGH);
    digitalWrite(fechadura, HIGH);
  } else if (estado==2){ //não abre
    digitalWrite(fechadura, HIGH); //TRANCADA
    Serial.print("falha na tentativa");
    digitalWrite(buzzer, LOW);
    delay(500);
    digitalWrite(buzzer, HIGH);
    delay(500);
    digitalWrite(buzzer, LOW);
    delay(500);
    digitalWrite(buzzer, HIGH); 
  }
}
bool verificaSenha (String sa, String sd){  
  //funçao chamada para comparar as senhas 
  bool resultado=false;
  if(sa.compareTo(sd)==0){//se a comparação for 0 é porque são iguais
    resultado=true; //se senha correta função verificaSenha fica verdadeira
  } else {
    resultado=false;
  }
  return resultado;
}
void abreComando(){
   if (comando=="1"){ //comando recebido via mqtt
    estado=1; //senha certa
    estadoSenha(estado);
    acerteiSenha = true;
    StaticJsonDocument<256> doc;
    doc["local"] = "Porta-Transmissor";
    doc["ip"] = ip.toString();
    doc["mac"] = mac;
    doc["user"] = comando;
    char buffer[256];
    serializeJson(doc, buffer);
    client.publish(topic, buffer);
    estado=0;
    estadoSenha(estado);
    comando="";
  }
}
void abreBotao(void * pvParameters){
  while(1){
    if(digitalRead(botaoAbre)==HIGH){ //se apertar o botao abre a porta 
      digitalWrite(fechadura, LOW);
      delay(2000);
      digitalWrite(fechadura, HIGH);
      estado=0; //retorna para standy by
      estadoSenha(estado);
    }
    vTaskDelay(500);
  }
}
void loop2(void * pvParameters){             
  while(1){
    if(WL_DISCONNECTED || WL_CONNECTION_LOST){
    rede=0;
    }else if(rede==0){
      Serial.println(rede);
      tentaReconexao();
    }
    abreComando();
    reconectaMQTT();

    vTaskDelay(500);
  }
}
void statusPorta(){
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= intervalo) { //a cada 10s envia  o status da porta
    previousMillis = currentMillis;
    leitura=digitalRead(tranca);
    if(leitura==1){
      String payload1=String("FECHADO");
      client.publish (topic2, (char*) payload1.c_str());
    } else {
      String payload1=String("ABERTO");
      client.publish (topic2, (char*) payload1.c_str());
    }
  Serial.println("ENTROU NO LOOP DE ENVIAR CODIGO");
  }
}
void setup(){
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  escreveEEPROM(STARTING_EEPROM_ADDRESS, senhas, tamanho_array);
  leEEPROM(STARTING_EEPROM_ADDRESS, novasSenhas, tamanho_array);
  iniciaWifi();
  ntp.begin();
  ntp.forceUpdate();
  if (!ntp.forceUpdate()){
    Serial.print("Erro ao carregar a hora!");
  } else {
    timeval tv;
    tv.tv_sec=ntp.getEpochTime();
    settimeofday(&tv, NULL);
  }
  client.setServer (BROKER_MQTT, 1883);//define mqtt
  client.setCallback(callback); 
  pin();
  Serial.print("digite a senha");
  estado=0;//inicializa com porta travada
  estadoSenha(estado);
  ip=WiFi.localIP(); //pega ip
  mac=DEVICE_ID;     //pega mac
  xTaskCreatePinnedToCore( //taskmonitora
    loop2,  //funçao que implementa a tarefa   
    "loopGeral",    //nome da tarefa
    2000,         //numero de palavras a serem alocadas para uso com a pilha
    NULL,          //parametro de entrada da a task
    1,             //prioridade é menor 
    NULL,          //task_handle de referencia
    taskCoreZero   //nucleo onde vai ser executado a tarefa
  );
  xTaskCreatePinnedToCore( //taskbotao
    abreBotao,  //funçao que implementa a tarefa   
    "abrePorta", //nome da tarefa
    2000,         //numero de palavras a serem alocadas para uso com a pilha
    NULL,          //parametro de entrada da a task
    2,             //prioridade da tarefa 2 é mais alta, faz primeiro
    NULL,          //task_handle de referencia
    taskCoreZero   //nucleo onde vai ser executado a tarefa
  );
}
void loop(){
  server.handleClient();
  statusPorta();
  char key = keypad.getKey(); //le as teclas
  if (key!=NO_KEY){ //se digitar...
    digitalWrite (buzzer, LOW);
    delay(50);
    digitalWrite(buzzer, HIGH); 
    if (key=='A'){ //campainha
    digitalWrite (buzzer, LOW);
    delay(1500);
    digitalWrite(buzzer, HIGH);
    digitada="";
    } else if (key=='C'){
    //limpa senha
    digitada="";
    } else if(key=='#'){ //depois do enter vai verificar a senha 
      for (int i = 0; i < 6; i++) { 
      if(verificaSenha(novasSenhas[i], digitada)){
        String usuarioo = usuario[i];
        estado=1; //senha certa
        estadoSenha(estado);
        acerteiSenha = true;
        StaticJsonDocument<256> doc;
        doc["local"] = "Porta-Transmissor";
        doc["ip"] = ip.toString();
        doc["mac"] = mac;
        doc["user"] = usuarioo;
        char buffer[256];
        serializeJson(doc, buffer);
        client.publish(topic, buffer);
        Serial.println("Sending message to MQTT topic..");
        Serial.println(buffer);
        estado=0;
        estadoSenha(estado);
        delay(2000); 
        break;
      }
    } 
    if (acerteiSenha==false){
      estado=2;
      estadoSenha(estado);
      delay(2000);
      estado=0;
      estadoSenha(estado);
      StaticJsonDocument<256> doc;
      doc["local"] = "Porta-Transmissor";
      doc["status"] = "Falha na tentativa!!!";
      char buffer[256];
      serializeJson(doc, buffer);
      client.publish(topic, buffer);
    }
    digitada=""; //limpa o que foi digitado
    acerteiSenha=false;
  } else {
    digitada+=key;  //concatena as info que são digitadas
    Serial.println(digitada);
  }
    estadoSenha(estado);
  } 
} 