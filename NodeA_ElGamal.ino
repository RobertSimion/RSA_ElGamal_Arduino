#include <mcp_can_dfs.h>
#include <SPI.h>
#include <mcp_can.h>

#include <math.h>
#include "BigNumber.h"
#include <stdlib.h>

#define  SPICSPIN  10
#define  SCKPIN  13
#define  SIPIN  11
#define  SOPIN  12
#define  ISRPIN 9

// GND_CAN - POWER GND MCU
// VCC_CAN - POWER 5V MCU

MCP_CAN CAN(SPICSPIN);

void setup(){
  
    Serial.begin(115200);
    //Serial.begin(9600);
    while (CAN_OK != CAN.begin(CAN_500KBPS,MCP_8MHz)){
        Serial.println("CAN BUS init Failed");
        delay(100);
    }
    Serial.println("CAN BUS Shield Init OK!");
    BigNumber::begin();
}

enum EL_GAMAL_STATE{
  STATE_UNINIT,
  STATE_RECV_PK,
  STATE_INIT_PARAM,
  STATE_ENCRYPT,
  STATE_TX,
  STATE_STOP
};

EL_GAMAL_STATE g_state = STATE_UNINIT;

struct crypto_packet{
  unsigned char C1;
  unsigned char C2[8];
};

crypto_packet cryptogram;


unsigned int generate_pk(unsigned int a, unsigned int x, unsigned int  q){

  BigNumber base = BigNumber(a);
  BigNumber ex = BigNumber(x);
  unsigned int y;

  y = base.powMod(ex, q);
  
  return y;
}
unsigned int generate_a(int q){
  
  unsigned int aux, next, ex;
  int j = 0, index;

  unsigned int *a = (unsigned int *)malloc((q - 1) * sizeof(unsigned int));
  memset(a, 0, (q - 1) * sizeof(unsigned int));

  for(int i = 1; i < q; i++){
    aux = i;
    ex = 1;
    next = aux % q;
    while(next != 1 ){
      next = ((next * aux) % q);
      ex++;
    }
    
    if(ex == q - 1){
      *(a + j) = aux;
      j++;
    }
    
  }
  index = rand() % j;
  
  return *(a + index);
}


unsigned char g_msg[8] = {'e','l','g','a','m','a','l','.'};
void encrypt(unsigned int a, unsigned int k, unsigned int K, unsigned int q){ 
//C1 = a^k mod q C2 = KM mod q
  BigNumber base = BigNumber(a);
  BigNumber ex = BigNumber(k);
  cryptogram.C1 = base.powMod(ex,q);
  Serial.println("C1");
  Serial.println(cryptogram.C1);

  for(int i = 0; i < 8; i++){
    unsigned int aux = g_msg[i];
    aux *= K;
    aux %= q;
    cryptogram.C2[i] = aux;
  }
  Serial.println("C2");
  for(int i = 0; i < 8; i++){
    Serial.println(cryptogram.C2[i]);
  }
  
}
unsigned int generate_session_key(unsigned int y,unsigned int k, unsigned int q){
  unsigned int K;
  BigNumber base = BigNumber(y);
  BigNumber ex = BigNumber(k);

  K = base.powMod(ex, q);

  return K;
}


unsigned int g_a, g_q, g_x, g_y, g_y_B,k, K;
unsigned char g_pk_B_size;
unsigned char g_pk_B[3];
unsigned char *g_ack0;

void loop(){
  
  static unsigned char esc = 0;
  
  if(esc == 1)exit(0);
  
  switch(g_state){
    
    case STATE_UNINIT:{
      
      Serial.println("START state");
      g_state = STATE_RECV_PK;
      
      break;
    }
    

    case STATE_RECV_PK:{
      
      Serial.println("RECV PK STATE ");
      
      if (CAN_MSGAVAIL == CAN.checkReceive()) {         // check if data coming
        CAN.readMsgBuf(&g_pk_B_size, g_pk_B);    // read data,  len: data length, buf: data buf

        unsigned long canId = CAN.getCanId();

        Serial.println("-----------------------------");
        Serial.println("get data from ID: 0x");
        Serial.println(canId, HEX);

        for (int i = 0; i < g_pk_B_size; i++) { // print the data
            Serial.println("****************************");
            Serial.println(g_pk_B[i]);
        }
        
        g_ack0[0] = '0';
        CAN.sendMsgBuf(0x01, 0, 1, g_ack0);
        
        g_state = STATE_INIT_PARAM;
      }
      else g_state = STATE_RECV_PK;
    
      break;
    }

    case STATE_INIT_PARAM:{
      
        Serial.println("Init params state: ");
       
        g_q = g_pk_B[0];
        g_a = g_pk_B[1];
        g_y_B = g_pk_B[2];
        
        Serial.println("Prime q:");
        Serial.println(g_q);
        Serial.println("Generator a:");
        Serial.println(g_a);
        Serial.println("B public y:");
        Serial.println(g_y_B);
        srand(2);
        g_x = rand()% (g_q - 3) + 2;// 2<=x<q-1
        Serial.println("Private key x: ");
        Serial.println(g_x);
        g_y = generate_pk(g_a, g_x, g_q);
        Serial.println("Public key y: ");
        Serial.println(g_y);
        g_state = STATE_ENCRYPT;
     
      break;
    }
    
    
    case STATE_ENCRYPT:{
      
      Serial.println("STATE ENCRYPT");

      k = rand() % (g_q - 1) + 1;
      Serial.println("Random k: ");
      Serial.println(k);
      K = generate_session_key(g_y_B, k, g_q);
      Serial.println("Session key");
      Serial.println(K);
      encrypt(g_a, k, K, g_q);
      g_state = STATE_TX;
     
      break;
    }
    
    case STATE_TX:{

      Serial.println("STATE TX");
      unsigned char aux_cryptogram[2];
      
      aux_cryptogram[0] = 0x00;
      aux_cryptogram[1] = cryptogram.C1;
      
      CAN.sendMsgBuf(0x02, 0, 2, aux_cryptogram);
      CAN.sendMsgBuf(0x03, 0, 8, cryptogram.C2);

      g_state = STATE_STOP;
       
      break;
    }

    case STATE_STOP:{
      
      Serial.println("STATE STOP");
      
      BigNumber::finish();
      esc++;
      
      g_state = STATE_STOP;
      
      break;
    }

    default:{
      g_state = STATE_STOP;
      Serial.println("STATE error");
      
    }
    
  }

  delay(1000);
  
}
