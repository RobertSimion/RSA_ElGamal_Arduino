#include <SPI.h>
#include <mcp_can.h>
#include <mcp_can_dfs.h>

#include <stdlib.h>
#include <math.h>
#include "BigNumber.h"

#define SPICSPIN  10
#define SCKPIN 13
#define SIPIN 11
#define SOPIN 12
#define ISRPIN 9

// GND_CAN - POWER GND MCU
// VCC_CAN - POWER 5V MCU

MCP_CAN CAN(SPICSPIN);

void setup(){
  
    Serial.begin(115200);
   
    while (CAN_OK != CAN.begin(CAN_500KBPS,MCP_8MHz)){
        
        Serial.println("CAN BUS Init Failed");
        delay(100);
        
    }
    
    Serial.println("CAN BUS  Init OK!");
    BigNumber::begin ();

}

enum EL_GAMAL_STATE{
  STATE_UNINIT,
  STATE_INIT_PARAM,
  STATE_SEND_PK,
  STATE_RX,
  STATE_DECRYPT,
  STATE_STOP
};

EL_GAMAL_STATE g_state = STATE_UNINIT;

unsigned char g_len_ack0;
unsigned char *g_ack0;
unsigned int g_a, g_q, g_x, g_y;
unsigned char g_cryptogram_len;
unsigned char g_pk_B[3];

struct crypto_packet{
  unsigned char C1;
  unsigned char C2[8];
};

crypto_packet cryptogram;

unsigned char* decrypt(unsigned char* C2, unsigned int K, unsigned int q){
  
  unsigned char *plaintext = (unsigned char*)malloc(8 * sizeof(unsigned char));
  memset(plaintext, 0, 8 * sizeof(unsigned char));
  
  unsigned int inv_K;
  
  for(unsigned int i = 0; i < q; i++){
     if((i * K) % q == 1){
        inv_K = i;
        break;
     }
  }

  for(int i = 0; i < 8; i++){
    *(plaintext + i) = ((unsigned int)C2[i] * inv_K) % q;
  }
  return plaintext;
}

unsigned char decrypt_session_key(unsigned int x, unsigned int q, unsigned int C1){

  unsigned char K; 
  BigNumber base = BigNumber(C1);
  BigNumber ex = BigNumber(x);

  K = base.powMod(ex,q);
  
  return K;
}
unsigned int generate_pk(unsigned int a, unsigned int x, unsigned int  q){

  unsigned int y;
  BigNumber base = BigNumber(a);
  BigNumber ex = BigNumber(x);
  

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

void loop(){ 
  
    static unsigned char esc = 0;
    
    if(esc)exit(0);
    
    switch(g_state){
      
      case STATE_UNINIT:{
        Serial.println("START state");
        g_state = STATE_INIT_PARAM;
                
        break;
      }
    
      case STATE_INIT_PARAM:{
      
        Serial.println("INIT PARAM state");
        g_q = 251;
        Serial.println("q param: ");
        Serial.println(g_q);

        g_a = generate_a(g_q);

        Serial.println("Generator a: ");
        Serial.println(g_a);

        g_x = rand() % (g_q - 3) + 2; //  2 <= x < q-1
        //rand() % (max_number + 1 - minimum_number) + minimum_number
        
        Serial.println("Private key x: ");
        Serial.println(g_x);

        g_y = generate_pk(g_a, g_x, g_q);

        Serial.println("Public y: ");
        Serial.println(g_y);
        
        g_state = STATE_SEND_PK;
        
        break;
      }
    
      case STATE_SEND_PK:{
        
        Serial.println("SEND PK state");
        
        g_pk_B[0] = g_q & 0xFF;
        g_pk_B[1] = g_a & 0xFF;
        g_pk_B[2] = g_y & 0xFF;
      
        CAN.sendMsgBuf(0x00, 0, sizeof(g_pk_B), g_pk_B);
        
        if (CAN_MSGAVAIL == CAN.checkReceive()){
           CAN.readMsgBuf(&g_len_ack0, g_ack0);
           
           g_state = STATE_RX;  
        }
        else g_state = STATE_SEND_PK;
        
        break;
      }
    
      case STATE_RX:{
        Serial.println("STATE RX:");
        unsigned char aux_cryptogram[2];
        if (CAN_MSGAVAIL == CAN.checkReceive()) {         // check if data coming
          CAN.readMsgBuf(&g_cryptogram_len, aux_cryptogram);    
          // read data,  len: data length, buf: data buf
          cryptogram.C1 = aux_cryptogram[1];
          unsigned long canId = CAN.getCanId();

          Serial.println("-----------------------------");
          Serial.println("get data from ID: 0x");
          Serial.println(canId, HEX);
          Serial.println("****************************");

          Serial.println(cryptogram.C1);
          CAN.readMsgBuf(&g_cryptogram_len, cryptogram.C2);    // read data,  len: data length, buf: data buf

          canId = CAN.getCanId();

          Serial.println("-----------------------------");
          Serial.println("get data from ID: 0x");
          Serial.println(canId, HEX);
          

          for (int i = 0; i < g_cryptogram_len; i++) { // print the data
            Serial.println("****************************");
            Serial.println(cryptogram.C2[i]);
            
          }
          
          g_state = STATE_DECRYPT;
        }
        else g_state = STATE_RX;
        
        break;
      }
      
      case STATE_DECRYPT:{
        
        Serial.println("STATE DECRYPT");
        unsigned char K = decrypt_session_key(g_x, g_q, cryptogram.C1);
        Serial.println("Session key K:");
        Serial.println(K);
        unsigned char *plaintext;
        plaintext = decrypt(cryptogram.C2, K, g_q);
        Serial.println("Plain text:");
        for(int i = 0; i < 8; i++){
          Serial.print((char)*(plaintext + i));
        }
        Serial.println();

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
